#include "table_codec.hpp"

#include <QDate>
#include <QObject>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

namespace wb::transfer
{
namespace
{
struct Cell
{
    QString text;
    bool quoted{};
};
struct CsvRow
{
    int line{};
    QVector<Cell> cells;
};

bool validOptions(const Options& options)
{
    static const QRegularExpression safeNull(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]{0,31}$"));
    return (options.delimiter == ',' || options.delimiter == ';' || options.delimiter == '\t') &&
        (options.decimal == '.' || options.decimal == ',') &&
        (options.datePattern == "yyyy-MM-dd" || options.datePattern == "dd.MM.yyyy") &&
        safeNull.match(options.nullMarker).hasMatch();
}

QVector<CsvRow> csv(QStringView text, QChar delimiter, QString& error)
{
    enum class State { ready, unquoted, quoted, closed };
    State state{State::ready};
    QVector<CsvRow> rows;
    CsvRow row{1};
    Cell cell;
    int line = 1;
    const auto field = [&] {
        row.cells.push_back(std::move(cell));
        cell = {};
        state = State::ready;
    };
    const auto end = [&] {
        field();
        rows.push_back(std::move(row));
        row = {line + 1};
    };
    for (qsizetype i = 0; i < text.size(); ++i)
    {
        const auto ch = text.at(i);
        if (state == State::quoted)
        {
            if (ch == '"' && i + 1 < text.size() && text.at(i + 1) == '"')
            { cell.text += ch; ++i; }
            else if (ch == '"') state = State::closed;
            else { cell.text += ch; if (ch == '\n') ++line; }
            continue;
        }
        if (ch == delimiter) field();
        else if (ch == '\n' || ch == '\r')
        {
            if (ch == '\r' && i + 1 < text.size() && text.at(i + 1) == '\n') ++i;
            end();
            ++line;
        }
        else if (ch == '"' && state == State::ready) { cell.quoted = true; state = State::quoted; }
        else if (state == State::closed || ch == '"')
        {
            error = QObject::tr("Invalid CSV quoting at line %1.").arg(line);
            return {};
        }
        else { cell.text += ch; state = State::unquoted; }
        if (rows.size() > maximumRows + 1 || row.cells.size() > 256)
        {
            error = QObject::tr("The CSV exceeds the file row or column limit.");
            return {};
        }
    }
    if (state == State::quoted)
    {
        error = QObject::tr("CSV ends inside a quoted field.");
        return {};
    }
    if (state != State::ready || !row.cells.isEmpty()) end();
    return rows;
}

QString spreadsheetText(QString text)
{
    const auto visible = text.trimmed();
    const bool formula = !visible.isEmpty() && QStringLiteral("=+-@").contains(visible.front());
    if (formula || text.startsWith('\'') || text.startsWith('\t') ||
        text.startsWith('\r') || text.startsWith('\n'))
        text.prepend('\'');
    return text;
}

QJsonObject decimalValue(QString text, const QJsonObject& type, const Options& options)
{
    if (options.decimal == ',')
    {
        if (text.contains('.')) return {};
        text.replace(',', '.');
    }
    static const QRegularExpression decimal(QStringLiteral(R"(^(-?)([0-9]+)(?:\.([0-9]+))?$)"));
    const auto match = decimal.match(text);
    if (!match.hasMatch()) return {};
    const auto declared = type.value("scale").toInt(-1);
    const auto precision = type.value("precision").toInt();
    auto fraction = match.captured(3);
    if (declared < 0 || declared > precision || precision < 1 || precision > 38 || fraction.size() > declared) return {};
    fraction += QString(declared - fraction.size(), '0');
    auto digit = match.captured(2) + fraction;
    while (digit.size() > 1 && digit.startsWith('0')) digit.remove(0, 1);
    if (digit.size() > precision) return {};
    if (digit != "0" && !match.captured(1).isEmpty()) digit.prepend('-');
    return {{"t", "decimal"}, {"unscaled", digit}, {"scale", declared}};
}

QJsonObject csvValue(const Cell& cell, const QJsonObject& type, const Options& options)
{
    auto text = cell.text;
    if (!cell.quoted && text == options.nullMarker) return {{"t", "null"}};
    const auto name = type.value("type").toString();
    if (name == "TEXT")
    {
        if (options.spreadsheetEscaped && text.startsWith('\'') && spreadsheetText(text.mid(1)) == text)
            text.remove(0, 1);
        return {{"t", "text"}, {"v", text}};
    }
    text = text.trimmed();
    if (name == "BIGINT")
    {
        bool ok{};
        const auto value = text.toLongLong(&ok);
        return ok ? QJsonObject{{"t", "i64"}, {"v", QString::number(value)}} : QJsonObject{};
    }
    if (name == "DECIMAL") return decimalValue(text, type, options);
    if (name == "BOOLEAN")
    {
        if (text.compare("true", Qt::CaseInsensitive) == 0) return {{"t", "bool"}, {"v", true}};
        if (text.compare("false", Qt::CaseInsensitive) == 0) return {{"t", "bool"}, {"v", false}};
        return {};
    }
    if (name == "BLOB")
    {
        if (text.startsWith("0x", Qt::CaseInsensitive)) text.remove(0, 2);
        static const QRegularExpression hex(QStringLiteral("^(?:[0-9A-Fa-f]{2})*$"));
        if (!hex.match(text).hasMatch()) return {};
        return {{"t", "binary"}, {"hex", text.toLower()}};
    }
    if (name == "DATE")
    {
        const auto date = QDate::fromString(text, options.datePattern);
        if (date.isValid()) return {{"t", "text"}, {"v", date.toString(Qt::ISODate)}};
        return {};
    }
    if (name == "TIME")
    {
        static const QRegularExpression clock(QStringLiteral("^(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9](?:\\.[0-9]{1,9})?$"));
        if (clock.match(text).hasMatch()) return {{"t", "text"}, {"v", text}};
        return {};
    }
    if (name == "TIMESTAMP WITH TIME ZONE" || name == "RECORD" || name == "COLLECTION")
        return QJsonDocument::fromJson(text.toUtf8()).object();
    return {};
}

bool matches(const QJsonObject& value, const QJsonObject& type)
{
    const auto name = type.value("type").toString();
    const auto tag = value.value("t").toString();
    if (tag == "null") return value.size() == 1;
    if (tag == "i64" && name == "BIGINT")
    {
        bool ok{};
        const auto text = value.value("v").toString();
        const auto number = text.toLongLong(&ok);
        return ok && QString::number(number) == text;
    }
    if (tag == "text") return (name == "TEXT" || name == "DATE" || name == "TIME") && value.value("v").isString();
    if (tag == "bool") return name == "BOOLEAN" && value.value("v").isBool();
    if (tag == "binary")
    {
        static const QRegularExpression hex(QStringLiteral("^(?:[0-9A-Fa-f]{2})*$"));
        return name == "BLOB" && value.value("hex").isString() && hex.match(value.value("hex").toString()).hasMatch();
    }
    if (tag == "decimal")
    {
        const auto scale = value.value("scale").toInt(-1);
        static const QRegularExpression scaled(QStringLiteral("^-?(?:0|[1-9][0-9]*)$"));
        return name == "DECIMAL" && scale >= 0 && scale <= 38 &&
            scaled.match(value.value("unscaled").toString()).hasMatch();
    }
    return (tag == "timestamp" && name == "TIMESTAMP WITH TIME ZONE") ||
        (tag == "record" && name == "RECORD") || (tag == "collection" && name == "COLLECTION");
}

void reject(Parsed& result, int row, QString message)
{
    ++result.rejected;
    if (result.rejectionReport.size() < 100)
        result.rejectionReport.push_back(QObject::tr("Row %1 · %2").arg(row).arg(message));
}

QString quoteCsv(QString value)
{
    value.replace('"', "\"\"");
    return '"' + value + '"';
}

QString valueText(const QJsonObject& value, const Options& options)
{
    const auto tag = value.value("t").toString();
    if (tag == "binary") return "0x" + value.value("hex").toString();
    if (tag == "bool") return value.value("v").toBool() ? "true" : "false";
    if (tag == "text" || tag == "i64") return value.value("v").toString();
    if (tag == "decimal")
    {
        auto digits = value.value("unscaled").toString();
        const bool negative = digits.startsWith('-');
        if (negative) digits.remove(0, 1);
        const auto scale = value.value("scale").toInt();
        while (digits.size() <= scale) digits.prepend('0');
        if (scale) digits.insert(digits.size() - scale, options.decimal);
        return negative ? "-" + digits : digits;
    }
    return QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact));
}
}

