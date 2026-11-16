#include "decision_studio.hpp"
#include "history_decision_smoke_support.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QThreadPool>
#include <QTreeWidget>

using namespace studio_smoke;
using namespace wb;

namespace
{
constexpr auto table = "wb_decision_entries";

QJsonObject report(QDialog& studio)
{
    return QJsonDocument::fromJson(control<QPlainTextEdit>(studio, "decisionResult").toPlainText().toUtf8()).object();
}

template<class Predicate>
QJsonObject expectReport(QDialog& studio, Predicate matches, const char* description)
{
    if (!waitFor([&] { return matches(report(studio)); }))
        std::cerr << control<QLabel>(studio, "decisionStatus").text().toStdString() << '\n';
    const auto result = report(studio);
    require(matches(result), description);
    return result;
}

void scenario(QDialog& studio, const QString& rowId, int amount)
{
    auto& event = control<QComboBox>(studio, "decisionEvent");
    const auto update = event.findData(QStringLiteral("update"));
    require(update >= 0, "Decision scenario supports the update event");
    event.setCurrentIndex(update);
    control<QLineEdit>(studio, "decisionRowId").setText(rowId);
    const QJsonObject proposal{{"amount", integer(amount)}};
    control<QPlainTextEdit>(studio, "decisionProposed").setPlainText(QString::fromUtf8(QJsonDocument(proposal).toJson()));
}

QString nodeState(const QJsonObject& result, QStringView operation)
{
    for (const auto rule : result.value("rules").toArray())
        for (const auto node : rule.toObject().value("nodes").toArray())
            if (node.toObject().value("op").toString() == operation)
                return node.toObject().value("state").toString();
    return {};
}

bool stateInTree(const QTreeWidgetItem& item, QStringView operation, QStringView state)
{
    bool operationMatch = false;
    bool stateMatch = false;
    for (int column = 0; column < item.columnCount(); ++column)
    {
        const auto value = item.text(column);
        operationMatch |= value.contains(operation, Qt::CaseInsensitive);
        stateMatch |= value.compare(state, Qt::CaseInsensitive) == 0;
    }
    if (operationMatch && stateMatch) return true;
    for (int row = 0; row < item.childCount(); ++row)
        if (stateInTree(*item.child(row), operation, state)) return true;
    return false;
}

bool traceVisible(const QTreeWidget& tree, QStringView operation, QStringView state)
{
    for (int row = 0; row < tree.topLevelItemCount(); ++row)
        if (stateInTree(*tree.topLevelItem(row), operation, state)) return true;
    return false;
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    const auto profile = fixtureProfile(argc, argv);
    Password administrator{fixturePassword()};
    QThreadPool workers;
    SqlSession session(profile, connect(profile, administrator), workers);
    write(session, "CREATE TABLE wb_decision_entries (id BIGINT PRIMARY KEY, amount BIGINT, doubled BIGINT)");
    write(session, "CREATE TABLE wb_decision_permission (id BIGINT PRIMARY KEY)");
    write(session, "CREATE TABLE wb_decision_audit (id BIGINT PRIMARY KEY)");
    write(session, "INSERT INTO wb_decision_entries VALUES (1,2,0),(2,3,0)");
    const auto rows = success(history(session, SDP_HISTORY_READ, {{"target", table}, {"revision", head(session)}}),
        "Decision fixture row identities").data.value("rows").toArray();
    require(rows.size() == 2, "Decision fixture rows are present");
    QString first, second;
    for (const auto row : rows)
    {
        const auto record = row.toObject();
        const auto id = record.value("values").toObject().value("id").toObject();
        if (id == integer(1)) first = record.value("row_id").toString();
        if (id == integer(2)) second = record.value("row_id").toString();
    }
    require(!first.isEmpty() && !second.isEmpty() && first != second, "Decision uses stable row IDs");
    write(session, "INSERT INTO wb_decision_permission VALUES (1)");
    write(session, "CREATE DECISION RULE wb_counter ON wb_decision_entries FOR UPDATE PRIORITY 30 "
                   "WHEN (NEW.amount > 0) THEN SET doubled = NEW.amount * 2");
    write(session, "CREATE DECISION RULE wb_guard ON wb_decision_entries FOR UPDATE PRIORITY 20 "
                   "WHEN (NEW.amount > 0 AND EXISTS (SELECT id FROM wb_decision_permission WHERE id = :NEW.id)) THEN ALLOW");
    write(session, "CREATE DECISION RULE wb_guard ON wb_decision_entries FOR UPDATE PRIORITY 20 "
                   "WHEN (NEW.amount > 10 AND EXISTS (SELECT id FROM wb_decision_permission WHERE id = :NEW.id)) THEN ALLOW");
    write(session, "CREATE TRIGGER wb_decision_updated AFTER UPDATE ON wb_decision_entries FOR EACH ROW "
                   "BEGIN INSERT INTO wb_decision_audit VALUES (:NEW.id); END;");
    const auto baseline = head(session);
    const auto actual = sql(session, "UPDATE wb_decision_entries SET amount=0 WHERE id=1");
    require(actual.error.code == SDP_PERMISSION_DENIED && QString::fromUtf8(actual.error.message).contains(u"(denied)"),
        "actual mutation is prevented by the active Decision rule");

    QWidget window;
    window.show();
    const auto objects = catalog(session);
    auto* studio = showDecisionStudio(session, objects, named(objects, u"wb_decision_entries"), &window);
    require(studio != nullptr, "Decision Studio opened");
    auto& tree = control<QTreeWidget>(*studio, "decisionConditionTree");
    scenario(*studio, first, 0);
    click(*studio, "decisionWhynot");
    const auto denied = expectReport(*studio, [](const QJsonObject& result) {
        return result.value("allowed").isBool() && !result.value("allowed").toBool() &&
            result.value("reason").toString() == u"denied" && nodeState(result, u"exists") == u"skipped";
    }, "WHYNOT reports the actual denied decision and skipped dependency");
    require(denied.value("revision").toString() == baseline && traceVisible(tree, u"exists", u"skipped"),
        "the condition tree renders the real skipped node at the current revision");

    scenario(*studio, second, 2);
    click(*studio, "decisionWhynot");
    const auto missing = expectReport(*studio, [](const QJsonObject& result) {
        return result.value("reason").toString() == u"denied" && nodeState(result, u"exists") == u"evaluated";
    }, "WHYNOT evaluates the dependency when the amount guard succeeds");
    require(missing.value("revision").toString() == baseline && traceVisible(tree, u"exists", u"evaluated"),
        "the condition tree renders the evaluated dependency from the same revision");

    scenario(*studio, first, 9);
    click(*studio, "decisionWhatif");
    const auto simulated = expectReport(*studio, [](const QJsonObject& result) {
        return result.value("allowed").toBool() && result.value("computed").toObject().value("doubled") == integer(18);
    }, "WHATIF shows the typed computation for an allowed proposed change");
    require(simulated.value("revision").toString() == baseline, "WHATIF retained the actual input revision");

    control<QLineEdit>(*studio, "decisionRuleName").setText("wb_guard");
    control<QSpinBox>(*studio, "decisionFromVersion").setValue(1);
    control<QSpinBox>(*studio, "decisionToVersion").setValue(2);
    const QJsonArray corpus{QJsonObject{{"event", "update"}, {"row_id", first},
        {"proposed", QJsonObject{{"amount", integer(7)}}}}};
    control<QPlainTextEdit>(*studio, "decisionCases").setPlainText(QString::fromUtf8(QJsonDocument(corpus).toJson()));
    click(*studio, "decisionCompare");
    const auto comparison = expectReport(*studio, [](const QJsonObject& result) {
        return result.value("changed").toInt() == 1 && result.value("cases").toArray().size() == 1;
    }, "comparison reports the impact of both rule versions");
    const auto values = comparison.value("cases").toArray().first().toObject();
    require(comparison.value("revision").toString() == baseline &&
        values.value("prior").toObject().value("allowed").toBool() &&
        values.value("candidate").toObject().value("allowed").isBool() &&
        !values.value("candidate").toObject().value("allowed").toBool(),
        "rule comparison uses one revision and shows the changed outcome");

    require(head(session) == baseline, "WHYNOT, WHATIF and version comparison created no History revision");
    require(select(session, "SELECT id,amount,doubled FROM wb_decision_entries ORDER BY id") ==
        QJsonArray{QJsonArray{integer(1), integer(2), integer(0)}, QJsonArray{integer(2), integer(3), integer(0)}} &&
        select(session, "SELECT id FROM wb_decision_audit").isEmpty(),
        "inspection changed no data and fired no trigger");
    std::cout << "S40-T03: live Decision denied writes, explained true traversal, simulated and compared without a change\n";
    return 0;
}
