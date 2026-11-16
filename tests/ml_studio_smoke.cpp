#include "dataset_studio.hpp"
#include "history_decision_smoke_support.hpp"
#include "job_center.hpp"
#include "ml_studio.hpp"

#include <QAbstractItemModel>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSpinBox>
#include <QTableWidget>
#include <QThreadPool>
#include <QTreeWidget>
#include <QUuid>
#include <QVariant>

using namespace studio_smoke;
using namespace wb;

namespace
{
constexpr auto clusterModel = "wb_ml_clusters";
constexpr auto classifierModel = "wb_ml_classifier";
constexpr auto mlpModel = "wb_ml_resumable";

QString token() { return QUuid::createUuid().toString(QUuid::Id128); }

QJsonObject json(const QPlainTextEdit& view)
{
    return QJsonDocument::fromJson(view.toPlainText().toUtf8()).object();
}

QString pretty(const QJsonObject& value) { return QString::fromUtf8(QJsonDocument(value).toJson()); }

void waitEnabled(QDialog& dialog, const char* action)
{
    require(waitFor([&] { return control<QPushButton>(dialog, action).isEnabled(); }), action);
}

void confirmAction(QDialog& dialog, const char* action)
{
    waitEnabled(dialog, action);
    QTimer::singleShot(0, &dialog, [&] {
        auto* confirmation = dialog.findChild<QMessageBox*>();
        if (!confirmation) confirmation = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        require(confirmation != nullptr && confirmation->button(QMessageBox::Yes) != nullptr,
            "the user reviews the operation before committing it");
        confirmation->button(QMessageBox::Yes)->click();
    });
    click(dialog, action);
}

QJsonObject job(SqlSession& session, sdp_job_operation operation, const QJsonObject& body)
{
    const auto encoded = QJsonDocument(body).toJson(QJsonDocument::Compact);
    return success(await([&](auto done) {
        return session.request([&](auto handle, auto* output, auto* error) {
            return sdp_job_command(handle, operation, wire(encoded), output, error);
        }, done);
    }), "real server job command").data;
}

bool settled(const QJsonObject& state)
{
    const auto label = state.value("state").toString();
    return label == u"succeeded" || label == u"paused" || label == u"failed" || label == u"cancelled";
}

QJsonObject waitJob(SqlSession& session, const QString& id)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < 30000)
    {
        const auto current = job(session, SDP_JOB_WATCH, {{"job_id", id}});
        if (settled(current)) return current;
        QEventLoop delay;
        QTimer::singleShot(70, &delay, &QEventLoop::quit);
        delay.exec();
    }
    require(false, "worker did not reach the expected persisted state");
    return {};
}

bool sha256(const QString& value)
{
    return QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(value).hasMatch();
}

void seed(SqlSession& session)
{
    write(session, "CREATE TABLE wb_ml_measurements (sample_key BIGINT PRIMARY KEY, axis_x BIGINT,"
                   " axis_y BIGINT, class_name TEXT)");
    QStringList records;
    for (int index = 0; index < 48; ++index)
    {
        const bool positive = index % 2;
        const int x = (positive ? 1 : -1) * (20 + (index / 2) % 3);
        const int y = (index / 2) % 5 - 2;
        records.push_back(QStringLiteral("(%1,%2,%3,'%4')").arg(index + 1).arg(x).arg(y)
            .arg(positive ? QStringLiteral("warm") : QStringLiteral("cold")));
    }
    write(session, "INSERT INTO wb_ml_measurements VALUES " + records.join(u','));
    require(select(session, "SELECT COUNT(*) FROM wb_ml_measurements").size() == 1, "model source query returns data");
}

