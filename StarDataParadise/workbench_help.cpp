#include "workbench_help.hpp"

#include <QDialog>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QVBoxLayout>

namespace wb
{
void showOfflineHelp(QWidget* parent)
{
    auto* help = new QDialog(parent);
    help->setObjectName("offlineWorkbenchHelp");
    help->setAttribute(Qt::WA_DeleteOnClose);
    help->setWindowTitle(QObject::tr("Workbench offline help"));
    help->resize(745, 580);
    auto* layout = new QVBoxLayout(help);
    auto* tabs = new QTabWidget;
    layout->addWidget(tabs);
    const auto page = [tabs](const QString& title, const QString& text) {
        auto* contents = new QPlainTextEdit(text);
        contents->setReadOnly(true);
        tabs->addTab(contents, title);
    };
    page(QObject::tr("SQL"), QObject::tr(
        "Use the SQL worksheet to prepare one statement, execute a selection or step through a script.\n\n"
        "SELECT note FROM main.public.items WHERE id = :id;\n"
        "Declare id as BIGINT in the parameter panel and supply a separate typed value.\n\n"
        "BEGIN;\nSAVEPOINT before_edit;\nROLLBACK TO SAVEPOINT before_edit;\nROLLBACK;\n\n"
        "A result row budget bounds returned rows. ORDER BY supports ASC and DESC. "
        "Use EXPLAIN before running an unfamiliar query. Schema changes participate in a transaction."));
    page(QObject::tr("PL/SQL"), QObject::tr(
        "PL/SQL Studio edits and compiles functions, procedures and triggers. A function returns a typed value. "
        "Use CALL for procedures and review returned OUT values and named result sets.\n\n"
        "CREATE FUNCTION twice(p IN BIGINT) RETURN BIGINT AS\n"
        "BEGIN\n    RETURN p * 2;\nEND;\n\n"
        "A separate authenticated controller attaches to the target worksheet before the routine is invoked. "
        "Routine tests require an isolated Server instance with routine_test_instance enabled."));
    page(QObject::tr("Workbench keys"), QObject::tr(
        "Ctrl+Enter runs the current SQL statement.\n"
        "Ctrl+F opens worksheet find and replace.\n"
        "Ctrl+O opens a SQL file for editing.\n"
        "Ctrl+S saves a SQL file.\n"
        "Ctrl+Shift+O searches visible objects in connected worksheets.\n"
        "F12 shows the definition of a visible object at the cursor.\n"
        "The worksheet menu includes snippets and opens a local script project for file review."));
    page(QObject::tr("Administration"), QObject::tr(
        "Health entries show the last server-side check time and its evidence. UNKNOWN means that the Server "
        "has no confirmed result for the relevant check.\n\n"
        "A verified restore creates a separate target in the Server's restore area. The restore name "
        "is not a workstation pathname. Connect to the restored target using its own Server profile.\n\n"
        "Profile export saves connection settings. The Windows credential store keeps saved passwords separate. "
        "Imported profiles are reviewed and opened through the Connections menu."));
    help->show();
}
}
