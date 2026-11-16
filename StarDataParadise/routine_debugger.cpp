#include "routine_debugger.hpp"

#include "sql_session.hpp"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QThreadPool>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <memory>

namespace wb
{
namespace
{
constexpr int pausePollingMs = 120;
constexpr int leasePollingMs = 450;

sdp_text wire(const QByteArray& text)
{
    return {text.constData(), static_cast<uint32_t>(text.size())};
}

QString joinedName(const QJsonArray& parts, bool requireQualified = true)
{
    if (parts.isEmpty() || parts.size() > 3 || (requireQualified && parts.size() != 3)) return {};
    QStringList names;
    for (const auto& part : parts)
    {
        const auto name = part.toString();
        if (name.isEmpty() || name.contains(u'.') || name.contains(QChar(u'\0')) || name.toUtf8().size() > 128)
            return {};
        names.append(name);
    }
    return names.join(u'.');
}

QString diagnostic(const SqlReply& reply)
{
    const auto detail = QString::fromUtf8(reply.error.message);
    return detail.isEmpty() ? status(reply.error.code) : detail;
}

QString valueText(const QJsonObject& value)
{
    const auto type = value.value("t").toString();
    if (type == u"null") return QStringLiteral("NULL");
    if (type == u"text" || type == u"i64") return value.value("v").toString();
    if (type == u"bool") return value.value("v").toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact));
}

bool terminal(QStringView state)
{
    return state == u"finished" || state == u"cancelled" || state == u"timed_out";
}

QTableWidget* table(const char* name, const QStringList& headers)
{
    auto* result = new QTableWidget(0, static_cast<int>(headers.size()));
    result->setObjectName(name);
    result->setHorizontalHeaderLabels(headers);
    result->setEditTriggers(QAbstractItemView::NoEditTriggers);
    result->setSelectionBehavior(QAbstractItemView::SelectRows);
    result->setSelectionMode(QAbstractItemView::SingleSelection);
    result->setAlternatingRowColors(true);
    result->verticalHeader()->hide();
    result->horizontalHeader()->setStretchLastSection(true);
    return result;
}

void fillRow(QTableWidget& table, int row, const QStringList& cells)
{
    for (int column = 0; column < cells.size(); ++column)
        table.setItem(row, column, new QTableWidgetItem(cells.at(column)));
}