QString createDataset(SqlSession& session, QWidget& window, bool supervised)
{
    QJsonObject selected;
    const QPointer<QDialog> studio(showDatasetStudio(session, &window,
        [&](const QJsonObject& value) { selected = value; }));
    require(studio != nullptr, "Dataset Studio opened");
    control<QPlainTextEdit>(*studio, "datasetSql").setPlainText(
        "SELECT sample_key,axis_x,axis_y,class_name FROM wb_ml_measurements ORDER BY sample_key");
    const QJsonObject x{{"column", "axis_x"}, {"transform", supervised ? "standard" : "numeric"}, {"impute", "reject"}};
    const QJsonObject y{{"column", "axis_y"}, {"transform", "numeric"}, {"impute", "reject"}};
    QJsonObject options;
    if (supervised)
    {
        options = {{"features", QJsonArray{x}}, {"target", "class_name"}, {"target_kind", "categorical"},
            {"split", QJsonObject{{"mode", "stratified"}, {"seed", 37}, {"train", 50}, {"validation", 25}}}};
    }
    else
    {
        options = {{"features", QJsonArray{x, y}},
            {"split", QJsonObject{{"mode", "time"}, {"time", "sample_key"}, {"train", 60}, {"validation", 20}}}};
    }
    control<QPlainTextEdit>(*studio, "datasetOptions").setPlainText(pretty(options));
    click(*studio, "datasetPreview");
    if (!waitFor([&] { return control<QPushButton>(*studio, "datasetCreate").isEnabled(); }))
        std::cerr << control<QLabel>(*studio, "datasetStatus").text().toStdString() << '\n';
    click(*studio, "datasetCreate");
    if (!waitFor([&] { return control<QPushButton>(*studio, "datasetUse").isEnabled(); }))
        std::cerr << control<QLabel>(*studio, "datasetStatus").text().toStdString() << '\n';
    const auto snapshot = json(control<QPlainTextEdit>(*studio, "datasetSummary"));
    const auto split = snapshot.value("prepared").toObject().value("split").toObject();
    require(snapshot.value("row_count").toInt() == 48 && !split.value("train").toArray().isEmpty() &&
        !split.value("validation").toArray().isEmpty() && !split.value("test").toArray().isEmpty(),
        "Dataset Studio shows the actual source count and populated split");
    require(control<QTableWidget>(*studio, "datasetSample").rowCount() > 0 &&
        control<QTreeWidget>(*studio, "datasetSplits").topLevelItemCount() > 0,
        "the Dataset Studio displays samples separately from the actual split");
    click(*studio, "datasetUse");
    require(selected.value("dataset_id") == snapshot.value("dataset_id") &&
        selected.value("dataset_id").toString().size() == 32, "Dataset Studio returned its committed snapshot ID");
    if (studio) studio->close();
    return selected.value("dataset_id").toString();
}

template<class Matches>
QJsonObject modelResponse(QDialog& studio, Matches matches, const char* description)
{
    const auto latest = [&] { return studio.property("mlLastResponse").toJsonObject(); };
    if (!waitFor([&] { return matches(latest()); }))
        std::cerr << control<QLabel>(studio, "mlStatus").text().toStdString() << '\n'
                  << control<QPlainTextEdit>(studio, "mlResult").toPlainText().toStdString() << '\n';
    require(matches(latest()), description);
    require(json(control<QPlainTextEdit>(studio, "mlResult")) == latest(), "ML Studio displays the authoritative response");
    return latest();
}

QDialog* modelStudio(SqlSession& session, QWidget& window)
{
    auto* studio = showMlStudio(session, catalog(session), {}, &window, {}, {});
    require(studio != nullptr, "ML Studio opened");
    return studio;
}

QString trainModel(QDialog& studio, const QString& name, const QString& algorithm, const QString& dataset,
    const QJsonObject& options, int pause = 0)
{
    auto& algorithms = control<QComboBox>(studio, "mlAlgorithm");
    const auto algorithmIndex = algorithms.findData(algorithm);
    require(algorithmIndex >= 0, "selected model algorithm is available");
    algorithms.setCurrentIndex(algorithmIndex);
    control<QLineEdit>(studio, "mlModelName").setText(name);
    confirmAction(studio, "mlCreate");
    modelResponse(studio, [&](const QJsonObject& created) {
        return created.value("algorithm").toString() == algorithm && created.value("active_version").isNull();
    }, "ML Studio created an inactive model");
    control<QLineEdit>(studio, "mlDatasetId").setText(dataset);
    control<QPlainTextEdit>(studio, "mlTrainingOptions").setPlainText(pretty(options));
    if (pause) control<QSpinBox>(studio, "mlPauseAfterEpoch").setValue(pause);
    confirmAction(studio, "mlTrain");
    const auto submitted = modelResponse(studio, [](const QJsonObject& reply) {
        return reply.value("job_id").toString().size() == 32;
    }, "ML Studio obtained the server training job ID");
    return submitted.value("job_id").toString();
}

QJsonObject predictSql(SqlSession& session, const QString& name, const QJsonObject& features)
{
    const QJsonObject body{{"name", name}, {"features", features}};
    auto encoded = QString::fromUtf8(QJsonDocument(body).toJson(QJsonDocument::Compact));
    encoded.replace(QStringLiteral("'"), QStringLiteral("''"));
    const auto result = success(sql(session, QStringLiteral("PREDICT MODEL USING '%1'").arg(encoded)),
        "actual SQL model prediction");
    const auto prediction = result.data.value("model").toObject();
    require(!prediction.isEmpty(), "SQL prediction returned the real model response");
    return prediction;
}

