#include "table_codec.hpp"
#include "table_transfer.hpp"
#include "sql_session.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QThreadPool>
#include <QTimer>

#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, const char* reason)
{
    if (!condition) throw std::runtime_error(reason);
}

template <class Widget>
Widget& control(QDialog& dialog, const char* name)
{
    auto* result = dialog.findChild<Widget*>(QString::fromLatin1(name));
    require(result, name);
    return *result;
}

template <class Predicate>
void waitFor(Predicate ready, const char* failure)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!ready() && elapsed.elapsed() < 12000)
    {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    require(ready(), failure);
}

QJsonArray query(wb::SqlSession& session, QString sql)
{
    QJsonArray rows;
    bool finished = false;
    wb::SqlReply result;
    require(session.execute({{std::move(sql)}}, {}, {}, {100, 5000}, [&](const wb::SqlEvent& event) {
        if (event.kind == wb::SqlEvent::Kind::batch)
            for (const auto& row : event.data.value("rows").toArray()) rows.append(row);
    }, [&](const auto& reply) { result = reply; finished = true; }), "SQL command failed to start");
    waitFor([&] { return finished; }, "SQL command did not finish");
    require(result.error.code == SDP_OK, result.error.message);
    return rows;
}

QJsonObject table(wb::SqlSession& session)
{
    bool finished = false;
    wb::SqlReply result;
    require(session.refreshMetadata([&](const auto& reply) {
        result = reply;
        finished = true;
    }), "Metadata command failed to start");
    waitFor([&] { return finished; }, "Metadata command did not finish");
    require(result.error.code == SDP_OK, result.error.message);
    for (const auto& value : result.data.value("objects").toArray())
    {
        const auto object = value.toObject();
        const auto name = object.value("qualified_name").toArray();
        if (!name.isEmpty() && name.last() == "transfer_items") return object;
    }
    throw std::runtime_error("Missing transfer table");
}

void writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Failed to write fixture file");
}

