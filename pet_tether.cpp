#include "pet_tether.h"
#include "pet_window.h"
#include "random_movement.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QElapsedTimer>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>

#include <array>
#include <cmath>

namespace {
constexpr int holdMs = 220;
constexpr qreal dragThreshold = 6;
constexpr qreal fadeSeconds = .30;
constexpr QColor silver(248, 250, 252);
constexpr QColor mist(203, 213, 225);

double length(QPointF point) { return std::hypot(point.x(), point.y()); }

QColor opacity(QColor color, double alpha)
{
    color.setAlphaF(static_cast<float>(qBound(0.0, alpha, 1.0)));
    return color;
}

QImage ghostArrow()
{
    constexpr int width = 72;
    constexpr int height = 52;
    constexpr std::array<int, 5> gaussian{1, 4, 6, 4, 1};
    QImage silhouette(width, height, QImage::Format_ARGB32_Premultiplied);
    silhouette.fill(Qt::transparent);
    {
        QPainter painter(&silhouette);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath shape;
        shape.moveTo(13, 24);
        shape.cubicTo(13, 18, 25, 18, 36, 19);
        shape.cubicTo(40, 18, 35, 8, 44, 13);
        shape.quadTo(63, 23, 61, 28);
        shape.quadTo(56, 33, 43, 38);
        shape.cubicTo(36, 41, 41, 32, 35, 32);
        shape.cubicTo(24, 32, 13, 33, 13, 27);
        shape.closeSubpath();
        painter.fillPath(shape, opacity(mist, .40));
    }
    std::array<int, width * height> horizontal{};
    for (int y = 0; y < height; ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(silhouette.constScanLine(y));
        for (int x = 2; x < width - 2; ++x) {
            for (int i = 0; i < 5; ++i) {
                horizontal[y * width + x] += gaussian[i] * qAlpha(row[x + i - 2]);
            }
        }
    }
    QImage blurred(width, height, QImage::Format_ARGB32);
    blurred.fill(Qt::transparent);
    for (int y = 2; y < height - 2; ++y) {
        auto* row = reinterpret_cast<QRgb*>(blurred.scanLine(y));
        for (int x = 0; x < width; ++x) {
            int alpha = 0;
            for (int i = 0; i < 5; ++i) {
                alpha += gaussian[i] * horizontal[(y + i - 2) * width + x];
            }
            row[x] = qRgba(203, 213, 225, alpha / 256);
        }
    }
    return blurred;
}

void drawSparkle(QPainter& painter, const QPointF& point, double alpha)
{
    painter.setPen(Qt::NoPen);
    QRadialGradient aura(point, 20);
    aura.setColorAt(0, opacity(silver, .27 * alpha));
    aura.setColorAt(1, opacity(mist, 0));
    painter.setBrush(aura);
    painter.drawEllipse(point, 20, 20);
    QPainterPath star;
    star.moveTo(point + QPointF(0, -10));
    star.quadTo(point + QPointF(1.2, -1.2), point + QPointF(8, 0));
    star.quadTo(point + QPointF(1.2, 1.2), point + QPointF(0, 10));
    star.quadTo(point + QPointF(-1.2, 1.2), point + QPointF(-8, 0));
    star.quadTo(point + QPointF(-1.2, -1.2), point + QPointF(0, -10));
    star.closeSubpath();
    painter.fillPath(star, opacity(silver, .52 * alpha));
}
}

