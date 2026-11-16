#pragma once

#include <QJsonObject>

class QDialog;
class QThreadPool;
class QWidget;

namespace wb
{
class SqlSession;

// The target remains available to its worksheet while the control connection is attached.
QDialog* showRoutineDebugger(SqlSession& target, QThreadPool& workers,
    const QJsonObject& routineMetadata, QWidget* parent);
}

