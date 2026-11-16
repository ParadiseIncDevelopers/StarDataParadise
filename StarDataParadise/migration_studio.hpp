#pragma once

class QDialog;
class QThreadPool;
class QWidget;

namespace wb
{
class SqlSession;
QDialog* showMigrationStudio(SqlSession& destination, QThreadPool& workers, QWidget* parent);
}
