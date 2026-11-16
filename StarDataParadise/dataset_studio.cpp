#include "dataset_studio.hpp"

#include "sql_editor.hpp"
#include "sql_session.hpp"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace wb
{
namespace
{
constexpr int displayRows = 64;
constexpr int maximumRecentIds = 12;
constexpr int datasetTimeoutMs = 30000;
constexpr qsizetype maximumRequestBytes = 64 * 1024;
const QStringList transforms{"numeric", "standard", "normalize", "onehot"};
const QStringList imputations{"reject", "mean", "zero", "mode"};
const QStringList splitModes{"random", "stratified", "time"};

QString pretty(const QJsonObject& value)
{
    return QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Indented));
}

QString display(const QJsonValue& value)
{
    if (value.isNull() || value.isUndefined()) return QStringLiteral("NULL");
    if (value.isString()) return value.toString();
    if (value.isBool()) return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    auto number = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    if (number.size() >= 2) return QString::fromUtf8(number.sliced(1, number.size() - 2));
    return {};
}

QString number(double value)
{
    return QString::number(value, 'g', 10);
}

bool datasetId(const QString& id)
{
    static const QRegularExpression shape(QStringLiteral("^[0-9a-f]{32}$"));
    return shape.match(id).hasMatch();
}

QString requestToken()
{
    return QUuid::createUuid().toString(QUuid::Id128);
}

QString errorText(const SqlReply& reply)
{
    auto text = wb::status(reply.error.code);
    const auto message = QString::fromUtf8(reply.error.message);
    if (!message.isEmpty()) text += QStringLiteral(" · ") + message;
    if (reply.error.line && reply.error.column)
        text += QObject::tr(" · Line %1, column %2").arg(reply.error.line).arg(reply.error.column);
    return text;
}

QPlainTextEdit* editor(const char* name, bool readOnly = true)
{
    auto* field = new QPlainTextEdit;
    field->setObjectName(name);
    field->setReadOnly(readOnly);
    return field;
}

std::optional<QJsonObject> parseObject(QStringView source)
{
    if (source.trimmed().isEmpty()) return QJsonObject{};
    const auto document = QJsonDocument::fromJson(source.toUtf8());
    if (!document.isObject()) return {};
    return document.object();
}

bool only(const QJsonObject& object, const QStringList& allowed)
{
    return std::ranges::all_of(object.keys(), [&](const QString& name) { return allowed.contains(name); });
}

bool validColumn(const QJsonValue& value)
{
    const auto name = value.toString();
    return !name.isEmpty() && name.toUtf8().size() <= 128 && !name.contains(u'\0');
}

bool unsignedInteger(const QJsonValue& value)
{
    return value.isDouble() && value.toInteger(-1) >= 0;
}

QString validateOptions(const QJsonObject& options)
{
    if (!only(options, {"features", "target", "target_kind", "split"}) ||
        !options.value("features").isArray() || !options.value("split").isObject())
        return QObject::tr("Dataset options require a features array and a split object.");
    const auto features = options.value("features").toArray();
    if (features.isEmpty() || features.size() > 128)
        return QObject::tr("Choose between 1 and 128 projected features.");

    const auto target = options.value("target");
    const auto targetKind = options.value("target_kind").toString();
    if ((!target.isUndefined() && !validColumn(target)) ||
        (options.contains("target_kind") && (!validColumn(target) || (targetKind != u"numeric" && targetKind != u"categorical"))))
        return QObject::tr("Choose a projected target and a numeric or categorical target type.");

    QSet<QString> selected;
    for (const auto item : features)
    {
        const auto feature = item.toObject();
        const auto column = feature.value("column");
        const auto transform = feature.value("transform").toString();
        const auto impute = feature.value("impute").toString();
        if (feature.size() != 3 || !validColumn(column) || selected.contains(column.toString()) ||
            column == target || !transforms.contains(transform) || !imputations.contains(impute) ||
            (transform == u"onehot" && (impute == u"mean" || impute == u"zero")))
            return QObject::tr("Check distinct feature columns, transforms and imputation. Keep the target separate from the features.");
        selected.insert(column.toString());
    }

    const auto split = options.value("split").toObject();
    const auto mode = split.value("mode").toString();
    const auto train = split.value("train").toInteger(split.contains("train") ? -1 : 60);
    const auto validation = split.value("validation").toInteger(split.contains("validation") ? -1 : 20);
    if (!only(split, {"mode", "seed", "train", "validation", "time", "group"}) || !splitModes.contains(mode) ||
        (split.contains("seed") && !unsignedInteger(split.value("seed"))) ||
        train < 1 || validation < 1 || train > 98 || validation > 98 || train + validation >= 100)
        return QObject::tr("Choose a split and percentages that leave rows for training, validation and testing.");
    if ((mode == u"time") != split.contains("time") || (split.contains("time") && !validColumn(split.value("time"))) ||
        (split.contains("group") && !validColumn(split.value("group"))))
        return QObject::tr("Time splits need a projected time column. Grouping uses a projected group column.");
    if (mode == u"stratified" && (targetKind != u"categorical" || !validColumn(target)))
        return QObject::tr("Stratified splitting requires a categorical target.");
    return {};
}

