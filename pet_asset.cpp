#include "pet_asset.h"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QSaveFile>

#include <avif/avif.h>

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

    QBuffer input;
    input.setData(bytes);
    input.open(QIODevice::ReadOnly);
    QImageReader reader(&input);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    format = reader.format();
    if (format != "png" && format != "jpeg" && !(allowGif && format == "gif")) {
        error = allowGif ? QStringLiteral("请选择可读取的 PNG、JPEG、GIF 或静态 AVIF。")
                         : QStringLiteral("序列帧需要使用 PNG、JPEG 或静态 AVIF。");
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
    auto bytes = readAssetFile(path, error);
    return bytes ? decodePetAsset(std::move(*bytes), error) : std::nullopt;
}

QImage loadPetSequenceFrame(const QString& path, QString& error)
{
    const auto bytes = readAssetFile(path, error);
    QByteArray format;
    return bytes ? readFrame(*bytes, false, format, error) : QImage{};
}

std::optional<PetAsset> decodePetAsset(QByteArray bytes, QString& error)
{
    QByteArray format;
    const QPixmap frame = QPixmap::fromImage(readFrame(bytes, true, format, error));
    if (frame.isNull()) {
        return std::nullopt;
    }
    if (format == "gif") {
        auto movie = std::make_unique<QMovie>();
        auto* playbackBuffer = new QBuffer(movie.get());
        playbackBuffer->setData(bytes);
        playbackBuffer->open(QIODevice::ReadOnly);
        movie->setDevice(playbackBuffer);
        movie->setFormat("gif");
        return PetAsset{std::move(bytes), frame, std::move(movie)};
    }
    return PetAsset{std::move(bytes), frame, nullptr};
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
