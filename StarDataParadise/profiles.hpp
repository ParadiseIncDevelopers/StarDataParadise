#pragma once

#include <StarDataParadiseDLLler.h>
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <memory>

namespace wb
{
struct Profile
{
    QString name;
    QString host;
    QString database;
    QString user;
    QString tls_name;
    QString ca_file;
    QString environment = QStringLiteral("dev");
    quint16 port = 7443;
    bool read_only = false;
};

struct Password
{
    QByteArray bytes;
    ~Password();
};

QString validate(const Profile& profile);
QString endpoint(const Profile& profile);
QString status(sdp_status code);
bool export_profiles(const QString& path, const QVector<Profile>& profiles, QString& error);
QVector<Profile> import_profiles(const QString& path, QString& error);

struct Session
{
    sdp_connection handle{};
    QJsonObject capabilities;
    ~Session();
};

struct ConnectResult
{
    std::shared_ptr<Session> session;
    QString error;
    QString credential_note;
};

ConnectResult connect(const Profile& profile, Password& password, bool load_saved, bool save_credential,
    bool session_only = false);
bool forget_credential(const Profile& profile);
}
