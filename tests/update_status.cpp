#include "update_status.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, const char* error)
{
    if (!condition) throw std::runtime_error(error);
}

void save(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "save status fixture");
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        if (app.arguments().size() == 2)
        {
            QString error;
            const auto status = wb::read_update_status(app.arguments().at(1), error);
            require(error.isEmpty(), "read orchestrator update journal");
            std::cout << QJsonDocument(status).toJson(QJsonDocument::Compact).constData() << '\n';
            return 0;
        }
        QTemporaryDir directory;
        require(directory.isValid(), "create fixture directory");
        const auto path = directory.filePath("update-status.json");
        const QByteArray bytes = R"({"schema":1,"component":"server","instance":"test-instance","from_version":"0.4.0","to_version":"0.4.1-preview.1","phase":"staged","pending":"verified_target","rollback_boundary":"source_preserved","requires_admin":true,"unused":"fixture-secret"})";
        save(path, bytes);
        QString error;
        const auto status = wb::read_update_status(path, error);
        require(error.isEmpty() && status.size() == 9 && status.value("schema").toInt() == 1 &&
            status.value("component").toString() == "server" && status.value("instance").toString() == "test-instance" &&
            status.value("from_version").toString() == "0.4.0" && status.value("to_version").toString() == "0.4.1-preview.1" &&
            status.value("phase").toString() == "staged" && status.value("pending").toString() == "verified_target" &&
            status.value("rollback_boundary").toString() == "source_preserved" && status.value("requires_admin").toBool() &&
            !status.contains("unused"), "read a bounded status with shared fields");
        {
            QFile file(path);
            require(file.open(QIODevice::ReadOnly) && file.readAll() == bytes, "status reader changed the journal");
        }
        save(path, R"({"schema":2,"component":"fixture-secret"})");
        require(wb::read_update_status(path, error).isEmpty() && !error.isEmpty() &&
            !error.contains("fixture-secret"), "reject unsupported schema");
        save(path, QByteArray(64 * 1024 + 1, ' '));
        require(wb::read_update_status(path, error).isEmpty() && !error.isEmpty(), "reject oversized journal");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
