#include "history_studio.hpp"

#include "sql_session.hpp"
#include "table_design.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QSplitter>
#include <QTabWidget>
#include <QTimeZone>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>

#include <optional>

namespace wb
{
namespace
{
constexpr int pageSize = 32;
constexpr int readTimeout = 15000;
constexpr int recoveryTimeout = 30000;

QStringList parts(const QJsonValue& value)
{
    QStringList names;
    for (const auto part : value.toArray()) names.push_back(part.toString());
    return names;
}

QString pretty(const QJsonValue& value)
{
    if (value.isObject()) return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Indented));
    if (value.isArray()) return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Indented));
    if (value.isString()) return value.toString();
    if (value.isNull() || value.isUndefined()) return QObject::tr("No image");
    if (value.isBool()) return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return QString::number(value.toDouble(), 'g', 16);
}

QString compact(const QJsonValue& value)
{
    if (value.isObject()) return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    if (value.isArray()) return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    return pretty(value);
}

bool decimalId(const QString& text)
{
    bool ok = false;
    const auto id = text.toULongLong(&ok);
    return ok && id > 0 && QString::number(id) == text;
}

bool addId(QJsonObject& body, const char* key, const QLineEdit& field, bool required = false)
{
    const auto value = field.text().trimmed();
    if (value.isEmpty()) return !required;
    if (!decimalId(value)) return false;
    body.insert(QLatin1StringView(key), value);
    return true;
}

std::optional<qint64> dateBound(QString value, bool upper)
{
    const auto date = QDate::fromString(value, Qt::ISODate);
    if (date.isValid())
        return QDateTime(date, upper ? QTime(23, 59, 59) : QTime(0, 0, 0), QTimeZone::UTC).toSecsSinceEpoch();
    auto time = QDateTime::fromString(value, Qt::ISODate);
    if (!time.isValid()) return {};
    if (time.timeSpec() == Qt::LocalTime) time = QDateTime(time.date(), time.time(), QTimeZone::UTC);
    return time.toSecsSinceEpoch();
}

bool addDate(QJsonObject& body, const char* key, const QLineEdit& field, bool upper)
{
    const auto text = field.text().trimmed();
    if (text.isEmpty()) return true;
    const auto timestamp = dateBound(text, upper);
    if (!timestamp) return false;
    body.insert(QLatin1StringView(key), *timestamp);
    return true;
}

QString fromUtc(const QJsonValue& value)
{
    if (!value.isDouble()) return {};
    return QDateTime::fromSecsSinceEpoch(value.toInteger(), QTimeZone::UTC).toString(Qt::ISODate);
}

QString requestLabel(sdp_history_operation operation)
{
    switch (operation)
    {
    case SDP_HISTORY_LOG: return QStringLiteral("SDP_HISTORY_LOG");
    case SDP_HISTORY_DIFF: return QStringLiteral("SDP_HISTORY_DIFF");
    case SDP_HISTORY_READ: return QStringLiteral("SDP_HISTORY_READ");
    case SDP_HISTORY_VERIFY: return QStringLiteral("SDP_HISTORY_VERIFY");
    case SDP_HISTORY_STATUS: return QStringLiteral("SDP_HISTORY_STATUS");
    case SDP_HISTORY_RESTORE: return QStringLiteral("SDP_HISTORY_RESTORE");
    case SDP_HISTORY_REVERT: return QStringLiteral("SDP_HISTORY_REVERT");
    }
    return {};
}

QPlainTextEdit* viewer(const char* name)
{
    auto* widget = new QPlainTextEdit;
    widget->setObjectName(name);
    widget->setReadOnly(true);
    return widget;
}

QLineEdit* field(const char* name, const QString& placeholder = {})
{
    auto* widget = new QLineEdit;
    widget->setObjectName(name);
    widget->setPlaceholderText(placeholder);
    return widget;
}

QString deniedMessage(const SqlReply& reply)
{
    const auto message = QString::fromUtf8(reply.error.message);
    const auto summary = wb::status(reply.error.code);
    return message.isEmpty() ? summary : QObject::tr("%1 · %2").arg(summary, message);
}

struct HistoryPage
{
    sdp_history_operation operation{};
    QJsonObject request;
    std::optional<qint64> next;
};

