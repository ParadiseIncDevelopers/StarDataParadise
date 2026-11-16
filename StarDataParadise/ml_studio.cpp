#include "ml_studio.hpp"
#include "dataset_studio.hpp"
#include "sql_session.hpp"
#include "table_codec.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <string_view>

namespace wb
{
namespace
{
QString qualified(const QJsonObject& metadata)
{
    QStringList parts;
    for (const auto& item : metadata.value("qualified_name").toArray())
        parts.append(transfer::identifier(item.toString()));
    return parts.size() == 3 ? parts.join('.') : QString{};
}

QString pretty(const QJsonObject& object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
}

QString token()
{
    return QString::fromLatin1(QUuid::createUuid().toRfc4122().toHex());
}

bool parseObject(const QString& text, QJsonObject& object, int maximum = 8192)
{
    const auto bytes = text.toUtf8();
    if (bytes.size() > maximum) return false;
    QJsonParseError failed;
    const auto data = QJsonDocument::fromJson(bytes, &failed);
    if (failed.error != QJsonParseError::NoError || !data.isObject()) return false;
    object = data.object();
    return true;
}

struct Algorithm
{
    const char* name;
    const char* label;
    const char* capability;
    const char* validKeys;
    const char* example;
    bool supervised;
};

constexpr Algorithm algorithms[]{
    {"linear", "Linear regression", "supervised_ml_v1", "ridge", R"({"ridge": 0})", true},
    {"logistic", "Logistic classification", "supervised_ml_v1", "iterations learning_rate l2", R"({"iterations": 512})", true},
    {"tree", "Decision tree", "supervised_ml_v1", "max_depth min_leaf", R"({"max_depth": 4})", true},
    {"forest", "Random forest", "supervised_ml_v1", "max_depth min_leaf trees max_features", R"({"trees": 8})", true},
    {"kmeans", "K-Means", "unsupervised_ml_v1", "k max_iterations tolerance", R"({"k": 2})", false},
    {"dbscan", "DBSCAN", "unsupervised_ml_v1", "eps min_points", R"({"eps": 1.0, "min_points": 3})", false},
    {"pca", "PCA", "unsupervised_ml_v1", "components", R"({"components": 2})", false},
    {"mlp", "MLP (binary classification)", "mlp_cpu_v1",
        "hidden_layers activation loss optimizer learning_rate batch_size epochs patience min_delta",
        R"({"hidden_layers": [8], "activation": "tanh", "optimizer": "adam", "loss": "bce", "epochs": 120})", true}
};

bool integerIn(const QJsonValue& value, qint64 minimum, qint64 maximum)
{
    const auto candidate = value.toInteger(-1);
    return candidate >= minimum && candidate <= maximum;
}

QString invalidOption(const Algorithm& algorithm, const QJsonObject& options)
{
    const auto supported = QString::fromLatin1(algorithm.validKeys).split(' ');
    for (auto it = options.begin(); it != options.end(); ++it)
        if (!supported.contains(it.key())) return it.key();
    if (std::string_view(algorithm.name) == "kmeans" && !integerIn(options.value("k"), 1, 16)) return "k";
    if (std::string_view(algorithm.name) == "pca" && !integerIn(options.value("components"), 1, 64)) return "components";
    if (std::string_view(algorithm.name) == "dbscan")
    {
        if (!options.value("eps").isDouble() || options.value("eps").toDouble() <= 0) return "eps";
        if (!integerIn(options.value("min_points"), 1, 1024)) return "min_points";
    }
    const std::pair<const char*, int> integers[]{
        {"iterations", 2000}, {"max_iterations", 200}, {"epochs", 1000}, {"patience", 1000}, {"batch_size", 256}
    };
    for (const auto& [name, limit] : integers)
        if (options.contains(name) && !integerIn(options.value(name), 1, limit)) return QString::fromLatin1(name);
    const auto hidden = options.value("hidden_layers");
    if (!hidden.isUndefined())
    {
        if (!hidden.isArray() || hidden.toArray().isEmpty() || hidden.toArray().size() > 3) return "hidden_layers";
        for (const auto& units : hidden.toArray())
            if (!integerIn(units, 1, 32)) return "hidden_layers";
    }
    return {};
}

QString formatMetric(const QJsonValue& value)
{
    if (value.isNull()) return QStringLiteral("N/A");
    if (value.isString()) return value.toString();
    if (value.isDouble()) return QString::number(value.toDouble(), 'g', 16);
    if (value.isBool()) return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return {};
}

void addMetrics(QTreeWidgetItem* parent, const QJsonObject& values, int depth = 0)
{
    if (depth > 6) return;
    for (auto it = values.begin(); it != values.end(); ++it)
    {
        const auto& value = it.value();
        auto* row = new QTreeWidgetItem(parent, {it.key(), formatMetric(value)});
        if (value.isObject()) addMetrics(row, value.toObject(), depth + 1);
        else if (value.isArray())
        {
            const auto series = value.toArray();
            const auto count = std::min<qsizetype>(series.size(), 1024);
            for (qsizetype i = 0; i < count; ++i)
            {
                const auto entry = series.at(i);
                auto* point = new QTreeWidgetItem(row, {QString::number(i + 1), formatMetric(entry)});
                if (entry.isObject()) addMetrics(point, entry.toObject(), depth + 1);
            }
        }
    }
}

class MlStudio final : public QDialog
{
public:
    MlStudio(SqlSession&, const QJsonArray& catalog, const QJsonObject& model, QWidget*,
        std::function<void()> refresh, std::function<void(const QString&,const QString&)> watch,
        std::function<void(const QJsonObject&)> openRoutine, std::function<void(const QJsonObject&)> exportRoutine);
    void reject() override;

private:
    using Finished = std::function<void(const QJsonObject&)>;
    QPushButton* action(QHBoxLayout&, const QString&, const char*, std::function<void()>);
    void trainingPage();
    void versionPage();
    void routinesPage(const QJsonArray&);
    void availability(bool probe);
    void selectModel(const QJsonObject&);
    void selectDataset(const QJsonObject&);
    void changeAlgorithm();
    void modelAction(sdp_model_operation operation);
    void predict();
    void compare();
    void renderVersions(const QJsonObject&);
    void renderMetrics(const QJsonObject&);
    void present(const QJsonObject&);
    void call(sdp_model_operation, const QString&, QJsonObject, bool mutation, Finished = {});
    void enabled(bool running);

