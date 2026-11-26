#include "speech_bubble.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QTextLayout>
#include <QtMath>

#include <algorithm>
#include <array>
#include <vector>

namespace {
constexpr auto floating = Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint;
constexpr int horizontalInset = 31;
constexpr int topInset = 22;
constexpr int bottomInset = 22;
constexpr int minWidth = 142;
constexpr int gap = 8;

QPoint positionIn(const QRect& area, QSize size, QPoint preferred)
{
    return {qBound(area.left(), preferred.x(), qMax(area.left(), area.right() + 1 - size.width())),
            qBound(area.top(), preferred.y(), qMax(area.top(), area.bottom() + 1 - size.height()))};
}
}

struct SpeechBubble::Preview {
    std::vector<std::unique_ptr<QTextLayout>> layouts;
    std::vector<QTextLine> lines;
    QFont font;
    int width = 0;
    int textHeight = 0;

    void layout(const QStringList& paragraphs, const QFont& requestedFont, int requestedWidth)
    {
        if (requestedWidth == width && requestedFont == font) {
            return;
        }
        lines.clear();
        layouts.clear();
        width = requestedWidth;
        font = requestedFont;
        textHeight = 0;
        QTextOption options;
        options.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        for (const auto& paragraph : paragraphs) {
            auto layout = std::make_unique<QTextLayout>(paragraph.isEmpty() ? QStringLiteral(" ") : paragraph, font);
            layout->setTextOption(options);
            layout->beginLayout();
            for (auto line = layout->createLine(); line.isValid(); line = layout->createLine()) {
                line.setLineWidth(width);
                line.setPosition(QPointF(0, textHeight));
                textHeight += qCeil(line.height());
                lines.push_back(line);
            }
            layout->endLayout();
            layouts.push_back(std::move(layout));
        }
    }
};

SpeechBubble::SpeechBubble(QWidget* pet)
    : QWidget(pet, floating | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput),
      preview_(std::make_unique<Preview>()), narrowPreview_(std::make_unique<Preview>())
{
    setObjectName(QStringLiteral("petSpeechBubble"));
    setAccessibleName(QStringLiteral("桌宠发言"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    scrollAnimation_.setEasingCurve(QEasingCurve::OutCubic);
    connect(&scrollAnimation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        scrollOffset_ = value.toReal();
        update();
    });
}

SpeechBubble::~SpeechBubble() = default;

void SpeechBubble::beginReply()
{
    setMessage({});
    scrollAnimation_.stop();
    scrollOffset_ = scrollTarget_ = 0;
    preferredWidth_ = preferredHeight_ = 0;
    side_ = Side::Above;
}

void SpeechBubble::setMessage(const QString& text)
{
    if (message_ == text) {
        return;
    }
    message_ = text;
    setAccessibleDescription(text);
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(u'\r', u'\n');
    paragraphs_ = normalized.split(u'\n');
    preview_->width = narrowPreview_->width = 0;
    displayed_ = nullptr;
}

void SpeechBubble::scrollTo(qreal offset)
{
    if (scrollTarget_ == offset) {
        return;
    }
    scrollAnimation_.stop();
    scrollTarget_ = offset;
    if (offset <= scrollOffset_) {
        scrollOffset_ = offset;
        return;
    }
    scrollAnimation_.setDuration(qBound(125, 115 + qRound((offset - scrollOffset_) / 3), 340));
    scrollAnimation_.setStartValue(scrollOffset_);
    scrollAnimation_.setEndValue(offset);
    scrollAnimation_.start();
}

int SpeechBubble::visibleLines() const
{
    if (!displayed_) {
        return 0;
    }
    const int area = qMax(0, height() - topInset - bottomInset);
    const auto& lines = displayed_->lines;
    const auto first = std::partition_point(lines.cbegin(), lines.cend(),
        [this](const QTextLine& line) { return line.y() + line.height() <= scrollTarget_; });
    const auto last = std::partition_point(first, lines.cend(),
        [this, area](const QTextLine& line) { return line.y() < scrollTarget_ + area; });
    return static_cast<int>(last - first);
}

void SpeechBubble::place(const QRect& workArea, const QRect& pet, const QRect& occupied)
{
    const auto insetArea = workArea.adjusted(gap, gap, -gap, -gap);
    const QRect area = insetArea.isValid() ? insetArea : workArea;
    const int capWidth = qMax(1, qMin(420, qRound(area.width() * .45)));
    const int capHeight = qMax(1, qMin(260, qRound(area.height() * .35)));
    const QFontMetricsF metrics(font());
    int width = qMin(capWidth, qMax(minWidth, preferredWidth_));
    for (const auto& paragraph : paragraphs_) {
        if (width == capWidth) {
            break;
        }
        width = qMax(width, qMin(capWidth, qCeil(metrics.horizontalAdvance(paragraph)) + 2 * horizontalInset + 2));
    }
    preferredWidth_ = width;
    const auto textWidth = [](int w) { return qMax(1, w - 2 * horizontalInset); };
    preview_->layout(paragraphs_, font(), textWidth(width));
    const int naturalHeight = topInset + preview_->textHeight + bottomInset;
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
    displayed_ = preview_.get();
    if (size.width() != width) {
        narrowPreview_->layout(paragraphs_, font(), textWidth(size.width()));
        displayed_ = narrowPreview_.get();
    }
    const int viewportHeight = qMax(0, size.height() - topInset - bottomInset);
    scrollTo(qMax(0, displayed_->textHeight - viewportHeight));
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
}

void SpeechBubble::placeWithTransitOffset(const QRect& workArea, const QRect& landingPetRect, const QPoint& liveOffset)
{
    place(workArea, landingPetRect, landingPetRect);
    if (!liveOffset.isNull()) {
        move(pos() + liveOffset);
    }
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

    if (!displayed_) {
        return;
    }
    const QRect viewport = rect().adjusted(horizontalInset, topInset, -horizontalInset, -bottomInset);
    if (viewport.isEmpty()) {
        return;
    }
    painter.setClipRect(viewport);
    painter.setPen(QColor("#262631"));
    const auto& lines = displayed_->lines;
    const bool singleLine = lines.size() == 1;
    const auto first = std::partition_point(lines.cbegin(), lines.cend(),
        [this](const QTextLine& line) { return line.y() + line.height() <= scrollOffset_; });
    for (auto line = first; line != lines.cend() && line->y() < scrollOffset_ + viewport.height(); ++line) {
        const qreal offset = singleLine ? qMax(0.0, (viewport.width() - line->naturalTextWidth()) / 2) : 0;
        line->draw(&painter, QPointF(viewport.left() + offset, viewport.top() - scrollOffset_));
    }
}
