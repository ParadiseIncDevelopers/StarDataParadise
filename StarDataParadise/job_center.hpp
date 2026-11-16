#pragma once

#include <QString>

class QDialog;
class QWidget;

namespace wb
{
class SqlSession;

QDialog* showJobCenter(SqlSession&, QWidget* parent, const QString& stateFile,
    const QString& jobId = {}, const QString& model = {});
}

