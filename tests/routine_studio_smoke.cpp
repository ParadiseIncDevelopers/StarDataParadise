#include "routine_debugger.hpp"
#include "routine_studio.hpp"
#include "sql_session.hpp"

#include <QApplication>
#include <QDialog>
#include <QEventLoop>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTableWidget>
#include <QThreadPool>
#include <QTimer>
#include <QTreeWidget>
#include <QVariant>

#include <algorithm>
#include <cstdlib>
#include <iostream>

using namespace wb;

namespace
{
void require(bool condition, const char* description)
{
    if (condition) return;
    std::cerr << description << '\n';
    std::exit(1);
}

template<class T>
T& widget(QObject& parent, const char* name)
{
    auto* found = parent.findChild<T*>(QString::fromLatin1(name));
    require(found != nullptr, name);
    return *found;
}

template<class Condition>
bool waitFor(Condition condition, int milliseconds = 10000)
{
    if (condition()) return true;
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (condition()) loop.quit();
    });
    poll.start(10);
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
    return condition();
}

void click(QObject& parent, const char* name)
{
    auto& button = widget<QPushButton>(parent, name);
    if (!button.isEnabled()) std::cerr << name << " is disabled\n";
    require(button.isEnabled(), "expected available routine action");
    button.click();
}

bool localValue(const QTableWidget& locals, QStringView name, QStringView value)
{
    for (int row = 0; row < locals.rowCount(); ++row)
    {
        const auto* part = locals.item(row, 0);
        const auto* cell = locals.item(row, 2);
        if (part && cell && part->text() == name && cell->text() == value) return true;
    }
    return false;
}

void expectPause(QDialog& debugger, int line, QStringView value)
{
    auto& stack = widget<QTableWidget>(debugger, "debugStack");
    const auto paused = [&] {
        return debugger.property("debugState").toString() == u"paused" && stack.rowCount() == 1 &&
            stack.item(0, 2) && stack.item(0, 2)->text() == QString::number(line);
    };
    if (!waitFor(paused, 2000))
        std::cerr << widget<QLabel>(debugger, "debugStatus").text().toStdString() << '\n';
    require(paused(), "actual breakpoint or step must pause at the expected source line");
    require(stack.item(0, 0)->text().contains(u"studio_increment") &&
        stack.item(0, 1)->text() == u"1",
        "server returned the matching call stack and source revision");
    require(localValue(widget<QTableWidget>(debugger, "debugLocals"), u"local_value", value),
        "server returned the local value for the current frame");
    require(!widget<QLabel>(debugger, "debugLease").text().isEmpty(),
        "debugger displays its lease status");
}

