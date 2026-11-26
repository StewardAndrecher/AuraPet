#include "pet_treats.h"
#include "pet_window.h"

#include <QAbstractButton>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QScreen>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <cmath>

namespace {
constexpr double treatIconSize = 36;
constexpr double pi = 3.141592653589793;
constexpr int effectMs = 830;

bool darkPalette(const QWidget& widget)
{
    return widget.palette().color(QPalette::Window).lightness() < 128;
}

void star(QPainter& painter, QPointF point, double radius, const QColor& color)
{
    QPainterPath rays;
    rays.moveTo(point + QPointF(0, -radius));
    rays.quadTo(point + QPointF(.8, -.8), point + QPointF(radius, 0));
    rays.quadTo(point + QPointF(.8, .8), point + QPointF(0, radius));
    rays.quadTo(point + QPointF(-.8, .8), point + QPointF(-radius, 0));
    rays.quadTo(point + QPointF(-.8, -.8), point + QPointF(0, -radius));
    painter.fillPath(rays, color);
}

void leaf(QPainter& painter, const QColor& light, const QColor& shade)
{
    QPainterPath blade;
    blade.moveTo(0, 7);
    blade.cubicTo(-8, 4, -8, -5, 0, -10);
    blade.cubicTo(8, -5, 8, 4, 0, 7);
    QLinearGradient gradient(-6, -8, 5, 6);
    gradient.setColorAt(0, light);
    gradient.setColorAt(1, shade);
    painter.fillPath(blade, gradient);
    painter.setPen(QPen(QColor("#EFFADF"), .8, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(0, -4), QPointF(0, 6));
}

void drawModeIcon(QPainter& painter, CompanionMode mode, QRectF area, const QColor& color, double elapsed = 0, bool hovered = false)
{
    painter.save();
    painter.translate(area.center());
    const double s = qMin(area.width(), area.height()) / 20.0;
    painter.scale(s, s);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPen pen(color, 1.55, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    switch (mode) {
    case CompanionMode::Auto: {
        const double pulse = hovered ? std::sin(elapsed * 7.0) * 0.6 : 0.0;
        painter.drawEllipse(QPointF(0, 0), 6.2 + pulse * 0.4, 6.2 + pulse * 0.4);
        painter.setPen(Qt::NoPen);
        star(painter, QPointF(0, 0), 3.6 + pulse * 0.35, color);
        star(painter, QPointF(5.5, -5.5), 1.8, color);
        break;
    }
    case CompanionMode::Play: {
        if (hovered) {
            painter.rotate(std::sin(elapsed * 10.0) * 6.0);
        }
        QPainterPath bolt;
        bolt.moveTo(1.2, -7.2);
        bolt.lineTo(-4.8, 0.8);
        bolt.lineTo(0.2, 0.8);
        bolt.lineTo(-1.4, 7.2);
        bolt.lineTo(4.8, -0.9);
        bolt.lineTo(-0.2, -0.9);
        bolt.closeSubpath();
        painter.setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QColor fill = color;
        fill.setAlphaF(0.22);
        painter.setBrush(fill);
        painter.drawPath(bolt);
        break;
    }
    case CompanionMode::Daily: {
        const double r = 3.2;
        painter.drawEllipse(QPointF(0, 0), r, r);
        const double rot = hovered ? elapsed * 0.9 : 0.0;
        for (int i = 0; i < 6; ++i) {
            const double a = rot + i * pi / 3.0;
            painter.drawLine(QPointF(std::cos(a) * 5.2, std::sin(a) * 5.2),
                             QPointF(std::cos(a) * 7.1, std::sin(a) * 7.1));
        }
        break;
    }
    case CompanionMode::Focus: {
        if (hovered) {
            painter.rotate(std::sin(elapsed * 4.5) * 4.0);
        }
        QPainterPath moon;
        moon.moveTo(1.8, -6.5);
        moon.cubicTo(-4.8, -5.6, -6.6, 1.8, -1.6, 5.8);
        moon.cubicTo(2.4, 8.2, 6.8, 5.2, 6.5, 2.2);
        moon.cubicTo(2.4, 4.1, -1.8, 1.4, -1.2, -2.5);
        moon.cubicTo(-0.8, -4.6, 0.4, -5.8, 1.8, -6.5);
        QColor fill = color;
        fill.setAlphaF(0.20);
        painter.setBrush(fill);
        painter.drawPath(moon);
        break;
    }
    }
    painter.restore();
}

void drawActionIcon(QPainter& painter, PetTreat treat, QRectF area, const QColor& color, double elapsed = 0, bool hovered = false)
{
    painter.save();
    painter.translate(area.center());
    const double s = qMin(area.width(), area.height()) / 18.0;
    painter.scale(s, s);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (treat == PetTreat::YarnBall) {
        const double bounce = hovered ? -std::abs(std::sin(elapsed * 9.0)) * 1.4 : 0.0;
        painter.translate(0, bounce);
        painter.setPen(Qt::NoPen);
        star(painter, QPointF(-1.2, 0.5), 5.6, color);
        star(painter, QPointF(4.8, -4.4), 2.5, color);
        star(painter, QPointF(5.2, 4.2), 1.9, color);
    } else {
        painter.setPen(QPen(color, 1.45, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(-6.8, -4.2, 12.2, 8.4), 2.2, 2.2);
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(QRectF(5.6, -1.9, 1.9, 3.8), 0.8, 0.8);
        const double fillRatio = hovered ? (0.45 + 0.55 * (0.5 + 0.5 * std::sin(elapsed * 7.0))) : 0.72;
        QColor fill = color;
        fill.setAlphaF(0.82);
        painter.setBrush(fill);
        painter.drawRoundedRect(QRectF(-5.1, -2.5, 8.8 * fillRatio, 5.0), 1.1, 1.1);
    }
    painter.restore();
}

void drawIcon(QPainter& painter, PetTreat treat, QRectF area, double elapsed = 0, bool hovered = false)
{
    painter.save();
    painter.translate(area.topLeft());
    painter.scale(area.width() / treatIconSize, area.height() / treatIconSize);
    painter.setPen(Qt::NoPen);

    switch (treat) {
    case PetTreat::SlowKibble:
        drawModeIcon(painter, CompanionMode::Focus, QRectF(4, 4, 28, 28), QColor("#8CA6DB"), elapsed, hovered);
        break;
    case PetTreat::HighKibble:
        drawModeIcon(painter, CompanionMode::Play, QRectF(4, 4, 28, 28), QColor("#F0BA63"), elapsed, hovered);
        break;
    case PetTreat::YarnBall: {
        const QPointF center(16.3, 17.2);
        QPainterPath ball;
        ball.addEllipse(center, 10.8, 10.5);
        QRadialGradient wool(QPointF(12, 12), 17);
        wool.setColorAt(0, QColor("#FFF0F4"));
        wool.setColorAt(.56, QColor("#E7AFC7"));
        wool.setColorAt(1, QColor("#9DBBD8"));
        painter.fillPath(ball, wool);
        painter.save();
        painter.setClipPath(ball);
        painter.setPen(QPen(QColor("#FFF3F5"), 1.05));
        for (int i = 0; i < 4; ++i) {
            const double x = 6 + 4 * i;
            QPainterPath thread(QPointF(x, 5));
            thread.cubicTo(x - 3, 14, x + 12, 23, x + 14, 28);
            painter.drawPath(thread);
        }
        painter.setPen(QPen(QColor("#B988B7"), 1.05));
        for (int i = 0; i < 3; ++i) {
            const double y = 15 + 4 * i;
            QPainterPath thread(QPointF(12, y + 6));
            thread.quadTo(20, y - 5, 29, y - 7);
            painter.drawPath(thread);
        }
        painter.restore();
        const double wag = hovered ? std::sin(elapsed * 16) * 2.8 : 0;
        QPainterPath tail(QPointF(22, 25));
        tail.cubicTo(24, 32, 33, 30 - wag, 30, 26 + wag);
        painter.setPen(QPen(QColor("#C084AA"), 1.3, Qt::SolidLine, Qt::RoundCap));
        painter.drawPath(tail);
        break;
    }
    case PetTreat::Catnip:
        painter.translate(17, 21);
        painter.rotate(-39);
        leaf(painter, QColor("#E6F6BD"), QColor("#80B389"));
        painter.rotate(77);
        painter.translate(5, -4);
        leaf(painter, QColor("#F1F9CD"), QColor("#92C891"));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#FAFFEB"));
        painter.drawEllipse(QPointF(2, -4), 1.7, 1.9);
        break;
    }
    painter.restore();
}

class ModeSegmentButton : public QAbstractButton {
public:
    ModeSegmentButton(CompanionMode mode, QWidget* parent) : QAbstractButton(parent), mode_(mode)
    {
        setText(companionModeShortName(mode));
        setAccessibleName(companionModeName(mode));
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setCursor(Qt::PointingHandCursor);
        hover_.setInterval(32);
        connect(&hover_, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    }

    QSize sizeHint() const override
    {
        return {qMax(58, fontMetrics().horizontalAdvance(text()) + 14), 48};
    }

    void setSelected(bool selected)
    {
        selected_ = selected;
        setAccessibleDescription((selected ? QStringLiteral("当前模式。") : QString()) + companionModeDescription(mode_));
        update();
    }

    std::function<void(bool)> hoverChanged;

protected:
    void enterEvent(QEnterEvent* event) override
    {
        entered_.start();
        hover_.start();
        update();
        if (hoverChanged) hoverChanged(true);
        QAbstractButton::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        hover_.stop();
        update();
        if (hoverChanged) hoverChanged(false);
        QAbstractButton::leaveEvent(event);
    }

    void hideEvent(QHideEvent* event) override
    {
        hover_.stop();
        QAbstractButton::hideEvent(event);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool dark = darkPalette(*this);
        const bool hovered = underMouse();
        const QRectF tile = QRectF(rect()).adjusted(1, 1, -1, -1);

        if (selected_) {
            painter.setBrush(dark ? QColor(232, 176, 92, hovered ? 56 : 38)
                                  : QColor(218, 152, 56, hovered ? 48 : 34));
            painter.setPen(QPen(dark ? QColor(232, 176, 92, 190) : QColor(204, 138, 42, 170), 1.0));
            painter.drawRoundedRect(tile, 8, 8);
        } else if (hovered || hasFocus()) {
            painter.setBrush(dark ? QColor(255, 255, 255, isDown() ? 28 : 16)
                                  : QColor(0, 0, 0, isDown() ? 18 : 10));
            painter.setPen(hasFocus()
                ? QPen(dark ? QColor(232, 176, 92, 140) : QColor(204, 138, 42, 140), 1.0)
                : QPen(Qt::NoPen));
            painter.drawRoundedRect(tile, 8, 8);
        }

        const QColor iconColor = selected_
            ? (dark ? QColor("#F7CB7A") : QColor("#B97518"))
            : (dark ? QColor("#C6CBD6") : QColor("#4A505C"));
        const double size = isDown() ? 17.5 : 19.5;
        const bool animated = hover_.isActive();
        drawModeIcon(painter, mode_, QRectF((width() - size) / 2.0, 15.0 - size / 2.0, size, size),
                     iconColor, animated ? entered_.elapsed() / 1000.0 : 0.0, animated);

        if (selected_) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(dark ? QColor("#F0BA63") : QColor("#D4902A"));
            painter.drawEllipse(QPointF(width() - 7.5, 7.5), 2.6, 2.6);
        }

        QFont f = font();
        if (f.pointSizeF() > 9.0) {
            f.setPointSizeF(f.pointSizeF() - 1.0);
        }
        if (selected_) {
            f.setWeight(QFont::DemiBold);
        }
        painter.setFont(f);
        painter.setPen(selected_
            ? (dark ? QColor("#FCE3B4") : QColor("#8A5410"))
            : (dark ? QColor("#DFE3EB") : QColor("#2D3038")));
        painter.drawText(QRect(0, 28, width(), 16), Qt::AlignCenter, text());
    }

private:
    CompanionMode mode_;
    bool selected_ = false;
    QTimer hover_;
    QElapsedTimer entered_;
};

class QuickActionPill : public QAbstractButton {
public:
    QuickActionPill(PetTreat treat, const QString& label, const QString& deltaTag, QWidget* parent)
        : QAbstractButton(parent), treat_(treat), deltaTag_(deltaTag)
    {
        setText(label);
        setAccessibleName(label);
        setAccessibleDescription(treatDescription(treat));
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setCursor(Qt::PointingHandCursor);
        hover_.setInterval(32);
        connect(&hover_, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    }

    QSize sizeHint() const override
    {
        return {116, 30};
    }

    std::function<void(bool)> hoverChanged;

protected:
    void enterEvent(QEnterEvent* event) override
    {
        entered_.start();
        hover_.start();
        update();
        if (hoverChanged) hoverChanged(true);
        QAbstractButton::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        hover_.stop();
        update();
        if (hoverChanged) hoverChanged(false);
        QAbstractButton::leaveEvent(event);
    }

    void hideEvent(QHideEvent* event) override
    {
        hover_.stop();
        QAbstractButton::hideEvent(event);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool dark = darkPalette(*this);
        const bool hovered = underMouse();
        const QRectF pill = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

        QColor bg = dark
            ? QColor(255, 255, 255, isDown() ? 30 : (hovered ? 22 : 11))
            : QColor(0, 0, 0, isDown() ? 20 : (hovered ? 13 : 6));
        QColor border = dark
            ? QColor(255, 255, 255, hovered ? 38 : 18)
            : QColor(0, 0, 0, hovered ? 28 : 14);
        painter.setBrush(bg);
        painter.setPen(QPen(border, 1.0));
        painter.drawRoundedRect(pill, 7, 7);

        const QColor accent = treat_ == PetTreat::YarnBall
            ? (dark ? QColor("#F0A8C8") : QColor("#C25B88"))
            : (dark ? QColor("#98D89E") : QColor("#3A9149"));
        const bool animated = hover_.isActive();
        drawActionIcon(painter, treat_, QRectF(9, (height() - 15) / 2.0, 15, 15),
                       accent, animated ? entered_.elapsed() / 1000.0 : 0.0, animated);

        QFont labelFont = font();
        if (labelFont.pointSizeF() > 9.0) {
            labelFont.setPointSizeF(labelFont.pointSizeF() - 1.0);
        }
        painter.setFont(labelFont);
        painter.setPen(dark ? QColor("#E6E9F0") : QColor("#252830"));
        painter.drawText(QRect(28, 0, width() - 62, height()), Qt::AlignVCenter | Qt::AlignLeft, text());

        QFont tagFont = labelFont;
        if (tagFont.pointSizeF() > 8.0) {
            tagFont.setPointSizeF(tagFont.pointSizeF() - 1.5);
        }
        painter.setFont(tagFont);
        painter.setPen(accent);
        painter.drawText(QRect(0, 0, width() - 9, height()), Qt::AlignVCenter | Qt::AlignRight, deltaTag_);
    }

private:
    PetTreat treat_;
    QString deltaTag_;
    QTimer hover_;
    QElapsedTimer entered_;
};
}

QString companionModeName(CompanionMode mode)
{
    switch (mode) {
    case CompanionMode::Auto: return QStringLiteral("智能自动");
    case CompanionMode::Play: return QStringLiteral("互动游玩");
    case CompanionMode::Daily: return QStringLiteral("日常陪伴");
    case CompanionMode::Focus: return QStringLiteral("静伴专注");
    }
    return QStringLiteral("智能自动");
}

QString companionModeShortName(CompanionMode mode)
{
    switch (mode) {
    case CompanionMode::Auto: return QStringLiteral("自动");
    case CompanionMode::Play: return QStringLiteral("游玩");
    case CompanionMode::Daily: return QStringLiteral("日常");
    case CompanionMode::Focus: return QStringLiteral("静伴");
    }
    return QStringLiteral("自动");
}

QString companionModeDescription(CompanionMode mode)
{
    switch (mode) {
    case CompanionMode::Auto:
        return QStringLiteral("自动：跟随活跃窗口与全屏状态自动切换陪伴节奏");
    case CompanionMode::Play:
        return QStringLiteral("游玩：高频移动与互动发言（每 2–4 分钟），适合陪玩");
    case CompanionMode::Daily:
        return QStringLiteral("日常：适中节奏，偶尔走动与轻问候（每 12–20 分钟）");
    case CompanionMode::Focus:
        return QStringLiteral("静伴：低频小范围微动，关闭主动打扰与弹窗");
    }
    return {};
}

QString effectiveRhythmName(EffectiveRhythm rhythm)
{
    switch (rhythm) {
    case EffectiveRhythm::Play: return QStringLiteral("游玩中");
    case EffectiveRhythm::Daily: return QStringLiteral("日常中");
    case EffectiveRhythm::Focus: return QStringLiteral("静伴中");
    }
    return QStringLiteral("日常中");
}

QString treatName(PetTreat treat)
{
    switch (treat) {
    case PetTreat::SlowKibble: return QStringLiteral("静伴专注");
    case PetTreat::HighKibble: return QStringLiteral("互动游玩");
    case PetTreat::YarnBall: return QStringLiteral("逗一逗");
    case PetTreat::Catnip: return QStringLiteral("充个电");
    }
    return {};
}

QString treatDescription(PetTreat treat)
{
    switch (treat) {
    case PetTreat::SlowKibble:
        return companionModeDescription(CompanionMode::Focus);
    case PetTreat::HighKibble:
        return companionModeDescription(CompanionMode::Play);
    case PetTreat::YarnBall:
        return QStringLiteral("逗一逗：触发互动动作并消耗最多 %1 点活跃度").arg(yarnActivityCost);
    case PetTreat::Catnip:
        return QStringLiteral("充个电：快速补充最多 %1 点活跃度（上限 100）").arg(catnipActivityGain);
    }
    return {};
}

QIcon treatIcon(PetTreat treat)
{
    QPixmap image(72, 72);
    image.setDevicePixelRatio(2);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    drawIcon(painter, treat, QRectF(0, 0, treatIconSize, treatIconSize));
    painter.end();
    return QIcon(image);
}

TreatCard::TreatCard()
{
    setObjectName(QStringLiteral("petTreatCard"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 10, 14, 9);
    layout->setSpacing(6);

    auto* headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(2, 0, 2, 0);
    headerRow->setSpacing(6);

    title_ = new QLabel(QStringLiteral("陪伴模式"), this);
    auto heading = title_->font();
    heading.setBold(true);
    if (heading.pointSizeF() > 9.5) {
        heading.setPointSizeF(heading.pointSizeF() - 0.5);
    }
    title_->setFont(heading);
    headerRow->addWidget(title_);
    headerRow->addStretch(1);

    statusBadge_ = new QLabel(this);
    auto badgeFont = statusBadge_->font();
    if (badgeFont.pointSizeF() > 8.5) {
        badgeFont.setPointSizeF(badgeFont.pointSizeF() - 2.0);
    }
    statusBadge_->setFont(badgeFont);
    headerRow->addWidget(statusBadge_);
    layout->addLayout(headerRow);

    auto* modesRow = new QHBoxLayout;
    modesRow->setContentsMargins(0, 0, 0, 0);
    modesRow->setSpacing(4);
    for (size_t index = 0; index < companionModes.size(); ++index) {
        const auto mode = companionModes[index];
        auto* button = new ModeSegmentButton(mode, this);
        modeButtons_[index] = button;
        modesRow->addWidget(button, 1);
        connect(button, &QAbstractButton::clicked, this, [this, mode] {
            if (modeSelected) {
                modeSelected(mode);
            }
        });
        const QString key = QStringLiteral("mode_%1").arg(static_cast<int>(mode));
        button->hoverChanged = [this, mode, key](bool entered) {
            if (entered) {
                hoveredKey_ = key;
                showHintText(companionModeDescription(mode));
            } else if (hoveredKey_ == key) {
                hoveredKey_.clear();
                showHintText({});
            }
        };
    }
    layout->addLayout(modesRow);

    auto* quickRow = new QHBoxLayout;
    quickRow->setContentsMargins(0, 1, 0, 0);
    quickRow->setSpacing(6);
    auto* teaseBtn = new QuickActionPill(
        PetTreat::YarnBall, QStringLiteral("逗一逗"), QStringLiteral("-%1").arg(yarnActivityCost), this);
    auto* rechargeBtn = new QuickActionPill(
        PetTreat::Catnip, QStringLiteral("充个电"), QStringLiteral("+%1").arg(catnipActivityGain), this);
    teaseButton_ = teaseBtn;
    rechargeButton_ = rechargeBtn;
    quickRow->addWidget(teaseBtn, 1);
    quickRow->addWidget(rechargeBtn, 1);
    connect(teaseBtn, &QAbstractButton::clicked, this, [this] {
        if (picked) picked(PetTreat::YarnBall);
    });
    connect(rechargeBtn, &QAbstractButton::clicked, this, [this] {
        if (picked) picked(PetTreat::Catnip);
    });
    teaseBtn->hoverChanged = [this](bool entered) {
        const QString key = QStringLiteral("act_tease");
        if (entered) {
            hoveredKey_ = key;
            showHintText(treatDescription(PetTreat::YarnBall));
        } else if (hoveredKey_ == key) {
            hoveredKey_.clear();
            showHintText({});
        }
    };
    rechargeBtn->hoverChanged = [this](bool entered) {
        const QString key = QStringLiteral("act_recharge");
        if (entered) {
            hoveredKey_ = key;
            showHintText(treatDescription(PetTreat::Catnip));
        } else if (hoveredKey_ == key) {
            hoveredKey_.clear();
            showHintText({});
        }
    };
    layout->addLayout(quickRow);

    footer_ = new QStackedWidget(this);
    auto caption = font();
    if (caption.pointSizeF() > 8.5) {
        caption.setPointSizeF(caption.pointSizeF() - 2.0);
    }
    hint_ = new QLabel(QStringLiteral("悬停查看节奏说明 · 专注计时自动切静伴"), footer_);
    hint_->setFont(caption);
    hint_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    details_ = new QLabel(footer_);
    details_->setObjectName(QStringLiteral("petTreatDetails"));
    details_->setTextFormat(Qt::PlainText);
    details_->setFont(caption);
    details_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    const auto metrics = details_->fontMetrics();
    footer_->setFixedHeight(metrics.lineSpacing() + 2);
    footer_->addWidget(hint_);
    footer_->addWidget(details_);
    layout->addWidget(footer_);

    auto* opacity = new QGraphicsOpacityEffect(details_);
    opacity->setOpacity(1);
    details_->setGraphicsEffect(opacity);
    fade_ = new QPropertyAnimation(opacity, "opacity", details_);
    fade_->setDuration(110);
    fade_->setStartValue(0.0);
    fade_->setEndValue(1.0);
    setCompanionState(CompanionMode::Auto, EffectiveRhythm::Daily);
}

void TreatCard::updateThemeColors(bool force)
{
    const bool dark = darkPalette(*this);
    if (!force && themeInitialized_ && dark == lastDark_) {
        return;
    }
    themeInitialized_ = true;
    lastDark_ = dark;
    title_->setStyleSheet(dark ? QStringLiteral("color: #EAECEF;") : QStringLiteral("color: #20232A;"));
    const bool accented = !overrideTag_.isEmpty() || effectiveRhythm_ == EffectiveRhythm::Play || userMode_ == CompanionMode::Auto;
    if (accented) {
        statusBadge_->setStyleSheet(dark
            ? QStringLiteral("QLabel { color: #FAD99B; background: rgba(232,176,92,0.16);"
                             "border: 1px solid rgba(232,176,92,0.34); border-radius: 6px; padding: 1px 6px; }")
            : QStringLiteral("QLabel { color: #8A5410; background: rgba(218,152,56,0.15);"
                             "border: 1px solid rgba(204,138,42,0.32); border-radius: 6px; padding: 1px 6px; }"));
    } else {
        statusBadge_->setStyleSheet(dark
            ? QStringLiteral("QLabel { color: #9CA3AF; background: rgba(255,255,255,0.06);"
                             "border-radius: 6px; padding: 1px 6px; }")
            : QStringLiteral("QLabel { color: #5C626E; background: rgba(0,0,0,0.05);"
                             "border-radius: 6px; padding: 1px 6px; }"));
    }
    const QString muted = dark ? QStringLiteral("color: #9399A6;") : QStringLiteral("color: #6B7280;");
    hint_->setStyleSheet(muted);
    details_->setStyleSheet(dark ? QStringLiteral("color: #D2D6DF;") : QStringLiteral("color: #374151;"));
}

void TreatCard::setLoaded(std::optional<PetTreat> treat)
{
    if (!treat) {
        setCompanionState(CompanionMode::Daily, EffectiveRhythm::Daily);
    } else if (*treat == PetTreat::HighKibble) {
        setCompanionState(CompanionMode::Play, EffectiveRhythm::Play);
    } else if (*treat == PetTreat::SlowKibble) {
        setCompanionState(CompanionMode::Focus, EffectiveRhythm::Focus);
    }
}

void TreatCard::setCompanionState(CompanionMode userMode, EffectiveRhythm effectiveRhythm, const QString& overrideTag)
{
    userMode_ = userMode;
    effectiveRhythm_ = effectiveRhythm;
    overrideTag_ = overrideTag;
    if (!overrideTag.isEmpty()) {
        statusBadge_->setText(overrideTag);
    } else if (userMode == CompanionMode::Auto) {
        statusBadge_->setText(QStringLiteral("自动 · %1").arg(effectiveRhythmName(effectiveRhythm)));
    } else {
        statusBadge_->setText(QStringLiteral("%1 · 生效中").arg(companionModeName(userMode)));
    }
    for (size_t index = 0; index < companionModes.size(); ++index) {
        static_cast<ModeSegmentButton*>(modeButtons_[index])->setSelected(userMode == companionModes[index]);
    }
    updateThemeColors(true);
}

void TreatCard::showHintText(const QString& text)
{
    if (!text.isEmpty()) {
        const bool wasShowing = footer_->currentWidget() == details_;
        const auto metrics = details_->fontMetrics();
        const int maxW = qMax(200, footer_->width() - 2);
        details_->setText(metrics.elidedText(text, Qt::ElideRight, maxW));
        footer_->setCurrentWidget(details_);
        if (!wasShowing) {
            fade_->start();
        }
    } else {
        fade_->stop();
        footer_->setCurrentWidget(hint_);
    }
}

void TreatCard::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
        updateThemeColors(false);
    }
    QWidget::changeEvent(event);
}

void TreatCard::showEvent(QShowEvent* event)
{
    updateThemeColors(false);
    QWidget::showEvent(event);
}

void TreatCard::hideEvent(QHideEvent* event)
{
    hoveredKey_.clear();
    showHintText({});
    QWidget::hideEvent(event);
}

void TreatCard::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF card = QRectF(rect()).adjusted(6, 2, -6, -2);
    const bool dark = darkPalette(*this);
    painter.setBrush(dark ? QColor("#2C2E35") : QColor("#FFFFFF"));
    painter.setPen(QPen(dark ? QColor(255, 255, 255, 22) : QColor(0, 0, 0, 18), 1.0));
    painter.drawRoundedRect(card, 10, 10);
}

TreatEffect::TreatEffect(PetWindow& pet)
    : QWidget(&pet, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                   | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput), pet_(pet)
{
    setObjectName(QStringLiteral("petTreatEffect"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setFixedSize(172, 112);
    pet_.installEventFilter(this);
    frames_.setInterval(16);
    connect(&frames_, &QTimer::timeout, this, [this] {
        if (elapsed_.elapsed() >= effectMs) {
            clear();
        } else {
            update();
        }
    });
}

void TreatEffect::play(PetTreat treat)
{
    if (!pet_.isVisible()) {
        return;
    }
    treat_ = treat;
    elapsed_.restart();
    syncPosition();
    show();
    raise();
    update();
    frames_.start();
}

void TreatEffect::clear()
{
    frames_.stop();
    hide();
}

void TreatEffect::syncPosition()
{
    const QRect pet = pet_.frameGeometry();
    const int y = treat_ == PetTreat::YarnBall ? pet.top() + pet.height() * .72 : pet.top() + pet.height() * .27;
    const QPoint proposed(pet.center().x() - width() / 2, y - height() / 2);
    const auto* screen = pet_.screen();
    if (!screen) {
        move(proposed);
        return;
    }
    const QRect work = screen->availableGeometry();
    move(qBound(work.left(), proposed.x(), qMax(work.left(), work.right() + 1 - width())),
         qBound(work.top(), proposed.y(), qMax(work.top(), work.bottom() + 1 - height())));
}

bool TreatEffect::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == &pet_) {
        if (event->type() == QEvent::Hide) {
            clear();
        } else if (isVisible() && (event->type() == QEvent::Move || event->type() == QEvent::Resize)) {
            syncPosition();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TreatEffect::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const double progress = qBound(0.0, elapsed_.elapsed() / double(effectMs), 1.0);
    const double alpha = qBound(0.0, (1 - progress) * 2.1, 1.0);
    if (treat_ == PetTreat::YarnBall) {
        const double roll = qMin(1.0, progress / .52);
        const QPointF spot(39 + 78 * roll, 70 - 22 * std::sin(roll * pi));
        if (progress < .60) {
            painter.save();
            painter.setOpacity(qBound(0.0, (.60 - progress) * 12, 1.0));
            painter.translate(spot);
            painter.rotate(roll * 265);
            drawIcon(painter, PetTreat::YarnBall, QRectF(-19, -19, 38, 38));
            painter.restore();
        }
        if (progress > .42) {
            painter.setOpacity(alpha * qMin(1.0, (progress - .42) * 11));
            for (int i = 0; i < 6; ++i) {
                const double angle = i * pi / 3 + .35;
                const double spread = 10 + (progress - .42) * 48;
                star(painter, QPointF(117, 68) + QPointF(std::cos(angle), std::sin(angle)) * spread,
                     2.4 + i % 3, i % 2 ? QColor("#FFF3DB") : QColor("#F8D7E9"));
            }
        }
    } else {
        for (int i = 0; i < 5; ++i) {
            const double t = qBound(0.0, (progress - i * .045) * 1.26, 1.0);
            painter.save();
            painter.setOpacity(alpha * qMin(1.0, t * 9));
            painter.translate(32 + i * 26 + 7 * std::sin(t * 5 + i), 18 + 70 * t - 5 * (i % 2));
            painter.rotate(-50 + i * 23 + 60 * std::sin(t * 3.4));
            painter.scale(.64 + .12 * (i % 3), .64 + .12 * (i % 3));
            leaf(painter, QColor("#EFFBDC"), QColor("#95CE9D"));
            painter.restore();
        }
    }
}
