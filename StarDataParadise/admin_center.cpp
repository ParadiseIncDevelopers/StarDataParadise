#include "admin_center.hpp"

#include "sql_session.hpp"

#include <windows.h>
#include <winsvc.h>

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTabWidget>
#include <QTimeZone>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>

namespace wb
{
namespace
{
const QStringList checks{"storage", "history", "backup", "certificate", "jobs"};
const QStringList categories{"query", "audit", "recovery", "history", "operational", "ml", "decision", "error"};
const QStringList events{
    "administrator_created", "authentication_passed", "authentication_failed", "listening", "stopped",
    "session_open", "session_closed", "session_timeout", "session_error", "accept_failed", "tls_handshake_failed",
    "protocol_rejected", "request", "request_completed", "request_rejected", "request_failed", "query_prepare",
    "query_execute", "query_completed", "query_failed", "query_cancelled", "transaction_committed",
    "transaction_rolled_back", "recovery_completed", "recovery_failed", "history_verified", "history_applied",
    "backup_verified", "backup_failed", "job_submitted", "job_completed", "job_failed", "model_trained",
    "model_activated", "decision_allowed", "decision_denied", "decision_evaluated", "health_checked",
    "config_changed", "support_created", "redacted_event"};
constexpr qint64 maxSupportBytes = 1024 * 1024;

QString json(const QJsonObject& object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
}

QString valueText(const QJsonValue& value)
{
    if (value.isUndefined() || value.isNull()) return QStringLiteral("—");
    if (value.isString()) return value.toString();
    const auto packed = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return packed.size() > 2 ? QString::fromUtf8(packed.sliced(1, packed.size() - 2)) : QString{};
}

QString utc(const QJsonValue& value)
{
    if (!value.isDouble()) return QStringLiteral("—");
    return QDateTime::fromSecsSinceEpoch(value.toInteger(), QTimeZone::UTC).toString(Qt::ISODate);
}

QString adminOp(sdp_admin_operation operation)
{
    switch (operation)
    {
    case SDP_ADMIN_STATUS: return QStringLiteral("SDP_ADMIN_STATUS");
    case SDP_ADMIN_HEALTH: return QStringLiteral("SDP_ADMIN_HEALTH");
    case SDP_ADMIN_HEALTH_CHECK: return QStringLiteral("SDP_ADMIN_HEALTH_CHECK");
    case SDP_ADMIN_CONFIG_PREVIEW: return QStringLiteral("SDP_ADMIN_CONFIG_PREVIEW");
    case SDP_ADMIN_CONFIG_APPLY: return QStringLiteral("SDP_ADMIN_CONFIG_APPLY");
    case SDP_ADMIN_SUPPORT_PREVIEW: return QStringLiteral("SDP_ADMIN_SUPPORT_PREVIEW");
    case SDP_ADMIN_SUPPORT_CREATE: return QStringLiteral("SDP_ADMIN_SUPPORT_CREATE");
    case SDP_ADMIN_MAINTENANCE_SUBMIT: return QStringLiteral("SDP_ADMIN_MAINTENANCE_SUBMIT");
    }
    return {};
}

QString sqlError(const SqlReply& reply)
{
    const auto detail = QString::fromUtf8(reply.error.message);
    return detail.isEmpty() ? wb::status(reply.error.code) :
        QObject::tr("%1 · %2").arg(wb::status(reply.error.code), detail);
}

QPlainTextEdit* viewer(const char* name)
{
    auto* widget = new QPlainTextEdit;
    widget->setObjectName(name);
    widget->setReadOnly(true);
    return widget;
}

QLineEdit* input(const char* name, const QString& placeholder = {})
{
    auto* widget = new QLineEdit;
    widget->setObjectName(name);
    widget->setPlaceholderText(placeholder);
    return widget;
}

bool safeStem(const QString& value, int limit)
{
    static const QRegularExpression name(QStringLiteral("\\A[a-z0-9_-]+\\z"));
    return value.size() <= limit && name.match(value).hasMatch();
}

bool boundedNumber(const QJsonValue& value, qint64 least, qint64 greatest)
{
    const auto number = value.toInteger(least - 1);
    return value.isDouble() && number >= least && number <= greatest;
}

bool numericPatch(QJsonObject& patch, const char* name, const QLineEdit* field, int least, int greatest)
{
    const auto text = field->text().trimmed();
    if (text.isEmpty()) return true;
    bool ok{};
    const auto number = text.toInt(&ok, 10);
    if (!ok || number < least || number > greatest) return false;
    patch.insert(QLatin1StringView(name), number);
    return true;
}

QJsonObject redactedSummary(const QJsonObject& source)
{
    if (source.value("format").toInt() != 1 || source.value("secret_scan_passed") != QJsonValue(true)) return {};
    const auto config = source.value("config").toObject();
    const auto logs = source.value("log_counts").toObject();
    const auto instance = config.value("instance_name").toString();
    const QHostAddress bind(config.value("bind").toString());
    if (!safeStem(instance, 48) || bind.isNull() || !bind.scopeId().isEmpty()) return {};
    QJsonObject safeConfig{{"instance_name", instance}, {"bind", bind.toString()}};
    for (const auto name : {"port", "max_connections", "timeout_ms"})
    {
        const auto key = QLatin1StringView(name);
        const auto value = config.value(key);
        if (!boundedNumber(value, 0, 120000)) return {};
        safeConfig.insert(key, value);
    }
    for (const auto name : {"decision_enabled", "mlp_cpu_enabled", "backup_enabled"})
    {
        const auto key = QLatin1StringView(name);
        const auto value = config.value(key);
        if (!value.isBool()) return {};
        safeConfig.insert(key, value);
    }
    QJsonObject safeLogs;
    for (const auto& category : categories)
    {
        const auto count = logs.value(category);
        if (!boundedNumber(count, 0, 64)) return {};
        safeLogs.insert(category, count);
    }
    const auto discarded = source.value("discarded_records");
    if (!boundedNumber(discarded, 0, maxSupportBytes)) return {};
    QJsonObject summary{{"format", 1}, {"config", safeConfig}, {"log_counts", safeLogs},
        {"discarded_records", discarded}, {"secret_scan_passed", true}};
    const auto target = source.value("target_name");
    if (!target.isUndefined())
    {
        if (!safeStem(target.toString(), 32)) return {};
        summary.insert("target_name", target);
    }
    return summary;
}

QString localWindowsState(DWORD state)
{
    switch (state)
    {
    case SERVICE_STOPPED: return QObject::tr("Stopped");
    case SERVICE_START_PENDING: return QObject::tr("Starting");
    case SERVICE_STOP_PENDING: return QObject::tr("Stopping");
    case SERVICE_RUNNING: return QObject::tr("Running");
    case SERVICE_CONTINUE_PENDING: return QObject::tr("Continuing");
    case SERVICE_PAUSE_PENDING: return QObject::tr("Pausing");
    case SERVICE_PAUSED: return QObject::tr("Paused");
    }
    return QObject::tr("Unknown");
}

QString localWindowsError(DWORD code)
{
    if (code == ERROR_ACCESS_DENIED) return QObject::tr("Windows denied access with the current account.");
    if (code == ERROR_SERVICE_DOES_NOT_EXIST) return QObject::tr("The named service is absent on this computer.");
    return QObject::tr("Windows SCM returned error %1.").arg(code);
}

class AdminCenter : public QDialog
{
public:
    AdminCenter(SqlSession&, QWidget*, std::function<void(const QString&)>);
    void reject() override { if (!busy_) QDialog::reject(); }

private:
    using Render = std::function<void(const QJsonObject&)>;
    QPushButton* action(QLayout*, const char*, const QString&, std::function<void()>);
    QWidget* tab(const QString&, QVBoxLayout*&);
    bool invoke(sdp_admin_operation, QJsonObject, const QString& progress, Render,
        bool writing = false, bool engineLease = false);
    void work(bool);
    void status(QString text) { status_->setText(std::move(text)); }
    void loadStatus();
    void loadHealth();
    void runCheck();
    void renderHealth();
    void inspectService();
    void previewConfig();
    void applyConfig();
    void invalidateConfig();
    void previewSupport();
    void createSupport();
    void exportSupport();
    void openLogs();
    void maintenance();

