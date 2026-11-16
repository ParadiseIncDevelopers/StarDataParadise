#pragma once

class QDialog;
class QWidget;

namespace wb
{
class SqlSession;

QDialog* showCompareStudio(SqlSession& source, SqlSession& destination, QWidget* parent);
}