void assessModel(SqlSession& session, QDialog& studio, const QString& name, const QString& algorithm,
    const QString& dataset, const QString& id)
{
    const auto status = waitJob(session, id);
    const auto artifact = status.value("result").toObject();
    require(status.value("state").toString() == u"succeeded" && artifact.value("dataset_id").toString() == dataset &&
        artifact.value("version").toInt() > 0 && sha256(artifact.value("checksum").toString()),
        "training published a real checksummed model version");
    const auto version = artifact.value("version").toInt();
    control<QSpinBox>(studio, "mlModelVersion").setValue(version);
    click(studio, "mlEvaluate");
    const auto evaluation = modelResponse(studio, [&](const QJsonObject& reply) {
        return reply.value("version").toInt() == version && reply.value("metrics").isObject();
    }, "ML Studio fetched metrics for the published version");
    require(evaluation.value("algorithm").toString() == algorithm &&
        evaluation.value("checksum") == artifact.value("checksum") && evaluation.value("dataset_id").toString() == dataset,
        "evaluation is bound to the exact same dataset and artifact as the job");
    const auto metrics = evaluation.value("metrics").toObject().value("test").toObject();
    require(metrics.value("count").toInt() > 0, "evaluation has real held-out sample counts");
    if (algorithm == u"kmeans")
        require(evaluation.value("artifact").toObject().value("centers").toArray().size() == 2,
            "K-Means artifact has both real cluster centers");
    else
        require(metrics.value("confusion").isObject() && metrics.value("accuracy").toDouble() >= 0.9,
            "classification reports a real confusion matrix and separated-class score");

    confirmAction(studio, "mlActivate");
    modelResponse(studio, [&](const QJsonObject& reply) { return reply.value("active_version").toInt() == version; },
        "reviewed activation selected the trained artifact");

    QJsonObject low{{"axis_x", -20}}, high{{"axis_x", 20}};
    if (algorithm == u"kmeans")
    {
        low.insert("axis_y", 0);
        high.insert("axis_y", 0);
    }
    const auto left = predictSql(session, name, low);
    const auto right = predictSql(session, name, high);
    require(left.value("version").toInt() == version && right.value("version").toInt() == version,
        "SQL prediction uses the activated version");
    if (algorithm == u"kmeans")
    {
        require(left.value("cluster").isDouble() && right.value("cluster").isDouble() &&
            left.value("cluster") != right.value("cluster"), "SQL inference separates both known clusters");
    }
    else
    {
        require(left.value("label").isDouble() && right.value("label").isDouble() &&
            left.value("label_value").toString() == u"cold" && right.value("label_value").toString() == u"warm",
            "SQL inference returns both original classification labels");
    }

    control<QPlainTextEdit>(studio, "mlFeatures").setPlainText(pretty(low));
    click(studio, "mlPrediction");
    const auto shown = modelResponse(studio, [](const QJsonObject& reply) {
        return reply.value("prediction").isDouble();
    }, "ML Studio performed inference with its feature form");
    require(shown.value("version") == left.value("version") && shown.value("prediction") == left.value("prediction"),
        "Workbench model prediction agrees with genuine SQL inference");
}

QJsonObject expectCenter(QDialog& center, const QString& id, QStringView desired, int timeout = 20000)
{
    const auto current = [&] { return json(control<QPlainTextEdit>(center, "jobResult")); };
    const auto matches = [&] {
        const auto reply = current();
        return reply.value("job_id").toString() == id &&
            (desired.isEmpty() || reply.value("state").toString() == desired);
    };
    if (!waitFor(matches, timeout)) std::cerr << control<QLabel>(center, "jobStatus").text().toStdString() << '\n';
    require(matches(), "Job Center obtained the requested ID and persisted Server state");
    if (!desired.isEmpty())
        require(center.property("jobState").toString() == desired, "Job Center state matches the server response");
    return current();
}

QJsonObject checkpoint(QDialog& center, const QString& id)
{
    waitEnabled(center, "jobCheckpoint");
    click(center, "jobCheckpoint");
    auto& detail = control<QPlainTextEdit>(center, "jobCheckpointResult");
    const auto loaded = [&] { return json(detail).value("job_id").toString() == id; };
    require(waitFor(loaded), "Job Center retrieved the paused job's checkpoint");
    const auto result = json(detail);
    require(result.value("epoch").toInt() == 2 && sha256(result.value("checksum").toString()),
        "paused checkpoint has the real epoch and content checksum");
    return result;
}

