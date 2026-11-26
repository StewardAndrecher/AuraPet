#pragma once

#include <QNetworkAccessManager>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <memory>
#include <optional>

class QSettings;
class QWidget;

class PcmOutput {
public:
    virtual ~PcmOutput() = default;
    virtual bool append(const QByteArray& samples) = 0;
    virtual qint64 playedFrames() const = 0;
    virtual void reset() = 0;
};

std::unique_ptr<PcmOutput> makePcmOutput();

class SystemSpeechOutput {
public:
    struct Range { qsizetype start; qsizetype length; };
    virtual ~SystemSpeechOutput() = default;
    virtual bool speak(const QString& text, const QString& language) = 0;
    virtual bool active() const = 0;
    virtual std::optional<Range> spokenRange() const = 0;
    virtual void reset() = 0;
};

std::unique_ptr<SystemSpeechOutput> makeSystemSpeechOutput();

class RecordingOutput {
public:
    virtual ~RecordingOutput() = default;
    virtual bool play(const QByteArray& bytes) = 0;
    virtual bool active() const = 0;
    virtual qreal progress() const = 0;
    virtual void reset() = 0;
};

std::unique_ptr<RecordingOutput> makeRecordingOutput();

class SpeechPlayback : public QObject {
    Q_OBJECT

public:
    static constexpr int sampleRate = 24000;

    explicit SpeechPlayback(QSettings& settings, std::unique_ptr<PcmOutput> output = makePcmOutput(),
                            std::unique_ptr<SystemSpeechOutput> system = makeSystemSpeechOutput(),
                            std::unique_ptr<RecordingOutput> recording = makeRecordingOutput());
    ~SpeechPlayback() override;
    void configure(QWidget& parent, const QString& roleId);
    bool available() const;
    void setQuiet(bool quiet);
    void setPackagedVoices(const QString& roleId, const QJsonObject& voices);
    void begin(const QString& roleId);
    bool playRecording(const QByteArray& bytes, const QString& caption);
    void submit(const QString& text, bool finished);
    void stopReading();
    void reset();

    bool managesReply() const { return manages_; }
    bool busy() const { return speaking_ && !text_.isEmpty(); }
    bool textFinished() const { return finished_; }

signals:
    void captionChanged(const QString& text);
    void ended();

private:
    struct Segment {
        enum class Kind { Spoken, Pause };
        Kind kind = Kind::Spoken;
        QString text;
        qsizetype end = 0;
        QList<qsizetype> spokenEnds;
        QList<qsizetype> captions;
        int pauseMs = 0;
    };
    struct Caption {
        qint64 firstFrame;
        qint64 frames = 0;
        QList<qsizetype> ends;
        bool complete = false;
    };

    void segment(bool allowShortPause);
    void launchNext();
    void startPause(Segment&& seg);
    void tickPause();
    void receive(bool last);
    void finishRequest();
    void tick();
    void tickSystemSpeech();
    void tickStreamCaptions(qint64 played);
    void advanceCaption(const QList<qsizetype>& ends, qsizetype target);
    void advanceByProgress(const QList<qsizetype>& ends, qreal progress);
    void selectLanguage();
    QString voiceName(const QString& roleId, const QString& language) const;
    void reveal(qsizetype end);
    void finishReading();
    void clearAudio();

    QSettings& settings_;
    std::unique_ptr<PcmOutput> output_;
    std::unique_ptr<SystemSpeechOutput> system_;
    std::unique_ptr<RecordingOutput> recording_;
    QNetworkAccessManager network_;
    QNetworkReply* reply_ = nullptr;
    QTimer phraseTimer_;
    QTimer firstAudioTimer_;
    QTimer streamTimer_;
    QTimer playbackTimer_;
    QElapsedTimer outputProgress_;
    QElapsedTimer pauseElapsed_;
    QQueue<Segment> segments_;
    QQueue<Caption> captions_;
    Segment current_{};
    QUrl url_;
    QString packagedRole_;
    QJsonObject packagedVoices_;
    QString chineseVoice_;
    QString japaneseVoice_;
    QString language_;
    QString text_;
    QByteArray pendingPcm_;
    qint64 submittedFrames_ = 0;
    qint64 lastPlayedFrames_ = 0;
    qreal framesPerGlyph_ = 0;
    qsizetype cut_ = 0;
    qsizetype shown_ = 0;
    bool quiet_ = false;
    bool playingRecording_ = false;
    bool manages_ = false;
    bool speaking_ = false;
    bool finished_ = false;
    bool firstAudio_ = false;
    bool currentHasAudio_ = false;
    bool usingSystem_ = false;
    bool systemReading_ = false;
    bool pausing_ = false;
};
