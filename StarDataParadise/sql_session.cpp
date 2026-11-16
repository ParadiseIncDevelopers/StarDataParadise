#include "sql_session.hpp"
#include "sql_script.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QStringList>
#include <QThreadPool>
#include <QUuid>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <iterator>
#include <new>
#include <optional>

namespace wb
{
namespace
{
constexpr qsizetype maxOutputBytes = 8 * 1024 * 1024;
constexpr qsizetype maxInputBytes = 1024 * 1024;
constexpr int maxStatements = 256;

sdp_text wire(const QByteArray& bytes)
{
    return {bytes.constData(), static_cast<uint32_t>(bytes.size())};
}

SqlReply failed(sdp_status code, const char* message = nullptr)
{
    SqlReply result;
    result.error.code = code;
    const auto detail = message ? QByteArray(message) : status(code).toUtf8();
    std::memcpy(result.error.message, detail.constData(),
        static_cast<size_t>(std::min<qsizetype>(detail.size(), sizeof(result.error.message) - 1)));
    return result;
}

struct Buffer
{
    sdp_buffer value{};
    ~Buffer() { sdp_buffer_free(&value); }
    QByteArray bytes() const
    {
        return QByteArray::fromRawData(reinterpret_cast<const char*>(value.data), value.size);
    }
    QJsonDocument document() const { return QJsonDocument::fromJson(bytes()); }
};

QByteArray token()
{
    return QUuid::createUuid().toRfc4122().toHex();
}

bool lost(sdp_status code)
{
    return code == SDP_CANCELLED || code == SDP_TIMEOUT || code == SDP_CONNECTION_LOST ||
        code == SDP_TLS_FAILED || code == SDP_MALFORMED_MESSAGE || code == SDP_INCOMPATIBLE_PROTOCOL;
}

struct Intent
{
    enum Kind { invalid, read, write, routine, specialRead, specialWrite, control };
    Kind kind = invalid;
    std::optional<SqlTransaction> action;
    QString argument;
    QStringList parameters;
    bool requiresAutocommit = false;
};

Intent inspect(QStringView source)
{
    auto state = SqlLexState::normal;
    const auto tokens = sql_tokens(source, state);
    if (state != SqlLexState::normal) return {};

    Intent result;
    QStringList words;
    bool simple = true;
    for (qsizetype i = 0; i < tokens.size(); ++i)
    {
        const auto entry = tokens[i];
        const auto value = source.mid(entry.offset, entry.length);
        if (entry.kind == SqlTokenKind::whitespace || entry.kind == SqlTokenKind::comment) continue;
        if (entry.kind == SqlTokenKind::word) words.push_back(value.toString());
        else if (value != u";") simple = false;
        if (entry.kind != SqlTokenKind::symbol || value != u":" || i + 1 >= tokens.size()) continue;
        const auto next = tokens[i + 1];
        if (next.kind != SqlTokenKind::word || next.offset != entry.offset + 1) continue;
        const auto name = source.mid(next.offset, next.length).toString();
        if (!result.parameters.contains(name)) result.parameters.push_back(name);
    }
    if (words.isEmpty()) return {};
    const auto first = words[0].toUpper();
    const auto second = words.value(1).toUpper();
    const auto third = words.value(2).toUpper();
    const bool beginTx = first == u"BEGIN" && (second.isEmpty() || second == u"WORK" || second == u"TRANSACTION");
    if (beginTx || first == u"COMMIT" || first == u"ROLLBACK" || first == u"SAVEPOINT" || first == u"RELEASE")
    {
        result.kind = Intent::control;
        if (!simple) return result;
        if (beginTx && words.size() <= 2) result.action = SqlTransaction::begin;
        else if ((first == u"COMMIT" || first == u"ROLLBACK") &&
            (words.size() == 1 || (words.size() == 2 && second == u"WORK")))
            result.action = first == u"COMMIT" ? SqlTransaction::commit : SqlTransaction::rollback;
        else if (first == u"SAVEPOINT" && words.size() == 2)
            result.action = SqlTransaction::savepoint;
        else if (first == u"RELEASE" && words.size() == 3 && second == u"SAVEPOINT")
            result.action = SqlTransaction::release;
        else if (first == u"ROLLBACK" && second == u"TO" &&
            ((words.size() == 3 && third != u"SAVEPOINT") || (words.size() == 4 && third == u"SAVEPOINT")))
            result.action = SqlTransaction::rollbackTo;
        if (result.action) result.argument = words.back();
        return result;
    }

    if (first == u"SELECT" || first == u"WITH" || first == u"EXPLAIN") result.kind = Intent::read;
    else if (first == u"CALL" || first == u"DECLARE" || first == u"BEGIN") result.kind = Intent::routine;
    else if (first == u"HISTORY")
    {
        const bool apply = (second == u"RESTORE" || second == u"REVERT") && third == u"APPLY";
        const bool mutate = apply || second == u"RETAIN" || second == u"PURGE" || second == u"PIN" || second == u"UNPIN";
        result.kind = mutate ? Intent::specialWrite : Intent::specialRead;
    }
    else if (first == u"TRAIN" || ((first == u"CREATE" || first == u"ALTER") && second == u"MODEL"))
        result.kind = Intent::specialWrite;
    else if (first == u"DECISION" || first == u"EVALUATE" || first == u"PREDICT")
        result.kind = Intent::specialRead;
    else
    {
        result.kind = Intent::write;
        result.requiresAutocommit = first == u"GRANT" || first == u"REVOKE" ||
            (first == u"CREATE" && (second == u"USER" || second == u"ROLE"));
    }
    return result;
}

bool mayWrite(Intent::Kind kind)
{
    return kind == Intent::write || kind == Intent::routine || kind == Intent::specialWrite;
}
}

bool statementMayWrite(QStringView source)
{
    const auto intent = inspect(source);
    return mayWrite(intent.kind) || intent.action == SqlTransaction::commit;
}

struct SqlSession::Job
{
    std::shared_ptr<Session> connection;
    SqlSessionState state;
    QVector<std::pair<QString, bool>> savepoints;
    std::atomic<sdp_status> stopCode{SDP_OK};
    qsizetype remainingBytes = maxOutputBytes;

