#include "companion_menu.h"
#include "focus_session.h"

#include <QActionGroup>
#include <QEvent>
#include <QInputDialog>
#include <QMenu>
#include <QPointer>
#include <QScreen>

namespace {
QString statusText(const FocusSession& focus)
{
    const QString kind = focus.kind() == FocusSession::TimerKind::Focus ? QStringLiteral("专注")
        : focus.kind() == FocusSession::TimerKind::ShortBreak ? QStringLiteral("短休息") : QStringLiteral("长休息");
    switch (focus.phase()) {
    case FocusSession::Phase::Idle: return QStringLiteral("尚未开始计时");
    case FocusSession::Phase::Running: return QStringLiteral("%1中 · %2").arg(kind, focus.remainingLabel());
    case FocusSession::Phase::Paused: return QStringLiteral("%1已暂停 · %2").arg(kind, focus.remainingLabel());
    case FocusSession::Phase::Completed:
        return focus.kind() == FocusSession::TimerKind::Focus
            ? QStringLiteral("专注已结束 · 建议休息 %1 分钟").arg(focus.recommendedBreakMinutes())
            : QStringLiteral("休息已结束");
    }
    return {};
}
}

QMenu* addCompanionMenu(QMenu& parent, QWidget& pet, FocusSession& focus)
{
    auto* menu = parent.addMenu(QStringLiteral("桌面陪伴"));
    menu->setObjectName(QStringLiteral("petCompanionMenu"));
    auto* status = menu->addAction(statusText(focus));
    status->setObjectName(QStringLiteral("focusStatus"));
    status->setEnabled(false);
    auto* today = menu->addAction(focus.todayLabel());
    today->setObjectName(QStringLiteral("todayCompanionStatistics"));
    today->setEnabled(false);
    QObject::connect(&focus, &FocusSession::statisticsChanged, menu, [&, today] { today->setText(focus.todayLabel()); });
    auto* quiet = menu->addAction(QStringLiteral("免打扰"));
    quiet->setObjectName(QStringLiteral("manualDoNotDisturb"));
    quiet->setCheckable(true);
    quiet->setChecked(focus.doNotDisturb());
    QObject::connect(quiet, &QAction::triggered, &focus, &FocusSession::setDoNotDisturb);

    menu->addSeparator();
    auto* start = menu->addMenu(QStringLiteral("开始专注"));
    for (const int minutes : {15, 25, 50}) {
        auto* duration = start->addAction(QStringLiteral("%1 分钟").arg(minutes));
        duration->setObjectName(QStringLiteral("focusMinutes%1").arg(minutes));
        QObject::connect(duration, &QAction::triggered, &focus, [&focus, minutes] {
            focus.start(std::chrono::minutes(minutes));
        });
    }
    auto* custom = start->addAction(QStringLiteral("自定义时长…"));
    const QPointer<QWidget> dialogAnchor(&pet);
    QObject::connect(custom, &QAction::triggered, menu, [dialogAnchor, &focus] {
        bool confirmed = false;
        const int minutes = QInputDialog::getInt(dialogAnchor.data(), QStringLiteral("自定义专注时长"),
                                                QStringLiteral("专注分钟数"), 25, 1, 180, 1, &confirmed);
        if (confirmed) {
            focus.start(std::chrono::minutes(minutes));
        }
    });

    auto* breaks = menu->addMenu(QStringLiteral("开始休息"));
    breaks->setObjectName(QStringLiteral("breakMenu"));
    auto* recommended = breaks->addAction(QStringLiteral("建议休息"));
    QObject::connect(recommended, &QAction::triggered, &focus, [&focus] {
        focus.startBreak(focus.recommendedBreakMinutes() == 15);
    });
    breaks->addSeparator();
    auto* shortBreak = breaks->addAction(QStringLiteral("短休息 · 5 分钟"));
    shortBreak->setObjectName(QStringLiteral("startShortBreak"));
    QObject::connect(shortBreak, &QAction::triggered, &focus, [&focus] { focus.startBreak(false); });
    auto* longBreak = breaks->addAction(QStringLiteral("长休息 · 15 分钟"));
    longBreak->setObjectName(QStringLiteral("startLongBreak"));
    QObject::connect(longBreak, &QAction::triggered, &focus, [&focus] { focus.startBreak(true); });

    auto* pause = menu->addAction(QStringLiteral("暂停专注"));
    pause->setObjectName(QStringLiteral("pauseFocus"));
    QObject::connect(pause, &QAction::triggered, &focus, [&focus] {
        if (focus.phase() == FocusSession::Phase::Paused) {
            focus.resume();
        } else {
            focus.pause();
        }
    });
    auto* cancel = menu->addAction(QStringLiteral("取消本次专注"));
    cancel->setObjectName(QStringLiteral("cancelFocus"));
    QObject::connect(cancel, &QAction::triggered, &focus, &FocusSession::cancel);

    auto* reminder = menu->addMenu(QStringLiteral("结束提醒"));
    auto* reminders = new QActionGroup(reminder);
    using EndAction = FocusSession::EndAction;
    for (const auto& [text, value] : {std::pair{QStringLiteral("轻跳一下"), EndAction::Bounce},
                                    std::pair{QStringLiteral("播放「开心」动作"), EndAction::Happy},
                                    std::pair{QStringLiteral("仅显示文字"), EndAction::Text}}) {
        auto* action = reminder->addAction(text);
        action->setCheckable(true);
        action->setData(static_cast<int>(value));
        reminders->addAction(action);
    }
    QObject::connect(reminders, &QActionGroup::triggered, &focus, [&focus](QAction* action) {
        focus.setEndAction(static_cast<EndAction>(action->data().toInt()));
    });

    const auto update = [&, menu, status, quiet, start, breaks, recommended, pause, cancel, reminders] {
        status->setText(statusText(focus));
        quiet->setChecked(focus.doNotDisturb());
        const bool active = focus.active();
        menu->setTitle(active ? QStringLiteral("桌面陪伴 · %1").arg(focus.remainingLabel())
                              : QStringLiteral("桌面陪伴"));
        start->menuAction()->setVisible(!active);
        breaks->menuAction()->setVisible(!active);
        recommended->setText(QStringLiteral("开始建议休息 · %1 分钟").arg(focus.recommendedBreakMinutes()));
        recommended->setVisible(focus.phase() == FocusSession::Phase::Completed
                                && focus.kind() == FocusSession::TimerKind::Focus);
        pause->setVisible(active);
        const auto label = focus.kind() == FocusSession::TimerKind::Focus ? QStringLiteral("专注") : QStringLiteral("休息");
        pause->setText((focus.phase() == FocusSession::Phase::Paused ? QStringLiteral("继续") : QStringLiteral("暂停")) + label);
        cancel->setVisible(active);
        cancel->setText(QStringLiteral("取消本次") + label);
        for (auto* action : reminders->actions()) {
            action->setChecked(action->data().toInt() == static_cast<int>(focus.endAction()));
        }
    };
    QObject::connect(&focus, &FocusSession::changed, menu, update);
    QObject::connect(menu, &QMenu::aboutToShow, menu, [&, today, update] {
        today->setText(focus.todayLabel());
        update();
    });
    update();
    return menu;
}

