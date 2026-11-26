#include "settings_store.h"
#include "role_library.h"

#include <QDir>
#include <QJsonValue>
#include <QStandardPaths>
#include <QtGlobal>

namespace {
const auto legacyName = QStringLiteral("Gif-Pet");
const auto activeRoleKey = QStringLiteral("pet/activeRoleId");
const auto activeRolesKey = QStringLiteral("pet/activeRoleIds");
const auto multiPetKey = QStringLiteral("pet/multiPetEnabled");
const auto screenPolicyKey = QStringLiteral("pet/screenPrimaryOnly");

QString roleKey(const QString& role, const QString& name)
{
    return QStringLiteral("roles/%1/%2").arg(role, name);
}

QStringList normalizedRoles(const QStringList& raw)
{
    QStringList result;
    for (const auto& id : raw) {
        const auto trimmed = id.trimmed();
        if (!trimmed.isEmpty() && !result.contains(trimmed)) {
            result.append(trimmed);
        }
    }
    return result;
}
}

SettingsStore::SettingsStore()
    : settings_(QSettings::NativeFormat, QSettings::UserScope, legacyName, legacyName)
{
}

QString SettingsStore::dataPath()
{
    const auto root = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return root.isEmpty() ? QString{} : QDir(root).filePath(legacyName + u'/' + legacyName);
}

QString SettingsStore::activeRole(const QString& fallback) const
{
    return settings_.value(activeRoleKey, fallback).toString();
}

QStringList SettingsStore::activeRoles(const QString& fallback) const
{
    if (settings_.contains(activeRolesKey)) {
        const auto list = normalizedRoles(settings_.value(activeRolesKey).toStringList());
        if (!list.isEmpty()) {
            return list;
        }
    }
    const auto single = activeRole(fallback).trimmed();
    return single.isEmpty() ? QStringList{} : QStringList{single};
}

void SettingsStore::setActiveRoles(const QStringList& roles)
{
    const auto list = normalizedRoles(roles);
    settings_.setValue(activeRolesKey, list);
    if (!list.isEmpty()) {
        settings_.setValue(activeRoleKey, list.constFirst());
    }
}

bool SettingsStore::multiPetEnabled() const
{
    if (settings_.contains(multiPetKey)) {
        return settings_.value(multiPetKey, false).toBool();
    }
    return activeRoles().size() > 1;
}

void SettingsStore::setMultiPetEnabled(bool enabled)
{
    settings_.setValue(multiPetKey, enabled);
}

bool SettingsStore::hasSavedPosition(const QString& role) const
{
    return !role.isEmpty() && settings_.contains(roleKey(role, QStringLiteral("position")));
}

QPoint SettingsStore::position(const QString& role, const QPoint& fallback, bool useGlobalFallback) const
{
    const auto previous = useGlobalFallback ? settings_.value(QStringLiteral("pet/position"), fallback).toPoint() : fallback;
    return settings_.value(roleKey(role, QStringLiteral("position")), previous).toPoint();
}

int SettingsStore::scale(const QString& role) const
{
    return settings_.value(roleKey(role, QStringLiteral("scale")), 100).toInt();
}

void SettingsStore::saveRoleLayout(const QString& role, QPoint position, int scale)
{
    if (!role.isEmpty()) {
        settings_.setValue(roleKey(role, QStringLiteral("position")), position);
        settings_.setValue(roleKey(role, QStringLiteral("scale")), scale);
    }
}

void SettingsStore::saveLayout(const QString& role, QPoint position, int scale)
{
    if (!role.isEmpty()) {
        saveRoleLayout(role, position, scale);
        settings_.setValue(activeRoleKey, role);
    }
}

void SettingsStore::removeRole(const QString& role)
{
    if (role.isEmpty()) {
        return;
    }
    settings_.remove(QStringLiteral("roles/") + role);
    settings_.remove(QStringLiteral("speech/roles/") + role);
    settings_.remove(QStringLiteral("companion/greetings/") + role);
    if (settings_.contains(activeRolesKey)) {
        auto current = activeRoles();
        if (current.removeAll(role) > 0) {
            setActiveRoles(current);
        }
    }
}

bool SettingsStore::primaryScreenOnly() const
{
    return settings_.value(screenPolicyKey, false).toBool();
}

void SettingsStore::setPrimaryScreenOnly(bool enabled)
{
    settings_.setValue(screenPolicyKey, enabled);
}

QString SettingsStore::kibble(const QString& role, const QString& preset) const
{
    const auto fallback = (!preset.isEmpty() || role != RoleLibrary::legacyId())
        ? preset
        : settings_.value(QStringLiteral("pet/loadedKibble")).toString();
    return settings_.value(roleKey(role, QStringLiteral("loadedKibble")), fallback).toString();
}

void SettingsStore::setKibble(const QString& role, const QString& value)
{
    if (!role.isEmpty()) {
        settings_.setValue(roleKey(role, QStringLiteral("loadedKibble")), value);
    }
}

CompanionMode SettingsStore::companionMode(const QString& role, const QString& legacyPreset) const
{
    const QString key = roleKey(role, QStringLiteral("companionMode"));
    if (!role.isEmpty() && settings_.contains(key)) {
        const QString v = settings_.value(key).toString().trimmed().toLower();
        if (v == QStringLiteral("play")) return CompanionMode::Play;
        if (v == QStringLiteral("daily")) return CompanionMode::Daily;
        if (v == QStringLiteral("focus")) return CompanionMode::Focus;
        return CompanionMode::Auto;
    }
    const QString legacy = kibble(role, legacyPreset).trimmed().toLower();
    if (legacy == QStringLiteral("high")) return CompanionMode::Play;
    if (legacy == QStringLiteral("slow")) return CompanionMode::Focus;
    if (legacy == QStringLiteral("none")) return CompanionMode::Daily;
    return CompanionMode::Auto;
}

