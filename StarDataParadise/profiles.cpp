#include "profiles.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryFile>

#include <windows.h>
#include <wincred.h>

#include <chrono>
#include <utility>

namespace wb
{
namespace
{
constexpr auto profile_limit = 64 * 1024;
constexpr auto tls_file_limit = 1024 * 1024;

QJsonObject serialized(const Profile& profile)
{
    return {{"name", profile.name}, {"host", profile.host}, {"port", profile.port},
        {"database", profile.database}, {"user", profile.user}, {"tls_name", profile.tls_name},
        {"ca_file", profile.ca_file}, {"environment", profile.environment}, {"read_only", profile.read_only}};
}

QByteArray ca_bytes(const Profile& profile)
{
    QFile ca(profile.ca_file);
    if (!ca.open(QIODevice::ReadOnly) || ca.size() < 1 || ca.size() > tls_file_limit) return {};
    auto content = ca.read(tls_file_limit + 1);
    if (content.size() > tls_file_limit || !ca.atEnd() || ca.error() != QFileDevice::NoError) return {};
    return content;
}

std::wstring credential_target(const Profile& p, const QByteArray& ca)
{
    const QJsonArray parameters{p.name, p.host, p.port, p.user, p.tls_name,
        QString::fromLatin1(QCryptographicHash::hash(ca, QCryptographicHash::Sha256).toHex())};
    const auto key = QCryptographicHash::hash(QJsonDocument(parameters).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256).toHex();
    return (QStringLiteral("StarDataParadise/") + QString::fromLatin1(key)).toStdWString();
}

bool read_credential(const std::wstring& name, Password& output)
{
    PCREDENTIALW raw{};
    if (!CredReadW(name.c_str(), CRED_TYPE_GENERIC, 0, &raw)) return false;
    const auto release = [](CREDENTIALW* value) {
        if (value->CredentialBlob) SecureZeroMemory(value->CredentialBlob, value->CredentialBlobSize);
        CredFree(value);
    };
    std::unique_ptr<CREDENTIALW, decltype(release)> item(raw, release);
    if (item->CredentialBlobSize < 1 || item->CredentialBlobSize > 1024 || !item->CredentialBlob) return false;
    output.bytes = QByteArray(reinterpret_cast<const char*>(item->CredentialBlob), item->CredentialBlobSize);
    return true;
}

bool write_credential(std::wstring name, const Profile& profile, Password& password, bool session_only)
{
    auto username = profile.user.toStdWString();
    CREDENTIALW record{};
    record.Type = CRED_TYPE_GENERIC;
    record.TargetName = name.data();
    record.UserName = username.data();
    record.Persist = session_only ? CRED_PERSIST_SESSION : CRED_PERSIST_LOCAL_MACHINE;
    record.CredentialBlob = reinterpret_cast<BYTE*>(password.bytes.data());
    record.CredentialBlobSize = static_cast<DWORD>(password.bytes.size());
    return CredWriteW(&record, 0) != 0;
}

QList<QHostAddress> resolve(const QString& host)
{
    QHostAddress address;
    if (address.setAddress(host)) return {address};
    const auto result = QHostInfo::fromName(host);
    return result.addresses();
}

sdp_text sdp(const QByteArray& value)
{
    return {value.constData(), static_cast<uint32_t>(value.size())};
}

bool plain(const QString& value, qsizetype maximum)
{
    if (value.size() > maximum) return false;
    for (const auto character : value)
        if (character.isNull() || character.category() == QChar::Other_Control ||
            character.category() == QChar::Other_Format) return false;
    return true;
}
}

Password::~Password()
{
    if (!bytes.isEmpty()) SecureZeroMemory(bytes.data(), bytes.size());
}

QString endpoint(const Profile& profile)
{
    const QString host = profile.host.contains(u':') ? u'[' + profile.host + u']' : profile.host;
    return QStringLiteral("%1:%2").arg(host).arg(profile.port);
}

QString validate(const Profile& profile)
{
    if (profile.name.trimmed().isEmpty() || !plain(profile.name, 80) || profile.host.isEmpty() ||
        !plain(profile.host, 253) || !plain(profile.tls_name, 253) || !profile.port ||
        profile.user.isEmpty() || !plain(profile.user, 64) || profile.user.toUtf8().size() > 64 ||
        profile.ca_file.isEmpty() || !plain(profile.ca_file, 4096) || !plain(profile.database, 80) ||
        !QSet<QString>{"dev", "test", "prod"}.contains(profile.environment))
        return QStringLiteral("Enter a valid profile name, host, user and CA PEM.");
    constexpr auto allowed = u"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-:%";
    for (const auto& field : {profile.host, profile.tls_name})
        for (const auto character : field)
            if (!QStringView(allowed).contains(character))
                return QStringLiteral("Use an ASCII DNS name or numeric IP address.");
    return {};
}

QString status(sdp_status code)
{
    switch (code)
    {
    case SDP_OK: return QStringLiteral("Connected");
    case SDP_AUTH_FAILED: return QStringLiteral("Authentication failed");
    case SDP_PERMISSION_DENIED: return QStringLiteral("Not authorized");
    case SDP_MODULE_UNAVAILABLE: return QStringLiteral("Module not installed");
    case SDP_INCOMPATIBLE_PROTOCOL: return QStringLiteral("Incompatible protocol");
    case SDP_TLS_FAILED: return QStringLiteral("TLS certificate or identity verification failed");
    case SDP_TIMEOUT: return QStringLiteral("Connection timed out");
    case SDP_CANCELLED: return QStringLiteral("Cancelled");
    case SDP_FEATURE_UNSUPPORTED: return QStringLiteral("Unavailable");
    case SDP_CONNECTION_LOST: return QStringLiteral("Disconnected");
    default: return QStringLiteral("Connection could not be completed");
    }
}

bool export_profiles(const QString& path, const QVector<Profile>& profiles, QString& error)
{
    QJsonArray rows;
    for (const auto& profile : profiles) rows.push_back(serialized(profile));
    auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"profiles", rows}}).toJson(QJsonDocument::Indented);
    QSaveFile output(path);
    if (bytes.size() > profile_limit || !output.open(QIODevice::WriteOnly) ||
        output.write(bytes) != bytes.size() || !output.commit())
    {
        error = QStringLiteral("Could not save the profile file.");
        return false;
    }
    return true;
}