bool validBindings(const QJsonObject& types, const QJsonObject& parameters)
{
    if (types.keys() != parameters.keys()) return false;
    for (const auto& key : types.keys())
    {
        if (!validColumn(QJsonValue(key)) || !types.value(key).toObject().value("type").isString() ||
            !parameters.value(key).toObject().value("t").isString()) return false;
    }
    return true;
}

bool selectQuery(QStringView sql)
{
    auto state = SqlLexState::normal;
    for (const auto& token : sql_tokens(sql, state))
    {
        if (token.kind == SqlTokenKind::whitespace || token.kind == SqlTokenKind::comment) continue;
        const auto first = sql.sliced(token.offset, token.length).toString().toUpper();
        return token.kind == SqlTokenKind::word && (first == u"SELECT" || first == u"WITH");
    }
    return false;
}

struct Stats
{
    qsizetype absent{};
    qsizetype numericCount{};
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    double mean{};
    QHash<QString, qsizetype> categories;

    void add(const QJsonValue& value, bool categorical)
    {
        if (value.isNull() || value.isUndefined())
        {
            ++absent;
            return;
        }
        if (value.isDouble() && !categorical)
        {
            const auto scalar = value.toDouble();
            minimum = std::min(minimum, scalar);
            maximum = std::max(maximum, scalar);
            ++numericCount;
            mean += (scalar - mean) / static_cast<double>(numericCount);
            return;
        }
        ++categories[display(value)];
    }

    QString distribution() const
    {
        if (numericCount)
            return QObject::tr("Min %1 · Max %2 · Mean %3").arg(number(minimum), number(maximum), number(mean));
        if (categories.isEmpty()) return QObject::tr("No non-NULL values");
        auto keys = categories.keys();
        std::ranges::sort(keys, [&](const QString& a, const QString& b) {
            const auto first = categories.value(a);
            const auto second = categories.value(b);
            return first == second ? a < b : first > second;
        });
        QStringList frequent;
        for (const auto& key : keys.mid(0, 5))
        {
            const auto label = key.isEmpty() ? QObject::tr("(empty text)") : key.left(120);
            frequent.push_back(QObject::tr("%1 × %2").arg(label).arg(categories.value(key)));
        }
        if (keys.size() > 5) frequent.push_back(QObject::tr("%1 more values").arg(keys.size() - 5));
        return frequent.join(QStringLiteral(" · "));
    }
};

QString scopeKey(const Profile& profile)
{
    const QJsonArray scope{profile.host.toLower(), profile.port, profile.tls_name.toLower(),
        profile.ca_file, profile.database, profile.user};
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(scope).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256).toHex());
}

class DatasetStudio : public QDialog
{
public:
    DatasetStudio(SqlSession&, QWidget*, std::function<void(const QJsonObject&)>, const QString&);
    void reject() override { if (!busy_) QDialog::reject(); }

private:
    QPushButton* action(QLayout*, const char*, const QString&, std::function<void()>);
    void addFeature();
    void buildOptions();
    void updateControls();
    void invalidate();
    bool available(bool write = false);
    bool send(sdp_dataset_operation, const QJsonObject&, SqlSession::Finished);
    void preview();
    void create();
    void inspect();
    void load(const QString&, bool afterCreate = false);
    bool render(const QJsonObject&);
    void clear();
    void setBusy(bool busy);
    void remember(const QString&);
    void refreshRecent();
    void setStatus(QString message) { status_->setText(std::move(message)); }

