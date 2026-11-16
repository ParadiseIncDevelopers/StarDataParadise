#include "compare_studio.hpp"

#include "sql_session.hpp"
#include "table_design.hpp"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <map>
#include <windows.h>

namespace wb
{
namespace
{
constexpr int tableKind = 3;
constexpr int indexKind = 4;
constexpr int viewKind = 5;
constexpr int routineKind = 6;
constexpr int sequenceKind = 7;
constexpr int ruleKind = 8;
constexpr int maximumRows = 10000;
constexpr qsizetype maximumDataBytes = 8 * 1024 * 1024;

QString tr(const char* message) { return QCoreApplication::translate("CompareStudio", message); }

QStringList names(const QJsonValue& value)
{
    QStringList result;
    for (const auto part : value.toArray()) result.push_back(part.toString());
    return result;
}

QString key(const QJsonObject& object)
{
    auto path = names(object.value("qualified_name"));
    if (path.size() == 3) path.removeFirst();
    return sqlQualifiedIdentifier(path);
}

QString label(int kind)
{
    switch (kind)
    {
    case tableKind: return tr("Table");
    case indexKind: return tr("Index");
    case viewKind: return tr("View");
    case routineKind: return tr("Routine or trigger");
    case sequenceKind: return tr("Sequence");
    case ruleKind: return tr("Decision rule");
    default: return tr("Catalog object");
    }
}

QString revision(const QJsonObject& response)
{
    const auto global = response.value("revision").toString();
    if (!global.isEmpty()) return global;
    for (const auto object : response.value("objects").toArray())
    {
        const auto value = object.toObject().value("data_revision").toString();
        if (!value.isEmpty()) return value;
    }
    return {};
}

QString errorText(const SqlReply& reply)
{
    const auto error = QString::fromUtf8(reply.error.message).trimmed();
    return error.isEmpty() ? wb::status(reply.error.code) : error;
}

QJsonObject objectNamed(const QJsonArray& catalog, const QString& relativeName, int kind)
{
    for (const auto value : catalog)
    {
        const auto object = value.toObject();
        if (object.value("kind").toInt() == kind && key(object) == relativeName) return object;
    }
    return {};
}

QJsonObject objectId(const QJsonArray& catalog, const QJsonValue& id)
{
    for (const auto value : catalog)
        if (value.toObject().value("id") == id) return value.toObject();
    return {};
}

QMap<QString, QJsonObject> ofKind(const QJsonArray& catalog, int kind)
{
    QMap<QString, QJsonObject> result;
    for (const auto value : catalog)
    {
        const auto object = value.toObject();
        if (object.value("kind").toInt() == kind && !key(object).isEmpty()) result.insert(key(object), object);
    }
    return result;
}

QStringList columnNames(const QJsonObject& table)
{
    QStringList result;
    for (const auto& column : readDesignerColumns(table)) result.push_back(column.name);
    return result;
}

QStringList indexedColumns(const QJsonObject& table, const QJsonValue& ordinals, const QMap<QString, QString>& renames = {})
{
    const auto columns = columnNames(table);
    QStringList result;
    for (const auto value : ordinals.toArray())
    {
        const int index = value.toInt(-1);
        if (index < 0 || index >= columns.size()) return {};
        result.push_back(renames.value(columns.at(index), columns.at(index)));
    }
    return result;
}

QStringList replaceDatabase(QStringList path, const QString& database)
{
    if (path.size() == 3) path[0] = database;
    return path;
}

using ColumnRenames = QMap<QString, QMap<QString, QString>>; // relative table name, destination column -> source column

DesignerConstraint constraintFrom(const QJsonObject& item, const QJsonObject& table, const QJsonArray& catalog,
    const QString& destinationDatabase, const ColumnRenames& renames = {})
{
    DesignerConstraint result;
    result.name = item.value("name").toString();
    result.kind = item.value("kind").toString();
    result.columns = indexedColumns(table, item.value("columns"), renames.value(key(table)));
    result.checkSql = item.value("check_sql").toString();
    if (result.kind == u"foreign_key")
    {
        const auto referenced = objectId(catalog, item.value("reference_table_id"));
        result.referenceTable = replaceDatabase(names(referenced.value("qualified_name")), destinationDatabase);
        result.referenceColumns = indexedColumns(referenced, item.value("reference_columns"), renames.value(key(referenced)));
    }
    return result;
}

bool sameConstraint(const DesignerConstraint& left, const DesignerConstraint& right)
{
    return left.name == right.name && left.kind == right.kind && left.columns == right.columns &&
        left.referenceTable == right.referenceTable && left.referenceColumns == right.referenceColumns &&
        left.checkSql == right.checkSql;
}

DesignerIndex indexFrom(const QJsonObject& item, const QJsonObject& table, const QMap<QString, QString>& renames = {})
{
    const auto details = item.value("index").toObject();
    return {names(item.value("qualified_name")).value(2), indexedColumns(table, details.value("columns"), renames),
        item.value("collation").toObject().value("name").toString(), details.value("unique").toBool()};
}

bool sameIndex(const DesignerIndex& left, const DesignerIndex& right)
{
    return left.name == right.name && left.columns == right.columns && left.collation == right.collation && left.unique == right.unique;
}

QMap<QString, QJsonObject> indexesFor(const QMap<QString, QJsonObject>& indexes, const QJsonObject& table)
{
    QMap<QString, QJsonObject> result;
    for (auto item = indexes.cbegin(); item != indexes.cend(); ++item)
        if (item.value().value("index").toObject().value("table_id") == table.value("id")) result.insert(item.key(), item.value());
    return result;
}

QStringList dependencyNames(const QJsonObject& object, const QJsonArray& catalog)
{
    QStringList result;
    for (const auto id : object.value("depends_on").toArray()) result.push_back(key(objectId(catalog, id)));
    result.sort();
    return result;
}

struct CatalogSnapshot
{
    QJsonArray objects;
    QString revision;
    QMap<QString, QString> definitions;
};

QString definitionKey(const QJsonObject& object)
{
    return QString::number(object.value("kind").toInt()) + u'/' + key(object);
}

QString normalizedDefinition(const QJsonObject& object, const QJsonArray& catalog, int kind)
{
    if (kind == routineKind) return object.value("source").toString().trimmed();
    const auto definition = object.value("definition").toString();
    auto image = QJsonDocument::fromJson(definition.toUtf8()).object();
    if (image.isEmpty()) return definition;
    if (kind == viewKind) image.remove("base"); // not a cross-instance identity
    if (kind == ruleKind)
    {
        // Rule bindings carry instance-local table IDs. Keep the AST and resolve the binding separately.
        for (const auto& name : {"table", "target", "table_id"})
        {
            const auto id = image.value(name);
            const auto number = id.isDouble() ? QString::number(id.toInteger()) : id.toString();
            const auto table = objectId(catalog, number);
            if (!number.isEmpty() && !table.isEmpty()) image.insert(name, key(table));
        }
    }
    return QString::fromUtf8(QJsonDocument(image).toJson(QJsonDocument::Compact));
}

struct SchemaPlan
{
    QStringList differences;
    QStringList manual;
    QStringList statements;
    bool destructive = false;
    bool ambiguous = false;
    bool checksRows = false;
};

struct OrderedTables
{
    QStringList names;
    QStringList cycles;
};

OrderedTables dependencyOrder(QSet<QString> pending, const QMap<QString, QJsonObject>& tables, const QJsonArray& catalog)
{
    OrderedTables result;
    while (!pending.isEmpty())
    {
        auto names = pending.values();
        names.sort();
        bool advanced = false;
        for (const auto& name : names)
        {
            const auto dependencies = dependencyNames(tables.value(name), catalog);
            if (std::ranges::any_of(dependencies, [&](const QString& dependency) { return pending.contains(dependency); })) continue;
            result.names.push_back(name);
            pending.remove(name);
            advanced = true;
        }
        if (!advanced)
        {
            result.cycles = names;
            break;
        }
    }
    return result;
}

bool sameColumn(const DesignerColumn& left, const DesignerColumn& right)
{
    return left.name == right.name && left.type == right.type && left.nullable == right.nullable &&
        left.value == right.value && left.defaultSql == right.defaultSql;
}

void columnPlan(const QJsonObject& from, const QJsonObject& to, const QMap<QString, QString>& renames,
    TableDesign& design, SchemaPlan& plan)
{
    const auto table = key(from);
    const auto sourceColumns = readDesignerColumns(from);
    const auto destinationColumns = readDesignerColumns(to);
    QMap<QString, DesignerColumn> existing;
    for (const auto& column : destinationColumns) existing.insert(renames.value(column.name, column.name), column);
    QStringList added, removed, survivingSource, survivingDestination;
    for (auto column : sourceColumns)
    {
        if (!existing.contains(column.name))
        {
            column.originalOrdinal = -1;
            added.push_back(column.name);
            plan.differences.push_back(tr("%1 · add column %2 (%3)").arg(table, column.name, column.type));
        }
        else
        {
            const auto previous = existing.take(column.name);
            column.originalOrdinal = previous.originalOrdinal;
            survivingSource.push_back(column.name);
            if (previous.name != column.name)
                plan.differences.push_back(tr("%1 · rename column %2 → %3").arg(table, previous.name, column.name));
            auto sameName = previous;
            sameName.name = column.name;
            if (!sameColumn(sameName, column))
                plan.differences.push_back(tr("%1 · %2 type, nullability, default or identity differs (%3 → %4)")
                    .arg(table, column.name, previous.type, column.type));
        }
        design.columns.push_back(std::move(column));
    }
    for (const auto& column : destinationColumns)
    {
        const auto mapped = renames.value(column.name, column.name);
        if (existing.contains(mapped))
        {
            removed.push_back(column.name);
            plan.differences.push_back(tr("%1 · remove column %2 (%3)").arg(table, column.name, column.type));
        }
        else survivingDestination.push_back(mapped);
    }
    if (survivingSource != survivingDestination)
        plan.manual.push_back(tr("%1 · existing column order differs. Column reorder requires a separately reviewed rebuild.").arg(table));
    plan.ambiguous |= !added.isEmpty() && !removed.isEmpty();
}

void constraintPlan(const QJsonObject& from, const QJsonObject& to, const CatalogSnapshot& source,
    const CatalogSnapshot& destination, const QString& database, const ColumnRenames& renames,
    TableDesign& design, SchemaPlan& plan)
{
    const auto current = to.value("constraints").toArray();
    QSet<int> retained;
    for (const auto item : from.value("constraints").toArray())
    {
        const auto wanted = constraintFrom(item.toObject(), from, source.objects, database);
        int found = -1;
        for (int i = 0; i < current.size(); ++i)
        {
            if (!retained.contains(i) && sameConstraint(wanted, constraintFrom(current.at(i).toObject(), to,
                    destination.objects, database, renames)))
            {
                found = i;
                break;
            }
        }
        if (found >= 0) retained.insert(found);
        else design.addedConstraints.push_back(wanted);
    }
    for (int i = 0; i < current.size(); ++i)
        if (!retained.contains(i)) design.droppedConstraints.push_back(i);
    if (!design.addedConstraints.isEmpty() || !design.droppedConstraints.isEmpty())
        plan.differences.push_back(tr("%1 · constraints: %2 add, %3 remove or replace").arg(key(from))
            .arg(design.addedConstraints.size()).arg(design.droppedConstraints.size()));
}

SchemaPlan makePlan(const CatalogSnapshot& source, const CatalogSnapshot& destination, const QString& database,
    const ColumnRenames& renames)
{
    SchemaPlan plan;
    const auto sources = ofKind(source.objects, tableKind);
    const auto targets = ofKind(destination.objects, tableKind);
    const auto sourceIndexes = ofKind(source.objects, indexKind);
    const auto targetIndexes = ofKind(destination.objects, indexKind);
    QSet<QString> creates, drops;
    for (auto it = sources.cbegin(); it != sources.cend(); ++it) if (!targets.contains(it.key())) creates.insert(it.key());
    for (auto it = targets.cbegin(); it != targets.cend(); ++it) if (!sources.contains(it.key())) drops.insert(it.key());
    plan.ambiguous = !creates.isEmpty() && !drops.isEmpty();
    QJsonArray validationCatalog = destination.objects;
    for (auto it = sources.cbegin(); it != sources.cend(); ++it)
    {
        auto shape = it.value();
        shape["qualified_name"] = QJsonArray::fromStringList(replaceDatabase(names(shape.value("qualified_name")), database));
        // Source shapes validate referenced new/renamed columns, while destination IDs continue to identify owned indexes.
        if (!targets.contains(it.key())) continue;
        for (qsizetype i = 0; i < validationCatalog.size(); ++i)
        {
            auto value = validationCatalog.at(i).toObject();
            if (value.value("kind").toInt() != tableKind || key(value) != it.key()) continue;
            value["columns"] = shape.value("columns");
            validationCatalog[i] = value;
            break;
        }
    }

    QStringList pre, changes, keys, indexes, removals;
    const auto addTable = [&](const QJsonObject& from, const QJsonObject& to) {
        TableDesign design;
        design.target = replaceDatabase(names(from.value("qualified_name")), database);
        const auto relativeName = key(from);
        if (!from.value("schema_details").toBool())
        {
            plan.manual.push_back(tr("%1 · source default and constraint details require DDL access.").arg(relativeName));
            return;
        }
        columnPlan(from, to, renames.value(relativeName), design, plan);
        constraintPlan(from, to, source, destination, database, renames, design, plan);
        const auto oldIndexes = indexesFor(targetIndexes, to);
        auto newIndexes = indexesFor(sourceIndexes, from);
        for (auto it = oldIndexes.cbegin(); it != oldIndexes.cend(); ++it)
        {
            const auto counterpart = newIndexes.take(it.key());
            const auto old = indexFrom(it.value(), to, renames.value(relativeName));
            const auto candidate = indexFrom(counterpart, from);
            if (!counterpart.isEmpty() && sameIndex(old, candidate) && it.value().value("collation").toObject() == counterpart.value("collation").toObject()) continue;
            design.droppedIndexIds.push_back(it.value().value("id").toString());
            if (!counterpart.isEmpty()) design.addedIndexes.push_back(candidate);
        }
        for (const auto& item : newIndexes) design.addedIndexes.push_back(indexFrom(item, from));
        if (!design.addedIndexes.isEmpty() || !design.droppedIndexIds.isEmpty())
            plan.differences.push_back(tr("%1 · indexes: %2 add, %3 remove or replace").arg(relativeName)
                .arg(design.addedIndexes.size()).arg(design.droppedIndexIds.size()));

        // An index hidden by permissions must remain in the manual review, rather than being treated as absent.
        for (const auto& item : sourceIndexes)
            if (item.value("depends_on").toArray().contains(from.value("id")) && item.value("index").toObject().isEmpty())
                plan.manual.push_back(tr("%1 · index definition is inaccessible.").arg(key(item)));

        const auto ddl = planTableDesign(validationCatalog, to, design);
        if (!ddl.error.isEmpty())
        {
            plan.manual.push_back(relativeName + QStringLiteral(" · ") + ddl.error);
            return;
        }
        const auto alter = QStringLiteral("ALTER TABLE %1 ").arg(sqlQualifiedIdentifier(design.target));
        for (const auto& statement : ddl.statements)
        {
            if (statement.startsWith(u"DROP INDEX ") || statement.startsWith(alter + QStringLiteral("DROP CONSTRAINT "))) pre.push_back(statement);
            else if (statement.startsWith(u"CREATE INDEX ") || statement.startsWith(u"CREATE UNIQUE INDEX ")) indexes.push_back(statement);
            else if (statement.startsWith(alter + QStringLiteral("ADD CONSTRAINT ")) || statement.startsWith(alter + QStringLiteral("ADD FOREIGN KEY ")) ||
                statement.startsWith(alter + QStringLiteral("ADD UNIQUE ")) || statement.startsWith(alter + QStringLiteral("ADD CHECK ")) ||
                statement.startsWith(alter + QStringLiteral("ADD PRIMARY KEY "))) keys.push_back(statement);
            else changes.push_back(statement);
        }
        plan.destructive |= ddl.destructive;
        plan.checksRows |= ddl.checksRows;
        if (to.isEmpty())
        {
            auto published = from;
            published["qualified_name"] = QJsonArray::fromStringList(design.target);
            validationCatalog.push_back(published);
        }
    };

    const auto additions = dependencyOrder(creates, sources, source.objects);
    for (const auto& name : additions.names)
    {
        plan.differences.push_back(tr("Create table %1").arg(name));
        addTable(sources.value(name), {});
    }
    for (const auto& name : additions.cycles) plan.manual.push_back(tr("%1 · cyclic table dependencies require a staged migration.").arg(name));
    for (auto it = sources.cbegin(); it != sources.cend(); ++it)
        if (targets.contains(it.key())) addTable(it.value(), targets.value(it.key()));

    const auto deletions = dependencyOrder(drops, targets, destination.objects);
    for (auto it = deletions.names.crbegin(); it != deletions.names.crend(); ++it)
    {
        const auto& name = *it;
        const auto table = targets.value(name);
        plan.differences.push_back(tr("Remove table %1").arg(name));
        bool blocked = false;
        for (const auto value : destination.objects)
        {
            const auto item = value.toObject();
            const auto kind = item.value("kind").toInt();
            if (kind == indexKind || (kind == tableKind && drops.contains(key(item)))) continue;
            if (!item.value("depends_on").toArray().contains(table.value("id"))) continue;
            plan.manual.push_back(tr("%1 · remove dependency from %2 before dropping the table.").arg(name, key(item)));
            blocked = true;
        }
        if (!blocked)
        {
            removals.push_back(QStringLiteral("DROP TABLE ") + sqlQualifiedIdentifier(names(table.value("qualified_name"))));
            plan.destructive = true;
        }
    }
    for (const auto& name : deletions.cycles) plan.manual.push_back(tr("%1 · cyclic dependencies prevent automatic drop.").arg(name));

    for (const auto kind : {viewKind, routineKind, sequenceKind, ruleKind})
    {
        const auto left = ofKind(source.objects, kind);
        const auto right = ofKind(destination.objects, kind);
        auto items = left.keys() + right.keys();
        items.removeDuplicates();
        items.sort();
        for (const auto& name : items)
        {
            const auto from = left.value(name);
            const auto to = right.value(name);
            const auto sourceDefinition = source.definitions.value(definitionKey(from));
            const auto destinationDefinition = destination.definitions.value(definitionKey(to));
            QString detail;
            if (from.isEmpty()) detail = tr("destination only");
            else if (to.isEmpty()) detail = tr("source only");
            else if (from.value("columns") != to.value("columns") || dependencyNames(from, source.objects) != dependencyNames(to, destination.objects) ||
                (!sourceDefinition.isEmpty() && !destinationDefinition.isEmpty() && sourceDefinition != destinationDefinition)) detail = tr("shape, definition or dependencies differ");
            else if (kind != sequenceKind && (sourceDefinition.isEmpty() || destinationDefinition.isEmpty())) detail = tr("definition comparison unavailable");
            if (!detail.isEmpty())
                plan.manual.push_back(tr("%1 %2 · %3. Review deployment manually.").arg(label(kind), name, detail));
        }
    }
    plan.statements = pre + changes + keys + indexes + removals;
    if (plan.statements.size() > 256)
    {
        plan.statements.clear();
        plan.manual.push_back(tr("The plan exceeds the 256-statement limit. Deploy a smaller change set."));
    }
    return plan;
}

bool historyRead(SqlSession& session, const QString& target, const QString& revision, SqlSession::Finished complete)
{
    const auto payload = QJsonDocument(QJsonObject{{"target", target}, {"revision", revision}, {"limit", 1}})
        .toJson(QJsonDocument::Compact);
    return session.request([payload](sdp_connection connection, sdp_buffer* output, sdp_error* error) {
        return sdp_history_command(connection, SDP_HISTORY_READ, {payload.constData(), static_cast<uint32_t>(payload.size())}, output, error);
    }, std::move(complete));
}

QStringList selectedColumns(const QString& input)
{
    QStringList result;
    if (input.trimmed().isEmpty()) return result;
    for (const auto& name : input.split(u',')) result.push_back(name.trimmed());
    return result;
}

QString selectSql(const QJsonObject& table, const QStringList& columns)
{
    const auto relation = sqlQualifiedIdentifier(names(table.value("qualified_name")));
    if (relation.isEmpty() || !table.value("permissions").toObject().value("select").toBool() ||
        columns.isEmpty() || columns.size() > 60) return {};
    QSet<QString> used;
    QStringList quoted;
    const auto available = columnNames(table);
    for (const auto& name : columns)
    {
        const auto column = sqlQualifiedIdentifier({name});
        if (column.isEmpty() || !available.contains(name) || used.contains(name)) return {};
        used.insert(name);
        quoted.push_back(column);
    }
    return QStringLiteral("SELECT %1 FROM %2").arg(quoted.join(QStringLiteral(", ")), relation);
}

QJsonObject comparable(QJsonObject value, bool languageCollation, bool& valid)
{
    const auto type = value.value("t").toString();
    if (type.isEmpty()) valid = false;
    if (type == u"decimal")
    {
        auto digits = value.value("unscaled").toString();
        int scale = value.value("scale").toInt(-1);
        if (digits.isEmpty() || scale < 0 || scale > 38)
        {
            valid = false;
            return {};
        }
        if (digits == u"0" || digits == u"-0")
        {
            digits = QStringLiteral("0");
            scale = 0;
        }
        while (scale && digits.size() > 1 && digits.back() == u'0')
        {
            digits.chop(1);
            --scale;
        }
        value["scale"] = scale;
        value["unscaled"] = digits;
    }
    if (type == u"text" && languageCollation)
    {
        const auto content = value.value("v").toString();
        const auto* text = reinterpret_cast<LPCWSTR>(content.utf16());
        const auto length = content.isEmpty() ? -1 : static_cast<int>(content.size());
        constexpr DWORD flags = LCMAP_SORTKEY | LINGUISTIC_IGNORECASE | NORM_LINGUISTIC_CASING;
        const int size = LCMapStringEx(L"tr-TR", flags, text, length, nullptr, 0, nullptr, nullptr, 0);
        QByteArray sortKey(size, '\0');
        if (size <= 0 || !LCMapStringEx(L"tr-TR", flags, text, length, reinterpret_cast<LPWSTR>(sortKey.data()), size, nullptr, nullptr, 0))
        {
            valid = false;
            return {};
        }
        value["v"] = QString::fromLatin1(sortKey.toHex());
    }
    return value;
}

struct ComparedRows
{
    std::map<QByteArray, QJsonArray> values;
    qsizetype bytes = 0;
    QString error;

