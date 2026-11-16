#pragma once

#include <QByteArrayView>
#include <QJsonArray>
#include <QList>
#include <QStringList>

#include <functional>

namespace wb::migration
{
enum class ExternalProvider { postgresql, mysql };

struct ExternalSource
{
    ExternalProvider provider = ExternalProvider::postgresql;
    QString host, database, schema, username, caFile, driverFile;
    quint16 port{};
    QStringList selectedTables;
    QString helperFile;
    bool allowInsecureLoopback = false;
};

struct ExternalColumn
{
    QString name, sourceType, type, defaultSql, collation, encoding;
    bool nullable = true;
    bool generated = false;
};

struct ExternalForeignKey
{
    QString name;
    QStringList columns, referenceTable, referenceColumns;
    QString onUpdate, onDelete;
};

struct ExternalTable
{
    QString name;
    QList<ExternalColumn> columns;
    QStringList primaryKey;
    QList<ExternalForeignKey> foreignKeys;
    QJsonArray rows;
    QStringList unsupported;
};

struct ExternalSnapshot
{
    QList<ExternalTable> tables;
    QString encoding, timeZone;
    QStringList unsupported;
    QString error;
};

using ExternalProgress = std::function<bool(qsizetype rows, qsizetype bytes)>;

ExternalSnapshot readExternalSnapshot(const ExternalSource& source, QByteArrayView password,
    const ExternalProgress& progress = {});
}
