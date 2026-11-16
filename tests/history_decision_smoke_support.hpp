#pragma once

#include "sql_session.hpp"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QTimer>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <optional>

namespace studio_smoke
{
inline void require(bool condition, const char* detail)
{
    if (condition) return;
    std::cerr << detail << '\n';
    std::exit(1);
}

template<class Widget>
Widget& control(QObject& parent, const char* name)
{
    auto* found = parent.findChild<Widget*>(QString::fromLatin1(name));
    require(found != nullptr, name);
    return *found;
}

inline void click(QObject& parent, const char* name)
{
    auto& button = control<QPushButton>(parent, name);
    require(button.isEnabled(), name);
    button.click();
}

template<class Predicate>
bool waitFor(Predicate predicate, int milliseconds = 10000)
{
    if (predicate()) return true;
    QEventLoop loop;
    QTimer check;
    QObject::connect(&check, &QTimer::timeout, &loop, [&] {
        if (predicate()) loop.quit();
    });
    check.start(10);
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
    return predicate();
}

template<class Start>
wb::SqlReply await(Start start)
{
    std::optional<wb::SqlReply> result;
    require(start([&](const wb::SqlReply& reply) { result = reply; }), "fixture operation could not start");
    require(waitFor([&] { return result.has_value(); }), "fixture operation did not finish");
    return std::move(*result);
}

inline wb::SqlReply success(wb::SqlReply result, const char* detail)
{
    if (result.error.code != SDP_OK) std::cerr << result.error.message << '\n';
    require(result.error.code == SDP_OK, detail);
    return result;
}

inline sdp_text wire(const QByteArray& value)
{
    return {value.constData(), static_cast<uint32_t>(value.size())};
}

inline wb::SqlReply history(wb::SqlSession& session, sdp_history_operation action, const QJsonObject& body = {})
{
    const auto bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
    return await([&](auto done) {
        return session.request([&](auto handle, auto* output, auto* error) {
            return sdp_history_command(handle, action, wire(bytes), output, error);
        }, done);
    });
}

inline QString head(wb::SqlSession& session)
{
    auto revision = success(history(session, SDP_HISTORY_STATUS), "History status").data.value("current_revision").toString();
    require(!revision.isEmpty(), "missing committed History revision");
    return revision;
}

inline wb::SqlReply sql(wb::SqlSession& session, const QString& text, QJsonArray* rows = nullptr)
{
    return await([&](auto done) {
        return session.execute({{text}}, {}, {}, {}, [rows](const wb::SqlEvent& event) {
            if (rows && event.kind == wb::SqlEvent::Kind::batch)
                for (const auto& row : event.data.value("rows").toArray()) rows->push_back(row);
        }, done);
    });
}

inline void write(wb::SqlSession& session, const QString& text)
{
    success(sql(session, text), "fixture SQL write");
}

inline QJsonArray select(wb::SqlSession& session, const QString& text)
{
    QJsonArray rows;
    success(sql(session, text, &rows), "fixture SQL query");
    return rows;
}

inline QJsonArray catalog(wb::SqlSession& session)
{
    return success(await([&](auto done) { return session.refreshMetadata(done); }), "fixture metadata")
        .data.value("objects").toArray();
}

inline QJsonObject named(const QJsonArray& objects, QStringView name)
{
    for (const auto entry : objects)
    {
        const auto object = entry.toObject();
        const auto qualified = object.value("qualified_name").toArray();
        if (!qualified.isEmpty() && qualified.last().toString() == name)
            return object;
    }
    return {};
}

inline QJsonObject integer(int value) { return {{"t", "i64"}, {"v", QString::number(value)}}; }
inline QJsonObject text(const QString& value) { return {{"t", "text"}, {"v", value}}; }

inline wb::Profile fixtureProfile(int argc, char** argv)
{
    require(argc == 2, "expected one fixture profile argument");
    QString error;
    const auto profiles = wb::import_profiles(QString::fromLocal8Bit(argv[1]), error);
    require(profiles.size() == 1, "could not read the fixture profile");
    return profiles.first();
}

inline QByteArray fixturePassword()
{
    std::string source;
    std::getline(std::cin, source);
    if (!source.empty() && source.back() == '\r') source.pop_back();
    QByteArray password(source.data(), static_cast<qsizetype>(source.size()));
    std::fill(source.begin(), source.end(), '\0');
    return password;
}

inline std::shared_ptr<wb::Session> connect(const wb::Profile& profile, wb::Password& password)
{
    const auto result = wb::connect(profile, password, false, false);
    if (!result.session) std::cerr << result.error.toStdString() << '\n';
    require(bool(result.session), "isolated TLS login");
    return result.session;
}
}
