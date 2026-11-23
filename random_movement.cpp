#include "random_movement.h"

#include <QApplication>
#include <QCursor>
#include <QMouseEvent>
#include <QScreen>
#include <QWidget>

#include <utility>

namespace {
QRectF movementBounds(const QWidget& window, const QRect& workArea)
{
    return {workArea.topLeft(), (workArea.size() - window.size()).expandedTo(QSize(0, 0))};
}
}

RandomMovement::RandomMovement(QWidget& window, std::function<void(double)> onActivity)
    : window_(window), rules_(*QRandomGenerator::global()), onActivity_(std::move(onActivity))
{
    window_.installEventFilter(this);
    timer_.setInterval(16);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &RandomMovement::tick);
    if (onActivity_) {
        onActivity_(rules_.activity());
    }
}

void RandomMovement::beginTargeting()
{
    targeting_ = true;
    rules_.cancelDestination();
    postpone();
}

void RandomMovement::cancelTargeting()
{
    targeting_ = false;
    rules_.cancelDestination();
    postpone();
}

QPointF RandomMovement::landingPoint(const QPointF& requested) const
{
    const QPointF half(window_.width() / 2.0, window_.height() / 2.0);
    const auto* screen = window_.screen();
    if (!screen) {
        return QPointF(window_.pos()) + half;
    }
    const auto bounds = movementBounds(window_, screen->availableGeometry());
    const auto target = (requested - half).toPoint();
    return QPointF(qBound(bounds.left(), double(target.x()), bounds.right()),
                   qBound(bounds.top(), double(target.y()), bounds.bottom())) + half;
}

void RandomMovement::moveTo(const QPointF& landing)
{
    targeting_ = false;
    if (window_.isVisible() && !window_.isMinimized()) {
        const QPointF half(window_.width() / 2.0, window_.height() / 2.0);
        rules_.setDestination(window_.pos(), landingPoint(landing) - half);
    }
    postpone();
}

void RandomMovement::setEnabled(bool enabled)
{
    enabled_ = enabled;
    postpone();
}

void RandomMovement::setSuspended(bool suspended)
{
    if (suspended_ != suspended) {
        suspended_ = suspended;
        postpone();
    }
}

void RandomMovement::setKibble(Kibble kibble)
{
    rules_.setKibble(kibble);
}

void RandomMovement::setQuietMode(bool quiet)
{
    if (quiet_ != quiet) {
        quiet_ = quiet;
        postpone();
    }
}

double RandomMovement::adjustActivity(double change)
{
    const double applied = rules_.adjustActivity(change);
    if (onActivity_ && applied != 0) {
        onActivity_(rules_.activity());
    }
    if (applied < 0 && !timer_.isActive()) {
        postpone();
    }
    return applied;
}

void RandomMovement::postpone()
{
    blocked_ = false;
    const bool directed = rules_.hasDestination();
    if (!window_.isVisible() || window_.isMinimized()
        || (!directed && !canRoam() && rules_.activity() >= 100)) {
        timer_.stop();
        return;
    }
    if (!directed && canRoam()) {
        rules_.resumeAt(window_.pos());
    }
    clock_.start();
    timer_.start();
}

bool RandomMovement::canRoam() const
{
    return enabled_ && !suspended_ && !quiet_ && !targeting_ && !mouseHeld_;
}

void RandomMovement::tick()
{
    const double elapsed = clock_.nsecsElapsed() / 1e9;
    clock_.restart();
    const bool directed = rules_.hasDestination();
    const bool roaming = canRoam();
    const double previousActivity = rules_.activity();
    if (!directed && (!roaming || QApplication::activeModalWidget() || QApplication::activePopupWidget())) {
        blocked_ = roaming;
        rules_.recoverActivity(elapsed);
    } else if (auto* screen = window_.screen()) {
        advanceMovement(elapsed, *screen, directed);
    }
    if (onActivity_ && previousActivity != rules_.activity()) {
        onActivity_(rules_.activity());
    }
    if (directed && !rules_.hasDestination()) {
        postpone();
    } else if (!directed && !roaming && rules_.activity() >= 100) {
        timer_.stop();
    }
}

void RandomMovement::advanceMovement(double elapsed, QScreen& screen, bool directed)
{
    if (&screen != screen_) {
        disconnect(workAreaChanged_);
        screen_ = &screen;
        workAreaChanged_ = connect(&screen, &QScreen::availableGeometryChanged, this, &RandomMovement::postpone);
        blocked_ = true;
    }
    // When the main thread wakes after a stall, resume from the visible pet instead of jumping ahead.
    if (!directed && (blocked_ || elapsed > .25)) {
        rules_.resumeAt(window_.pos());
        blocked_ = false;
    }
    const QRect workArea = screen.availableGeometry();
    MotionSpace space{movementBounds(window_, workArea), window_.size(), std::nullopt};
    if (!directed) {
        if (window_.width() > workArea.width() || window_.height() > workArea.height()) {
            rules_.recoverActivity(elapsed);
            return;
        }
        const QPoint cursor = QCursor::pos();
        if (workArea.contains(cursor)) {
            space.cursor = cursor;
        }
    }
    rules_.advance(elapsed, space);
    const QPoint position = rules_.position().toPoint();
    if (position != window_.pos()) {
        window_.move(position);
    }
}

bool RandomMovement::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
        if (static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
            rules_.cancelDestination();
            mouseHeld_ = true;
            pressPosition_ = window_.pos();
            postpone();
        }
        break;
    case QEvent::MouseButtonRelease:
        if (static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
            mouseHeld_ = false;
            rules_.interacted((window_.pos() - pressPosition_).manhattanLength() >= QApplication::startDragDistance());
            postpone();
        }
        break;
    case QEvent::WindowDeactivate:
        if (mouseHeld_) {
            mouseHeld_ = false;
            postpone();
        }
        break;
    case QEvent::Hide:
        mouseHeld_ = false;
        targeting_ = false;
        rules_.cancelDestination();
        timer_.stop();
        break;
    case QEvent::Resize:
    case QEvent::WindowStateChange:
        rules_.cancelDestination();
        postpone();
        break;
    case QEvent::Show:
        postpone();
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}
