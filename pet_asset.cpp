#include "pet_asset.h"

#include <QBuffer>
#include <QCache>
#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QMutex>
#include <QSaveFile>

#include <avif/avif.h>

#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#endif

#include <utility>

namespace {
constexpr int maxImageSide = 4096;

bool validSize(const QSize& size)
{
    return !size.isEmpty() && size.width() <= maxImageSide && size.height() <= maxImageSide;
}

QImage decodeAvif(const avifROData& data, QString& error)
{
    std::unique_ptr<avifDecoder, decltype(&avifDecoderDestroy)> decoder(
        avifDecoderCreate(), avifDecoderDestroy);
    if (!decoder) {
        error = QStringLiteral("无法初始化 AVIF 解码器。");
        return {};
    }

    decoder->imageDimensionLimit = maxImageSide;
    decoder->imageSizeLimit = maxImageSide * maxImageSide;
    avifResult result = avifDecoderSetIOMemory(decoder.get(), data.data, data.size);
    if (result == AVIF_RESULT_OK) {
        result = avifDecoderParse(decoder.get());
    }
    if (result == AVIF_RESULT_OK && decoder->imageCount != 1) {
        error = QStringLiteral("目前支持静态 AVIF。动画请使用 GIF。");
        return {};
    }
    if (result == AVIF_RESULT_OK) {
        result = avifDecoderNextImage(decoder.get());
    }
    if (result != AVIF_RESULT_OK) {
        error = QString::fromLatin1(avifResultToString(result));
        return {};
    }

    const avifImage* image = decoder->image;
    QImage decoded(image->width, image->height, QImage::Format_RGBA8888);
    if (decoded.isNull()) {
        error = QStringLiteral("无法为 AVIF 分配像素缓冲区。");
        return {};
    }
    avifRGBImage rgba;
    avifRGBImageSetDefaults(&rgba, image);
    rgba.format = AVIF_RGB_FORMAT_RGBA;
    rgba.depth = 8;
    rgba.alphaPremultiplied = AVIF_FALSE;
    rgba.pixels = decoded.bits();
    rgba.rowBytes = decoded.bytesPerLine();
    result = avifImageYUVToRGB(image, &rgba);
    if (result != AVIF_RESULT_OK) {
        error = QString::fromLatin1(avifResultToString(result));
        return {};
    }
    return decoded;
}

#ifdef Q_OS_MACOS
QImage decodeMacWebp(const QByteArray& bytes, QString& error)
{
    CFDataRef data = CFDataCreateWithBytesNoCopy(kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(bytes.constData()), bytes.size(), kCFAllocatorNull);
    if (!data) {
        error = QStringLiteral("无法读取 WebP 数据。");
        return {};
    }
    CGImageSourceRef source = CGImageSourceCreateWithData(data, nullptr);
    CFRelease(data);
    if (!source) {
        error = QStringLiteral("无法解析 WebP 图片。");
        return {};
    }
    const size_t count = CGImageSourceGetCount(source);
    if (count == 0) {
        CFRelease(source);
        error = QStringLiteral("WebP 图片不包含有效帧。");
        return {};
    }
    if (count > 1) {
        CFRelease(source);
        error = QStringLiteral("目前支持静态 WebP。动画请使用 GIF。");
        return {};
    }
    CGImageRef cgImage = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    if (!cgImage) {
        error = QStringLiteral("无法解码 WebP 图像。");
        return {};
    }
    const int width = static_cast<int>(CGImageGetWidth(cgImage));
    const int height = static_cast<int>(CGImageGetHeight(cgImage));
    if (!validSize(QSize(width, height))) {
        CGImageRelease(cgImage);
        error = QStringLiteral("无法读取素材尺寸，或图片宽高超过 4096 像素。");
        return {};
    }
    QImage decoded(width, height, QImage::Format_RGBA8888_Premultiplied);
    if (decoded.isNull()) {
        CGImageRelease(cgImage);
        error = QStringLiteral("无法为 WebP 分配像素缓冲区。");
        return {};
    }
    decoded.fill(Qt::transparent);
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(decoded.bits(), width, height, 8, decoded.bytesPerLine(),
        colorSpace, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(colorSpace);
    if (!ctx) {
        CGImageRelease(cgImage);
        error = QStringLiteral("无法绘制 WebP 图像。");
        return {};
    }
    CGContextDrawImage(ctx, CGRectMake(0, 0, width, height), cgImage);
    CGContextRelease(ctx);
    CGImageRelease(cgImage);
    return decoded.convertToFormat(QImage::Format_RGBA8888);
}
#endif

std::optional<QByteArray> readAssetFile(const QString& path, QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return std::nullopt;
    }
    QByteArray bytes = file.read(maxPetAssetBytes + 1);
    if (file.error() != QFileDevice::NoError) {
        error = file.errorString();
        return std::nullopt;
    }
    return bytes;
}

QImage readFrame(const QByteArray& bytes, bool allowGif, QByteArray& format, QString& error)
{
    if (bytes.size() > maxPetAssetBytes) {
        error = QStringLiteral("素材文件上限为 32 MiB。");
        return {};
    }

    const avifROData avifData{
        reinterpret_cast<const uint8_t*>(bytes.constData()),
        static_cast<size_t>(bytes.size())
    };
    if (avifPeekCompatibleFileType(&avifData)) {
        format = "avif";
        return decodeAvif(avifData, error);
    }

#ifdef Q_OS_MACOS
    if (bytes.size() >= 12 && bytes.startsWith("RIFF") && bytes.mid(8, 4) == "WEBP") {
        format = "webp";
        return decodeMacWebp(bytes, error);
    }
#endif

    QBuffer input;
    input.setData(bytes);
    input.open(QIODevice::ReadOnly);
    QImageReader reader(&input);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    format = reader.format();
    if (format != "png" && format != "jpeg" && format != "webp" && !(allowGif && format == "gif")) {
        error = allowGif ? QStringLiteral("请选择可读取的 PNG、JPEG、GIF、静态 WebP 或静态 AVIF。")
                         : QStringLiteral("序列帧需要使用 PNG、JPEG、静态 WebP 或静态 AVIF。");
        return {};
    }
    if (!validSize(reader.size())) {
        error = QStringLiteral("无法读取素材尺寸，或图片宽高超过 4096 像素。");
        return {};
    }
    auto frame = reader.read();
    if (frame.isNull()) {
        error = reader.errorString();
    }
    return frame;
}
}