    SqlSession& session_;
    std::function<void(const QString&)> openJob_;
    QTabWidget* tabs_{};
    QLabel* status_{};
    QPlainTextEdit* statusPayload_{};
    QTreeWidget* metrics_{};
    QTreeWidget* sessions_{};
    QTreeWidget* healthChecks_{};
    QPlainTextEdit* healthPayload_{};
    QComboBox* checkName_{};
    QLineEdit* localInstance_{};
    QPlainTextEdit* localStatus_{};
    QLineEdit* timeout_{};
    QLineEdit* port_{};
    QLineEdit* connections_{};
    QPushButton* configApply_{};
    QPlainTextEdit* configPayload_{};
    QLineEdit* supportTarget_{};
    QLineEdit* supportPath_{};
    QPushButton* supportCreate_{};
    QPushButton* supportExport_{};
    QPlainTextEdit* supportPayload_{};
    QLineEdit* logsPath_{};
    QTreeWidget* logs_{};
    QComboBox* maintenanceKind_{};
    QPlainTextEdit* maintenancePayload_{};
    QPushButton* showJob_{};
    QPlainTextEdit* request_{};
    QJsonObject health_;
    QJsonObject reviewedConfig_;
    QJsonObject support_;
    QJsonObject maintenanceRequest_;
    QString jobId_;
    bool busy_{};
};
AdminCenter::AdminCenter(SqlSession& session, QWidget* parent, std::function<void(const QString&)> openJob)
    : QDialog(parent), session_(session), openJob_(std::move(openJob))
{
    setObjectName("adminCenter");
    setWindowTitle(tr("Admin Center · %1").arg(endpoint(session.profile())));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(1080, 720);
    auto* layout = new QVBoxLayout(this);
    tabs_ = new QTabWidget;
    tabs_->setObjectName("adminTabs");
    layout->addWidget(tabs_, 1);

    QVBoxLayout* view{};
    tab(tr("Instance"), view);
    auto* statusActions = new QHBoxLayout;
    action(statusActions, "adminLoadStatus", tr("Refresh server status"), [this] { loadStatus(); });
    statusActions->addStretch();
    view->addLayout(statusActions);
    metrics_ = new QTreeWidget;
    metrics_->setObjectName("adminMetrics");
    metrics_->setHeaderLabels({tr("Instance measurement"), tr("Value")});
    metrics_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    view->addWidget(metrics_, 1);
    sessions_ = new QTreeWidget;
    sessions_->setObjectName("adminSessions");
    sessions_->setHeaderLabels({tr("Session"), tr("Actor"), tr("Transaction"), tr("Request"), tr("Operation"),
        tr("Running ms"), tr("Lock wait ms")});
    sessions_->setRootIsDecorated(false);
    view->addWidget(sessions_, 1);
    statusPayload_ = viewer("adminStatusPayload");
    view->addWidget(statusPayload_, 1);

    tab(tr("Health"), view);
    auto* healthActions = new QHBoxLayout;
    action(healthActions, "adminLoadHealth", tr("Read last checks"), [this] { loadHealth(); });
    checkName_ = new QComboBox;
    checkName_->setObjectName("adminCheckName");
    for (const auto& name : checks) checkName_->addItem(name, name);
    healthActions->addWidget(checkName_);
    action(healthActions, "adminRunCheck", tr("Run selected check"), [this] { runCheck(); });
    healthActions->addStretch();
    view->addLayout(healthActions);
    healthChecks_ = new QTreeWidget;
    healthChecks_->setObjectName("adminHealthChecks");
    healthChecks_->setHeaderLabels({tr("Check"), tr("Status"), tr("Checked at UTC"), tr("Evidence"), tr("Remediation")});
    healthChecks_->setRootIsDecorated(false);
    healthChecks_->header()->setSectionResizeMode(4, QHeaderView::Stretch);
    view->addWidget(healthChecks_, 2);
    healthPayload_ = viewer("adminHealthPayload");
    view->addWidget(healthPayload_, 1);

    tab(tr("Server configuration"), view);
    auto* config = new QFormLayout;
    timeout_ = input("adminTimeout", tr("100–120000"));
    port_ = input("adminPort", tr("0–65535"));
    connections_ = input("adminMaxConnections", tr("1–128"));
    config->addRow(tr("Timeout in milliseconds"), timeout_);
    config->addRow(tr("Listen port"), port_);
    config->addRow(tr("Maximum connections"), connections_);
    view->addLayout(config);
    auto* configActions = new QHBoxLayout;
    action(configActions, "adminConfigPreview", tr("Preview settings"), [this] { previewConfig(); });
    configApply_ = action(configActions, "adminConfigApply", tr("Apply reviewed settings"), [this] { applyConfig(); });
    configActions->addStretch();
    view->addLayout(configActions);
    configPayload_ = viewer("adminConfigPayload");
    view->addWidget(configPayload_, 1);
    for (auto* field : {timeout_, port_, connections_})
        connect(field, &QLineEdit::textChanged, this, [this] { invalidateConfig(); });

    tab(tr("Local Windows service"), view);
    auto* localBoundary = new QLabel(tr("This panel queries Windows SCM on this computer with the current Windows account. "
        "Server administration uses the authenticated SQL connection."));
    localBoundary->setTextFormat(Qt::PlainText);
    localBoundary->setWordWrap(true);
    view->addWidget(localBoundary);
    auto* localActions = new QHBoxLayout;
    localInstance_ = input("adminLocalInstance", tr("Local instance name"));
    localActions->addWidget(localInstance_, 1);
    action(localActions, "adminInspectLocalService", tr("Query local service"), [this] { inspectService(); });
    view->addLayout(localActions);
    localStatus_ = viewer("adminLocalServiceStatus");
    view->addWidget(localStatus_, 1);

    tab(tr("Support and logs"), view);
    auto* support = new QFormLayout;
    auto* remote = new QHBoxLayout;
    supportTarget_ = input("adminSupportTarget", tr("Remote bundle name"));
    remote->addWidget(supportTarget_, 1);
    action(remote, "adminSupportPreview", tr("Preview redacted summary"), [this] { previewSupport(); });
    supportCreate_ = action(remote, "adminSupportCreate", tr("Create bundle on server"), [this] { createSupport(); });
    support->addRow(tr("Server bundle"), remote);
    auto* exportActions = new QHBoxLayout;
    supportPath_ = input("adminSupportExportPath", tr("Local JSON destination"));
    exportActions->addWidget(supportPath_, 1);
    action(exportActions, "adminChooseSupportExportPath", tr("Choose file…"), [this] {
        const auto path = QFileDialog::getSaveFileName(this, tr("Export redacted summary"),
            supportPath_->text().isEmpty() ? QStringLiteral("support-summary.json") : supportPath_->text(),
            tr("JSON (*.json)"), nullptr, QFileDialog::DontConfirmOverwrite);
        if (!path.isEmpty()) supportPath_->setText(path);
    });
    supportExport_ = action(exportActions, "adminSupportExport", tr("Save summary locally"), [this] { exportSupport(); });
    support->addRow(tr("Local summary export"), exportActions);
    auto* logActions = new QHBoxLayout;
    logsPath_ = input("adminLogPath", tr("Local copy of a redacted bundle.json"));
    logActions->addWidget(logsPath_, 1);
    action(logActions, "adminChooseLogPath", tr("Choose file…"), [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("Open a redacted server bundle"),
            logsPath_->text(), tr("JSON (*.json)"));
        if (!path.isEmpty()) logsPath_->setText(path);
    });
    action(logActions, "adminLoadLogs", tr("Open redacted logs"), [this] { openLogs(); });
    support->addRow(tr("Local log viewer"), logActions);
    view->addLayout(support);
    supportPayload_ = viewer("adminSupportPayload");
    view->addWidget(supportPayload_, 1);
    logs_ = new QTreeWidget;
    logs_->setObjectName("adminLogs");
    logs_->setHeaderLabels({tr("Category"), tr("Time UTC"), tr("Event"), tr("Code"), tr("Duration ms"), tr("Bundle correlation")});
    logs_->setRootIsDecorated(false);
    view->addWidget(logs_, 1);

