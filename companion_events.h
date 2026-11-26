#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

#include <array>

class QSettings;

enum class CompanionEvent {
    SlowKibble, HighKibble, Yarn, Catnip, DoubleClick, FocusStarted, FocusFinished,
    BreakStarted, BreakFinished, Inactive, LowActivity, Morning, Evening
};

struct CompanionEventInfo {
    CompanionEvent event;
    const char* key;
    const char* label;
};

inline constexpr std::array<CompanionEventInfo, 13> companionEvents{{
    {CompanionEvent::SlowKibble, "slow", "切入静伴专注"},
    {CompanionEvent::HighKibble, "high", "切入互动游玩"},
    {CompanionEvent::Yarn, "yarn", "逗一逗（轻互动）"},
    {CompanionEvent::Catnip, "catnip", "充个电（补充活力）"},
    {CompanionEvent::DoubleClick, "doubleClick", "双击桌宠"},
    {CompanionEvent::FocusStarted, "focusStart", "专注开始"},
    {CompanionEvent::FocusFinished, "focus", "专注结束"},
    {CompanionEvent::BreakStarted, "breakStart", "休息开始"},
    {CompanionEvent::BreakFinished, "breakEnd", "休息结束"},
    {CompanionEvent::Inactive, "inactive", "30 分钟未互动"},
    {CompanionEvent::LowActivity, "lowActivity", "低活跃度（≤20）"},
    {CompanionEvent::Morning, "morning", "早间问候（6–12 点）"},
    {CompanionEvent::Evening, "evening", "晚间问候（18–23 点）"},
}};

QString eventKey(CompanionEvent event);
QString eventLabel(const QString& key);

class CompanionEvents {
public:
    explicit CompanionEvents(QSettings& settings);
    void setRole(const QString& role, const QJsonObject& behavior, const QDateTime& now);
    void interacted(const QDateTime& now);
    QDateTime lastInteraction() const { return lastInteraction_; }
    QJsonObject binding(CompanionEvent event) const;
    QJsonObject nextReminder(const QDateTime& now, double activity, bool available, bool suppressLowActivity = false);

private:
    QJsonObject greeting(CompanionEvent event, const QDateTime& now);
    QSettings& settings_;
    QString role_;
    QJsonObject bindings_;
    QDateTime lastInteraction_;
    QDateTime lastReminder_;
    bool inactiveReminded_ = false;
    bool lowReminded_ = false;
};
