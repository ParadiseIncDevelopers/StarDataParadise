#include "routine_export.hpp"
#include "table_codec.hpp"

#include <QComboBox>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <array>
#include <functional>
#include <memory>
#include <utility>

namespace wb
{
namespace
{
constexpr qsizetype payloadLimit = 1024 * 1024;
constexpr auto javaPackage = "stardataparadise.exported.";
constexpr auto javaDirectory = "stardataparadise/exported/";
constexpr auto jdbcDigest = "c40f92b8d725eefff235980f25b264fbfca056b13fa313aa5a00d2fa14c69298";
constexpr auto gsonDigest = "dd0ce1b55a3ed2080cb70f9c655850cda86c206862310009dcb5e5c95265a5e0";

struct Tool
{
    const char* name;
    const char* digest;
};

constexpr std::array<Tool, 6> tools{{
    {"snapshot.py", "52d7931b96a00f01397506f35261bd9f2df9643ec105b16543f41faed3ae8984"},
    {"emit_java.py", "2c30fa9d240b6e97612a9afe8918a45a432fc5b0b74f1b7f90d541e80d1f2d49"},
    {"emit_connected.py", "43951982847b5d814e7fb2e9db2aa73a3d4fb5ffab87b0f51289081f6bd1a9e2"},
    {"RoutineRuntime.java", "359b04724834f3521dca3bb088bdcf9ffb5627768705e836579f2eadac6202a6"},
    {"build.gradle.in", "68c26ad4c736804a6523f134e9306336e6efaa533acb303f021789602e29260a"},
    {"ConnectedRoutine.java.in", "f5a5c59662c6887e2b929afce7a4e3f631724f7a54f9c02a562cc386a62144a2"},
}};

QByteArray readFile(const QString& path, qsizetype maximum = payloadLimit)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximum) return {};
    const auto bytes = file.read(maximum + 1);
    return bytes.size() <= maximum ? bytes : QByteArray{};
}

