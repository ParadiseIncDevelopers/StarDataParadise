#include "migration_source_driver.hpp"
#include "migration_source_protocol.hpp"

#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QMap>
#include <QStringDecoder>
#include <QTimeZone>

#include <windows.h>

#include <algorithm>
#include <memory>
#include <optional>

namespace wb::migration::sourceDriver
{
namespace
{
using namespace sourceProtocol;
using Field = std::optional<QString>;
using Row = QList<Field>;
using Handler = std::function<void(const Row&)>;

constexpr qsizetype maxFieldBytes = 65536;
constexpr qsizetype maxMetadataBytes = 2 * 1024 * 1024;

struct ReadError { QString message; };

[[noreturn]] void fail(const char* message)
{
    throw ReadError{sourceProtocol::tr(message)};
}

Field utf8(const char* data, qsizetype bytes)
{
    if (bytes < 0 || bytes > maxFieldBytes) fail("A source field exceeded the transfer limit.");
    QStringDecoder decoder(QStringDecoder::Utf8,
        QStringConverter::Flag::Stateless | QStringConverter::Flag::ConvertInitialBom);
    QString value = decoder(QByteArrayView(data, bytes));
    if (decoder.hasError()) fail("A source field is not valid UTF-8.");
    return value;
}

class DriverLibrary
{
public:
    explicit DriverLibrary(const ExternalSource& source)
    {
        const QFileInfo info(source.driverFile);
        const auto file = QDir::toNativeSeparators(info.canonicalFilePath());
        if (source.provider == ExternalProvider::mysql && info.dir().dirName() == u"lib")
        {
            const auto bin = QDir::toNativeSeparators(QFileInfo(info.dir().absoluteFilePath("../bin")).canonicalFilePath());
            if (!bin.isEmpty()) directory_ = AddDllDirectory(reinterpret_cast<LPCWSTR>(bin.utf16()));
        }
        module_ = LoadLibraryExW(reinterpret_cast<LPCWSTR>(file.utf16()), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_USER_DIRS | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module_)
        {
            if (directory_) RemoveDllDirectory(directory_);
            fail("The installed source driver or one of its dependencies could not be loaded.");
        }
    }

    ~DriverLibrary()
    {
        FreeLibrary(module_);
        if (directory_) RemoveDllDirectory(directory_);
    }

    DriverLibrary(const DriverLibrary&) = delete;
    DriverLibrary& operator=(const DriverLibrary&) = delete;

    template<typename Function>
    Function symbol(const char* name) const
    {
        const auto result = GetProcAddress(module_, name);
        if (!result) fail("The installed source driver does not expose the required client API.");
        return reinterpret_cast<Function>(result);
    }

private:
    HMODULE module_{};
    DLL_DIRECTORY_COOKIE directory_{};
};

class Session
{
public:
    virtual ~Session() = default;
    virtual void command(const QString& sql) = 0;
    virtual void scan(const QString& sql, int columns, const Handler& visit) = 0;
};

class Postgres final : public Session
{
public:
    Postgres(const ExternalSource& source, QByteArray& password)
        : library_(source), connection_(nullptr, library_.symbol<void(*)(void*)>("PQfinish"))
    {
        const auto connect = library_.symbol<void*(*)(const char* const*, const char* const*, int)>("PQconnectdbParams");
        const auto version = library_.symbol<int(*)()>("PQlibVersion");
        const auto status = library_.symbol<int(*)(const void*)>("PQstatus");
        const auto sslInUse = library_.symbol<int(*)(const void*)>("PQsslInUse");
        if (version() < 140000) fail("PostgreSQL client version 14 or newer is required.");
        const auto host = source.host.toUtf8(), port = QByteArray::number(source.port), db = source.database.toUtf8();
        const auto user = source.username.toUtf8(), ca = QDir::toNativeSeparators(source.caFile).toUtf8();
        const char* keys[]{"host", "port", "dbname", "user", "password", "sslmode", "sslrootcert",
            "gssencmode", "client_encoding", "connect_timeout", nullptr};
        const char* values[]{host.constData(), port.constData(), db.constData(), user.constData(),
            password.constData(), source.allowInsecureLoopback ? "disable" : "verify-full",
            source.allowInsecureLoopback ? nullptr : ca.constData(), "disable", "UTF8", "5", nullptr};
        connection_.reset(connect(keys, values, 0));
        password.fill('\0');
        if (!connection_ || status(connection_.get()) != 0 ||
            (!source.allowInsecureLoopback && !sslInUse(connection_.get())))
            fail("The PostgreSQL source connection or TLS identity verification failed.");
        command("SET search_path TO pg_catalog");
        command("SET TIME ZONE 'UTC'");
        command("SET DateStyle TO 'ISO, YMD'");
        command("SET statement_timeout TO '10s'");
        command("BEGIN ISOLATION LEVEL REPEATABLE READ READ ONLY");
    }