    sdp_connection handle() const noexcept { return connection ? connection->handle : 0; }

    void clearTransaction()
    {
        state.transactionActive = false;
        state.pendingChanges = false;
        state.transactionStarted = {};
        savepoints.clear();
    }

    void disconnect()
    {
        if (state.connected && handle()) sdp_close(handle(), nullptr);
        state.connected = false;
        state.identity = {};
        clearTransaction();
    }

    template<class Operation>
    bool call(SqlReply& reply, Operation&& operation, bool cancellable = true)
    {
        const auto requested = stopCode.load();
        if ((cancellable && requested != SDP_OK) || !state.connected || !handle())
        {
            reply.error = failed(cancellable && requested != SDP_OK ? requested : SDP_CONNECTION_LOST).error;
            return false;
        }
        reply.error = {};
        reply.error.code = operation(handle(), &reply.error);
        if (lost(reply.error.code) || reply.error.unknown_outcome)
        {
            const auto stop = stopCode.load();
            if (stop != SDP_OK && (reply.error.code == SDP_CANCELLED || reply.error.code == SDP_CONNECTION_LOST))
            {
                const auto original = reply.error;
                reply.error = failed(stop).error;
                reply.error.unknown_outcome = original.unknown_outcome;
            }
            disconnect();
        }
        return reply.error.code == SDP_OK;
    }

    SqlReply request(const NativeRequest& operation)
    {
        SqlReply reply;
        Buffer buffer;
        if (!call(reply, [&](auto connection, auto* error) { return operation(connection, &buffer.value, error); }))
            return reply;
        if (!buffer.value.size) return reply;
        const auto content = buffer.document();
        if (!content.isObject())
        {
            disconnect();
            return failed(SDP_MALFORMED_MESSAGE, "Invalid server response");
        }
        reply.data = content.object();
        return reply;
    }

