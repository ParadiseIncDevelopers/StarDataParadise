#include "job_center.hpp"

#include "sql_session.hpp"

#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QLockFile>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QTimeZone>
#include <QUuid>
#include <QVBoxLayout>

#include <limits>
#include <optional>

namespace wb
{
namespace
{
constexpr qsizetype persistenceLimit = 128 * 1024;
constexpr qsizetype recentLimit = 20;
constexpr int watchIntervalMs = 400;

bool hexId(QStringView text, qsizetype length)
{
    if (text.size() != length) return false;
    for (const auto character : text)
    {
        const auto value = character.unicode();
        if ((value < u'0' || value > u'9') && (value < u'a' || value > u'f')) return false;
    }
    return true;
}

bool terminal(QStringView state)
{
    return state == u"paused" || state == u"succeeded" || state == u"failed" || state == u"cancelled";
}

bool knownState(QStringView state)
{
    return terminal(state) || state == u"queued" || state == u"starting" ||
        state == u"running" || state == u"checkpointing";
}

QString displayState(QStringView state)
{
    if (state == u"queued") return QObject::tr("Queued");
    if (state == u"starting") return QObject::tr("Starting");
    if (state == u"running") return QObject::tr("Running");
    if (state == u"checkpointing") return QObject::tr("Checkpointing");
    if (state == u"paused") return QObject::tr("Paused");
    if (state == u"succeeded") return QObject::tr("Succeeded");
    if (state == u"failed") return QObject::tr("Failed");
    if (state == u"cancelled") return QObject::tr("Cancelled");
    return QObject::tr("Unknown");
}

QString displayError(const SqlReply& reply)
{
    if (reply.error.code == SDP_PERMISSION_DENIED) return QObject::tr("The server did not grant access to this job.");
    if (reply.error.code == SDP_FEATURE_UNSUPPORTED) return QObject::tr("The server does not support this job operation.");
    if (reply.error.code == SDP_MODULE_UNAVAILABLE) return QObject::tr("The requested server module is unavailable.");
    if (reply.error.code == SDP_INVALID_STATE) return QObject::tr("The job or checkpoint is no longer in the required state.");
    if (reply.error.code == SDP_INCOMPATIBLE_PROTOCOL) return QObject::tr("The server returned an incompatible job response.");
    const auto detail = QString::fromUtf8(reply.error.message);
    if (!detail.isEmpty()) return detail;
    return QObject::tr("Server request failed with status %1.").arg(reply.error.code);
}

QString pretty(const QJsonObject& payload)
{
    return QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Indented));
}

// Hash the connection and trust identity so the local file needs no account or certificate details.
QString profileKey(const Profile& profile)
{
    QFile certificate(profile.ca_file);
    if (!certificate.open(QIODevice::ReadOnly) || certificate.size() <= 0 ||
        certificate.size() > 1024 * 1024) return {};
    const auto bytes = certificate.read(1024 * 1024 + 1);
    if (bytes.isEmpty() || bytes.size() > 1024 * 1024 || certificate.error() != QFileDevice::NoError) return {};
    const auto trust = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    const auto tlsName = profile.tls_name.isEmpty() ? profile.host : profile.tls_name;
    const QJsonArray connection{profile.host.toLower(), profile.port, tlsName.toLower(), profile.user, profile.database, trust};
    const auto identity = QJsonDocument(connection).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
}

std::optional<QJsonObject> savedState(const QString& path)
{
    QFile file(path);
    if (!file.exists()) return QJsonObject{{"version", 1}, {"profiles", QJsonObject{}}};
    if (!file.open(QIODevice::ReadOnly) || file.size() > persistenceLimit) return {};
    const auto bytes = file.read(persistenceLimit + 1);
    if (bytes.size() > persistenceLimit || file.error() != QFileDevice::NoError) return {};
    QJsonParseError error;
    const auto parsed = QJsonDocument::fromJson(bytes, &error);
    const auto object = parsed.object();
    if (error.error != QJsonParseError::NoError || object.value("version").toInt() != 1 ||
        !object.value("profiles").isObject()) return {};
    return object;
}