    SqlSession& session_;
    std::function<void(const QJsonObject&)> selected_;
    QString recentKey_;
    QSettings recentStore_;
    QJsonObject reviewed_;
    QJsonObject dataset_;
    QTabWidget* tabs_{};
    QWidget* requestPage_{};
    QWidget* inspectorPage_{};
    SqlEditor* sql_{};
    QPlainTextEdit* types_{};
    QPlainTextEdit* parameters_{};
    QPlainTextEdit* options_{};
    QPlainTextEdit* request_{};
    QPlainTextEdit* summary_{};
    QPlainTextEdit* source_{};
    QTableWidget* features_{};
    QLineEdit* target_{};
    QComboBox* targetKind_{};
    QComboBox* splitMode_{};
    QSpinBox* train_{};
    QSpinBox* validation_{};
    QSpinBox* seed_{};
    QLineEdit* time_{};
    QLineEdit* group_{};
    QLineEdit* id_{};
    QComboBox* recents_{};
    QTreeWidget* stats_{};
    QTreeWidget* splits_{};
    QTableWidget* sample_{};
    QLabel* scope_{};
    QLabel* status_{};
    QPushButton* preview_{};
    QPushButton* create_{};
    QPushButton* use_{};
    bool busy_{};
    bool unknown_{};
};
DatasetStudio::DatasetStudio(SqlSession& session, QWidget* parent, std::function<void(const QJsonObject&)> selected,
    const QString& initialSql)
    : QDialog(parent), session_(session), selected_(std::move(selected)), recentKey_(scopeKey(session.profile())),
      recentStore_(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/dataset-ids.ini"),
          QSettings::IniFormat)
{
    setObjectName("datasetStudio");
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Dataset Studio · %1").arg(endpoint(session.profile())));
    resize(1080, 750);
    auto* layout = new QVBoxLayout(this);
    auto* selection = new QHBoxLayout;
    selection->addWidget(new QLabel(tr("Dataset ID")));
    id_ = new QLineEdit;
    id_->setObjectName("datasetId");
    id_->setPlaceholderText(tr("32-character server ID"));
    id_->setMaxLength(32);
    selection->addWidget(id_, 1);
    recents_ = new QComboBox;
    recents_->setObjectName("datasetRecent");
    selection->addWidget(recents_, 1);
    action(selection, "datasetInspect", tr("Inspect saved snapshot"), [this] { inspect(); });
    layout->addLayout(selection);

    tabs_ = new QTabWidget;
    tabs_->setObjectName("datasetTabs");
    layout->addWidget(tabs_, 1);
    auto* queryPage = new QWidget;
    auto* query = new QVBoxLayout(queryPage);
    sql_ = new SqlEditor;
    sql_->setObjectName("datasetSql");
    sql_->setPlaceholderText(tr("SELECT named columns from a table or view"));
    sql_->setPlainText(initialSql);
    query->addWidget(sql_, 2);
    auto* bind = new QHBoxLayout;
    auto* bindTypes = new QVBoxLayout;
    bindTypes->addWidget(new QLabel(tr("Parameter types")));
    types_ = editor("datasetParameterTypes", false);
    types_->setPlainText(QStringLiteral("{}"));
    types_->setPlaceholderText(QStringLiteral("{\"min_id\":{\"type\":\"BIGINT\"}}"));
    bindTypes->addWidget(types_);
    auto* bindValues = new QVBoxLayout;
    bindValues->addWidget(new QLabel(tr("Parameter values")));
    parameters_ = editor("datasetParameters", false);
    parameters_->setPlainText(QStringLiteral("{}"));
    parameters_->setPlaceholderText(QStringLiteral("{\"min_id\":{\"t\":\"i64\",\"v\":\"1\"}}"));
    bindValues->addWidget(parameters_);
    bind->addLayout(bindTypes);
    bind->addLayout(bindValues);
    query->addLayout(bind, 1);
    tabs_->addTab(queryPage, tr("Source query"));

    auto* preparation = new QWidget;
    auto* preparationLayout = new QVBoxLayout(preparation);
    features_ = new QTableWidget(0, 3);
    features_->setObjectName("datasetFeatures");
    features_->setHorizontalHeaderLabels({tr("Feature column"), tr("Transform"), tr("Missing values")});
    features_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    features_->setSelectionBehavior(QAbstractItemView::SelectRows);
    preparationLayout->addWidget(features_, 2);
    auto* featureActions = new QHBoxLayout;
    action(featureActions, "datasetAddFeature", tr("Add feature"), [this] { addFeature(); });
    action(featureActions, "datasetRemoveFeature", tr("Remove feature"), [this] {
        if (!busy_ && !unknown_ && features_->currentRow() >= 0) features_->removeRow(features_->currentRow());
    });
    featureActions->addStretch();
    preparationLayout->addLayout(featureActions);

    auto* design = new QFormLayout;
    target_ = new QLineEdit;
    target_->setObjectName("datasetTarget");
    target_->setPlaceholderText(tr("Optional projected target column"));
    targetKind_ = new QComboBox;
    targetKind_->setObjectName("datasetTargetKind");
    targetKind_->addItems({"numeric", "categorical"});
    auto* targetRow = new QHBoxLayout;
    targetRow->addWidget(target_);
    targetRow->addWidget(targetKind_);
    design->addRow(tr("Target"), targetRow);
    splitMode_ = new QComboBox;
    splitMode_->setObjectName("datasetSplitMode");
    splitMode_->addItems(splitModes);
    train_ = new QSpinBox;
    train_->setObjectName("datasetTrainShare");
    train_->setRange(1, 98);
    train_->setValue(60);
    train_->setSuffix(QStringLiteral("%"));
    validation_ = new QSpinBox;
    validation_->setObjectName("datasetValidationShare");
    validation_->setRange(1, 98);
    validation_->setValue(20);
    validation_->setSuffix(QStringLiteral("%"));
    seed_ = new QSpinBox;
    seed_->setObjectName("datasetSeed");
    seed_->setRange(0, std::numeric_limits<int>::max());
    auto* split = new QHBoxLayout;
    split->addWidget(splitMode_);
    split->addWidget(new QLabel(tr("Train")));
    split->addWidget(train_);
    split->addWidget(new QLabel(tr("Validation")));
    split->addWidget(validation_);
    split->addWidget(new QLabel(tr("Seed")));
    split->addWidget(seed_);
    design->addRow(tr("Split"), split);
    time_ = new QLineEdit;
    time_->setObjectName("datasetTimeColumn");
    time_->setPlaceholderText(tr("Required for time split"));
    group_ = new QLineEdit;
    group_->setObjectName("datasetGroupColumn");
    group_->setPlaceholderText(tr("Optional group column"));
    auto* grouping = new QHBoxLayout;
    grouping->addWidget(time_);
    grouping->addWidget(group_);
    design->addRow(tr("Time and grouping"), grouping);
    preparationLayout->addLayout(design);
    auto* generate = new QHBoxLayout;
    action(generate, "datasetBuildOptions", tr("Build options from fields"), [this] { buildOptions(); });
    generate->addStretch();
    preparationLayout->addLayout(generate);
    preparationLayout->addWidget(new QLabel(tr("Dataset options sent to the server")));
    options_ = editor("datasetOptions", false);
    options_->setPlainText(QStringLiteral("{\n    \"features\": [],\n    \"split\": {\"mode\": \"random\", \"train\": 60, \"validation\": 20}\n}"));
    preparationLayout->addWidget(options_, 2);
    tabs_->addTab(preparation, tr("Preprocessing and split"));
    addFeature();

    requestPage_ = new QWidget;
    auto* requestLayout = new QVBoxLayout(requestPage_);
    requestLayout->addWidget(new QLabel(QStringLiteral("sdp_dataset_command · SDP_DATASET_CREATE")));
    request_ = editor("datasetRequest");
    requestLayout->addWidget(request_);
    tabs_->addTab(requestPage_, tr("Create request"));

    inspectorPage_ = new QWidget;
    auto* inspected = new QVBoxLayout(inspectorPage_);
    scope_ = new QLabel;
    scope_->setObjectName("datasetScope");
    scope_->setWordWrap(true);
    scope_->setTextFormat(Qt::PlainText);
    inspected->addWidget(scope_);
    auto* views = new QTabWidget;
    inspected->addWidget(views, 1);
    sample_ = new QTableWidget;
    sample_->setObjectName("datasetSample");
    sample_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    views->addTab(sample_, tr("Sample rows"));
    stats_ = new QTreeWidget;
    stats_->setObjectName("datasetStatistics");
    stats_->setHeaderLabels({tr("Projected column"), tr("Role"), tr("Type"), tr("NULL count"), tr("NULL rate"), tr("Observed distribution")});
    stats_->setRootIsDecorated(false);
    stats_->header()->setSectionResizeMode(5, QHeaderView::Stretch);
    views->addTab(stats_, tr("Observed statistics"));
    splits_ = new QTreeWidget;
    splits_->setObjectName("datasetSplits");
    splits_->setHeaderLabels({tr("Partition or transform"), tr("Server result")});
    splits_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    views->addTab(splits_, tr("Split and fitted transforms"));
    source_ = editor("datasetSourceQuery");
    views->addTab(source_, tr("Saved query"));
    summary_ = editor("datasetSummary");
    views->addTab(summary_, tr("Server response"));
    tabs_->addTab(inspectorPage_, tr("Snapshot"));

    status_ = new QLabel;
    status_->setObjectName("datasetStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto* actions = new QHBoxLayout;
    preview_ = action(actions, "datasetPreview", tr("Preview create request"), [this] { preview(); });
    create_ = action(actions, "datasetCreate", tr("Create dataset"), [this] { create(); });
    use_ = action(actions, "datasetUse", tr("Use dataset"), [this] {
        if (!busy_ && !dataset_.isEmpty() && dataset_.value("dataset_id").toString() == id_->text().trimmed() && selected_)
            selected_(dataset_);
    });
    action(actions, "datasetCopyRequest", tr("Copy create request"), [this] {
        if (!request_->toPlainText().isEmpty()) QApplication::clipboard()->setText(request_->toPlainText());
    });
    actions->addStretch();
    action(actions, "datasetClose", tr("Close"), [this] { reject(); });
    layout->addLayout(actions);

    for (auto* input : std::initializer_list<QPlainTextEdit*>{sql_, types_, parameters_, options_})
        connect(input, &QPlainTextEdit::textChanged, this, [this] { invalidate(); });
    connect(id_, &QLineEdit::textChanged, this, [this] { updateControls(); });
    connect(recents_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index > 0) id_->setText(recents_->itemText(index));
    });
    refreshRecent();
    setBusy(false);
}