QByteArray digest(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

bool saveFile(const QString& path, const QByteArray& bytes)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

QString qualified(const QJsonObject& object)
{
    QStringList path;
    for (const auto& part : object.value("qualified_name").toArray())
    {
        if (!part.isString() || part.toString().isEmpty()) return {};
        path.push_back(transfer::identifier(part.toString()));
    }
    return path.size() == 3 ? path.join('.') : QString{};
}

class ExportDialog final : public QDialog
{
public:
    ExportDialog(Profile profile, QJsonObject routine, QWidget* parent);
    ~ExportDialog() override;

private:
    using Finished = std::function<void(bool, const QString&)>;
    QLineEdit* localPath(QFormLayout&, const QString&, const char*, bool directory,
        const QString& defaultText = {});
    void inspect();
    void generate();
    void build();
    void invalidate();
    void renderSnapshot(const QJsonObject&);
    void start(QString program, QStringList arguments, QByteArray input, Finished);
    void complete(bool, const QString&);
    void finishProcess();
    void sync();
    void state(const QString& text) { status_->setText(text); }
    QString mode() const { return mode_->currentIndex() == 0 ? "standalone" : "connected"; }
    QString localScript(const QString& file) const { return workspace_.filePath(file); }

    const Profile profile_;
    const QJsonObject routine_;
    const QString target_;
    QTemporaryDir workspace_;
    QComboBox* mode_{};
    QLineEdit *python_{}, *toolkit_{}, *password_{}, *output_{}, *java_{}, *jdbc_{}, *gson_{};
    QPlainTextEdit *plan_{}, *source_{}, *log_{};
    QLabel* status_{};
    QTabWidget* views_{};
    QPushButton *inspect_{}, *generate_{}, *build_{}, *folder_{}, *cancel_{};
    QProcess process_;
    QTimer deadline_;
    QByteArray processOutput_, processInput_;
    Finished finished_;
    QString pythonPath_, snapshotPath_, generated_;
    QJsonObject manifest_;
    bool aborting_{};
};

ExportDialog::ExportDialog(Profile profile, QJsonObject routine, QWidget* parent)
    : QDialog(parent), profile_(std::move(profile)), routine_(std::move(routine)), target_(qualified(routine_))
{
    setObjectName("routineExport");
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Export PL/SQL to Java"));
    resize(760, 690);
    auto* layout = new QVBoxLayout(this);
    auto* identity = new QLabel(tr("%1 · revision %2 · %3")
        .arg(target_).arg(routine_.value("schema_version").toInteger()).arg(endpoint(profile_)));
    identity->setTextFormat(Qt::PlainText);
    layout->addWidget(identity);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    mode_ = new QComboBox;
    mode_->setObjectName("exportMode");
    mode_->addItems({tr("Standalone Java 17"), tr("Connected JDBC 0.2.0")});
    form->addRow(tr("Target"), mode_);
    python_ = localPath(*form, tr("Python 3.12 executable"), "exportPython", false,
        QStandardPaths::findExecutable("python"));
    toolkit_ = localPath(*form, tr("Local Stage 35 exporter tools"), "exportToolkit", true);
    password_ = new QLineEdit;
    password_->setObjectName("exportPassword");
    password_->setEchoMode(QLineEdit::Password);
    password_->setMaxLength(1024);
    form->addRow(tr("Server password for inspection"), password_);
    output_ = new QLineEdit;
    output_->setObjectName("exportOutput");
    output_->setPlaceholderText(tr("Absolute path to a new local directory"));
    form->addRow(tr("Artifact directory"), output_);
    java_ = localPath(*form, tr("Local JDK directory"), "exportJdk", true);
    jdbc_ = localPath(*form, tr("Local JDBC 0.2.0 JAR"), "exportJdbc", false);
    gson_ = localPath(*form, tr("Local Gson 2.13.2 JAR"), "exportGson", false);

    auto* actions = new QHBoxLayout;
    layout->addLayout(actions);
    const auto button = [actions](const QString& title, const char* name) {
        auto* result = new QPushButton(title);
        result->setObjectName(QString::fromLatin1(name));
        actions->addWidget(result);
        return result;
    };
    inspect_ = button(tr("Inspect on Server"), "inspectRoutineExport");
    generate_ = button(tr("Generate files"), "generateRoutineExport");
    build_ = button(tr("Build and test locally…"), "buildRoutineExport");
    folder_ = button(tr("Open artifact folder"), "openRoutineExportFolder");
    cancel_ = button(tr("Cancel task"), "cancelRoutineExport");
    connect(inspect_, &QPushButton::clicked, this, [this] { inspect(); });
    connect(generate_, &QPushButton::clicked, this, [this] { generate(); });
    connect(build_, &QPushButton::clicked, this, [this] { build(); });
    connect(folder_, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(generated_));
    });
    connect(cancel_, &QPushButton::clicked, this, [this] {
        aborting_ = true;
        process_.kill();
    });

    views_ = new QTabWidget;
    layout->addWidget(views_, 1);
    const auto view = [this](const QString& title, const char* name) {
        auto* result = new QPlainTextEdit;
        result->setObjectName(QString::fromLatin1(name));
        result->setReadOnly(true);
        views_->addTab(result, title);
        return result;
    };
    plan_ = view(tr("Eligibility and dependencies"), "exportPlan");
    source_ = view(tr("Generated Java"), "exportSource");
    log_ = view(tr("Local build output"), "exportLog");
    status_ = new QLabel;
    status_->setObjectName("exportStatus");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    layout->addWidget(status_);
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        aborting_ = true;
        process_.kill();
    });
    process_.setProcessChannelMode(QProcess::MergedChannels);
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] {
        processOutput_.append(process_.readAllStandardOutput());
        if (processOutput_.size() <= payloadLimit) return;
        aborting_ = true;
        process_.kill();
    });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) complete(false, tr("Could not start the selected local tool."));
    });
    connect(&process_, &QProcess::started, this, [this] {
        if (!processInput_.isEmpty()) process_.write(processInput_);
        processInput_.fill('\0');
        processInput_.clear();
        process_.closeWriteChannel();
    });
    connect(&process_, &QProcess::finished, this, [this] { finishProcess(); });
    connect(mode_, &QComboBox::currentIndexChanged, this, [this] { invalidate(); sync(); });
    for (auto* field : {python_, toolkit_})
        connect(field, &QLineEdit::textChanged, this, [this] { invalidate(); sync(); });
    state(tr("Select the local exporter and connect with an authorized account to inspect the routine."));
    sync();
}

