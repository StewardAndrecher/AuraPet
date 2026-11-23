#include "speech_playback.h"
#include "role_package.h"

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

#include <utility>

namespace {
const QString enabledKey = QStringLiteral("speech/enabled");
const QString endpointKey = QStringLiteral("speech/endpoint");
const QString systemVoiceKey = QStringLiteral("speech/useSystemVoice");
const QString oldEngineKey = QStringLiteral("speech/engine");
const QString defaultEndpoint = QStringLiteral("http://127.0.0.1:17860/v1/audio/speech");
constexpr int firstAudioMs = 1200;
constexpr int localFirstAudioMs = 6500;
constexpr int localFirstCaptionMs = 650;
constexpr int noPhraseMs = 2400;
constexpr int segmentPauseMs = 230;
constexpr int stalledStreamMs = 3500;
constexpr int packetBytes = SpeechPlayback::sampleRate * 2 * 120 / 1000;
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
        if (text.mid(i, 7) == QStringLiteral("http://") || text.mid(i, 8) == QStringLiteral("https://")) {
            while (i + 1 < text.size() && !text.at(i + 1).isSpace()
                   && !QStringLiteral("）】。！？，、").contains(text.at(i + 1))) {
                ++i;
            }
            if (i + 1 == text.size() && !finished) {
                return soft;
            }
            if (i >= limit) {
                return i + 1;
            }
            continue;
        }
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

QString cleanForSpeech(QString text)
{
    static const QRegularExpression urls(QStringLiteral(R"(https?://[^\s）】。！？，、]+)"));
    text.remove(urls);
    const auto clean = text.trimmed();
    if ((clean.startsWith(u'（') && clean.endsWith(u'）')) || (clean.startsWith(u'(') && clean.endsWith(u')'))) {
        return {};
    }
    for (const auto token : {u'*', u'`', u'#', u'[', u']', u'>'}) {
        text.remove(token);
    }
    return text.trimmed();
}
}

SpeechPlayback::SpeechPlayback(QSettings& settings, std::unique_ptr<PcmOutput> output,
                               std::unique_ptr<SystemSpeechOutput> system)
    : settings_(settings), output_(std::move(output)), system_(std::move(system))
{
    phraseTimer_.setSingleShot(true);
    phraseTimer_.setInterval(segmentPauseMs);
    firstAudioTimer_.setSingleShot(true);
    firstAudioTimer_.setInterval(firstAudioMs);
    streamTimer_.setSingleShot(true);
    streamTimer_.setInterval(stalledStreamMs);
    playbackTimer_.setInterval(25);
    connect(&phraseTimer_, &QTimer::timeout, this, [this] {
        segment(true);
        tick();
    });
    firstCaptionTimer_.setSingleShot(true);
    connect(&firstCaptionTimer_, &QTimer::timeout, this, [this] {
        if (speaking_ && reply_ && !firstAudio_) {
            reveal(current_.end);
        }
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
    auto* tip = new QLabel(QStringLiteral("开启「使用系统发音」会直接使用 macOS 音色。关闭时使用千问，服务不可用时显示文字。"), &dialog);
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
                                     QStringLiteral("请填写有效的 HTTP 或 HTTPS 服务地址及两个音色名。"));
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
    if (!usingSystem_ && !firstAudio_ && !text_.trimmed().isEmpty() && !firstAudioTimer_.isActive()) {
        firstAudioTimer_.start(noPhraseMs);
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
    while (segments_.size() < 3 && cut_ < text_.size()) {
        qsizetype length = segmentEnd(text_.mid(cut_), allowShortPause, finished_);
        if (length == 0) {
            break;
        }
        auto phrase = cleanForSpeech(text_.mid(cut_, length));
        cut_ += length;
        if (!phrase.isEmpty()) {
            segments_.enqueue({std::move(phrase), cut_});
        }
    }
}

void SpeechPlayback::launchNext()
{
    if (!speaking_ || reply_ || segments_.isEmpty()
        || submittedFrames_ - output_->playedFrames() > 2 * sampleRate) {
        return;
    }
    current_ = segments_.dequeue();
    const bool waitingForLocalStart = !firstAudio_ && isLoopback(url_);
    if (!firstAudio_) {
        firstAudioTimer_.start(waitingForLocalStart ? localFirstAudioMs : firstAudioMs);
        if (waitingForLocalStart) {
            firstCaptionTimer_.start(localFirstCaptionMs);
        }
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
        language_ = kana.match(text_).hasMatch() ? QStringLiteral("ja") : QStringLiteral("zh");
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
            captions_.enqueue({submittedFrames_, current_.end});
            currentHasAudio_ = true;
        }
        submittedFrames_ += count / 2;
        if (!outputProgress_.isValid()) {
            outputProgress_.start();
        }
        if (!firstAudio_) {
            firstAudio_ = true;
            firstAudioTimer_.stop();
            firstCaptionTimer_.stop();
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
    while (!captions_.isEmpty() && captions_.head().frame <= played) {
        reveal(captions_.dequeue().end);
    }
    launchNext();
    if (finished_ && !reply_ && segments_.isEmpty() && played >= submittedFrames_) {
        finishReading();
    }
}

void SpeechPlayback::tickSystemSpeech()
{
    if (systemReading_) {
        if (system_->active()) {
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
    const auto next = segments_.dequeue();
    selectLanguage();
    if (!system_->speak(next.text, language_)) {
        finishReading();
        return;
    }
    systemReading_ = true;
    reveal(next.end);
    streamTimer_.start(systemSpeechMs);
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
    if (auto* reply = std::exchange(reply_, nullptr)) {
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    phraseTimer_.stop();
    firstAudioTimer_.stop();
    firstCaptionTimer_.stop();
    streamTimer_.stop();
    playbackTimer_.stop();
    segments_.clear();
    captions_.clear();
    pendingPcm_.clear();
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
}

void SpeechPlayback::reset()
{
    clearAudio();
    text_.clear();
    language_.clear();
    cut_ = shown_ = 0;
    manages_ = speaking_ = finished_ = firstAudio_ = usingSystem_ = false;
}