    SqlSession& session_;
    const std::function<void()> refresh_;
    const std::function<void(const QString&,const QString&)> watch_;
    const std::function<void(const QJsonObject&)> openRoutine_, exportRoutine_;
    QLineEdit *name_{}, *datasetId_{}, *localPython_{}, *compareModel_{};
    QComboBox *models_{}, *algorithm_{}, *device_{}, *split_{}, *routines_{};
    QSpinBox *seed_{}, *pause_{}, *memory_{}, *deviceIndex_{}, *version_{}, *otherVersion_{};
    QCheckBox* fallback_{};
    QTreeWidget *versions_{}, *metrics_{};
    QLabel *status_{}, *datasetDetails_{}, *evidence_{};
    QPlainTextEdit *options_{}, *features_{}, *result_{}, *request_{}, *availability_{};
    QTabWidget* tabs_{};
    QList<QPushButton*> actions_;
    QList<QPushButton*> mutations_;
    QJsonObject dataset_;
    QString verifiedModel_, verifiedAlgorithm_;
    bool inFlight_{};
};

QPushButton* MlStudio::action(QHBoxLayout& layout, const QString& title, const char* name,
    std::function<void()> invoke)
{
    auto* button = new QPushButton(title);
    button->setObjectName(QString::fromLatin1(name));
    layout.addWidget(button);
    actions_.push_back(button);
    connect(button, &QPushButton::clicked, this, std::move(invoke));
    return button;
}

MlStudio::MlStudio(SqlSession& session, const QJsonArray& catalog, const QJsonObject& initial, QWidget* parent,
    std::function<void()> refresh, std::function<void(const QString&,const QString&)> watch,
    std::function<void(const QJsonObject&)> openRoutine, std::function<void(const QJsonObject&)> exportRoutine)
    : QDialog(parent), session_(session), refresh_(std::move(refresh)), watch_(std::move(watch)),
      openRoutine_(std::move(openRoutine)), exportRoutine_(std::move(exportRoutine))
{
    setObjectName("mlStudio");
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Machine Learning Studio"));
    resize(875, 720);
    auto* layout = new QVBoxLayout(this);
    auto* modelForm = new QFormLayout;
    layout->addLayout(modelForm);
    models_ = new QComboBox;
    models_->setObjectName("mlModelCatalog");
    models_->addItem(tr("Choose a catalog model"), QJsonObject{});
    for (const auto& entry : catalog)
    {
        const auto model = entry.toObject();
        if (model.value("kind").toInt() == 9) models_->addItem(qualified(model), model);
    }
    modelForm->addRow(tr("Models on %1").arg(endpoint(session_.profile())), models_);
    name_ = new QLineEdit;
    name_->setObjectName("mlModelName");
    name_->setMaxLength(392);
    modelForm->addRow(tr("Model name"), name_);
    algorithm_ = new QComboBox;
    algorithm_->setObjectName("mlAlgorithm");
    auto& available = *qobject_cast<QStandardItemModel*>(algorithm_->model());
    for (const auto& algorithm : algorithms)
    {
        algorithm_->addItem(tr(algorithm.label), QString::fromLatin1(algorithm.name));
        available.item(algorithm_->count() - 1)->setEnabled(
            session_.capabilities().value(QString::fromLatin1(algorithm.capability)).toBool());
    }
    modelForm->addRow(tr("Algorithm"), algorithm_);

    auto* actions = new QHBoxLayout;
    layout->addLayout(actions);
    mutations_ << action(*actions, tr("Create model…"), "mlCreate", [this] { modelAction(SDP_MODEL_CREATE); });
    action(*actions, tr("Read model registry"), "mlInspect", [this] { modelAction(SDP_MODEL_STATUS); });
    tabs_ = new QTabWidget;
    layout->addWidget(tabs_, 1);
    trainingPage();
    versionPage();
    routinesPage(catalog);

    result_ = new QPlainTextEdit;
    result_->setObjectName("mlResult");
    result_->setReadOnly(true);
    tabs_->addTab(result_, tr("Server response"));
    auto* requestPage = new QWidget;
    auto* requestLayout = new QVBoxLayout(requestPage);
    request_ = new QPlainTextEdit;
    request_->setObjectName("mlRequest");
    request_->setReadOnly(true);
    requestLayout->addWidget(request_);
    auto* copy = new QPushButton(tr("Copy API request"));
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(request_->toPlainText()); });
    requestLayout->addWidget(copy);
    tabs_->addTab(requestPage, tr("API request"));
    status_ = new QLabel;
    status_->setObjectName("mlStatus");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    layout->addWidget(status_);

