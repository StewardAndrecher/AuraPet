#pragma once

#include <QImage>
#include <QString>

struct BackgroundResult {
    QImage image;
    QString error;
};

bool hasTransparentPixels(const QImage& image);
BackgroundResult removeStaticBackground(const QImage& image);