QPushButton* DatasetStudio::action(QLayout* row, const char* name, const QString& label, std::function<void()> run)
{
    auto* button = new QPushButton(label);
    button->setObjectName(name);
    row->addWidget(button);
    connect(button, &QPushButton::clicked, this, std::move(run));
    return button;
}

void DatasetStudio::addFeature()
{
    if (busy_ || unknown_ || features_->rowCount() >= 128) return;
    const auto row = features_->rowCount();
    features_->insertRow(row);
    features_->setItem(row, 0, new QTableWidgetItem);
    auto* transform = new QComboBox;
    transform->addItems(transforms);
    features_->setCellWidget(row, 1, transform);
    auto* impute = new QComboBox;
    impute->addItems(imputations);
    features_->setCellWidget(row, 2, impute);
}

void DatasetStudio::buildOptions()
{
    if (busy_ || unknown_) return;
    QJsonArray features;
    for (int row = 0; row < features_->rowCount(); ++row)
    {
        const auto name = features_->item(row, 0)->text();
        if (name.isEmpty()) continue;
        const auto* transform = static_cast<QComboBox*>(features_->cellWidget(row, 1));
        const auto* impute = static_cast<QComboBox*>(features_->cellWidget(row, 2));
        features.append(QJsonObject{{"column", name}, {"transform", transform->currentText()}, {"impute", impute->currentText()}});
    }
    QJsonObject split{{"mode", splitMode_->currentText()}, {"seed", seed_->value()},
        {"train", train_->value()}, {"validation", validation_->value()}};
    if (splitMode_->currentText() == u"time") split.insert("time", time_->text());
    if (!group_->text().isEmpty()) split.insert("group", group_->text());
    QJsonObject options{{"features", features}, {"split", split}};
    if (!target_->text().isEmpty())
    {
        options.insert("target", target_->text());
        options.insert("target_kind", targetKind_->currentText());
    }
    options_->setPlainText(pretty(options));
    setStatus(tr("Dataset options updated from the form."));
}