    connect(models_, &QComboBox::currentIndexChanged, this, [this] {
        selectModel(models_->currentData().toJsonObject());
        if (!name_->text().isEmpty()) modelAction(SDP_MODEL_STATUS);
    });
    connect(algorithm_, &QComboBox::currentIndexChanged, this, [this] { changeAlgorithm(); });
    changeAlgorithm();
    availability(false);
    if (!initial.isEmpty())
    {
        selectModel(initial);
        QTimer::singleShot(0, this, [this] { modelAction(SDP_MODEL_STATUS); });
    }
    enabled(false);
}


void MlStudio::trainingPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    auto* datasetRow = new QHBoxLayout;
    datasetId_ = new QLineEdit;
    datasetId_->setObjectName("mlDatasetId");
    datasetId_->setMaxLength(32);
    datasetRow->addWidget(datasetId_, 1);
    action(*datasetRow, tr("Open Dataset Studio…"), "mlOpenDataset", [this] {
        const QPointer<MlStudio> self(this);
        showDatasetStudio(session_, this, [self](const QJsonObject& dataset) {
            if (self) self->selectDataset(dataset);
        });
    });
    form->addRow(tr("Immutable dataset ID"), datasetRow);
    datasetDetails_ = new QLabel;
    datasetDetails_->setTextFormat(Qt::PlainText);
    datasetDetails_->setWordWrap(true);
    form->addRow(datasetDetails_);
    seed_ = new QSpinBox;
    seed_->setRange(0, 2147483647);
    seed_->setValue(42);
    form->addRow(tr("Seed"), seed_);
    options_ = new QPlainTextEdit;
    options_->setObjectName("mlTrainingOptions");
    options_->setMaximumHeight(82);
    form->addRow(tr("Algorithm options (JSON)"), options_);
    pause_ = new QSpinBox;
    pause_->setObjectName("mlPauseAfterEpoch");
    pause_->setRange(0, 999);
    pause_->setSpecialValueText(tr("Run to completion"));
    form->addRow(tr("Pause MLP after epoch"), pause_);
    device_ = new QComboBox;
    device_->addItem(tr("CPU"), QStringLiteral("cpu"));
    device_->addItem(tr("Request GPU"), QStringLiteral("gpu"));
    form->addRow(tr("Run on the connected Server"), device_);
    auto* deviceRow = new QHBoxLayout;
    deviceIndex_ = new QSpinBox;
    deviceIndex_->setRange(0, 31);
    deviceIndex_->setPrefix(tr("GPU index "));
    deviceRow->addWidget(deviceIndex_);
    memory_ = new QSpinBox;
    memory_->setRange(1, 131072);
    memory_->setValue(128);
    memory_->setSuffix(tr(" MiB GPU memory"));
    deviceRow->addWidget(memory_);
    fallback_ = new QCheckBox(tr("Allow Server CPU fallback"));
    deviceRow->addWidget(fallback_);
    form->addRow(deviceRow);
    const auto syncDevice = [this] {
        const auto requested = device_->currentIndex() == 1;
        deviceIndex_->setEnabled(requested);
        memory_->setEnabled(requested);
        fallback_->setEnabled(requested);
    };
    connect(device_, &QComboBox::currentIndexChanged, this, syncDevice);
    syncDevice();

    auto* local = new QHBoxLayout;
    localPython_ = new QLineEdit(QStandardPaths::findExecutable("python"));
    localPython_->setObjectName("mlLocalPython");
    local->addWidget(localPython_, 1);
    action(*local, tr("Refresh availability"), "mlRefreshAvailability", [this] { availability(true); });
    form->addRow(tr("Local Python for routine export"), local);
    availability_ = new QPlainTextEdit;
    availability_->setObjectName("mlAvailability");
    availability_->setReadOnly(true);
    availability_->setMaximumHeight(92);
    form->addRow(availability_);

    auto* actions = new QHBoxLayout;
    layout->addLayout(actions);
    mutations_ << action(*actions, tr("Review and train…"), "mlTrain", [this] { modelAction(SDP_MODEL_TRAIN); });
    actions->addStretch();
    layout->addStretch();
    tabs_->addTab(page, tr("Training"));
    connect(datasetId_, &QLineEdit::textChanged, this, [this](const QString& id) {
        if (dataset_.value("dataset_id") == id) return;
        dataset_ = {};
        datasetDetails_->clear();
    });
}

