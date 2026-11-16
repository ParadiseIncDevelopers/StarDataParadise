#include "data_grid.hpp"

#include "sql_session.hpp"
#include "table_codec.hpp"

#include <QAbstractTableModel>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QTableView>
#include <QTextDocument>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace wb
{
namespace
{
constexpr int snapshotRows = 512;
constexpr int pageRows = 64;
constexpr int maxChanges = 64;
constexpr qsizetype maxFieldBytes = 256 * 1024;

QString quoteIdentifier(QString name)
{
    name.replace(u'"', QStringLiteral("\"\""));
    return u'"' + name + u'"';
}

QString quotePath(const QJsonArray& parts)
{
    if (parts.isEmpty() || parts.size() > 3) return {};
    QStringList names;
    for (const auto& part : parts)
    {
        const auto name = part.toString();
        if (name.isEmpty() || name.contains(QChar(u'\0'))) return {};
        names.append(quoteIdentifier(name));
    }
    return names.join(u'.');
}

QJsonObject declaration(QString type)
{
    static const QRegularExpression decimal(QStringLiteral(R"(^DECIMAL\((\d+),\s*(\d+)\)$)"));
    const auto match = decimal.match(type);
    if (match.hasMatch())
        return {{"type", "DECIMAL"}, {"precision", match.captured(1).toInt()}, {"scale", match.captured(2).toInt()}};
    static const QStringList scalars{"BIGINT", "BOOLEAN", "TEXT", "BLOB", "DATE", "TIME",
        "TIMESTAMP WITH TIME ZONE", "RECORD", "COLLECTION"};
    return scalars.contains(type) ? QJsonObject{{"type", std::move(type)}} : QJsonObject{};
}

struct Column
{
    QString name;
    QString sql;
    QString type;
    QJsonObject declaration;
    bool nullable{};
};

struct Table
{
    QString id;
    QString sql;
    qint64 schemaVersion = -1;
    QString revision;
    QVector<Column> columns;
    QVector<int> keys;
    QJsonObject permissions;

    explicit Table(const QJsonObject& value)
        : id(value.value("id").toString()), sql(quotePath(value.value("qualified_name").toArray())),
          schemaVersion(value.value("schema_version").toInteger(-1)),
          revision(value.value("data_revision").toString()), permissions(value.value("permissions").toObject())
    {
        bool revisionValid = false;
        const auto revisionValue = revision.toULongLong(&revisionValid);
        if (!revisionValid || QString::number(revisionValue) != revision) revision.clear();
        for (const auto& item : value.value("columns").toArray())
        {
            const auto column = item.toObject();
            auto name = column.value("name").toString();
            auto type = column.value("type").toString();
            if (name.isEmpty() || name.contains(QChar(u'\0')) || column.value("ordinal").toInt(-1) != columns.size() + 1)
            {
                columns.clear();
                break;
            }
            columns.append({name, quoteIdentifier(name), type, declaration(type), column.value("nullable").toBool(true)});
        }
        const auto locator = value.value("row_locator").toObject();
        const auto kind = locator.value("kind").toString();
        if (kind != u"primary_key" && kind != u"unique") return;
        for (const auto& item : locator.value("columns").toArray())
        {
            const auto position = item.toInt(-1);
            if (position < 0 || position >= columns.size() || keys.contains(position) ||
                columns.at(position).declaration.isEmpty())
            {
                keys.clear();
                break;
            }
            keys.append(position);
        }
    }
    bool valid() const { return !id.isEmpty() && !sql.isEmpty() && schemaVersion > 0 && !columns.isEmpty(); }
    bool editable() const { return !keys.isEmpty() && !revision.isEmpty(); }
    bool allowed(const char* action) const { return permissions.value(QLatin1String(action)).toBool(); }
};

bool isNull(const QJsonValue& value)
{
    return value.toObject().value("t").toString() == u"null";
}

QString displayCell(const QJsonValue& value, bool compact)
{
    const auto cell = value.toObject();
    const auto kind = cell.value("t").toString();
    if (kind == u"null") return compact ? QStringLiteral("NULL") : QString{};
    if (kind == u"binary")
    {
        const auto hex = cell.value("hex").toString();
        return compact ? QObject::tr("BLOB · %1 bytes").arg(hex.size() / 2) : hex;
    }
    if (kind == u"decimal")
    {
        auto digits = cell.value("unscaled").toString();
        const auto scale = cell.value("scale").toInt();
        const bool negative = digits.startsWith(u'-');
        if (negative) digits.remove(0, 1);
        if (scale > 0 && scale <= 38)
        {
            while (digits.size() <= scale) digits.prepend(u'0');
            digits.insert(digits.size() - scale, u'.');
        }
        return negative ? u'-' + digits : digits;
    }
    if (kind == u"bool") return cell.value("v").toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (kind == u"text" || kind == u"i64")
    {
        auto text = cell.value("v").toString();
        if (!compact) return text;
        if (text.isEmpty() && kind == u"text") return QObject::tr("(empty text)");
        if (text.size() > 180) text = text.left(180) + QChar(0x2026);
        return text;
    }
    return QString::fromUtf8(QJsonDocument(cell).toJson(QJsonDocument::Compact));
}

QJsonObject encodeCell(const Column& column, const QString& text, bool null)
{
    if (null) return column.nullable ? QJsonObject{{"t", "null"}} : QJsonObject{};
    if (text.toUtf8().size() > maxFieldBytes * (column.type == u"BLOB" ? 2 : 1)) return {};
    const auto type = column.declaration.value("type").toString();
    if (type == u"TEXT" || type == u"DATE" || type == u"TIME") return {{"t", "text"}, {"v", text}};
    if (type == u"BIGINT")
    {
        bool valid = false;
        const auto number = text.trimmed().toLongLong(&valid);
        return valid ? QJsonObject{{"t", "i64"}, {"v", QString::number(number)}} : QJsonObject{};
    }
    if (type == u"BOOLEAN")
    {
        const auto value = text.trimmed().toLower();
        return value == u"true" || value == u"false"
            ? QJsonObject{{"t", "bool"}, {"v", value == u"true"}} : QJsonObject{};
    }
    if (type == u"BLOB")
    {
        const auto digit = [](QChar value) {
            const auto c = value.unicode();
            return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f') || (c >= u'A' && c <= u'F');
        };
        return text.size() % 2 == 0 && std::all_of(text.cbegin(), text.cend(), digit)
            ? QJsonObject{{"t", "binary"}, {"hex", text.toLower()}} : QJsonObject{};
    }
    if (type == u"DECIMAL")
    {
        static const QRegularExpression decimal(QStringLiteral(R"(^([+-]?)([0-9]+)(?:\.([0-9]+))?$)"));
        const auto match = decimal.match(text.trimmed());
        if (!match.hasMatch()) return {};
        auto fraction = match.captured(3);
        const auto scale = column.declaration.value("scale").toInt();
        if (fraction.size() > scale) return {};
        fraction = fraction.leftJustified(scale, u'0');
        auto digits = match.captured(2) + fraction;
        qsizetype first = 0;
        while (first + 1 < digits.size() && digits.at(first) == u'0') ++first;
        digits.remove(0, first);
        if (digits.size() > column.declaration.value("precision").toInt()) return {};
        if (match.captured(1) == u"-" && digits != u"0") digits.prepend(u'-');
        return {{"t", "decimal"}, {"unscaled", digits}, {"scale", scale}};
    }
    const auto object = QJsonDocument::fromJson(text.toUtf8()).object();
    const auto tag = object.value("t").toString();
    if ((type == u"TIMESTAMP WITH TIME ZONE" && tag == u"timestamp") ||
        (type == u"RECORD" && tag == u"record") || (type == u"COLLECTION" && tag == u"collection"))
        return object;
    return {};
}

struct Query
{
    QString sql;
    QJsonObject types;
    QJsonObject values;

    QString parameter(const Column& column, const QJsonValue& value)
    {
        const auto name = QStringLiteral("grid_p%1").arg(types.size());
        types.insert(name, column.declaration);
        values.insert(name, value);
        return u':' + name;
    }
};


class GridModel final : public QAbstractTableModel
{
public:
    explicit GridModel(QObject* parent) : QAbstractTableModel(parent) {}

    void reset(Table table, QVector<QJsonArray> rows)
    {
        beginResetModel();
        columns_ = std::move(table.columns);
        rows_ = std::move(rows);
        page_ = 0;
        endResetModel();
    }

    void changePage(int page)
    {
        beginResetModel();
        page_ = page;
        endResetModel();
    }

    const QVector<QJsonArray>& rows() const { return rows_; }
    int page() const { return page_; }
    int pages() const { return std::max(1, (static_cast<int>(rows_.size()) + pageRows - 1) / pageRows); }
    int sourceRow(int displayRow) const { return page_ * pageRows + displayRow; }

    int rowCount(const QModelIndex& parent) const override
    {
        return parent.isValid() ? 0 : std::clamp(static_cast<int>(rows_.size()) - sourceRow(0), 0, pageRows);
    }
    int columnCount(const QModelIndex& parent) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(columns_.size());
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole) return {};
        if (orientation == Qt::Vertical) return sourceRow(section) + 1;
        return columns_.value(section).name;
    }
    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() >= rowCount({}) || index.column() >= columnCount({})) return {};
        const auto value = rows_.at(sourceRow(index.row())).at(index.column());
        if (role == Qt::DisplayRole || role == Qt::ToolTipRole) return displayCell(value, true);
        if (role == Qt::UserRole) return value.toObject();
        return {};
    }

