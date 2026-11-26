#include "pet_motion_rules.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double pi = 3.14159265358979323846;
constexpr double softMargin = 36;
constexpr double maxAcceleration = 620;
constexpr double smoothing = 0.18;

double length(QPointF point) { return std::hypot(point.x(), point.y()); }

QPointF unit(QPointF point)
{
    const double magnitude = length(point);
    return magnitude > 0.001 ? point / magnitude : QPointF{};
}

QPointF limit(QPointF point, double cap)
{
    const double magnitude = length(point);
    return magnitude > cap ? point * (cap / magnitude) : point;
}

QPointF inBounds(QPointF point, const QRectF& box)
{
    return {qBound(box.left(), point.x(), box.right()), qBound(box.top(), point.y(), box.bottom())};
}

QRectF inset(const QRectF& box, double margin)
{
    const double x = qMin(margin, box.width() * 0.25);
    const double y = qMin(margin, box.height() * 0.25);
    return box.adjusted(x, y, -x, -y);
}

double edgeDistance(QPointF point, const QRectF& box)
{
    return std::min({point.x() - box.left(), box.right() - point.x(),
                     point.y() - box.top(), box.bottom() - point.y()});
}

bool resting(MotionState state)
{
    return state == MotionState::Idle || state == MotionState::Observe || state == MotionState::Rest;
}

QPointF centerOffset(const MotionSpace& space)
{
    return {space.petSize.width() * 0.5, space.petSize.height() * 0.5};
}

double movementScale(const MotionSpace& space)
{
    return qBound(.75, space.petSize.width() / 265, 1.25);
}

double pointerClearance(const MotionSpace& space, QPointF position)
{
    if (!space.cursor) {
        return 1e6;
    }
    const QRectF pet(position, space.petSize);
    return length(*space.cursor - inBounds(*space.cursor, pet));
}

double followDistance(const MotionSpace& space)
{
    return qMax(160.0, qMax(space.petSize.width(), space.petSize.height()) * 0.5 + 40);
}

QPointF arrive(QPointF offset, double speed, double stopRadius)
{
    const double remaining = qMax(0.0, length(offset) - stopRadius * 0.5);
    return unit(offset) * (speed * qMin(1.0, remaining / 60.0));
}

double softLimit(double speed, double negativeRoom, double positiveRoom)
{
    const double room = speed < 0 ? negativeRoom : positiveRoom;
    return speed * qBound(0.0, room / softMargin, 1.0);
}
}

PetMotionRules::PetMotionRules(QRandomGenerator& random) : random_(random)
{
}

double PetMotionRules::range(double low, double high)
{
    return low + (high - low) * random_.generateDouble();
}

void PetMotionRules::resumeAt(QPointF position, double idleSeconds)
{
    position_ = lastTravelPoint_ = position;
    if (!hasAnchor_) {
        anchor_ = position;
        hasAnchor_ = true;
    }
    velocity_ = {};
    previousCursor_.reset();
    mouseVelocity_ = {};
    startledUntil_ = 0;
    state_ = MotionState::Idle;
    movingFor_ = 0;
    lastTravel_ = now_;
    until_ = decideAt_ = now_ + idleSeconds;
    if (restAt_ == 0) {
        restAt_ = now_ + range(24, 42);
    }
}

void PetMotionRules::interacted(bool dragged)
{
    curiosity_ = qMin(1.0, curiosity_ + 0.4);
    restAt_ = now_ + range(24, 42);
    if (dragged) {
        anchor_ = position_;
        hasAnchor_ = true;
        dragUntil_ = now_ + 3;
    }
}

void PetMotionRules::setDestination(QPointF position, QPointF destination)
{
    position_ = position;
    anchor_ = destination;
    hasAnchor_ = true;
    velocity_ = {};
    destination_ = destination;
}

void PetMotionRules::cancelDestination()
{
    destination_.reset();
}

