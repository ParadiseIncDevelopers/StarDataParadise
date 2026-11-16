#pragma once

#include <QString>

#include <functional>

class QDialog;
class QWidget;

namespace wb
{
class SqlSession;

QDialog* showAdminCenter(SqlSession&, QWidget* parent, std::function<void(const QString&)> openJob = {});
}
