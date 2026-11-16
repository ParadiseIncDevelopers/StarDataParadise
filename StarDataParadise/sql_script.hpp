#pragma once

#include <QList>
#include <QString>
#include <QStringView>

namespace wb
{
enum class SqlScope { statement, selection, script };

struct SqlCommand
{
    QString text;
    qsizetype offset{};
};

struct SqlBatch
{
    QList<SqlCommand> statements;
    bool incomplete{};
    qsizetype remainder_offset{};
};

enum class SqlLexState { normal, comment, literal, quoted_name };
enum class SqlTokenKind { word, whitespace, comment, literal, quoted_name, symbol };

struct SqlToken
{
    SqlTokenKind kind;
    qsizetype offset;
    qsizetype length;
};

QList<SqlToken> sql_tokens(QStringView text, SqlLexState& state);
SqlBatch split_sql(QStringView text);
}
