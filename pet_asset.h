#pragma once

#include <QByteArray>
#include <QMovie>
#include <QPixmap>
#include <QString>

#include <memory>
#include <optional>

class QImage;

inline constexpr qsizetype maxPetAssetBytes = 32 * 1024 * 1024;

struct PetAsset {
    QByteArray source;
    QPixmap firstFrame;
    std::unique_ptr<QMovie> movie;
};

std::optional<PetAsset> loadPetAsset(const QString& path, QString& error);
QImage loadPetSequenceFrame(const QString& path, QString& error);
std::optional<PetAsset> decodePetAsset(QByteArray bytes, QString& error);
std::optional<PetAsset> makePngPetAsset(const QImage& image, QString& error);
bool savePetAsset(const PetAsset& asset, const QString& path, QString& error);