void expectWatch(QDialog& debugger, QStringView value)
{
    widget<QLineEdit>(debugger, "debugWatchName").setText("local_value");
    click(debugger, "debugWatch");
    auto& watch = widget<QPlainTextEdit>(debugger, "debugWatchValue");
    require(waitFor([&] {
        const auto detail = watch.toPlainText().trimmed();
        return detail.startsWith(u"local_value") && detail.endsWith(QStringLiteral("= %1").arg(value));
    }, 1000),
        "watched variable must match the real paused invocation");
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    require(argc == 2, "usage: routine Studio smoke test <test-profile.json>");
    QString reason;
    const auto profiles = import_profiles(QString::fromLocal8Bit(argv[1]), reason);
    require(profiles.size() == 1, "one isolated test profile");
    const auto& profile = profiles.first();
    std::string input;
    std::getline(std::cin, input);
    if (!input.empty() && input.back() == '\r') input.pop_back();
    Password password{QByteArray(input.data(), static_cast<qsizetype>(input.size()))};
    std::fill(input.begin(), input.end(), '\0');
    const auto login = wb::connect(profile, password, false, false);
    if (!login.session) std::cerr << login.error.toStdString() << '\n';
    require(bool(login.session), "TLS Workbench login");

    QThreadPool workers;
    SqlSession session(profile, login.session, workers);
    QWidget window;
    window.show();
    int refreshes = 0;
    QDialog* debugger = nullptr;
    QDialog* studio = showRoutineStudio(session, {}, {}, &window, [&] { ++refreshes; },
        [&](const QJsonObject& routine) {
            require(routine.value("schema_version").toInt() == 1, "compiled revision selected for debug");
            debugger = showRoutineDebugger(session, workers, routine, &window);
        });
    require(studio != nullptr, "PL/SQL Studio opened");
    widget<QPlainTextEdit>(*studio, "routineSource").setPlainText(
        "CREATE FUNCTION studio_increment(p_input IN BIGINT) RETURN BIGINT AS\n"
        "    local_value BIGINT := p_input;\n"
        "BEGIN\n"
        "    local_value := local_value + 2;\n"
        "    IF p_input < 0 THEN RAISE studio_negative; END IF;\n"
        "    RETURN local_value;\n"
        "END;");
    click(*studio, "routineCompile");
    auto& status = widget<QLabel>(*studio, "routineStatus");
    auto& inputs = widget<QTableWidget>(*studio, "routineInputs");
    auto& debug = widget<QPushButton>(*studio, "routineDebug");
    if (!waitFor([&] { return refreshes == 1 && debug.isEnabled() && inputs.rowCount() == 1; }))
        std::cerr << status.text().toStdString() << '\n';
    require(refreshes == 1 && debug.isEnabled() && inputs.rowCount() == 1,
        "compiled function refreshed its metadata and typed input form");
    require(inputs.item(0, 0)->text() == u"p_input" && inputs.item(0, 2)->text() == u"BIGINT",
        "the input form matches the compiled signature");
    inputs.item(0, 3)->setText("5");

    click(*studio, "routineDebug");
    require(debugger != nullptr, "routine debugger opened");
    widget<QLineEdit>(*debugger, "debugPassword").setText(QString::fromUtf8(password.bytes));
    click(*debugger, "debugAttach");
    const auto armed = [&] { return debugger->property("debugState").toString() == u"armed"; };
    if (!waitFor(armed))
        std::cerr << widget<QLabel>(*debugger, "debugStatus").text().toStdString() << '\n';
    require(armed(), "controller attached to the live target session");
    widget<QSpinBox>(*debugger, "debugLine").setValue(4);
    click(*debugger, "debugSetBreakpoint");
    require(waitFor([&] {
        const auto lines = debugger->property("debugBreakpointLines").toList();
        return std::ranges::any_of(lines, [](const QVariant& line) { return line.toInt() == 4; });
    }), "server acknowledged the source breakpoint");

    click(*studio, "routineRun");
    expectPause(*debugger, 4, u"5");
    expectWatch(*debugger, u"5");
    click(*debugger, "debugStepOver");
    expectPause(*debugger, 5, u"7");
    expectWatch(*debugger, u"7");
    click(*debugger, "debugContinue");
    require(waitFor([&] {
        return debugger->property("debugState").toString() == u"finished" && !session.busy();
    }), "resumed invocation finished");
    auto& profileTree = widget<QTreeWidget>(*studio, "routineProfiler");
    require(waitFor([&] { return profileTree.topLevelItemCount() > 0 && !session.busy(); }),
        "profiler received samples from the resumed invocation");
    const auto* sample = profileTree.topLevelItem(0);
    require(sample->text(0).contains(u"studio_increment") && sample->text(1) == u"1" && sample->text(5).toInt() >= 2,
        "profiler identifies the executed routine revision and processed instructions");
    const QRegularExpression returnedSeven(QStringLiteral(R"("v"\s*:\s*"7")"));
    require(returnedSeven.match(widget<QPlainTextEdit>(*studio, "routineOutputs").toPlainText()).hasMatch(),
        "the resumed function returned 7 in the Studio");
    debugger->close();

    widget<QPlainTextEdit>(*studio, "routineTestExpected").setPlainText(
        R"([{"name":"out","rows":[[{"t":"i64","v":"7"}]]}])");
    click(*studio, "routineAddTest");
    inputs.item(0, 3)->setText("-5");
    widget<QLineEdit>(*studio, "routineTestName").setText("Negative input");
    widget<QPlainTextEdit>(*studio, "routineTestExpected").setPlainText("[]");
    widget<QLineEdit>(*studio, "routineExpectedException").setText("studio_negative");
    click(*studio, "routineAddTest");
    click(*studio, "routineRunTests");
    auto& testResults = widget<QPlainTextEdit>(*studio, "routineTestResults");
    const QRegularExpression testPassed(QStringLiteral(R"("passed"\s*:\s*true)"));
    const QRegularExpression rolledBack(QStringLiteral(R"("rolled_back"\s*:\s*true)"));
    const auto testDone = [&] {
        const auto result = testResults.toPlainText();
        return result.count(testPassed) == 2 && result.count(rolledBack) == 2 && result.contains(u"studio_negative");
    };
    if (!waitFor(testDone)) std::cerr << testResults.toPlainText().toStdString() << '\n';
    require(testDone(), "routine tests verified output 7 and the expected exception, then rolled back");

    std::cout << "S39-T01: Qt Studio compiled the function, debugged live locals, profiled and tested output 7\n";
    return 0;
}
