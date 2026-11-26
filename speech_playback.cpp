#include "speech_playback.h"
#include "role_package.h"
#include "speech_text.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QTextBoundaryFinder>

#include <algorithm>
#include <utility>

namespace {
const QString enabledKey = QStringLiteral("speech/enabled");
const QString endpointKey = QStringLiteral("speech/endpoint");
const QString systemVoiceKey = QStringLiteral("speech/useSystemVoice");
const QString oldEngineKey = QStringLiteral("speech/engine");
const QString defaultEndpoint = QStringLiteral("http://127.0.0.1:17860/v1/audio/speech");
constexpr int firstAudioMs = 1200;
constexpr int localFirstAudioMs = 6500;
constexpr int segmentPauseMs = 230;
constexpr int stalledStreamMs = 3500;
constexpr int packetBytes = SpeechPlayback::sampleRate * 2 * 60 / 1000;
constexpr int systemSpeechMs = 25000;

bool isLoopback(const QUrl& url)
{
    return url.host() == QStringLiteral("127.0.0.1") || url.host() == QStringLiteral("localhost")
        || url.host() == QStringLiteral("::1");
}

bool systemVoiceEnabled(const QSettings& settings)
{
    const bool previouslySelectedSystem = settings.value(oldEngineKey, 0).toInt() == 1;
    return settings.value(systemVoiceKey, previouslySelectedSystem).toBool();
}

QString roleKey(const QString& role, const QString& item)
{
    return QStringLiteral("speech/roles/") + role + u'/' + item;
}

QUrl speechUrl(QString address)
{
    QUrl url(address.trimmed(), QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty()
        || url.hasQuery() || url.hasFragment() || (url.scheme() == QStringLiteral("http") && !isLoopback(url))
        || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))) {
        return {};
    }
    if (url.path().isEmpty() || url.path() == QStringLiteral("/")) {
        url.setPath(QStringLiteral("/v1/audio/speech"));
    } else if (url.path() == QStringLiteral("/v1") || url.path() == QStringLiteral("/v1/")) {
        url.setPath(QStringLiteral("/v1/audio/speech"));
    }
    return url;
}

qsizetype segmentEnd(const QString& text, bool force, bool finished)
{
    constexpr qsizetype comfortable = 48;
    constexpr qsizetype maximum = 88;
    qsizetype soft = 0;
    const auto limit = qMin(text.size(), maximum);
    for (qsizetype i = 0; i < limit; ++i) {
        const auto c = text.at(i);
        const bool period = c == u'.' && (i + 1 < text.size() ? text.at(i + 1).isSpace() : finished);
        if (i > 0 && (period || QStringLiteral("。！？!?\n").contains(c))) {
            return i + 1;
        }
        if (i >= 8 && (QStringLiteral("，、,；;").contains(c) || (i >= 18 && c.isSpace()))) {
            soft = i + 1;
            if (i >= comfortable) {
                return soft;
            }
        }
    }
    if (soft > 0 && (force || limit >= comfortable)) {
        return soft;
    }
    if (limit == maximum && text.size() > maximum) {
        QTextBoundaryFinder words(QTextBoundaryFinder::Word, text);
        words.setPosition(maximum + 1);
        const auto boundary = words.toPreviousBoundary();
        if (boundary >= comfortable) {
            return boundary;
        }
        QTextBoundaryFinder characters(QTextBoundaryFinder::Grapheme, text);
        characters.setPosition(maximum + 1);
        return characters.toPreviousBoundary();
    }
    return finished ? text.size() : 0;
}

QList<qsizetype> captionEnds(const QString& text, qsizetype offset)
{
    QList<qsizetype> ends;
    QTextBoundaryFinder graphemes(QTextBoundaryFinder::Grapheme, text);
    for (auto end = graphemes.toNextBoundary(); end > 0; end = graphemes.toNextBoundary()) {
        ends.append(offset + end);
    }
    return ends;
}

}