void SettingsStore::setCompanionMode(const QString& role, CompanionMode mode)
{
    if (role.isEmpty()) {
        return;
    }
    QString encoded = QStringLiteral("auto");
    if (mode == CompanionMode::Play) encoded = QStringLiteral("play");
    else if (mode == CompanionMode::Daily) encoded = QStringLiteral("daily");
    else if (mode == CompanionMode::Focus) encoded = QStringLiteral("focus");
    settings_.setValue(roleKey(role, QStringLiteral("companionMode")), encoded);
}

FocusMotionPolicy SettingsStore::focusMotionPolicy() const
{
    const QString v = settings_.value(QStringLiteral("pet/focusMotionPolicy"), QStringLiteral("micro"))
                          .toString().trimmed().toLower();
    return v == QStringLiteral("still") ? FocusMotionPolicy::CompletelyStill
                                        : FocusMotionPolicy::GentleMicroMove;
}

void SettingsStore::setFocusMotionPolicy(FocusMotionPolicy policy)
{
    settings_.setValue(QStringLiteral("pet/focusMotionPolicy"),
                       policy == FocusMotionPolicy::CompletelyStill
                           ? QStringLiteral("still")
                           : QStringLiteral("micro"));
}

bool SettingsStore::roaming(const QString& role, const QJsonValue& preset) const
{
    const bool legacyDefault = role == RoleLibrary::legacyId()
        ? settings_.value(QStringLiteral("pet/randomMovement"), true).toBool()
        : true;
    return settings_.value(roleKey(role, QStringLiteral("randomMovement")), preset.toBool(legacyDefault)).toBool();
}

void SettingsStore::setRoaming(const QString& role, bool enabled)
{
    if (!role.isEmpty()) {
        settings_.setValue(roleKey(role, QStringLiteral("randomMovement")), enabled);
    }
}

double SettingsStore::activity(const QString& role, double fallback) const
{
    if (role.isEmpty()) {
        return qBound(0.0, fallback, 100.0);
    }
    return qBound(0.0, settings_.value(roleKey(role, QStringLiteral("activity")), fallback).toDouble(), 100.0);
}

void SettingsStore::setActivity(const QString& role, double value)
{
    if (!role.isEmpty()) {
        settings_.setValue(roleKey(role, QStringLiteral("activity")), qBound(0.0, value, 100.0));
    }
}

bool SettingsStore::clickThrough(const QString& role) const
{
    if (role.isEmpty()) {
        return false;
    }
    return settings_.value(roleKey(role, QStringLiteral("clickThrough")), false).toBool();
}

void SettingsStore::setClickThrough(const QString& role, bool enabled)
{
    if (!role.isEmpty()) {
        settings_.setValue(roleKey(role, QStringLiteral("clickThrough")), enabled);
    }
}

bool SettingsStore::followAllSpaces(const QString& role) const
{
    const bool globalDefault = settings_.value(QStringLiteral("pet/followAllSpaces"), true).toBool();
    if (role.isEmpty()) {
        return globalDefault;
    }
    return settings_.value(roleKey(role, QStringLiteral("followAllSpaces")), globalDefault).toBool();
}

void SettingsStore::setFollowAllSpaces(const QString& role, bool enabled)
{
    if (!role.isEmpty()) {
        settings_.setValue(roleKey(role, QStringLiteral("followAllSpaces")), enabled);
    }
}

SpaceTransitMode SettingsStore::spaceTransitMode() const
{
    const QString value = settings_.value(QStringLiteral("pet/spaceTransitMode"), QStringLiteral("mixed"))
                              .toString()
                              .trimmed()
                              .toLower();
    if (value == QStringLiteral("jump")) {
        return SpaceTransitMode::JumpOnly;
    }
    if (value == QStringLiteral("walk")) {
        return SpaceTransitMode::WalkOnly;
    }
    return SpaceTransitMode::MixedRandom;
}

void SettingsStore::setSpaceTransitMode(SpaceTransitMode mode)
{
    QString encoded = QStringLiteral("mixed");
    if (mode == SpaceTransitMode::JumpOnly) {
        encoded = QStringLiteral("jump");
    } else if (mode == SpaceTransitMode::WalkOnly) {
        encoded = QStringLiteral("walk");
    }
    settings_.setValue(QStringLiteral("pet/spaceTransitMode"), encoded);
}

bool SettingsStore::alwaysOnTop(const QString& role) const
{
    const bool globalDefault = settings_.value(QStringLiteral("pet/alwaysOnTop"), true).toBool();
    if (role.isEmpty()) {
        return globalDefault;
    }
    return settings_.value(roleKey(role, QStringLiteral("alwaysOnTop")), globalDefault).toBool();
}

void SettingsStore::setAlwaysOnTop(const QString& role, bool enabled)
{
    if (!role.isEmpty()) {
        settings_.setValue(roleKey(role, QStringLiteral("alwaysOnTop")), enabled);
    }
}

bool SettingsStore::showActivityMeter() const
{
    return settings_.value(QStringLiteral("pet/showActivityMeter"), true).toBool();
}

void SettingsStore::setShowActivityMeter(bool enabled)
{
    settings_.setValue(QStringLiteral("pet/showActivityMeter"), enabled);
}
