#include "migration_studio.hpp"
#include "migration_plan.hpp"
#include "sql_session.hpp"
#include "table_codec.hpp"

#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLockFile>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QSpinBox>
#include <QThreadPool>
#include <QUuid>
#include <QVBoxLayout>

namespace wb
{
namespace
{
using namespace migration;

template<class Widget>
Widget* named(const char* name)
{
    auto* widget = new Widget;
    widget->setObjectName(QString::fromLatin1(name));
    return widget;
}

class MigrationDialog final : public QDialog
{
public:
    MigrationDialog(SqlSession& session, QThreadPool& workers, QWidget* parent);
    void reject() override;

private:
    template<class Start> SqlReply wait(Start start);
    SqlReply sql(const QString& statement, const QJsonObject& types = {}, const QJsonObject& values = {},
        SqlSession::Stream stream = {}, int limit = 1);
    SqlReply transaction(SqlTransaction command);
    ExternalSource source(bool selected) const;
    ExternalSnapshot capture(const ExternalSource& source);
    void discover();
    void preview();
    void apply();
    void cleanup();
    void resetPlan();
    void publish(const QString& state, const QString& message, const QString& code = {});
    void working(bool active);

    bool loadJournal(QJsonObject& journal) const;
    bool saveJournal(const QJsonObject& journal) const;
    QJsonArray destination() const;
    QJsonObject objects(const QJsonArray& catalog) const;
    bool verifyOwner(const QJsonObject& journal);
    bool createScope(QJsonObject& journal);
    bool readTarget(const TargetTable& table, KeyedRows& existing);
    bool copyChunk(const TargetTable& table, const QList<QJsonArray>& rows, const QJsonObject& journal);
    void rollback();

