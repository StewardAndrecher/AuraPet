#pragma once

#include <QPointF>
#include <QRandomGenerator>
#include <QRectF>
#include <QSizeF>

#include <array>
#include <optional>

enum class MotionState { Idle, Wander, Explore, Follow, Flee, Rest, EdgePlay, Observe };
enum class Kibble { None, Slow, High };

struct ActivityRates {
    double recovery = 5.5;
    double consumption = 5;
};

constexpr ActivityRates kibbleRates(Kibble kibble)
{
    switch (kibble) {
    case Kibble::Slow: return {3.4, 6.8};
    case Kibble::High: return {8.5, 3.0};
    case Kibble::None: return {};
    }
    return {};
}

struct MotionSpace {
    QRectF bounds;  // Legal top-left positions for the pet window.
    QSizeF petSize;
    std::optional<QPointF> cursor;
};

class PetMotionRules {
public:
    explicit PetMotionRules(QRandomGenerator& random);
    void resumeAt(QPointF position, double idleSeconds = 0.8);
    void interacted(bool dragged);
    void setDestination(QPointF position, QPointF destination);
    void cancelDestination();
    bool hasDestination() const { return destination_.has_value(); }
    void setKibble(Kibble kibble);
    double adjustActivity(double change);
    void recoverActivity(double seconds);
    void advance(double seconds, const MotionSpace& space);

    QPointF position() const { return position_; }
    QPointF velocity() const { return velocity_; }
    MotionState state() const { return state_; }
    double activity() const { return activity_; }

private:
    double range(double low, double high);
    void sampleCursor(const MotionSpace& space, double dt);
    void decide(const MotionSpace& space);
    void enter(MotionState state, const MotionSpace& space);
    QPointF explorationTarget(const QRectF& bounds, bool wide);
    QPointF edgeTarget(const QRectF& bounds);
    QPointF desiredVelocity(const MotionSpace& space);
    void steer(QPointF desired, double acceleration, double dt);
    void advanceToDestination(const MotionSpace& space, double dt);

    QRandomGenerator& random_;
    MotionState state_ = MotionState::Idle;
    QPointF position_;
    QPointF velocity_;
    QPointF target_;
    std::optional<QPointF> destination_;
    QPointF lastTravelPoint_;
    std::optional<QPointF> previousCursor_;
    QPointF mouseVelocity_;
    std::array<double, 8> cooldown_{};
    double now_ = 0;
    double until_ = 0;
    double decideAt_ = 0;
    double turnAt_ = 0;
    double wideAt_ = 0;
    double restAt_ = 0;
    double lastTravel_ = 0;
    double movingFor_ = 0;
    double startledUntil_ = 0;
    double dragUntil_ = 0;
    double wanderAngle_ = 0;
    double wanderSpeed_ = 0;
    double activity_ = 78;
    ActivityRates activityRates_;
    double curiosity_ = 0;
};
