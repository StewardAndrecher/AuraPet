#include "pet_chat_overlay.h"
#include "chat_session.h"
#include "speech_bubble.h"
#include "speech_playback.h"

#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScreen>
#include <QTextDocument>
#include <QTextLayout>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWidget>
#include <utility>

namespace {
constexpr auto floatingPanel = Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint;
constexpr int gap = 8;
constexpr int inputHorizontalPadding = 8;
constexpr int inputVerticalPadding = 5;
constexpr int documentMargin = 2;

QRect desktopArea(const QWidget& pet)
{
    return pet.screen() ? pet.screen()->availableGeometry() : pet.frameGeometry().adjusted(-360, -220, 360, 180);
}

QPoint boundedPanel(const QRect& area, const QSize& size, int centerX, int preferredY)
{
    return QPoint(qBound(area.left(), centerX - size.width() / 2, qMax(area.left(), area.right() + 1 - size.width())),
                  qBound(area.top(), preferredY, qMax(area.top(), area.bottom() + 1 - size.height())));
}

class ComposerSurface : public QWidget {
public:
    explicit ComposerSurface(QWidget* parent) : QWidget(parent) {}

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto colors = QApplication::palette();
        auto border = colors.color(QPalette::Mid);
        border.setAlpha(140);
        painter.setPen(border);
        painter.setBrush(colors.color(QPalette::Base));
        painter.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 11, 11);
    }
};
}