SpeechPlayback::SpeechPlayback(QSettings& settings, std::unique_ptr<PcmOutput> output,
                               std::unique_ptr<SystemSpeechOutput> system, std::unique_ptr<RecordingOutput> recording)
    : settings_(settings), output_(std::move(output)), system_(std::move(system)), recording_(std::move(recording))
{
    phraseTimer_.setSingleShot(true);
    phraseTimer_.setInterval(segmentPauseMs);
    firstAudioTimer_.setSingleShot(true);
    firstAudioTimer_.setInterval(firstAudioMs);
    streamTimer_.setSingleShot(true);
    streamTimer_.setInterval(stalledStreamMs);
    playbackTimer_.setInterval(25);
    playbackTimer_.setTimerType(Qt::PreciseTimer);
    connect(&phraseTimer_, &QTimer::timeout, this, [this] {
        segment(true);
        tick();
    });
    connect(&firstAudioTimer_, &QTimer::timeout, this, &SpeechPlayback::finishReading);
    connect(&streamTimer_, &QTimer::timeout, this, &SpeechPlayback::finishReading);
    connect(&playbackTimer_, &QTimer::timeout, this, &SpeechPlayback::tick);
}

SpeechPlayback::~SpeechPlayback()
{
    reset();
}

void SpeechPlayback::configure(QWidget& parent, const QString& roleId)
{
    QDialog dialog(&parent);
    dialog.setWindowTitle(QStringLiteral("回复语音"));
    auto* layout = new QFormLayout(&dialog);
    auto* enabled = new QCheckBox(QStringLiteral("朗读桌宠发言"), &dialog);
    enabled->setChecked(settings_.value(enabledKey, false).toBool());
    auto* useSystem = new QCheckBox(QStringLiteral("使用系统发音"), &dialog);
    useSystem->setObjectName(QStringLiteral("useSystemVoice"));
    useSystem->setChecked(systemVoiceEnabled(settings_));
    auto* endpoint = new QLineEdit(settings_.value(endpointKey, defaultEndpoint).toString(), &dialog);
    endpoint->setObjectName(QStringLiteral("voiceServiceEndpoint"));
    endpoint->setMinimumWidth(325);
    const auto zh = roleKey(roleId, QStringLiteral("zh"));
    const auto ja = roleKey(roleId, QStringLiteral("ja"));
    const auto lang = roleKey(roleId, QStringLiteral("language"));
    auto* chinese = new QLineEdit(voiceName(roleId, QStringLiteral("zh")), &dialog);
    auto* japanese = new QLineEdit(voiceName(roleId, QStringLiteral("ja")), &dialog);
    auto* language = new QComboBox(&dialog);
    language->addItems({QStringLiteral("自动识别"), QStringLiteral("中文"), QStringLiteral("日文")});
    language->setCurrentIndex(qBound(0, settings_.value(lang, 0).toInt(), 2));
    auto* tip = new QLabel(QStringLiteral("使用系统发音时直接选用 macOS 音色。使用千问时，待朗读的文本会发送到此地址。"
        "远程地址使用 HTTPS。本机服务不可用时继续显示文字。"), &dialog);
    tip->setWordWrap(true);
    layout->addRow(enabled);
    layout->addRow(useSystem);
    layout->addRow(QStringLiteral("语音服务地址"), endpoint);
    layout->addRow(QStringLiteral("当前角色语言"), language);
    layout->addRow(QStringLiteral("中文音色名"), chinese);
    layout->addRow(QStringLiteral("日文音色名"), japanese);
    layout->addRow(tip);
    const auto syncEngineFields = [=] {
        const bool qwen = !useSystem->isChecked();
        endpoint->setEnabled(qwen);
        chinese->setEnabled(qwen);
        japanese->setEnabled(qwen);
    };
    connect(useSystem, &QCheckBox::toggled, &dialog, syncEngineFields);
    syncEngineFields();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (enabled->isChecked() && !useSystem->isChecked() && (speechUrl(endpoint->text()).isEmpty()
            || chinese->text().trimmed().isEmpty() || japanese->text().trimmed().isEmpty())) {
            QMessageBox::information(&dialog, QStringLiteral("语音设置"),
                                     QStringLiteral("请填写服务地址及两个音色名。远程地址使用 HTTPS，本机可用 HTTP。"));
            return;
        }
        settings_.setValue(enabledKey, enabled->isChecked());
        settings_.setValue(systemVoiceKey, useSystem->isChecked());
        settings_.remove(oldEngineKey);
        settings_.setValue(endpointKey, endpoint->text().trimmed());
        for (const auto& entry : {qMakePair(zh, chinese->text().trimmed()), qMakePair(ja, japanese->text().trimmed())}) {
            if (entry.second == roleVoiceAlias(roleId, entry.first.section(u'/', -1))) {
                settings_.remove(entry.first);
            } else {
                settings_.setValue(entry.first, entry.second);
            }
        }
        settings_.setValue(lang, language->currentIndex());
        settings_.sync();
        stopReading();
        dialog.accept();
    });
    dialog.exec();
}

