#pragma once

#include <QJsonObject>
#include <QStringList>

struct RoleShare;

struct RandomSpeechLine {
    QString id;
    QString text;
    bool enabled = true;
};

struct RandomSpeechConfig {
    QStringList examples;
    QList<RandomSpeechLine> lines;
    QJsonObject recordings;
    bool withAi = true;
};

struct RandomSpeechChoice {
    QString id;
    QString text;
    QString recording;
    QString digest;
};

const QList<RandomSpeechLine>& randomSpeechExamples();
RandomSpeechConfig randomSpeechConfig(const QJsonObject& behavior);
QJsonObject encodeRandomSpeech(const RandomSpeechConfig& config);
bool validateRandomSpeech(const QJsonObject& config, QString& error);
bool isRandomSpeechRecording(const QString& path);
QString recordingDigest(const QByteArray& audio);
void removeChangedRandomRecordings(RoleShare& share);
QList<RandomSpeechChoice> randomSpeechChoices(const RoleShare& share);

class RandomSpeechDraw {
public:
    RandomSpeechChoice take(const QString& role, const QList<RandomSpeechChoice>& choices);

private:
    QString role_;
    QStringList ids_;
    QStringList remaining_;
    QString last_;
};
