#include "workbench.hpp"

#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableView>
#include <QTabWidget>
#include <QTextCursor>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void require(bool success, const char* message)
{
    if (!success) throw std::runtime_error(message);
}

template<class Predicate>
void wait_until(Predicate ready, const char* message, qint64 timeout_ms = 12000)
{
    QElapsedTimer clock;
    clock.start();
    while (!ready() && clock.elapsed() < timeout_ms)
    {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    require(ready(), message);
}

template<class Control>
Control& named(QObject& parent, const char* name)
{
    auto* item = parent.findChild<Control*>(QString::fromLatin1(name));
    require(item != nullptr, name);
    return *item;
}

int find_pane(const QTabWidget& tabs, const wb::Profile& profile)
{
    for (int index = 1; index < tabs.count(); ++index)
        if (tabs.widget(index)->property("endpoint").toString() == wb::endpoint(profile)) return index;
    return -1;
}

QWidget& activate(QTabWidget& tabs, const wb::Profile& profile)
{
    const auto index = find_pane(tabs, profile);
    require(index > 0, "missing endpoint-specific SQL pane");
    tabs.setCurrentIndex(index);
    return *tabs.widget(index);
}

void click(QWidget& pane, const char* action)
{
    auto& button = named<QPushButton>(pane, action);
    wait_until([&] { return button.isEnabled() && !pane.property("sqlBusy").toBool(); }, action);
    button.click();
}

void finish(QWidget& pane)
{
    auto& run = named<QPushButton>(pane, "runScript");
    wait_until([&] { return run.isEnabled() && !pane.property("sqlBusy").toBool(); },
        "SQL operation did not finish");
    const auto status = named<QLabel>(pane, "sqlStatus").text();
    if (!status.startsWith("Completed"))
        throw std::runtime_error(("SQL fixture operation failed: " + status).toStdString());
}

void sql(QWidget& pane, const QString& source, const char* action = "runScript")
{
    named<QPlainTextEdit>(pane, "sqlEditor").setPlainText(source);
    click(pane, action);
    try { finish(pane); }
    catch (const std::runtime_error& error)
    {
        throw std::runtime_error(std::string(error.what()) + " [" +
            std::to_string(source.toUtf8().size()) + " source bytes]");
    }
}

void parameters(QWidget& pane, const QJsonObject& types = {}, const QJsonObject& values = {})
{
    const QJsonDocument bind(QJsonObject{{"parameter_types", types}, {"parameters", values}});
    named<QPlainTextEdit>(pane, "sqlParameters").setPlainText(QString::fromUtf8(bind.toJson(QJsonDocument::Compact)));
}

QJsonObject declaration(const char* type)
{
    return {{"type", QString::fromLatin1(type)}};
}

QJsonObject typed(const char* type, const QString& value)
{
    return {{"t", QString::fromLatin1(type)}, {"v", value}};
}

QList<QTableView*> result_tables(QWidget& pane)
{
    return named<QTabWidget>(pane, "sqlResults").findChildren<QTableView*>();
}

bool result_has(QWidget& pane, const QString& expected)
{
    for (const auto* table : result_tables(pane))
    {
        const auto* model = table->model();
        if (!model) continue;
        for (int row = 0; row < model->rowCount(); ++row)
            for (int col = 0; col < model->columnCount(); ++col)
            {
                const auto value = model->index(row, col).data().toString();
                if (value == expected || value.contains(u'"' + expected + u'"')) return true;
            }
    }
    return false;
}

int result_rows(QWidget& pane)
{
    int rows{};
    for (const auto* table : result_tables(pane))
        if (const auto* model = table->model()) rows += model->rowCount();
    return rows;
}

void marker(QWidget& pane, const QString& marker_value)
{
    sql(pane, "CREATE TABLE wb_marker (id BIGINT PRIMARY KEY, label_text TEXT);"
        "INSERT INTO wb_marker VALUES (1, '" + marker_value + "');"
        "SELECT label_text FROM wb_marker WHERE id=1;");
    require(result_has(pane, marker_value), "instance marker was not returned by its own tab");
}

void transaction(QWidget& pane, const char* action)
{
    click(pane, action);
    finish(pane);
}

void answer_close(wb::Window& window, int index, bool discard)
{
    bool answered = false;
    QTimer modal;
    QObject::connect(&modal, &QTimer::timeout, &window, [&]
    {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!dialog) return;
        modal.stop();
        for (auto* choice : dialog->buttons())
        {
            if (discard && (choice->text().contains("Rollback", Qt::CaseInsensitive) ||
                dialog->buttonRole(choice) == QMessageBox::DestructiveRole))
            {
                answered = true;
                choice->click();
                return;
            }
            if (!discard && dialog->buttonRole(choice) == QMessageBox::RejectRole)
            {
                answered = true;
                choice->click();
                return;
            }
        }
        dialog->reject();
    });
    modal.start(5);
    window.close_session(index);
    require(answered, "closing an open transaction lacked a deliberate confirmation");
}