bool SpeechPlayback::available() const
{
    return !quiet_ && settings_.value(enabledKey, false).toBool()
        && (systemVoiceEnabled(settings_) || !speechUrl(settings_.value(endpointKey, defaultEndpoint).toString()).isEmpty());
}

void SpeechPlayback::setQuiet(bool quiet)
{
    quiet_ = quiet;
    if (quiet) {
        stopReading();
    }
}

void SpeechPlayback::setPackagedVoices(const QString& roleId, const QJsonObject& voices)
{
    packagedRole_ = roleId;
    packagedVoices_ = voices;
}

QString SpeechPlayback::voiceName(const QString& roleId, const QString& language) const
{
    const auto endpoint = speechUrl(settings_.value(endpointKey, defaultEndpoint).toString());
    if (roleId == packagedRole_ && packagedVoices_.contains(language)
        && (endpoint.host() == QStringLiteral("127.0.0.1") || endpoint.host() == QStringLiteral("localhost"))
        && endpoint.port() == 17860) {
        // An explicitly entered external voice name remains the user's override.
        return settings_.value(roleKey(roleId, language), roleVoiceAlias(roleId, language)).toString();
    }
    return settings_.value(roleKey(roleId, language), language).toString();
}

void SpeechPlayback::begin(const QString& roleId)
{
    reset();
    if (available()) {
        url_ = speechUrl(settings_.value(endpointKey, defaultEndpoint).toString());
        usingSystem_ = systemVoiceEnabled(settings_);
        chineseVoice_ = voiceName(roleId, QStringLiteral("zh"));
        japaneseVoice_ = voiceName(roleId, QStringLiteral("ja"));
        const int language = settings_.value(roleKey(roleId, QStringLiteral("language")), 0).toInt();
        if (language != 0) {
            language_ = language == 2 ? QStringLiteral("ja") : QStringLiteral("zh");
        }
        manages_ = speaking_ = true;
    }
}

bool SpeechPlayback::playRecording(const QByteArray& bytes, const QString& caption)
{
    reset();
    if (quiet_ || !recording_->play(bytes)) {
        return false;
    }
    text_ = caption;
    current_.captions = captionEnds(caption, 0);
    speaking_ = finished_ = playingRecording_ = true;
    playbackTimer_.start();
    return true;
}

void SpeechPlayback::submit(const QString& text, bool finished)
{
    if (!manages_) {
        return;
    }
    finished_ = finished;
    const bool appended = text.startsWith(text_);
    text_ = text;
    if (!appended) {
        shown_ = -1;
        if (speaking_) {
            finishReading();
            return;
        }
    }
    if (!speaking_) {
        reveal(text_.size());
        if (finished_) {
            emit ended();
        }
        return;
    }
    segment(false);
    if (!finished_ && cut_ < text_.size() && !phraseTimer_.isActive()) {
        phraseTimer_.start();
    }
    if (!playbackTimer_.isActive()) {
        playbackTimer_.start();
    }
    tick();
}

