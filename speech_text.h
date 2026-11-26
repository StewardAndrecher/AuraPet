#pragma once

#include <QList>
#include <QString>

struct SpeechAside {
    qsizetype sourceStart = 0;
    qsizetype sourceEnd = 0;
    qsizetype textOffset = 0;
    int pauseMs = 0;
    bool closed = false;
};

struct SpeechText {
    QString text;
    QList<qsizetype> sourceEnds;
    QList<SpeechAside> asides;
};

int asidePauseDurationMs(QStringView innerText);
SpeechText textForSpeech(QStringView source, bool finished);
