#include "activity_meter.h"
#include "pet_asset.h"
#include "pet_window.h"

#include <QCryptographicHash>
#include <QEvent>
#include <QFutureWatcher>
#include <QMutex>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QThread>
#include <QThreadPool>
#include <QtConcurrentRun>

#include <cmath>
#include <utility>

namespace {
constexpr int meterWidth = 13;
constexpr int trackWidth = 5;
constexpr int glowPadding = 7;
constexpr int gap = 3;

QMutex& profileCacheMutex()
{
    static QMutex mutex;
    return mutex;
}

QCache<QByteArray, PetAppearance>& sharedProfiles()
{
    static QCache<QByteArray, PetAppearance> cache(64);
    return cache;
}

QThreadPool* lowPriorityAppearancePool()
{
    static QThreadPool* pool = [] {
        auto* instance = new QThreadPool;
        instance->setMaxThreadCount(1);
        instance->setThreadPriority(QThread::LowPriority);
        instance->setExpiryTimeout(15000);
        return instance;
    }();
    return pool;
}
}

ActivityMeter::ActivityMeter(PetWindow& pet)
    : QWidget(&pet, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                 | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput),
      pet_(pet)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setObjectName(QStringLiteral("petActivityMeter"));
    setAccessibleName(QStringLiteral("活跃度"));
    setFixedWidth(meterWidth);
    pet_.installEventFilter(this);
    feedbackAnimation_.setStartValue(0.0);
    feedbackAnimation_.setEndValue(1.0);
    feedbackAnimation_.setDuration(760);
    connect(&feedbackAnimation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        feedbackProgress_ = value.toDouble();
        update();
    });
    connect(&feedbackAnimation_, &QVariantAnimation::finished, this, &ActivityMeter::updateVisibility);
}

ActivityMeter::~ActivityMeter()
{
    if (analysis_) {
        analysis_->cancel();
    }
}

void ActivityMeter::setAsset(const PetAsset& asset)
{
    const auto key = QCryptographicHash::hash(asset.source, QCryptographicHash::Sha256);
    if (key == assetKey_) {
        return;
    }
    assetKey_ = key;
    if (analysis_) {
        analysis_->cancel();
        analysis_->deleteLater();
        analysis_ = nullptr;
    }
    {
        QMutexLocker lock(&profileCacheMutex());
        if (const auto* cached = sharedProfiles().object(key)) {
            appearance_ = *cached;
            updateVisibility();
            return;
        }
    }
    appearance_.reset();
    updateVisibility();
    auto* watcher = new QFutureWatcher<PetAppearance>(this);
    analysis_ = watcher;
    connect(watcher, &QFutureWatcher<PetAppearance>::finished, this, [this, watcher, key] {
        if (analysis_ != watcher) {
            return;
        }
        analysis_ = nullptr;
        if (!watcher->isCanceled() && watcher->future().resultCount() > 0) {
            appearance_ = watcher->result();
            {
                QMutexLocker lock(&profileCacheMutex());
                sharedProfiles().insert(key, new PetAppearance(*appearance_));
            }
            updateVisibility();
        }
        watcher->deleteLater();
    });
    const QImage firstFrame = asset.firstFrame.toImage();
    const QByteArray gif = asset.movie ? asset.source : QByteArray{};
    watcher->setFuture(QtConcurrent::run(lowPriorityAppearancePool(), [firstFrame, gif](QPromise<PetAppearance>& promise) {
        auto result = analyzePetAppearance(firstFrame, gif, [&promise] { return promise.isCanceled(); });
        if (result) {
            promise.addResult(std::move(*result));
        }
    }));
}

void ActivityMeter::setActivity(double percent)
{
    const int next = qRound(qBound(0.0, percent, 100.0));
    if (next != activity_) {
        activity_ = next;
        setAccessibleDescription(QStringLiteral("%1%").arg(activity_));
        update();
    }
}

void ActivityMeter::setDisplayEnabled(bool enabled)
{
    if (displayEnabled_ == enabled) {
        return;
    }
    displayEnabled_ = enabled;
    if (!displayEnabled_) {
        feedbackAnimation_.stop();
        feedbackProgress_ = 1;
    }
    updateVisibility();
}

void ActivityMeter::setSuppressed(bool suppressed)
{
    if (suppressed_ == suppressed) {
        return;
    }
    suppressed_ = suppressed;
    if (suppressed_) {
        feedbackAnimation_.stop();
        feedbackProgress_ = 1;
    }
    updateVisibility();
}

void ActivityMeter::setRevealed(bool revealed)
{
    revealed_ = revealed;
    updateVisibility();
}

void ActivityMeter::flash(Feedback feedback)
{
    if (!displayEnabled_ || suppressed_ || !pet_.isVisible()) {
        return;
    }
    feedbackStart_ = displayedActivity();
    feedback_ = feedback;
    feedbackAnimation_.stop();
    feedbackProgress_ = 0;
    feedbackAnimation_.start();
    updateVisibility();
    if (isVisible()) {
        raise();
    }
}

double ActivityMeter::displayedActivity() const
{
    const double progress = qMin(1.0, feedbackProgress_ * 2.4);
    const double remaining = 1 - progress;
    return activity_ + (feedbackStart_ - activity_) * remaining * remaining;
}

void ActivityMeter::updateVisibility()
{
    if (displayEnabled_ && !suppressed_
        && (revealed_ || feedbackAnimation_.state() == QAbstractAnimation::Running)
        && pet_.isVisible() && appearance_) {
        syncPosition();
        show();
        update();
    } else {
        hide();
    }
}

