#include "space_transit.h"

#include <QRandomGenerator>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace {
constexpr double pi = 3.14159265358979323846;

double smoothstep(double edge0, double edge1, double x)
{
    const double t = qBound(0.0, (x - edge0) / qMax(1e-6, edge1 - edge0), 1.0);
    return t * t * (3.0 - 2.0 * t);
}

SpaceEntrySide resolveEntrySide(const SpaceTransitInput& input)
{
    return input.spaceDelta >= 0 ? SpaceEntrySide::FromLeft : SpaceEntrySide::FromRight;
}

double resolveNearCollisionY(double preferredY, const std::vector<double>& placedY, double minGapY, double minY, double maxY)
{
    if (maxY <= minY + 1.0) {
        return minY;
    }
    auto conflicts = [&](double y) {
        for (const double existing : placedY) {
            if (std::abs(y - existing) < minGapY) {
                return true;
            }
        }
        return false;
    };
    const double clampedPreferred = qBound(minY, preferredY, maxY);
    if (!conflicts(clampedPreferred)) {
        return clampedPreferred;
    }
    const double step = qMax(8.0, minGapY * 0.55);
    const int maxSteps = static_cast<int>((maxY - minY) / step) + 2;
    for (int k = 1; k <= maxSteps; ++k) {
        for (const int sign : {1, -1}) {
            const double candidate = clampedPreferred + sign * k * step;
            if (candidate >= minY && candidate <= maxY && !conflicts(candidate)) {
                return candidate;
            }
        }
    }
    return clampedPreferred;
}
}

std::vector<SpaceTransitPlan> planSpaceTransits(const std::vector<SpaceTransitInput>& inputs)
{
    std::vector<SpaceTransitPlan> plans(inputs.size());
    if (inputs.empty()) {
        return plans;
    }

    const int n = static_cast<int>(inputs.size());
    std::vector<int> order(static_cast<size_t>(n));
    std::iota(order.begin(), order.end(), 0);

    const SpaceEntrySide primarySide = resolveEntrySide(inputs.front());
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const auto& ia = inputs[static_cast<size_t>(a)];
        const auto& ib = inputs[static_cast<size_t>(b)];
        if (primarySide == SpaceEntrySide::FromLeft) {
            if (ia.previousPos.x() != ib.previousPos.x()) {
                return ia.previousPos.x() > ib.previousPos.x();
            }
        } else {
            if (ia.previousPos.x() != ib.previousPos.x()) {
                return ia.previousPos.x() < ib.previousPos.x();
            }
        }
        return a < b;
    });

    std::vector<double> placedY;
    placedY.reserve(inputs.size());

    for (int rank = 0; rank < n; ++rank) {
        const int idx = order[static_cast<size_t>(rank)];
        const SpaceTransitInput& in = inputs[static_cast<size_t>(idx)];
        const QSize petSize = in.petSize.expandedTo(QSize(1, 1));
        const QRect workArea = in.workArea.isValid()
            ? in.workArea
            : QRect(in.previousPos, petSize);

        const int minX = workArea.left();
        const int maxX = qMax(minX, workArea.right() - petSize.width() + 1);
        const int minY = workArea.top();
        const int maxY = qMax(minY, workArea.bottom() - petSize.height() + 1);

        const SpaceEntrySide side = resolveEntrySide(in);
        const double baseMarginX = qBound(28.0, petSize.width() * 0.28 + 26.0, 92.0);
        const double staggerX = (rank % 2) * qMin(32.0, petSize.width() * 0.26);
        const double maxAllowedOffset = (maxX > minX)
            ? qMax(36.0, qMin(200.0, (maxX - minX) * 0.28))
            : 0.0;
        const double offset = (maxX > minX) ? qMin(baseMarginX + staggerX, maxAllowedOffset) : 0.0;
        const double landX = (side == SpaceEntrySide::FromLeft)
            ? qBound(double(minX), minX + offset, double(maxX))
            : qBound(double(minX), maxX - offset, double(maxX));

        const double preferredY = (maxY - minY >= 36)
            ? qBound(double(minY + 18), double(in.previousPos.y()), double(maxY - 18))
            : double(minY + (maxY - minY) / 2.0);
        const double minGapY = (n > 1)
            ? qMin(double(petSize.height() + 16), double(qMax(1, maxY - minY)) / qMax(1, n))
            : 0.0;
        const double landY = (n > 1)
            ? resolveNearCollisionY(preferredY, placedY, minGapY, double(minY), double(maxY))
            : preferredY;
        placedY.push_back(landY);

        const double startX = (side == SpaceEntrySide::FromLeft)
            ? (workArea.left() - petSize.width() * 0.72)
            : (workArea.right() + 1.0 - petSize.width() * 0.28);
        const double peekX = (side == SpaceEntrySide::FromLeft)
            ? (workArea.left() - petSize.width() * 0.32)
            : (workArea.right() + 1.0 - petSize.width() * 0.68);

        const double energy = qBound(0.0, in.activity / 100.0, 1.0);
        const int steps = qBound(1, std::abs(in.spaceDelta == 0 ? 1 : in.spaceDelta), 4);

        SpaceTransitPlan plan;
        plan.inputIndex = idx;
        plan.side = side;
        plan.style = in.style;
        plan.landingPos = QPointF(landX, landY);
        plan.delaySeconds = 0.05 + rank * 0.09;
        if (in.style == SpaceMotionStyle::Walk) {
            plan.startPos = QPointF(startX, landY);
            plan.peekPos = QPointF(peekX, landY);
            plan.durationSeconds = (0.68 - 0.14 * energy) * (steps >= 2 ? 0.90 : 1.0);
            plan.hopCount = 0;
            plan.hopAmplitude = 0.0;
            plan.overshootPx = 0.0;
        } else {
            plan.startPos = QPointF(startX, landY + 4.0);
            plan.peekPos = QPointF(peekX, landY + 2.0);
            plan.durationSeconds = (0.74 - 0.16 * energy) * (steps >= 2 ? 0.90 : 1.0);
            plan.hopCount = (steps >= 2 && energy >= 0.35) ? 3 : 2;
            plan.hopAmplitude = qBound(5.0, petSize.height() * (0.065 + 0.085 * energy) * (steps >= 2 ? 1.18 : 1.0), 22.0);
            plan.overshootPx = qBound(5.0, (7.0 + 7.0 * energy) * (steps >= 2 ? 1.28 : 1.0), 18.0);
        }

        plans[static_cast<size_t>(idx)] = plan;
    }

    return plans;
}

