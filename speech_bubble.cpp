#include "speech_bubble.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFontMetricsF>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QScreen>
#include <QTextCursor>
#include <QTextLayout>
#include <QVBoxLayout>
#include <QtMath>

#include <array>
#include <vector>

namespace {
constexpr auto floating = Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint;
constexpr int horizontalInset = 31;
constexpr int topInset = 22;
constexpr int bottomInset = 22;
constexpr int buttonGap = 6;
constexpr int minWidth = 142;
constexpr int gap = 8;

QPoint positionIn(const QRect& area, QSize size, QPoint preferred)
{
    return {qBound(area.left(), preferred.x(), qMax(area.left(), area.right() + 1 - size.width())),
            qBound(area.top(), preferred.y(), qMax(area.top(), area.bottom() + 1 - size.height()))};
}
}

class ReplyReader : public QDialog {
public:
    ReplyReader()
        : status_(new QLabel(this)), text_(new QPlainTextEdit(this))
    {
        setObjectName(QStringLiteral("petReplyReader"));
        setWindowTitle(QStringLiteral("本条回复"));
        setWindowModality(Qt::NonModal);
        resize(535, 368);
        auto* layout = new QVBoxLayout(this);
        status_->setObjectName(QStringLiteral("petReplyStatus"));
        status_->hide();
        layout->addWidget(status_);
        text_->setObjectName(QStringLiteral("petReplyText"));
        text_->setAccessibleName(QStringLiteral("回复全文"));
        text_->setReadOnly(true);
        text_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        text_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        layout->addWidget(text_);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
        layout->addWidget(buttons);
    }

    void setReply(const QString& text, bool generating, bool startOver = false)
    {
        const QString visible = startOver ? QString{} : displayed_;
        if (startOver || visible != text) {
            if (!startOver && text.startsWith(visible)) {
                auto* scroll = text_->verticalScrollBar();
                const int scrollPosition = scroll->value();
                QTextCursor end(text_->document());
                end.movePosition(QTextCursor::End);
                end.insertText(text.mid(visible.size()));
                scroll->setValue(scrollPosition);
            } else {
                text_->setPlainText(text);
            }
            displayed_ = text;
        }
        status_->setText(QStringLiteral("正在生成…"));
        status_->setVisible(generating);
    }

    void stopFollowing()
    {
        status_->hide();
    }

private:
    QLabel* status_;
    QPlainTextEdit* text_;
    QString displayed_;
};

struct SpeechBubble::Preview {
    std::vector<std::unique_ptr<QTextLayout>> layouts;
    std::vector<QTextLine> lines;
    int textHeight = 0;
    bool overflow = false;

    static Preview layout(const QStringList& paragraphs, const QFont& font, int width, int maxHeight)
    {
        Preview result;
        QTextOption options;
        options.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        for (const auto& paragraph : paragraphs) {
            auto layout = std::make_unique<QTextLayout>(paragraph.isEmpty() ? QStringLiteral(" ") : paragraph, font);
            layout->setTextOption(options);
            layout->beginLayout();
            for (auto line = layout->createLine(); line.isValid(); line = layout->createLine()) {
                line.setLineWidth(width);
                const int next = result.textHeight + qCeil(line.height());
                if (next > maxHeight) {
                    result.overflow = true;
                    break;
                }
                line.setPosition(QPointF(0, result.textHeight));
                result.textHeight = next;
                result.lines.push_back(line);
            }
            layout->endLayout();
            result.layouts.push_back(std::move(layout));
            if (result.overflow) {
                break;
            }
        }
        return result;
    }
};

SpeechBubble::SpeechBubble(QWidget* pet)
    : QWidget(pet, floating | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput),
      preview_(std::make_unique<Preview>()),
      readButton_(std::make_unique<QPushButton>(QStringLiteral("查看本条全文"), pet))
{
    setObjectName(QStringLiteral("petSpeechBubble"));
    setAccessibleName(QStringLiteral("桌宠发言"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    readButton_->setWindowFlags(floating | Qt::WindowDoesNotAcceptFocus);
    readButton_->setObjectName(QStringLiteral("petReplyReadButton"));
    readButton_->setAttribute(Qt::WA_ShowWithoutActivating);
    readButton_->setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    readButton_->setFocusPolicy(Qt::NoFocus);
    readButton_->setCursor(Qt::PointingHandCursor);
    readButton_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #f4f2fa; color: #39324f; border: 1px solid #dfdaeb;"
        "border-radius: 8px; padding: 3px 9px; }"
        "QPushButton:hover { background: #ebe7f7; }"
        "QPushButton:pressed { background: #e3ddf3; }"));
    connect(readButton_.get(), &QPushButton::clicked, this, &SpeechBubble::openReader);
}