    SqlReply transaction(SqlTransaction action, const QString& argument = {}, bool cancellable = true)
    {
        SqlReply reply;
        if (action == SqlTransaction::autocommitOn || action == SqlTransaction::autocommitOff)
        {
            if (state.transactionActive)
                return failed(SDP_INVALID_STATE, "Finish the active transaction before changing autocommit");
            state.autocommit = action == SqlTransaction::autocommitOn;
            return reply;
        }
        if (action == SqlTransaction::begin)
        {
            if (state.transactionActive) return failed(SDP_INVALID_STATE, "A transaction is already active");
            if (call(reply, sdp_tx_begin, cancellable))
            {
                state.transactionActive = true;
                state.transactionStarted = QDateTime::currentDateTimeUtc();
            }
            return reply;
        }
        if (!state.transactionActive) return failed(SDP_INVALID_STATE, "No active transaction");
        if (cancellable && stopCode.load() != SDP_OK) return failed(stopCode.load());
        if (action == SqlTransaction::commit)
        {
            const auto id = token();
            Buffer buffer;
            if (call(reply, [&](auto connection, auto* error) {
                return sdp_tx_commit(connection, wire(id), &buffer.value, error);
            }, false))
                reply.data = buffer.document().object();
            if (reply.error.unknown_outcome) reply.requestToken = QString::fromLatin1(id);
            clearTransaction();
            return reply;
        }
        if (action == SqlTransaction::rollback)
        {
            call(reply, sdp_tx_rollback, false);
            clearTransaction();
            return reply;
        }

        const auto name = argument.toUtf8();
        const auto op = action == SqlTransaction::savepoint ? sdp_tx_savepoint :
            action == SqlTransaction::rollbackTo ? sdp_tx_rollback_to : sdp_tx_release;
        if (!call(reply, [&](auto connection, auto* error) { return op(connection, wire(name), error); }, cancellable))
            return reply;
        if (action == SqlTransaction::savepoint) savepoints.push_back({argument, state.pendingChanges});
        else if (const auto saved = std::find_if(savepoints.begin(), savepoints.end(),
            [&](const auto& item) { return item.first == argument; }); saved != savepoints.end())
        {
            if (action == SqlTransaction::rollbackTo)
            {
                state.pendingChanges = saved->second;
                savepoints.erase(std::next(saved), savepoints.end());
            }
            else savepoints.erase(saved, savepoints.end());
        }
        return reply;
    }

    bool output(SqlReply& reply, SqlEvent::Kind kind, const Buffer& buffer, const QJsonObject& data, const Stream& stream)
    {
        if (buffer.value.size > remainingBytes)
        {
            reply.error = failed(SDP_RESOURCE_LIMIT, "The result exceeded the Workbench output budget").error;
            return false;
        }
        remainingBytes -= buffer.value.size;
        if (stream) stream({kind, data, reply.statementIndex});
        return true;
    }

    void fetch(SqlReply& reply, const QByteArray& result, int rowLimit, const Stream& stream)
    {
        int left = rowLimit;
        bool ended = false;
        while (left > 0)
        {
            Buffer page;
            const auto count = static_cast<uint32_t>(std::min(left, 64));
            if (!call(reply, [&](auto connection, auto* error) {
                return sdp_fetch(connection, wire(result), count, &page.value, error);
            })) break;
            const auto batch = page.document().object().value("batch").toObject();
            const auto rows = batch.value("rows");
            const auto end = batch.value("end_of_results");
            if (!rows.isArray() || !end.isBool())
            {
                reply.error = failed(SDP_MALFORMED_MESSAGE, "Invalid result batch").error;
                disconnect();
                return;
            }
            ended = end.toBool();
            left -= static_cast<int>(rows.toArray().size());
            if (!output(reply, SqlEvent::Kind::batch, page, batch, stream) || ended) break;
        }
        if (!ended && state.connected)
        {
            SqlReply cleanup;
            if (!call(cleanup, [&](auto connection, auto* error) {
                return sdp_result_cancel(connection, wire(result), error);
            }, false) && cleanup.error.code != SDP_INVALID_STATE) disconnect();
            if (!state.connected && reply.error.code == SDP_OK) reply.error = cleanup.error;
        }
        if (left == 0 && !ended && reply.error.code == SDP_OK) reply.resultsTruncated = true;
    }