void PetMotionRules::recoverActivity(double seconds)
{
    adjustActivity(qBound(0.0, seconds, .30) * profile_.recovery);
}

void PetMotionRules::setKibble(Kibble kibble)
{
    switch (kibble) {
    case Kibble::High: setRhythm(EffectiveRhythm::Play); break;
    case Kibble::Slow: setRhythm(EffectiveRhythm::Focus); break;
    case Kibble::None: setRhythm(EffectiveRhythm::Daily); break;
    }
}

void PetMotionRules::setRhythm(EffectiveRhythm rhythm)
{
    if (rhythm_ == rhythm) {
        return;
    }
    rhythm_ = rhythm;
    profile_ = rhythmProfile(rhythm);
    if (profile_.anchorRadius > 0) {
        anchor_ = position_;
        hasAnchor_ = true;
        if (!resting(state_)) {
            state_ = MotionState::Idle;
            velocity_ = {};
            until_ = decideAt_ = now_ + range(profile_.idleMin * 0.5, profile_.idleMax * 0.6);
        }
    }
}

void PetMotionRules::setAnchor(QPointF anchor)
{
    anchor_ = anchor;
    hasAnchor_ = true;
}

void PetMotionRules::setActivity(double value)
{
    activity_ = qBound(0.0, value, 100.0);
}

double PetMotionRules::adjustActivity(double change)
{
    const double before = activity_;
    activity_ = qBound(0.0, before + change, 100.0);
    return activity_ - before;
}

void PetMotionRules::sampleCursor(const MotionSpace& space, double dt)
{
    if (space.cursor && previousCursor_) {
        const auto instantaneous = limit((*space.cursor - *previousCursor_) / dt, 1400);
        mouseVelocity_ += (instantaneous - mouseVelocity_) * (1 - std::exp(-dt / 0.10));
        if (profile_.allowFlee) {
            const auto towardPet = unit(position_ + centerOffset(space) - *space.cursor);
            const double approaching = QPointF::dotProduct(mouseVelocity_, towardPet);
            const double clearance = pointerClearance(space, position_);
            if (clearance >= 24 && clearance < 115 && approaching > 310) {
                startledUntil_ = now_ + 0.85;
            }
        } else {
            startledUntil_ = 0;
        }
    } else {
        mouseVelocity_ = {};
        startledUntil_ = 0;
    }
    previousCursor_ = space.cursor;
}

QPointF PetMotionRules::explorationTarget(const QRectF& bounds, bool wide)
{
    QRectF safe = inset(bounds, 32);
    if (profile_.anchorRadius > 0 && hasAnchor_) {
        const double r = profile_.anchorRadius;
        const QRectF anchorBox(anchor_.x() - r, anchor_.y() - r, r * 2.0, r * 2.0);
        const QRectF clamped = safe.intersected(anchorBox);
        if (clamped.width() >= 12.0 && clamped.height() >= 12.0) {
            safe = clamped;
        }
    }
    const auto previous = target_;
    QPointF best = safe.center();
    double bestScore = -1e9;
    for (int i = 0; i < 14; ++i) {
        const double angle = range(0, 2 * pi);
        const double distance = wide ? range(280, 580)
                                     : range(profile_.exploreMinDist, profile_.exploreMaxDist);
        const auto candidate = inBounds(position_ + QPointF(std::cos(angle), std::sin(angle)) * distance, safe);
        const double traveled = length(candidate - position_);
        const double score = -std::abs(traveled - distance) + qMin(110.0, length(candidate - previous)) * 0.45;
        if (score > bestScore) {
            bestScore = score;
            best = candidate;
        }
    }
    return best;
}