    tab(tr("Maintenance"), view);
    auto* maintenanceActions = new QHBoxLayout;
    maintenanceKind_ = new QComboBox;
    maintenanceKind_->setObjectName("adminMaintenanceAction");
    maintenanceKind_->addItem(tr("Integrity"), QStringLiteral("integrity"));
    maintenanceKind_->addItem(tr("Statistics"), QStringLiteral("statistics"));
    maintenanceKind_->addItem(tr("Index maintenance"), QStringLiteral("index"));
    maintenanceActions->addWidget(maintenanceKind_);
    action(maintenanceActions, "adminMaintenanceSubmit", tr("Submit Server job"), [this] { maintenance(); });
    showJob_ = action(maintenanceActions, "adminOpenJob", tr("Open job"), [this] {
        if (openJob_ && !jobId_.isEmpty()) openJob_(jobId_);
    });
    maintenanceActions->addStretch();
    view->addLayout(maintenanceActions);
    maintenancePayload_ = viewer("adminMaintenancePayload");
    view->addWidget(maintenancePayload_, 1);

    tab(tr("Most recent API request"), view);
    request_ = viewer("adminRequest");
    view->addWidget(request_, 1);
    status_ = new QLabel;
    status_->setObjectName("adminStatusText");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto* footer = new QHBoxLayout;
    action(footer, "adminCopyRequest", tr("Copy API request"), [this] {
        if (!request_->toPlainText().isEmpty()) QApplication::clipboard()->setText(request_->toPlainText());
    });
    footer->addStretch();
    action(footer, "adminClose", tr("Close"), [this] { reject(); });
    layout->addLayout(footer);
    work(false);
}

QPushButton* AdminCenter::action(QLayout* layout, const char* name, const QString& label, std::function<void()> callback)
{
    auto* button = new QPushButton(label);
    button->setObjectName(name);
    layout->addWidget(button);
    connect(button, &QPushButton::clicked, this, std::move(callback));
    return button;
}

QWidget* AdminCenter::tab(const QString& title, QVBoxLayout*& layout)
{
    auto* widget = new QWidget;
    layout = new QVBoxLayout(widget);
    tabs_->addTab(widget, title);
    return widget;
}

void AdminCenter::work(bool busy)
{
    busy_ = busy;
    for (auto* field : {timeout_, port_, connections_, supportTarget_})
        field->setReadOnly(busy);
    configApply_->setEnabled(!busy && !reviewedConfig_.isEmpty() && !session_.profile().read_only);
    supportCreate_->setEnabled(!busy && !support_.isEmpty() && !session_.profile().read_only);
    supportExport_->setEnabled(!busy && !support_.isEmpty());
    showJob_->setEnabled(!jobId_.isEmpty() && bool(openJob_));
}

bool AdminCenter::invoke(sdp_admin_operation operation, QJsonObject body, const QString& progress, Render render,
    bool writing, bool engineLease)
{
    if (busy_) return false;
    if (!session_.state().connected)
    {
        status(tr("Reconnect to the server to use its administration API."));
        return false;
    }
    if (!session_.capabilities().value("admin_v1").toBool())
    {
        status(tr("The connected server does not expose the administration module."));
        return false;
    }
    if (session_.busy() || (engineLease && session_.state().transactionActive))
    {
        status(tr("Finish the worksheet operation or transaction before starting this request."));
        return false;
    }
    if (writing && session_.profile().read_only)
    {
        status(tr("This profile permits read-only administration."));
        return false;
    }
    work(true);
    status(progress);
    const auto bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
    const QPointer<AdminCenter> self(this);
    if (session_.request([operation, bytes](auto connection, auto* result, auto* error) {
        return sdp_admin_command(connection, operation, {bytes.constData(), static_cast<uint32_t>(bytes.size())}, result, error);
    }, [self, operation, render = std::move(render)](const SqlReply& reply) {
        if (!self) return;
        if (reply.error.code != SDP_OK)
        {
            if (operation == SDP_ADMIN_CONFIG_APPLY) self->reviewedConfig_ = {};
            if (operation == SDP_ADMIN_SUPPORT_CREATE && reply.error.unknown_outcome) self->support_ = {};
            if (operation == SDP_ADMIN_MAINTENANCE_SUBMIT && !reply.error.unknown_outcome) self->maintenanceRequest_ = {};
            if (operation == SDP_ADMIN_HEALTH && reply.error.code == SDP_PERMISSION_DENIED)
            {
                self->health_ = {};
                self->renderHealth();
            }
            self->status(reply.error.unknown_outcome
                ? QObject::tr("The server outcome is unknown. Verify the current state before repeating the request. %1")
                    .arg(sqlError(reply))
                : sqlError(reply));
            self->work(false);
            return;
        }
        self->work(false);
        render(reply.data);
    }, 30000))
    {
        request_->setPlainText(QStringLiteral("sdp_admin_command · %1\n%2").arg(adminOp(operation), json(body)));
        return true;
    }
    work(false);
    status(tr("The server administration request could not start."));
    return false;
}
void AdminCenter::loadStatus()
{
    invoke(SDP_ADMIN_STATUS, {}, tr("Reading live server measurements…"), [this](const QJsonObject& result) {
        statusPayload_->setPlainText(json(result));
        metrics_->clear();
        sessions_->clear();
        for (const auto& entry : {
                std::pair{"instance_name", tr("Server instance")}, std::pair{"instance_id", tr("Instance ID")},
                std::pair{"port", tr("Listening port")}, std::pair{"active_transactions", tr("Active transactions")},
                std::pair{"waiting_sessions", tr("Sessions waiting for locks")},
                std::pair{"slow_queries", tr("Slow queries in live sessions (≥250 ms)")}})
            new QTreeWidgetItem(metrics_, {entry.second, valueText(result.value(QLatin1StringView(entry.first)))});
        for (const auto& group : {std::pair{"disk", tr("Disk")}, std::pair{"limits", tr("Server limits")}})
        {
            auto* node = new QTreeWidgetItem(metrics_, {group.second});
            const auto values = result.value(QLatin1StringView(group.first)).toObject();
            for (auto field = values.constBegin(); field != values.constEnd(); ++field)
                new QTreeWidgetItem(node, {field.key(), valueText(field.value())});
            metrics_->expandItem(node);
        }
        new QTreeWidgetItem(metrics_, {tr("Deadlock graph"),
            result.value("deadlock_graph_available").toBool() ? tr("Available in this server") : tr("Unavailable")});
        new QTreeWidgetItem(metrics_, {tr("Memory and connection pool"), tr("No server measurements reported")});
        for (const auto entry : result.value("sessions").toArray())
        {
            const auto session = entry.toObject();
            new QTreeWidgetItem(sessions_, {valueText(session.value("session_id")), valueText(session.value("actor")),
                session.value("transaction_active").toBool() ? tr("Active") : tr("Inactive"), valueText(session.value("request_id")),
                valueText(session.value("operation")), valueText(session.value("running_ms")), valueText(session.value("lock_wait_ms"))});
        }
        metrics_->resizeColumnToContents(0);
        sessions_->resizeColumnToContents(0);
        status(tr("Live server status loaded."));
    });
}

void AdminCenter::loadHealth()
{
    invoke(SDP_ADMIN_HEALTH, {}, tr("Reading the server’s recorded evidence…"), [this](const QJsonObject& result) {
        health_ = result;
        renderHealth();
        status(tr("Server health evidence loaded."));
    });
}

void AdminCenter::runCheck()
{
    const auto name = checkName_->currentData().toString();
    if (!checks.contains(name)) return;
    invoke(SDP_ADMIN_HEALTH_CHECK, {{"check", name}}, tr("Measuring %1 on the server…").arg(name),
        [this, name](const QJsonObject& result) {
            if (result.value("check").toString() != name || !result.value("result").isObject())
            {
                status(tr("The server returned an inconsistent health result."));
                return;
            }
            if (health_.isEmpty())
            {
                loadHealth();
                return;
            }
            auto records = health_.value("checks").toObject();
            records.insert(name, result.value("result"));
            health_.insert("checks", records);
            renderHealth();
            status(tr("Server health check recorded."));
        }, false, name != u"certificate");
}

void AdminCenter::renderHealth()
{
    healthChecks_->clear();
    if (health_.isEmpty())
    {
        healthPayload_->clear();
        return;
    }
    const auto records = health_.value("checks").toObject();
    for (const auto& check : checks)
    {
        const auto record = records.value(check).toObject();
        if (record.isEmpty()) continue;
        auto* row = new QTreeWidgetItem(healthChecks_, {check, record.value("status").toString(), utc(record.value("checked_at")),
            valueText(record.value("evidence")), valueText(record.value("remediation"))});
        row->setToolTip(3, valueText(record.value("evidence")));
    }
    for (int column = 0; column < 3; ++column) healthChecks_->resizeColumnToContents(column);
    healthPayload_->setPlainText(json(health_));
}

void AdminCenter::inspectService()
{
    const auto instance = localInstance_->text().trimmed();
    if (!safeStem(instance, 48))
    {
        localStatus_->setPlainText(tr("Enter a local instance name with letters, digits, underscores or hyphens."));
        return;
    }
    const auto key = QStringLiteral("StarDataParadise-") + instance;
    const auto scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm)
    {
        localStatus_->setPlainText(localWindowsError(GetLastError()));
        return;
    }
    const auto service = OpenServiceW(scm, reinterpret_cast<LPCWSTR>(key.utf16()), SERVICE_QUERY_STATUS);
    const auto serviceError = service ? ERROR_SUCCESS : GetLastError();
    CloseServiceHandle(scm);
    if (!service)
    {
        localStatus_->setPlainText(tr("%1\n%2").arg(key, localWindowsError(serviceError)));
        return;
    }
    SERVICE_STATUS_PROCESS current{};
    DWORD size{};
    const bool read = QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&current),
        sizeof(current), &size);
    const auto readError = read ? ERROR_SUCCESS : GetLastError();
    CloseServiceHandle(service);
    if (!read)
    {
        localStatus_->setPlainText(tr("%1\n%2").arg(key, localWindowsError(readError)));
        return;
    }
    localStatus_->setPlainText(json({{"service", key}, {"state", localWindowsState(current.dwCurrentState)},
        {"state_code", static_cast<qint64>(current.dwCurrentState)}, {"process_id", static_cast<qint64>(current.dwProcessId)},
        {"win32_exit_code", static_cast<qint64>(current.dwWin32ExitCode)}}));
}

