#pragma once

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QRectF>

#include <array>
#include <functional>
#include <optional>

struct PetAppearance {
    QRectF visibleBounds{0, 0, 1, 1};
    std::array<QColor, 3> palette{QColor("#FAF6F0"), QColor("#FDE68A"), QColor("#FBBF24")};
};

std::optional<PetAppearance> analyzePetAppearance(const QImage& firstFrame, const QByteArray& gif,
                                                  const std::function<bool()>& canceled);