struct Recovery
{
    sdp_history_operation operation{};
    QJsonObject plan;
    QString token;

    QJsonObject applyRequest() const
    {
        return {{"plan_id", plan.value("plan_id")}, {"request_token", token}};
    }
};

class HistoryStudio : public QDialog
{
public:
    HistoryStudio(SqlSession&, const QJsonArray&, const QJsonObject&, QWidget*, std::function<void()> refresh);
    void reject() override
    {
        if (!busy_) QDialog::reject();
    }

private:
    QPushButton* action(QLayout*, const char* name, const QString& label, std::function<void()>);
    void work(bool busy);
    bool available(bool forRecovery = false);
    void setStatus(QString status) { status_->setText(std::move(status)); }
    void selectTarget(const QString& target);
    QString target() const { return target_->currentText().trimmed(); }
    void clearReview();
    void resetPaging();
    void useSelection();
    void showSelection();
    void firstPage(sdp_history_operation);
    void fetchPage(sdp_history_operation, QJsonObject);
    void showEntries(const QJsonArray&, bool applyLogFilters);
    void preview();
    void compare(const QString& target, const QString& revision, const QString& row, bool current);
    void apply();
    void showRetention(bool verify);
    void lookup();
    bool call(sdp_history_operation, QJsonObject, SqlSession::Finished, int timeout = readTimeout, bool record = true);

    SqlSession& session_;
    std::function<void()> refresh_;
    QComboBox* target_{};
    QLineEdit* from_{};
    QLineEdit* to_{};
    QLineEdit* row_{};
    QLineEdit* revision_{};
    QLineEdit* actor_{};
    QLineEdit* since_{};
    QLineEdit* until_{};
    QCheckBox* deleted_{};
    QTreeWidget* entries_{};
    QTreeWidget* changes_{};
    QPlainTextEdit* before_{};
    QPlainTextEdit* after_{};
    QPlainTextEdit* snapshot_{};
    QTabWidget* tabs_{};
    QWidget* entryPage_{};
    QWidget* snapshotPage_{};
    QWidget* recoveryPage_{};
    QWidget* retentionPage_{};
    QComboBox* mode_{};
    QLineEdit* field_{};
    QPlainTextEdit* plan_{};
    QPlainTextEdit* sourceImage_{};
    QPlainTextEdit* currentImage_{};
    QPlainTextEdit* retention_{};
    QLabel* retentionSummary_{};
    QPlainTextEdit* request_{};
    QLabel* anchor_{};
    QLabel* status_{};
    QPushButton* next_{};
    QPushButton* apply_{};
    QPushButton* recoverLookup_{};
    HistoryPage page_;
    std::optional<Recovery> recovery_;
    bool busy_{};
    bool unknown_{};
};

