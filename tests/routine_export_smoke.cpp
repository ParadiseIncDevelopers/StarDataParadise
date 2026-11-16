#include "routine_export.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QThread>
#include <QTimer>

#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void require(bool passed, const char* message)
{
    if (!passed) throw std::runtime_error(message);
}

template <class Control>
Control& named(QDialog& dialog, const char* name)
{
    auto* widget = dialog.findChild<Control*>(QString::fromLatin1(name));
    require(widget != nullptr, name);
    return *widget;
}

template <class Predicate>
void waitFor(Predicate ready, const char* message)
{
    QElapsedTimer clock;
    clock.start();
    while (!ready() && clock.elapsed() < 50000)
    {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    require(ready(), message);
}

QJsonObject readJson(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Read local fixture JSON");
    return QJsonDocument::fromJson(file.readAll()).object();
}

void set(QDialog& dialog, const char* name, const QString& value)
{
    named<QLineEdit>(dialog, name).setText(value);
}

QString status(QDialog& dialog)
{
    return named<QLabel>(dialog, "exportStatus").text();
}

QString text(QDialog& dialog, const char* name)
{
    return named<QPlainTextEdit>(dialog, name).toPlainText();
}

void action(QDialog& dialog, const char* name, QStringView expected)
{
    auto& button = named<QPushButton>(dialog, name);
    require(button.isEnabled(), name);
    const auto before = status(dialog);
    button.click();
    auto& cancel = named<QPushButton>(dialog, "cancelRoutineExport");
    waitFor([&] { return !cancel.isEnabled() && status(dialog) != before; }, name);
    if (!status(dialog).contains(expected))
        throw std::runtime_error((QString::fromLatin1(name) + QStringLiteral(": ") + status(dialog)).toStdString());
}

void configure(QDialog& dialog, const QJsonObject& fixture)
{
    set(dialog, "exportPython", fixture.value("python").toString());
    set(dialog, "exportToolkit", fixture.value("toolkit").toString());
    set(dialog, "exportJdk", fixture.value("jdk").toString());
    set(dialog, "exportJdbc", fixture.value("jdbc").toString());
    set(dialog, "exportGson", fixture.value("gson").toString());
}

void inspect(QDialog& dialog, const wb::Password& password, QStringView expected)
{
    set(dialog, "exportPassword", QString::fromUtf8(password.bytes));
    action(dialog, "inspectRoutineExport", expected);
    require(named<QLineEdit>(dialog, "exportPassword").text().isEmpty(), "Password remained in the export dialog");
}

void generate(QDialog& dialog, const char* outputName, const char* mode, QStringView source)
{
    const auto path = QDir::current().absoluteFilePath(QString::fromLatin1(outputName));
    set(dialog, "exportOutput", path);
    action(dialog, "generateRoutineExport", u"Generated Java source and manifest");
    require(text(dialog, "exportSource").contains(source), "Generated source preview is missing the expected entry");
    require(readJson(QDir(path).filePath("manifest.json")).value("mode").toString() == QString::fromLatin1(mode),
        "The generated manifest does not identify the selected mode");
}

void build(QDialog& dialog, QStringView expected)
{
    bool confirmed = false;
    QTimer::singleShot(0, &dialog, [&] {
        auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (message && message->standardButtons().testFlag(QMessageBox::Yes))
        {
            confirmed = true;
            message->button(QMessageBox::Yes)->click();
        }
    });
    action(dialog, "buildRoutineExport", expected);
    require(confirmed, "Build did not request local toolchain approval");
}

wb::Password readSecret()
{
    std::string line;
    std::getline(std::cin, line);
    wb::Password result{QByteArray::fromStdString(line)};
    line.assign(line.size(), '\0');
    require(!result.bytes.isEmpty(), "TLS fixture did not supply a password");
    return result;
}

void exercise(const QVector<wb::Profile>& profiles, const QJsonObject& fixture)
{
    auto ownerSecret = readSecret();
    auto guestSecret = readSecret();
    QWidget owner;
    owner.show();

    auto& hosted = *wb::showRoutineExport(profiles.at(0), fixture.value("owner_hosted").toObject(), &owner);
    configure(hosted, fixture);
    inspect(hosted, ownerSecret, u"Server rejected this export mode");
    const auto location = status(hosted);
    const auto expectedLocation = fixture.value("unsupported_location").toString();
    require(!expectedLocation.isEmpty() && location.contains(expectedLocation) &&
        !named<QPushButton>(hosted, "generateRoutineExport").isEnabled(),
        "Standalone hosted SQL lacked the exact source location or permitted generation");

    // Change export mode in the same dialog, then verify the real connected snapshot and source.
    named<QComboBox>(hosted, "exportMode").setCurrentIndex(1);
    inspect(hosted, ownerSecret, u"Authorized snapshot ready");
    const auto authorizedPlan = text(hosted, "exportPlan");
    require(authorizedPlan.contains("requires Server") && authorizedPlan.contains("export_hidden_table"),
        "Connected mode did not identify its Server dependency and authorized catalog closure");
    generate(hosted, "connected-java", "connected", u"class ConnectedRoutine");

    auto& guest = *wb::showRoutineExport(profiles.at(1), fixture.value("guest_hosted").toObject(), &owner);
    configure(guest, fixture);
    named<QComboBox>(guest, "exportMode").setCurrentIndex(1);
    inspect(guest, guestSecret, u"Server denied export permissions for the routine or a dependency");
    require(!status(guest).contains("export_hidden_table") && text(guest, "exportPlan").isEmpty() &&
        text(guest, "exportSource").isEmpty() && !named<QPushButton>(guest, "generateRoutineExport").isEnabled(),
        "Unauthorized dependency information was visible or permitted generation");
    guest.close();
    std::cout << "S39-T03: standalone hosted SQL shows its source location and connected export hides denied dependencies\n";

    auto& standalone = *wb::showRoutineExport(profiles.at(0), fixture.value("owner_pure").toObject(), &owner);
    configure(standalone, fixture);
    inspect(standalone, ownerSecret, u"Authorized snapshot ready");
    require(text(standalone, "exportPlan").contains("standalone"), "Standalone export plan missing");
    generate(standalone, "standalone-java", "standalone", u"class Routines");
    std::cout << "READY" << std::endl;

    std::string control;
    std::getline(std::cin, control);
    require(control == "GO", "Fixture did not confirm that the Server stopped");
    build(standalone, u"Standalone Java compiled and executed successfully");
    build(hosted, u"JDBC linkage test passed. Routine execution requires Server");
    std::cout << "S39-T02: standalone Workbench build and execution passed offline; connected wrapper compiled and linked\n";
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    try
    {
        require(app.arguments().size() == 3, "Expected an export fixture profile and catalog bundle");
        QString error;
        const auto profiles = wb::import_profiles(app.arguments().at(1), error);
        require(profiles.size() == 2, "Expected owner and guest fixture profiles");
        exercise(profiles, readJson(app.arguments().at(2)));
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