void MlStudio::versionPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    versions_ = new QTreeWidget;
    versions_->setObjectName("mlVersions");
    versions_->setHeaderLabels({tr("Version"), tr("Active"), tr("Dataset"), tr("Run"), tr("Artifact SHA-256")});
    versions_->setRootIsDecorated(false);
    versions_->setMaximumHeight(110);
    layout->addWidget(versions_);
    auto* tools = new QHBoxLayout;
    layout->addLayout(tools);
    version_ = new QSpinBox;
    version_->setObjectName("mlModelVersion");
    version_->setRange(1, 1000000);
    version_->setPrefix(tr("Version "));
    tools->addWidget(version_);
    connect(versions_, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (auto* item = versions_->currentItem()) version_->setValue(item->data(0, Qt::UserRole).toInt());
    });
    action(*tools, tr("Evaluate / verify"), "mlEvaluate", [this] { modelAction(SDP_MODEL_EVALUATE); });
    mutations_ << action(*tools, tr("Activate…"), "mlActivate", [this] { modelAction(SDP_MODEL_ACTIVATE); });
    mutations_ << action(*tools, tr("Deactivate…"), "mlDeactivate", [this] { modelAction(SDP_MODEL_DEACTIVATE); });
    mutations_ << action(*tools, tr("Roll back version…"), "mlRollback", [this] { modelAction(SDP_MODEL_ROLLBACK); });

    auto* form = new QFormLayout;
    layout->addLayout(form);
    auto* compareRow = new QHBoxLayout;
    compareModel_ = new QLineEdit;
    compareModel_->setMaxLength(392);
    compareModel_->setPlaceholderText(tr("Same model when left empty"));
    compareRow->addWidget(compareModel_, 1);
    otherVersion_ = new QSpinBox;
    otherVersion_->setRange(1, 1000000);
    otherVersion_->setValue(2);
    otherVersion_->setPrefix(tr("Version "));
    compareRow->addWidget(otherVersion_);
    split_ = new QComboBox;
    split_->addItems({"train", "validation", "test"});
    split_->setCurrentIndex(2);
    compareRow->addWidget(split_);
    action(*compareRow, tr("Compare"), "mlCompare", [this] { compare(); });
    form->addRow(tr("Compare with"), compareRow);
    features_ = new QPlainTextEdit("{}");
    features_->setObjectName("mlFeatures");
    features_->setMaximumHeight(58);
    form->addRow(tr("Prediction features (JSON)"), features_);
    auto* infer = new QHBoxLayout;
    layout->addLayout(infer);
    action(*infer, tr("Predict with active version"), "mlPrediction", [this] { predict(); });
    infer->addStretch();
    evidence_ = new QLabel;
    evidence_->setTextFormat(Qt::PlainText);
    evidence_->setWordWrap(true);
    layout->addWidget(evidence_);
    metrics_ = new QTreeWidget;
    metrics_->setObjectName("mlMetrics");
    metrics_->setHeaderLabels({tr("Server metric"), tr("Value")});
    layout->addWidget(metrics_, 1);
    tabs_->addTab(page, tr("Versions and evaluation"));
}