HistoryStudio::HistoryStudio(SqlSession& session, const QJsonArray& catalog, const QJsonObject& selected,
    QWidget* parent, std::function<void()> refresh)
    : QDialog(parent), session_(session), refresh_(std::move(refresh))
{
    setObjectName("historyStudio");
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("History Studio · %1").arg(endpoint(session.profile())));
    resize(1060, 720);
    auto* layout = new QVBoxLayout(this);
    auto* filters = new QFormLayout;
    target_ = new QComboBox;
    target_->setObjectName("historyTarget");
    target_->setEditable(true);
    target_->setInsertPolicy(QComboBox::NoInsert);
    target_->addItem(QString{});
    for (const auto entry : catalog)
    {
        const auto item = entry.toObject();
        if (item.value("kind").toInt() < 3) continue;
        const auto name = sqlQualifiedIdentifier(parts(item.value("qualified_name")));
        if (!name.isEmpty()) target_->addItem(name);
    }
    selectTarget(sqlQualifiedIdentifier(parts(selected.value("qualified_name"))));
    filters->addRow(tr("Object"), target_);
    from_ = field("historyFrom", tr("First revision"));
    to_ = field("historyTo", tr("Last revision"));
    row_ = field("historyRow", tr("Stable row ID"));
    revision_ = field("historyRevision", tr("Source or time travel revision"));
    auto* interval = new QHBoxLayout;
    interval->addWidget(from_);
    interval->addWidget(to_);
    interval->addWidget(row_);
    interval->addWidget(revision_);
    filters->addRow(tr("Revisions and row"), interval);
    since_ = field("historySince", tr("Since UTC · YYYY-MM-DD"));
    until_ = field("historyUntil", tr("Until UTC · YYYY-MM-DD"));
    actor_ = field("historyActor", tr("Exact actor name"));
    deleted_ = new QCheckBox(tr("Deleted rows only"));
    deleted_->setObjectName("historyDeletedOnly");
    auto* logFilters = new QHBoxLayout;
    logFilters->addWidget(since_);
    logFilters->addWidget(until_);
    logFilters->addWidget(actor_);
    logFilters->addWidget(deleted_);
    filters->addRow(tr("Log filters"), logFilters);
    layout->addLayout(filters);

    auto* actions = new QHBoxLayout;
    action(actions, "historyLoadLog", tr("Load log"), [this] { firstPage(SDP_HISTORY_LOG); });
    action(actions, "historyLoadDiff", tr("Compare revisions"), [this] { firstPage(SDP_HISTORY_DIFF); });
    action(actions, "historyLoadRead", tr("Read as of revision"), [this] { firstPage(SDP_HISTORY_READ); });
    next_ = action(actions, "historyNextPage", tr("Next page"), [this] {
        if (page_.next && !busy_)
        {
            auto next = page_.request;
            next.insert("offset", *page_.next);
            fetchPage(page_.operation, std::move(next));
        }
    });
    actions->addStretch();
    layout->addLayout(actions);

    tabs_ = new QTabWidget;
    tabs_->setObjectName("historyTabs");
    layout->addWidget(tabs_, 1);
    entryPage_ = new QWidget;
    auto* entriesLayout = new QVBoxLayout(entryPage_);
    entries_ = new QTreeWidget;
    entries_->setObjectName("historyEntries");
    entries_->setHeaderLabels({tr("Revision"), tr("Commit UTC"), tr("Actor"), tr("Object"), tr("Kind"), tr("Row ID"), tr("Change")});
    entries_->setRootIsDecorated(false);
    entries_->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    entriesLayout->addWidget(entries_, 2);
    auto* selection = new QHBoxLayout;
    action(selection, "historyUseSelection", tr("Use selected revision and row"), [this] { useSelection(); });
    selection->addStretch();
    entriesLayout->addLayout(selection);
    changes_ = new QTreeWidget;
    changes_->setObjectName("historyFieldDiff");
    changes_->setHeaderLabels({tr("Changed field"), tr("Before"), tr("After")});
    changes_->setRootIsDecorated(false);
    entriesLayout->addWidget(changes_, 1);
    auto* images = new QSplitter;
    before_ = viewer("historyBefore");
    after_ = viewer("historyAfter");
    before_->setPlaceholderText(tr("Before image"));
    after_->setPlaceholderText(tr("After image"));
    images->addWidget(before_);
    images->addWidget(after_);
    entriesLayout->addWidget(images, 1);
    tabs_->addTab(entryPage_, tr("Log and comparison"));
    connect(entries_, &QTreeWidget::currentItemChanged, this, [this] { showSelection(); });

    snapshotPage_ = new QWidget;
    auto* snapshotLayout = new QVBoxLayout(snapshotPage_);
    snapshot_ = viewer("historySnapshot");
    snapshotLayout->addWidget(snapshot_);
    tabs_->addTab(snapshotPage_, tr("Time travel"));

    recoveryPage_ = new QWidget;
    auto* recoveryLayout = new QVBoxLayout(recoveryPage_);
    auto* recoveryForm = new QFormLayout;
    mode_ = new QComboBox;
    mode_->setObjectName("historyRecoveryMode");
    mode_->addItem(tr("Restore historical state"), static_cast<unsigned>(SDP_HISTORY_RESTORE));
    mode_->addItem(tr("Revert changes of a revision"), static_cast<unsigned>(SDP_HISTORY_REVERT));
    field_ = field("historyField", tr("Optional column name"));
    recoveryForm->addRow(tr("Operation"), mode_);
    recoveryForm->addRow(tr("Single field"), field_);
    recoveryLayout->addLayout(recoveryForm);
    auto* recoveryActions = new QHBoxLayout;
    action(recoveryActions, "historyPreview", tr("Preview recovery"), [this] { preview(); });
    apply_ = action(recoveryActions, "historyApply", tr("Apply reviewed recovery"), [this] { apply(); });
    recoverLookup_ = action(recoveryActions, "historyResolve", tr("Check request outcome"), [this] { lookup(); });
    recoveryActions->addStretch();
    recoveryLayout->addLayout(recoveryActions);
    auto* validation = new QLabel(tr("The server revalidates permissions, current rules and constraints when applying a recovery plan."));
    validation->setTextFormat(Qt::PlainText);
    validation->setWordWrap(true);
    recoveryLayout->addWidget(validation);
    plan_ = viewer("historyPlan");
    plan_->setPlaceholderText(tr("The server’s recovery plan and affected rows"));
    recoveryLayout->addWidget(plan_, 1);
    auto* comparison = new QSplitter;
    sourceImage_ = viewer("historySourceImage");
    currentImage_ = viewer("historyCurrentImage");
    sourceImage_->setPlaceholderText(tr("Historical source image"));
    currentImage_->setPlaceholderText(tr("Current image at preview revision"));
    comparison->addWidget(sourceImage_);
    comparison->addWidget(currentImage_);
    recoveryLayout->addWidget(comparison, 1);
    tabs_->addTab(recoveryPage_, tr("Recovery"));

    retentionPage_ = new QWidget;
    auto* retentionLayout = new QVBoxLayout(retentionPage_);
    auto* retentionActions = new QHBoxLayout;
    action(retentionActions, "historyLoadStatus", tr("Refresh retention"), [this] { showRetention(false); });
    action(retentionActions, "historyVerify", tr("Verify history"), [this] { showRetention(true); });
    retentionActions->addStretch();
    retentionLayout->addLayout(retentionActions);
    retentionSummary_ = new QLabel;
    retentionSummary_->setObjectName("historyRetentionSummary");
    retentionSummary_->setTextFormat(Qt::PlainText);
    retentionLayout->addWidget(retentionSummary_);
    retention_ = viewer("historyRetentionStatus");
    retentionLayout->addWidget(retention_);
    tabs_->addTab(retentionPage_, tr("Retention and verification"));

    request_ = viewer("historyRequest");
    tabs_->addTab(request_, tr("Last API request"));
    auto* footer = new QHBoxLayout;
    action(footer, "historyCopyRequest", tr("Copy API request"), [this] {
        if (!request_->toPlainText().isEmpty()) QApplication::clipboard()->setText(request_->toPlainText());
    });
    footer->addStretch();
    anchor_ = new QLabel;
    footer->addWidget(anchor_);
    action(footer, "historyClose", tr("Close"), [this] { reject(); });
    layout->addLayout(footer);

    status_ = new QLabel;
    status_->setObjectName("historyStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);

    for (auto* input : {from_, to_, since_, until_, actor_, row_, revision_})
        connect(input, &QLineEdit::textChanged, this, [this] { resetPaging(); });
    for (auto* input : {row_, revision_, field_})
        connect(input, &QLineEdit::textChanged, this, [this] { clearReview(); });
    connect(mode_, &QComboBox::currentIndexChanged, this, [this] { clearReview(); });
    connect(target_, &QComboBox::currentTextChanged, this, [this] { resetPaging(); clearReview(); });
    connect(deleted_, &QCheckBox::toggled, this, [this] { resetPaging(); });
    work(false);
}

