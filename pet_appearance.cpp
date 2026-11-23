#include "pet_appearance.h"

#include <QBuffer>
#include <QImageReader>

#include <algorithm>
#include <array>
#include <cmath>

namespace {
bool extendBounds(const QImage& frame, QRect& bounds, const std::function<bool()>& canceled)
{
    if (!frame.hasAlphaChannel()) {
        bounds = frame.rect();
        return true;
    }
    if (bounds == frame.rect()) {
        return true;
    }
    int left = bounds.isEmpty() ? frame.width() : bounds.left();
    int right = bounds.right();
    int top = bounds.isEmpty() ? frame.height() : bounds.top();
    int bottom = bounds.bottom();
    for (int y = 0; y < frame.height(); ++y) {
        if (y % 64 == 0 && canceled()) {
            return false;
        }
        const auto* row = reinterpret_cast<const QRgb*>(frame.constScanLine(y));
        // Inside the accumulated height, only inspect columns that can extend the outline.
        const bool withinHeight = y >= top && y <= bottom;
        const int leftLimit = withinHeight ? left : frame.width();
        for (int x = 0; x < leftLimit; ++x) {
            if (qAlpha(row[x]) > 0) {
                left = qMin(left, x);
                right = qMax(right, x);
                top = qMin(top, y);
                bottom = qMax(bottom, y);
                break;
            }
        }
        for (int x = frame.width() - 1; x > right; --x) {
            if (qAlpha(row[x]) > 0) {
                left = qMin(left, x);
                right = x;
                top = qMin(top, y);
                bottom = qMax(bottom, y);
                break;
            }
        }
    }
    if (right >= left) {
        bounds = QRect(QPoint(left, top), QPoint(right, bottom));
    }
    return true;
}

struct ColorBucket {
    double weight = 0;
    double red = 0;
    double green = 0;
    double blue = 0;

    QColor average() const
    {
        return {qRound(red / weight), qRound(green / weight), qRound(blue / weight)};
    }
};

using Histogram = std::array<ColorBucket, 512>;

void sampleColors(const QImage& frame, const QRect& bounds, int frameDuration, Histogram& colors)
{
    const int xStep = qMax(1, bounds.width() / 40);
    const int yStep = qMax(1, bounds.height() / 40);
    for (int y = bounds.top() + yStep / 2; y <= bounds.bottom(); y += yStep) {
        const auto* row = reinterpret_cast<const QRgb*>(frame.constScanLine(y));
        for (int x = bounds.left() + xStep / 2; x <= bounds.right(); x += xStep) {
            if (qAlpha(row[x]) < 24) {
                continue;
            }
            const int r = qRed(row[x]);
            const int g = qGreen(row[x]);
            const int b = qBlue(row[x]);
            const int high = qMax(r, qMax(g, b));
            const int low = qMin(r, qMin(g, b));
            // Favor recognizable colors over dark outlines, while retaining a monochrome palette when needed.
            const double weight = frameDuration * (qAlpha(row[x]) / 255.0)
                * (.12 + 1.5 * ((high - low) / 255.0)) * (.08 + .92 * (high / 255.0));
            auto& bucket = colors[((r >> 5) << 6) | ((g >> 5) << 3) | (b >> 5)];
            bucket.weight += weight;
            bucket.red += r * weight;
            bucket.green += g * weight;
            bucket.blue += b * weight;
        }
    }
}

void choosePalette(Histogram& colors, PetAppearance& appearance)
{
    std::stable_sort(colors.begin(), colors.end(), [](const auto& a, const auto& b) { return a.weight > b.weight; });
    qsizetype selected = 0;
    for (const auto& bucket : colors) {
        if (bucket.weight == 0 || selected == static_cast<qsizetype>(appearance.palette.size())) {
            break;
        }
        const auto color = bucket.average();
        const auto end = appearance.palette.begin() + selected;
        const bool distinct = std::none_of(appearance.palette.begin(), end, [&](const QColor& old) {
            return qAbs(old.red() - color.red()) + qAbs(old.green() - color.green())
                + qAbs(old.blue() - color.blue()) < 70;
        });
        if (distinct) {
            appearance.palette[selected++] = color;
        }
    }
    if (selected > 0) {
        std::fill(appearance.palette.begin() + selected, appearance.palette.end(), appearance.palette[selected - 1]);
    }
}
}

std::optional<PetAppearance> analyzePetAppearance(const QImage& firstFrame, const QByteArray& gif,
                                                  const std::function<bool()>& canceled)
{
    QRect bounds;
    Histogram colors{};
    const auto collect = [&](const QImage& decoded, int delay) {
        if (canceled()) {
            return false;
        }
        const QImage frame = decoded.convertToFormat(decoded.hasAlphaChannel() ? QImage::Format_ARGB32
                                                                              : QImage::Format_RGB32);
        if (!extendBounds(frame, bounds, canceled)) {
            return false;
        }
        if (!bounds.isEmpty()) {
            sampleColors(frame, bounds, qBound(20, delay, 1000), colors);
        }
        return true;
    };
    bool collected = false;
    if (!gif.isEmpty()) {
        QBuffer source;
        source.setData(gif);
        source.open(QIODevice::ReadOnly);
        QImageReader reader(&source, "gif");
        while (!canceled() && reader.canRead()) {
            const QImage frame = reader.read();
            if (frame.size() != firstFrame.size() || !collect(frame, reader.nextImageDelay())) {
                break;
            }
            collected = true;
        }
    }
    if (!collected && !collect(firstFrame, 100)) {
        return std::nullopt;
    }
    if (canceled()) {
        return std::nullopt;
    }
    PetAppearance appearance;
    if (!bounds.isEmpty()) {
        const double width = firstFrame.width();
        const double height = firstFrame.height();
        appearance.visibleBounds = {bounds.x() / width, bounds.y() / height,
                                    bounds.width() / width, bounds.height() / height};
    }
    choosePalette(colors, appearance);
    return appearance;
}
