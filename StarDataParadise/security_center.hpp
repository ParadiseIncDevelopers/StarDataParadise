#pragma once

#include <QJsonArray>
#include <functional>

class QDialog;
class QWidget;

namespace wb
{
class SqlSession;

QDialog* showSecurityCenter(SqlSession&, const QJsonArray& catalog, QWidget*,
    std::function<void()> refreshed = {});
}