QPushButton* HistoryStudio::action(QLayout* layout, const char* name, const QString& label, std::function<void()> run)
{
    auto* button = new QPushButton(label);
    button->setObjectName(name);
    layout->addWidget(button);
    connect(button, &QPushButton::clicked, this, std::move(run));
    return button;
}

void HistoryStudio::work(bool busy)
{
    busy_ = busy;
    next_->setEnabled(!busy && page_.next.has_value());
    apply_->setEnabled(!busy && recovery_.has_value() && !unknown_ && !session_.profile().read_only);
    recoverLookup_->setEnabled(!busy && unknown_ && recovery_.has_value() && session_.state().connected);
    target_->setEnabled(!busy && !unknown_);
    for (auto* filter : {from_, to_, row_, revision_, since_, until_, actor_, field_})
        filter->setReadOnly(busy || unknown_);
    mode_->setEnabled(!busy && !unknown_);
    deleted_->setEnabled(!busy);
}

bool HistoryStudio::available(bool forRecovery)
{
    if (busy_) return false;
    if (!session_.state().connected)
    {
        setStatus(tr("Reconnect the worksheet to use History."));
        return false;
    }
    if (session_.busy() || session_.state().transactionActive)
    {
        setStatus(tr("Finish the worksheet operation or transaction before using History."));
        return false;
    }
    if (!session_.capabilities().value("history_v1").toBool() ||
        (forRecovery && !session_.capabilities().value("history_restore_v1").toBool()))
    {
        setStatus(forRecovery ? tr("This server does not expose History recovery.") : tr("This server does not expose History."));
        return false;
    }
    return true;
}

