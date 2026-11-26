#pragma once

#include "role_library.h"

#include <QImage>
#include <functional>

class QWidget;

inline const QString rolePackageSuffix = QStringLiteral("gifpet");

struct RolePackage {
    QString id;
    QString name;
    QByteArray asset;
    QMap<ActionSlot, ActionAsset> actions;
    RoleShare share;
    QImage cover;
};

using RolePackageProgress = std::function<bool(int)>;
std::optional<RolePackage> readRolePackage(const QString& path, QString& error, const RolePackageProgress& progress = {});
bool writeRolePackage(const QString& path, const RolePackage& package, bool includeCorpus, QString& error,
                      const RolePackageProgress& progress = {});
bool validateRoleCard(const QJsonObject& card, QString& error);
bool validateRoleShare(const RoleShare& share, QString& error);
bool replaceRoleCorpusFolder(RoleShare& share, const QString& language, const QString& path, QString& error);
QList<QPair<QString, QString>> describeRoleBehavior(const QJsonObject& behavior);
QStringList rolePromptContext(const QJsonObject& card, const QString& text);
QString roleVoiceAlias(const QString& localId, const QString& language);
QString importRolePackage(QWidget& parent, RoleLibrary& library, const QString& path);
bool editRoleShare(QWidget& parent, RoleLibrary& library, const QString& roleId);
void exportRolePackage(QWidget& parent, RoleLibrary& library, const QString& roleId, const QString& name);
