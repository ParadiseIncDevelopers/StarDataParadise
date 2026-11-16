#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

namespace wb::transfer
{
enum class Format { csv, json, sql };

struct Options
{
    QChar delimiter = ',';
    QChar decimal = '.';
    QString nullMarker = QStringLiteral("NULL");
    QString datePattern = QStringLiteral("yyyy-MM-dd");
    bool spreadsheetEscaped = false;
};

struct Row
{
    int source{};
    QJsonArray values;
};

struct Parsed
{
    QVector<QJsonObject> columns;
    QVector<Row> rows;
    QStringList rejectionReport;
    int rejected{};
    QString error;
};

inline constexpr qsizetype maximumFileBytes = 8 * 1024 * 1024;
inline constexpr int maximumRows = 5000;

QString identifier(QString value);
QString relation(const QJsonObject& table);
QJsonObject parameterType(const QJsonObject& column);
Parsed decode(Format format, QStringView text, const QJsonArray& available, const Options& options);
QString encode(Format format, const QJsonObject& table, const QJsonArray& columns,
    const QVector<QJsonArray>& rows, const Options& options, QString& error);
}
