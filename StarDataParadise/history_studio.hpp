#pragma once

#include <QJsonArray>
#include <QJsonObject>

#include <functional>

class QDialog;
class QWidget;

namespace wb
{
class SqlSession;

QDialog* showHistoryStudio(SqlSession&, const QJsonArray& catalog, const QJsonObject& selected,
    QWidget* parent, std::function<void()> refresh);
}
