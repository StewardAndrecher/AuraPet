#include "gif_background.h"
#include "background_removal.h"

#include <QBuffer>
#include <QImageReader>
#include <QList>

#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <type_traits>
#include <utility>

namespace {
constexpr int maxFrames = 60;
constexpr int maxSide = 512;

struct GifFrame {
    QImage image;
    int milliseconds;
};

QImage gifTransparency(const QImage& foreground)
{
    QImage frame = foreground;
    for (int y = 0; y < frame.height(); ++y) {
        auto* pixels = frame.scanLine(y);
        for (int x = 0; x < frame.width(); ++x) {
            auto* pixel = pixels + 4 * x;
            if (pixel[3] < 128) {
                pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0;
            } else {
                pixel[3] = 255;
            }
        }
    }
    return frame;
}

void stabilizeEdges(QImage& current, const QImage& previous, const QImage& currentOriginal,
                    const QImage& previousOriginal)
{
    // Blend soft edges only where both source frames show the same color.
    for (int y = 0; y < current.height(); ++y) {
        auto* now = current.scanLine(y);
        const auto* last = previous.constScanLine(y);
        const auto* source = currentOriginal.constScanLine(y);
        const auto* priorSource = previousOriginal.constScanLine(y);
        for (int x = 0; x < current.width(); ++x) {
            const int p = 4 * x;
            auto& alpha = now[p + 3];
            const int priorAlpha = last[p + 3];
            if (alpha > 24 && alpha < 232 && std::abs(alpha - priorAlpha) <= 64
                && std::abs(source[p] - priorSource[p]) <= 12
                && std::abs(source[p + 1] - priorSource[p + 1]) <= 12
                && std::abs(source[p + 2] - priorSource[p + 2]) <= 12) {
                alpha = (2 * alpha + priorAlpha) / 3;
            }
        }
    }
}
}

GifBackgroundResult removeGifBackground(const QByteArray& source,
                                        const std::function<bool(int done, int total)>& progress)
{
    QBuffer input;
    input.setData(source);
    input.open(QIODevice::ReadOnly);
    QImageReader reader(&input, "gif");
    const int count = reader.imageCount();
    if (count <= 0) {
        return {{}, QStringLiteral("无法读取 GIF 的完整帧。")};
    }
    if (count > maxFrames) {
        return {{}, QStringLiteral("这段动图有 %1 帧。本地抠图支持最多 %2 帧，可以直接保留原动图。")
                        .arg(count).arg(maxFrames)};
    }
    const QSize canvas = reader.size();
    if (canvas.isEmpty()) {
        return {{}, QStringLiteral("无法读取动图画布。")};
    }
    const int loops = reader.loopCount();
    const QSize processedSize = std::max(canvas.width(), canvas.height()) > maxSide
        ? canvas.scaled(maxSide, maxSide, Qt::KeepAspectRatio).expandedTo(QSize(1, 1)) : canvas;

    QList<GifFrame> frames;
    frames.reserve(count);
    for (int index = 0; index < count; ++index) {
        if (!progress(0, count)) {
            return {};
        }
        QImage frame = reader.read();
        if (frame.size() != canvas) {
            return {{}, QStringLiteral("无法合成 GIF 的第 %1 帧。").arg(index + 1)};
        }
        if (hasTransparentPixels(frame)) {
            return {{}, QStringLiteral("动图已经含有透明帧，可以直接保留原动图。")};
        }
        if (processedSize != canvas) {
            frame = frame.scaled(processedSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        frames.append({frame.convertToFormat(QImage::Format_RGBA8888), reader.nextImageDelay()});
    }

    @autoreleasepool {
        NSMutableData* encoded = [NSMutableData data];
        using Destination = std::unique_ptr<std::remove_pointer_t<CGImageDestinationRef>, decltype(&CFRelease)>;
        const Destination destination(
            CGImageDestinationCreateWithData((__bridge CFMutableDataRef)encoded,
                                             CFSTR("com.compuserve.gif"), count, nullptr),
            CFRelease);
        if (!destination) {
            return {{}, QStringLiteral("无法初始化 GIF 编码器。")};
        }
        if (loops != 0) {
            // Qt counts repeats after the first play. Image I/O counts total plays.
            NSDictionary* properties = @{(__bridge NSString*)kCGImagePropertyGIFDictionary:
                @{(__bridge NSString*)kCGImagePropertyGIFLoopCount: @(loops < 0 ? 0 : loops + 1)}};
            CGImageDestinationSetProperties(destination.get(), (__bridge CFDictionaryRef)properties);
        }

        QImage previous;
        for (int index = 0; index < count; ++index) {
            if (!progress(index, count)) {
                return {};
            }
            @autoreleasepool {
                const GifFrame& frame = frames[index];
                auto result = removeStaticBackground(frame.image);
                if (result.image.isNull()) {
                    return {{}, QStringLiteral("第 %1 / %2 帧抠图失败。%3")
                                    .arg(index + 1).arg(count).arg(result.error)};
                }
                if (!previous.isNull()) {
                    stabilizeEdges(result.image, previous, frame.image, frames[index - 1].image);
                }
                using Image = std::unique_ptr<std::remove_pointer_t<CGImageRef>, decltype(&CGImageRelease)>;
                const Image fg(gifTransparency(result.image).toCGImage(), CGImageRelease);
                if (!fg) {
                    return {{}, QStringLiteral("无法编码 GIF 的第 %1 帧。").arg(index + 1)};
                }
                const double delay = frame.milliseconds / 1000.0;
                NSDictionary* properties = @{(__bridge NSString*)kCGImagePropertyGIFDictionary:
                    @{(__bridge NSString*)kCGImagePropertyGIFUnclampedDelayTime: @(delay),
                      (__bridge NSString*)kCGImagePropertyGIFDelayTime: @(delay)}};
                CGImageDestinationAddImage(destination.get(), fg.get(), (__bridge CFDictionaryRef)properties);
                previous = std::move(result.image);
            }
        }
        if (!progress(count, count)) {
            return {};
        }
        if (!CGImageDestinationFinalize(destination.get())) {
            return {{}, QStringLiteral("无法保存抠图后的 GIF。")};
        }
        return {QByteArray(static_cast<const char*>(encoded.bytes), static_cast<qsizetype>(encoded.length)), {}};
    }
}