    void command(const QString& sql) override
    {
        auto execute = library_.symbol<void*(*)(void*, const char*)>("PQexec");
        std::unique_ptr<void, void(*)(void*)> result(execute(connection_.get(), sql.toUtf8().constData()), clear_);
        if (!result || status_(result.get()) != 1) fail("PostgreSQL could not begin or close the read-only snapshot.");
    }

    void scan(const QString& sql, int columns, const Handler& visit) override
    {
        if (!send_(connection_.get(), sql.toUtf8().constData()) || !single_(connection_.get()))
            fail("PostgreSQL rejected a source metadata or data read.");
        bool done{};
        while (auto* raw = result_(connection_.get()))
        {
            const std::unique_ptr<void, void(*)(void*)> item(raw, clear_);
            const auto state = status_(raw);
            if (done || (state != 9 && state != 2) || fields_(raw) != columns)
                fail("PostgreSQL could not read from the source snapshot.");
            if (state == 2)
            {
                done = true;
                continue;
            }
            Row row;
            row.reserve(columns);
            for (int column = 0; column < columns; ++column)
            {
                if (null_(raw, 0, column)) row.push_back(std::nullopt);
                else row.push_back(utf8(value_(raw, 0, column), length_(raw, 0, column)));
            }
            visit(row);
        }
        if (!done) fail("The PostgreSQL source stream ended early.");
    }

private:
    DriverLibrary library_;
    std::unique_ptr<void, void(*)(void*)> connection_;
    void (*clear_)(void*) = library_.symbol<void(*)(void*)>("PQclear");
    int (*status_)(const void*) = library_.symbol<int(*)(const void*)>("PQresultStatus");
    int (*send_)(void*, const char*) = library_.symbol<int(*)(void*, const char*)>("PQsendQuery");
    int (*single_)(void*) = library_.symbol<int(*)(void*)>("PQsetSingleRowMode");
    void* (*result_)(void*) = library_.symbol<void*(*)(void*)>("PQgetResult");
    int (*fields_)(const void*) = library_.symbol<int(*)(const void*)>("PQnfields");
    int (*null_)(const void*, int, int) = library_.symbol<int(*)(const void*, int, int)>("PQgetisnull");
    int (*length_)(const void*, int, int) = library_.symbol<int(*)(const void*, int, int)>("PQgetlength");
    char* (*value_)(const void*, int, int) = library_.symbol<char*(*)(const void*, int, int)>("PQgetvalue");
};

class Mysql final : public Session
{
    using Close = void(WINAPI*)(void*);
    // Oracle MySQL 8.0 mysql_option numbers from the public C client ABI.
    enum Option { connectTimeout = 0, characterSet = 7, localInfile = 8, protocol = 9, readTimeout = 11,
        writeTimeout = 12, sslCa = 21, sslMode = 35 };

public:
    Mysql(const ExternalSource& source, QByteArray& password)
        : library_(source), connection_(nullptr, library_.symbol<Close>("mysql_close"))
    {
        const auto version = library_.symbol<unsigned long(WINAPI*)()>("mysql_get_client_version")();
        if (version < 80000 || version >= 80100) fail("Oracle MySQL 8.0 client is required.");
        connection_.reset(library_.symbol<void*(WINAPI*)(void*)>("mysql_init")(nullptr));
        if (!connection_) fail("The MySQL source driver could not be initialized.");
        const auto option = library_.symbol<int(WINAPI*)(void*, int, const void*)>("mysql_options");
        const unsigned int connectSeconds = 5, readSeconds = 10, writeSeconds = 10, noInfile = 0, tcp = 1;
        const unsigned int tls = source.allowInsecureLoopback ? 1 : 5;
        const auto ca = QDir::toNativeSeparators(source.caFile).toUtf8();
        auto* raw = connection_.get();
        if (option(raw, connectTimeout, &connectSeconds) || option(raw, readTimeout, &readSeconds) ||
            option(raw, writeTimeout, &writeSeconds) || option(raw, localInfile, &noInfile) ||
            option(raw, protocol, &tcp) || option(raw, characterSet, "utf8mb4") ||
            (!source.allowInsecureLoopback && option(raw, sslCa, ca.constData())) || option(raw, sslMode, &tls))
            fail("The MySQL source driver could not apply secure connection options.");
        const auto connect = library_.symbol<void*(WINAPI*)(void*, const char*, const char*, const char*,
            const char*, unsigned int, const char*, unsigned long)>("mysql_real_connect");
        const auto ssl = library_.symbol<const char*(WINAPI*)(void*)>("mysql_get_ssl_cipher");
        const auto connected = connect(raw, source.host.toUtf8().constData(), source.username.toUtf8().constData(),
            password.constData(), source.database.toUtf8().constData(), source.port, nullptr, 0);
        password.fill('\0');
        if (!connected || (!source.allowInsecureLoopback && !ssl(raw)))
            fail("The MySQL source connection or TLS identity verification failed.");
        command("SET SESSION time_zone = '+00:00'");
        command("SET SESSION max_execution_time = 10000");
        command("SET TRANSACTION ISOLATION LEVEL REPEATABLE READ");
        command("START TRANSACTION WITH CONSISTENT SNAPSHOT, READ ONLY");
    }

