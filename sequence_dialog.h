#pragma once

#include "role_actions.h"

class QWidget;

std::optional<ActionAsset> importFrameSequence(ActionSlot slot, QWidget* parent);