    void accept(const SqlEvent& event, int keyCount, int width, int rowLimit, bool languageCollation)
    {
        if (event.kind != SqlEvent::Kind::batch || !error.isEmpty()) return;
        for (const auto value : event.data.value("rows").toArray())
        {
            if (values.size() >= static_cast<size_t>(rowLimit))
            {
                error = tr("The row limit was exceeded. A partial comparison cannot determine equality.");
                return;
            }
            const auto row = value.toArray();
            if (row.size() != width)
            {
                error = tr("A streamed row was missing one or more selected columns.");
                return;
            }
            QJsonArray identity, fields;
            bool valid = true;
            for (int i = 0; i < width; ++i)
            {
                const auto field = comparable(row.at(i).toObject(), languageCollation, valid);
                if (i < keyCount)
                {
                    if (field.value("t").toString() == u"null")
                    {
                        error = tr("A selected key contains NULL.");
                        return;
                    }
                    identity.push_back(field);
                }
                else fields.push_back(field);
            }
            if (!valid)
            {
                error = tr("A streamed value had an unsupported encoding or collation.");
                return;
            }
            auto identityBytes = QJsonDocument(identity).toJson(QJsonDocument::Compact);
            bytes += identityBytes.size() + QJsonDocument(fields).toJson(QJsonDocument::Compact).size();
            if (bytes > maximumDataBytes)
            {
                error = tr("The comparison exceeded the 8 MiB snapshot budget per side.");
                return;
            }
            if (!values.emplace(std::move(identityBytes), std::move(fields)).second)
            {
                error = tr("Duplicate key under the selected collation.");
                return;
            }
        }
    }
};

class CompareStudio final : public QDialog
{
public:
    CompareStudio(SqlSession& source, SqlSession& destination, QWidget* parent);
    void reject() override { if (!working_) QDialog::reject(); }

protected:
    void closeEvent(QCloseEvent* event) override
    {
        if (working_) event->ignore();
        else QDialog::closeEvent(event);
    }

private:
    void refresh();
    void capture(SqlSession& session, CatalogSnapshot& snapshot, std::function<void()> next);
    void definitions(SqlSession& session, CatalogSnapshot& snapshot, const QJsonArray& objects,
        qsizetype at, std::function<void()> next);
    void rebuild();
    void apply();
    void rollback(const QString& reason);
    void finish(const QString& message);
    bool ready();
    void buttons();
    void dataCompare();
    void historyCompare();
    void compareRead(SqlSession& session, const QString& sql, int keys, int columns, int limit, bool turkish,
        std::function<void(ComparedRows)> done);

