#include "table_design.hpp"

#include "sql_script.hpp"
#include "sql_session.hpp"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace wb
{
namespace
{
using Value = DesignerColumn::Value;

QStringList components(const QJsonValue& value)
{
    QStringList names;
    for (const auto name : value.toArray()) names.push_back(name.toString());
    return names;
}

QJsonObject findObject(const QJsonArray& catalog, const QString& id)
{
    if (id.isEmpty()) return {};
    for (const auto entry : catalog)
    {
        const auto object = entry.toObject();
        if (object.value("id").toString() == id) return object;
    }
    return {};
}

QStringList objectColumnNames(const QJsonObject& object, const QJsonValue& ordinals)
{
    const auto columns = object.value("columns").toArray();
    QStringList listed;
    for (const auto ordinal : ordinals.toArray())
    {
        const auto offset = ordinal.toInt(-1);
        if (offset < 0 || offset >= columns.size()) return {};
        listed.push_back(columns.at(offset).toObject().value("name").toString());
    }
    return listed;
}

QString displayColumns(const QStringList& columns)
{
    QStringList names;
    for (const auto& name : columns) names.push_back(sqlQualifiedIdentifier({name}));
    return names.join(QStringLiteral(", "));
}

QStringList parseColumnList(const QString& source)
{
    QStringList names;
    auto state = SqlLexState::normal;
    bool next = true;
    for (const auto& token : sql_tokens(source, state))
    {
        if (token.kind == SqlTokenKind::whitespace) continue;
        auto value = source.mid(token.offset, token.length);
        if (!next && token.kind == SqlTokenKind::symbol && value == u",")
        {
            next = true;
            continue;
        }
        if (!next) return {};
        if (token.kind == SqlTokenKind::quoted_name && value.size() >= 2)
        {
            value.removeFirst();
            value.chop(1);
            value.replace(QStringLiteral("\"\""), QStringLiteral("\""));
        }
        else if (token.kind != SqlTokenKind::word) return {};
        names.push_back(value);
        next = false;
    }
    return state == SqlLexState::normal && !next ? names : QStringList{};
}

QString errorText(const SqlReply& reply)
{
    if (reply.error.unknown_outcome)
        return QObject::tr("Commit outcome unknown. Keep the request token and verify it before applying changes again.\n%1")
            .arg(reply.requestToken);
    return QObject::tr("Server error %1 · %2").arg(reply.error.code).arg(QString::fromUtf8(reply.error.message));
}

QJsonObject schemaSnapshot(const QJsonArray& catalog, const QJsonObject& table, const TableDesign& design)
{
    const auto tableId = table.value("id");
    const auto related = table.value("depends_on").toArray();
    const auto schema = design.target.mid(0, design.target.size() - 1);
    QList<QStringList> referenceTables;
    for (const auto& constraint : design.addedConstraints)
        if (constraint.kind == u"foreign_key") referenceTables.push_back(constraint.referenceTable);
    QList<QStringList> indexNames;
    for (const auto& index : design.addedIndexes) indexNames.push_back(schema + QStringList{index.name});

    QJsonObject copy;
    for (const auto value : catalog)
    {
        auto object = value.toObject();
        const auto id = object.value("id");
        const auto name = components(object.value("qualified_name"));
        const auto kind = object.value("kind").toInt();
        const bool targetDependency = !tableId.toString().isEmpty() &&
            (id == tableId || related.contains(id) || object.value("depends_on").toArray().contains(tableId) ||
                object.value("index").toObject().value("table_id") == tableId);
        if (!targetDependency && !(kind == 2 && name == schema) &&
            !(kind == 3 && (name == design.target || referenceTables.contains(name))) &&
            !(kind == 4 && indexNames.contains(name))) continue;
        object.remove("data_revision");
        copy.insert(id.toString(), object);
    }
    return copy;
}

class TableDesigner : public QDialog
{
public:
    TableDesigner(SqlSession& session, QJsonArray catalog, QJsonObject original, QWidget* parent,
        std::function<void()> refresh)
        : QDialog(parent), session_(session), catalog_(std::move(catalog)), original_(std::move(original)),
          refresh_(std::move(refresh))
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setObjectName("tableDesigner");
        setWindowTitle(tr("Table designer · %1").arg(endpoint(session.profile())));
        resize(860, 620);
        const auto target = components(original_.value("qualified_name"));
        const bool creating = original_.isEmpty();
        editable_ = !session.profile().read_only && (creating || (original_.value("schema_details").toBool() &&
            original_.value("permissions").toObject().value("ddl").toBool()));
        auto* layout = new QVBoxLayout(this);
        form_ = new QWidget;
        auto* form = new QFormLayout(form_);
        schema_ = new QComboBox;
        schema_->setObjectName("designerSchema");
        for (const auto entry : catalog_)
        {
            const auto object = entry.toObject();
            if (object.value("kind").toInt() != 2) continue;
            const auto name = components(object.value("qualified_name"));
            if (!name.isEmpty()) schema_->addItem(sqlQualifiedIdentifier(name), QVariant::fromValue(name));
        }
        if (!creating)
        {
            const auto schema = target.mid(0, std::max<qsizetype>(0, target.size() - 1));
            const int index = schema_->findData(QVariant::fromValue(schema));
            if (index < 0) schema_->addItem(sqlQualifiedIdentifier(schema), QVariant::fromValue(schema));
            schema_->setCurrentIndex(index >= 0 ? index : schema_->count() - 1);
        }
        schema_->setEnabled(creating);
        form->addRow(tr("Schema"), schema_);
        name_ = new QLineEdit(creating ? QString{} : target.value(target.size() - 1));
        name_->setObjectName("designerTableName");
        name_->setReadOnly(!creating);
        form->addRow(tr("Table name"), name_);
        layout->addWidget(form_);

        tabs_ = new QTabWidget;
        tabs_->setObjectName("designerTabs");
        layout->addWidget(tabs_, 1);
        columns_ = new QTableWidget(0, 5);
        columns_->setObjectName("designerColumns");
        columns_->setHorizontalHeaderLabels({tr("Name"), tr("Type"), tr("NULL allowed"), tr("Value source"), tr("Default SQL")});
        columns_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        columns_->horizontalHeader()->setStretchLastSection(true);
        columns_->setSelectionBehavior(QAbstractItemView::SelectRows);
        columns_->setSelectionMode(QAbstractItemView::SingleSelection);
        auto* columnView = new QWidget;
        auto* columnLayout = new QVBoxLayout(columnView);
        columnLayout->addWidget(columns_);
        auto* row = new QHBoxLayout;
        addButton(row, tr("Add column"), "designerAddColumn", [this] { addColumn({}); invalidate(); });
        addButton(row, tr("Remove column"), "designerRemoveColumn", [this] {
            const auto selected = columns_->currentRow();
            if (selected >= 0) { columns_->removeRow(selected); invalidate(); }
        });
        row->addStretch();
        columnLayout->addLayout(row);
        tabs_->addTab(columnView, tr("Columns"));
        for (const auto& col : readDesignerColumns(original_)) addColumn(col);
        if (creating)
        {
            DesignerColumn initial;
            initial.name = QStringLiteral("id");
            initial.type = QStringLiteral("BIGINT");
            initial.nullable = false;
            addColumn(initial);
        }

        constraints_ = new QTreeWidget;
        constraints_->setObjectName("designerConstraints");
        constraints_->setHeaderLabels({tr("Name"), tr("Kind"), tr("Columns or expression"), tr("References")});
        indexes_ = new QTreeWidget;
        indexes_->setObjectName("designerIndexes");
        indexes_->setHeaderLabels({tr("Name"), tr("Unique"), tr("Columns"), tr("Collation")});
        auto* constraintsView = listTab(constraints_, tr("Constraints"), "designerAddConstraint", [this] { addConstraint(); },
            "designerRemoveConstraint", [this] { dropConstraint(); });
        auto* indexesView = listTab(indexes_, tr("Indexes"), "designerAddIndex", [this] { addIndex(); },
            "designerRemoveIndex", [this] { dropIndex(); });
        Q_UNUSED(constraintsView);
        Q_UNUSED(indexesView);
        refillConstraints();
        refillIndexes();

        previewText_ = new QPlainTextEdit;
        previewText_->setObjectName("designerDdlPreview");
        previewText_->setReadOnly(true);
        tabs_->addTab(previewText_, tr("DDL preview"));
        status_ = new QLabel;
        status_->setObjectName("designerStatus");
        status_->setTextFormat(Qt::PlainText);
        status_->setWordWrap(true);
        layout->addWidget(status_);
        auto* buttons = new QHBoxLayout;
        preview_ = addButton(buttons, tr("Preview"), "designerPreview", [this] { preview(); });
        apply_ = addButton(buttons, tr("Apply plan"), "designerApply", [this] { apply(); });
        apply_->setEnabled(false);
        buttons->addStretch();
        auto* close = new QPushButton(tr("Close"));
        connect(close, &QPushButton::clicked, this, &TableDesigner::reject);
        buttons->addWidget(close);
        layout->addLayout(buttons);

        connect(name_, &QLineEdit::textChanged, this, [this] { invalidate(); });
        connect(schema_, &QComboBox::currentIndexChanged, this, [this] { invalidate(); });
        connect(columns_, &QTableWidget::itemChanged, this, [this] { invalidate(); });
        if (!editable_)
        {
            form_->setEnabled(false);
            tabs_->setEnabled(false);
            preview_->setEnabled(false);
            status_->setText(tr("This session can browse table definitions. Table edits require DDL permission."));
        }
    }

    void reject() override { if (!applying_) QDialog::reject(); }

protected:
    void closeEvent(QCloseEvent* event) override
    {
        if (applying_) { event->ignore(); return; }
        QDialog::closeEvent(event);
    }

private:
    QPushButton* addButton(QHBoxLayout* row, const QString& text, const char* name, std::function<void()> action)
    {
        auto* button = new QPushButton(text);
        button->setObjectName(name);
        connect(button, &QPushButton::clicked, this, std::move(action));
        row->addWidget(button);
        return button;
    }

    QWidget* listTab(QTreeWidget* list, const QString& title, const char* addName, std::function<void()> add,
        const char* removeName, std::function<void()> remove)
    {
        auto* page = new QWidget;
        auto* layout = new QVBoxLayout(page);
        layout->addWidget(list);
        auto* actions = new QHBoxLayout;
        addButton(actions, tr("Add"), addName, std::move(add));
        addButton(actions, tr("Remove"), removeName, std::move(remove));
        actions->addStretch();
        layout->addLayout(actions);
        tabs_->addTab(page, title);
        return page;
    }

    void addColumn(const DesignerColumn& column);
    TableDesign design() const;
    void invalidate();
    void refillConstraints();
    void refillIndexes();
    void addConstraint();
    void dropConstraint();
    void addIndex();
    void dropIndex();
    void preview();
    void apply();
    void finish(QString message, bool success = false);
    void failOrRollback(const SqlReply& reply);

    SqlSession& session_;
    QJsonArray catalog_;
    QJsonObject original_;
    std::function<void()> refresh_;
    QWidget* form_{};
    QTabWidget* tabs_{};
    QComboBox* schema_{};
    QLineEdit* name_{};
    QTableWidget* columns_{};
    QTreeWidget* constraints_{};
    QTreeWidget* indexes_{};
    QPlainTextEdit* previewText_{};
    QLabel* status_{};
    QPushButton* preview_{};
    QPushButton* apply_{};
    QList<DesignerConstraint> newConstraints_;
    QList<int> removedConstraints_;
    QList<DesignerIndex> newIndexes_;
    QStringList removedIndexIds_;
    TableDdlPlan plan_;
    bool editable_{};
    bool applying_{};
};
void TableDesigner::addColumn(const DesignerColumn& column)
{
    QSignalBlocker block(columns_);
    const int row = columns_->rowCount();
    columns_->insertRow(row);
    auto* name = new QTableWidgetItem(column.name);
    name->setData(Qt::UserRole, column.originalOrdinal);
    columns_->setItem(row, 0, name);
    auto* kind = new QComboBox;
    kind->setEditable(true);
    kind->addItems({"BIGINT", "TEXT", "BOOLEAN", "BLOB", "DATE", "TIME", "TIMESTAMP WITH TIME ZONE", "DECIMAL(12,2)"});
    kind->setCurrentText(column.type);
    columns_->setCellWidget(row, 1, kind);
    connect(kind, &QComboBox::currentTextChanged, this, [this] { invalidate(); });
    auto* nullable = new QTableWidgetItem;
    nullable->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    nullable->setCheckState(column.nullable ? Qt::Checked : Qt::Unchecked);
    columns_->setItem(row, 2, nullable);
    auto* value = new QComboBox;
    value->addItem(tr("None"), static_cast<int>(Value::none));
    value->addItem(tr("SQL expression"), static_cast<int>(Value::expression));
    value->addItem(tr("Identity"), static_cast<int>(Value::identity));
    if (column.value == Value::keep) value->addItem(tr("Keep existing"), static_cast<int>(Value::keep));
    value->setCurrentIndex(value->findData(static_cast<int>(column.value)));
    columns_->setCellWidget(row, 3, value);
    connect(value, &QComboBox::currentIndexChanged, this, [this] { invalidate(); });
    auto* expression = new QTableWidgetItem(column.defaultSql);
    expression->setToolTip(tr("Used when Value source is SQL expression"));
    columns_->setItem(row, 4, expression);
}

TableDesign TableDesigner::design() const
{
    TableDesign design;
    if (schema_->currentIndex() >= 0)
    {
        design.target = schema_->currentData().toStringList();
        design.target.push_back(name_->text());
    }
    for (int row = 0; row < columns_->rowCount(); ++row)
    {
        DesignerColumn col;
        col.originalOrdinal = columns_->item(row, 0)->data(Qt::UserRole).toInt();
        col.name = columns_->item(row, 0)->text();
        col.type = static_cast<QComboBox*>(columns_->cellWidget(row, 1))->currentText();
        col.nullable = columns_->item(row, 2)->checkState() == Qt::Checked;
        col.value = static_cast<Value>(static_cast<QComboBox*>(columns_->cellWidget(row, 3))->currentData().toInt());
        col.defaultSql = columns_->item(row, 4)->text().trimmed();
        design.columns.push_back(col);
    }
    design.addedConstraints = newConstraints_;
    design.droppedConstraints = removedConstraints_;
    design.addedIndexes = newIndexes_;
    design.droppedIndexIds = removedIndexIds_;
    return design;
}

void TableDesigner::invalidate()
{
    if (applying_ || !apply_) return;
    plan_ = {};
    apply_->setEnabled(false);
    previewText_->clear();
    status_->clear();
}

void TableDesigner::refillConstraints()
{
    constraints_->clear();
    const auto old = original_.value("constraints").toArray();
    for (int ordinal = 0; ordinal < old.size(); ++ordinal)
    {
        if (removedConstraints_.contains(ordinal)) continue;
        const auto constraint = old.at(ordinal).toObject();
        const auto parent = findObject(catalog_, constraint.value("reference_table_id").toString());
        auto name = constraint.value("name").toString();
        if (name.isEmpty()) name = tr("Unnamed");
        auto definition = constraint.value("check_sql").toString();
        if (definition.isEmpty()) definition = displayColumns(objectColumnNames(original_, constraint.value("columns")));
        auto* item = new QTreeWidgetItem(constraints_, {name, constraint.value("kind").toString(),
            definition, sqlQualifiedIdentifier(components(parent.value("qualified_name")))});
        item->setData(0, Qt::UserRole, ordinal);
        item->setData(0, Qt::UserRole + 1, false);
    }
    for (int index = 0; index < newConstraints_.size(); ++index)
    {
        const auto& c = newConstraints_.at(index);
        auto* item = new QTreeWidgetItem(constraints_, {c.name, c.kind,
            c.checkSql.isEmpty() ? displayColumns(c.columns) : c.checkSql,
            sqlQualifiedIdentifier(c.referenceTable)});
        item->setData(0, Qt::UserRole, index);
        item->setData(0, Qt::UserRole + 1, true);
    }
    constraints_->resizeColumnToContents(0);
}

void TableDesigner::refillIndexes()
{
    indexes_->clear();
    const auto tableId = original_.value("id").toString();
    for (const auto entry : catalog_)
    {
        const auto object = entry.toObject();
        if (object.value("kind").toInt() != 4) continue;
        const auto id = object.value("id").toString();
        const auto details = object.value("index").toObject();
        if (tableId.isEmpty() || details.value("table_id").toString() != tableId || removedIndexIds_.contains(id)) continue;
        const auto name = components(object.value("qualified_name"));
        auto* item = new QTreeWidgetItem(indexes_, {name.value(name.size() - 1),
            details.value("unique").toBool() ? tr("Yes") : tr("No"),
            displayColumns(objectColumnNames(original_, details.value("columns"))),
            object.value("collation").toObject().value("name").toString()});
        item->setData(0, Qt::UserRole, id);
        item->setData(0, Qt::UserRole + 1, false);
    }
    for (int index = 0; index < newIndexes_.size(); ++index)
    {
        const auto& item = newIndexes_.at(index);
        auto* row = new QTreeWidgetItem(indexes_, {item.name, item.unique ? tr("Yes") : tr("No"),
            displayColumns(item.columns), item.collation});
        row->setData(0, Qt::UserRole, index);
        row->setData(0, Qt::UserRole + 1, true);
    }
    indexes_->resizeColumnToContents(0);
}

void TableDesigner::addConstraint()
{
    QDialog modal(this);
    modal.setObjectName("designerConstraintEditor");
    modal.setWindowTitle(tr("New constraint"));
    auto* form = new QFormLayout(&modal);
    QLineEdit name;
    name.setObjectName("constraintName");
    QComboBox kind;
    kind.setObjectName("constraintKind");
    kind.addItem(tr("Primary key"), "primary_key");
    kind.addItem(tr("Unique"), "unique");
    kind.addItem(tr("Foreign key"), "foreign_key");
    kind.addItem(tr("Check"), "check");
    QLineEdit columns;
    columns.setObjectName("constraintColumns");
    QStringList available;
    for (const auto& column : design().columns) available.push_back(column.name);
    columns.setPlaceholderText(displayColumns(available));
    QComboBox reference;
    reference.setObjectName("constraintReference");
    for (const auto entry : catalog_)
    {
        const auto object = entry.toObject();
        if (object.value("kind").toInt() != 3) continue;
        const auto target = components(object.value("qualified_name"));
        reference.addItem(sqlQualifiedIdentifier(target), QVariant::fromValue(target));
    }
    QLineEdit refColumns;
    refColumns.setObjectName("constraintReferenceColumns");
    QLineEdit expression;
    expression.setObjectName("constraintCheck");
    form->addRow(tr("Name"), &name);
    form->addRow(tr("Kind"), &kind);
    form->addRow(tr("Columns in key order"), &columns);
    form->addRow(tr("Referenced table"), &reference);
    form->addRow(tr("Referenced columns in key order"), &refColumns);
    form->addRow(tr("CHECK expression"), &expression);
    const auto variantChanged = [&] {
        const auto type = kind.currentData().toString();
        columns.setEnabled(type != u"check");
        reference.setEnabled(type == u"foreign_key");
        refColumns.setEnabled(type == u"foreign_key");
        expression.setEnabled(type == u"check");
    };
    connect(&kind, &QComboBox::currentIndexChanged, &modal, variantChanged);
    variantChanged();
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &modal, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &modal, &QDialog::reject);
    if (modal.exec() != QDialog::Accepted) return;
    DesignerConstraint item;
    item.name = name.text();
    item.kind = kind.currentData().toString();
    if (item.kind == u"check") item.checkSql = expression.text();
    else item.columns = parseColumnList(columns.text());
    if (item.kind == u"foreign_key")
    {
        item.referenceTable = reference.currentData().toStringList();
        item.referenceColumns = parseColumnList(refColumns.text());
    }
    newConstraints_.push_back(item);
    refillConstraints();
    invalidate();
}

