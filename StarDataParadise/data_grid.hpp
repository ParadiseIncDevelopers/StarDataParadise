#pragma once

#include <QJsonObject>

class QDialog;
class QWidget;

namespace wb
{
class SqlSession;

// Uses the worksheet's authenticated session. The dialog owns its bounded result snapshot.
QDialog* openDataGrid(SqlSession& session, const QJsonObject& tableMetadata, QWidget* parent);
}

