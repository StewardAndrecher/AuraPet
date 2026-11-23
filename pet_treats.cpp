#include "pet_treats.h"
#include "pet_motion_rules.h"
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

struct TreatDetails {
    QString effect;
    QString usage;
};

TreatDetails detailsFor(PetTreat treat)
{
    switch (treat) {
    case PetTreat::SlowKibble:
    case PetTreat::HighKibble: {
        const auto rates = kibbleRates(treat == PetTreat::SlowKibble ? Kibble::Slow : Kibble::High);
        return {QStringLiteral("待机 +%1 点/秒，运动 −%2 点/秒").arg(rates.recovery).arg(rates.consumption),
                QStringLiteral("点击装载，再点当前猫粮可卸下")};
    }
    case PetTreat::YarnBall:
        return {QStringLiteral("立即消耗最多 %1 点活跃度").arg(yarnActivityCost),
                QStringLiteral("点击陪玩，活跃度不足时少扣")};
    case PetTreat::Catnip:
        return {QStringLiteral("立即补充最多 %1 点活跃度").arg(catnipActivityGain),
                QStringLiteral("点击投喂，最多补到 100 点")};
    }
    return {};
}

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

void drawIcon(QPainter& painter, PetTreat treat, QRectF area, double elapsed = 0, bool hovered = false)
{
    painter.save();
    painter.translate(area.topLeft());
    painter.scale(area.width() / treatIconSize, area.height() / treatIconSize);
    painter.setPen(Qt::NoPen);

    switch (treat) {
    case PetTreat::SlowKibble:
    case PetTreat::HighKibble: {
        const bool high = treat == PetTreat::HighKibble;
        if (hovered && !high) {
            painter.translate(18, 21);
            painter.rotate(std::sin(elapsed * 6) * 5);
            painter.translate(-18, -21);
        }
        const std::array centers{QPointF(11, 23), QPointF(23, 23), QPointF(18, 13)};
        for (const auto& center : centers) {
            QRadialGradient glaze(center - QPointF(2, 2), 8);
            glaze.setColorAt(0, high ? QColor("#FFF3CB") : QColor("#FFF0D9"));
            glaze.setColorAt(1, high ? QColor("#E9A46B") : QColor("#CF9D7B"));
            painter.setPen(QPen(high ? QColor("#E4A478") : QColor("#D5A384"), .7));
            painter.setBrush(glaze);
            painter.drawEllipse(center, high ? 6.5 : 6.1, high ? 6.4 : 4.8);
        }
        if (high) {
            QColor glint("#FFFDF2");
            glint.setAlphaF(hovered ? .65 + .35 * std::sin(elapsed * 10) : .9);
            star(painter, QPointF(28, 10), 4.1, glint);
            star(painter, QPointF(8, 13), 2.4, glint);
        }
        break;
    }
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

class TreatButton : public QAbstractButton {
public:
    TreatButton(PetTreat treat, QWidget* parent) : QAbstractButton(parent), treat_(treat)
    {
        setText(treatName(treat));
        setAccessibleName(text());
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        setCursor(Qt::PointingHandCursor);
        hover_.setInterval(32);
        connect(&hover_, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    }

    QSize sizeHint() const override
    {
        return {qMax(65, fontMetrics().horizontalAdvance(text()) + 12), 58};
    }

    void setLoaded(bool loaded)
    {
        loaded_ = loaded;
        setAccessibleDescription((loaded ? QStringLiteral("生效中。") : QString()) + treatDescription(treat_));
        update();
    }

    std::function<void(bool)> hoverChanged;

protected:
    void enterEvent(QEnterEvent* event) override
    {
        entered_.start();
        hover_.start();
        update();
        hoverChanged(true);
        QAbstractButton::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        hover_.stop();
        update();
        hoverChanged(false);
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
        const QRectF tile = QRectF(rect()).adjusted(1, 1, -1, -1);
        if (underMouse() || hasFocus() || loaded_) {
            painter.setBrush(dark ? QColor(115, 89, 63, 105) : QColor(255, 240, 215, 195));
            painter.setPen(loaded_ || hasFocus()
                ? QPen(dark ? QColor("#CAA574") : QColor("#DBAE75"), .9) : QPen(Qt::NoPen));
            painter.drawRoundedRect(tile, 10, 10);
        }
        const double size = isDown() ? 31 : treatIconSize;
        const bool hovered = hover_.isActive();
        drawIcon(painter, treat_, QRectF((width() - size) / 2, 21 - size / 2, size, size),
                 hovered ? entered_.elapsed() / 1000.0 : 0, hovered);
        if (loaded_) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor("#E9B771"));
            painter.drawEllipse(QPointF(width() - 10, 10), 3.2, 3.2);
            painter.setBrush(QColor("#FFF8E9"));
            painter.drawEllipse(QPointF(width() - 10, 10), 1.6, 1.6);
        }
        painter.setPen(dark ? QColor("#F3E8D9") : QColor("#523F38"));
        painter.drawText(QRect(0, 38, width(), 16), Qt::AlignCenter, text());
    }

private:
    PetTreat treat_;
    bool loaded_ = false;
    QTimer hover_;
    QElapsedTimer entered_;
};
}