PetChatOverlay::PetChatOverlay(QWidget& pet, ChatSession& chat, SpeechPlayback* voice)
    : pet_(pet), chat_(chat), voice_(voice), inputPanel_(new QWidget(&pet, floatingPanel)),
      input_(new QPlainTextEdit(inputPanel_)), bubble_(new SpeechBubble(&pet))
{
    pet_.installEventFilter(this);
    input_->installEventFilter(this);
    inputPanel_->setObjectName(QStringLiteral("petChatInputPanel"));
    inputPanel_->setWindowTitle(QStringLiteral("桌宠聊天输入"));
    inputPanel_->setAttribute(Qt::WA_TranslucentBackground);
    inputPanel_->setAttribute(Qt::WA_ShowWithoutActivating);
    inputPanel_->setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    auto* surface = new ComposerSurface(inputPanel_);
    auto* outer = new QVBoxLayout(inputPanel_);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(surface);
    auto* contents = new QVBoxLayout(surface);
    contents->setContentsMargins(inputHorizontalPadding, inputVerticalPadding,
                                 inputHorizontalPadding, inputVerticalPadding);
    input_->setParent(surface);
    input_->setFrameShape(QFrame::NoFrame);
    input_->setStyleSheet(QStringLiteral("QPlainTextEdit { background: transparent; }"));
    input_->document()->setDocumentMargin(documentMargin);
    input_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    input_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    input_->setTabChangesFocus(true);
    input_->setPlaceholderText(QStringLiteral("输入消息…"));
    input_->setToolTip(QStringLiteral("回车发送 · Shift＋回车换行"));
    input_->setAccessibleName(QStringLiteral("桌宠聊天输入"));
    contents->addWidget(input_);

    hideBubble_.setSingleShot(true);
    streamRefresh_.setSingleShot(true);
    streamRefresh_.setInterval(40);
    connect(&streamRefresh_, &QTimer::timeout, this, [this] { showSpeech(pendingSpeech_, true); });
    pointerClock_.setInterval(160);
    connect(&pointerClock_, &QTimer::timeout, this, &PetChatOverlay::checkPointer);
    const auto watchWorkArea = [this](QScreen* screen) {
        connect(screen, &QScreen::availableGeometryChanged, this, [this] { positionPanels(); });
    };
    for (auto* screen : QGuiApplication::screens()) {
        watchWorkArea(screen);
    }
    connect(qApp, &QGuiApplication::screenAdded, this, watchWorkArea);
    summaryClock_.setInterval(60000);
    connect(&summaryClock_, &QTimer::timeout, this, [this] { chat_.organize(false); });
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget* previous, QWidget* current) {
        if (current == input_ && interaction_.inputVisible()) {
            summaryClock_.start();
            chat_.organize(false);
        } else if (previous == input_) {
            summaryClock_.stop();
            preedit_.clear();
            positionPanels();
        }
    });
    connect(&hideBubble_, &QTimer::timeout, this, &PetChatOverlay::hideSpeech);
    connect(input_, &QPlainTextEdit::textChanged, this, [this] {
        positionPanels();
        input_->ensureCursorVisible();
    });
    if (voice_) {
        connect(voice_, &SpeechPlayback::captionChanged, this, [this](const QString& text) {
            showSpeech(text, !voice_->textFinished());
        });
        connect(voice_, &SpeechPlayback::ended, this, [this] {
            if (interaction_.bubbleVisible() && bubble_->isHidden()) {
                hideSpeech();
            } else {
                scheduleSpeechHide();
            }
            QTimer::singleShot(0, this, &PetChatOverlay::playQueuedSpeech);
        });
    }
    connect(&chat_, &ChatSession::replyStarted, this, [this] {
        input_->setReadOnly(true);
        proactiveSpeech_ = chat_.proactiveReply();
        hideBubble_.stop();
        streamRefresh_.stop();
        pendingSpeech_.clear();
        fullSpeech_.clear();
        speechWindowStart_ = 0;
        speechContinuationPrefix_ = false;
        hideSpeech();
        bubble_->beginReply();
        if (voice_) {
            if (pet_.isVisible()) {
                voice_->begin(chat_.roleId());
            } else {
                voice_->reset();
            }
        }
    });
    connect(&chat_, &ChatSession::replyUpdated, this, [this](const QString& text) {
        if (voice_ && voice_->managesReply()) {
            voice_->submit(text, false);
            return;
        }
        pendingSpeech_ = text;
        if (!streamRefresh_.isActive()) {
            streamRefresh_.start();
        }
    });
    connect(&chat_, &ChatSession::replyFinished, this, [this](const QString& text, const QString& error) {
        streamRefresh_.stop();
        pendingSpeech_.clear();
        input_->setReadOnly(false);
        if (!error.isEmpty() && !pendingPrompt_.isEmpty()) {
            input_->setPlainText(pendingPrompt_);
            input_->moveCursor(QTextCursor::End);
        }
        pendingPrompt_.clear();
        if (voice_ && error.isEmpty() && voice_->managesReply() && !text.isEmpty()) {
            voice_->submit(text, true);
            return;
        }
        if (voice_) {
            voice_->reset();
        }
        if (!error.isEmpty() && !proactiveSpeech_) {
            showChatError(error);
        }
        showSpeech(text, false);
        scheduleSpeechHide();
        updateActivity();
        QTimer::singleShot(0, this, &PetChatOverlay::playQueuedSpeech);
    });
}

void PetChatOverlay::syncEnvironmentFacts()
{
    interaction_.petVisible = pet_.isVisible();
    interaction_.clickThrough = pet_.windowFlags().testFlag(Qt::WindowTransparentForInput);
    interaction_.modalBlocked = QApplication::activeModalWidget() || QApplication::activePopupWidget();
}

void PetChatOverlay::reconcilePresentation()
{
    syncEnvironmentFacts();
    const OverlayPresentation next = interaction_.resolve();

    if (next.showInput) {
        input_->setReadOnly(chat_.busy());
        positionPanels();
        if (!inputShown_) {
            inputPanel_->show();
            inputPanel_->raise();
            pointerClock_.start();
        }
    } else {
        pointerClock_.stop();
        summaryClock_.stop();
        preedit_.clear();
        if (inputShown_ || inputPanel_->isVisible()) {
            inputPanel_->hide();
        }
    }

    if (next.showInput != inputShown_) {
        inputShown_ = next.showInput;
        emit inputVisibilityChanged(inputShown_);
    }
    if (next.showActivityMeter != meterShown_) {
        meterShown_ = next.showActivityMeter;
        emit activityMeterVisibilityChanged(meterShown_);
    }
    updateActivity();
}

