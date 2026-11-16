#include "workbench.hpp"
#include "compare_studio.hpp"
#include "migration_studio.hpp"
#include "sql_pane.hpp"
#include "workbench_help.hpp"
#include <QCloseEvent>
#include <QJsonArray>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QKeySequence>
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
    objects_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(objects_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        auto* item = objects_->itemAt(position);
        if (!item) return;
        auto* root = item;
        while (root->parent()) root = root->parent();
        for (const auto& [page, tab] : sessions_)
        {
            if (tab.node != root) continue;
            const auto object = item->data(0, Qt::UserRole).toJsonObject();
            const auto kind = object.value("kind").toInt();
            QMenu actions(this);
            QPointer<QWidget> owner(page);
            const auto add = [&](const QString& text, auto use) {
                actions.addAction(text, this, [this, owner, use] {
                    if (!owner) return;
                    const auto found = sessions_.find(owner.data());
                    if (found == sessions_.end()) return;
                    tabs_->setCurrentWidget(owner.data());
                    use(*found->second.pane);
                });
            };
            if (kind == 3)
            {
                add(tr("Open data grid…"), [object](SqlPane& pane) { pane.showGrid(object); });
                add(tr("Design table…"), [object](SqlPane& pane) { pane.designTable(object); });
                add(tr("Import or export table…"), [object](SqlPane& pane) { pane.transferTable(object); });
                add(tr("Create dataset…"), [object](SqlPane& pane) { pane.openDatasetStudio(object); });
            }
            if (kind == 6)
            {
                add(tr("Open PL/SQL Studio…"), [object](SqlPane& pane) { pane.openRoutineStudio(object); });
                add(tr("Export routine to Java…"), [object](SqlPane& pane) { pane.exportRoutine(object); });
            }
            if (kind >= 3)
                add(tr("History Studio…"), [object](SqlPane& pane) { pane.openHistoryStudio(object); });
            if (kind == 3 || kind == 8)
                add(tr("Decision Studio…"), [object](SqlPane& pane) { pane.openDecisionStudio(object); });
            if (kind == 9)
                add(tr("ML Studio…"), [object](SqlPane& pane) { pane.openMlStudio(object); });
            add(tr("Create table…"), [](SqlPane& pane) { pane.designTable(); });
            add(tr("Schema diagram and dictionary…"), [](SqlPane& pane) { pane.showDictionary(); });
            actions.exec(objects_->viewport()->mapToGlobal(position));
            break;
        }
    });
    explorer->setWidget(objects_);
    addDockWidget(Qt::LeftDockWidgetArea, explorer);
    connect(objects_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        auto* root = item;
        while (root->parent()) root = root->parent();
        for (const auto& [page, tab] : sessions_)
            if (tab.node == root)
            {
                tabs_->setCurrentWidget(page);
                tab.pane->peek(item->data(0, Qt::UserRole).toJsonObject());
                break;
            }
    });

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

    auto* navigate = menuBar()->addMenu(tr("Navigate"));
    auto* search = navigate->addAction(tr("Search connected objects…"), this, [this] { search_objects(); });
    search->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    auto* tools = menuBar()->addMenu(tr("Tools"));
    tools->addAction(tr("Compare schema and data…"), this, [this] { compare_sessions(); });
    tools->addAction(tr("Migrate from PostgreSQL or MySQL…"), this, [this] {
        const auto active = sessions_.find(tabs_->currentWidget());
        if (active == sessions_.end() || !active->second.pane->session().state().connected)
        {
            QMessageBox::information(this, tr("Migration Studio"), tr("Open a destination Server connection first."));
            return;
        }
        showMigrationStudio(active->second.pane->session(), workers_, this);
    });
    menuBar()->addMenu(tr("Help"))->addAction(tr("Offline SQL and PL/SQL help…"), this, [this] {
        showOfflineHelp(this);
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

void Window::compare_sessions()
{
    const auto active = sessions_.find(tabs_->currentWidget());
    if (active == sessions_.end() || !active->second.pane->session().state().connected)
    {
        QMessageBox::information(this, tr("Compare Studio"), tr("Open source and destination Server connections first."));
        return;
    }
    QStringList choices;
    QVector<SqlSession*> destinations;
    for (int index = 0; index < tabs_->count(); ++index)
    {
        const auto candidate = sessions_.find(tabs_->widget(index));
        if (candidate == sessions_.end() || candidate == active || !candidate->second.pane->session().state().connected)
            continue;
        choices << tr("%1. %2").arg(index + 1).arg(tabs_->tabText(index));
        destinations.append(&candidate->second.pane->session());
    }
    if (choices.isEmpty())
    {
        QMessageBox::information(this, tr("Compare Studio"), tr("Open another Server connection as the destination."));
        return;
    }
    bool selected = false;
    const auto choice = QInputDialog::getItem(this, tr("Compare Studio"),
        tr("Choose the destination for the active source worksheet."), choices, 0, false, &selected);
    if (selected)
        showCompareStudio(active->second.pane->session(), *destinations[choices.indexOf(choice)], this);
}

Window::~Window()
{
    while (!sessions_.empty())
    {
        auto* page = sessions_.begin()->first;
        sessions_.erase(sessions_.begin());
        delete page;
    }
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

void Window::search_objects()
{
    struct Candidate
    {
        QString label;
        QJsonObject metadata;
        QPointer<QWidget> page;
    };
    QList<Candidate> candidates;
    for (const auto& [page, tab] : sessions_)
    {
        for (QTreeWidgetItemIterator node(tab.node); *node; ++node)
        {
            auto* item = *node;
            auto* root = item;
            while (root->parent()) root = root->parent();
            if (root != tab.node) break;
            const auto metadata = item->data(0, Qt::UserRole).toJsonObject();
            const auto name = metadata.value("qualified_name").toArray();
            if (name.isEmpty()) continue;
            QStringList parts;
            for (const auto& identifier : name) parts.append(identifier.toString());
            candidates.push_back({parts.join('.') + " · " + tab.node->text(0), metadata, page});
        }
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Search connected objects"));
    dialog.resize(630, 420);
    auto* layout = new QVBoxLayout(&dialog);
    auto* search = new QLineEdit;
    search->setPlaceholderText(tr("Object, schema or connection name"));
    layout->addWidget(search);
    auto* results = new QListWidget;
    layout->addWidget(results, 1);
    const auto update = [&] {
        results->clear();
        const auto filter = search->text().trimmed();
        for (qsizetype index = 0; index < candidates.size() && results->count() < 256; ++index)
        {
            const auto& item = candidates.at(index);
            if (!item.label.contains(filter, Qt::CaseInsensitive)) continue;
            auto* result = new QListWidgetItem(item.label, results);
            result->setData(Qt::UserRole, index);
        }
        if (results->count()) results->setCurrentRow(0);
    };
    connect(search, &QLineEdit::textChanged, &dialog, update);
    connect(search, &QLineEdit::returnPressed, &dialog, [&] { if (results->currentItem()) dialog.accept(); });
    connect(results, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
    update();
    if (dialog.exec() != QDialog::Accepted || !results->currentItem()) return;
    const auto index = results->currentItem()->data(Qt::UserRole).toLongLong();
    if (index < 0 || index >= candidates.size()) return;
    const auto& candidate = candidates.at(index);
    if (!candidate.page) return;
    const auto tab = sessions_.find(candidate.page.data());
    if (tab == sessions_.end()) return;
    tabs_->setCurrentWidget(candidate.page.data());
    tab->second.pane->peek(candidate.metadata);
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
    auto* header = new QHBoxLayout;
    header->addWidget(state, 1);
    auto* health = new QPushButton(tr("Check connection"));
    header->addWidget(health);
    content->addLayout(header);
    connect(health, &QPushButton::clicked, this, [this, page] { check_session(tabs_->indexOf(page)); });

    auto* pane = new SqlPane(profile, std::move(session), workers_,
        settings_file_.isEmpty() ? QString{} : settings_file_ + ".sql-drafts.json", page);
    content->addWidget(pane, 1);
    auto* node = new QTreeWidgetItem(objects_, {profile.name + " (" + endpoint(profile) + ")"});
    sessions_.emplace(page, Tab{pane, node, state});
    pane->setMetadataHandler([this, page, node, pane](const QJsonArray& payload) {
        if (!sessions_.contains(page)) return;
        qDeleteAll(node->takeChildren());
        QHash<QString, QTreeWidgetItem*> parents;
        parents.insert(QString{}, node);
        static const QStringList kinds{tr("Object"), tr("Database"), tr("Schema"), tr("Table"),
            tr("Index"), tr("View"), tr("Routine"), tr("Sequence"), tr("Decision"), tr("Model")};
        for (const auto& entry : payload)
        {
            const auto object = entry.toObject();
            QStringList qualified;
            for (const auto& name : object.value("qualified_name").toArray()) qualified.append(name.toString());
            if (qualified.isEmpty()) continue;
            const auto full = qualified.join(QChar(0x1f));
            const auto name = qualified.takeLast();
            const auto kind = kinds.value(object.value("kind").toInt(), kinds.front());
            auto* item = new QTreeWidgetItem(parents.value(qualified.join(QChar(0x1f)), node),
                {tr("%1 · %2").arg(name, kind)});
            item->setData(0, Qt::UserRole, object);
            if (qualified.size() < 2) parents.insert(full, item);
            for (const auto& value : object.value("columns").toArray())
            {
                const auto column = value.toObject();
                auto* child = new QTreeWidgetItem(item, {tr("%1 · %2")
                    .arg(column.value("name").toString(), column.value("type").toString())});
                child->setData(0, Qt::UserRole, column);
            }
        }
        const auto& features = pane->session().capabilities();
        for (const auto& item : {std::pair{"history_v1", tr("History")}, std::pair{"jobs_v1", tr("Jobs")}})
            if (features.value(item.first).toBool() && features.contains(item.first))
                new QTreeWidgetItem(node, {item.second});
        node->setExpanded(true);
    });
    tabs_->setCurrentIndex(tabs_->addTab(page, profile.name + " — " + endpoint(profile)));
    note(tr("Connected to %1").arg(endpoint(profile)));
    pane->session().refreshIdentity([pane](const SqlReply&) { pane->refreshObjects(); });
}

void Window::check_session(int index)
{
    auto* page = tabs_->widget(index);
    const auto match = sessions_.find(page);
    if (match == sessions_.end() || match->second.checking) return;
    match->second.checking = true;
    match->second.state->setText(tr("Checking connection…"));
    QPointer<QWidget> tab(page);
    match->second.pane->health([this, tab](const SqlReply& reply) {
        if (!tab) return;
        const auto match = sessions_.find(tab.data());
        if (match == sessions_.end()) return;
        const auto name = tab->property("endpoint").toString();
        match->second.checking = false;
        if (reply.error.code == SDP_OK)
        {
            match->second.state->setText(tr("Connected to %1").arg(name));
            note(tr("Health verified for %1").arg(name));
        }
        else
        {
            match->second.state->setText(status(reply.error.code));
            note(tr("%1 — %2").arg(name, status(reply.error.code)));
        }
    });
}

void Window::close_session(int index)
{
    auto* page = tabs_->widget(index);
    const auto match = sessions_.find(page);
    if (match == sessions_.end() || !match->second.pane->allowClose()) return;
    match->second.pane->saveDraft();
    delete match->second.node;
    sessions_.erase(match);
    tabs_->removeTab(index);
    note(sessions_.empty() ? tr("Disconnected") : tr("Closed %1").arg(page->property("endpoint").toString()));
    delete page;
}

void Window::closeEvent(QCloseEvent* event)
{
    for (const auto& [page, tab] : sessions_)
        if (!tab.pane->allowClose()) { event->ignore(); return; }
    QMainWindow::closeEvent(event);
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