void saveState(const QString& path, const QJsonObject& manifest)
{
    QSaveFile file(path);
    require(file.open(QIODevice::WriteOnly), "fixture metadata can be saved");
    const auto bytes = QJsonDocument(manifest).toJson();
    require(file.write(bytes) == bytes.size() && file.commit(), "fixture checkpoint metadata was saved atomically");
}

QJsonObject loadState(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "new Workbench process can read the fixture checkpoint metadata");
    return QJsonDocument::fromJson(file.readAll()).object();
}

void expectAvailability(SqlSession& session, QWidget& window, const QString& metadata, bool enabled)
{
    auto* studio = modelStudio(session, window);
    const auto remoteMlp = session.capabilities().value("mlp_cpu_v1");
    require(remoteMlp.isBool() && remoteMlp.toBool() == enabled, "Server fixture reported the requested MLP availability");
    const auto path = QFileInfo(metadata).dir().filePath(QStringLiteral("missing-fixture-python.exe"));
    require(!QFileInfo::exists(path), "local Python fixture path is missing");
    control<QLineEdit>(*studio, "mlLocalPython").setText(path);
    click(*studio, "mlRefreshAvailability");
    auto& status = control<QPlainTextEdit>(*studio, "mlAvailability");
    const auto row = [&](QStringView word) {
        for (const auto& line : status.toPlainText().split(u'\n'))
            if (line.contains(word, Qt::CaseInsensitive)) return line;
        return QString{};
    };
    const auto complete = [&] {
        constexpr auto unavailable = u"unavailable";
        const auto mlp = row(u"MLP");
        const bool mlpUnavailable = mlp.contains(unavailable, Qt::CaseInsensitive);
        return row(u"GPU").contains(unavailable, Qt::CaseInsensitive) &&
            (enabled ? !mlpUnavailable && mlp.contains(u"available", Qt::CaseInsensitive) : mlpUnavailable) &&
            row(u"Local").contains(unavailable, Qt::CaseInsensitive) &&
            !row(u"Server GPU probe reported").isEmpty();
    };
    if (!waitFor(complete)) std::cerr << status.toPlainText().toStdString() << '\n';
    require(complete() && row(u"GPU") != row(u"Local") && row(u"MLP") != row(u"Local"),
        "Workbench distinguishes remote GPU, remote MLP, and local executable checks");
    auto& algorithm = control<QComboBox>(*studio, "mlAlgorithm");
    const auto index = algorithm.findData(QStringLiteral("mlp"));
    const bool selectable = index >= 0 && (algorithm.model()->flags(algorithm.model()->index(index, 0)) & Qt::ItemIsEnabled);
    require(selectable == enabled, "missing remote MLP module does not offer unsupported training");
    studio->close();
}

void trainPhase(SqlSession& session, QWidget& window, const QString& metadata, const QString& recent)
{
    seed(session);
    const auto clusters = createDataset(session, window, false);
    const auto classifier = createDataset(session, window, true);
    require(clusters != classifier, "independent supervised and unsupervised dataset snapshots");

    auto* studio = modelStudio(session, window);
    const auto clusterJob = trainModel(*studio, clusterModel, "kmeans", clusters, {{"k", 2}});
    assessModel(session, *studio, clusterModel, "kmeans", clusters, clusterJob);
    studio->close();
    studio = modelStudio(session, window);
    const auto classifierJob = trainModel(*studio, classifierModel, "logistic", classifier, {});
    assessModel(session, *studio, classifierModel, "logistic", classifier, classifierJob);
    studio->close();
    std::cout << "S41-T01: SQL datasets trained K-Means and classifier; artifact metrics and activated SQL predictions matched\n";

    const QJsonObject options{{"hidden_layers", QJsonArray{8}}, {"activation", "tanh"}, {"loss", "bce"},
        {"optimizer", "adam"}, {"learning_rate", 0.03}, {"batch_size", 4}, {"epochs", 20}, {"patience", 20}};
    studio = modelStudio(session, window);
    const auto paused = trainModel(*studio, mlpModel, "mlp", classifier, options, 2);
    require(waitJob(session, paused).value("state").toString() == u"paused", "MLP job paused at the requested epoch");
    studio->close();
    auto* center = showJobCenter(session, &window, recent, paused, mlpModel);
    require(center != nullptr, "Job Center opened for the submitted MLP job");
    expectCenter(*center, paused, u"paused");
    const auto saved = checkpoint(*center, paused);
    require(saved.value("dataset_id").toString() == classifier, "checkpoint pinned the source dataset");
    saveState(metadata, saved);
    center->close();
    expectAvailability(session, window, metadata, true);
    std::cout << "First Workbench process exits with a paused MLP job and verified checkpoint\n";
}

