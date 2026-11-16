#include "sql_editor.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QCompleter>
#include <QDialog>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStringDecoder>
#include <QStringListModel>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace wb
{
namespace
{
constexpr qint64 file_limit = 8 * 1024 * 1024;

const QStringList& keyword_list()
{
    static const auto list = QStringLiteral(
        "ADD ALTER ANALYZE AND AS ASC BEGIN BIGINT BLOB BOOLEAN BY CALL CASE CHECK CLOSE "
        "COLLATE COLUMN COMMIT CONSTRAINT CREATE CROSS CURSOR DATABASE DATE DECIMAL DECLARE "
        "DEFAULT DELETE DESC DISTINCT DROP ELSE ELSIF EMIT END EXCEPTION EXECUTE EXISTS "
        "EXPLAIN FALSE FETCH FOR FOREIGN FROM FUNCTION GRANT GROUP HAVING HISTORY IDENTITY "
        "IF IN INDEX INNER INSERT INT INTEGER INTO IS JOIN LEFT LOOP MODEL NOT NULL ON OPEN "
        "OR ORDER OUT OUTER PRIMARY PROCEDURE RECORD REFCURSOR REFERENCES RELEASE REPLACE "
        "RETURN RETURNS REVOKE ROLLBACK ROW SAVEPOINT SCHEMA SELECT SEQUENCE SET TABLE THEN "
        "TEXT TIME TIMESTAMP TO TRANSACTION TRIGGER TRUE TRUNCATE UNION UNIQUE UPDATE VALUES "
        "VIEW WHEN WHERE WHILE WITH WORK ZONE").split(u' ');
    return list;
}

bool keyword(const QString& upper)
{
    static const QSet<QString> words(keyword_list().begin(), keyword_list().end());
    return words.contains(upper);
}

bool identifier_char(QChar ch)
{
    return ch.isLetterOrNumber() || ch == u'_';
}

int bounded_position(qsizetype position, const QTextDocument* document)
{
    return static_cast<int>(std::clamp(position, qsizetype{0}, qsizetype{document->characterCount() - 1}));
}

QString format_sql(QStringView text)
{
    auto state = SqlLexState::normal;
    const auto tokens = sql_tokens(text, state);
    if (state != SqlLexState::normal) return text.toString();

    QString result;
    int parentheses = 0;
    bool break_clauses = false;
    bool first_word = true;
    QString preceding;
    for (const auto& token : tokens)
    {
        const auto value = text.mid(token.offset, token.length);
        if (token.kind == SqlTokenKind::word)
        {
            const auto upper = value.toString().toUpper();
            if (first_word)
            {
                break_clauses = upper == u"SELECT" || upper == u"UPDATE" || upper == u"DELETE" || upper == u"INSERT";
                first_word = false;
            }
            const bool clause = upper == u"FROM" || upper == u"WHERE" || upper == u"HAVING" ||
                upper == u"GROUP" || upper == u"ORDER" || upper == u"VALUES" ||
                (upper == u"JOIN" && preceding != u"LEFT" && preceding != u"INNER" &&
                 preceding != u"OUTER" && preceding != u"CROSS");
            if (break_clauses && parentheses == 0 && clause && !result.isEmpty() && result.back() == u' ')
            {
                while (!result.isEmpty() && (result.back() == u' ' || result.back() == u'\t')) result.chop(1);
                if (!result.isEmpty() && !result.back().isSpace()) result += u'\n';
            }
            if (keyword(upper)) result += upper;
            else result += value;
            preceding = upper;
            continue;
        }
        if (token.kind == SqlTokenKind::symbol)
        {
            if (value == u"(") ++parentheses;
            if (value == u")" && parentheses > 0) --parentheses;
        }
        result += value;
    }
    return result;
}

class Highlight final : public QSyntaxHighlighter
{
public:
    explicit Highlight(QTextDocument* document, bool dark) : QSyntaxHighlighter(document)
    {
        keyword_.setForeground(QColor(dark ? "#8cbcff" : "#164b8a"));
        keyword_.setFontWeight(QFont::DemiBold);
        comment_.setForeground(QColor(dark ? "#91a598" : "#436752"));
        literal_.setForeground(QColor(dark ? "#edb37a" : "#8e3b13"));
        quoted_.setForeground(QColor(dark ? "#c8aeec" : "#593a89"));
        number_.setForeground(QColor(dark ? "#80d7c0" : "#276657"));
    }

private:
    void highlightBlock(const QString& text) override
    {
        auto state = previousBlockState() < 0 ? SqlLexState::normal : static_cast<SqlLexState>(previousBlockState());
        const auto tokens = sql_tokens(QStringView(text), state);
        for (const auto& token : tokens)
        {
            const auto value = QStringView(text).mid(token.offset, token.length);
            const QTextCharFormat* style = nullptr;
            switch (token.kind)
            {
            case SqlTokenKind::word:
                if (keyword(value.toString().toUpper())) style = &keyword_;
                else if (value.front().isDigit()) style = &number_;
                break;
            case SqlTokenKind::comment: style = &comment_; break;
            case SqlTokenKind::literal: style = &literal_; break;
            case SqlTokenKind::quoted_name: style = &quoted_; break;
            default: break;
            }
            if (style) setFormat(static_cast<int>(token.offset), static_cast<int>(token.length), *style);
        }
        setCurrentBlockState(static_cast<int>(state));
    }

    QTextCharFormat keyword_, comment_, literal_, quoted_, number_;
};
}

class SqlEditorMargin final : public QWidget
{
public:
    explicit SqlEditorMargin(SqlEditor* editor) : QWidget(editor), editor_(editor)
    {
        setToolTip(tr("Click a line number to toggle folding. Ctrl+F2 toggles a bookmark."));
    }
private:
    void paintEvent(QPaintEvent* event) override { editor_->paint_margin(event); }
    void mousePressEvent(QMouseEvent* event) override { editor_->click_margin(event->position().toPoint().y()); }
    SqlEditor* editor_;
};

SqlEditor::SqlEditor(QWidget* parent) : QPlainTextEdit(parent)
{
    setObjectName("sqlEditor");
    setAccessibleName(tr("SQL editor"));
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(fontMetrics().horizontalAdvance(u' ') * 4.0);
    setPlaceholderText(tr("Write SQL or PL/SQL"));
    const bool dark = palette().color(QPalette::Base).lightness() < 128;
    new Highlight(document(), dark);

    margin_ = new SqlEditorMargin(this);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { update_margin(); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& rect, int delta) {
        if (delta) margin_->scroll(0, delta);
        else margin_->update(0, rect.y(), margin_->width(), rect.height());
        if (rect.contains(viewport()->rect())) update_margin();
    });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        update_selections();
        margin_->update();
    });
    connect(this, &QPlainTextEdit::textChanged, this, [this] {
        if (!editing_all_) extra_cursors_.clear();
        error_cursor_ = {};
        unfold_all();
        update_selections();
    });

    completion_model_ = new QStringListModel(this);
    completion_ = new QCompleter(completion_model_, this);
    completion_->setWidget(this);
    completion_->setCaseSensitivity(Qt::CaseInsensitive);
    completion_->setCompletionMode(QCompleter::PopupCompletion);
    connect(completion_, qOverload<const QString&>(&QCompleter::activated),
        this, &SqlEditor::accept_completion);
    set_catalog_words({});
    update_margin();
}

