#include "speech_playback.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QMutex>
#include <QProcess>
#include <QtEndian>

#include <atomic>
#include <deque>
#include <memory>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

namespace {

#ifdef Q_OS_WIN
struct WaveChunk {
    WAVEHDR header{};
    QByteArray data;
    qint64 frames = 0;
};

class WinPcmOutput : public PcmOutput {
public:
    bool append(const QByteArray& samples) override
    {
        if (samples.isEmpty()) {
            return true;
        }
        const qint64 frameCount = samples.size() / 2;
        if (frameCount <= 0) {
            return true;
        }
        QMutexLocker lock(&mutex_);
        if (!handle_ && !openDevice()) {
            return false;
        }
        reapFinishedLocked();

        auto chunk = std::make_unique<WaveChunk>();
        chunk->data = samples;
        chunk->frames = frameCount;
        chunk->header.lpData = chunk->data.data();
        chunk->header.dwBufferLength = static_cast<DWORD>(chunk->data.size());
        chunk->header.dwUser = reinterpret_cast<DWORD_PTR>(this);

        if (waveOutPrepareHeader(handle_, &chunk->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            return false;
        }
        if (waveOutWrite(handle_, &chunk->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            waveOutUnprepareHeader(handle_, &chunk->header, sizeof(WAVEHDR));
            return false;
        }
        pending_.push_back(std::move(chunk));
        return true;
    }

    qint64 playedFrames() const override
    {
        QMutexLocker lock(&mutex_);
        const_cast<WinPcmOutput*>(this)->reapFinishedLocked();
        return played_.load(std::memory_order_relaxed);
    }

    void reset() override
    {
        QMutexLocker lock(&mutex_);
        if (handle_) {
            waveOutReset(handle_);
            for (auto& chunk : pending_) {
                if (chunk->header.dwFlags & WHDR_PREPARED) {
                    waveOutUnprepareHeader(handle_, &chunk->header, sizeof(WAVEHDR));
                }
            }
            pending_.clear();
            waveOutClose(handle_);
            handle_ = nullptr;
        }
        played_.store(0, std::memory_order_relaxed);
    }

    ~WinPcmOutput() override { reset(); }

private:
    bool openDevice()
    {
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 1;
        format.nSamplesPerSec = SpeechPlayback::sampleRate;
        format.wBitsPerSample = 16;
        format.nBlockAlign = format.nChannels * (format.wBitsPerSample / 8);
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
        return waveOutOpen(&handle_, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR;
    }

    void reapFinishedLocked()
    {
        while (!pending_.empty() && (pending_.front()->header.dwFlags & WHDR_DONE)) {
            auto chunk = std::move(pending_.front());
            pending_.pop_front();
            if (handle_ && (chunk->header.dwFlags & WHDR_PREPARED)) {
                waveOutUnprepareHeader(handle_, &chunk->header, sizeof(WAVEHDR));
            }
            played_.fetch_add(chunk->frames, std::memory_order_relaxed);
        }
    }

    mutable QMutex mutex_;
    HWAVEOUT handle_ = nullptr;
    std::deque<std::unique_ptr<WaveChunk>> pending_;
    std::atomic<qint64> played_{0};
};

class WinRecordingOutput : public RecordingOutput {
public:
    bool play(const QByteArray& bytes) override
    {
        reset();
        if (bytes.size() < 44 || !bytes.startsWith("RIFF") || bytes.mid(8, 4) != "WAVE") {
            return false;
        }
        // Parse WAV fmt and data chunks to estimate duration for caption sync
        int offset = 12;
        quint32 byteRate = 0;
        quint32 dataBytes = 0;
        while (offset + 8 <= bytes.size()) {
            const QByteArray chunkId = bytes.mid(offset, 4);
            const quint32 chunkSize = qFromLittleEndian<quint32>(bytes.constData() + offset + 4);
            offset += 8;
            if (offset + static_cast<qint64>(chunkSize) > bytes.size()) {
                break;
            }
            if (chunkId == "fmt " && chunkSize >= 16) {
                byteRate = qFromLittleEndian<quint32>(bytes.constData() + offset + 8);
            } else if (chunkId == "data") {
                dataBytes = chunkSize;
                break;
            }
            offset += static_cast<int>(chunkSize + (chunkSize & 1U));
        }
        durationMs_ = (byteRate > 0 && dataBytes > 0)
            ? qMax<qint64>(200, static_cast<qint64>(dataBytes) * 1000 / byteRate)
            : 1500;
        buffer_ = bytes;
        if (!PlaySoundA(buffer_.constData(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT)) {
            buffer_.clear();
            return false;
        }
        playing_ = true;
        clock_.start();
        return true;
    }

    bool active() const override
    {
        if (!playing_) {
            return false;
        }
        if (clock_.elapsed() >= durationMs_) {
            const_cast<WinRecordingOutput*>(this)->playing_ = false;
            return false;
        }
        return true;
    }

    qreal progress() const override
    {
        if (!playing_ || durationMs_ <= 0) {
            return 0.0;
        }
        return qBound(0.0, static_cast<qreal>(clock_.elapsed()) / static_cast<qreal>(durationMs_), 1.0);
    }

    void reset() override
    {
        if (playing_) {
            PlaySoundA(nullptr, nullptr, 0);
            playing_ = false;
        }
        buffer_.clear();
        durationMs_ = 0;
    }

    ~WinRecordingOutput() override { reset(); }

private:
    QByteArray buffer_;
    QElapsedTimer clock_;
    qint64 durationMs_ = 0;
    bool playing_ = false;
};

class WinSystemSpeech : public SystemSpeechOutput {
public:
    bool speak(const QString& text, const QString& language) override
    {
        reset();
        if (text.trimmed().isEmpty()) {
            return false;
        }
        textLength_ = text.size();
        estimatedMs_ = qBound<qint64>(800, textLength_ * 170, 30000);
        const QString culture = (language == QStringLiteral("ja")) ? QStringLiteral("ja-JP") : QStringLiteral("zh-CN");
        const QString script = QStringLiteral(
            "Add-Type -AssemblyName System.Speech;"
            "$s = New-Object System.Speech.Synthesis.SpeechSynthesizer;"
            "$v = $s.GetInstalledVoices() | Where-Object { $_.VoiceInfo.Culture.Name -like '%1*' } | Select-Object -First 1;"
            "if ($v) { $s.SelectVoice($v.VoiceInfo.Name) };"
            "$t = [Console]::In.ReadToEnd();"
            "$s.Speak($t);").arg(culture);

        process_ = std::make_unique<QProcess>();
        process_->start(QStringLiteral("powershell.exe"),
                        {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                         QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"),
                         QStringLiteral("-Command"), script});
        if (!process_->waitForStarted(1500)) {
            process_.reset();
            return false;
        }
        process_->write(text.toUtf8());
        process_->closeWriteChannel();
        clock_.start();
        return true;
    }

    bool active() const override
    {
        return process_ && process_->state() != QProcess::NotRunning;
    }

    std::optional<Range> spokenRange() const override
    {
        if (!active() || textLength_ <= 0 || estimatedMs_ <= 0) {
            return std::nullopt;
        }
        const qreal frac = qBound(0.0, static_cast<qreal>(clock_.elapsed()) / static_cast<qreal>(estimatedMs_), 0.95);
        const qsizetype count = qMax<qsizetype>(1, qRound(frac * textLength_));
        return Range{0, qMin(count, textLength_)};
    }

    void reset() override
    {
        if (process_) {
            if (process_->state() != QProcess::NotRunning) {
                process_->kill();
                process_->waitForFinished(500);
            }
            process_.reset();
        }
        textLength_ = 0;
        estimatedMs_ = 0;
    }

    ~WinSystemSpeech() override { reset(); }

private:
    std::unique_ptr<QProcess> process_;
    QElapsedTimer clock_;
    qsizetype textLength_ = 0;
    qint64 estimatedMs_ = 0;
};
#else
class NullPcmOutput : public PcmOutput {
public:
    bool append(const QByteArray&) override { return false; }
    qint64 playedFrames() const override { return 0; }
    void reset() override {}
};

class NullSystemSpeech : public SystemSpeechOutput {
public:
    bool speak(const QString&, const QString&) override { return false; }
    bool active() const override { return false; }
    std::optional<Range> spokenRange() const override { return std::nullopt; }
    void reset() override {}
};

class NullRecordingOutput : public RecordingOutput {
public:
    bool play(const QByteArray&) override { return false; }
    bool active() const override { return false; }
    qreal progress() const override { return 0.0; }
    void reset() override {}
};
#endif

}

std::unique_ptr<PcmOutput> makePcmOutput()
{
#ifdef Q_OS_WIN
    return std::make_unique<WinPcmOutput>();
#else
    return std::make_unique<NullPcmOutput>();
#endif
}

std::unique_ptr<SystemSpeechOutput> makeSystemSpeechOutput()
{
#ifdef Q_OS_WIN
    return std::make_unique<WinSystemSpeech>();
#else
    return std::make_unique<NullSystemSpeech>();
#endif
}

std::unique_ptr<RecordingOutput> makeRecordingOutput()
{
#ifdef Q_OS_WIN
    return std::make_unique<WinRecordingOutput>();
#else
    return std::make_unique<NullRecordingOutput>();
#endif
}
