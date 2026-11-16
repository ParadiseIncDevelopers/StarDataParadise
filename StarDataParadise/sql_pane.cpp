#include "sql_pane.hpp"
#include "sql_definition.hpp"
#include "admin_center.hpp"
#include "backup_center.hpp"
#include "security_center.hpp"
#include "dataset_studio.hpp"
#include "job_center.hpp"
#include "ml_studio.hpp"
#include "table_codec.hpp"
#include "decision_studio.hpp"
#include "history_studio.hpp"
#include "routine_studio.hpp"
#include "routine_debugger.hpp"
#include "routine_export.hpp"
#include "data_grid.hpp"
#include "table_design.hpp"
#include "table_transfer.hpp"
#include "sql_editor.hpp"
#include "sql_script.hpp"

#include <QAbstractTableModel>
#include <QAction>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDir>
#include <QDialog>
#include <QLineEdit>
#include <QPointer>
#include <QCoreApplication>
#include <climits>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTableView>
#include <QTabWidget>
#include <QTimer>
#include <QThreadPool>
#include <QToolButton>
#include <QVBoxLayout>

namespace wb
{
namespace
{
constexpr qsizetype draftLimit = 64 * 1024;
constexpr qsizetype workspaceLimit = 256 * 1024;

QString pretty(const QJsonObject& object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
}

QJsonObject draftEntries(const QString& path)
{
    if (path.isEmpty()) return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > workspaceLimit) return {};
    return QJsonDocument::fromJson(file.read(workspaceLimit + 1)).object();
}

bool canRecover(const QString& sql)
{
    // Bind values are separate. Omit literal-bearing statements and credential commands.
    static const QRegularExpression sensitive(
        QStringLiteral(R"(['"]|--|/\*|\b(password|secret|token|credential|identified|grant|revoke|user)\b)"),
        QRegularExpression::CaseInsensitiveOption);
    return !sql.trimmed().isEmpty() && sql.toUtf8().size() <= draftLimit && !sensitive.match(sql).hasMatch();
}

bool isRead(const QString& sql)
{
    static const QRegularExpression read(
        QStringLiteral(R"(^\s*(?:EXPLAIN\s+(?:ANALYZE\s+)?)?(?:SELECT|WITH)\b)"),
        QRegularExpression::CaseInsensitiveOption);
    return read.match(sql).hasMatch();
}

QString cellText(const QJsonValue& v)
{
    if (v.isNull() || v.isUndefined()) return QStringLiteral("NULL");
    if (v.isString()) return v.toString();
    if (v.isBool()) return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isDouble()) return QString::number(v.toDouble(), 'g', 16);
    if (v.isArray()) return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    const auto o = v.toObject();
    const auto type = o.value("t").toString();
    if (type == "null") return QStringLiteral("NULL");
    if (type == "decimal")
    {
        auto digits = o.value("unscaled").toString();
        const auto scale = o.value("scale").toInt();
        const bool negative = digits.startsWith('-');
        if (negative) digits.remove(0, 1);
        if (scale > 0 && scale <= 38)
        {
            while (digits.size() <= scale) digits.prepend('0');
            digits.insert(digits.size() - scale, '.');
        }
        return negative ? "-" + digits : digits;
    }
    if (type == "binary") return QStringLiteral("0x") + o.value("hex").toString();
    if (o.contains("v") && !o.value("v").isObject()) return cellText(o.value("v"));
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
}

class SqlTable : public QAbstractTableModel
{
public:
    SqlTable(QJsonArray columns, QObject* parent) : QAbstractTableModel(parent), columns_(std::move(columns)) {}
    int rowCount(const QModelIndex& parent) const override { return parent.isValid() ? 0 : static_cast<int>(rows_.size()); }
    int columnCount(const QModelIndex& parent) const override { return parent.isValid() ? 0 : static_cast<int>(columns_.size()); }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole) return {};
        if (orientation == Qt::Vertical) return section + 1;
        const auto value = columns_.at(section);
        return value.isObject() ? value.toObject().value("name").toString() : value.toString();
    }
    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid()) return {};
        const auto value = rows_.at(index.row()).toArray().at(index.column());
        if (role == Qt::UserRole) return value.toVariant();
        if (role == Qt::DisplayRole || role == Qt::ToolTipRole) return cellText(value);
        return {};
    }
    void append(const QJsonArray& values)
    {
        if (values.isEmpty()) return;
        const auto first = static_cast<int>(rows_.size());
        beginInsertRows({}, first, first + static_cast<int>(values.size()) - 1);
        for (const auto& v : values) rows_.append(v);
        endInsertRows();
    }
