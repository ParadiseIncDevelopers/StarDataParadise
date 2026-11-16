#include "routine_studio.hpp"

#include "sql_definition.hpp"
#include "sql_editor.hpp"
#include "sql_session.hpp"
#include "table_design.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <limits>
#include <optional>

namespace wb
{
namespace
{
enum class RoutineKind { function, procedure, trigger };

struct Parameter
{
    QString name;
    QString mode = QStringLiteral("IN");
    QString displayType;
    QJsonObject type;
};

struct RoutineHeader
{
    RoutineKind kind{};
    QStringList name;
    QList<Parameter> parameters;
    QString returnType;
    bool replace{};
    qsizetype createEnd{};
};

QStringList nameParts(const QJsonValue& value)
{
    QStringList result;
    for (const auto part : value.toArray()) result.push_back(part.toString());
    return result;
}

QString kindName(RoutineKind kind)
{
    switch (kind)
    {
    case RoutineKind::function: return QObject::tr("Function");
    case RoutineKind::procedure: return QObject::tr("Procedure");
    case RoutineKind::trigger: return QObject::tr("Trigger");
    }
    return {};
}

QString jsonText(const QJsonObject& object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
}

QByteArray wireJson(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

sdp_text wire(const QByteArray& bytes)
{
    return {bytes.constData(), static_cast<uint32_t>(bytes.size())};
}

class HeaderTokens
{
public:
    explicit HeaderTokens(QStringView source) : source_(source)
    {
        auto state = SqlLexState::normal;
        for (const auto& token : sql_tokens(source, state))
            if (token.kind != SqlTokenKind::whitespace && token.kind != SqlTokenKind::comment)
                tokens_.push_back(token);
    }

    bool word(QStringView text)
    {
        if (current().kind != SqlTokenKind::word || value().compare(text, Qt::CaseInsensitive) != 0) return false;
        ++index_;
        return true;
    }

    bool symbol(QChar symbol)
    {
        if (value().size() != 1 || value().front() != symbol) return false;
        ++index_;
        return true;
    }

    QString identifier()
    {
        if (current().kind == SqlTokenKind::word)
        {
            auto name = value().toString().toLower();
            ++index_;
            return name;
        }
        if (current().kind != SqlTokenKind::quoted_name || value().size() < 2 ||
            value().front() != u'"' || value().back() != u'"') return {};
        auto identifier = value().sliced(1, value().size() - 2).toString();
        identifier.replace(QStringLiteral("\"\""), QStringLiteral("\""));
        ++index_;
        return identifier;
    }

    std::optional<int> number()
    {
        if (current().kind != SqlTokenKind::word) return {};
        bool valid{};
        const auto result = value().toInt(&valid);
        if (valid) ++index_;
        return valid ? std::optional<int>(result) : std::nullopt;
    }

    qsizetype endOfConsumed() const
    {
        const auto token = tokens_.value(index_ - 1);
        return token.offset + token.length;
    }

private:
    SqlToken current() const { return tokens_.value(index_, {SqlTokenKind::symbol, source_.size(), 0}); }
    QStringView value() const { return source_.mid(current().offset, current().length); }

    QStringView source_;
    QList<SqlToken> tokens_;
    qsizetype index_{};
};

bool parameterType(HeaderTokens& tokens, Parameter& parameter)
{
    auto type = tokens.identifier().toUpper();
    if (type == u"DECIMAL")
    {
        if (!tokens.symbol(u'(')) return false;
        const auto precision = tokens.number();
        if (!precision || !tokens.symbol(u',')) return false;
        const auto scale = tokens.number();
        if (!scale || !tokens.symbol(u')') || *precision < 1 || *precision > 38 || *scale < 0 || *scale > *precision)
            return false;
        parameter.displayType = QStringLiteral("DECIMAL(%1,%2)").arg(*precision).arg(*scale);
        parameter.type = {{"type", "DECIMAL"}, {"precision", *precision}, {"scale", *scale}};
        return true;
    }
    if (type == u"TIMESTAMP")
    {
        if (!tokens.word(u"WITH") || !tokens.word(u"TIME") || !tokens.word(u"ZONE")) return false;
        type = QStringLiteral("TIMESTAMP WITH TIME ZONE");
    }
    if (type == u"INT" || type == u"INTEGER") type = QStringLiteral("BIGINT");
    static const QStringList supported{"BIGINT", "BOOLEAN", "TEXT", "BLOB", "DATE", "TIME",
        "TIMESTAMP WITH TIME ZONE", "RECORD", "COLLECTION", "REFCURSOR"};
    if (!supported.contains(type)) return false;
    parameter.displayType = type;
    parameter.type = {{"type", type}};
    return true;
}

std::optional<RoutineHeader> header(QStringView source)
{
    HeaderTokens tokens(source);
    RoutineHeader result;
    if (!tokens.word(u"CREATE")) return {};
    result.createEnd = tokens.endOfConsumed();
    if (tokens.word(u"OR"))
    {
        if (!tokens.word(u"REPLACE")) return {};
        result.replace = true;
    }
    if (tokens.word(u"FUNCTION")) result.kind = RoutineKind::function;
    else if (tokens.word(u"PROCEDURE")) result.kind = RoutineKind::procedure;
    else if (tokens.word(u"TRIGGER") && !result.replace) result.kind = RoutineKind::trigger;
    else return {};

    do
    {
        const auto name = tokens.identifier();
        if (name.isEmpty() || result.name.size() == 3) return {};
        result.name.push_back(name);
    } while (tokens.symbol(u'.'));
    if (result.name.size() == 1) result.name.prepend(QStringLiteral("public"));
    if (result.name.size() == 2) result.name.prepend(QStringLiteral("main"));
    if (sqlQualifiedIdentifier(result.name).isEmpty()) return {};

    if (result.kind == RoutineKind::trigger) return result;
    if (tokens.symbol(u'(') && !tokens.symbol(u')'))
    {
        do
        {
            Parameter parameter;
            parameter.name = tokens.identifier();
            if (parameter.name.isEmpty()) return {};
            if (tokens.word(u"IN"))
            {
                if (tokens.word(u"OUT")) parameter.mode = QStringLiteral("IN OUT");
            }
            else if (tokens.word(u"OUT")) parameter.mode = QStringLiteral("OUT");
            if (!parameterType(tokens, parameter)) return {};
            result.parameters.push_back(std::move(parameter));
        } while (tokens.symbol(u','));
        if (!tokens.symbol(u')')) return {};
    }
    if (result.kind == RoutineKind::function)
    {
        Parameter returns;
        if ((!tokens.word(u"RETURN") && !tokens.word(u"RETURNS")) || !parameterType(tokens, returns)) return {};
        result.returnType = returns.displayType;
    }
    return result;
}

QJsonObject typedValue(const Parameter& parameter, QString text, bool isNull)
{
    if (isNull) return {{"t", "null"}};
    const auto type = parameter.type.value("type").toString();
    if (type == u"TEXT" || type == u"DATE" || type == u"TIME") return {{"t", "text"}, {"v", text}};
    text = text.trimmed();
    if (type == u"BIGINT")
    {
        bool ok{};
        const auto value = text.toLongLong(&ok);
        return ok ? QJsonObject{{"t", "i64"}, {"v", QString::number(value)}} : QJsonObject{};
    }
    if (type == u"BOOLEAN")
    {
        if (text.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0) return {{"t", "bool"}, {"v", true}};
        if (text.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0) return {{"t", "bool"}, {"v", false}};
        return {};
    }
    if (type == u"BLOB")
    {
        static const QRegularExpression hex(QStringLiteral("^(?:[0-9a-fA-F]{2})*$"));
        return hex.match(text).hasMatch() ? QJsonObject{{"t", "binary"}, {"hex", text.toLower()}} : QJsonObject{};
    }
    if (type == u"DECIMAL")
    {
        static const QRegularExpression decimal(QStringLiteral(R"(^(-?)([0-9]+)(?:\.([0-9]+))?$)"));
        const auto parts = decimal.match(text);
        const auto fraction = parts.captured(3);
        if (!parts.hasMatch() || fraction.size() > parameter.type.value("scale").toInt()) return {};
        auto digits = parts.captured(2) + fraction;
        while (digits.size() > 1 && digits.startsWith(u'0')) digits.remove(0, 1);
        if (digits.size() > parameter.type.value("precision").toInt()) return {};
        if (parts.captured(1) == u"-" && digits != u"0") digits.prepend(u'-');
        return {{"t", "decimal"}, {"unscaled", digits}, {"scale", fraction.size()}};
    }
    const auto json = QJsonDocument::fromJson(text.toUtf8()).object();
    const auto tag = type == u"TIMESTAMP WITH TIME ZONE" ? QStringLiteral("timestamp") : type.toLower();
    return json.value("t").toString() == tag ? json : QJsonObject{};
}

struct Invocation
{
    QString sql;
    QJsonObject types;
    QJsonObject values;
};

struct TestCase
{
    QString name;
    Invocation call;
    QJsonObject expected;
};

sdp_status runTest(sdp_connection connection, const TestCase& test, sdp_buffer* output, sdp_error* error)
{
    const auto sql = test.call.sql.toUtf8();
    const auto types = wireJson(test.call.types);
    const auto values = wireJson(test.call.values);
    const auto expected = wireJson(test.expected);
    sdp_buffer statement{};
    const auto prepared = sdp_prepare_typed(connection, wire(sql), wire(types), &statement, error);
    if (prepared != SDP_OK)
    {
        sdp_buffer_free(&statement);
        return prepared;
    }
    const sdp_text id{reinterpret_cast<const char*>(statement.data), statement.size};
    const auto result = sdp_routine_test(connection, id, wire(values), wire(expected), output, error);
    sdp_statement_close(connection, id, nullptr);
    sdp_buffer_free(&statement);
    return result;
}

QJsonObject findRoutine(const QJsonArray& catalog, const QStringList& name)
{
    for (const auto entry : catalog)
    {
        const auto object = entry.toObject();
        if (object.value("kind").toInt() == 6 && nameParts(object.value("qualified_name")) == name)
            return object;
    }
    return {};
}

QString diffText(QStringView original, QStringView changed)
{
    if (original == changed) return QObject::tr("No source changes.");
    const auto oldLines = original.split(u'\n');
    const auto newLines = changed.split(u'\n');
    const auto size = qMin(oldLines.size(), newLines.size());
    qsizetype first = 0;
    while (first < size && oldLines.at(first) == newLines.at(first)) ++first;
    qsizetype ending = 0;
    while (ending < size - first && oldLines.at(oldLines.size() - ending - 1) == newLines.at(newLines.size() - ending - 1))
        ++ending;
    QString result = QObject::tr("Changes beginning at line %1\n\n").arg(first + 1);
    for (auto line = first; line < oldLines.size() - ending; ++line) result += QStringLiteral("- ") + oldLines.at(line) + u'\n';
    for (auto line = first; line < newLines.size() - ending; ++line) result += QStringLiteral("+ ") + newLines.at(line) + u'\n';
    return result;
}

class RoutineStudio : public QDialog
{
public:
    RoutineStudio(SqlSession& session, const QJsonArray& catalog, const QJsonObject& routine,
        QWidget* parent, std::function<void()> refresh, std::function<void(const QJsonObject&)> debug);

    void reject() override
    {
        if (!busy_) QDialog::reject();
    }

private:
    QPushButton* action(QLayout* row, const char* name, const QString& label, std::function<void()> handler);
    void status(QString text) { status_->setText(std::move(text)); }
    void work(bool busy);
    void loadSource();
    void updateDescriptor();
    void putTemplate(RoutineKind kind);
    void compile();
    void discardCompile(const SqlReply& reply, bool rollback);
    void compilationCommitted(const RoutineHeader& target);
    std::optional<Invocation> invocation() const;
    void run();
    void showProfiler();
    void addTest();
    void runTestCases(qsizetype index);
    QString errorText(const SqlReply& reply) const;

    SqlSession& session_;
    QJsonArray catalog_;
    QJsonObject routine_;
    std::function<void()> refresh_;
    std::function<void(const QJsonObject&)> debug_;
    SqlEditor* source_{};
    QTabWidget* tabs_{};
    QPlainTextEdit* diff_{};
    QTreeWidget* dependencies_{};
    QTableWidget* inputs_{};
    QPlainTextEdit* outputs_{};
    QTreeWidget* profiler_{};
    QLabel* status_{};
    QLabel* descriptor_{};
    QPushButton* compile_{};
    QPushButton* run_{};
    QPushButton* debugButton_{};
    QPushButton* addTest_{};
    QPushButton* runTests_{};
    QLineEdit* testName_{};
    QLineEdit* expectedException_{};
    QPlainTextEdit* expected_{};
    QPlainTextEdit* testResults_{};
    QListWidget* cases_{};
    QComboBox* kinds_{};
    QComboBox* schema_{};
    QString liveSource_;
    QString definitionRevision_;
    std::optional<RoutineHeader> liveHeader_;
    QList<TestCase> tests_;
    bool busy_{};
    bool unsafeOutcome_{};
};
RoutineStudio::RoutineStudio(SqlSession& session, const QJsonArray& catalog, const QJsonObject& routine,
    QWidget* parent, std::function<void()> refresh, std::function<void(const QJsonObject&)> debug)
    : QDialog(parent), session_(session), catalog_(catalog), routine_(routine),
      refresh_(std::move(refresh)), debug_(std::move(debug))
{
    setObjectName("routineStudio");
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("PL/SQL Studio · %1").arg(endpoint(session.profile())));
    resize(965, 660);
    auto* layout = new QVBoxLayout(this);
    descriptor_ = new QLabel;
    descriptor_->setTextFormat(Qt::PlainText);
    layout->addWidget(descriptor_);

    auto* target = new QHBoxLayout;
    kinds_ = new QComboBox;
    kinds_->setObjectName("routineKind");
    for (auto kind : {RoutineKind::function, RoutineKind::procedure, RoutineKind::trigger})
        kinds_->addItem(kindName(kind), static_cast<int>(kind));
    schema_ = new QComboBox;
    schema_->setObjectName("routineSchema");
    for (const auto entry : catalog_)
    {
        const auto object = entry.toObject();
        if (object.value("kind").toInt() != 2) continue;
        const auto parts = nameParts(object.value("qualified_name"));
        if (parts.size() == 2) schema_->addItem(parts.join(u'.'), QVariant::fromValue(parts));
    }
    const auto defaultSchema = schema_->findData(QVariant::fromValue(QStringList{"main", "public"}));
    if (defaultSchema >= 0) schema_->setCurrentIndex(defaultSchema);
    target->addWidget(new QLabel(tr("Start with")));
    target->addWidget(kinds_);
    target->addWidget(schema_);
    action(target, "routineTemplate", tr("Insert template"), [this] {
        if (routine_.isEmpty()) putTemplate(static_cast<RoutineKind>(kinds_->currentData().toInt()));
    });
    target->addStretch();
    auto* templates = new QWidget;
    templates->setLayout(target);
    templates->setVisible(routine_.isEmpty());
    layout->addWidget(templates);

    tabs_ = new QTabWidget;
    tabs_->setObjectName("routineTabs");
    layout->addWidget(tabs_, 1);
    source_ = new SqlEditor;
    source_->setObjectName("routineSource");
    tabs_->addTab(source_, tr("Source"));
    diff_ = new QPlainTextEdit;
    diff_->setObjectName("routineSourceDiff");
    diff_->setReadOnly(true);
    tabs_->addTab(diff_, tr("Source diff"));
    dependencies_ = new QTreeWidget;
    dependencies_->setObjectName("routineDependencies");
    dependencies_->setHeaderLabels({tr("Object"), tr("Schema revision")});
    tabs_->addTab(dependencies_, tr("Dependencies"));

    auto* invocationPage = new QWidget;
    auto* invocationLayout = new QVBoxLayout(invocationPage);
    inputs_ = new QTableWidget(0, 5);
    inputs_->setObjectName("routineInputs");
    inputs_->setHorizontalHeaderLabels({tr("Parameter"), tr("Mode"), tr("Type"), tr("Value"), tr("NULL")});
    inputs_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    invocationLayout->addWidget(inputs_, 2);
    auto* hint = new QLabel(tr("Enter text as-is, Boolean as true or false, BLOB as hex. "
        "Use wire JSON for RECORD, COLLECTION and timestamp values."));
    hint->setWordWrap(true);
    invocationLayout->addWidget(hint);
    outputs_ = new QPlainTextEdit;
    outputs_->setObjectName("routineOutputs");
    outputs_->setReadOnly(true);
    invocationLayout->addWidget(outputs_, 2);
    tabs_->addTab(invocationPage, tr("Input and results"));

    auto* testPage = new QWidget;
    auto* testLayout = new QVBoxLayout(testPage);
    auto* testForm = new QFormLayout;
    testName_ = new QLineEdit;
    testName_->setObjectName("routineTestName");
    testName_->setText(tr("Test 1"));
    expectedException_ = new QLineEdit;
    expectedException_->setObjectName("routineExpectedException");
    expected_ = new QPlainTextEdit;
    expected_->setObjectName("routineTestExpected");
    expected_->setPlainText(QStringLiteral("[]"));
    expected_->setMaximumHeight(100);
    expected_->setPlaceholderText(QStringLiteral("[{\"name\":\"out\",\"rows\":[[{\"t\":\"i64\",\"v\":\"7\"}]]}]"));
    testForm->addRow(tr("Test name"), testName_);
    testForm->addRow(tr("Expected exception (optional)"), expectedException_);
    testForm->addRow(tr("Expected typed result sets"), expected_);
    testLayout->addLayout(testForm);
    auto* testActions = new QHBoxLayout;
    addTest_ = action(testActions, "routineAddTest", tr("Save current inputs as test"), [this] { addTest(); });
    runTests_ = action(testActions, "routineRunTests", tr("Run test suite"), [this] {
        if (busy_ || tests_.isEmpty() || session_.busy() || session_.state().transactionActive ||
            session_.profile().environment != u"test" || session_.profile().read_only ||
            !liveHeader_ || source_->sql_text() != liveSource_) return;
        testResults_->clear();
        work(true);
        status(tr("Running tests in transactions the server rolls back…"));
        runTestCases(0);
    });
    auto* remove = action(testActions, "routineRemoveTest", tr("Remove selected test"), [this] {
        if (busy_ || cases_->currentRow() < 0) return;
        const auto index = cases_->currentRow();
        tests_.removeAt(index);
        delete cases_->takeItem(index);
        work(false);
    });
    testActions->addStretch();
    testLayout->addLayout(testActions);
    cases_ = new QListWidget;
    cases_->setObjectName("routineTestCases");
    testLayout->addWidget(cases_, 1);
    connect(cases_, &QListWidget::currentRowChanged, remove, [remove](int row) { remove->setEnabled(row >= 0); });
    remove->setEnabled(false);
    testResults_ = new QPlainTextEdit;
    testResults_->setObjectName("routineTestResults");
    testResults_->setReadOnly(true);
    testLayout->addWidget(testResults_, 2);
    auto* notice = new QLabel(tr("Tests run only on a test profile and a server configured as an isolated test instance."));
    notice->setWordWrap(true);
    testLayout->addWidget(notice);
    tabs_->addTab(testPage, tr("Tests"));

    profiler_ = new QTreeWidget;
    profiler_->setObjectName("routineProfiler");
    profiler_->setHeaderLabels({tr("Routine"), tr("Revision"), tr("Inclusive µs"), tr("Exclusive µs"),
        tr("SQL µs"), tr("Instructions")});
    tabs_->addTab(profiler_, tr("Profiler"));

    status_ = new QLabel;
    status_->setObjectName("routineStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto* controls = new QHBoxLayout;
    compile_ = action(controls, "routineCompile", tr("Compile"), [this] { compile(); });
    run_ = action(controls, "routineRun", tr("Run compiled routine"), [this] { run(); });
    debugButton_ = action(controls, "routineDebug", tr("Attach debugger"), [this] {
        if (!busy_ && !session_.busy() && liveHeader_ && liveHeader_->kind != RoutineKind::trigger &&
            source_->sql_text() == liveSource_ && debug_) debug_(routine_);
    });
    controls->addStretch();
    action(controls, "routineClose", tr("Close"), [this] { reject(); });
    layout->addLayout(controls);

    connect(source_, &SqlEditor::textChanged, this, [this] {
        diff_->setPlainText(diffText(liveSource_, source_->sql_text()));
        if (!busy_) work(false);
    });
    if (routine_.isEmpty())
    {
        putTemplate(RoutineKind::function);
        updateDescriptor();
    }
    else loadSource();
}

QPushButton* RoutineStudio::action(QLayout* row, const char* name, const QString& label, std::function<void()> handler)
{
    auto* button = new QPushButton(label);
    button->setObjectName(name);
    row->addWidget(button);
    connect(button, &QPushButton::clicked, this, std::move(handler));
    return button;
}

void RoutineStudio::putTemplate(RoutineKind kind)
{
    if (schema_->currentIndex() < 0 || !routine_.isEmpty()) return;
    const auto schema = schema_->currentData().toStringList();
    if (kind == RoutineKind::function)
    {
        const auto name = sqlQualifiedIdentifier(schema + QStringList{"new_function"});
        source_->setPlainText(QStringLiteral("CREATE FUNCTION %1(value IN BIGINT) RETURN BIGINT AS\nBEGIN\n    RETURN value;\nEND;\n").arg(name));
    }
    else if (kind == RoutineKind::procedure)
    {
        const auto name = sqlQualifiedIdentifier(schema + QStringList{"new_procedure"});
        source_->setPlainText(QStringLiteral("CREATE PROCEDURE %1(value IN BIGINT, result OUT BIGINT) AS\nBEGIN\n    result := value;\nEND;\n").arg(name));
    }
    else
    {
        const auto name = sqlQualifiedIdentifier(schema + QStringList{"new_trigger"});
        source_->setPlainText(QStringLiteral("CREATE TRIGGER %1 BEFORE INSERT ON table_name FOR EACH ROW\nBEGIN\n    RAISE error_name;\nEND;\n").arg(name));
    }
}

void RoutineStudio::work(bool busy)
{
    busy_ = busy;
    const bool readOnly = session_.profile().read_only;
    const bool compiled = liveHeader_.has_value() && liveHeader_->kind != RoutineKind::trigger &&
        source_->sql_text() == liveSource_;
    const bool editsAllowed = !readOnly && !unsafeOutcome_ && (routine_.isEmpty() || liveHeader_.has_value());
    source_->setReadOnly(busy || !editsAllowed);
    compile_->setEnabled(!busy && editsAllowed && session_.state().connected);
    run_->setEnabled(!busy && compiled && !unsafeOutcome_ && session_.state().connected &&
        (!readOnly || liveHeader_->kind == RoutineKind::function));
    debugButton_->setEnabled(!busy && compiled && debug_ && !unsafeOutcome_ &&
        session_.capabilities().value("plsql_debug_v1").toBool());
    addTest_->setEnabled(!busy && !unsafeOutcome_ && !readOnly && compiled);
    runTests_->setEnabled(!busy && !unsafeOutcome_ && !readOnly && compiled &&
        !tests_.isEmpty() && session_.profile().environment == u"test");
    inputs_->setEnabled(!busy && compiled);
}

void RoutineStudio::loadSource()
{
    work(true);
    const QPointer<RoutineStudio> self(this);
    readDefinition(session_, routine_, [self](SqlDefinition definition) {
        if (!self) return;
        if (!definition.error.isEmpty())
        {
            self->updateDescriptor();
            self->status(QObject::tr("Source unavailable. %1").arg(definition.error));
            self->work(false);
            return;
        }
        self->source_->setPlainText(definition.text);
        self->liveSource_ = self->source_->sql_text();
        self->liveHeader_ = header(self->liveSource_);
        self->definitionRevision_ = std::move(definition.revision);
        self->updateDescriptor();
        self->status(QObject::tr("Loaded the committed source."));
        self->work(false);
    });
}

void RoutineStudio::updateDescriptor()
{
    const auto name = nameParts(routine_.value("qualified_name")).join(u'.');
    const auto version = routine_.value("schema_version").toInteger();
    if (routine_.isEmpty()) descriptor_->setText(tr("New routine"));
    else descriptor_->setText(tr("%1 · schema revision %2%3").arg(name).arg(version).arg(
        definitionRevision_.isEmpty() ? QString{} : tr(" · History revision %1").arg(definitionRevision_)));
    diff_->setPlainText(diffText(liveSource_, source_->sql_text()));
    dependencies_->clear();
    const auto ids = routine_.value("depends_on").toArray();
    for (const auto entry : catalog_)
    {
        const auto object = entry.toObject();
        if (!ids.contains(object.value("id"))) continue;
        new QTreeWidgetItem(dependencies_, {nameParts(object.value("qualified_name")).join(u'.'),
            QString::number(object.value("schema_version").toInteger())});
    }
    dependencies_->resizeColumnToContents(0);
    inputs_->setRowCount(0);
    if (!liveHeader_ || liveHeader_->kind == RoutineKind::trigger) return;
    const auto& parameters = liveHeader_->parameters;
    inputs_->setRowCount(static_cast<int>(parameters.size()));
    for (int row = 0; row < parameters.size(); ++row)
    {
        const auto& parameter = parameters.at(row);
        const QStringList fields{parameter.name, parameter.mode, parameter.displayType};
        for (int column = 0; column < fields.size(); ++column)
        {
            auto* item = new QTableWidgetItem(fields.at(column));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            inputs_->setItem(row, column, item);
        }
        auto* value = new QTableWidgetItem;
        auto* null = new QTableWidgetItem;
        if (parameter.mode == u"OUT")
        {
            value->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            null->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        }
        else
        {
            null->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            null->setCheckState(Qt::Unchecked);
        }
        inputs_->setItem(row, 3, value);
        inputs_->setItem(row, 4, null);
    }
}
QString RoutineStudio::errorText(const SqlReply& reply) const
{
    auto error = wb::status(reply.error.code);
    const auto message = QString::fromUtf8(reply.error.message);
    if (!message.isEmpty()) error += QStringLiteral(" · ") + message;
    if (reply.error.line && reply.error.column)
        error += tr(" · Line %1, column %2").arg(reply.error.line).arg(reply.error.column);
    if (reply.error.unknown_outcome)
        error += tr("\nThe outcome is unknown. Verify the request token before retrying.\n%1").arg(reply.requestToken);
    return error;
}

void RoutineStudio::compile()
{
    if (busy_ || unsafeOutcome_ || session_.profile().read_only) return;
    if (!session_.state().connected || session_.busy() || session_.state().transactionActive)
    {
        status(tr("Finish the worksheet operation or transaction before compiling."));
        return;
    }
    auto source = source_->sql_text();
    auto target = header(source);
    if (!target || source.toUtf8().size() > 64 * 1024)
    {
        tabs_->setCurrentWidget(source_);
        source_->goto_error(0, 1, 1);
        status(tr("Enter one CREATE FUNCTION, PROCEDURE or TRIGGER declaration with a supported signature."));
        return;
    }
    const auto previous = routine_;
    if (previous.isEmpty() && target->replace)
    {
        status(tr("Open the existing routine to compile a replacement."));
        return;
    }
    if (!previous.isEmpty())
    {
        if (!liveHeader_ || target->name != nameParts(previous.value("qualified_name")) || target->kind != liveHeader_->kind)
        {
            status(tr("The edited declaration must retain this routine’s qualified name and kind."));
            return;
        }
        if (target->kind != RoutineKind::trigger && !target->replace)
        {
            source.insert(target->createEnd, QStringLiteral(" OR REPLACE"));
            source_->setPlainText(source);
        }
    }

    SqlStatement definition{source};
    QVector<SqlStatement> commands;
    if (!previous.isEmpty() && target->kind == RoutineKind::trigger)
    {
        if (QMessageBox::question(this, tr("Recompile trigger"),
                tr("Replace this trigger in one transaction? A successful replacement creates a new trigger identity."),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        commands.push_back({QStringLiteral("DROP TRIGGER ") + sqlQualifiedIdentifier(target->name)});
    }
    commands.push_back(std::move(definition));
    work(true);
    status(tr("Compiling on the server…"));
    const QPointer<RoutineStudio> self(this);
    if (!session_.transaction(SqlTransaction::begin, {}, [self, target = *target, previous, commands](const SqlReply& begun) {
        if (!self) return;
        if (begun.error.code != SDP_OK) { self->discardCompile(begun, false); return; }
        if (self->session_.refreshMetadata([self, target, previous, commands](const SqlReply& metadata) {
            if (!self) return;
            if (metadata.error.code != SDP_OK) { self->discardCompile(metadata, true); return; }
            const auto catalog = metadata.data.value("objects").toArray();
            const auto current = findRoutine(catalog, target.name);
            if ((previous.isEmpty() && !current.isEmpty()) ||
                (!previous.isEmpty() && (current.value("id") != previous.value("id") ||
                    current.value("schema_version") != previous.value("schema_version"))))
            {
                SqlReply stale;
                stale.error.code = SDP_INVALID_STATE;
                qstrncpy(stale.error.message, "Routine identity or revision changed. Reopen the committed source.", sizeof(stale.error.message));
                self->discardCompile(stale, true);
                return;
            }
            if (self->session_.execute(commands, {}, {}, {1, 15000}, {}, [self, target](const SqlReply& executed) {
                if (!self) return;
                if (executed.error.code != SDP_OK) { self->discardCompile(executed, true); return; }
                if (self->session_.transaction(SqlTransaction::commit, {}, [self, target](const SqlReply& committed) {
                    if (!self) return;
                    if (committed.error.code != SDP_OK) { self->discardCompile(committed, true); return; }
                    self->compilationCommitted(target);
                })) return;
                SqlReply unavailable;
                unavailable.error.code = SDP_INVALID_STATE;
                self->discardCompile(unavailable, true);
            })) return;
            SqlReply unavailable;
            unavailable.error.code = SDP_INVALID_STATE;
            self->discardCompile(unavailable, true);
        })) return;
        SqlReply unavailable;
        unavailable.error.code = SDP_INVALID_STATE;
        self->discardCompile(unavailable, true);
    }))
    {
        status(tr("The compilation transaction could not start."));
        work(false);
    }
}

void RoutineStudio::discardCompile(const SqlReply& reply, bool rollback)
{
    if (reply.error.unknown_outcome) unsafeOutcome_ = true;
    const auto message = errorText(reply);
    if (reply.error.line && reply.error.column)
    {
        tabs_->setCurrentWidget(source_);
        source_->goto_error(0, static_cast<int>(reply.error.line), static_cast<int>(reply.error.column));
    }
    if (rollback && session_.state().transactionActive)
    {
        const QPointer<RoutineStudio> self(this);
        if (session_.transaction(SqlTransaction::rollback, {}, [self, message](const SqlReply& result) {
            if (!self) return;
            self->status(message + (result.error.code == SDP_OK ? QObject::tr("\nCompilation transaction rolled back.")
                : QObject::tr("\nRollback could not be confirmed. %1").arg(self->errorText(result))));
            self->work(false);
        })) return;
    }
    status(message);
    work(false);
}

void RoutineStudio::compilationCommitted(const RoutineHeader& target)
{
    const QPointer<RoutineStudio> self(this);
    if (session_.refreshMetadata([self, target](const SqlReply& refreshed) {
        if (!self) return;
        if (refreshed.error.code == SDP_OK)
        {
            self->catalog_ = refreshed.data.value("objects").toArray();
            self->routine_ = findRoutine(self->catalog_, target.name);
        }
        if (refreshed.error.code != SDP_OK || self->routine_.isEmpty())
        {
            self->unsafeOutcome_ = true;
            self->status(QObject::tr("The server committed the routine. Reopen it to refresh its identity and revision."));
            self->work(false);
            return;
        }
        self->liveSource_ = self->source_->sql_text();
        self->liveHeader_ = target;
        self->definitionRevision_.clear();
        self->updateDescriptor();
        self->status(QObject::tr("Routine compiled."));
        self->work(false);
        if (self->refresh_) self->refresh_();
    })) return;
    unsafeOutcome_ = true;
    status(tr("The server committed the routine. Reopen it to refresh its identity and revision."));
    work(false);
}

std::optional<Invocation> RoutineStudio::invocation() const
{
    if (!liveHeader_ || liveHeader_->kind == RoutineKind::trigger) return {};
    Invocation result;
    QStringList arguments;
    const auto& parameters = liveHeader_->parameters;
    for (int row = 0; row < parameters.size(); ++row)
    {
        const auto& parameter = parameters.at(row);
        if (parameter.mode == u"OUT") continue;
        const auto placeholder = QStringLiteral("p%1").arg(row + 1);
        const auto cell = typedValue(parameter, inputs_->item(row, 3)->text(), inputs_->item(row, 4)->checkState() == Qt::Checked);
        if (cell.isEmpty()) return {};
        arguments.push_back(u':' + placeholder);
        result.types.insert(placeholder, parameter.type);
        result.values.insert(placeholder, cell);
    }
    const auto prefix = liveHeader_->kind == RoutineKind::function ? QStringLiteral("SELECT ") : QStringLiteral("CALL ");
    result.sql = prefix + sqlQualifiedIdentifier(liveHeader_->name) + u'(' + arguments.join(u',') + u')';
    return result;
}

void RoutineStudio::run()
{
    if (busy_ || unsafeOutcome_ || !liveHeader_ || source_->sql_text() != liveSource_) return;
    if (session_.busy())
    {
        status(tr("Finish the worksheet operation before running the routine."));
        return;
    }
    const auto call = invocation();
    if (!call)
    {
        status(tr("Check the typed parameter values or select NULL."));
        return;
    }
    outputs_->clear();
    profiler_->clear();
    tabs_->setCurrentWidget(inputs_->parentWidget());
    work(true);
    status(tr("Running the compiled routine…"));
    const QPointer<RoutineStudio> self(this);
    if (session_.execute({{call->sql}}, call->types, call->values, {500, 15000}, [self](const SqlEvent& event) {
        if (self) self->outputs_->appendPlainText(jsonText(event.data));
    }, [self](const SqlReply& reply) {
        if (!self) return;
        if (reply.error.code != SDP_OK)
        {
            self->status(self->errorText(reply));
            self->work(false);
            return;
        }
        self->status(reply.resultsTruncated ? QObject::tr("Routine completed. Results limited to 500 rows.")
            : QObject::tr("Routine completed."));
        self->showProfiler();
    })) return;
    status(tr("The routine could not start."));
    work(false);
}

void RoutineStudio::showProfiler()
{
    const QPointer<RoutineStudio> self(this);
    if (session_.request(sdp_profile_last, [self](const SqlReply& reply) {
        if (!self) return;
        if (reply.error.code != SDP_OK)
        {
            self->status(QObject::tr("Routine completed. Profiler unavailable. %1").arg(self->errorText(reply)));
            self->work(false);
            return;
        }
        for (const auto entry : reply.data.value("samples").toArray())
        {
            const auto sample = entry.toObject();
            QStringList row{nameParts(sample.value("routine")).join(u'.')};
            for (const auto key : {"revision", "inclusive_us", "exclusive_us", "sql_us", "instructions"})
                row.push_back(QString::number(sample.value(QLatin1StringView(key)).toInteger()));
            new QTreeWidgetItem(self->profiler_, row);
        }
        self->work(false);
    })) return;
    work(false);
}

void RoutineStudio::addTest()
{
    if (busy_ || !liveHeader_ || source_->sql_text() != liveSource_) return;
    const auto call = invocation();
    const auto name = testName_->text().trimmed();
    const auto expectedText = expected_->toPlainText().toUtf8();
    const auto exception = expectedException_->text().trimmed();
    const auto expected = QJsonDocument::fromJson(expectedText);
    if (!call || name.isEmpty() || name.size() > 128 || exception.size() > 128 ||
        expectedText.size() > 24 * 1024 || !expected.isArray() || tests_.size() == 32)
    {
        status(tr("A test needs a name, valid typed inputs and a JSON array of expected sets."));
        return;
    }
    for (const auto& item : expected.array())
    {
        const auto set = item.toObject();
        if (set.size() != 2 || !set.value("name").isString() || !set.value("rows").isArray())
        {
            status(tr("Each expected set needs a name and a rows array."));
            return;
        }
    }
    if (!exception.isEmpty() && !expected.array().isEmpty())
    {
        status(tr("Use an empty result set array for an expected exception."));
        return;
    }
    const auto answer = exception.isEmpty() ? QJsonObject{{"sets", expected.array()}} :
        QJsonObject{{"exception", exception}};
    tests_.push_back({name, *call, answer});
    cases_->addItem(name);
    testName_->setText(tr("Test %1").arg(tests_.size() + 1));
    status(tr("Added %1 to the test suite.").arg(name));
    work(false);
}

void RoutineStudio::runTestCases(qsizetype index)
{
    if (index == tests_.size())
    {
        status(tr("Test suite finished."));
        work(false);
        return;
    }
    const auto test = tests_.at(index);
    const QPointer<RoutineStudio> self(this);
    if (session_.request([test](auto handle, auto* output, auto* error) { return runTest(handle, test, output, error); },
        [self, index, test](const SqlReply& reply) {
            if (!self) return;
            auto result = reply.data;
            result.insert("test", test.name);
            if (reply.error.code != SDP_OK)
            {
                result.insert("error", self->errorText(reply));
                self->testResults_->appendPlainText(jsonText(result));
                self->status(QObject::tr("Test suite stopped. %1").arg(self->errorText(reply)));
                self->work(false);
                return;
            }
            self->testResults_->appendPlainText(jsonText(result));
            self->runTestCases(index + 1);
        }, 12000)) return;
    status(tr("The next test could not start."));
    work(false);
}
}

QDialog* showRoutineStudio(SqlSession& session, const QJsonArray& catalog, const QJsonObject& routine,
    QWidget* parent, std::function<void()> refresh, std::function<void(const QJsonObject&)> debug)
{
    auto* studio = new RoutineStudio(session, catalog, routine, parent, std::move(refresh), std::move(debug));
    studio->show();
    return studio;
}
}