    void command(const QString& sql) override
    {
        if (query_(connection_.get(), sql.toUtf8().constData()))
            fail("MySQL could not begin or close the read-only snapshot.");
    }

    void scan(const QString& sql, int columns, const Handler& visit) override
    {
        auto* connection = connection_.get();
        if (query_(connection, sql.toUtf8().constData())) fail("MySQL rejected a source metadata or data read.");
        const std::unique_ptr<void, Close> result(use_(connection), free_);
        if (!result || fields_(result.get()) != static_cast<unsigned>(columns))
            fail("MySQL could not read from the source snapshot.");
        while (const auto values = row_(result.get()))
        {
            const auto lengths = length_(result.get());
            if (!lengths) fail("The MySQL source stream ended early.");
            Row row;
            row.reserve(columns);
            for (int column = 0; column < columns; ++column)
            {
                if (values[column]) row.push_back(utf8(values[column], lengths[column]));
                else row.push_back(std::nullopt);
            }
            visit(row);
        }
        if (error_(connection)) fail("The MySQL source stream ended early.");
    }

private:
    DriverLibrary library_;
    std::unique_ptr<void, Close> connection_;
    int (WINAPI*query_)(void*, const char*) = library_.symbol<int(WINAPI*)(void*, const char*)>("mysql_query");
    void* (WINAPI*use_)(void*) = library_.symbol<void*(WINAPI*)(void*)>("mysql_use_result");
    Close free_ = library_.symbol<Close>("mysql_free_result");
    char** (WINAPI*row_)(void*) = library_.symbol<char**(WINAPI*)(void*)>("mysql_fetch_row");
    unsigned long* (WINAPI*length_)(void*) = library_.symbol<unsigned long*(WINAPI*)(void*)>("mysql_fetch_lengths");
    unsigned int (WINAPI*fields_)(void*) = library_.symbol<unsigned int(WINAPI*)(void*)>("mysql_num_fields");
    unsigned int (WINAPI*error_)(void*) = library_.symbol<unsigned int(WINAPI*)(void*)>("mysql_errno");
};

QString ident(QString name, bool mysql)
{
    const auto quote = mysql ? u'`' : u'"';
    return quote + name.replace(quote, QString(2, quote)) + quote;
}

QString textLiteral(const QString& value, bool mysql)
{
    const auto hex = QString::fromLatin1(value.toUtf8().toHex());
    return mysql ? QStringLiteral("CONVERT(0x%1 USING utf8mb4)").arg(hex) :
        QStringLiteral("pg_catalog.convert_from(pg_catalog.decode('%1','hex'),'UTF8')").arg(hex);
}

const QString& field(const Row& row, qsizetype index)
{
    if (!row[index]) fail("Source metadata contained an unexpected NULL.");
    return *row[index];
}

void accountMetadata(const Row& row, qsizetype& bytes)
{
    for (const auto& value : row)
        if (value) bytes += value->toUtf8().size();
    if (bytes > maxMetadataBytes) fail("The source schema exceeded the metadata byte limit.");
}

QString targetType(const QString& base, const QString& declared, bool mysql)
{
    if (base == u"bigint" && declared.endsWith(u" unsigned")) return QStringLiteral("DECIMAL(20,0)");
    static const QSet<QString> integers{"int2", "int4", "int8", "bigint", "int", "smallint", "mediumint", "tinyint"};
    static const QSet<QString> text{"text", "varchar", "bpchar", "char", "tinytext", "mediumtext", "longtext"};
    static const QSet<QString> binary{"bytea", "binary", "varbinary", "blob", "tinyblob", "mediumblob", "longblob"};
    if (integers.contains(base)) return QStringLiteral("BIGINT");
    if (text.contains(base)) return QStringLiteral("TEXT");
    if (binary.contains(base)) return QStringLiteral("BLOB");
    if (base == u"bool") return QStringLiteral("BOOLEAN");
    if (base == u"date") return QStringLiteral("DATE");
    if (base == u"time") return QStringLiteral("TIME");
    if (base == u"timestamptz" || (mysql && base == u"timestamp")) return QStringLiteral("TIMESTAMP WITH TIME ZONE");
    if (base != u"decimal" && base != u"numeric") return {};
    static const QRegularExpression decimal(QStringLiteral(R"(^(?:decimal|numeric)\(([0-9]+),\s*([0-9]+)\)(?: unsigned)?$)"));
    const auto match = decimal.match(declared);
    if (!match.hasMatch()) return {};
    const auto precision = match.captured(1).toInt(), scale = match.captured(2).toInt();
    if (precision < 1 || precision > 38 || scale > precision) return {};
    return QStringLiteral("DECIMAL(%1,%2)").arg(precision).arg(scale);
}

QString action(const QString& name, bool mysql)
{
    if (mysql) return name;
    static const QMap<QString, QString> names{{"a", "NO ACTION"}, {"r", "RESTRICT"}, {"c", "CASCADE"},
        {"n", "SET NULL"}, {"d", "SET DEFAULT"}};
    return names.value(name);
}

void addKey(ExternalTable& table, const Row& row, bool mysql)
{
    const auto& kind = field(row, 0);
    if (kind == u"p" || kind == u"PRIMARY KEY")
    {
        table.primaryKey.push_back(field(row, 2));
        return;
    }
    if (kind != u"f" && kind != u"FOREIGN KEY")
    {
        table.unsupported.push_back(QStringLiteral("CONSTRAINT %1 %2").arg(field(row, 1), kind));
        return;
    }
    const auto& name = field(row, 1);
    const auto found = std::ranges::find(table.foreignKeys, name, &ExternalForeignKey::name);
    ExternalForeignKey* key = nullptr;
    if (found == table.foreignKeys.end())
    {
        table.foreignKeys.push_back({name, {}, {field(row, 3), field(row, 4)}, {},
            action(field(row, 6), mysql), action(field(row, 7), mysql)});
        key = &table.foreignKeys.last();
        if (field(row, 8) == u"true")
            table.unsupported.push_back(QStringLiteral("DEFERRABLE OR UNVALIDATED CONSTRAINT %1").arg(name));
    }
    else key = &*found;
    key->columns.push_back(field(row, 2));
    key->referenceColumns.push_back(field(row, 5));
}

void metadata(Session& session, const ExternalSource& source, ExternalTable& table, qsizetype& bytes)
{
    const auto mysql = source.provider == ExternalProvider::mysql;
    const auto schema = textLiteral(source.schema, mysql), name = textLiteral(table.name, mysql);
    const auto columns = mysql ? QStringLiteral(R"(
SELECT COLUMN_NAME,COLUMN_TYPE,DATA_TYPE,IS_NULLABLE,
       CASE WHEN COLUMN_DEFAULT IS NULL THEN '' WHEN COLUMN_DEFAULT='' THEN CHAR(39,39) ELSE COLUMN_DEFAULT END,
       IF(GENERATION_EXPRESSION<>'' OR EXTRA LIKE '%auto_increment%' OR EXTRA LIKE '%on update%','true','false'),
       COALESCE(COLLATION_NAME,''),COALESCE(CHARACTER_SET_NAME,'')
FROM information_schema.COLUMNS
WHERE BINARY TABLE_SCHEMA=BINARY %1 AND BINARY TABLE_NAME=BINARY %2
ORDER BY ORDINAL_POSITION LIMIT %3)") :
        QStringLiteral(R"(
SELECT a.attname,format_type(a.atttypid,a.atttypmod),CASE WHEN t.typnamespace='pg_catalog'::regnamespace THEN t.typname::text ELSE '' END,
       CASE WHEN a.attnotnull THEN 'NO' ELSE 'YES' END,coalesce(pg_get_expr(d.adbin,d.adrelid),''),
       (a.attgenerated<>'' OR a.attidentity<>'')::text,coalesce(coll.collname,''),
       CASE WHEN coll.oid IS NOT NULL THEN getdatabaseencoding() ELSE '' END
FROM pg_namespace n JOIN pg_class c ON c.relnamespace=n.oid
JOIN pg_attribute a ON a.attrelid=c.oid JOIN pg_type t ON t.oid=a.atttypid
LEFT JOIN pg_attrdef d ON d.adrelid=c.oid AND d.adnum=a.attnum
LEFT JOIN pg_collation coll ON coll.oid=a.attcollation
WHERE n.nspname=%1 AND c.relname=%2 AND a.attnum>0 AND NOT a.attisdropped
ORDER BY a.attnum LIMIT %3)");
    session.scan(columns.arg(schema, name).arg(maxColumns + 1), 8, [&](const Row& row) {
        accountMetadata(row, bytes);
        if (table.columns.size() >= maxColumns) fail("A source table has too many columns.");
        const auto type = targetType(field(row, 2), field(row, 1), mysql);
        ExternalColumn column{field(row, 0), field(row, 1), type, field(row, 4), field(row, 6), field(row, 7),
            field(row, 3) == u"YES", field(row, 5) == u"true"};
        if (type.isEmpty())
            table.unsupported.push_back(QStringLiteral("TYPE %1 %2").arg(column.name, column.sourceType));
        if (column.generated) table.unsupported.push_back(QStringLiteral("GENERATED COLUMN %1").arg(column.name));
        if (!column.defaultSql.isEmpty()) table.unsupported.push_back(QStringLiteral("SOURCE DEFAULT %1").arg(column.name));
        table.columns.push_back(std::move(column));
    });
    if (table.columns.isEmpty()) fail("A source table disappeared from the read-only snapshot.");

    const auto keys = mysql ? QStringLiteral(R"(
SELECT t.CONSTRAINT_TYPE,t.CONSTRAINT_NAME,COALESCE(k.COLUMN_NAME,''),COALESCE(k.REFERENCED_TABLE_SCHEMA,''),
       COALESCE(k.REFERENCED_TABLE_NAME,''),COALESCE(k.REFERENCED_COLUMN_NAME,''),
       COALESCE(r.UPDATE_RULE,''),COALESCE(r.DELETE_RULE,''),'false'
FROM information_schema.TABLE_CONSTRAINTS t
LEFT JOIN information_schema.KEY_COLUMN_USAGE k
 ON k.CONSTRAINT_SCHEMA=t.CONSTRAINT_SCHEMA AND k.TABLE_NAME=t.TABLE_NAME AND k.CONSTRAINT_NAME=t.CONSTRAINT_NAME
LEFT JOIN information_schema.REFERENTIAL_CONSTRAINTS r
 ON r.CONSTRAINT_SCHEMA=t.CONSTRAINT_SCHEMA AND r.TABLE_NAME=t.TABLE_NAME AND r.CONSTRAINT_NAME=t.CONSTRAINT_NAME
WHERE BINARY t.TABLE_SCHEMA=BINARY %1 AND BINARY t.TABLE_NAME=BINARY %2
ORDER BY BINARY t.CONSTRAINT_NAME,k.ORDINAL_POSITION LIMIT %3)") :
        QStringLiteral(R"(
SELECT k.contype::text,k.conname,coalesce(a.attname,''),coalesce(rn.nspname,''),coalesce(rr.relname,''),
       coalesce(rc.attname,''),k.confupdtype::text,k.confdeltype::text,(k.condeferrable OR NOT k.convalidated)::text
FROM pg_namespace n JOIN pg_class c ON c.relnamespace=n.oid JOIN pg_constraint k ON k.conrelid=c.oid
LEFT JOIN LATERAL unnest(k.conkey) WITH ORDINALITY cells(attnum,seq) ON k.contype IN ('f','p')
LEFT JOIN pg_attribute a ON a.attrelid=c.oid AND a.attnum=cells.attnum
LEFT JOIN pg_class rr ON rr.oid=k.confrelid LEFT JOIN pg_namespace rn ON rn.oid=rr.relnamespace
LEFT JOIN pg_attribute rc ON rc.attrelid=rr.oid AND rc.attnum=k.confkey[cells.seq::int]
WHERE n.nspname=%1 AND c.relname=%2 AND k.contype IN ('p','f','u','c','x')
ORDER BY k.conname, cells.seq LIMIT %3)");
    int keyRows{};
    session.scan(keys.arg(schema, name).arg(maxMetadata + 1), 9, [&](const Row& row) {
        accountMetadata(row, bytes);
        if (++keyRows > maxMetadata) fail("A source table has too many constraints.");
        addKey(table, row, mysql);
    });
    table.unsupported.removeDuplicates();
}

ExternalSnapshot discover(Session& session, const ExternalSource& source, qsizetype& bytes)
{
    const auto mysql = source.provider == ExternalProvider::mysql;
    const auto schema = textLiteral(source.schema, mysql);
    if (!mysql)
    {
        bool exists{};
        session.scan(QStringLiteral("SELECT 1 FROM pg_namespace WHERE nspname=%1 AND has_schema_privilege(oid,'USAGE')")
            .arg(schema), 1, [&](const Row&) { exists = true; });
        if (!exists) fail("The PostgreSQL source schema is unavailable to this account.");
    }
    const auto relations = mysql ? QStringLiteral(R"(
SELECT TABLE_NAME,TABLE_TYPE,COALESCE(ENGINE,'') FROM information_schema.TABLES
WHERE BINARY TABLE_SCHEMA=BINARY %1 ORDER BY BINARY TABLE_NAME LIMIT %2)") :
        QStringLiteral(R"(
SELECT c.relname,c.relkind::text,
       (c.relrowsecurity OR c.relispartition OR EXISTS(SELECT 1 FROM pg_inherits i WHERE i.inhparent=c.oid OR i.inhrelid=c.oid))::text
FROM pg_namespace n JOIN pg_class c ON c.relnamespace=n.oid
WHERE n.nspname=%1 AND c.relkind IN ('r','p','v','m','f') AND has_table_privilege(c.oid,'SELECT')
ORDER BY c.relname COLLATE "C" LIMIT %2)");
    ExternalSnapshot result;
    result.encoding = QStringLiteral("UTF-8");
    result.timeZone = QStringLiteral("UTC");
    int objectCount{};
    session.scan(relations.arg(schema).arg(maxMetadata + 1), 3, [&](const Row& row) {
        accountMetadata(row, bytes);
        if (++objectCount > maxMetadata) fail("The source schema exceeded the metadata limit.");
        const auto& name = field(row, 0);
        const auto& kind = field(row, 1);
        if (kind == u"v" || kind == u"m" || kind == u"VIEW" || kind == u"SYSTEM VIEW")
            result.unsupported.push_back(QStringLiteral("VIEW %1.%2").arg(source.schema, name));
        else if ((mysql && (kind != u"BASE TABLE" || field(row, 2).compare(u"InnoDB", Qt::CaseInsensitive))) ||
                 (!mysql && (kind != u"r" || field(row, 2) == u"true")))
            result.unsupported.push_back(QStringLiteral("TABLE %1.%2 (no supported snapshot semantics)").arg(source.schema, name));
        else
        {
            if (result.tables.size() >= maxTables) fail("The source schema has too many visible tables.");
            ExternalTable table;
            table.name = name;
            result.tables.push_back(std::move(table));
        }
    });
    const auto others = mysql ? QStringLiteral(R"(
SELECT 'ROUTINE',ROUTINE_NAME FROM information_schema.ROUTINES WHERE BINARY ROUTINE_SCHEMA=BINARY %1
UNION ALL
SELECT 'TRIGGER',TRIGGER_NAME FROM information_schema.TRIGGERS WHERE BINARY TRIGGER_SCHEMA=BINARY %1
ORDER BY 1,2 LIMIT %2)") : QStringLiteral(R"(
SELECT 'ROUTINE',p.proname FROM pg_namespace n JOIN pg_proc p ON p.pronamespace=n.oid
 WHERE n.nspname=%1 AND has_function_privilege(p.oid,'EXECUTE')
UNION ALL
SELECT 'TRIGGER',t.tgname FROM pg_namespace n JOIN pg_class c ON c.relnamespace=n.oid
 JOIN pg_trigger t ON t.tgrelid=c.oid WHERE n.nspname=%1 AND NOT t.tgisinternal AND has_table_privilege(c.oid,'SELECT')
ORDER BY 1,2 LIMIT %2)");
    session.scan(others.arg(schema).arg(maxMetadata + 1), 2, [&](const Row& row) {
        accountMetadata(row, bytes);
        if (++objectCount > maxMetadata) fail("The source schema exceeded the metadata limit.");
        result.unsupported.push_back(QStringLiteral("%1 %2.%3").arg(field(row, 0), source.schema, field(row, 1)));
    });
    result.unsupported.removeDuplicates();
    if (mysql)
        result.unsupported.push_back(QStringLiteral("MySQL metadata omits routines and triggers hidden by source privileges."));
    return result;
}

QJsonObject timestamp(const QString& value)
{
    static const QRegularExpression clock(QStringLiteral(
        R"(^(\d{4}-\d{2}-\d{2})[ T](\d{2}:\d{2}:\d{2})(?:\.(\d{1,9}))?(?:\+00(?::00)?)?$)"));
    const auto match = clock.match(value);
    if (!match.hasMatch()) fail("A source timestamp cannot be represented as a UTC value.");
    const auto date = QDate::fromString(match.captured(1), Qt::ISODate);
    const auto time = QTime::fromString(match.captured(2), Qt::ISODate);
    if (!date.isValid() || !time.isValid()) fail("A source timestamp cannot be represented as a UTC value.");
    const auto seconds = QDateTime(date, time, QTimeZone(QTimeZone::UTC)).toSecsSinceEpoch();
    if (seconds < -64000000000LL || seconds > 255000000000LL)
        fail("A source timestamp is outside the destination's UTC range.");
    const auto nanos = match.captured(3).leftJustified(9, u'0').toInt();
    return {{"t", "timestamp"}, {"seconds", QString::number(seconds)}, {"nanos", nanos}, {"offset_minutes", 0}};
}

QJsonObject decimal(const QString& value, const QString& type)
{
    const auto comma = type.indexOf(u',');
    const auto precision = type.sliced(8, comma - 8).toInt();
    const auto scale = type.sliced(comma + 1, type.size() - comma - 2).toInt();
    static const QRegularExpression syntax(QStringLiteral(R"(^(-?)([0-9]+)(?:\.([0-9]+))?$)"));
    const auto match = syntax.match(value);
    if (!match.hasMatch() || match.capturedLength(3) > scale)
        fail("A source decimal cannot be represented at its declared precision and scale.");
    auto digits = match.captured(2) + match.captured(3).leftJustified(scale, u'0');
    while (digits.size() > 1 && digits.front() == u'0') digits.remove(0, 1);
    if (digits.size() > precision) fail("A source decimal exceeds the destination precision.");
    if (digits != u"0" && match.capturedLength(1)) digits.prepend(u'-');
    return {{"t", "decimal"}, {"unscaled", digits}, {"scale", scale}};
}

QJsonObject typed(const Field& cell, const QString& type)
{
    if (!cell) return {{"t", "null"}};
    const auto& value = *cell;
    if (type == u"TEXT") return {{"t", "text"}, {"v", value}};
    if (type == u"BIGINT")
    {
        bool valid{};
        const auto converted = value.toLongLong(&valid);
        if (!valid) fail("A source integer exceeds the destination range.");
        return {{"t", "i64"}, {"v", QString::number(converted)}};
    }
    if (type == u"BOOLEAN")
    {
        if (value != u"t" && value != u"f") fail("A source Boolean value is invalid.");
        return {{"t", "bool"}, {"v", value == u"t"}};
    }
    if (type == u"BLOB")
    {
        static const QRegularExpression hex(QStringLiteral("^(?:[0-9a-fA-F]{2})*$"));
        if (!hex.match(value).hasMatch()) fail("A source binary value could not be represented exactly.");
        return {{"t", "binary"}, {"hex", value.toLower()}};
    }
    if (type.startsWith(u"DECIMAL(")) return decimal(value, type);
    if (type == u"TIMESTAMP WITH TIME ZONE") return timestamp(value);
    if (type == u"DATE" && QDate::fromString(value, Qt::ISODate).isValid())
        return {{"t", "text"}, {"v", value}};
    if (type == u"TIME")
    {
        static const QRegularExpression clock(QStringLiteral(
            R"(^(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9](?:\.[0-9]{1,9})?$)"));
        if (clock.match(value).hasMatch()) return {{"t", "text"}, {"v", value}};
    }
    fail("A source date or time value cannot be represented exactly.");
}

void scanRows(Session& session, const ExternalSource& source, ExternalTable& table,
    qsizetype& rows, qsizetype& bytes, const ExternalProgress& progress)
{
    const auto mysql = source.provider == ExternalProvider::mysql;
    if (table.primaryKey.isEmpty() || std::ranges::any_of(table.columns, [](const auto& c) { return c.type.isEmpty(); }))
    {
        table.unsupported.push_back(QStringLiteral("DATA %1 requires a primary key and supported column types.").arg(table.name));
        return;
    }
    QStringList projection, keys;
    for (const auto& column : table.columns)
    {
        const auto id = ident(column.name, mysql);
        if (column.type == u"BLOB")
            projection.push_back((mysql ? QStringLiteral("LEFT(HEX(%1),%2)") : QStringLiteral("left(encode(%1,'hex'),%2)"))
                .arg(id).arg(maxFieldBytes + 1));
        else if (column.type == u"TEXT")
            projection.push_back(QStringLiteral("LEFT(%1,%2)").arg(id).arg(maxFieldBytes + 1));
        else projection.push_back(id);
    }
    for (const auto& key : table.primaryKey) keys.push_back(ident(key, mysql));
    const auto sql = QStringLiteral("SELECT %1 FROM %2.%3 ORDER BY %4 LIMIT %5")
        .arg(projection.join(u','), ident(source.schema, mysql), ident(table.name, mysql), keys.join(u','))
        .arg(maxRows - rows + 1);
    session.scan(sql, static_cast<int>(table.columns.size()), [&](const Row& values) {
        if (++rows > maxRows) fail("The selected source tables exceeded the row limit.");
        QJsonArray row;
        for (qsizetype index = 0; index < values.size(); ++index)
        {
            if (!values[index] && !table.columns[index].nullable)
                fail("A source row contained NULL in a non-nullable column.");
            row.push_back(typed(values[index], table.columns[index].type));
        }
        bytes += QJsonDocument(row).toJson(QJsonDocument::Compact).size();
        if (bytes > maxData) fail("The selected source tables exceeded the byte limit.");
        table.rows.push_back(row);
        if (rows % 25 == 0 && progress && !progress(rows, bytes)) fail("Reading the source snapshot was cancelled.");
    });
}
}

ExternalSnapshot snapshot(const ExternalSource& source, QByteArray& password, const ExternalProgress& progress)
{
    try
    {
        const auto mysql = source.provider == ExternalProvider::mysql;
        std::unique_ptr<Session> session;
        if (mysql) session = std::make_unique<Mysql>(source, password);
        else session = std::make_unique<Postgres>(source, password);
        qsizetype metadataBytes{};
        auto result = discover(*session, source, metadataBytes);
        QSet<QString> selection(source.selectedTables.cbegin(), source.selectedTables.cend());
        for (const auto& table : result.tables)
        {
            if (!selection.remove(table.name)) continue;
            // Hold the table's read lock until rollback so metadata and rows have one definition.
            const auto sql = QStringLiteral("SELECT 1 FROM %1.%2 LIMIT 0")
                .arg(ident(source.schema, mysql), ident(table.name, mysql));
            session->scan(sql, 1, [](const Row&) {});
        }
        if (!selection.isEmpty()) fail("A selected source table is unavailable or lacks supported snapshot semantics.");
        if (!source.selectedTables.isEmpty())
        {
            result.tables.removeIf([&](const auto& table) { return !source.selectedTables.contains(table.name); });
        }
        qsizetype rows{}, bytes{};
        for (auto& table : result.tables)
        {
            metadata(*session, source, table, metadataBytes);
            if (source.selectedTables.contains(table.name)) scanRows(*session, source, table, rows, bytes, progress);
            if (progress && !progress(rows, bytes)) fail("Reading the source snapshot was cancelled.");
        }
        session->command(QStringLiteral("ROLLBACK"));
        return result;
    }
    catch (const ReadError& error)
    {
        password.fill('\0');
        ExternalSnapshot result;
        result.error = error.message;
        return result;
    }
}
}