ExportDialog::~ExportDialog()
{
    if (process_.state() == QProcess::NotRunning) return;
    process_.disconnect(this);
    process_.kill();
    process_.waitForFinished(1500);
}

QLineEdit* ExportDialog::localPath(QFormLayout& form, const QString& label, const char* name,
    bool directory, const QString& defaultText)
{
    auto* row = new QHBoxLayout;
    auto* field = new QLineEdit(defaultText);
    field->setObjectName(QString::fromLatin1(name));
    row->addWidget(field, 1);
    auto* browse = new QPushButton(tr("Browse…"));
    row->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this, directory, field] {
        if (finished_) return;
        const auto path = directory ? QFileDialog::getExistingDirectory(this, tr("Select local tool directory"), field->text())
            : QFileDialog::getOpenFileName(this, tr("Select local tool file"), field->text());
        if (!path.isEmpty()) field->setText(QDir::toNativeSeparators(path));
    });
    form.addRow(label, row);
    return field;
}

void ExportDialog::invalidate()
{
    if (!snapshotPath_.isEmpty()) QFile::remove(snapshotPath_);
    snapshotPath_.clear();
    generated_.clear();
    manifest_ = {};
    plan_->clear();
    source_->clear();
    log_->clear();
}

void ExportDialog::sync()
{
    const auto active = bool(finished_);
    const auto connected = mode() == "connected";
    mode_->setEnabled(!active);
    for (auto* field : {python_, toolkit_, password_, output_, java_}) field->setEnabled(!active);
    jdbc_->setEnabled(!active && connected);
    gson_->setEnabled(!active && connected);
    inspect_->setEnabled(!active && workspace_.isValid() && !target_.isEmpty());
    generate_->setEnabled(!active && !snapshotPath_.isEmpty());
    build_->setEnabled(!active && !generated_.isEmpty() && !manifest_.isEmpty());
    folder_->setEnabled(!active && !generated_.isEmpty());
    cancel_->setEnabled(active);
}

void ExportDialog::start(QString program, QStringList arguments, QByteArray input, Finished finished)
{
    Q_ASSERT(!finished_);
    auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto key : {"JAVA_TOOL_OPTIONS", "JDK_JAVA_OPTIONS", "_JAVA_OPTIONS", "CLASSPATH", "SDP_PASSWORD"})
        environment.remove(QString::fromLatin1(key));
    process_.setProcessEnvironment(environment);
    process_.setWorkingDirectory(workspace_.path());
    processOutput_.clear();
    aborting_ = false;
    finished_ = std::move(finished);
    sync();
    deadline_.start(45000);
    processInput_ = std::move(input);
    process_.start(program, arguments);
}

void ExportDialog::complete(bool success, const QString& detail)
{
    deadline_.stop();
    processInput_.fill('\0');
    processInput_.clear();
    auto finish = std::exchange(finished_, {});
    sync();
    if (finish) finish(success, detail);
}

void ExportDialog::finishProcess()
{
    if (!finished_) return;
    processOutput_.append(process_.readAllStandardOutput());
    if (aborting_ || processOutput_.size() > payloadLimit)
    {
        complete(false, tr("Local task cancelled, timed out or exceeded its output limit."));
        return;
    }
    const auto success = process_.exitStatus() == QProcess::NormalExit && process_.exitCode() == 0;
    complete(success, QString::fromUtf8(processOutput_).trimmed());
}


