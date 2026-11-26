#include "pet_window.h"
#include "pet_asset.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QShowEvent>
#include <QTimer>
#include <QUrl>

#include <cmath>
#include <utility>

PetWindow::PetWindow()
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAcceptDrops(true);
    setWindowTitle(QStringLiteral("AuraPet"));
    setFocusPolicy(Qt::StrongFocus);
    actionHold_.setSingleShot(true);
    connect(&actionHold_, &QTimer::timeout, this, &PetWindow::returnToIdle);
    bounceAnimation_.setDuration(630);
    bounceAnimation_.setStartValue(0.0);
    bounceAnimation_.setEndValue(1.0);
    connect(&bounceAnimation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        bounceProgress_ = value.toDouble();
        update();
    });

}

void PetWindow::setAsset(PetAsset asset, int scalePercent)
{
    clearAction();
    bounceAnimation_.stop();
    bounceProgress_ = 1;
    movie_ = std::move(asset.movie);
    firstFrame_ = std::move(asset.firstFrame);
    cachedIdleScaled_ = {};
    cachedIdlePixelSize_ = {};
    cachedIdleFrameNumber_ = -2;
    movieStarted_ = false;
    baseSize_ = firstFrame_.deviceIndependentSize().toSize();
    if (baseSize_.width() > 360 || baseSize_.height() > 360) {
        baseSize_.scale(360, 360, Qt::KeepAspectRatio);
    }
    baseSize_ = baseSize_.expandedTo(QSize(1, 1));
    setScalePercent(scalePercent);
    if (movie_) {
        connect(movie_.get(), &QMovie::frameChanged, this, [this] { update(); });
        if (isVisible()) {
            resumeAnimation();
        }
    }
    update();
}

void PetWindow::setScalePercent(int scalePercent)
{
    scalePercent_ = qBound(50, scalePercent, 200);
    placeAt(pos());
}

int PetWindow::scalePercent() const
{
    return scalePercent_;
}

bool PetWindow::applyZoomKey(const QKeyEvent& event)
{
    if (event.modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)
        || (event.key() != Qt::Key_Plus && event.key() != Qt::Key_Minus)) {
        return false;
    }
    setScalePercent(scalePercent_ + (event.key() == Qt::Key_Plus ? 10 : -10));
    if (layoutChanged) {
        layoutChanged();
    }
    return true;
}