void HistoryStudio::selectTarget(const QString& name)
{
    const auto found = target_->findText(name);
    if (found >= 0) target_->setCurrentIndex(found);
    else target_->setEditText(name);
}

void HistoryStudio::resetPaging()
{
    page_ = {};
    if (next_) next_->setEnabled(false);
}

void HistoryStudio::clearReview()
{
    if (unknown_) return;
    recovery_.reset();
    plan_->clear();
    sourceImage_->clear();
    currentImage_->clear();
    apply_->setEnabled(false);
}
bool HistoryStudio::call(sdp_history_operation operation, QJsonObject body, SqlSession::Finished done, int timeout, bool record)
{
    const auto json = QJsonDocument(body).toJson(QJsonDocument::Compact);
    const auto sent = session_.request([operation, json](auto handle, auto* output, auto* error) {
        const sdp_text request{json.constData(), static_cast<uint32_t>(json.size())};
        return sdp_history_command(handle, operation, request, output, error);
    }, std::move(done), timeout);
    if (sent && record)
        request_->setPlainText(QStringLiteral("sdp_history_command · %1\n%2").arg(requestLabel(operation), pretty(body)));
    return sent;
}

void HistoryStudio::firstPage(sdp_history_operation operation)
{
    if (!available()) return;
    QJsonObject query{{"offset", 0}, {"limit", pageSize}};
    const auto object = target();
    if (!object.isEmpty()) query.insert("target", object);
    if ((object.isEmpty() && operation != SDP_HISTORY_LOG) || !addId(query, "row_id", *row_))
    {
        setStatus(tr("Choose an object and supply a positive decimal row ID when filtering rows."));
        return;
    }
    if (operation == SDP_HISTORY_READ)
    {
        if (!addId(query, "revision", *revision_, true))
        {
            setStatus(tr("Enter a recoverable revision for the read."));
            return;
        }
    }
    else
    {
        const bool diff = operation == SDP_HISTORY_DIFF;
        if (!addId(query, "from", *from_, diff) || !addId(query, "to", *to_, diff) ||
            (!diff && (!addDate(query, "since_utc", *since_, false) || !addDate(query, "until_utc", *until_, true))))
        {
            setStatus(tr("Check the positive revision numbers and ISO UTC date filters."));
            return;
        }
        const auto first = query.value("from").toString().toULongLong();
        const auto last = query.value("to").toString().toULongLong();
        if (first && last && (first > last || (diff && first == last)))
        {
            setStatus(tr("Choose the revision bounds in ascending order."));
            return;
        }
    }
    resetPaging();
    fetchPage(operation, std::move(query));
}