private:
    QJsonArray columns_, rows_;
};


SqlPane::SqlPane(Profile profile, std::shared_ptr<Session> session, QThreadPool& workers,
    QString draftFile, QWidget* parent)
    : QWidget(parent), session_(profile, std::move(session), workers, this), workers_(workers),
      draftFile_(std::move(draftFile))
{
    draftKey_ = QString::fromLatin1(QCryptographicHash::hash(
        (profile.name + "\n" + endpoint(profile) + "\n" + profile.database + "\n" + profile.user).toUtf8(),
        QCryptographicHash::Sha256).toHex());
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 6);
    layout->setSpacing(6);
    context_ = new QLabel;
    context_->setObjectName("sqlContext");
    context_->setTextFormat(Qt::PlainText);
    context_->setWordWrap(true);
    layout->addWidget(context_);
    const auto button = [this](QHBoxLayout* row, const QString& label, const char* id, auto fn) {
        auto* item = new QPushButton(label, this);
        item->setObjectName(id);
        row->addWidget(item);
        connect(item, &QPushButton::clicked, this, std::move(fn));
        return item;
    };

    auto* execution = new QHBoxLayout;
    layout->addLayout(execution);
    button(execution, tr("Run statement"), "runStatement", [this] { run(SqlScope::statement); });
    button(execution, tr("Run selection"), "runSelection", [this] { run(SqlScope::selection); });
    button(execution, tr("Run script"), "runScript", [this] { run(SqlScope::script); });
    button(execution, tr("Run next"), "runNext", [this] { run(SqlScope::statement, {}, true); });
    cancel_ = button(execution, tr("Cancel"), "cancelSql", [this] { session_.cancel(); });
    execution->addStretch();
    rowLimit_ = new QSpinBox;
    rowLimit_->setObjectName("sqlRowLimit");
    rowLimit_->setRange(1, 10000);
    rowLimit_->setValue(1000);
    rowLimit_->setSuffix(tr(" rows"));
    rowLimit_->setToolTip(tr("Maximum rows per statement"));
    execution->addWidget(rowLimit_);
    timeout_ = new QSpinBox;
    timeout_->setObjectName("sqlTimeout");
    timeout_->setRange(1, 120);
    timeout_->setValue(5);
    timeout_->setSuffix(tr(" s"));
    timeout_->setToolTip(tr("Query timeout"));
    execution->addWidget(timeout_);

    auto* transactions = new QHBoxLayout;
    layout->addLayout(transactions);
    autocommit_ = new QCheckBox(tr("Autocommit"));
    autocommit_->setObjectName("sqlAutocommit");
    transactions->addWidget(autocommit_);
    connect(autocommit_, &QCheckBox::toggled, this, [this](bool on) {
        action(on ? SqlTransaction::autocommitOn : SqlTransaction::autocommitOff);
    });
    button(transactions, tr("Begin"), "beginTransaction", [this] { action(SqlTransaction::begin); });
    button(transactions, tr("Commit"), "commitTransaction", [this] { action(SqlTransaction::commit); });
    button(transactions, tr("Rollback"), "rollbackTransaction", [this] { action(SqlTransaction::rollback); });
    const auto dropdown = [this, transactions](const QString& label) {
        auto* item = new QToolButton(this);
        item->setText(label);
        item->setPopupMode(QToolButton::InstantPopup);
        auto* operations = new QMenu(item);
        item->setMenu(operations);
        transactions->addWidget(item);
        return operations;
    };
    auto* savepoints = dropdown(tr("Savepoints"));
    savepoints->addAction(tr("Create"), this, [this] { action(SqlTransaction::savepoint, true); });
    savepoints->addAction(tr("Rollback to"), this, [this] { action(SqlTransaction::rollbackTo, true); });
    savepoints->addAction(tr("Release"), this, [this] { action(SqlTransaction::release, true); });
    auto* menu = dropdown(tr("Editor"));
    transactions->addStretch();

    auto* splitter = new QSplitter(Qt::Vertical);
    layout->addWidget(splitter, 1);
    auto* input = new QWidget;
    auto* inputs = new QVBoxLayout(input);
    inputs->setContentsMargins(0, 0, 0, 0);
    editor_ = new SqlEditor;
    editor_->setObjectName("sqlEditor");
    inputs->addWidget(editor_, 1);
    params_ = new QPlainTextEdit;
    params_->setObjectName("sqlParameters");
    params_->setMaximumHeight(94);
    params_->setPlaceholderText(tr(R"({"parameter_types": {}, "parameters": {}})"));
    params_->hide();
    inputs->addWidget(params_);
    splitter->addWidget(input);

    results_ = new QTabWidget;
    results_->setObjectName("sqlResults");
    details_ = new QPlainTextEdit;
    details_->setObjectName("sqlDetails");
    details_->setReadOnly(true);
    details_->setMaximumBlockCount(4000);
    results_->addTab(details_, tr("Messages and plans"));
    splitter->addWidget(results_);
    splitter->setSizes({420, 185});
    state_ = new QLabel(tr("Ready"));
    state_->setObjectName("sqlStatus");
    state_->setTextFormat(Qt::PlainText);
    state_->setWordWrap(true);
    layout->addWidget(state_);

    auto* actions = new QHBoxLayout;
    layout->addLayout(actions);
    restore_ = button(actions, tr("Restore SQL draft"), "restoreSqlDraft", [this] { restoreDraft(); });
    restore_->setVisible(draftEntries(draftFile_).contains(draftKey_));
    button(actions, tr("Check commit outcome"), "lookupSqlOutcome", [this] { showOutcome(); });
    actions->addStretch();

    const auto edit = [this, menu](const QString& label, const QKeySequence& key, auto fn) {
        auto* command = menu->addAction(label, this, std::move(fn));
        if (key.isEmpty()) return;
        addAction(command);
        command->setShortcut(key);
        command->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    };
    edit(tr("Open SQL file…"), QKeySequence::Open, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("Open SQL file"), {}, tr("SQL files (*.sql);;All files (*)"));
        if (path.isEmpty()) return;
        const auto error = editor_->open_file(path);
        if (error.isEmpty()) file_ = path;
        else state_->setText(error);
    });
    edit(tr("Save SQL file…"), QKeySequence::Save, [this] {
        auto path = file_;
        if (path.isEmpty()) path = QFileDialog::getSaveFileName(this, tr("Save SQL file"), {}, tr("SQL files (*.sql)"));
        if (path.isEmpty()) return;
        const auto error = editor_->save_file(path);
        if (error.isEmpty()) file_ = path;
        else state_->setText(error);
    });
    edit(tr("Find and replace…"), QKeySequence::Find, [this] { editor_->show_find_replace(); });
    edit(tr("Format statement"), {}, [this] { editor_->format_statement(); });
    edit(tr("Fold or unfold block"), {}, [this] { editor_->toggle_fold(); });
    edit(tr("Toggle bookmark"), {}, [this] { editor_->toggle_bookmark(); });
    edit(tr("Next bookmark"), {}, [this] { editor_->next_bookmark(); });
    auto* snippets = menu->addMenu(tr("Insert snippet"));
    for (const auto& name : {QString("select"), QString("insert"), QString("procedure")})
        snippets->addAction(name, this, [this, name] { editor_->insert_snippet(name); });
    edit(tr("Show bound parameters"), {}, [this] { params_->setVisible(params_->isHidden()); });
    edit(tr("Run statement"), QKeySequence(Qt::CTRL | Qt::Key_Return), [this] { run(SqlScope::statement); });
    edit(tr("Explain statement"), {}, [this] { run(SqlScope::statement, "EXPLAIN "); });
    edit(tr("Analyze SELECT"), {}, [this] { run(SqlScope::statement, "EXPLAIN ANALYZE "); });
    edit(tr("New PL/SQL routine…"), {}, [this] { openRoutineStudio(); });
    edit(tr("Routine debugger…"), {}, [this] { showDebug(); });
    edit(tr("SDK examples…"), {}, [this] { showSdkExamples(this); });
    edit(tr("History Studio…"), {}, [this] { openHistoryStudio(); });
    edit(tr("Decision Studio…"), {}, [this] { openDecisionStudio(); });
    edit(tr("ML Studio…"), {}, [this] { openMlStudio(); });
    edit(tr("Dataset Studio…"), {}, [this] { openDatasetStudio(); });
    edit(tr("Job Center…"), {}, [this] { openJobCenter(); });
    edit(tr("Instance and Health Center…"), {}, [this] { openAdminCenter(); });
    edit(tr("Backup and schedules…"), {}, [this] { openBackupCenter(); });
    edit(tr("Users and permissions…"), {}, [this] { openSecurityCenter(); });
    edit(tr("Open SQL project folder…"), {}, [this] { openScriptProject(); });
    edit(tr("Refresh object explorer"), {}, [this] { refreshObjects(); });
    edit(tr("Create table…"), {}, [this] { designTable(); });
    edit(tr("Schema diagram and dictionary…"), {}, [this] { showDictionary(); });
    edit(tr("Peek object at cursor"), QKeySequence(Qt::Key_F12), [this] {
        const auto word = editor_->word_at_cursor();
        QJsonArray matches;
        for (const auto& entry : catalog_)
        {
            const auto path = entry.toObject().value("qualified_name").toArray();
            if (!path.isEmpty() && path.last().toString().compare(word, Qt::CaseInsensitive) == 0)
                matches.append(entry);
        }
        if (matches.size() == 1) peek(matches.first().toObject());
        else state_->setText(matches.isEmpty() ? tr("No visible object matches the cursor.")
            : tr("Several objects match. Choose one in Object Explorer."));
    });

    draftTimer_ = new QTimer(this);
    draftTimer_->setSingleShot(true);
    draftTimer_->setInterval(800);
    connect(draftTimer_, &QTimer::timeout, this, [this] { saveDraft(); });
    connect(editor_, &QPlainTextEdit::textChanged, this, [this] {
        draftDirty_ = true;
        if (!draftFile_.isEmpty()) draftTimer_->start();
    });
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] { refreshContext(); });
    timer->start(1000);
    session_.setStateChanged([this] { refreshContext(); });
    refreshContext();
}