    SqlReply statement(const SqlStatement& statement, int index, const Intent& intent,
        const QJsonObject& definitions, const QJsonObject& values, int rowLimit, const Stream& stream)
    {
        const auto sql = statement.text.toUtf8();
        QJsonObject typed, parameters;
        for (const auto& name : intent.parameters)
        {
            // PL/SQL also uses colon notation for lexically scoped values in embedded SQL.
            // The server resolves unbound names within a routine at prepare time.
            if (!definitions.contains(name) && !values.contains(name)) continue;
            if (!definitions.value(name).isObject() || !values.value(name).isObject())
                return failed(SDP_INVALID_ARGUMENT, "A named parameter is missing its declaration or value");
            typed.insert(name, definitions.value(name));
            parameters.insert(name, values.value(name));
        }
        const auto typesBytes = QJsonDocument(typed).toJson(QJsonDocument::Compact);
        const auto valueBytes = QJsonDocument(parameters).toJson(QJsonDocument::Compact);
        SqlReply reply;
        reply.statementIndex = index;
        Buffer prepared;
        if (!call(reply, [&](auto connection, auto* error) {
            return sdp_prepare_typed(connection, wire(sql), wire(typesBytes), &prepared.value, error);
        })) return reply;

        const auto id = prepared.bytes();
        const auto writeToken = !state.transactionActive && intent.kind == Intent::write ? token() : QByteArray{};
        Buffer executed;
        const bool transactionWasActive = state.transactionActive;
        if (call(reply, [&](auto connection, auto* error) {
            return sdp_execute(connection, wire(id), wire(valueBytes), wire(writeToken),
                static_cast<uint32_t>(rowLimit), &executed.value, error);
        }))
        {
            if (state.transactionActive && mayWrite(intent.kind)) state.pendingChanges = true;
            const auto response = executed.document();
            if (!response.isObject())
            {
                reply.error = failed(SDP_MALFORMED_MESSAGE, "Invalid query response").error;
                if (!writeToken.isEmpty())
                {
                    reply.error.unknown_outcome = 1;
                    reply.requestToken = QString::fromLatin1(writeToken);
                }
                disconnect();
            }
            else
            {
                reply.data = response.object();
                const auto resultId = reply.data.value("result_id").toString().toUtf8();
                if (output(reply, SqlEvent::Kind::statement, executed, reply.data, stream))
                {
                    if (!resultId.isEmpty()) fetch(reply, resultId, rowLimit, stream);
                }
                else if (!resultId.isEmpty())
                {
                    SqlReply cleanup;
                    if (!call(cleanup, [&](auto connection, auto* error) {
                        return sdp_result_cancel(connection, wire(resultId), error);
                    }, false)) disconnect();
                }
            }
        }
        else
        {
            if (!writeToken.isEmpty() && (reply.error.unknown_outcome || !state.connected))
            {
                reply.error.unknown_outcome = 1;
                reply.requestToken = QString::fromLatin1(writeToken);
            }
            if (intent.kind == Intent::specialWrite && !state.connected) reply.error.unknown_outcome = 1;
        }

        if (state.connected)
        {
            SqlReply closed;
            if (!call(closed, [&](auto connection, auto* error) {
                return sdp_statement_close(connection, wire(id), error);
            }, false))
            {
                disconnect();
                if (transactionWasActive && reply.error.code == SDP_OK)
                    reply.error = closed.error;
            }
        }
        return reply;
    }

