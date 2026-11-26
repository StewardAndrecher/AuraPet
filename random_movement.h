#pragma once

#include "pet_motion_rules.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <functional>

class QScreen;
class QWidget;

class RandomMovement : public QObject {
public:
    explicit RandomMovement(QWidget& window, std::function<void(double)> onActivity = {});

    void setEnabled(bool enabled);
    void setSuspended(bool suspended);
    void setQuietMode(bool quiet);
    void setKibble(Kibble kibble);
    void setRhythm(EffectiveRhythm rhythm);
    EffectiveRhythm rhythm() const { return rules_.rhythm(); }
    void setAnchor(const QPoint& anchor);
    void setActivity(double value);
    double adjustActivity(double change);
    double activity() const { return rules_.activity(); }
    void postpone();
    void beginTargeting();
    void cancelTargeting();
    void setSpaceTransitActive(bool active);
    void finishSpaceTransit(const QPoint& landedPos, double idlePauseSeconds = 2.4);
    QPointF landingPoint(const QPointF& requested) const;
    void moveTo(const QPointF& landing);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool canRoam() const;
    void tick();
    void advanceMovement(double elapsed, QScreen& screen, bool directed);

    QWidget& window_;
    PetMotionRules rules_;
    std::function<void(double)> onActivity_;
    QTimer timer_;
    QElapsedTimer clock_;
    QPointer<QScreen> screen_;
    QMetaObject::Connection workAreaChanged_;
    QPoint pressPosition_;
    bool enabled_ = false;
    bool suspended_ = false;
    bool quiet_ = false;
    bool mouseHeld_ = false;
    bool blocked_ = false;
    bool targeting_ = false;
    bool spaceTransit_ = false;
};
