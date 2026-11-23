#pragma once

#include <QByteArray>
#include <QImage>
#include <QStringList>

#include <functional>

inline constexpr int minSequenceFrames = 2;
inline constexpr int maxSequenceFrames = 120;
inline constexpr int maxSequenceFps = 30;
inline constexpr int maxSequenceCanvasSide = 512;

struct FrameSequenceResult {
    QByteArray gif;
    QString error;
};

FrameSequenceResult encodeFrameSequence(const QStringList& paths, int fps,
                                        const std::function<bool(int done)>& progress);
FrameSequenceResult encodeLoopingGif(int frameCount, int fps,
                                     const std::function<QImage(int index)>& frameAt,
                                     const std::function<bool(int done)>& progress);