private:
    QVector<Column> columns_;
    QVector<QJsonArray> rows_;
    int page_{};
};

enum class ChangeKind { insert, update, erase };
struct Change
{
    ChangeKind kind{};
    int sourceRow = -1;
    QJsonArray original;
    QHash<int, QJsonObject> fields;
};

void exportBlob(QWidget* parent, const QJsonObject& value)
{
    if (value.value("t").toString() != u"binary") return;
    const auto fileName = QFileDialog::getSaveFileName(parent, QObject::tr("Export binary data"));
    if (fileName.isEmpty()) return;
    const auto data = QByteArray::fromHex(value.value("hex").toString().toLatin1());
    QSaveFile output(fileName);
    if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size() || !output.commit())
        QMessageBox::warning(parent, QObject::tr("Binary export"), output.errorString());
}

class RowForm final : public QDialog
{
public:
    RowForm(const Table& table, const QJsonArray& row, bool editing, QWidget* parent)
        : QDialog(parent), table_(table), original_(row), editing_(editing), inserting_(row.isEmpty())
    {
        setObjectName("gridRowForm");
        setWindowTitle(inserting_ ? tr("Insert row") : tr("Row values"));
        resize(650, 610);
        auto* layout = new QVBoxLayout(this);
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        auto* fields = new QWidget;
        auto* form = new QFormLayout(fields);
        scroll->setWidget(fields);
        layout->addWidget(scroll);
        for (int i = 0; i < table.columns.size(); ++i)
        {
            const auto& column = table.columns.at(i);
            const auto value = inserting_ ? QJsonValue{} : row.at(i);
            auto* controls = new QWidget;
            auto* lines = new QVBoxLayout(controls);
            lines->setContentsMargins(0, 0, 0, 2);
            auto* input = new QPlainTextEdit;
            input->setObjectName(QStringLiteral("gridCell%1").arg(i));
            if (!inserting_) input->setPlainText(displayCell(value, false));
            input->document()->setModified(false);
            input->setMaximumHeight(62);
            input->setReadOnly(!editing || column.declaration.isEmpty());
            lines->addWidget(input);
            auto* options = new QHBoxLayout;
            lines->addLayout(options);
            auto* null = new QCheckBox(tr("NULL"));
            null->setObjectName(QStringLiteral("gridCellNull%1").arg(i));
            null->setChecked(isNull(value));
            null->setEnabled(editing && column.nullable);
            options->addWidget(null);
            auto* explicitValue = new QCheckBox(tr("Supply value"));
            explicitValue->setObjectName(QStringLiteral("gridCellSupplied%1").arg(i));
            explicitValue->setChecked(!inserting_);
            explicitValue->setVisible(inserting_);
            explicitValue->setEnabled(editing);
            options->addWidget(explicitValue);
            const auto sync = [input, null, explicitValue] {
                input->setEnabled(explicitValue->isChecked() && !null->isChecked());
            };
            connect(null, &QCheckBox::toggled, this, sync);
            connect(explicitValue, &QCheckBox::toggled, this, sync);
            sync();
            if (column.type == u"BLOB")
            {
                if (!inserting_ && !isNull(value))
                {
                    auto* save = new QPushButton(tr("Export binary…"));
                    options->addWidget(save);
                    connect(save, &QPushButton::clicked, this, [this, value] { exportBlob(this, value.toObject()); });
                }
                if (editing)
                {
                    auto* load = new QPushButton(tr("Load binary…"));
                    options->addWidget(load);
                    connect(load, &QPushButton::clicked, this, [this, input, null, explicitValue] {
                        const auto fileName = QFileDialog::getOpenFileName(this, tr("Load binary data"));
                        if (fileName.isEmpty()) return;
                        QFile file(fileName);
                        if (!file.open(QIODevice::ReadOnly) || file.size() > maxFieldBytes)
                        {
                            problem_->setText(tr("Choose a readable binary file of at most 256 KiB."));
                            return;
                        }
                        const auto data = file.read(maxFieldBytes + 1);
                        if (file.error() != QFileDevice::NoError || data.size() > maxFieldBytes)
                        {
                            problem_->setText(tr("The binary file changed or could not be read."));
                            return;
                        }
                        explicitValue->setChecked(true);
                        null->setChecked(false);
                        input->setPlainText(QString::fromLatin1(data.toHex()));
                        input->document()->setModified(true);
                    });
                }
            }
            options->addStretch();
            form->addRow(column.name + QStringLiteral(" · ") + column.type, controls);
            fields_.append({input, null, explicitValue});
        }
        problem_ = new QLabel;
        problem_->setTextFormat(Qt::PlainText);
        problem_->setWordWrap(true);
        layout->addWidget(problem_);
        auto* buttons = new QDialogButtonBox(editing ? QDialogButtonBox::Ok | QDialogButtonBox::Cancel
            : QDialogButtonBox::Close);
        layout->addWidget(buttons);
        if (editing)
        {
            buttons->button(QDialogButtonBox::Ok)->setText(tr("Stage row"));
            connect(buttons, &QDialogButtonBox::accepted, this, [this] { validateInput(); });
        }
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    const QHash<int, QJsonObject>& values() const { return changed_; }

private:
    struct Field
    {
        QPlainTextEdit* input;
        QCheckBox* null;
        QCheckBox* explicitValue;
    };

    void validateInput()
    {
        QHash<int, QJsonObject> values;
        for (int i = 0; i < fields_.size(); ++i)
        {
            const auto& field = fields_.at(i);
            const auto& column = table_.columns.at(i);
            if (!field.explicitValue->isChecked() || column.declaration.isEmpty()) continue;
            if (!inserting_ && field.null->isChecked() == isNull(original_.at(i)) &&
                !field.input->document()->isModified()) continue;
            const auto text = field.input->document()->toRawText().replace(QChar(0x2029), u'\n');
            if (!inserting_ && field.null->isChecked() == isNull(original_.at(i)) &&
                text == displayCell(original_.at(i), false)) continue;
            const auto encoded = encodeCell(column, text, field.null->isChecked());
            if (encoded.isEmpty())
            {
                problem_->setText(tr("Enter a valid %1 value for %2. Use the NULL option to store NULL.")
                    .arg(column.type, column.name));
                return;
            }
            if (inserting_ || encoded != original_.at(i).toObject()) values.insert(i, encoded);
        }
        if (inserting_ && values.isEmpty())
        {
            problem_->setText(tr("Supply at least one value. Omitted columns use their declared defaults."));
            return;
        }
        changed_ = std::move(values);
        accept();
    }

    const Table& table_;
    QJsonArray original_;
    bool editing_{};
    bool inserting_{};
    QVector<Field> fields_;
    QHash<int, QJsonObject> changed_;
    QLabel* problem_{};
};


class GridDialog final : public QDialog
{
public:
    GridDialog(SqlSession& session, const QJsonObject& tableMetadata, QWidget* parent)
        : QDialog(parent), session_(&session), table_(tableMetadata), tableMetadata_(tableMetadata)
    {
        setObjectName("workbenchDataGrid");
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowModality(Qt::WindowModal);
        setWindowTitle(tr("Data grid · %1").arg(table_.sql));
        resize(1040, 720);
        auto* pane = new QVBoxLayout(this);

        auto* filterBar = new QHBoxLayout;
        pane->addLayout(filterBar);
        filterBar->addWidget(new QLabel(tr("Filter")));
        filter_ = new QComboBox;
        filter_->setObjectName("gridFilterColumn");
        filter_->addItem(tr("All rows"), -1);
        filterBar->addWidget(filter_);
        filterValue_ = new QLineEdit;
        filterValue_->setObjectName("gridFilterValue");
        filterValue_->setPlaceholderText(tr("Exact typed value"));
        filterBar->addWidget(filterValue_, 1);
        filterNull_ = new QCheckBox(tr("Match NULL"));
        filterBar->addWidget(filterNull_);
        connect(filterNull_, &QCheckBox::toggled, filterValue_, &QLineEdit::setDisabled);
        filterBar->addWidget(new QLabel(tr("Sort")));
        sort_ = new QComboBox;
        sort_->setObjectName("gridSortColumn");
        sort_->addItem(tr("Server order"), -1);
        filterBar->addWidget(sort_);
        descending_ = new QCheckBox(tr("Descending"));
        filterBar->addWidget(descending_);
        for (int index = 0; index < table_.columns.size(); ++index)
        {
            const auto& column = table_.columns.at(index);
            if (column.type != u"RECORD" && column.type != u"COLLECTION")
                filter_->addItem(column.name, index);
            if (column.type != u"RECORD" && column.type != u"COLLECTION" && column.type != u"BLOB")
                sort_->addItem(column.name, index);
        }
        reload_ = button(filterBar, tr("Refresh"), "gridReload", [this] { refresh(); });
        cancel_ = button(filterBar, tr("Cancel"), "gridCancel", [this] { cancelWork(); });

        view_ = new QTableView;
        view_->setObjectName("gridRows");
        view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        view_->setSelectionBehavior(QAbstractItemView::SelectRows);
        view_->setSelectionMode(QAbstractItemView::SingleSelection);
        view_->setAlternatingRowColors(true);
        view_->horizontalHeader()->setDefaultSectionSize(145);
        model_ = new GridModel(view_);
        view_->setModel(model_);
        pane->addWidget(view_, 1);
        connect(view_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { sync(); });
        connect(view_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { sync(); });
        connect(view_, &QTableView::doubleClicked, this, [this] { rowForm(false); });

        auto* navBar = new QHBoxLayout;
        pane->addLayout(navBar);
        previous_ = button(navBar, tr("Previous"), "gridPrevious", [this] { paginate(-1); });
        next_ = button(navBar, tr("Next"), "gridNext", [this] { paginate(1); });
        pageLabel_ = new QLabel;
        navBar->addWidget(pageLabel_);
        navBar->addStretch();
        viewRow_ = button(navBar, tr("View row"), "gridViewRow", [this] { rowForm(false); });
        insert_ = button(navBar, tr("Insert"), "gridInsert", [this] { rowForm(true); });
        update_ = button(navBar, tr("Edit row"), "gridEditRow", [this] { rowForm(false, true); });
        erase_ = button(navBar, tr("Stage delete"), "gridDelete", [this] { stageDelete(); });
        export_ = button(navBar, tr("Export BLOB…"), "gridExportBlob", [this] { exportSelected(); });
        exportPage_ = button(navBar, tr("Export page…"), "gridExportPage", [this] { exportPage(); });

        auto* changeBar = new QHBoxLayout;
        pane->addLayout(changeBar);
        copyInsert_ = button(changeBar, tr("Copy as INSERT"), "gridCopyInsert", [this] { copyInsert(); });
        copyUpdate_ = button(changeBar, tr("Copy as UPDATE"), "gridCopyUpdate", [this] { copyUpdate(); });
        generate_ = button(changeBar, tr("Generate sample rows…"), "gridGenerate", [this] { generate(); });
        changeBar->addStretch();
        discard_ = button(changeBar, tr("Discard selected"), "gridDiscard", [this] { discard(); });
        apply_ = button(changeBar, tr("Apply changes"), "gridApply", [this] { applyChanges(); });

        changesView_ = new QListWidget;
        changesView_->setObjectName("gridPendingChanges");
        changesView_->setMaximumHeight(84);
        connect(changesView_, &QListWidget::currentRowChanged, this, [this] { sync(); });
        pane->addWidget(changesView_);
        status_ = new QLabel;
        status_->setObjectName("gridStatus");
        status_->setTextFormat(Qt::PlainText);
        status_->setWordWrap(true);
        pane->addWidget(status_);

        if (table_.valid()) refresh();
        else
        {
            status_->setText(tr("This object has no current table metadata. Refresh Object Explorer and try again."));
            sync();
        }
    }

