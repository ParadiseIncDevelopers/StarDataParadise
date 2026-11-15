#include "workbench.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace wb
{
namespace
{
enum class Action { connect, test, save };

class ProfileEditor : public QDialog
{
public:
    ProfileEditor(QWidget* parent, bool local, const QVector<Profile>& profiles) : QDialog(parent)
    {
        setWindowTitle(tr("Connection manager"));
        auto* form = new QFormLayout(this);
        auto* saved = new QComboBox;
        saved->addItem(tr("New profile"));
        for (const auto& profile : profiles) saved->addItem(profile.name);
        form->addRow(tr("Saved profile"), saved);
        form->addRow(tr("Name"), &name_);
        form->addRow(tr("Host"), &host_);
        port_.setRange(1, 65535);
        form->addRow(tr("Port"), &port_);
        form->addRow(tr("Database preference"), &database_);
        form->addRow(tr("User"), &user_);
        form->addRow(tr("TLS name (optional)"), &tls_name_);

        auto* trust = new QWidget;
        auto* trust_layout = new QHBoxLayout(trust);
        trust_layout->setContentsMargins(0, 0, 0, 0);
        trust_layout->addWidget(&ca_file_);
        auto* browse = new QPushButton(tr("Browse"));
        trust_layout->addWidget(browse);
        form->addRow(tr("CA PEM"), trust);
        connect(browse, &QPushButton::clicked, this, [this] {
            const auto path = QFileDialog::getOpenFileName(this, tr("Select trusted CA bundle"), {}, tr("PEM files (*.pem *.crt);;All files (*)"));
            if (!path.isEmpty()) ca_file_.setText(path);
        });

        environment_.addItems({"dev", "test", "prod"});
        form->addRow(tr("Environment"), &environment_);
        read_only_.setText(tr("Read-only preference"));
        read_only_.setToolTip(tr("Server permissions determine which operations are authorized."));
        form->addRow(&read_only_);
        password_.setEchoMode(QLineEdit::Password);
        password_.setMaxLength(1024);
        form->addRow(tr("Password"), &password_);
        remember_.setText(tr("Use or save password in Windows Credential Manager"));
        form->addRow(&remember_);
        auto* samples = new QCheckBox(tr("Load sample data"));
        samples->setEnabled(false);
        samples->setToolTip(tr("Requires SQL support from the server."));
        form->addRow(samples);
        form->addRow(&validation_);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
        auto* test = buttons->addButton(tr("Test"), QDialogButtonBox::ActionRole);
        auto* save = buttons->addButton(tr("Save profile"), QDialogButtonBox::ActionRole);
        auto* open = buttons->addButton(tr("Connect"), QDialogButtonBox::AcceptRole);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(test, &QPushButton::clicked, this, [this] { apply(Action::test); });
        connect(save, &QPushButton::clicked, this, [this] { apply(Action::save); });
        connect(open, &QPushButton::clicked, this, [this] { apply(Action::connect); });

        Profile initial;
        initial.name = local ? tr("Local") : tr("Remote");
        if (local) initial.host = "127.0.0.1";
        set_fields(initial);
        connect(saved, &QComboBox::currentIndexChanged, this, [this, profiles, initial](int index) {
            set_fields(index == 0 ? initial : profiles.at(index - 1));
        });
    }

    Profile profile() const
    {
        return {name_.text().trimmed(), host_.text().trimmed(), database_.text().trimmed(), user_.text().trimmed(),
            tls_name_.text().trimmed(), ca_file_.text().trimmed(), environment_.currentText(),
            static_cast<quint16>(port_.value()), read_only_.isChecked()};
    }
    QByteArray take_password() { auto value = password_.text().toUtf8(); password_.clear(); return value; }
    bool remember() const { return remember_.isChecked(); }
    Action action() const { return action_; }

private:
    void set_fields(const Profile& profile)
    {
        name_.setText(profile.name);
        host_.setText(profile.host);
        port_.setValue(profile.port);
        database_.setText(profile.database);
        user_.setText(profile.user);
        tls_name_.setText(profile.tls_name);
        ca_file_.setText(profile.ca_file);
        environment_.setCurrentText(profile.environment);
        read_only_.setChecked(profile.read_only);
        password_.clear();
        remember_.setChecked(false);
        validation_.clear();
    }

    void apply(Action action)
    {
        const auto error = validate(profile());
        if (!error.isEmpty()) { validation_.setText(error); return; }
        action_ = action;
        accept();
    }

    QLineEdit name_, host_, database_, user_, tls_name_, ca_file_, password_;
    QComboBox environment_;
    QSpinBox port_;
    QCheckBox read_only_, remember_;
    QLabel validation_;
    Action action_{Action::connect};
};
}

Window::Window(QString settings_file) : settings_file_(std::move(settings_file))
{
    setWindowTitle(QStringLiteral("StarDataParadise"));
    resize(1000, 660);
    storage_.setMaxThreadCount(1);
    workers_.setMaxThreadCount(3);

    tabs_ = new QTabWidget;
    tabs_->setObjectName("sessions");
    tabs_->setTabsClosable(true);
    setCentralWidget(tabs_);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) { close_session(index); });

    first_run_ = new QWidget;
    auto* start = new QVBoxLayout(first_run_);
    start->setAlignment(Qt::AlignCenter);
    auto* summary = new QLabel(tr("Connect to a local or remote StarDataParadise Server."));
    start->addWidget(summary);
    auto* local = new QPushButton(tr("Local connection"));
    auto* remote = new QPushButton(tr("Remote connection"));
    start->addWidget(local);
    start->addWidget(remote);
    connect(local, &QPushButton::clicked, this, [this] { edit_profiles(true); });
    connect(remote, &QPushButton::clicked, this, [this] { edit_profiles(false); });
    tabs_->addTab(first_run_, tr("Connections"));

    auto* explorer = new QDockWidget(tr("Object explorer"), this);
    objects_ = new QTreeWidget;
    objects_->setHeaderHidden(true);
    explorer->setWidget(objects_);
    addDockWidget(Qt::LeftDockWidgetArea, explorer);

    auto* tasks = new QDockWidget(tr("Tasks"), this);
    tasks_ = new QListWidget;
    tasks_->setObjectName("connectionTasks");
    tasks->setWidget(tasks_);
    addDockWidget(Qt::BottomDockWidgetArea, tasks);
    statusBar()->showMessage(tr("Disconnected"));

    auto* connection = menuBar()->addMenu(tr("Connections"));
    connection->addAction(tr("Manage profiles…"), this, [this] { edit_profiles(false); });
    connection->addAction(tr("Import workspace…"), this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("Import workspace"), {}, tr("JSON files (*.json)"));
        if (!path.isEmpty()) import_from(path);
    });
    connection->addAction(tr("Export workspace…"), this, [this] {
        const auto path = QFileDialog::getSaveFileName(this, tr("Export workspace"), {}, tr("JSON files (*.json)"));
        if (!path.isEmpty()) export_to(path);
    });

    if (!settings_file_.isEmpty())
    {
        const auto path = settings_file_;
        storage_.start([this, path] {
            if (!QFileInfo::exists(path)) return;
            QString error;
            auto items = wb::import_profiles(path, error);
            QMetaObject::invokeMethod(this, [this, items = std::move(items), error] {
                if (error.isEmpty()) loaded(items);
                else note(error);
            }, Qt::QueuedConnection);
        });
    }
}