void TableDesigner::dropConstraint()
{
    auto* current = constraints_->currentItem();
    if (!current) return;
    const int index = current->data(0, Qt::UserRole).toInt();
    if (current->data(0, Qt::UserRole + 1).toBool()) newConstraints_.removeAt(index);
    else removedConstraints_.push_back(index);
    refillConstraints();
    invalidate();
}

void TableDesigner::addIndex()
{
    QDialog modal(this);
    modal.setWindowTitle(tr("New index"));
    modal.setObjectName("designerIndexEditor");
    auto* form = new QFormLayout(&modal);
    QLineEdit name;
    name.setObjectName("indexName");
    QLineEdit columns;
    columns.setObjectName("indexColumns");
    QStringList available;
    for (const auto& column : design().columns) available.push_back(column.name);
    columns.setPlaceholderText(displayColumns(available));
    QLineEdit collation;
    collation.setObjectName("indexCollation");
    QCheckBox unique;
    unique.setObjectName("indexUnique");
    form->addRow(tr("Name"), &name);
    form->addRow(tr("Columns in key order"), &columns);
    form->addRow(tr("Collation (optional)"), &collation);
    form->addRow(tr("Unique"), &unique);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &modal, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &modal, &QDialog::reject);
    if (modal.exec() != QDialog::Accepted) return;
    newIndexes_.push_back({name.text(), parseColumnList(columns.text()), collation.text(), unique.isChecked()});
    refillIndexes();
    invalidate();
}