FocusNotice::FocusNotice(QWidget& pet)
    : QLabel(&pet, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                       | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput),
      pet_(pet)
{
    setObjectName(QStringLiteral("petFocusNotice"));
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setText(QStringLiteral("专注结束，休息一下吧"));
    setAccessibleName(text());
    setAlignment(Qt::AlignCenter);
    expiry_.setSingleShot(true);
    connect(&expiry_, &QTimer::timeout, this, &FocusNotice::hide);
    pet_.installEventFilter(this);
}

void FocusNotice::announce(const QString& message)
{
    if (!pet_.isVisible()) {
        return;
    }
    setText(message);
    setAccessibleName(message);
    if (palette().color(QPalette::Window).lightness() < 128) {
        setStyleSheet(QStringLiteral("QLabel { background: #403631; color: #fff0d8;"
                                     "border: 1px solid #75614b; border-radius: 10px; padding: 6px 11px; }"));
    } else {
        setStyleSheet(QStringLiteral("QLabel { background: #fff8eb; color: #59453a;"
                                     "border: 1px solid #e4d3b8; border-radius: 10px; padding: 6px 11px; }"));
    }
    adjustSize();
    placeBesidePet();
    show();
    raise();
    expiry_.start(5000);
}

void FocusNotice::placeBesidePet()
{
    const auto* screen = pet_.screen();
    if (!screen) {
        return;
    }
    constexpr int gap = 8;
    const QRect area = screen->availableGeometry();
    const QRect pet = pet_.frameGeometry();
    QPoint target(pet.right() + 1 + gap, pet.top() + pet.height() / 3);
    if (target.x() + width() > area.right() + 1) {
        target.setX(pet.left() - width() - gap);
        if (target.x() < area.left()) {
            target.setX(pet.center().x() - width() / 2);
            target.setY(pet.top() - gap - height());
            if (target.y() < area.top()) {
                target.setY(pet.bottom() + 1 + gap);
            }
        }
    }
    move(qBound(area.left(), target.x(), qMax(area.left(), area.right() + 1 - width())),
         qBound(area.top(), target.y(), qMax(area.top(), area.bottom() + 1 - height())));
}

bool FocusNotice::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == &pet_ && isVisible()) {
        if (event->type() == QEvent::Hide) {
            if (!pet_.property("auraTransientHide").toBool()) {
                expiry_.stop();
                hide();
            }
        } else if (event->type() == QEvent::Move || event->type() == QEvent::Resize) {
            placeBesidePet();
        }
    }
    return QLabel::eventFilter(watched, event);
}
