#include "compare_studio.hpp"
#include "sql_session.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QEventLoop>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <optional>

using namespace wb;

namespace
{
void require(bool accepted, const char* explanation)
{
    if (accepted) return;
    std::cerr << explanation << '\n';
    std::exit(1);
}

template<class Start>
SqlReply await(Start start)
{
    QEventLoop loop;
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    std::optional<SqlReply> received;
    require(start([&](SqlReply value) { received = std::move(value); loop.quit(); }), "request started");
    if (!received) loop.exec();
    require(received.has_value(), "request completed");
    if (received->error.code != SDP_OK) std::cerr << received->error.message << '\n';
    require(received->error.code == SDP_OK, "fixture SQL succeeded");
    return std::move(*received);
}

template<class T>
T& widget(QObject& parent, const char* name)
{
    auto* child = parent.findChild<T*>(QString::fromLatin1(name));
    require(child != nullptr, name);
    return *child;
}

QString waitFor(QDialog& dialog, QStringView snippet)
{
    auto& status = widget<QLabel>(dialog, "compareStatus");
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (status.text().contains(snippet)) loop.quit(); });
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    if (!status.text().contains(snippet))
    {
        poll.start(10);
        loop.exec();
    }
    if (!status.text().contains(snippet)) std::cerr << status.text().toStdString() << '\n';
    require(status.text().contains(snippet), "expected compare workflow status");
    return status.text();
}

Profile fixture(const char* path)
{
    QString error;
    const auto profiles = import_profiles(QString::fromLocal8Bit(path), error);
    require(profiles.size() == 1, "fixture profile loaded");
    return profiles.first();
}