SqlBatch SqlEditor::commands(SqlScope scope) const
{
    const auto cursor = textCursor();
    if (scope == SqlScope::selection)
    {
        if (!cursor.hasSelection()) return {};
        const auto source = sql_text().mid(cursor.selectionStart(), cursor.selectionEnd() - cursor.selectionStart());
        auto result = split_sql(QStringView(source));
        for (auto& command : result.statements) command.offset += cursor.selectionStart();
        result.remainder_offset += cursor.selectionStart();
        return result;
    }

    const auto source = sql_text();
    const auto batch = split_sql(QStringView(source));
    if (scope == SqlScope::script) return batch;
    if (batch.incomplete && cursor.position() >= batch.remainder_offset)
        return {{}, true, batch.remainder_offset};

    for (const auto& statement : batch.statements)
    {
        if (cursor.position() < statement.offset + statement.text.size())
            return {{statement}, false, statement.offset + statement.text.size()};
    }
    if (!batch.statements.isEmpty())
        return {{batch.statements.last()}, false, source.size()};
    return {};
}

QString SqlEditor::sql_text() const
{
    auto text = document()->toRawText();
    text.replace(QChar::ParagraphSeparator, u'\n');
    return text;
}

QTextCursor SqlEditor::word_cursor() const
{
    auto cursor = textCursor();
    cursor.clearSelection();
    const auto block = cursor.block();
    const auto text = block.text();
    const auto position = cursor.position() - block.position();
    qsizetype start = position;
    qsizetype end = position;
    while (start > 0 && identifier_char(text[start - 1])) --start;
    while (end < text.size() && identifier_char(text[end])) ++end;
    cursor.setPosition(block.position() + static_cast<int>(start));
    cursor.setPosition(block.position() + static_cast<int>(end), QTextCursor::KeepAnchor);
    return cursor;
}

