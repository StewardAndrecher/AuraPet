#pragma once

#include <QString>

class RoleLibrary;
class QWidget;

bool showRoleMemory(RoleLibrary& library, const QString& roleId, const QString& roleName, QWidget* parent);
