#include "sql_definition.hpp"

#include "sql_session.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QStringList>

#include <limits>
#include <utility>

namespace wb
{
namespace
{
QString pretty(const QJsonObject& payload)
{
    return QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Indented));
}

QString positiveId(const QJsonValue& value)
{
    const auto text = value.toString();
    bool valid = false;
    const auto id = text.toULongLong(&valid);
    if (!valid || !id || QString::number(id) != text) return {};
    return text;
}

QString qualifiedName(const QJsonArray& components)
{
    if (components.isEmpty() || components.size() > 3) return {};
    QStringList quoted;
    for (const auto& component : components)
    {
        auto name = component.toString();
        if (name.isEmpty() || name.contains(QChar(u'\0'))) return {};
        name.replace(u'"', QStringLiteral("\"\""));
        quoted << u'"' + name + u'"';
    }
    return quoted.join(u'.');
}

QString supportedKind(int kind)
{
    switch (kind)
    {
    case 3: return QStringLiteral("table");
    case 4: return QStringLiteral("index");
    case 5: return QStringLiteral("view");
    case 6: return QStringLiteral("routine");
    default: return {};
    }
}

QString unavailableReason(sdp_status code)
{
    switch (code)
    {
    case SDP_PERMISSION_DENIED:
        return QObject::tr("the server denied History access.");
    case SDP_FEATURE_UNSUPPORTED:
    case SDP_MODULE_UNAVAILABLE:
        return QObject::tr("History is unavailable on this server.");
    case SDP_INVALID_STATE:
        return QObject::tr("the revision or session state changed. Finish the transaction and refresh the object explorer.");
    case SDP_TIMEOUT:
        return QObject::tr("the History request timed out.");
    case SDP_CANCELLED:
        return QObject::tr("the History request was cancelled.");
    case SDP_CONNECTION_LOST:
        return QObject::tr("the connection was closed.");
    default:
        return QObject::tr("the History request returned error %1.").arg(code);
    }
}

QString definitionText(const QJsonObject& object, const QString& kind)
{
    const auto definition = object.value(kind == u"routine" ? QStringLiteral("source") :
        QStringLiteral("definition")).toString();
    if (definition.isEmpty()) return {};
    if (kind == u"routine") return definition;
    const auto json = QJsonDocument::fromJson(definition.toUtf8());
    return json.isNull() ? definition : QString::fromUtf8(json.toJson(QJsonDocument::Indented));
}
}

void readDefinition(SqlSession& session, QJsonObject liveMetadata, std::function<void(SqlDefinition)> finish)
{
    if (!finish) return;
    const auto objectId = positiveId(liveMetadata.value("id"));
    const auto name = liveMetadata.value("qualified_name").toArray();
    const auto target = qualifiedName(name);
    const auto schemaVersion = liveMetadata.value("schema_version").toInteger(-1);
    const auto kind = supportedKind(liveMetadata.value("kind").toInt());

    if (objectId.isEmpty() || kind.isEmpty())
    {
        finish({{}, {}, QObject::tr("refresh the object explorer to read its identity and schema version.")});
        return;
    }
    const auto unavailable = [finish](const QString& reason) {
        finish({{}, {}, reason});
    };
    if (target.isEmpty() || schemaVersion <= 0 || schemaVersion > std::numeric_limits<quint32>::max())
    {
        unavailable(QObject::tr("refresh the object explorer to read its identity and schema version."));
        return;
    }
    if (!session.capabilities().value("history_v1").toBool())
    {
        unavailable(QObject::tr("History is unavailable on this server."));
        return;
    }
    if (session.state().transactionActive)
    {
        unavailable(QObject::tr("finish the open transaction before reading a committed definition."));
        return;
    }

    const auto busy = QObject::tr("another operation is active in this worksheet.");
    QPointer<SqlSession> current(&session);
    const auto status = [](sdp_connection connection, sdp_buffer* output, sdp_error* error) {
        return sdp_history_command(connection, SDP_HISTORY_STATUS, {}, output, error);
    };
    if (!session.request(status, [current, objectId, name, target, schemaVersion, kind, finish, unavailable,
            busy](const SqlReply& statusReply) {
        if (!current) return;
        if (statusReply.error.code != SDP_OK)
        {
            unavailable(unavailableReason(statusReply.error.code));
            return;
        }
        const auto revision = positiveId(statusReply.data.value("current_revision"));
        if (revision.isEmpty())
        {
            unavailable(QObject::tr("the server did not return a recoverable current revision."));
            return;
        }
        const auto body = QJsonDocument(QJsonObject{{"target", target}, {"revision", revision}, {"limit", 1}})
            .toJson(QJsonDocument::Compact);
        const auto read = [body](sdp_connection connection, sdp_buffer* output, sdp_error* error) {
            const sdp_text json{body.constData(), static_cast<uint32_t>(body.size())};
            return sdp_history_command(connection, SDP_HISTORY_READ, json, output, error);
        };
        if (!current->request(read, [objectId, name, schemaVersion, kind, finish, unavailable,
                revision](const SqlReply& readReply) {
            if (readReply.error.code != SDP_OK)
            {
                unavailable(unavailableReason(readReply.error.code));
                return;
            }
            const auto object = readReply.data.value("object").toObject();
            if (positiveId(readReply.data.value("object_id")) != objectId ||
                positiveId(readReply.data.value("revision")) != revision ||
                readReply.data.value("name").toArray() != name || object.value("kind").toString() != kind ||
                object.value("schema_version").toInteger(-1) != schemaVersion)
            {
                unavailable(QObject::tr("the object identity or schema version changed. Refresh the object explorer."));
                return;
            }
            const auto definition = definitionText(object, kind);
            if (definition.isEmpty())
            {
                unavailable(QObject::tr("the server did not return a definition. Reading it requires DDL access."));
                return;
            }
            finish({definition, revision, {}});
        })) unavailable(busy);
    })) unavailable(busy);
}

void inspectDefinition(SqlSession& session, QJsonObject liveMetadata, std::function<void(QString)> render)
{
    if (!render || liveMetadata.isEmpty()) return;
    const auto metadata = pretty(liveMetadata);
    if (positiveId(liveMetadata.value("id")).isEmpty() || supportedKind(liveMetadata.value("kind").toInt()).isEmpty())
    {
        render(metadata);
        return;
    }
    readDefinition(session, std::move(liveMetadata), [metadata, render = std::move(render)](SqlDefinition definition) {
        if (!definition.error.isEmpty())
            render(metadata + QObject::tr("\nDefinition unavailable. %1").arg(definition.error));
        else render(metadata + QObject::tr("\nDefinition at revision %1\n").arg(definition.revision) + definition.text);
    });
}
}