void reopenPhase(SqlSession& session, SqlSession& monitor, QWidget& window, const QString& metadata, const QString& recent)
{
    const auto prior = loadState(metadata);
    const auto paused = prior.value("job_id").toString();
    require(paused.size() == 32 && sha256(prior.value("checksum").toString()), "saved fixture has the original job ID and hash");
    auto* center = showJobCenter(session, &window, recent, {}, mlpModel);
    require(center != nullptr, "fresh Workbench process opens the Job Center");
    expectCenter(*center, paused, u"paused");
    require(control<QLineEdit>(*center, "jobId").text() == paused && checkpoint(*center, paused) == prior,
        "new Workbench process looked up its recent ID and exact existing checkpoint on Server");
    confirmAction(*center, "jobResume");
    auto& jobId = control<QLineEdit>(*center, "jobId");
    const auto resumed = [&] { return jobId.text(); };
    require(waitFor([&] { return resumed().size() == 32 && resumed() != paused; }),
        "resume created a distinct Server job ID");
    const auto newId = resumed();
    const auto finished = waitJob(monitor, newId);
    require(finished.value("state").toString() == u"succeeded" && finished.value("result").isObject(),
        "resumed MLP run produced a model artifact");
    jobId.setText(newId);
    waitEnabled(*center, "jobOpen");
    click(*center, "jobOpen");
    expectCenter(*center, newId, u"succeeded");
    require(job(monitor, SDP_JOB_STATUS, {{"job_id", paused}}).value("state").toString() == u"paused",
        "resuming did not mutate the original checkpoint job");

    const auto failed = job(monitor, SDP_JOB_SUBMIT,
        {{"kind", "compute"}, {"count", 30000}, {"output_bytes", 1}, {"request_token", token()}}).value("job_id").toString();
    const auto failedStatus = waitJob(monitor, failed);
    require(failedStatus.value("state").toString() == u"failed" && failedStatus.value("reason").toString() == u"output_limit",
        "failed fixture is a real bounded Server worker failure");
    jobId.setText(failed);
    waitEnabled(*center, "jobOpen");
    click(*center, "jobOpen");
    require(expectCenter(*center, failed, u"failed").value("result").isNull(),
        "Job Center didn't show a failed result as an artifact");

    const auto cancelling = job(monitor, SDP_JOB_SUBMIT,
        {{"kind", "compute"}, {"count", 1000000000}, {"timeout_ms", 30000}, {"request_token", token()}}).value("job_id").toString();
    jobId.setText(cancelling);
    waitEnabled(*center, "jobOpen");
    click(*center, "jobOpen");
    expectCenter(*center, cancelling, {});
    confirmAction(*center, "jobCancel");
    const auto cancelled = waitJob(monitor, cancelling);
    require(cancelled.value("state").toString() == u"cancelled", "Job Center cancelled the actual Server job");
    jobId.setText(cancelling);
    waitEnabled(*center, "jobOpen");
    click(*center, "jobOpen");
    require(expectCenter(*center, cancelling, u"cancelled").value("result").isNull(),
        "Job Center didn't show a cancelled result as an artifact");
    std::cout << "S41-T02: fresh Workbench recovered the paused MLP checkpoint, resumed it, and distinguished real failure/cancel\n";
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    require(argc == 5, "expected profile, phase and two fixture file arguments");
    const auto profile = fixtureProfile(2, argv);
    const auto phase = QString::fromLatin1(argv[2]);
    const auto metadata = QString::fromLocal8Bit(argv[3]);
    const auto recent = QString::fromLocal8Bit(argv[4]);
    Password administrator{fixturePassword()};
    QThreadPool workers;
    SqlSession session(profile, connect(profile, administrator), workers);
    QWidget window;
    window.show();
    if (phase == u"train") trainPhase(session, window, metadata, recent);
    else if (phase == u"reopen")
    {
        SqlSession monitor(profile, connect(profile, administrator), workers);
        reopenPhase(session, monitor, window, metadata, recent);
    }
    else if (phase == u"disabled")
    {
        expectAvailability(session, window, metadata, false);
        std::cout << "S41-T03: missing remote MLP, unavailable GPU, and local Python absence were distinguished\n";
    }
    else require(false, "unknown fixture phase");
    return 0;
}