Window::~Window()
{
    for (auto& [page, tab] : sessions_)
        workers_.start([session = std::move(tab.session)] { });
    workers_.waitForDone();
    storage_.waitForDone();
}

void Window::note(const QString& text)
{
    if (tasks_->count() >= 128) delete tasks_->takeItem(0);
    tasks_->addItem(text);
    tasks_->scrollToBottom();
    statusBar()->showMessage(text);
}

void Window::edit_profiles(bool local)
{
    ProfileEditor editor(this, local, profiles_);
    if (editor.exec() != QDialog::Accepted) return;
    auto profile = editor.profile();
    if (editor.action() == Action::save) { save_profile(std::move(profile)); return; }
    open_profile(std::move(profile), editor.take_password(), editor.remember(), editor.action() == Action::test);
}

void Window::open_profile(Profile profile, QByteArray bytes, bool remember, bool test_only, bool session_credential)
{
    auto password = std::make_shared<Password>();
    password->bytes = std::move(bytes);
    note(tr("Connecting to %1").arg(endpoint(profile)));
    workers_.start([this, profile = std::move(profile), password, remember, test_only, session_credential] {
        ConnectResult result;
        try
        {
            const bool save = remember && !password->bytes.isEmpty() && !test_only;
            result = wb::connect(profile, *password, remember, save, session_credential);
        }
        catch (...) { result.error = QStringLiteral("Connection could not be completed."); }
        if (!password->bytes.isEmpty()) password->bytes.fill('\0');
        const bool connected = bool(result.session);
        if (test_only) result.session.reset();
        QMetaObject::invokeMethod(this, [this, profile, result = std::move(result), test_only, connected] () mutable {
            if (!connected)
            {
                note(tr("%1 — %2").arg(endpoint(profile), result.error));
                return;
            }
            if (test_only) { note(tr("Connection test passed for %1").arg(endpoint(profile))); return; }
            add_tab(profile, std::move(result.session));
            if (!result.credential_note.isEmpty()) note(result.credential_note);
        }, Qt::QueuedConnection);
    });
}