QPointF PetMotionRules::edgeTarget(const QRectF& bounds)
{
    const QRectF safe = inset(bounds, 26);
    const std::array<double, 4> distances{position_.x() - bounds.left(), bounds.right() - position_.x(),
                                          position_.y() - bounds.top(), bounds.bottom() - position_.y()};
    const auto edge = std::min_element(distances.begin(), distances.end()) - distances.begin();
    const bool vertical = edge < 2;
    const double along = vertical ? position_.y() : position_.x();
    const double low = vertical ? safe.top() : safe.left();
    const double high = vertical ? safe.bottom() : safe.right();
    const double step = range(85, 180) * (high - along > along - low ? 1 : -1);
    return inBounds(vertical ? QPointF(edge == 0 ? safe.left() : safe.right(), along + step)
                             : QPointF(along + step, edge == 2 ? safe.top() : safe.bottom()), safe);
}

void PetMotionRules::enter(MotionState next, const MotionSpace& space)
{
    if (next != state_) {
        cooldown_[static_cast<int>(state_)] = now_ + range(3, 8);
    }
    state_ = next;
    switch (state_) {
    case MotionState::Idle:
        until_ = now_ + range(profile_.idleMin, profile_.idleMax);
        break;
    case MotionState::Observe:
        until_ = now_ + range(0.8, 1.5 + (rhythm_ == EffectiveRhythm::Focus ? 2.5 : 0.0));
        break;
    case MotionState::Rest:
        until_ = now_ + range(profile_.restMin, profile_.restMax);
        restAt_ = now_ + range(24, 42);
        break;
    case MotionState::Wander:
        until_ = now_ + range(rhythm_ == EffectiveRhythm::Focus ? 1.4 : 2.0,
                              rhythm_ == EffectiveRhythm::Focus ? 3.2 : 5.8);
        turnAt_ = now_ + range(0.3, 0.8);
        wanderSpeed_ = range(38, 92) * profile_.speedScale;
        wanderAngle_ = length(velocity_) > 15 ? std::atan2(velocity_.y(), velocity_.x()) : range(-pi, pi);
        break;
    case MotionState::Explore: {
        until_ = now_ + range(rhythm_ == EffectiveRhythm::Focus ? 2.4 : 3.8,
                              rhythm_ == EffectiveRhythm::Focus ? 4.8 : 7.6);
        const bool wide = profile_.wideChance > 0 && now_ >= wideAt_ && random_.generateDouble() < profile_.wideChance;
        if (wide) {
            wideAt_ = now_ + range(30, 60);
        }
        target_ = explorationTarget(space.bounds, wide);
        curiosity_ *= .45;
        break;
    }
    case MotionState::Follow:
        until_ = now_ + range(1.6, 3.4);
        target_ = position_;
        break;
    case MotionState::Flee:
        until_ = now_ + range(1.0, 1.7);
        startledUntil_ = dragUntil_ = 0;
        target_ = inBounds(position_ + unit(position_ + centerOffset(space) - *space.cursor) * 125,
                           inset(space.bounds, 26));
        break;
    case MotionState::EdgePlay:
        until_ = now_ + range(2, 4);
        target_ = edgeTarget(space.bounds);
        break;
    }
}

