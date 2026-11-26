#pragma once

struct OverlayPresentation {
    bool canOpenInput = false;
    bool showInput = false;
    bool showActivityMeter = false;
    bool allowActivityFlash = false;
};

struct OverlayInteraction {
    enum class Input { Hidden, Visible };
    enum class Speech { Hidden, Generating, Displayed };
    enum class Escape { Ignore, StopReply, StopSpeech, CloseInput, HideBubble };

    // Input & speech state
    Input input = Input::Hidden;
    Speech speech = Speech::Hidden;

    // Orthogonal environmental & gesture facts (Single Source of Truth)
    bool petVisible = true;
    bool clickThrough = false;
    bool dragging = false;
    bool aiming = false;
    bool modalBlocked = false;
    bool activityMeterEnabled = true;

    OverlayPresentation resolve() const
    {
        OverlayPresentation out;
        out.canOpenInput = petVisible && !clickThrough && !dragging && !aiming && !modalBlocked;
        out.showInput = out.canOpenInput && input == Input::Visible;
        out.showActivityMeter = activityMeterEnabled && out.showInput;
        out.allowActivityFlash = activityMeterEnabled && petVisible && !dragging && !aiming;
        return out;
    }

    bool inputVisible() const { return resolve().showInput; }
    bool bubbleVisible() const { return speech != Speech::Hidden; }
    Escape escape(bool composing, bool waitingForReply, bool speaking) const
    {
        if (composing) return Escape::Ignore;
        if (waitingForReply) return Escape::StopReply;
        if (speaking) return Escape::StopSpeech;
        if (inputVisible()) return Escape::CloseInput;
        return bubbleVisible() ? Escape::HideBubble : Escape::Ignore;
    }
};