SpeechBubble::~SpeechBubble() = default;

void SpeechBubble::beginReply()
{
    message_.clear();
    fullMessage_.clear();
    preferredWidth_ = preferredHeight_ = 0;
    side_ = Side::Above;
    if (readerFollows_) {
        reader_->stopFollowing();
    }
    readerFollows_ = false;
}

void SpeechBubble::setMessage(const QString& text, bool generating, const QString& fullText)
{
    if (message_ != text) {
        message_ = text;
        setAccessibleDescription(text);
    }
    updateReader(fullText.isNull() ? text : fullText, generating);
}

void SpeechBubble::updateReader(const QString& text, bool generating)
{
    fullMessage_ = text;
    generating_ = generating;
    if (readerFollows_ && reader_->isVisible()) {
        reader_->setReply(fullMessage_, generating_);
    }
}

int SpeechBubble::visibleLines() const
{
    return static_cast<int>(preview_->lines.size());
}

void SpeechBubble::place(const QRect& workArea, const QRect& pet, const QRect& occupied)
{
    const auto insetArea = workArea.adjusted(gap, gap, -gap, -gap);
    const QRect area = insetArea.isValid() ? insetArea : workArea;
    const int capWidth = qMax(1, qMin(420, qRound(area.width() * .45)));
    const int capHeight = qMax(1, qMin(260, qRound(area.height() * .35)));
    const int footer = readButton_->sizeHint().height() + buttonGap;
    QString normalized = message_;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(u'\r', u'\n');
    const auto paragraphs = normalized.split(u'\n');

    const QFontMetricsF metrics(font());
    int width = qMin(capWidth, qMax(minWidth, preferredWidth_));
    for (const auto& paragraph : paragraphs) {
        if (width == capWidth) {
            break;
        }
        width = qMax(width, qMin(capWidth, qCeil(metrics.horizontalAdvance(paragraph)) + 2 * horizontalInset + 2));
    }
    preferredWidth_ = width;
    const auto layoutFor = [&](int w, int h) {
        return Preview::layout(paragraphs, font(), qMax(1, w - 2 * horizontalInset), qMax(0, h - topInset - bottomInset));
    };
    auto preferred = layoutFor(width, capHeight);
    const int naturalHeight = preferred.overflow ? capHeight : topInset + preferred.textHeight + bottomInset;
    const int height = qMin(capHeight, qMax(preferredHeight_, naturalHeight));
    preferredHeight_ = height;

    struct Region { Side side; QRect room; };
    const std::array regions{
        Region{Side::Above, QRect(area.left(), area.top(), area.width(), occupied.top() - gap - area.top())},
        Region{Side::Right, QRect(occupied.right() + gap + 1, area.top(), area.right() - occupied.right() - gap, area.height())},
        Region{Side::Left, QRect(area.left(), area.top(), occupied.left() - gap - area.left(), area.height())},
        Region{Side::Below, QRect(area.left(), occupied.bottom() + gap + 1, area.width(), area.bottom() - occupied.bottom() - gap)}
    };
    const Region* choice = nullptr;
    double bestCoverage = -1;
    for (const auto& region : regions) {
        if (region.room.width() < minWidth || region.room.height() < topInset + bottomInset + qCeil(metrics.height())) {
            continue;
        }
        const double coverage = qMin(width, region.room.width()) * qMin(height, region.room.height());
        if (coverage > bestCoverage || (coverage == bestCoverage && region.side == side_)) {
            choice = &region;
            bestCoverage = coverage;
        }
    }
    // A pet that uses almost the entire screen may leave no usable region beside it.
    const QRect room = choice ? choice->room : area;
    side_ = choice ? choice->side : Side::Above;
    const QSize size(qMin(width, room.width()), qMin(height, room.height()));
    if (size.width() != width || size.height() < height) {
        preferred = layoutFor(size.width(), size.height());
    }

    if (preferred.overflow) {
        preferred = layoutFor(size.width(), size.height() - footer);
        preferred.overflow = true;
    }
    *preview_ = std::move(preferred);
    QPoint position(pet.center().x() - size.width() / 2, pet.center().y() - size.height() / 2);
    switch (side_) {
    case Side::Above: position.setY(room.bottom() + 1 - size.height()); break;
    case Side::Right: position.setX(room.left()); break;
    case Side::Left: position.setX(room.right() + 1 - size.width()); break;
    case Side::Below: position.setY(room.top()); break;
    }
    position = positionIn(room, size, position);
    tailAnchor_ = pet.center() - position;
    setGeometry(QRect(position, size));
    update();
    syncButton();
}