class RoutineDebugger final : public QDialog
{
public:
    RoutineDebugger(SqlSession& session, QThreadPool& workers, const QJsonObject& metadata, QWidget* parent)
        : QDialog(parent), target_(&session), workers_(workers), metadata_(metadata),
          routine_(joinedName(metadata.value("qualified_name").toArray()))
    {
        const auto version = metadata.value("schema_version").toInteger();
        if (metadata.value("kind").toInt() == 6 && !metadata.value("id").toString().isEmpty() &&
            version > 0 && version <= std::numeric_limits<quint32>::max()) revision_ = static_cast<quint32>(version);

        setAttribute(Qt::WA_DeleteOnClose);
        setWindowModality(Qt::NonModal);
        setObjectName("routineDebugger");
        setWindowTitle(tr("Routine debugger"));
        resize(870, 700);
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(label("debugRoutine", tr("%1 · source revision %2").arg(routine_).arg(revision_)));
        access_ = label("debugAccess", tr("Attach with a separate control connection to this worksheet."));
        layout->addWidget(access_);
        auto* connectionRow = new QHBoxLayout;
        layout->addLayout(connectionRow);
        password_ = new QLineEdit;
        password_->setObjectName("debugPassword");
        password_->setEchoMode(QLineEdit::Password);
        password_->setPlaceholderText(tr("Password for %1").arg(session.profile().user));
        connectionRow->addWidget(password_, 1);
        attach_ = button(*connectionRow, "debugAttach", tr("Attach"), [this] { attach(); });
        detach_ = button(*connectionRow, "debugDetach", tr("Detach"), [this] { detach(false); });
        connect(password_, &QLineEdit::textChanged, this, [this] { controls(); });
        connect(password_, &QLineEdit::returnPressed, attach_, &QPushButton::click);

        auto* breakpoints = new QHBoxLayout;
        layout->addLayout(breakpoints);
        breakpoints->addWidget(label("debugLineLabel", tr("Source line")));
        line_ = new QSpinBox;
        line_->setObjectName("debugLine");
        line_->setRange(1, std::numeric_limits<int>::max());
        breakpoints->addWidget(line_);
        setBreakpoint_ = button(*breakpoints, "debugSetBreakpoint", tr("Set breakpoint"), [this] { breakpoint(true); });
        removeBreakpoint_ = button(*breakpoints, "debugRemoveBreakpoint", tr("Remove breakpoint"),
            [this] { breakpoint(false); });
        breakpointList_ = label("debugBreakpoints", {});
        breakpoints->addWidget(breakpointList_, 1);
        exception_ = new QCheckBox(tr("Stop on exception"));
        exception_->setObjectName("debugStopOnException");
        breakpoints->addWidget(exception_);
        connect(exception_, &QCheckBox::clicked, this, [this](bool enabled) { exceptionStop(enabled); });

        auto* actions = new QHBoxLayout;
        layout->addLayout(actions);
        stepInto_ = button(*actions, "debugStepInto", tr("Step into"), [this] { resume(SDP_STEP_INTO); });
        stepOver_ = button(*actions, "debugStepOver", tr("Step over"), [this] { resume(SDP_STEP_OVER); });
        stepOut_ = button(*actions, "debugStepOut", tr("Step out"), [this] { resume(SDP_STEP_OUT); });
        continue_ = button(*actions, "debugContinue", tr("Continue"), [this] { resume(0); });
        actions->addStretch();

        status_ = label("debugStatus", {});
        layout->addWidget(status_);
        lease_ = label("debugLease", {});
        layout->addWidget(lease_);
        auto* detail = new QSplitter(Qt::Vertical);
        layout->addWidget(detail, 1);
        auto* stackPanel = new QWidget;
        auto* stackLayout = new QVBoxLayout(stackPanel);
        stackLayout->setContentsMargins(0, 0, 0, 0);
        stackLayout->addWidget(label("debugStackLabel", tr("Call stack")));
        stackTable_ = table("debugStack", {tr("Routine"), tr("Revision"), tr("Line"), tr("Column")});
        stackLayout->addWidget(stackTable_);
        detail->addWidget(stackPanel);
        auto* localsPanel = new QWidget;
        auto* localsLayout = new QVBoxLayout(localsPanel);
        localsLayout->setContentsMargins(0, 0, 0, 0);
        localsLayout->addWidget(label("debugLocalsLabel", tr("Local variables")));
        locals_ = table("debugLocals", {tr("Name"), tr("Type"), tr("Value")});
        localsLayout->addWidget(locals_);
        localsNote_ = label("debugLocalsNote", {});
        localsLayout->addWidget(localsNote_);
        detail->addWidget(localsPanel);
        connect(stackTable_, &QTableWidget::currentCellChanged, this, [this](int row) { showLocals(row); });

        auto* watches = new QHBoxLayout;
        layout->addLayout(watches);
        watchName_ = new QLineEdit;
        watchName_->setObjectName("debugWatchName");
        watchName_->setPlaceholderText(tr("Local name in the active frame"));
        watchName_->setMaxLength(256);
        watches->addWidget(watchName_, 1);
        watch_ = button(*watches, "debugWatch", tr("Watch"), [this] { readWatch(); });
        watchResult_ = new QPlainTextEdit;
        watchResult_->setObjectName("debugWatchValue");
        watchResult_->setReadOnly(true);
        watchResult_->setMaximumHeight(65);
        layout->addWidget(watchResult_);
        connect(watchName_, &QLineEdit::textChanged, this, [this] { controls(); });
        connect(watchName_, &QLineEdit::returnPressed, watch_, &QPushButton::click);

        poll_.setInterval(pausePollingMs);
        connect(&poll_, &QTimer::timeout, this, [this] { poll(); });
        connect(&session, &QObject::destroyed, this, [this] {
            disconnectControl(tr("The target worksheet closed."));
        });
        report({}, available() ? tr("Attach before starting the routine in the worksheet.") :
            tr("Select a compiled routine with a current source revision on a server that supports debugging."));
        updateBreakpoints();
        controls();
    }

