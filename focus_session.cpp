#include "focus_session.h"

#include <QDateTime>
#include <QSettings>
#include <QStringList>

namespace {
const QString timeKey = QStringLiteral("companion/focusTime");
const QString kindKey = QStringLiteral("companion/timerKind");
const QString plannedKey = QStringLiteral("companion/timerDuration");
const QString statsDateKey = QStringLiteral("companion/today/date");
const QString focusMsKey = QStringLiteral("companion/today/milliseconds");
const QString focusCountKey = QStringLiteral("companion/today/focusCount");
const QString interactionsKey = QStringLiteral("companion/today/interactions");
const QString quietKey = QStringLiteral("companion/doNotDisturb");
const QString endActionKey = QStringLiteral("companion/endAction");
constexpr qint64 maxDuration = 180 * 60 * 1000;

qint64 now()
{
    return QDateTime::currentMSecsSinceEpoch();
}

qint64 roundedSeconds(qint64 milliseconds)
{
    return (milliseconds + 999) / 1000;
}
}

FocusSession::FocusSession(QSettings& settings) : settings_(settings)
{
    doNotDisturb_ = settings_.value(quietKey, false).toBool();
    const int action = settings_.value(endActionKey, 0).toInt();
    if (action >= 0 && action <= static_cast<int>(EndAction::Text)) {
        endAction_ = static_cast<EndAction>(action);
    }
    statsDate_ = settings_.value(statsDateKey).toDate();
    focusMs_ = settings_.value(focusMsKey).toLongLong();
    focusCount_ = settings_.value(focusCountKey).toInt();
    interactionCount_ = settings_.value(interactionsKey).toInt();
    ensureToday();
    kind_ = static_cast<TimerKind>(qBound(0, settings_.value(kindKey).toInt(), 2));
    planned_ = qBound<qint64>(0, settings_.value(plannedKey).toLongLong(), maxDuration);
    clock_.setInterval(250);
    connect(&clock_, &QTimer::timeout, this, &FocusSession::refresh);
    // Positive values are UTC deadlines. Negative values store a paused time remaining.
    const qint64 saved = settings_.value(timeKey).toLongLong();
    if (saved > 0 && saved <= now() + maxDuration) {
        phase_ = Phase::Running;
        time_ = saved;
        clock_.start();
    } else if (saved < 0 && saved >= -maxDuration) {
        phase_ = Phase::Paused;
        time_ = -saved;
    }
}

bool FocusSession::active() const
{
    return phase_ == Phase::Running || phase_ == Phase::Paused;
}

qint64 FocusSession::remainingMs() const
{
    if (phase_ == Phase::Running) {
        return qMax<qint64>(0, time_ - now());
    }
    return phase_ == Phase::Paused ? time_ : 0;
}

QString FocusSession::remainingLabel() const
{
    const qint64 seconds = roundedSeconds(remainingMs());
    const auto twoDigits = [](qint64 value) { return QString::number(value).rightJustified(2, u'0'); };
    const QString minutes = twoDigits((seconds / 60) % 60) + u':' + twoDigits(seconds % 60);
    return seconds >= 3600 ? QString::number(seconds / 3600) + u':' + minutes : minutes;
}

void FocusSession::ensureToday()
{
    const auto today = QDate::currentDate();
    if (statsDate_ != today) {
        statsDate_ = today;
        focusMs_ = 0;
        focusCount_ = 0;
        interactionCount_ = 0;
    }
}

void FocusSession::saveStatistics()
{
    settings_.setValue(statsDateKey, statsDate_);
    settings_.setValue(focusMsKey, focusMs_);
    settings_.setValue(focusCountKey, focusCount_);
    settings_.setValue(interactionsKey, interactionCount_);
}

void FocusSession::recordInteraction()
{
    ensureToday();
    ++interactionCount_;
    saveStatistics();
    emit statisticsChanged();
}

QString FocusSession::todayLabel() const
{
    const bool today = statsDate_ == QDate::currentDate();
    return QStringLiteral("今日专注 %1 分钟 · %2 轮 · 互动 %3 次")
        .arg(today ? focusMs_ / 60000 : 0).arg(today ? focusCount_ : 0).arg(today ? interactionCount_ : 0);
}

int FocusSession::recommendedBreakMinutes() const
{
    return statsDate_ == QDate::currentDate() && focusCount_ > 0 && focusCount_ % 4 == 0 ? 15 : 5;
}

void FocusSession::start(Milliseconds duration)
{
    startTimer(TimerKind::Focus, duration);
}

void FocusSession::startBreak(bool longBreak)
{
    startTimer(longBreak ? TimerKind::LongBreak : TimerKind::ShortBreak,
               std::chrono::minutes(longBreak ? 15 : 5));
}

void FocusSession::startTimer(TimerKind kind, Milliseconds duration)
{
    const auto milliseconds = duration.count();
    if (active() || milliseconds <= 0 || milliseconds > maxDuration) {
        return;
    }
    kind_ = kind;
    planned_ = milliseconds;
    apply(Phase::Running, now() + milliseconds);
    if (kind_ == TimerKind::Focus) {
        emit focusStarted();
    } else {
        emit breakStarted();
    }
}

void FocusSession::pause()
{
    if (phase_ == Phase::Running) {
        const qint64 remaining = remainingMs();
        if (remaining > 0) {
            apply(Phase::Paused, remaining);
        } else {
            refresh();
        }
    }
}

void FocusSession::resume()
{
    if (phase_ == Phase::Paused) {
        apply(Phase::Running, now() + time_);
    }
}

void FocusSession::cancel()
{
    if (active()) {
        apply(Phase::Idle, 0);
    }
}

void FocusSession::setDoNotDisturb(bool enabled)
{
    if (doNotDisturb_ != enabled) {
        doNotDisturb_ = enabled;
        settings_.setValue(quietKey, enabled);
        settings_.sync();
        emit changed();
    }
}

void FocusSession::setEndAction(EndAction action)
{
    if (endAction_ != action) {
        endAction_ = action;
        settings_.setValue(endActionKey, static_cast<int>(action));
        settings_.sync();
        emit changed();
    }
}

void FocusSession::refresh()
{
    if (phase_ != Phase::Running) {
        return;
    }
    const qint64 seconds = roundedSeconds(remainingMs());
    if (seconds == 0) {
        const bool focus = kind_ == TimerKind::Focus;
        const bool endedToday = QDateTime::fromMSecsSinceEpoch(time_).date() == QDate::currentDate();
        if (focus && endedToday) {
            ensureToday();
            focusMs_ += planned_;
            ++focusCount_;
            saveStatistics();
        }
        apply(Phase::Completed, 0);
        if (endedToday) {
            if (focus) {
                emit statisticsChanged();
                emit completed();
            } else {
                emit breakCompleted();
            }
        }
    } else if (displayedSeconds_ != seconds) {
        displayedSeconds_ = seconds;
        emit changed();
    }
}

void FocusSession::apply(Phase phase, qint64 time)
{
    phase_ = phase;
    time_ = time;
    if (active()) {
        settings_.setValue(kindKey, static_cast<int>(kind_));
        settings_.setValue(plannedKey, planned_);
    } else {
        settings_.remove(kindKey);
        settings_.remove(plannedKey);
    }
    if (phase == Phase::Running) {
        settings_.setValue(timeKey, time);
        clock_.start();
    } else {
        clock_.stop();
        if (phase == Phase::Paused) {
            settings_.setValue(timeKey, -time);
        } else {
            settings_.remove(timeKey);
        }
    }
    displayedSeconds_ = roundedSeconds(remainingMs());
    settings_.sync();
    emit changed();
}
