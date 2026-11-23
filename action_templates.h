#pragma once

#include "frame_sequence.h"

enum class ActionTemplate { Hop, Sway, Breathe };

inline constexpr int minTemplateDurationMs = 600;
inline constexpr int maxTemplateDurationMs = 4000;

struct ActionTemplateOptions {
    ActionTemplate kind;
    int intensity;
    int durationMs;
};

QString actionTemplateName(ActionTemplate kind);
int actionTemplateFrameCount(int durationMs);
FrameSequenceResult renderActionTemplate(const QImage& idleFrame, const ActionTemplateOptions& options,
                                         const std::function<bool(int done)>& progress);