class TetherView : public QWidget {
public:
    explicit TetherView(PetWindow& pet)
        : QWidget(&pet, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                         | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus),
          pet_(pet), arrow_(ghostArrow())
    {
        setObjectName(QStringLiteral("petTetherVisual"));
        setAccessibleName(QStringLiteral("灵犀牵引"));
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_MacAlwaysShowToolWindow);
        frames_.setInterval(16);
        frames_.setTimerType(Qt::PreciseTimer);
        connect(&frames_, &QTimer::timeout, this, &TetherView::animate);
    }

    void start(const QPointF& pointer)
    {
        const auto* screen = pet_.screen();
        if (!screen) {
            return;
        }
        setGeometry(screen->availableGeometry());
        desired_ = shown_ = localPointer(pointer);
        elapsed_ = phase_ = stretch_ = bounce_ = lean_ = 0;
        released_ = -1;
        frozenStart_ = originFor(shown_);
        clock_.start();
        show();
        raise();
        dirty_ = rect();
        update();
        frames_.start();
    }

    void pointAt(const QPointF& pointer)
    {
        desired_ = localPointer(pointer);
    }

    void finish(const QPointF& landing)
    {
        if (!isVisible()) {
            return;
        }
        frozenStart_ = originFor(shown_);
        landing_ = landing - QPointF(pos());
        released_ = elapsed_;
        pet_.setAimLean(0);
        repaintGuide();
    }

    void cancel()
    {
        frames_.stop();
        pet_.setAimLean(0);
        hide();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(event->rect(), Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const double sinceRelease = released_ < 0 ? 0 : elapsed_ - released_;
        const double guideAlpha = qMax(0.0, 1 - sinceRelease / .19);
        const QPointF start = released_ < 0 ? originFor(shown_) : frozenStart_;
        if (guideAlpha > 0) {
            painter.setOpacity(guideAlpha);
            drawGuide(painter, start);
            drawReticle(painter, start);
        }
        if (released_ >= 0) {
            painter.setOpacity(1);
            drawSparkle(painter, landing_, qMax(0.0, 1 - sinceRelease / fadeSeconds));
        }
    }

private:
    QPointF localPointer(const QPointF& global) const
    {
        const QPointF point = global - QPointF(pos());
        return {qBound(0.0, point.x(), static_cast<double>(width())),
                qBound(0.0, point.y(), static_cast<double>(height()))};
    }

    QPointF originFor(const QPointF& target) const
    {
        const auto box = pet_.frameGeometry();
        const QPointF center = box.center() - pos();
        const QPointF delta = target - center;
        const double horizontal = (box.width() * .24) / qMax(1.0, qAbs(delta.x()));
        const double vertical = (box.height() * .29) / qMax(1.0, qAbs(delta.y()));
        return center + delta * qMin(.68, qMin(horizontal, vertical));
    }

    void animate()
    {
        const double seconds = qBound(0.0, clock_.nsecsElapsed() / 1e9, .05);
        clock_.restart();
        elapsed_ += seconds;
        if (released_ >= 0) {
            if (elapsed_ - released_ >= fadeSeconds) {
                cancel();
                return;
            }
        } else {
            const QPointF step = (desired_ - shown_) * (1 - std::exp(-seconds / .05));
            shown_ += step;
            const double targetStretch = qMin(1.0, length(step) / qMax(.001, seconds) / 1200);
            if (length(step) > .2) {
                cursorAngle_ = std::atan2(step.y(), step.x()) * (180 / 3.141592653589793);
            }
            bounce_ += ((targetStretch - stretch_) * 185 - bounce_ * 18) * seconds;
            stretch_ = qBound(0.0, stretch_ + bounce_ * seconds, 1.1);
            const double distance = length(shown_ - originFor(shown_));
            phase_ = std::fmod(phase_ + seconds / 1.2 * (1 + .35 * qMin(distance / 700, 1.0)), 1.0);
            const double x = shown_.x() + pos().x() - pet_.frameGeometry().center().x();
            lean_ += (qBound(-4.0, x / 45.0, 4.0) - lean_) * (1 - std::exp(-seconds / .09));
            pet_.setAimLean(lean_);
        }
        repaintGuide();
    }

    void repaintGuide()
    {
        const auto start = released_ < 0 ? originFor(shown_) : frozenStart_;
        QRect frame = QRectF(start, shown_).normalized().adjusted(-55,-55,55,55).toAlignedRect();
        if (released_ >= 0) {
            frame |= QRectF(landing_ - QPointF(22,22),QSizeF(44,44)).toAlignedRect();
        }
        update(dirty_.united(frame));
        dirty_ = frame;
    }

    void drawGuide(QPainter& painter, const QPointF& start) const
    {
        const auto delta = shown_ - start;
        const double distance = length(delta);
        if (distance < 6) {
            return;
        }
        const QPointF direction = delta / distance;
        const QPointF end = shown_ - direction * qMin(13.0, distance * .25);
        const auto bend = QPointF(-direction.y(), direction.x()) * qMin(36.0, distance * .08);
        QPainterPath line(start);
        line.quadTo((start + end) / 2 + bend, end);
        painter.strokePath(line, QPen(opacity(mist, .35), 1.8, Qt::SolidLine, Qt::RoundCap));
        QLinearGradient shimmer(start, end);
        for (int i = 0; i <= 22; ++i) {
            const double t = i / 22.0;
            const double distanceToLight = std::remainder(t - phase_, 1.0) / .114;
            shimmer.setColorAt(t, opacity(silver, .46 * std::exp(-.5 * distanceToLight * distanceToLight)));
        }
        painter.strokePath(line, QPen(QBrush(shimmer), 1.12, Qt::SolidLine, Qt::RoundCap));
    }

    void drawReticle(QPainter& painter, const QPointF& start) const
    {
        const auto line = shown_ - start;
        const double heading = std::atan2(line.y(),line.x()) * (180 / 3.141592653589793);
        painter.save();
        painter.translate(shown_ + QPointF(0, std::sin(elapsed_*4) * 1.8 * (1 - qMin(stretch_,1.0))));
        painter.rotate(heading);
        painter.scale(1 + .44 * stretch_, 1 - .10 * stretch_);
        painter.drawImage(QRectF(-17,-19,49,38),arrow_);
        painter.restore();

        const double radius = 17 + .85 * std::sin(elapsed_*3.1);
        painter.save();
        painter.translate(shown_);
        painter.rotate(elapsed_ * 24);
        painter.setPen(QPen(opacity(silver,.57),1.25,Qt::SolidLine,Qt::RoundCap));
        for (int i = 0; i < 6; ++i) {
            painter.drawArc(QRectF(-radius,-radius,2*radius,2*radius), i * 60 * 16, 36 * 16);
        }
        painter.restore();

        painter.save();
        painter.translate(shown_);
        painter.rotate(cursorAngle_);
        painter.setPen(Qt::NoPen);
        QRadialGradient dot(QPointF{}, 8);
        dot.setColorAt(0, opacity(silver,.47));
        dot.setColorAt(.42, opacity(silver,.43));
        dot.setColorAt(1, opacity(mist,0));
        painter.setBrush(dot);
        painter.drawEllipse(QPointF{}, 7.2 * (1 + .58 * stretch_), 7.2 * (1 - .24 * stretch_));
        painter.restore();
    }

    PetWindow& pet_;
    QImage arrow_;
    QTimer frames_;
    QElapsedTimer clock_;
    QPointF desired_;
    QPointF shown_;
    QPointF frozenStart_;
    QPointF landing_;
    QRect dirty_;
    double elapsed_ = 0;
    double released_ = -1;
    double phase_ = 0;
    double stretch_ = 0;
    double bounce_ = 0;
    double lean_ = 0;
    double cursorAngle_ = 0;
};

