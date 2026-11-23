#pragma once

#include <QDate>
#include <QObject>
#include <QTimer>

#include <chrono>

class QSettings;

class FocusSession : public QObject {
    Q_OBJECT

public:
    enum class Phase { Idle, Running, Paused, Completed };
    enum class TimerKind { Focus, ShortBreak, LongBreak };
    enum class EndAction { Bounce, Happy, Text };

    explicit FocusSession(QSettings& settings);

    Phase phase() const { return phase_; }
    bool active() const;
    bool doNotDisturb() const { return doNotDisturb_; }
    bool quiet() const { return doNotDisturb_ || (active() && kind_ == TimerKind::Focus); }
    TimerKind kind() const { return kind_; }
    int recommendedBreakMinutes() const;
    QString todayLabel() const;
    void recordInteraction();
    EndAction endAction() const { return endAction_; }
    QString remainingLabel() const;

    void start(std::chrono::milliseconds duration);
    void startBreak(bool longBreak);
    void pause();
    void resume();
    void cancel();
    void setDoNotDisturb(bool enabled);
    void setEndAction(EndAction action);

signals:
    void changed();
    void completed();
    void focusStarted();
    void breakStarted();
    void breakCompleted();
    void statisticsChanged();

private:
    using Milliseconds = std::chrono::milliseconds;

    qint64 remainingMs() const;
    void refresh();
    void apply(Phase phase, qint64 time);
    void startTimer(TimerKind kind, Milliseconds duration);
    void ensureToday();
    void saveStatistics();

    QSettings& settings_;
    QTimer clock_;
    Phase phase_ = Phase::Idle;
    TimerKind kind_ = TimerKind::Focus;
    QDate statsDate_;
    qint64 focusMs_ = 0;
    qint64 planned_ = 0;
    int focusCount_ = 0;
    int interactionCount_ = 0;
    EndAction endAction_ = EndAction::Bounce;
    qint64 time_ = 0;
    qint64 displayedSeconds_ = -1;
    bool doNotDisturb_ = false;
};