void DatasetStudio::invalidate()
{
    if (busy_ || unknown_) return;
    reviewed_ = {};
    request_->clear();
    updateControls();
}

void DatasetStudio::setBusy(bool busy)
{
    busy_ = busy;
    for (auto* edit : std::initializer_list<QPlainTextEdit*>{sql_, types_, parameters_, options_})
        edit->setReadOnly(busy || unknown_ || session_.profile().read_only);
    id_->setReadOnly(busy);
    recents_->setEnabled(!busy);
    features_->setEnabled(!busy && !unknown_ && !session_.profile().read_only);
    updateControls();
}

void DatasetStudio::updateControls()
{
    const bool connected = session_.state().connected && session_.capabilities().value("dataset_v1").toBool();
    preview_->setEnabled(!busy_ && !unknown_ && !session_.profile().read_only);
    create_->setEnabled(!busy_ && !session_.profile().read_only && !reviewed_.isEmpty());
    create_->setText(unknown_ ? tr("Retry the same request") : tr("Create dataset"));
    use_->setEnabled(!busy_ && connected && !dataset_.isEmpty() &&
        dataset_.value("dataset_id").toString() == id_->text().trimmed() && bool(selected_));
}

bool DatasetStudio::available(bool write)
{
    if (busy_) return false;
    if (!session_.state().connected)
    {
        setStatus(tr("Reconnect this worksheet to use Dataset Studio."));
        return false;
    }
    if (!session_.capabilities().value("dataset_v1").toBool())
    {
        setStatus(tr("The connected server does not expose the Dataset module."));
        return false;
    }
    if (session_.busy() || session_.state().transactionActive)
    {
        setStatus(tr("Finish the current worksheet operation or transaction before opening a dataset."));
        return false;
    }
    if (write && session_.profile().read_only)
    {
        setStatus(tr("This profile allows inspection of existing datasets."));
        return false;
    }
    return true;
}

