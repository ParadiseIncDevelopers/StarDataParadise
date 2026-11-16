#include "security_center.hpp"
#include "sql_session.hpp"
#include "table_codec.hpp"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>

namespace wb
{
namespace
{
QString qualified(const QJsonObject& object)
{
    QStringList names;
    for (const auto& item : object.value("qualified_name").toArray())
    {
        const auto value = item.toString();
        if (value.isEmpty() || value.contains(QChar(u'\0'))) return {};
        names.push_back(transfer::identifier(value));
    }
    return names.join('.');
}

QString pretty(const QJsonObject& data)
{
    return QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Indented));
}

bool validIdentifier(QStringView text)
{
    if (text.isEmpty() || text.toUtf8().size() > 128) return false;
    for (const auto c : text)
        if (c.category() == QChar::Other_Control) return false;
    return true;
}

class SecurityCenter final : public QDialog
{
public:
    SecurityCenter(SqlSession&, const QJsonArray&, QWidget*, std::function<void()>);
    void reject() override;

private:
    QPushButton* action(QHBoxLayout&, const QString&, const char*, std::function<void()>);
    void scopeTargets();
    void grant(bool grant);
    void membership(bool grant);
    void create(bool role);
    void resetPassword();
    void inspectRights();
    void inspectSessions();
    bool available(bool mutate);
    void run(const QString& sql, bool accessRisk = false);
    void render(const SqlReply&);
    void setPending(bool pending);

    SqlSession& session_;
    QJsonArray catalog_;
    const std::function<void()> refresh_;
    QLineEdit *principal_{}, *membership_{}, *password_{}, *repeatPassword_{};
    QComboBox *principalKind_{}, *privilege_{}, *scope_{}, *target_{};
    QPlainTextEdit *preview_{}, *result_{};
    QLabel* status_{};
    QTreeWidget* rights_{};
    QList<QPushButton*> buttons_, changes_;
    bool pending_{};
};

QPushButton* SecurityCenter::action(QHBoxLayout& row, const QString& label, const char* id, std::function<void()> apply)
{
    auto* button = new QPushButton(label);
    button->setObjectName(QString::fromLatin1(id));
    row.addWidget(button);
    buttons_.push_back(button);
    connect(button, &QPushButton::clicked, this, std::move(apply));
    return button;
}

