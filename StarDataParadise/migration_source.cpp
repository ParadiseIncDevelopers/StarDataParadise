#include "migration_source_protocol.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>

#include <windows.h>

namespace wb::migration
{
namespace
{
ExternalSnapshot failure(const char* message)
{
    ExternalSnapshot result;
    result.error = sourceProtocol::tr(message);
    return result;
}
}

ExternalSnapshot readExternalSnapshot(const ExternalSource& source, QByteArrayView password,
    const ExternalProgress& progress)
{
    using namespace sourceProtocol;
    const auto payload = request(source, password.size());
    ExternalSource checked;
    if (!decodeRequest(payload, checked) || password.contains('\0'))
        return failure("Enter a valid source, installed driver, password and TLS CA.");
    const auto program = source.helperFile.isEmpty() ?
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("StarDataParadiseMigrationSource.exe") :
        source.helperFile;
    const QFileInfo helper(program);
    if (!helper.isAbsolute() || !helper.isFile())
        return failure("The Workbench source adapter is missing.");

    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto& key : environment.keys())
        if (key.startsWith("PG", Qt::CaseInsensitive) || key.startsWith("MYSQL", Qt::CaseInsensitive))
            environment.remove(key);
    process.setProcessEnvironment(environment);
    process.setStandardErrorFile(QProcess::nullDevice());
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
    process.start(program, QStringList{});
    if (!process.waitForStarted(5000)) return failure("The Workbench source adapter could not be started.");

    const auto stop = [&] {
        if (process.state() == QProcess::NotRunning) return;
        process.kill();
        process.waitForFinished(2000);
    };
    auto frame = QJsonDocument(payload).toJson(QJsonDocument::Compact) + '\n';
    if (process.write(frame) != frame.size() || process.write(password.data(), password.size()) != password.size())
    {
        stop();
        return failure("The Workbench source adapter could not receive its connection settings.");
    }
    process.closeWriteChannel();

    QElapsedTimer timer;
    timer.start();
    QByteArray pending;
    QJsonObject result;
    qsizetype bytesReceived{}, rows{}, bytes{};
    do
    {
        process.waitForReadyRead(100);
        auto chunk = process.readAllStandardOutput();
        bytesReceived += chunk.size();
        if (bytesReceived > maxResponse + 256 * 1024)
        {
            stop();
            return failure("The source snapshot exceeded the transfer limit.");
        }
        pending += chunk;
        for (qsizetype newline; (newline = pending.indexOf('\n')) >= 0;)
        {
            const auto line = pending.first(newline);
            pending.remove(0, newline + 1);
            const auto document = line.size() >= 2 ? QJsonDocument::fromJson(line.sliced(2)) : QJsonDocument{};
            if (!result.isEmpty() || !document.isObject())
            {
                stop();
                return failure("The Workbench source adapter returned an invalid response.");
            }
            if (line.startsWith("S ")) result = document.object();
            else if (line.startsWith("P "))
            {
                const auto item = document.object();
                rows = item.value("rows").toInteger();
                bytes = item.value("bytes").toInteger();
            }
            else
            {
                stop();
                return failure("The Workbench source adapter returned an unknown response.");
            }
        }
        if (result.isEmpty() && progress && !progress(rows, bytes))
        {
            stop();
            return failure("Reading the source snapshot was cancelled.");
        }
    } while (process.state() != QProcess::NotRunning && timer.elapsed() < timeoutMilliseconds);

    if (process.state() != QProcess::NotRunning)
    {
        stop();
        return failure("Reading the source database timed out.");
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 ||
        !pending.isEmpty() || result.isEmpty())
        return failure("The Workbench source adapter ended before returning a snapshot.");
    return decodeSnapshot(result);
}
}
