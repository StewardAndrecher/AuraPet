#pragma once

#include <QPointF>
#include <QRandomGenerator>
#include <QRectF>
#include <QSizeF>

#include <array>
#include <optional>

enum class MotionState { Idle, Wander, Explore, Follow, Flee, Rest, EdgePlay, Observe };
enum class Kibble { None, Slow, High };
enum class CompanionMode { Auto, Play, Daily, Focus };
enum class EffectiveRhythm { Play, Daily, Focus };
enum class FocusMotionPolicy { GentleMicroMove, CompletelyStill };

struct ActivityRates {
    double recovery = 5.5;
    double consumption = 5;
};

struct RhythmProfile {
    double recovery = 5.5;
    double consumption = 5.0;
    double speedScale = 0.90;
    double idleMin = 2.2;
    double idleMax = 5.5;
    double restMin = 3.5;
    double restMax = 7.0;
    double maxStillSeconds = 26.0;
    double exploreMinDist = 80.0;
    double exploreMaxDist = 250.0;
    double wideChance = 0.14;
    double anchorRadius = 0.0;
    double moveWeightBias = -0.08;
    bool allowFollow = true;
    bool allowFlee = false;
    bool allowEdgePlay = true;
    int proactiveMinSec = 720;
    int proactiveMaxSec = 1200;
    double proactiveMinActivity = 40.0;
    double proactiveActivityCost = 6.0;
};

struct RhythmContext {
    CompanionMode userMode = CompanionMode::Auto;
    bool focusTimerActive = false;
    bool breakTimerActive = false;
    bool doNotDisturb = false;
    bool appActive = false;
    bool recentInteraction = false;
    bool externalFullScreen = false;
};

constexpr EffectiveRhythm resolveEffectiveRhythm(const RhythmContext& ctx)
{
    if (ctx.doNotDisturb || ctx.focusTimerActive) {
        return EffectiveRhythm::Focus;
    }
    if (ctx.breakTimerActive) {
        return EffectiveRhythm::Daily;
    }
    switch (ctx.userMode) {
    case CompanionMode::Play: return EffectiveRhythm::Play;
    case CompanionMode::Daily: return EffectiveRhythm::Daily;
    case CompanionMode::Focus: return EffectiveRhythm::Focus;
    case CompanionMode::Auto:
        if (ctx.externalFullScreen) {
            return EffectiveRhythm::Focus;
        }
        if (ctx.appActive || ctx.recentInteraction) {
            return EffectiveRhythm::Play;
        }
        return EffectiveRhythm::Daily;
    }
    return EffectiveRhythm::Daily;
}

constexpr RhythmProfile rhythmProfile(EffectiveRhythm rhythm)
{
    switch (rhythm) {
    case EffectiveRhythm::Play:
        return {
            8.5, 3.0, 1.15,
            0.9, 2.6, 1.8, 3.4, 11.0,
            120.0, 380.0, 0.35, 0.0, 0.42,
            true, true, true,
            120, 240, 35.0, 8.0
        };
    case EffectiveRhythm::Daily:
        return {
            5.5, 5.0, 0.90,
            2.2, 5.5, 3.5, 7.0, 26.0,
            80.0, 250.0, 0.14, 0.0, -0.08,
            true, false, true,
            720, 1200, 40.0, 6.0
        };
    case EffectiveRhythm::Focus:
        return {
            3.2, 6.8, 0.45,
            8.0, 18.0, 12.0, 24.0, 75.0,
            30.0, 85.0, 0.0, 95.0, -0.75,
            false, false, false,
            0, 0, 100.0, 0.0
        };
    }
    return {};
}

constexpr ActivityRates kibbleRates(Kibble kibble)
{
    switch (kibble) {
    case Kibble::Slow: return {3.2, 6.8};
    case Kibble::High: return {8.5, 3.0};
    case Kibble::None: return {5.5, 5.0};
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
    void setRhythm(EffectiveRhythm rhythm);
    EffectiveRhythm rhythm() const { return rhythm_; }
    void setAnchor(QPointF anchor);
    void setActivity(double value);
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
    QPointF anchor_;
    bool hasAnchor_ = false;
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
    EffectiveRhythm rhythm_ = EffectiveRhythm::Daily;
    RhythmProfile profile_ = rhythmProfile(EffectiveRhythm::Daily);
    double curiosity_ = 0;
};
