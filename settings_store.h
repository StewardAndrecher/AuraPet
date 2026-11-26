#pragma once

#include "pet_motion_rules.h"
#include "space_transit.h"

#include <QPoint>
#include <QSettings>
#include <QStringList>

class QJsonValue;

class SettingsStore {
public:
    SettingsStore();

    static QString dataPath();
    QSettings& native() { return settings_; }
    QString activeRole(const QString& fallback) const;
    QStringList activeRoles(const QString& fallback = {}) const;
    void setActiveRoles(const QStringList& roles);
    bool multiPetEnabled() const;
    void setMultiPetEnabled(bool enabled);
    bool hasSavedPosition(const QString& role) const;
    QPoint position(const QString& role, const QPoint& fallback, bool useGlobalFallback) const;
    int scale(const QString& role) const;
    void saveRoleLayout(const QString& role, QPoint position, int scale);
    void saveLayout(const QString& role, QPoint position, int scale);
    void removeRole(const QString& role);
    bool primaryScreenOnly() const;
    void setPrimaryScreenOnly(bool enabled);
    QString kibble(const QString& role, const QString& preset) const;
    void setKibble(const QString& role, const QString& value);
    CompanionMode companionMode(const QString& role, const QString& legacyPreset = {}) const;
    void setCompanionMode(const QString& role, CompanionMode mode);
    FocusMotionPolicy focusMotionPolicy() const;
    void setFocusMotionPolicy(FocusMotionPolicy policy);
    bool roaming(const QString& role, const QJsonValue& preset) const;
    void setRoaming(const QString& role, bool enabled);
    double activity(const QString& role, double fallback = 78.0) const;
    void setActivity(const QString& role, double value);
    bool clickThrough(const QString& role) const;
    void setClickThrough(const QString& role, bool enabled);
    bool followAllSpaces(const QString& role) const;
    void setFollowAllSpaces(const QString& role, bool enabled);
    SpaceTransitMode spaceTransitMode() const;
    void setSpaceTransitMode(SpaceTransitMode mode);
    bool alwaysOnTop(const QString& role) const;
    void setAlwaysOnTop(const QString& role, bool enabled);
    bool showActivityMeter() const;
    void setShowActivityMeter(bool enabled);

private:
    QSettings settings_;
};