void ActivityMeter::syncPosition()
{
    const QRectF canvas = pet_.frameGeometry();
    const auto& bounds = appearance_->visibleBounds;
    const QRectF anchor(canvas.x() + bounds.x() * canvas.width(), canvas.y() + bounds.y() * canvas.height(),
                        bounds.width() * canvas.width(), bounds.height() * canvas.height());
    const auto* screen = pet_.screen();
    const QRect area = screen ? screen->availableGeometry() : pet_.frameGeometry().adjusted(-24, -24, 24, 24);
    setFixedHeight(qMin(area.height(), qMax(8, qRound(anchor.height())) + 2 * glowPadding));
    const int right = qRound(anchor.right() + gap - (meterWidth - trackWidth) / 2.0);
    const int left = qRound(anchor.left() - gap - (meterWidth + trackWidth) / 2.0);
    const int x = right + width() > area.right() + 1 && left >= area.left() ? left : right;
    move(qBound(area.left(), x, qMax(area.left(), area.right() + 1 - width())),
         qBound(area.top(), qRound(anchor.top()) - glowPadding, qMax(area.top(), area.bottom() + 1 - height())));
}

bool ActivityMeter::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == &pet_) {
        switch (event->type()) {
        case QEvent::Show:
            updateVisibility();
            break;
        case QEvent::Hide:
            feedbackAnimation_.stop();
            feedbackProgress_ = 1;
            hide();
            break;
        case QEvent::Move:
        case QEvent::Resize:
            if (isVisible()) {
                syncPosition();
            }
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ActivityMeter::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    const QRectF track((width() - trackWidth) / 2.0, glowPadding, trackWidth, height() - 2 * glowPadding);
    if (track.isEmpty() || !appearance_) {
        return;
    }
    QPainterPath outline;
    outline.addRoundedRect(track, track.width() / 2, track.width() / 2);
    QLinearGradient background(track.topLeft(), track.bottomLeft());
    const auto& palette = appearance_->palette;
    for (int i = 0; i < static_cast<int>(palette.size()); ++i) {
        QColor color = palette[i];
        color.setAlpha(110);
        background.setColorAt(i / 2.0, color);
    }
    painter.fillPath(outline, background);
    const double display = displayedActivity();
    if (display <= .01) {
        drawFeedback(painter, QPointF(track.center().x(), track.bottom()));
        return;
    }

    const double fillHeight = track.height() * display / 100;
    const QRectF fill(track.left(), track.bottom() - fillHeight, track.width(), fillHeight);
    QLinearGradient gradient(fill.bottomLeft(), fill.topLeft());
    gradient.setColorAt(0, QColor(QStringLiteral("#FFF1EB")));
    gradient.setColorAt(.65, QColor(QStringLiteral("#FDE68A")));
    gradient.setColorAt(qMax(.68, 1 - 4 / fillHeight), QColor(QStringLiteral("#FDE68A")));
    gradient.setColorAt(1, QColor(QStringLiteral("#FBBF24")));
    QPainterPath fillShape;
    const double radius = qMin(fill.width(), fill.height()) / 2;
    fillShape.addRoundedRect(fill, radius, radius);
    painter.save();
    painter.setClipPath(outline);
    painter.fillPath(fillShape, gradient);
    painter.restore();

    const QPointF tip(fill.center().x(), fill.top() + qMin(2.2, fill.height() / 2));
    QRadialGradient glow(tip, 6.4);
    glow.setColorAt(0, QColor(251, 191, 36, 130));
    glow.setColorAt(1, QColor(251, 191, 36, 0));
    painter.setBrush(glow);
    painter.drawEllipse(tip, 6.4, 6.4);

    QPainterPath sparkle;
    sparkle.moveTo(tip + QPointF(0, -5.1));
    sparkle.quadTo(tip + QPointF(.8, -.8), tip + QPointF(4.7, 0));
    sparkle.quadTo(tip + QPointF(.8, .8), tip + QPointF(0, 5.1));
    sparkle.quadTo(tip + QPointF(-.8, .8), tip + QPointF(-4.7, 0));
    sparkle.quadTo(tip + QPointF(-.8, -.8), tip + QPointF(0, -5.1));
    sparkle.closeSubpath();
    painter.setPen(QPen(QColor(251, 191, 36, 135), .65));
    painter.setBrush(Qt::white);
    painter.drawPath(sparkle);
    drawFeedback(painter, tip);
}

void ActivityMeter::drawFeedback(QPainter& painter, QPointF tip)
{
    if (feedbackProgress_ >= 1) {
        return;
    }
    const bool up = feedback_ == Feedback::Raise;
    const bool neutral = feedback_ == Feedback::Neutral;
    const double offset = 10 + 5 * std::sin(feedbackProgress_ * 3.141592653589793);
    tip.ry() = qBound(6.0, tip.y() + (up || neutral ? -offset : offset), qMax(6.0, height() - 6.0));
    QColor color = neutral ? QColor("#FFD992") : up ? QColor("#C8E7AE") : QColor("#F2B0BC");
    const double alpha = (1 - feedbackProgress_) * (.86 + .14 * std::cos(feedbackProgress_ * 14));
    color.setAlphaF(alpha);
    QRadialGradient glow(tip, 6);
    glow.setColorAt(0, color);
    glow.setColorAt(1, Qt::transparent);
    painter.setPen(Qt::NoPen);
    painter.setBrush(glow);
    painter.drawEllipse(tip, 6, 6);
    if (!neutral) {
        const double direction = up ? -1 : 1;
        QPainterPath arrow(tip + QPointF(-2.6, -.7 * direction));
        arrow.lineTo(tip + QPointF(0, 2.2 * direction));
        arrow.lineTo(tip + QPointF(2.6, -.7 * direction));
        painter.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(arrow);
    }
}
