#pragma once

#include "profiles.hpp"

#include <QJsonObject>

class QDialog;
class QWidget;

namespace wb
{
QDialog* showRoutineExport(const Profile&, const QJsonObject& routine, QWidget* parent);
void showSdkExamples(QWidget* parent);
}
