#include "decision_studio.hpp"

#include "sql_script.hpp"
#include "sql_session.hpp"
#include "table_codec.hpp"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <optional>

namespace wb
{
namespace
{
constexpr qsizetype inputLimit = 128 * 1024;
constexpr qsizetype sqlLimit = 16 * 1024;

QString formatted(const QJsonObject& value)
{
    return QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Indented));
}

QString stableNumber(const QString& text)
{
    bool ok{};
    const auto number = text.toULongLong(&ok);
    return ok && number && QString::number(number) == text ? text : QString{};
}

QString objectId(const QJsonValue& value)
{
    if (value.isString()) return stableNumber(value.toString());
    const auto number = value.toInteger();
    return number > 0 ? QString::number(number) : QString{};
}

QString displayName(const QJsonObject& object)
{
    QStringList parts;
    for (const auto& part : object.value("qualified_name").toArray()) parts.append(part.toString());
    return parts.join(u'.');
}

std::optional<QJsonDocument> readInput(const QString& text)
{
    const auto bytes = text.toUtf8();
    if (bytes.size() > inputLimit) return {};
    QJsonParseError error;
    const auto result = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || result.isNull()) return {};
    return result;
}

QString describe(const SqlReply& reply)
{
    switch (reply.error.code)
    {
    case SDP_PERMISSION_DENIED: return QObject::tr("The server denied access to the target, rule or a dependency.");
    case SDP_FEATURE_UNSUPPORTED: return QObject::tr("The server does not support this Decision operation.");
    case SDP_MODULE_UNAVAILABLE: return QObject::tr("The Decision module or the requested snapshot is unavailable.");
    case SDP_INCOMPATIBLE_PROTOCOL: return QObject::tr("The server uses an incompatible Decision protocol.");
    case SDP_INVALID_STATE: return QObject::tr("The requested row, version or session state is no longer available.");
    default: break;
    }
    const auto detail = QString::fromUtf8(reply.error.message);
    auto message = detail.isEmpty() ? QObject::tr("Decision request failed (code %1).").arg(reply.error.code) : detail;
    if (reply.error.line)
        message += QObject::tr(" Line %1, column %2.").arg(reply.error.line).arg(reply.error.column);
    return message;
}

QJsonObject findObject(const QJsonArray& objects, const QString& id)
{
    for (const auto& item : objects)
    {
        const auto object = item.toObject();
        if (object.value("id").toString() == id) return object;
    }
    return {};
}

// Validate the editable statement's rule and target names against the visible form.
// Conditions and expressions are validated by the server, including nested SQL.
bool sameRuleHeader(const QString& sql, const QString& ruleName, const QJsonArray& target)
{
    auto state = SqlLexState::normal;
    QList<SqlToken> tokens;
    for (const auto& token : sql_tokens(sql, state))
        if (token.kind != SqlTokenKind::whitespace && token.kind != SqlTokenKind::comment) tokens.append(token);
    if (state != SqlLexState::normal || tokens.size() < 8) return false;
    qsizetype at{};
    const auto text = [&](const SqlToken& token) { return QStringView(sql).mid(token.offset, token.length); };
    const auto keyword = [&](QStringView value) {
        return at < tokens.size() && tokens[at].kind == SqlTokenKind::word &&
            text(tokens[at++]).compare(value, Qt::CaseInsensitive) == 0;
    };
    const auto identifier = [&]() {
        if (at >= tokens.size()) return QString{};
        const auto token = tokens[at++];
        if (token.kind == SqlTokenKind::word) return text(token).toString().toLower();
        if (token.kind != SqlTokenKind::quoted_name || token.length < 3) return QString{};
        return text(token).mid(1, token.length - 2).toString().replace(QStringLiteral("\"\""), QStringLiteral("\""));
    };
    if (!keyword(u"create") || !keyword(u"decision") || !keyword(u"rule") || identifier() != ruleName || !keyword(u"on"))
        return false;

    QStringList relation{identifier()};
    while (at < tokens.size() && text(tokens.at(at)) == u".")
    {
        ++at;
        relation.append(identifier());
    }
    if (!keyword(u"for") || relation.isEmpty() || relation.size() > 3 || target.size() != 3) return false;
    if (relation.size() == 1) relation.prepend(QStringLiteral("public"));
    if (relation.size() == 2) relation.prepend(QStringLiteral("main"));
    for (qsizetype i = 0; i < target.size(); ++i)
        if (relation.at(i) != target.at(i).toString()) return false;
    return true;
}