void ExportDialog::inspect()
{
    invalidate();
    sync();
    if (const auto error = validate(profile_); !error.isEmpty()) { state(error); return; }
    const QFileInfo python(python_->text());
    const QFileInfo toolkit(toolkit_->text());
    if (!python.isAbsolute() || !python.isFile() || python.suffix().compare("exe", Qt::CaseInsensitive) != 0 ||
        !toolkit.isAbsolute() || !toolkit.isDir())
    {
        state(tr("Select a local Python executable and the Stage 35 toolkit directory."));
        return;
    }
    for (const auto& tool : tools)
    {
        const auto name = QString::fromLatin1(tool.name);
        auto bytes = readFile(QDir(toolkit.absoluteFilePath()).filePath(name));
        bytes.replace("\r\n", "\n");
        if (digest(bytes) != tool.digest || !saveFile(localScript(name), bytes))
        {
            state(tr("Could not verify and stage %1 from the Stage 35 toolkit.").arg(name));
            return;
        }
    }
    auto secret = password_->text().toUtf8();
    password_->clear();
    if (secret.isEmpty() || secret.size() > 1024 || secret.contains('\n') || secret.contains('\r') || secret.contains('\0'))
    {
        secret.fill('\0');
        state(tr("Enter a valid password for the selected Server account."));
        return;
    }
    secret.append('\n');
    pythonPath_ = python.absoluteFilePath();
    const auto local = localScript("snapshot.json");
    const auto selectedMode = mode();
    QStringList args{"-I", "-B", "-X", "utf8", localScript("snapshot.py"),
        "--host", profile_.host, "--port", QString::number(profile_.port), "--user", profile_.user,
        "--routine", target_, "--mode", selectedMode, "--output", local, "--password-stdin"};
    if (!profile_.tls_name.isEmpty()) args.append({"--tls-name", profile_.tls_name});
    if (!profile_.ca_file.isEmpty()) args.append({"--ca", QFileInfo(profile_.ca_file).absoluteFilePath()});
    state(tr("Reading the authorized routine snapshot over TLS…"));
    start(pythonPath_, args, std::move(secret), [this, local, selectedMode](bool success, const QString& detail) {
        if (!success)
        {
            QFile::remove(local);
            if (detail.contains("Server error 1006"))
                state(tr("Server denied export permissions for the routine or a dependency."));
            else if (detail.contains("Server error 1004"))
                state(tr("Server rejected this export mode for the selected code. %1").arg(detail));
            else state(detail.isEmpty() ? tr("Snapshot inspection failed.") : detail);
            return;
        }
        const auto snapshot = QJsonDocument::fromJson(readFile(local, 8 * payloadLimit)).object();
        const auto entries = snapshot.value("entries").toArray();
        const auto entryId = entries.isEmpty() ? QJsonValue{} : entries.first();
        bool checked = false;
        for (const auto& value : snapshot.value("routines").toArray())
        {
            const auto current = value.toObject();
            if (current.value("id") == entryId && current.value("id") == routine_.value("id") &&
                current.value("schema_version") == routine_.value("schema_version")) checked = true;
        }
        if (snapshot.value("format").toInt() != 1 || snapshot.value("mode") != selectedMode || !checked)
        {
            QFile::remove(local);
            state(tr("The routine revision or snapshot format changed. Refresh Object Explorer and reopen export."));
            return;
        }
        snapshotPath_ = local;
        renderSnapshot(snapshot);
        views_->setCurrentWidget(plan_);
        state(tr("Authorized snapshot ready. Choose a new local artifact directory."));
        sync();
    });
}

void ExportDialog::renderSnapshot(const QJsonObject& snapshot)
{
    QStringList lines{mode() == "standalone" ? tr("Java 17 · local runtime · standalone")
        : tr("Java 17 · JDBC 0.2.0 · requires Server and a caller-owned transaction")};
    if (const auto blocker = snapshot.value("standalone_blocker").toObject(); !blocker.isEmpty())
        lines.append(tr("Standalone unavailable at line %1, column %2 (%3).")
            .arg(blocker.value("line").toInt()).arg(blocker.value("column").toInt())
            .arg(blocker.value("op").toString()));
    lines.append(snapshot.value("writes_sql").toBool() ? tr("The routine can write SQL data.")
        : snapshot.value("uses_sql").toBool() ? tr("The routine reads hosted SQL data.") : tr("No hosted SQL data access."));
    lines.append(QString{});
    for (const auto& item : snapshot.value("routines").toArray())
    {
        const auto routine = item.toObject();
        lines.append(tr("%1 · revision %2").arg(qualified(routine)).arg(routine.value("schema_version").toInt()));
        for (const auto& dependency : routine.value("dependencies").toArray())
        {
            const auto object = dependency.toObject();
            lines.append(tr("  %1 · revision %2").arg(qualified(object)).arg(object.value("schema_version").toInt()));
        }
    }
    lines.append(QString{});
    lines.append(tr("Type mapping for Java 17\nBIGINT → Long, DECIMAL → BigDecimal, BOOLEAN → Boolean, "
        "TEXT → String, BLOB → byte[], DATE → LocalDate, TIME → LocalTime. "
        "Use the generated runtime or SDK types for timestamps, records and collections."));
    plan_->setPlainText(lines.join('\n'));
}

