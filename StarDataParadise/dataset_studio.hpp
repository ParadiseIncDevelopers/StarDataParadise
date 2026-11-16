#pragma once

#include <QJsonObject>
#include <QString>

#include <functional>

class QDialog;
class QWidget;

namespace wb
{
class SqlSession;

QDialog* showDatasetStudio(SqlSession&, QWidget* parent, std::function<void(const QJsonObject&)> selected,
    const QString& initialSql = {});
}
