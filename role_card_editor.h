#pragma once

#include <QJsonObject>
#include <QString>

class QWidget;

QString roleCardText(const QJsonObject& card);
bool editRoleCard(QWidget& parent, QJsonObject& card, const QString& roleId, const QString& roleName);
