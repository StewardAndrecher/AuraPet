#pragma once

#include <QString>
#include <QStringList>
#include <QVariantAnimation>
#include <QWidget>

#include <memory>

class SpeechBubble : public QWidget {
public:
    explicit SpeechBubble(QWidget* pet);
    ~SpeechBubble() override;

    void beginReply();
    void setMessage(const QString& text);
    void place(const QRect& workArea, const QRect& pet, const QRect& occupied);
    void placeWithTransitOffset(const QRect& workArea, const QRect& landingPetRect, const QPoint& liveOffset);
    int visibleLines() const;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    enum class Side { Above, Right, Left, Below };
    struct Preview;

    void scrollTo(qreal offset);

    std::unique_ptr<Preview> preview_;
    std::unique_ptr<Preview> narrowPreview_;
    const Preview* displayed_ = nullptr;
    QString message_;
    QStringList paragraphs_;
    QVariantAnimation scrollAnimation_;
    qreal scrollOffset_ = 0;
    qreal scrollTarget_ = 0;
    QPoint tailAnchor_;
    int preferredWidth_ = 0;
    int preferredHeight_ = 0;
    Side side_ = Side::Above;
};