QString identifier(QString value)
{
    value.replace('"', "\"\"");
    return '"' + value + '"';
}

QString relation(const QJsonObject& table)
{
    QStringList parts;
    for (const auto& part : table.value("qualified_name").toArray())
    {
        if (!part.isString() || part.toString().isEmpty()) return {};
        parts.push_back(identifier(part.toString()));
    }
    return parts.size() == 3 ? parts.join('.') : QString{};
}

QJsonObject parameterType(const QJsonObject& column)
{
    auto type = column.value("type").toString();
    if (!type.startsWith("DECIMAL")) return {{"type", type}};
    return {{"type", "DECIMAL"}, {"precision", column.value("precision")}, {"scale", column.value("scale")}};
}


Parsed decode(Format format, QStringView text, const QJsonArray& available, const Options& options)
{
    Parsed result;
    if (text.size() > maximumFileBytes || (format == Format::csv && !validOptions(options)))
    {
        result.error = QObject::tr("Input exceeds the file limit or contains invalid format options.");
        return result;
    }
    if (format == Format::sql)
    {
        result.error = QObject::tr("SQL files open in the worksheet for review and execution.");
        return result;
    }
    QVector<CsvRow> csvRows;
    QJsonArray rawRows;
    QStringList headers;
    if (format == Format::csv)
    {
        csvRows = csv(text, options.delimiter, result.error);
        if (!result.error.isEmpty()) return result;
        if (csvRows.isEmpty())
        {
            result.error = QObject::tr("CSV needs a header row.");
            return result;
        }
        const auto header = csvRows.takeFirst();
        for (const auto& cell : header.cells)
        {
            auto name = cell.text;
            if (options.spreadsheetEscaped && name.startsWith('\'') && spreadsheetText(name.mid(1)) == name)
                name.remove(0, 1);
            headers.append(name);
        }
    }
    else
    {
        QJsonParseError parse;
        const auto file = QJsonDocument::fromJson(text.toString().toUtf8(), &parse).object();
        if (parse.error != QJsonParseError::NoError || file.value("format") != "stardataparadise.table.v1" ||
            !file.value("columns").isArray() || !file.value("rows").isArray())
        {
            result.error = QObject::tr("Invalid StarDataParadise typed JSON table.");
            return result;
        }
        for (const auto& header : file.value("columns").toArray()) headers.append(header.toString());
        rawRows = file.value("rows").toArray();
    }
    if (headers.isEmpty() || headers.size() > 64 || csvRows.size() > maximumRows || rawRows.size() > maximumRows)
    {
        result.error = QObject::tr("The input exceeds the 64-column or 5,000-row transfer limit.");
        return result;
    }
    QSet<QString> used;
    for (const auto& name : headers)
    {
        QJsonObject mapped;
        for (const auto& option : available)
            if (option.toObject().value("name").toString() == name) mapped = option.toObject();
        if (mapped.isEmpty() || used.contains(name) || name.isEmpty())
        {
            result.error = QObject::tr("Every input header must map to a distinct table column.");
            return result;
        }
        used.insert(name);
        result.columns.push_back(std::move(mapped));
    }

    const auto count = format == Format::csv ? csvRows.size() : rawRows.size();
    for (qsizetype row = 0; row < count; ++row)
    {
        const auto line = format == Format::csv ? csvRows.at(row).line : static_cast<int>(row + 1);
        const auto cells = format == Format::json ? rawRows.at(row).toArray() : QJsonArray{};
        const auto width = format == Format::csv ? csvRows.at(row).cells.size() : cells.size();
        if (width != headers.size())
        {
            reject(result, line, QObject::tr("Number of fields differs from the header."));
            continue;
        }
        QJsonArray values;
        for (qsizetype column = 0; column < width; ++column)
        {
            const auto& target = result.columns.at(column);
            const auto type = parameterType(target);
            const auto field = format == Format::csv ? csvValue(csvRows.at(row).cells.at(column), type, options)
                : cells.at(column).toObject();
            if (!matches(field, type) ||
                (field.value("t") == "null" && !target.value("nullable").toBool()))
            {
                reject(result, line, QObject::tr("Invalid value for column %1.").arg(target.value("name").toString()));
                break;
            }
            values.append(field);
        }
        if (values.size() == width) result.rows.push_back({line, std::move(values)});
    }
    return result;
}