void exercise(wb::Window& window, QTabWidget& tabs, const QVector<wb::Profile>& profiles, const QByteArray& password)
{
    auto& first = activate(tabs, profiles[0]);
    marker(first, "first-instance-marker");
    auto& second = activate(tabs, profiles[1]);
    marker(second, "second-instance-marker");
    activate(tabs, profiles[0]);
    sql(first, "SELECT label_text FROM wb_marker WHERE id=1;");
    require(result_has(first, "first-instance-marker") && !result_has(first, "second-instance-marker"),
        "switching tabs changed the session identity");
    activate(tabs, profiles[1]);
    sql(second, "SELECT label_text FROM wb_marker WHERE id=1;");
    require(result_has(second, "second-instance-marker") && !result_has(second, "first-instance-marker"),
        "secondary query reached the primary Server");
    std::cout << "S37-T01: matching objects and independent writes on two TLS instances passed\n";

    activate(tabs, profiles[0]);
    parameters(first, {{"key", declaration("BIGINT")}, {"label", declaration("TEXT")}},
        {{"key", typed("i64", "9007199254740993")}, {"label", typed("text", QStringLiteral("\u0130stanbul ' SQL ; --"))}});
    auto& editor = named<QPlainTextEdit>(first, "sqlEditor");
    editor.setPlainText("SELECT definitely_missing FROM wb_marker;\nINSERT INTO wb_marker VALUES (:key,:label);");
    auto selection = editor.textCursor();
    selection.movePosition(QTextCursor::End);
    selection.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    editor.setTextCursor(selection);
    click(first, "runSelection");
    finish(first);
    parameters(first, {{"key", declaration("BIGINT")}}, {{"key", typed("i64", "9007199254740993")}});
    editor.setPlainText("SELECT definitely_missing FROM wb_marker;\nSELECT id,label_text FROM wb_marker WHERE id=:key;");
    editor.moveCursor(QTextCursor::End);
    click(first, "runStatement");
    finish(first);
    require(result_has(first, QStringLiteral("\u0130stanbul ' SQL ; --")) &&
        result_has(first, "9007199254740993"), "typed parameters, current statement or selection changed");
    parameters(first);
    sql(first, "CREATE PROCEDURE wb_split(p_key IN BIGINT,p_value OUT BIGINT) AS\n"
        "DECLARE c REFCURSOR;\nBEGIN\n"
        "SELECT id INTO p_value FROM wb_marker WHERE id=:p_key;\n"
        "OPEN c FOR SELECT label_text FROM wb_marker WHERE id=:p_key;\n"
        "EMIT c AS found;\nEND;\nCALL wb_split(1,NULL);");
    require(result_tables(first).size() >= 2 && result_has(first, "first-instance-marker"),
        "PL/SQL block splitting or multiple result sets failed");

    transaction(first, "beginTransaction");
    sql(first, "UPDATE wb_marker SET label_text='rolled-back-marker' WHERE id=1; SELECT label_text FROM wb_marker WHERE id=1;");
    require(result_has(first, "rolled-back-marker"), "transaction did not see its own write");
    transaction(first, "rollbackTransaction");
    sql(first, "SELECT label_text FROM wb_marker WHERE id=1;");
    require(result_has(first, "first-instance-marker") && !result_has(first, "rolled-back-marker"),
        "rollback changed the committed row");

    sql(first, "CREATE TABLE wb_many (id BIGINT PRIMARY KEY, payload TEXT);");
    const QString payload(320, u'x');
    // Server SQL source is limited to 64 KiB per statement, independent of result size.
    for (int start = 1; start <= 1200; start += 100)
    {
        QStringList rows;
        for (int id = start; id < start + 100; ++id)
            rows.push_back(QStringLiteral("(%1,'%2')").arg(id).arg(payload));
        sql(first, "INSERT INTO wb_many VALUES " + rows.join(u',') + u';');
    }
    auto& limit = named<QSpinBox>(first, "sqlRowLimit");
    limit.setValue(170);
    int ticks{};
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; });
    heartbeat.start(1);
    sql(first, "SELECT id,payload FROM wb_many ORDER BY id;");
    heartbeat.stop();
    require(result_rows(first) == 170 && result_has(first, payload) && ticks > 0,
        "large result did not preserve the row cap or the Qt event loop");
    std::cout << "S37-T02: typed selection, current statement, PL/SQL script, OUT/cursor, rollback and streamed cap passed\n";

    transaction(first, "beginTransaction");
    sql(first, "UPDATE wb_marker SET label_text='close-discard-marker' WHERE id=1;");
    answer_close(window, find_pane(tabs, profiles[0]), false);
    require(window.session_count() == 2, "cancelled close lost an open transaction");
    answer_close(window, find_pane(tabs, profiles[0]), true);
    wait_until([&] { return window.session_count() == 1; }, "confirming rollback did not close the tab");
    window.open_profile(profiles[0], password);
    wait_until([&] { return window.session_count() == 2; }, "could not reconnect after closing transaction");
    auto& reopened = activate(tabs, profiles[0]);
    sql(reopened, "SELECT label_text FROM wb_marker WHERE id=1;");
    require(result_has(reopened, "first-instance-marker") && !result_has(reopened, "close-discard-marker"),
        "closing an open transaction committed its edits");
    std::cout << "S37-T03: cancel close retained the tab, confirmed close rolled back the transaction\n";
}