QStringList recentIds(const QJsonObject& state, const QString& key)
{
    QStringList ids;
    for (const auto& value : state.value("profiles").toObject().value(key).toArray())
    {
        const auto id = value.toString();
        if (hexId(id, 32) && !ids.contains(id)) ids.append(id);
        if (ids.size() == recentLimit) break;
    }
    return ids;
}

bool rememberId(const QString& path, const QString& key, const QString& id)
{
    if (path.isEmpty() || key.isEmpty()) return false;
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QLockFile lock(path + QStringLiteral(".lock"));
    if (!lock.tryLock(40)) return false;
    auto state = savedState(path);
    if (!state) return false;
    auto profiles = state->value("profiles").toObject();
    auto ids = recentIds(*state, key);
    if (!ids.isEmpty() && ids.first() == id) return true;
    ids.removeAll(id);
    ids.prepend(id);
    if (ids.size() > recentLimit) ids.removeLast();
    profiles.insert(key, QJsonArray::fromStringList(ids));
    state->insert("profiles", profiles);
    const auto bytes = QJsonDocument(*state).toJson(QJsonDocument::Compact);
    if (bytes.size() > persistenceLimit) return false;
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

class JobCenter final : public QDialog
{
public:
    JobCenter(SqlSession& session, QWidget* parent, const QString& stateFile, const QString& initialId, const QString& model)
        : QDialog(parent), session_(&session), stateFile_(stateFile), profileKey_(profileKey(session.profile()))
    {
        setObjectName("jobCenter");
        setWindowTitle(tr("Job Center"));
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowModality(Qt::WindowModal);
        resize(820, 650);

        auto* layout = new QVBoxLayout(this);
        auto* form = new QFormLayout;
        layout->addLayout(form);
        recent_ = new QComboBox;
        recent_->setObjectName("jobRecent");
        form->addRow(tr("Recently opened job"), recent_);
        auto* lookup = new QHBoxLayout;
        id_ = new QLineEdit;
        id_->setObjectName("jobId");
        id_->setMaxLength(32);
        id_->setPlaceholderText(tr("32-character Server job ID"));
        lookup->addWidget(id_, 1);
        open_ = button(*lookup, "jobOpen", tr("Open job"), [this] { openJob(id_->text().trimmed()); });
        form->addRow(tr("Server job ID"), lookup);
        model_ = new QLineEdit(model);
        model_->setObjectName("jobModel");
        model_->setMaxLength(390);
        model_->setPlaceholderText(tr("Qualified model name for checkpoint and resume"));
        form->addRow(tr("Model"), model_);

        auto* actions = new QHBoxLayout;
        layout->addLayout(actions);
        watch_ = button(*actions, "jobWatch", tr("Refresh status"), [this] { readJob(SDP_JOB_WATCH); });
        cancel_ = button(*actions, "jobCancel", tr("Cancel job…"), [this] { cancelJob(); });
        checkpoint_ = button(*actions, "jobCheckpoint", tr("Verify checkpoint"), [this] { readCheckpoint(); });
        resume_ = button(*actions, "jobResume", tr("Resume from checkpoint…"), [this] { resumeJob(); });
        actions->addStretch();

        status_ = new QLabel;
        status_->setObjectName("jobStatus");
        status_->setTextFormat(Qt::PlainText);
        status_->setWordWrap(true);
        status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(status_);
        progressText_ = new QLabel;
        progressText_->setObjectName("jobProgressText");
        progressText_->setTextFormat(Qt::PlainText);
        layout->addWidget(progressText_);
        progress_ = new QProgressBar;
        progress_->setObjectName("jobProgress");
        progress_->setTextVisible(false);
        progress_->hide();
        layout->addWidget(progress_);

        auto* details = new QTabWidget;
        layout->addWidget(details, 1);
        events_ = new QTableWidget(0, 2);
        events_->setObjectName("jobEvents");
        events_->setHorizontalHeaderLabels({tr("Server event"), tr("Time (UTC)")});
        events_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        details->addTab(events_, tr("Server event log"));
        jobResult_ = new QPlainTextEdit;
        jobResult_->setObjectName("jobResult");
        jobResult_->setReadOnly(true);
        details->addTab(jobResult_, tr("Job response"));
        manifestResult_ = new QPlainTextEdit;
        manifestResult_->setObjectName("jobCheckpointResult");
        manifestResult_->setReadOnly(true);
        details->addTab(manifestResult_, tr("Verified checkpoint"));

        timer_.setInterval(watchIntervalMs);
        connect(&timer_, &QTimer::timeout, this, [this] {
            if (ready()) readJob(SDP_JOB_WATCH);
        });
        connect(id_, &QLineEdit::returnPressed, open_, &QPushButton::click);
        connect(id_, &QLineEdit::textChanged, this, [this] {
            if (id_->text().trimmed() != activeId_)
            {
                resetJob();
                status_->setText(available() ? tr("Open the ID to read the job's current Server state.") :
                    tr("Job monitoring is unavailable on this server connection."));
            }
            sync();
        });
        connect(model_, &QLineEdit::textChanged, this, [this] { resetManifest(); sync(); });
        connect(recent_, &QComboBox::activated, this, [this](int index) {
            const auto selected = recent_->itemData(index).toString();
            if (!selected.isEmpty()) openJob(selected);
        });
        connect(&session, &QObject::destroyed, this, [this] {
            busy_ = false;
            resetJob();
            status_->setText(tr("The worksheet connection closed."));
            sync();
        });

        if (!stateFile_.isEmpty() && !profileKey_.isEmpty())
            if (const auto saved = savedState(stateFile_)) recentIds_ = recentIds(*saved, profileKey_);
        updateRecent();
        status_->setText(available() ? tr("Open a Server job by its ID.") :
            tr("Job monitoring is unavailable on this server connection."));
        const auto selected = initialId.isEmpty() ? recentIds_.value(0) : initialId;
        if (!selected.isEmpty())
        {
            id_->setText(selected);
            QTimer::singleShot(0, this, [this, selected] { openJob(selected); });
        }
        sync();
    }

    void reject() override
    {
        if (busy_)
        {
            closing_ = true;
            timer_.stop();
            status_->setText(tr("Waiting for the current request to finish…"));
            return;
        }
        QDialog::reject();
    }

private:
    QPushButton* button(QHBoxLayout& row, const char* name, const QString& title, auto run)
    {
        auto* control = new QPushButton(title);
        control->setObjectName(name);
        row.addWidget(control);
        connect(control, &QPushButton::clicked, this, std::move(run));
        return control;
    }

    bool available() const
    {
        return session_ && session_->state().connected && session_->capabilities().value("jobs_v1").toBool();
    }

    bool ready() const
    {
        return available() && !busy_ && !session_->busy() && session_->state().autocommit &&
            !session_->state().transactionActive;
    }

    bool hasCheckpoints() const
    {
        return available() && session_->capabilities().value("model_checkpoints_v1").toBool();
    }

    bool mayMutate() const
    {
        return ready() && !session_->profile().read_only;
    }

    void sync()
    {
        const bool idle = ready();
        const bool open = hexId(activeId_, 32);
        const bool paused = state_ == u"paused";
        const bool model = !model_->text().trimmed().isEmpty();
        id_->setEnabled(!busy_);
        recent_->setEnabled(idle && !recentIds_.isEmpty());
        model_->setEnabled(!busy_);
        open_->setEnabled(idle && hexId(id_->text().trimmed(), 32));
        watch_->setEnabled(idle && open);
        cancel_->setEnabled(mayMutate() && open && knownState(state_) && !terminal(state_) &&
            !image_.value("cancel_requested").toBool());
        checkpoint_->setEnabled(idle && paused && hasCheckpoints() && model);
        checkpoint_->setToolTip(hasCheckpoints() ? tr("Verify the stored training checkpoint on the Server.") :
            tr("The connected Server does not offer model checkpoints."));
        resume_->setEnabled(mayMutate() && paused && hasCheckpoints() && !verifiedChecksum_.isEmpty() &&
            pendingResumeToken_.isEmpty());
    }


    void resetManifest()
    {
        verifiedChecksum_.clear();
        verifiedModel_.clear();
        manifestResult_->clear();
    }

    void resetJob()
    {
        timer_.stop();
        activeId_.clear();
        state_.clear();
        setProperty("jobState", QString{});
        status_->setProperty("jobState", QString{});
        image_ = {};
        events_->setRowCount(0);
        progress_->hide();
        progressText_->clear();
        jobResult_->clear();
        resetManifest();
    }

    void updateRecent()
    {
        const QSignalBlocker block(recent_);
        recent_->clear();
        recent_->addItem(tr("Select a recent ID"), QString{});
        for (const auto& id : recentIds_) recent_->addItem(id, id);
        const auto current = recent_->findData(activeId_);
        if (current >= 0) recent_->setCurrentIndex(current);
    }

    bool rememberCurrent()
    {
        if (!recentIds_.isEmpty() && recentIds_.first() == activeId_) return !recentSaveFailed_;
        recentIds_.removeAll(activeId_);
        recentIds_.prepend(activeId_);
        if (recentIds_.size() > recentLimit) recentIds_.removeLast();
        recentSaveFailed_ = !stateFile_.isEmpty() && !rememberId(stateFile_, profileKey_, activeId_);
        updateRecent();
        return !recentSaveFailed_;
    }

    void openJob(const QString& id)
    {
        if (!ready() || !hexId(id, 32))
        {
            if (!available()) status_->setText(tr("Job monitoring is unavailable on this server connection."));
            else if (!hexId(id, 32)) status_->setText(tr("Enter a valid 32-character Server job ID."));
            else status_->setText(tr("Finish the current worksheet operation and enable autocommit to check jobs."));
            sync();
            return;
        }
        resetJob();
        activeId_ = id;
        const QSignalBlocker block(id_);
        id_->setText(id);
        status_->setText(tr("Reading the job from the Server…"));
        readJob(SDP_JOB_STATUS);
    }

    bool send(bool modelApi, uint32_t operation, const QJsonObject& body, SqlSession::Finished finished)
    {
        if (!ready()) return false;
        const auto bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
        const auto request = [modelApi, operation, bytes](auto connection, auto* output, auto* error) {
            const sdp_text input{bytes.constData(), static_cast<uint32_t>(bytes.size())};
            return modelApi ? sdp_model_command(connection, operation, input, output, error) :
                sdp_job_command(connection, operation, input, output, error);
        };
        busy_ = true;
        sync();
        QPointer<JobCenter> self(this);
        const auto started = session_->request(request, [self, finished = std::move(finished)](const SqlReply& reply) {
            if (!self) return;
            self->busy_ = false;
            finished(reply);
            if (!self) return;
            if (self->closing_)
            {
                self->timer_.stop();
                self->QDialog::reject();
                return;
            }
            self->sync();
        }, 5000);
        if (started) return true;
        busy_ = false;
        sync();
        status_->setText(tr("The worksheet could not start the request."));
        return false;
    }

    void readJob(sdp_job_operation operation)
    {
        if (!hexId(activeId_, 32)) return;
        const auto id = activeId_;
        send(false, operation, {{"job_id", id}}, [this, id](const SqlReply& reply) {
            applyJobReply(reply, id);
        });
    }

    void applyJobReply(const SqlReply& reply, const QString& id)
    {
        if (activeId_ != id || id_->text().trimmed() != id) return;
        if (reply.error.code != SDP_OK)
        {
            resetJob();
            status_->setText(displayError(reply));
            return;
        }
        if (!applyJobImage(reply.data, id))
        {
            resetJob();
            status_->setText(tr("The Server returned an invalid job response."));
        }
    }

    bool applyJobImage(const QJsonObject& payload, const QString& id)
    {
        const auto currentState = payload.value("state").toString();
        if (payload.value("job_id").toString() != id || !knownState(currentState) ||
            !payload.value("events").isArray() || !payload.value("attempt").isDouble() ||
            !payload.value("cancel_requested").isBool()) return false;
        if (activeId_ != id || currentState != u"paused") resetManifest();
        activeId_ = id;
        state_ = currentState;
        setProperty("jobState", state_);
        status_->setProperty("jobState", state_);

        const bool stored = rememberCurrent();
        if (image_ != payload)
        {
            image_ = payload;
            jobResult_->setPlainText(pretty(payload));
            showEvents(payload.value("events").toArray());
        }
        auto line = tr("%1 · attempt %2").arg(displayState(currentState)).arg(payload.value("attempt").toInteger());
        if (const auto reason = payload.value("reason").toString(); !reason.isEmpty())
            line += QStringLiteral(" · ") + reason;
        if (payload.value("cancel_requested").toBool() && !terminal(currentState))
            line += QStringLiteral(" · ") + tr("Cancel requested");
        if (const auto pid = payload.value("worker_pid").toInteger(); pid > 0)
            line += tr(" · worker %1").arg(pid);
        if (!stored) line += tr(" · Could not save the recent ID in this workspace");
        status_->setText(line);
        showProgress(payload.value("progress").toObject());

        if (terminal(currentState)) timer_.stop();
        else if (!closing_ && !timer_.isActive()) timer_.start();
        return true;
    }

    void showEvents(const QJsonArray& entries)
    {
        events_->setRowCount(0);
        for (const auto& entry : entries)
        {
            const auto event = entry.toObject();
            const auto stage = event.value("state").toString();
            if (!knownState(stage) || !event.value("at").isDouble()) continue;
            const auto row = events_->rowCount();
            events_->insertRow(row);
            events_->setItem(row, 0, new QTableWidgetItem(displayState(stage)));
            const auto at = QDateTime::fromSecsSinceEpoch(event.value("at").toInteger(), QTimeZone::utc());
            events_->setItem(row, 1, new QTableWidgetItem(at.toString(Qt::ISODate)));
        }
        events_->resizeColumnsToContents();
    }

    void showProgress(const QJsonObject& report)
    {
        const auto done = report.value("done").toInteger(-1);
        const auto total = report.value("total").toInteger(-1);
        const bool measured = done >= 0 && total > 0 && done <= total;
        progressText_->setText(measured ? tr("Server progress: %1 of %2").arg(done).arg(total) :
            tr("The Server did not report a work total for this state."));
        const bool fits = measured && total <= std::numeric_limits<int>::max();
        if (fits)
        {
            progress_->setRange(0, static_cast<int>(total));
            progress_->setValue(static_cast<int>(done));
        }
        progress_->setVisible(fits);
    }

    void cancelJob()
    {
        if (!cancel_->isEnabled()) return;
        // The timer can still run while the confirmation dialog has its own event loop.
        const auto id = activeId_;
        timer_.stop();
        const auto yes = QMessageBox::question(this, tr("Cancel Server job"),
            tr("Request cancellation of job %1? The Server will report its final state.").arg(id),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
        if (!yes)
        {
            if (!terminal(state_)) timer_.start();
            return;
        }
        send(false, SDP_JOB_CANCEL, {{"job_id", id}}, [this, id](const SqlReply& reply) {
            applyJobReply(reply, id);
        });
    }

    void readCheckpoint()
    {
        if (!checkpoint_->isEnabled()) return;
        const auto name = model_->text().trimmed();
        const auto id = activeId_;
        resetManifest();
        status_->setText(tr("Verifying the stored checkpoint on the Server…"));
        send(true, SDP_MODEL_CHECKPOINT, {{"name", name}, {"job_id", id}},
            [this, id, name](const SqlReply& reply) {
                if (activeId_ != id || id_->text().trimmed() != id || model_->text().trimmed() != name) return;
                if (reply.error.code != SDP_OK)
                {
                    status_->setText(displayError(reply));
                    return;
                }
                const auto manifest = reply.data;
                const auto checksum = manifest.value("checksum").toString();
                const auto expected = image_.value("result").toObject().value("checksum").toString();
                if (manifest.value("job_id").toString() != id || !hexId(checksum, 64) ||
                    (!expected.isEmpty() && expected != checksum) || manifest.value("epoch").toInteger() <= 0 ||
                    manifest.value("runtime").toString().isEmpty() || manifest.value("dataset_id").toString().isEmpty())
                {
                    status_->setText(tr("The Server returned an invalid or changed checkpoint manifest."));
                    return;
                }
                verifiedModel_ = name;
                verifiedChecksum_ = checksum;
                manifestResult_->setPlainText(pretty(manifest));
                status_->setText(tr("Paused · Server verified the checkpoint at epoch %1.")
                    .arg(manifest.value("epoch").toInteger()));
            });
    }

    void resumeJob()
    {
        if (!resume_->isEnabled() || verifiedModel_ != model_->text().trimmed()) return;
        const auto id = activeId_;
        const auto name = verifiedModel_;
        const auto checksum = verifiedChecksum_;
        if (QMessageBox::question(this, tr("Resume Server job"),
            tr("Resume training for %1 from the verified checkpoint of %2? The Server will create a new job ID.")
                .arg(name, id), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;

        const auto token = QString::fromLatin1(QUuid::createUuid().toRfc4122().toHex());
        status_->setText(tr("Requesting checksum-verified resume on the Server…"));
        send(true, SDP_MODEL_RESUME, {{"name", name}, {"job_id", id}, {"checksum", checksum}, {"request_token", token}},
            [this, id, token](const SqlReply& reply) {
                if (reply.error.unknown_outcome)
                {
                    pendingResumeToken_ = token;
                    status_->setText(tr("The Server did not confirm resume. Check the request outcome for token %1.").arg(token));
                    return;
                }
                if (reply.error.code != SDP_OK)
                {
                    resetManifest();
                    status_->setText(displayError(reply));
                    return;
                }
                const auto newId = reply.data.value("job_id").toString();
                if (newId == id || !hexId(newId, 32))
                {
                    pendingResumeToken_ = token;
                    status_->setText(tr("The Server did not return a new job ID. Check the request outcome for token %1.").arg(token));
                    return;
                }
                const QSignalBlocker block(id_);
                id_->setText(newId);
                if (!applyJobImage(reply.data, newId))
                {
                    resetJob();
                    pendingResumeToken_ = token;
                    status_->setText(tr("The Server did not return a valid resume status. Check the request outcome for token %1.").arg(token));
                }
            });
    }

    QPointer<SqlSession> session_;
    const QString stateFile_, profileKey_;
    QString activeId_, state_, verifiedModel_, verifiedChecksum_, pendingResumeToken_;
    QStringList recentIds_;
    QJsonObject image_;
    QTimer timer_;
    bool busy_{}, closing_{}, recentSaveFailed_{};
    QComboBox* recent_{};
    QLineEdit *id_{}, *model_{};
    QPushButton *open_{}, *watch_{}, *cancel_{}, *checkpoint_{}, *resume_{};
    QLabel *status_{}, *progressText_{};
    QProgressBar* progress_{};
    QPlainTextEdit *jobResult_{}, *manifestResult_{};
    QTableWidget* events_{};
};
}

QDialog* showJobCenter(SqlSession& session, QWidget* parent, const QString& stateFile, const QString& jobId,
    const QString& model)
{
    auto* center = new JobCenter(session, parent, stateFile, jobId, model);
    center->show();
    return center;
}
}