QString encode(Format format, const QJsonObject& table, const QJsonArray& columns,
    const QVector<QJsonArray>& rows, const Options& options, QString& error)
{
    error.clear();
    if (columns.isEmpty() || columns.size() > 256 || rows.size() > maximumRows ||
        (format == Format::csv && !validOptions(options)))
    {
        error = QObject::tr("Invalid transfer size or format options.");
        return {};
    }
    QStringList headers;
    QJsonArray rawRows;
    QString text;
    for (const auto& column : columns) headers.append(column.toObject().value("name").toString());
    const auto target = relation(table);
    if (format == Format::sql && target.isEmpty())
    {
        error = QObject::tr("SQL export needs a qualified table name.");
        return {};
    }
    if (format == Format::csv)
    {
        QStringList escaped;
        for (const auto& name : headers) escaped.append(quoteCsv(spreadsheetText(name)));
        text = escaped.join(options.delimiter) + "\r\n";
    }
    QStringList identifiers;
    if (format == Format::sql)
        for (const auto& name : headers) identifiers.push_back(identifier(name));

    for (const auto& row : rows)
    {
        if (row.size() != columns.size()) { error = QObject::tr("Server returned an inconsistent row."); return {}; }
        QStringList cells;
        for (qsizetype i = 0; i < row.size(); ++i)
        {
            const auto field = row.at(i).toObject();
            const auto type = parameterType(columns.at(i).toObject());
            const auto schema = type.value("type").toString();
            if (!matches(field, type))
            {
                error = QObject::tr("Server returned a value with an incompatible type.");
                return {};
            }
            if (format == Format::json) continue;
            if (field.value("t") == "null")
            {
                cells.append(format == Format::csv ? options.nullMarker : QStringLiteral("NULL"));
                continue;
            }
            if (format == Format::csv)
            {
                auto cell = valueText(field, options);
                if (schema == "DATE") cell = QDate::fromString(cell, Qt::ISODate).toString(options.datePattern);
                if (schema == "TEXT") cell = spreadsheetText(cell);
                cells.append(quoteCsv(cell));
                continue;
            }
            if (schema == "BLOB" || schema == "TIMESTAMP WITH TIME ZONE" || schema == "RECORD" || schema == "COLLECTION")
            {
                error = QObject::tr("Use typed JSON or CSV to export %1 values.").arg(schema);
                return {};
            }
            auto literal = valueText(field, Options{});
            if (literal.contains(QChar(u'\0')))
            {
                error = QObject::tr("Use typed JSON to export text with embedded NUL.");
                return {};
            }
            if (schema == "TEXT" || schema == "DATE" || schema == "TIME")
            {
                literal.replace('\'', "''");
                literal = '\'' + literal + '\'';
                if (schema != "TEXT") literal = schema + " " + literal;
            }
            cells.append(literal);
        }
        if (format == Format::json) rawRows.append(row);
        else if (format == Format::csv) text += cells.join(options.delimiter) + "\r\n";
        else text += "INSERT INTO " + target + " (" + identifiers.join(", ") + ") VALUES (" + cells.join(", ") + ");\n";
        if (text.size() > maximumFileBytes)
        {
            error = QObject::tr("The export exceeds the 8 MiB file limit.");
            return {};
        }
    }
    if (format == Format::json)
    {
        const auto data = QJsonDocument(QJsonObject{{"format", "stardataparadise.table.v1"},
            {"columns", QJsonArray::fromStringList(headers)}, {"rows", rawRows}}).toJson(QJsonDocument::Compact);
        if (data.size() > maximumFileBytes) error = QObject::tr("The export exceeds the 8 MiB file limit.");
        else text = QString::fromUtf8(data);
    }
    if (error.isEmpty() && text.toUtf8().size() > maximumFileBytes)
    {
        error = QObject::tr("The export exceeds the 8 MiB file limit.");
        return {};
    }
    return text;
}
}