SecurityCenter::SecurityCenter(SqlSession& session, const QJsonArray& catalog, QWidget* parent,
    std::function<void()> refresh)
    : QDialog(parent), session_(session), catalog_(catalog), refresh_(std::move(refresh))
{
    setObjectName("securityCenter");
    setWindowTitle(tr("Users, Roles and Permissions"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(775, 680);
    auto* layout = new QVBoxLayout(this);
    auto* identity = new QLabel(tr("Signed in as %1 on %2").arg(session.profile().user, endpoint(session.profile())));
    identity->setTextFormat(Qt::PlainText);
    layout->addWidget(identity);
    auto* account = new QFormLayout;
    layout->addLayout(account);
    principal_ = new QLineEdit;
    principal_->setObjectName("permissionPrincipal");
    principal_->setMaxLength(128);
    account->addRow(tr("User or role name"), principal_);
    auto* accountActions = new QHBoxLayout;
    changes_ << action(*accountActions, tr("Create user…"), "permissionCreateUser", [this] { create(false); });
    changes_ << action(*accountActions, tr("Create role…"), "permissionCreateRole", [this] { create(true); });
    account->addRow(accountActions);
    auto* tabs = new QTabWidget;
    layout->addWidget(tabs, 1);

    auto* access = new QWidget;
    auto* accessLayout = new QVBoxLayout(access);
    auto* form = new QFormLayout;
    accessLayout->addLayout(form);
    principalKind_ = new QComboBox;
    principalKind_->setObjectName("permissionPrincipalKind");
    principalKind_->addItem(tr("User"), QStringLiteral("USER"));
    principalKind_->addItem(tr("Role"), QStringLiteral("ROLE"));
    form->addRow(tr("Recipient"), principalKind_);
    privilege_ = new QComboBox;
    privilege_->setObjectName("permissionPrivilege");
    for (const auto privilege : {"SELECT", "INSERT", "UPDATE", "DELETE", "USAGE", "DDL", "EXECUTE",
        "HISTORY", "BACKUP", "DEBUG", "MONITOR", "CONTROL"})
        privilege_->addItem(QString::fromLatin1(privilege), QString::fromLatin1(privilege));
    form->addRow(tr("Privilege"), privilege_);
    scope_ = new QComboBox;
    scope_->setObjectName("permissionScope");
    const std::pair<const char*, int> scopes[]{
        {"TABLE", 3}, {"SCHEMA", 2}, {"DATABASE", 1}, {"VIEW", 5}, {"ROUTINE", 6}, {"MODEL", 9}, {"GLOBAL", 0}
    };
    for (const auto& [key, kind] : scopes)
    {
        scope_->addItem(QString::fromLatin1(key), QString::fromLatin1(key));
        scope_->setItemData(scope_->count() - 1, kind, Qt::UserRole + 1);
    }
    form->addRow(tr("Scope"), scope_);
    target_ = new QComboBox;
    target_->setObjectName("permissionTarget");
    form->addRow(tr("Catalog object"), target_);
    connect(scope_, &QComboBox::currentIndexChanged, this, [this] { scopeTargets(); });
    scopeTargets();
    auto* grants = new QHBoxLayout;
    form->addRow(grants);
    changes_ << action(*grants, tr("Grant…"), "permissionGrant", [this] { grant(true); });
    changes_ << action(*grants, tr("Revoke…"), "permissionRevoke", [this] { grant(false); });
    membership_ = new QLineEdit;
    membership_->setObjectName("permissionRoleName");
    membership_->setMaxLength(128);
    form->addRow(tr("Role to assign to the named user"), membership_);
    auto* roles = new QHBoxLayout;
    form->addRow(roles);
    changes_ << action(*roles, tr("Add role membership…"), "permissionAssignRole", [this] { membership(true); });
    changes_ << action(*roles, tr("Revoke role membership…"), "permissionRemoveRole", [this] { membership(false); });
    preview_ = new QPlainTextEdit;
    preview_->setObjectName("permissionSql");
    preview_->setReadOnly(true);
    accessLayout->addWidget(preview_, 1);
    auto* copy = new QPushButton(tr("Copy reviewed SQL"));
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(preview_->toPlainText()); });
    accessLayout->addWidget(copy);
    tabs->addTab(access, tr("Grants and membership"));

    auto* credentials = new QWidget;
    auto* credentialsLayout = new QVBoxLayout(credentials);
    auto* passwordForm = new QFormLayout;
    credentialsLayout->addLayout(passwordForm);
    password_ = new QLineEdit;
    password_->setEchoMode(QLineEdit::Password);
    password_->setMaxLength(1024);
    repeatPassword_ = new QLineEdit;
    repeatPassword_->setEchoMode(QLineEdit::Password);
    repeatPassword_->setMaxLength(1024);
    passwordForm->addRow(tr("New password for the named user"), password_);
    passwordForm->addRow(tr("Repeat password"), repeatPassword_);
    auto* passwords = new QHBoxLayout;
    passwordForm->addRow(passwords);
    changes_ << action(*passwords, tr("Set password…"), "permissionSetPassword", [this] { resetPassword(); });
    credentialsLayout->addStretch();
    tabs->addTab(credentials, tr("Password reset"));

    auto* effective = new QWidget;
    auto* effectiveLayout = new QVBoxLayout(effective);
    auto* refreshes = new QHBoxLayout;
    effectiveLayout->addLayout(refreshes);
    action(*refreshes, tr("Read my effective table permissions"), "permissionInspect", [this] { inspectRights(); });
    action(*refreshes, tr("Read connected sessions"), "permissionSessions", [this] { inspectSessions(); });
    rights_ = new QTreeWidget;
    rights_->setObjectName("permissionEffective");
    rights_->setHeaderLabels({"Table", "SELECT", "INSERT", "UPDATE", "DELETE", "DDL"});
    rights_->setRootIsDecorated(false);
    effectiveLayout->addWidget(rights_);
    tabs->addTab(effective, tr("Effective access"));

    result_ = new QPlainTextEdit;
    result_->setObjectName("permissionResult");
    result_->setReadOnly(true);
    tabs->addTab(result_, tr("Server response"));
    status_ = new QLabel;
    status_->setObjectName("permissionStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    setPending(false);
}

void SecurityCenter::reject()
{
    if (pending_)
    {
        status_->setText(tr("Wait for the Server response before closing."));
        return;
    }
    QDialog::reject();
}

void SecurityCenter::setPending(bool pending)
{
    pending_ = pending;
    for (auto* button : buttons_) button->setEnabled(!pending);
    for (auto* button : changes_) button->setEnabled(!pending && !session_.profile().read_only);
}

bool SecurityCenter::available(bool mutate)
{
    if (pending_ || session_.busy() || !session_.state().connected)
    {
        status_->setText(tr("Use an idle connected worksheet."));
        return false;
    }
    if (mutate && (session_.profile().read_only || !session_.state().autocommit || session_.state().transactionActive))
    {
        status_->setText(tr("Changing access requires a writable profile and autocommit with no open transaction."));
        return false;
    }
    if (!session_.capabilities().value("sql_security_v1").toBool())
    {
        status_->setText(tr("The connected Server does not expose SQL security."));
        return false;
    }
    return true;
}

void SecurityCenter::scopeTargets()
{
    target_->clear();
    const auto kind = scope_->currentData(Qt::UserRole + 1).toInt();
    target_->setEnabled(kind != 0);
    for (const auto& value : catalog_)
    {
        const auto object = value.toObject();
        if (object.value("kind").toInt() != kind) continue;
        const auto name = qualified(object);
        if (!name.isEmpty()) target_->addItem(name);
    }
}

void SecurityCenter::grant(bool granting)
{
    const auto principal = principal_->text().trimmed();
    const auto kind = principalKind_->currentData().toString();
    const auto scope = scope_->currentData().toString();
    const auto target = target_->currentText();
    if (!validIdentifier(principal) || (scope != "GLOBAL" && target.isEmpty()))
    {
        status_->setText(tr("Enter a valid recipient name and select the catalog object."));
        return;
    }
    const auto object = scope == "GLOBAL" ? scope : scope + ' ' + target;
    const auto query = QStringLiteral("%1 %2 ON %3 %4 %5 %6")
        .arg(granting ? "GRANT" : "REVOKE", privilege_->currentData().toString(), object,
            granting ? "TO" : "FROM", kind, transfer::identifier(principal));
    run(query, !granting && (kind == "ROLE" || principal == session_.profile().user));
}

void SecurityCenter::membership(bool granting)
{
    const auto user = principal_->text().trimmed();
    const auto role = membership_->text().trimmed();
    if (!validIdentifier(user) || !validIdentifier(role))
    {
        status_->setText(tr("Enter an existing user and role name."));
        return;
    }
    run(QStringLiteral("%1 %2 %3 USER %4").arg(granting ? "GRANT" : "REVOKE",
        transfer::identifier(role), granting ? "TO" : "FROM", transfer::identifier(user)),
        !granting && user == session_.profile().user);
}

void SecurityCenter::create(bool role)
{
    const auto name = principal_->text().trimmed();
    if (!validIdentifier(name))
    {
        status_->setText(tr("Enter an account or role name of up to 128 UTF-8 bytes."));
        return;
    }
    run((role ? QStringLiteral("CREATE ROLE ") : QStringLiteral("CREATE USER ")) + transfer::identifier(name));
}

void SecurityCenter::run(const QString& sql, bool accessRisk)
{
    if (!available(true)) return;
    preview_->setPlainText(sql);
    const auto message = accessRisk ? tr("This change can revoke access from your account or current role.\n%1").arg(sql) : sql;
    if (QMessageBox::question(this, tr("Review access change on %1").arg(endpoint(session_.profile())), message,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    setPending(true);
    setProperty("permissionLastResponse", QJsonObject{});
    result_->clear();
    status_->setText(tr("Waiting for the Server to apply the access change…"));
    const QPointer<SecurityCenter> self(this);
    if (session_.execute({{sql}}, {}, {}, {10, 5000}, {}, [self](const SqlReply& reply) {
        if (!self) return;
        self->render(reply);
        if (reply.error.code == SDP_OK && self->refresh_) self->refresh_();
    })) return;
    setPending(false);
    status_->setText(tr("Could not start the access change."));
}

void SecurityCenter::resetPassword()
{
    if (!available(true)) return;
    const auto principal = principal_->text().trimmed();
    if (!validIdentifier(principal))
    {
        status_->setText(tr("Enter a valid target user."));
        return;
    }
    auto secret = std::make_shared<Password>();
    secret->bytes = password_->text().toUtf8();
    const bool matches = password_->text() == repeatPassword_->text();
    password_->clear();
    repeatPassword_->clear();
    if (!matches || secret->bytes.isEmpty() || secret->bytes.size() > 1024 || secret->bytes.contains('\0'))
    {
        status_->setText(tr("Enter matching passwords within the Server length limits."));
        return;
    }
    if (QMessageBox::question(this, tr("Reset credentials"),
            tr("Set a new password for %1 on %2?").arg(principal, endpoint(session_.profile())),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    const auto name = principal.toUtf8();
    const auto token = QUuid::createUuid().toRfc4122().toHex();
    preview_->clear();
    result_->clear();
    status_->setText(tr("Waiting for the Server to update the credential…"));
    setPending(true);
    const QPointer<SecurityCenter> self(this);
    if (session_.request([name, secret, token](auto handle, auto*, auto* error) {
        const auto view = [](const QByteArray& bytes) {
            return sdp_text{bytes.constData(), static_cast<uint32_t>(bytes.size())};
        };
        const auto status = sdp_password_set(handle, view(name), {}, view(secret->bytes), view(token), error);
        secret->bytes.fill('\0');
        return status;
    }, [self, token](SqlReply reply) {
        if (!self) return;
        if (reply.error.unknown_outcome) reply.requestToken = QString::fromLatin1(token);
        self->render(reply);
    })) return;
    setPending(false);
    status_->setText(tr("Could not start the credential update."));
}

void SecurityCenter::inspectRights()
{
    if (!available(false)) return;
    setPending(true);
    const QPointer<SecurityCenter> self(this);
    if (session_.refreshMetadata([self](const SqlReply& reply) {
        if (!self) return;
        self->render(reply);
        if (reply.error.code != SDP_OK) return;
        self->catalog_ = reply.data.value("objects").toArray();
        self->scopeTargets();
        self->rights_->clear();
        for (const auto& value : self->catalog_)
        {
            const auto object = value.toObject();
            if (object.value("kind").toInt() != 3) continue;
            const auto permissions = object.value("permissions").toObject();
            QStringList cells{qualified(object)};
            for (const auto key : {"select", "insert", "update", "delete", "ddl"})
                cells.append(!permissions.contains(key) ? tr("Unknown") :
                    permissions.value(key).toBool() ? tr("Allowed") : tr("Denied"));
            new QTreeWidgetItem(self->rights_, cells);
        }
        self->rights_->resizeColumnToContents(0);
    })) return;
    setPending(false);
}

void SecurityCenter::inspectSessions()
{
    if (!available(false)) return;
    setPending(true);
    const QPointer<SecurityCenter> self(this);
    if (session_.request(sdp_session_list, [self](const SqlReply& reply) {
        if (self) self->render(reply);
    })) return;
    setPending(false);
}

void SecurityCenter::render(const SqlReply& reply)
{
    setPending(false);
    if (reply.error.code == SDP_OK)
    {
        result_->setPlainText(pretty(reply.data));
        setProperty("permissionLastResponse", reply.data);
        status_->setText(tr("Server confirmed the request."));
        return;
    }
    setProperty("permissionLastResponse", QJsonObject{});
    result_->clear();
    auto error = QString::fromUtf8(reply.error.message);
    if (error.isEmpty()) error = wb::status(reply.error.code);
    status_->setText(reply.error.unknown_outcome
        ? tr("Request outcome unknown. Check token %1 in the worksheet. %2").arg(reply.requestToken, error) : error);
}
}

QDialog* showSecurityCenter(SqlSession& session, const QJsonArray& catalog, QWidget* parent,
    std::function<void()> refreshed)
{
    auto* center = new SecurityCenter(session, catalog, parent, std::move(refreshed));
    center->show();
    return center;
}
}