constexpr auto recoverable = "SELECT id FROM wb_marker WHERE id = :row_id";

QByteArray read_file(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

void save_before_crash(QTabWidget& tabs, const QVector<wb::Profile>& profiles,
    const QString& settings, const QByteArray& secret, const QByteArray& other)
{
    auto& first = activate(tabs, profiles[0]);
    named<QPlainTextEdit>(first, "sqlEditor").setPlainText(recoverable);
    parameters(first, {{"row_id", declaration("TEXT")}}, {{"row_id", typed("text", QString::fromUtf8(secret))}});
    auto& second = activate(tabs, profiles[1]);
    named<QPlainTextEdit>(second, "sqlEditor").setPlainText("SELECT '" + QString::fromUtf8(other) + "';");
    const auto path = settings + ".sql-drafts.json";
    wait_until([&] { return read_file(path).contains(recoverable); }, "SQL autosave did not write the recoverable draft");
    const auto data = read_file(path);
    require(!data.contains(secret) && !data.contains(other), "SQL autosave serialized a secret");
    std::_Exit(77);
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    try
    {
        const auto args = app.arguments();
        require(args.size() == 4, "provide profile JSON, settings path and fixture mode");
        std::string credentials;
        require(bool(std::getline(std::cin, credentials)), "missing fixture credentials");
        const auto inputs = QJsonDocument::fromJson(QByteArray::fromStdString(credentials)).object();
        const auto secret = inputs.value("first").toString().toUtf8();
        const auto other = inputs.value("second").toString().toUtf8();
        require(!secret.isEmpty() && !other.isEmpty(), "fixture credentials are empty");
        std::fill(credentials.begin(), credentials.end(), '\0');
        QString error;
        const auto profiles = wb::import_profiles(args[1], error);
        require(error.isEmpty() && profiles.size() == 2, "invalid SQL fixture profile data");
        wb::Window window(args[2]);
        window.show();
        auto& tabs = named<QTabWidget>(window, "sessions");
        window.open_profile(profiles[0], secret);
        window.open_profile(profiles[1], other);
        wait_until([&] { return window.session_count() == 2; }, "two real SQL sessions did not connect");
        if (args[3] == "exercise") exercise(window, tabs, profiles, secret);
        else if (args[3] == "crash") save_before_crash(tabs, profiles, args[2], secret, other);
        else if (args[3] == "restore")
        {
            auto& first = activate(tabs, profiles[0]);
            click(first, "restoreSqlDraft");
            require(named<QPlainTextEdit>(first, "sqlEditor").toPlainText() == recoverable,
                "crash recovery did not restore the accepted SQL draft");
            auto& second = activate(tabs, profiles[1]);
            require(!named<QPlainTextEdit>(second, "sqlEditor").toPlainText().contains(QString::fromUtf8(other)),
                "crash recovery exposed secret SQL");
            require(!named<QPlainTextEdit>(first, "sqlParameters").toPlainText().contains(QString::fromUtf8(secret)),
                "crash recovery exposed a bind secret");
            std::cout << "S37-T03: real process loss restored SQL without credentials or bind data\n";
        }
        else require(false, "unknown SQL fixture mode");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