    void reject() override
    {
        if (busy_)
        {
            status_->setText(tr("Wait for completion or cancel the active operation before closing."));
            return;
        }
        if (!changes_.isEmpty() && QMessageBox::question(this, tr("Pending changes"),
                tr("Discard the staged changes and close the grid?"), QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) != QMessageBox::Yes) return;
        QDialog::reject();
    }

private:
    QPushButton* button(QHBoxLayout* layout, const QString& label, const char* name, auto fn)
    {
        auto* control = new QPushButton(label);
        control->setObjectName(name);
        layout->addWidget(control);
        connect(control, &QPushButton::clicked, this, std::move(fn));
        return control;
    }

    bool sameTable(const Table& other) const
    {
        return other.valid() && table_.id == other.id && table_.sql == other.sql &&
            table_.schemaVersion == other.schemaVersion;
    }

    Table metadataTable(const SqlReply& reply) const
    {
        for (const auto& entry : reply.data.value("objects").toArray())
        {
            const auto object = entry.toObject();
            if (object.value("id").toString() == table_.id) return Table(object);
        }
        return Table(QJsonObject{});
    }

    void fail(const QString& message)
    {
        busy_ = false;
        committing_ = false;
        status_->setText(message);
        sync();
    }

    static QString diagnostic(const SqlReply& reply)
    {
        const auto message = QString::fromUtf8(reply.error.message);
        return message.isEmpty() ? status(reply.error.code) : message;
    }