void HistoryStudio::fetchPage(sdp_history_operation operation, QJsonObject query)
{
    if (!available()) return;
    const bool read = operation == SDP_HISTORY_READ;
    tabs_->setCurrentWidget(read ? snapshotPage_ : entryPage_);
    work(true);
    setStatus(tr("Loading History…"));
    const QPointer<HistoryStudio> self(this);
    const auto submitted = query;
    if (call(operation, std::move(query), [self, operation, submitted, read](const SqlReply& reply) {
        if (!self) return;
        if (reply.error.code != SDP_OK)
        {
            self->resetPaging();
            if (read) self->snapshot_->clear();
            else self->entries_->clear();
            self->anchor_->clear();
            self->setStatus(deniedMessage(reply));
            self->work(false);
            return;
        }
        auto bounds = submitted;
        if (operation == SDP_HISTORY_LOG)
        {
            for (const auto& range : {std::pair{"from", "earliest_revision"}, std::pair{"to", "current_revision"}})
            {
                const auto revision = reply.data.value(QLatin1StringView(range.second)).toString();
                if (!bounds.contains(QLatin1StringView(range.first)) && decimalId(revision))
                    bounds.insert(QLatin1StringView(range.first), revision);
            }
        }
        const auto more = reply.data.value("next_offset").toInteger(-1);
        self->page_ = {operation, std::move(bounds), more >= 0 ? std::optional<qint64>(more) : std::nullopt};
        if (read)
        {
            self->snapshot_->setPlainText(pretty(reply.data));
            self->anchor_->setText(QObject::tr("Snapshot revision %1").arg(reply.data.value("revision").toString()));
            self->setStatus(QObject::tr("Historical data is read-only."));
        }
        else
        {
            const auto rows = reply.data.value("entries").toArray();
            self->showEntries(rows, operation == SDP_HISTORY_LOG);
            if (operation == SDP_HISTORY_LOG)
            {
                const auto first = self->page_.request.value("from").toString();
                const auto last = self->page_.request.value("to").toString();
                self->anchor_->setText(first.isEmpty() || last.isEmpty() ? QString{} : QObject::tr("Revisions %1–%2").arg(first, last));
            }
            else self->anchor_->setText(QObject::tr("Compared revisions %1–%2")
                .arg(submitted.value("from").toString(), submitted.value("to").toString()));
            self->setStatus(QObject::tr("%1 of %2 records on this page.").arg(self->entries_->topLevelItemCount()).arg(rows.size()));
        }
        self->work(false);
    })) return;
    setStatus(tr("The History request could not start."));
    work(false);
}

void HistoryStudio::showEntries(const QJsonArray& rows, bool applyLogFilters)
{
    entries_->clear();
    const auto matchingActor = actor_->text().trimmed();
    for (const auto entry : rows)
    {
        const auto item = entry.toObject();
        const auto actor = item.value("actor").toString();
        const auto kind = item.value("kind").toString();
        const auto before = item.value("before");
        const auto after = item.value("after");
        if (applyLogFilters && ((!matchingActor.isEmpty() && actor != matchingActor) ||
            (deleted_->isChecked() && (kind != u"row" || before.isNull() || !after.isNull())))) continue;
        const auto change = before.isNull() ? tr("Created") : after.isNull() ? tr("Deleted") : tr("Changed");
        auto* row = new QTreeWidgetItem(entries_, {item.value("revision").toString(), fromUtc(item.value("commit_utc")),
            actor, parts(item.value("name")).join(u'.'), kind, item.value("row_id").toString(), change});
        row->setData(0, Qt::UserRole, item);
    }
    if (entries_->topLevelItemCount() > 0) entries_->setCurrentItem(entries_->topLevelItem(0));
    else showSelection();
}

void HistoryStudio::showSelection()
{
    changes_->clear();
    const auto* item = entries_->currentItem();
    if (!item)
    {
        before_->clear();
        after_->clear();
        return;
    }
    const auto entry = item->data(0, Qt::UserRole).toJsonObject();
    const auto before = entry.value("before");
    const auto after = entry.value("after");
    before_->setPlainText(pretty(before));
    after_->setPlainText(pretty(after));
    const auto fields = [&](const QJsonValue& image) {
        const auto object = image.toObject();
        return entry.value("kind").toString() == u"row" ? object.value("values").toObject() : object;
    };
    const auto old = fields(before);
    const auto current = fields(after);
    QSet<QString> keys;
    for (const auto& name : old.keys()) keys.insert(name);
    for (const auto& name : current.keys()) keys.insert(name);
    auto ordered = keys.values();
    ordered.sort();
    for (const auto& name : ordered)
    {
        const auto a = old.value(name);
        const auto b = current.value(name);
        if (a != b) new QTreeWidgetItem(changes_, {name, compact(a).left(200), compact(b).left(200)});
    }
    changes_->resizeColumnToContents(0);
}

void HistoryStudio::useSelection()
{
    if (busy_ || unknown_ || !entries_->currentItem()) return;
    const auto item = entries_->currentItem()->data(0, Qt::UserRole).toJsonObject();
    selectTarget(sqlQualifiedIdentifier(parts(item.value("name"))));
    row_->setText(item.value("row_id").toString());
    const bool restoringDelete = mode_->currentData().toUInt() == SDP_HISTORY_RESTORE &&
        !item.value("before").isNull() && item.value("after").isNull();
    const auto revision = item.value(restoringDelete ? QStringLiteral("parent") : QStringLiteral("revision")).toString();
    revision_->setText(revision);
    tabs_->setCurrentWidget(recoveryPage_);
    setStatus(tr("Loaded the selected object's recovery inputs."));
}

