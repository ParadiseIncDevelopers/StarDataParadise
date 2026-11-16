#pragma once

#include "profiles.hpp"
#include "sql_session.hpp"

#include <QWidget>
#include <QHash>
#include <QJsonArray>
#include <functional>

class QLabel;
class QPlainTextEdit;
class QSpinBox;
class QTabWidget;
class QPushButton;
class QCheckBox;
class QTimer;

namespace wb
{
class SqlEditor;
class SqlTable;
enum class SqlScope;
struct SqlCommand;

class SqlPane : public QWidget
{
public:
    SqlPane(Profile profile, std::shared_ptr<Session> session, QThreadPool& workers,
        QString draftFile, QWidget* parent = nullptr);
    ~SqlPane() override;

    const Profile& profile() const { return session_.profile(); }
    SqlSession& session() { return session_; }
    bool allowClose();
    void health(SqlSession::Finished done);
    void refreshObjects();
    void setMetadataHandler(std::function<void(const QJsonArray&)> handler);
    void peek(const QJsonObject& item);
    void designTable(const QJsonObject& table = {});
    void showDictionary();
    void showGrid(const QJsonObject& table);
    void transferTable(const QJsonObject& table);
    void openRoutineStudio(const QJsonObject& routine = {});
    void exportRoutine(const QJsonObject& routine);
    void openHistoryStudio(const QJsonObject& object = {});
    void openDecisionStudio(const QJsonObject& object = {});
    void openMlStudio(const QJsonObject& model = {});
    void openDatasetStudio(const QJsonObject& table = {});
    void openJobCenter(const QString& jobId = {}, const QString& model = {});
    void openAdminCenter();
    void openBackupCenter();
    void openSecurityCenter();
    void saveDraft();

private:
    void run(SqlScope scope, const QString& prefix = {}, bool advance = false);
    void onSqlEvent(const SqlEvent& event);
    void action(SqlTransaction control, bool named = false);
    void refreshContext();
    void displayReply(const SqlReply& reply, const QList<SqlCommand>& commands);
    void restoreDraft();
    void showOutcome();
    void showDebug();
    void openScriptProject();

    SqlSession session_;
    QThreadPool& workers_;
    QString draftFile_;
    QString draftKey_;
    QString uncertainToken_;
    QString file_;
    SqlEditor* editor_{};
    QPlainTextEdit* params_{};
    QLabel* state_{};
    QLabel* context_{};
    QSpinBox* rowLimit_{};
    QSpinBox* timeout_{};
    QTabWidget* results_{};
    QPlainTextEdit* details_{};
    QCheckBox* autocommit_{};
    QPushButton* cancel_{};
    QPushButton* restore_{};
    QTimer* draftTimer_{};
    bool running_{};
    bool draftDirty_{};
    int statementIndex_ = -1;
    QHash<int, SqlTable*> tables_;
    QJsonArray catalog_;
    quint64 definitionRequest_{};
    std::function<void(const QJsonArray&)> metadataHandler_;
};
}