void SpeechBubble::syncButton()
{
    if (!isVisible() || !preview_->overflow) {
        readButton_->hide();
        return;
    }
    const QSize size = readButton_->sizeHint();
    const QRect geometry(pos() + QPoint((width() - size.width()) / 2, height() - bottomInset - size.height()), size);
    readButton_->setGeometry(geometry);
    if (readButton_->isHidden()) {
        readButton_->show();
    }
    readButton_->raise();
}

void SpeechBubble::openReader()
{
    if (!reader_) {
        reader_ = std::make_unique<ReplyReader>();
    }
    reader_->setReply(fullMessage_, generating_, !readerFollows_ || !reader_->isVisible());
    readerFollows_ = true;
    if (!reader_->isVisible()) {
        const auto* screen = this->screen();
        if (screen) {
            const QRect area = screen->availableGeometry();
            reader_->resize(reader_->size().boundedTo(area.size()));
            reader_->move(positionIn(area, reader_->size(), area.center() - reader_->rect().center()));
        }
        reader_->show();
    }
    reader_->raise();
    reader_->activateWindow();
}

void SpeechBubble::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    syncButton();
}

void SpeechBubble::hideEvent(QHideEvent* event)
{
    readButton_->hide();
    QWidget::hideEvent(event);
}

void SpeechBubble::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const QRectF body = QRectF(rect()).adjusted(14, 12, -14, -13);
    if (body.isEmpty()) {
        return;
    }
    QPainterPath cloud;
    cloud.addRoundedRect(body, 18, 18);
    const auto addLobe = [&](qreal center, qreal radius, qreal top) {
        QPainterPath lobe;
        lobe.addEllipse(QRectF(width() * center - radius, top, radius * 2, 23));
        cloud = cloud.united(lobe);
    };
    addLobe(.22, 18, 5);
    addLobe(.48, 22, 2);
    addLobe(.75, 17, 6);
    painter.setPen(QPen(QColor(214, 216, 223), 1));
    painter.setBrush(Qt::white);
    painter.drawPath(cloud);

    QPointF root;
    QPointF outward;
    const qreal xInset = qMin(11.0, body.width() / 2);
    const qreal yInset = qMin(11.0, body.height() / 2);
    const qreal x = qBound(body.left() + xInset, qreal(tailAnchor_.x()), body.right() - xInset);
    const qreal y = qBound(body.top() + yInset, qreal(tailAnchor_.y()), body.bottom() - yInset);
    switch (side_) {
    case Side::Above: root = {x, body.bottom()}; outward = {0, 1}; break;
    case Side::Below: root = {x, body.top()}; outward = {0, -1}; break;
    case Side::Right: root = {body.left(), y}; outward = {-1, 0}; break;
    case Side::Left: root = {body.right(), y}; outward = {1, 0}; break;
    }
    painter.drawEllipse(root + outward * 4.5, 4.4, 4.4);
    painter.drawEllipse(root + outward * 10, 2.2, 2.2);

    painter.setPen(QColor("#262631"));
    const bool singleLine = preview_->lines.size() == 1 && !preview_->overflow;
    for (const auto& line : preview_->lines) {
        const qreal offset = singleLine ? qMax(0.0, (width() - 2 * horizontalInset - line.naturalTextWidth()) / 2) : 0;
        line.draw(&painter, QPointF(horizontalInset + offset, topInset));
    }
}