void PetMotionRules::decide(const MotionSpace& space)
{
    decideAt_ = now_ + range(.55, 1.2);
    const double clearance = pointerClearance(space, position_);
    const double distance = space.cursor ? length(*space.cursor - position_ - centerOffset(space)) : 1e6;
    const bool flee = profile_.allowFlee && space.cursor && clearance >= 24 && clearance < 115
        && (now_ < startledUntil_ || (now_ < dragUntil_ && length(mouseVelocity_) > 180));
    if (flee && state_ != MotionState::Rest && state_ != MotionState::Observe && state_ != MotionState::Flee
        && now_ >= cooldown_[static_cast<int>(MotionState::Flee)]) {
        enter(MotionState::Flee, space);
        return;
    }
    if (now_ < until_) {
        return;
    }
    if (clearance < 24) {
        enter(MotionState::Idle, space);
        return;
    }
    if (activity_ < 22 || (resting(state_) && now_ >= restAt_)) {
        enter(MotionState::Rest, space);
        return;
    }
    if (now_ - lastTravel_ >= profile_.maxStillSeconds && resting(state_)) {
        enter(profile_.anchorRadius > 0 ? MotionState::Explore : MotionState::Wander, space);
        return;
    }

    const double activity = activity_ / 100.0;
    const bool nearEdge = profile_.allowEdgePlay && edgeDistance(position_, space.bounds) < 90;
    const bool follow = profile_.allowFollow && distance >= followDistance(space) + 26
        && distance <= followDistance(space) + 190
        && clearance >= 34 && length(mouseVelocity_) < 115;
    const double bias = profile_.moveWeightBias;
    struct Choice { MotionState state; double score; };
    std::array<Choice, 6> choices{{
        {MotionState::Idle, 1.70 - bias * 0.65},
        {MotionState::Wander, .68 + activity * .95 + bias},
        {MotionState::Explore, .30 + activity * .80 + curiosity_ * .85 + bias * 0.9 + range(0, .22)},
        {MotionState::Follow, follow ? .62 + curiosity_ * .65 + bias * 0.5 + range(0, .65) : -100.0},
        {MotionState::Rest, 1.25 - activity * 1.40 - bias * 0.8},
        {MotionState::EdgePlay, nearEdge ? .23 + activity * .75 + bias * 0.6 + range(0, .45) : -100.0},
    }};
    for (auto& choice : choices) {
        if (choice.state == state_) {
            choice.score -= .50;
        }
        if (!resting(choice.state) && cooldown_[static_cast<int>(choice.state)] > now_) {
            choice.score -= 100;
        }
        choice.score += range(0, .16);
    }
    std::partial_sort(choices.begin(), choices.begin() + 2, choices.end(),
                      [](const Choice& a, const Choice& b) { return a.score > b.score; });
    const double keepFirst = 1 / (1 + std::exp((choices[1].score - choices[0].score) * 4));
    enter(random_.generateDouble() < keepFirst ? choices[0].state : choices[1].state, space);
}

QPointF PetMotionRules::desiredVelocity(const MotionSpace& space)
{
    const double scale = movementScale(space);
    // Activity naturally modulates briskness (0.78x at low energy -> 1.12x at full energy)
    const double energyFactor = 0.78 + 0.34 * (activity_ / 100.0);
    const double pace = scale * profile_.speedScale * energyFactor;
    switch (state_) {
    case MotionState::Wander: {
        if (now_ >= turnAt_) {
            wanderAngle_ += range(-36, 36) * pi / 180;
            turnAt_ = now_ + range(.3, .8);
        }
        const QPointF direction(std::cos(wanderAngle_), std::sin(wanderAngle_));
        QPointF push = inBounds(position_, inset(space.bounds, 42)) - position_;
        if (profile_.anchorRadius > 0 && hasAnchor_) {
            const QPointF toAnchor = anchor_ - position_;
            if (length(toAnchor) > profile_.anchorRadius * 0.80) {
                push += toAnchor * 0.9;
            }
        }
        const auto steered = unit(direction + push / 31.0);
        return steered * (wanderSpeed_ * scale * energyFactor * (1 + .17 * std::sin(now_ * 2.1)));
    }
    case MotionState::Explore: return arrive(target_ - position_, 122 * pace, 22);
    case MotionState::EdgePlay: return arrive(target_ - position_, 112 * pace, 16);
    case MotionState::Flee: return arrive(target_ - position_, 148 * scale, 16);
    case MotionState::Follow: {
        if (!space.cursor) {
            return {};
        }
        const auto offset = position_ + centerOffset(space) - *space.cursor;
        const double distance = length(offset);
        const double preferred = followDistance(space);
        const auto offsetTarget = inBounds(*space.cursor + unit(offset) * preferred - centerOffset(space),
                                           inset(space.bounds, 24));
        if (distance < preferred + 20 || length(mouseVelocity_) > 245) {
            return {};
        }
        if (length(offsetTarget - target_) > 20) {
            target_ = offsetTarget;
        }
        return arrive(target_ - position_, 87 * pace, 20);
    }
    default: return {};
    }
}

