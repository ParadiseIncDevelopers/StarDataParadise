#include "admin_center.hpp"
#include "backup_center.hpp"
#include "history_decision_smoke_support.hpp"
#include "security_center.hpp"
#include "workbench.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QSaveFile>
#include <QSpinBox>
#include <QThreadPool>
#include <QTreeWidget>
#include <QUuid>

using namespace studio_smoke;
using namespace wb;

namespace
{
constexpr auto originalMarker = "snapshot only";
constexpr auto sourceMarker = "source after verified backup";
constexpr auto guestName = "wb_dba_reader";

QString token() { return QUuid::createUuid().toString(QUuid::Id128); }

QJsonObject body(const QPlainTextEdit& editor)
{
    return QJsonDocument::fromJson(editor.toPlainText().toUtf8()).object();
}

template<class Matches>
QJsonObject expectBody(QDialog& dialog, const char* pane, const char* status, Matches matches, const char* description,
    int timeout = 13000)
{
    auto& editor = control<QPlainTextEdit>(dialog, pane);
    if (!waitFor([&] { return matches(body(editor)); }, timeout))
        std::cerr << control<QLabel>(dialog, status).text().toStdString() << '\n'
                  << editor.toPlainText().toStdString() << '\n';
    const auto result = body(editor);
    require(matches(result), description);
    return result;
}

void readyClick(QDialog& dialog, const char* action)
{
    require(waitFor([&] { return control<QPushButton>(dialog, action).isEnabled(); }), action);
    click(dialog, action);
}

void confirm(QDialog& dialog, const char* action)
{
    require(waitFor([&] { return control<QPushButton>(dialog, action).isEnabled(); }), action);
    QTimer::singleShot(0, &dialog, [&] {
        auto* confirmation = dialog.findChild<QMessageBox*>();
        if (!confirmation) confirmation = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        require(confirmation && confirmation->button(QMessageBox::Yes), "explicit review confirmation");
        confirmation->button(QMessageBox::Yes)->click();
    });
    click(dialog, action);
}

void choose(QDialog& dialog, const char* name, const QString& value)
{
    auto& box = control<QComboBox>(dialog, name);
    for (int index = 0; index < box.count(); ++index)
    {
        if (box.itemData(index).toString().compare(value, Qt::CaseInsensitive) == 0 ||
            box.itemText(index).compare(value, Qt::CaseInsensitive) == 0)
        {
            box.setCurrentIndex(index);
            return;
        }
    }
    require(false, name);
}

void chooseTable(QDialog& dialog, QStringView table)
{
    auto& target = control<QComboBox>(dialog, "permissionTarget");
    for (int index = 0; index < target.count(); ++index)
        if (target.itemText(index).contains(table))
        {
            target.setCurrentIndex(index);
            return;
        }
    require(false, "permission editor contains the real table");
}

void expectMarker(SqlSession& session, const QString& marker)
{
    require(select(session, "SELECT note FROM wb_dba_marker WHERE id=1") ==
        QJsonArray{QJsonValue(QJsonArray{text(marker)})}, "the isolated Server returned the expected snapshot marker");
}

void writeJson(const QString& fileName, const QJsonObject& value)
{
    QSaveFile file(fileName);
    require(file.open(QIODevice::WriteOnly), "open fixture output");
    const auto bytes = QJsonDocument(value).toJson();
    require(file.write(bytes) == bytes.size() && file.commit(), "save fixture output");
}

QByteArray readFile(const QString& fileName)
{
    QFile file(fileName);
    require(file.open(QIODevice::ReadOnly), "open fixture output for validation");
    return file.readAll();
}

QJsonObject healthRecord(QDialog& admin, const QString& name, const QString& status)
{
    const auto payload = expectBody(admin, "adminHealthPayload", "adminStatusText", [&](const QJsonObject& response) {
        return response.value("checks").toObject().value(name).toObject().value("status").toString() == status;
    }, "Health Center shows the expected measured or unknown state");
    return payload.value("checks").toObject().value(name).toObject();
}

void checkHealth(QDialog& admin, const QString& name)
{
    choose(admin, "adminCheckName", name);
    readyClick(admin, "adminRunCheck");
}

void expectHealthUi(QDialog& admin, QStringView name, QStringView status)
{
    const auto& tree = control<QTreeWidget>(admin, "adminHealthChecks");
    for (int index = 0; index < tree.topLevelItemCount(); ++index)
    {
        const auto* item = tree.topLevelItem(index);
        if (item->text(0).compare(name, Qt::CaseInsensitive) == 0 && item->text(1).compare(status, Qt::CaseInsensitive) == 0)
            return;
    }
    require(false, "visible health row carries the true status");
}

QString backupAndRestore(SqlSession& session, QDialog& backup)
{
    confirm(backup, "backupSubmit");
    const auto finished = expectBody(backup, "backupJobResult", "backupCenterStatus", [](const QJsonObject& status) {
        return status.value("state").toString() == u"succeeded";
    }, "Server created a real verified backup in the worker", 25000);
    const auto archive = finished.value("result").toObject();
    const auto id = archive.value("backup_id").toString();
    const auto digest = archive.value("manifest_sha256").toString();
    require(id.size() == 32 && digest.size() == 64 && archive.value("verified").toBool(),
        "backup job returned its archive identity and verified manifest hash");
    control<QLineEdit>(backup, "backupId").setText(id);
    readyClick(backup, "backupVerify");
    const auto verified = expectBody(backup, "backupVerifyResult", "backupCenterStatus", [&](const QJsonObject& result) {
        return result.value("verified").toBool() && result.value("backup_id").toString() == id;
    }, "Workbench independently verified the chosen backup");
    require(verified.value("manifest_sha256").toString() == digest, "verification matches the worker's manifest");
    write(session, "UPDATE wb_dba_marker SET note='source after verified backup' WHERE id=1");
    control<QLineEdit>(backup, "backupRestoreName").setText("wb-verified");
    confirm(backup, "backupRestore");
    expectBody(backup, "backupRestoreResult", "backupCenterStatus", [&](const QJsonObject& result) {
        return result.value("restored").toBool() && result.value("target_name").toString() == u"wb-verified" &&
            result.value("backup_id").toString() == id;
    }, "Workbench restored verified backup to a separate Server-owned target");
    expectMarker(session, sourceMarker);
    return id;
}

void support(QDialog& admin, const QDir& root)
{
    readyClick(admin, "adminSupportPreview");
    const auto preview = expectBody(admin, "adminSupportPayload", "adminStatusText", [](const QJsonObject& result) {
        return result.value("secret_scan_passed").toBool();
    }, "support preview showed successful secret scan");
    require(preview.value("log_counts").toObject().value("operational").toInt() >= 2,
        "preview processed real Server log evidence");
    const auto summary = root.filePath("support-summary.json");
    control<QLineEdit>(admin, "adminSupportExportPath").setText(summary);
    readyClick(admin, "adminSupportExport");
    require(waitFor([&] { return QFileInfo::exists(summary); }), "support export wrote the approved local summary");
    require(QJsonDocument::fromJson(readFile(summary)).object().value("secret_scan_passed").toBool(),
        "local support summary carries a positive redaction receipt");
    control<QLineEdit>(admin, "adminSupportTarget").setText("wb-redacted");
    confirm(admin, "adminSupportCreate");
    expectBody(admin, "adminSupportPayload", "adminStatusText", [](const QJsonObject& result) {
        return result.value("target_name").toString() == u"wb-redacted";
    }, "Server created a redacted support artifact under the reviewed target");
}

void cliPermission(const QString& executable, const QString& profile, const QByteArray& password, bool allowed)
{
    QProcess cli;
    cli.setProgram(executable);
    cli.setArguments({"--profile", profile, "--password-stdin", "--format", "json", "--color", "never",
                     "--command", "SELECT note FROM wb_dba_marker WHERE id=1"});
    cli.start();
    require(cli.waitForStarted(5000), "approved CLI executable started");
    cli.write(password + QByteArray("\n"));
    cli.closeWriteChannel();
    require(cli.waitForFinished(8000) && cli.exitStatus() == QProcess::NormalExit, "CLI permission check finished");
    const auto output = cli.readAllStandardOutput();
    const auto errors = cli.readAllStandardError();
    require(!output.contains(password) && !errors.contains(password), "CLI output protected the fixture credential");
    const auto document = QJsonDocument::fromJson(output).object();
    bool matched = false;
    for (const auto event : document.value("events").toArray())
    {
        const auto record = event.toObject();
        const auto detail = record.value("data").toObject();
        if (allowed && record.value("type").toString() == u"batch")
            matched |= detail.value("rows").toArray() == QJsonArray{QJsonValue(QJsonArray{text(sourceMarker)})};
        if (!allowed && record.value("type").toString() == u"error")
            matched |= detail.value("code").toInt() == SDP_PERMISSION_DENIED;
    }
    if (!matched) std::cerr << output.constData() << '\n' << errors.constData() << '\n';
    require(matched && cli.exitCode() == (allowed ? 0 : 3), "real CLI observes the same grant or revoke as Native SDK");
}

void permissions(SqlSession& owner, Profile profile, QThreadPool& workers, QWidget& parent,
    const QDir& root, const QString& cli)
{
    write(owner, "CREATE USER wb_dba_reader");
    Password guest{token().toLatin1()};
    const auto request = token().toLatin1();
    success(await([&](auto done) {
        return owner.request([&](auto handle, auto*, auto* error) {
            return sdp_password_set(handle, wire(guestName), {}, wire(guest.bytes), wire(request), error);
        }, done);
    }), "isolated guest password set");
    profile.user = guestName;
    SqlSession user(profile, connect(profile, guest), workers);
    const auto cliProfile = root.filePath("native-cli-profile.json");
    writeJson(cliProfile, {{"host", profile.host}, {"port", profile.port}, {"tls_name", profile.tls_name},
        {"user", profile.user}, {"ca_file", profile.ca_file}, {"timeout_ms", 5000}});
    const QString query = "SELECT note FROM wb_dba_marker WHERE id=1";
    require(sql(user, query).error.code == SDP_PERMISSION_DENIED, "guest starts without SELECT");

    auto* dialog = showSecurityCenter(owner, catalog(owner), &parent);
    require(dialog != nullptr, "Security Center opened");
    control<QLineEdit>(*dialog, "permissionPrincipal").setText(guestName);
    choose(*dialog, "permissionPrincipalKind", "USER");
    choose(*dialog, "permissionPrivilege", "SELECT");
    choose(*dialog, "permissionScope", "TABLE");
    chooseTable(*dialog, u"wb_dba_marker");
    auto permissionChange = [&](const char* action) {
        confirm(*dialog, action);
        require(waitFor([&] {
            const auto receipt = control<QPlainTextEdit>(*dialog, "permissionResult").toPlainText();
            return !owner.busy() && !receipt.isEmpty() &&
                control<QLabel>(*dialog, "permissionStatus").text().contains(u"Server confirmed", Qt::CaseInsensitive);
        }), "Security Center committed the explicit grant or revoke");
    };
    permissionChange("permissionGrant");
    expectMarker(user, sourceMarker);
    cliPermission(cli, cliProfile, guest.bytes, true);
    permissionChange("permissionRevoke");
    require(sql(user, query).error.code == SDP_PERMISSION_DENIED,
        "existing Native connection immediately lost the revoked permission");
    cliPermission(cli, cliProfile, guest.bytes, false);
    dialog->close();
    std::cout << "Workbench GRANT and REVOKE affected the same live Native session and the independent CLI\n";
}

void profileRoundtrip(const Profile& current, Password& administrator, const QString& profilePath, const QDir& root)
{
    const auto workspacePath = root.filePath("isolated-workspace.json");
    const auto exported = root.filePath("roundtrip-profiles.json");
    const auto afterFile = root.filePath("profiles-after-rejected-import.json");
    const auto invalid = root.filePath("unsupported-workspace-field.json");
    Window window(workspacePath);
    window.show();
    window.import_from(profilePath);
    require(waitFor([&] { return QFileInfo::exists(workspacePath); }), "Workbench imported and saved the safe profile");
    window.open_profile(current, administrator.bytes);
    require(waitFor([&] { return window.session_count() == 1; }), "real logged-in Workbench worksheet before profile export");
    window.export_to(exported);
    require(waitFor([&] { return QFileInfo::exists(exported); }), "Workbench exported the active workspace profiles");
    const auto safeBytes = readFile(exported);
    require(!safeBytes.contains(administrator.bytes) && !safeBytes.contains("password"), "profile export contains no credential");
    QString problem;
    const auto imported = import_profiles(exported, problem);
    require(problem.isEmpty() && imported.size() == 1 && imported[0].host == current.host &&
        imported[0].user == current.user && imported[0].port == current.port, "profile export imports as the same endpoint");

    auto unsafe = QJsonDocument::fromJson(safeBytes).object();
    unsafe.insert("post_import_command", "unused-fixture-action");
    writeJson(invalid, unsafe);
    require(import_profiles(invalid, problem).isEmpty() && !problem.isEmpty(),
        "profile validation rejects unsupported action metadata");
    window.import_from(invalid);
    auto& tasks = control<QListWidget>(window, "connectionTasks");
    require(waitFor([&] {
        for (int index = 0; index < tasks.count(); ++index)
            if (tasks.item(index)->text() == problem) return true;
        return false;
    }), "Workbench reports the invalid workspace operation before exporting");
    window.export_to(afterFile);
    require(waitFor([&] { return QFileInfo::exists(afterFile); }), "profile export after rejected import completed");
    require(QJsonDocument::fromJson(readFile(afterFile)) == QJsonDocument::fromJson(safeBytes) &&
        window.session_count() == 1, "unsupported metadata did not add an action or alter active profiles");
    window.close_session(0);
}

QString createSchedule(QDialog& backup, const QString& kind, const QString& dataset = {})
{
    const auto previousId = body(control<QPlainTextEdit>(backup, "scheduleResult")).value("schedule_id").toString();
    choose(backup, "scheduleKind", kind);
    choose(backup, "scheduleType", "interval");
    control<QSpinBox>(backup, "scheduleSeconds").setValue(8);
    if (kind == u"compute") control<QSpinBox>(backup, "scheduleCount").setValue(42000);
    if (kind == u"sql_script")
        control<QPlainTextEdit>(backup, "scheduleSql").setPlainText(
            "INSERT INTO wb_scheduled_sql VALUES (1); INSERT INTO wb_scheduled_sql VALUES (2);");
    if (kind == u"retrain")
    {
        control<QLineEdit>(backup, "scheduleModel").setText("wb_scheduled_model");
        control<QLineEdit>(backup, "scheduleDataset").setText(dataset);
        control<QSpinBox>(backup, "scheduleSeed").setValue(23);
    }
    confirm(backup, "scheduleCreate");
    const auto receipt = expectBody(backup, "scheduleResult", "backupCenterStatus", [&](const QJsonObject& result) {
        const auto id = result.value("schedule_id").toString();
        return id.size() == 32 && id != previousId && result.value("enabled").toBool();
    }, "Workbench got a durable schedule ID");
    return receipt.value("schedule_id").toString();
}

void sourcePhase(SqlSession& session, const Profile& profile, Password& administrator, QThreadPool& workers,
    const QString& profilePath, const QString& statePath, const QString& cli, QWidget& parent)
{
    const auto folder = QFileInfo(statePath).dir();
    write(session, "CREATE TABLE wb_dba_marker (id BIGINT PRIMARY KEY, note TEXT NOT NULL)");
    write(session, "INSERT INTO wb_dba_marker VALUES (1,'snapshot only')");
    write(session, "CREATE FUNCTION wb_backup_identity(input_value IN BIGINT) RETURN BIGINT AS BEGIN RETURN input_value; END;");
    auto* admin = showAdminCenter(session, &parent);
    auto* backup = showBackupCenter(session, &parent);
    require(admin && backup, "Admin and Backup centers opened");

    readyClick(*admin, "adminLoadHealth");
    for (const auto name : {"storage", "history", "backup", "certificate", "jobs"})
    {
        const auto record = healthRecord(*admin, name, "UNKNOWN");
        require(record.value("checked_at").isNull(), "unchecked health never showed a measured timestamp");
        expectHealthUi(*admin, QString::fromLatin1(name), u"UNKNOWN");
    }
    checkHealth(*admin, "storage");
    const auto storage = healthRecord(*admin, "storage", "OK");
    require(!storage.value("checked_at").isNull() && !storage.value("evidence").toObject().isEmpty(),
        "storage OK requires actual checked_at and evidence");
    expectHealthUi(*admin, u"storage", u"OK");
    checkHealth(*admin, "backup");
    require(waitFor([&] { return !session.busy(); }), "Server answered the unverified backup health check");
    const auto unchanged = healthRecord(*admin, "backup", "UNKNOWN");
    require(unchanged.value("status").toString() == u"UNKNOWN", "without a verified archive backup remains unknown");
    const auto archived = backupAndRestore(session, *backup);
    checkHealth(*admin, "backup");
    const auto measuredBackup = healthRecord(*admin, "backup", "OK");
    require(measuredBackup.value("evidence").toObject().value("backup_id").toString() == archived,
        "backup health binds to the actual verified archive");
    support(*admin, folder);
    admin->close();
    profileRoundtrip(profile, administrator, profilePath, folder);
    permissions(session, profile, workers, parent, folder, cli);

    auto fixture = QJsonDocument::fromJson(readFile(statePath)).object();
    const auto dataset = fixture.value("dataset_id").toString();
    require(dataset.size() == 32, "fixture provides an immutable training dataset");
    fixture.insert("schedule_id", createSchedule(*backup, "compute"));
    fixture.insert("sql_schedule_id", createSchedule(*backup, "sql_script"));
    fixture.insert("retrain_schedule_id", createSchedule(*backup, "retrain", dataset));
    writeJson(statePath, fixture);
    std::cout << "Workbench exits after creating the Server schedule and a separately verified restore target\n";
}

void reopenPhase(SqlSession& session, QWidget& window, const QString& statePath)
{
    const auto fixture = QJsonDocument::fromJson(readFile(statePath)).object();
    const auto schedule = fixture.value("schedule_id").toString();
    const auto observed = fixture.value("observed_job_id").toString();
    require(schedule.size() == 32 && observed.size() == 32, "reopen fixture carries concrete scheduler identities");
    auto* backup = showBackupCenter(session, &window);
    require(backup != nullptr, "fresh Workbench opens Backup and Schedule Center");
    control<QLineEdit>(*backup, "scheduleId").setText(schedule);
    readyClick(*backup, "scheduleStatus");
    expectBody(*backup, "scheduleResult", "backupCenterStatus", [&](const QJsonObject& result) {
        return result.value("schedule_id").toString() == schedule &&
            result.value("last_job_id").toString() == observed && result.value("enabled").toBool();
    }, "fresh Workbench observes the exact job the Server ran while closed");
    readyClick(*backup, "scheduleHistory");
    expectBody(*backup, "scheduleHistoryResult", "backupCenterStatus", [&](const QJsonObject& result) {
        return result.value("job_id").toString() == observed && result.value("state").toString() == u"succeeded" &&
            result.value("result").toObject().value("count").toInt() == 42000;
    }, "fresh Workbench reports the actual completed scheduled job");
    confirm(*backup, "scheduleDisable");
    expectBody(*backup, "scheduleResult", "backupCenterStatus", [&](const QJsonObject& result) {
        return result.value("schedule_id").toString() == schedule && result.value("enabled").isBool() &&
            !result.value("enabled").toBool();
    }, "Workbench explicitly stopped the verified recurring schedule");
    for (const auto& kind : {QStringLiteral("sql"), QStringLiteral("retrain")})
    {
        const auto id = fixture.value(kind + "_schedule_id").toString();
        const auto job = fixture.value(kind + "_observed_job_id").toString();
        control<QLineEdit>(*backup, "scheduleId").setText(id);
        readyClick(*backup, "scheduleStatus");
        expectBody(*backup, "scheduleResult", "backupCenterStatus", [&](const QJsonObject& result) {
            return result.value("schedule_id").toString() == id && result.value("last_job_id").toString() == job;
        }, "Workbench recovered the persistent recipe schedule and run");
        readyClick(*backup, "scheduleHistory");
        expectBody(*backup, "scheduleHistoryResult", "backupCenterStatus", [&](const QJsonObject& result) {
            return result.value("job_id").toString() == job && result.value("state").toString() == u"succeeded";
        }, "Workbench reported the successful scheduled recipe");
        confirm(*backup, "scheduleDisable");
        expectBody(*backup, "scheduleResult", "backupCenterStatus", [&](const QJsonObject& result) {
            return result.value("schedule_id").toString() == id &&
                result.value("enabled").isBool() && !result.value("enabled").toBool();
        }, "Workbench stopped future recipe runs");
    }
    require(select(session, "SELECT id FROM wb_scheduled_sql ORDER BY id") ==
        QJsonArray{QJsonArray{QJsonObject{{"t", "i64"}, {"v", "1"}}},
            QJsonArray{QJsonObject{{"t", "i64"}, {"v", "2"}}}}, "scheduled SQL committed both statements");
    expectMarker(session, sourceMarker);
    std::cout << "S42-T02: permissions enforced in CLI and Native; Server schedule completed while Workbench was closed\n";
}

void restoredPhase(SqlSession& session)
{
    expectMarker(session, originalMarker);
    require(select(session, "SELECT wb_backup_identity(42)") == QJsonArray{QJsonValue(QJsonArray{integer(42)})},
        "independent restored Server executes the captured routine");
    const auto proof = success(history(session, SDP_HISTORY_VERIFY), "restored History verification").data;
    require(proof.value("verified").toBool(), "restored TLS Server verified native History");
    std::cout << "S42-T01: unchecked health stayed UNKNOWN; verified backup restored into a distinct checked TLS Server\n";
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    require(argc == 5, "expected profile, phase, fixture state and CLI arguments");
    const auto profile = fixtureProfile(2, argv);
    const auto profilePath = QString::fromLocal8Bit(argv[1]);
    const auto phase = QString::fromLatin1(argv[2]);
    const auto statePath = QString::fromLocal8Bit(argv[3]);
    const auto cli = QString::fromLocal8Bit(argv[4]);
    require(QFileInfo(cli).isAbsolute() && QFileInfo(cli).isFile(), "approved CLI path is explicit and valid");
    Password administrator{fixturePassword()};
    QThreadPool workers;
    SqlSession session(profile, connect(profile, administrator), workers);
    QWidget parent;
    parent.show();
    if (phase == u"source") sourcePhase(session, profile, administrator, workers, profilePath, statePath, cli, parent);
    else if (phase == u"reopen") reopenPhase(session, parent, statePath);
    else if (phase == u"restored") restoredPhase(session);
    else require(false, "unknown acceptance phase");
    return 0;
}