class Studio final : public QDialog
{
public:
    Studio(SqlSession& session, const QJsonArray& catalog, const QJsonObject& selected, QWidget* parent,
        std::function<void()> refresh)
        : QDialog(parent), session_(&session), catalog_(catalog), refresh_(std::move(refresh))
    {
        setObjectName("decisionStudio");
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowModality(Qt::WindowModal);
        setWindowTitle(tr("Decision Studio"));
        resize(960, 760);
        auto* root = new QVBoxLayout(this);
        auto* selector = new QHBoxLayout;
        root->addLayout(selector);
        selector->addWidget(label(tr("Target table")));
        target_ = new QComboBox;
        target_->setObjectName("decisionTarget");
        selector->addWidget(target_, 1);
        selector->addWidget(label(tr("Rule name")));
        ruleName_ = field("decisionRuleName");
        selector->addWidget(ruleName_, 1);

        tabs_ = new QTabWidget;
        root->addWidget(tabs_, 1);
        buildSimulation();
        buildAuthoring();
        buildCompare();
        buildEvidence();
        policy_ = label({});
        root->addWidget(policy_);
        status_ = label({});
        status_->setObjectName("decisionStatus");
        root->addWidget(status_);

        const auto selectedId = selected.value("id").toString();
        loadCatalog(selected.value("kind").toInt() == 3 ? selectedId : QString{}, selectedId);
        connect(target_, &QComboBox::currentIndexChanged, this, [this] {
            validated_.clear();
            updateColumns();
            sync();
        });
        connect(ruleName_, &QLineEdit::textChanged, this, [this] { validated_.clear(); sync(); });
        connect(source_, &QPlainTextEdit::textChanged, this, [this] { validated_.clear(); sync(); });
        connect(event_, &QComboBox::currentIndexChanged, this, [this] { sync(); });
        connect(effect_, &QComboBox::currentIndexChanged, this, [this] { sync(); });
        connect(savedVersion_, &QComboBox::currentIndexChanged, this, [this] { renderVersion(); });
        connect(savedRule_, &QComboBox::currentIndexChanged, this, [this] {
            if (savedRule_->currentData().toString() != loadedRuleId_) versionDetails_->clear();
            else renderVersion();
            sync();
        });
        updateColumns();
        sync();
        if (selected.value("kind").toInt() == 8 && savedRule_->currentData().toString() == selectedId)
        {
            tabs_->setCurrentWidget(author_);
            loadRule();
        }
        else if (supported()) status_->setText(tr("Choose a target table and a typed example, or author a rule."));
        else status_->setText(tr("The connected server has no Decision inspection module."));
    }

    void reject() override
    {
        if (busy_)
        {
            status_->setText(tr("Wait for the active Decision request to finish before closing."));
            return;
        }
        QDialog::reject();
    }

private:
    QLabel* label(const QString& caption)
    {
        auto* result = new QLabel(caption);
        result->setTextFormat(Qt::PlainText);
        result->setWordWrap(true);
        return result;
    }

    QLineEdit* field(const char* name)
    {
        auto* result = new QLineEdit;
        result->setObjectName(name);
        return result;
    }

    QPlainTextEdit* editor(const char* name, bool readOnly = false)
    {
        auto* result = new QPlainTextEdit;
        result->setObjectName(name);
        result->setReadOnly(readOnly);
        return result;
    }

    QPushButton* button(QHBoxLayout& box, const char* name, const QString& title, auto action)
    {
        auto* result = new QPushButton(title);
        result->setObjectName(name);
        box.addWidget(result);
        connect(result, &QPushButton::clicked, this, std::move(action));
        return result;
    }

    void buildSimulation()
    {
        auto* pane = new QWidget;
        auto* layout = new QVBoxLayout(pane);
        auto* form = new QFormLayout;
        layout->addLayout(form);
        event_ = new QComboBox;
        event_->setObjectName("decisionEvent");
        event_->addItem(tr("Insert"), "insert");
        event_->addItem(tr("Update"), "update");
        event_->addItem(tr("Delete"), "delete");
        form->addRow(tr("Proposed write"), event_);
        row_ = field("decisionRowId");
        row_->setPlaceholderText(tr("Stable row ID from History"));
        form->addRow(tr("Row ID"), row_);
        revision_ = field("decisionRevision");
        revision_->setPlaceholderText(tr("Current data if blank"));
        form->addRow(tr("Snapshot revision"), revision_);
        proposed_ = editor("decisionProposed");
        proposed_->setPlainText("{}");
        proposed_->setPlaceholderText(tr("JSON object keyed by column name, with typed values"));
        form->addRow(tr("Proposed column values"), proposed_);
        layout->addWidget(label(tr("Simulations evaluate Decision rules and return their reasoning. Applying a row change still requires the normal write procedure.")));
        auto* actions = new QHBoxLayout;
        layout->addLayout(actions);
        for (const auto& item : {Action{SDP_DECISION_EXPLAIN, "decisionExplain", tr("Explain")},
            Action{SDP_DECISION_WHYNOT, "decisionWhynot", tr("Why not?")},
            Action{SDP_DECISION_WHATIF, "decisionWhatif", tr("What if?")},
            Action{SDP_DECISION_RECHECK, "decisionRecheck", tr("Recheck")}})
            inspect_.append(button(*actions, item.name, item.title, [this, operation = item.operation] { simulate(operation); }));
        layout->addStretch();
        tabs_->addTab(pane, tr("Simulation"));
    }

