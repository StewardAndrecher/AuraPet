#pragma once

#include "role_actions.h"

class QWidget;

std::optional<ActionAsset> createTemplateAction(ActionSlot slot, const QImage& idleFrame, QWidget* parent,
                                                const QJsonObject& recipe = {});
