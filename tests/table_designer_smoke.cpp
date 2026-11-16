#include "table_design.hpp"
#include "sql_session.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QThreadPool>
#include <QTimer>
#include <QTreeWidget>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <optional>

using namespace wb;

void require(bool accepted, const char* explanation)
{
    if (accepted) return;
    std::cerr << explanation << '\n';
    std::exit(1);
}

template<class Start>
SqlReply await(Start start)
{
    QEventLoop loop;
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    std::optional<SqlReply> received;
    require(start([&](SqlReply value) { received = std::move(value); loop.quit(); }), "request started");
    if (!received) loop.exec();
    require(received.has_value(), "request completed");
    if (received->error.code != SDP_OK) std::cerr << received->error.message << '\n';
    require(received->error.code == SDP_OK, "fixture request succeeded");
    return std::move(*received);
}

template<class T>
T& widget(QObject& parent, const char* name)
{
    auto* child = parent.findChild<T*>(QString::fromLatin1(name));
    require(child != nullptr, name);
    return *child;
}

void settled(SqlSession& session)
{
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QTimer poll;
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (!session.busy()) loop.quit(); });
    deadline.start(12000);
    poll.start(10);
    loop.exec();
    require(!session.busy(), "designer workflow completed");
}

QJsonObject locate(const QJsonArray& catalog, int kind, QStringView plainName)
{
    for (const auto entry : catalog)
    {
        const auto object = entry.toObject();
        const auto name = object.value("qualified_name").toArray();
        if (object.value("kind").toInt() == kind && !name.isEmpty() && name.last().toString() == plainName) return object;
    }
    return {};
}