SqlPane::~SqlPane()
{
    session_.setStateChanged({});
    saveDraft();
}

bool SqlPane::allowClose()
{
    if (running_)
    {
        state_->setText(tr("Cancel the active query and wait for it to finish before closing."));
        return false;
    }
    if (!session_.state().transactionActive) return true;
    QMessageBox message(QMessageBox::Warning, tr("Open transaction"),
        tr("Closing this session rolls back its pending transaction."), QMessageBox::Cancel, this);
    auto* rollback = message.addButton(tr("Rollback and close"), QMessageBox::DestructiveRole);
    message.exec();
    return message.clickedButton() == rollback;
}

void SqlPane::refreshContext()
{
    const auto& current = session_.state();
    const auto& connection = profile();
    const auto id = current.identity.value("session_id").toString();
    const auto elapsed = current.transactionActive
        ? tr("Open %1 s · %2").arg(current.transactionStarted.secsTo(QDateTime::currentDateTimeUtc()))
            .arg(current.pendingChanges ? tr("Pending changes") : tr("No pending writes"))
        : tr("No open transaction");
    context_->setText(tr("%1 · %2 · Database %3 · Schema %4 · Session %5 · %6").arg(
        endpoint(connection), connection.environment.toUpper(), QStringLiteral("main"),
        QStringLiteral("public"), id.isEmpty() ? tr("Unavailable") : id, elapsed));
    const QSignalBlocker signal(autocommit_);
    autocommit_->setChecked(current.autocommit);
    autocommit_->setEnabled(!session_.busy() && current.connected && !current.transactionActive);
    cancel_->setEnabled(running_);
    const bool busy = running_ || session_.busy();
    setProperty("sqlBusy", busy);
    if (parentWidget()) parentWidget()->setProperty("sqlBusy", busy);
    for (const auto& id : {"runStatement", "runSelection", "runScript", "runNext"})
        findChild<QPushButton*>(id)->setEnabled(!busy && current.connected);
}

