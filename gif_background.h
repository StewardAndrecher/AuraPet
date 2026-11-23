#pragma once

#include <QByteArray>
#include <QString>

#include <functional>

struct GifBackgroundResult {
    QByteArray gif;
    QString error;
};

GifBackgroundResult removeGifBackground(const QByteArray& source,
                                        const std::function<bool(int done, int total)>& progress);