QString treatName(PetTreat treat)
{
    switch (treat) {
    case PetTreat::SlowKibble: return QStringLiteral("慢食猫粮");
    case PetTreat::HighKibble: return QStringLiteral("高能猫粮");
    case PetTreat::YarnBall: return QStringLiteral("毛线球");
    case PetTreat::Catnip: return QStringLiteral("猫薄荷");
    }
    return {};
}

QString treatDescription(PetTreat treat)
{
    const auto details = detailsFor(treat);
    return QStringLiteral("效果  %1\n用法  %2").arg(details.effect, details.usage);
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
    layout->setContentsMargins(14, 11, 14, 10);
    layout->setSpacing(4);
    auto* title = new QLabel(QStringLiteral("小食与玩具"), this);
    auto heading = title->font();
    heading.setBold(true);
    title->setFont(heading);
    layout->addWidget(title);

    auto* items = new QHBoxLayout;
    items->setSpacing(2);
    for (size_t index = 0; index < petTreats.size(); ++index) {
        const auto treat = petTreats[index];
        auto* button = new TreatButton(treat, this);
        buttons_[index] = button;
        items->addWidget(button);
        connect(button, &QAbstractButton::clicked, this, [this, treat] { if (picked) picked(treat); });
        button->hoverChanged = [this, treat](bool entered) {
            if (entered) {
                showDetails(treat);
            } else if (hovered_ == treat) {
                showDetails(std::nullopt);
            }
        };
    }
    layout->addLayout(items);

    footer_ = new QStackedWidget(this);
    status_ = new QLabel(footer_);
    auto caption = status_->font();
    if (caption.pointSizeF() > 7) {
        caption.setPointSizeF(caption.pointSizeF() - 1);
    }
    status_->setFont(caption);
    details_ = new QLabel(footer_);
    details_->setObjectName(QStringLiteral("petTreatDetails"));
    details_->setTextFormat(Qt::PlainText);
    details_->setFont(caption);
    const auto metrics = details_->fontMetrics();
    int width = 0;
    for (const auto treat : petTreats) {
        const auto description = treatDescription(treat);
        for (const auto& line : description.split(u'\n')) {
            width = qMax(width, metrics.horizontalAdvance(line));
        }
    }
    footer_->setMinimumWidth(width + 2);
    footer_->setFixedHeight(metrics.lineSpacing() * 2 + 4);
    footer_->addWidget(status_);
    footer_->addWidget(details_);
    layout->addWidget(footer_);

    auto* opacity = new QGraphicsOpacityEffect(details_);
    opacity->setOpacity(1);
    details_->setGraphicsEffect(opacity);
    fade_ = new QPropertyAnimation(opacity, "opacity", details_);
    fade_->setDuration(135);
    fade_->setStartValue(0.0);
    fade_->setEndValue(1.0);
    setLoaded(std::nullopt);
}

void TreatCard::setLoaded(std::optional<PetTreat> treat)
{
    status_->setText(treat ? QStringLiteral("%1 · 生效中").arg(treatName(*treat))
                           : QStringLiteral("猫粮槽空闲"));
    for (size_t index = 0; index < petTreats.size(); ++index) {
        static_cast<TreatButton*>(buttons_[index])->setLoaded(treat == petTreats[index]);
    }
}

void TreatCard::showDetails(std::optional<PetTreat> treat)
{
    if (hovered_ == treat) {
        return;
    }
    const bool wasShowing = hovered_.has_value();
    hovered_ = treat;
    if (treat) {
        details_->setText(treatDescription(*treat));
        footer_->setCurrentWidget(details_);
        if (!wasShowing) {
            fade_->start();
        }
    } else {
        fade_->stop();
        footer_->setCurrentWidget(status_);
    }
}

void TreatCard::hideEvent(QHideEvent* event)
{
    showDetails(std::nullopt);
    QWidget::hideEvent(event);
}

void TreatCard::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF card = QRectF(rect()).adjusted(5, 3, -5, -3);
    const bool dark = darkPalette(*this);
    QLinearGradient glaze(card.topLeft(), card.bottomRight());
    glaze.setColorAt(0, dark ? QColor("#39312D") : QColor("#FFFCF5"));
    glaze.setColorAt(1, dark ? QColor("#302A29") : QColor("#F7ECE3"));
    painter.setBrush(glaze);
    painter.setPen(QPen(dark ? QColor("#59483E") : QColor("#EBD9CB"), 1));
    painter.drawRoundedRect(card, 13, 13);
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
