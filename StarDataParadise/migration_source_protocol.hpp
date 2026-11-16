#pragma once

#include "migration_source.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonObject>
#include <QObject>
#include <QRegularExpression>
#include <QSet>

namespace wb::migration::sourceProtocol
{
inline constexpr qsizetype maxPassword = 1024;
inline constexpr qsizetype maxRequest = 32 * 1024;
inline constexpr qsizetype maxData = 8 * 1024 * 1024;
inline constexpr qsizetype maxResponse = 12 * 1024 * 1024;
inline constexpr int maxTables = 128;
inline constexpr int maxColumns = 256;
inline constexpr int maxRows = 5000;
inline constexpr int maxMetadata = 4096;
inline constexpr int timeoutMilliseconds = 120000;

inline QString tr(const char* message)
{
    return QCoreApplication::translate("MigrationSource", message);
}

inline QStringList strings(const QJsonValue& value)
{
    QStringList result;
    for (const auto& entry : value.toArray()) result.push_back(entry.toString());
    return result;
}

inline QJsonObject request(const ExternalSource& source, qsizetype passwordBytes)
{
    return {{"provider", source.provider == ExternalProvider::postgresql ? "postgresql" : "mysql"},
        {"host", source.host}, {"port", source.port}, {"database", source.database}, {"schema", source.schema},
        {"username", source.username}, {"caFile", source.caFile}, {"driverFile", source.driverFile},
        {"selectedTables", QJsonArray::fromStringList(source.selectedTables)}, {"passwordBytes", passwordBytes},
        {"allowInsecureLoopback", source.allowInsecureLoopback}};
}

inline bool boundedName(const QString& value, qsizetype max)
{
    return !value.isEmpty() && !value.contains(u'\0') && value.toUtf8().size() <= max;
}

inline bool decodeRequest(const QJsonObject& object, ExternalSource& source)
{
    const auto provider = object.value("provider").toString();
    if (provider != "postgresql" && provider != "mysql") return false;
    source.provider = provider == "postgresql" ? ExternalProvider::postgresql : ExternalProvider::mysql;
    source.host = object.value("host").toString();
    source.database = object.value("database").toString();
    source.schema = object.value("schema").toString();
    source.username = object.value("username").toString();
    source.caFile = object.value("caFile").toString();
    source.driverFile = object.value("driverFile").toString();
    source.selectedTables = strings(object.value("selectedTables"));
    source.allowInsecureLoopback = object.value("allowInsecureLoopback").toBool();
    const auto port = object.value("port").toInt();
    const auto passwordBytes = object.value("passwordBytes").toInteger(-1);
    static const QRegularExpression hostPattern(QStringLiteral("^[A-Za-z0-9._:%-]+$"));
    if (port <= 0 || port > 65535 || passwordBytes <= 0 || passwordBytes > maxPassword ||
        !boundedName(source.host, 253) || !hostPattern.match(source.host).hasMatch() ||
        !boundedName(source.database, 256) || !boundedName(source.schema, 256) ||
        !boundedName(source.username, 256) || source.selectedTables.size() > maxTables)
        return false;
    source.port = static_cast<quint16>(port);
    const auto mysql = source.provider == ExternalProvider::mysql;
    if (mysql && source.schema != source.database) return false;
    if (source.allowInsecureLoopback && source.host != u"127.0.0.1" && source.host != u"::1") return false;
    const QFileInfo driver(source.driverFile), ca(source.caFile);
    if (!driver.isAbsolute() || !driver.isFile() ||
        driver.fileName().compare(mysql ? u"libmysql.dll" : u"libpq.dll", Qt::CaseInsensitive) != 0 ||
        (!source.allowInsecureLoopback && (!ca.isAbsolute() || !ca.isReadable() || !ca.isFile())))
        return false;
    QSet<QString> names;
    for (const auto& name : source.selectedTables)
    {
        if (!boundedName(name, 256) || names.contains(name)) return false;
        names.insert(name);
    }
    return true;
}

inline QJsonObject snapshot(const ExternalSnapshot& value)
{
    QJsonArray tables;
    for (const auto& table : value.tables)
    {
        QJsonArray columns, foreignKeys;
        for (const auto& column : table.columns)
            columns.push_back(QJsonObject{{"name", column.name}, {"sourceType", column.sourceType},
                {"type", column.type}, {"defaultSql", column.defaultSql}, {"collation", column.collation},
                {"encoding", column.encoding}, {"nullable", column.nullable}, {"generated", column.generated}});
        for (const auto& key : table.foreignKeys)
            foreignKeys.push_back(QJsonObject{{"name", key.name}, {"columns", QJsonArray::fromStringList(key.columns)},
                {"referenceTable", QJsonArray::fromStringList(key.referenceTable)},
                {"referenceColumns", QJsonArray::fromStringList(key.referenceColumns)},
                {"onUpdate", key.onUpdate}, {"onDelete", key.onDelete}});
        tables.push_back(QJsonObject{{"name", table.name}, {"columns", columns}, {"foreignKeys", foreignKeys},
            {"primaryKey", QJsonArray::fromStringList(table.primaryKey)}, {"rows", table.rows},
            {"unsupported", QJsonArray::fromStringList(table.unsupported)}});
    }
    return {{"tables", tables}, {"encoding", value.encoding}, {"timeZone", value.timeZone},
        {"unsupported", QJsonArray::fromStringList(value.unsupported)}, {"error", value.error}};
}

inline ExternalSnapshot decodeSnapshot(const QJsonObject& object)
{
    ExternalSnapshot result;
    result.error = object.value("error").toString();
    if (!result.error.isEmpty()) return result;
    result.encoding = object.value("encoding").toString();
    result.timeZone = object.value("timeZone").toString();
    result.unsupported = strings(object.value("unsupported"));
    for (const auto& entry : object.value("tables").toArray())
    {
        const auto json = entry.toObject();
        ExternalTable table;
        table.name = json.value("name").toString();
        table.primaryKey = strings(json.value("primaryKey"));
        table.unsupported = strings(json.value("unsupported"));
        table.rows = json.value("rows").toArray();
        for (const auto& item : json.value("columns").toArray())
        {
            const auto field = item.toObject();
            table.columns.push_back({field.value("name").toString(), field.value("sourceType").toString(),
                field.value("type").toString(), field.value("defaultSql").toString(), field.value("collation").toString(),
                field.value("encoding").toString(), field.value("nullable").toBool(), field.value("generated").toBool()});
        }
        for (const auto& item : json.value("foreignKeys").toArray())
        {
            const auto key = item.toObject();
            table.foreignKeys.push_back({key.value("name").toString(), strings(key.value("columns")),
                strings(key.value("referenceTable")), strings(key.value("referenceColumns")),
                key.value("onUpdate").toString(), key.value("onDelete").toString()});
        }
        result.tables.push_back(std::move(table));
    }
    return result;
}
}
