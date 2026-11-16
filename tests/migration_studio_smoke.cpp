#include "history_decision_smoke_support.hpp"
#include "migration_studio.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSpinBox>
#include <QThreadPool>

using namespace studio_smoke;
using namespace wb;

namespace
{
constexpr int customerRows = 96;
constexpr int invoiceRows = 8;

QJsonObject report(QDialog& dialog)
{
    const auto& editor = control<QPlainTextEdit>(dialog, "migrationReport");
    return QJsonDocument::fromJson(editor.toPlainText().toUtf8()).object();
}

QJsonObject expectState(QDialog& dialog, const QString& expected)
{
    const auto state = [&] { return report(dialog).value("state").toString(); };
    if (!waitFor([&] { return state() == expected || state() == u"error"; }, 20000) || state() != expected)
    {
        std::cerr << control<QLabel>(dialog, "migrationStatus").text().toStdString() << '\n'
                  << QJsonDocument(report(dialog)).toJson(QJsonDocument::Compact).toStdString() << '\n';
        require(false, qPrintable(QStringLiteral("Migration state was not ") + expected));
    }
    return report(dialog);
}

QString journalPath(const QJsonObject& source, const QString& prefix)
{
    return QFileInfo(source.value("provider").toString() + "-" + prefix + "journal.json").absoluteFilePath();
}

void fill(QDialog& dialog, const QJsonObject& source, const QByteArray& password, const QString& prefix)
{
    auto& provider = control<QComboBox>(dialog, "migrationProvider");
    const auto index = provider.findData(source.value("provider").toString());
    require(index >= 0, "missing external provider option");
    provider.setCurrentIndex(index);
    for (const auto& [field, key] : {
             std::pair{"migrationDriver", "driver"}, {"migrationHost", "host"}, {"migrationDatabase", "database"},
             {"migrationSchema", "schema"}, {"migrationUser", "username"}, {"migrationCa", "ca"}})
        control<QLineEdit>(dialog, field).setText(source.value(key).toString());
    control<QSpinBox>(dialog, "migrationPort").setValue(source.value("port").toInt());
    control<QLineEdit>(dialog, "migrationPassword").setText(QString::fromLatin1(password));
    control<QLineEdit>(dialog, "migrationPrefix").setText(prefix);
    control<QLineEdit>(dialog, "migrationJournal").setText(journalPath(source, prefix));
}

void selectTables(QDialog& dialog)
{
    auto& items = control<QListWidget>(dialog, "migrationTables");
    bool customer = false;
    bool invoice = false;
    for (int index = 0; index < items.count(); ++index)
    {
        auto* item = items.item(index);
        const auto name = item->data(Qt::UserRole).toString().isEmpty() ? item->text() : item->data(Qt::UserRole).toString();
        if (name == u"customer") customer = true;
        if (name == u"invoice") invoice = true;
        item->setCheckState(name == u"customer" || name == u"invoice" ? Qt::Checked : Qt::Unchecked);
    }
    require(customer && invoice, "real driver did not discover both parent and child tables");
}

void confirmApply(QDialog& dialog)
{
    QTimer confirmation;
    QObject::connect(&confirmation, &QTimer::timeout, &dialog, [&] {
        auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (message && message->button(QMessageBox::Yes)) message->button(QMessageBox::Yes)->click();
    });
    confirmation.start(10);
    click(dialog, "migrationApply");
}

void preview(QDialog& dialog)
{
    click(dialog, "migrationDiscover");
    expectState(dialog, "discovered");
    selectTables(dialog);
    click(dialog, "migrationPreview");
    expectState(dialog, "ready");
}

int count(SqlSession& session, const QString& table)
{
    const auto rows = select(session, "SELECT COUNT(*) FROM " + table);
    require(rows.size() == 1 && rows.at(0).toArray().size() == 1, "destination count result format");
    const auto value = rows.at(0).toArray().at(0).toObject();
    bool valid{};
    const auto total = value.value("v").toString().toInt(&valid);
    require(value.value("t").toString() == u"i64" && valid, "destination COUNT(*) did not return a typed integer");
    return total;
}

void pauseAfterCommit(QDialog& dialog)
{
    auto& editor = control<QPlainTextEdit>(dialog, "migrationReport");
    auto& pause = control<QPushButton>(dialog, "migrationPause");
    bool requested = false;
    const auto wish = [&] {
        const auto progress = report(dialog);
        if (!requested && pause.isEnabled() && progress.value("state").toString() == u"running" &&
            progress.value("committed_rows").toInt() > 0)
        {
            requested = true;
            pause.click();
        }
    };
    const auto listener = QObject::connect(&editor, &QPlainTextEdit::textChanged, &dialog, wish);
    confirmApply(dialog);
    const auto paused = expectState(dialog, "paused");
    QObject::disconnect(listener);
    const auto partial = paused.value("committed_rows").toInt();
    require(requested && partial > 0 && partial < customerRows + invoiceRows,
        "pause must occur after a durable chunk and before the migration completes");
}

void verifyManifest(const QJsonObject& complete)
{
    require(complete.value("committed_rows").toInt() == customerRows + invoiceRows,
        "migration report did not count all verified rows");
    const QRegularExpression checksum(QStringLiteral("^[0-9a-f]{64}$"));
    const auto tables = complete.value("tables").toArray();
    require(tables.size() == 2, "migration manifest did not contain both tables");
    for (const auto& item : tables)
    {
        const auto table = item.toObject();
        const auto name = table.value("name").toString();
        require((name.endsWith(u"customer") && table.value("row_count").toInt() == customerRows) ||
                (name.endsWith(u"invoice") && table.value("row_count").toInt() == invoiceRows),
            "migration manifest did not retain per-table row counts");
        require(checksum.match(table.value("checksum").toString()).hasMatch(),
            "migration manifest did not retain a table checksum");
    }
}

void verifyValues(SqlSession& session)
{
    require(count(session, "good_customer") == customerRows && count(session, "good_invoice") == invoiceRows,
        "resumed migration lost or duplicated destination rows");
    const auto rows = select(session,
        "SELECT caption, amount, observed_at FROM good_customer WHERE id<=3 ORDER BY id");
    const QJsonObject price{{"t", "decimal"}, {"unscaled", "12345678901234567890123456712345678901"}, {"scale", 11}};
    const auto epoch = QDateTime::fromString(QStringLiteral("2026-01-12T08:43:00Z"), Qt::ISODate).toSecsSinceEpoch();
    const QJsonObject when{{"t", "timestamp"}, {"seconds", QString::number(epoch)}, {"nanos", 0}, {"offset_minutes", 0}};
    const auto expected = QJsonArray{
        QJsonArray{text(QStringLiteral("İzmir – Şırnak")), price, when},
        QJsonArray{QJsonObject{{"t", "null"}}, price, when},
        QJsonArray{text(QString{}), price, when}};
    require(rows == expected, "driver migration changed Unicode, DECIMAL precision, UTC timestamp, NULL or empty text");
    require(sql(session, "INSERT INTO good_invoice VALUES (987, 99999, 1.00000000000)").error.code != SDP_OK &&
        count(session, "good_invoice") == invoiceRows, "migrated foreign key did not protect the child table");
}

void initial(SqlSession& session, QThreadPool& workers, QWidget& parent, const QJsonObject& source,
    const QByteArray& password)
{
    auto* good = showMigrationStudio(session, workers, &parent);
    require(good, "migration dialog did not open");
    auto wrongHost = source;
    wrongHost["host"] = "127.0.0.1";
    fill(*good, wrongHost, password, "good_");
    click(*good, "migrationDiscover");
    expectState(*good, "error");
    fill(*good, source, password, "good_");
    preview(*good);
    const auto unsupported = QJsonDocument(report(*good).value("unsupported").toArray()).toJson(QJsonDocument::Compact);
    require(unsupported.contains("customer_view") && unsupported.contains("customer_function"),
        "driver did not explicitly report the unsupported view and routine");
    if (source.value("provider").toString() == u"postgresql")
        require(unsupported.contains("customer_audit"), "PostgreSQL trigger was missing from the incompatibility report");
    pauseAfterCommit(*good);
    require(QFileInfo::exists(journalPath(source, "good_")), "the first migration did not persist a resume journal");
    good->close();

    auto* stale = showMigrationStudio(session, workers, &parent);
    require(stale, "migration dialog did not reopen");
    fill(*stale, source, password, "stale_");
    preview(*stale);
    pauseAfterCommit(*stale);
    require(QFileInfo::exists(journalPath(source, "stale_")), "the interrupted migration did not persist a resume journal");
    stale->close();
}

void resume(SqlSession& session, QThreadPool& workers, QWidget& parent, const QJsonObject& source,
    const QByteArray& password)
{
    const auto before = count(session, "good_customer");
    require(before > 0 && before < customerRows, "expected committed partial rows in a new Workbench process");
    auto* dialog = showMigrationStudio(session, workers, &parent);
    require(dialog, "migration dialog did not reopen for the valid checkpoint");
    fill(*dialog, source, password, "good_");
    preview(*dialog);
    confirmApply(*dialog);
    verifyManifest(expectState(*dialog, "complete"));
    verifyValues(session);
    std::cout << "S43-T02/T03: verified TLS identity, restart and duplicate-free resume, exact values, FK and unsupported objects\n";
}

void changed(SqlSession& session, QThreadPool& workers, QWidget& parent, const QJsonObject& source,
    const QByteArray& password)
{
    const auto before = count(session, "stale_customer");
    require(before > 0 && before < customerRows, "expected committed incomplete data before restarting Workbench");
    auto* dialog = showMigrationStudio(session, workers, &parent);
    require(dialog, "migration dialog did not reopen for journal resume");
    fill(*dialog, source, password, "stale_");
    click(*dialog, "migrationDiscover");
    expectState(*dialog, "discovered");
    selectTables(*dialog);
    click(*dialog, "migrationPreview");
    const auto failure = expectState(*dialog, "error");
    require(failure.value("code").toString() == u"source_snapshot_changed",
        "reopened migration did not detect that the real source changed");
    require(!control<QPushButton>(*dialog, "migrationApply").isEnabled(),
        "changed source should disable the resume action");
    require(count(session, "stale_customer") == before && count(session, "good_customer") == customerRows,
        "stale resume changed committed partial data or touched another migration");
    std::cout << "S43-T02: new Workbench process refused changed source without advancing the journal\n";
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    require(argc == 4, "expected destination profile, source settings and phase");
    const auto profile = fixtureProfile(2, argv);
    QFile settings(QString::fromLocal8Bit(argv[2]));
    require(settings.open(QIODevice::ReadOnly), "cannot open public fixture source settings");
    const auto source = QJsonDocument::fromJson(settings.readAll()).object();
    require(!source.isEmpty(), "cannot parse public fixture source settings");

    Password destination{fixturePassword()};
    auto sourceSecret = fixturePassword();
    QThreadPool workers;
    SqlSession session(profile, connect(profile, destination), workers);
    QWidget parent;
    parent.show();
    const auto phase = QString::fromLatin1(argv[3]);
    if (phase == u"initial") initial(session, workers, parent, source, sourceSecret);
    else if (phase == u"resume") resume(session, workers, parent, source, sourceSecret);
    else if (phase == u"changed") changed(session, workers, parent, source, sourceSecret);
    else require(false, "unknown migration acceptance phase");
    sourceSecret.fill('\0');
    return 0;
}