bool PetChatOverlay::showInput()
{
    syncEnvironmentFacts();
    if (!interaction_.resolve().canOpenInput) {
        return false;
    }
    if (inputShown_) {
        return true;
    }
    interaction_.input = OverlayInteraction::Input::Visible;
    pointerWasAway_ = false;
    reconcilePresentation();
    return inputShown_;
}

void PetChatOverlay::focusInput()
{
    if (!showInput()) {
        return;
    }
    inputPanel_->activateWindow();
    input_->setFocus(Qt::MouseFocusReason);
}

void PetChatOverlay::releaseInputFocus()
{
    input_->clearFocus();
    pet_.activateWindow();
}

void PetChatOverlay::setDragging(bool dragging)
{
    if (interaction_.dragging == dragging) {
        return;
    }
    interaction_.dragging = dragging;
    if (dragging) {
        interaction_.input = OverlayInteraction::Input::Hidden;
    }
    reconcilePresentation();
}

void PetChatOverlay::setAiming(bool aiming)
{
    if (interaction_.aiming == aiming) {
        return;
    }
    interaction_.aiming = aiming;
    if (aiming) {
        interaction_.input = OverlayInteraction::Input::Hidden;
    }
    reconcilePresentation();
}

void PetChatOverlay::setActivityMeterEnabled(bool enabled)
{
    if (interaction_.activityMeterEnabled == enabled) {
        return;
    }
    interaction_.activityMeterEnabled = enabled;
    reconcilePresentation();
}

void PetChatOverlay::setSpaceTransitTarget(const QPoint& landingPos)
{
    spaceTransitActive_ = true;
    spaceTransitLandingPos_ = landingPos;

    const bool activelySpeaking = interaction_.speech == OverlayInteraction::Speech::Generating
        || (voice_ && voice_->busy()) || chat_.busy();
    const qsizetype displayedLen = qMax<qsizetype>(0, fullSpeech_.size() - speechWindowStart_);
    if (activelySpeaking && interaction_.bubbleVisible()
        && (displayedLen >= 28 || bubble_->visibleLines() >= 2)) {
        speechWindowStart_ = fullSpeech_.size();
        QChar prev;
        for (qsizetype i = speechWindowStart_ - 1; i >= 0; --i) {
            const QChar c = fullSpeech_.at(i);
            if (!c.isSpace() && c != u'*') {
                prev = c;
                break;
            }
        }
        const bool atClauseEnd = prev.isNull() || QStringLiteral("。！？!?…\n）)").contains(prev);
        speechContinuationPrefix_ = !atClauseEnd;
        bubble_->beginReply();
        bubble_->hide();
    } else if (interaction_.bubbleVisible()) {
        positionPanels();
    }
}

void PetChatOverlay::endSpaceTransit()
{
    if (!spaceTransitActive_) {
        return;
    }
    spaceTransitActive_ = false;
    positionPanels();
}

void PetChatOverlay::checkPointer()
{
    const auto* focused = QApplication::focusWidget();
    if (focused && inputPanel_->isAncestorOf(focused)) {
        pointerWasAway_ = false;
        return;
    }

    const QRect petArea = pet_.frameGeometry();
    const QRect inputArea = inputPanel_->frameGeometry();
    const QRect gapArea(QPoint(qMin(petArea.left(), inputArea.left()), qMin(petArea.bottom(), inputArea.bottom()) + 1),
                        QPoint(qMax(petArea.right(), inputArea.right()), qMax(petArea.top(), inputArea.top()) - 1));
    const QPoint pointer = QCursor::pos();
    const bool away = !petArea.contains(pointer) && !inputArea.contains(pointer)
        && (gapArea.isEmpty() || !gapArea.contains(pointer));
    if (away && pointerWasAway_) {
        closeInput();
    } else {
        pointerWasAway_ = away;
    }
}