    void refresh()
    {
        if (!table_.valid() || busy_ || !changes_.isEmpty()) return;
        if (!session_ || session_->busy() || !session_->state().connected || session_->state().transactionActive
            || !session_->state().autocommit)
        {
            status_->setText(tr("The grid needs an idle connection with autocommit enabled. Finish the worksheet transaction first."));
            sync();
            return;
        }

        Query query{QStringLiteral("SELECT * FROM ") + table_.sql};
        const auto filter = filter_->currentData().toInt();
        if (filter >= 0)
        {
            const auto& column = table_.columns.at(filter);
            query.sql += QStringLiteral(" WHERE ") + column.sql;
            if (filterNull_->isChecked()) query.sql += QStringLiteral(" IS NULL");
            else
            {
                const auto value = encodeCell(column, filterValue_->text(), false);
                if (column.declaration.isEmpty() || value.isEmpty())
                {
                    status_->setText(tr("Enter a valid %1 filter value.").arg(column.type));
                    return;
                }
                query.sql += QStringLiteral(" = ") + query.parameter(column, value);
            }
        }
        const auto sort = sort_->currentData().toInt();
        if (sort >= 0)
            query.sql += QStringLiteral(" ORDER BY ") + table_.columns.at(sort).sql
                + (descending_->isChecked() ? " DESC" : " ASC");

        busy_ = true;
        stopRequested_ = false;
        status_->setText(tr("Reading table metadata…"));
        sync();
        QPointer<GridDialog> self(this);
        if (!session_->refreshMetadata([self, query = std::move(query)](const SqlReply& reply) {
            if (!self) return;
            if (reply.error.code != SDP_OK) { self->fail(diagnostic(reply)); return; }
            if (self->stopRequested_) { self->fail(tr("Cancelled.")); return; }
            auto table = self->metadataTable(reply);
            if (!self->sameTable(table))
            {
                self->stale_ = true;
                self->fail(tr("The table identity or schema changed. Open it again from Object Explorer."));
                return;
            }
            self->readRows(query, std::move(table));
        })) fail(tr("The worksheet cannot read metadata right now."));
    }

