#include "workbench.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QListWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QThread>
#include <QTimer>

#include <iostream>
#include <stdexcept>

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template<class Condition>
void wait_until(Condition condition, const char* message)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < 8000)
    {
        QCoreApplication::processEvents();
        QThread::msleep(12);
    }
    require(condition(), message);
}

int find_tab(QTabWidget& tabs, const QString& identity)
{
    for (int index = 0; index < tabs.count(); ++index)
        if (tabs.widget(index)->property("endpoint").toString() == identity) return index;
    return -1;
}

struct CredentialCleanup
{
    const wb::Profile& profile;
    ~CredentialCleanup() { wb::forget_credential(profile); }
};

void smoke(const QString& workspace, const QString& output_path, const QString& invalid,
    const QJsonObject& inputs)
{
    QString error;
    const auto profiles = wb::import_profiles(workspace, error);
    require(error.isEmpty() && profiles.size() == 2, "invalid fixture profile data");
    const auto& primary = profiles.at(0);
    const auto& secondary = profiles.at(1);
    const auto primary_id = wb::endpoint(primary);
    const auto secondary_id = wb::endpoint(secondary);
    const auto secret = inputs.value("first").toString().toUtf8();
    const auto other = inputs.value("second").toString().toUtf8();
    require(!secret.isEmpty() && !other.isEmpty(), "missing test credential");
    CredentialCleanup cleanup{primary};

    wb::Window window(QString{});
    window.show();
    require(window.isVisible() && window.session_count() == 0
        && window.statusBar()->currentMessage() == "Disconnected", "Workbench requires local Server");
    auto* tabs = window.findChild<QTabWidget*>("sessions");
    auto* tasks = window.findChild<QListWidget*>("connectionTasks");
    require(tabs && tasks, "missing connection widgets");

    window.import_from(workspace);
    wait_until([&] { return window.statusBar()->currentMessage() == "Workspace profiles imported"; }, "workspace import failed");
    window.import_from(invalid);
    wait_until([&] { return window.statusBar()->currentMessage() == "Invalid workspace profile file."; },
        "unexpected imported profile fields were accepted");

    QTimer ticker;
    int ticks{};
    QObject::connect(&ticker, &QTimer::timeout, [&] { ++ticks; });
    ticker.start(20);
    window.open_profile(primary, secret, true, false, true);
    window.open_profile(secondary, other);
    wait_until([&] { return window.session_count() == 2; }, "two real TLS connections did not complete");
    require(ticks >= 3, "GUI event loop did not run during connection");
    require(find_tab(*tabs, primary_id) > 0 && find_tab(*tabs, secondary_id) > 0, "endpoint tab mismatch");
    require(tabs->tabText(find_tab(*tabs, primary_id)).contains(primary_id)
        && tabs->tabText(find_tab(*tabs, secondary_id)).contains(secondary_id), "tab endpoint label mismatch");

    window.close_session(find_tab(*tabs, primary_id));
    require(window.session_count() == 1 && find_tab(*tabs, secondary_id) > 0, "closing one tab affected another");
    window.check_session(find_tab(*tabs, secondary_id));
    wait_until([&] { return window.statusBar()->currentMessage() == "Health verified for " + secondary_id; },
        "remaining Server session was not live");

    window.open_profile(primary, {}, true, true, true);
    wait_until([&] { return window.statusBar()->currentMessage() == "Connection test passed for " + primary_id; },
        "credential store roundtrip failed");
    require(window.session_count() == 1, "profile test opened a tab");

    window.export_to(output_path);
    wait_until([&] { return window.statusBar()->currentMessage() == "Workspace profiles exported"; }, "workspace export failed");
    QFile exported(output_path);
    require(exported.open(QIODevice::ReadOnly), "cannot read workspace export");
    const auto output = exported.readAll();
    require(!output.contains(secret) && !output.contains(other) && !output.contains("password"),
        "credential appeared in exported workspace");
    require(wb::import_profiles(output_path, error).size() == 2 && error.isEmpty(), "cannot reimport export");
    for (int index = 0; index < tasks->count(); ++index)
    {
        const auto text = tasks->item(index)->text().toUtf8();
        require(!text.contains(secret) && !text.contains(other), "credential appeared in task output");
    }
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    try
    {
        const auto args = app.arguments();
        require(args.size() == 4, "provide import, export and invalid fixture paths");
        std::string credentials;
        require(bool(std::getline(std::cin, credentials)), "missing test credentials");
        const auto input = QJsonDocument::fromJson(QByteArray::fromStdString(credentials)).object();
        smoke(args.at(1), args.at(2), args.at(3), input);
        std::cout << "TLS tabs, live health, responsive event loop, Credential Manager and redacted export passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