void ExportDialog::generate()
{
    const QFileInfo destination(output_->text().trimmed());
    if (!destination.isAbsolute() || destination.exists() || destination.isSymLink() || !destination.dir().exists())
    {
        state(tr("Choose a new absolute local directory in an existing parent folder."));
        return;
    }
    const auto connected = mode() == "connected";
    if (connected && digest(readFile(jdbc_->text(), 16 * payloadLimit)) != jdbcDigest)
    {
        state(tr("Select the verified StarDataParadise JDBC 0.2.0 runtime JAR."));
        return;
    }
    const auto path = destination.absoluteFilePath();
    QStringList args{"-I", "-B", "-X", "utf8", localScript(connected ? "emit_connected.py" : "emit_java.py"),
        "--snapshot", snapshotPath_, "--output", path};
    if (connected) args.append({"--sdk-sha256", QString::fromLatin1(jdbcDigest)});
    state(tr("Generating Java source from the authorized snapshot…"));
    start(pythonPath_, args, {}, [this, path, connected](bool success, const QString& detail) {
        if (!success) { state(detail.isEmpty() ? tr("Java generation failed.") : detail); return; }
        const QDir base(path);
        const auto entry = base.filePath(QString("src/main/java/") + javaDirectory +
            (connected ? "ConnectedRoutine.java" : "Routines.java"));
        const auto generated = readFile(entry);
        const auto manifest = QJsonDocument::fromJson(readFile(base.filePath("manifest.json"), 8 * payloadLimit)).object();
        if (generated.isEmpty() || manifest.value("mode") != mode())
        {
            state(tr("The generated artifact lacks the expected Java source or manifest."));
            return;
        }
        generated_ = path;
        manifest_ = manifest;
        source_->setPlainText(QString::fromUtf8(generated));
        views_->setCurrentWidget(source_);
        state(tr("Generated Java source and manifest in %1.").arg(QDir::toNativeSeparators(path)));
        sync();
    });
}

