#pragma once

#include <QJsonObject>
#include <QString>

#include <functional>

namespace wb
{
class SqlSession;

struct SqlDefinition
{
    QString text;
    QString revision;
    QString error;
};

void readDefinition(SqlSession&, QJsonObject liveMetadata, std::function<void(SqlDefinition)> finish);
void inspectDefinition(SqlSession&, QJsonObject liveMetadata, std::function<void(QString)> render);
}