void MlStudio::routinesPage(const QJsonArray& catalog)
{
    if (!openRoutine_ && !exportRoutine_) return;
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    auto* target = new QHBoxLayout;
    layout->addLayout(target);
    routines_ = new QComboBox;
    target->addWidget(routines_, 1);
    for (const auto& entry : catalog)
    {
        const auto routine = entry.toObject();
        if (routine.value("kind").toInt() == 6)
            routines_->addItem(qualified(routine) + tr(" · revision %1").arg(routine.value("schema_version").toInt()), routine);
    }
    if (openRoutine_)
        action(*target, tr("Open source and tests"), "mlRoutineStudio", [this] {
            const auto routine = routines_->currentData().toJsonObject();
            if (!routine.isEmpty()) openRoutine_(routine);
        });
    if (exportRoutine_)
        action(*target, tr("Export routine"), "mlRoutineExport", [this] {
            const auto routine = routines_->currentData().toJsonObject();
            if (!routine.isEmpty()) exportRoutine_(routine);
        });
    layout->addStretch();
    tabs_->addTab(page, tr("PL/SQL calculation tools"));
}

void MlStudio::reject()
{
    if (inFlight_)
    {
        status_->setText(tr("Wait for the Server response to preserve any submitted job ID."));
        return;
    }
    QDialog::reject();
}

void MlStudio::enabled(bool running)
{
    inFlight_ = running;
    for (auto* button : actions_) button->setEnabled(!running);
    for (auto* button : mutations_) button->setEnabled(!running && !session_.profile().read_only);
    models_->setEnabled(!running);
    name_->setEnabled(!running);
    algorithm_->setEnabled(!running);
}

void MlStudio::selectModel(const QJsonObject& info)
{
    name_->setText(qualified(info));
    versions_->clear();
    metrics_->clear();
    evidence_->clear();
}

void MlStudio::selectDataset(const QJsonObject& metadata)
{
    const auto id = metadata.value("dataset_id").toString();
    if (id.isEmpty() || !metadata.value("prepared").isObject()) return;
    dataset_ = metadata;
    datasetId_->setText(id);
    const auto options = metadata.value("options").toObject();
    const auto parts = metadata.value("prepared").toObject().value("split").toObject();
    datasetDetails_->setText(tr("Dataset revision %1 · %2 rows · target %3 · train %4 · validation %5 · test %6")
        .arg(metadata.value("revision").toString())
        .arg(metadata.value("row_count").toInteger())
        .arg(options.value("target").toString(tr("none")))
        .arg(parts.value("train").toArray().size())
        .arg(parts.value("validation").toArray().size())
        .arg(parts.value("test").toArray().size()));
}

void MlStudio::changeAlgorithm()
{
    const auto& algorithm = algorithms[algorithm_->currentIndex()];
    options_->setPlainText(QString::fromLatin1(algorithm.example));
    const bool mlp = algorithm_->currentData().toString() == "mlp";
    if (!mlp) pause_->setValue(0);
    pause_->setEnabled(mlp);
}

