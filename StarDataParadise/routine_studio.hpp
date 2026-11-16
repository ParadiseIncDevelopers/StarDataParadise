#pragma once

#include <QJsonArray>
#include <QJsonObject>

#include <functional>

class QDialog;
class QWidget;

namespace wb
{
class SqlSession;

QDialog* showRoutineStudio(SqlSession&, const QJsonArray& catalog, const QJsonObject& routine,
    QWidget* parent, std::function<void()> refresh, std::function<void(const QJsonObject&)> debug);
}