Password readPassword()
{
    std::string line;
    std::getline(std::cin, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    Password password{QByteArray(line.data(), static_cast<qsizetype>(line.size()))};
    std::fill(line.begin(), line.end(), '\0');
    return password;
}

void apply(QDialog& studio, QStringView expected)
{
    auto& action = widget<QPushButton>(studio, "compareApply");
    require(action.isEnabled(), "preview approved for apply");
    QTimer::singleShot(0, &studio, [&] {
        auto* message = studio.findChild<QMessageBox*>();
        require(message != nullptr, "confirmation opened");
        message->button(QMessageBox::Yes)->click();
    });
    action.click();
    waitFor(studio, expected);
}

QJsonArray sql(SqlSession& target, const QString& statement)
{
    QJsonArray data;
    await([&](auto done) {
        return target.execute({{statement}}, {}, {}, {}, [&](const SqlEvent& event) {
            if (event.kind != SqlEvent::Kind::batch) return;
            for (const auto row : event.data.value("rows").toArray()) data.push_back(row);
        }, done);
    });
    return data;
}

void tableInputs(QDialog& dialog, QStringView table, const QString& keys, const QString& fields)
{
    for (const auto id : {"compareSourceTable", "compareDestinationTable"})
    {
        auto& choice = widget<QComboBox>(dialog, id);
        const auto index = choice.findText(table.toString());
        require(index >= 0, "streamed table is visible");
        choice.setCurrentIndex(index);
    }
    widget<QLineEdit>(dialog, "compareSourceKeys").setText(keys);
    widget<QLineEdit>(dialog, "compareDestinationKeys").setText(keys);
    widget<QLineEdit>(dialog, "compareSourceFields").setText(fields);
    widget<QLineEdit>(dialog, "compareDestinationFields").setText(fields);
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    require(argc == 3, "source and destination profile filenames");
    const auto left = fixture(argv[1]);
    const auto right = fixture(argv[2]);
    auto leftPassword = readPassword();
    auto rightPassword = readPassword();
    Password updaterPassword{QByteArray(rightPassword.bytes.constData(), rightPassword.bytes.size())};
    const auto first = wb::connect(left, leftPassword, false, false);
    const auto second = wb::connect(right, rightPassword, false, false);
    const auto updater = wb::connect(right, updaterPassword, false, false);
    for (const auto* connection : {&first, &second, &updater})
        if (!connection->session) std::cerr << connection->error.toStdString() << '\n';
    require(first.session && second.session && updater.session && left.port != right.port, "two real TLS endpoints and a concurrent destination session");
    QThreadPool workers;
    SqlSession source(left, first.session, workers);
    SqlSession destination(right, second.session, workers);
    SqlSession other(right, updater.session, workers);

    sql(source, "CREATE TABLE compare_base (id BIGINT PRIMARY KEY, display_label TEXT DEFAULT ('source'))");
    sql(source, "CREATE INDEX compare_idx ON compare_base (display_label)");
    sql(source, "CREATE TABLE compare_child (id BIGINT PRIMARY KEY, parent_id BIGINT REFERENCES compare_base (id))");
    sql(source, "CREATE VIEW compare_view AS SELECT id FROM compare_base");
    sql(source, "CREATE FUNCTION compare_routine() RETURN BIGINT AS BEGIN RETURN 1; END;");
    sql(source, "CREATE DECISION RULE compare_rule ON compare_base FOR UPDATE PRIORITY 1 WHEN (TRUE) THEN ALLOW");
    sql(destination, "CREATE TABLE compare_base (id BIGINT PRIMARY KEY, old_label TEXT, obsolete TEXT)");
    sql(destination, "INSERT INTO compare_base VALUES (1, 'preserved', 'drop this')");
    QWidget parent;
    parent.show();
    auto& studio = *showCompareStudio(source, destination, &parent);
    auto& refresh = widget<QPushButton>(studio, "compareRefresh");
    auto& preview = widget<QPlainTextEdit>(studio, "comparePreview");
    auto& action = widget<QPushButton>(studio, "compareApply");
    auto& destructive = widget<QCheckBox>(studio, "compareDestructive");
    refresh.click();
    waitFor(studio, u"Catalogs captured");
    require(preview.toPlainText().contains(u"Unmatched additions and removals") && !action.isEnabled(),
        "ambiguous column changes require a deliberate user decision");
    require(preview.toPlainText().contains(u"compare_view") && preview.toPlainText().contains(u"compare_routine") &&
        preview.toPlainText().contains(u"compare_rule"), "view, routine and rule differences are reported");
    widget<QPushButton>(studio, "compareAddMapping").click();
    auto& mappings = widget<QTableWidget>(studio, "compareMappings");
    mappings.setItem(0, 0, new QTableWidgetItem(QStringLiteral("\"public\".\"compare_base\"")));
    mappings.setItem(0, 1, new QTableWidgetItem(QStringLiteral("old_label")));
    mappings.setItem(0, 2, new QTableWidgetItem(QStringLiteral("display_label")));
    const auto ddl = preview.toPlainText();
    if (!ddl.contains(u"CREATE TABLE")) std::cerr << ddl.toStdString() << '\n';
    require(ddl.contains(u"RENAME COLUMN \"old_label\" TO \"display_label\"") && ddl.contains(u"DROP COLUMN \"obsolete\"") &&
        ddl.contains(u"SET DEFAULT") && ddl.contains(u"CREATE INDEX") && ddl.contains(u"CREATE TABLE") && ddl.contains(u"REFERENCES"),
        "preview includes explicit rename, destructive drop, default, index and foreign key");
    require(!action.isEnabled(), "destructive changes require explicit opt-in");
    destructive.setChecked(true);
    sql(other, "UPDATE compare_base SET old_label = 'preserved after concurrent write' WHERE id = 1");
    apply(studio, u"The transaction was rolled back");
    require(widget<QLabel>(studio, "compareStatus").text().contains(u"destination changed") && !destination.state().transactionActive,
        "a data-only destination revision change blocks and closes the deployment transaction");
    require(sql(other, "SELECT old_label, obsolete FROM compare_base").size() == 1, "stale preview did not change the destination schema");
    refresh.click();
    waitFor(studio, u"Catalogs captured");
    require(!action.isEnabled(), "capture clears destructive approval");
    destructive.setChecked(true);
    apply(studio, u"Plan committed");
    const auto retained = sql(other, "SELECT display_label FROM compare_base");
    require(retained.at(0).toArray().at(0).toObject().value("v").toString() == u"preserved after concurrent write", "rename preserved the destination value");
    sql(other, "INSERT INTO compare_child VALUES (1, 1)");
    require(sql(other, "SELECT id FROM compare_child").size() == 1, "dependency-ordered table and foreign key were applied");

    mappings.removeRow(0);
    sql(source, "CREATE TABLE compare_rollback (id BIGINT PRIMARY KEY)");
    sql(destination, "CREATE TABLE compare_rollback (id BIGINT PRIMARY KEY)");
    sql(destination, "INSERT INTO compare_rollback VALUES (1)");
    sql(source, "ALTER TABLE compare_rollback ADD COLUMN before_failed_constraint TEXT");
    sql(source, "ALTER TABLE compare_rollback ADD CONSTRAINT impossible_on_target CHECK (id > 1)");
    refresh.click();
    waitFor(studio, u"Catalogs captured");
    require(preview.toPlainText().contains(u"ADD COLUMN \"before_failed_constraint\"") &&
        preview.toPlainText().contains(u"CONSTRAINT \"impossible_on_target\""), "preview includes an earlier change and a later failing constraint");
    apply(studio, u"The transaction was rolled back");
    const auto metadata = await([&](auto done) { return other.refreshMetadata(done); });
    for (const auto item : metadata.data.value("objects").toArray())
    {
        const auto object = item.toObject();
        if (object.value("qualified_name").toArray().last().toString() != u"compare_rollback") continue;
        for (const auto column : object.value("columns").toArray())
            require(column.toObject().value("name").toString() != u"before_failed_constraint", "later error rolled back the earlier ALTER");
    }

    sql(source, "CREATE TABLE compare_values (k TEXT, amount DECIMAL(15,4), label TEXT)");
    sql(destination, "CREATE TABLE compare_values (k TEXT, amount DECIMAL(16,2), label TEXT)");
    sql(source, "INSERT INTO compare_values VALUES ('İ', 1.2000, 'İzmir'), ('ı', 2.3500, NULL)");
    sql(destination, "INSERT INTO compare_values VALUES ('I', 2.35, NULL), ('i', 1.20, 'izmir')");
    refresh.click();
    waitFor(studio, u"Catalogs captured");
    tableInputs(studio, u"\"public\".\"compare_values\"", QStringLiteral("k"), QStringLiteral("amount, label"));
    widget<QPushButton>(studio, "compareData").click();
    waitFor(studio, u"Data differences found");
    widget<QComboBox>(studio, "compareCollation").setCurrentIndex(1);
    widget<QPushButton>(studio, "compareData").click();
    waitFor(studio, u"The compared data matches");
    const auto data = widget<QPlainTextEdit>(studio, "compareDataOutput").toPlainText();
    require(data.contains(u"Source rows 2") && data.contains(u"Destination rows 2"), "streaming compared both independently ordered snapshots");
    widget<QLineEdit>(studio, "compareLimit").setText(QStringLiteral("1"));
    widget<QPushButton>(studio, "compareData").click();
    waitFor(studio, u"row limit was exceeded");
    widget<QLineEdit>(studio, "compareLimit").setText(QStringLiteral("10"));
    sql(source, "INSERT INTO compare_values VALUES ('i', 9.0000, 'duplicate')");
    widget<QPushButton>(studio, "compareData").click();
    waitFor(studio, u"Duplicate key");
    sql(source, "DELETE FROM compare_values WHERE k = 'i'");
    sql(source, "INSERT INTO compare_values VALUES (NULL, 9.0000, 'missing')");
    widget<QPushButton>(studio, "compareData").click();
    waitFor(studio, u"key contains NULL");

    widget<QLineEdit>(studio, "compareSourceObject").setText(QStringLiteral("compare_values"));
    widget<QLineEdit>(studio, "compareDestinationObject").setText(QStringLiteral("compare_values"));
    widget<QPushButton>(studio, "compareHistorical").click();
    waitFor(studio, u"The visible saved definitions differ");
    std::cout << "S43-T01: TLS rename and destructive review, concurrent stale revision, atomic apply, typed data and historical definitions passed.\n";
}
