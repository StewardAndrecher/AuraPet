#pragma once

#include <QMovie>
#include <QPixmap>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

#include <functional>
#include <memory>

struct PetAsset;
class QKeyEvent;
class QScreen;

class PetWindow : public QWidget {
public:
    PetWindow();

    void setAsset(PetAsset asset, int scalePercent = 100);
    void placeAt(const QPoint& position, bool snapToEdge = false);
    void setScalePercent(int scalePercent);
    bool applyZoomKey(const QKeyEvent& event);
    void setPrimaryOnly(bool enabled);
    void moveToDisplay(const QScreen* display);
    int scalePercent() const;
    const QPixmap& previewPixmap() const { return firstFrame_; }
    void setClickThrough(bool enabled);
    bool alwaysOnTop() const { return alwaysOnTop_; }
    void setAlwaysOnTop(bool enabled);
    bool isDragging() const { return dragMoved_; }
    void setAimLean(qreal degrees);
    void setTransitPose(qreal leanDegrees, qreal scaleX, qreal scaleY);
    void resetTransitPose();
    void playBounce();
    void playAction(PetAsset asset, int stillDurationMs);
    void returnToIdle();
    std::function<void(const QString&)> importFile;
    std::function<void()> layoutChanged;
    std::function<void()> clickRequested;
    std::function<void()> doubleClickRequested;
    std::function<void(bool)> dragStateChanged;

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void placeOnDisplay(const QPoint& position, const QScreen* display, bool snapToEdge);
    void resumeAnimation();
    void resumeAction();
    void clearAction();

    QPixmap firstFrame_;
    QPixmap cachedIdleScaled_;
    QSize cachedIdlePixelSize_;
    int cachedIdleFrameNumber_ = -2;
    std::unique_ptr<QMovie> movie_;
    QPixmap actionFrame_;
    std::unique_ptr<QMovie> actionMovie_;
    QTimer actionHold_;
    QVariantAnimation bounceAnimation_;
    int holdRemaining_ = 0;
    int precedingActionFrame_ = -1;
    QSize baseSize_;
    int scalePercent_ = 100;
    bool primaryOnly_ = false;
    bool alwaysOnTop_ = true;
    bool movieStarted_ = false;
    bool dragMoved_ = false;
    bool suppressClickRelease_ = false;
    qreal aimLean_ = 0;
    qreal transitLean_ = 0;
    qreal transitScaleX_ = 1.0;
    qreal transitScaleY_ = 1.0;
    double bounceProgress_ = 1;
    QPoint dragOffset_;
};
