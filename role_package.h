#pragma once

#include "role_library.h"

#include <QPixmap>

class QWidget;

inline const QString rolePackageSuffix = QStringLiteral("gifpet");

struct RolePackage {
    QString id;
    QString name;
    QByteArray asset;
    QMap<ActionSlot, ActionAsset> actions;
    RoleShare share;
    QPixmap cover;
};

std::optional<RolePackage> readRolePackage(const QString& path, QString& error);
bool writeRolePackage(const QString& path, const RolePackage& package, bool includeCorpus, QString& error);
bool validateRoleShare(const RoleShare& share, QString& error);
QStringList rolePromptContext(const QJsonObject& card, const QString& text);
QString roleVoiceAlias(const QString& localId, const QString& language);
QString importRolePackage(QWidget& parent, RoleLibrary& library, const QString& path);
bool editRoleShare(QWidget& parent, RoleLibrary& library, const QString& roleId);
void exportRolePackage(QWidget& parent, RoleLibrary& library, const QString& roleId, const QString& name);