    void buildAuthoring()
    {
        author_ = new QWidget;
        auto* layout = new QVBoxLayout(author_);
        auto* saved = new QHBoxLayout;
        layout->addLayout(saved);
        savedRule_ = new QComboBox;
        savedRule_->setObjectName("decisionStoredRule");
        saved->addWidget(savedRule_, 2);
        load_ = button(*saved, "decisionLoad", tr("Load saved rule"), [this] { loadRule(); });
        savedVersion_ = new QComboBox;
        savedVersion_->setObjectName("decisionStoredVersion");
        saved->addWidget(savedVersion_, 1);
        useVersion_ = button(*saved, "decisionUseVersion", tr("Edit this version"), [this] { editVersion(); });
        versionDetails_ = editor("decisionDependencies", true);
        versionDetails_->setMaximumHeight(118);
        layout->addWidget(versionDetails_);
        auto* form = new QFormLayout;
        layout->addLayout(form);
        ruleEvent_ = new QComboBox;
        ruleEvent_->setObjectName("decisionRuleEvent");
        ruleEvent_->addItems({"INSERT", "UPDATE", "DELETE", "WRITE"});
        form->addRow(tr("Rule event"), ruleEvent_);
        priority_ = new QSpinBox;
        priority_->setRange(0, 2'147'483'647);
        priority_->setValue(10);
        form->addRow(tr("Priority"), priority_);
        effect_ = new QComboBox;
        effect_->setObjectName("decisionEffect");
        effect_->addItems({"ALLOW", "DENY", "SET"});
        form->addRow(tr("Rule effect"), effect_);
        column_ = new QComboBox;
        column_->setObjectName("decisionColumn");
        form->addRow(tr("Computed column"), column_);
        condition_ = field("decisionCondition");
        condition_->setText("TRUE");
        form->addRow(tr("SQL condition"), condition_);
        calculation_ = field("decisionCalculation");
        form->addRow(tr("SQL calculation"), calculation_);
        auto* sourceActions = new QHBoxLayout;
        layout->addLayout(sourceActions);
        template_ = button(*sourceActions, "decisionTemplate", tr("Build SQL"), [this] { buildSource(); });
        validate_ = button(*sourceActions, "decisionValidate", tr("Validate SQL"), [this] { validateSource(); });
        save_ = button(*sourceActions, "decisionSave", tr("Save immutable version"), [this] { saveSource(); });
        button(*sourceActions, "decisionCopySql", tr("Copy SQL"), [this] {
            QApplication::clipboard()->setText(source_->toPlainText());
        });
        source_ = editor("decisionSource");
        source_->setPlaceholderText(tr("CREATE DECISION RULE …"));
        layout->addWidget(source_, 1);
        auto* activation = new QHBoxLayout;
        layout->addLayout(activation);
        activation->addWidget(label(tr("Existing version")));
        activateVersion_ = new QSpinBox;
        activateVersion_->setRange(1, 16);
        activateVersion_->setObjectName("decisionActivationVersion");
        activation->addWidget(activateVersion_);
        activate_ = button(*activation, "decisionActivate", tr("Validate and activate…"), [this] { activateVersion(); });
        activation->addStretch();
        tabs_->addTab(author_, tr("Rules and versions"));
    }

    void buildCompare()
    {
        auto* pane = new QWidget;
        auto* layout = new QVBoxLayout(pane);
        layout->addWidget(label(tr("Compare two immutable versions of the selected rule against one server snapshot. Use up to 16 objects with event, row_id when needed, and typed proposed values.")));
        auto* form = new QFormLayout;
        layout->addLayout(form);
        prior_ = new QSpinBox;
        prior_->setRange(1, 16);
        prior_->setObjectName("decisionFromVersion");
        form->addRow(tr("Earlier version"), prior_);
        candidate_ = new QSpinBox;
        candidate_->setRange(1, 16);
        candidate_->setValue(2);
        candidate_->setObjectName("decisionToVersion");
        form->addRow(tr("Candidate version"), candidate_);
        compareRevision_ = field("decisionCompareRevision");
        compareRevision_->setPlaceholderText(tr("Current data if blank"));
        form->addRow(tr("Snapshot revision"), compareRevision_);
        cases_ = editor("decisionCases");
        cases_->setPlainText("[]");
        layout->addWidget(cases_, 1);
        auto* actions = new QHBoxLayout;
        layout->addLayout(actions);
        compare_ = button(*actions, "decisionCompare", tr("Compare impact"), [this] { compare(); });
        actions->addStretch();
        tabs_->addTab(pane, tr("Version comparison"));
    }

    void buildEvidence()
    {
        evidence_ = new QWidget;
        auto* layout = new QVBoxLayout(evidence_);
        auto* evidenceTabs = new QTabWidget;
        layout->addWidget(evidenceTabs);
        tree_ = new QTreeWidget;
        tree_->setObjectName("decisionConditionTree");
        tree_->setHeaderLabels({tr("Rule or condition"), tr("State"), tr("Outcome"), tr("Elapsed (ns)")});
        evidenceTabs->addTab(tree_, tr("Condition tree"));
        result_ = editor("decisionResult", true);
        evidenceTabs->addTab(result_, tr("Server result"));
        auto* call = new QWidget;
        auto* callBox = new QVBoxLayout(call);
        call_ = editor("decisionRequest", true);
        callBox->addWidget(call_);
        auto* actions = new QHBoxLayout;
        callBox->addLayout(actions);
        button(*actions, "decisionCopyCall", tr("Copy request"), [this] {
            QApplication::clipboard()->setText(call_->toPlainText());
        });
        actions->addStretch();
        evidenceTabs->addTab(call, tr("Typed request"));
        tabs_->addTab(evidence_, tr("Results"));
    }

    struct Action
    {
        sdp_decision_operation operation;
        const char* name;
        QString title;
    };


    bool supported() const
    {
        return session_ && session_->state().connected && session_->capabilities().value("decision_explain_v1").toBool();
    }

    bool ready() const
    {
        return supported() && !busy_ && !session_->busy() && !session_->state().transactionActive &&
            session_->state().autocommit;
    }

    bool writable() const
    {
        return ready() && !session_->profile().read_only && uncertain_.isEmpty() &&
            session_->capabilities().value("decision_v1").toBool();
    }

    QJsonObject target() const { return findObject(catalog_, target_->currentData().toString()); }
    QString targetSql() const { return transfer::relation(target()); }
    QString name() const { return ruleName_->text().trimmed(); }

    void sync()
    {
        const bool operational = ready();
        const bool selected = !targetSql().isEmpty();
        const bool authoring = writable() && selected;
        const bool writingRule = authoring && !name().isEmpty();
        const auto event = event_->currentData().toString();
        const bool computation = effect_->currentText() == u"SET";
        target_->setEnabled(!busy_);
        ruleName_->setEnabled(!busy_);
        effect_->setEnabled(!busy_);
        column_->setEnabled(!busy_ && computation);
        calculation_->setEnabled(!busy_ && computation);
        row_->setEnabled(!busy_ && event != u"insert");
        proposed_->setReadOnly(busy_ || event == u"delete");
        source_->setReadOnly(busy_);
        for (auto* button : inspect_) button->setEnabled(operational && selected);
        compare_->setEnabled(operational && selected && !name().isEmpty());
        load_->setEnabled(!busy_ && session_ && session_->state().connected && !session_->busy() &&
            !session_->state().transactionActive && session_->capabilities().value("history_v1").toBool() &&
            savedRule_->currentIndex() >= 0);
        template_->setEnabled(writingRule);
        validate_->setEnabled(writingRule && !source_->toPlainText().trimmed().isEmpty());
        save_->setEnabled(writingRule && !validated_.isEmpty() && validated_ == source_->toPlainText().trimmed());
        activate_->setEnabled(writingRule);
        useVersion_->setEnabled(!busy_ && savedVersion_->currentIndex() >= 0 &&
            savedRule_->currentData().toString() == loadedRuleId_);
        if (!uncertain_.isEmpty()) policy_->setText(tr("A write may have committed. Check its worksheet outcome token before retrying."));
        else if (!supported()) policy_->setText(tr("Decision inspection is unavailable on this connection."));
        else if (!busy_ && (session_->state().transactionActive || !session_->state().autocommit))
            policy_->setText(tr("Finish the worksheet transaction and enable autocommit before using Decision Studio."));
        else policy_->clear();
    }

    void loadCatalog(const QString& selectedTable, const QString& selectedRule)
    {
        const QSignalBlocker targetSignal(target_);
        const QSignalBlocker ruleSignal(savedRule_);
        target_->clear();
        savedRule_->clear();
        for (const auto& value : catalog_)
        {
            const auto object = value.toObject();
            const auto id = object.value("id").toString();
            if (id.isEmpty() || transfer::relation(object).isEmpty()) continue;
            const auto kind = object.value("kind").toInt();
            if (kind == 3) target_->addItem(displayName(object), id);
            if (kind == 8) savedRule_->addItem(displayName(object), id);
        }
        if (const auto index = target_->findData(selectedTable); index >= 0) target_->setCurrentIndex(index);
        if (const auto index = savedRule_->findData(selectedRule); index >= 0) savedRule_->setCurrentIndex(index);
    }

    void updateColumns()
    {
        const QSignalBlocker locked(column_);
        column_->clear();
        for (const auto& col : target().value("columns").toArray())
        {
            const auto name = col.toObject().value("name").toString();
            if (!name.isEmpty()) column_->addItem(name);
        }
    }

    const char* actionName(sdp_decision_operation operation) const
    {
        switch (operation)
        {
        case SDP_DECISION_CREATE: return "SDP_DECISION_CREATE";
        case SDP_DECISION_VALIDATE: return "SDP_DECISION_VALIDATE";
        case SDP_DECISION_ACTIVATE: return "SDP_DECISION_ACTIVATE";
        case SDP_DECISION_TEST: return "SDP_DECISION_TEST";
        case SDP_DECISION_EXPLAIN: return "SDP_DECISION_EXPLAIN";
        case SDP_DECISION_WHYNOT: return "SDP_DECISION_WHYNOT";
        case SDP_DECISION_WHATIF: return "SDP_DECISION_WHATIF";
        case SDP_DECISION_RECHECK: return "SDP_DECISION_RECHECK";
        default: return "UNKNOWN";
        }
    }

    // The same object displayed in the request pane is serialized for the Native SDK.
    bool call(const char* api, const char* operation, const QJsonObject& body,
        const QString& message, SqlSession::NativeRequest request, SqlSession::Finished finished)
    {
        if (!session_ || !session_->state().connected || session_->busy() || busy_) return false;
        call_->setPlainText(formatted({{"api", api}, {"operation", operation}, {"request", body}}));
        busy_ = true;
        status_->setText(message);
        sync();
        result_->clear();
        tree_->clear();
        QPointer<Studio> self(this);
        if (session_->request(std::move(request), [self, finished = std::move(finished)](const SqlReply& reply) {
            if (!self) return;
            self->busy_ = false;
            finished(reply);
            if (self) self->sync();
        }, 10000)) return true;
        busy_ = false;
        sync();
        status_->setText(tr("The worksheet could not start the request."));
        return false;
    }

    void send(sdp_decision_operation operation, const QJsonObject& body, const QString& message, SqlSession::Finished done)
    {
        const auto bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
        const auto native = [operation, bytes](auto connection, auto* output, auto* error) {
            const sdp_text input{bytes.constData(), static_cast<uint32_t>(bytes.size())};
            return sdp_decision_command(connection, operation, input, output, error);
        };
        if (!call("sdp_decision_command", actionName(operation), body, message, native, std::move(done)))
            status_->setText(tr("The worksheet cannot send the Decision request right now."));
    }

    void history(sdp_history_operation operation, const QJsonObject& body, SqlSession::Finished done)
    {
        const auto bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
        const auto native = [operation, bytes](auto connection, auto* output, auto* error) {
            const sdp_text input{bytes.constData(), static_cast<uint32_t>(bytes.size())};
            return sdp_history_command(connection, operation, input, output, error);
        };
        const auto name = operation == SDP_HISTORY_STATUS ? "SDP_HISTORY_STATUS" : "SDP_HISTORY_READ";
        if (!call("sdp_history_command", name, body, tr("Reading the current saved rule…"), native, std::move(done)))
            status_->setText(tr("The worksheet cannot read the saved rule right now."));
    }

    bool addRevision(QJsonObject& request, const QString& field)
    {
        const auto revision = field.trimmed();
        if (revision.isEmpty()) return true;
        if (stableNumber(revision).isEmpty())
        {
            status_->setText(tr("Enter a positive decimal History revision or leave it blank."));
            return false;
        }
        request.insert("revision", revision);
        return true;
    }

    void simulate(sdp_decision_operation operation)
    {
        if (!ready() || targetSql().isEmpty()) return;
        const auto event = event_->currentData().toString();
        QJsonObject request{{"target", targetSql()}, {"event", event}};
        if (!addRevision(request, revision_->text())) return;

        if (event != u"insert")
        {
            const auto id = stableNumber(row_->text().trimmed());
            if (id.isEmpty())
            {
                status_->setText(tr("Use the stable row ID shown by History for an update or delete."));
                return;
            }
            request.insert("row_id", id);
        }
        if (event != u"delete")
        {
            const auto typed = readInput(proposed_->toPlainText());
            if (!typed || !typed->isObject())
            {
                status_->setText(tr("Proposed values must be a bounded JSON object with typed values."));
                return;
            }
            request.insert("proposed", typed->object());
        }
        tabs_->setCurrentWidget(evidence_);
        send(operation, request, tr("Evaluating the Decision simulation…"), [this](const SqlReply& reply) {
            if (!acceptReply(reply)) return;
            const auto response = reply.data;
            const auto revision = response.value("revision").toString();
            if (stableNumber(revision).isEmpty() || !response.value("allowed").isBool() ||
                !response.value("rules").isArray())
            {
                status_->setText(tr("The server returned an incompatible Decision result."));
                return;
            }
            result_->setPlainText(formatted(response));
            appendRules(nullptr, response.value("rules").toArray());
            tree_->expandAll();
            const auto outcome = response.value("allowed").toBool() ? tr("allowed by the rules") : tr("denied by the rules");
            status_->setText(tr("Simulation at revision %1: %2 (%3).")
                .arg(revision, outcome, response.value("reason").toString()));
        });
    }

    void compare()
    {
        if (!ready() || targetSql().isEmpty() || name().isEmpty()) return;
        const auto parsed = readInput(cases_->toPlainText());
        if (!parsed || !parsed->isArray() || parsed->array().isEmpty() || parsed->array().size() > 16 ||
            prior_->value() == candidate_->value())
        {
            status_->setText(tr("Choose different rule versions and provide a JSON array of 1–16 test cases."));
            return;
        }
        for (const auto& entry : parsed->array())
            if (!entry.isObject())
            {
                status_->setText(tr("Each test case must be a JSON object."));
                return;
            }
        QJsonObject request{{"target", targetSql()}, {"rule", transfer::identifier(name())},
            {"from_version", prior_->value()}, {"to_version", candidate_->value()}, {"cases", parsed->array()}};
        if (!addRevision(request, compareRevision_->text())) return;
        tabs_->setCurrentWidget(evidence_);
        send(SDP_DECISION_TEST, request, tr("Comparing both rule versions on the same server snapshot…"),
            [this](const SqlReply& reply) {
                if (!acceptReply(reply)) return;
                const auto revision = reply.data.value("revision").toString();
                const auto cases = reply.data.value("cases").toArray();
                if (stableNumber(revision).isEmpty() || cases.isEmpty())
                {
                    status_->setText(tr("The server returned an incompatible version comparison."));
                    return;
                }
                for (qsizetype index = 0; index < cases.size(); ++index)
                {
                    const auto pair = cases.at(index).toObject();
                    const auto row = new QTreeWidgetItem(tree_, {tr("Case %1").arg(index + 1)});
                    for (const auto& kind : {QStringLiteral("prior"), QStringLiteral("candidate")})
                    {
                        const auto image = pair.value(kind).toObject();
                        const auto label = kind == u"prior" ? tr("Earlier version") : tr("Candidate version");
                        const auto item = new QTreeWidgetItem(row, {label, {}, image.value("reason").toString()});
                        appendRules(item, image.value("rules").toArray());
                    }
                }
                result_->setPlainText(formatted(reply.data));
                tree_->expandToDepth(1);
                status_->setText(tr("Compared %1 cases at revision %2. %3 outcomes changed.")
                    .arg(cases.size()).arg(revision).arg(reply.data.value("changed").toInteger()));
            });
    }

    void appendRules(QTreeWidgetItem* parent, const QJsonArray& rules)
    {
        for (const auto& entry : rules)
        {
            const auto rule = entry.toObject();
            const auto id = objectId(rule.value("object_id"));
            const auto known = findObject(catalog_, id);
            const auto title = known.isEmpty() ? tr("Rule %1").arg(id) : displayName(known);
            const auto root = new QTreeWidgetItem({tr("%1 · v%2").arg(title).arg(rule.value("version").toInteger()),
                rule.value("state").toString(), rule.value("truth").toString(),
                humanElapsed(rule.value("elapsed_ns"))});
            if (parent) parent->addChild(root);
            else tree_->addTopLevelItem(root);

            // Server traces use stable numeric node IDs with parents preceding descendants.
            QHash<qint64, QTreeWidgetItem*> parents;
            for (const auto& child : rule.value("nodes").toArray())
            {
                const auto node = child.toObject();
                const auto item = new QTreeWidgetItem(parents.value(node.value("parent_id").toInteger(), root),
                    {node.value("op").toString(), node.value("state").toString(), node.value("truth").toString(),
                        humanElapsed(node.value("elapsed_ns"))});
                parents.insert(node.value("id").toInteger(), item);
            }
        }
    }

    QString humanElapsed(const QJsonValue& value) const
    {
        return value.isDouble() ? QString::number(value.toInteger()) : QString{};
    }

    bool acceptReply(const SqlReply& reply)
    {
        if (reply.error.code == SDP_OK) return true;
        status_->setText(describe(reply));
        return false;
    }


    void buildSource()
    {
        if (!writable() || targetSql().isEmpty() || name().isEmpty()) return;
        const auto condition = condition_->text().trimmed();
        auto effect = effect_->currentText();
        if (condition.isEmpty())
        {
            status_->setText(tr("Enter the rule condition."));
            return;
        }
        if (effect == u"SET")
        {
            const auto column = column_->currentText();
            const auto calculation = calculation_->text().trimmed();
            if (column.isEmpty() || calculation.isEmpty())
            {
                status_->setText(tr("Choose the computed column and enter its SQL calculation."));
                return;
            }
            effect += QStringLiteral(" %1 = %2").arg(transfer::identifier(column), calculation);
        }
        source_->setPlainText(QStringLiteral("CREATE DECISION RULE %1 ON %2 FOR %3 PRIORITY %4\nWHEN (%5)\nTHEN %6;")
            .arg(transfer::identifier(name()), targetSql(), ruleEvent_->currentText()).arg(priority_->value())
            .arg(condition, effect));
        status_->setText(tr("SQL ready for server validation."));
    }

    bool validSource(const QString& source)
    {
        if (source.isEmpty() || source.toUtf8().size() > sqlLimit || source.contains(QChar(u'\0')))
        {
            status_->setText(tr("Enter a Decision rule SQL statement of at most 16 KiB."));
            return false;
        }
        if (!sameRuleHeader(source, name(), target().value("qualified_name").toArray()))
        {
            status_->setText(tr("Use CREATE DECISION RULE for the rule name and target table selected above."));
            return false;
        }
        return true;
    }

    void validateSource()
    {
        if (!writable()) return;
        const auto source = source_->toPlainText().trimmed();
        if (!validSource(source)) return;
        validated_.clear();
        send(SDP_DECISION_VALIDATE, {{"sql", source}}, tr("Validating rule SQL and dependencies on the server…"),
            [this, source](const SqlReply& reply) {
                if (!validReply(reply, true)) return;
                validated_ = source;
                status_->setText(tr("Rule SQL and dependencies validated. Save this source as an immutable version."));
            });
    }

    bool validReply(const SqlReply& reply, bool navigate)
    {
        if (!acceptReply(reply))
        {
            if (navigate && reply.error.line)
            {
                const auto block = source_->document()->findBlockByNumber(static_cast<int>(reply.error.line) - 1);
                if (block.isValid())
                {
                    QTextCursor cursor(block);
                    const auto count = std::min<int>(std::max(0, static_cast<int>(reply.error.column) - 1),
                        block.length() - 1);
                    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, count);
                    source_->setTextCursor(cursor);
                    source_->setFocus();
                }
            }
            return false;
        }
        if (!reply.data.value("valid").toBool())
        {
            status_->setText(tr("The server did not validate this Decision statement."));
            return false;
        }
        result_->setPlainText(formatted(reply.data));
        return true;
    }