void PetChatOverlay::closeInput()
{
    interaction_.input = OverlayInteraction::Input::Hidden;
    reconcilePresentation();
}

void PetChatOverlay::reset()
{
    queuedFixedSpeech_.clear();
    if (voice_) {
        voice_->reset();
    }
    spaceTransitActive_ = false;
    closeInput();
    hideSpeech();
    streamRefresh_.stop();
    pendingSpeech_.clear();
    fullSpeech_.clear();
    speechWindowStart_ = 0;
    speechContinuationPrefix_ = false;
    bubble_->beginReply();
    pendingPrompt_.clear();
    proactiveSpeech_ = false;
    input_->clear();
    input_->setReadOnly(false);
}

void PetChatOverlay::sayFixed(const QString& sentence)
{
    if (!pet_.isVisible() || sentence.trimmed().isEmpty()) {
        return;
    }
    if (chat_.busy() || (voice_ && voice_->busy())) {
        queuedFixedSpeech_ = sentence;
        return;
    }
    presentSpeech(sentence, false);
}

void PetChatOverlay::sayRandom(const QString& sentence, const QByteArray& recording)
{
    if (!pet_.isVisible() || chat_.busy() || sentence.trimmed().isEmpty()) {
        return;
    }
    queuedFixedSpeech_.clear();
    presentSpeech(sentence, true, recording);
}

void PetChatOverlay::presentSpeech(const QString& sentence, bool proactive, const QByteArray& recording)
{
    hideBubble_.stop();
    streamRefresh_.stop();
    pendingSpeech_.clear();
    fullSpeech_.clear();
    speechWindowStart_ = 0;
    speechContinuationPrefix_ = false;
    proactiveSpeech_ = proactive;
    hideSpeech();
    bubble_->beginReply();
    if (!recording.isEmpty()) {
        if (!voice_ || !voice_->playRecording(recording, sentence)) {
            showSpeech(sentence, false);
            scheduleSpeechHide();
        }
        return;
    }
    if (voice_) {
        voice_->begin(chat_.roleId());
        if (voice_->managesReply()) {
            voice_->submit(sentence, true);
            return;
        }
    }
    showSpeech(sentence, false);
    scheduleSpeechHide();
}

void PetChatOverlay::playQueuedSpeech()
{
    if (queuedFixedSpeech_.isEmpty() || chat_.busy() || (voice_ && voice_->busy())) {
        return;
    }
    const auto sentence = std::exchange(queuedFixedSpeech_, QString{});
    sayFixed(sentence);
}

void PetChatOverlay::dismissProactiveSpeech()
{
    if (proactiveSpeech_) {
        if (voice_) {
            voice_->reset();
        }
        streamRefresh_.stop();
        pendingSpeech_.clear();
        hideSpeech();
        proactiveSpeech_ = false;
    }
}

void PetChatOverlay::sendInput()
{
    if (chat_.busy()) {
        return;
    }
    const auto text = input_->toPlainText().trimmed();
    if (text.isEmpty()) {
        return;
    }
    QToolTip::hideText();
    QString error;
    pendingPrompt_ = text;
    if (chat_.send(text, error)) {
        input_->clear();
        emit interactionSent();
    } else {
        pendingPrompt_.clear();
        showChatError(error);
    }
}

void PetChatOverlay::showChatError(const QString& error)
{
    if (!error.isEmpty() && pet_.isVisible()) {
        QToolTip::showText(input_->mapToGlobal(QPoint(0, input_->height())), error, input_);
    }
}