void MlStudio::availability(bool probe)
{
    const auto caps = session_.capabilities();
    const QFileInfo python(localPython_->text());
    QStringList lines{
        caps.value("mlp_cpu_v1").toBool() ? tr("Remote MLP CPU available") : tr("Remote MLP CPU unavailable"),
        caps.value("ml_gpu_v1").toBool() ? tr("Remote GPU training backend available") : tr("Remote GPU training backend unavailable"),
        python.isAbsolute() && python.isExecutable() ? tr("Local Python executable found") : tr("Local Python executable unavailable")
    };
    availability_->setPlainText(lines.join('\n'));
    if (probe && (caps.value("supervised_ml_v1").toBool() || caps.value("unsupervised_ml_v1").toBool()))
        call(SDP_MODEL_DEVICES, "SDP_MODEL_DEVICES",
            {{"device_index", deviceIndex_->value()}, {"memory_mib", memory_->value()}}, false,
            [this, lines = std::move(lines)](const QJsonObject& details) mutable {
                const auto reason = details.value("gpu_reason").toString();
                if (!reason.isEmpty()) lines.append(tr("The Server GPU probe reported %1.").arg(reason));
                availability_->setPlainText(lines.join('\n'));
            });
}


void MlStudio::modelAction(sdp_model_operation operation)
{
    const auto name = name_->text().trimmed();
    if (name.isEmpty())
    {
        status_->setText(tr("Enter a model name or select one from the catalog."));
        return;
    }
    QJsonObject body{{"name", name}};
    const auto& algorithm = algorithms[algorithm_->currentIndex()];
    const auto caps = session_.capabilities();
    switch (operation)
    {
    case SDP_MODEL_STATUS:
        call(operation, "SDP_MODEL_STATUS", body, false, [this, name](const QJsonObject& model) {
            verifiedModel_ = name;
            verifiedAlgorithm_ = model.value("algorithm").toString();
            renderVersions(model);
        });
        return;
    case SDP_MODEL_CREATE:
        if (!caps.value(QString::fromLatin1(algorithm.capability)).toBool())
        {
            status_->setText(tr("The connected Server does not expose the selected algorithm."));
            return;
        }
        body.insert("algorithm", QString::fromLatin1(algorithm.name));
        call(operation, "SDP_MODEL_CREATE", body, true, [this, name](const QJsonObject& model) {
            verifiedModel_ = name;
            verifiedAlgorithm_ = model.value("algorithm").toString();
            renderVersions(model);
            if (refresh_) refresh_();
        });
        return;
    case SDP_MODEL_TRAIN:
        break;
    case SDP_MODEL_EVALUATE:
        body.insert("version", version_->value());
        call(operation, "SDP_MODEL_EVALUATE", body, false, [this](const QJsonObject& report) {
            renderMetrics(report);
            evidence_->setText(tr("Server verified artifact SHA-256 %1 · dataset %2 · run %3.")
                .arg(report.value("checksum").toString(), report.value("dataset_id").toString(),
                    report.value("run_id").toString()));
            tabs_->setCurrentIndex(1);
        });
        return;
    case SDP_MODEL_ACTIVATE:
    case SDP_MODEL_ROLLBACK:
        body.insert("version", version_->value());
        call(operation, operation == SDP_MODEL_ACTIVATE ? "SDP_MODEL_ACTIVATE" : "SDP_MODEL_ROLLBACK",
            body, true, [this](const QJsonObject& reply) {
                evidence_->setText(tr("Active version %1. Server verified artifact and dataset compatibility.")
                    .arg(reply.value("active_version").toInteger()));
                if (refresh_) refresh_();
            });
        return;
    case SDP_MODEL_DEACTIVATE:
        call(operation, "SDP_MODEL_DEACTIVATE", body, true, [this](const QJsonObject&) {
            evidence_->setText(tr("The Server cleared the active version."));
            if (refresh_) refresh_();
        });
        return;
    default:
        return;
    }

    if (verifiedModel_ != name || verifiedAlgorithm_ != QString::fromLatin1(algorithm.name))
    {
        status_->setText(tr("Read the model registry before training. The training algorithm must match the stored model."));
        return;
    }
    if (!caps.value(QString::fromLatin1(algorithm.capability)).toBool() || !caps.value("jobs_v1").toBool())
    {
        status_->setText(tr("The selected algorithm or durable jobs are unavailable on this Server."));
        return;
    }
    const auto id = datasetId_->text().trimmed();
    static const QRegularExpression datasetId(QStringLiteral("^[a-f0-9]{32}$"));
    if (!datasetId.match(id).hasMatch())
    {
        status_->setText(tr("Choose a dataset or enter its 32-character lowercase identifier."));
        return;
    }
    if (algorithm.supervised && !dataset_.isEmpty() && !dataset_.value("options").toObject().contains("target"))
    {
        status_->setText(tr("This algorithm requires a dataset with a target."));
        return;
    }
    QJsonObject options;
    if (!parseObject(options_->toPlainText(), options, 4096) ||
        QJsonDocument(options).toJson(QJsonDocument::Compact).size() > 1024)
    {
        status_->setText(tr("Enter a JSON object of at most 1024 bytes for algorithm options."));
        return;
    }
    if (const auto error = invalidOption(algorithm, options); !error.isEmpty())
    {
        status_->setText(tr("Option %2 is invalid for %1.").arg(algorithm_->currentText(), error));
        return;
    }
    body.insert("dataset_id", id);
    body.insert("seed", seed_->value());
    body.insert("options", options);
    if (pause_->value())
    {
        if (verifiedAlgorithm_ != "mlp" || !caps.value("model_checkpoints_v1").toBool() ||
            pause_->value() >= options.value("epochs").toInt(120))
        {
            status_->setText(tr("A supported MLP checkpoint requires an epoch earlier than the last epoch."));
            return;
        }
        body.insert("pause_after_epoch", pause_->value());
    }
    if (device_->currentData().toString() == "gpu")
    {
        if (!caps.value("ml_gpu_v1").toBool() && !fallback_->isChecked())
        {
            status_->setText(tr("Remote GPU training backend unavailable. Select CPU or explicitly allow Server CPU fallback."));
            return;
        }
        body.insert("device", "gpu");
        body.insert("device_index", deviceIndex_->value());
        body.insert("memory_mib", memory_->value());
        body.insert("allow_cpu_fallback", fallback_->isChecked());
    }
    call(operation, "SDP_MODEL_TRAIN", body, true, [this, name](const QJsonObject& response) {
        const auto job = response.value("job_id").toString();
        if (job.isEmpty())
        {
            status_->setText(tr("The Server returned no training job identifier."));
            return;
        }
        status_->setText(tr("Training job %1 submitted to Server. Follow the live job for its outcome.").arg(job));
        if (watch_) watch_(job, name);
    });
}