SpaceMotionStyle resolveSpaceMotionStyle(SpaceTransitMode mode, QRandomGenerator& rng)
{
    switch (mode) {
    case SpaceTransitMode::JumpOnly:
        return SpaceMotionStyle::Jump;
    case SpaceTransitMode::WalkOnly:
        return SpaceMotionStyle::Walk;
    case SpaceTransitMode::MixedRandom:
        break;
    }
    return rng.bounded(2) == 0 ? SpaceMotionStyle::Jump : SpaceMotionStyle::Walk;
}

SpaceTransitPlan planSpaceTransit(const SpaceTransitInput& input)
{
    return planSpaceTransits({input}).front();
}

SpaceTransitSample sampleSpaceTransit(const SpaceTransitPlan& plan, double elapsedSeconds)
{
    if (plan.style == SpaceMotionStyle::Walk) {
        if (elapsedSeconds < plan.delaySeconds) {
            return {plan.startPos, 0.0, 1.0, 1.0, true, false};
        }
        const double duration = qMax(0.1, plan.durationSeconds);
        const double u = (elapsedSeconds - plan.delaySeconds) / duration;
        if (u >= 1.0) {
            return {plan.landingPos, 0.0, 1.0, 1.0, false, true};
        }
        const double easeInOut = u * u * (3.0 - 2.0 * u);
        const double easeOut = 1.0 - std::pow(1.0 - u, 2.3);
        const double progress = 0.35 * easeInOut + 0.65 * easeOut;
        const double x = plan.startPos.x() + (plan.landingPos.x() - plan.startPos.x()) * progress;
        return {QPointF(x, plan.landingPos.y()), 0.0, 1.0, 1.0, false, false};
    }

    const double dir = (plan.side == SpaceEntrySide::FromLeft) ? 1.0 : -1.0;
    if (elapsedSeconds < plan.delaySeconds) {
        return {plan.startPos, dir * 3.5, 0.98, 1.02, true, false};
    }

    const double duration = qMax(0.1, plan.durationSeconds);
    const double u = (elapsedSeconds - plan.delaySeconds) / duration;
    if (u >= 1.0) {
        return {plan.landingPos, 0.0, 1.0, 1.0, false, true};
    }

    if (u <= 0.16) {
        const double p = u / 0.16;
        const double s = smoothstep(0.0, 1.0, p);
        const double x = plan.startPos.x() + (plan.peekPos.x() - plan.startPos.x()) * s;
        const double y = plan.startPos.y() + (plan.landingPos.y() - plan.startPos.y()) * s;
        const double lean = dir * (3.5 + 4.5 * std::sin(pi * p));
        const double crouch = 0.04 * std::sin(pi * p);
        return {QPointF(x, y), lean, 1.0 + crouch, 1.0 - crouch, false, false};
    }

    if (u <= 0.78) {
        const double p = (u - 0.16) / 0.62;
        const double ease = 1.0 - std::pow(1.0 - p, 2.15);
        const double overshootTargetX = plan.landingPos.x() + dir * plan.overshootPx;
        const double x = plan.peekPos.x() + (overshootTargetX - plan.peekPos.x()) * ease;

        const int hops = qMax(1, plan.hopCount);
        const double phi = p * hops;
        const int k = qMin(hops - 1, static_cast<int>(std::floor(phi)));
        const double f = phi - k;
        const double damp = qMax(0.35, 1.0 - 0.22 * k);
        const double arch = 4.0 * f * (1.0 - f);
        const double y = plan.landingPos.y() - plan.hopAmplitude * damp * arch;

        const double lean = dir * (8.5 * (1.0 - 0.45 * p) + 1.8 * std::sin(2.0 * pi * phi));
        const double wave = std::cos(2.0 * pi * f);
        return {QPointF(x, y), lean, 1.0 + 0.045 * damp * wave, 1.0 - 0.045 * damp * wave, false, false};
    }

    const double p = (u - 0.78) / 0.22;
    const double spring = (1.0 - p) * (1.0 - p) * std::cos(1.35 * pi * p);
    const double x = plan.landingPos.x() + dir * plan.overshootPx * spring;
    const double cushion = std::sin(pi * p) * (1.0 - p);
    const double y = plan.landingPos.y() + 2.2 * cushion;
    const double lean = -dir * 4.2 * cushion;
    return {QPointF(x, y), lean, 1.0 + 0.05 * cushion, 1.0 - 0.05 * cushion, false, false};
}