void addConstraint(QDialog& designer, QString kind, QString name, QString columns, QString parent = {}, QString predicate = {})
{
    QTimer::singleShot(0, &designer, [&, kind, name, columns, parent, predicate] {
        auto& modal = widget<QDialog>(designer, "designerConstraintEditor");
        auto& choices = widget<QComboBox>(modal, "constraintKind");
        choices.setCurrentIndex(choices.findData(kind));
        widget<QLineEdit>(modal, "constraintName").setText(name);
        widget<QLineEdit>(modal, "constraintColumns").setText(columns);
        widget<QLineEdit>(modal, "constraintCheck").setText(predicate);
        if (!parent.isEmpty())
        {
            auto& targets = widget<QComboBox>(modal, "constraintReference");
            for (int index = 0; index < targets.count(); ++index)
            {
                if (targets.itemData(index).toStringList().last() != parent) continue;
                targets.setCurrentIndex(index);
                break;
            }
            require(targets.currentData().toStringList().last() == parent, "referenced table is present");
            widget<QLineEdit>(modal, "constraintReferenceColumns").setText(columns);
        }
        modal.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    widget<QPushButton>(designer, "designerAddConstraint").click();
}

void addIndex(QDialog& designer, QString name, QString columns)
{
    QTimer::singleShot(0, &designer, [&, name, columns] {
        auto& modal = widget<QDialog>(designer, "designerIndexEditor");
        widget<QLineEdit>(modal, "indexName").setText(name);
        widget<QLineEdit>(modal, "indexColumns").setText(columns);
        modal.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    widget<QPushButton>(designer, "designerAddIndex").click();
}

void apply(QDialog& designer, SqlSession& session, const QString& expected)
{
    auto& action = widget<QPushButton>(designer, "designerApply");
    if (!action.isEnabled()) std::cerr << widget<QLabel>(designer, "designerStatus").text().toStdString() << '\n';
    require(action.isEnabled(), "previewed plan is enabled");
    QTimer::singleShot(0, &designer, [&] {
        auto* message = designer.findChild<QMessageBox*>();
        require(message != nullptr, "confirmation opened");
        message->button(QMessageBox::Yes)->click();
    });
    action.click();
    settled(session);
    const auto status = widget<QLabel>(designer, "designerStatus").text();
    if (!status.contains(expected)) std::cerr << status.toStdString() << '\n';
    require(status.contains(expected), "expected designer result");
    require(!session.state().transactionActive, "workflow closes its own transaction");
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    require(argc == 2, "fixture profile filename");
    QString profileError;
    const auto profiles = import_profiles(QString::fromLocal8Bit(argv[1]), profileError);
    require(profiles.size() == 1, "one fixture profile loaded");
    const auto& profile = profiles.first();
    std::string line;
    std::getline(std::cin, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    Password password{QByteArray(line.data(), static_cast<qsizetype>(line.size()))};
    std::fill(line.begin(), line.end(), '\0');
    const auto first = wb::connect(profile, password, false, false);
    if (!first.session) std::cerr << first.error.toStdString() << '\n';
    require(bool(first.session), "first TLS login");
    const auto second = wb::connect(profile, password, false, false);
    require(bool(second.session), "second TLS login");

    QThreadPool workers;
    SqlSession session(profile, first.session, workers);
    SqlSession other(profile, second.session, workers);
    const auto sql = [](SqlSession& target, const QString& statement) {
        return await([&](auto done) { return target.execute({{statement}}, {}, {}, {}, {}, done); });
    };
    const auto catalog = [&] {
        return await([&](auto done) { return session.refreshMetadata(done); }).data.value("objects").toArray();
    };
    sql(session, "CREATE TABLE designer_parent (id BIGINT PRIMARY KEY)");
    sql(session, "INSERT INTO designer_parent (id) VALUES (1)");
    QWidget view;
    view.show();
    int commits = 0;
    showTableDesigner(session, catalog(), {}, &view, [&] { ++commits; });
    auto& creator = widget<QDialog>(view, "tableDesigner");
    widget<QLineEdit>(creator, "designerTableName").setText("designer_child");
    widget<QPushButton>(creator, "designerAddColumn").click();
    auto& columns = widget<QTableWidget>(creator, "designerColumns");
    columns.item(1, 0)->setText("note");
    static_cast<QComboBox*>(columns.cellWidget(1, 1))->setCurrentText("TEXT");
    columns.item(1, 2)->setCheckState(Qt::Unchecked);
    auto& valueSource = *static_cast<QComboBox*>(columns.cellWidget(1, 3));
    valueSource.setCurrentIndex(valueSource.findData(static_cast<int>(DesignerColumn::Value::expression)));
    columns.item(1, 4)->setText("'reviewed; value'");
    addConstraint(creator, "primary_key", "pk_ch", "id");
    addConstraint(creator, "foreign_key", "fk_ch_parent", "id", "designer_parent");
    addConstraint(creator, "check", "ck_ch_id", {}, {}, "\"id\" > 0");
    addIndex(creator, "ix_ch_note", "note");
    widget<QPushButton>(creator, "designerPreview").click();
    const auto preview = widget<QPlainTextEdit>(creator, "designerDdlPreview").toPlainText();
    require(preview.contains(u"CREATE TABLE") && preview.contains(u"CREATE INDEX") && preview.contains(u"REFERENCES") &&
        preview.contains(u"designer_parent") && preview.contains(u"reviewed; value"), "CREATE DDL and references were reviewed");
    apply(creator, session, "Plan committed");
    creator.reject();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    require(commits == 1, "first commit refresh signaled");
    sql(session, "INSERT INTO designer_child (id) VALUES (1)");
    const auto objects = catalog();
    const auto child = locate(objects, 3, u"designer_child");
    const auto index = locate(objects, 4, u"ix_ch_note");
    require(!child.isEmpty() && !index.isEmpty(), "server recorded table and index");
    require(child.value("constraints").toArray().size() == 3, "server recorded primary, foreign and CHECK constraints");
    require(child.value("row_locator").toObject().value("columns").toArray().size() == 1, "server published row key");
    require(child.value("columns").toArray().at(1).toObject().value("default_sql").toString().contains(u"reviewed; value"),
        "server published canonical default SQL");

    QString picked;
    showSchemaDictionary(session, objects, &view, [&](const QJsonObject& selected) { picked = selected.value("id").toString(); });
    auto& schema = widget<QDialog>(view, "schemaDictionary");
    auto& dictionary = widget<QTreeWidget>(schema, "schemaDictionaryObjects");
    QTreeWidgetItem* dictionaryChild = nullptr;
    for (int row = 0; row < dictionary.topLevelItemCount(); ++row)
        if (dictionary.topLevelItem(row)->text(0).endsWith(u".designer_child")) dictionaryChild = dictionary.topLevelItem(row);
    require(dictionaryChild && dictionaryChild->childCount() == 2 && dictionaryChild->text(2).contains(u"Row key"),
        "dictionary shows live table columns and row key");
    QGraphicsItem* fk = nullptr;
    for (auto* item : widget<QGraphicsView>(schema, "schemaDiagram").scene()->items())
        if (item->type() == QGraphicsPathItem::Type && item->toolTip() == u"fk_ch_parent") fk = item;
    require(fk != nullptr, "real foreign key is drawn in the ERD");
    dictionary.setCurrentItem(dictionaryChild);
    widget<QPushButton>(schema, "schemaEditTable").click();
    require(picked == child.value("id").toString(), "dictionary opens the matching live object");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    showTableDesigner(session, objects, child, &view, [&] { ++commits; });
    auto& modifier = widget<QDialog>(view, "tableDesigner");
    auto& liveCols = widget<QTableWidget>(modifier, "designerColumns");
    liveCols.item(1, 0)->setText("remark");
    liveCols.item(1, 2)->setCheckState(Qt::Checked);
    liveCols.item(1, 4)->setText("NULL");
    widget<QPushButton>(modifier, "designerPreview").click();
    const auto alterPreview = widget<QPlainTextEdit>(modifier, "designerDdlPreview").toPlainText();
    require(alterPreview.contains(u"RENAME COLUMN") && alterPreview.contains(u"DROP NOT NULL") &&
        alterPreview.indexOf(u"DROP NOT NULL") < alterPreview.indexOf(u"SET DEFAULT (NULL)"), "ALTER includes nullable default in execution order");
    sql(other, "CREATE TABLE designer_unrelated (x BIGINT)");
    apply(modifier, session, "Plan committed");
    require(commits == 2, "unrelated catalog change allowed");
    modifier.reject();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    const auto current = catalog();
    const auto altered = locate(current, 3, u"designer_child");
    const auto alteredColumn = altered.value("columns").toArray().at(1).toObject();
    require(alteredColumn.value("name").toString() == u"remark" && alteredColumn.value("nullable").toBool() &&
        alteredColumn.value("default_sql").toString() == u"NULL", "ALTER persisted");
    showTableDesigner(session, current, altered, &view, [&] { ++commits; });
    auto& stale = widget<QDialog>(view, "tableDesigner");
    widget<QTableWidget>(stale, "designerColumns").item(1, 0)->setText("must_not_apply");
    widget<QPushButton>(stale, "designerPreview").click();
    sql(other, "ALTER TABLE designer_child ADD COLUMN simultaneous TEXT");
    apply(stale, session, "The catalog changed");
    require(commits == 2, "stale preview blocked");
    const auto observed = locate(catalog(), 3, u"designer_child").value("columns").toArray();
    require(observed.size() == 3 && observed.at(1).toObject().value("name").toString() == u"remark", "stale plan caused no partial DDL");
    stale.reject();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    const auto retryCatalog = catalog();
    showTableDesigner(session, retryCatalog, locate(retryCatalog, 3, u"designer_child"), &view, [&] { ++commits; });
    auto& rejected = widget<QDialog>(view, "tableDesigner");
    widget<QTableWidget>(rejected, "designerColumns").item(1, 0)->setText("must_rollback");
    addConstraint(rejected, "check", "must_fail", {}, {}, "\"id\" > 1");
    widget<QPushButton>(rejected, "designerPreview").click();
    apply(rejected, session, "The transaction was rolled back");
    const auto postFailure = locate(catalog(), 3, u"designer_child");
    require(commits == 2 && postFailure.value("columns").toArray().at(1).toObject().value("name").toString() == u"remark" &&
        postFailure.value("constraints").toArray().size() == 3, "constraint failure rolled back earlier DDL");
    std::cout << "Designer TLS CREATE, constraint/default/index, dictionary and ERD, ALTER, stale checks and rollback passed.\n";
}