void handleImport(QDialog& dialog)
{
    QTimer::singleShot(0, &dialog, [] {
        if (auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            prompt->button(QMessageBox::Yes)->click();
    });
    control<QPushButton>(dialog, "applyTransfer").click();
}

void exercise(wb::SqlSession& session)
{
    using namespace wb::transfer;
    query(session, "CREATE TABLE transfer_items (id BIGINT PRIMARY KEY, note TEXT, rate DECIMAL(38,9), payload BLOB)");
    const auto metadata = table(session);
    const auto columns = metadata.value("columns").toArray();
    QWidget owner;
    owner.show();
    wb::showTableTransfer(session, metadata, &owner, {}, {});
    auto* dialog = owner.findChild<QDialog*>("tableTransfer");
    require(dialog, "Missing transfer dialog");
    auto& direction = control<QComboBox>(*dialog, "transferDirection");
    auto& format = control<QComboBox>(*dialog, "transferFormat");
    auto& path = control<QLineEdit>(*dialog, "transferFile");
    auto& preview = control<QPushButton>(*dialog, "previewTransfer");
    auto& apply = control<QPushButton>(*dialog, "applyTransfer");
    auto& skip = control<QCheckBox>(*dialog, "transferSkipRejected");
    auto& report = control<QPlainTextEdit>(*dialog, "transferReport");
    auto& status = control<QLabel>(*dialog, "transferStatus");
    const auto amount = QJsonObject{{"t", "decimal"}, {"unscaled", "-123456789000100000999"}, {"scale", 9}};
    const auto null = QJsonObject{{"t", "null"}};
    const QJsonArray row{QJsonObject{{"t", "i64"}, {"v", "9007199254740993"}},
        QJsonObject{{"t", "text"}, {"v", QStringLiteral("=İstanbul, şeker\n\"özel\"")}},
        amount, QJsonObject{{"t", "binary"}, {"hex", "00ff1a"}}};
    const QJsonArray nullRow{QJsonObject{{"t", "i64"}, {"v", "9007199254740994"}}, null, amount, null};
    const QJsonArray emptyRow{QJsonObject{{"t", "i64"}, {"v", "9007199254740995"}},
        QJsonObject{{"t", "text"}, {"v", ""}}, amount, QJsonObject{{"t", "binary"}, {"hex", ""}}};
    const QVector<QJsonArray> expected{row, nullRow, emptyRow};
    QString error;
    auto source = encode(Format::json, metadata, columns, expected, {}, error);
    require(error.isEmpty(), "Could not create typed JSON fixture");
    writeFile("incoming.json", source.toUtf8());
    direction.setCurrentIndex(1);
    format.setCurrentIndex(1);
    path.setText("incoming.json");
    preview.click();
    require(apply.isEnabled(), "Valid JSON preview was rejected");
    handleImport(*dialog);
    waitFor([&] { return status.text().contains("Committed 3"); }, "JSON import did not commit all three rows");
    const auto read = [&] { return query(session, "SELECT id, note, rate, payload FROM transfer_items ORDER BY id"); };
    require(read() == QJsonArray{row, nullRow, emptyRow}, "Server changed typed or NULL/empty values");

    direction.setCurrentIndex(0);
    format.setCurrentIndex(0);
    path.setText("export.csv");
    preview.click();
    require(apply.isEnabled(), "CSV export preview failed");
    apply.click();
    waitFor([&] { return !session.busy(); }, "CSV export did not finish");
    require(status.text().contains("Exported 3"), status.text().toUtf8().constData());
    QFile output("export.csv");
    require(output.open(QIODevice::ReadOnly), "CSV file was not written");
    const auto csv = QString::fromUtf8(output.readAll());
    require(csv.contains(QStringLiteral("\"'=İstanbul")), "Export failed to escape spreadsheet formula");
    Options options;
    options.spreadsheetEscaped = true;
    const auto exported = decode(Format::csv, csv, columns, options);
    require(exported.error.isEmpty() && exported.rejected == 0 && exported.rows.size() == 3,
        "Could not decode exported CSV");
    for (qsizetype i = 0; i < expected.size(); ++i)
        require(exported.rows.at(i).values == expected.at(i), "CSV changed an exact value");

    direction.setCurrentIndex(1);
    writeFile("rejected.csv", "id,note\n16,accepted\nnope,rejected\n");
    path.setText("rejected.csv");
    preview.click();
    require(!apply.isEnabled() && report.toPlainText().contains("1 rejected") &&
        report.toPlainText().contains("Row 3"), "Atomic preview did not show bad row and disable apply");
    skip.setChecked(true);
    preview.click();
    handleImport(*dialog);
    waitFor([&] { return status.text().contains("Committed 1") && status.text().contains("1 rejected"); },
        "Explicit skip did not commit one accepted row with reject count");
    require(read().size() == 4, "Skip mode committed wrong row count");

    skip.setChecked(false);
    QByteArray cancellations("id,note\n");
    for (int id = 100; id < 300; ++id)
        cancellations += QByteArray::number(id) + ",to cancel\n";
    writeFile("cancel.csv", cancellations);
    path.setText("cancel.csv");
    preview.click();
    require(apply.isEnabled(), "Cancellation preview was rejected");
    auto* progress = dialog->findChild<QProgressBar*>();
    require(progress, "Missing import progress");
    bool cancelledAfterWrite = false;
    QObject::connect(progress, &QProgressBar::valueChanged, dialog, [&](int value) {
        if (value != 1) return;
        cancelledAfterWrite = true;
        control<QPushButton>(*dialog, "cancelTransfer").click();
    });
    handleImport(*dialog);
    waitFor([&] { return status.text().contains("No rows were committed"); }, "Import cancellation did not roll back");
    require(cancelledAfterWrite && !session.state().transactionActive && read().size() == 4,
        "Cancellation left a transaction or a partial import");
}
}

int main(int argc, char** argv)
{
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
        const auto connected = wb::connect(profiles.first(), password, false, false, true);
        require(bool(connected.session), "Could not connect to the TLS fixture");
        QThreadPool workers;
        wb::SqlSession session(profiles.first(), connected.session, workers);
        exercise(session);
        std::cout << "Workbench transfer: exact Turkish/DECIMAL/NULL/BLOB round-trip, rejection and cancellation passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
