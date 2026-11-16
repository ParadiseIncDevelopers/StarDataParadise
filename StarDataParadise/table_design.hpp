#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QStringList>

#include <functional>

class QWidget;

namespace wb
{
class SqlSession;

struct DesignerColumn
{
    enum class Value { none, expression, identity, keep };

    int originalOrdinal = -1;
    QString name;
    QString type = QStringLiteral("TEXT");
    bool nullable = true;
    Value value = Value::none;
    QString defaultSql;
};

struct DesignerConstraint
{
    QString name;
    QString kind;
    QStringList columns;
    QStringList referenceTable;
    QStringList referenceColumns;
    QString checkSql;
};

struct DesignerIndex
{
    QString name;
    QStringList columns;
    QString collation;
    bool unique = false;
};

struct TableDesign
{
    QStringList target;
    QList<DesignerColumn> columns;
    QList<DesignerConstraint> addedConstraints;
    QList<int> droppedConstraints;
    QList<DesignerIndex> addedIndexes;
    QStringList droppedIndexIds;
};

struct TableDdlPlan
{
    QStringList statements;
    QStringList affectedObjects;
    QString error;
    bool destructive = false;
    bool checksRows = false;
};

QString sqlQualifiedIdentifier(const QStringList& names);
QList<DesignerColumn> readDesignerColumns(const QJsonObject& table);
TableDdlPlan planTableDesign(const QJsonArray& catalog, const QJsonObject& original, const TableDesign& design);
void showTableDesigner(SqlSession& session, const QJsonArray& catalog,
    const QJsonObject& table, QWidget* parent, std::function<void()> refresh);
void showSchemaDictionary(SqlSession& session, const QJsonArray& catalog,
    QWidget* parent, std::function<void(const QJsonObject&)> editTable);
}