    SqlReply run(const QVector<SqlStatement>& script, const QJsonObject& definitions, const QJsonObject& values,
        int rowLimit, bool readOnly, const Stream& stream)
    {
        if (script.isEmpty() || script.size() > maxStatements || rowLimit < 1 || rowLimit > 1'000'000)
            return failed(SDP_INVALID_ARGUMENT, "Invalid statement count or result limit");
        SqlReply reply;
        qsizetype inputBytes = 0;
        bool truncated = false;
        for (int index = 0; index < script.size(); ++index)
        {
            const auto& current = script.at(index);
            const auto intent = inspect(current.text);
            inputBytes += current.text.toUtf8().size();
            if (inputBytes > maxInputBytes)
                reply = failed(SDP_RESOURCE_LIMIT, "The script exceeds the Workbench input limit");
            else if (intent.kind == Intent::invalid || (intent.kind == Intent::control && !intent.action))
                reply = failed(SDP_INVALID_ARGUMENT, "Invalid SQL or transaction command");
            else if (readOnly && (mayWrite(intent.kind) || intent.action == SqlTransaction::commit))
                reply = failed(SDP_PERMISSION_DENIED, "This profile is set to read-only");
            else if (intent.action)
                reply = transaction(*intent.action, intent.argument);
            else
            {
                const bool special = intent.kind == Intent::specialRead || intent.kind == Intent::specialWrite;
                const bool owned = intent.kind == Intent::routine && state.autocommit && !state.transactionActive;
                if (state.transactionActive && (intent.requiresAutocommit || special))
                    reply = failed(SDP_INVALID_STATE, "Complete the active transaction before this command");
                else
                {
                    reply = {};
                    if ((!state.autocommit || owned) && !state.transactionActive && !special && !intent.requiresAutocommit)
                        reply = transaction(SqlTransaction::begin);
                    if (!reply.error.code) reply = statement(current, index, intent, definitions, values, rowLimit, stream);
                    if (owned && state.transactionActive)
                    {
                        if (reply.error.code == SDP_OK)
                        {
                            auto committed = transaction(SqlTransaction::commit);
                            if (committed.error.code != SDP_OK && state.transactionActive)
                                transaction(SqlTransaction::rollback, {}, false);
                            if (committed.error.code != SDP_OK) reply = std::move(committed);
                        }
                        else transaction(SqlTransaction::rollback, {}, false);
                    }
                }
            }
            truncated = truncated || reply.resultsTruncated;
            reply.statementIndex = index;
            if (reply.error.code != SDP_OK) break;
        }
        reply.resultsTruncated = truncated;
        return reply;
    }
};

SqlSession::SqlSession(Profile profile, std::shared_ptr<Session> session, QThreadPool& workers, QObject* parent)
    : QObject(parent), profile_(std::move(profile)), connection_(std::move(session)),
      capabilities_(connection_ ? connection_->capabilities : QJsonObject{}), workers_(workers)
{
    state_.connected = connection_ && connection_->handle;
    deadline_.setSingleShot(true);
    cancellation_.setInterval(25);
    connect(&deadline_, &QTimer::timeout, this, [this] { stop(SDP_TIMEOUT); });
    connect(&cancellation_, &QTimer::timeout, this, [this] {
        if (job_ && job_->handle()) sdp_cancel(job_->handle(), nullptr);
    });
}

SqlSession::~SqlSession()
{
    cancel();
}

void SqlSession::signalStateChanged()
{
    const auto callback = changed_;
    if (callback) callback();
}

void SqlSession::stop(sdp_status reason)
{
    if (!job_) return;
    sdp_status expected = SDP_OK;
    if (!job_->stopCode.compare_exchange_strong(expected, reason)) return;
    deadline_.stop();
    if (job_->handle()) sdp_cancel(job_->handle(), nullptr);
    cancellation_.start();
}

void SqlSession::cancel()
{
    stop(SDP_CANCELLED);
}

bool SqlSession::start(int timeLimitMs, Work work, Stream stream, Finished finished)
{
    if (busy() || timeLimitMs < 100 || timeLimitMs > 120'000) return false;
    auto job = std::make_shared<Job>();
    job->connection = connection_;
    job->state = state_;
    job->savepoints = savepoints_;
    job_ = job;
    deadline_.start(timeLimitMs);
    QPointer<SqlSession> target(this);
    const auto app = QCoreApplication::instance();
    workers_.start([job, target, app, work = std::move(work), stream = std::move(stream), finished = std::move(finished)] {
        QElapsedTimer timer;
        timer.start();
        Stream dispatch;
        if (stream) dispatch = [target, app, stream](const SqlEvent& event) {
            QMetaObject::invokeMethod(app, [target, stream, event] {
                if (target) stream(event);
            }, Qt::QueuedConnection);
        };
        SqlReply reply;
        try { reply = work(*job, dispatch); }
        catch (const std::bad_alloc&)
        {
            job->disconnect();
            reply = failed(SDP_OUT_OF_MEMORY, "Workbench could not allocate the result");
        }
        catch (...)
        {
            job->disconnect();
            reply = failed(SDP_INTERNAL_ERROR, "The Workbench worker failed");
        }
        reply.elapsedMs = timer.elapsed();
        if (reply.error.unknown_outcome && !reply.requestToken.isEmpty())
            job->state.unknownRequestToken = reply.requestToken;
        QMetaObject::invokeMethod(app, [job, target, reply = std::move(reply), finished] {
            if (!target) return;
            target->deadline_.stop();
            target->cancellation_.stop();
            target->state_ = std::move(job->state);
            target->savepoints_ = std::move(job->savepoints);
            target->job_.reset();
            target->signalStateChanged();
            if (target && finished) finished(reply);
        }, Qt::QueuedConnection);
    });
    signalStateChanged();
    return true;
}

bool SqlSession::execute(QVector<SqlStatement> script, QJsonObject parameterTypes, QJsonObject parameters,
    SqlRunOptions options, Stream stream, Finished finished)
{
    return start(options.timeLimitMs, [script = std::move(script), defs = std::move(parameterTypes),
        params = std::move(parameters), options, readOnly = profile_.read_only](Job& job, const Stream& sink) {
        return job.run(script, defs, params, options.rowLimit, readOnly, sink);
    }, std::move(stream), std::move(finished));
}

bool SqlSession::request(NativeRequest operation, Finished finished, int timeLimitMs)
{
    return start(timeLimitMs, [operation = std::move(operation)](Job& job, const Stream&) {
        return job.request(operation);
    }, {}, std::move(finished));
}

bool SqlSession::refreshIdentity(Finished finished)
{
    return start(5000, [](Job& job, const Stream&) {
        auto reply = job.request(sdp_get_session_info);
        if (reply.error.code == SDP_OK) job.state.identity = reply.data;
        return reply;
    }, {}, std::move(finished));
}

bool SqlSession::refreshMetadata(Finished finished)
{
    return request(sdp_metadata, std::move(finished));
}

bool SqlSession::health(Finished finished)
{
    return request([](auto connection, auto*, auto* error) { return sdp_health(connection, error); },
        std::move(finished));
}

bool SqlSession::transaction(SqlTransaction action, QString argument, Finished finished)
{
    return start(5000, [action, argument = std::move(argument), readOnly = profile_.read_only](Job& job, const Stream&) {
        if (readOnly && action == SqlTransaction::commit && job.state.pendingChanges)
            return failed(SDP_PERMISSION_DENIED, "This profile is set to read-only");
        return job.transaction(action, argument);
    }, {}, std::move(finished));
}

bool SqlSession::lookupOutcome(QString requestToken, Finished finished)
{
    return start(5000, [requestToken = std::move(requestToken)](Job& job, const Stream&) {
        const auto id = requestToken.toLatin1();
        auto reply = job.request([&](auto connection, auto* output, auto* error) {
            return sdp_tx_outcome(connection, wire(id), output, error);
        });
        if (reply.error.code == SDP_OK && reply.data.value("status").toString() == u"committed" &&
            job.state.unknownRequestToken == requestToken)
            job.state.unknownRequestToken.clear();
        return reply;
    }, {}, std::move(finished));
}

bool SqlSession::debugOpen(QString targetSession, QString routine, quint32 revision, Finished finished)
{
    return start(5000, [target = targetSession.toUtf8(), name = routine.toUtf8(), revision](Job& job, const Stream&) {
        SqlReply reply;
        Buffer output;
        if (job.call(reply, [&](auto connection, auto* error) {
            return sdp_debug_open(connection, wire(target), wire(name), revision, &output.value, error);
        }))
            reply.data.insert("debug_id", QString::fromUtf8(output.bytes()));
        return reply;
    }, {}, std::move(finished));
}

bool SqlSession::reconnect(std::shared_ptr<Session> connection, Finished finished)
{
    if (busy() || state_.transactionActive || !connection || !connection->handle) return false;
    connection_ = std::move(connection);
    capabilities_ = connection_->capabilities;
    state_.connected = true;
    state_.identity = {};
    return refreshIdentity(std::move(finished));
}
}
