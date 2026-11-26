#pragma once

#include "overlay_interaction.h"

#include <QByteArray>
#include <QObject>
#include <QPoint>
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
    void setDragging(bool dragging);
    void setAiming(bool aiming);
    void setActivityMeterEnabled(bool enabled);
    void setSpaceTransitTarget(const QPoint& landingPos);
    void endSpaceTransit();
    void dismissProactiveSpeech();
    void sayFixed(const QString& sentence);
    void sayRandom(const QString& sentence, const QByteArray& recording = {});
    void reset();
    std::function<bool(const QKeyEvent&)> zoomRequested;

signals:
    void activityChanged(bool active);
    void interactionSent();
    void inputVisibilityChanged(bool visible);
    void activityMeterVisibilityChanged(bool visible);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool showInput();
    void syncEnvironmentFacts();
    void reconcilePresentation();
    void checkPointer();
    void sendInput();
    void showSpeech(const QString& text, bool generating);
    void showChatError(const QString& error);
    void scheduleSpeechHide();
    void playQueuedSpeech();
    void presentSpeech(const QString& sentence, bool proactive, const QByteArray& recording = {});
    void hideSpeech();
    void fitInput(const QRect& area);
    void positionPanels();
    void updateActivity();
    bool handleEscape(QEvent* event, bool composing);

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
    QString fullSpeech_;
    QString queuedFixedSpeech_;
    QString preedit_;
    qsizetype speechWindowStart_ = 0;
    bool speechContinuationPrefix_ = false;
    bool spaceTransitActive_ = false;
    QPoint spaceTransitLandingPos_;
    OverlayInteraction interaction_;
    bool inputShown_ = false;
    bool meterShown_ = false;
    bool proactiveSpeech_ = false;
    bool active_ = false;
    bool pointerWasAway_ = false;
};
