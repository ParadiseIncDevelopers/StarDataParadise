#include "sql_script.hpp"

#include <QStringList>

namespace wb
{
namespace
{
bool word_character(QChar ch)
{
    return ch.isLetterOrNumber() || ch == u'_';
}

struct BlockBoundary
{
    QStringList prefix;
    QStringList blocks;
    QString closed;
    bool procedural{};
    bool begun{};

    void word(const QString& value)
    {
        if (prefix.size() < 4) prefix.push_back(value);
        if (!procedural)
        {
            if (prefix.size() == 1)
                procedural = value == u"BEGIN" || value == u"DECLARE";
            else if (prefix.front() == u"CREATE" && (prefix.size() == 2 ||
                (prefix.size() == 4 && prefix[1] == u"OR" && prefix[2] == u"REPLACE")))
                procedural = value == u"FUNCTION" || value == u"PROCEDURE" || value == u"TRIGGER";
        }
        else if (prefix.size() == 2 && prefix.front() == u"BEGIN" &&
            (value == u"WORK" || value == u"TRANSACTION"))
        {
            procedural = false;
            blocks.clear();
        }

        if (!procedural) return;
        if (!closed.isEmpty())
        {
            const bool suffix = closed == value && value != u"BEGIN";
            closed.clear();
            if (suffix) return;
        }
        if (value == u"BEGIN")
        {
            begun = true;
            blocks.push_back(value);
        }
        else if (begun && (value == u"IF" || value == u"CASE" || value == u"LOOP"))
            blocks.push_back(value);
        else if (value == u"END" && !blocks.isEmpty())
            closed = blocks.takeLast();
    }

    bool complete() const
    {
        return !procedural || (begun && blocks.isEmpty()) ||
            (prefix.size() == 1 && prefix.front() == u"BEGIN");
    }
};
}

QList<SqlToken> sql_tokens(QStringView text, SqlLexState& state)
{
    QList<SqlToken> tokens;
    for (qsizetype position = 0; position < text.size();)
    {
        const qsizetype start = position;
        const auto current = text[position];
        const auto remaining = text.sliced(position);
        SqlTokenKind kind;
        if (state == SqlLexState::comment || (state == SqlLexState::normal && remaining.startsWith(u"/*")))
        {
            if (state == SqlLexState::normal) position += 2;
            const qsizetype end = text.indexOf(QStringView(u"*/"), position);
            state = end < 0 ? SqlLexState::comment : SqlLexState::normal;
            position = end < 0 ? text.size() : end + 2;
            kind = SqlTokenKind::comment;
        }
        else if (state == SqlLexState::literal || state == SqlLexState::quoted_name || current == u'\'' || current == u'"')
        {
            if (state == SqlLexState::normal)
            {
                state = current == u'\'' ? SqlLexState::literal : SqlLexState::quoted_name;
                ++position;
            }
            const auto delimiter = state == SqlLexState::literal ? u'\'' : u'"';
            kind = state == SqlLexState::literal ? SqlTokenKind::literal : SqlTokenKind::quoted_name;
            while (position < text.size())
            {
                if (text[position++] != delimiter) continue;
                if (position < text.size() && text[position] == delimiter) { ++position; continue; }
                state = SqlLexState::normal;
                break;
            }
        }
        else if (remaining.startsWith(u"--"))
        {
            position += 2;
            while (position < text.size() && text[position] != u'\n' && text[position] != u'\r') ++position;
            kind = SqlTokenKind::comment;
        }
        else if (current.isSpace())
        {
            while (position < text.size() && text[position].isSpace()) ++position;
            kind = SqlTokenKind::whitespace;
        }
        else if (word_character(current))
        {
            while (position < text.size() && word_character(text[position])) ++position;
            kind = SqlTokenKind::word;
        }
        else
        {
            ++position;
            kind = SqlTokenKind::symbol;
        }
        tokens.push_back({kind, start, position - start});
    }
    return tokens;
}

SqlBatch split_sql(QStringView text)
{
    SqlBatch result;
    auto state = SqlLexState::normal;
    const auto tokens = sql_tokens(text, state);
    BlockBoundary boundary;
    qsizetype start = 0;
    bool has_code = false;

    auto append = [&](qsizetype end) {
        while (start < end && text[start].isSpace()) ++start;
        while (end > start && text[end - 1].isSpace()) --end;
        result.statements.push_back({text.mid(start, end - start).toString(), start});
    };

    for (const auto& token : tokens)
    {
        if (token.kind == SqlTokenKind::whitespace || token.kind == SqlTokenKind::comment) continue;
        const auto value = text.mid(token.offset, token.length);
        if (token.kind == SqlTokenKind::word)
        {
            boundary.word(value.toString().toUpper());
            has_code = true;
            continue;
        }
        if (token.kind == SqlTokenKind::symbol && value == u";" && boundary.complete())
        {
            if (has_code) append(token.offset + 1);
            start = token.offset + 1;
            boundary = {};
            has_code = false;
            continue;
        }
        boundary.closed.clear();
        has_code = true;
    }

    result.incomplete = state != SqlLexState::normal || (has_code && !boundary.complete());
    result.remainder_offset = result.incomplete ? start : text.size();
    if (has_code && !result.incomplete) append(text.size());
    return result;
}
}
