#include "speech_playback.h"

#include <QtEndian>

#import <AVFoundation/AVFoundation.h>

#include <atomic>

@interface GifPetSpeechDelegate : NSObject <AVSpeechSynthesizerDelegate>
@property(nonatomic, strong) AVSpeechUtterance* utterance;
@end

@implementation GifPetSpeechDelegate
- (void)clearUtterance:(AVSpeechUtterance*)utterance
{
    dispatch_async(dispatch_get_main_queue(), ^{
        if (self.utterance == utterance) {
            self.utterance = nil;
        }
    });
}

- (void)speechSynthesizer:(AVSpeechSynthesizer*)synthesizer didFinishSpeechUtterance:(AVSpeechUtterance*)utterance
{
    [self clearUtterance:utterance];
}

- (void)speechSynthesizer:(AVSpeechSynthesizer*)synthesizer didCancelSpeechUtterance:(AVSpeechUtterance*)utterance
{
    [self clearUtterance:utterance];
}
@end

namespace {
class MacPcmOutput : public PcmOutput {
public:
    bool append(const QByteArray& samples) override
    {
        if (!engine_ && !startEngine()) {
            return false;
        }
        const auto count = static_cast<AVAudioFrameCount>(samples.size() / 2);
        if (count == 0) {
            return true;
        }
        auto* buffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:format_ frameCapacity:count];
        if (!buffer) {
            return false;
        }
        buffer.frameLength = count;
        auto* output = buffer.floatChannelData[0];
        for (AVAudioFrameCount i = 0; i < count; ++i) {
            output[i] = qFromLittleEndian<qint16>(samples.constData() + 2 * i) / 32768.0f;
        }
        const auto progress = played_;
        [player_ scheduleBuffer:buffer completionCallbackType:AVAudioPlayerNodeCompletionDataPlayedBack
              completionHandler:^(AVAudioPlayerNodeCompletionCallbackType) {
                  progress->fetch_add(count, std::memory_order_relaxed);
              }];
        if (!player_.isPlaying) {
            [player_ play];
        }
        return true;
    }

    qint64 playedFrames() const override
    {
        return played_->load(std::memory_order_relaxed);
    }

    void reset() override
    {
        if (player_) {
            [player_ stop];
            [engine_ stop];
            [engine_ detachNode:player_];
        }
        player_ = nil;
        engine_ = nil;
        format_ = nil;
        played_ = std::make_shared<std::atomic<qint64>>(0);
    }

    ~MacPcmOutput() override
    {
        reset();
    }

private:
    bool startEngine()
    {
        engine_ = [[AVAudioEngine alloc] init];
        player_ = [[AVAudioPlayerNode alloc] init];
        format_ = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:SpeechPlayback::sampleRate channels:1];
        [engine_ attachNode:player_];
        [engine_ connect:player_ to:engine_.mainMixerNode format:format_];
        NSError* error = nil;
        if (![engine_ startAndReturnError:&error]) {
            reset();
            return false;
        }
        return true;
    }

    AVAudioEngine* engine_ = nil;
    AVAudioPlayerNode* player_ = nil;
    AVAudioFormat* format_ = nil;
    std::shared_ptr<std::atomic<qint64>> played_ = std::make_shared<std::atomic<qint64>>(0);
};

class MacSystemSpeech : public SystemSpeechOutput {
public:
    bool speak(const QString& text, const QString& language) override
    {
        NSString* locale = language == QStringLiteral("ja") ? @"ja-JP" : @"zh-CN";
        auto* voice = [AVSpeechSynthesisVoice voiceWithLanguage:locale];
        if (!voice) {
            return false;
        }
        if (!synthesizer_) {
            synthesizer_ = [[AVSpeechSynthesizer alloc] init];
            delegate_ = [[GifPetSpeechDelegate alloc] init];
            synthesizer_.delegate = delegate_;
        }
        auto* utterance = [AVSpeechUtterance speechUtteranceWithString:text.toNSString()];
        utterance.voice = voice;
        utterance.rate = AVSpeechUtteranceDefaultSpeechRate;
        delegate_.utterance = utterance;
        [synthesizer_ speakUtterance:utterance];
        return true;
    }

    bool active() const override { return delegate_.utterance != nil; }

    void reset() override
    {
        delegate_.utterance = nil;
        [synthesizer_ stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
    }

    ~MacSystemSpeech() override
    {
        reset();
        synthesizer_.delegate = nil;
    }

private:
    AVSpeechSynthesizer* synthesizer_ = nil;
    GifPetSpeechDelegate* delegate_ = nil;
};
}

std::unique_ptr<PcmOutput> makePcmOutput()
{
    return std::make_unique<MacPcmOutput>();
}

std::unique_ptr<SystemSpeechOutput> makeSystemSpeechOutput()
{
    return std::make_unique<MacSystemSpeech>();
}