    void reject() override { detach(true); }

private:
    static QLabel* label(const char* name, const QString& text)
    {
        auto* result = new QLabel(text);
        result->setObjectName(name);
        result->setTextFormat(Qt::PlainText);
        result->setWordWrap(true);
        return result;
    }

    QPushButton* button(QHBoxLayout& layout, const char* name, const QString& caption, auto action)
    {
        auto* result = new QPushButton(caption);
        result->setObjectName(name);
        layout.addWidget(result);
        connect(result, &QPushButton::clicked, this, std::move(action));
        return result;
    }

    bool available() const
    {
        return target_ && target_->state().connected && revision_ && !routine_.isEmpty() &&
            target_->capabilities().value("plsql_debug_v1").toBool();
    }

    void controls()
    {
        if (!watch_) return;
        const bool ready = !opening_ && !detaching_ && control_ && !control_->busy() && !id_.isEmpty();
        const bool canDebug = ready && !terminal(state_);
        const bool paused = canDebug && state_ == u"paused";
        const bool credentials = available() && !control_ && !opening_ && !detaching_;
        password_->setEnabled(credentials);
        attach_->setEnabled(credentials && !password_->text().isEmpty());
        detach_->setEnabled((opening_ || control_) && !detaching_);
        line_->setEnabled(canDebug);
        setBreakpoint_->setEnabled(canDebug);
        removeBreakpoint_->setEnabled(canDebug);
        exception_->setEnabled(canDebug);
        stepInto_->setEnabled(paused);
        stepOver_->setEnabled(paused);
        stepOut_->setEnabled(paused);
        continue_->setEnabled(paused);
        watchName_->setEnabled(paused);
        watch_->setEnabled(paused && !watchName_->text().isEmpty());
    }

    void report(QString state, const QString& message)
    {
        state_ = std::move(state);
        setProperty("debugState", state_);
        status_->setProperty("debugState", state_);
        status_->setText(message);
    }

    bool sameRoutine(const QJsonObject& current) const
    {
        return current.value("id") == metadata_.value("id") && current.value("kind").toInt() == 6 &&
            current.value("qualified_name") == metadata_.value("qualified_name") &&
            current.value("schema_version").toInteger() == revision_;
    }

    void attach()
    {
        if (!available() || opening_ || control_) return;
        if (target_->busy())
        {
            report({}, tr("Wait for the target worksheet to finish its current request."));
            return;
        }
        auto secret = std::make_shared<Password>();
        secret->bytes = password_->text().toUtf8();
        if (secret->bytes.isEmpty()) return;
        password_->setText({});
        opening_ = true;
        const auto attempt = ++attempt_;
        access_->setText(tr("Verifying the target session and compiled source."));
        report({}, tr("Checking the target worksheet…"));
        controls();

        QPointer<RoutineDebugger> self(this);
        if (!target_->refreshIdentity([self, attempt, secret](const SqlReply& reply) {
            if (!self || attempt != self->attempt_) return;
            if (reply.error.code != SDP_OK) { self->disconnectControl(diagnostic(reply)); return; }
            const auto targetId = reply.data.value("session_id").toString();
            if (!self->target_ || targetId.isEmpty())
            {
                self->disconnectControl(tr("The server did not identify the target session."));
                return;
            }
            self->verifyRoutine(attempt, targetId, secret);
        })) disconnectControl(tr("The target worksheet could not identify its session."));
    }