void PetMotionRules::steer(QPointF desired, double acceleration, double dt)
{
    velocity_ += limit((desired - velocity_) * (1 - std::exp(-dt / smoothing)), acceleration * dt);
}

void PetMotionRules::advanceToDestination(const MotionSpace& space, double dt)
{
    const auto target = inBounds(*destination_, space.bounds);
    const auto offset = target - position_;
    const double distance = length(offset);
    if (distance <= 1) {
        position_ = target;
        velocity_ = {};
        cancelDestination();
        return;
    }

    const double activity = activity_ / 100;
    const double speed = (105 + 150 * activity) * movementScale(space);
    const double acceleration = 310 + 290 * activity;
    const auto direction = offset / distance;
    // Allow for steering lag as well as braking distance.
    const double deceleration = acceleration * .62;
    const double steeringLag = deceleration * .30;
    const double brakingSpeed = std::sqrt(steeringLag * steeringLag + 2 * deceleration * distance) - steeringLag;
    steer(direction * qMin(speed, brakingSpeed), acceleration, dt);
    adjustActivity(-dt * profile_.consumption);

    const auto step = velocity_ * dt;
    if (QPointF::dotProduct(step, direction) >= distance) {
        position_ = target;
        velocity_ = {};
        cancelDestination();
    } else {
        position_ = inBounds(position_ + step, space.bounds);
    }
}

void PetMotionRules::advance(double seconds, const MotionSpace& space)
{
    const double dt = qBound(0.0, seconds, .05);
    if (dt == 0 || space.bounds.width() < 0 || space.bounds.height() < 0) {
        return;
    }
    now_ += dt;
    position_ = inBounds(position_, space.bounds);
    if (hasDestination()) {
        advanceToDestination(space, dt);
        return;
    }
    sampleCursor(space, dt);
    const bool still = resting(state_);
    adjustActivity(dt * (still ? profile_.recovery : -profile_.consumption));
    curiosity_ = qMax(0.0, curiosity_ - .035 * dt);

    const double clearance = pointerClearance(space, position_);
    if (!still && clearance < 22) {
        enter(MotionState::Observe, space);
        velocity_ = {};
    } else if (!still && (movingFor_ >= (rhythm_ == EffectiveRhythm::Focus ? 4.2 : 7.3) || activity_ < 14)) {
        enter(MotionState::Observe, space);
    } else if ((!still && now_ >= until_)
        || ((state_ == MotionState::Explore || state_ == MotionState::EdgePlay || state_ == MotionState::Flee)
            && length(target_ - position_) < 20)
        || (state_ == MotionState::Follow && (!space.cursor
            || length(position_ + centerOffset(space) - *space.cursor) > followDistance(space) + 230))) {
        if (random_.generateDouble() < .30) {
            enter(MotionState::Observe, space);
        } else {
            until_ = now_;
            decide(space);
        }
    } else if (now_ >= decideAt_) {
        decide(space);
    }

    auto desired = desiredVelocity(space);
    desired.setX(softLimit(desired.x(), position_.x() - space.bounds.left(), space.bounds.right() - position_.x()));
    desired.setY(softLimit(desired.y(), position_.y() - space.bounds.top(), space.bounds.bottom() - position_.y()));
    steer(desired, maxAcceleration, dt);
    if (desired.isNull() && length(velocity_) < 1.5) {
        velocity_ = {};
    }
    const auto next = position_ + velocity_ * dt;
    position_ = inBounds(next, space.bounds);
    if (next.x() != position_.x()) velocity_.setX(0);
    if (next.y() != position_.y()) velocity_.setY(0);
    if (resting(state_) && length(velocity_) < 2) {
        movingFor_ = 0;
    } else {
        movingFor_ += dt;
    }
    if (length(position_ - lastTravelPoint_) > 45) {
        lastTravelPoint_ = position_;
        lastTravel_ = now_;
    }
}
