#include "backup_center.hpp"

#include "sql_session.hpp"
#include "sql_script.hpp"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimeZone>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

namespace wb
{
namespace
{
constexpr int backupLimit = 512 * 1024 * 1024;

bool isHex(QStringView value, qsizetype length = 32)
{
    if (value.size() != length) return false;
    for (const auto& character : value)
    {
        const auto code = character.unicode();
        if ((code < u'0' || code > u'9') && (code < u'a' || code > u'f')) return false;
    }
    return true;
}

bool restoreName(QStringView name)
{
    if (name.isEmpty() || name.size() > 32) return false;
    for (const auto& character : name)
    {
        const auto code = character.unicode();
        if ((code < u'a' || code > u'z') && (code < u'0' || code > u'9') && code != u'-' && code != u'_')
            return false;
    }
    if (name == u"con" || name == u"prn" || name == u"aux" || name == u"nul") return false;
    if (name.size() == 4 && (name.startsWith(u"com") || name.startsWith(u"lpt")) &&
        name.at(3).unicode() >= u'0' && name.at(3).unicode() <= u'9') return false;
    return true;
}

QString pretty(const QJsonObject& value)
{
    return QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Indented));
}

QString newToken()
{
    return QString::fromLatin1(QUuid::createUuid().toRfc4122().toHex());
}

QString dateUtc(const QJsonValue& value)
{
    const auto seconds = value.toInteger();
    return seconds > 0 ? QDateTime::fromSecsSinceEpoch(seconds, QTimeZone::utc()).toString(Qt::ISODate) : QString{};
}

QString errorText(const SqlReply& reply)
{
    switch (reply.error.code)
    {
    case SDP_PERMISSION_DENIED: return QObject::tr("The Server denied permission for this archive, job or schedule.");
    case SDP_MODULE_UNAVAILABLE: return QObject::tr("The requested Server module is unavailable.");
    case SDP_FEATURE_UNSUPPORTED: return QObject::tr("The connected Server does not support this operation.");
    case SDP_INVALID_STATE: return QObject::tr("The archive or job is invalid, or the restore name is already in use.");
    case SDP_MALFORMED_MESSAGE: return QObject::tr("The backup or scheduling response could not be read by this SDK.");
    case SDP_INCOMPATIBLE_PROTOCOL: return QObject::tr("The Server and Native SDK protocols are incompatible.");
    default: break;
    }
    const auto text = QString::fromUtf8(reply.error.message);
    return text.isEmpty() ? QObject::tr("The Server returned error %1.").arg(reply.error.code) : text;
}

bool terminal(QStringView state)
{
    return state == u"failed" || state == u"cancelled" || state == u"succeeded" || state == u"paused";
}

bool knownJobState(QStringView state)
{
    return terminal(state) || state == u"queued" || state == u"starting" ||
        state == u"running" || state == u"checkpointing";
}

bool archiveProof(const QJsonObject& reply, const QString& requested)
{
    return reply.value("verified").isBool() && reply.value("verified").toBool() &&
        isHex(requested) && reply.value("backup_id").toString() == requested &&
        reply.value("lsn").toInteger() > 0 && isHex(reply.value("manifest_sha256").toString(), 64);
}

class BackupCenter final : public QDialog
{
public:
    BackupCenter(SqlSession& session, QWidget* parent, std::function<void(const QString&)> openJob)
        : QDialog(parent), session_(&session), openJob_(std::move(openJob))
    {
        setObjectName("backupCenter");
        setWindowTitle(tr("Backup and scheduling"));
        setWindowModality(Qt::WindowModal);
        setAttribute(Qt::WA_DeleteOnClose);
        resize(930, 760);
        auto* layout = new QVBoxLayout(this);
        panes_ = new QTabWidget;
        layout->addWidget(panes_, 1);
        buildBackup();
        buildScheduler();
        buildRequest();
        status_ = note(tr("Use the Server's archive IDs and schedule IDs."));
        status_->setObjectName("backupCenterStatus");
        status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(status_);

        timer_.setInterval(550);
        connect(&timer_, &QTimer::timeout, this, [this] {
            if (panes_->currentWidget() == backupPage_ && ready() && backups() && !activeBackup_.isEmpty())
                readBackupJob(SDP_BACKUP_STATUS, activeBackup_, false);
        });
        connect(backupJobId_, &QLineEdit::textChanged, this, [this] {
            activeBackup_.clear();
            backupState_.clear();
            setProperty("backupState", QString{});
            backupJobLabel_->clear();
            backupJobResult_->clear();
            timer_.stop();
            sync();
        });
        connect(backupId_, &QLineEdit::textChanged, this, [this] {
            verifiedArchive_.clear();
            verifiedDigest_.clear();
            backupVerifyResult_->clear();
            backupRestoreResult_->clear();
            archiveLabel_->setText(tr("This selection has not been reverified."));
            sync();
        });
        connect(restoreName_, &QLineEdit::textChanged, this, [this] {
            backupRestoreResult_->clear();
            sync();
        });
        connect(scheduleId_, &QLineEdit::textChanged, this, [this] {
            loadedSchedule_ = {};
            scheduleResult_->clear();
            scheduleHistoryResult_->clear();
            scheduleSummary_->clear();
            sync();
        });
        connect(scheduleType_, &QComboBox::currentIndexChanged, this, [this] { sync(); });
        connect(scheduleKind_, &QComboBox::currentIndexChanged, this, [this] { sync(); });
        connect(timezone_, &QLineEdit::textChanged, this, [this] { sync(); });
        connect(&session, &QObject::destroyed, this, [this] {
            busy_ = false;
            timer_.stop();
            status_->setText(tr("The worksheet connection closed."));
            sync();
            if (closing_) QDialog::reject();
        });
        sync();
    }