std::optional<PetAsset> loadPetAsset(const QString& path, QString& error)
{
    auto prepared = loadPetPixels(path, error);
    return prepared ? std::optional(makePlayableAsset(std::move(*prepared))) : std::nullopt;
}

std::optional<PreparedPetAsset> loadPetPixels(const QString& path, QString& error)
{
    auto bytes = readAssetFile(path, error);
    return bytes ? decodePetPixels(std::move(*bytes), error) : std::nullopt;
}

QImage loadPetSequenceFrame(const QString& path, QString& error)
{
    const auto bytes = readAssetFile(path, error);
    QByteArray format;
    return bytes ? readFrame(*bytes, false, format, error) : QImage{};
}

std::optional<PetAsset> decodePetAsset(QByteArray bytes, QString& error)
{
    auto prepared = decodePetPixels(std::move(bytes), error);
    return prepared ? std::optional(makePlayableAsset(std::move(*prepared))) : std::nullopt;
}

std::optional<PreparedPetAsset> decodePetPixels(QByteArray bytes, QString& error)
{
    if (bytes.isEmpty() || bytes.size() > maxPetAssetBytes) {
        error = QStringLiteral("素材为空或超过 32 MiB。");
        return std::nullopt;
    }
    struct Frame { QImage image; QByteArray format; };
    static QMutex mutex;
    static QCache<QByteArray, Frame> cache(64 * 1024 * 1024);
    const auto key = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    {
        QMutexLocker lock(&mutex);
        if (const auto* cached = cache.object(key)) {
            return PreparedPetAsset{std::move(bytes), cached->image, cached->format};
        }
    }
    QByteArray format;
    auto frame = readFrame(bytes, true, format, error);
    if (frame.isNull()) {
        return std::nullopt;
    }
    {
        QMutexLocker lock(&mutex);
        cache.insert(key, new Frame{frame, format}, frame.sizeInBytes());
    }
    return PreparedPetAsset{std::move(bytes), std::move(frame), std::move(format)};
}

PetAsset makePlayableAsset(PreparedPetAsset prepared)
{
    auto frame = QPixmap::fromImage(std::move(prepared.firstFrame));
    if (prepared.format == "gif") {
        auto movie = std::make_unique<QMovie>();
        auto* playbackBuffer = new QBuffer(movie.get());
        playbackBuffer->setData(prepared.source);
        playbackBuffer->open(QIODevice::ReadOnly);
        movie->setDevice(playbackBuffer);
        movie->setFormat("gif");
        return PetAsset{std::move(prepared.source), std::move(frame), std::move(movie)};
    }
    return PetAsset{std::move(prepared.source), std::move(frame), nullptr};
}

std::optional<PetAsset> makePngPetAsset(const QImage& image, QString& error)
{
    QByteArray bytes;
    QBuffer output(&bytes);
    if (!output.open(QIODevice::WriteOnly) || !image.save(&output, "PNG")) {
        error = QStringLiteral("无法编码透明 PNG。");
        return std::nullopt;
    }
    if (bytes.size() > maxPetAssetBytes) {
        error = QStringLiteral("抠图结果超过 32 MiB，请缩小素材后重试。");
        return std::nullopt;
    }
    const QPixmap frame = QPixmap::fromImage(image);
    if (frame.isNull()) {
        error = QStringLiteral("无法显示抠图结果。");
        return std::nullopt;
    }
    return PetAsset{std::move(bytes), frame, nullptr};
}

bool savePetAsset(const PetAsset& asset, const QString& path, QString& error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(asset.source) != asset.source.size()
        || !file.commit()) {
        error = file.errorString();
        return false;
    }
    return true;
}