void ExportDialog::build()
{
    const QFileInfo jdk(java_->text());
    const auto compiler = QDir(jdk.absoluteFilePath()).filePath("bin/javac.exe");
    const auto launcher = QDir(jdk.absoluteFilePath()).filePath("bin/java.exe");
    if (!jdk.isAbsolute() || !QFileInfo(compiler).isFile() || !QFileInfo(launcher).isFile())
    {
        state(tr("JDK unavailable. Select a local JDK with javac.exe and java.exe."));
        return;
    }
    const bool connected = mode() == "connected";
    const auto jarData = connected ? readFile(jdbc_->text(), 16 * payloadLimit) : QByteArray{};
    const auto gsonData = connected ? readFile(gson_->text(), 16 * payloadLimit) : QByteArray{};
    if (connected && (digest(jarData) != jdbcDigest || digest(gsonData) != gsonDigest))
    {
        state(tr("The local JDBC 0.2.0 and Gson 2.13.2 runtime JARs must match their verified digests."));
        return;
    }
    if (QMessageBox::question(this, tr("Run the local Java toolchain"),
        tr("Compile generated Java for release 17 with %1, then run the local %2 test with %3?")
            .arg(QDir::toNativeSeparators(compiler),
                connected ? tr("JDBC class loading") : tr("standalone routine"),
                QDir::toNativeSeparators(launcher)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;

    auto local = std::make_shared<QTemporaryDir>(localScript("java-build-XXXXXX"));
    if (!local->isValid())
    {
        state(tr("Could not create a private local build directory."));
        return;
    }
    const auto classes = local->filePath("classes");
    if (!QDir().mkdir(classes))
    {
        state(tr("Could not create the local classes directory."));
        return;
    }
    QString dependencies;
    if (connected)
    {
        const auto jdbc = local->filePath("jdbc.jar");
        const auto gson = local->filePath("gson.jar");
        if (!saveFile(jdbc, jarData) || !saveFile(gson, gsonData))
        {
            state(tr("Could not stage the verified JARs for the local build."));
            return;
        }
        dependencies = jdbc + QDir::listSeparator() + gson;
    }
    QStringList sources;
    const QDir artifact(generated_);
    const auto files = manifest_.value("files").toArray();
    const QString main = QString("src/main/java/") + javaDirectory;
    const QString tests = QString("src/test/java/") + javaDirectory;
    const QStringList expected = connected ? QStringList{main + "ConnectedRoutine.java"}
        : QStringList{main + "Routines.java", main + "RoutineRuntime.java", tests + "ExportSmoke.java"};
    for (const auto& relative : expected)
    {
        QByteArray claimed;
        for (const auto& item : files)
        {
            const auto file = item.toObject();
            if (file.value("path") == relative) claimed = file.value("sha256").toString().toLatin1();
        }
        const auto bytes = readFile(artifact.filePath(relative));
        const auto name = local->filePath(QFileInfo(relative).fileName());
        if (bytes.isEmpty() || digest(bytes) != claimed || !saveFile(name, bytes))
        {
            state(tr("Generated Java source changed. Regenerate the artifact before building."));
            return;
        }
        sources.append(name);
    }
    QString entry = QString::fromLatin1(javaPackage) + "ExportSmoke";
    if (connected)
    {
        entry = QStringLiteral("CheckLinkage");
        const auto path = local->filePath("CheckLinkage.java");
        if (!saveFile(path, QByteArrayLiteral(
            "public class CheckLinkage {\n"
            "  public static void main(String[] args) throws Exception {\n"
            "    Class.forName(\"stardataparadise.exported.ConnectedRoutine\").getDeclaredMethods();\n"
            "    System.out.println(\"Connected JDBC wrapper and SDK classes loaded.\");\n"
            "  }\n}\n")))
        {
            state(tr("Could not write the local linkage test."));
            return;
        }
        sources.append(path);
    }
    QStringList arguments{"-J-Xmx256m", "--release", "17", "-encoding", "UTF-8", "-proc:none", "-d", classes};
    if (connected) arguments.append({"-classpath", dependencies});
    arguments.append(sources);
    log_->clear();
    views_->setCurrentWidget(log_);
    state(tr("Compiling the generated Java with the selected local JDK…"));
    start(compiler, arguments, {}, [this, connected, classes, dependencies, entry, launcher, local]
        (bool success, const QString& detail) {
        log_->appendPlainText(detail);
        if (!success) { state(tr("Local Java compilation failed.")); return; }
        const auto classpath = connected ? classes + QDir::listSeparator() + dependencies : classes;
        state(tr("Executing the local %1 test…").arg(connected ? tr("JDBC linkage") : tr("standalone routine")));
        start(launcher, {"-Xmx128m", "-cp", classpath, entry}, {},
            [this, connected, local](bool ran, const QString& output) {
                log_->appendPlainText(output);
                if (!ran) { state(tr("Compiled successfully. The local Java test failed.")); return; }
                state(connected ? tr("Connected wrapper compiled. JDBC linkage test passed. Routine execution requires Server.")
                    : tr("Standalone Java compiled and executed successfully."));
            });
    });
}
}

QDialog* showRoutineExport(const Profile& profile, const QJsonObject& routine, QWidget* parent)
{
    auto* dialog = new ExportDialog(profile, routine, parent);
    dialog->show();
    return dialog;
}
}
