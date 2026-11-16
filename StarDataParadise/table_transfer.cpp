#include "table_transfer.hpp"
#include "table_codec.hpp"
#include "sql_session.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QStringConverter>
#include <QTimer>
#include <QVBoxLayout>
#include <memory>

namespace wb
{
namespace
{
class TransferDialog final : public QDialog
{
public:
    TransferDialog(SqlSession& session, QJsonObject table, QWidget* parent,
        std::function<void(const QString&)> reviewSql, std::function<void()> refresh);
    void reject() override;

private:
    void browse();
    void preview();
    void apply();
    void nextRow();
    void rollback(const QString& reason);
    void startExport();
    void showState(const QString& value);
    bool sameSchema(const QJsonObject& metadata) const;
    transfer::Format format() const;
    transfer::Options options() const;
    QStringConverter::Encoding encoding() const;
    QString error(const SqlReply& reply) const;
    void busy(bool active);

    SqlSession& session_;
    const QJsonObject table_;
    const QString target_;
    const QJsonArray columns_;
    const std::function<void(const QString&)> reviewSql_;
    const std::function<void()> refresh_;
    QComboBox* direction_{};
    QComboBox* format_{};
    QComboBox* encoding_{};
    QComboBox* delimiter_{};
    QComboBox* decimal_{};
    QComboBox* date_{};
    QLineEdit* file_{};
    QLineEdit* null_{};
    QCheckBox* escaped_{};
    QCheckBox* skip_{};
    QCheckBox* all_{};
    QSpinBox* limit_{};
    QPushButton* preview_{};
    QPushButton* apply_{};
    QPushButton* cancel_{};
    QPlainTextEdit* report_{};
    QLabel* status_{};
    QProgressBar* progress_{};
    transfer::Parsed parsed_;
    QString sql_;
    QString previewPath_;
    QString previewFingerprint_;
    QVector<QJsonArray> exportRows_;
    QJsonObject types_;
    transfer::Options runOptions_;
    qsizetype index_{}, exportBytes_{};
    int inserted_{}, runtimeRejected_{};
    bool active_{}, cancelling_{}, exportBudgetExceeded_{};
};

TransferDialog::TransferDialog(SqlSession& session, QJsonObject table, QWidget* parent,
    std::function<void(const QString&)> reviewSql, std::function<void()> refresh)
    : QDialog(parent), session_(session), table_(std::move(table)), target_(transfer::relation(table_)),
      columns_(table_.value("columns").toArray()), reviewSql_(std::move(reviewSql)), refresh_(std::move(refresh))
{
    setObjectName("tableTransfer");
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowModality(Qt::WindowModal);
    setWindowTitle(tr("Table import and export"));
    resize(670, 590);
    auto* layout = new QVBoxLayout(this);
    auto* target = new QLabel(target_);
    target->setTextFormat(Qt::PlainText);
    layout->addWidget(target);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    direction_ = new QComboBox;
    direction_->setObjectName("transferDirection");
    direction_->addItems({tr("Export"), tr("Import")});
    const auto permissions = table_.value("permissions").toObject();
    if (!permissions.value("select").toBool() && permissions.value("insert").toBool())
        direction_->setCurrentIndex(1);
    form->addRow(tr("Operation"), direction_);
    format_ = new QComboBox;
    format_->setObjectName("transferFormat");
    format_->addItems({QStringLiteral("CSV"), tr("Typed JSON"), QStringLiteral("SQL")});
    form->addRow(tr("Format"), format_);
    encoding_ = new QComboBox;
    encoding_->addItems({QStringLiteral("UTF-8"), QStringLiteral("UTF-16 LE"), QStringLiteral("UTF-16 BE")});
    form->addRow(tr("Encoding"), encoding_);
    delimiter_ = new QComboBox;
    delimiter_->addItems({tr("Comma"), tr("Semicolon"), tr("Tab")});
    form->addRow(tr("CSV delimiter"), delimiter_);
    decimal_ = new QComboBox;
    decimal_->addItems({tr("Decimal point"), tr("Decimal comma")});
    form->addRow(tr("CSV decimals"), decimal_);
    date_ = new QComboBox;
    date_->addItems({QStringLiteral("yyyy-MM-dd"), QStringLiteral("dd.MM.yyyy")});
    form->addRow(tr("CSV date format"), date_);
    null_ = new QLineEdit(QStringLiteral("NULL"));
    null_->setMaxLength(32);
    form->addRow(tr("Unquoted CSV NULL marker"), null_);
    escaped_ = new QCheckBox(tr("Read spreadsheet escaping from a Workbench CSV"));
    escaped_->setChecked(false);
    form->addRow(escaped_);
    skip_ = new QCheckBox(tr("Skip rejected rows and commit accepted rows"));
    skip_->setObjectName("transferSkipRejected");
    form->addRow(skip_);
    all_ = new QCheckBox(tr("Export all rows up to the safety limit"));
    all_->setChecked(true);
    form->addRow(all_);
    limit_ = new QSpinBox;
    limit_->setRange(1, transfer::maximumRows);
    limit_->setValue(250);
    limit_->setEnabled(false);
    form->addRow(tr("Export first N rows"), limit_);
    connect(all_, &QCheckBox::toggled, this, [this](bool all) { limit_->setEnabled(!all && !active_); });

    auto* path = new QHBoxLayout;
    file_ = new QLineEdit;
    file_->setObjectName("transferFile");
    path->addWidget(file_, 1);
    auto* pick = new QPushButton(tr("Browse…"));
    connect(pick, &QPushButton::clicked, this, [this] { browse(); });
    path->addWidget(pick);
    form->addRow(tr("File"), path);
    report_ = new QPlainTextEdit;
    report_->setObjectName("transferReport");
    report_->setReadOnly(true);
    layout->addWidget(report_, 1);
    status_ = new QLabel;
    status_->setObjectName("transferStatus");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    layout->addWidget(status_);
    progress_ = new QProgressBar;
    progress_->setVisible(false);
    layout->addWidget(progress_);

    auto* buttons = new QHBoxLayout;
    layout->addLayout(buttons);
    preview_ = new QPushButton(tr("Preview"));
    preview_->setObjectName("previewTransfer");
    apply_ = new QPushButton(tr("Apply"));
    apply_->setObjectName("applyTransfer");
    apply_->setEnabled(false);
    cancel_ = new QPushButton(tr("Close"));
    cancel_->setObjectName("cancelTransfer");
    buttons->addStretch();
    buttons->addWidget(preview_);
    buttons->addWidget(apply_);
    buttons->addWidget(cancel_);
    connect(preview_, &QPushButton::clicked, this, [this] { preview(); });
    connect(apply_, &QPushButton::clicked, this, [this] { apply(); });
    connect(cancel_, &QPushButton::clicked, this, [this] { reject(); });
    const auto invalidate = [this] { if (!active_) apply_->setEnabled(false); };
    for (auto* combination : {direction_, format_, encoding_, delimiter_, decimal_, date_})
        connect(combination, &QComboBox::currentIndexChanged, this, invalidate);
    for (auto* check : {escaped_, skip_, all_}) connect(check, &QCheckBox::toggled, this, invalidate);
    connect(file_, &QLineEdit::textChanged, this, invalidate);
    connect(null_, &QLineEdit::textChanged, this, invalidate);
    connect(limit_, &QSpinBox::valueChanged, this, invalidate);
    connect(direction_, &QComboBox::currentIndexChanged, this, [this] { preview_->setText(tr("Preview")); });
    showState(tr("Select a file and review the transfer before applying."));
}


transfer::Format TransferDialog::format() const { return static_cast<transfer::Format>(format_->currentIndex()); }

transfer::Options TransferDialog::options() const
{
    return {QStringLiteral(",;\t").at(delimiter_->currentIndex()),
        decimal_->currentIndex() == 0 ? QChar('.') : QChar(','), null_->text(),
        date_->currentText(), escaped_->isChecked()};
}

QStringConverter::Encoding TransferDialog::encoding() const
{
    switch (encoding_->currentIndex())
    {
    case 1: return QStringConverter::Utf16LE;
    case 2: return QStringConverter::Utf16BE;
    default: return QStringConverter::Utf8;
    }
}

void TransferDialog::showState(const QString& value) { status_->setText(value); }

QString TransferDialog::error(const SqlReply& reply) const
{
    if (reply.error.unknown_outcome)
        return tr("Commit outcome unknown. Check token %1 in the worksheet.").arg(reply.requestToken);
    const auto message = QString::fromUtf8(reply.error.message);
    return message.isEmpty() ? tr("Server error %1.").arg(reply.error.code) : message;
}

void TransferDialog::busy(bool active)
{
    active_ = active;
    const QList<QWidget*> inputs{direction_, format_, encoding_, delimiter_, decimal_, date_, file_,
        null_, escaped_, skip_, all_, preview_};
    for (auto* input : inputs) input->setEnabled(!active);
    limit_->setEnabled(!active && !all_->isChecked());
    apply_->setEnabled(false);
    cancel_->setText(active ? tr("Cancel") : tr("Close"));
    progress_->setVisible(active);
}

void TransferDialog::browse()
{
    if (active_) return;
    const auto filter = format() == transfer::Format::csv ? tr("CSV files (*.csv)") :
        format() == transfer::Format::json ? tr("JSON files (*.json)") : tr("SQL files (*.sql)");
    const auto file = direction_->currentIndex() == 1 ? QFileDialog::getOpenFileName(this, tr("Select import file"), {}, filter)
        : QFileDialog::getSaveFileName(this, tr("Choose export file"), {}, filter);
    if (!file.isEmpty()) file_->setText(file);
}

void TransferDialog::preview()
{
    if (active_) return;
    parsed_ = {};
    sql_.clear();
    previewPath_ = file_->text().trimmed();
    previewFingerprint_.clear();
    report_->clear();
    apply_->setEnabled(false);
    if (previewPath_.isEmpty() || target_.isEmpty())
    {
        showState(tr("Select a target table and a file."));
        return;
    }
    const auto permissions = table_.value("permissions").toObject();
    if (direction_->currentIndex() == 0)
    {
        if (!permissions.value("select").toBool())
        {
            showState(tr("Export requires SELECT permission for the table."));
            return;
        }
        const auto scope = all_->isChecked() ? tr("All rows, with a 5,000-row limit. Export stops if the table exceeds it.")
            : tr("First %1 rows.").arg(limit_->value());
        report_->setPlainText(tr("%1\n%2\n%3").arg(target_, scope, encoding_->currentText()));
        showState(tr("Preview ready."));
        apply_->setEnabled(true);
        return;
    }

    if (session_.profile().read_only || !permissions.value("insert").toBool())
    {
        showState(tr("Import requires an editable profile and INSERT permission."));
        return;
    }
    QFile file(previewPath_);
    if (!file.open(QIODevice::ReadOnly) || file.size() > transfer::maximumFileBytes)
    {
        showState(tr("Cannot open the import file within the 8 MiB limit."));
        return;
    }
    const auto bytes = file.read(transfer::maximumFileBytes + 1);
    if (bytes.size() > transfer::maximumFileBytes)
    {
        showState(tr("The import file exceeds 8 MiB."));
        return;
    }
    QStringDecoder decoder(encoding());
    const auto text = QString(decoder.decode(bytes));
    if (decoder.hasError())
    {
        showState(tr("The file does not match the selected encoding."));
        return;
    }
    previewFingerprint_ = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    if (format() == transfer::Format::sql)
    {
        sql_ = text;
        report_->setPlainText(tr("Apply opens this script in the current worksheet for review.\n%1")
            .arg(text.left(24000)));
        apply_->setEnabled(!sql_.trimmed().isEmpty());
        showState(tr("Script review ready."));
        return;
    }

    parsed_ = transfer::decode(format(), text, columns_, options());
    if (!parsed_.error.isEmpty()) { showState(parsed_.error); return; }
    report_->setPlainText(tr("%1 rows ready · %2 rejected\n%3")
        .arg(parsed_.rows.size()).arg(parsed_.rejected).arg(parsed_.rejectionReport.join('\n')));
    apply_->setEnabled(!parsed_.rows.isEmpty() && (parsed_.rejected == 0 || skip_->isChecked()));
    showState(tr("Preview ready."));
}

bool TransferDialog::sameSchema(const QJsonObject& metadata) const
{
    for (const auto& item : metadata.value("objects").toArray())
    {
        const auto object = item.toObject();
        if (object.value("id") != table_.value("id")) continue;
        return object.value("schema_version") == table_.value("schema_version") &&
            object.value("qualified_name") == table_.value("qualified_name") &&
            object.value("permissions").toObject().value("insert").toBool();
    }
    return false;
}

void TransferDialog::apply()
{
    if (active_ || previewPath_.isEmpty()) return;
    if (!session_.state().connected || session_.busy() || session_.state().transactionActive)
    {
        showState(tr("Finish the current operation and transaction before starting a transfer."));
        return;
    }
    if (direction_->currentIndex() == 0)
    {
        if (!session_.state().autocommit)
        {
            showState(tr("Enable autocommit before exporting from this worksheet."));
            return;
        }
        if (QFileInfo::exists(previewPath_) && QMessageBox::question(this, tr("Replace file"),
            tr("Replace the chosen export file?"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes) return;
        startExport();
        return;
    }
    QFile input(previewPath_);
    if (!input.open(QIODevice::ReadOnly) || input.size() > transfer::maximumFileBytes ||
        QString::fromLatin1(QCryptographicHash::hash(input.read(transfer::maximumFileBytes + 1),
            QCryptographicHash::Sha256).toHex()) != previewFingerprint_)
    {
        showState(tr("The import file changed. Preview it again."));
        apply_->setEnabled(false);
        return;
    }
    input.close();
    if (format() == transfer::Format::sql)
    {
        if (reviewSql_) reviewSql_(sql_);
        accept();
        return;
    }
    if (QMessageBox::question(this, tr("Apply table import"),
        tr("Import %1 rows into %2 on %3?").arg(parsed_.rows.size()).arg(target_, endpoint(session_.profile())),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;

    types_ = {};
    for (qsizetype i = 0; i < parsed_.columns.size(); ++i)
        types_.insert("p" + QString::number(i), transfer::parameterType(parsed_.columns.at(i)));
    index_ = 0;
    inserted_ = 0;
    runtimeRejected_ = 0;
    cancelling_ = false;
    progress_->setRange(0, static_cast<int>(parsed_.rows.size()));
    progress_->setValue(0);
    busy(true);
    showState(tr("Opening import transaction…"));
    if (!session_.transaction(SqlTransaction::begin, {}, [this](const SqlReply& reply) {
        if (reply.error.code != SDP_OK) { busy(false); showState(error(reply)); return; }
        if (cancelling_) { rollback(tr("Import cancelled.")); return; }
        if (!session_.refreshMetadata([this](const SqlReply& schema) {
            if (schema.error.code != SDP_OK) { rollback(error(schema)); return; }
            if (cancelling_ || !sameSchema(schema.data))
            {
                rollback(cancelling_ ? tr("Import cancelled.") : tr("Table schema or permissions changed. Preview again."));
                return;
            }
            nextRow();
        })) rollback(tr("Could not verify the current table schema."));
    }))
    {
        busy(false);
        showState(tr("Could not begin the import transaction."));
    }
}


void TransferDialog::nextRow()
{
    if (cancelling_) { rollback(tr("Import cancelled.")); return; }
    if (index_ >= parsed_.rows.size())
    {
        if (inserted_ == 0) { rollback(tr("No rows were accepted.")); return; }
        showState(tr("Committing %1 rows…").arg(inserted_));
        if (!session_.transaction(SqlTransaction::commit, {}, [this](const SqlReply& reply) {
            if (reply.error.code != SDP_OK)
            {
                if (!reply.error.unknown_outcome && session_.state().transactionActive)
                    rollback(error(reply));
                else { busy(false); showState(error(reply)); }
                return;
            }
            busy(false);
            showState(tr("Committed %1 rows · %2 rejected.").arg(inserted_)
                .arg(parsed_.rejected + runtimeRejected_));
            if (refresh_) refresh_();
        })) rollback(tr("Could not commit the import."));
        return;
    }

    const auto row = parsed_.rows.at(index_);
    QStringList names, placeholders;
    QJsonObject values;
    for (qsizetype i = 0; i < parsed_.columns.size(); ++i)
    {
        const auto key = "p" + QString::number(i);
        names.append(transfer::identifier(parsed_.columns.at(i).value("name").toString()));
        placeholders.append(":" + key);
        values.insert(key, row.values.at(i));
    }
    const auto statement = "INSERT INTO " + target_ + " (" + names.join(", ") +
        ") VALUES (" + placeholders.join(", ") + ")";
    auto affected = std::make_shared<qint64>(-1);
    if (!session_.execute({SqlStatement{statement}}, types_, values, {1, 5000},
        [affected](const SqlEvent& event) {
            if (event.kind == SqlEvent::Kind::statement)
                *affected = event.data.value("affected_rows").toInteger(-1);
        },
        [this, row, affected](const SqlReply& reply) {
            if (cancelling_) { rollback(tr("Import cancelled.")); return; }
            if (reply.error.code == SDP_OK && *affected == 1) ++inserted_;
            else
            {
                ++runtimeRejected_;
                if (runtimeRejected_ <= 100)
                    report_->appendPlainText(tr("Row %1 · %2").arg(row.source)
                        .arg(reply.error.code == SDP_OK ? tr("Unexpected row count.") : error(reply)));
                const auto sqlstate = QString::fromLatin1(reply.error.sqlstate);
                if (!skip_->isChecked() || !session_.state().transactionActive || reply.error.unknown_outcome ||
                    !(sqlstate.startsWith("22") || sqlstate.startsWith("23")))
                {
                    rollback(tr("A row was rejected."));
                    return;
                }
            }
            ++index_;
            progress_->setValue(static_cast<int>(index_));
            showState(tr("Processed %1 of %2 rows.").arg(index_).arg(parsed_.rows.size()));
            QTimer::singleShot(0, this, [this] { nextRow(); });
        })) rollback(tr("Could not start the next import row."));
}

void TransferDialog::rollback(const QString& reason)
{
    const auto finish = [this, reason](const SqlReply& reply) {
        busy(false);
        showState(reply.error.code == SDP_OK ? tr("%1 No rows were committed.").arg(reason)
            : tr("%1 Rollback returned %2.").arg(reason, error(reply)));
    };
    if (!session_.state().transactionActive)
    {
        finish({});
        return;
    }
    if (!session_.transaction(SqlTransaction::rollback, {}, finish))
    {
        busy(false);
        showState(tr("%1 Check the worksheet for the active transaction.").arg(reason));
    }
}

void TransferDialog::startExport()
{
    QStringList identifiers;
    for (const auto& column : columns_)
        identifiers.append(transfer::identifier(column.toObject().value("name").toString()));
    if (identifiers.isEmpty() || target_.isEmpty())
    {
        showState(tr("The table has no visible columns."));
        return;
    }
    const auto requested = all_->isChecked() ? transfer::maximumRows + 1 : limit_->value();
    QString sql = "SELECT " + identifiers.join(", ") + " FROM " + target_;
    QStringList order;
    for (const auto& item : table_.value("row_locator").toObject().value("columns").toArray())
    {
        const auto ordinal = item.toInt(-1);
        if (ordinal >= 0 && ordinal < identifiers.size()) order.push_back(identifiers.at(ordinal));
    }
    if (!order.isEmpty()) sql += " ORDER BY " + order.join(", ");
    const QJsonObject noBindings;
    cancelling_ = false;
    exportRows_.clear();
    exportBytes_ = 0;
    exportBudgetExceeded_ = false;
    runOptions_ = options();
    progress_->setRange(0, requested);
    progress_->setValue(0);
    busy(true);
    showState(tr("Reading table rows…"));
    if (!session_.execute({SqlStatement{sql}}, noBindings, noBindings, {requested, 20000},
        [this](const SqlEvent& event) {
            if (event.kind != SqlEvent::Kind::batch || exportBudgetExceeded_) return;
            const auto batch = event.data.contains("batch") ? event.data.value("batch").toObject() : event.data;
            for (const auto& row : batch.value("rows").toArray())
            {
                auto values = row.toArray();
                exportBytes_ += QJsonDocument(values).toJson(QJsonDocument::Compact).size();
                if (exportBytes_ > transfer::maximumFileBytes)
                {
                    exportBudgetExceeded_ = true;
                    session_.cancel();
                    return;
                }
                exportRows_.push_back(std::move(values));
            }
            progress_->setValue(static_cast<int>(exportRows_.size()));
        },
        [this](const SqlReply& reply) {
            busy(false);
            if (cancelling_) { exportRows_.clear(); showState(tr("Export cancelled.")); return; }
            if (exportBudgetExceeded_)
            {
                exportRows_.clear();
                showState(tr("The result exceeds the export memory limit. Choose a smaller row count."));
                return;
            }
            if (reply.error.code != SDP_OK) { exportRows_.clear(); showState(error(reply)); return; }
            if (all_->isChecked() && (exportRows_.size() > transfer::maximumRows || reply.resultsTruncated))
            {
                exportRows_.clear();
                showState(tr("The result exceeded the export limit. Choose an explicit row count."));
                return;
            }
            const auto count = exportRows_.size();
            QString failure;
            const auto text = transfer::encode(format(), table_, columns_, exportRows_, runOptions_, failure);
            exportRows_.clear();
            if (!failure.isEmpty()) { showState(failure); return; }
            QStringEncoder encoder(encoding(), encoding() == QStringConverter::Utf8
                ? QStringConverter::Flag::Default : QStringConverter::Flag::WriteBom);
            const auto payload = QByteArray(encoder.encode(text));
            if (encoder.hasError() || payload.size() > transfer::maximumFileBytes)
            {
                showState(tr("The encoded export exceeds its file or encoding limit."));
                return;
            }
            QSaveFile output(previewPath_);
            if (!output.open(QIODevice::WriteOnly) || output.write(payload) != payload.size() || !output.commit())
            {
                showState(tr("Could not write the selected export file."));
                return;
            }
            showState(tr("Exported %1 rows.").arg(count));
        }))
    {
        busy(false);
        showState(tr("Could not start export."));
    }
}

void TransferDialog::reject()
{
    if (!active_) { QDialog::reject(); return; }
    cancelling_ = true;
    session_.cancel();
    showState(tr("Cancellation requested…"));
}
}

void showTableTransfer(SqlSession& session, QJsonObject table, QWidget* parent,
    std::function<void(const QString&)> reviewSql, std::function<void()> refresh)
{
    auto* dialog = new TransferDialog(session, std::move(table), parent, std::move(reviewSql), std::move(refresh));
    dialog->open();
}
}
