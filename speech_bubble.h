#pragma once

#include <QString>
#include <QWidget>

#include <memory>

class QPushButton;
class ReplyReader;

class SpeechBubble : public QWidget {
public:
    explicit SpeechBubble(QWidget* pet);
    ~SpeechBubble() override;

    void beginReply();
    void setMessage(const QString& text, bool generating, const QString& fullText = {});
    void updateReader(const QString& text, bool generating);
    void place(const QRect& workArea, const QRect& pet, const QRect& occupied);
    int visibleLines() const;

protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    enum class Side { Above, Right, Left, Below };
    struct Preview;

    void syncButton();
    void openReader();

    std::unique_ptr<Preview> preview_;
    std::unique_ptr<QPushButton> readButton_;
    std::unique_ptr<ReplyReader> reader_;
    QString message_;
    QString fullMessage_;
    QPoint tailAnchor_;
    int preferredWidth_ = 0;
    int preferredHeight_ = 0;
    Side side_ = Side::Above;
    bool generating_ = false;
    bool readerFollows_ = false;
};