    void verifyRoutine(quint64 attempt, const QString& targetId, const std::shared_ptr<Password>& secret)
    {
        QPointer<RoutineDebugger> self(this);
        if (!target_->refreshMetadata([self, attempt, targetId, secret](const SqlReply& reply) {
            if (!self || attempt != self->attempt_) return;
            if (reply.error.code != SDP_OK) { self->disconnectControl(diagnostic(reply)); return; }
            bool found = false;
            for (const auto& value : reply.data.value("objects").toArray())
            {
                if (self->sameRoutine(value.toObject())) { found = true; break; }
            }
            if (!found)
            {
                self->disconnectControl(tr("The compiled source revision changed. Refresh the routine before attaching."));
                return;
            }
            self->connectControl(attempt, targetId, secret);
        })) disconnectControl(tr("The target worksheet could not verify the source revision."));
    }

    void connectControl(quint64 attempt, const QString& targetId, std::shared_ptr<Password> secret)
    {
        report({}, tr("Authenticating the debugger connection…"));
        QPointer<RoutineDebugger> self(this);
        workers_.start([self, attempt, targetId, secret = std::move(secret), profile = target_->profile()] {
            auto result = wb::connect(profile, *secret, false, false);
            secret->bytes.fill('\0');
            QMetaObject::invokeMethod(QCoreApplication::instance(), [self, attempt, targetId, result = std::move(result)] {
                if (!self || attempt != self->attempt_) return;
                if (!result.session) { self->disconnectControl(result.error); return; }
                self->bindControl(targetId, result.session);
            }, Qt::QueuedConnection);
        });
    }

    void bindControl(const QString& targetId, const std::shared_ptr<Session>& session)
    {
        if (!available() || target_->busy() || target_->state().identity.value("session_id").toString() != targetId)
        {
            disconnectControl(tr("The target session changed or started a request. Attach before invoking the routine."));
            return;
        }

        targetId_ = targetId;
        control_ = new SqlSession(target_->profile(), session, workers_, this);
        QPointer<RoutineDebugger> self(this);
        const auto active = control_;
        if (!active->debugOpen(targetId_, routine_, revision_, [self, active](const SqlReply& reply) {
            if (!self || self->control_ != active) return;
            self->opening_ = false;
            if (reply.error.code != SDP_OK)
            {
                if (reply.error.code == SDP_PERMISSION_DENIED) self->denied();
                else self->disconnectControl(diagnostic(reply));
                return;
            }
            self->id_ = reply.data.value("debug_id").toString().toUtf8();
            if (self->id_.isEmpty())
            {
                self->disconnectControl(tr("The server did not return a debugger identifier."));
                return;
            }
            if (self->detaching_) { self->closeBinding(); return; }
            self->access_->setText(tr("The server authorized this control connection."));
            self->lease_->clear();
            self->report(QStringLiteral("armed"), tr("Armed. Set breakpoints, then invoke this routine in the target worksheet."));
            self->poll_.start();
            self->controls();
        })) disconnectControl(tr("The debugger could not attach."));
    }

    void denied()
    {
        disconnectControl(tr("The server denied debugger access. The call stack, locals and watches were cleared."));
        if (!closeWindow_) access_->setText(tr("The server requires debug permission and control ownership."));
    }


    bool request(SqlSession::NativeRequest operation, SqlSession::Finished finished)
    {
        if (!control_ || control_->busy() || detaching_) return false;
        QPointer<RoutineDebugger> self(this);
        const auto active = control_;
        const auto started = active->request(std::move(operation),
            [self, active, finished = std::move(finished)](const SqlReply& reply) {
                if (!self || self->control_ != active) return;
                if (self->detaching_) { self->closeBinding(); return; }
                if (reply.error.code == SDP_PERMISSION_DENIED) { self->denied(); return; }
                if (!self->control_->state().connected)
                {
                    self->disconnectControl(diagnostic(reply));
                    return;
                }
                finished(reply);
                if (self) self->controls();
            }, 2400);
        controls();
        return started;
    }