void Window::add_tab(const Profile& profile, std::shared_ptr<Session> session)
{
    auto* page = new QWidget;
    page->setProperty("endpoint", endpoint(profile));
    auto* content = new QVBoxLayout(page);
    auto* state = new QLabel(tr("Connected to %1").arg(endpoint(profile)));
    state->setObjectName("sessionState");
    state->setTextFormat(Qt::PlainText);
    content->addWidget(state);
    auto* preferences = new QLabel(tr("%1 · %2 · Database preference: %3").arg(
        profile.environment, profile.read_only ? tr("Read-only preference") : tr("Default permissions"),
        profile.database.isEmpty() ? tr("Default") : profile.database));
    preferences->setTextFormat(Qt::PlainText);
    content->addWidget(preferences);
    auto* capabilities = new QTreeWidget;
    capabilities->setHeaderLabels({tr("Capability"), tr("State")});
    for (auto it = session->capabilities.begin(); it != session->capabilities.end(); ++it)
        new QTreeWidgetItem(capabilities, {it.key(), it.value().toBool() ? tr("Available") : tr("Unavailable")});
    content->addWidget(capabilities);
    auto* refresh = new QPushButton(tr("Check connection"));
    content->addWidget(refresh);
    connect(refresh, &QPushButton::clicked, this, [this, page] { check_session(tabs_->indexOf(page)); });
    auto* node = new QTreeWidgetItem(objects_, {profile.name + " (" + endpoint(profile) + ")"});
    sessions_.emplace(page, Tab{std::move(session), node, state});
    tabs_->setCurrentIndex(tabs_->addTab(page, profile.name + " — " + endpoint(profile)));
    note(tr("Connected to %1").arg(endpoint(profile)));
}

void Window::check_session(int index)
{
    auto* page = tabs_->widget(index);
    const auto match = sessions_.find(page);
    if (match == sessions_.end()) return;
    if (match->second.checking) return;
    match->second.checking = true;
    match->second.state->setText(tr("Checking connection…"));
    QPointer<QWidget> tab(page);
    workers_.start([this, tab, session = match->second.session] {
        const auto result = sdp_health(session->handle, nullptr);
        QMetaObject::invokeMethod(this, [this, tab, result] {
            if (!tab) return;
            const auto match = sessions_.find(tab.data());
            if (match == sessions_.end()) return;
            const auto name = tab->property("endpoint").toString();
            match->second.checking = false;
            if (result == SDP_OK)
            {
                match->second.state->setText(tr("Connected to %1").arg(name));
                note(tr("Health verified for %1").arg(name));
            }
            else
            {
                close_session(tabs_->indexOf(tab.data()));
                note(tr("%1 — %2").arg(name, status(result)));
            }
        }, Qt::QueuedConnection);
    });
}

void Window::close_session(int index)
{
    auto* page = tabs_->widget(index);
    const auto match = sessions_.find(page);
    if (match == sessions_.end()) return;
    delete match->second.node;
    auto session = std::move(match->second.session);
    sessions_.erase(match);
    tabs_->removeTab(index);
    page->deleteLater();
    workers_.start([session = std::move(session)] { });
    note(sessions_.empty() ? tr("Disconnected") : tr("Closed %1").arg(page->property("endpoint").toString()));
}

void Window::loaded(QVector<Profile> profiles)
{
    for (auto& profile : profiles)
    {
        const auto found = std::ranges::find(profiles_, profile.name, &Profile::name);
        if (found != profiles_.end()) continue;
        if (profiles_.size() == 64) break;
        profiles_.push_back(std::move(profile));
    }
}

void Window::save_profile(Profile profile)
{
    const auto found = std::ranges::find(profiles_, profile.name, &Profile::name);
    if (found == profiles_.end())
    {
        if (profiles_.size() == 64) { note(tr("Profile limit reached.")); return; }
        profiles_.push_back(std::move(profile));
    }
    else *found = std::move(profile);
    note(tr("Profile saved to workspace"));
    write_settings();
}

void Window::write_settings()
{
    if (settings_file_.isEmpty()) return;
    const auto path = settings_file_;
    const auto profiles = profiles_;
    storage_.start([this, path, profiles] {
        QString error;
        QDir().mkpath(QFileInfo(path).absolutePath());
        if (!export_profiles(path, profiles, error))
            QMetaObject::invokeMethod(this, [this, error] { note(error); }, Qt::QueuedConnection);
    });
}

void Window::import_from(QString path)
{
    storage_.start([this, path = std::move(path)] {
        QString error;
        auto profiles = import_profiles(path, error);
        QMetaObject::invokeMethod(this, [this, profiles = std::move(profiles), error] {
            if (error.isEmpty())
            {
                loaded(profiles);
                write_settings();
                note(tr("Workspace profiles imported"));
            }
            else note(error);
        }, Qt::QueuedConnection);
    });
}

void Window::export_to(QString path)
{
    storage_.start([this, path = std::move(path), profiles = profiles_] {
        QString error;
        const bool ok = export_profiles(path, profiles, error);
        QMetaObject::invokeMethod(this, [this, ok, error] {
            note(ok ? tr("Workspace profiles exported") : error);
        }, Qt::QueuedConnection);
    });
}
}
