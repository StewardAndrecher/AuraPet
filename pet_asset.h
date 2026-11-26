#pragma once

#include <QByteArray>
#include <QImage>
#include <QMovie>
#include <QPixmap>
#include <QString>

#include <memory>
#include <optional>

inline constexpr qsizetype maxPetAssetBytes = 32 * 1024 * 1024;

struct PreparedPetAsset {
    QByteArray source;
    QImage firstFrame;
    QByteArray format;
};

struct PetAsset {
    QByteArray source;
    QPixmap firstFrame;
    std::unique_ptr<QMovie> movie;
};

std::optional<PetAsset> loadPetAsset(const QString& path, QString& error);
std::optional<PreparedPetAsset> loadPetPixels(const QString& path, QString& error);
std::optional<PreparedPetAsset> decodePetPixels(QByteArray bytes, QString& error);
PetAsset makePlayableAsset(PreparedPetAsset prepared);
QImage loadPetSequenceFrame(const QString& path, QString& error);
std::optional<PetAsset> decodePetAsset(QByteArray bytes, QString& error);
std::optional<PetAsset> makePngPetAsset(const QImage& image, QString& error);
bool savePetAsset(const PetAsset& asset, const QString& path, QString& error);
