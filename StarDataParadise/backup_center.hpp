#pragma once

#include <QString>

#include <functional>

class QDialog;
class QWidget;

namespace wb
{
class SqlSession;

QDialog* showBackupCenter(SqlSession&, QWidget* parent, std::function<void(const QString&)> openJob = {});
}