    using RowResult = std::function<void(const SqlReply&, QVector<QJsonArray>)>;
    bool select(Query query, int limit, RowResult result)
    {
        if (!session_) return false;
        struct Batch
        {
            QVector<QJsonArray> rows;
            bool malformed{};
        };
        auto output = std::make_shared<Batch>();
        QPointer<GridDialog> self(this);
        const auto columns = table_.columns.size();
        return session_->execute({SqlStatement{query.sql}}, std::move(query.types), std::move(query.values),
            {limit, 10000}, [output, columns, limit](const SqlEvent& event) {
                if (event.kind != SqlEvent::Kind::batch) return;
                for (const auto& entry : event.data.value("rows").toArray())
                {
                    const auto row = entry.toArray();
                    if (row.size() != columns || output->rows.size() == limit)
                    {
                        output->malformed = true;
                        break;
                    }
                    output->rows.append(row);
                }
            }, [self, output, result = std::move(result)](const SqlReply& reply) {
                if (!self) return;
                if (output->malformed)
                {
                    auto error = reply;
                    error.error = {};
                    error.error.code = SDP_MALFORMED_MESSAGE;
                    result(error, {});
                    return;
                }
                result(reply, std::move(output->rows));
            });
    }

    void readRows(const Query& query, Table before)
    {
        status_->setText(tr("Reading rows…"));
        if (!select(query, snapshotRows + 1, [this, before = std::move(before)](const SqlReply& reply,
                QVector<QJsonArray> rows) {
            if (reply.error.code != SDP_OK) { fail(diagnostic(reply)); return; }
            if (stopRequested_) { fail(tr("Cancelled.")); return; }
            QPointer<GridDialog> self(this);
            if (!session_->refreshMetadata([self, before, rows = std::move(rows), capped = reply.resultsTruncated]
                    (const SqlReply& after) mutable {
                if (!self) return;
                if (after.error.code != SDP_OK) { self->fail(diagnostic(after)); return; }
                if (self->stopRequested_) { self->fail(tr("Cancelled.")); return; }
                auto current = self->metadataTable(after);
                if (!self->sameTable(current) || before.revision != current.revision || before.keys != current.keys)
                {
                    self->stale_ = true;
                    self->fail(tr("The table changed while rows were loading. Refresh the grid."));
                    return;
                }

                self->capped_ = capped || rows.size() > snapshotRows;
                if (rows.size() > snapshotRows) rows.resize(snapshotRows);
                self->table_ = std::move(current);
                self->model_->reset(self->table_, std::move(rows));
                self->snapshotValid_ = true;
                self->stale_ = false;
                self->fail(self->mayEdit()
                    ? tr("Rows loaded. Edits are staged until you apply them.")
                    : tr("Rows loaded in read-only mode. Editing needs a verified row key, revision and write permission."));
            })) fail(tr("The worksheet could not verify the row snapshot."));
        })) fail(tr("The worksheet cannot read rows right now."));
    }

    void paginate(int delta)
    {
        const auto page = model_->page() + delta;
        if (page < 0 || page >= model_->pages()) return;
        model_->changePage(page);
        sync();
    }


    bool mayEdit() const
    {
        return snapshotValid_ && !stale_ && session_ && session_->state().connected &&
            !session_->profile().read_only && table_.editable()
            && (table_.allowed("insert") || table_.allowed("update") || table_.allowed("delete"));
    }

    int selectedRow() const
    {
        const auto selection = view_->selectionModel()->selectedRows();
        return selection.isEmpty() ? -1 : model_->sourceRow(selection.first().row());
    }

    bool locatable(int row) const
    {
        if (!table_.editable() || row < 0 || row >= model_->rows().size()) return false;
        const auto& values = model_->rows().at(row);
        for (const auto index : table_.keys)
            if (!values.at(index).isObject() || isNull(values.at(index))) return false;
        return true;
    }

    void sync()
    {
        const bool editing = !busy_ && mayEdit();
        const auto row = selectedRow();
        const bool selected = !busy_ && row >= 0 && row < model_->rows().size();
        const bool located = selected && locatable(row);
        const bool canAdd = editing && table_.allowed("insert") && changes_.size() < maxChanges;
        reload_->setEnabled(!busy_ && changes_.isEmpty() && table_.valid());
        cancel_->setEnabled(busy_ && !committing_);
        previous_->setEnabled(!busy_ && model_->page() > 0);
        next_->setEnabled(!busy_ && model_->page() + 1 < model_->pages());
        if (model_->rows().isEmpty()) pageLabel_->setText(tr("No rows"));
        else
        {
            const auto first = model_->sourceRow(0) + 1;
            const auto last = first + model_->rowCount({}) - 1;
            const auto format = capped_ ? tr("Rows %1–%2 of first %3") : tr("Rows %1–%2 of %3");
            pageLabel_->setText(format.arg(first).arg(last).arg(model_->rows().size()));
        }
        viewRow_->setEnabled(selected);
        insert_->setEnabled(canAdd);
        update_->setEnabled(editing && located && table_.allowed("update"));
        erase_->setEnabled(editing && located && table_.allowed("delete"));
        generate_->setEnabled(canAdd);
        copyInsert_->setEnabled(selected);
        copyUpdate_->setEnabled(located);
        const auto index = view_->currentIndex();
        export_->setEnabled(selected && index.isValid() && index.column() >= 0 &&
            index.column() < model_->rows().at(row).size() &&
            model_->rows().at(row).at(index.column()).toObject().value("t").toString() == u"binary");
        exportPage_->setEnabled(!busy_ && model_->rowCount({}) > 0);
        discard_->setEnabled(!busy_ && changesView_->currentRow() >= 0);
        apply_->setEnabled(editing && !changes_.isEmpty());
    }