void PetChatOverlay::showSpeech(const QString& text, bool generating)
{
    if (text.trimmed().isEmpty()) {
        return;
    }
    if (!text.startsWith(fullSpeech_.left(speechWindowStart_))) {
        speechWindowStart_ = 0;
        speechContinuationPrefix_ = false;
    }
    fullSpeech_ = text;
    QString display = speechWindowStart_ > 0 ? fullSpeech_.mid(speechWindowStart_) : fullSpeech_;
    if (speechWindowStart_ > 0) {
        qsizetype trim = 0;
        while (trim < display.size() && (display.at(trim).isSpace() || QStringLiteral("，、,；;。").contains(display.at(trim)))) {
            ++trim;
        }
        display = display.mid(trim);
        if (display.isEmpty()) {
            interaction_.speech = generating ? OverlayInteraction::Speech::Generating : OverlayInteraction::Speech::Displayed;
            updateActivity();
            return;
        }
        if (speechContinuationPrefix_) {
            display.prepend(QStringLiteral("…"));
        }
    }
    bubble_->setMessage(display);
    if (!pet_.isVisible()) {
        return;
    }
    interaction_.speech = generating ? OverlayInteraction::Speech::Generating : OverlayInteraction::Speech::Displayed;
    updateActivity();
    positionPanels();
    if (bubble_->isHidden()) {
        bubble_->show();
        bubble_->raise();
    }
}

void PetChatOverlay::scheduleSpeechHide()
{
    if (interaction_.bubbleVisible()) {
        hideBubble_.start(qBound(5500, 4600 + 950 * bubble_->visibleLines(), 16000));
    }
}

void PetChatOverlay::hideSpeech()
{
    hideBubble_.stop();
    interaction_.speech = OverlayInteraction::Speech::Hidden;
    fullSpeech_.clear();
    speechWindowStart_ = 0;
    speechContinuationPrefix_ = false;
    bubble_->hide();
    updateActivity();
}

void PetChatOverlay::fitInput(const QRect& area)
{
    auto text = input_->toPlainText();
    if (!preedit_.isEmpty()) {
        text.insert(input_->textCursor().position(), preedit_);
    }
    const auto paragraphs = text.split(QLatin1Char('\n'));
    const QFontMetricsF metrics(input_->font());
    const int maxWidth = qMax(1, qMin(560, area.width()));
    constexpr int horizontalPadding = 2 * (inputHorizontalPadding + documentMargin);
    const int minWidth = qMin(qMax(168, qCeil(metrics.horizontalAdvance(input_->placeholderText())) + horizontalPadding),
                              maxWidth);
    int width = minWidth;
    for (const auto& paragraph : paragraphs) {
        width = qMax(width, qMin(maxWidth, qCeil(metrics.horizontalAdvance(paragraph)) + horizontalPadding + 4));
        if (width == maxWidth) {
            break;
        }
    }

    const auto anchor = pet_.frameGeometry();
    const int spaceAbove = anchor.top() - area.top() - gap;
    const int spaceBelow = area.bottom() - anchor.bottom() - gap;
    const int lineHeight = qCeil(metrics.lineSpacing()) + 2 * documentMargin + 1;
    const int maxHeight = qMax(lineHeight, qMin(200, qMax(spaceAbove, spaceBelow) - 2 * inputVerticalPadding));
    const qreal wrapWidth = qMax(1, width - horizontalPadding);
    qreal height = 2 * documentMargin + 1;
    for (const auto& paragraph : paragraphs) {
        if (paragraph.isEmpty()) {
            height += metrics.lineSpacing();
        } else {
            QTextLayout layout(paragraph, input_->font());
            layout.setTextOption(input_->document()->defaultTextOption());
            layout.beginLayout();
            while (height < maxHeight) {
                auto line = layout.createLine();
                if (!line.isValid()) {
                    break;
                }
                line.setLineWidth(wrapWidth);
                height += qCeil(line.height());
            }
            layout.endLayout();
        }
        if (height >= maxHeight) {
            break;
        }
    }
    const int editorHeight = qBound(lineHeight, qCeil(height), maxHeight);
    input_->setFixedHeight(editorHeight);
    inputPanel_->setFixedSize(width, editorHeight + 2 * inputVerticalPadding);
}

