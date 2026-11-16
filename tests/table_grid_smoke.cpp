#include "data_grid.hpp"
#include "sql_session.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableView>
#include <QTextDocument>
#include <QThread>
#include <QThreadPool>
#include <QTimer>

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace
{
void require(bool success, const char* message)
{
    if (!success) throw std::runtime_error(message);
}

template <class Predicate>
void waitUntil(Predicate ready, const char* message)
{
    QElapsedTimer clock;
    clock.start();
    while (!ready() && clock.elapsed() < 12000)
    {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    require(ready(), message);
}

template <class Control>
Control& named(QObject& parent, const char* name)
{
    auto* widget = parent.findChild<Control*>(QString::fromLatin1(name));
    require(widget != nullptr, name);
    return *widget;
}

wb::SqlReply finish(bool started, bool& done, wb::SqlReply& reply)
{
    require(started, "SQL request did not start");
    waitUntil([&] { return done; }, "SQL request did not finish");
    if (reply.error.code != SDP_OK)
        throw std::runtime_error(std::string("SQL fixture failed: ") + reply.error.message);
    return std::move(reply);
}

struct Result
{
    wb::SqlReply reply;
    QJsonArray rows;
};

Result execute(wb::SqlSession& session, QString text)
{
    Result result;
    bool done = false;
    const auto started = session.execute({{std::move(text)}}, {}, {}, {512, 5000},
        [&result](const wb::SqlEvent& event) {
            if (event.kind == wb::SqlEvent::Kind::batch)
                for (const auto& row : event.data.value("rows").toArray()) result.rows.append(row);
        }, [&result, &done](const auto& reply) { result.reply = reply; done = true; });
    auto reply = finish(started, done, result.reply);
    result.reply = std::move(reply);
    return result;
}

QJsonObject table(wb::SqlSession& session, const QString& name)
{
    bool done = false;
    wb::SqlReply response;
    const auto started = session.refreshMetadata([&](const auto& reply) { response = reply; done = true; });
    for (const auto& object : finish(started, done, response).data.value("objects").toArray())
    {
        const auto fields = object.toObject();
        const auto qualified = fields.value("qualified_name").toArray();
        if (fields.value("kind").toInt() == 3 && !qualified.isEmpty() && qualified.last().toString() == name)
            return fields;
    }
    throw std::runtime_error("Table metadata missing");
}

quint64 history(wb::SqlSession& session)
{
    wb::SqlReply response;
    bool done = false;
    const auto started = session.request([](auto connection, auto* output, auto* error) {
        return sdp_history_command(connection, SDP_HISTORY_STATUS, {"{}", 2}, output, error);
    }, [&](const auto& reply) { response = reply; done = true; });
    return finish(started, done, response).data.value("current_revision").toString().toULongLong();
}

void click(QDialog& grid, const char* action)
{
    auto& button = named<QPushButton>(grid, action);
    waitUntil([&] { return button.isEnabled(); }, action);
    button.click();
}

QString status(QDialog& grid)
{
    return named<QLabel>(grid, "gridStatus").text();
}

void waitGrid(QDialog& grid, const QString& expected)
{
    waitUntil([&] { return named<QPushButton>(grid, "gridReload").isEnabled() ||
        status(grid).contains(expected); }, "Grid operation did not finish");
    if (!status(grid).contains(expected)) throw std::runtime_error(status(grid).toStdString());
}

void rowForm(QDialog& grid, const char* action,
    const QList<std::pair<int, std::optional<QString>>>& values)
{
    auto& button = named<QPushButton>(grid, action);
    waitUntil([&] { return button.isEnabled(); }, action);
    bool accepted = false;
    QTimer::singleShot(0, &grid, [&] {
        auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!modal || modal->objectName() != "gridRowForm") return;
        for (const auto& [index, value] : values)
        {
            auto* supplied = modal->findChild<QCheckBox*>(QStringLiteral("gridCellSupplied%1").arg(index));
            auto* null = modal->findChild<QCheckBox*>(QStringLiteral("gridCellNull%1").arg(index));
            auto* input = modal->findChild<QPlainTextEdit*>(QStringLiteral("gridCell%1").arg(index));
            if (!supplied || !null || !input) { modal->reject(); return; }
            supplied->setChecked(true);
            null->setChecked(!value.has_value());
            if (value)
            {
                input->setPlainText(*value);
                input->document()->setModified(true);
            }
        }
        if (auto* buttons = modal->findChild<QDialogButtonBox*>())
            buttons->button(QDialogButtonBox::Ok)->click();
        accepted = modal->result() == QDialog::Accepted;
        if (!accepted) modal->reject();
    });
    button.click();
    require(accepted, "Typed row form did not stage values");
}

void requireCommitted(QDialog& grid)
{
    click(grid, "gridApply");
    waitGrid(grid, QStringLiteral("Committed"));
    require(named<QListWidget>(grid, "gridPendingChanges").count() == 0, "Committed changes remain pending");
}

QByteArray exportFile(QDialog& grid, const char* action, const QString& fileName)
{
    const auto path = QDir::current().absoluteFilePath(fileName);
    auto& button = named<QPushButton>(grid, action);
    waitUntil([&] { return button.isEnabled(); }, action);
    QTimer::singleShot(0, &grid, [&] {
        if (auto* file = qobject_cast<QFileDialog*>(QApplication::activeModalWidget()))
        {
            file->selectFile(path);
            static_cast<QDialog*>(file)->accept();
        }
    });
    button.click();
    QFile output(path);
    require(output.open(QIODevice::ReadOnly), "Grid export file was not produced");
    return output.readAll();
}

QJsonArray find(wb::SqlSession& session, qint64 id)
{
    const auto answer = execute(session, QStringLiteral("SELECT * FROM grid_items WHERE id = %1").arg(id));
    return answer.rows.size() == 1 ? answer.rows.first().toArray() : QJsonArray{};
}

void exercise(wb::Profile profile, wb::Password& password)
{
    auto connected = wb::connect(profile, password, false, false);
    auto otherConnection = wb::connect(profile, password, false, false);
    if (!connected.session || !otherConnection.session)
        throw std::runtime_error((QStringLiteral("Could not open two TLS sessions: %1 / %2")
            .arg(connected.error, otherConnection.error)).toStdString());
    QThreadPool workers;
    wb::SqlSession session(profile, std::move(connected.session), workers);
    wb::SqlSession other(profile, std::move(otherConnection.session), workers);
    execute(session, QStringLiteral("CREATE TABLE grid_items (id BIGINT PRIMARY KEY, note TEXT, "
        "amount DECIMAL(12,2), payload BLOB)"));
    execute(session, QStringLiteral("CREATE TABLE grid_no_key (id BIGINT, note TEXT)"));
    execute(session, QStringLiteral("INSERT INTO grid_no_key VALUES (1,'read only')"));
    const auto target = table(session, QStringLiteral("grid_items"));
    require(!target.value("row_locator").toObject().isEmpty() &&
        !target.value("data_revision").toString().isEmpty(), "Grid write metadata not published");
    auto before = history(session);
    require(before != 0, "Could not read History revision");

    QWidget owner;
    owner.show();
    auto& grid = *wb::openDataGrid(session, target, &owner);
    waitGrid(grid, QStringLiteral("Rows loaded."));
    auto& view = named<QTableView>(grid, "gridRows");
    const auto& pending = named<QListWidget>(grid, "gridPendingChanges");
    rowForm(grid, "gridInsert", {{0, QStringLiteral("184")}, {1, QStringLiteral("İzmir Şğü")},
        {2, QStringLiteral("-12.40")}, {3, QStringLiteral("000aff7f")}});
    require(pending.count() == 1 && find(other, 184).isEmpty(), "Pending insert was visible before commit");
    requireCommitted(grid);
    const QJsonArray inserted = find(other, 184);
    require(inserted.size() == 4 && inserted.at(1).toObject().value("v").toString() == QStringLiteral("İzmir Şğü") &&
        inserted.at(2).toObject().value("unscaled") == "-1240" &&
        inserted.at(3).toObject().value("hex") == "000aff7f", "Typed insert changed text, decimal or BLOB");
    require(history(other) > before, "Grid commit did not create a History revision");
    click(grid, "gridReload");
    waitGrid(grid, QStringLiteral("Rows loaded."));
    view.selectRow(0);
    view.setCurrentIndex(view.model()->index(0, 3));
    require(exportFile(grid, "gridExportBlob", QStringLiteral("grid-blob.bin")) == QByteArray::fromHex("000aff7f"),
        "Binary export changed the original bytes");
    const auto page = exportFile(grid, "gridExportPage", QStringLiteral("grid-page.json"));
    require(!QJsonDocument::fromJson(page).isNull() && page.contains("000aff7f"), "Typed page export lost the row");
    for (const auto* action : {"gridCopyInsert", "gridCopyUpdate"})
    {
        click(grid, action);
        auto& preview = named<QDialog>(grid, "gridCopiedQuery");
        const auto query = named<QPlainTextEdit>(preview, "gridCopySqlText").toPlainText();
        const auto bindings = named<QPlainTextEdit>(preview, "gridCopyBindText").toPlainText();
        require(query.contains(":grid_p") && !query.contains(QStringLiteral("İzmir")) &&
            bindings.contains(QStringLiteral("İzmir Şğü")), "Copied SQL does not keep values as separate binds");
        preview.reject();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    view.selectRow(0);
    rowForm(grid, "gridEditRow", {{1, QStringLiteral("")}, {2, std::nullopt}});
    requireCommitted(grid);
    const auto edited = find(other, 184);
    require(edited.size() == 4 && edited.at(1).toObject().value("t") == "text" && edited.at(1).toObject().value("v") == "" &&
        edited.at(2).toObject().value("t") == "null", "Empty text and NULL were confused");

    auto& filter = named<QComboBox>(grid, "gridFilterColumn");
    filter.setCurrentIndex(filter.findData(0));
    named<QLineEdit>(grid, "gridFilterValue").setText(QStringLiteral("184"));
    click(grid, "gridReload");
    waitGrid(grid, QStringLiteral("Rows loaded."));
    require(view.model()->rowCount() == 1, "Prepared filter failed");

    // The first insert succeeds inside the private transaction. The duplicate must roll it back.
    rowForm(grid, "gridInsert", {{0, QStringLiteral("185")}});
    rowForm(grid, "gridInsert", {{0, QStringLiteral("184")}});
    click(grid, "gridApply");
    waitUntil([&] { return !session.busy() && status(grid).contains("rolled back"); }, "Duplicate did not roll back");
    require(pending.count() == 2 && find(other, 185).isEmpty(), "Failed batch left a committed partial insert");
    click(grid, "gridDiscard");
    click(grid, "gridDiscard");

    view.selectRow(0);
    rowForm(grid, "gridEditRow", {{1, QStringLiteral("Stale change")}});
    execute(other, QStringLiteral("UPDATE grid_items SET note = 'Concurrent' WHERE id = 184"));
    before = history(other);
    click(grid, "gridApply");
    waitUntil([&] { return !session.busy() && status(grid).contains("Conflict:"); }, "Stale update did not conflict");
    require(find(other, 184).at(1).toObject().value("v") == "Concurrent" && history(other) == before,
        "Conflicting write changed the row or History");
    click(grid, "gridDiscard");
    click(grid, "gridReload");
    waitGrid(grid, QStringLiteral("Rows loaded."));
    view.selectRow(0);
    click(grid, "gridDelete");
    requireCommitted(grid);
    require(find(other, 184).isEmpty(), "Grid did not delete exactly the selected row");
    grid.reject();

    auto& noKey = *wb::openDataGrid(session, table(session, QStringLiteral("grid_no_key")), &owner);
    waitGrid(noKey, QStringLiteral("read-only"));
    named<QTableView>(noKey, "gridRows").selectRow(0);
    require(!named<QPushButton>(noKey, "gridEditRow").isEnabled() &&
        !named<QPushButton>(noKey, "gridDelete").isEnabled() &&
        !named<QPushButton>(noKey, "gridInsert").isEnabled(), "No-key table allowed editing");
    noKey.reject();

    execute(session, QStringLiteral("INSERT INTO grid_items (id) VALUES (1)"));
    auto& paging = *wb::openDataGrid(session, table(session, QStringLiteral("grid_items")), &owner);
    waitGrid(paging, QStringLiteral("Rows loaded."));
    QTimer::singleShot(0, &paging, [&] {
        if (auto* amount = qobject_cast<QInputDialog*>(QApplication::activeModalWidget()))
        {
            amount->setIntValue(64);
            amount->accept();
        }
    });
    click(paging, "gridGenerate");
    require(named<QListWidget>(paging, "gridPendingChanges").count() == 64,
        "Sample generator did not stage the requested values");
    requireCommitted(paging);
    auto& sorting = named<QComboBox>(paging, "gridSortColumn");
    sorting.setCurrentIndex(sorting.findData(0));
    click(paging, "gridReload");
    waitGrid(paging, QStringLiteral("Rows loaded."));
    const auto* model = named<QTableView>(paging, "gridRows").model();
    require(model->rowCount() == 64 && model->index(0, 0).data().toString() == "1", "First sorted page is invalid");
    click(paging, "gridNext");
    require(model->rowCount() == 1 && model->index(0, 0).data().toString() == "1000064",
        "Second sorted page is invalid");
    paging.reject();
}
}

int main(int argc, char** argv)
{
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    try
    {
        require(app.arguments().size() == 2, "Expected TLS fixture profile path");
        QString error;
        const auto profiles = wb::import_profiles(app.arguments().last(), error);
        require(profiles.size() == 1, "Invalid TLS fixture profile");
        std::string input;
        std::getline(std::cin, input);
        wb::Password password{QByteArray::fromStdString(input)};
        input.assign(input.size(), '\0');
        exercise(profiles.first(), password);
        std::cout << "Workbench grid: typed CRUD, rollback, stale conflict, History, no-key read-only, "
            "binary/page exports, parameterized copy, synthetic rows, sorting and paging verified\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