QString SqlEditor::word_at_cursor() const
{
    return word_cursor().selectedText();
}

void SqlEditor::set_catalog_words(QStringList words)
{
    words.removeAll(QString{});
    words.removeDuplicates();
    catalog_words_ = std::move(words);
    QStringList entries = keyword_list();
    entries.append(catalog_words_);
    entries.removeDuplicates();
    entries.sort(Qt::CaseInsensitive);
    completion_model_->setStringList(entries);
}

void SqlEditor::complete()
{
    if (isReadOnly() || !extra_cursors_.isEmpty()) return;
    const auto cursor = textCursor();
    if (cursor.hasSelection()) return;
    const auto block = cursor.block();
    const auto prefix = block.text().left(cursor.positionInBlock());
    auto state = block.previous().userState() < 0 ? SqlLexState::normal :
        static_cast<SqlLexState>(block.previous().userState());
    const auto tokens = sql_tokens(QStringView(prefix), state);
    if (state != SqlLexState::normal || (!tokens.isEmpty() &&
        tokens.last().kind == SqlTokenKind::comment && !prefix.endsWith(u"*/"))) return;

    const auto word = word_cursor();
    completion_->setCompletionPrefix(word.selectedText().left(cursor.position() - word.selectionStart()));
    if (!completion_->completionCount()) { completion_->popup()->hide(); return; }
    completion_->popup()->setCurrentIndex(completion_->completionModel()->index(0, 0));
    auto area = cursorRect();
    area.setWidth(std::max(180, completion_->popup()->sizeHintForColumn(0) + 24));
    completion_->complete(area);
}

void SqlEditor::accept_completion(const QString& word)
{
    QString value = word;
    static const QRegularExpression unquoted(QStringLiteral("^[a-z_][a-z0-9_]*$"));
    if (catalog_words_.contains(word) && (!unquoted.match(word).hasMatch() || keyword(word.toUpper())) &&
        !keyword_list().contains(word))
    {
        value.replace(u'"', QStringLiteral("\"\""));
        value = u'"' + value + u'"';
    }
    auto cursor = word_cursor();
    cursor.insertText(value);
    setTextCursor(cursor);
}

void SqlEditor::goto_error(qsizetype offset, int line, int column)
{
    if (line < 1 || column < 1) return;
    const auto source = sql_text();
    auto position = std::clamp(offset, qsizetype{0}, source.size());
    for (int current = 1; current < line; ++current)
    {
        const auto newline = source.indexOf(u'\n', position);
        if (newline < 0) break;
        position = newline + 1;
    }
    for (int current = 1; current < column && position < source.size() && source[position] != u'\n'; ++current)
    {
        if (source[position++].isHighSurrogate() && position < source.size() && source[position].isLowSurrogate())
            ++position;
    }
    unfold_all();
    clear_extra_cursors();
    error_cursor_ = QTextCursor(document());
    error_cursor_.setPosition(bounded_position(position, document()));
    setTextCursor(error_cursor_);
    if (!error_cursor_.atBlockEnd()) error_cursor_.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    centerCursor();
    update_selections();
}