PetTether::PetTether(PetWindow& pet, RandomMovement& movement)
    : pet_(pet), movement_(movement), view_(std::make_unique<TetherView>(pet))
{
    pet_.installEventFilter(this);
    hold_.setSingleShot(true);
    connect(&hold_, &QTimer::timeout, this, &PetTether::beginAim);
}

PetTether::~PetTether() = default;

void PetTether::cancel()
{
    hold_.stop();
    gesture_ = Gesture::None;
    view_->cancel();
    movement_.cancelTargeting();
}

void PetTether::beginAim()
{
    if (gesture_ != Gesture::Pending) {
        return;
    }
    hold_.stop();
    gesture_ = Gesture::Aiming;
    if (aimingStarted) {
        aimingStarted();
    }
    view_->start(pointer_);
}

bool PetTether::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick: {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::RightButton && !QApplication::activeModalWidget()) {
            cancel();
            gesture_ = Gesture::Pending;
            pressed_ = pointer_ = mouse->globalPosition();
            movement_.beginTargeting();
            hold_.start(holdMs);
            return true;
        }
        if (gesture_ != Gesture::None) {
            cancel();
        }
        break;
    }
    case QEvent::MouseMove:
        if (gesture_ != Gesture::None) {
            pointer_ = static_cast<QMouseEvent*>(event)->globalPosition();
            if (gesture_ == Gesture::Pending && length(pointer_ - pressed_) >= dragThreshold) {
                beginAim();
            }
            if (gesture_ == Gesture::Aiming) {
                view_->pointAt(pointer_);
            }
            return true;
        }
        break;
    case QEvent::MouseButtonRelease: {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::RightButton) {
            break;
        }
        hold_.stop();
        const auto previous = gesture_;
        gesture_ = Gesture::None;
        if (previous == Gesture::Aiming) {
            const QPointF landing = movement_.landingPoint(mouse->globalPosition());
            view_->finish(landing);
            movement_.moveTo(landing);
        } else if (previous == Gesture::Pending) {
            movement_.cancelTargeting();
            if (menuRequested) {
                menuRequested(mouse->globalPosition().toPoint());
            }
        }
        return true;
    }
    case QEvent::ContextMenu: {
        const auto* menu = static_cast<QContextMenuEvent*>(event);
        if (menu->reason() == QContextMenuEvent::Keyboard && menuRequested) {
            cancel();
            menuRequested(pet_.frameGeometry().center());
        }
        return true;
    }
    case QEvent::ShortcutOverride:
    case QEvent::KeyPress:
        if (gesture_ != Gesture::None && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            event->accept();
            if (event->type() == QEvent::KeyPress) {
                cancel();
            }
            return true;
        }
        break;
    case QEvent::WindowDeactivate:
        if (gesture_ != Gesture::None) {
            cancel();
        }
        break;
    case QEvent::Hide:
        cancel();
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}