bool DatasetStudio::send(sdp_dataset_operation operation, const QJsonObject& request, SqlSession::Finished done)
{
    const auto json = QJsonDocument(request).toJson(QJsonDocument::Compact);
    return session_.request([json, operation](auto connection, auto* response, auto* error) {
        return sdp_dataset_command(connection, operation, {json.constData(), static_cast<uint32_t>(json.size())}, response, error);
    }, std::move(done), datasetTimeoutMs);
}
void DatasetStudio::preview()
{
    if (busy_ || unknown_ || session_.profile().read_only) return;
    const auto sql = sql_->sql_text();
    if (!selectQuery(sql))
    {
        setStatus(tr("Enter a SELECT query that projects the feature and target columns."));
        return;
    }
    const auto options = parseObject(options_->toPlainText());
    const auto declarations = parseObject(types_->toPlainText());
    const auto bindings = parseObject(parameters_->toPlainText());
    if (!options || !declarations || !bindings || !validBindings(*declarations, *bindings))
    {
        setStatus(tr("Enter valid JSON options and matching typed parameter declarations and values."));
        return;
    }
    if (const auto issue = validateOptions(*options); !issue.isEmpty())
    {
        setStatus(issue);
        return;
    }
    QJsonObject request{{"sql", sql}, {"options", *options}, {"request_token", requestToken()}};
    if (!declarations->isEmpty())
    {
        request.insert("parameter_types", *declarations);
        request.insert("parameters", *bindings);
    }
    if (QJsonDocument(request).toJson(QJsonDocument::Compact).size() > maximumRequestBytes)
    {
        setStatus(tr("The dataset request exceeds 64 KiB."));
        return;
    }
    reviewed_ = std::move(request);
    request_->setPlainText(pretty(reviewed_));
    tabs_->setCurrentWidget(requestPage_);
    setStatus(tr("Review the source query, bindings, split and preprocessing before creating the dataset."));
    updateControls();
}

void DatasetStudio::create()
{
    if (!available(true) || reviewed_.isEmpty()) return;
    setBusy(true);
    setStatus(unknown_ ? tr("Retrying the reviewed request with its original token…") :
        tr("Materializing the reviewed query on the server…"));
    const QPointer<DatasetStudio> self(this);
    if (send(SDP_DATASET_CREATE, reviewed_, [self](const SqlReply& reply) {
        if (!self) return;
        if (reply.error.unknown_outcome)
        {
            self->unknown_ = true;
            self->setStatus(QObject::tr("Dataset outcome unknown. Reconnect and retry this exact request to retrieve its result. Token %1")
                .arg(self->reviewed_.value("request_token").toString()));
            self->setBusy(false);
            return;
        }
        self->unknown_ = false;
        if (reply.error.code != SDP_OK)
        {
            self->setStatus(errorText(reply));
            self->setBusy(false);
            return;
        }
        const auto id = reply.data.value("dataset_id").toString();
        self->reviewed_ = {};
        if (!datasetId(id))
        {
            self->setStatus(QObject::tr("The server did not return a dataset ID."));
            self->setBusy(false);
            return;
        }
        self->id_->setText(id);
        self->remember(id);
        self->load(id, true);
    })) return;
    setStatus(tr("The dataset create request could not start."));
    setBusy(false);
}