QString SqlEditor::open_file(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return file.errorString();
    if (file.size() > file_limit) return tr("SQL files can contain at most 8 MiB.");
    const auto bytes = file.read(file_limit + 1);
    if (file.error() != QFileDevice::NoError) return file.errorString();
    if (bytes.size() > file_limit) return tr("SQL files can contain at most 8 MiB.");
    QStringDecoder utf8(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    const QString text = utf8.decode(bytes);
    if (utf8.hasError() || text.contains(QChar(u'\0')))
        return tr("Choose a UTF-8 SQL text file.");
    bookmarks_.clear();
    setPlainText(text);
    path_ = QFileInfo(path).absoluteFilePath();
    document()->setModified(false);
    return {};
}

QString SqlEditor::save_file(const QString& path)
{
    const auto bytes = sql_text().toUtf8();
    if (bytes.size() > file_limit) return tr("SQL files can contain at most 8 MiB.");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return file.errorString();
    path_ = QFileInfo(path).absoluteFilePath();
    document()->setModified(false);
    return {};
}

void SqlEditor::format_statement()
{
    if (isReadOnly()) return;
    const auto scope = textCursor().hasSelection() ? SqlScope::selection : SqlScope::statement;
    const auto batch = commands(scope);
    if (batch.incomplete || batch.statements.isEmpty()) return;
    clear_extra_cursors();
    QTextCursor cursor(document());
    cursor.beginEditBlock();
    for (auto it = batch.statements.rbegin(); it != batch.statements.rend(); ++it)
    {
        cursor.setPosition(bounded_position(it->offset, document()));
        cursor.setPosition(bounded_position(it->offset + it->text.size(), document()), QTextCursor::KeepAnchor);
        cursor.insertText(format_sql(QStringView(it->text)));
    }
    cursor.endEditBlock();
    setTextCursor(cursor);
}

void SqlEditor::show_find_replace()
{
    if (find_dialog_)
    {
        find_dialog_->raise();
        find_dialog_->activateWindow();
        return;
    }
    auto* dialog = new QDialog(this);
    find_dialog_ = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Find and replace"));
    auto* layout = new QVBoxLayout(dialog);
    auto* fields = new QFormLayout;
    auto* term = new QLineEdit;
    auto* replacement = new QLineEdit;
    auto* case_sensitive = new QCheckBox(tr("Match case"));
    auto* feedback = new QLabel;
    fields->addRow(tr("Find"), term);
    fields->addRow(tr("Replace with"), replacement);
    fields->addRow(case_sensitive);
    layout->addLayout(fields);
    layout->addWidget(feedback);
    auto* buttons = new QHBoxLayout;
    auto* find = new QPushButton(tr("Find next"));
    auto* replace = new QPushButton(tr("Replace"));
    auto* replace_all = new QPushButton(tr("Replace all"));
    replace->setEnabled(!isReadOnly());
    replace_all->setEnabled(!isReadOnly());
    buttons->addWidget(find);
    buttons->addWidget(replace);
    buttons->addWidget(replace_all);
    layout->addLayout(buttons);

    const auto matches = [case_sensitive] {
        return case_sensitive->isChecked() ? QTextDocument::FindFlags(QTextDocument::FindCaseSensitively) :
            QTextDocument::FindFlags{};
    };
    const auto find_next = [this, term, feedback, matches] {
        if (term->text().isEmpty()) return;
        auto found = document()->find(term->text(), textCursor(), matches());
        if (found.isNull()) found = document()->find(term->text(), 0, matches());
        if (found.isNull()) { feedback->setText(tr("No matches.")); return; }
        clear_extra_cursors();
        unfold_all();
        setTextCursor(found);
        centerCursor();
        feedback->clear();
    };
    connect(find, &QPushButton::clicked, dialog, find_next);
    connect(term, &QLineEdit::returnPressed, dialog, find_next);
    connect(replace, &QPushButton::clicked, dialog, [this, term, replacement, matches, find_next] {
        if (term->text().isEmpty() || isReadOnly()) return;
        auto cursor = textCursor();
        const auto sensitivity = matches() ? Qt::CaseSensitive : Qt::CaseInsensitive;
        if (cursor.selectedText().compare(term->text(), sensitivity) != 0) { find_next(); return; }
        cursor.insertText(replacement->text());
        setTextCursor(cursor);
        find_next();
    });
    connect(replace_all, &QPushButton::clicked, dialog, [this, term, replacement, feedback, matches] {
        if (term->text().isEmpty() || isReadOnly()) return;
        const auto flags = matches();
        QTextCursor edit(document());
        edit.beginEditBlock();
        int total = 0;
        for (QTextCursor result; !(result = document()->find(term->text(), edit, flags)).isNull();)
        {
            result.insertText(replacement->text());
            edit.setPosition(result.position());
            ++total;
        }
        edit.endEditBlock();
        feedback->setText(tr("%1 replacement(s).").arg(total));
    });
    const auto selection = textCursor().selectedText();
    if (!selection.contains(QChar::ParagraphSeparator)) term->setText(selection);
    dialog->show();
    term->selectAll();
    term->setFocus();
}

int SqlEditor::margin_width() const
{
    const auto digits = QString::number(std::max(1, blockCount())).size();
    return 17 + static_cast<int>(digits) * fontMetrics().horizontalAdvance(u'9');
}

void SqlEditor::update_margin()
{
    const auto width = margin_width();
    if (viewportMargins().left() != width) setViewportMargins(width, 0, 0, 0);
    margin_->setGeometry(contentsRect().left(), contentsRect().top(), width, contentsRect().height());
}

void SqlEditor::resizeEvent(QResizeEvent* event)
{
    QPlainTextEdit::resizeEvent(event);
    update_margin();
}

void SqlEditor::paint_margin(QPaintEvent* event)
{
    QPainter painter(margin_);
    painter.fillRect(event->rect(), palette().alternateBase());
    painter.setFont(font());

    int top = qRound(blockBoundingGeometry(firstVisibleBlock()).translated(contentOffset()).top());
    for (auto block = firstVisibleBlock(); block.isValid() && top < event->rect().bottom(); block = block.next())
    {
        if (!block.isVisible()) continue;
        const int height = qRound(blockBoundingRect(block).height());
        if (top + height >= event->rect().top())
        {
            const auto marked = std::ranges::any_of(bookmarks_, [&](const QTextCursor& entry) {
                return entry.block() == block;
            });
            painter.setPen(block == textCursor().block() ? palette().text().color() : palette().mid().color());
            painter.drawText(9, top, margin_->width() - 13, fontMetrics().height(),
                Qt::AlignRight, QString::number(block.blockNumber() + 1));
            if (marked)
            {
                painter.setPen(Qt::NoPen);
                painter.setBrush(palette().highlight());
                painter.drawEllipse(2, top + 4, 6, 6);
            }
            else if (block.next().isValid() && !block.next().isVisible())
            {
                painter.setPen(palette().text().color());
                painter.drawText(1, top, 9, fontMetrics().height(), Qt::AlignLeft, QStringLiteral("+"));
            }
        }
        top += height;
    }
}

void SqlEditor::click_margin(int y)
{
    for (auto block = firstVisibleBlock(); block.isValid(); block = block.next())
    {
        if (!block.isVisible()) continue;
        const auto area = blockBoundingGeometry(block).translated(contentOffset());
        if (area.top() > y) return;
        if (y <= area.bottom()) { toggle_fold_at(block); return; }
    }
}

void SqlEditor::toggle_fold()
{
    const auto batch = commands(SqlScope::statement);
    if (!batch.statements.isEmpty())
        toggle_fold_at(document()->findBlock(bounded_position(batch.statements.first().offset, document())));
}

void SqlEditor::toggle_fold_at(QTextBlock block)
{
    if (!block.isValid() || !block.isVisible()) return;
    if (block.next().isValid() && !block.next().isVisible())
    {
        for (auto next = block.next(); next.isValid() && !next.isVisible(); next = next.next())
        {
            next.setVisible(true);
            next.setLineCount(1);
        }
    }
    else
    {
        const auto source = sql_text();
        const auto batch = split_sql(QStringView(source));
        const auto item = std::ranges::find_if(batch.statements, [&](const SqlCommand& statement) {
            return document()->findBlock(bounded_position(statement.offset, document())) == block;
        });
        if (item == batch.statements.end()) return;

        const auto end_position = item->offset + item->text.size();
        const auto end = document()->findBlock(bounded_position(end_position - 1, document()));
        if (block == end) return;
        const auto next_on_line = std::ranges::find_if(batch.statements, [&](const SqlCommand& statement) {
            return statement.offset >= end_position &&
                document()->findBlock(bounded_position(statement.offset, document())) == end;
        });
        const auto last = next_on_line == batch.statements.end() ? end.next() : end;
        if (block.next() == last) return;

        if (textCursor().block().position() >= block.next().position() &&
            (!last.isValid() || textCursor().block().position() < last.position()))
            setTextCursor(QTextCursor(block));

        for (auto next = block.next(); next.isValid() && next != last; next = next.next())
        {
            next.setVisible(false);
            next.setLineCount(0);
        }
        has_folds_ = true;
    }
    document()->markContentsDirty(block.position(), document()->characterCount() - block.position());
    viewport()->update();
    margin_->update();
}

void SqlEditor::unfold_all()
{
    if (!has_folds_) return;
    has_folds_ = false;
    for (auto block = document()->begin(); block.isValid(); block = block.next())
    {
        if (block.isVisible()) continue;
        block.setVisible(true);
        block.setLineCount(1);
    }
    document()->markContentsDirty(0, document()->characterCount());
    viewport()->update();
    margin_->update();
}

void SqlEditor::toggle_bookmark()
{
    const auto block = textCursor().block();
    const auto found = std::ranges::find_if(bookmarks_, [&](const QTextCursor& entry) { return entry.block() == block; });
    if (found == bookmarks_.end()) bookmarks_.append(QTextCursor(block));
    else bookmarks_.erase(found);
    margin_->update();
}

void SqlEditor::next_bookmark()
{
    if (bookmarks_.isEmpty()) return;
    const auto line = textCursor().block().position();
    const auto by_line = [](const QTextCursor& left, const QTextCursor& right) {
        return left.block().position() < right.block().position();
    };
    std::ranges::sort(bookmarks_, by_line);
    const auto next = std::ranges::find_if(bookmarks_, [&](const QTextCursor& entry) {
        return entry.block().position() > line;
    });
    unfold_all();
    setTextCursor(next == bookmarks_.end() ? bookmarks_.first() : *next);
    centerCursor();
}

void SqlEditor::insert_snippet(QStringView id)
{
    if (isReadOnly()) return;
    QString source;
    if (id.compare(u"select", Qt::CaseInsensitive) == 0)
        source = QStringLiteral("SELECT *\nFROM table_name\nWHERE id = :id;");
    else if (id.compare(u"insert", Qt::CaseInsensitive) == 0)
        source = QStringLiteral("INSERT INTO table_name (id, name)\nVALUES (:id, :name);");
    else if (id.compare(u"block", Qt::CaseInsensitive) == 0)
        source = QStringLiteral("DECLARE\n    item BIGINT;\nBEGIN\n    item := :value;\nEND;");
    else if (id.compare(u"procedure", Qt::CaseInsensitive) == 0)
        source = QStringLiteral("CREATE PROCEDURE procedure_name (value IN BIGINT, result OUT BIGINT) AS\n"
            "BEGIN\n    result := value;\nEND procedure_name;");
    if (source.isEmpty()) return;
    clear_extra_cursors();
    insertPlainText(source);
}

void SqlEditor::update_selections()
{
    QList<QTextEdit::ExtraSelection> entries;
    QTextEdit::ExtraSelection current;
    current.cursor = textCursor();
    current.cursor.clearSelection();
    current.format.setBackground(palette().alternateBase());
    current.format.setProperty(QTextFormat::FullWidthSelection, true);
    entries.append(current);
    for (const auto& cursor : extra_cursors_)
    {
        if (!cursor.hasSelection()) continue;
        QTextEdit::ExtraSelection selected;
        selected.cursor = cursor;
        selected.format.setBackground(palette().highlight());
        selected.format.setForeground(palette().highlightedText());
        entries.append(selected);
    }
    if (!error_cursor_.isNull())
    {
        QTextEdit::ExtraSelection error;
        error.cursor = error_cursor_;
        error.format.setBackground(palette().alternateBase());
        error.format.setProperty(QTextFormat::FullWidthSelection, true);
        error.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        error.format.setUnderlineColor(QColor("#bd4f44"));
        entries.append(error);
    }
    setExtraSelections(entries);
    viewport()->update();
}

void SqlEditor::paintEvent(QPaintEvent* event)
{
    QPlainTextEdit::paintEvent(event);
    QPainter painter(viewport());
    painter.setPen(palette().text().color());
    for (const auto& cursor : extra_cursors_)
    {
        if (!cursor.block().isVisible()) continue;
        const auto area = cursorRect(cursor);
        painter.drawLine(area.topLeft(), area.bottomLeft());
    }
}

bool SqlEditor::overlaps_cursor(const QTextCursor& cursor) const
{
    const auto overlaps = [&](const QTextCursor& other) {
        if (!cursor.hasSelection())
            return cursor.position() >= other.selectionStart() && cursor.position() <= other.selectionEnd();
        if (!other.hasSelection())
            return other.position() >= cursor.selectionStart() && other.position() <= cursor.selectionEnd();
        return cursor.selectionStart() < other.selectionEnd() && other.selectionStart() < cursor.selectionEnd();
    };
    return overlaps(textCursor()) || std::ranges::any_of(extra_cursors_, overlaps);
}

void SqlEditor::add_cursor(int direction)
{
    if (extra_cursors_.size() >= 128) return;
    const auto reference = extra_cursors_.isEmpty() ? textCursor() : extra_cursors_.last();
    auto block = reference.block();
    do { block = direction < 0 ? block.previous() : block.next(); }
    while (block.isValid() && !block.isVisible());
    if (!block.isValid()) return;
    QTextCursor added(block);
    added.setPosition(block.position() + std::min(reference.positionInBlock(), block.length() - 1));
    if (overlaps_cursor(added)) return;
    extra_cursors_.append(added);
    update_selections();
}

void SqlEditor::select_next_occurrence()
{
    if (extra_cursors_.size() >= 128) return;
    auto seed = textCursor();
    if (!seed.hasSelection())
    {
        seed = word_cursor();
        if (!seed.hasSelection()) return;
        setTextCursor(seed);
    }

    const auto needle = seed.selectedText();
    if (needle.contains(QChar::ParagraphSeparator)) return;
    const auto resume = extra_cursors_.isEmpty() ? seed : extra_cursors_.last();
    auto found = document()->find(needle, resume, QTextDocument::FindCaseSensitively);
    if (found.isNull()) found = document()->find(needle, 0, QTextDocument::FindCaseSensitively);
    const auto start = found.position();
    while (!found.isNull() && overlaps_cursor(found))
    {
        found = document()->find(needle, found, QTextDocument::FindCaseSensitively);
        if (found.isNull()) found = document()->find(needle, 0, QTextDocument::FindCaseSensitively);
        if (found.position() == start) return;
    }
    if (!found.isNull()) extra_cursors_.append(found);
    update_selections();
}

void SqlEditor::edit_cursors(const QString& replacement, bool previous, bool next)
{
    if (isReadOnly()) return;
    struct Edit { QTextCursor cursor; bool primary; };
    QList<Edit> edits{{textCursor(), true}};
    for (const auto& cursor : extra_cursors_) edits.push_back({cursor, false});
    std::ranges::sort(edits, std::greater{}, [](const Edit& edit) { return edit.cursor.selectionStart(); });
    editing_all_ = true;
    QTextCursor transaction(document());
    transaction.beginEditBlock();
    for (auto& edit : edits)
    {
        if (!edit.cursor.hasSelection())
        {
            if (previous) { edit.cursor.deletePreviousChar(); continue; }
            if (next) { edit.cursor.deleteChar(); continue; }
        }
        edit.cursor.insertText(replacement);
    }
    transaction.endEditBlock();
    editing_all_ = false;

    extra_cursors_.clear();
    for (const auto& edit : edits)
    {
        if (edit.primary) setTextCursor(edit.cursor);
        else extra_cursors_.append(edit.cursor);
    }
    update_selections();
}

void SqlEditor::clear_extra_cursors()
{
    if (extra_cursors_.isEmpty()) return;
    extra_cursors_.clear();
    update_selections();
}

void SqlEditor::insertFromMimeData(const QMimeData* source)
{
    if (!extra_cursors_.isEmpty() && source && source->hasText()) edit_cursors(source->text());
    else QPlainTextEdit::insertFromMimeData(source);
}

void SqlEditor::mousePressEvent(QMouseEvent* event)
{
    if (event->modifiers().testFlag(Qt::AltModifier) && event->button() == Qt::LeftButton)
    {
        const auto cursor = cursorForPosition(event->position().toPoint());
        if (extra_cursors_.size() < 128 && !overlaps_cursor(cursor)) extra_cursors_.append(cursor);
        update_selections();
        event->accept();
        return;
    }
    clear_extra_cursors();
    QPlainTextEdit::mousePressEvent(event);
}

void SqlEditor::keyPressEvent(QKeyEvent* event)
{
    const auto key = event->key();
    const auto modifiers = event->modifiers();
    if (completion_->popup()->isVisible() &&
        (key == Qt::Key_Enter || key == Qt::Key_Return || key == Qt::Key_Escape ||
         key == Qt::Key_Tab || key == Qt::Key_Backtab))
    {
        event->ignore();
        return;
    }

    if (modifiers == Qt::ControlModifier)
    {
        if (key == Qt::Key_Space) { complete(); return; }
        if (key == Qt::Key_F) { show_find_replace(); return; }
        if (key == Qt::Key_D) { select_next_occurrence(); return; }
        if (key == Qt::Key_F2) { toggle_bookmark(); return; }
    }
    if (key == Qt::Key_F2 && modifiers == Qt::NoModifier) { next_bookmark(); return; }
    if (modifiers == (Qt::ControlModifier | Qt::AltModifier) && (key == Qt::Key_Up || key == Qt::Key_Down))
    {
        add_cursor(key == Qt::Key_Up ? -1 : 1);
        return;
    }
    if (modifiers == (Qt::ControlModifier | Qt::ShiftModifier) && key == Qt::Key_F)
    {
        format_statement();
        return;
    }
    if (modifiers == (Qt::ControlModifier | Qt::ShiftModifier) && key == Qt::Key_Minus)
    {
        toggle_fold();
        return;
    }

    if (!extra_cursors_.isEmpty())
    {
        if (key == Qt::Key_Escape) { clear_extra_cursors(); return; }
        if (event->matches(QKeySequence::Paste)) { QPlainTextEdit::keyPressEvent(event); return; }
        if (modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier)
        {
            if (key == Qt::Key_Backspace) { edit_cursors({}, true); return; }
            if (key == Qt::Key_Delete) { edit_cursors({}, false, true); return; }
            if (key == Qt::Key_Return || key == Qt::Key_Enter) { edit_cursors(QStringLiteral("\n")); return; }
            if (key == Qt::Key_Tab) { edit_cursors(QStringLiteral("    ")); return; }
            if (!event->text().isEmpty() && event->text().front().isPrint())
            {
                edit_cursors(event->text());
                return;
            }
        }
        clear_extra_cursors();
    }
    QPlainTextEdit::keyPressEvent(event);
}
}