void SpeechPlayback::segment(bool allowShortPause)
{
    static const QRegularExpression words(QStringLiteral(R"([\p{L}\p{N}])"));
    while (segments_.size() < 3 && cut_ < text_.size()) {
        const auto filtered = textForSpeech(QStringView(text_).mid(cut_), finished_);
        qsizetype length = 0;
        if (!filtered.asides.isEmpty()) {
            const auto& aside = filtered.asides.first();
            if (aside.textOffset == 0) {
                if (!aside.closed) {
                    if (finished_) {
                        cut_ = text_.size();
                    }
                    break;
                }
                qsizetype asideEnd = cut_ + aside.sourceEnd;
                while (asideEnd < text_.size() && text_.at(asideEnd) == u'*') {
                    ++asideEnd;
                }
                if (aside.pauseMs > 0) {
                    Segment pause;
                    pause.kind = Segment::Kind::Pause;
                    pause.end = asideEnd;
                    pause.pauseMs = aside.pauseMs;
                    pause.captions = captionEnds(text_.mid(cut_, asideEnd - cut_), cut_);
                    segments_.enqueue(std::move(pause));
                }
                cut_ = asideEnd;
                continue;
            }
            const bool boundaryComplete = aside.closed || finished_;
            const QString prefix = filtered.text.left(aside.textOffset);
            length = segmentEnd(prefix, allowShortPause || boundaryComplete, boundaryComplete);
        } else {
            if (filtered.text.isEmpty()) {
                if (finished_) {
                    cut_ = text_.size();
                }
                break;
            }
            length = segmentEnd(filtered.text, allowShortPause, finished_);
        }
        if (length == 0) {
            break;
        }
        const auto phrase = QStringView(filtered.text).first(length);
        const auto spoken = phrase.trimmed();
        Segment next;
        next.kind = Segment::Kind::Spoken;
        if (words.matchView(spoken).hasMatch()) {
            const auto first = spoken.data() - phrase.data();
            next.text = spoken.toString();
            for (qsizetype i = first; i < first + spoken.size(); ++i) {
                next.spokenEnds.append(cut_ + filtered.sourceEnds.at(i));
            }
            for (const auto end : captionEnds(next.text, 0)) {
                next.captions.append(next.spokenEnds.at(end - 1));
            }
        }
        cut_ += filtered.sourceEnds.at(length - 1);
        next.end = cut_;
        if (!next.text.isEmpty()) {
            segments_.enqueue(std::move(next));
        }
    }
}

void SpeechPlayback::startPause(Segment&& seg)
{
    current_ = std::move(seg);
    pausing_ = true;
    pauseElapsed_.restart();
    tickPause();
}

void SpeechPlayback::tickPause()
{
    if (!pausing_) {
        return;
    }
    const qint64 elapsed = pauseElapsed_.elapsed();
    if (elapsed >= current_.pauseMs) {
        reveal(current_.end);
        pausing_ = false;
        pauseElapsed_.invalidate();
        current_ = {};
        return;
    }
    const qreal revealWindow = qMax(1.0, current_.pauseMs * 0.70);
    const qreal progress = qBound(0.0, elapsed / revealWindow, 1.0);
    if (progress >= 1.0) {
        advanceCaption(current_.captions, current_.end);
        if (current_.captions.isEmpty() || shown_ >= current_.captions.last()) {
            reveal(current_.end);
        }
    } else {
        advanceByProgress(current_.captions, progress);
    }
}

void SpeechPlayback::launchNext()
{
    if (pausing_ || !speaking_ || reply_ || segments_.isEmpty()
        || segments_.head().kind != Segment::Kind::Spoken
        || submittedFrames_ - output_->playedFrames() > 2 * sampleRate) {
        return;
    }
    current_ = segments_.dequeue();
    const bool waitingForLocalStart = !firstAudio_ && isLoopback(url_);
    if (!firstAudio_) {
        firstAudioTimer_.start(waitingForLocalStart ? localFirstAudioMs : firstAudioMs);
    }
    if (framesPerGlyph_ <= 0) {
        static const QRegularExpression cjk(QStringLiteral(R"([\p{Han}\p{Hiragana}\p{Katakana}])"));
        framesPerGlyph_ = sampleRate / (cjk.match(current_.text).hasMatch() ? 4.5 : 11.0);
    }
    selectLanguage();
    currentHasAudio_ = false;
    QNetworkRequest request(url_);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "audio/pcm");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    const auto voice = language_ == QStringLiteral("ja") ? japaneseVoice_ : chineseVoice_;
    reply_ = network_.post(request, QJsonDocument(QJsonObject{
        {QStringLiteral("model"), QStringLiteral("Qwen3-TTS-12Hz-0.6B-Base")},
        {QStringLiteral("input"), current_.text},
        {QStringLiteral("voice"), voice},
        {QStringLiteral("response_format"), QStringLiteral("pcm")},
    }).toJson(QJsonDocument::Compact));
    connect(reply_, &QNetworkReply::readyRead, this, [this] { receive(false); });
    connect(reply_, &QNetworkReply::finished, this, &SpeechPlayback::finishRequest);
    streamTimer_.start(waitingForLocalStart ? localFirstAudioMs : stalledStreamMs);
}