    void rowForm(bool insert, bool edit = false)
    {
        if (busy_) return;
        const auto row = selectedRow();
        if (insert && (!mayEdit() || !table_.allowed("insert"))) return;
        if (!insert && row < 0) return;
        if (edit && (!mayEdit() || !table_.allowed("update") || !locatable(row))) return;
        auto original = insert ? QJsonArray{} : model_->rows().at(row);
        const auto existing = existingChange(row);
        if (edit && existing >= 0 && changes_.at(existing).kind == ChangeKind::erase)
        {
            status_->setText(tr("Discard the staged delete before editing this row."));
            return;
        }
        auto displayed = original;
        if (!insert && existing >= 0 && changes_.at(existing).kind == ChangeKind::update)
            for (auto it = changes_[existing].fields.cbegin(); it != changes_[existing].fields.cend(); ++it)
                displayed.replace(it.key(), it.value());
        RowForm form(table_, displayed, insert || edit, this);
        if (form.exec() != QDialog::Accepted) return;
        auto fields = form.values();
        if (edit && existing >= 0)
        {
            fields = changes_.at(existing).fields;
            for (auto it = form.values().cbegin(); it != form.values().cend(); ++it)
                if (it.value() == original.at(it.key()).toObject()) fields.remove(it.key());
                else fields.insert(it.key(), it.value());
        }
        if (fields.isEmpty())
        {
            if (existing >= 0)
            {
                changes_.removeAt(existing);
                showChanges();
            }
            return;
        }
        stage({insert ? ChangeKind::insert : ChangeKind::update, insert ? -1 : row,
            std::move(original), std::move(fields)});
    }

    void stageDelete()
    {
        const auto row = selectedRow();
        if (busy_ || !mayEdit() || !table_.allowed("delete") || !locatable(row)) return;
        stage({ChangeKind::erase, row, model_->rows().at(row), {}});
    }

    int existingChange(int row) const
    {
        if (row < 0) return -1;
        for (int index = 0; index < changes_.size(); ++index)
            if (changes_.at(index).sourceRow == row) return index;
        return -1;
    }

    void stage(Change change)
    {
        const auto current = existingChange(change.sourceRow);
        if (current >= 0) changes_[current] = std::move(change);
        else if (changes_.size() < maxChanges) changes_.append(std::move(change));
        else
        {
            status_->setText(tr("Apply or discard some changes before staging more rows."));
            return;
        }
        showChanges();
    }

    void showChanges()
    {
        changesView_->clear();
        for (const auto& item : changes_)
        {
            auto label = item.kind == ChangeKind::insert ? tr("Insert row") :
                item.kind == ChangeKind::erase ? tr("Delete row %1").arg(item.sourceRow + 1) :
                tr("Update row %1").arg(item.sourceRow + 1);
            QStringList fields;
            for (int index = 0; index < table_.columns.size(); ++index)
                if (item.fields.contains(index)) fields.append(table_.columns.at(index).name);
            if (!fields.isEmpty()) label += QStringLiteral(": ") + fields.join(QStringLiteral(", "));
            changesView_->addItem(label);
        }
        if (!changes_.isEmpty()) changesView_->setCurrentRow(static_cast<int>(changes_.size()) - 1);
        status_->setText(tr("%1 pending changes.").arg(changes_.size()));
        sync();
    }

    void discard()
    {
        const auto index = changesView_->currentRow();
        if (busy_ || index < 0 || index >= changes_.size()) return;
        changes_.removeAt(index);
        showChanges();
    }

    void exportSelected()
    {
        const auto row = selectedRow();
        const auto column = view_->currentIndex();
        if (row >= 0 && column.isValid()) exportBlob(this, model_->rows().at(row).at(column.column()).toObject());
    }

    void exportPage()
    {
        if (busy_ || model_->rowCount({}) == 0) return;
        const auto csv = tr("CSV (*.csv)");
        const auto json = tr("Typed JSON (*.json)");
        QString selected = json;
        const auto fileName = QFileDialog::getSaveFileName(this, tr("Export visible rows"), {},
            csv + QStringLiteral(";;") + json, &selected);
        if (fileName.isEmpty()) return;
        QVector<QJsonArray> rows;
        for (int index = 0; index < model_->rowCount({}); ++index)
            rows.append(model_->rows().at(model_->sourceRow(index)));
        QString error;
        const auto format = selected == csv ? transfer::Format::csv : transfer::Format::json;
        const auto contents = transfer::encode(format, tableMetadata_, tableMetadata_.value("columns").toArray(),
            rows, {}, error).toUtf8();
        if (error.isEmpty())
        {
            QSaveFile output(fileName);
            if (output.open(QIODevice::WriteOnly) && output.write(contents) == contents.size() && output.commit())
            {
                status_->setText(tr("Exported %1 rows from this page.").arg(rows.size()));
                return;
            }
            error = output.errorString();
        }
        QMessageBox::warning(this, tr("Export visible rows"), error);
    }

    QString keyPredicate(const QJsonArray& original, Query& query) const
    {
        QStringList clauses;
        for (const auto key : table_.keys)
        {
            const auto& column = table_.columns.at(key);
            const auto value = original.at(key);
            if (!value.isObject() || isNull(value)) return {};
            clauses.append(column.sql + QStringLiteral(" = ") + query.parameter(column, value));
        }
        return clauses.join(QStringLiteral(" AND "));
    }

    Query changeSql(const Change& change) const
    {
        Query query;
        QStringList fields, assignments;
        auto ordinals = change.fields.keys();
        std::sort(ordinals.begin(), ordinals.end());
        for (const auto index : ordinals)
        {
            const auto& column = table_.columns.at(index);
            fields.append(column.sql);
            const auto parameter = query.parameter(column, change.fields.value(index));
            assignments.append(change.kind == ChangeKind::insert
                ? parameter : column.sql + QStringLiteral(" = ") + parameter);
        }
        if (change.kind == ChangeKind::insert)
        {
            query.sql = QStringLiteral("INSERT INTO %1 (%2) VALUES (%3)")
                .arg(table_.sql, fields.join(u','), assignments.join(u','));
            return query;
        }

        const auto where = keyPredicate(change.original, query);
        if (where.isEmpty()) return {};
        if (change.kind == ChangeKind::update)
            query.sql = QStringLiteral("UPDATE %1 SET %2 WHERE %3").arg(table_.sql, assignments.join(u','), where);
        else query.sql = QStringLiteral("DELETE FROM %1 WHERE %2").arg(table_.sql, where);
        return query;
    }