    void poll()
    {
        if (!target_ || !target_->state().connected ||
            target_->state().identity.value("session_id").toString() != targetId_)
        {
            disconnectControl(tr("The target connection changed. Reattach to debug its current session."));
            return;
        }
        const auto id = id_;
        request([id](auto handle, auto* output, auto* error) { return sdp_debug_status(handle, wire(id), output, error); },
            [this](const SqlReply& reply) {
                if (reply.error.code != SDP_OK)
                {
                    disconnectControl(diagnostic(reply));
                    return;
                }
                displayStatus(reply.data);
            });
    }

    void displayStatus(const QJsonObject& snapshot)
    {
        const auto state = snapshot.value("state").toString();
        if (snapshot.value("debug_id").toString().toUtf8() != id_ ||
            snapshot.value("revision").toInteger() != revision_ ||
            snapshot.value("routine").toArray() != metadata_.value("qualified_name").toArray() ||
            (!terminal(state) && state != u"armed" && state != u"running" && state != u"paused"))
        {
            disconnectControl(tr("The server returned a different debugger or source revision."));
            return;
        }

        if (state == u"paused")
        {
            const auto reason = snapshot.value("reason").toString();
            const auto detail = reason == u"exception" ? tr("Exception") :
                reason == u"breakpoint" ? tr("Breakpoint") : reason == u"step" ? tr("Step") : tr("Paused");
            report(state, tr("%1 at line %2, column %3.").arg(detail).arg(snapshot.value("line").toInteger())
                .arg(snapshot.value("column").toInteger()));
            showStack(snapshot.value("stack").toArray());
            refreshLease();
            return;
        }

        showStack({});
        watchResult_->clear();
        lease_->clear();
        if (state == u"armed") report(state, tr("Armed. Set breakpoints, then invoke this routine in the target worksheet."));
        else if (state == u"running") report(state, tr("The target routine is running."));
        else
        {
            poll_.stop();
            if (state == u"finished") report(state, tr("The routine finished. Detach before starting another debug run."));
            if (state == u"cancelled") report(state, tr("The debug run was cancelled. Detach before trying again."));
            if (state == u"timed_out") report(state, tr("The server timed out the debug run. Detach before trying again."));
        }
    }

    void refreshLease()
    {
        const auto paused = tr("The paused routine holds its transaction lease. A server pause may last up to 3 seconds.");
        if (monitorDenied_)
        {
            lease_->setText(paused + u' ' + tr("The server requires monitor permission for session details."));
            return;
        }
        if (lease_->text().isEmpty()) lease_->setText(paused);
        if (lastMonitor_.isValid() && lastMonitor_.elapsed() < leasePollingMs) return;

        // Monitoring has its own grant. Debug permission failures are handled separately.
        QPointer<RoutineDebugger> self(this);
        const auto active = control_;
        if (active->request(sdp_session_list, [self, active, paused](const SqlReply& reply) {
            if (!self || self->control_ != active) return;
            if (self->detaching_) { self->closeBinding(); return; }
            if (!self->control_->state().connected)
            {
                self->disconnectControl(diagnostic(reply));
                return;
            }
            if (reply.error.code == SDP_PERMISSION_DENIED)
            {
                self->monitorDenied_ = true;
                self->lease_->setText(paused + u' ' + tr("The server requires monitor permission for session details."));
            }
            else if (reply.error.code == SDP_OK)
            {
                bool found = false;
                for (const auto& entry : reply.data.value("sessions").toArray())
                {
                    const auto info = entry.toObject();
                    if (info.value("session_id").toString() != self->targetId_) continue;
                    found = true;
                    const auto transaction = info.value("transaction_active").toBool() ?
                        tr("active") : tr("no explicit transaction");
                    const auto request = info.value("request_id").isDouble()
                        ? QString::number(info.value("request_id").toInteger()) : tr("none");
                    self->lease_->setText(paused + u' ' + tr("Transaction: %1. Request: %2. Lock wait: %3 ms.")
                        .arg(transaction, request).arg(info.value("lock_wait_ms").toInteger()));
                    break;
                }
                if (!found) self->lease_->setText(paused + u' ' + tr("The server did not report the target session."));
            }
            else self->lease_->setText(paused + u' ' + diagnostic(reply));
            self->controls();
        }, 1000)) lastMonitor_.start();
    }

