#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <functional>

class QDialog;
class QWidget;

namespace wb
{
class SqlSession;

QDialog* showMlStudio(SqlSession&, const QJsonArray& catalog, const QJsonObject& initialModel, QWidget*,
    std::function<void()> refresh, std::function<void(const QString&, const QString&)> watch,
    std::function<void(const QJsonObject&)> openRoutine = {},
    std::function<void(const QJsonObject&)> exportRoutine = {});
}