QVector<Profile> import_profiles(const QString& path, QString& error)
{
    error = QStringLiteral("Invalid workspace profile file.");
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly) || input.size() > profile_limit) return {};
    const auto bytes = input.read(profile_limit + 1);
    if (bytes.size() > profile_limit || input.error() != QFileDevice::NoError) return {};
    QJsonParseError parse{};
    const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) return {};
    const auto body = document.object();
    if (body.size() != 2 || body.value("version") != 1 || !body.value("profiles").isArray()) return {};
    const auto rows = body.value("profiles").toArray();
    if (rows.size() > 64) return {};

    QVector<Profile> profiles;
    QSet<QString> names;
    const QStringList text_keys{"name", "host", "database", "user", "tls_name", "ca_file", "environment"};
    for (const auto& item : rows)
    {
        const auto fields = item.toObject();
        if (fields.size() != 9 || !fields.value("read_only").isBool()) return {};
        for (const auto& name : text_keys)
            if (!fields.value(name).isString()) return {};
        const int port = fields.value("port").toInt(-1);
        if (port < 1 || port > 65535) return {};

        Profile profile;
        profile.name = fields.value("name").toString();
        profile.host = fields.value("host").toString();
        profile.user = fields.value("user").toString();
        profile.tls_name = fields.value("tls_name").toString();
        profile.database = fields.value("database").toString();
        profile.ca_file = fields.value("ca_file").toString();
        profile.environment = fields.value("environment").toString();
        profile.port = static_cast<quint16>(port);
        profile.read_only = fields.value("read_only").toBool();
        if (!validate(profile).isEmpty() || names.contains(profile.name)) return {};
        names.insert(profile.name);
        profiles.push_back(std::move(profile));
    }
    error.clear();
    return profiles;
}

Session::~Session()
{
    if (handle) sdp_close(handle, nullptr);
}

ConnectResult connect(const Profile& profile, Password& password, bool load_saved, bool save_credential, bool session_only)
{
    if (auto error = validate(profile); !error.isEmpty()) return {{}, error, {}};
    const auto ca = ca_bytes(profile);
    if (ca.isEmpty()) return {{}, QStringLiteral("Cannot read the selected CA PEM."), {}};

    QTemporaryFile trusted;
    if (!trusted.open() || trusted.write(ca) != ca.size() || !trusted.flush())
        return {{}, QStringLiteral("Cannot prepare CA trust material."), {}};
    const auto ca_path = trusted.fileName().toUtf8();
    trusted.close();

    const auto target = credential_target(profile, ca);
    if (password.bytes.isEmpty() && load_saved && !read_credential(target, password))
        return {{}, QStringLiteral("Saved credential unavailable. Enter the password."), {}};
    if (password.bytes.isEmpty() || password.bytes.size() > 1024 || password.bytes.contains('\0'))
        return {{}, QStringLiteral("Enter a password of 1–1024 UTF-8 bytes."), {}};

    auto session = std::make_shared<Session>();
    const auto server_name = (profile.tls_name.isEmpty() ? profile.host : profile.tls_name).toUtf8();
    const auto user = profile.user.toUtf8();
    auto code = static_cast<sdp_status>(SDP_CONNECTION_LOST);
    const auto addresses = resolve(profile.host);
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    for (const auto& address : addresses)
    {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (remaining < 100) { code = SDP_TIMEOUT; break; }
        const auto numeric = address.toString().toUtf8();
        sdp_open_options options{};
        options.struct_size = sizeof(options);
        options.abi_version = SDP_ABI_VERSION;
        options.address = sdp(numeric);
        options.server_name = sdp(server_name);
        options.ca_file = sdp(ca_path);
        options.username = sdp(user);
        options.password = sdp(password.bytes);
        options.port = profile.port;
        options.timeout_ms = static_cast<uint32_t>(remaining);
        code = sdp_open(&options, &session->handle, nullptr);
        if (code != SDP_CONNECTION_LOST) break;
    }
    if (code != SDP_OK) return {{}, status(code), {}};

    sdp_buffer buffer{};
    const auto feature_status = sdp_get_capabilities(session->handle, &buffer, nullptr);
    if (feature_status != SDP_OK) return {{}, status(feature_status), {}};
    const auto features = QJsonDocument::fromJson(QByteArray::fromRawData(
        reinterpret_cast<const char*>(buffer.data), buffer.size));
    sdp_buffer_free(&buffer);
    if (!features.isObject()) return {{}, QStringLiteral("Server capabilities are invalid."), {}};
    session->capabilities = features.object();
    for (const auto value : session->capabilities)
        if (!value.isBool()) return {{}, QStringLiteral("Server capabilities are invalid."), {}};

    const QString note = save_credential && !write_credential(target, profile, password, session_only)
        ? QStringLiteral("Windows Credential Manager unavailable. The password was not saved.") : QString{};
    return {std::move(session), {}, note};
}

bool forget_credential(const Profile& profile)
{
    const auto ca = ca_bytes(profile);
    if (ca.isEmpty()) return false;
    const auto target = credential_target(profile, ca);
    return CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND;
}
}
