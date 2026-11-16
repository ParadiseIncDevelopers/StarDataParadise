#include "migration_source_driver.hpp"
#include "migration_source_protocol.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QProcessEnvironment>

#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <windows.h>

namespace
{
using namespace wb::migration;
using namespace wb::migration::sourceProtocol;

bool frame(QFile& output, QByteArrayView prefix, const QJsonObject& body)
{
    const auto payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    return payload.size() <= maxResponse && output.write(prefix.data(), prefix.size()) == prefix.size() &&
        output.write(payload) == payload.size() && output.write("\n", 1) == 1 && output.flush();
}

int error(QFile& output, const char* message)
{
    ExternalSnapshot result;
    result.error = sourceProtocol::tr(message);
    return frame(output, "S ", snapshot(result)) ? 0 : 1;
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 1) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS)) return 2;
    const auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto& key : environment.keys())
        if (key.startsWith("PG", Qt::CaseInsensitive) || key.startsWith("MYSQL", Qt::CaseInsensitive))
            qunsetenv(key.toLocal8Bit().constData());
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    QFile input, output;
    if (!input.open(stdin, QIODevice::ReadOnly) || !output.open(stdout, QIODevice::WriteOnly)) return 1;
    try
    {
        const auto header = input.readLine(maxRequest + 1);
        if (!header.endsWith('\n') || header.size() > maxRequest)
            return error(output, "The source adapter received an invalid request.");
        const auto document = QJsonDocument::fromJson(header);
        ExternalSource source;
        if (!document.isObject() || !decodeRequest(document.object(), source))
            return error(output, "The source adapter received invalid connection settings.");
        const auto count = document.object().value("passwordBytes").toInteger();
        auto password = input.read(count + 1);
        struct Clear { QByteArray& value; ~Clear() { value.fill('\0'); } } clear{password};
        if (password.size() != count || password.contains('\0'))
            return error(output, "The source adapter received an invalid credential.");
        const auto result = sourceDriver::snapshot(source, password, [&](qsizetype rows, qsizetype bytes) {
            return frame(output, "P ", {{"rows", rows}, {"bytes", bytes}});
        });
        const auto answer = snapshot(result);
        if (QJsonDocument(answer).toJson(QJsonDocument::Compact).size() > maxResponse)
            return error(output, "The source snapshot exceeded the response size limit.");
        return frame(output, "S ", answer) ? 0 : 1;
    }
    catch (...)
    {
        return error(output, "The source adapter could not finish reading the snapshot.");
    }
}
