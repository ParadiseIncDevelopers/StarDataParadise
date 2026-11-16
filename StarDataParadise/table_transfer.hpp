#pragma once

#include <QJsonObject>
#include <QString>
#include <functional>

class QWidget;

namespace wb
{
class SqlSession;

void showTableTransfer(SqlSession&, QJsonObject table, QWidget* parent,
    std::function<void(const QString&)> reviewSql, std::function<void()> refresh);
}
