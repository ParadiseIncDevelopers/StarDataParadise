#include "history_studio.hpp"
#include "history_decision_smoke_support.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QThreadPool>
#include <QTreeWidget>
#include <QUuid>

using namespace studio_smoke;
using namespace wb;

namespace
{
constexpr auto table = "wb_history_items";

QString message(QDialog& studio)
{
    return control<QLabel>(studio, "historyStatus").text();
}

void expectMessage(QDialog& studio, QStringView value, const char* details)
{
    const auto matches = [&] { return message(studio).contains(value, Qt::CaseInsensitive); };
    if (!waitFor(matches)) std::cerr << message(studio).toStdString() << '\n';
    require(matches(), details);
}

void historyRange(QDialog& studio, const QString& revision)
{
    control<QLineEdit>(studio, "historyFrom").setText(revision);
    control<QLineEdit>(studio, "historyTo").setText(revision);
    click(studio, "historyLoadLog");
}

bool revisionInLog(const QTreeWidget& entries, const QString& revision)
{
    for (int row = 0; row < entries.topLevelItemCount(); ++row)
        for (int column = 0; column < entries.columnCount(); ++column)
            if (entries.topLevelItem(row)->text(column) == revision)
                return true;
    return false;
}

QJsonObject preview(QDialog& studio)
{
    click(studio, "historyPreview");
    auto& apply = control<QPushButton>(studio, "historyApply");
    if (!waitFor([&] { return apply.isEnabled(); })) std::cerr << message(studio).toStdString() << '\n';
    require(apply.isEnabled(), "server offered a reviewable restore or revert plan");
    auto& display = control<QPlainTextEdit>(studio, "historyPlan");
    const auto plan = QJsonDocument::fromJson(display.toPlainText().toUtf8()).object();
    require(!plan.value("plan_id").toString().isEmpty() && !plan.value("current_revision").toString().isEmpty() &&
        plan.value("dependencies").isArray(), "displayed plan must contain the real source/current/dependency response");
    return plan.value("effects").toObject();
}

void apply(QDialog& studio)
{
    QTimer::singleShot(0, &studio, [&] {
        auto* confirmation = studio.findChild<QMessageBox*>();
        require(confirmation != nullptr, "recovery operation opened a confirmation");
        auto* yes = confirmation->button(QMessageBox::Yes);
        require(yes != nullptr, "recovery operation asks for confirmation");
        yes->click();
    });
    click(studio, "historyApply");
}

QString committed(QDialog& studio, const QString& prior)
{
    const auto revision = [&] { return studio.property("historyCommittedRevision").toString(); };
    if (!waitFor([&] { return revision().toULongLong() > prior.toULongLong(); }))
        std::cerr << message(studio).toStdString() << '\n';
    require(revision().toULongLong() > prior.toULongLong(), "restoration did not publish a new History revision");
    expectMessage(studio, revision(), "new committed revision is visible in the Studio");
    return revision();
}

void expectCurrent(SqlSession& session, const QString& comment)
{
    const auto actual = select(session, "SELECT id,note FROM wb_history_items ORDER BY id");
    const QJsonArray expected{QJsonValue(QJsonArray{integer(1), text(comment)})};
    if (actual != expected)
        std::cerr << "Expected " << QJsonDocument(expected).toJson(QJsonDocument::Compact).constData()
                  << ", got " << QJsonDocument(actual).toJson(QJsonDocument::Compact).constData() << '\n';
    require(actual == expected, "restored table has the wrong current row");
}

void restrictedHistory(SqlSession& owner, Profile profile, QThreadPool& workers, QWidget& window, const QString& revision)
{
    write(owner, "CREATE USER history_viewer");
    write(owner, "GRANT SELECT ON TABLE wb_history_items TO USER history_viewer");
    Password password{QUuid::createUuid().toString(QUuid::Id128).toLatin1()};
    const auto token = QUuid::createUuid().toString(QUuid::Id128).toLatin1();
    success(await([&](auto done) {
        return owner.request([&](auto handle, auto*, auto* error) {
            return sdp_password_set(handle, wire("history_viewer"), {}, wire(password.bytes), wire(token), error);
        }, done);
    }), "fixture account password");
    profile.user = "history_viewer";
    SqlSession guest(profile, connect(profile, password), workers);
    const auto objects = catalog(guest);
    const auto target = named(objects, u"wb_history_items");
    require(!target.isEmpty(), "restricted user still has current table SELECT");
    require(select(guest, "SELECT id FROM wb_history_items") == QJsonArray{QJsonValue(QJsonArray{integer(1)})},
        "restricted fixture's current table access must be real");
    auto* studio = showHistoryStudio(guest, objects, target, &window, {});
    require(studio != nullptr, "History Studio opened under restricted identity");
    control<QLineEdit>(*studio, "historyRevision").setText(revision);
    if (auto& read = control<QPushButton>(*studio, "historyLoadRead"); read.isEnabled()) read.click();
    expectMessage(*studio, u"permission", "Workbench reports current History permission denial");
    require(control<QTreeWidget>(*studio, "historyEntries").topLevelItemCount() == 0,
        "unauthorized account should see no historical rows");
    require(!control<QPushButton>(*studio, "historyApply").isEnabled(), "unauthorized recovery cannot be applied");
    studio->close();
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    const auto profile = fixtureProfile(argc, argv);
    Password administrator{fixturePassword()};
    QThreadPool workers;
    SqlSession session(profile, connect(profile, administrator), workers);
    SqlSession peer(profile, connect(profile, administrator), workers);
    write(session, "CREATE TABLE wb_history_items (id BIGINT PRIMARY KEY, note TEXT)");
    write(session, "CREATE TABLE wb_history_peer (id BIGINT PRIMARY KEY)");
    write(session, "INSERT INTO wb_history_items VALUES (1,'deleted original')");
    const auto source = head(session);
    const auto snapshot = success(history(session, SDP_HISTORY_READ, {{"target", table}, {"revision", source}}),
        "historical row snapshot").data.value("rows").toArray();
    require(snapshot.size() == 1, "fixture source row");
    const auto rowId = snapshot.first().toObject().value("row_id").toString();
    require(!rowId.isEmpty(), "fixture source stable row ID");
    write(session, "DELETE FROM wb_history_items WHERE id=1");
    const auto deleted = head(session);
    const auto objects = catalog(session);
    const auto target = named(objects, u"wb_history_items");
    require(!target.isEmpty(), "fixture table visible in SQL catalog");

    QWidget window;
    window.show();
    int refreshes = 0;
    auto* studio = showHistoryStudio(session, objects, target, &window, [&] { ++refreshes; });
    require(studio != nullptr, "History Studio opened");
    auto& entries = control<QTreeWidget>(*studio, "historyEntries");
    auto& recoverRevision = control<QLineEdit>(*studio, "historyRevision");
    control<QCheckBox>(*studio, "historyDeletedOnly").setChecked(true);
    require(waitFor([&] { return control<QPushButton>(*studio, "historyLoadLog").isEnabled(); }),
        "History reads became available");
    recoverRevision.setText(source);
    click(*studio, "historyLoadRead");
    auto& snapshotView = control<QPlainTextEdit>(*studio, "historySnapshot");
    require(waitFor([&] {
        const auto payload = QJsonDocument::fromJson(snapshotView.toPlainText().toUtf8()).object();
        return payload.value("revision").toString() == source && payload.value("rows").toArray() == snapshot;
    }) && snapshotView.isReadOnly(), "Workbench displays an immutable copy of the historical row");
    control<QLineEdit>(*studio, "historyFrom").setText(source);
    control<QLineEdit>(*studio, "historyTo").setText(deleted);
    click(*studio, "historyLoadDiff");
    auto& changes = control<QTreeWidget>(*studio, "historyFieldDiff");
    require(waitFor([&] {
        for (int index = 0; index < changes.topLevelItemCount(); ++index)
        {
            const auto* field = changes.topLevelItem(index);
            if (field->text(0) == u"note" && field->text(1).contains(u"deleted original")) return true;
        }
        return false;
    }), "Workbench shows the deleted historical field value in the real diff");
    historyRange(*studio, deleted);
    require(waitFor([&] {
        return !session.busy() && revisionInLog(entries, deleted) && entries.topLevelItemCount() == 1 &&
            entries.topLevelItem(0)->text(2) == u"local-admin";
    }), "deleted row found by its real History revision and actor");
    entries.setCurrentItem(entries.topLevelItem(0));
    click(*studio, "historyUseSelection");
    require(control<QLineEdit>(*studio, "historyRow").text() == rowId && recoverRevision.text() == source,
        "deletion selection opens recovery for stable row ID at its previous revision");

    require(preview(*studio).value("inserted").toInt() == 1, "initial plan preview identifies the row insertion");
    write(peer, "INSERT INTO wb_history_peer VALUES (1)");
    const auto advanced = head(peer);
    apply(*studio);
    expectMessage(*studio, u"stale_preview", "server rejected a plan reviewed before a concurrent commit");
    require(head(session) == advanced && select(session, "SELECT id FROM wb_history_items").isEmpty() &&
        !control<QPushButton>(*studio, "historyApply").isEnabled(), "stale apply did not restore a row or make a revision");

    write(peer, "INSERT INTO wb_history_items VALUES (1,'conflicting owner')");
    const auto conflictRevision = head(peer);
    click(*studio, "historyPreview");
    const auto readyOrConflict = [&] {
        return control<QPushButton>(*studio, "historyApply").isEnabled() || message(*studio).contains(u"restore_conflict");
    };
    require(waitFor(readyOrConflict), "server evaluated the conflicting recovery");
    if (control<QPushButton>(*studio, "historyApply").isEnabled()) apply(*studio);
    expectMessage(*studio, u"restore_conflict", "server found a primary key conflict");
    require(head(session) == conflictRevision, "conflict did not commit");
    expectCurrent(session, "conflicting owner");

    write(peer, "DELETE FROM wb_history_items WHERE id=1");
    const auto revalidatedAt = head(peer);
    require(preview(*studio).value("inserted").toInt() == 1, "fresh recovery review restores one row");
    apply(*studio);
    const auto restored = committed(*studio, revalidatedAt);
    const auto request = control<QPlainTextEdit>(*studio, "historyRequest").toPlainText();
    const auto copyableBody = QJsonDocument::fromJson(request.mid(request.indexOf(u'\n') + 1).toUtf8()).object();
    require(request.contains(u"SDP_HISTORY_RESTORE") && copyableBody.size() == 2 &&
        copyableBody.value("plan_id").toString().size() == 32 && copyableBody.value("request_token").toString().size() == 32,
        "successful recovery exposes the exact typed apply request");
    expectCurrent(session, "deleted original");
    require(head(session) == restored && refreshes == 1, "restored revision and related views refreshed once");
    const auto recoveredRow = success(history(session, SDP_HISTORY_READ,
        {{"target", table}, {"revision", restored}, {"row_id", rowId}}), "restored row snapshot").data.value("rows").toArray();
    require(recoveredRow == snapshot, "restoration did not change the original stable row identity");
    control<QCheckBox>(*studio, "historyDeletedOnly").setChecked(false);
    historyRange(*studio, restored);
    require(waitFor([&] { return revisionInLog(entries, restored); }), "Workbench lists the new recovery revision");
    std::cout << "S40-T01: deleted row found, live key conflict checked, restore committed with stable row identity\n";

    write(peer, "UPDATE wb_history_items SET note='field change' WHERE id=1");
    const auto changed = head(peer);
    control<QComboBox>(*studio, "historyRecoveryMode").setCurrentIndex(1);
    recoverRevision.setText(changed);
    control<QLineEdit>(*studio, "historyField").setText("note");
    require(preview(*studio).value("updated").toInt() == 1, "field revert preview updates one row");
    apply(*studio);
    const auto reverted = committed(*studio, changed);
    expectCurrent(session, "deleted original");
    require(head(session) == reverted && refreshes == 2, "field revert published exactly one observed revision");

    success(history(peer, SDP_HISTORY_PURGE, {{"revision", restored}}), "fixture retention purge");
    click(*studio, "historyLoadStatus");
    auto& retention = control<QPlainTextEdit>(*studio, "historyRetentionStatus");
    require(waitFor([&] {
        return QJsonDocument::fromJson(retention.toPlainText().toUtf8()).object()
            .value("earliest_revision").toString() == restored;
    }), "Workbench shows the actual recoverable History floor");
    click(*studio, "historyVerify");
    require(waitFor([&] {
        return QJsonDocument::fromJson(retention.toPlainText().toUtf8()).object().value("verified").toBool();
    }), "Workbench shows the server's History integrity verification");
    recoverRevision.setText(source);
    click(*studio, "historyLoadRead");
    expectMessage(*studio, u"revision_not_recoverable", "retired History was recategorized with the actual server reason");
    require(!control<QPushButton>(*studio, "historyApply").isEnabled(), "retired data cannot be applied");
    restrictedHistory(session, profile, workers, window, restored);
    std::cout << "S40-T02: stale preview, retired revision, and missing current History permission were rejected\n";
    return 0;
}