void AdminCenter::invalidateConfig()
{
    if (busy_ || reviewedConfig_.isEmpty()) return;
    reviewedConfig_ = {};
    configPayload_->clear();
    work(false);
}

void AdminCenter::previewConfig()
{
    if (busy_) return;
    QJsonObject patch;
    if (!numericPatch(patch, "timeout_ms", timeout_, 100, 120000) ||
        !numericPatch(patch, "port", port_, 0, 65535) ||
        !numericPatch(patch, "max_connections", connections_, 1, 128) || patch.isEmpty())
    {
        status(tr("Enter at least one setting within its displayed range."));
        return;
    }
    const QJsonObject body{{"config", patch}};
    invoke(SDP_ADMIN_CONFIG_PREVIEW, body, tr("Validating configuration on the server…"), [this, body](const QJsonObject& result) {
        reviewedConfig_ = body;
        configPayload_->setPlainText(json(result));
        status(tr("Review dynamic settings and settings that take effect after restart."));
        work(false);
    });
}

void AdminCenter::applyConfig()
{
    if (busy_ || reviewedConfig_.isEmpty() || session_.profile().read_only) return;
    const auto reviewed = reviewedConfig_;
    if (QMessageBox::question(this, tr("Apply reviewed configuration"),
            tr("Apply the reviewed settings to %1? The server preserves the previous file and reports settings that require a restart.")
                .arg(endpoint(session_.profile())), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    invoke(SDP_ADMIN_CONFIG_APPLY, reviewed, tr("Applying reviewed server configuration…"), [this](const QJsonObject& result) {
        reviewedConfig_ = {};
        configPayload_->setPlainText(json(result));
        status(tr("The server applied the reviewed configuration."));
        work(false);
    }, true);
}

void AdminCenter::previewSupport()
{
    invoke(SDP_ADMIN_SUPPORT_PREVIEW, {}, tr("Reading the redacted server summary…"), [this](const QJsonObject& result) {
        support_ = redactedSummary(result);
        if (support_.isEmpty())
        {
            supportPayload_->clear();
            status(tr("The server summary failed validation."));
        }
        else
        {
            supportPayload_->setPlainText(json(support_));
            status(tr("Redacted support preview loaded."));
        }
        work(false);
    });
}

void AdminCenter::createSupport()
{
    if (busy_ || support_.isEmpty() || session_.profile().read_only) return;
    const auto target = supportTarget_->text().trimmed();
    if (!safeStem(target, 32))
    {
        status(tr("Enter a bundle name with up to 32 lowercase letters, digits, underscores or hyphens."));
        return;
    }
    if (QMessageBox::question(this, tr("Create server support bundle"),
            tr("Create a redacted bundle named %1 on %2?").arg(target, endpoint(session_.profile())),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    invoke(SDP_ADMIN_SUPPORT_CREATE, {{"target_name", target}}, tr("Creating the redacted bundle on the server…"),
        [this, target](const QJsonObject& result) {
            support_ = redactedSummary(result);
            if (support_.isEmpty() || support_.value("target_name").toString() != target)
            {
                support_ = {};
                supportPayload_->clear();
                status(tr("The server returned an inconsistent support receipt."));
            }
            else
            {
                supportPayload_->setPlainText(json(support_));
                status(tr("Server bundle created at temp/support/%1/bundle.json on the server.").arg(target));
            }
            work(false);
        }, true);
}

void AdminCenter::exportSupport()
{
    if (busy_ || support_.isEmpty()) return;
    const auto path = supportPath_->text().trimmed();
    if (path.isEmpty() || !QFileInfo(path).isAbsolute())
    {
        status(tr("Choose an absolute local destination for the redacted summary."));
        return;
    }
    if (QFileInfo::exists(path) && QMessageBox::question(this, tr("Replace local file"),
        tr("Replace the selected local summary file?"), QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No) != QMessageBox::Yes) return;
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    const auto bytes = QJsonDocument(support_).toJson(QJsonDocument::Indented);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
    {
        status(tr("The local summary could not be saved. %1").arg(output.errorString()));
        return;
    }
    status(tr("Redacted summary saved locally."));
}

void AdminCenter::openLogs()
{
    logs_->clear();
    QFile input(logsPath_->text().trimmed());
    if (!input.open(QIODevice::ReadOnly) || input.size() > maxSupportBytes)
    {
        status(tr("Choose a readable redacted bundle within 1 MiB."));
        return;
    }
    const auto bytes = input.read(maxSupportBytes + 1);
    const auto parsed = bytes.size() <= maxSupportBytes ? QJsonDocument::fromJson(bytes).object() : QJsonObject{};
    if (redactedSummary(parsed).isEmpty() || !parsed.value("logs").isObject())
    {
        status(tr("The file is missing the expected redacted bundle structure."));
        return;
    }
    const auto groups = parsed.value("logs").toObject();
    for (const auto& category : categories)
    {
        const auto records = groups.value(category).toArray();
        for (qsizetype index = 0; index < records.size() && index < 64; ++index)
        {
            const auto event = records.at(index).toObject();
            if (!boundedNumber(event.value("at_utc"), 0, 32503680000)) continue;
            const auto name = event.value("event").toString();
            const auto code = event.value("code");
            const auto duration = event.value("duration_ms");
            const auto correlation = event.value("correlation");
            new QTreeWidgetItem(logs_, {category, utc(event.value("at_utc")), events.contains(name) ? name : QStringLiteral("redacted_event"),
                boundedNumber(code, 0, 1016) ? valueText(code) : QString{},
                boundedNumber(duration, 0, 120000) ? valueText(duration) : QString{},
                boundedNumber(correlation, 1, 512) ? valueText(correlation) : QString{}});
        }
    }
    logs_->resizeColumnToContents(1);
    status(tr("Opened %1 redacted log records from the local bundle.").arg(logs_->topLevelItemCount()));
}

void AdminCenter::maintenance()
{
    if (busy_) return;
    const auto action = maintenanceKind_->currentData().toString();
    if (!maintenanceRequest_.isEmpty() && maintenanceRequest_.value("action").toString() != action)
    {
        status(tr("Resolve the previous %1 request using the same action and token before submitting another job.")
            .arg(maintenanceRequest_.value("action").toString()));
        return;
    }
    const bool possibleReplay = !maintenanceRequest_.isEmpty();
    const QJsonObject request = possibleReplay ? maintenanceRequest_ : QJsonObject{
        {"action", action}, {"request_token", QUuid::createUuid().toString(QUuid::Id128)}};
    if (invoke(SDP_ADMIN_MAINTENANCE_SUBMIT, request,
        possibleReplay ? tr("Checking the original maintenance request…") : tr("Submitting a maintenance job…"),
        [this](const QJsonObject& result) {
            maintenanceRequest_ = {};
            jobId_ = result.value("job_id").toString();
            maintenancePayload_->setPlainText(json(result));
            status(tr("Server accepted maintenance job %1.").arg(jobId_));
            work(false);
        }, true, true)) maintenanceRequest_ = request;
}
}

QDialog* showAdminCenter(SqlSession& session, QWidget* parent, std::function<void(const QString&)> openJob)
{
    auto* admin = new AdminCenter(session, parent, std::move(openJob));
    admin->show();
    return admin;
}
}
