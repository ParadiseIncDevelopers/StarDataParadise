#include "update_status.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStringList>

#include <windows.h>

namespace wb
{
namespace
{
constexpr qint64 journal_limit = 64 * 1024;
const QStringList phase_names{
    "planned", "verified", "preflight", "stopped", "staging", "staged", "verified_target",
    "started", "complete", "safe_stop", "rolled_back"};
const QStringList components{"server", "workbench", "cli"};
const QStringList rollback_boundaries{"source_preserved", "has_new_writes", "rollback_complete", "unknown"};

bool local_file(const QString& file)
{
    auto path = QDir::toNativeSeparators(file);
    if (path.size() < 4 || path.at(1) != u':' || path.at(2) != u'\\' || path.indexOf(u':', 2) >= 0) return false;
    const auto root = path.left(3).toStdWString();
    const auto drive = GetDriveTypeW(root.c_str());
    if (drive != DRIVE_FIXED && drive != DRIVE_REMOVABLE && drive != DRIVE_RAMDISK && drive != DRIVE_CDROM)
        return false;
    const auto attributes = GetFileAttributesW(path.toStdWString().c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) == 0;
}
}

QJsonObject read_update_status(const QString& file, QString& error)
{
    error.clear();
    if (!local_file(file))
    {
        error = QObject::tr("Choose a regular file on a local drive.");
        return {};
    }

    QFile journal(file);
    if (!journal.open(QIODevice::ReadOnly))
    {
        error = QObject::tr("The update status file could not be read.");
        return {};
    }
    const auto bytes = journal.read(journal_limit + 1);
    if (journal.error() != QFileDevice::NoError || bytes.size() > journal_limit)
    {
        error = QObject::tr("The update status file could not be read within 64 KiB.");
        return {};
    }
    const auto document = QJsonDocument::fromJson(bytes);
    const auto source = document.object();
    const auto component = source.value("component").toString();
    const auto from = source.value("from_version").toString();
    const auto to = source.value("to_version").toString();
    const auto phase = source.value("phase").toString();
    const auto pending = source.value("pending");
    const auto boundary = source.value("rollback_boundary").toString();
    const auto requires_admin = source.value("requires_admin");
    const auto instance = source.value("instance");
    const QRegularExpression version_token(QStringLiteral("\\A[A-Za-z0-9][A-Za-z0-9.+_-]{0,63}\\z"));
    const QRegularExpression instance_token(QStringLiteral("\\A[A-Za-z0-9][A-Za-z0-9_.-]{0,63}\\z"));
    if (!document.isObject() || source.value("schema").toDouble() != 1.0 || !components.contains(component) ||
        !version_token.match(from).hasMatch() || !version_token.match(to).hasMatch() || !phase_names.contains(phase) ||
        (!pending.isUndefined() && !pending.isNull() && !phase_names.contains(pending.toString())) ||
        !rollback_boundaries.contains(boundary) || !requires_admin.isBool() ||
        (!instance.isUndefined() && !instance.isNull() && !instance_token.match(instance.toString()).hasMatch()))
    {
        error = QObject::tr("Invalid or unsupported update status.");
        return {};
    }
    QJsonObject status{
        {"schema", 1}, {"component", component}, {"from_version", from}, {"to_version", to}, {"phase", phase},
        {"pending", pending.isString() ? pending : QJsonValue(QJsonValue::Null)},
        {"rollback_boundary", boundary}, {"requires_admin", requires_admin}};
    if (instance.isString()) status.insert("instance", instance);
    return status;
}

void show_update_status(QWidget* parent, const QString& file)
{
    QString error;
    const auto status = read_update_status(file, error);
    const auto title = QObject::tr("Update status");
    if (!error.isEmpty())
    {
        QMessageBox::warning(parent, title, error);
        return;
    }
    QStringList lines{
        QObject::tr("Component: %1").arg(status.value("component").toString()),
        QObject::tr("From version: %1").arg(status.value("from_version").toString()),
        QObject::tr("To version: %1").arg(status.value("to_version").toString()),
        QObject::tr("Phase: %1").arg(status.value("phase").toString()),
        QObject::tr("Pending: %1").arg(status.value("pending").toString(QObject::tr("none"))),
        QObject::tr("Rollback boundary: %1").arg(status.value("rollback_boundary").toString()),
        QObject::tr("Administrator required: %1").arg(status.value("requires_admin").toBool()
            ? QObject::tr("yes") : QObject::tr("no"))};
    if (status.contains("instance"))
        lines.insert(1, QObject::tr("Instance: %1").arg(status.value("instance").toString()));
    if (status.value("rollback_boundary").toString() == "has_new_writes")
        lines.append(QObject::tr("Returning to the source could discard writes made on the target."));
    QMessageBox dialog(QMessageBox::Information, title, lines.join(u'\n'), QMessageBox::Ok, parent);
    dialog.setTextFormat(Qt::PlainText);
    dialog.exec();
}
}