    void showSql(const Query& query)
    {
        if (query.sql.isEmpty()) return;
        auto* modal = new QDialog(this);
        modal->setObjectName("gridCopiedQuery");
        modal->setAttribute(Qt::WA_DeleteOnClose);
        modal->setWindowTitle(tr("Parameterized SQL"));
        modal->resize(720, 420);
        auto* contents = new QVBoxLayout(modal);
        auto* sql = new QPlainTextEdit(query.sql);
        sql->setObjectName("gridCopySqlText");
        sql->setReadOnly(true);
        contents->addWidget(sql);
        auto* buttons = new QHBoxLayout;
        contents->addLayout(buttons);
        button(buttons, tr("Copy SQL"), "gridCopySql", [sql] { QApplication::clipboard()->setText(sql->toPlainText()); });
        auto* binds = new QPlainTextEdit(QString::fromUtf8(QJsonDocument(QJsonObject{
            {"parameter_types", query.types}, {"parameters", query.values}}).toJson(QJsonDocument::Indented)));
        binds->setObjectName("gridCopyBindText");
        binds->setReadOnly(true);
        contents->addWidget(new QLabel(tr("Bind values")));
        contents->addWidget(binds);
        auto* copy = new QPushButton(tr("Copy bind values"));
        contents->addWidget(copy);
        connect(copy, &QPushButton::clicked, modal, [binds] { QApplication::clipboard()->setText(binds->toPlainText()); });
        modal->setWindowModality(Qt::WindowModal);
        modal->show();
    }

    void copyInsert()
    {
        const auto row = selectedRow();
        if (row < 0) return;
        QHash<int, QJsonObject> fields;
        for (int index = 0; index < table_.columns.size(); ++index)
        {
            if (table_.columns.at(index).declaration.isEmpty()) return;
            fields.insert(index, model_->rows().at(row).at(index).toObject());
        }
        showSql(changeSql({ChangeKind::insert, -1, {}, std::move(fields)}));
    }

    void copyUpdate()
    {
        const auto row = selectedRow();
        if (!locatable(row)) return;
        QHash<int, QJsonObject> fields;
        for (int index = 0; index < table_.columns.size(); ++index)
        {
            if (table_.keys.contains(index) || table_.columns.at(index).declaration.isEmpty()) continue;
            fields.insert(index, model_->rows().at(row).at(index).toObject());
        }
        if (fields.isEmpty())
        {
            status_->setText(tr("There are no writable non-key columns in this row."));
            return;
        }
        showSql(changeSql({ChangeKind::update, row, model_->rows().at(row), std::move(fields)}));
    }

    static QJsonObject sampleCell(const Column& column, int number)
    {
        const auto type = column.declaration.value("type").toString();
        if (type == u"BIGINT") return {{"t", "i64"}, {"v", QString::number(number)}};
        if (type == u"TEXT") return {{"t", "text"}, {"v", QStringLiteral("sample_%1").arg(number)}};
        if (type == u"BOOLEAN") return {{"t", "bool"}, {"v", bool(number % 2)}};
        if (type == u"DECIMAL")
        {
            const auto p = column.declaration.value("precision").toInt();
            const auto s = column.declaration.value("scale").toInt();
            return {{"t", "decimal"}, {"unscaled", QString::number(p == 1 ? number % 10 : number % 100)}, {"scale", s}};
        }
        if (type == u"BLOB") return {{"t", "binary"}, {"hex", "53414d504c45"}};
        if (type == u"DATE") return {{"t", "text"}, {"v", "2000-01-01"}};
        if (type == u"TIME") return {{"t", "text"}, {"v", "12:00:00"}};
        if (type == u"TIMESTAMP WITH TIME ZONE")
            return {{"t", "timestamp"}, {"seconds", "946728000"}, {"nanos", 0}, {"offset_minutes", 0}};
        if (type == u"RECORD") return {{"t", "record"}, {"fields", QJsonArray{}}};
        if (type == u"COLLECTION") return {{"t", "collection"}, {"items", QJsonArray{}}};
        return {};
    }

    void generate()
    {
        if (busy_ || !mayEdit() || !table_.allowed("insert") || changes_.size() >= maxChanges) return;
        bool proceed = false;
        const int max = maxChanges - static_cast<int>(changes_.size());
        const int count = QInputDialog::getInt(this, tr("Generate sample rows"),
            tr("Number of synthetic rows to stage. The server checks table constraints when you apply."),
            std::min(5, max), 1, max, 1, &proceed);
        if (!proceed) return;
        for (int row = 0; row < count; ++row)
        {
            QHash<int, QJsonObject> fields;
            for (int column = 0; column < table_.columns.size(); ++column)
            {
                const auto& metadata = table_.columns.at(column);
                if (metadata.nullable && !table_.keys.contains(column)) continue;
                auto sample = sampleCell(metadata, syntheticNumber_ + row);
                if (!sample.isEmpty()) fields.insert(column, std::move(sample));
            }
            if (fields.isEmpty()) continue;
            changes_.append({ChangeKind::insert, -1, {}, std::move(fields)});
        }
        syntheticNumber_ += count;
        showChanges();
    }


