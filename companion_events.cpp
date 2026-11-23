#include "companion_events.h"

#include <QSettings>

QString eventKey(CompanionEvent event)
{
    for (const auto& entry : companionEvents) {
        if (entry.event == event) {
            return QString::fromLatin1(entry.key);
        }
    }
    return {};
}

QString eventLabel(const QString& key)
{
    for (const auto& entry : companionEvents) {
        if (key == QLatin1String(entry.key)) {
            return QString::fromUtf8(entry.label);
        }
    }
    return {};
}

CompanionEvents::CompanionEvents(QSettings& settings) : settings_(settings) {}

void CompanionEvents::setRole(const QString& role, const QJsonObject& behavior, const QDateTime& now)
{
    if (role_ != role) {
        role_ = role;
        interacted(now);
        lowReminded_ = false;
    }
    bindings_ = behavior.value(QStringLiteral("events")).toObject();
}

void CompanionEvents::interacted(const QDateTime& now)
{
    lastInteraction_ = now;
    inactiveReminded_ = false;
}

QJsonObject CompanionEvents::binding(CompanionEvent event) const
{
    return bindings_.value(eventKey(event)).toObject();
}

QJsonObject CompanionEvents::greeting(CompanionEvent event, const QDateTime& now)
{
    const auto key = QStringLiteral("companion/greetings/") + role_ + u'/' + eventKey(event);
    const auto action = binding(event);
    if (action.isEmpty() || settings_.value(key).toDate() == now.date()) {
        return {};
    }
    settings_.setValue(key, now.date());
    return action;
}

QJsonObject CompanionEvents::nextReminder(const QDateTime& now, double activity, bool available)
{
    if (activity >= 30) {
        lowReminded_ = false;
    }
    if (!available || role_.isEmpty() || bindings_.isEmpty()
        || (lastReminder_.isValid() && qAbs(lastReminder_.secsTo(now)) < 90)) {
        return {};
    }
    QJsonObject action;
    if (activity <= 20 && !lowReminded_) {
        action = binding(CompanionEvent::LowActivity);
        lowReminded_ = !action.isEmpty();
    }
    if (action.isEmpty() && !inactiveReminded_ && lastInteraction_.secsTo(now) >= 30 * 60) {
        action = binding(CompanionEvent::Inactive);
        inactiveReminded_ = !action.isEmpty();
    }
    const int hour = now.time().hour();
    if (action.isEmpty() && hour >= 6 && hour < 12) {
        action = greeting(CompanionEvent::Morning, now);
    }
    if (action.isEmpty() && hour >= 18 && hour < 23) {
        action = greeting(CompanionEvent::Evening, now);
    }
    if (!action.isEmpty()) {
        lastReminder_ = now;
    }
    return action;
}
