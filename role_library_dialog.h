#pragma once

#include <QString>
#include <QStringList>
#include <functional>

class RoleLibrary;
class VoiceTraining;
class QWidget;

struct RoleLibraryContext {
    QString builtInAsset;
    QString preferredRole;
    QStringList activeRoles;
    bool multiPetEnabled = false;
    VoiceTraining* voiceTraining = nullptr;
    std::function<void(const QStringList& activeRoles, bool multiPetEnabled)> selectionChanged;
    std::function<void(const QString& roleId)> roleUpdated;
    std::function<void(const QString& roleId)> roleRemoved;
};

struct RoleLibraryChoice {
    QStringList activeRoles;
    bool multiPetEnabled = false;
    QStringList removed;
    QStringList changed;
};

RoleLibraryChoice manageRoles(RoleLibrary& library, const RoleLibraryContext& context, QWidget* parent = nullptr);
