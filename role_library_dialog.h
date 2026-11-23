#pragma once

#include <QString>
#include <QStringList>

class RoleLibrary;
class QWidget;

struct RoleLibraryChoice {
    QString selected;
    QStringList removed;
    QStringList changed;
};

RoleLibraryChoice manageRoles(RoleLibrary& library, const QString& current, QWidget* parent);