void HistoryStudio::preview()
{
    if (!available(true) || unknown_) return;
    const auto name = target();
    QJsonObject query{{"target", name}};
    const auto column = field_->text().trimmed();
    if (name.isEmpty() || !addId(query, "revision", *revision_, true) || !addId(query, "row_id", *row_) ||
        (!column.isEmpty() && !query.contains("row_id")))
    {
        setStatus(tr("Enter an object and revision. A field also requires a stable row ID."));
        return;
    }
    if (!column.isEmpty())
    {
        const auto identifier = sqlQualifiedIdentifier({column});
        if (identifier.isEmpty())
        {
            setStatus(tr("Enter a valid column name."));
            return;
        }
        query.insert("field", identifier);
    }
    const auto operation = mode_->currentData().toUInt();
    clearReview();
    tabs_->setCurrentWidget(recoveryPage_);
    work(true);
    setStatus(tr("Requesting a recovery plan from the server…"));
    const QPointer<HistoryStudio> self(this);
    if (call(operation, query, [self, operation](const SqlReply& reply) {
        if (!self) return;
        if (reply.error.code != SDP_OK)
        {
            self->setStatus(deniedMessage(reply));
            self->work(false);
            return;
        }
        const auto id = reply.data.value("plan_id").toString();
        const auto current = reply.data.value("current_revision").toString();
        const auto target = sqlQualifiedIdentifier(parts(reply.data.value("name")));
        auto source = reply.data.value("revision").toString();
        if (id.isEmpty() || !decimalId(source) || !decimalId(current) || target.isEmpty())
        {
            self->setStatus(QObject::tr("The server returned an incomplete recovery plan."));
            self->work(false);
            return;
        }
        if (operation == SDP_HISTORY_REVERT) source = QString::number(source.toULongLong() - 1);
        self->recovery_ = Recovery{operation, reply.data, {}};
        self->plan_->setPlainText(pretty(reply.data));
        self->compare(target, source, reply.data.value("row_id").toString(), false);
    }, recoveryTimeout)) return;
    setStatus(tr("The recovery preview could not start."));
    work(false);
}

void HistoryStudio::compare(const QString& target, const QString& revision, const QString& row, bool current)
{
    QJsonObject query{{"target", target}, {"revision", revision}, {"offset", 0}, {"limit", pageSize}};
    if (!row.isEmpty()) query.insert("row_id", row);
    const QPointer<HistoryStudio> self(this);
    if (call(SDP_HISTORY_READ, query, [self, target, row, current](const SqlReply& reply) {
        if (!self || !self->recovery_) return;
        auto* view = current ? self->currentImage_ : self->sourceImage_;
        view->setPlainText(reply.error.code == SDP_OK ? pretty(reply.data)
            : QObject::tr("Comparison image unavailable. %1").arg(deniedMessage(reply)));
        if (!current)
        {
            self->compare(target, self->recovery_->plan.value("current_revision").toString(), row, true);
            return;
        }
        self->setStatus(self->session_.profile().read_only
            ? QObject::tr("Recovery preview loaded. This profile is read-only.")
            : QObject::tr("Recovery preview loaded. Review the source, current data and effects before applying."));
        self->work(false);
    }, readTimeout, false)) return;
    setStatus(tr("The comparison could not start. Refresh the preview."));
    clearReview();
    work(false);
}