    QPointer<SqlSession> source_, destination_;
    CatalogSnapshot from_, to_;
    SchemaPlan plan_;
    bool working_ = false;
    bool captured_ = false;
    QString mappingError_;
    QPushButton *refresh_{}, *apply_{}, *compare_{}, *historical_{};
    QCheckBox *destructive_{}, *treatAsSeparate_{};
    QComboBox *sourceTable_{}, *destinationTable_{}, *collation_{};
    QLineEdit *sourceKeys_{}, *destinationKeys_{}, *sourceFields_{}, *destinationFields_{}, *rowLimit_{};
    QLineEdit *sourceRevision_{}, *destinationRevision_{}, *sourceObject_{}, *destinationObject_{};
    QLabel* message_{};
    QPlainTextEdit *preview_{}, *dataOutput_{}, *historyOutput_{};
    QTableWidget* mappings_{};
};

CompareStudio::CompareStudio(SqlSession& source, SqlSession& destination, QWidget* parent)
    : QDialog(parent), source_(&source), destination_(&destination)
{
    setObjectName(QStringLiteral("compareStudio"));
    setWindowTitle(tr("Compare Studio"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(940, 710);
    auto* root = new QVBoxLayout(this);
    auto* endpoints = new QLabel(tr("Source %1 (%2) → Destination %3 (%4)").arg(source.profile().name,
        endpoint(source.profile()), destination.profile().name, endpoint(destination.profile())), this);
    endpoints->setTextFormat(Qt::PlainText);
    endpoints->setWordWrap(true);
    root->addWidget(endpoints);
    refresh_ = new QPushButton(tr("Capture live catalogs"), this);
    refresh_->setObjectName(QStringLiteral("compareRefresh"));
    root->addWidget(refresh_, 0, Qt::AlignLeft);

    auto* tabs = new QTabWidget(this);
    root->addWidget(tabs, 1);
    auto* schema = new QWidget(tabs);
    auto* schemaLayout = new QVBoxLayout(schema);
    tabs->addTab(schema, tr("Schema and deploy"));
    auto* mappingNote = new QLabel(tr("Map a destination column to its source name before applying a rename. Enter the quoted schema and table as shown in the preview."), schema);
    mappingNote->setWordWrap(true);
    schemaLayout->addWidget(mappingNote);
    mappings_ = new QTableWidget(0, 3, schema);
    mappings_->setObjectName(QStringLiteral("compareMappings"));
    mappings_->setHorizontalHeaderLabels({tr("Schema and table"), tr("Current destination column"), tr("Desired source column")});
    mappings_->setMaximumHeight(140);
    mappings_->setColumnWidth(0, 260);
    mappings_->setColumnWidth(1, 250);
    mappings_->setColumnWidth(2, 250);
    schemaLayout->addWidget(mappings_);
    auto* mappingActions = new QHBoxLayout;
    auto* addMapping = new QPushButton(tr("Add rename mapping"), schema);
    addMapping->setObjectName(QStringLiteral("compareAddMapping"));
    auto* removeMapping = new QPushButton(tr("Remove selected mapping"), schema);
    mappingActions->addWidget(addMapping);
    mappingActions->addWidget(removeMapping);
    mappingActions->addStretch();
    schemaLayout->addLayout(mappingActions);
    preview_ = new QPlainTextEdit(schema);
    preview_->setObjectName(QStringLiteral("comparePreview"));
    preview_->setReadOnly(true);
    schemaLayout->addWidget(preview_, 1);
    treatAsSeparate_ = new QCheckBox(tr("I reviewed unmatched additions and removals as separate objects"), schema);
    treatAsSeparate_->setObjectName(QStringLiteral("compareSeparate"));
    destructive_ = new QCheckBox(tr("Allow destructive changes in this preview"), schema);
    destructive_->setObjectName(QStringLiteral("compareDestructive"));
    apply_ = new QPushButton(tr("Apply reviewed DDL"), schema);
    apply_->setObjectName(QStringLiteral("compareApply"));
    schemaLayout->addWidget(treatAsSeparate_);
    schemaLayout->addWidget(destructive_);
    schemaLayout->addWidget(apply_, 0, Qt::AlignLeft);

    auto input = [](QWidget* parent, const char* name) {
        auto* field = new QLineEdit(parent);
        field->setObjectName(QString::fromLatin1(name));
        return field;
    };
    auto* data = new QWidget(tabs);
    auto* dataLayout = new QVBoxLayout(data);
    auto* form = new QFormLayout;
    dataLayout->addLayout(form);
    tabs->addTab(data, tr("Table data"));
    sourceTable_ = new QComboBox(data);
    sourceTable_->setObjectName(QStringLiteral("compareSourceTable"));
    destinationTable_ = new QComboBox(data);
    destinationTable_->setObjectName(QStringLiteral("compareDestinationTable"));
    sourceKeys_ = input(data, "compareSourceKeys");
    destinationKeys_ = input(data, "compareDestinationKeys");
    sourceFields_ = input(data, "compareSourceFields");
    destinationFields_ = input(data, "compareDestinationFields");
    rowLimit_ = input(data, "compareLimit");
    rowLimit_->setText(QStringLiteral("1000"));
    collation_ = new QComboBox(data);
    collation_->setObjectName(QStringLiteral("compareCollation"));
    collation_->addItems({tr("Binary UTF-8"), tr("Turkish case-insensitive (Windows NLS)")});
    form->addRow(tr("Source table"), sourceTable_);
    form->addRow(tr("Destination table"), destinationTable_);
    form->addRow(tr("Source key columns"), sourceKeys_);
    form->addRow(tr("Destination key columns"), destinationKeys_);
    form->addRow(tr("Source value columns"), sourceFields_);
    form->addRow(tr("Destination value columns"), destinationFields_);
    form->addRow(tr("Text comparison"), collation_);
    form->addRow(tr("Row limit per side (1–10,000)"), rowLimit_);
    sourceKeys_->setPlaceholderText(tr("Comma-separated names in corresponding order"));
    sourceFields_->setPlaceholderText(tr("Optional comma-separated names in corresponding order"));
    compare_ = new QPushButton(tr("Compare table data"), data);
    compare_->setObjectName(QStringLiteral("compareData"));
    dataLayout->addWidget(compare_, 0, Qt::AlignLeft);
    dataOutput_ = new QPlainTextEdit(data);
    dataOutput_->setObjectName(QStringLiteral("compareDataOutput"));
    dataOutput_->setReadOnly(true);
    dataLayout->addWidget(dataOutput_, 1);

    auto* history = new QWidget(tabs);
    auto* historyLayout = new QVBoxLayout(history);
    auto* historyForm = new QFormLayout;
    historyLayout->addLayout(historyForm);
    tabs->addTab(history, tr("Historical object"));
    sourceObject_ = input(history, "compareSourceObject");
    sourceRevision_ = input(history, "compareSourceRevision");
    destinationObject_ = input(history, "compareDestinationObject");
    destinationRevision_ = input(history, "compareDestinationRevision");
    historyForm->addRow(tr("Source qualified object"), sourceObject_);
    historyForm->addRow(tr("Source revision"), sourceRevision_);
    historyForm->addRow(tr("Destination qualified object"), destinationObject_);
    historyForm->addRow(tr("Destination revision"), destinationRevision_);
    historical_ = new QPushButton(tr("Compare saved definitions"), history);
    historical_->setObjectName(QStringLiteral("compareHistorical"));
    historyLayout->addWidget(historical_, 0, Qt::AlignLeft);
    historyOutput_ = new QPlainTextEdit(history);
    historyOutput_->setObjectName(QStringLiteral("compareHistoryOutput"));
    historyOutput_->setReadOnly(true);
    historyLayout->addWidget(historyOutput_, 1);

    message_ = new QLabel(this);
    message_->setObjectName(QStringLiteral("compareStatus"));
    message_->setTextFormat(Qt::PlainText);
    message_->setWordWrap(true);
    root->addWidget(message_);
    connect(refresh_, &QPushButton::clicked, this, &CompareStudio::refresh);
    connect(apply_, &QPushButton::clicked, this, &CompareStudio::apply);
    connect(compare_, &QPushButton::clicked, this, &CompareStudio::dataCompare);
    connect(historical_, &QPushButton::clicked, this, &CompareStudio::historyCompare);
    connect(addMapping, &QPushButton::clicked, this, [this] { mappings_->insertRow(mappings_->rowCount()); });
    connect(removeMapping, &QPushButton::clicked, this, [this] {
        if (mappings_->currentRow() < 0) return;
        mappings_->removeRow(mappings_->currentRow());
        rebuild();
    });
    connect(mappings_, &QTableWidget::cellChanged, this, [this] { rebuild(); });
    connect(destructive_, &QCheckBox::toggled, this, &CompareStudio::buttons);
    connect(treatAsSeparate_, &QCheckBox::toggled, this, &CompareStudio::buttons);
    const auto closed = [this] {
        captured_ = false;
        finish(tr("A worksheet was closed. Open two connected worksheets to compare again."));
    };
    connect(&source, &QObject::destroyed, this, closed);
    connect(&destination, &QObject::destroyed, this, closed);
    buttons();
}

void CompareStudio::buttons()
{
    const bool connected = source_ && destination_;
    refresh_->setEnabled(connected && !working_);
    mappings_->setEnabled(!working_);
    destructive_->setEnabled(!working_ && plan_.destructive);
    treatAsSeparate_->setEnabled(!working_ && plan_.ambiguous);
    apply_->setEnabled(connected && !working_ && captured_ && mappingError_.isEmpty() && !plan_.statements.isEmpty() &&
        (!plan_.destructive || destructive_->isChecked()) && (!plan_.ambiguous || treatAsSeparate_->isChecked()));
    compare_->setEnabled(connected && !working_ && captured_ && sourceTable_->count() && destinationTable_->count());
    historical_->setEnabled(connected && !working_ && source_->capabilities().value("history_v1").toBool() &&
        destination_->capabilities().value("history_v1").toBool());
}

bool CompareStudio::ready()
{
    if (working_) return false;
    if (!source_ || !destination_ || !source_->state().connected || !destination_->state().connected ||
        source_->busy() || destination_->busy() || source_->state().transactionActive || destination_->state().transactionActive)
    {
        finish(tr("Both worksheets must be connected and idle with no open transaction."));
        return false;
    }
    return true;
}

void CompareStudio::finish(const QString& message)
{
    working_ = false;
    message_->setText(message);
    buttons();
}

void CompareStudio::definitions(SqlSession& session, CatalogSnapshot& snapshot, const QJsonArray& objects,
    qsizetype at, std::function<void()> next)
{
    if (at >= objects.size())
    {
        next();
        return;
    }
    const auto object = objects.at(at).toObject();
    const auto qualified = names(object.value("qualified_name"));
    const auto target = sqlQualifiedIdentifier(qualified);
    const QPointer<CompareStudio> self(this);
    const QPointer<SqlSession> current(&session);
    if (!historyRead(session, target, snapshot.revision, [self, current, &snapshot, objects, at, next, object](const SqlReply& reply) {
        if (!self || !current) return;
        if (reply.error.code == SDP_OK && reply.data.value("object_id") == object.value("id") &&
            reply.data.value("revision").toString() == snapshot.revision && reply.data.value("name") == object.value("qualified_name"))
        {
            const auto value = reply.data.value("object").toObject();
            if (value.value("schema_version") == object.value("schema_version"))
                snapshot.definitions.insert(definitionKey(object), normalizedDefinition(value, snapshot.objects, object.value("kind").toInt()));
        }
        self->definitions(*current, snapshot, objects, at + 1, next);
    })) finish(tr("A worksheet became busy during the capture. Capture both catalogs again."));
}

void CompareStudio::capture(SqlSession& session, CatalogSnapshot& snapshot, std::function<void()> next)
{
    const QPointer<CompareStudio> self(this);
    const QPointer<SqlSession> current(&session);
    if (!session.refreshMetadata([self, current, &snapshot, next](const SqlReply& reply) {
        if (!self || !current) return;
        if (reply.error.code != SDP_OK)
        {
            self->finish(tr("Catalog capture failed. %1").arg(errorText(reply)));
            return;
        }
        snapshot = {reply.data.value("objects").toArray(), revision(reply.data), {}};
        QJsonArray inspect;
        constexpr int maximumDefinitions = 64;
        if (!snapshot.revision.isEmpty() && current->capabilities().value("history_v1").toBool())
        {
            for (const auto value : snapshot.objects)
            {
                const int kind = value.toObject().value("kind").toInt();
                if (inspect.size() >= maximumDefinitions) break;
                if (kind == viewKind || kind == routineKind || kind == ruleKind) inspect.push_back(value);
            }
        }
        self->definitions(*current, snapshot, inspect, 0, next);
    })) finish(tr("A worksheet became busy during the capture. Capture both catalogs again."));
}

void CompareStudio::refresh()
{
    if (!ready()) return;
    working_ = true;
    captured_ = false;
    destructive_->setChecked(false);
    treatAsSeparate_->setChecked(false);
    buttons();
    message_->setText(tr("Capturing source and destination catalog snapshots…"));
    capture(*source_, from_, [this] {
        if (!destination_) return;
        capture(*destination_, to_, [this] {
            captured_ = true;
            sourceTable_->clear();
            destinationTable_->clear();
            const auto tables = [](QComboBox& selector, const CatalogSnapshot& snapshot) {
                for (const auto& object : ofKind(snapshot.objects, tableKind))
                    if (object.value("permissions").toObject().value("select").toBool()) selector.addItem(key(object), object);
            };
            tables(*sourceTable_, from_);
            tables(*destinationTable_, to_);
            sourceRevision_->setText(from_.revision);
            destinationRevision_->setText(to_.revision);
            rebuild();
            finish(tr("Catalogs captured. Review the DDL and any manual items before applying."));
        });
    });
}

void CompareStudio::rebuild()
{
    if (!captured_) return;
    destructive_->setChecked(false);
    treatAsSeparate_->setChecked(false);
    ColumnRenames renames;
    mappingError_.clear();
    for (int row = 0; row < mappings_->rowCount(); ++row)
    {
        const auto cell = [&](int column) { return mappings_->item(row, column) ? mappings_->item(row, column)->text() : QString{}; };
        const auto table = cell(0).trimmed();
        const auto oldName = cell(1);
        const auto newName = cell(2);
        if (table.isEmpty() && oldName.isEmpty() && newName.isEmpty()) continue;
        const auto source = columnNames(objectNamed(from_.objects, table, tableKind));
        const auto target = columnNames(objectNamed(to_.objects, table, tableKind));
        auto& mapping = renames[table];
        if (!target.contains(oldName) || !source.contains(newName) || source.contains(oldName) || target.contains(newName) ||
            mapping.contains(oldName) || mapping.values().contains(newName))
        {
            mappingError_ = tr("Mapping row %1 must pair a unique destination-only column with a source-only column on the same table.").arg(row + 1);
            break;
        }
        mapping.insert(oldName, newName);
    }
    plan_ = makePlan(from_, to_, destination_->profile().database, renames);
    const auto unavailable = tr("visible catalog fingerprint only");
    QStringList text{tr("Source revision %1 · Destination revision %2").arg(from_.revision.isEmpty() ? unavailable : from_.revision,
        to_.revision.isEmpty() ? unavailable : to_.revision)};
    text << tr("The catalogs include objects visible to the connected accounts. Confirm the source account can see every object in scope.")
         << tr("Apply uses one destination transaction and holds its exclusive database lock. Runtime depends on data and has not been measured.")
         << tr("A failed statement rolls back the deployment. After commit, decommissioned data requires a verified backup or History recovery.");
    if (!mappingError_.isEmpty()) text << mappingError_;
    if (plan_.destructive) text << tr("Destructive changes can remove data or replace constraints/indexes.");
    if (plan_.ambiguous) text << tr("Unmatched additions and removals may be renames. Supply explicit column mappings or review them as separate objects. Table rename has no automatic DDL.");
    if (plan_.checksRows) text << tr("The Server may scan existing rows to validate this change.");
    if (!plan_.differences.isEmpty()) text << QString{} << tr("Schema differences") << plan_.differences;
    if (!plan_.manual.isEmpty()) text << QString{} << tr("Manual review · excluded from the generated plan") << plan_.manual;
    text << QString{} << tr("Destination DDL preview");
    text << (plan_.statements.isEmpty() ? tr("No automatic changes") : plan_.statements.join(QStringLiteral(";\n\n")) + u';');
    preview_->setPlainText(text.join(u'\n'));
    buttons();
}

void CompareStudio::apply()
{
    if (!apply_->isEnabled() || !ready()) return;
    auto warning = tr("Apply the reviewed DDL to %1 in one transaction? The destination's snapshot will be checked again under lock.")
        .arg(endpoint(destination_->profile()));
    if (!plan_.manual.isEmpty()) warning += u'\n' + tr("Items marked for manual review are outside this deployment.");
    if (QMessageBox::question(this, tr("Apply schema changes"), warning, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes || !ready()) return;
    working_ = true;
    buttons();
    message_->setText(tr("Verifying the destination snapshot under the transaction lock…"));
    const QPointer<CompareStudio> self(this);
    if (!destination_->transaction(SqlTransaction::begin, {}, [self](const SqlReply& begun) {
        if (!self || !self->destination_) return;
        if (begun.error.code != SDP_OK)
        {
            self->finish(errorText(begun));
            return;
        }
        if (!self->destination_->refreshMetadata([self](const SqlReply& current) {
            if (!self || !self->destination_) return;
            if (current.error.code != SDP_OK)
            {
                self->rollback(tr("Could not verify the destination. %1").arg(errorText(current)));
                return;
            }
            if (revision(current.data) != self->to_.revision || current.data.value("objects").toArray() != self->to_.objects)
            {
                self->rollback(tr("The destination changed after preview. Capture both catalogs before retrying."));
                return;
            }
            QVector<SqlStatement> statements;
            for (const auto& sql : self->plan_.statements) statements.push_back({sql});
            if (!self->destination_->execute(statements, {}, {}, {1, 30000}, {}, [self](const SqlReply& ran) {
                if (!self || !self->destination_) return;
                if (ran.error.code != SDP_OK)
                {
                    self->rollback(tr("Statement %1 failed. %2").arg(ran.statementIndex + 1).arg(errorText(ran)));
                    return;
                }
                if (!self->destination_->transaction(SqlTransaction::commit, {}, [self](const SqlReply& committed) {
                    if (!self) return;
                    if (committed.error.code != SDP_OK)
                    {
                        self->rollback(tr("Commit could not be confirmed. %1").arg(errorText(committed)));
                        return;
                    }
                    self->captured_ = false;
                    self->finish(tr("Plan committed. Capture fresh catalogs to verify the destination."));
                })) self->rollback(tr("Commit could not start."));
            })) self->rollback(tr("Deployment could not start."));
        })) self->rollback(tr("Destination verification could not start."));
    })) finish(tr("The destination transaction could not start."));
}

void CompareStudio::rollback(const QString& reason)
{
    captured_ = false;
    const QPointer<CompareStudio> self(this);
    if (destination_ && destination_->state().transactionActive &&
        destination_->transaction(SqlTransaction::rollback, {}, [self, reason](const SqlReply& rolled) {
            if (!self) return;
            self->finish(rolled.error.code == SDP_OK ? reason + u'\n' + tr("The transaction was rolled back.") :
                reason + u'\n' + tr("Rollback could not be confirmed. %1").arg(errorText(rolled)));
        })) return;
    finish(reason + u'\n' + tr("Inspect the destination transaction and its outcome before retrying."));
}

void CompareStudio::compareRead(SqlSession& session, const QString& sql, int keys, int columns, int limit, bool turkish,
    std::function<void(ComparedRows)> done)
{
    const QPointer<CompareStudio> self(this);
    const auto result = std::make_shared<ComparedRows>();
    if (!session.execute({{sql}}, {}, {}, {limit + 1, 20000}, [result, keys, columns, limit, turkish](const SqlEvent& event) {
        result->accept(event, keys, columns, limit, turkish);
    }, [self, result, done](const SqlReply& reply) {
        if (!self) return;
        if (reply.error.code != SDP_OK) result->error = errorText(reply);
        if (reply.resultsTruncated && result->error.isEmpty()) result->error = tr("The Server did not return a complete result within the limit.");
        done(std::move(*result));
    })) finish(tr("The row stream could not start."));
}

void CompareStudio::dataCompare()
{
    if (!ready() || !captured_) return;
    const auto fromKey = selectedColumns(sourceKeys_->text());
    const auto toKey = selectedColumns(destinationKeys_->text());
    const auto fromValue = selectedColumns(sourceFields_->text());
    const auto toValue = selectedColumns(destinationFields_->text());
    const auto from = selectSql(sourceTable_->currentData().toJsonObject(), fromKey + fromValue);
    const auto to = selectSql(destinationTable_->currentData().toJsonObject(), toKey + toValue);
    bool validLimit = false;
    const auto limit = rowLimit_->text().toInt(&validLimit);
    if (fromKey.isEmpty() || fromKey.size() != toKey.size() || fromValue.size() != toValue.size() ||
        from.isEmpty() || to.isEmpty() || !validLimit || limit < 1 || limit > maximumRows)
    {
        finish(tr("Select matching key and value columns on both sides and a row limit from 1 to 10,000."));
        return;
    }
    dataOutput_->clear();
    working_ = true;
    buttons();
    message_->setText(tr("Reading two bounded table snapshots…"));
    const auto keys = static_cast<int>(fromKey.size());
    const auto width = keys + static_cast<int>(fromValue.size());
    const auto turkish = collation_->currentIndex() == 1;
    compareRead(*source_, from, keys, width, limit, turkish, [this, to, keys, width, limit, turkish](ComparedRows left) {
        if (!left.error.isEmpty())
        {
            finish(tr("Source comparison could not complete. %1").arg(left.error));
            return;
        }
        if (!destination_) return;
        compareRead(*destination_, to, keys, width, limit, turkish, [this, left = std::move(left)](ComparedRows right) {
            if (!right.error.isEmpty())
            {
                finish(tr("Destination comparison could not complete. %1").arg(right.error));
                return;
            }
            qsizetype sourceOnly = 0, destinationOnly = 0, changed = 0;
            for (const auto& [key, fields] : left.values)
            {
                const auto other = right.values.find(key);
                if (other == right.values.end()) ++sourceOnly;
                else if (other->second != fields) ++changed;
            }
            for (const auto& [key, fields] : right.values)
                if (!left.values.contains(key)) ++destinationOnly;
            dataOutput_->setPlainText(tr("Source rows %1\nDestination rows %2\nSource-only keys %3\nDestination-only keys %4\nShared keys with different values %5\n\nEach table was streamed in its own read snapshot.")
                .arg(left.values.size()).arg(right.values.size()).arg(sourceOnly).arg(destinationOnly).arg(changed));
            finish(sourceOnly || destinationOnly || changed ? tr("Data differences found.") : tr("The compared data matches."));
        });
    });
}

void CompareStudio::historyCompare()
{
    if (!ready()) return;
    const auto from = sourceObject_->text().trimmed();
    const auto to = destinationObject_->text().trimmed();
    const auto first = sourceRevision_->text().trimmed();
    const auto second = destinationRevision_->text().trimmed();
    const auto validRevision = [](const QString& value) {
        bool ok = false;
        const auto number = value.toULongLong(&ok);
        return ok && number && QString::number(number) == value;
    };
    if (from.isEmpty() || to.isEmpty() || !validRevision(first) || !validRevision(second))
    {
        finish(tr("Enter a qualified object name and a positive committed revision for each side."));
        return;
    }
    working_ = true;
    historyOutput_->clear();
    buttons();
    const QPointer<CompareStudio> self(this);
    if (!historyRead(*source_, from, first, [self, to, first, second](const SqlReply& left) {
        if (!self || !self->destination_) return;
        if (left.error.code != SDP_OK || left.data.value("revision").toString() != first)
        {
            self->finish(tr("Could not read the selected source revision. %1").arg(errorText(left)));
            return;
        }
        if (!historyRead(*self->destination_, to, second, [self, left, first, second](const SqlReply& right) {
            if (!self) return;
            if (right.error.code != SDP_OK || right.data.value("revision").toString() != second)
            {
                self->finish(tr("Could not read the selected destination revision. %1").arg(errorText(right)));
                return;
            }
            auto from = left.data.value("object").toObject();
            auto to = right.data.value("object").toObject();
            from.remove("parent_id");
            to.remove("parent_id");
            from.remove("schema_version");
            to.remove("schema_version");
            self->historyOutput_->setPlainText(tr("Source revision %1\n%2\nDestination revision %3\n%4")
                .arg(first, QString::fromUtf8(QJsonDocument(from).toJson(QJsonDocument::Indented)),
                    second, QString::fromUtf8(QJsonDocument(to).toJson(QJsonDocument::Indented))));
            self->finish(from == to ? tr("The visible saved definitions match.") : tr("The visible saved definitions differ."));
        })) self->finish(tr("The destination History request could not start."));
    })) finish(tr("The source History request could not start."));
}
}

QDialog* showCompareStudio(SqlSession& source, SqlSession& destination, QWidget* parent)
{
    auto* studio = new CompareStudio(source, destination, parent);
    studio->show();
    return studio;
}
}