void TableDesigner::dropIndex()
{
    auto* current = indexes_->currentItem();
    if (!current) return;
    if (current->data(0, Qt::UserRole + 1).toBool()) newIndexes_.removeAt(current->data(0, Qt::UserRole).toInt());
    else removedIndexIds_.push_back(current->data(0, Qt::UserRole).toString());
    refillIndexes();
    invalidate();
}
void TableDesigner::preview()
{
    if (!editable_ || applying_) return;
    plan_ = planTableDesign(catalog_, original_, design());
    apply_->setEnabled(plan_.error.isEmpty() && !plan_.statements.isEmpty());
    if (!plan_.error.isEmpty())
    {
        status_->setText(plan_.error);
        previewText_->clear();
        return;
    }
    if (plan_.statements.isEmpty())
    {
        status_->setText(tr("There are no changes to preview."));
        previewText_->clear();
        return;
    }
    QStringList details;
    if (plan_.destructive) details.push_back(tr("This plan removes columns, constraints or indexes."));
    if (plan_.checksRows) details.push_back(tr("The server may scan or rewrite existing rows and hold a write lock."));
    if (!plan_.affectedObjects.isEmpty())
        details.push_back(tr("Visible dependent and referenced objects\n%1").arg(plan_.affectedObjects.join(u'\n')));
    else details.push_back(tr("No dependent objects are visible in this session."));
    details.push_back(tr("Catalog versions and permissions are checked in the same transaction before applying the plan."));
    details.push_back(plan_.statements.join(QStringLiteral(";\n\n")) + u';');
    previewText_->setPlainText(details.join(QStringLiteral("\n\n")));
    tabs_->setCurrentWidget(previewText_);
    status_->setText(tr("%1 DDL statements ready for review.").arg(plan_.statements.size()));
}

