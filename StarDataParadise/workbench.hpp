#pragma once

#include "profiles.hpp"

#include <QMainWindow>
#include <QThreadPool>

#include <map>

class QLabel;
class QListWidget;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace wb
{
class SqlPane;
class Window : public QMainWindow
{
public:
    explicit Window(QString settings_file);
    ~Window() override;

    void open_profile(Profile profile, QByteArray password, bool remember = false,
        bool test_only = false, bool session_credential = false);
    void check_session(int tab);
    void close_session(int tab);
    void import_from(QString path);
    void export_to(QString path);
    int session_count() const noexcept { return static_cast<int>(sessions_.size()); }

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct Tab
    {
        SqlPane* pane;
        QTreeWidgetItem* node;
        QLabel* state;
        bool checking{};
    };

    void edit_profiles(bool local);
    void search_objects();
    void compare_sessions();
    void save_profile(Profile profile);
    void write_settings();
    void loaded(QVector<Profile> profiles);
    void add_tab(const Profile& profile, std::shared_ptr<Session> session);
    void note(const QString& text);

    QString settings_file_;
    QWidget* first_run_{};
    QTabWidget* tabs_{};
    QTreeWidget* objects_{};
    QListWidget* tasks_{};
    QVector<Profile> profiles_;
    std::map<QWidget*, Tab> sessions_;
    QThreadPool workers_;
    QThreadPool storage_;
};
}