    void showStack(QJsonArray stack)
    {
        if (stack.size() > 32) stack = {};
        if (stack == stack_) return;
        stack_ = std::move(stack);
        const QSignalBlocker block(stackTable_);
        stackTable_->setRowCount(static_cast<int>(stack_.size()));
        for (int row = 0; row < stack_.size(); ++row)
        {
            const auto frame = stack_.at(row).toObject();
            fillRow(*stackTable_, row, {
                joinedName(frame.value("routine").toArray(), false),
                QString::number(frame.value("revision").toInteger()),
                QString::number(frame.value("line").toInteger()),
                QString::number(frame.value("column").toInteger())
            });
        }
        const auto current = static_cast<int>(stack_.size()) - 1;
        if (current >= 0) stackTable_->selectRow(current);
        showLocals(current);
    }

    void showLocals(int row)
    {
        locals_->setRowCount(0);
        localsNote_->clear();
        if (row < 0 || row >= stack_.size()) return;
        const auto frame = stack_.at(row).toObject();
        const auto locals = frame.value("locals").toArray();
        if (locals.size() > 128) return;
        locals_->setRowCount(static_cast<int>(locals.size()));
        for (int index = 0; index < locals.size(); ++index)
        {
            const auto local = locals.at(index).toObject();
            fillRow(*locals_, index, {local.value("name").toString(), local.value("type").toString(),
                local.value("available").toBool() ? valueText(local.value("value").toObject()) : tr("Value unavailable")});
        }
        if (frame.value("locals_truncated").toBool())
            localsNote_->setText(tr("The server limited the values visible in this frame."));
    }

    void breakpoint(bool enabled)
    {
        const auto id = id_;
        const auto revision = revision_;
        const auto line = static_cast<uint32_t>(line_->value());
        request([id, revision, line, enabled](auto handle, auto*, auto* error) {
            return sdp_debug_breakpoint(handle, wire(id), line, revision, enabled ? 1u : 0u, error);
        }, [this, enabled, line](const SqlReply& reply) {
            if (reply.error.code != SDP_OK) { report(state_, diagnostic(reply)); return; }
            if (enabled) breakpoints_.insert(line);
            else breakpoints_.remove(line);
            updateBreakpoints();
        });
    }

    void updateBreakpoints()
    {
        auto ordered = breakpoints_.values();
        std::sort(ordered.begin(), ordered.end());
        QVariantList announced;
        QStringList labels;
        for (const auto line : ordered)
        {
            announced.append(line);
            labels.append(QString::number(line));
        }
        setProperty("debugBreakpointLines", announced);
        breakpointList_->setText(labels.isEmpty() ? tr("No breakpoints") : labels.join(QStringLiteral(", ")));
    }

    void exceptionStop(bool enabled)
    {
        const auto id = id_;
        if (request([id, enabled](auto handle, auto*, auto* error) {
            return sdp_debug_options(handle, wire(id), enabled ? 1u : 0u, error);
        }, [this, enabled](const SqlReply& reply) {
            if (reply.error.code == SDP_OK) exceptionStop_ = enabled;
            else report(state_, diagnostic(reply));
            exception_->setChecked(exceptionStop_);
        })) return;
        exception_->setChecked(exceptionStop_);
    }

    void resume(sdp_debug_step_mode mode)
    {
        const auto id = id_;
        request([id, mode](auto handle, auto*, auto* error) {
            return mode ? sdp_debug_step(handle, wire(id), mode, error) : sdp_debug_continue(handle, wire(id), error);
        }, [this](const SqlReply& reply) {
            if (reply.error.code != SDP_OK) { report(state_, diagnostic(reply)); return; }
            showStack({});
            watchResult_->clear();
            lease_->clear();
            report(QStringLiteral("running"), tr("The target routine is running."));
        });
    }

