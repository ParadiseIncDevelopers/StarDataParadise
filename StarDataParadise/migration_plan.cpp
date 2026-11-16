#include "migration_plan.hpp"
#include "table_codec.hpp"
#include "table_design.hpp"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QObject>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace wb::migration
{
namespace
{
QString hash(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QByteArray json(const QJsonArray& value)
{
    return QJsonDocument(value).toJson(QJsonDocument::Compact);
}

QString columnsSql(const QStringList& list)
{
    QStringList quoted;
    for (const auto& name : list) quoted << transfer::identifier(name);
    return quoted.join(", ");
}

bool validIdentifier(const QString& name)
{
    return !name.isEmpty() && name.toUtf8().size() <= 128 && !name.contains(QChar::Null);
}

QString mappedType(const ExternalColumn& column, QJsonObject& type)
{
    static const QSet<QString> types{"BIGINT", "BOOLEAN", "TEXT", "DATE", "TIME", "BLOB",
        "TIMESTAMP WITH TIME ZONE"};
    if (types.contains(column.type))
    {
        type = {{"type", column.type}};
        return column.type;
    }
    static const QRegularExpression decimal(QStringLiteral("^DECIMAL\\(([1-9][0-9]?),([0-9]{1,2})\\)$"));
    const auto matched = decimal.match(column.type);
    if (!matched.hasMatch()) return {};
    const auto precision = matched.captured(1).toInt(), scale = matched.captured(2).toInt();
    if (precision > 38 || scale > precision) return {};
    type = {{"type", "DECIMAL"}, {"precision", precision}, {"scale", scale}};
    return column.type;
}

bool keyAction(const QString& action)
{
    return action.isEmpty() || action == u"NO ACTION" || action == u"RESTRICT";
}

TargetPlan failed(TargetPlan plan, const QString& reason)
{
    plan.error = reason;
    return plan;
}
}

QString targetName(const QString& item)
{
    return sqlQualifiedIdentifier({"main", "public", item});
}

QByteArray keyOf(const QJsonArray& row, const QVector<int>& key)
{
    QJsonArray values;
    for (const auto i : key)
    {
        if (i < 0 || i >= row.size()) return {};
        const auto value = row.at(i).toObject();
        if (value.isEmpty() || value.value("t") == "null") return {};
        values.append(value);
    }
    return values.isEmpty() ? QByteArray{} : json(values);
}

QString checksum(const KeyedRows& rows)
{
    QCryptographicHash sum(QCryptographicHash::Sha256);
    for (const auto& [key, row] : rows)
    {
        sum.addData(key);
        sum.addData(json(row));
    }
    return QString::fromLatin1(sum.result().toHex());
}

TargetPlan planImport(const ExternalSource& source, const ExternalSnapshot& snapshot, const QString& prefix)
{
    TargetPlan plan;
    plan.diagnostics = snapshot.unsupported;
    static const QRegularExpression validPrefix(QStringLiteral("^[A-Za-z][A-Za-z0-9_]{0,31}$"));
    if (!validPrefix.match(prefix).hasMatch())
        return failed(plan, QObject::tr("Enter a target prefix of 1–32 letters, digits or underscores, starting with a letter."));
    if (snapshot.tables.isEmpty() || snapshot.tables.size() != source.selectedTables.size())
        return failed(plan, QObject::tr("Select the source tables to copy."));

    plan.ownerTable = prefix + "__migration_owner";
    QSet<QString> selected, ordered, occupied{plan.ownerTable};
    QList<const ExternalTable*> remaining;
    for (const auto& table : snapshot.tables)
    {
        plan.diagnostics.append(table.unsupported);
        if (!source.selectedTables.contains(table.name) || occupied.contains(prefix + table.name))
            return failed(plan, QObject::tr("Duplicate or unexpected source table %1.").arg(table.name));
        occupied.insert(prefix + table.name);
        selected.insert(table.name);
        remaining.append(&table);
    }
    std::ranges::sort(remaining, {}, [](const auto* item) { return item->name; });

    QJsonArray auditTables;
    while (!remaining.isEmpty())
    {
        const auto next = std::find_if(remaining.cbegin(), remaining.cend(), [&](const auto* table) {
            return std::ranges::all_of(table->foreignKeys, [&](const auto& fk) {
                return fk.referenceTable.size() != 2 || fk.referenceTable[0] != source.schema ||
                    !selected.contains(fk.referenceTable[1]) || ordered.contains(fk.referenceTable[1]);
            });
        });
        if (next == remaining.cend())
            return failed(plan, QObject::tr("Selected tables contain a cyclic or self-referencing foreign key. Resolve the dependency in the source first."));
        const auto& table = **next;
        remaining.erase(next);
        TargetTable target;
        target.name = prefix + table.name;
        if (!validIdentifier(target.name) || table.primaryKey.isEmpty() || table.columns.isEmpty() ||
            table.columns.size() > 60)
            return failed(plan, QObject::tr("%1 needs a supported target name and a primary key to resume safely.").arg(table.name));

        QStringList names, declarations;
        QJsonArray columnMap, keys;
        for (const auto& column : table.columns)
        {
            QJsonObject type;
            const auto sqlType = mappedType(column, type);
            if (!validIdentifier(column.name) || names.contains(column.name) || sqlType.isEmpty())
                return failed(plan, QObject::tr("%1.%2 has an unsupported name or source type %3.")
                    .arg(table.name, column.name, column.sourceType));
            if (column.generated || !column.defaultSql.isEmpty())
                plan.diagnostics << QObject::tr("%1.%2 copies stored values. Review its source default or generated expression separately.")
                    .arg(table.name, column.name);
            names << column.name;
            declarations << transfer::identifier(column.name) + " " + sqlType + (column.nullable ? "" : " NOT NULL");
            columnMap.append(QJsonObject{{"name", column.name}, {"source_type", column.sourceType},
                {"target_type", sqlType}, {"source_encoding", column.encoding},
                {"source_collation", column.collation}, {"source_default", column.defaultSql},
                {"generated", column.generated}, {"nullable", column.nullable}});
            target.types.append(type);
        }
        for (const auto& name : table.primaryKey)
        {
            const auto index = names.indexOf(name);
            if (index < 0 || target.key.contains(static_cast<int>(index)))
                return failed(plan, QObject::tr("Invalid key definition for %1.").arg(table.name));
            target.key.append(static_cast<int>(index));
            keys.append(name);
        }
        declarations << "PRIMARY KEY (" + columnsSql(table.primaryKey) + ")";
        for (const auto& fk : table.foreignKeys)
        {
            if (fk.referenceTable.size() != 2 || fk.referenceTable[0] != source.schema ||
                !selected.contains(fk.referenceTable[1]))
                return failed(plan, QObject::tr("Select the referenced table for %1.%2 in the same source schema.")
                    .arg(table.name, fk.name));
            if (!validIdentifier(fk.name) || fk.columns.isEmpty() || fk.columns.size() != fk.referenceColumns.size() ||
                !keyAction(fk.onUpdate) || !keyAction(fk.onDelete))
                return failed(plan, QObject::tr("Foreign key %1.%2 has a definition or action requiring manual conversion.")
                    .arg(table.name, fk.name));
            declarations << "CONSTRAINT " + transfer::identifier(fk.name) + " FOREIGN KEY (" +
                columnsSql(fk.columns) + ") REFERENCES " + targetName(prefix + fk.referenceTable[1]) +
                " (" + columnsSql(fk.referenceColumns) + ")";
        }

        target.sql = "CREATE TABLE " + targetName(target.name) + " (" + declarations.join(", ") + ")";
        target.select = "SELECT " + columnsSql(names) + " FROM " + targetName(target.name);
        target.insert = "INSERT INTO " + targetName(target.name) + " (" + columnsSql(names) + ") VALUES ";
        qsizetype ordinal = 0;
        for (const auto& value : table.rows)
        {
            ++ordinal;
            const auto row = value.toArray();
            const auto key = keyOf(row, target.key);
            if (row.size() != names.size() || key.isEmpty() || !target.rows.emplace(key, row).second)
            {
                plan.report["rejected_rows"] = QJsonArray{QJsonObject{{"table", table.name},
                    {"source_ordinal", ordinal}, {"reason", "invalid_or_duplicate_key"}}};
                return failed(plan, QObject::tr("%1 has a malformed row or a duplicate or NULL key.").arg(table.name));
            }
        }
        plan.rowCount += target.rows.size();
        target.checksum = checksum(target.rows);
        auditTables.append(QJsonObject{{"name", table.name}, {"target", target.name}, {"columns", columnMap},
            {"primary_key", keys}, {"row_count", static_cast<qint64>(target.rows.size())},
            {"checksum", target.checksum}, {"ddl", target.sql}});
        ordered.insert(table.name);
        plan.tables.append(std::move(target));
    }
    plan.diagnostics.removeDuplicates();
    plan.diagnostics.sort(Qt::CaseSensitive);
    QJsonArray diagnostics;
    for (const auto& item : plan.diagnostics) diagnostics.append(item);
    const QJsonArray sourceIdentity{source.provider == ExternalProvider::postgresql ? "postgresql" : "mysql",
        source.host, source.port, source.database, source.schema, source.username, snapshot.encoding, snapshot.timeZone,
        auditTables, diagnostics};
    plan.fingerprint = hash(json(sourceIdentity));
    plan.report = {{"tables", auditTables}, {"unsupported", diagnostics}, {"source_snapshot", plan.fingerprint},
        {"encoding", snapshot.encoding}, {"time_zone", snapshot.timeZone}, {"source_rows", plan.rowCount},
        {"rejected_rows", QJsonArray{}}};
    return plan;
}
}