void TableDesigner::finish(QString message, bool success)
{
    applying_ = false;
    if (success) editable_ = false;
    form_->setEnabled(editable_);
    if (editable_) schema_->setEnabled(original_.isEmpty());
    tabs_->setEnabled(true);
    preview_->setEnabled(editable_);
    apply_->setEnabled(false);
    status_->setText(std::move(message));
    if (success && refresh_) refresh_();
}

void TableDesigner::failOrRollback(const SqlReply& reply)
{
    const auto message = errorText(reply);
    if (reply.error.unknown_outcome) editable_ = false;
    if (!session_.state().transactionActive)
    {
        finish(message);
        return;
    }
    if (!session_.transaction(SqlTransaction::rollback, {}, [this, message](const SqlReply& rolled) {
        if (rolled.error.code == SDP_OK) finish(message + tr("\nThe transaction was rolled back."));
        else finish(message + tr("\nRollback status could not be confirmed. %1").arg(errorText(rolled)));
    })) finish(message + tr("\nRollback could not be started. Check the session before applying again."));
}

void TableDesigner::apply()
{
    if (!editable_ || applying_ || plan_.statements.isEmpty() || !plan_.error.isEmpty()) return;
    if (session_.busy() || session_.state().transactionActive || !session_.state().connected)
    {
        status_->setText(tr("Finish the active operation or transaction before applying DDL."));
        return;
    }
    const auto intended = design();
    const auto reviewed = plan_;
    const auto confirmation = reviewed.destructive
        ? tr("Apply this plan and its removals to %1?").arg(endpoint(session_.profile()))
        : tr("Apply these schema changes to %1?").arg(endpoint(session_.profile()));
    if (QMessageBox::question(this, tr("Apply reviewed DDL"), confirmation, QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) return;

    applying_ = true;
    form_->setEnabled(false);
    tabs_->setEnabled(false);
    preview_->setEnabled(false);
    apply_->setEnabled(false);
    status_->setText(tr("Checking catalog revisions under a server transaction…"));
    if (!session_.transaction(SqlTransaction::begin, {}, [this, intended, reviewed](const SqlReply& opened) {
        if (opened.error.code != SDP_OK) { failOrRollback(opened); return; }
        if (!session_.refreshMetadata([this, intended, reviewed](const SqlReply& metadata) {
            if (metadata.error.code != SDP_OK) { failOrRollback(metadata); return; }
            const auto latest = metadata.data.value("objects").toArray();
            const auto table = findObject(latest, original_.value("id").toString());
            const auto nextPlan = planTableDesign(latest, table, intended);
            if ((!original_.isEmpty() && table.isEmpty()) ||
                schemaSnapshot(latest, table, intended) != schemaSnapshot(catalog_, original_, intended) ||
                !nextPlan.error.isEmpty() || nextPlan.statements != reviewed.statements)
            {
                const auto stale = tr("The catalog changed after this preview. Close the designer and refresh the table.");
                if (!session_.transaction(SqlTransaction::rollback, {}, [this, stale](const SqlReply& rollback) {
                    if (rollback.error.code == SDP_OK) finish(stale);
                    else finish(stale + tr("\nRollback status could not be confirmed. %1").arg(errorText(rollback)));
                })) finish(stale + tr("\nCheck the transaction in the SQL worksheet."));
                return;
            }
            QVector<SqlStatement> statements;
            for (const auto& sql : reviewed.statements) statements.push_back({sql});
            status_->setText(tr("Applying reviewed DDL…"));
            if (!session_.execute(std::move(statements), {}, {}, {1, 120000}, {}, [this](const SqlReply& executed) {
                if (executed.error.code != SDP_OK) { failOrRollback(executed); return; }
                if (!session_.transaction(SqlTransaction::commit, {}, [this](const SqlReply& committed) {
                    if (committed.error.code != SDP_OK) { failOrRollback(committed); return; }
                    finish(tr("Plan committed."), true);
                }))
                {
                    SqlReply unavailable;
                    unavailable.error.code = SDP_INVALID_STATE;
                    failOrRollback(unavailable);
                }
            }))
            {
                SqlReply unavailable;
                unavailable.error.code = SDP_INVALID_STATE;
                failOrRollback(unavailable);
            }
        }))
        {
            SqlReply unavailable;
            unavailable.error.code = SDP_INVALID_STATE;
            failOrRollback(unavailable);
        }
    })) finish(tr("The transaction could not be started."));
}
struct RelationEdge
{
    QString from;
    QString to;
    QString label;
};

QList<RelationEdge> relationships(const QJsonArray& catalog)
{
    QList<RelationEdge> edges;
    for (const auto value : catalog)
    {
        const auto table = value.toObject();
        if (table.value("kind").toInt() != 3) continue;
        const auto from = table.value("id").toString();
        for (const auto entry : table.value("constraints").toArray())
        {
            const auto constraint = entry.toObject();
            if (constraint.value("kind").toString() != u"foreign_key") continue;
            const auto to = constraint.value("reference_table_id").toString();
            if (!from.isEmpty() && !to.isEmpty() && !findObject(catalog, to).isEmpty())
                edges.push_back({from, to, constraint.value("name").toString()});
        }
    }
    return edges;
}

void buildDiagram(QGraphicsScene& scene, const QJsonArray& catalog, const QPalette& palette)
{
    constexpr int columns = 3;
    constexpr qreal stepX = 260;
    constexpr qreal stepY = 166;
    QHash<QString, QRectF> anchors;
    int index = 0;
    for (const auto entry : catalog)
    {
        const auto table = entry.toObject();
        if (table.value("kind").toInt() != 3) continue;
        const auto id = table.value("id").toString();
        if (id.isEmpty()) continue;
        const auto catalogColumns = table.value("columns").toArray();
        constexpr qsizetype listed = 4;
        QStringList lines;
        lines.push_back(components(table.value("qualified_name")).join(u'.'));
        for (qsizetype col = 0; col < std::min(catalogColumns.size(), listed); ++col)
        {
            const auto item = catalogColumns.at(col).toObject();
            lines.push_back(QObject::tr("%1 · %2").arg(item.value("name").toString(), item.value("type").toString()));
        }
        if (catalogColumns.size() > listed) lines.push_back(QObject::tr("+%1 more").arg(catalogColumns.size() - listed));
        const QRectF rect((index % columns) * stepX + 18, (index / columns) * stepY + 18, 232, 140);
        anchors.insert(id, rect);
        auto* node = scene.addRect(rect, QPen(palette.color(QPalette::Mid)), QBrush(palette.color(QPalette::AlternateBase)));
        node->setData(0, id);
        node->setFlag(QGraphicsItem::ItemIsSelectable);
        auto* text = new QGraphicsTextItem(node);
        text->setPos(rect.topLeft() + QPointF(3, 3));
        text->setTextWidth(rect.width() - 6);
        text->setDefaultTextColor(palette.color(QPalette::Text));
        for (auto& line : lines) if (line.size() > 36) line = line.left(35) + u'\u2026';
        text->setPlainText(lines.join(u'\n'));
        node->setToolTip(components(table.value("qualified_name")).join(u'.'));
        ++index;
    }

    const auto pen = QPen(palette.color(QPalette::Highlight), 1.4);
    for (const auto& edge : relationships(catalog))
    {
        if (!anchors.contains(edge.from) || !anchors.contains(edge.to)) continue;
        const auto from = anchors.value(edge.from);
        const auto to = anchors.value(edge.to);
        QPainterPath path;
        if (edge.from == edge.to)
        {
            path.moveTo(from.right(), from.center().y() - 14);
            path.lineTo(from.right() + 12, from.center().y() - 14);
            path.lineTo(from.right() + 12, from.center().y() + 14);
            path.lineTo(from.right(), from.center().y() + 14);
        }
        else
        {
            const auto below = to.center().y() > from.center().y();
            const auto sameRow = std::abs(to.center().y() - from.center().y()) < 1;
            if (sameRow)
            {
                const bool right = to.left() > from.left();
                const auto a = QPointF(right ? from.right() : from.left(), from.center().y());
                const auto b = QPointF(right ? to.left() : to.right(), to.center().y());
                path.moveTo(a);
                path.lineTo(b);
            }
            else
            {
                const auto a = QPointF(from.center().x(), below ? from.bottom() : from.top());
                const auto b = QPointF(to.center().x(), below ? to.top() : to.bottom());
                const auto middle = (a.y() + b.y()) / 2;
                path.moveTo(a);
                path.lineTo(a.x(), middle);
                path.lineTo(b.x(), middle);
                path.lineTo(b);
            }
        }
        auto* line = scene.addPath(path, pen);
        line->setZValue(-1);
        line->setToolTip(edge.label.isEmpty() ? QObject::tr("Foreign key") : edge.label);
    }
    scene.setSceneRect(scene.itemsBoundingRect().adjusted(-8, -8, 8, 8));
}
}