void SqlPane::run(SqlScope scope, const QString& prefix, bool advance)
{
    if (session_.busy()) { state_->setText(tr("This session is busy.")); return; }
    const auto batch = editor_->commands(scope);
    if (batch.incomplete) { state_->setText(tr("Complete the SQL or PL/SQL block before running it.")); return; }
    if (batch.statements.isEmpty()) { state_->setText(tr("No SQL statement at this position.")); return; }
    QVector<SqlStatement> commands;
    bool writing = false;
    for (const auto& item : batch.statements)
    {
        const auto command = prefix + item.text;
        if (prefix == "EXPLAIN ANALYZE " && !isRead(item.text))
        {
            state_->setText(tr("Choose a SELECT query for EXPLAIN ANALYZE."));
            return;
        }
        writing |= statementMayWrite(command);
        commands.push_back({command, 1 + static_cast<int>(editor_->sql_text().left(item.offset).count('\n'))});
    }
    if (writing && profile().read_only)
    {
        state_->setText(tr("The profile prevents writes in this worksheet."));
        return;
    }
    if (writing && profile().environment == "prod" &&
        QMessageBox::question(this, tr("Production SQL"),
            tr("This may change data or schema on %1. Run it?").arg(endpoint(profile())),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;

    QJsonObject types, values;
    const auto input = params_->toPlainText().trimmed();
    if (!input.isEmpty())
    {
        QJsonParseError error;
        const auto bound = QJsonDocument::fromJson(input.toUtf8(), &error);
        const auto object = bound.object();
        if (error.error != QJsonParseError::NoError || !bound.isObject() ||
            !object.value("parameter_types").isObject() || !object.value("parameters").isObject() ||
            object.size() != 2)
        {
            state_->setText(tr("Provide parameter_types and parameters as JSON objects."));
            params_->show();
            return;
        }
        types = object.value("parameter_types").toObject();
        values = object.value("parameters").toObject();
    }

    while (results_->count() > 1) delete results_->widget(results_->count() - 1);
    tables_.clear();
    statementIndex_ = -1;
    ++definitionRequest_;
    details_->clear();
    running_ = true;
    const bool accepted = session_.execute(std::move(commands), std::move(types), std::move(values),
        {rowLimit_->value(), timeout_->value() * 1000},
        [this](const SqlEvent& event) { onSqlEvent(event); },
        [this, list = batch.statements, advance, writing](const SqlReply& reply) {
            running_ = false;
            refreshContext();
            displayReply(reply, list);
            if (reply.error.code != SDP_OK) return;
            if (advance)
            {
                auto cursor = editor_->textCursor();
                cursor.setPosition(static_cast<int>(list.front().offset + list.front().text.size()));
                editor_->setTextCursor(cursor);
            }
            if (writing) refreshObjects();
        });
    if (accepted) state_->setText(tr("Running…"));
    else
    {
        running_ = false;
        state_->setText(tr("No connected SQL session is available."));
    }
    refreshContext();
}

void SqlPane::onSqlEvent(const SqlEvent& event)
{
    if (statementIndex_ != event.statementIndex)
    {
        tables_.clear();
        statementIndex_ = event.statementIndex;
    }
    if (event.kind == SqlEvent::Kind::statement)
    {
        if (event.data.contains("affected_rows"))
            details_->appendPlainText(tr("Statement %1 · %2 affected rows").arg(statementIndex_ + 1)
                .arg(event.data.value("affected_rows").toInteger()));
        return;
    }
    const auto batch = event.data.contains("batch") ? event.data.value("batch").toObject() : event.data;
    const auto index = batch.value("result_set").toInt();
    auto* model = tables_.value(index);
    if (!model)
    {
        if (results_->count() >= 129) return;
        auto* view = new QTableView;
        view->setAlternatingRowColors(true);
        view->setEditTriggers(QAbstractItemView::NoEditTriggers);
        view->horizontalHeader()->setDefaultSectionSize(148);
        model = new SqlTable(batch.value("columns").toArray(), view);
        view->setModel(model);
        tables_.insert(index, model);
        auto label = batch.value("set_name").toString();
        if (label.isEmpty()) label = tr("Result %1.%2").arg(statementIndex_ + 1).arg(index + 1);
        results_->setCurrentIndex(results_->addTab(view, label));
    }
    model->append(batch.value("rows").toArray());
    for (const auto& row : batch.value("rows").toArray())
    {
        const auto cells = row.toArray();
        if (cells.size() == 1 && cells.at(0).toObject().value("t") == "text" &&
            !batch.value("columns").toArray().isEmpty() && batch.value("columns").toArray().at(0).toObject().value("name") == "plan")
            details_->appendPlainText(cellText(cells.at(0)));
    }
}

void SqlPane::displayReply(const SqlReply& reply, const QList<SqlCommand>& commands)
{
    if (reply.error.code == SDP_OK)
    {
        state_->setText(reply.resultsTruncated ? tr("Completed with result limit (%1 ms)").arg(reply.elapsedMs)
            : tr("Completed (%1 ms)").arg(reply.elapsedMs));
        return;
    }
    if (reply.error.unknown_outcome)
    {
        uncertainToken_ = reply.requestToken;
        state_->setText(tr("Commit outcome unknown. Check the request token before repeating any write. %1")
            .arg(uncertainToken_));
    }
    else
    {
        state_->setText(tr("%1 · %2").arg(status(reply.error.code), QString::fromUtf8(reply.error.message)));
    }
    if (reply.error.line && reply.statementIndex >= 0 && reply.statementIndex < commands.size())
        editor_->goto_error(commands.at(reply.statementIndex).offset, reply.error.line, reply.error.column);
    details_->appendPlainText(state_->text());
}

void SqlPane::action(SqlTransaction control, bool named)
{
    QString name;
    if (named)
    {
        bool ok = false;
        name = QInputDialog::getText(this, tr("Savepoint"), tr("Name"), QLineEdit::Normal, {}, &ok).trimmed();
        if (!ok || name.isEmpty()) return;
    }
    if (!session_.transaction(control, name, [this, control](const SqlReply& reply) {
        displayReply(reply, {});
        if (reply.error.code == SDP_OK && (control == SqlTransaction::commit || control == SqlTransaction::rollback))
            refreshObjects();
    })) state_->setText(tr("The session cannot change transaction state right now."));
    refreshContext();
}


void SqlPane::health(SqlSession::Finished done)
{
    if (session_.busy())
    {
        QTimer::singleShot(60, this, [this, done = std::move(done)]() mutable { health(std::move(done)); });
        return;
    }
    if (!session_.health(done))
    {
        SqlReply result;
        result.error.code = SDP_CONNECTION_LOST;
        done(result);
    }
}

void SqlPane::setMetadataHandler(std::function<void(const QJsonArray&)> handler)
{
    metadataHandler_ = std::move(handler);
}

void SqlPane::refreshObjects()
{
    if (!session_.capabilities().value("sql_crud_v1").toBool()) return;
    session_.refreshMetadata([this](const SqlReply& reply) {
        if (reply.error.code != SDP_OK) return;
        const auto objects = reply.data.value("objects").toArray();
        catalog_ = objects;
        QStringList names;
        for (const auto& obj : objects)
        {
            for (const auto& component : obj.toObject().value("qualified_name").toArray())
                if (component.isString()) names.append(component.toString());
            for (const auto& column : obj.toObject().value("columns").toArray())
                names.append(column.toObject().value("name").toString());
        }
        names.removeAll(QString{});
        names.removeDuplicates();
        editor_->set_catalog_words(names);
        if (metadataHandler_) metadataHandler_(objects);
    });
}

void SqlPane::peek(const QJsonObject& item)
{
    if (item.isEmpty()) return;
    details_->setPlainText(pretty(item));
    const auto revision = ++definitionRequest_;
    inspectDefinition(session_, item, [this, revision](const QString& text) {
        if (definitionRequest_ == revision) details_->setPlainText(text);
    });
    results_->setCurrentWidget(details_);
}

void SqlPane::designTable(const QJsonObject& table)
{
    if (session_.busy() || !session_.state().connected)
    {
        state_->setText(tr("Wait for an idle, connected session."));
        return;
    }
    showTableDesigner(session_, catalog_, table, this, [this] { refreshObjects(); });
}

void SqlPane::showDictionary()
{
    showSchemaDictionary(session_, catalog_, this, [this](const QJsonObject& table) { designTable(table); });
}

void SqlPane::showGrid(const QJsonObject& table)
{
    openDataGrid(session_, table, this);
}

void SqlPane::transferTable(const QJsonObject& table)
{
    showTableTransfer(session_, table, this, [this](const QString& script) {
        if (!editor_->sql_text().isEmpty() && QMessageBox::question(this, tr("Open SQL import"),
            tr("Replace the current worksheet with the import script?")) != QMessageBox::Yes) return;
        editor_->setPlainText(script);
        editor_->setFocus();
    }, [this] { refreshObjects(); });
}

void SqlPane::saveDraft()
{
    if (!draftDirty_ || draftFile_.isEmpty()) return;
    auto entries = draftEntries(draftFile_);
    const auto query = editor_->sql_text();
    if (canRecover(query)) entries.insert(draftKey_, query);
    else entries.remove(draftKey_);
    auto data = QJsonDocument(entries).toJson(QJsonDocument::Compact);
    if (data.size() > workspaceLimit) return;
    if (!QDir().mkpath(QFileInfo(draftFile_).absolutePath())) return;
    QSaveFile file(draftFile_);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) return;
    draftDirty_ = false;
    restore_->setVisible(entries.contains(draftKey_));
}

void SqlPane::restoreDraft()
{
    const auto query = draftEntries(draftFile_).value(draftKey_).toString();
    if (!canRecover(query)) return;
    if (!editor_->sql_text().isEmpty() && editor_->sql_text() != query &&
        QMessageBox::question(this, tr("Restore SQL draft"), tr("Replace this worksheet with the saved draft?"))
            != QMessageBox::Yes) return;
    const QSignalBlocker suspended(editor_);
    editor_->setPlainText(query);
    state_->setText(tr("SQL draft restored"));
}

void SqlPane::showOutcome()
{
    const auto current = session_.state().unknownRequestToken;
    bool ok = false;
    const auto token = QInputDialog::getText(this, tr("Check commit outcome"), tr("Request token"),
        QLineEdit::Normal, current.isEmpty() ? uncertainToken_ : current, &ok).trimmed();
    if (!ok || token.isEmpty()) return;
    const auto lookup = [this, token] {
        if (!session_.lookupOutcome(token, [this](const SqlReply& result) {
            if (result.error.code != SDP_OK) { displayReply(result, {}); return; }
            state_->setText(tr("Commit outcome · %1").arg(result.data.value("status").toString()));
            details_->setPlainText(pretty(result.data));
            results_->setCurrentWidget(details_);
        })) state_->setText(tr("The current session cannot check the outcome."));
    };
    if (session_.state().connected) { lookup(); return; }
    auto password = std::make_shared<Password>();
    password->bytes = QInputDialog::getText(this, tr("Reconnect to check outcome"),
        tr("Password for %1").arg(endpoint(profile())), QLineEdit::Password, {}, &ok).toUtf8();
    if (!ok) return;
    QPointer<SqlPane> pane(this);
    workers_.start([profile = profile(), pane, password, lookup] {
        const auto result = wb::connect(profile, *password, true, false);
        password->bytes.fill('\0');
        QMetaObject::invokeMethod(QCoreApplication::instance(), [pane, result, lookup] {
            if (!pane) return;
            if (!result.session) { pane->state_->setText(result.error); return; }
            pane->session_.reconnect(result.session, [pane, lookup](const SqlReply& reply) {
                if (!pane) return;
                if (reply.error.code == SDP_OK) lookup();
                else pane->displayReply(reply, {});
            });
        }, Qt::QueuedConnection);
    });
}


void SqlPane::openRoutineStudio(const QJsonObject& routine)
{
    QPointer<SqlPane> pane(this);
    showRoutineStudio(session_, catalog_, routine, this,
        [pane] { if (pane) pane->refreshObjects(); },
        [pane](const QJsonObject& selected) {
            if (pane) showRoutineDebugger(pane->session_, pane->workers_, selected, pane);
        });
}

void SqlPane::exportRoutine(const QJsonObject& routine)
{
    if (!session_.state().connected || !session_.capabilities().value("routine_export_v1").toBool())
    {
        state_->setText(tr("The connected Server does not expose routine export."));
        return;
    }
    if (session_.busy() || session_.state().transactionActive)
    {
        state_->setText(tr("Finish the current worksheet operation or transaction before inspecting an export."));
        return;
    }
    showRoutineExport(profile(), routine, this);
}

void SqlPane::openHistoryStudio(const QJsonObject& object)
{
    const QPointer<SqlPane> pane(this);
    showHistoryStudio(session_, catalog_, object, this,
        [pane] { if (pane) pane->refreshObjects(); });
}

void SqlPane::openDecisionStudio(const QJsonObject& object)
{
    const QPointer<SqlPane> pane(this);
    showDecisionStudio(session_, catalog_, object, this,
        [pane] { if (pane) pane->refreshObjects(); });
}

void SqlPane::openMlStudio(const QJsonObject& model)
{
    const QPointer<SqlPane> pane(this);
    showMlStudio(session_, catalog_, model, this,
        [pane] { if (pane) pane->refreshObjects(); },
        [pane](const QString& jobId, const QString& name) { if (pane) pane->openJobCenter(jobId, name); },
        [pane](const QJsonObject& routine) { if (pane) pane->openRoutineStudio(routine); },
        [pane](const QJsonObject& routine) { if (pane) pane->exportRoutine(routine); });
}

void SqlPane::openDatasetStudio(const QJsonObject& table)
{
    const auto relation = transfer::relation(table);
    showDatasetStudio(session_, this, {}, relation.isEmpty() ? QString{} : QStringLiteral("SELECT * FROM ") + relation);
}

void SqlPane::openJobCenter(const QString& jobId, const QString& model)
{
    showJobCenter(session_, this, draftFile_.isEmpty() ? QString{} : draftFile_ + ".ml-jobs.json", jobId, model);
}

void SqlPane::openAdminCenter()
{
    const QPointer<SqlPane> pane(this);
    showAdminCenter(session_, this, [pane](const QString& id) { if (pane) pane->openJobCenter(id); });
}

void SqlPane::openBackupCenter()
{
    const QPointer<SqlPane> pane(this);
    showBackupCenter(session_, this, [pane](const QString& id) { if (pane) pane->openJobCenter(id); });
}

void SqlPane::openSecurityCenter()
{
    const QPointer<SqlPane> pane(this);
    showSecurityCenter(session_, catalog_, this, [pane] { if (pane) pane->refreshObjects(); });
}

void SqlPane::openScriptProject()
{
    const auto root = QFileDialog::getExistingDirectory(this, tr("Select a local SQL project folder"));
    if (root.isEmpty()) return;
    const QDir dir(root);
    const auto files = dir.entryInfoList({"*.sql"}, QDir::Files | QDir::Readable | QDir::NoSymLinks, QDir::Name);
    if (files.isEmpty())
    {
        state_->setText(tr("The selected folder contains no readable SQL files."));
        return;
    }
    QDialog project(this);
    project.setWindowTitle(tr("SQL project · %1").arg(dir.dirName()));
    project.resize(490, 400);
    auto* layout = new QVBoxLayout(&project);
    auto* list = new QListWidget;
    layout->addWidget(list, 1);
    for (const auto& file : files)
    {
        auto* item = new QListWidgetItem(file.fileName(), list);
        item->setData(Qt::UserRole, file.absoluteFilePath());
    }
    auto* open = new QPushButton(tr("Open in worksheet"));
    layout->addWidget(open);
    connect(open, &QPushButton::clicked, &project, [&] { if (list->currentItem()) project.accept(); });
    connect(list, &QListWidget::itemDoubleClicked, &project, &QDialog::accept);
    list->setCurrentRow(0);
    if (project.exec() != QDialog::Accepted) return;
    const auto path = list->currentItem()->data(Qt::UserRole).toString();
    const auto error = editor_->open_file(path);
    if (error.isEmpty()) file_ = path;
    else state_->setText(error);
}

void SqlPane::showDebug()
{
    QStringList names;
    QList<QJsonObject> routines;
    for (const auto& value : catalog_)
    {
        const auto object = value.toObject();
        if (object.value("kind").toInt() != 6) continue;
        QStringList path;
        for (const auto& part : object.value("qualified_name").toArray()) path.append(part.toString());
        names.append(path.join('.') + tr(" · revision %1").arg(object.value("schema_version").toInt()));
        routines.append(object);
    }
    if (names.isEmpty())
    {
        state_->setText(tr("Compile a routine and refresh Object Explorer first."));
        return;
    }
    bool accepted = false;
    const auto name = QInputDialog::getItem(this, tr("Attach routine debugger"),
        tr("Compiled routine"), names, 0, false, &accepted);
    if (accepted) showRoutineDebugger(session_, workers_, routines.at(names.indexOf(name)), this);
}
}
