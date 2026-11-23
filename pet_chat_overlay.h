#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>

class ChatSession;
class QKeyEvent;
class QPlainTextEdit;
class QRect;
class QWidget;
class SpeechBubble;
class SpeechPlayback;

class PetChatOverlay : public QObject {
    Q_OBJECT

public:
    PetChatOverlay(QWidget& pet, ChatSession& chat, SpeechPlayback* voice = nullptr);

    void focusInput();
    void releaseInputFocus();
    void closeInput();
    void dismissProactiveSpeech();
    void sayFixed(const QString& sentence);
    void reset();
    std::function<bool(const QKeyEvent&)> zoomRequested;

signals:
    void activityChanged(bool active);
    void interactionSent();
    void inputVisibilityChanged(bool visible);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool showInput();
    void checkPointer();
    void sendInput();
    void showSpeech(const QString& text, bool generating, const QString& fullText = {});
    void scheduleSpeechHide();
    void playQueuedSpeech();
    void hideSpeech();
    void fitInput(const QRect& area);
    void positionPanels();
    void updateActivity();

    QWidget& pet_;
    ChatSession& chat_;
    SpeechPlayback* voice_;
    QWidget* inputPanel_;
    QPlainTextEdit* input_;
    SpeechBubble* bubble_;
    QTimer pointerClock_;
    QTimer hideBubble_;
    QTimer streamRefresh_;
    QTimer summaryClock_;
    QString pendingPrompt_;
    QString pendingSpeech_;
    QString queuedFixedSpeech_;
    QString preedit_;
    bool inputOpen_ = false;
    bool bubbleOpen_ = false;
    bool proactiveSpeech_ = false;
    bool active_ = false;
    bool pointerWasAway_ = false;
};
