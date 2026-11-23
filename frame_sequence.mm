#include "frame_sequence.h"
#include "pet_asset.h"

#include <QFileInfo>
#include <QImage>
#include <QList>
#include <QPainter>

#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

#include <memory>
#include <type_traits>
#include <utility>

namespace {
struct Frame {
    QImage image;
    QSize sourceSize;
};

QImage alignedFrame(const Frame& frame, const QSize& canvasSize, double scale)
{
    QImage canvas(canvasSize, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    const auto fit = (frame.sourceSize * scale).expandedTo(QSize(1, 1));
    const QRect rect((canvas.width() - fit.width()) / 2, canvas.height() - fit.height(),
                     fit.width(), fit.height());
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(rect, frame.image);
    return canvas;
}
}

FrameSequenceResult encodeFrameSequence(const QStringList& paths, int fps,
                                        const std::function<bool(int done)>& progress)
{
    if (paths.size() < minSequenceFrames || paths.size() > maxSequenceFrames
        || fps < 1 || fps > maxSequenceFps) {
        return {{}, QStringLiteral("请选择 %1–%2 张图片，并设置 1–%3 帧/秒。")
                        .arg(minSequenceFrames).arg(maxSequenceFrames).arg(maxSequenceFps)};
    }

    // Cache only desktop-sized copies. Keep the source sizes for common scaling at encode time.
    QList<Frame> frames;
    frames.reserve(paths.size());
    QSize sourceCanvas(1, 1);
    for (int index = 0; index < paths.size(); ++index) {
        if (!progress(index)) {
            return {};
        }
        const auto& path = paths.at(index);
        QString error;
        auto image = loadPetSequenceFrame(path, error);
        if (image.isNull()) {
            return {{}, QStringLiteral("第 %1 帧「%2」无法读取。%3")
                            .arg(index + 1).arg(QFileInfo(path).fileName(), error)};
        }
        const QSize sourceSize = image.size();
        sourceCanvas = sourceCanvas.expandedTo(sourceSize);
        if (qMax(sourceSize.width(), sourceSize.height()) > maxSequenceCanvasSide) {
            const QSize fit = sourceSize.scaled(maxSequenceCanvasSide, maxSequenceCanvasSide, Qt::KeepAspectRatio)
                                  .expandedTo(QSize(1, 1));
            image = image.scaled(fit, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        image.convertTo(QImage::Format_ARGB32_Premultiplied);
        if (image.isNull()) {
            return {{}, QStringLiteral("无法缩放第 %1 帧。").arg(index + 1)};
        }
        frames.append({std::move(image), sourceSize});
    }

    const double scale = qMin(1.0, static_cast<double>(maxSequenceCanvasSide)
                               / qMax(sourceCanvas.width(), sourceCanvas.height()));
    const QSize canvas = (sourceCanvas * scale).expandedTo(QSize(1, 1));
    const int count = static_cast<int>(frames.size());
    return encodeLoopingGif(count, fps,
        [&](int index) { return alignedFrame(frames.at(index), canvas, scale); },
        [&](int done) { return progress(count + done); });
}

FrameSequenceResult encodeLoopingGif(int frameCount, int fps,
                                     const std::function<QImage(int index)>& frameAt,
                                     const std::function<bool(int done)>& progress)
{
    if (frameCount < minSequenceFrames || frameCount > maxSequenceFrames || fps < 1 || fps > maxSequenceFps) {
        return {{}, QStringLiteral("无法编码动图，帧数或播放速度超出范围。")};
    }
    @autoreleasepool {
        NSMutableData* encoded = [NSMutableData data];
        using Destination = std::unique_ptr<std::remove_pointer_t<CGImageDestinationRef>, decltype(&CFRelease)>;
        const Destination destination(
            CGImageDestinationCreateWithData((__bridge CFMutableDataRef)encoded,
                                             CFSTR("com.compuserve.gif"), frameCount, nullptr),
            CFRelease);
        if (!destination) {
            return {{}, QStringLiteral("无法初始化 GIF 编码器。")};
        }
        NSDictionary* properties = @{(__bridge NSString*)kCGImagePropertyGIFDictionary:
            @{(__bridge NSString*)kCGImagePropertyGIFLoopCount: @0}};
        CGImageDestinationSetProperties(destination.get(), (__bridge CFDictionaryRef)properties);

        int elapsed = 0;
        for (int index = 0; index < frameCount; ++index) {
            if (!progress(index)) {
                return {};
            }
            @autoreleasepool {
                const auto frame = frameAt(index);
                using Image = std::unique_ptr<std::remove_pointer_t<CGImageRef>, decltype(&CFRelease)>;
                const Image image(frame.isNull() ? nullptr : frame.toCGImage(), CFRelease);
                if (!image) {
                    return {{}, QStringLiteral("无法编码第 %1 帧。").arg(index + 1)};
                }
                // GIF stores centiseconds. Round accumulated time to avoid drifting at rates like 24 fps.
                const int nextTime = qRound(100.0 * (index + 1) / fps);
                const double delay = (nextTime - elapsed) / 100.0;
                elapsed = nextTime;
                NSDictionary* frameProperties = @{(__bridge NSString*)kCGImagePropertyGIFDictionary:
                    @{(__bridge NSString*)kCGImagePropertyGIFUnclampedDelayTime: @(delay),
                      (__bridge NSString*)kCGImagePropertyGIFDelayTime: @(delay)}};
                CGImageDestinationAddImage(destination.get(), image.get(), (__bridge CFDictionaryRef)frameProperties);
            }
        }
        if (!progress(frameCount)) {
            return {};
        }
        if (!CGImageDestinationFinalize(destination.get())) {
            return {{}, QStringLiteral("无法合成序列帧 GIF。")};
        }
        if (encoded.length > maxPetAssetBytes) {
            return {{}, QStringLiteral("生成的 GIF 超过 32 MiB，请减少帧数或缩小源图片后重试。")};
        }
        return {QByteArray(static_cast<const char*>(encoded.bytes), static_cast<qsizetype>(encoded.length)), {}};
    }
}