    void saveSource()
    {
        if (!writable()) return;
        const auto source = source_->toPlainText().trimmed();
        if (source != validated_ || !validSource(source)) return;
        if (QMessageBox::question(this, tr("Save Decision rule"),
            tr("Save a new immutable version of %1 on %2? Its first version is active. An existing rule keeps its active version.")
                .arg(name(), displayName(target())), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        mutate(SDP_DECISION_CREATE, source, tr("Saved an immutable rule version."));
    }

    void activateVersion()
    {
        if (!writable() || targetSql().isEmpty() || name().isEmpty()) return;
        const auto version = activateVersion_->value();
        const auto source = QStringLiteral("ALTER DECISION RULE %1 ON %2 ACTIVATE VERSION %3;")
            .arg(transfer::identifier(name()), targetSql()).arg(version);
        send(SDP_DECISION_VALIDATE, {{"sql", source}}, tr("Validating the saved version and its current dependencies…"),
            [this, source, version](const SqlReply& reply) {
                if (!validReply(reply, false)) return;
                if (QMessageBox::question(this, tr("Activate Decision rule version"),
                    tr("Activate version %1 of %2 for future writes on %3?\n\n%4")
                        .arg(version).arg(name(), displayName(target()), source),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                {
                    status_->setText(tr("Activation cancelled."));
                    return;
                }
                mutate(SDP_DECISION_ACTIVATE, source, tr("Activated Decision rule version %1.").arg(version));
            });
    }

    void mutate(sdp_decision_operation action, const QString& sql, const QString& success)
    {
        if (!writable()) return;
        const auto token = QString::fromLatin1(QUuid::createUuid().toRfc4122().toHex());
        send(action, {{"sql", sql}, {"request_token", token}}, tr("Updating the Decision catalog…"),
            [this, token, success](const SqlReply& reply) {
                validated_.clear();
                if (reply.error.unknown_outcome)
                {
                    uncertain_ = token;
                    status_->setText(tr("The server did not confirm the write. Check request token %1.").arg(token));
                    return;
                }
                if (!acceptReply(reply)) return;
                result_->setPlainText(formatted(reply.data));
                const auto revision = reply.data.value("lsn").toString();
                status_->setText(revision.isEmpty() ? success : success + u' ' + tr("History revision %1.").arg(revision));
                versions_ = {};
                savedVersion_->clear();
                versionDetails_->clear();
                refreshCatalog();
            });
    }

    void refreshCatalog()
    {
        const auto tableId = target_->currentData().toString();
        const auto ruleId = savedRule_->currentData().toString();
        QPointer<Studio> self(this);
        busy_ = session_ && session_->refreshMetadata([self, tableId, ruleId](const SqlReply& reply) {
            if (!self) return;
            self->busy_ = false;
            if (reply.error.code == SDP_OK)
            {
                self->catalog_ = reply.data.value("objects").toArray();
                self->loadCatalog(tableId, ruleId);
                self->updateColumns();
            }
            self->sync();
            if (self->refresh_) self->refresh_();
        });
        sync();
    }

    void loadRule()
    {
        const auto id = savedRule_->currentData().toString();
        const auto selected = findObject(catalog_, id);
        const auto relation = transfer::relation(selected);
        if (id.isEmpty() || relation.isEmpty() || !load_->isEnabled()) return;
        versions_ = {};
        loadedRuleId_.clear();
        savedVersion_->clear();
        versionDetails_->clear();
        history(SDP_HISTORY_STATUS, {}, [this, selected, id, relation](const SqlReply& reply) {
            if (!acceptReply(reply)) return;
            const auto revision = stableNumber(reply.data.value("current_revision").toString());
            if (revision.isEmpty())
            {
                status_->setText(tr("The server did not return a current History revision."));
                return;
            }
            history(SDP_HISTORY_READ, {{"target", relation}, {"revision", revision}, {"limit", 1}},
                [this, selected, id, revision](const SqlReply& image) {
                    if (!acceptReply(image)) return;
                    showSavedRule(selected, id, revision, image.data);
                });
        });
    }

    void showSavedRule(const QJsonObject& selected, const QString& id, const QString& revision,
        const QJsonObject& image)
    {
        const auto object = image.value("object").toObject();
        if (image.value("object_id").toString() != id || image.value("revision").toString() != revision ||
            image.value("name") != selected.value("qualified_name") || object.value("kind").toString() != u"decision" ||
            object.value("schema_version") != selected.value("schema_version"))
        {
            status_->setText(tr("The Decision object or its revision changed. Reopen it from the refreshed Object Explorer."));
            return;
        }
        const auto definition = readInput(object.value("definition").toString());
        if (!definition || !definition->isObject())
        {
            status_->setText(tr("The server did not grant access to the rule definition."));
            return;
        }
        const auto details = definition->object();
        const auto targetId = objectId(details.value("target"));
        const auto versions = details.value("versions").toArray();
        const auto active = details.value("active").toInt();
        const auto targetIndex = target_->findData(targetId);
        if (details.value("format").toInt() != 1 || targetIndex < 0 || versions.isEmpty() ||
            versions.size() > 16 || active < 1 || active > versions.size())
        {
            status_->setText(tr("The saved rule format or its target is unavailable."));
            return;
        }
        const auto qualified = selected.value("qualified_name").toArray();
        if (qualified.size() != 3) return;

        loadedRuleId_ = id;
        loadedTargetId_ = targetId;
        loadedName_ = qualified.last().toString();
        activeVersion_ = active;
        versions_ = versions;
        target_->setCurrentIndex(targetIndex);
        ruleName_->setText(loadedName_);
        const QSignalBlocker signal(savedVersion_);
        savedVersion_->clear();
        for (int index = 0; index < versions_.size(); ++index)
            savedVersion_->addItem(index + 1 == active ? tr("Version %1 · active").arg(index + 1) : tr("Version %1").arg(index + 1));
        savedVersion_->setCurrentIndex(active - 1);
        activateVersion_->setValue(active);
        renderVersion();
        result_->setPlainText(formatted(image));
        status_->setText(tr("Loaded %1 immutable versions from History revision %2.").arg(versions_.size()).arg(revision));
    }

    void renderVersion()
    {
        const auto at = savedVersion_->currentIndex();
        versionDetails_->clear();
        if (at >= 0 && at < versions_.size())
        {
            QStringList lines{tr("Version %1 · %2").arg(at + 1).arg(at + 1 == activeVersion_ ? tr("active") : tr("saved"))};
            for (const auto& item : versions_.at(at).toObject().value("dependencies").toArray())
            {
                const auto dependency = item.toArray();
                if (dependency.size() != 2) continue;
                const auto id = objectId(dependency.at(0));
                const auto object = findObject(catalog_, id);
                if (object.isEmpty()) lines.append(tr("Dependency · revision %1").arg(dependency.at(1).toInteger()));
                else lines.append(tr("%1 · revision %2").arg(displayName(object)).arg(dependency.at(1).toInteger()));
            }
            versionDetails_->setPlainText(lines.join(u'\n'));
        }
        sync();
    }

    void editVersion()
    {
        const auto index = savedVersion_->currentIndex();
        if (index < 0 || index >= versions_.size()) return;
        const auto source = versions_.at(index).toObject().value("source").toString();
        const auto targetIndex = target_->findData(loadedTargetId_);
        if (targetIndex < 0 || source.isEmpty())
        {
            status_->setText(tr("The version source or its target is unavailable."));
            return;
        }
        target_->setCurrentIndex(targetIndex);
        ruleName_->setText(loadedName_);
        source_->setPlainText(source);
        activateVersion_->setValue(index + 1);
        status_->setText(tr("The saved source is in the editor. Saving changes adds an immutable version."));
    }

    QPointer<SqlSession> session_;
    QJsonArray catalog_;
    std::function<void()> refresh_;
    bool busy_{};
    QString validated_, uncertain_, loadedRuleId_, loadedTargetId_, loadedName_;
    QJsonArray versions_;
    int activeVersion_{};
    QTabWidget* tabs_{};
    QWidget *author_{}, *evidence_{};
    QComboBox *target_{}, *event_{}, *savedRule_{}, *savedVersion_{}, *ruleEvent_{}, *effect_{}, *column_{};
    QLineEdit *ruleName_{}, *row_{}, *revision_{}, *condition_{}, *calculation_{}, *compareRevision_{};
    QPlainTextEdit *proposed_{}, *source_{}, *versionDetails_{}, *cases_{}, *result_{}, *call_{};
    QTreeWidget* tree_{};
    QSpinBox *priority_{}, *activateVersion_{}, *prior_{}, *candidate_{};
    QLabel *status_{}, *policy_{};
    QList<QPushButton*> inspect_;
    QPushButton *compare_{}, *load_{}, *useVersion_{}, *template_{}, *validate_{}, *save_{}, *activate_{};
};
}

QDialog* showDecisionStudio(SqlSession& session, const QJsonArray& catalog, const QJsonObject& selected,
    QWidget* parent, std::function<void()> refresh)
{
    auto* studio = new Studio(session, catalog, selected, parent, std::move(refresh));
    studio->show();
    return studio;
}
}