void PetChatOverlay::positionPanels()
{
    if (!interaction_.inputVisible() && !interaction_.bubbleVisible()) {
        return;
    }
    const QRect area = desktopArea(pet_);
    if (interaction_.inputVisible()) {
        fitInput(area);
    }
    const QRect anchor = pet_.frameGeometry();
    if (interaction_.inputVisible()) {
        const int below = anchor.bottom() + 1 + gap;
        const int above = anchor.top() - gap - inputPanel_->height();
        const bool inputAbove = below + inputPanel_->height() > area.bottom() + 1 && above >= area.top();
        inputPanel_->move(boundedPanel(area, inputPanel_->size(), anchor.center().x(), inputAbove ? above : below));
    }
    if (interaction_.bubbleVisible()) {
        if (spaceTransitActive_) {
            const QRect landingRect(spaceTransitLandingPos_, pet_.size());
            const QPoint liveOffset = pet_.pos() - spaceTransitLandingPos_;
            bubble_->placeWithTransitOffset(area, landingRect, liveOffset);
        } else {
            bubble_->place(area, anchor, interaction_.inputVisible() ? anchor.united(inputPanel_->frameGeometry()) : anchor);
        }
    }
}

void PetChatOverlay::updateActivity()
{
    const bool active = interaction_.inputVisible() || interaction_.bubbleVisible() || chat_.busy();
    if (active != active_) {
        active_ = active;
        emit activityChanged(active);
    }
}

bool PetChatOverlay::handleEscape(QEvent* event, bool composing)
{
    using Escape = OverlayInteraction::Escape;
    const auto command = interaction_.escape(composing, chat_.busy(), voice_ && voice_->busy());
    if (command == Escape::Ignore) {
        return false;
    }
    event->accept();
    if (event->type() != QEvent::KeyPress) {
        return true;
    }
    switch (command) {
    case Escape::Ignore: break;
    case Escape::StopReply:
        queuedFixedSpeech_.clear();
        chat_.stop();
        break;
    case Escape::StopSpeech:
        queuedFixedSpeech_.clear();
        voice_->stopReading();
        break;
    case Escape::CloseInput: closeInput(); break;
    case Escape::HideBubble: hideSpeech(); break;
    }
    return true;
}

bool PetChatOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == input_ && event->type() == QEvent::KeyPress && preedit_.isEmpty() && input_->document()->isEmpty()
        && zoomRequested && zoomRequested(*static_cast<QKeyEvent*>(event))) {
        event->accept();
        return true;
    }
    if (watched == input_) {
        if (event->type() == QEvent::InputMethod) {
            preedit_ = static_cast<QInputMethodEvent*>(event)->preeditString();
            QTimer::singleShot(0, this, [this] { positionPanels(); });
        } else if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) {
            const auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Escape && handleEscape(event, !preedit_.isEmpty())) {
                return true;
            }
            if (event->type() == QEvent::KeyPress && preedit_.isEmpty()
                && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
                if (key->modifiers().testFlag(Qt::ShiftModifier)) {
                    if (!input_->isReadOnly()) {
                        input_->insertPlainText(QStringLiteral("\n"));
                    }
                } else {
                    sendInput();
                }
                return true;
            }
        }
    }
    if (watched == &pet_) {
        if (event->type() == QEvent::Hide) {
            if (pet_.property("auraTransientHide").toBool()) {
                return false;
            }
            queuedFixedSpeech_.clear();
            if (voice_) {
                voice_->reset();
            }
            closeInput();
            hideSpeech();
        } else if (event->type() == QEvent::Show) {
            if (interaction_.bubbleVisible()) {
                positionPanels();
                if (bubble_->isHidden()) {
                    bubble_->show();
                    bubble_->raise();
                }
            }
        } else if ((event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)
                   && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape && handleEscape(event, false)) {
            return true;
        } else if (event->type() == QEvent::Enter && QGuiApplication::mouseButtons() == Qt::NoButton) {
            showInput();
        } else if (event->type() == QEvent::Move || event->type() == QEvent::Resize) {
            positionPanels();
        }
    }
    return QObject::eventFilter(watched, event);
}