void MlStudio::predict()
{
    QJsonObject features;
    const auto name = name_->text().trimmed();
    if (name.isEmpty() || !parseObject(features_->toPlainText(), features) || features.isEmpty())
    {
        status_->setText(tr("Enter a model name and a JSON object containing the feature values."));
        return;
    }
    call(SDP_MODEL_PREDICT, "SDP_MODEL_PREDICT", {{"name", name}, {"features", features}}, false);
}

void MlStudio::compare()
{
    const auto left = name_->text().trimmed();
    const auto right = compareModel_->text().trimmed();
    if (left.isEmpty())
    {
        status_->setText(tr("Enter the first model and choose versions and a split."));
        return;
    }
    const QJsonArray runs{
        QJsonObject{{"name", left}, {"version", version_->value()}},
        QJsonObject{{"name", right.isEmpty() ? left : right}, {"version", otherVersion_->value()}}
    };
    call(SDP_MODEL_COMPARE, "SDP_MODEL_COMPARE", {{"runs", runs}, {"split", split_->currentText()}}, false,
        [this](const QJsonObject& report) {
            renderMetrics(report);
            const auto reason = report.value("reason").toString();
            evidence_->setText(report.value("comparable").toBool()
                ? tr("Server reports these runs are comparable on the requested split.")
                : tr("Runs cannot be compared on the requested split. Server reason %1")
                    .arg(reason.isEmpty() ? tr("unavailable") : reason));
            tabs_->setCurrentIndex(1);
        });
}

void MlStudio::renderVersions(const QJsonObject& model)
{
    const auto algorithm = algorithm_->findData(model.value("algorithm").toString());
    if (algorithm >= 0) algorithm_->setCurrentIndex(algorithm);
    const auto active = model.value("active_version");
    versions_->clear();
    for (const auto& item : model.value("versions").toArray())
    {
        const auto row = item.toObject();
        const auto version = row.value("version");
        auto* node = new QTreeWidgetItem(versions_, {formatMetric(version),
            version == active ? tr("Active") : QString{}, row.value("dataset_id").toString(),
            row.value("run_id").toString(), row.value("checksum").toString()});
        node->setData(0, Qt::UserRole, version.toInteger());
        node->setToolTip(4, row.value("checksum").toString());
    }
    if (active.toInteger() > 0) version_->setValue(active.toInt());
    versions_->resizeColumnToContents(0);
    evidence_->setText(tr("Owner %1 · %2 · active version %3. Evaluate to verify an artifact.")
        .arg(model.value("owner").toString(), model.value("algorithm").toString(),
            active.isNull() ? tr("none") : formatMetric(active)));
    tabs_->setCurrentIndex(1);
}

