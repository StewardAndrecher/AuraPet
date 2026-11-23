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
    virtual ~SystemSpeechOutput() = default;
    virtual bool speak(const QString& text, const QString& language) = 0;
    virtual bool active() const = 0;
    virtual void reset() = 0;
};

std::unique_ptr<SystemSpeechOutput> makeSystemSpeechOutput();

class SpeechPlayback : public QObject {
    Q_OBJECT

public:
    static constexpr int sampleRate = 24000;

    explicit SpeechPlayback(QSettings& settings, std::unique_ptr<PcmOutput> output = makePcmOutput(),
                            std::unique_ptr<SystemSpeechOutput> system = makeSystemSpeechOutput());
    ~SpeechPlayback() override;
    void configure(QWidget& parent, const QString& roleId);
    bool available() const;
    void setQuiet(bool quiet);
    void setPackagedVoices(const QString& roleId, const QJsonObject& voices);
    void begin(const QString& roleId);
    void submit(const QString& text, bool finished);
    void stopReading();
    void reset();

    bool managesReply() const { return manages_; }
    bool busy() const { return speaking_ && !text_.isEmpty(); }
    const QString& fullText() const { return text_; }
    bool textFinished() const { return finished_; }

signals:
    void captionChanged(const QString& text);
    void ended();

private:
    struct Segment {
        QString text;
        qsizetype end;
    };
    struct Caption {
        qint64 frame;
        qsizetype end;
    };

    void segment(bool allowShortPause);
    void launchNext();
    void receive(bool last);
    void finishRequest();
    void tick();
    void tickSystemSpeech();
    void selectLanguage();
    QString voiceName(const QString& roleId, const QString& language) const;
    void reveal(qsizetype end);
    void finishReading();
    void clearAudio();

    QSettings& settings_;
    std::unique_ptr<PcmOutput> output_;
    std::unique_ptr<SystemSpeechOutput> system_;
    QNetworkAccessManager network_;
    QNetworkReply* reply_ = nullptr;
    QTimer phraseTimer_;
    QTimer firstAudioTimer_;
    QTimer firstCaptionTimer_;
    QTimer streamTimer_;
    QTimer playbackTimer_;
    QElapsedTimer outputProgress_;
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
    qsizetype cut_ = 0;
    qsizetype shown_ = 0;
    bool quiet_ = false;
    bool manages_ = false;
    bool speaking_ = false;
    bool finished_ = false;
    bool firstAudio_ = false;
    bool currentHasAudio_ = false;
    bool usingSystem_ = false;
    bool systemReading_ = false;
};