void DatasetStudio::inspect()
{
    if (!available()) return;
    const auto id = id_->text().trimmed();
    if (!datasetId(id))
    {
        setStatus(tr("Enter the 32-character lowercase hexadecimal dataset ID returned by the server."));
        return;
    }
    load(id);
}

void DatasetStudio::load(const QString& id, bool afterCreate)
{
    clear();
    tabs_->setCurrentWidget(inspectorPage_);
    setBusy(true);
    setStatus(tr("Reading the materialized dataset…"));
    const QPointer<DatasetStudio> self(this);
    if (send(SDP_DATASET_GET, {{"dataset_id", id}}, [self, id, afterCreate](const SqlReply& reply) {
        if (!self) return;
        if (reply.error.code != SDP_OK)
        {
            self->setStatus(afterCreate ? QObject::tr("Dataset committed as %1. Inspection failed. %2").arg(id, errorText(reply)) :
                errorText(reply));
            self->setBusy(false);
            return;
        }
        if (reply.data.value("dataset_id").toString() != id || !self->render(reply.data))
        {
            self->setStatus(QObject::tr("The server returned an inconsistent dataset snapshot."));
            self->setBusy(false);
            return;
        }
        self->dataset_ = reply.data;
        if (!afterCreate) self->remember(id);
        self->setStatus(QObject::tr("Dataset snapshot loaded."));
        self->setBusy(false);
    })) return;
    setStatus(afterCreate ? tr("Dataset committed as %1. Start inspection again.").arg(id) :
        tr("The dataset inspection could not start."));
    setBusy(false);
}

void DatasetStudio::clear()
{
    dataset_ = {};
    scope_->clear();
    sample_->setRowCount(0);
    sample_->setColumnCount(0);
    stats_->clear();
    splits_->clear();
    source_->clear();
    summary_->clear();
}

