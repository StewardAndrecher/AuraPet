#include "background_removal.h"

#import <CoreGraphics/CoreGraphics.h>
#import <CoreVideo/CoreVideo.h>
#import <Vision/Vision.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <type_traits>

namespace {
QString visionError(NSError* error, const QString& fallback)
{
    return error ? QString::fromNSString(error.localizedDescription) : fallback;
}

BackgroundResult applyMask(const QImage& image, CVPixelBufferRef mask)
{
    if (CVPixelBufferGetPixelFormatType(mask) != kCVPixelFormatType_OneComponent32Float
        || CVPixelBufferGetWidth(mask) != static_cast<size_t>(image.width())
        || CVPixelBufferGetHeight(mask) != static_cast<size_t>(image.height())) {
        return {{}, QStringLiteral("系统返回了无法使用的抠图遮罩。")};
    }

    QImage cutout = image.convertToFormat(QImage::Format_RGBA8888);
    if (cutout.isNull() || CVPixelBufferLockBaseAddress(mask, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
        return {{}, QStringLiteral("无法读取抠图结果。")};
    }
    const auto* base = static_cast<const uchar*>(CVPixelBufferGetBaseAddress(mask));
    const auto stride = CVPixelBufferGetBytesPerRow(mask);
    bool hasForeground = false;
    bool removedBackground = false;
    for (int y = 0; y < cutout.height(); ++y) {
        const auto* maskRow = reinterpret_cast<const float*>(base + y * stride);
        auto* pixels = cutout.scanLine(y);
        for (int x = 0; x < cutout.width(); ++x) {
            auto* pixel = pixels + 4 * x;
            const uchar alpha = pixel[3];
            const float coverage = maskRow[x];
            const auto next = static_cast<uchar>(
                std::isfinite(coverage) ? qRound(alpha * std::clamp(coverage, 0.0f, 1.0f)) : 0);
            hasForeground |= next != 0;
            removedBackground |= next < alpha;
            pixel[3] = next;
            if (next == 0) {
                pixel[0] = pixel[1] = pixel[2] = 0;
            }
        }
    }
    CVPixelBufferUnlockBaseAddress(mask, kCVPixelBufferLock_ReadOnly);
    if (!hasForeground || !removedBackground) {
        return {{}, QStringLiteral("没有得到可用的前景，请保留原图或尝试其他素材。")};
    }
    return {cutout, {}};
}
}

bool hasTransparentPixels(const QImage& image)
{
    if (!image.hasAlphaChannel()) {
        return false;
    }
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < rgba.height(); ++y) {
        const auto* row = rgba.constScanLine(y);
        for (int x = 0; x < rgba.width(); ++x) {
            if (row[4 * x + 3] < 255) {
                return true;
            }
        }
    }
    return false;
}

BackgroundResult removeStaticBackground(const QImage& image)
{
    if (@available(macOS 14.0, *)) {
        @autoreleasepool {
            using Image = std::unique_ptr<std::remove_pointer_t<CGImageRef>, decltype(&CGImageRelease)>;
            const Image source(image.toCGImage(), CGImageRelease);
            if (!source) {
                return {{}, QStringLiteral("无法读取图像以进行抠图。")};
            }

            auto* handler = [[VNImageRequestHandler alloc] initWithCGImage:source.get() options:@{}];
            auto* request = [[VNGenerateForegroundInstanceMaskRequest alloc] init];
            NSError* error = nil;
            if (![handler performRequests:@[request] error:&error]) {
                return {{}, visionError(error, QStringLiteral("本地抠图失败。"))};
            }
            VNInstanceMaskObservation* observation = request.results.firstObject;
            NSIndexSet* instances = observation.allInstances;
            if (instances.count == 0) {
                return {{}, QStringLiteral("没有检测到前景主体，可以保留原图。")};
            }

            using Mask = std::unique_ptr<std::remove_pointer_t<CVPixelBufferRef>,
                                         decltype(&CVPixelBufferRelease)>;
            const Mask mask([observation generateScaledMaskForImageForInstances:instances
                                                            fromRequestHandler:handler
                                                                          error:&error],
                            CVPixelBufferRelease);
            if (!mask) {
                return {{}, visionError(error, QStringLiteral("无法生成主体遮罩。"))};
            }
            return applyMask(image, mask.get());
        }
    }
    return {{}, QStringLiteral("本地抠图需要 macOS 14 或更新版本。")};
}