    void applyChanges()
    {
        if (busy_ || !mayEdit() || changes_.isEmpty() || !session_ || session_->busy()
            || session_->state().transactionActive || !session_->state().autocommit)
        {
            status_->setText(tr("Finish the current worksheet transaction before applying grid changes."));
            return;
        }
        if (session_->profile().environment == u"prod" &&
            QMessageBox::question(this, tr("Production write"),
                tr("Apply %1 changes to %2?").arg(changes_.size()).arg(table_.sql),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;

        busy_ = true;
        stopRequested_ = false;
        applyingIndex_ = 0;
        status_->setText(tr("Checking the snapshot under a write transaction…"));
        sync();
        QPointer<GridDialog> self(this);
        if (!session_->transaction(SqlTransaction::begin, {}, [self](const SqlReply& reply) {
            if (!self) return;
            if (reply.error.code != SDP_OK) { self->fail(diagnostic(reply)); return; }
            if (self->stopRequested_) { self->rollback(tr("Cancelled.")); return; }
            self->checkSnapshot();
        })) fail(tr("The worksheet cannot begin a grid transaction right now."));
    }

    void checkSnapshot()
    {
        QPointer<GridDialog> self(this);
        if (!session_->refreshMetadata([self](const SqlReply& reply) {
            if (!self) return;
            if (reply.error.code != SDP_OK) { self->rollback(diagnostic(reply)); return; }
            if (self->stopRequested_) { self->rollback(tr("Cancelled.")); return; }
            const auto live = self->metadataTable(reply);
            if (!self->sameTable(live) || live.revision.isEmpty() || live.revision != self->table_.revision ||
                live.keys != self->table_.keys)
            {
                self->stale_ = true;
                self->rollback(tr("Conflict: the revision or verified row key has changed. Review the rows in a fresh grid."));
                return;
            }
            for (const auto& change : self->changes_)
            {
                const auto access = change.kind == ChangeKind::insert ? "insert"
                    : change.kind == ChangeKind::update ? "update" : "delete";
                if (!live.allowed(access))
                {
                    self->stale_ = true;
                    self->rollback(tr("The current server permissions no longer allow a staged operation."));
                    return;
                }
            }
            self->applyNext();
        })) rollback(tr("The worksheet cannot verify the write revision."));
    }

    void applyNext()
    {
        if (stopRequested_) { rollback(tr("Cancelled.")); return; }
        if (applyingIndex_ == changes_.size()) { commit(); return; }
        status_->setText(tr("Applying change %1 of %2…").arg(applyingIndex_ + 1).arg(changes_.size()));
        const auto& item = changes_.at(applyingIndex_);
        if (item.kind == ChangeKind::insert) { applyOne(); return; }

        Query probe{QStringLiteral("SELECT * FROM ") + table_.sql};
        const auto where = keyPredicate(item.original, probe);
        if (where.isEmpty()) { rollback(tr("The row no longer has a proven key.")); return; }
        probe.sql += QStringLiteral(" WHERE ") + where;
        if (!select(std::move(probe), 2, [this](const SqlReply& reply, QVector<QJsonArray> rows) {
                if (reply.error.code != SDP_OK) { rollback(diagnostic(reply)); return; }
                if (stopRequested_) { rollback(tr("Cancelled.")); return; }
                if (rows.size() != 1 || rows.first() != changes_.at(applyingIndex_).original)
                {
                    stale_ = true;
                    rollback(tr("Conflict: the selected row was removed or changed."));
                    return;
                }
                applyOne();
            })) rollback(tr("The worksheet cannot verify the selected row."));
    }

    void applyOne()
    {
        if (!session_ || stopRequested_) { rollback(tr("Cancelled.")); return; }
        const auto query = changeSql(changes_.at(applyingIndex_));
        if (query.sql.isEmpty() || QJsonDocument(query.values).toJson(QJsonDocument::Compact).size() > 768 * 1024)
        {
            rollback(tr("The staged row is outside the limit for a single grid write."));
            return;
        }
        QPointer<GridDialog> self(this);
        if (!session_->execute({SqlStatement{query.sql}}, query.types, query.values, {1, 10000}, {},
            [self](const SqlReply& reply) {
                if (!self) return;
                if (reply.error.code != SDP_OK) { self->rollback(diagnostic(reply)); return; }
                if (reply.data.value("affected_rows").toInteger(-1) != 1)
                {
                    self->stale_ = true;
                    self->rollback(tr("Conflict: a write did not affect exactly one row."));
                    return;
                }
                ++self->applyingIndex_;
                self->applyNext();
            })) rollback(tr("The worksheet cannot apply the staged write."));
    }

    void rollback(const QString& reason)
    {
        if (!session_ || !session_->state().transactionActive)
        {
            fail(reason);
            return;
        }
        QPointer<GridDialog> self(this);
        if (!session_->transaction(SqlTransaction::rollback, {}, [self, reason](const SqlReply& reply) {
            if (!self) return;
            if (reply.error.code != SDP_OK) { self->fail(tr("%1 Rollback response: %2").arg(reason, diagnostic(reply))); return; }
            self->fail(tr("%1 The grid transaction was rolled back; staged changes remain.").arg(reason));
        })) fail(tr("%1 The worksheet could not send rollback.").arg(reason));
    }

    void commit()
    {
        if (!session_ || stopRequested_) { rollback(tr("Cancelled.")); return; }
        committing_ = true;
        status_->setText(tr("Waiting for commit confirmation…"));
        sync();
        QPointer<GridDialog> self(this);
        if (!session_->transaction(SqlTransaction::commit, {}, [self](const SqlReply& reply) {
            if (!self) return;
            if (reply.error.code != SDP_OK)
            {
                if (reply.error.unknown_outcome)
                {
                    self->stale_ = true;
                    self->fail(tr("Commit outcome is unknown. Confirm the worksheet request token before acting: %1")
                        .arg(reply.requestToken));
                }
                else self->rollback(diagnostic(reply));
                return;
            }

            const auto count = self->changes_.size();
            self->changes_.clear();
            self->changesView_->clear();
            self->snapshotValid_ = false;
            self->fail(tr("Committed %1 changes. Refresh the grid to see the updated rows.").arg(count));
        })) rollback(tr("The worksheet could not send commit."));
    }

    void cancelWork()
    {
        if (!busy_ || committing_) return;
        stopRequested_ = true;
        if (session_) session_->cancel();
    }

    QPointer<SqlSession> session_;
    Table table_;
    const QJsonObject tableMetadata_;
    GridModel* model_{};
    QTableView* view_{};
    QComboBox* filter_{};
    QComboBox* sort_{};
    QLineEdit* filterValue_{};
    QCheckBox* filterNull_{};
    QCheckBox* descending_{};
    QLabel* pageLabel_{};
    QLabel* status_{};
    QListWidget* changesView_{};
    QPushButton* reload_{};
    QPushButton* cancel_{};
    QPushButton* previous_{};
    QPushButton* next_{};
    QPushButton* viewRow_{};
    QPushButton* insert_{};
    QPushButton* update_{};
    QPushButton* erase_{};
    QPushButton* export_{};
    QPushButton* exportPage_{};
    QPushButton* copyInsert_{};
    QPushButton* copyUpdate_{};
    QPushButton* generate_{};
    QPushButton* discard_{};
    QPushButton* apply_{};
    QVector<Change> changes_;
    qsizetype applyingIndex_{};
    int syntheticNumber_{1'000'001};
    bool snapshotValid_{};
    bool capped_{};
    bool stale_{};
    bool busy_{};
    bool committing_{};
    bool stopRequested_{};
};
}

QDialog* openDataGrid(SqlSession& session, const QJsonObject& tableMetadata, QWidget* parent)
{
    auto* dialog = new GridDialog(session, tableMetadata, parent);
    dialog->show();
    return dialog;
}
}