void SpeechPlayback::selectLanguage()
{
    if (language_.isEmpty()) {
        static const QRegularExpression kana(QStringLiteral("[ぁ-ヿ]"));
        language_ = kana.match(current_.text).hasMatch() ? QStringLiteral("ja") : QStringLiteral("zh");
    }
}

void SpeechPlayback::receive(bool last)
{
    if (!reply_) {
        return;
    }
    const auto content = reply_->header(QNetworkRequest::ContentTypeHeader).toString();
    if (reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200
        || !content.startsWith(QStringLiteral("audio/pcm"))) {
        finishReading();
        return;
    }
    const auto bytes = reply_->readAll();
    pendingPcm_.append(bytes);
    if (!bytes.isEmpty()) {
        streamTimer_.start(stalledStreamMs);
    }
    if (last && pendingPcm_.size() % 2) {
        finishReading();
        return;
    }
    while (pendingPcm_.size() >= packetBytes || (last && !pendingPcm_.isEmpty())) {
        const auto count = qMin<qsizetype>(pendingPcm_.size(), packetBytes);
        if (!output_->append(pendingPcm_.left(count))) {
            finishReading();
            return;
        }
        pendingPcm_.remove(0, count);
        if (!currentHasAudio_) {
            captions_.enqueue({submittedFrames_, 0, current_.captions});
            currentHasAudio_ = true;
        }
        submittedFrames_ += count / 2;
        captions_.back().frames = submittedFrames_ - captions_.back().firstFrame;
        if (!outputProgress_.isValid()) {
            outputProgress_.start();
        }
        if (!firstAudio_) {
            firstAudio_ = true;
            firstAudioTimer_.stop();
        }
    }
    tick();
}

void SpeechPlayback::finishRequest()
{
    if (!reply_ || reply_->error() != QNetworkReply::NoError) {
        finishReading();
        return;
    }
    receive(true);
    if (!reply_) {
        return;
    }
    if (!currentHasAudio_) {
        finishReading();
        return;
    }
    auto& caption = captions_.back();
    caption.complete = true;
    if (!caption.ends.isEmpty()) {
        const qreal observed = qBound(700.0, qreal(caption.frames) / caption.ends.size(), 14000.0);
        framesPerGlyph_ = .35 * framesPerGlyph_ + .65 * observed;
    }
    reply_->deleteLater();
    reply_ = nullptr;
    streamTimer_.stop();
    tick();
}

void SpeechPlayback::tick()
{
    if (!speaking_) {
        return;
    }
    if (playingRecording_) {
        if (!recording_->active()) {
            advanceCaption(current_.captions, text_.size());
            if (shown_ == text_.size()) {
                finishReading();
            }
        } else {
            advanceByProgress(current_.captions, recording_->progress());
        }
        return;
    }
    if (finished_ && cut_ < text_.size() && segments_.size() < 3) {
        segment(false);
    }
    if (usingSystem_) {
        tickSystemSpeech();
        return;
    }
    const auto played = output_->playedFrames();
    if (played != lastPlayedFrames_) {
        lastPlayedFrames_ = played;
        outputProgress_.restart();
    }
    if (played >= submittedFrames_) {
        outputProgress_.invalidate();
    } else if (outputProgress_.isValid() && outputProgress_.elapsed() >= stalledStreamMs) {
        finishReading();
        return;
    }
    tickStreamCaptions(played);
    if (pausing_) {
        tickPause();
    }
    if (!pausing_ && !reply_ && captions_.isEmpty() && played >= submittedFrames_
        && !segments_.isEmpty() && segments_.head().kind == Segment::Kind::Pause) {
        startPause(segments_.dequeue());
    }
    if (!pausing_) {
        launchNext();
    }
    if (finished_ && !reply_ && !pausing_ && segments_.isEmpty() && captions_.isEmpty() && played >= submittedFrames_) {
        finishReading();
    }
}

