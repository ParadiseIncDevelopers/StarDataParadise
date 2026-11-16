#pragma once

#include "sql_script.hpp"

#include <QPlainTextEdit>
#include <QPointer>
#include <QStringList>

class QCompleter;
class QDialog;
class QStringListModel;
class QMimeData;

namespace wb
{
class SqlEditorMargin;

class SqlEditor : public QPlainTextEdit
{
public:
    explicit SqlEditor(QWidget* parent = nullptr);

    SqlBatch commands(SqlScope scope) const;
    QString sql_text() const;
    QString word_at_cursor() const;
    QString file_path() const { return path_; }
    void set_catalog_words(QStringList words);
    void goto_error(qsizetype offset, int line, int column);
    QString open_file(const QString& path);
    QString save_file(const QString& path);

    void show_find_replace();
    void format_statement();
    void toggle_fold();
    void toggle_bookmark();
    void next_bookmark();
    void insert_snippet(QStringView id);
    void select_next_occurrence();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void insertFromMimeData(const QMimeData* data) override;

private:
    friend class SqlEditorMargin;
    int margin_width() const;
    void update_margin();
    void paint_margin(QPaintEvent* event);
    void click_margin(int y);
    void update_selections();
    void unfold_all();
    void toggle_fold_at(QTextBlock block);
    void complete();
    void accept_completion(const QString& value);
    QTextCursor word_cursor() const;
    void add_cursor(int direction);
    bool overlaps_cursor(const QTextCursor& cursor) const;
    void edit_cursors(const QString& replacement, bool previous = false, bool next = false);
    void clear_extra_cursors();

    SqlEditorMargin* margin_{};
    QCompleter* completion_{};
    QStringListModel* completion_model_{};
    QPointer<QDialog> find_dialog_;
    QStringList catalog_words_;
    QString path_;
    QList<QTextCursor> bookmarks_;
    QList<QTextCursor> extra_cursors_;
    bool editing_all_{};
    bool has_folds_{};
    QTextCursor error_cursor_;
};
}