    void readWatch()
    {
        const auto name = watchName_->text().toUtf8();
        if (name.isEmpty() || name.size() > 256 || name.contains('\0')) return;
        const auto id = id_;
        request([id, name](auto handle, auto* output, auto* error) {
            return sdp_debug_watch(handle, wire(id), wire(name), output, error);
        }, [this](const SqlReply& reply) {
            if (reply.error.code != SDP_OK) { watchResult_->setPlainText(diagnostic(reply)); return; }
            watchResult_->setPlainText(tr("%1 (%2) = %3").arg(reply.data.value("name").toString(),
                reply.data.value("type").toString(), valueText(reply.data.value("value").toObject())));
        });
    }


    void detach(bool closeWindow)
    {
        closeWindow_ = closeWindow_ || closeWindow;
        if (detaching_) return;
        detaching_ = true;
        poll_.stop();
        showStack({});
        watchResult_->clear();
        lease_->clear();
        report({}, tr("Detaching the debugger…"));
        controls();
        closeBinding();
    }

    void closeBinding()
    {
        if (control_ && control_->busy()) return;
        if (!control_ || id_.isEmpty() || !control_->state().connected)
        {
            disconnectControl(tr("Debugger detached."));
            return;
        }

        QPointer<RoutineDebugger> self(this);
        const auto active = control_;
        const auto id = id_;
        if (!active->request([id](auto handle, auto*, auto* error) { return sdp_debug_close(handle, wire(id), error); },
            [self, active](const SqlReply& reply) {
                if (!self || self->control_ != active) return;
                self->disconnectControl(reply.error.code == SDP_OK ?
                    tr("Debugger detached.") : diagnostic(reply));
            }, 2400))
            disconnectControl(tr("The debugger control connection closed."));
    }

    void disconnectControl(const QString& message)
    {
        ++attempt_;
        opening_ = false;
        detaching_ = false;
        poll_.stop();
        if (control_)
        {
            control_->deleteLater();
            control_ = nullptr;
        }
        targetId_.clear();
        id_.clear();
        lastMonitor_.invalidate();
        monitorDenied_ = false;
        breakpoints_.clear();
        updateBreakpoints();
        exceptionStop_ = false;
        exception_->setChecked(false);
        showStack({});
        watchResult_->clear();
        watchName_->clear();
        password_->clear();
        lease_->clear();
        access_->setText(tr("The debugger control connection is closed."));
        report({}, message);
        controls();
        if (closeWindow_) QDialog::reject();
    }

    QPointer<SqlSession> target_;
    QThreadPool& workers_;
    const QJsonObject metadata_;
    const QString routine_;
    quint32 revision_{};
    SqlSession* control_{};
    QString targetId_;
    QByteArray id_;
    QString state_;
    QTimer poll_;
    QElapsedTimer lastMonitor_;
    QJsonArray stack_;
    QSet<quint32> breakpoints_;
    quint64 attempt_{};
    bool opening_{};
    bool detaching_{};
    bool closeWindow_{};
    bool monitorDenied_{};
    bool exceptionStop_{};

    QLineEdit* password_{};
    QLineEdit* watchName_{};
    QSpinBox* line_{};
    QLabel* access_{};
    QLabel* status_{};
    QLabel* lease_{};
    QLabel* localsNote_{};
    QLabel* breakpointList_{};
    QPushButton* attach_{};
    QPushButton* detach_{};
    QPushButton* setBreakpoint_{};
    QPushButton* removeBreakpoint_{};
    QPushButton* stepInto_{};
    QPushButton* stepOver_{};
    QPushButton* stepOut_{};
    QPushButton* continue_{};
    QPushButton* watch_{};
    QCheckBox* exception_{};
    QTableWidget* stackTable_{};
    QTableWidget* locals_{};
    QPlainTextEdit* watchResult_{};
};
}

QDialog* showRoutineDebugger(SqlSession& target, QThreadPool& workers,
    const QJsonObject& routineMetadata, QWidget* parent)
{
    auto* dialog = new RoutineDebugger(target, workers, routineMetadata, parent);
    dialog->show();
    return dialog;
}
}