    QPointer<SqlSession> session_;
    QThreadPool& workers_;
    QWidget* settings_{};
    QComboBox* provider_{};
    QLineEdit *host_{}, *database_{}, *schema_{}, *username_{}, *password_{}, *driver_{}, *ca_{}, *prefix_{}, *journal_{};
    QSpinBox* port_{};
    QListWidget* tables_{};
    QLabel* status_{};
    QPlainTextEdit* report_{};
    QPushButton *discover_{}, *preview_{}, *apply_{}, *pause_{}, *cleanup_{};
    TargetPlan plan_;
    qsizetype committed_{};
    bool active_{}, copying_{}, pauseRequested_{};
};

MigrationDialog::MigrationDialog(SqlSession& session, QThreadPool& workers, QWidget* parent)
    : QDialog(parent), session_(&session), workers_(workers)
{
    setObjectName("migrationStudio");
    setWindowTitle(tr("Migration Studio"));
    setWindowModality(Qt::WindowModal);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(790, 730);

    auto* layout = new QVBoxLayout(this);
    auto* notice = new QLabel(tr("Copy selected tables from a read-only PostgreSQL or MySQL snapshot to the connected Server."));
    notice->setWordWrap(true);
    layout->addWidget(notice);
    settings_ = new QWidget;
    auto* form = new QFormLayout(settings_);
    provider_ = named<QComboBox>("migrationProvider");
    provider_->addItem("PostgreSQL", "postgresql");
    provider_->addItem("MySQL", "mysql");
    form->addRow(tr("Source"), provider_);
    host_ = named<QLineEdit>("migrationHost");
    host_->setText("localhost");
    port_ = named<QSpinBox>("migrationPort");
    port_->setRange(1, 65535);
    port_->setValue(5432);
    auto* endpoint = new QHBoxLayout;
    endpoint->addWidget(host_, 1);
    endpoint->addWidget(port_);
    form->addRow(tr("Host and port"), endpoint);
    database_ = named<QLineEdit>("migrationDatabase");
    schema_ = named<QLineEdit>("migrationSchema");
    schema_->setText("public");
    form->addRow(tr("Source database"), database_);
    form->addRow(tr("Source schema"), schema_);
    username_ = named<QLineEdit>("migrationUser");
    password_ = named<QLineEdit>("migrationPassword");
    password_->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Source username"), username_);
    form->addRow(tr("Source password"), password_);
    const auto pathField = [this, form](const QString& label, const char* name, const QString& filter, bool save) {
        auto* input = named<QLineEdit>(name);
        auto* browse = new QPushButton(tr("Browse…"));
        auto* row = new QHBoxLayout;
        row->addWidget(input, 1);
        row->addWidget(browse);
        form->addRow(label, row);
        connect(browse, &QPushButton::clicked, this, [this, input, label, filter, save] {
            const auto path = save ? QFileDialog::getSaveFileName(this, label, input->text(), filter)
                : QFileDialog::getOpenFileName(this, label, input->text(), filter);
            if (!path.isEmpty()) input->setText(path);
        });
        return input;
    };
    driver_ = pathField(tr("Installed source driver"), "migrationDriver", tr("Driver DLL (*.dll)"), false);
    ca_ = pathField(tr("Source TLS CA"), "migrationCa", tr("Certificates (*.pem *.crt *.cer);;All files (*)"), false);
    tables_ = named<QListWidget>("migrationTables");
    tables_->setMaximumHeight(120);
    form->addRow(tr("Tables to copy"), tables_);
    prefix_ = named<QLineEdit>("migrationPrefix");
    prefix_->setPlaceholderText("import_");
    form->addRow(tr("Target table prefix in main.public"), prefix_);
    journal_ = pathField(tr("Migration checkpoint"), "migrationJournal", tr("Checkpoint (*.json)"), true);
    layout->addWidget(settings_);

    auto* actions = new QHBoxLayout;
    discover_ = named<QPushButton>("migrationDiscover");
    discover_->setText(tr("Find source tables"));
    preview_ = named<QPushButton>("migrationPreview");
    preview_->setText(tr("Preview snapshot"));
    apply_ = named<QPushButton>("migrationApply");
    apply_->setText(tr("Apply or resume"));
    pause_ = named<QPushButton>("migrationPause");
    pause_->setText(tr("Pause after commit"));
    cleanup_ = named<QPushButton>("migrationCleanup");
    cleanup_->setText(tr("Remove migration tables…"));
    for (auto* button : {discover_, preview_, apply_, pause_, cleanup_}) actions->addWidget(button);
    layout->addLayout(actions);
    status_ = named<QLabel>("migrationStatus");
    status_->setWordWrap(true);
    layout->addWidget(status_);
    report_ = named<QPlainTextEdit>("migrationReport");
    report_->setReadOnly(true);
    layout->addWidget(report_, 1);

    connect(provider_, &QComboBox::currentIndexChanged, this, [this](int index) {
        port_->setValue(index ? 3306 : 5432);
        if (!index) schema_->setText("public");
        else if (schema_->text() == u"public") schema_->clear();
        tables_->clear();
        resetPlan();
    });
    for (auto* input : {host_, database_, schema_, username_, password_, driver_, ca_, prefix_, journal_})
        connect(input, &QLineEdit::textChanged, this, [this] { resetPlan(); });
    connect(port_, &QSpinBox::valueChanged, this, [this] { resetPlan(); });
    connect(tables_, &QListWidget::itemChanged, this, [this] { resetPlan(); });
    connect(discover_, &QPushButton::clicked, this, [this] { discover(); });
    connect(preview_, &QPushButton::clicked, this, [this] { preview(); });
    connect(apply_, &QPushButton::clicked, this, [this] { apply(); });
    connect(cleanup_, &QPushButton::clicked, this, [this] { cleanup(); });
    connect(pause_, &QPushButton::clicked, this, [this] {
        pauseRequested_ = true;
        pause_->setEnabled(false);
        status_->setText(tr("Pausing at the next committed checkpoint…"));
    });
    working(false);
    publish("idle", tr("Enter the source details and find its tables."));
}

void MigrationDialog::reject()
{
    if (active_) return;
    password_->clear();
    QDialog::reject();
}

void MigrationDialog::working(bool active)
{
    active_ = active;
    settings_->setEnabled(!active);
    discover_->setEnabled(!active);
    preview_->setEnabled(!active);
    apply_->setEnabled(!active && !plan_.fingerprint.isEmpty());
    cleanup_->setEnabled(!active && !plan_.fingerprint.isEmpty());
    pause_->setEnabled(active && copying_ && !pauseRequested_);
}

void MigrationDialog::resetPlan()
{
    plan_ = {};
    committed_ = 0;
    if (!active_)
    {
        working(false);
        publish("idle", tr("Refresh the source snapshot before applying."));
    }
}

void MigrationDialog::publish(const QString& state, const QString& message, const QString& code)
{
    auto report = plan_.report;
    report["state"] = state;
    report["code"] = code;
    report["committed_rows"] = static_cast<qint64>(committed_);
    status_->setText(message);
    report_->setPlainText(QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Indented)));
}