void HistoryStudio::apply()
{
    if (!available(true) || unknown_ || !recovery_) return;
    if (session_.profile().read_only)
    {
        setStatus(tr("Recovery apply is disabled for this read-only profile."));
        return;
    }
    const auto revision = recovery_->plan.value("current_revision").toString();
    const auto target = parts(recovery_->plan.value("name")).join(u'.');
    if (QMessageBox::question(this, tr("Apply reviewed History recovery"),
            tr("Apply the reviewed recovery for %1 against revision %2?").arg(target, revision),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    recovery_->token = QString::fromLatin1(QUuid::createUuid().toRfc4122().toHex());
    const auto request = recovery_->applyRequest();
    work(true);
    setStatus(tr("Applying the reviewed recovery…"));
    const QPointer<HistoryStudio> self(this);
    if (call(recovery_->operation, request, [self](const SqlReply& reply) {
        if (!self) return;
        if (reply.error.unknown_outcome)
        {
            self->unknown_ = true;
            self->setStatus(QObject::tr("Recovery outcome unknown. Keep the exact request and check token %1 before starting another recovery. %2")
                .arg(self->recovery_->token, deniedMessage(reply)));
            self->work(false);
            return;
        }
        if (reply.error.code != SDP_OK)
        {
            self->clearReview();
            self->setStatus(deniedMessage(reply));
            self->work(false);
            return;
        }
        const auto revision = reply.data.value("lsn").toString();
        self->clearReview();
        self->resetPaging();
        self->anchor_->setText(QObject::tr("Current revision %1").arg(revision));
        self->setProperty("historyCommittedRevision", revision);
        self->setStatus(QObject::tr("History recovery committed at revision %1.").arg(revision));
        self->work(false);
        if (self->refresh_) self->refresh_();
    }, recoveryTimeout)) return;
    setStatus(tr("The recovery request could not start. Review the preview and try again."));
    work(false);
}

void HistoryStudio::lookup()
{
    if (!unknown_ || !recovery_ || !available(true)) return;
    const auto token = recovery_->token;
    request_->setPlainText(QStringLiteral("sdp_history_command · %1\n%2\n\nsdp_tx_outcome\n%3")
        .arg(requestLabel(recovery_->operation), pretty(recovery_->applyRequest()), pretty(QJsonObject{{"request_token", token}})));
    work(true);
    const QPointer<HistoryStudio> self(this);
    if (session_.lookupOutcome(token, [self](const SqlReply& reply) {
        if (!self) return;
        if (reply.error.code != SDP_OK || reply.data.value("status").toString() != u"committed")
        {
            self->setStatus(reply.error.code == SDP_OK
                ? QObject::tr("A committed outcome has not been found. Keep the original request.")
                : deniedMessage(reply));
            self->work(false);
            return;
        }
        self->unknown_ = false;
        const auto revision = reply.data.value("lsn").toString();
        self->clearReview();
        self->resetPaging();
        self->setProperty("historyCommittedRevision", revision);
        self->setStatus(QObject::tr("History recovery committed at revision %1.").arg(revision));
        self->work(false);
        if (self->refresh_) self->refresh_();
    })) return;
    setStatus(tr("Checking the request outcome could not start."));
    work(false);
}

void HistoryStudio::showRetention(bool verify)
{
    if (!available()) return;
    tabs_->setCurrentWidget(retentionPage_);
    work(true);
    setStatus(verify ? tr("Verifying the server’s History journal…") : tr("Reading the retention boundary…"));
    const QPointer<HistoryStudio> self(this);
    if (call(verify ? SDP_HISTORY_VERIFY : SDP_HISTORY_STATUS, {}, [self, verify](const SqlReply& reply) {
        if (!self) return;
        if (reply.error.code != SDP_OK)
        {
            self->retention_->clear();
            self->retentionSummary_->clear();
            self->setStatus(deniedMessage(reply));
            self->work(false);
            return;
        }
        const auto value = [&](const char* field) { return reply.data.value(QLatin1StringView(field)).toString(); };
        self->retentionSummary_->setText(QObject::tr("Current revision %1\nEarliest recoverable revision %2\nRetained bytes %3 of quota %4")
            .arg(value("current_revision"), value("earliest_revision"), value("retained_bytes"), value("quota_bytes")));
        self->retention_->setPlainText(pretty(reply.data));
        self->setStatus(verify && reply.data.value("verified").toBool() ? QObject::tr("History verified.") :
            QObject::tr("History retention status loaded."));
        self->work(false);
    }, recoveryTimeout)) return;
    setStatus(tr("The retention request could not start."));
    work(false);
}
}

QDialog* showHistoryStudio(SqlSession& session, const QJsonArray& catalog, const QJsonObject& selected,
    QWidget* parent, std::function<void()> refresh)
{
    auto* studio = new HistoryStudio(session, catalog, selected, parent, std::move(refresh));
    studio->show();
    return studio;
}
}