void showTableDesigner(SqlSession& session, const QJsonArray& catalog,
    const QJsonObject& table, QWidget* parent, std::function<void()> refresh)
{
    auto* dialog = new TableDesigner(session, catalog, table, parent, std::move(refresh));
    dialog->open();
}

void showSchemaDictionary(SqlSession& session, const QJsonArray& catalog,
    QWidget* parent, std::function<void(const QJsonObject&)> editTable)
{
    auto* dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName("schemaDictionary");
    dialog->setWindowTitle(QObject::tr("Data dictionary and relations · %1").arg(endpoint(session.profile())));
    dialog->resize(1040, 680);
    auto* layout = new QVBoxLayout(dialog);
    auto* views = new QTabWidget;
    layout->addWidget(views, 1);
    auto* dictionary = new QTreeWidget;
    dictionary->setObjectName("schemaDictionaryObjects");
    dictionary->setHeaderLabels({QObject::tr("Object"), QObject::tr("Type"), QObject::tr("Description")});
    views->addTab(dictionary, QObject::tr("Data dictionary"));
    for (const auto entry : catalog)
    {
        const auto table = entry.toObject();
        if (table.value("kind").toInt() != 3) continue;
        const auto id = table.value("id").toString();
        const auto names = components(table.value("qualified_name"));
        const auto columns = table.value("columns").toArray();
        const auto locators = objectColumnNames(table, table.value("row_locator").toObject().value("columns"));
        auto description = QObject::tr("%1 columns · schema revision %2").arg(columns.size()).arg(table.value("schema_version").toInt());
        if (!locators.isEmpty()) description += QObject::tr(" · Row key %1").arg(displayColumns(locators));
        auto* item = new QTreeWidgetItem(dictionary, {names.join(u'.'), QObject::tr("Table"), description});
        item->setData(0, Qt::UserRole, id);
        for (const auto column : columns)
        {
            const auto field = column.toObject();
            const auto name = field.value("name").toString();
            QString flags = field.value("nullable").toBool() ? QObject::tr("NULL allowed") : QObject::tr("NOT NULL");
            if (field.value("identity").toBool()) flags += QObject::tr(" · Identity");
            if (field.value("has_default").toBool()) flags += QObject::tr(" · Default");
            if (locators.contains(name)) flags += QObject::tr(" · Row key");
            auto* child = new QTreeWidgetItem(item, {name, field.value("type").toString(), flags});
            child->setData(0, Qt::UserRole, id);
        }
    }
    dictionary->resizeColumnToContents(0);
    dictionary->resizeColumnToContents(1);

    auto* scene = new QGraphicsScene(dialog);
    auto* diagram = new QGraphicsView(scene);
    diagram->setObjectName("schemaDiagram");
    diagram->setRenderHint(QPainter::Antialiasing);
    diagram->setDragMode(QGraphicsView::ScrollHandDrag);
    buildDiagram(*scene, catalog, dialog->palette());
    views->addTab(diagram, QObject::tr("Relationships"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto* edit = buttons->addButton(QObject::tr("Open table designer"), QDialogButtonBox::ActionRole);
    edit->setObjectName("schemaEditTable");
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    const auto openSelected = [dialog, views, scene, diagram, dictionary, catalog, editTable] {
        if (!editTable) return;
        QString id;
        if (views->currentWidget() == diagram)
        {
            const auto items = scene->selectedItems();
            if (!items.isEmpty()) id = items.first()->data(0).toString();
        }
        else if (dictionary->currentItem()) id = dictionary->currentItem()->data(0, Qt::UserRole).toString();
        const auto table = findObject(catalog, id);
        if (table.isEmpty()) return;
        const auto next = editTable;
        dialog->accept();
        next(table);
    };
    QObject::connect(edit, &QPushButton::clicked, dialog, openSelected);
    QObject::connect(dictionary, &QTreeWidget::itemDoubleClicked, dialog, openSelected);
    dialog->open();
}
}
