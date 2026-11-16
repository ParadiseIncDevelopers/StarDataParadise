#pragma once

#include "migration_source.hpp"

#include <QJsonObject>
#include <map>

namespace wb::migration
{
using KeyedRows = std::map<QByteArray, QJsonArray>;

struct TargetTable
{
    QString name;
    QString sql;
    QString select;
    QString insert;
    QVector<int> key;
    QVector<QJsonObject> types;
    KeyedRows rows;
    QString checksum;
};

struct TargetPlan
{
    QList<TargetTable> tables;
    QString ownerTable;
    QStringList diagnostics;
    QString fingerprint;
    qsizetype rowCount{};
    QJsonObject report;
    QString error;
};

TargetPlan planImport(const ExternalSource& source, const ExternalSnapshot& snapshot, const QString& prefix);
QString targetName(const QString& item);
QByteArray keyOf(const QJsonArray& row, const QVector<int>& key);
QString checksum(const KeyedRows& rows);
}