    void reject() override
    {
        timer_.stop();
        if (busy_)
        {
            closing_ = true;
            status_->setText(tr("Waiting for the Server to finish the current request…"));
            return;
        }
        QDialog::reject();
    }

private:
    QLabel* note(const QString& message)
    {
        auto* label = new QLabel(message);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        return label;
    }

    QLineEdit* line(const char* id, int maxLength, const QString& placeholder = {})
    {
        auto* field = new QLineEdit;
        field->setObjectName(id);
        field->setMaxLength(maxLength);
        field->setPlaceholderText(placeholder);
        return field;
    }

    QPlainTextEdit* result(const char* id)
    {
        auto* field = new QPlainTextEdit;
        field->setObjectName(id);
        field->setReadOnly(true);
        return field;
    }

    QSpinBox* number(const char* id, int minimum, int maximum, int initial)
    {
        auto* field = new QSpinBox;
        field->setObjectName(id);
        field->setRange(minimum, maximum);
        field->setValue(initial);
        return field;
    }

    QComboBox* choices(const char* id, const QList<std::pair<QString, QString>>& entries)
    {
        auto* box = new QComboBox;
        box->setObjectName(id);
        for (const auto& [label, value] : entries) box->addItem(label, value);
        return box;
    }

    QPushButton* button(QHBoxLayout& row, const char* id, const QString& title, auto action)
    {
        auto* control = new QPushButton(title);
        control->setObjectName(id);
        row.addWidget(control);
        connect(control, &QPushButton::clicked, this, std::move(action));
        return control;
    }

    void buildBackup()
    {
        backupPage_ = new QWidget;
        auto* layout = new QVBoxLayout(backupPage_);
        layout->addWidget(note(tr("Archives live in the connected Server's backup repository. Use archive IDs here.")));
        auto* form = new QFormLayout;
        layout->addLayout(form);
        backupMaxBytes_ = number("backupMaxBytes", 1, backupLimit, backupLimit);
        form->addRow(tr("Backup limit (bytes)"), backupMaxBytes_);
        backupJobId_ = line("backupJobId", 32);
        form->addRow(tr("Backup job ID"), backupJobId_);
        auto* jobs = new QHBoxLayout;
        layout->addLayout(jobs);
        backupSubmit_ = button(*jobs, "backupSubmit", tr("Submit backup…"), [this] { submitBackup(); });
        backupStatus_ = button(*jobs, "backupStatus", tr("Read job"), [this] {
            readBackupJob(SDP_BACKUP_STATUS, backupJobId_->text().trimmed());
        });
        backupCancel_ = button(*jobs, "backupCancel", tr("Cancel backup…"), [this] { cancelBackup(); });
        followBackup_ = button(*jobs, "backupOpenJob", tr("Open in Job Center"), [this] {
            if (openJob_ && isHex(activeBackup_)) openJob_(activeBackup_);
        });
        jobs->addStretch();
        backupJobLabel_ = note({});
        backupJobLabel_->setObjectName("backupJobStatus");
        layout->addWidget(backupJobLabel_);

        auto* archiveFields = new QFormLayout;
        layout->addLayout(archiveFields);
        backupId_ = line("backupId", 32);
        archiveFields->addRow(tr("Server archive ID"), backupId_);
        restoreName_ = line("backupRestoreName", 32, tr("A new name on the Server"));
        archiveFields->addRow(tr("Separate restore name"), restoreName_);
        auto* archives = new QHBoxLayout;
        layout->addLayout(archives);
        backupCatalog_ = button(*archives, "backupCatalog", tr("Read archive catalog"), [this] { catalog(); });
        backupVerify_ = button(*archives, "backupVerify", tr("Verify temporary restore"), [this] { verify(); });
        backupRestore_ = button(*archives, "backupRestore", tr("Restore to separate instance…"), [this] { restore(); });
        archives->addStretch();
        archiveLabel_ = note(tr("Select an archive to verify its checksums and temporary restore."));
        archiveLabel_->setObjectName("backupArchiveStatus");
        layout->addWidget(archiveLabel_);

        auto* detail = new QTabWidget;
        layout->addWidget(detail, 1);
        backupJobResult_ = result("backupJobResult");
        detail->addTab(backupJobResult_, tr("Backup job"));
        auto* catalogPage = new QWidget;
        auto* catalogLayout = new QVBoxLayout(catalogPage);
        archiveList_ = new QTableWidget(0, 3);
        archiveList_->setObjectName("backupArchives");
        archiveList_->setHorizontalHeaderLabels({tr("Archive ID"), tr("Revision"), tr("Publication verified")});
        archiveList_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        archiveList_->setSelectionBehavior(QAbstractItemView::SelectRows);
        archiveList_->setSelectionMode(QAbstractItemView::SingleSelection);
        catalogLayout->addWidget(archiveList_);
        connect(archiveList_, &QTableWidget::cellClicked, this, [this](int row) {
            if (auto* field = archiveList_->item(row, 0)) backupId_->setText(field->text());
        });
        backupCatalogResult_ = result("backupCatalogResult");
        catalogLayout->addWidget(backupCatalogResult_);
        detail->addTab(catalogPage, tr("Archive catalog"));
        backupVerifyResult_ = result("backupVerifyResult");
        detail->addTab(backupVerifyResult_, tr("Verification proof"));
        backupRestoreResult_ = result("backupRestoreResult");
        detail->addTab(backupRestoreResult_, tr("Restore proof"));
        backupDetail_ = detail;
        panes_->addTab(backupPage_, tr("Server backup"));
    }