bool DatasetStudio::render(const QJsonObject& dataset)
{
    const auto source = dataset.value("source").toObject();
    const auto columns = source.value("columns").toArray();
    const auto rows = source.value("rows").toArray();
    const auto types = dataset.value("column_types").toArray();
    const auto captured = dataset.value("row_count").toInteger(-1);
    const auto options = dataset.value("options").toObject();
    const auto prepared = dataset.value("prepared").toObject();
    if (captured < rows.size() || rows.size() > 1024 || columns.isEmpty() || columns.size() > 128 ||
        columns.size() != types.size() || prepared.isEmpty())
        return false;

    QStringList headings;
    QHash<QString, QString> transformByName;
    for (const auto entry : options.value("features").toArray())
    {
        const auto feature = entry.toObject();
        transformByName.insert(feature.value("column").toString(), feature.value("transform").toString());
    }
    const auto target = options.value("target").toString();
    const auto targetKind = options.value("target_kind").toString();
    QList<bool> categorical;
    for (const auto name : columns)
    {
        if (!validColumn(name)) return false;
        headings.push_back(name.toString());
        categorical.push_back(transformByName.value(name.toString()) == u"onehot" ||
            (name.toString() == target && targetKind == u"categorical"));
    }

    QList<Stats> observed(columns.size());
    for (const auto row : rows)
    {
        const auto values = row.toArray();
        if (values.size() != columns.size()) return false;
        for (qsizetype column = 0; column < columns.size(); ++column)
            observed[column].add(values.at(column), categorical.at(column));
    }
    const auto count = std::min(displayRows, static_cast<int>(rows.size()));
    scope_->setText(tr("Owner %1 · Source revision %2 · %3 bytes\n"
        "Statistics cover %4 returned rows of the %5 captured by the query. The grid shows the first %6.")
        .arg(dataset.value("owner").toString(), dataset.value("revision").toString())
        .arg(dataset.value("byte_count").toInteger()).arg(rows.size()).arg(captured).arg(count));
    sample_->setColumnCount(static_cast<int>(columns.size()));
    sample_->setHorizontalHeaderLabels(headings);
    sample_->setRowCount(count);
    for (int row = 0; row < count; ++row)
    {
        const auto values = rows.at(row).toArray();
        for (int column = 0; column < values.size(); ++column)
        {
            const auto value = values.at(column);
            auto* item = new QTableWidgetItem(display(value));
            if (value.isNull()) item->setToolTip(tr("SQL NULL"));
            else if (value.isString()) item->setToolTip(tr("Text value"));
            sample_->setItem(row, column, item);
        }
    }

    for (qsizetype column = 0; column < columns.size(); ++column)
    {
        const auto& stat = observed.at(column);
        const auto& name = headings.at(column);
        const auto role = name == target ? tr("Target") : transformByName.contains(name) ? tr("Feature") : QString{};
        const auto nullRate = rows.isEmpty() ? tr("N/A") :
            QString::number(static_cast<double>(stat.absent) * 100.0 / static_cast<double>(rows.size()), 'f', 2) + u'%';
        new QTreeWidgetItem(stats_, {name, role, types.at(column).toString(), QString::number(stat.absent), nullRate,
            stat.distribution()});
    }
    for (int column = 0; column < 5; ++column) stats_->resizeColumnToContents(column);

    const auto split = prepared.value("split").toObject();
    auto* partitions = new QTreeWidgetItem(splits_, {tr("Actual split"), options.value("split").toObject().value("mode").toString()});
    for (const auto& entry : {std::pair{"train", tr("Training")}, std::pair{"validation", tr("Validation")}, std::pair{"test", tr("Test")}})
    {
        const auto key = QLatin1StringView(entry.first);
        if (split.value(key).isArray())
            new QTreeWidgetItem(partitions, {entry.second, tr("%1 rows").arg(split.value(key).toArray().size())});
    }
    const auto transformed = prepared.value("transforms").toObject();
    if (transformed.value("width").isDouble())
        new QTreeWidgetItem(splits_, {tr("Fitted feature width"), QString::number(transformed.value("width").toInteger())});
    auto* mappings = new QTreeWidgetItem(splits_, {tr("Feature transforms fitted on training rows")});
    for (const auto entry : transformed.value("features").toArray())
    {
        const auto transform = entry.toObject();
        auto* feature = new QTreeWidgetItem(mappings, {transform.value("column").toString(), transform.value("transform").toString()});
        for (const auto& field : {std::pair{"impute", tr("Missing values")}, std::pair{"fill", tr("Fill")},
                std::pair{"offset", tr("Offset")}, std::pair{"scale", tr("Scale")}, std::pair{"categories", tr("Categories")}})
        {
            const auto value = transform.value(QLatin1StringView(field.first));
            if (!value.isUndefined()) new QTreeWidgetItem(feature, {field.second, display(value)});
        }
    }
    if (!target.isEmpty())
        new QTreeWidgetItem(splits_, {tr("Target"), target + QStringLiteral(" · ") +
            (targetKind.isEmpty() ? QStringLiteral("numeric") : targetKind)});
    const auto classes = prepared.value("target_classes").toArray();
    if (!classes.isEmpty()) new QTreeWidgetItem(splits_, {tr("Target classes"), display(classes)});
    splits_->expandItem(partitions);
    splits_->expandItem(mappings);
    splits_->resizeColumnToContents(0);
    source_->setPlainText(dataset.value("query").toString());
    summary_->setPlainText(pretty(dataset));
    return true;
}

void DatasetStudio::remember(const QString& id)
{
    recentStore_.sync();
    auto ids = recentStore_.value(recentKey_).toStringList();
    ids.removeAll(id);
    ids.prepend(id);
    while (ids.size() > maximumRecentIds) ids.removeLast();
    recentStore_.setValue(recentKey_, ids);
    refreshRecent();
}

void DatasetStudio::refreshRecent()
{
    recents_->clear();
    recents_->addItem(tr("Recent IDs for this connection"));
    auto ids = recentStore_.value(recentKey_).toStringList();
    ids.removeIf([](const QString& id) { return !datasetId(id); });
    recents_->addItems(ids.mid(0, maximumRecentIds));
}
}

QDialog* showDatasetStudio(SqlSession& session, QWidget* parent, std::function<void(const QJsonObject&)> selected,
    const QString& initialSql)
{
    auto* studio = new DatasetStudio(session, parent, std::move(selected), initialSql);
    studio->show();
    return studio;
}
}
