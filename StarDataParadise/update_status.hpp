#pragma once

#include <QJsonObject>
#include <QString>

class QWidget;

namespace wb
{
QJsonObject read_update_status(const QString& file, QString& error);
void show_update_status(QWidget* parent, const QString& file);
}