    void buildScheduler()
    {
        schedulerPage_ = new QWidget;
        auto* layout = new QVBoxLayout(schedulerPage_);
        layout->addWidget(note(tr("The Server stores schedules and runs them after Workbench closes. A due run is skipped when a run of the same schedule is active.")));
        auto* form = new QFormLayout;
        layout->addLayout(form);
        scheduleForm_ = form;
        scheduleKind_ = choices("scheduleKind", {{tr("Compute"), "compute"}, {tr("Maintenance"), "maintenance"},
            {tr("Backup"), "backup"}, {tr("SQL script"), "sql_script"}, {tr("Retrain model"), "retrain"}});
        form->addRow(tr("Task"), scheduleKind_);
        scheduleAction_ = choices("scheduleAction", {{tr("Integrity check"), "integrity"}, {tr("Statistics"), "statistics"},
            {tr("Index maintenance"), "index"}});
        form->addRow(tr("Maintenance action"), scheduleAction_);
        scheduleCount_ = number("scheduleCount", 1, 1'000'000'000, 10'000);
        form->addRow(tr("Compute iterations"), scheduleCount_);
        scheduleSql_ = new QPlainTextEdit;
        scheduleSql_->setObjectName("scheduleSql");
        scheduleSql_->setMaximumHeight(105);
        scheduleSql_->setPlaceholderText(tr("Enter up to 16 SELECT, INSERT, UPDATE or DELETE statements. The Server stores this SQL."));
        form->addRow(tr("SQL statements"), scheduleSql_);
        scheduleModel_ = line("scheduleModel", 392, tr("Existing model name"));
        form->addRow(tr("Model"), scheduleModel_);
        scheduleDataset_ = line("scheduleDataset", 32, tr("Immutable dataset ID"));
        form->addRow(tr("Dataset"), scheduleDataset_);
        scheduleSeed_ = number("scheduleSeed", 0, 2'147'483'647, 42);
        form->addRow(tr("Training seed"), scheduleSeed_);
        scheduleOptions_ = new QPlainTextEdit;
        scheduleOptions_->setObjectName("scheduleOptions");
        scheduleOptions_->setPlainText("{}");
        scheduleOptions_->setMaximumHeight(75);
        form->addRow(tr("Training options in JSON"), scheduleOptions_);
        scheduleType_ = choices("scheduleType", {{tr("Fixed interval"), "interval"}, {tr("Daily"), "daily"}});
        form->addRow(tr("Timing"), scheduleType_);
        scheduleSeconds_ = number("scheduleSeconds", 1, 31'536'000, 3600);
        form->addRow(tr("Interval (seconds)"), scheduleSeconds_);
        timezone_ = line("scheduleTimezone", 128, tr("UTC or a Windows time zone key"));
        timezone_->setText("UTC");
        form->addRow(tr("Server time zone"), timezone_);
        hour_ = number("scheduleHour", 0, 23, 9);
        minute_ = number("scheduleMinute", 0, 59, 0);
        auto* day = new QHBoxLayout;
        day->addWidget(hour_);
        day->addWidget(minute_);
        form->addRow(tr("Daily hour and minute"), day);
        misfire_ = choices("scheduleMisfire", {{tr("Skip missed runs"), "skip"}, {tr("Run once for missed runs"), "run_once"}});
        form->addRow(tr("Missed runs"), misfire_);
        retries_ = number("scheduleRetries", 0, 3, 0);
        backoff_ = number("scheduleBackoff", 1, 3600, 30);
        auto* retry = new QHBoxLayout;
        retry->addWidget(retries_);
        retry->addWidget(backoff_);
        form->addRow(tr("Retries and delay (seconds)"), retry);
        scheduleId_ = line("scheduleId", 32);
        form->addRow(tr("Server schedule ID"), scheduleId_);

        auto* actions = new QHBoxLayout;
        layout->addLayout(actions);
        scheduleCreate_ = button(*actions, "scheduleCreate", tr("Create schedule…"), [this] { createSchedule(); });
        scheduleStatus_ = button(*actions, "scheduleStatus", tr("Read schedule"), [this] { querySchedule(SDP_JOB_SCHEDULE_STATUS); });
        scheduleDisable_ = button(*actions, "scheduleDisable", tr("Disable schedule…"), [this] { disableSchedule(); });
        scheduleHistory_ = button(*actions, "scheduleHistory", tr("Read last run"), [this] { readScheduleHistory(); });
        auto* capabilities = button(*actions, "scheduleCapabilities", tr("Server capabilities"), [this] { readCapabilities(); });
        capabilities_ = capabilities;
        schedulerLabel_ = note(tr("Read Server capabilities to see available scheduling types."));
        layout->addWidget(schedulerLabel_);
        scheduleSummary_ = note({});
        scheduleSummary_->setObjectName("scheduleSummary");
        layout->addWidget(scheduleSummary_);
        auto* reports = new QTabWidget;
        scheduleResult_ = result("scheduleResult");
        scheduleHistoryResult_ = result("scheduleHistoryResult");
        reports->addTab(scheduleResult_, tr("Schedule response"));
        reports->addTab(scheduleHistoryResult_, tr("Last run and events"));
        scheduleDetail_ = reports;
        layout->addWidget(reports, 1);
        panes_->addTab(schedulerPage_, tr("Server scheduler"));
    }

    void buildRequest()
    {
        auto* page = new QWidget;
        auto* layout = new QVBoxLayout(page);
        request_ = result("backupRequest");
        layout->addWidget(request_);
        auto* actions = new QHBoxLayout;
        layout->addLayout(actions);
        button(*actions, "backupCopyRequest", tr("Copy typed request"), [this] {
            QApplication::clipboard()->setText(request_->toPlainText());
        });
        actions->addStretch();
        panes_->addTab(page, tr("Typed request"));
    }


    bool ready() const
    {
        return session_ && session_->state().connected && !busy_ && !closing_ && !session_->busy() &&
            session_->state().autocommit && !session_->state().transactionActive;
    }

    bool capability(const char* name) const
    {
        return session_ && session_->state().connected && session_->capabilities().value(QLatin1String(name)).toBool();
    }

    bool backups() const { return capability("backup_v1"); }
    bool schedules() const { return capability("jobs_v1"); }
    bool writable() const { return ready() && !session_->profile().read_only; }

    void sync()
    {
        const auto idle = ready();
        const auto backup = idle && backups();
        const auto schedule = idle && schedules();
        const auto backupId = backupId_->text().trimmed();
        const auto selectedJob = backupJobId_->text().trimmed();
        const auto selectedSchedule = scheduleId_->text().trimmed();
        const auto selectedJobState = isHex(activeBackup_) && activeBackup_ == selectedJob;
        const auto scheduleKnown = loadedSchedule_.value("schedule_id").toString() == selectedSchedule;
        const auto kind = scheduleKind_->currentData().toString();
        const auto daily = scheduleType_->currentData().toString() == u"daily";
        const auto sql = kind == u"sql_script";
        const auto retrain = kind == u"retrain";
        const auto nativeRecipes = sdp_library_version() >= 0x00000401u;
        const auto hostCanSchedule = (kind != u"backup" || backups()) &&
            (!sql || (nativeRecipes && capability("scheduled_sql_v1"))) &&
            (!retrain || (nativeRecipes && capability("scheduled_retrain_v1")));
        const auto canWrite = writable();
        backupPage_->setEnabled(!busy_);
        schedulerPage_->setEnabled(!busy_);
        backupSubmit_->setEnabled(backup && canWrite && pendingToken_.isEmpty());
        backupStatus_->setEnabled(backup && isHex(selectedJob));
        backupCancel_->setEnabled(backup && canWrite && selectedJobState && !backupState_.isEmpty() &&
            !terminal(backupState_) && !cancelRequested_);
        followBackup_->setEnabled(backup && selectedJobState && bool(openJob_));
        backupCatalog_->setEnabled(backup);
        backupVerify_->setEnabled(backup && isHex(backupId));
        backupRestore_->setEnabled(backup && canWrite && verifiedArchive_ == backupId &&
            isHex(verifiedDigest_, 64) && restoreName(restoreName_->text().trimmed()));

        scheduleCount_->setEnabled(kind == u"compute");
        scheduleAction_->setEnabled(kind == u"maintenance");
        scheduleForm_->setRowVisible(scheduleSql_, sql);
        for (auto* field : {static_cast<QWidget*>(scheduleModel_), static_cast<QWidget*>(scheduleDataset_),
            static_cast<QWidget*>(scheduleSeed_), static_cast<QWidget*>(scheduleOptions_)})
            scheduleForm_->setRowVisible(field, retrain);
        if ((sql || retrain) && !nativeRecipes)
            schedulerLabel_->setText(tr("This scheduling type needs Native SDK 0.4.1 or later."));
        else if (!hostCanSchedule)
            schedulerLabel_->setText(tr("The connected Server does not advertise this scheduling type."));
        scheduleSeconds_->setEnabled(!daily);
        timezone_->setEnabled(daily);
        hour_->setEnabled(daily);
        minute_->setEnabled(daily);
        scheduleCreate_->setEnabled(schedule && canWrite && pendingToken_.isEmpty() && hostCanSchedule &&
            (!daily || !timezone_->text().trimmed().isEmpty()));
        scheduleStatus_->setEnabled(schedule && isHex(selectedSchedule));
        scheduleDisable_->setEnabled(schedule && canWrite && scheduleKnown && loadedSchedule_.value("enabled").toBool());
        scheduleHistory_->setEnabled(schedule && scheduleKnown && isHex(loadedSchedule_.value("last_job_id").toString()));
        capabilities_->setEnabled(schedule);
    }

    bool confirmed(const QString& title, const QString& prompt)
    {
        return QMessageBox::question(this, title, prompt, QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) == QMessageBox::Yes;
    }

    bool send(bool backupApi, uint32_t operation, const char* operationName, const QJsonObject& body,
        const QString& message, SqlSession::Finished complete, int timeout = 10000, bool preview = true)
    {
        if (!ready()) return false;
        if (preview)
            request_->setPlainText(pretty({{"api", backupApi ? "sdp_backup_command" : "sdp_job_command"},
                {"operation", operationName}, {"request", body}}));
        if (!message.isEmpty()) status_->setText(message);
        const auto bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
        busy_ = true;
        sync();
        const QPointer<BackupCenter> self(this);
        if (session_->request([backupApi, operation, bytes](auto handle, auto* output, auto* error) {
            const sdp_text input{bytes.constData(), static_cast<uint32_t>(bytes.size())};
            return backupApi ? sdp_backup_command(handle, operation, input, output, error) :
                sdp_job_command(handle, operation, input, output, error);
        }, [self, complete = std::move(complete)](const SqlReply& reply) {
            if (!self) return;
            self->busy_ = false;
            complete(reply);
            if (!self) return;
            if (self->closing_)
            {
                self->timer_.stop();
                self->QDialog::reject();
                return;
            }
            self->sync();
        }, timeout)) return true;
        busy_ = false;
        status_->setText(tr("The worksheet could not start the request."));
        sync();
        return false;
    }

    bool accept(const SqlReply& reply, const QString& token = {})
    {
        if (reply.error.code == SDP_OK) return true;
        if (reply.error.unknown_outcome && !token.isEmpty())
        {
            pendingToken_ = token;
            status_->setText(tr("The Server did not confirm this submission. Check request token %1 before retrying.").arg(token));
        }
        else status_->setText(errorText(reply));
        return false;
    }

    void submitBackup()
    {
        if (!backupSubmit_->isEnabled()) return;
        const auto bytes = backupMaxBytes_->value();
        timer_.stop();
        const auto yes = confirmed(tr("Submit Server backup"), tr("Queue a verified Server archive with a limit of %1 bytes?").arg(bytes));
        if (!yes)
        {
            if (!activeBackup_.isEmpty() && !terminal(backupState_)) timer_.start();
            return;
        }
        const auto token = newToken();
        send(true, SDP_BACKUP_SUBMIT, "SDP_BACKUP_SUBMIT", {{"request_token", token}, {"max_bytes", bytes}},
            tr("Submitting the backup job to the Server…"), [this, token](const SqlReply& reply) {
                if (!accept(reply, token)) return;
                const auto id = reply.data.value("job_id").toString();
                if (!isHex(id))
                {
                    pendingToken_ = token;
                    status_->setText(tr("The Server did not return a backup job ID. Check the request outcome before retrying."));
                    return;
                }
                const QSignalBlocker block(backupJobId_);
                backupJobId_->setText(id);
                applyBackupJob(reply.data, id);
                backupDetail_->setCurrentWidget(backupJobResult_);
            });
    }

    void readBackupJob(sdp_backup_operation operation, const QString& id, bool manual = true)
    {
        if (!backups() || !isHex(id)) return;
        const auto cancelling = operation == SDP_BACKUP_CANCEL;
        send(true, operation, cancelling ? "SDP_BACKUP_CANCEL" : "SDP_BACKUP_STATUS", {{"job_id", id}},
            manual ? tr("Reading the backup job on the Server…") : QString{},
            [this, id, manual](const SqlReply& reply) {
                if (backupJobId_->text().trimmed() != id) return;
                if (!accept(reply))
                {
                    activeBackup_.clear();
                    backupState_.clear();
                    setProperty("backupState", QString{});
                    backupJobLabel_->clear();
                    backupJobResult_->clear();
                    timer_.stop();
                    return;
                }
                applyBackupJob(reply.data, id);
                if (manual) backupDetail_->setCurrentWidget(backupJobResult_);
            }, 10000, manual);
    }

    void applyBackupJob(const QJsonObject& response, const QString& id)
    {
        const auto state = response.value("state").toString();
        if (response.value("job_id").toString() != id || !knownJobState(state))
        {
            timer_.stop();
            activeBackup_.clear();
            backupState_.clear();
            setProperty("backupState", QString{});
            backupJobLabel_->clear();
            backupJobResult_->clear();
            status_->setText(tr("The Server returned a different backup job."));
            return;
        }

        activeBackup_ = id;
        backupState_ = state;
        cancelRequested_ = response.value("cancel_requested").toBool();
        setProperty("backupState", state);
        backupJobResult_->setPlainText(pretty(response));
        auto label = tr("Server backup job %1 · %2").arg(id, state);
        if (const auto reason = response.value("reason").toString(); !reason.isEmpty())
            label += QStringLiteral(" · ") + reason;
        if (cancelRequested_ && !terminal(state)) label += tr(" · cancellation requested");
        if (state == u"succeeded")
        {
            const auto proof = response.value("result").toObject();
            const auto archive = proof.value("backup_id").toString();
            if (archiveProof(proof, archive))
            {
                if (backupId_->text().trimmed().isEmpty()) backupId_->setText(archive);
                label += tr(" · Server verified archive %1").arg(archive);
            }
            else label += tr(" · no verified archive proof in the response");
        }
        if (terminal(state)) timer_.stop();
        else if (!closing_ && !timer_.isActive()) timer_.start();
        backupJobLabel_->setText(label);
        status_->setText(label);
    }

    void cancelBackup()
    {
        if (!backupCancel_->isEnabled()) return;
        const auto id = activeBackup_;
        timer_.stop();
        if (!confirmed(tr("Cancel backup job"), tr("Ask the Server to cancel %1?").arg(id)))
        {
            if (!terminal(backupState_)) timer_.start();
            return;
        }
        readBackupJob(SDP_BACKUP_CANCEL, id);
    }

    void catalog()
    {
        if (!backupCatalog_->isEnabled()) return;
        backupCatalogResult_->clear();
        archiveList_->setRowCount(0);
        send(true, SDP_BACKUP_CATALOG, "SDP_BACKUP_CATALOG", {}, tr("Reading the Server archive catalog…"),
            [this](const SqlReply& reply) {
                if (!accept(reply)) return;
                const auto backups = reply.data.value("backups").toArray();
                for (const auto& item : backups)
                {
                    const auto archive = item.toObject();
                    const auto id = archive.value("backup_id").toString();
                    if (!isHex(id)) continue;
                    const auto row = archiveList_->rowCount();
                    archiveList_->insertRow(row);
                    archiveList_->setItem(row, 0, new QTableWidgetItem(id));
                    const auto revision = archive.value("lsn").toInteger();
                    archiveList_->setItem(row, 1, new QTableWidgetItem(revision > 0 ? QString::number(revision) : tr("Unknown")));
                    archiveList_->setItem(row, 2, new QTableWidgetItem(archiveProof(archive, id) ? dateUtc(archive.value("verified_at")) :
                        tr("Verification unavailable")));
                }
                archiveList_->resizeColumnsToContents();
                backupCatalogResult_->setPlainText(pretty(reply.data));
                backupDetail_->setCurrentIndex(1);
                status_->setText(tr("Read %1 Server archives. Point-in-time restore availability: %2.")
                    .arg(backups.size()).arg(reply.data.value("pitr").toBool() ? tr("available") : tr("unavailable")));
            }, 50000);
    }

    void verify()
    {
        if (!backupVerify_->isEnabled()) return;
        const auto id = backupId_->text().trimmed();
        verifiedArchive_.clear();
        verifiedDigest_.clear();
        backupVerifyResult_->clear();
        backupRestoreResult_->clear();
        archiveLabel_->setText(tr("Verification requested on this Server."));
        send(true, SDP_BACKUP_VERIFY, "SDP_BACKUP_VERIFY", {{"backup_id", id}},
            tr("Verifying checksums and a temporary restore on the Server…"), [this, id](const SqlReply& reply) {
                if (backupId_->text().trimmed() != id) return;
                if (!accept(reply) || !archiveProof(reply.data, id))
                {
                    archiveLabel_->setText(tr("The Server did not verify this archive."));
                    if (reply.error.code == SDP_OK) status_->setText(tr("The server returned incomplete verification evidence."));
                    return;
                }
                verifiedArchive_ = id;
                verifiedDigest_ = reply.data.value("manifest_sha256").toString();
                backupVerifyResult_->setPlainText(pretty(reply.data));
                backupDetail_->setCurrentWidget(backupVerifyResult_);
                archiveLabel_->setText(tr("Server verified a temporary restore at revision %1. SHA-256 %2.")
                    .arg(reply.data.value("lsn").toInteger()).arg(verifiedDigest_));
                status_->setText(tr("Server verification passed."));
            }, 60000);
    }

    void restore()
    {
        if (!backupRestore_->isEnabled()) return;
        const auto id = backupId_->text().trimmed();
        const auto name = restoreName_->text().trimmed();
        const auto digest = verifiedDigest_;
        if (!confirmed(tr("Restore to a separate Server instance"),
            tr("Restore archive %1 into the new Server target %2?\n\nVerified manifest SHA-256 %3.").arg(id, name, digest)))
            return;
        backupRestoreResult_->clear();
        send(true, SDP_BACKUP_RESTORE, "SDP_BACKUP_RESTORE", {{"backup_id", id}, {"target_name", name}},
            tr("Restoring the verified archive to a separate Server target…"), [this, id, name, digest](const SqlReply& reply) {
                if (backupId_->text().trimmed() != id || restoreName_->text().trimmed() != name) return;
                if (!accept(reply))
                {
                    if (reply.error.unknown_outcome)
                        status_->setText(tr("Restore outcome is unknown. Check target %1 on the Server before trying another name.").arg(name));
                    return;
                }
                if (!archiveProof(reply.data, id) || !reply.data.value("restored").toBool() ||
                    reply.data.value("target_name").toString() != name)
                {
                    status_->setText(tr("The Server did not return a valid restore proof."));
                    return;
                }
                backupRestoreResult_->setPlainText(pretty(reply.data));
                backupDetail_->setCurrentWidget(backupRestoreResult_);
                if (reply.data.value("manifest_sha256").toString() != digest)
                {
                    verifiedArchive_.clear();
                    verifiedDigest_.clear();
                    archiveLabel_->setText(tr("The archive manifest changed since verification."));
                    status_->setText(tr("The Server restored %1 from a changed manifest. Verify that target before using it.").arg(name));
                    return;
                }
                status_->setText(tr("Server restored and verified %1. Configure a separate Server and TLS profile to connect.").arg(name));
            }, 60000);
    }


    void createSchedule()
    {
        if (!scheduleCreate_->isEnabled()) return;
        const auto kind = scheduleKind_->currentData().toString();
        const auto cadence = scheduleType_->currentData().toString();
        QJsonObject rule{{"misfire", misfire_->currentData().toString()}};
        QString timing;
        if (cadence == u"daily")
        {
            const auto zone = timezone_->text().trimmed();
            rule.insert("timezone", zone);
            rule.insert("hour", hour_->value());
            rule.insert("minute", minute_->value());
            timing = tr("daily at %1:%2 (%3)").arg(hour_->value(), 2, 10, QChar(u'0'))
                .arg(minute_->value(), 2, 10, QChar(u'0')).arg(zone);
        }
        else
        {
            rule.insert("every_seconds", scheduleSeconds_->value());
            timing = tr("every %1 seconds").arg(scheduleSeconds_->value());
        }
        const auto token = newToken();
        QJsonObject body{{"kind", kind}, {"rule", rule}, {"retries", retries_->value()},
            {"backoff_seconds", backoff_->value()}, {"request_token", token}};
        if (kind == u"compute") body.insert("count", scheduleCount_->value());
        if (kind == u"maintenance") body.insert("action", scheduleAction_->currentData().toString());
        if (kind == u"backup") body.insert("max_bytes", backupMaxBytes_->value());
        if (kind == u"sql_script")
        {
            const auto source = scheduleSql_->toPlainText();
            if (source.toUtf8().size() > 16384)
            {
                status_->setText(tr("The scheduled SQL exceeds 16 KiB."));
                return;
            }
            const auto batch = split_sql(source);
            if (batch.incomplete || batch.statements.isEmpty() || batch.statements.size() > 16)
            {
                status_->setText(tr("Enter one to sixteen complete SQL statements."));
                return;
            }
            QJsonArray statements;
            for (const auto& statement : batch.statements)
            {
                if (statement.text.toUtf8().size() > 4096)
                {
                    status_->setText(tr("A scheduled SQL statement exceeds 4 KiB."));
                    return;
                }
                statements.append(statement.text);
            }
            body.insert("sql", statements);
        }
        if (kind == u"retrain")
        {
            const auto model = scheduleModel_->text().trimmed();
            const auto dataset = scheduleDataset_->text().trimmed();
            const auto options = QJsonDocument::fromJson(scheduleOptions_->toPlainText().toUtf8());
            if (model.isEmpty() || model.toUtf8().size() > 392 || !isHex(dataset) || !options.isObject() ||
                options.toJson(QJsonDocument::Compact).size() > 1024)
            {
                status_->setText(tr("Enter a model, a 32-character dataset ID and a JSON options object of at most 1 KiB."));
                return;
            }
            body.insert("name", model);
            body.insert("dataset_id", dataset);
            body.insert("seed", scheduleSeed_->value());
            body.insert("options", options.object());
        }
        QMessageBox review(QMessageBox::Question, tr("Create Server schedule"),
            tr("Schedule %1 %2? The Server runs the schedule independently of Workbench.")
                .arg(scheduleKind_->currentText(), timing), QMessageBox::Yes | QMessageBox::No, this);
        review.setDefaultButton(QMessageBox::No);
        review.setDetailedText(pretty(body));
        if (review.exec() != QMessageBox::Yes) return;
        send(false, SDP_JOB_SCHEDULE_CREATE, "SDP_JOB_SCHEDULE_CREATE", body, tr("Creating the persistent schedule…"),
            [this, token](const SqlReply& reply) {
                if (!accept(reply, token)) return;
                const auto id = reply.data.value("schedule_id").toString();
                if (!isHex(id))
                {
                    pendingToken_ = token;
                    status_->setText(tr("The Server did not return a schedule ID. Check the request outcome before retrying."));
                    return;
                }
                const QSignalBlocker block(scheduleId_);
                scheduleId_->setText(id);
                scheduleHistoryResult_->clear();
                applySchedule(reply.data, id);
            });
    }

    void querySchedule(sdp_job_operation operation)
    {
        const auto id = scheduleId_->text().trimmed();
        if (!schedules() || !isHex(id)) return;
        const bool disabling = operation == SDP_JOB_SCHEDULE_CANCEL;
        send(false, operation, disabling ? "SDP_JOB_SCHEDULE_CANCEL" : "SDP_JOB_SCHEDULE_STATUS",
            {{"schedule_id", id}}, disabling ? tr("Disabling the Server schedule…") : tr("Reading the Server schedule…"),
            [this, id](const SqlReply& reply) {
                if (scheduleId_->text().trimmed() != id) return;
                if (!accept(reply))
                {
                    loadedSchedule_ = {};
                    scheduleResult_->clear();
                    scheduleHistoryResult_->clear();
                    scheduleSummary_->clear();
                    return;
                }
                applySchedule(reply.data, id);
            });
    }

    void disableSchedule()
    {
        if (!scheduleDisable_->isEnabled()) return;
        const auto id = scheduleId_->text().trimmed();
        if (confirmed(tr("Disable Server schedule"),
            tr("Disable future runs of schedule %1? An active run remains available in Job Center.").arg(id)))
            querySchedule(SDP_JOB_SCHEDULE_CANCEL);
    }

    void applySchedule(const QJsonObject& response, const QString& id)
    {
        if (response.value("schedule_id").toString() != id || !response.value("enabled").isBool() ||
            !response.value("rule").isObject() || !response.value("next_utc").isDouble())
        {
            loadedSchedule_ = {};
            scheduleResult_->clear();
            scheduleHistoryResult_->clear();
            scheduleSummary_->clear();
            status_->setText(tr("The Server returned an invalid schedule response."));
            return;
        }
        if (loadedSchedule_.value("last_job_id") != response.value("last_job_id")) scheduleHistoryResult_->clear();
        loadedSchedule_ = response;
        scheduleResult_->setPlainText(pretty(response));
        scheduleDetail_->setCurrentWidget(scheduleResult_);
        const auto lastId = response.value("last_job_id").toString();
        auto summary = tr("Server schedule %1").arg(id);
        const auto enabled = response.value("enabled").toBool();
        if (enabled) summary += tr(" · enabled · next due in UTC: %1").arg(dateUtc(response.value("next_utc")));
        else summary += tr(" · disabled");
        if (isHex(lastId)) summary += tr(" · last job %1").arg(lastId);
        scheduleSummary_->setText(summary);
        status_->setText(summary);
    }

    void readScheduleHistory()
    {
        if (!scheduleHistory_->isEnabled()) return;
        const auto scheduleId = scheduleId_->text().trimmed();
        const auto jobId = loadedSchedule_.value("last_job_id").toString();
        scheduleHistoryResult_->clear();
        send(false, SDP_JOB_WATCH, "SDP_JOB_WATCH", {{"job_id", jobId}}, tr("Reading the last dispatched run and its events…"),
            [this, scheduleId, jobId](const SqlReply& reply) {
                if (scheduleId_->text().trimmed() != scheduleId) return;
                if (!accept(reply)) return;
                if (reply.data.value("job_id").toString() != jobId)
                {
                    status_->setText(tr("The Server returned a different schedule run."));
                    return;
                }
                scheduleHistoryResult_->setPlainText(pretty(reply.data));
                scheduleDetail_->setCurrentWidget(scheduleHistoryResult_);
                status_->setText(tr("Last dispatched run %1 · %2").arg(jobId, reply.data.value("state").toString()));
            });
    }

    void readCapabilities()
    {
        if (!capabilities_->isEnabled()) return;
        send(false, SDP_JOB_CAPABILITIES, "SDP_JOB_CAPABILITIES", {}, tr("Reading the Server scheduler capabilities…"),
            [this](const SqlReply& reply) {
                if (!accept(reply)) return;
                QStringList unavailable;
                for (const auto& item : reply.data.value("unavailable_kinds").toArray())
                    if (!item.toString().isEmpty()) unavailable.append(item.toString());
                const auto paused = reply.data.value("scheduler_paused");
                auto message = paused.isBool() ? (paused.toBool() ?
                    tr("The Server scheduler is paused.") : tr("The Server scheduler is running.")) :
                    tr("The Server did not report whether scheduling is paused.");
                if (!unavailable.isEmpty())
                    message += u' ' + tr("Unavailable task kinds: %1.").arg(unavailable.join(QStringLiteral(", ")));
                schedulerLabel_->setText(message);
                scheduleHistoryResult_->setPlainText(pretty(reply.data));
                scheduleDetail_->setCurrentWidget(scheduleHistoryResult_);
                status_->setText(message);
            });
    }

    QPointer<SqlSession> session_;
    const std::function<void(const QString&)> openJob_;
    bool busy_{}, closing_{}, cancelRequested_{};
    QString activeBackup_, backupState_, verifiedArchive_, verifiedDigest_, pendingToken_;
    QTimer timer_;
    QJsonObject loadedSchedule_;
    QTabWidget *panes_{}, *backupDetail_{}, *scheduleDetail_{};
    QFormLayout* scheduleForm_{};
    QWidget *backupPage_{}, *schedulerPage_{};
    QLineEdit *backupJobId_{}, *backupId_{}, *restoreName_{}, *scheduleId_{}, *timezone_{};
    QLineEdit *scheduleModel_{}, *scheduleDataset_{};
    QLabel *status_{}, *backupJobLabel_{}, *archiveLabel_{}, *schedulerLabel_{}, *scheduleSummary_{};
    QPlainTextEdit *backupJobResult_{}, *backupCatalogResult_{}, *backupVerifyResult_{}, *backupRestoreResult_{};
    QPlainTextEdit *scheduleResult_{}, *scheduleHistoryResult_{}, *request_{};
    QPlainTextEdit *scheduleSql_{}, *scheduleOptions_{};
    QTableWidget* archiveList_{};
    QPushButton *backupSubmit_{}, *backupStatus_{}, *backupCancel_{}, *followBackup_{};
    QPushButton *backupCatalog_{}, *backupVerify_{}, *backupRestore_{};
    QPushButton *scheduleCreate_{}, *scheduleStatus_{}, *scheduleDisable_{}, *scheduleHistory_{}, *capabilities_{};
    QComboBox *scheduleKind_{}, *scheduleAction_{}, *scheduleType_{}, *misfire_{};
    QSpinBox *backupMaxBytes_{}, *scheduleCount_{}, *scheduleSeconds_{}, *hour_{}, *minute_{}, *retries_{}, *backoff_{};
    QSpinBox* scheduleSeed_{};
};
}

QDialog* showBackupCenter(SqlSession& session, QWidget* parent, std::function<void(const QString&)> openJob)
{
    auto* center = new BackupCenter(session, parent, std::move(openJob));
    center->show();
    return center;
}
}