void PetWindow::keyPressEvent(QKeyEvent* event)
{
    if (applyZoomKey(*event)) {
        event->accept();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void PetWindow::setPrimaryOnly(bool enabled)
{
    primaryOnly_ = enabled;
    if (enabled) {
        moveToDisplay(QGuiApplication::primaryScreen());
    }
}

void PetWindow::moveToDisplay(const QScreen* target)
{
    if (primaryOnly_) {
        target = QGuiApplication::primaryScreen();
    }
    if (!target) {
        return;
    }
    const auto* old = QGuiApplication::screenAt(frameGeometry().center());
    const auto bounds = target->availableGeometry();
    const auto fractions = [](int point, int size, int origin, int extent) {
        return qBound(0.0, double(point - origin) / qMax(1, extent - size), 1.0);
    };
    QPoint position(bounds.bottomRight() + QPoint(1 - width(), 1 - height()));
    if (old) {
        const auto source = old->availableGeometry();
        position = bounds.topLeft() + QPoint(
            qRound(fractions(x(), width(), source.x(), source.width()) * qMax(0, bounds.width() - width())),
            qRound(fractions(y(), height(), source.y(), source.height()) * qMax(0, bounds.height() - height())));
    }
    placeOnDisplay(position, target, true);
}

void PetWindow::placeAt(const QPoint& position, bool snapToEdge)
{
    auto* screen = primaryOnly_ ? QGuiApplication::primaryScreen()
                              : QGuiApplication::screenAt(position + rect().center());
    if (!screen) {
        screen = QGuiApplication::screenAt(position);
    }
    placeOnDisplay(position, screen ? screen : QGuiApplication::primaryScreen(), snapToEdge);
}

void PetWindow::placeOnDisplay(const QPoint& position, const QScreen* screen, bool snapToEdge)
{
    QSize displaySize = (baseSize_ * (scalePercent_ / 100.0)).expandedTo(QSize(1, 1));
    if (!screen) {
        setFixedSize(displaySize);
        move(position);
        return;
    }

    const QRect bounds = screen->availableGeometry();
    if (displaySize.width() > bounds.width() || displaySize.height() > bounds.height()) {
        displaySize.scale(bounds.size(), Qt::KeepAspectRatio);
    }
    setFixedSize(displaySize.expandedTo(QSize(1, 1)));
    const int minX = bounds.left();
    const int minY = bounds.top();
    const int maxX = qMax(minX, bounds.right() - width() + 1);
    const int maxY = qMax(minY, bounds.bottom() - height() + 1);
    const auto fit = [snapToEdge](int value, int minimum, int maximum) {
        const int bounded = qBound(minimum, value, maximum);
        if (snapToEdge && bounded - minimum <= 18) {
            return minimum;
        }
        if (snapToEdge && maximum - bounded <= 18) {
            return maximum;
        }
        return bounded;
    };
    move(fit(position.x(), minX, maxX), fit(position.y(), minY, maxY));
}

void PetWindow::setClickThrough(bool enabled)
{
    if (windowFlags().testFlag(Qt::WindowTransparentForInput) == enabled) {
        return;
    }
    const bool visible = isVisible();
    const QPoint position = pos();
    setProperty("auraTransientHide", true);
    setWindowFlag(Qt::WindowTransparentForInput, enabled);
    placeAt(position);
    if (visible) {
        show();
    }
    setProperty("auraTransientHide", false);
}

void PetWindow::setAlwaysOnTop(bool enabled)
{
    alwaysOnTop_ = enabled;
    if (windowFlags().testFlag(Qt::WindowStaysOnTopHint) == enabled) {
        return;
    }
    const bool visible = isVisible();
    const QPoint position = pos();
    setProperty("auraTransientHide", true);
    setWindowFlag(Qt::WindowStaysOnTopHint, enabled);
    placeAt(position);
    if (visible) {
        show();
    }
    setProperty("auraTransientHide", false);
}

void PetWindow::setAimLean(qreal degrees)
{
    if (qAbs(aimLean_ - degrees) > .04 || (degrees == 0 && aimLean_ != 0)) {
        aimLean_ = degrees;
        update();
    }
}

void PetWindow::setTransitPose(qreal leanDegrees, qreal scaleX, qreal scaleY)
{
    if (qAbs(transitLean_ - leanDegrees) > .02
        || qAbs(transitScaleX_ - scaleX) > .002
        || qAbs(transitScaleY_ - scaleY) > .002) {
        transitLean_ = leanDegrees;
        transitScaleX_ = scaleX;
        transitScaleY_ = scaleY;
        update();
    }
}

void PetWindow::resetTransitPose()
{
    if (transitLean_ != 0 || transitScaleX_ != 1.0 || transitScaleY_ != 1.0) {
        transitLean_ = 0;
        transitScaleX_ = 1.0;
        transitScaleY_ = 1.0;
        update();
    }
}

void PetWindow::playBounce()
{
    bounceAnimation_.stop();
    bounceAnimation_.start();
}

void PetWindow::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    const bool hasTransitPose = !qFuzzyIsNull(transitLean_)
        || !qFuzzyCompare(transitScaleX_, 1.0)
        || !qFuzzyCompare(transitScaleY_, 1.0);
    if (!qFuzzyIsNull(aimLean_) || bounceProgress_ < 1 || hasTransitPose) {
        painter.fillRect(rect(), Qt::transparent);
        const QPointF pivot = hasTransitPose
            ? QPointF(rect().center().x(), rect().bottom() - height() * 0.12)
            : QPointF(rect().center());
        const double pulse = bounceProgress_ < 1 ? std::sin(bounceProgress_ * 3.141592653589793) : 0.0;
        const double tilt = aimLean_ + transitLean_ + pulse * 12 * std::sin(bounceProgress_ * 8);
        painter.translate(pivot + QPointF(2 * pulse, -qMin(12.0, height() * .08) * pulse));
        painter.rotate(tilt);
        const double maxAxisScale = qMax(1.0, qMax(double(transitScaleX_), double(transitScaleY_)));
        const double clearance = (1 - .006 * qAbs(tilt)) / maxAxisScale;
        painter.scale(clearance * transitScaleX_, clearance * transitScaleY_ * (1 - pulse * .035));
        painter.translate(-pivot);
    }
    if (!actionFrame_.isNull()) {
        const auto moving = actionMovie_ ? actionMovie_->currentPixmap() : QPixmap{};
        const auto frame = moving.isNull() ? actionFrame_ : moving;
        const auto fit = frame.size().scaled(size(), Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
        painter.fillRect(rect(), Qt::transparent);
        painter.drawPixmap(QRect((width() - fit.width()) / 2, height() - fit.height(), fit.width(), fit.height()), frame);
        return;
    }
    const int frameIndex = movie_ ? movie_->currentFrameNumber() : -1;
    const qreal dpr = qMax(1.0, devicePixelRatioF());
    const QSize targetPixels = (QSizeF(size()) * dpr).toSize().expandedTo(QSize(1, 1));
    if (cachedIdleScaled_.isNull() || cachedIdleFrameNumber_ != frameIndex || cachedIdlePixelSize_ != targetPixels) {
        const QPixmap current = movie_ ? movie_->currentPixmap() : firstFrame_;
        const QPixmap& source = current.isNull() ? firstFrame_ : current;
        if (!source.isNull()) {
            if (source.size() == targetPixels) {
                cachedIdleScaled_ = source;
            } else {
                cachedIdleScaled_ = source.scaled(targetPixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            }
            cachedIdleScaled_.setDevicePixelRatio(dpr);
            cachedIdleFrameNumber_ = frameIndex;
            cachedIdlePixelSize_ = targetPixels;
        }
    }
    if (!cachedIdleScaled_.isNull()) {
        painter.drawPixmap(rect(), cachedIdleScaled_);
    }
}

void PetWindow::playAction(PetAsset asset, int stillDurationMs)
{
    clearAction();
    actionFrame_ = std::move(asset.firstFrame);
    if (movie_ && movie_->state() == QMovie::Running) {
        movie_->setPaused(true);
    }
    if (asset.movie && asset.movie->frameCount() != 1) {
        actionMovie_ = std::move(asset.movie);
        const QPointer<QMovie> source = actionMovie_.get();
        const auto finish = [this, source] {
            QTimer::singleShot(0, this, [this, source] {
                if (source && actionMovie_.get() == source.data()) {
                    returnToIdle();
                }
            });
        };
        connect(actionMovie_.get(), &QMovie::frameChanged, this, [this, finish](int frame) {
            if (frame == 0 && precedingActionFrame_ > 0) {
                finish();
            }
            precedingActionFrame_ = frame;
            update();
        });
        connect(actionMovie_.get(), &QMovie::finished, this, finish);
        connect(actionMovie_.get(), &QMovie::error, this, finish);
    } else {
        holdRemaining_ = qMax(1, stillDurationMs);
    }
    if (isVisible()) {
        resumeAction();
    }
    update();
}

void PetWindow::resumeAction()
{
    if (actionMovie_) {
        if (actionMovie_->state() == QMovie::Paused) {
            actionMovie_->setPaused(false);
        } else if (actionMovie_->state() == QMovie::NotRunning) {
            actionMovie_->start();
        }
    } else if (holdRemaining_ > 0) {
        actionHold_.start(holdRemaining_);
    }
}

void PetWindow::clearAction()
{
    actionHold_.stop();
    actionMovie_.reset();
    actionFrame_ = {};
    holdRemaining_ = 0;
    precedingActionFrame_ = -1;
}

void PetWindow::returnToIdle()
{
    clearAction();
    if (isVisible()) {
        resumeAnimation();
    }
    update();
}

void PetWindow::resumeAnimation()
{
    if (!movie_) {
        return;
    }
    if (!movieStarted_) {
        movie_->start();
        movieStarted_ = true;
    } else if (movie_->state() == QMovie::Paused) {
        movie_->setPaused(false);
    }
}

void PetWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (actionFrame_.isNull()) {
        resumeAnimation();
    } else {
        resumeAction();
    }
}

void PetWindow::hideEvent(QHideEvent* event)
{
    const bool wasDragging = dragMoved_;
    dragMoved_ = false;
    suppressClickRelease_ = false;
    transitLean_ = 0;
    transitScaleX_ = 1.0;
    transitScaleY_ = 1.0;
    bounceAnimation_.stop();
    bounceProgress_ = 1;
    if (wasDragging && dragStateChanged) {
        dragStateChanged(false);
    }
    if (movie_ && movie_->state() == QMovie::Running) {
        movie_->setPaused(true);
    }
    if (actionMovie_ && actionMovie_->state() == QMovie::Running) {
        actionMovie_->setPaused(true);
    }
    if (actionHold_.isActive()) {
        holdRemaining_ = qMax(1, actionHold_.remainingTime());
        actionHold_.stop();
    }
    QWidget::hideEvent(event);
}

void PetWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        dragOffset_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
        dragMoved_ = false;
        suppressClickRelease_ = false;
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void PetWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons().testFlag(Qt::LeftButton)) {
        const QPoint target = event->globalPosition().toPoint() - dragOffset_;
        if (dragMoved_ || (target - pos()).manhattanLength() >= QApplication::startDragDistance()) {
            const bool started = !dragMoved_;
            dragMoved_ = true;
            if (started && dragStateChanged) {
                dragStateChanged(true);
            }
            if (primaryOnly_) {
                placeAt(target);
            } else {
                move(target);
            }
        }
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void PetWindow::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && doubleClickRequested) {
        const bool wasDragging = dragMoved_;
        suppressClickRelease_ = true;
        dragMoved_ = false;
        if (wasDragging && dragStateChanged) {
            dragStateChanged(false);
        }
        dragOffset_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
        doubleClickRequested();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void PetWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        if (dragMoved_) {
            dragMoved_ = false;
            if (dragStateChanged) {
                dragStateChanged(false);
            }
            placeAt(pos(), true);
            if (layoutChanged) {
                layoutChanged();
            }
        } else if (!suppressClickRelease_ && clickRequested) {
            clickRequested();
        }
        suppressClickRelease_ = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void PetWindow::dragEnterEvent(QDragEnterEvent* event)
{
    const auto urls = event->mimeData()->urls();
    if (urls.size() == 1 && urls.constFirst().isLocalFile()
        && event->possibleActions().testFlag(Qt::CopyAction)) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    }
}

void PetWindow::dropEvent(QDropEvent* event)
{
    const auto urls = event->mimeData()->urls();
    if (importFile && urls.size() == 1 && urls.constFirst().isLocalFile()
        && event->possibleActions().testFlag(Qt::CopyAction)) {
        const QString path = urls.constFirst().toLocalFile();
        event->setDropAction(Qt::CopyAction);
        event->accept();
        // End the native drag before opening the import dialog.
        QTimer::singleShot(0, this, [this, path] { importFile(path); });
    }
}
