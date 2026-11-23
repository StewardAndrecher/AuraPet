#pragma once

#include <QString>

class RoleLibrary;
class QWidget;

bool editRoleActions(RoleLibrary& library, const QString& roleId, const QString& builtInAsset, QWidget* parent);
