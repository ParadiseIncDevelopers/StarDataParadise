#include "table_codec.hpp"
#include "sql_script.hpp"

#include <QCoreApplication>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, const char* reason)
{
    if (!condition) throw std::runtime_error(reason);
}

QJsonObject col(const char* name, const char* type, bool nullable = false)
{
    return {{"name", name}, {"type", type}, {"nullable", nullable}};
}

void smoke()
{
    using namespace wb::transfer;
    QJsonObject amount = col("amount", "DECIMAL(38,9)");
    amount["precision"] = 38;
    amount["scale"] = 9;
    const QJsonArray columns{col("id", "BIGINT"), col("text", "TEXT", true), amount,
        col("binary", "BLOB", true), col("calendar", "DATE"), col("clock", "TIME")};
    const QJsonObject table{{"qualified_name", QJsonArray{"main", "public", "a\"b"}}};
    const QJsonObject null{{"t", "null"}};
    const auto fixed = QJsonObject{{"t", "decimal"}, {"unscaled", "-123456789000100000999"}, {"scale", 9}};
    const auto date = QJsonObject{{"t", "text"}, {"v", "2026-11-15"}};
    const auto time = QJsonObject{{"t", "text"}, {"v", "08:10:12.123456789"}};
    const QVector<QJsonArray> rows{
        QJsonArray{QJsonObject{{"t", "i64"}, {"v", "9007199254740993"}},
            QJsonObject{{"t", "text"}, {"v", QStringLiteral("=HYPERLINK(\"örnek\")\r\nİstanbul")}},
            fixed, QJsonObject{{"t", "binary"}, {"hex", "00ff1a"}}, date, time},
        QJsonArray{QJsonObject{{"t", "i64"}, {"v", "2"}},
            QJsonObject{{"t", "text"}, {"v", "NULL"}}, fixed, null, date, time},
        QJsonArray{QJsonObject{{"t", "i64"}, {"v", "3"}},
            null, fixed, QJsonObject{{"t", "binary"}, {"hex", ""}}, date, time}
    };

    Options settings{';', ',', "NULL", "dd.MM.yyyy", true};
    for (const auto format : {Format::csv, Format::json})
    {
        QString error;
        const auto source = encode(format, table, columns, rows, settings, error);
        require(error.isEmpty() && !source.isEmpty(), "encoding failed");
        if (format == Format::csv)
            require(source.contains("\"'=HYPERLINK") && source.contains("15.11.2026"),
                "CSV formula escaping and date encoding");
        const auto decoded = decode(format, source, columns, settings);
        require(decoded.error.isEmpty() && decoded.rejected == 0 && decoded.rows.size() == rows.size(),
            "decoded row count and schema");
        for (qsizetype i = 0; i < rows.size(); ++i)
            require(decoded.rows.at(i).values == rows.at(i), "exact value round trip");
    }
    const auto rejects = decode(Format::csv, QStringLiteral("id;text\n2;ok\nbad;wrong\n3;\"NULL\"\n"), columns, settings);
    require(rejects.error.isEmpty() && rejects.rejected == 1 && rejects.rows.size() == 2 &&
        rejects.rejectionReport.first().contains("3"), "CSV row reject and line number");
    const auto malformed = decode(Format::csv, QStringLiteral("id;text\n3;\"missing"), columns, settings);
    require(!malformed.error.isEmpty(), "unclosed CSV field");
    QString error;
    (void)encode(Format::sql, table, columns, rows, settings, error);
    require(!error.isEmpty(), "unsupported binary SQL literal must fail closed");
    const QJsonArray textColumn{col("text", "TEXT")};
    const auto sql = encode(Format::sql, table, textColumn, {QJsonArray{QJsonObject{{"t", "text"},
        {"v", "O'Neil;DROP TABLE harmless;--"}}}}, settings, error);
    require(error.isEmpty() && sql.contains("\"a\"\"b\"") && sql.contains("O''Neil;") &&
        wb::split_sql(sql).statements.size() == 1, "SQL quoting preserved a single safe INSERT");
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        smoke();
        std::cout << "Typed CSV/JSON, formula escaping, SQL quoting and reject reporting passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