ExternalSource MigrationDialog::source(bool selected) const
{
    ExternalSource source;
    source.provider = provider_->currentIndex() == 0 ? ExternalProvider::postgresql : ExternalProvider::mysql;
    source.host = host_->text().trimmed();
    source.port = static_cast<quint16>(port_->value());
    source.database = database_->text().trimmed();
    source.schema = schema_->text().trimmed();
    if (source.provider == ExternalProvider::mysql && source.schema.isEmpty()) source.schema = source.database;
    source.username = username_->text();
    source.driverFile = driver_->text().trimmed();
    source.caFile = ca_->text().trimmed();
    if (selected)
        for (int row = 0; row < tables_->count(); ++row)
        {
            const auto* item = tables_->item(row);
            if (item->checkState() == Qt::Checked) source.selectedTables << item->text();
        }
    return source;
}

ExternalSnapshot MigrationDialog::capture(const ExternalSource& config)
{
    ExternalSnapshot snapshot;
    if (config.host.isEmpty() || config.database.isEmpty() || config.schema.isEmpty() || config.username.isEmpty() ||
        config.driverFile.isEmpty() || config.caFile.isEmpty() || password_->text().isEmpty())
    {
        snapshot.error = tr("Enter the source database, read-only credentials, installed driver and TLS CA.");
        return snapshot;
    }
    QEventLoop loop;
    auto secret = std::make_shared<Password>();
    secret->bytes = password_->text().toUtf8();
    workers_.start([config, secret, &snapshot, &loop] {
        try { snapshot = readExternalSnapshot(config, secret->bytes); }
        catch (const std::exception&) { snapshot.error = QObject::tr("Could not read the external snapshot."); }
        secret->bytes.fill('\0');
        QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
    });
    loop.exec();
    return snapshot;
}

