#pragma once

#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QtGlobal>

#include <vector>

class QRandomGenerator;

enum class SpaceTransitMode {
    MixedRandom, // 随机组合（跳动与正常移动）
    JumpOnly,    // 仅跳动进入
    WalkOnly,    // 仅正常移动
};

enum class SpaceMotionStyle {
    Jump,
    Walk,
};

enum class SpaceEntrySide {
    FromLeft,
    FromRight,
};

struct SpaceTransitInput {
    QRect workArea;
    QSize petSize;
    QPoint previousPos;
    int spaceDelta = 1;
    double activity = 78.0;
    SpaceMotionStyle style = SpaceMotionStyle::Jump;
};

struct SpaceTransitPlan {
    int inputIndex = 0;
    SpaceEntrySide side = SpaceEntrySide::FromLeft;
    SpaceMotionStyle style = SpaceMotionStyle::Jump;
    QPointF startPos;
    QPointF peekPos;
    QPointF landingPos;
    double delaySeconds = 0.05;
    double durationSeconds = 0.68;
    double hopAmplitude = 12.0;
    double overshootPx = 10.0;
    int hopCount = 2;
};

struct SpaceTransitSample {
    QPointF position;
    qreal leanDegrees = 0.0;
    qreal scaleX = 1.0;
    qreal scaleY = 1.0;
    bool waitingDelay = false;
    bool finished = false;
};

SpaceMotionStyle resolveSpaceMotionStyle(SpaceTransitMode mode, QRandomGenerator& rng);
std::vector<SpaceTransitPlan> planSpaceTransits(const std::vector<SpaceTransitInput>& inputs);
SpaceTransitPlan planSpaceTransit(const SpaceTransitInput& input);
SpaceTransitSample sampleSpaceTransit(const SpaceTransitPlan& plan, double elapsedSeconds);