void SpeechPlayback::tickSystemSpeech()
{
    if (pausing_) {
        tickPause();
        if (pausing_) {
            return;
        }
    }
    if (systemReading_) {
        if (system_->active()) {
            if (const auto range = system_->spokenRange()) {
                const auto last = qMin(current_.spokenEnds.size(), range->start + range->length);
                if (last > 0) {
                    advanceCaption(current_.captions, current_.spokenEnds.at(last - 1));
                }
            }
            return;
        }
        advanceCaption(current_.captions, current_.end);
        if (!current_.captions.isEmpty() && shown_ < current_.captions.last()) {
            return;
        }
        systemReading_ = false;
        streamTimer_.stop();
    }
    if (segments_.isEmpty()) {
        if (finished_) {
            finishReading();
        }
        return;
    }
    auto next = segments_.dequeue();
    if (next.kind == Segment::Kind::Pause) {
        startPause(std::move(next));
        return;
    }
    current_ = std::move(next);
    selectLanguage();
    if (!system_->speak(current_.text, language_)) {
        finishReading();
        return;
    }
    systemReading_ = true;
    streamTimer_.start(systemSpeechMs);
}

void SpeechPlayback::tickStreamCaptions(qint64 played)
{
    if (captions_.isEmpty()) {
        return;
    }
    const auto& caption = captions_.head();
    const qint64 elapsed = played - caption.firstFrame;
    if (elapsed <= 0 || caption.frames <= 0 || caption.ends.isEmpty()) {
        return;
    }
    const qreal expected = caption.complete ? caption.frames
        : qMax(qreal(caption.frames), framesPerGlyph_ * caption.ends.size());
    const qreal progress = qMin(elapsed / expected, caption.complete ? 1.0 : .999);
    advanceByProgress(caption.ends, progress);
    if (caption.complete && elapsed >= caption.frames && shown_ >= caption.ends.last()) {
        captions_.dequeue();
    }
}

void SpeechPlayback::advanceByProgress(const QList<qsizetype>& ends, qreal progress)
{
    if (!ends.isEmpty() && progress > 0) {
        const auto count = qBound(qsizetype(1), qsizetype(qBound(0.0, progress, 1.0) * ends.size()), ends.size());
        advanceCaption(ends, ends.at(count - 1));
    }
}

void SpeechPlayback::advanceCaption(const QList<qsizetype>& ends, qsizetype target)
{
    const auto next = std::upper_bound(ends.cbegin(), ends.cend(), shown_);
    if (next != ends.cend() && *next <= target) {
        reveal(*next);
    }
}

void SpeechPlayback::reveal(qsizetype end)
{
    if (shown_ != end) {
        shown_ = end;
        emit captionChanged(text_.left(shown_));
    }
}

void SpeechPlayback::finishReading()
{
    speaking_ = false;
    clearAudio();
    reveal(text_.size());
    if (finished_) {
        emit ended();
    }
}

void SpeechPlayback::stopReading()
{
    if (speaking_) {
        finishReading();
    }
}

void SpeechPlayback::clearAudio()
{
    if (playingRecording_) {
        recording_->reset();
        playingRecording_ = false;
    }
    if (auto* reply = std::exchange(reply_, nullptr)) {
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    phraseTimer_.stop();
    firstAudioTimer_.stop();
    streamTimer_.stop();
    playbackTimer_.stop();
    segments_.clear();
    captions_.clear();
    pendingPcm_.clear();
    current_ = {};
    if (systemReading_) {
        system_->reset();
        systemReading_ = false;
    }
    if (submittedFrames_ > 0) {
        output_->reset();
    }
    submittedFrames_ = 0;
    lastPlayedFrames_ = 0;
    outputProgress_.invalidate();
    pausing_ = false;
    pauseElapsed_.invalidate();
}

void SpeechPlayback::reset()
{
    clearAudio();
    text_.clear();
    language_.clear();
    cut_ = shown_ = 0;
    framesPerGlyph_ = 0;
    manages_ = speaking_ = finished_ = firstAudio_ = usingSystem_ = false;
}