void MlStudio::renderMetrics(const QJsonObject& report)
{
    metrics_->clear();
    QJsonObject shown;
    for (const auto key : {"metrics", "history", "loss_history", "runs", "comparable", "split",
        "reason", "hardware", "fit", "checksum", "dataset_id", "run_id"})
        if (report.contains(key)) shown.insert(key, report.value(key));
    if (const auto artifact = report.value("artifact").toObject(); !artifact.isEmpty())
        for (const auto key : {"loss_history", "epochs", "fit", "metrics", "validation_losses"})
            if (artifact.contains(key)) shown.insert(key, artifact.value(key));
    addMetrics(metrics_->invisibleRootItem(), shown.isEmpty() ? report : shown);
    metrics_->expandToDepth(1);
    metrics_->resizeColumnToContents(0);
}

void MlStudio::present(const QJsonObject& reply)
{
    setProperty("mlLastResponse", reply);
    result_->setPlainText(pretty(reply));
    tabs_->setCurrentWidget(result_);
}

void MlStudio::call(sdp_model_operation operation, const QString& title, QJsonObject body,
    bool mutation, Finished done)
{
    if (!session_.state().connected || session_.state().transactionActive || !session_.state().autocommit || session_.busy())
    {
        status_->setText(tr("Use an idle connected worksheet with autocommit enabled."));
        return;
    }
    if (mutation && session_.profile().read_only)
    {
        status_->setText(tr("This profile allows inspection only."));
        return;
    }
    QString requestToken;
    if (mutation)
    {
        requestToken = token();
        body.insert("request_token", requestToken);
    }
    const auto payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    request_->setPlainText(QStringLiteral("sdp_model_command · %1\n%2").arg(title, pretty(body)));
    if (mutation)
    {
        QMessageBox review(QMessageBox::Question, tr("Confirm model operation"),
            tr("%1 on %2").arg(title, endpoint(session_.profile())),
            QMessageBox::Yes | QMessageBox::No, this);
        review.setDefaultButton(QMessageBox::No);
        review.setDetailedText(pretty(body));
        if (review.exec() != QMessageBox::Yes) return;
    }
    result_->clear();
    setProperty("mlLastResponse", QJsonObject{});
    status_->setText(tr("Waiting for the Server…"));
    enabled(true);
    const QPointer<MlStudio> self(this);
    if (session_.request([operation, payload](auto handle, auto* output, auto* error) {
        return sdp_model_command(handle, operation, {payload.constData(), static_cast<uint32_t>(payload.size())}, output, error);
    }, [self, requestToken, done = std::move(done)](const SqlReply& reply) {
        if (!self) return;
        self->enabled(false);
        if (reply.error.code != SDP_OK)
        {
            auto message = QString::fromUtf8(reply.error.message);
            if (message.isEmpty()) message = wb::status(reply.error.code);
            if (reply.error.unknown_outcome && !requestToken.isEmpty())
                message = QObject::tr("Unknown outcome. Check request %1 before submitting again. %2").arg(requestToken, message);
            self->status_->setText(message);
            return;
        }
        self->present(reply.data);
        self->status_->setText(QObject::tr("Server response received."));
        if (done) done(reply.data);
    }, 15000)) return;
    enabled(false);
    status_->setText(tr("The request could not start."));
}
}

QDialog* showMlStudio(SqlSession& session, const QJsonArray& catalog, const QJsonObject& initial, QWidget* parent,
    std::function<void()> refresh, std::function<void(const QString&, const QString&)> watch,
    std::function<void(const QJsonObject&)> openRoutine, std::function<void(const QJsonObject&)> exportRoutine)
{
    auto* studio = new MlStudio(session, catalog, initial, parent, std::move(refresh), std::move(watch),
        std::move(openRoutine), std::move(exportRoutine));
    studio->show();
    return studio;
}
}
