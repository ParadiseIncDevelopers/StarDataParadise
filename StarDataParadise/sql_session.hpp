#pragma once

#include "profiles.hpp"

#include <QDateTime>
#include <QObject>
#include <QTimer>

#include <functional>
#include <utility>

class QThreadPool;

namespace wb
{
struct SqlStatement
{
    QString text;
    int firstLine = 1;
};

struct SqlRunOptions
{
    int rowLimit = 1000;
    int timeLimitMs = 5000;
};

enum class SqlTransaction { autocommitOn, autocommitOff, begin, commit, rollback, savepoint, rollbackTo, release };

struct SqlEvent
{
    enum class Kind { statement, batch };
    Kind kind;
    QJsonObject data;
    int statementIndex = 0;
};

struct SqlReply
{
    sdp_error error{};
    QJsonObject data;
    QString requestToken;
    int statementIndex = 0;
    qint64 elapsedMs = 0;
    bool resultsTruncated = false;
};

struct SqlSessionState
{
    bool connected = false;
    bool autocommit = true;
    bool transactionActive = false;
    bool pendingChanges = false;
    QDateTime transactionStarted;
    QJsonObject identity;
    QString unknownRequestToken;
};

bool statementMayWrite(QStringView statement);

class SqlSession : public QObject
{
public:
    using Stream = std::function<void(const SqlEvent&)>;
    using Finished = std::function<void(const SqlReply&)>;
    using NativeRequest = std::function<sdp_status(sdp_connection, sdp_buffer*, sdp_error*)>;

    SqlSession(Profile, std::shared_ptr<Session>, QThreadPool&, QObject* parent = nullptr);
    ~SqlSession() override;

    const Profile& profile() const noexcept { return profile_; }
    const SqlSessionState& state() const noexcept { return state_; }
    const QJsonObject& capabilities() const noexcept { return capabilities_; }
    bool busy() const noexcept { return bool(job_); }
    void setStateChanged(std::function<void()> changed) { changed_ = std::move(changed); }

    bool execute(QVector<SqlStatement>, QJsonObject parameterTypes, QJsonObject parameters,
        SqlRunOptions, Stream, Finished);
    bool refreshMetadata(Finished);
    bool refreshIdentity(Finished);
    bool health(Finished);
    bool transaction(SqlTransaction, QString argument, Finished);
    bool lookupOutcome(QString token, Finished);
    bool reconnect(std::shared_ptr<Session>, Finished);
    bool debugOpen(QString targetSession, QString routine, quint32 revision, Finished);
    bool request(NativeRequest, Finished, int timeLimitMs = 5000);
    void cancel();

private:
    struct Job;
    using Work = std::function<SqlReply(Job&, const Stream&)>;
    bool start(int timeLimitMs, Work, Stream, Finished);
    void stop(sdp_status);
    void signalStateChanged();

    Profile profile_;
    std::shared_ptr<Session> connection_;
    QJsonObject capabilities_;
    QThreadPool& workers_;
    SqlSessionState state_;
    QVector<std::pair<QString, bool>> savepoints_;
    std::shared_ptr<Job> job_;
    std::function<void()> changed_;
    QTimer deadline_;
    QTimer cancellation_;
};
}