void MigrationDialog::discover()
{
    working(true);
    publish("loading", tr("Reading source table metadata…"));
    const auto snapshot = capture(source(false));
    resetPlan();
    tables_->clear();
    QStringList names;
    for (const auto& table : snapshot.tables) names << table.name;
    names.sort(Qt::CaseSensitive);
    for (const auto& name : names)
    {
        auto* item = new QListWidgetItem(name, tables_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
    QJsonArray unsupported;
    for (const auto& item : snapshot.unsupported) unsupported.append(item);
    plan_.report["unsupported"] = unsupported;
    working(false);
    if (!snapshot.error.isEmpty()) publish("error", snapshot.error, "source_unavailable");
    else publish("discovered", tr("Select the tables to copy, including referenced tables."));
}

void MigrationDialog::preview()
{
    const auto config = source(true);
    if (config.selectedTables.isEmpty())
    {
        publish("error", tr("Select at least one source table."), "empty_selection");
        return;
    }
    if (!QFileInfo(journal_->text()).isAbsolute())
    {
        publish("error", tr("Choose an absolute path for the migration checkpoint."), "missing_checkpoint");
        return;
    }
    working(true);
    publish("loading", tr("Reading the selected tables in a consistent source snapshot…"));
    const auto snapshot = capture(config);
    plan_ = snapshot.error.isEmpty() ? planImport(config, snapshot, prefix_->text()) : TargetPlan{};
    working(false);
    if (!snapshot.error.isEmpty()) publish("error", snapshot.error, "source_unavailable");
    else if (!plan_.error.isEmpty()) publish("error", plan_.error, "mapping_rejected");
    else if (QFileInfo::exists(journal_->text()))
    {
        QJsonObject checkpoint;
        if (!loadJournal(checkpoint))
        {
            publish("error", tr("The checkpoint could not be read."), "invalid_checkpoint");
            return;
        }
        if (checkpoint.value("snapshot") != plan_.fingerprint)
        {
            apply_->setEnabled(false);
            cleanup_->setEnabled(false);
            publish("error", tr("The source snapshot changed. The partial target remains untouched."),
                "source_snapshot_changed");
            return;
        }
        publish("ready", tr("The source snapshot matches the checkpoint. Review the mapping before resuming."));
    }
    else publish("ready", tr("Review the table mapping, target DDL and unsupported source objects before applying."));
}

template<class Start>
SqlReply MigrationDialog::wait(Start start)
{
    SqlReply pending;
    pending.error.code = SDP_INVALID_STATE;
    if (!session_) return pending;
    QEventLoop loop;
    connect(session_, &QObject::destroyed, &loop, &QEventLoop::quit);
    if (start(*session_, [&](const SqlReply& reply) { pending = reply; loop.quit(); })) loop.exec();
    return pending;
}

SqlReply MigrationDialog::sql(const QString& statement, const QJsonObject& types, const QJsonObject& values,
    SqlSession::Stream stream, int limit)
{
    return wait([&](SqlSession& session, SqlSession::Finished completed) {
        return session.execute({{statement, 1}}, types, values, {limit, 15000}, std::move(stream), std::move(completed));
    });
}

SqlReply MigrationDialog::transaction(SqlTransaction command)
{
    return wait([&](SqlSession& session, SqlSession::Finished completed) {
        return session.transaction(command, {}, std::move(completed));
    });
}

QJsonArray MigrationDialog::destination() const
{
    const auto& profile = session_->profile();
    return {profile.host, profile.port, profile.tls_name, profile.database, profile.user};
}

bool MigrationDialog::loadJournal(QJsonObject& journal) const
{
    QFile file(journal_->text());
    if (!file.open(QIODevice::ReadOnly) || file.size() > 256 * 1024) return false;
    journal = QJsonDocument::fromJson(file.readAll()).object();
    return journal.value("format") == 1 && !QUuid(journal.value("owner").toString()).isNull() &&
        journal.value("destination").isArray() && journal.value("objects").isObject();
}

bool MigrationDialog::saveJournal(const QJsonObject& journal) const
{
    QSaveFile file(journal_->text());
    const auto data = QJsonDocument(journal).toJson(QJsonDocument::Compact);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

QJsonObject MigrationDialog::objects(const QJsonArray& catalog) const
{
    QSet<QString> scoped{plan_.ownerTable};
    for (const auto& table : plan_.tables) scoped.insert(table.name);
    QJsonObject result;
    for (const auto& value : catalog)
    {
        const auto object = value.toObject();
        const auto name = object.value("qualified_name").toArray();
        if (name.size() != 3 || name[0] != "main" || name[1] != "public" || !scoped.contains(name[2].toString()))
            continue;
        result.insert(name[2].toString(), QJsonObject{{"id", object.value("id")},
            {"schema_version", object.value("schema_version")}, {"kind", object.value("kind")}});
    }
    return result;
}

bool MigrationDialog::verifyOwner(const QJsonObject& journal)
{
    const auto metadata = wait([](SqlSession& session, SqlSession::Finished done) {
        return session.refreshMetadata(std::move(done));
    });
    if (metadata.error.code != SDP_OK || journal.value("destination") != destination() ||
        journal.value("objects").toObject().size() != plan_.tables.size() + 1 ||
        journal.value("objects") != objects(metadata.data.value("objects").toArray())) return false;
    QJsonArray receipt;
    const auto result = sql("SELECT owner_id, snapshot FROM " + targetName(plan_.ownerTable), {}, {},
        [&](const SqlEvent& event) {
            if (event.kind == SqlEvent::Kind::batch) receipt = event.data.value("rows").toArray();
        }, 2);
    if (result.error.code != SDP_OK || result.resultsTruncated || receipt.size() != 1) return false;
    const auto row = receipt[0].toArray();
    return row.size() == 2 && row[0].toObject().value("v") == journal.value("owner") &&
        row[1].toObject().value("v") == journal.value("snapshot");
}

void MigrationDialog::rollback()
{
    if (session_ && session_->state().transactionActive) (void)transaction(SqlTransaction::rollback);
}

bool MigrationDialog::createScope(QJsonObject& journal)
{
    if (transaction(SqlTransaction::begin).error.code != SDP_OK) return false;
    const auto meta = [this] {
        return wait([](SqlSession& session, SqlSession::Finished done) {
            return session.refreshMetadata(std::move(done));
        });
    };
    const auto before = meta();
    if (before.error.code != SDP_OK || !objects(before.data.value("objects").toArray()).isEmpty())
    {
        rollback();
        return false;
    }
    for (const auto& table : plan_.tables)
        if (sql(table.sql).error.code != SDP_OK)
        {
            rollback();
            return false;
        }
    const auto owner = targetName(plan_.ownerTable);
    if (sql("CREATE TABLE " + owner + " (owner_id TEXT PRIMARY KEY, snapshot TEXT NOT NULL)").error.code != SDP_OK ||
        sql("INSERT INTO " + owner + " VALUES (:owner, :snapshot)",
            {{"owner", QJsonObject{{"type", "TEXT"}}}, {"snapshot", QJsonObject{{"type", "TEXT"}}}},
            {{"owner", QJsonObject{{"t", "text"}, {"v", journal.value("owner")}}},
             {"snapshot", QJsonObject{{"t", "text"}, {"v", plan_.fingerprint}}}}).error.code != SDP_OK)
    {
        rollback();
        return false;
    }
    const auto after = meta();
    const auto scope = objects(after.data.value("objects").toArray());
    if (after.error.code != SDP_OK || scope.size() != plan_.tables.size() + 1)
    {
        rollback();
        return false;
    }
    journal["objects"] = scope;
    if (!saveJournal(journal))
    {
        rollback();
        return false;
    }
    const auto commit = transaction(SqlTransaction::commit);
    if (commit.error.code == SDP_OK) return true;
    rollback();
    return false;
}

bool MigrationDialog::readTarget(const TargetTable& table, KeyedRows& existing)
{
    bool invalid = false;
    const auto result = sql(table.select, {}, {}, [&](const SqlEvent& event) {
        if (event.kind != SqlEvent::Kind::batch || invalid) return;
        for (const auto& value : event.data.value("rows").toArray())
        {
            const auto row = value.toArray();
            const auto key = keyOf(row, table.key);
            const auto expected = table.rows.find(key);
            if (key.isEmpty() || expected == table.rows.end() || expected->second != row ||
                !existing.emplace(key, row).second)
            {
                invalid = true;
                break;
            }
        }
    }, static_cast<int>(table.rows.size()) + 1);
    return result.error.code == SDP_OK && !result.resultsTruncated && !invalid;
}

bool MigrationDialog::copyChunk(const TargetTable& table, const QList<QJsonArray>& rows, const QJsonObject& journal)
{
    QVector<SqlStatement> statements;
    QJsonObject types, values;
    for (qsizetype index = 0; index < rows.size(); ++index)
    {
        QStringList placeholders;
        for (qsizetype column = 0; column < table.types.size(); ++column)
        {
            const auto name = QStringLiteral("r%1c%2").arg(index).arg(column);
            placeholders << ":" + name;
            types.insert(name, table.types[column]);
            values.insert(name, rows[index].at(column));
        }
        statements.append({table.insert + "(" + placeholders.join(", ") + ")", 1});
    }
    if (transaction(SqlTransaction::begin).error.code != SDP_OK) return false;
    if (!verifyOwner(journal))
    {
        rollback();
        return false;
    }
    int affected = 0;
    const auto result = wait([&](SqlSession& session, SqlSession::Finished done) {
        return session.execute(statements, types, values, {1, 15000}, [&](const SqlEvent& event) {
            if (event.kind == SqlEvent::Kind::statement)
                affected += event.data.value("affected_rows").toInt();
        }, std::move(done));
    });
    if (result.error.code == SDP_OK && affected == rows.size() &&
        transaction(SqlTransaction::commit).error.code == SDP_OK) return true;
    plan_.report["rejected_rows"] = QJsonArray{QJsonObject{{"table", table.name},
        {"batch_statement", result.statementIndex}, {"server_code", result.error.code}}};
    rollback();
    return false;
}

void MigrationDialog::apply()
{
    if (!session_ || session_->busy() || !session_->state().connected || !session_->state().autocommit ||
        session_->state().transactionActive || session_->profile().read_only || plan_.fingerprint.isEmpty())
    {
        publish("error", tr("Use a connected, writable worksheet in autocommit mode."), "destination_unavailable");
        return;
    }
    if (QMessageBox::question(this, tr("Apply migration"),
        tr("Write only to the reviewed tables with prefix %1 in main.public?").arg(prefix_->text()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    QLockFile checkpointLock(journal_->text() + ".lock");
    checkpointLock.setStaleLockTime(0);
    if (!checkpointLock.tryLock(0))
    {
        publish("error", tr("Another migration is using this checkpoint, or the checkpoint is not writable."),
            "checkpoint_busy");
        return;
    }

    working(true);
    publish("loading", tr("Checking the source snapshot and destination ownership…"));
    const auto run = [this]() -> QString {
        const auto config = source(true);
        const auto fresh = capture(config);
        if (!fresh.error.isEmpty())
        {
            status_->setText(fresh.error);
            return "source_unavailable";
        }
        if (planImport(config, fresh, prefix_->text()).fingerprint != plan_.fingerprint)
        {
            status_->setText(tr("The source snapshot changed. The target remains untouched."));
            return "source_snapshot_changed";
        }
        QJsonObject journal;
        if (!QFileInfo::exists(journal_->text()))
        {
            journal = {{"format", 1}, {"owner", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                {"snapshot", plan_.fingerprint}, {"destination", destination()}, {"objects", QJsonObject{}},
                {"committed_rows", 0}, {"last_keys", QJsonObject{}}};
            if (!saveJournal(journal))
            {
                status_->setText(tr("Could not save the initial checkpoint."));
                return "checkpoint_write_failed";
            }
        }
        if (!loadJournal(journal) || journal.value("destination") != destination() ||
            journal.value("snapshot") != plan_.fingerprint || journal.value("removed").toBool())
        {
            status_->setText(tr("The checkpoint is invalid for this source or destination."));
            return "invalid_checkpoint";
        }
        if (!verifyOwner(journal) && !createScope(journal))
        {
            status_->setText(tr("Could not create or verify the migration-owned tables. Existing objects were preserved."));
            return "destination_ownership_failed";
        }
        if (!verifyOwner(journal))
        {
            status_->setText(tr("Could not verify the committed migration scope."));
            return "destination_ownership_failed";
        }

        copying_ = true;
        pauseRequested_ = false;
        committed_ = 0;
        working(true);
        publish("running", tr("Validating existing target rows against the source snapshot…"));
        for (const auto& table : plan_.tables)
        {
            KeyedRows existing;
            if (!readTarget(table, existing))
            {
                status_->setText(tr("Rows in %1 conflict with the source or exceed the read budget.").arg(table.name));
                return "destination_rows_conflict";
            }
            committed_ += existing.size();
            QList<QJsonArray> rows;
            QByteArray last;
            const auto flush = [&]() {
                if (!copyChunk(table, rows, journal)) return false;
                committed_ += rows.size();
                rows.clear();
                journal["committed_rows"] = static_cast<qint64>(committed_);
                auto keys = journal.value("last_keys").toObject();
                keys[table.name] = QString::fromLatin1(QCryptographicHash::hash(last, QCryptographicHash::Sha256).toHex());
                journal["last_keys"] = keys;
                const bool saved = saveJournal(journal);
                if (saved) publish("running", tr("Committed %1 of %2 source rows.").arg(committed_).arg(plan_.rowCount));
                return saved;
            };
            for (auto it = table.rows.cbegin(); it != table.rows.cend(); ++it)
            {
                if (pauseRequested_) return "paused";
                if (existing.contains(it->first)) continue;
                rows.append(it->second);
                last = it->first;
                if ((rows.size() == 16 || std::next(it) == table.rows.cend()) && !flush())
                {
                    status_->setText(tr("A batch in %1 could not be recorded. Preview again to reconcile committed keys.").arg(table.name));
                    return "batch_failed";
                }
            }
            if (!rows.isEmpty() && !flush())
            {
                status_->setText(tr("The final batch in %1 could not be recorded.").arg(table.name));
                return "batch_failed";
            }
            if (pauseRequested_) return "paused";
            KeyedRows verified;
            if (!readTarget(table, verified) || verified.size() != table.rows.size() || checksum(verified) != table.checksum)
            {
                status_->setText(tr("Target type, row count or checksum verification failed for %1.").arg(table.name));
                return "verification_failed";
            }
        }
        journal["verified"] = true;
        if (!saveJournal(journal))
        {
            status_->setText(tr("Rows were verified, but the checkpoint could not be updated."));
            return "checkpoint_write_failed";
        }
        return {};
    };

    const auto code = run();
    copying_ = false;
    working(false);
    if (code.isEmpty()) publish("complete", tr("Verified %1 target rows by key, type and checksum.").arg(committed_));
    else if (code == "paused") publish("paused", tr("Paused after %1 committed rows. Resume from this checkpoint.").arg(committed_));
    else publish("error", status_->text(), code);
}

void MigrationDialog::cleanup()
{
    if (!session_ || session_->busy() || !session_->state().connected || session_->state().transactionActive ||
        plan_.fingerprint.isEmpty()) return;
    QJsonObject journal;
    if (!loadJournal(journal) || journal.value("snapshot") != plan_.fingerprint) return;
    if (QMessageBox::warning(this, tr("Remove migration tables"),
        tr("Permanently remove the tables owned by this checkpoint and their copied rows?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    QLockFile checkpointLock(journal_->text() + ".lock");
    checkpointLock.setStaleLockTime(0);
    if (!checkpointLock.tryLock(0) || !loadJournal(journal) || journal.value("snapshot") != plan_.fingerprint)
    {
        publish("error", tr("The checkpoint is in use or changed before cleanup."), "checkpoint_busy");
        return;
    }
    working(true);
    bool success = transaction(SqlTransaction::begin).error.code == SDP_OK && verifyOwner(journal);
    if (success)
    {
        const auto metadata = wait([](SqlSession& session, SqlSession::Finished done) {
            return session.refreshMetadata(std::move(done));
        });
        const auto scope = journal.value("objects").toObject();
        QSet<QString> ids;
        for (const auto& owned : scope) ids.insert(owned.toObject().value("id").toString());
        success = metadata.error.code == SDP_OK;
        for (const auto& object : metadata.data.value("objects").toArray())
        {
            const auto details = object.toObject();
            if (ids.contains(details.value("id").toString())) continue;
            for (const auto& dependency : details.value("depends_on").toArray())
                if (ids.contains(dependency.toString())) success = false;
        }
    }
    for (auto it = plan_.tables.crbegin(); success && it != plan_.tables.crend(); ++it)
        success = sql("DROP TABLE " + targetName(it->name)).error.code == SDP_OK;
    if (success) success = sql("DROP TABLE " + targetName(plan_.ownerTable)).error.code == SDP_OK &&
        transaction(SqlTransaction::commit).error.code == SDP_OK;
    if (!success) rollback();
    working(false);
    if (success)
    {
        journal["removed"] = true;
        committed_ = 0;
        const auto saved = saveJournal(journal);
        publish("removed", saved ? tr("Removed the migration-owned tables. The checkpoint was retained.")
            : tr("Removed the migration-owned tables. The checkpoint could not be updated."));
    }
    else publish("error", tr("Cleanup could not prove ownership or encountered a dependent object."), "cleanup_rejected");
}
}

QDialog* showMigrationStudio(SqlSession& destination, QThreadPool& workers, QWidget* parent)
{
    auto* dialog = new MigrationDialog(destination, workers, parent);
    dialog->show();
    return dialog;
}
}
