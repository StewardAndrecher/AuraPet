#include "desktop_pet_manager.h"
#include "action_editor.h"
#include "background_task.h"
#include "focus_session.h"
#include "pet_asset.h"
#include "role_actions.h"
#include "role_library_dialog.h"
#include "role_package.h"
#include "settings_store.h"
#include "voice_training.h"

#include "chat_dialog.h"
#include "mac_companion_services.h"
#include "voice_service_dialog.h"

#ifdef Q_OS_MACOS
#include "import_preview.h"
#endif

#include <QAbstractButton>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLinearGradient>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QProxyStyle>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScopedValueRollback>
#include <QScreen>
#include <QSignalBlocker>
#include <QStyleOption>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <cmath>
#include <utility>

namespace {
const auto reminderKey = QStringLiteral("focus/systemNotifications");

bool isDarkUi(const QPalette& palette)
{
    return palette.color(QPalette::Window).lightness() < 128;
}

enum class MenuGlyph {
    Gallery,
    Companion,
    Actions,
    Dialogue,
    Roam,
    FollowSpace,
    LayerOrder,
    Appearance,
    ClickThrough,
    Dismiss,
    Visibility,
    ImportBackup,
    Settings,
    Quit,
    PetsGroup
};

void paintMenuGlyph(QPainter& p, MenuGlyph glyph, const QColor& stroke)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(stroke, 1.35, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (glyph) {
    case MenuGlyph::Gallery:
        p.drawRoundedRect(QRectF(2.2, 2.2, 4.8, 4.8), 1.3, 1.3);
        p.drawRoundedRect(QRectF(9.0, 2.2, 4.8, 4.8), 1.3, 1.3);
        p.drawRoundedRect(QRectF(2.2, 9.0, 4.8, 4.8), 1.3, 1.3);
        p.drawRoundedRect(QRectF(9.0, 9.0, 4.8, 4.8), 1.3, 1.3);
        break;
    case MenuGlyph::Companion:
        p.drawEllipse(QPointF(8.0, 8.4), 5.4, 5.4);
        p.drawLine(QPointF(8.0, 5.4), QPointF(8.0, 8.5));
        p.drawLine(QPointF(8.0, 8.5), QPointF(10.3, 9.8));
        p.drawLine(QPointF(6.3, 1.8), QPointF(9.7, 1.8));
        break;
    case MenuGlyph::Actions: {
        QPainterPath star;
        star.moveTo(8.0, 2.0);
        star.quadTo(8.8, 6.8, 13.5, 7.6);
        star.quadTo(8.8, 8.4, 8.0, 13.4);
        star.quadTo(7.2, 8.4, 2.5, 7.6);
        star.quadTo(7.2, 6.8, 8.0, 2.0);
        p.drawPath(star);
        p.setPen(Qt::NoPen);
        p.setBrush(stroke);
        p.drawEllipse(QPointF(12.6, 3.3), 1.0, 1.0);
        break;
    }
    case MenuGlyph::Dialogue: {
        QPainterPath bubble;
        bubble.addRoundedRect(QRectF(2.0, 2.6, 12.0, 8.6), 3.2, 3.2);
        p.drawPath(bubble);
        p.drawLine(QPointF(5.5, 11.2), QPointF(4.2, 13.6));
        p.drawLine(QPointF(4.2, 13.6), QPointF(7.6, 11.2));
        p.drawLine(QPointF(5.2, 6.2), QPointF(10.8, 6.2));
        p.drawLine(QPointF(5.2, 8.4), QPointF(9.0, 8.4));
        break;
    }
    case MenuGlyph::Roam:
        p.setPen(Qt::NoPen);
        p.setBrush(stroke);
        p.drawEllipse(QRectF(5.2, 7.8, 5.6, 4.4));
        p.drawEllipse(QRectF(3.3, 5.0, 2.3, 2.9));
        p.drawEllipse(QRectF(6.0, 3.3, 2.1, 2.8));
        p.drawEllipse(QRectF(8.5, 3.3, 2.1, 2.8));
        p.drawEllipse(QRectF(10.6, 5.0, 2.3, 2.9));
        break;
    case MenuGlyph::FollowSpace:
        p.drawRoundedRect(QRectF(1.8, 3.4, 7.4, 6.2), 1.4, 1.4);
        p.drawRoundedRect(QRectF(6.8, 6.4, 7.4, 6.2), 1.4, 1.4);
        p.drawLine(QPointF(5.2, 6.5), QPointF(10.6, 9.5));
        p.drawLine(QPointF(8.6, 9.6), QPointF(10.6, 9.5));
        p.drawLine(QPointF(10.4, 7.5), QPointF(10.6, 9.5));
        break;
    case MenuGlyph::LayerOrder: {
        QPainterPath topPlane;
        topPlane.moveTo(8.0, 2.4);
        topPlane.lineTo(13.8, 5.4);
        topPlane.lineTo(8.0, 8.4);
        topPlane.lineTo(2.2, 5.4);
        topPlane.closeSubpath();
        p.drawPath(topPlane);
        p.drawLine(QPointF(2.5, 8.8), QPointF(8.0, 11.6));
        p.drawLine(QPointF(8.0, 11.6), QPointF(13.5, 8.8));
        p.drawLine(QPointF(2.5, 11.5), QPointF(8.0, 14.2));
        p.drawLine(QPointF(8.0, 14.2), QPointF(13.5, 11.5));
        break;
    }
    case MenuGlyph::Appearance:
        p.drawRoundedRect(QRectF(2.0, 3.0, 12.0, 8.2), 1.8, 1.8);
        p.drawLine(QPointF(5.5, 13.4), QPointF(10.5, 13.4));
        p.drawLine(QPointF(8.0, 11.2), QPointF(8.0, 13.4));
        break;
    case MenuGlyph::ClickThrough: {
        QPen dashed = pen;
        dashed.setStyle(Qt::DashLine);
        p.setPen(dashed);
        p.drawEllipse(QPointF(8.0, 8.0), 5.3, 5.3);
        p.setPen(pen);
        p.drawLine(QPointF(6.5, 6.5), QPointF(11.2, 11.2));
        break;
    }
    case MenuGlyph::Dismiss:
        p.drawRoundedRect(QRectF(2.4, 3.0, 11.2, 10.0), 2.0, 2.0);
        p.drawLine(QPointF(5.6, 8.0), QPointF(10.4, 8.0));
        break;
    case MenuGlyph::Visibility: {
        QPainterPath eye;
        eye.moveTo(1.8, 8.0);
        eye.quadTo(8.0, 3.0, 14.2, 8.0);
        eye.quadTo(8.0, 13.0, 1.8, 8.0);
        p.drawPath(eye);
        p.drawEllipse(QPointF(8.0, 8.0), 1.8, 1.8);
        break;
    }
    case MenuGlyph::ImportBackup:
        p.drawRoundedRect(QRectF(2.4, 6.8, 11.2, 6.6), 1.6, 1.6);
        p.drawLine(QPointF(8.0, 2.2), QPointF(8.0, 9.6));
        p.drawLine(QPointF(5.6, 4.6), QPointF(8.0, 2.2));
        p.drawLine(QPointF(10.4, 4.6), QPointF(8.0, 2.2));
        break;
    case MenuGlyph::Settings:
        p.drawEllipse(QPointF(8.0, 8.0), 2.3, 2.3);
        for (int i = 0; i < 6; ++i) {
            const double angle = i * (3.141592653589793 / 3.0);
            const QPointF inner(8.0 + std::cos(angle) * 3.6, 8.0 + std::sin(angle) * 3.6);
            const QPointF outer(8.0 + std::cos(angle) * 5.6, 8.0 + std::sin(angle) * 5.6);
            p.drawLine(inner, outer);
        }
        break;
    case MenuGlyph::Quit:
        p.drawArc(QRectF(3.0, 3.4, 10.0, 10.0), 130 * 16, 280 * 16);
        p.drawLine(QPointF(8.0, 2.0), QPointF(8.0, 7.4));
        break;
    case MenuGlyph::PetsGroup:
        p.drawEllipse(QPointF(6.0, 6.2), 2.2, 2.2);
        p.drawArc(QRectF(2.5, 9.2, 7.0, 4.6), 0, 180 * 16);
        p.drawEllipse(QPointF(11.0, 6.8), 1.8, 1.8);
        p.drawArc(QRectF(8.4, 9.6, 5.4, 3.8), 0, 180 * 16);
        break;
    }
    p.restore();
}

QIcon menuGlyphIcon(MenuGlyph glyph, std::optional<QColor> tint = std::nullopt)
{
    QIcon icon;
    QColor stroke = tint.value_or(QApplication::palette().color(QPalette::WindowText));
    if (!tint) {
        stroke.setAlpha(210);
    }
    for (const int dpr : {1, 2}) {
        QPixmap pixmap(16 * dpr, 16 * dpr);
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        paintMenuGlyph(p, glyph, stroke);
        p.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

class AuraMenuStyle : public QProxyStyle {
public:
    AuraMenuStyle() : QProxyStyle(nullptr) {}

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                       QPainter* painter, const QWidget* widget = nullptr) const override
    {
        if (element == PE_IndicatorArrowRight && option && painter) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            const bool dark = isDarkUi(option->palette);
            const bool selected = option->state.testFlag(State_Selected);
            QColor color = dark ? (selected ? QColor("#FFFFFF") : QColor("#989FA9"))
                                : (selected ? QColor("#181A20") : QColor("#828894"));
            painter->setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            const QPointF c = QRectF(option->rect).center();
            QPainterPath chevron;
            chevron.moveTo(c.x() - 2.0, c.y() - 3.6);
            chevron.lineTo(c.x() + 1.8, c.y());
            chevron.lineTo(c.x() - 2.0, c.y() + 3.6);
            painter->drawPath(chevron);
            painter->restore();
            return;
        }
        if (element == PE_IndicatorMenuCheckMark && option && painter) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            const bool dark = isDarkUi(option->palette);
            QColor color = dark ? QColor("#F0BA63") : QColor("#C68324");
            painter->setPen(QPen(color, 1.75, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            const QPointF c = QRectF(option->rect).center();
            QPainterPath check;
            check.moveTo(c.x() - 3.8, c.y() + 0.2);
            check.lineTo(c.x() - 1.1, c.y() + 3.0);
            check.lineTo(c.x() + 4.1, c.y() - 2.8);
            painter->drawPath(check);
            painter->restore();
            return;
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr,
                    const QWidget* widget = nullptr) const override
    {
        if (metric == PM_SmallIconSize) {
            return 15;
        }
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
};

AuraMenuStyle* sharedMenuStyle()
{
    static auto* style = new AuraMenuStyle;
    return style;
}

QString auraMenuStyleSheet(bool dark)
{
    if (dark) {
        return QStringLiteral(
            "QMenu {"
            "  background-color: #22242A;"
            "  border: 1px solid rgba(255, 255, 255, 0.11);"
            "  border-radius: 11px;"
            "  padding: 5px 0px;"
            "  color: #ECEEF2;"
            "}"
            "QMenu::item {"
            "  background-color: transparent;"
            "  padding: 6px 26px 6px 14px;"
            "  margin: 1px 6px;"
            "  border-radius: 7px;"
            "  color: #ECEEF2;"
            "}"
            "QMenu::item:selected {"
            "  background-color: rgba(255, 255, 255, 0.10);"
            "  color: #FFFFFF;"
            "}"
            "QMenu::item:disabled {"
            "  color: #7E8591;"
            "  background-color: transparent;"
            "}"
            "QMenu::separator {"
            "  height: 1px;"
            "  background-color: rgba(255, 255, 255, 0.08);"
            "  margin: 5px 12px;"
            "}"
            "QMenu::icon {"
            "  padding-left: 6px;"
            "}");
    }
    return QStringLiteral(
        "QMenu {"
        "  background-color: #F7F7F9;"
        "  border: 1px solid rgba(0, 0, 0, 0.11);"
        "  border-radius: 11px;"
        "  padding: 5px 0px;"
        "  color: #1F2229;"
        "}"
        "QMenu::item {"
        "  background-color: transparent;"
        "  padding: 6px 26px 6px 14px;"
        "  margin: 1px 6px;"
        "  border-radius: 7px;"
        "  color: #1F2229;"
        "}"
        "QMenu::item:selected {"
        "  background-color: rgba(0, 0, 0, 0.065);"
        "  color: #111318;"
        "}"
        "QMenu::item:disabled {"
        "  color: #8B919E;"
        "  background-color: transparent;"
        "}"
        "QMenu::separator {"
        "  height: 1px;"
        "  background-color: rgba(0, 0, 0, 0.075);"
        "  margin: 5px 12px;"
        "}"
        "QMenu::icon {"
        "  padding-left: 6px;"
        "}");
}

void assignMenuGlyph(QAction* action, MenuGlyph glyph)
{
    if (!action) {
        return;
    }
    action->setProperty("auraGlyph", static_cast<int>(glyph));
    action->setIcon(menuGlyphIcon(glyph));
}

void applyAuraMenuTheme(QMenu& menu)
{
    menu.setStyle(sharedMenuStyle());
    menu.setMouseTracking(true);
    const auto sync = [&menu] {
        const bool dark = isDarkUi(QApplication::palette());
        const QVariant prev = menu.property("auraLastDark");
        if (!prev.isValid() || prev.toBool() != dark) {
            menu.setProperty("auraLastDark", dark);
            menu.setStyleSheet(auraMenuStyleSheet(dark));
            const QColor iconColor = dark ? QColor("#D6DAE2") : QColor("#3A3E48");
            for (QAction* act : menu.actions()) {
                const QVariant g = act->property("auraGlyph");
                if (g.isValid()) {
                    act->setIcon(menuGlyphIcon(static_cast<MenuGlyph>(g.toInt()), iconColor));
                }
            }
        }
        QTimer::singleShot(0, &menu, [&menu] {
            if (menu.isVisible()) {
                preparePopupWindow(menu);
            }
        });
    };
    sync();
    QObject::connect(&menu, &QMenu::aboutToShow, &menu, sync);
    for (QAction* act : menu.actions()) {
        if (auto* sub = act->menu()) {
            applyAuraMenuTheme(*sub);
        }
    }
}

class QuickGalleryButton : public QPushButton {
public:
    explicit QuickGalleryButton(QWidget* parent = nullptr)
        : QPushButton(QStringLiteral("角色库 ›"), parent)
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setFixedHeight(24);
    }

    QSize sizeHint() const override
    {
        QFont f = font();
        f.setPointSizeF(qMax(9.0, f.pointSizeF() - 1.5));
        f.setBold(true);
        return QSize(QFontMetrics(f).horizontalAdvance(text()) + 18, 24);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool dark = isDarkUi(palette());
        const bool hovered = underMouse();
        const bool pressed = isDown();
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

        QColor bg = dark ? QColor(232, 176, 92, pressed ? 72 : (hovered ? 54 : 34))
                         : QColor(218, 152, 56, pressed ? 62 : (hovered ? 44 : 26));
        QColor border = dark ? QColor(232, 176, 92, hovered ? 190 : 120)
                             : QColor(204, 138, 42, hovered ? 180 : 115);
        painter.setPen(QPen(border, 1.0));
        painter.setBrush(bg);
        painter.drawRoundedRect(box, 7, 7);

        QFont f = font();
        f.setPointSizeF(qMax(9.0, f.pointSizeF() - 1.5));
        f.setBold(true);
        painter.setFont(f);
        painter.setPen(dark ? QColor("#FCE3B4") : QColor("#8A5410"));
        painter.drawText(rect(), Qt::AlignCenter, text());
    }
};

class QuickToggleTile : public QAbstractButton {
public:
    QuickToggleTile(MenuGlyph glyph, const QString& label, const QString& tip, QWidget* parent)
        : QAbstractButton(parent), glyph_(glyph)
    {
        setText(label);
        setToolTip(tip);
        setAccessibleName(label);
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_Hover);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(48);
    }

    QSize sizeHint() const override
    {
        return {qMax(62, fontMetrics().horizontalAdvance(text()) + 12), 48};
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool dark = isDarkUi(palette());
        const bool on = isChecked();
        const bool hovered = underMouse();
        const bool pressed = isDown();
        const QRectF tile = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

        if (on) {
            painter.setBrush(dark ? QColor(232, 176, 92, pressed ? 64 : (hovered ? 48 : 34))
                                  : QColor(218, 152, 56, pressed ? 58 : (hovered ? 44 : 28)));
            painter.setPen(QPen(dark ? QColor(232, 176, 92, hovered ? 210 : 155)
                                     : QColor(204, 138, 42, hovered ? 195 : 145), 1.0));
        } else {
            painter.setBrush(dark ? QColor(255, 255, 255, pressed ? 24 : (hovered ? 16 : 7))
                                  : QColor(0, 0, 0, pressed ? 18 : (hovered ? 11 : 4)));
            painter.setPen(QPen(dark ? QColor(255, 255, 255, hovered ? 36 : 16)
                                     : QColor(0, 0, 0, hovered ? 28 : 14), 1.0));
        }
        painter.drawRoundedRect(tile, 8, 8);

        if (on) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(dark ? QColor("#F0BA63") : QColor("#D4902A"));
            painter.drawEllipse(QPointF(width() - 7.5, 7.5), 2.6, 2.6);
        }

        const QColor glyphColor = on
            ? (dark ? QColor("#FCE3B4") : QColor("#8A5410"))
            : (dark ? QColor("#B4BAC6") : QColor("#555B68"));
        painter.save();
        painter.translate((width() - 16.0) / 2.0, 7.5);
        paintMenuGlyph(painter, glyph_, glyphColor);
        painter.restore();

        QFont f = font();
        if (f.pointSizeF() > 9.0) {
            f.setPointSizeF(f.pointSizeF() - 1.5);
        }
        f.setBold(on);
        painter.setFont(f);
        painter.setPen(on
            ? (dark ? QColor("#FCE3B4") : QColor("#8A5410"))
            : (dark ? QColor("#C8CDD6") : QColor("#3D424D")));
        painter.drawText(QRect(2, 27, width() - 4, 16), Qt::AlignCenter, text());
    }

private:
    MenuGlyph glyph_;
};
}

// Cohesive companion status card at the top of each pet's right-click menu
class PetStatusBanner : public QWidget {
public:
    PetStatusBanner()
    {
        setObjectName(QStringLiteral("petStatusBanner"));
        setMinimumWidth(296);
        auto* root = new QHBoxLayout(this);
        root->setContentsMargins(14, 10, 14, 10);
        root->setSpacing(10);

        avatar_ = new QWidget(this);
        avatar_->setFixedSize(40, 40);
        avatar_->setAttribute(Qt::WA_TransparentForMouseEvents);
        root->addWidget(avatar_);

        auto* infoCol = new QVBoxLayout;
        infoCol->setContentsMargins(0, 0, 0, 0);
        infoCol->setSpacing(3);

        auto* topLine = new QHBoxLayout;
        topLine->setContentsMargins(0, 0, 0, 0);
        topLine->setSpacing(6);

        nameLabel_ = new QLabel(this);
        QFont nameFont = nameLabel_->font();
        nameFont.setBold(true);
        nameLabel_->setFont(nameFont);
        nameLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        topLine->addWidget(nameLabel_, 1);

        galleryButton_ = new QuickGalleryButton(this);
        galleryButton_->setToolTip(QStringLiteral("打开角色画廊，快速切换或同时展示多个桌宠"));
        connect(galleryButton_, &QPushButton::clicked, this, [this] {
            if (openLibraryRequested) {
                openLibraryRequested();
            }
        });
        topLine->addWidget(galleryButton_);
        infoCol->addLayout(topLine);

        metaLabel_ = new QLabel(this);
        QFont metaFont = metaLabel_->font();
        if (metaFont.pointSizeF() > 8.5) {
            metaFont.setPointSizeF(metaFont.pointSizeF() - 1.5);
        }
        metaLabel_->setFont(metaFont);
        metaLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        infoCol->addWidget(metaLabel_);
        root->addLayout(infoCol, 1);
    }

    void refresh(const QPixmap& portrait, const QString& name, double activityPercent,
                 int scalePercent, bool clickThrough, int indexInGroup, int totalPets)
    {
        portrait_ = portrait;
        activity_ = qBound(0.0, activityPercent, 100.0);

        const QFontMetrics fm(nameLabel_->font());
        const QString badge = totalPets > 1 ? QStringLiteral(" (%1/%2)").arg(indexInGroup).arg(totalPets) : QString{};
        const int nameMaxW = qMax(110, width() - 150);
        nameLabel_->setText(fm.elidedText(name, Qt::ElideMiddle, nameMaxW) + badge);

        QString meta = QStringLiteral("活力 %1% · 缩放 %2%").arg(qRound(activity_)).arg(scalePercent);
        if (clickThrough) {
            meta += QStringLiteral(" · 穿透中");
        }
        const QFontMetrics metaFm(metaLabel_->font());
        metaLabel_->setText(metaFm.elidedText(meta, Qt::ElideRight, qMax(160, width() - 80)));
        updateThemeColors(false);
        update();
    }

    std::function<void()> openLibraryRequested;

protected:
    void changeEvent(QEvent* event) override
    {
        if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
            updateThemeColors(false);
        }
        QWidget::changeEvent(event);
    }

    void showEvent(QShowEvent* event) override
    {
        updateThemeColors(false);
        QWidget::showEvent(event);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        const QRectF card = QRectF(rect()).adjusted(6, 2, -6, -2);
        const bool dark = isDarkUi(palette());
        painter.setBrush(dark ? QColor("#2C2E35") : QColor("#FFFFFF"));
        painter.setPen(QPen(dark ? QColor(255, 255, 255, 22) : QColor(0, 0, 0, 18), 1.0));
        painter.drawRoundedRect(card, 10, 10);

        if (!portrait_.isNull()) {
            const QRect box = avatar_->geometry().adjusted(1, 1, -1, -5);
            const QSize logical = portrait_.deviceIndependentSize().toSize().expandedTo(QSize(1, 1));
            const QSize fit = logical.scaled(box.size(), Qt::KeepAspectRatio);
            const QRect target(box.x() + (box.width() - fit.width()) / 2,
                               box.y() + (box.height() - fit.height()) / 2,
                               fit.width(), fit.height());
            painter.drawPixmap(target, portrait_);
        }

        const QRectF barTrack(avatar_->x() + 2, avatar_->geometry().bottom() - 2.5, avatar_->width() - 4, 3.4);
        painter.setPen(Qt::NoPen);
        painter.setBrush(dark ? QColor(255, 255, 255, 28) : QColor(0, 0, 0, 22));
        painter.drawRoundedRect(barTrack, 1.7, 1.7);

        if (activity_ > 0.5) {
            QRectF barFill = barTrack;
            barFill.setWidth(qMax(3.4, barTrack.width() * (activity_ / 100.0)));
            QLinearGradient gold(barFill.topLeft(), barFill.topRight());
            gold.setColorAt(0, QColor("#E8A33E"));
            gold.setColorAt(1, QColor("#F9D87E"));
            painter.setBrush(gold);
            painter.drawRoundedRect(barFill, 1.7, 1.7);
        }
    }

private:
    void updateThemeColors(bool force)
    {
        const bool dark = isDarkUi(palette());
        if (!force && themeInitialized_ && dark == lastDark_) {
            return;
        }
        themeInitialized_ = true;
        lastDark_ = dark;
        nameLabel_->setStyleSheet(dark ? QStringLiteral("color: #F2F4F8;") : QStringLiteral("color: #1E2026;"));
        metaLabel_->setStyleSheet(dark ? QStringLiteral("color: #9BA1AE;") : QStringLiteral("color: #6B7280;"));
    }

    QWidget* avatar_ = nullptr;
    QLabel* nameLabel_ = nullptr;
    QLabel* metaLabel_ = nullptr;
    QuickGalleryButton* galleryButton_ = nullptr;
    QPixmap portrait_;
    double activity_ = 78.0;
    bool themeInitialized_ = false;
    bool lastDark_ = false;
};

// Compact Control-Center style quick toggle bar aligned with the 4-column treat grid
class PetQuickControls : public QWidget {
public:
    PetQuickControls()
    {
        setObjectName(QStringLiteral("petQuickControls"));
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(10, 3, 10, 4);
        row->setSpacing(5);

        roamBtn_ = new QuickToggleTile(
            MenuGlyph::Roam, QStringLiteral("自由漫步"),
            QStringLiteral("开启后桌宠会在桌面上自主散步与停歇"), this);
        connect(roamBtn_, &QAbstractButton::clicked, this, [this](bool checked) {
            if (roamToggled) roamToggled(checked);
        });
        row->addWidget(roamBtn_, 1);

#ifdef Q_OS_MACOS
        followBtn_ = new QuickToggleTile(
            MenuGlyph::FollowSpace, QStringLiteral("跟随桌面"),
            QStringLiteral("开启后切换 macOS 桌面空间时，桌宠会从相邻一侧跃入新桌面"), this);
        connect(followBtn_, &QAbstractButton::clicked, this, [this](bool checked) {
            if (followToggled) followToggled(checked);
        });
        row->addWidget(followBtn_, 1);
#endif

        topBtn_ = new QuickToggleTile(
            MenuGlyph::LayerOrder, QStringLiteral("保持置顶"),
            QStringLiteral("开启时浮现在所有窗口上方；关闭时位于活跃窗口下第二层"), this);
        connect(topBtn_, &QAbstractButton::clicked, this, [this](bool checked) {
            if (topToggled) topToggled(checked);
        });
        row->addWidget(topBtn_, 1);

        passBtn_ = new QuickToggleTile(
            MenuGlyph::ClickThrough, QStringLiteral("鼠标穿透"),
            QStringLiteral("开启后点击穿透桌宠直达下方窗口，可在菜单栏或托盘恢复"), this);
        connect(passBtn_, &QAbstractButton::clicked, this, [this](bool checked) {
            if (clickThroughToggled) clickThroughToggled(checked);
        });
        row->addWidget(passBtn_, 1);
    }

    void sync(bool roaming, bool followSpace, bool alwaysOnTop, bool clickThrough)
    {
        if (roamBtn_) {
            QSignalBlocker b(roamBtn_);
            roamBtn_->setChecked(roaming);
        }
        if (followBtn_) {
            QSignalBlocker b(followBtn_);
            followBtn_->setChecked(followSpace);
        }
        if (topBtn_) {
            QSignalBlocker b(topBtn_);
            topBtn_->setChecked(alwaysOnTop);
        }
        if (passBtn_) {
            QSignalBlocker b(passBtn_);
            passBtn_->setChecked(clickThrough);
        }
    }

    std::function<void(bool)> roamToggled;
    std::function<void(bool)> followToggled;
    std::function<void(bool)> topToggled;
    std::function<void(bool)> clickThroughToggled;

private:
    QuickToggleTile* roamBtn_ = nullptr;
    QuickToggleTile* followBtn_ = nullptr;
    QuickToggleTile* topBtn_ = nullptr;
    QuickToggleTile* passBtn_ = nullptr;
};

namespace {
QPoint chooseSpawnPosition(const PetWindow& window, const QList<QRect>& occupied)
{
    const auto* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return QPoint(24 + occupied.size() * 64, 24);
    }
    const QRect area = screen->availableGeometry();
    const int w = qMax(64, window.width());
    const int h = qMax(64, window.height());
    const int stepX = w + 28;
    const int stepY = h + 28;
    const int startX = qMax(area.left() + 16, area.right() - w - 24);
    const int startY = qMax(area.top() + 16, area.bottom() - h - 24);

    for (int row = 0; row < 6; ++row) {
        const int y = qMax(area.top() + 16, startY - row * stepY);
        for (int col = 0; col < 12; ++col) {
            const int x = startX - col * stepX;
            if (x < area.left() + 12 && col > 0) {
                break;
            }
            const QPoint candidate(qMax(area.left(), x), y);
            const QRect box(candidate, QSize(w, h));
            const QRect core = box.adjusted(18, 18, -18, -18);
            bool collides = false;
            for (const QRect& other : occupied) {
                if (core.intersects(other.adjusted(18, 18, -18, -18))) {
                    collides = true;
                    break;
                }
            }
            if (!collides) {
                return candidate;
            }
        }
    }
    return QPoint(startX, startY);
}
}

// ============================================================================
// PetInstance
// ============================================================================

PetInstance::PetInstance(DesktopPetManager& manager, const QString& roleId)
    : manager_(manager),
      roleId_(roleId),
      focusNotice_(window_),
      activityMeter_(window_),
      treatEffect_(window_),
      movement_(window_, [this](double percent) { activityMeter_.setActivity(percent); }),
      tether_(window_, movement_),
      companionEvents_(manager.stored().native()),
      chat_(manager.library(), manager.stored().native()),
      voice_(manager.stored().native()),
      chatOverlay_(window_, chat_, &voice_)
{
    window_.setPrimaryOnly(manager_.stored().primaryScreenOnly());
    window_.layoutChanged = [this] { saveState(); };
    window_.importFile = [this](const QString& path) { manager_.enqueueImport(path, true); };

#ifdef Q_OS_MACOS
    spaceTransitTimer_.setInterval(16);
    spaceTransitTimer_.setTimerType(Qt::PreciseTimer);
    connect(&spaceTransitTimer_, &QTimer::timeout, this, &PetInstance::tickSpaceTransit);
#endif
    chatOverlay_.zoomRequested = [this](const QKeyEvent& key) { return window_.applyZoomKey(key); };
    connect(&chatOverlay_, &PetChatOverlay::activityChanged, &window_,
            [this](bool active) {
                chatActivity_ = active;
#ifdef Q_OS_MACOS
                manager_.windowLayers().setPetInteracting(window_, active);
#endif
            });
    connect(&chatOverlay_, &PetChatOverlay::interactionSent, &window_,
            [this] {
                companionEvents_.interacted(QDateTime::currentDateTime());
                manager_.recordInteraction();
            });
    connect(&chatOverlay_, &PetChatOverlay::activityChanged, &movement_, &RandomMovement::setSuspended);
    connect(&chatOverlay_, &PetChatOverlay::activityMeterVisibilityChanged, &activityMeter_, &ActivityMeter::setRevealed);
    window_.dragStateChanged = [this](bool dragging) {
        chatOverlay_.setDragging(dragging);
        activityMeter_.setSuppressed(dragging);
    };
    tether_.aimingChanged = [this](bool aiming) {
        chatOverlay_.setAiming(aiming);
        activityMeter_.setSuppressed(aiming || window_.isDragging());
    };
    setActivityMeterEnabled(manager_.showActivityMeter());

    window_.clickRequested = [this] {
#ifdef Q_OS_MACOS
        cancelSpaceTransit(true);
#endif
        companionEvents_.interacted(QDateTime::currentDateTime());
        manager_.recordInteraction();
        chat_.setRole(roleId_);
        chatOverlay_.focusInput();
    };

    connect(&chat_, &ChatSession::proactiveFailed, &window_, [this] {
        if (!manager_.focus().quiet() && window_.isVisible()) {
            QString err;
            const auto share = manager_.library().shareFor(roleId_, err, false);
            auto choices = randomSpeechChoices(share);
            while (!choices.isEmpty()) {
                const auto pick = speechDraw_.take(roleId_, choices);
                if (pick.recording.isEmpty()) {
                    chatOverlay_.sayRandom(pick.text);
                    break;
                }
                const auto audio = manager_.library().roleRecording(roleId_, pick.recording, err);
                if (!audio.isEmpty() && recordingDigest(audio) == pick.digest) {
                    chatOverlay_.sayRandom(pick.text, audio);
                    break;
                }
                choices.removeIf([&](const RandomSpeechChoice& item) { return item.id == pick.id; });
            }
        }
    });

    window_.doubleClickRequested = [this] {
        if (manager_.focus().quiet()) {
            return;
        }
        companionEvents_.interacted(QDateTime::currentDateTime());
        manager_.recordInteraction();
        chatOverlay_.releaseInputFocus();
        chat_.setRole(roleId_);
        playEventAction(companionEvents_.binding(CompanionEvent::DoubleClick));
        if (chat_.busy() || roleId_.isEmpty()) {
            return;
        }
        QString err;
        const auto share = manager_.library().shareFor(roleId_, err, false);
        if (!randomSpeechConfig(share.behavior).withAi || !chat_.speak()) {
            auto choices = randomSpeechChoices(share);
            while (!choices.isEmpty()) {
                const auto pick = speechDraw_.take(roleId_, choices);
                if (pick.recording.isEmpty()) {
                    chatOverlay_.sayRandom(pick.text);
                    break;
                }
                const auto audio = manager_.library().roleRecording(roleId_, pick.recording, err);
                if (!audio.isEmpty() && recordingDigest(audio) == pick.digest) {
                    chatOverlay_.sayRandom(pick.text, audio);
                    break;
                }
                choices.removeIf([&](const RandomSpeechChoice& item) { return item.id == pick.id; });
            }
        }
    };

    buildContextMenu();
    tether_.menuRequested = [this](QPoint position) {
        activateAppForPopup(window_);
        contextMenu_.popup(position);
        preparePopupWindow(contextMenu_);
    };
}

PetInstance::~PetInstance()
{
#ifdef Q_OS_MACOS
    manager_.windowLayers().unregisterPet(window_);
#endif
    saveState();
}

QString PetInstance::resolveRoleName() const
{
    if (roleId_ == RoleLibrary::builtInId()) {
        return RoleLibrary::builtInName();
    }
    if (manager_.library().available()) {
        QString err;
        for (const auto& entry : manager_.library().entries(err)) {
            if (entry.id == roleId_) {
                return entry.name;
            }
        }
    }
    return QStringLiteral("桌宠");
}

bool PetInstance::initialize(const QList<QRect>& occupiedRects, QString& error)
{
    if (roleId_ != RoleLibrary::builtInId() && !manager_.library().available()) {
        error = QStringLiteral("角色库尚未打开。");
        return false;
    }
    auto asset = loadRoleAction(manager_.library(), roleId_, ActionSlot::Idle, manager_.builtInPath(), error);
    if (!asset && roleId_ == RoleLibrary::builtInId() && !error.isEmpty()) {
        error.clear();
        asset = loadPetAsset(manager_.builtInPath(), error);
    }
    if (!asset) {
        return false;
    }

    roleName_ = resolveRoleName();
    window_.setWindowTitle(QStringLiteral("AuraPet · %1").arg(roleName_));

    QString shareErr;
    const auto share = manager_.library().shareFor(roleId_, shareErr, false);
    roleBehavior_ = share.behavior;
    companionEvents_.setRole(roleId_, roleBehavior_, QDateTime::currentDateTime());

    companionMode_ = manager_.stored().companionMode(roleId_, roleBehavior_.value(QStringLiteral("kibble")).toString());
    loadedKibble_ = companionMode_ == CompanionMode::Play ? std::optional{PetTreat::HighKibble}
        : companionMode_ == CompanionMode::Focus ? std::optional{PetTreat::SlowKibble} : std::nullopt;
    movement_.setActivity(manager_.stored().activity(roleId_, 78.0));
    activityMeter_.setActivity(movement_.activity());

    roaming_ = manager_.stored().roaming(roleId_, roleBehavior_.value(QStringLiteral("randomMovement")));
    movement_.setEnabled(roaming_);

    chatOverlay_.reset();
    chat_.setRole(roleId_, true);
    voice_.setPackagedVoices(roleId_, share.voices);

    activityMeter_.setAsset(*asset);
    window_.setAsset(std::move(*asset), manager_.stored().scale(roleId_));

    const bool hasPos = manager_.stored().hasSavedPosition(roleId_);
    const bool allowLegacyPos = !hasPos && roleId_ == RoleLibrary::legacyId() && occupiedRects.isEmpty();
    const QPoint fallbackPos = chooseSpawnPosition(window_, occupiedRects);
    const QPoint targetPos = hasPos || allowLegacyPos
        ? manager_.stored().position(roleId_, fallbackPos, allowLegacyPos)
        : fallbackPos;
    window_.placeAt(targetPos, false);
    movement_.setAnchor(window_.pos());
    syncQuietMode();

    followAllSpaces_ = manager_.stored().followAllSpaces(roleId_);
    alwaysOnTop_ = manager_.stored().alwaysOnTop(roleId_);
    window_.setAlwaysOnTop(alwaysOnTop_);

    const bool passThrough = manager_.stored().clickThrough(roleId_);
    window_.setClickThrough(passThrough);
    if (quickControls_) {
        quickControls_->sync(roaming_, followAllSpaces_, alwaysOnTop_, passThrough);
    }

    saveState();
    if (!manager_.isDesktopHidden()) {
        window_.show();
    }
#ifdef Q_OS_MACOS
    manager_.windowLayers().registerPet(window_, followAllSpaces_, alwaysOnTop_);
#endif
    return true;
}

void PetInstance::refreshFromLibrary(bool reloadIdleAsset)
{
    roleName_ = resolveRoleName();
    window_.setWindowTitle(QStringLiteral("AuraPet · %1").arg(roleName_));

    QString shareErr;
    const auto share = manager_.library().shareFor(roleId_, shareErr, false);
    roleBehavior_ = share.behavior;
    companionEvents_.setRole(roleId_, roleBehavior_, QDateTime::currentDateTime());

    chat_.setRole(roleId_, true);
    voice_.setPackagedVoices(roleId_, share.voices);

    if (reloadIdleAsset) {
        QString assetErr;
        auto asset = loadRoleAction(manager_.library(), roleId_, ActionSlot::Idle, manager_.builtInPath(), assetErr);
        if (!asset && roleId_ == RoleLibrary::builtInId()) {
            assetErr.clear();
            asset = loadPetAsset(manager_.builtInPath(), assetErr);
        }
        if (asset) {
            const QPoint currentPos = window_.pos();
            const int currentScale = window_.scalePercent();
            activityMeter_.setAsset(*asset);
            window_.setAsset(std::move(*asset), currentScale);
            window_.placeAt(currentPos, false);
        }
    }
}

void PetInstance::saveState(bool force)
{
    if (roleId_.isEmpty()) {
        return;
    }
    const QPoint pos = window_.pos();
    const int scale = window_.scalePercent();
    const double act = movement_.activity();
    if (force || pos != lastSavedPos_ || scale != lastSavedScale_) {
        manager_.stored().saveRoleLayout(roleId_, pos, scale);
        lastSavedPos_ = pos;
        lastSavedScale_ = scale;
    }
    if (force || qAbs(act - lastSavedActivity_) >= 1.0) {
        manager_.stored().setActivity(roleId_, act);
        lastSavedActivity_ = act;
    }
}

void PetInstance::syncQuietMode()
{
    const bool dnd = manager_.focus().doNotDisturb();
    const bool focusTimerActive = manager_.focus().active()
        && manager_.focus().kind() == FocusSession::TimerKind::Focus;
    const bool quiet = manager_.focus().quiet();
    const bool freezeMotion = dnd
        || (focusTimerActive && manager_.focusMotionPolicy() == FocusMotionPolicy::CompletelyStill);
    movement_.setQuietMode(freezeMotion);
    voice_.setQuiet(quiet);
    chat_.setVoiceFirst(voice_.available());
    chat_.setProactiveEnabled(!quiet);
    if (quiet) {
        chatOverlay_.dismissProactiveSpeech();
    }
    syncCompanionRhythm();
}

void PetInstance::applyPrimaryOnly(bool primaryOnly)
{
    window_.setPrimaryOnly(primaryOnly);
    clampToScreen();
}

void PetInstance::clampToScreen()
{
    tether_.cancel();
    window_.placeAt(window_.pos(), true);
    saveState();
}

void PetInstance::moveToDisplay(const QScreen* screen)
{
    if (screen) {
        window_.moveToDisplay(screen);
        saveState();
    }
}

bool PetInstance::clickThrough() const
{
    return window_.windowFlags().testFlag(Qt::WindowTransparentForInput);
}

void PetInstance::setClickThrough(bool enabled)
{
    window_.setClickThrough(enabled);
    manager_.stored().setClickThrough(roleId_, enabled);
    if (quickControls_) {
        quickControls_->sync(roaming_, followAllSpaces_, alwaysOnTop_, enabled);
    }
#ifdef Q_OS_MACOS
    manager_.windowLayers().updatePetPolicy(window_, followAllSpaces_, alwaysOnTop_);
#endif
}

void PetInstance::setFollowAllSpaces(bool enabled)
{
    followAllSpaces_ = enabled;
    manager_.stored().setFollowAllSpaces(roleId_, enabled);
#ifdef Q_OS_MACOS
    if (!enabled) {
        cancelSpaceTransit(true);
    }
#endif
    if (quickControls_) {
        quickControls_->sync(roaming_, followAllSpaces_, alwaysOnTop_, clickThrough());
    }
#ifdef Q_OS_MACOS
    manager_.windowLayers().updatePetPolicy(window_, followAllSpaces_, alwaysOnTop_);
#endif
}

#ifdef Q_OS_MACOS
void PetInstance::prepareSpaceTransit(const QPoint& landingPos)
{
    chatOverlay_.closeInput();
    chatOverlay_.setSpaceTransitTarget(landingPos);
}

void PetInstance::startSpaceTransit(const SpaceTransitPlan& plan)
{
    spaceTransitTimer_.stop();
    spaceTransitPlan_ = plan;
    spaceTransitActive_ = true;
    tether_.cancel();
    chatOverlay_.closeInput();
    chatOverlay_.setSpaceTransitTarget(plan.landingPos.toPoint());
    movement_.setSpaceTransitActive(true);

    const auto initial = sampleSpaceTransit(spaceTransitPlan_, 0.0);
    window_.move(initial.position.toPoint());
    window_.setTransitPose(initial.leanDegrees, initial.scaleX, initial.scaleY);
    spaceTransitClock_.restart();
    spaceTransitTimer_.start();
}

void PetInstance::cancelSpaceTransit(bool clampToBounds)
{
    if (!spaceTransitActive_) {
        return;
    }
    spaceTransitTimer_.stop();
    spaceTransitActive_ = false;
    window_.resetTransitPose();
    if (clampToBounds && window_.isVisible()) {
        window_.placeAt(window_.pos(), false);
    }
    chatOverlay_.endSpaceTransit();
    movement_.finishSpaceTransit(window_.pos(), 1.2);
}

void PetInstance::tickSpaceTransit()
{
    if (!spaceTransitActive_) {
        spaceTransitTimer_.stop();
        return;
    }
    if (!window_.isVisible() || window_.isDragging()) {
        cancelSpaceTransit(true);
        return;
    }

    const double elapsed = spaceTransitClock_.nsecsElapsed() / 1e9;
    const auto sample = sampleSpaceTransit(spaceTransitPlan_, elapsed);
    const QPoint nextPos = sample.position.toPoint();
    if (nextPos != window_.pos()) {
        window_.move(nextPos);
    }
    window_.setTransitPose(sample.leanDegrees, sample.scaleX, sample.scaleY);

    if (sample.finished) {
        spaceTransitTimer_.stop();
        spaceTransitActive_ = false;
        window_.resetTransitPose();
        window_.placeAt(spaceTransitPlan_.landingPos.toPoint(), false);
        chatOverlay_.endSpaceTransit();
        movement_.finishSpaceTransit(window_.pos(), 2.4);
        saveState(false);
    }
}
#endif

void PetInstance::setAlwaysOnTop(bool enabled)
{
    alwaysOnTop_ = enabled;
    manager_.stored().setAlwaysOnTop(roleId_, enabled);
    if (quickControls_) {
        quickControls_->sync(roaming_, followAllSpaces_, alwaysOnTop_, clickThrough());
    }
    if (topLayerRadio_ && secondLayerRadio_) {
        topLayerRadio_->setChecked(enabled);
        secondLayerRadio_->setChecked(!enabled);
    }
#ifdef Q_OS_MACOS
    manager_.windowLayers().updatePetPolicy(window_, followAllSpaces_, alwaysOnTop_);
#else
    window_.setAlwaysOnTop(enabled);
#endif
}

bool PetInstance::roaming() const
{
    return roaming_;
}

void PetInstance::setRoaming(bool enabled)
{
    roaming_ = enabled;
    movement_.setEnabled(enabled);
    manager_.stored().setRoaming(roleId_, enabled);
    if (quickControls_) {
        quickControls_->sync(roaming_, followAllSpaces_, alwaysOnTop_, clickThrough());
    }
}

void PetInstance::setActivityMeterEnabled(bool enabled)
{
    activityMeter_.setDisplayEnabled(enabled);
    chatOverlay_.setActivityMeterEnabled(enabled);
}

void PetInstance::selectKibble(const QString& kibble, bool persist)
{
    if (kibble == QStringLiteral("high")) {
        setCompanionMode(CompanionMode::Play, persist);
    } else if (kibble == QStringLiteral("slow")) {
        setCompanionMode(CompanionMode::Focus, persist);
    } else {
        setCompanionMode(CompanionMode::Daily, persist);
    }
}

QString PetInstance::activeOverrideTag() const
{
    if (manager_.focus().doNotDisturb()) {
        return QStringLiteral("勿扰 · 静伴");
    }
    if (manager_.focus().active()) {
        if (manager_.focus().kind() == FocusSession::TimerKind::Focus) {
            return manager_.focusMotionPolicy() == FocusMotionPolicy::CompletelyStill
                ? QStringLiteral("专注计时 · 静止")
                : QStringLiteral("专注计时 · 静伴");
        }
        return QStringLiteral("休息中 · 日常");
    }
    return {};
}

void PetInstance::setCompanionMode(CompanionMode mode, bool persist)
{
    companionMode_ = mode;
    loadedKibble_ = mode == CompanionMode::Play ? std::optional{PetTreat::HighKibble}
        : mode == CompanionMode::Focus ? std::optional{PetTreat::SlowKibble} : std::nullopt;
    if (persist) {
        manager_.stored().setCompanionMode(roleId_, mode);
    }
    syncCompanionRhythm(manager_.currentForegroundContext(), QDateTime::currentDateTime());
}

void PetInstance::syncCompanionRhythm()
{
    syncCompanionRhythm(manager_.currentForegroundContext(), QDateTime::currentDateTime());
}

void PetInstance::syncCompanionRhythm(const ForegroundContext& fg, const QDateTime& now)
{
    RhythmContext ctx;
    ctx.userMode = companionMode_;
    ctx.doNotDisturb = manager_.focus().doNotDisturb();
    ctx.focusTimerActive = manager_.focus().active()
        && manager_.focus().kind() == FocusSession::TimerKind::Focus;
    ctx.breakTimerActive = manager_.focus().active()
        && manager_.focus().kind() != FocusSession::TimerKind::Focus;
    ctx.appActive = fg.selfAppActive || chatActivity_;
    const QDateTime last = companionEvents_.lastInteraction();
    ctx.recentInteraction = last.isValid() && last.secsTo(now) >= 0 && last.secsTo(now) <= 120;
    ctx.externalFullScreen = fg.externalFullScreen;

    const EffectiveRhythm next = resolveEffectiveRhythm(ctx);
    const bool rhythmChanged = (next != effectiveRhythm_) || (movement_.rhythm() != next);
    if (rhythmChanged) {
        effectiveRhythm_ = next;
        if (next == EffectiveRhythm::Focus) {
            movement_.setAnchor(window_.pos());
        }
        movement_.setRhythm(next);
        scheduleNextProactive(now);
    } else if (!nextProactiveAt_.isValid() && rhythmProfile(effectiveRhythm_).proactiveMinSec > 0) {
        scheduleNextProactive(now);
    }
    if (treatCard_) {
        treatCard_->setCompanionState(companionMode_, effectiveRhythm_, activeOverrideTag());
    }
}

void PetInstance::scheduleNextProactive(const QDateTime& now)
{
    const RhythmProfile profile = rhythmProfile(effectiveRhythm_);
    if (profile.proactiveMinSec <= 0 || profile.proactiveMaxSec <= 0) {
        nextProactiveAt_ = QDateTime();
        return;
    }
    const int span = qMax(1, profile.proactiveMaxSec - profile.proactiveMinSec + 1);
    const int delay = profile.proactiveMinSec + QRandomGenerator::global()->bounded(span);
    nextProactiveAt_ = now.addSecs(delay);
}

bool PetInstance::triggerProactiveSpeech(const QDateTime& now)
{
    if (roleId_.isEmpty() || chat_.busy() || manager_.focus().quiet()) {
        return false;
    }
    chat_.setRole(roleId_);
    QString err;
    const auto share = manager_.library().shareFor(roleId_, err, false);
    bool spoke = false;
    if (randomSpeechConfig(share.behavior).withAi && chat_.speak()) {
        spoke = true;
    } else {
        auto choices = randomSpeechChoices(share);
        while (!choices.isEmpty()) {
            const auto pick = speechDraw_.take(roleId_, choices);
            if (pick.recording.isEmpty()) {
                chatOverlay_.sayRandom(pick.text);
                spoke = true;
                break;
            }
            const auto audio = manager_.library().roleRecording(roleId_, pick.recording, err);
            if (!audio.isEmpty() && recordingDigest(audio) == pick.digest) {
                chatOverlay_.sayRandom(pick.text, audio);
                spoke = true;
                break;
            }
            choices.removeIf([&](const RandomSpeechChoice& item) { return item.id == pick.id; });
        }
    }
    if (spoke) {
        const double cost = rhythmProfile(effectiveRhythm_).proactiveActivityCost;
        if (cost > 0.0) {
            movement_.adjustActivity(-cost);
        }
    }
    scheduleNextProactive(now);
    return spoke;
}

void PetInstance::useTreat(PetTreat treat)
{
    companionEvents_.interacted(QDateTime::currentDateTime());
    manager_.recordInteraction();
    using Feedback = ActivityMeter::Feedback;
    if (treat == PetTreat::SlowKibble || treat == PetTreat::HighKibble) {
        const CompanionMode target = treat == PetTreat::HighKibble ? CompanionMode::Play : CompanionMode::Focus;
        if (companionMode_ == target) {
            setCompanionMode(CompanionMode::Daily, true);
            activityMeter_.flash(Feedback::Neutral);
        } else {
            setCompanionMode(target, true);
            activityMeter_.flash(target == CompanionMode::Play ? Feedback::Raise : Feedback::Lower);
            runEvent(target == CompanionMode::Play ? CompanionEvent::HighKibble : CompanionEvent::SlowKibble);
        }
        saveState();
        return;
    }

    if (treat == PetTreat::Catnip) {
        activityMeter_.flash(movement_.activity() < 100 ? Feedback::Raise : Feedback::Neutral);
        movement_.adjustActivity(catnipActivityGain);
        treatEffect_.play(treat);
        runEvent(CompanionEvent::Catnip);
    } else if (movement_.activity() > 0) {
        activityMeter_.flash(Feedback::Lower);
        movement_.adjustActivity(-yarnActivityCost);
        treatEffect_.play(treat);
        if (!runEvent(CompanionEvent::Yarn) && window_.isVisible()
            && !playRoleAction(ActionSlot::Happy, false)) {
            window_.playBounce();
        }
    } else {
        activityMeter_.flash(Feedback::Neutral);
    }
    saveState();
}

bool PetInstance::playRoleAction(ActionSlot slot, bool explainMissing)
{
    QString actionError;
    auto asset = loadRoleAction(manager_.library(), roleId_, slot, manager_.builtInPath(), actionError);
    if (!asset) {
        if (explainMissing) {
            QMessageBox::information(&window_, QStringLiteral("无法播放动作"), actionError.isEmpty()
                ? QStringLiteral("可以在「动作与姿态 → 配置角色动作」中为「%1」设置%2素材。")
                      .arg(roleName_, actionSlotName(slot))
                : actionError);
        }
        return false;
    }
    window_.playAction(std::move(*asset), slot == ActionSlot::Sleepy ? 2200 : 1200);
    if (!manager_.isDesktopHidden()) {
        window_.show();
    }
    return true;
}

bool PetInstance::playEventAction(const QJsonObject& binding)
{
    const auto action = binding.value(QStringLiteral("action")).toString();
    if (action.isEmpty()) {
        return false;
    }
    if (action == QStringLiteral("idle")) {
        window_.returnToIdle();
    } else if (action == QStringLiteral("bounce")
        || (action == QStringLiteral("happy") && !playRoleAction(ActionSlot::Happy, false))
        || (action == QStringLiteral("sleepy") && !playRoleAction(ActionSlot::Sleepy, false))) {
        window_.playBounce();
    }
    return true;
}

bool PetInstance::sayEventLine(const QJsonObject& binding, bool atFocusStart)
{
    const auto line = binding.value(QStringLiteral("line")).toString();
    if ((!manager_.focus().quiet() || (atFocusStart && !manager_.focus().doNotDisturb())) && !line.isEmpty()) {
        chatOverlay_.sayFixed(line);
        return true;
    }
    return false;
}

bool PetInstance::runBinding(const QJsonObject& binding, bool atFocusStart)
{
    sayEventLine(binding, atFocusStart);
    return playEventAction(binding);
}

bool PetInstance::runEvent(CompanionEvent event)
{
    return runBinding(companionEvents_.binding(event));
}

void PetInstance::announceBanner(const QString& text)
{
    if (window_.isVisible() && !manager_.focus().doNotDisturb()) {
        focusNotice_.announce(text);
    }
}

void PetInstance::handleFocusCompleted(bool announce, bool allowFallbackVoice)
{
    if (!window_.isVisible() || manager_.focus().doNotDisturb()) {
        return;
    }
    if (announce) {
        focusNotice_.announce(
            QStringLiteral("专注结束 · 建议休息 %1 分钟").arg(manager_.focus().recommendedBreakMinutes()));
    }
    const auto endAct = manager_.focus().endAction();
    const auto binding = companionEvents_.binding(CompanionEvent::FocusFinished);
    const bool hasLine = sayEventLine(binding);
    if (!hasLine && allowFallbackVoice && endAct != FocusSession::EndAction::Text && voice_.available()) {
        chatOverlay_.sayFixed(QStringLiteral("专注结束，休息一下吧。"));
    }
    if (playEventAction(binding)) {
        return;
    }
    if (endAct == FocusSession::EndAction::Bounce
        || (endAct == FocusSession::EndAction::Happy && !playRoleAction(ActionSlot::Happy, false))) {
        window_.playBounce();
    }
}

bool PetInstance::isVoiceBusy() const
{
    return voice_.busy();
}

bool PetInstance::pollReminder(const QDateTime& now, bool anyVoiceBusy)
{
    const bool available = window_.isVisible() && !manager_.focus().quiet()
        && !QApplication::activeModalWidget() && !QApplication::activePopupWidget() && !anyVoiceBusy
        && !chatActivity_ && !chat_.busy() && !voice_.busy();
    const bool suppressLow = (effectiveRhythm_ == EffectiveRhythm::Focus);
    const auto action = companionEvents_.nextReminder(now, movement_.activity(), available, suppressLow);
    if (!action.isEmpty()) {
        runBinding(action);
        return true;
    }
    if (!available || effectiveRhythm_ == EffectiveRhythm::Focus) {
        return false;
    }
    const RhythmProfile profile = rhythmProfile(effectiveRhythm_);
    if (profile.proactiveMinSec <= 0) {
        return false;
    }
    if (!nextProactiveAt_.isValid()) {
        scheduleNextProactive(now);
        return false;
    }
    if (now >= nextProactiveAt_ && movement_.activity() >= profile.proactiveMinActivity) {
        return triggerProactiveSpeech(now);
    }
    return false;
}

void PetInstance::openRoleCardDialog()
{
    if (!manager_.library().available() || roleId_.isEmpty()) {
        return;
    }
    showRoleCard(window_, manager_.library(), roleId_, manager_.voiceTraining());
    manager_.refreshAllMetadata();
}

void PetInstance::openActionsDialog()
{
    if (editingActions_ || roleId_.isEmpty()) {
        return;
    }
    if (!manager_.library().available()) {
        QMessageBox::warning(&window_, QStringLiteral("无法打开角色动作"), manager_.libraryError());
        return;
    }
    QScopedValueRollback<bool> guard(editingActions_, true);
    if (editRoleActions(manager_.library(), roleId_, manager_.builtInPath(), &window_)) {
        refreshFromLibrary(true);
    }
}

void PetInstance::resizePet(int scalePercent)
{
    window_.setScalePercent(scalePercent);
    saveState();
}

void PetInstance::configureVoice()
{
    voice_.configure(window_, roleId_);
    chat_.setVoiceFirst(voice_.available());
}

void PetInstance::testVoice()
{
    chatOverlay_.sayFixed(QStringLiteral("你好，桌宠语音已经就绪。"));
}

void PetInstance::openChatHistory()
{
    if (historyOpen_ || roleId_.isEmpty()) {
        return;
    }
    if (!manager_.library().available()) {
        QMessageBox::warning(&window_, QStringLiteral("无法读取聊天记录"), manager_.libraryError());
        return;
    }
    QScopedValueRollback<bool> opening(historyOpen_, true);
    chatOverlay_.closeInput();
    chat_.setRole(roleId_);
    showRoleChatHistory(chat_, manager_.library(), manager_.stored().native(), &window_);
}

void PetInstance::updateMenuState()
{
    movement_.postpone();
    syncCompanionRhythm();
    if (statusBanner_) {
        statusBanner_->refresh(window_.previewPixmap(), roleName_, movement_.activity(),
                               window_.scalePercent(), clickThrough(),
                               manager_.petIndex(roleId_) + 1, static_cast<int>(manager_.petCount()));
    }
    if (quickControls_) {
        quickControls_->sync(roaming(), followAllSpaces_, alwaysOnTop_, clickThrough());
    }
    if (voiceTestAction_) {
        voiceTestAction_->setEnabled(window_.isVisible() && voice_.available());
    }
    if (scaleInfoAction_) {
        scaleInfoAction_->setText(QStringLiteral("当前大小：%1%").arg(window_.scalePercent()));
    }
    if (dismissAction_) {
        dismissAction_->setVisible(manager_.petCount() > 1);
        dismissAction_->setEnabled(manager_.petCount() > 1);
    }
    if (visibilityAction_) {
        visibilityAction_->setText(manager_.isDesktopHidden()
            ? QCoreApplication::translate("DesktopMenu", "显示全部桌宠")
            : QCoreApplication::translate("DesktopMenu", "隐藏桌宠"));
    }
    if (zoomInAction_) {
        zoomInAction_->setEnabled(window_.scalePercent() < 200);
    }
    if (zoomOutAction_) {
        zoomOutAction_->setEnabled(window_.scalePercent() > 50);
    }
    if (resetZoomAction_) {
        resetZoomAction_->setEnabled(window_.scalePercent() != 100);
    }
    if (topLayerRadio_ && secondLayerRadio_) {
        topLayerRadio_->setChecked(alwaysOnTop_);
        secondLayerRadio_->setChecked(!alwaysOnTop_);
    }
    if (primaryDisplayAction_ && currentDisplayAction_) {
        const bool primary = manager_.stored().primaryScreenOnly();
        primaryDisplayAction_->setChecked(primary);
        currentDisplayAction_->setChecked(!primary);
    }
}

void PetInstance::buildContextMenu()
{
    contextMenu_.setToolTipsVisible(true);

    // 1. Unified Top Dashboard: Role Identity Card (sole Role Library entry) + Treats + Quick Toggles
    auto* bannerAction = new QWidgetAction(&contextMenu_);
    statusBanner_ = new PetStatusBanner;
    statusBanner_->openLibraryRequested = [this] {
        contextMenu_.hide();
        const QString preferred = roleId_;
        QTimer::singleShot(0, &manager_, [mgr = &manager_, preferred] { mgr->openRoleLibrary(preferred); });
    };
    bannerAction->setDefaultWidget(statusBanner_);
    contextMenu_.addAction(bannerAction);

    auto* treatHeader = new QWidgetAction(&contextMenu_);
    treatCard_ = new TreatCard;
    treatCard_->setCompanionState(companionMode_, effectiveRhythm_, activeOverrideTag());
    treatCard_->modeSelected = [this](CompanionMode mode) {
        companionEvents_.interacted(QDateTime::currentDateTime());
        manager_.recordInteraction();
        const bool changed = (companionMode_ != mode);
        setCompanionMode(mode, true);
        if (changed) {
            using Feedback = ActivityMeter::Feedback;
            if (mode == CompanionMode::Play) {
                activityMeter_.flash(Feedback::Raise);
                runEvent(CompanionEvent::HighKibble);
            } else if (mode == CompanionMode::Focus) {
                activityMeter_.flash(Feedback::Lower);
                runEvent(CompanionEvent::SlowKibble);
            } else {
                activityMeter_.flash(Feedback::Neutral);
            }
        }
        saveState();
    };
    treatCard_->picked = [this](PetTreat treat) {
        contextMenu_.hide();
        useTreat(treat);
    };
    treatHeader->setDefaultWidget(treatCard_);
    contextMenu_.addAction(treatHeader);

    auto* quickAction = new QWidgetAction(&contextMenu_);
    quickControls_ = new PetQuickControls;
    quickControls_->roamToggled = [this](bool on) { setRoaming(on); };
    quickControls_->followToggled = [this](bool on) { setFollowAllSpaces(on); };
    quickControls_->topToggled = [this](bool on) { setAlwaysOnTop(on); };
    quickControls_->clickThroughToggled = [this](bool on) {
        setClickThrough(on);
        if (on) {
            contextMenu_.hide();
        }
    };
    quickAction->setDefaultWidget(quickControls_);
    contextMenu_.addAction(quickAction);
    contextMenu_.addSeparator();

    // 2. Role Interaction, Companion & Display Submenus
    auto* dialogueMenu = contextMenu_.addMenu(QStringLiteral("对话与语音"));
    assignMenuGlyph(dialogueMenu->menuAction(), MenuGlyph::Dialogue);
    auto* roleCard = dialogueMenu->addAction(QStringLiteral("角色卡与音色训练…"));
    roleCard->setObjectName(QStringLiteral("showRoleCard"));
    connect(roleCard, &QAction::triggered, &window_, [this] { openRoleCardDialog(); });
    connect(dialogueMenu->addAction(QStringLiteral("聊天记录与记忆…")),
            &QAction::triggered, &window_, [this] { openChatHistory(); });
    dialogueMenu->addSeparator();
    connect(dialogueMenu->addAction(QStringLiteral("回复语音设置…")),
            &QAction::triggered, &window_, [this] { configureVoice(); });
    voiceTestAction_ = dialogueMenu->addAction(QStringLiteral("试听当前发音"));
    voiceTestAction_->setObjectName(QStringLiteral("testReplyVoice"));
    connect(voiceTestAction_, &QAction::triggered, &window_, [this] { testVoice(); });
    connect(dialogueMenu->addAction(QStringLiteral("本机千问语音服务…")),
            &QAction::triggered, &window_, [this] { showVoiceService(window_); });

    auto* actionsMenu = contextMenu_.addMenu(QStringLiteral("动作与姿态"));
    assignMenuGlyph(actionsMenu->menuAction(), MenuGlyph::Actions);
    connect(actionsMenu->addAction(QCoreApplication::translate("DesktopMenu", "播放开心")),
            &QAction::triggered, &window_, [this] { playRoleAction(ActionSlot::Happy, true); });
    connect(actionsMenu->addAction(QCoreApplication::translate("DesktopMenu", "播放困倦")),
            &QAction::triggered, &window_, [this] { playRoleAction(ActionSlot::Sleepy, true); });
    connect(actionsMenu->addAction(QCoreApplication::translate("DesktopMenu", "回到待机")),
            &QAction::triggered, &window_, &PetWindow::returnToIdle);
    actionsMenu->addSeparator();
    connect(actionsMenu->addAction(QStringLiteral("配置角色动作…")),
            &QAction::triggered, &window_, [this] { openActionsDialog(); });

    auto* companionSub = addCompanionMenu(contextMenu_, window_, manager_.focus());
    assignMenuGlyph(companionSub->menuAction(), MenuGlyph::Companion);

    auto* appearanceMenu = contextMenu_.addMenu(QStringLiteral("外观与屏幕"));
    assignMenuGlyph(appearanceMenu->menuAction(), MenuGlyph::Appearance);
    appearanceMenu->setToolTipsVisible(true);
    scaleInfoAction_ = appearanceMenu->addAction(QStringLiteral("当前大小：100%"));
    scaleInfoAction_->setEnabled(false);
    zoomInAction_ = appearanceMenu->addAction(QStringLiteral("放大桌宠 (+10%)"));
    connect(zoomInAction_, &QAction::triggered, &window_, [this] { resizePet(window_.scalePercent() + 10); });
    zoomOutAction_ = appearanceMenu->addAction(QStringLiteral("缩小桌宠 (-10%)"));
    connect(zoomOutAction_, &QAction::triggered, &window_, [this] { resizePet(window_.scalePercent() - 10); });
    resetZoomAction_ = appearanceMenu->addAction(QStringLiteral("恢复默认大小 (100%)"));
    connect(resetZoomAction_, &QAction::triggered, &window_, [this] { resizePet(100); });

    appearanceMenu->addSeparator();
    auto* layerTitle = appearanceMenu->addAction(QStringLiteral("窗口浮现层级"));
    layerTitle->setEnabled(false);
    auto* layerGroup = new QActionGroup(appearanceMenu);
    topLayerRadio_ = appearanceMenu->addAction(QStringLiteral("保持浮现在最上层"));
    secondLayerRadio_ = appearanceMenu->addAction(QStringLiteral("活跃窗口下第二层（不被旧窗口掩盖）"));
    for (auto* act : {topLayerRadio_, secondLayerRadio_}) {
        act->setCheckable(true);
        layerGroup->addAction(act);
    }
    connect(layerGroup, &QActionGroup::triggered, &window_, [this](QAction* selected) {
        setAlwaysOnTop(selected == topLayerRadio_);
    });

    appearanceMenu->addSeparator();
    connect(appearanceMenu->addAction(QStringLiteral("移到鼠标所在屏幕")), &QAction::triggered, &window_, [this] {
        if (const auto* screen = QGuiApplication::screenAt(QCursor::pos())) {
            if (manager_.stored().primaryScreenOnly()) {
                manager_.setPrimaryScreenOnly(false);
            }
            moveToDisplay(screen);
        }
    });
    connect(appearanceMenu->addAction(QStringLiteral("移到主屏幕")),
            &QAction::triggered, &window_, [this] { moveToDisplay(QGuiApplication::primaryScreen()); });

    appearanceMenu->addSeparator();
    auto* displayPolicies = new QActionGroup(appearanceMenu);
    currentDisplayAction_ = appearanceMenu->addAction(QStringLiteral("停留在当前所在屏幕"));
    primaryDisplayAction_ = appearanceMenu->addAction(QStringLiteral("固定在主屏幕"));
    for (auto* action : {currentDisplayAction_, primaryDisplayAction_}) {
        action->setCheckable(true);
        displayPolicies->addAction(action);
    }
    connect(displayPolicies, &QActionGroup::triggered, &window_, [this](QAction* selected) {
        manager_.setPrimaryScreenOnly(selected == primaryDisplayAction_);
    });

    contextMenu_.addSeparator();

    // 3. Assets, Preferences & Visibility
    auto* assetsMenu = contextMenu_.addMenu(QStringLiteral("导入与备份"));
    assignMenuGlyph(assetsMenu->menuAction(), MenuGlyph::ImportBackup);
    connect(assetsMenu->addAction(QStringLiteral("导入图片或动图（智能抠图）…")),
            &QAction::triggered, &window_, [this] { manager_.pickAndImportAsset(true); });
    connect(assetsMenu->addAction(QStringLiteral("原样导入并播放…")),
            &QAction::triggered, &window_, [this] { manager_.pickAndImportAsset(false); });
    assetsMenu->addSeparator();
    connect(assetsMenu->addAction(QStringLiteral("备份本地角色库…")),
            &QAction::triggered, &window_, [this] { manager_.backupLibrary(&window_); });

    auto* appSettingsMenu = contextMenu_.addMenu(QStringLiteral("偏好设置"));
    assignMenuGlyph(appSettingsMenu->menuAction(), MenuGlyph::Settings);
    manager_.populateAppSettingsMenu(*appSettingsMenu, &window_);

    dismissAction_ = contextMenu_.addAction(QStringLiteral("收起此桌宠"));
    assignMenuGlyph(dismissAction_, MenuGlyph::Dismiss);
    connect(dismissAction_, &QAction::triggered, &window_, [this] {
        const QString id = roleId_;
        QTimer::singleShot(0, &manager_, [mgr = &manager_, id] { mgr->dismissRole(id); });
    });

    visibilityAction_ = contextMenu_.addAction(
        QCoreApplication::translate("DesktopMenu", "隐藏桌宠"));
    assignMenuGlyph(visibilityAction_, MenuGlyph::Visibility);
    connect(visibilityAction_, &QAction::triggered, &window_, [this] { manager_.toggleDesktopVisibility(); });

    contextMenu_.addSeparator();

    // 4. Application Exit
    auto* quitAction = contextMenu_.addAction(
        QCoreApplication::translate("DesktopMenu", "退出 AuraPet"));
    assignMenuGlyph(quitAction, MenuGlyph::Quit);
    quitAction->setMenuRole(QAction::QuitRole);
    quitAction->setShortcut(QKeySequence::Quit);
    quitAction->setShortcutContext(Qt::ApplicationShortcut);
    window_.addAction(quitAction);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    applyAuraMenuTheme(contextMenu_);
    connect(&contextMenu_, &QMenu::aboutToShow, &window_, [this] { updateMenuState(); });
}

void PetInstance::populateSubmenu(QMenu& sub)
{
    auto* info = sub.addAction(QStringLiteral("活跃度 %1% · 大小 %2%")
                                   .arg(qRound(movement_.activity()))
                                   .arg(window_.scalePercent()));
    info->setEnabled(false);

    auto* modeSub = sub.addMenu(QStringLiteral("陪伴模式"));
    modeSub->setToolTipsVisible(true);
    auto* modeGroup = new QActionGroup(modeSub);
    for (const auto mode : companionModes) {
        auto* action = modeSub->addAction(companionModeName(mode));
        action->setToolTip(companionModeDescription(mode));
        action->setCheckable(true);
        action->setChecked(companionMode_ == mode);
        modeGroup->addAction(action);
        connect(action, &QAction::triggered, &window_, [this, mode] {
            setCompanionMode(mode, true);
            saveState();
        });
    }
    modeSub->addSeparator();
    for (const auto treat : {PetTreat::YarnBall, PetTreat::Catnip}) {
        auto* action = modeSub->addAction(treatIcon(treat), treatName(treat));
        action->setToolTip(treatDescription(treat));
        connect(action, &QAction::triggered, &window_, [this, treat] { useTreat(treat); });
    }

    auto* actionsSub = sub.addMenu(QStringLiteral("动作与姿态"));
    connect(actionsSub->addAction(QStringLiteral("播放开心")), &QAction::triggered, &window_, [this] { playRoleAction(ActionSlot::Happy, true); });
    connect(actionsSub->addAction(QStringLiteral("播放困倦")), &QAction::triggered, &window_, [this] { playRoleAction(ActionSlot::Sleepy, true); });
    connect(actionsSub->addAction(QStringLiteral("回到待机")), &QAction::triggered, &window_, &PetWindow::returnToIdle);
    actionsSub->addSeparator();
    connect(actionsSub->addAction(QStringLiteral("配置角色动作…")), &QAction::triggered, &window_, [this] { openActionsDialog(); });

    auto* dialogueSub = sub.addMenu(QStringLiteral("对话与语音"));
    connect(dialogueSub->addAction(QStringLiteral("角色卡与音色训练…")), &QAction::triggered, &window_, [this] { openRoleCardDialog(); });
    connect(dialogueSub->addAction(QStringLiteral("聊天记录与记忆…")), &QAction::triggered, &window_, [this] { openChatHistory(); });
    dialogueSub->addSeparator();
    connect(dialogueSub->addAction(QStringLiteral("回复语音设置…")), &QAction::triggered, &window_, [this] { configureVoice(); });
    auto* testAct = dialogueSub->addAction(QStringLiteral("试听当前发音"));
    testAct->setEnabled(window_.isVisible() && voice_.available());
    connect(testAct, &QAction::triggered, &window_, [this] { testVoice(); });

    sub.addSeparator();
    auto* roamAct = sub.addAction(QStringLiteral("随机移动"));
    roamAct->setCheckable(true);
    roamAct->setChecked(roaming());
    connect(roamAct, &QAction::toggled, &window_, [this](bool on) { setRoaming(on); });

    auto* followAct = sub.addAction(QStringLiteral("跟随切换桌面"));
    followAct->setCheckable(true);
    followAct->setChecked(followAllSpaces_);
    followAct->setToolTip(QStringLiteral("切换桌面时从较近一侧跃入新桌面并停靠在近端"));
    connect(followAct, &QAction::toggled, &window_, [this](bool on) { setFollowAllSpaces(on); });

    auto* topAct = sub.addAction(QStringLiteral("保持在最上层"));
    topAct->setCheckable(true);
    topAct->setChecked(alwaysOnTop_);
    topAct->setToolTip(QStringLiteral("关闭时处于活跃窗口下第二层，不被旧窗口持续掩盖"));
    connect(topAct, &QAction::toggled, &window_, [this](bool on) { setAlwaysOnTop(on); });

    auto* scaleSub = sub.addMenu(QStringLiteral("缩放大小"));
    auto* zoomIn = scaleSub->addAction(QStringLiteral("放大桌宠 (+10%)"));
    zoomIn->setEnabled(window_.scalePercent() < 200);
    connect(zoomIn, &QAction::triggered, &window_, [this] { resizePet(window_.scalePercent() + 10); });
    auto* zoomOut = scaleSub->addAction(QStringLiteral("缩小桌宠 (-10%)"));
    zoomOut->setEnabled(window_.scalePercent() > 50);
    connect(zoomOut, &QAction::triggered, &window_, [this] { resizePet(window_.scalePercent() - 10); });
    auto* resetZoom = scaleSub->addAction(QStringLiteral("恢复默认大小 (100%)"));
    resetZoom->setEnabled(window_.scalePercent() != 100);
    connect(resetZoom, &QAction::triggered, &window_, [this] { resizePet(100); });

    auto* passAct = sub.addAction(QStringLiteral("鼠标穿透"));
    passAct->setCheckable(true);
    passAct->setChecked(clickThrough());
    connect(passAct, &QAction::toggled, &window_, [this](bool on) { setClickThrough(on); });

    if (manager_.petCount() > 1) {
        sub.addSeparator();
        auto* dismiss = sub.addAction(QStringLiteral("收起此桌宠"));
        connect(dismiss, &QAction::triggered, &window_, [this] {
            const QString id = roleId_;
            QTimer::singleShot(0, &manager_, [mgr = &manager_, id] { mgr->dismissRole(id); });
        });
    }
}

// ============================================================================
// DesktopPetManager
// ============================================================================

DesktopPetManager::DesktopPetManager(RoleLibrary& library, const QString& libraryError,
                                     SettingsStore& stored, FocusSession& focus,
                                     VoiceTraining& voiceTraining, QString builtInPath)
    : library_(library),
      libraryError_(libraryError),
      stored_(stored),
      focus_(focus),
      voiceTraining_(voiceTraining),
      builtInPath_(std::move(builtInPath))
{
    connect(&focus_, &FocusSession::changed, this, [this] {
        for (auto& pet : pets_) {
            pet->syncQuietMode();
        }
    });
    connect(&focus_, &FocusSession::focusStarted, this, [this] {
        if (focus_.doNotDisturb()) {
            return;
        }
        for (auto& pet : pets_) {
            if (pet->window().isVisible()) {
                QString err;
                const auto events = library_.shareFor(pet->roleId(), err, false)
                                        .behavior.value(QStringLiteral("events")).toObject();
                pet->runBinding(events.value(eventKey(CompanionEvent::FocusStarted)).toObject(), true);
            }
        }
    });
    connect(&focus_, &FocusSession::breakStarted, this, [this] {
        if (focus_.doNotDisturb()) {
            return;
        }
        for (auto& pet : pets_) {
            if (pet->window().isVisible()) {
                pet->runEvent(CompanionEvent::BreakStarted);
            }
        }
    });
    connect(&focus_, &FocusSession::breakCompleted, this, [this] {
        if (!focus_.doNotDisturb() && stored_.native().value(reminderKey, false).toBool()) {
            sendCompanionReminder(QStringLiteral("休息结束，可以开始下一轮专注。"));
        }
        if (focus_.doNotDisturb()) {
            return;
        }
        bool announced = false;
        for (auto& pet : pets_) {
            if (pet->window().isVisible()) {
                if (!announced) {
                    pet->announceBanner(QStringLiteral("休息结束 · 可以开始下一轮专注"));
                    announced = true;
                }
                pet->runEvent(CompanionEvent::BreakFinished);
            }
        }
    });
    connect(&focus_, &FocusSession::completed, this, [this] {
        if (!focus_.doNotDisturb() && stored_.native().value(reminderKey, false).toBool()) {
            sendCompanionReminder(
                QStringLiteral("专注结束，建议休息 %1 分钟。").arg(focus_.recommendedBreakMinutes()));
        }
        if (focus_.doNotDisturb()) {
            return;
        }
        bool firstVisible = true;
        for (auto& pet : pets_) {
            if (pet->window().isVisible()) {
                pet->handleFocusCompleted(firstVisible, firstVisible);
                firstVisible = false;
            }
        }
    });

    for (auto* screen : QGuiApplication::screens()) {
        watchScreen(screen);
    }
    connect(qApp, &QGuiApplication::screenAdded, this, &DesktopPetManager::watchScreen);
    connect(qApp, &QGuiApplication::screenRemoved, this, &DesktopPetManager::clampAllToScreen, Qt::QueuedConnection);
    connect(qApp, &QGuiApplication::primaryScreenChanged, this, [this](QScreen*) {
        setPrimaryScreenOnly(stored_.primaryScreenOnly());
    });

    reminderTimer_.setInterval(5000);
    connect(&reminderTimer_, &QTimer::timeout, this, &DesktopPetManager::checkReminders);
    reminderTimer_.start();

#ifdef Q_OS_MACOS
    windowLayers_.spaceSwitched = [this](const QList<PetWindow*>& switchedWindows, int spaceDelta) {
        if (hidden_ || switchedWindows.isEmpty()) {
            return;
        }
        struct ScreenGroup {
            const QScreen* screen = nullptr;
            QList<PetInstance*> pets;
        };
        QList<ScreenGroup> groups;
        for (PetWindow* w : switchedWindows) {
            PetInstance* owner = nullptr;
            for (const auto& pet : pets_) {
                if (&pet->window() == w) {
                    owner = pet.get();
                    break;
                }
            }
            if (!owner || !owner->followAllSpaces() || !owner->window().isVisible()) {
                continue;
            }
            const QScreen* targetScreen = owner->window().screen()
                ? owner->window().screen()
                : QGuiApplication::primaryScreen();
            bool grouped = false;
            for (auto& g : groups) {
                if (g.screen == targetScreen) {
                    g.pets.append(owner);
                    grouped = true;
                    break;
                }
            }
            if (!grouped) {
                groups.append({targetScreen, {owner}});
            }
        }

        for (const auto& group : std::as_const(groups)) {
            const QRect workArea = group.screen
                ? group.screen->availableGeometry()
                : QRect(0, 0, 1440, 900);
            const SpaceTransitMode transitMode = stored_.spaceTransitMode();
            std::vector<SpaceTransitInput> inputs;
            inputs.reserve(static_cast<size_t>(group.pets.size()));
            for (PetInstance* pet : group.pets) {
                const SpaceMotionStyle motionStyle = resolveSpaceMotionStyle(transitMode, *QRandomGenerator::global());
                inputs.push_back({
                    workArea,
                    pet->window().size(),
                    pet->preferredSpaceLandingPos(),
                    spaceDelta,
                    pet->activityPercent(),
                    motionStyle,
                });
            }
            const auto plans = planSpaceTransits(inputs);
            for (size_t i = 0; i < plans.size(); ++i) {
                PetInstance* pet = group.pets[static_cast<qsizetype>(i)];
                const SpaceTransitPlan& plan = plans[i];
                pet->prepareSpaceTransit(plan.landingPos.toPoint());
                windowLayers_.movePetToActiveSpace(pet->window(), plan.startPos.toPoint(),
                                                   windowLayers_.isCurrentSpaceFullScreen());
                pet->startSpaceTransit(plan);
            }
        }
    };
#endif
}

DesktopPetManager::~DesktopPetManager()
{
    saveAllStates();
}

void DesktopPetManager::watchScreen(QScreen* screen)
{
    if (screen) {
        connect(screen, &QScreen::availableGeometryChanged, this,
                &DesktopPetManager::clampAllToScreen, Qt::QueuedConnection);
    }
}

void DesktopPetManager::recordInteraction()
{
    focus_.recordInteraction();
}

int DesktopPetManager::petIndex(const QString& roleId) const
{
    for (size_t i = 0; i < pets_.size(); ++i) {
        if (pets_[i]->roleId() == roleId) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

PetInstance* DesktopPetManager::findPet(const QString& roleId) const
{
    for (const auto& pet : pets_) {
        if (pet->roleId() == roleId) {
            return pet.get();
        }
    }
    return nullptr;
}

bool DesktopPetManager::spawnPet(const QString& roleId, QString& error)
{
    if (findPet(roleId)) {
        return true;
    }
    QList<QRect> occupied;
    occupied.reserve(static_cast<qsizetype>(pets_.size()));
    for (const auto& existing : pets_) {
        occupied.append(existing->window().frameGeometry());
    }
    auto instance = std::make_unique<PetInstance>(*this, roleId);
    if (!instance->initialize(occupied, error)) {
        return false;
    }
    pets_.push_back(std::move(instance));
    return true;
}

bool DesktopPetManager::startInitialPets(const QString& commandLineAsset, const QString& legacyPath,
                                         const QString& soleWorkingGif, QString& error)
{
    if (!commandLineAsset.isEmpty()) {
        auto asset = loadPetAsset(commandLineAsset, error);
        if (!asset) {
            return false;
        }
        if (!library_.available()) {
            error = QStringLiteral("角色库尚未打开。");
            return false;
        }
        const QString addedId = library_.add(QFileInfo(commandLineAsset).completeBaseName(), asset->source, error);
        if (addedId.isEmpty() || !spawnPet(addedId, error)) {
            return false;
        }
        stored_.setActiveRoles(activeRoleIds());
        return true;
    }

    if (library_.available()) {
        const QString legacyRole = (!GIFPET_START_WITH_KAIMAO && QFileInfo::exists(legacyPath))
            ? RoleLibrary::legacyId()
            : QString{};
        QStringList savedRoles = stored_.activeRoles(legacyRole);
        if (!stored_.multiPetEnabled() && savedRoles.size() > 1) {
            savedRoles = {savedRoles.constFirst()};
        }
        for (const QString& id : std::as_const(savedRoles)) {
            if (id == RoleLibrary::builtInId()) {
                continue;
            }
            QString restoreErr;
            if (!spawnPet(id, restoreErr)) {
                qWarning().noquote() << QStringLiteral("无法恢复角色 %1：").arg(id) << restoreErr;
            }
        }
        if (pets_.empty()) {
            QString entriesErr;
            const auto availableRoles = library_.entries(entriesErr);
            if (!availableRoles.isEmpty()) {
                QString spawnErr;
                spawnPet(availableRoles.constFirst().id, spawnErr);
            }
        }
        if (pets_.empty() && !soleWorkingGif.isEmpty()) {
            QString gifErr;
            if (auto asset = loadPetAsset(soleWorkingGif, gifErr)) {
                const QString id = library_.add(QFileInfo(soleWorkingGif).completeBaseName(), asset->source, gifErr);
                if (!id.isEmpty()) {
                    spawnPet(id, gifErr);
                }
            }
        }
    }

    stored_.setActiveRoles(activeRoleIds());
    return true;
}

QStringList DesktopPetManager::activeRoleIds() const
{
    QStringList ids;
    ids.reserve(static_cast<qsizetype>(pets_.size()));
    for (const auto& pet : pets_) {
        ids.append(pet->roleId());
    }
    return ids;
}

void DesktopPetManager::setDesktopHidden(bool hidden)
{
    hidden_ = hidden;
    for (auto& pet : pets_) {
        if (hidden_) {
#ifdef Q_OS_MACOS
            pet->cancelSpaceTransit(true);
#endif
            pet->window().hide();
        } else {
            pet->window().placeAt(pet->window().pos());
            pet->window().show();
        }
    }
}

void DesktopPetManager::toggleDesktopVisibility()
{
    setDesktopHidden(!hidden_);
}

void DesktopPetManager::showAndRaiseAll()
{
    hidden_ = false;
    for (auto& pet : pets_) {
        pet->window().placeAt(pet->window().pos());
        pet->window().show();
        pet->window().raise();
    }
}

bool DesktopPetManager::anyClickThrough() const
{
    for (const auto& pet : pets_) {
        if (pet->clickThrough()) {
            return true;
        }
    }
    return false;
}

void DesktopPetManager::setAllClickThrough(bool enabled)
{
    for (auto& pet : pets_) {
        pet->setClickThrough(enabled);
    }
}

bool DesktopPetManager::allFollowSpaces() const
{
    if (pets_.empty()) {
        return stored_.followAllSpaces({});
    }
    for (const auto& pet : pets_) {
        if (!pet->followAllSpaces()) {
            return false;
        }
    }
    return true;
}

void DesktopPetManager::setAllFollowSpaces(bool enabled)
{
    stored_.native().setValue(QStringLiteral("pet/followAllSpaces"), enabled);
    for (auto& pet : pets_) {
        pet->setFollowAllSpaces(enabled);
    }
}

SpaceTransitMode DesktopPetManager::spaceTransitMode() const
{
    return stored_.spaceTransitMode();
}

void DesktopPetManager::setSpaceTransitMode(SpaceTransitMode mode)
{
    stored_.setSpaceTransitMode(mode);
}

bool DesktopPetManager::allAlwaysOnTop() const
{
    if (pets_.empty()) {
        return stored_.alwaysOnTop({});
    }
    for (const auto& pet : pets_) {
        if (!pet->alwaysOnTop()) {
            return false;
        }
    }
    return true;
}

void DesktopPetManager::setAllAlwaysOnTop(bool enabled)
{
    stored_.native().setValue(QStringLiteral("pet/alwaysOnTop"), enabled);
    for (auto& pet : pets_) {
        pet->setAlwaysOnTop(enabled);
    }
}

bool DesktopPetManager::showActivityMeter() const
{
    return stored_.showActivityMeter();
}

void DesktopPetManager::setShowActivityMeter(bool enabled)
{
    stored_.setShowActivityMeter(enabled);
    for (auto& pet : pets_) {
        pet->setActivityMeterEnabled(enabled);
    }
}

FocusMotionPolicy DesktopPetManager::focusMotionPolicy() const
{
    return stored_.focusMotionPolicy();
}

void DesktopPetManager::setFocusMotionPolicy(FocusMotionPolicy policy)
{
    stored_.setFocusMotionPolicy(policy);
    for (auto& pet : pets_) {
        pet->syncQuietMode();
    }
}

ForegroundContext DesktopPetManager::currentForegroundContext() const
{
    ForegroundContext fg = queryForegroundContext();
#ifdef Q_OS_MACOS
    if (!fg.selfAppActive && windowLayers_.isCurrentSpaceFullScreen()) {
        fg.externalFullScreen = true;
    }
#endif
    return fg;
}

void DesktopPetManager::setPrimaryScreenOnly(bool primaryOnly)
{
    stored_.setPrimaryScreenOnly(primaryOnly);
    for (auto& pet : pets_) {
        pet->applyPrimaryOnly(primaryOnly);
    }
}

void DesktopPetManager::moveAllToDisplay(const QScreen* screen)
{
    if (!screen) {
        return;
    }
    for (auto& pet : pets_) {
        pet->moveToDisplay(screen);
    }
}

void DesktopPetManager::clampAllToScreen()
{
    for (auto& pet : pets_) {
        pet->clampToScreen();
    }
}

void DesktopPetManager::saveAllStates()
{
    for (auto& pet : pets_) {
        pet->saveState();
    }
    stored_.setActiveRoles(activeRoleIds());
    stored_.native().sync();
}

void DesktopPetManager::applyActiveRoles(const QStringList& requestedIds, bool multiPetEnabled)
{
    stored_.setMultiPetEnabled(multiPetEnabled);
    QStringList desired;
    for (const QString& id : requestedIds) {
        if (!id.isEmpty() && id != RoleLibrary::builtInId() && !desired.contains(id)) {
            desired.append(id);
            if (!multiPetEnabled) {
                break;
            }
        }
    }

    for (qsizetype i = static_cast<qsizetype>(pets_.size()) - 1; i >= 0; --i) {
        const auto idx = static_cast<size_t>(i);
        if (!desired.contains(pets_[idx]->roleId())) {
            pets_[idx]->saveState();
            pets_[idx]->window().hide();
            auto removed = std::move(pets_[idx]);
            pets_.erase(pets_.begin() + i);
            removed.release()->deleteLater();
        }
    }

    std::vector<std::unique_ptr<PetInstance>> ordered;
    ordered.reserve(static_cast<size_t>(desired.size()));
    for (const QString& id : std::as_const(desired)) {
        std::unique_ptr<PetInstance> found;
        for (auto it = pets_.begin(); it != pets_.end(); ++it) {
            if ((*it)->roleId() == id) {
                found = std::move(*it);
                pets_.erase(it);
                break;
            }
        }
        if (found) {
            if (!hidden_) {
                found->window().show();
            }
            ordered.push_back(std::move(found));
        } else {
            QList<QRect> occupied;
            for (const auto& p : ordered) {
                occupied.append(p->window().frameGeometry());
            }
            for (const auto& p : pets_) {
                occupied.append(p->window().frameGeometry());
            }
            auto created = std::make_unique<PetInstance>(*this, id);
            QString err;
            if (created->initialize(occupied, err)) {
                ordered.push_back(std::move(created));
            } else {
                QMessageBox::warning(nullptr, QStringLiteral("无法展示桌宠"), err);
            }
        }
    }
    pets_ = std::move(ordered);
    hidden_ = false;
    stored_.setActiveRoles(activeRoleIds());
}

void DesktopPetManager::dismissRole(const QString& roleId)
{
    if (pets_.size() <= 1) {
        return;
    }
    auto current = activeRoleIds();
    if (current.removeAll(roleId) > 0) {
        applyActiveRoles(current, stored_.multiPetEnabled());
    }
}

void DesktopPetManager::refreshRole(const QString& roleId, bool reloadIdleAsset)
{
    if (auto* pet = findPet(roleId)) {
        pet->refreshFromLibrary(reloadIdleAsset);
    }
}

void DesktopPetManager::refreshAllMetadata()
{
    for (auto& pet : pets_) {
        pet->refreshFromLibrary(false);
    }
}

void DesktopPetManager::handleRoleRemoved(const QString& roleId)
{
    stored_.removeRole(roleId);
}

void DesktopPetManager::openRoleLibrary(const QString& preferredRole)
{
    if (libraryDialogOpen_) {
        return;
    }
    if (!library_.available()) {
        QMessageBox::warning(nullptr, QStringLiteral("无法打开角色库"), libraryError_);
        return;
    }
    QScopedValueRollback<bool> guard(libraryDialogOpen_, true);
    RoleLibraryContext ctx;
    ctx.builtInAsset = builtInPath_;
    ctx.preferredRole = !preferredRole.isEmpty() ? preferredRole : activeRoleIds().value(0);
    ctx.activeRoles = activeRoleIds();
    ctx.multiPetEnabled = stored_.multiPetEnabled() || pets_.size() > 1;
    ctx.voiceTraining = &voiceTraining_;
    ctx.selectionChanged = [this](const QStringList& roles, bool multi) {
        applyActiveRoles(roles, multi);
    };
    ctx.roleUpdated = [this](const QString& id) {
        refreshRole(id, true);
    };
    ctx.roleRemoved = [this](const QString& id) {
        handleRoleRemoved(id);
    };

    const auto choice = manageRoles(library_, ctx, nullptr);
    for (const QString& id : choice.removed) {
        stored_.removeRole(id);
    }
    applyActiveRoles(choice.activeRoles, choice.multiPetEnabled);
    for (const QString& id : choice.changed) {
        refreshRole(id, true);
    }
}

void DesktopPetManager::promptFirstPetImport()
{
    if (!pets_.empty()) {
        return;
    }
    QMessageBox box;
    box.setWindowTitle(QStringLiteral("欢迎使用 AuraPet"));
    box.setIcon(QMessageBox::Information);
    box.setText(QStringLiteral("导入你的第一位桌面伙伴"));
    box.setInformativeText(QStringLiteral(
        "AuraPet 采用精简无内置角色设计，完全由你自由导入喜欢的形象。\n"
        "支持透明 GIF、PNG、WebP、AVIF 图片或 .gifpet 角色分享包，之后也可随时通过顶部菜单栏或系统托盘导入。"));
    auto* importBtn = box.addButton(QStringLiteral("导入素材 / 角色包…"), QMessageBox::AcceptRole);
    auto* libraryBtn = box.addButton(QStringLiteral("打开角色库…"), QMessageBox::ActionRole);
    box.addButton(QStringLiteral("稍后再说"), QMessageBox::RejectRole);
    box.setDefaultButton(importBtn);
    box.exec();
    if (box.clickedButton() == importBtn) {
        pickAndImportAsset(true);
    } else if (box.clickedButton() == libraryBtn) {
        openRoleLibrary();
    }
}

void DesktopPetManager::pickAndImportAsset(bool preview)
{
    QWidget* parent = pets_.empty() ? nullptr : &pets_.front()->window();
    const auto path = QFileDialog::getOpenFileName(
        parent, QStringLiteral("导入桌宠素材或角色包"), {},
        QStringLiteral("桌宠素材与角色包 (*.png *.jpg *.jpeg *.gif *.agif *.webp *.avif *.gifpet);;所有文件 (*)"));
    if (!path.isEmpty()) {
        enqueueImport(path, preview);
    }
}

void DesktopPetManager::enqueueImport(const QString& path, bool preview)
{
    if (path.isEmpty()) {
        return;
    }
    const auto absolute = QFileInfo(path).absoluteFilePath();
    if (absolute == workingImport_) {
        return;
    }
    for (const auto& entry : std::as_const(pendingImports_)) {
        if (entry.first == absolute) {
            return;
        }
    }
    pendingImports_.enqueue(qMakePair(absolute, preview));
    if (!drainingImports_) {
        QTimer::singleShot(0, this, &DesktopPetManager::drainImports);
    }
}

void DesktopPetManager::drainImports()
{
    if (drainingImports_ || pendingImports_.isEmpty()) {
        return;
    }
    if (QApplication::activeModalWidget() || QApplication::activePopupWidget()) {
        QTimer::singleShot(150, this, &DesktopPetManager::drainImports);
        return;
    }
    QScopedValueRollback<bool> running(drainingImports_, true);
    while (!pendingImports_.isEmpty()) {
        const auto [path, preview] = pendingImports_.dequeue();
        QScopedValueRollback<QString> current(workingImport_, path);
        importPath(path, preview);
    }
}

void DesktopPetManager::importPath(const QString& path, bool preview)
{
    QWidget* anchor = pets_.empty() ? nullptr : &pets_.front()->window();
    QWidget fallbackParent;
    QWidget& dialogParent = anchor ? *anchor : fallbackParent;

    if (QFileInfo(path).suffix().compare(rolePackageSuffix, Qt::CaseInsensitive) == 0) {
        const auto id = importRolePackage(dialogParent, library_, path);
        if (id.isEmpty()) {
            return;
        }
        if (findPet(id)) {
            refreshRole(id, true);
        } else if (stored_.multiPetEnabled()) {
            auto roles = activeRoleIds();
            roles.append(id);
            applyActiveRoles(roles, true);
        } else {
            applyActiveRoles({id}, false);
        }
        return;
    }

    using Loading = QPair<std::optional<PreparedPetAsset>, QString>;
    auto loaded = waitForBackground<Loading>(dialogParent, QStringLiteral("正在读取图片或动图…"),
        [path](QPromise<Loading>&) {
            Loading result;
            result.first = loadPetPixels(path, result.second);
            return result;
        });
    if (!loaded) {
        return;
    }
    if (!loaded->first) {
        QMessageBox::warning(&dialogParent, QStringLiteral("导入失败"), loaded->second);
        return;
    }
    std::optional<PetAsset> asset(makePlayableAsset(std::move(*loaded->first)));
    QString error;
#ifdef Q_OS_MACOS
    if (preview) {
        asset = previewPetImport(std::move(*asset), &dialogParent, error);
    }
#else
    Q_UNUSED(preview);
#endif
    if (!asset) {
        if (!error.isEmpty()) {
            QMessageBox::warning(&dialogParent, QStringLiteral("导入失败"), error);
        }
        return;
    }
    if (!library_.available()) {
        QMessageBox::warning(&dialogParent, QStringLiteral("导入失败"), QStringLiteral("角色库尚未打开。"));
        return;
    }
    const QString newId = library_.add(QFileInfo(path).completeBaseName(), asset->source, error);
    if (newId.isEmpty()) {
        QMessageBox::warning(&dialogParent, QStringLiteral("导入失败"), error);
        return;
    }
    if (stored_.multiPetEnabled()) {
        auto roles = activeRoleIds();
        roles.append(newId);
        applyActiveRoles(roles, true);
    } else {
        applyActiveRoles({newId}, false);
    }
}

void DesktopPetManager::backupLibrary(QWidget* parent)
{
    const auto name = QStringLiteral("AuraPet-%1.sqlite3")
                          .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const auto path = QFileDialog::getSaveFileName(
        parent, QStringLiteral("保存角色库备份"), QDir::home().filePath(name),
        QStringLiteral("AuraPet 角色库 (*.sqlite3)"), nullptr, QFileDialog::DontConfirmOverwrite);
    if (path.isEmpty()) {
        return;
    }
    const QString dbPath = library_.databasePath();
    QWidget fallbackParent;
    QWidget& anchor = parent ? *parent : (pets_.empty() ? fallbackParent : pets_.front()->window());
    using BackupResult = QPair<bool, QString>;
    const auto done = waitForBackground<BackupResult>(anchor, QStringLiteral("正在后台生成角色库备份…"),
        [dbPath, path](QPromise<BackupResult>&) {
            BackupResult res;
            res.first = RoleLibrary::backupDatabaseFile(dbPath, path, res.second);
            return res;
        });
    if (!done) {
        return;
    }
    if (!done->first) {
        QMessageBox::warning(parent, QStringLiteral("备份失败"), done->second);
    } else {
        QMessageBox::information(parent, QStringLiteral("备份已保存"), QDir::toNativeSeparators(path));
    }
}

void DesktopPetManager::checkReminders()
{
    if (pets_.empty()) {
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    const ForegroundContext fg = currentForegroundContext();
    bool voiceBusy = false;
    for (const auto& pet : pets_) {
        pet->syncCompanionRhythm(fg, now);
        pet->saveState(false);
        voiceBusy = voiceBusy || pet->isVoiceBusy();
    }
    const int count = static_cast<int>(pets_.size());
    for (int offset = 0; offset < count; ++offset) {
        const int idx = (reminderCursor_ + offset) % count;
        if (pets_[static_cast<size_t>(idx)]->pollReminder(now, voiceBusy)) {
            reminderCursor_ = (idx + 1) % count;
            return;
        }
    }
    reminderCursor_ = (reminderCursor_ + 1) % count;
}

void DesktopPetManager::populateAppSettingsMenu(QMenu& settingsMenu, QWidget* dialogParent)
{
    settingsMenu.setToolTipsVisible(true);
    auto* showMeterAction = settingsMenu.addAction(QCoreApplication::translate("DesktopMenu", "显示活力条"));
    showMeterAction->setCheckable(true);
    showMeterAction->setToolTip(QStringLiteral("取消勾选后，悬停、点击或投喂桌宠时均不再浮现活力条"));
    const auto syncMeterSetting = [this, showMeterAction] {
        QSignalBlocker block(showMeterAction);
        showMeterAction->setChecked(showActivityMeter());
    };
    syncMeterSetting();
    connect(&settingsMenu, &QMenu::aboutToShow, &settingsMenu, syncMeterSetting);
    connect(showMeterAction, &QAction::triggered, &settingsMenu, [this](bool on) {
        setShowActivityMeter(on);
    });
    settingsMenu.addSeparator();

    auto* focusMotionHeader = settingsMenu.addAction(QStringLiteral("专注计时桌宠表现"));
    focusMotionHeader->setEnabled(false);
    auto* focusMotionGroup = new QActionGroup(&settingsMenu);
    auto* microMoveAct = settingsMenu.addAction(QStringLiteral("静伴微动（小幅低频，不打扰）"));
    auto* stillAct = settingsMenu.addAction(QStringLiteral("完全静止"));
    microMoveAct->setData(static_cast<int>(FocusMotionPolicy::GentleMicroMove));
    stillAct->setData(static_cast<int>(FocusMotionPolicy::CompletelyStill));
    microMoveAct->setToolTip(QStringLiteral("专注计时期间桌宠仅在原地附近低频缓慢微动，且保持静音不弹窗"));
    stillAct->setToolTip(QStringLiteral("专注计时期间桌宠完全静止在原地，静音且不主动发言"));
    for (auto* act : {microMoveAct, stillAct}) {
        act->setCheckable(true);
        focusMotionGroup->addAction(act);
    }
    const auto syncFocusMotion = [this, microMoveAct, stillAct] {
        const FocusMotionPolicy current = focusMotionPolicy();
        microMoveAct->setChecked(current == FocusMotionPolicy::GentleMicroMove);
        stillAct->setChecked(current == FocusMotionPolicy::CompletelyStill);
    };
    syncFocusMotion();
    connect(&settingsMenu, &QMenu::aboutToShow, &settingsMenu, syncFocusMotion);
    connect(focusMotionGroup, &QActionGroup::triggered, &settingsMenu, [this](QAction* selected) {
        if (selected) {
            setFocusMotionPolicy(static_cast<FocusMotionPolicy>(selected->data().toInt()));
        }
    });
    settingsMenu.addSeparator();

    auto* transitHeader = settingsMenu.addAction(QStringLiteral("跨桌面进入动作"));
    transitHeader->setEnabled(false);

    auto* transitGroup = new QActionGroup(&settingsMenu);
    auto* mixedAct = settingsMenu.addAction(QStringLiteral("随机组合（跳动与正常移动）"));
    auto* jumpAct = settingsMenu.addAction(QStringLiteral("仅跳动进入"));
    auto* walkAct = settingsMenu.addAction(QStringLiteral("仅正常移动"));
    mixedAct->setData(static_cast<int>(SpaceTransitMode::MixedRandom));
    jumpAct->setData(static_cast<int>(SpaceTransitMode::JumpOnly));
    walkAct->setData(static_cast<int>(SpaceTransitMode::WalkOnly));
    mixedAct->setToolTip(QStringLiteral("切换桌面进入时，在轻快跳入与平滑走入之间随机搭配"));
    jumpAct->setToolTip(QStringLiteral("每次切换桌面都以抛物线跳入新桌面"));
    walkAct->setToolTip(QStringLiteral("每次切换桌面都以平缓直行的方式走入新桌面"));
    for (auto* act : {mixedAct, jumpAct, walkAct}) {
        act->setCheckable(true);
        transitGroup->addAction(act);
    }
    const auto syncTransitMode = [this, mixedAct, jumpAct, walkAct] {
        const SpaceTransitMode current = spaceTransitMode();
        mixedAct->setChecked(current == SpaceTransitMode::MixedRandom);
        jumpAct->setChecked(current == SpaceTransitMode::JumpOnly);
        walkAct->setChecked(current == SpaceTransitMode::WalkOnly);
    };
    syncTransitMode();
    connect(&settingsMenu, &QMenu::aboutToShow, &settingsMenu, syncTransitMode);
    connect(transitGroup, &QActionGroup::triggered, &settingsMenu, [this](QAction* selected) {
        if (selected) {
            setSpaceTransitMode(static_cast<SpaceTransitMode>(selected->data().toInt()));
        }
    });
    settingsMenu.addSeparator();

    const QPointer<QWidget> anchor(dialogParent);
    auto* loginItem = settingsMenu.addAction(QCoreApplication::translate("DesktopMenu", "登录时启动 AuraPet"));
    loginItem->setCheckable(true);
    const auto refreshLogin = [loginItem] {
        const auto state = loginItemState();
        loginItem->setEnabled(state != LoginItemState::Unavailable);
        loginItem->setChecked(state == LoginItemState::Enabled || state == LoginItemState::NeedsApproval);
    };
    connect(&settingsMenu, &QMenu::aboutToShow, loginItem, refreshLogin);
    connect(loginItem, &QAction::triggered, loginItem, [anchor, refreshLogin](bool on) {
        QString error;
        if (!setLoginItem(on, error)) {
            QMessageBox::warning(anchor.data(), QStringLiteral("登录项"), error);
        } else if (on && loginItemState() == LoginItemState::NeedsApproval) {
            openLoginItemSettings();
        }
        refreshLogin();
    });

    auto* notifications = settingsMenu.addAction(QCoreApplication::translate("DesktopMenu", "专注和休息的系统通知"));
    notifications->setCheckable(true);
    connect(&settingsMenu, &QMenu::aboutToShow, notifications, [this, notifications] {
        notifications->setChecked(stored_.native().value(reminderKey, false).toBool());
    });
    connect(notifications, &QAction::triggered, notifications, [this](bool on) {
        stored_.native().setValue(reminderKey, on);
        if (on) {
            requestReminderPermission();
        }
    });

    if (hotkeyMenuBuilder_) {
        hotkeyMenuBuilder_(settingsMenu);
    }
    settingsMenu.addSeparator();
    connect(settingsMenu.addAction(QCoreApplication::translate("DesktopMenu", "查看新版本…")),
            &QAction::triggered, &settingsMenu, [] {
                QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/StewardAndrecher/AuraPet/releases/latest")));
            });
}

void DesktopPetManager::setupGlobalMenu(QMenu& target)
{
    auto* libraryAction = target.addAction(
        menuGlyphIcon(MenuGlyph::Gallery),
        QCoreApplication::translate("DesktopMenu", "角色库…"));
    connect(libraryAction, &QAction::triggered, this, [this] {
        QTimer::singleShot(0, this, [this] { openRoleLibrary(); });
    });

    auto* activePetsMenu = target.addMenu(menuGlyphIcon(MenuGlyph::PetsGroup), QStringLiteral("当前桌面角色"));
    connect(activePetsMenu, &QMenu::aboutToShow, this, [this, activePetsMenu] {
        activePetsMenu->clear();
        if (pets_.empty()) {
            auto* emptyAct = activePetsMenu->addAction(QStringLiteral("尚未导入角色 · 点击导入…"));
            connect(emptyAct, &QAction::triggered, this, [this] { pickAndImportAsset(true); });
            return;
        }
        for (const auto& pet : pets_) {
            auto* sub = activePetsMenu->addMenu(pet->roleName());
            pet->populateSubmenu(*sub);
        }
    });
    connect(&target, &QMenu::aboutToShow, this, [this, activePetsMenu] {
        activePetsMenu->setTitle(QStringLiteral("桌面角色（%1）").arg(pets_.size()));
    });

    if (!pets_.empty()) {
        auto* companionSub = addCompanionMenu(target, pets_.front()->window(), focus_);
        companionSub->setIcon(menuGlyphIcon(MenuGlyph::Companion));
    }

    target.addSeparator();

    auto* displayMenu = target.addMenu(menuGlyphIcon(MenuGlyph::Appearance), QStringLiteral("屏幕与布局"));
    displayMenu->setToolTipsVisible(true);
    auto* globalFollowSpace = displayMenu->addAction(QStringLiteral("跟随切换桌面"));
    globalFollowSpace->setCheckable(true);
    globalFollowSpace->setToolTip(QStringLiteral("开启后切换桌面空间时，桌宠从相邻一侧跃入新桌面并停留在桌面之间较近的一端"));
    connect(globalFollowSpace, &QAction::triggered, this, [this](bool checked) {
        setAllFollowSpaces(checked);
    });

    auto* globalTopLayer = displayMenu->addAction(QStringLiteral("保持浮现在最上层"));
    globalTopLayer->setCheckable(true);
    globalTopLayer->setToolTip(QStringLiteral("关闭时桌宠处于「活跃窗口下第二层」，仅被当前活跃窗口掩盖，不被旧窗口持续掩盖"));
    connect(globalTopLayer, &QAction::triggered, this, [this](bool checked) {
        setAllAlwaysOnTop(checked);
    });
    displayMenu->addSeparator();

    connect(displayMenu->addAction(QStringLiteral("全部移到鼠标所在屏幕")), &QAction::triggered, this, [this] {
        if (const auto* screen = QGuiApplication::screenAt(QCursor::pos())) {
            if (stored_.primaryScreenOnly()) {
                setPrimaryScreenOnly(false);
            }
            moveAllToDisplay(screen);
        }
    });
    connect(displayMenu->addAction(QStringLiteral("全部移到主屏幕")), &QAction::triggered, this, [this] {
        moveAllToDisplay(QGuiApplication::primaryScreen());
    });
    displayMenu->addSeparator();
    auto* displayPolicies = new QActionGroup(displayMenu);
    auto* currentDisplay = displayMenu->addAction(QStringLiteral("停留在当前所在屏幕"));
    auto* primaryDisplay = displayMenu->addAction(QStringLiteral("固定在主屏幕"));
    for (auto* act : {currentDisplay, primaryDisplay}) {
        act->setCheckable(true);
        displayPolicies->addAction(act);
    }
    connect(&target, &QMenu::aboutToShow, this, [this, currentDisplay, primaryDisplay, globalFollowSpace, globalTopLayer] {
        const bool primary = stored_.primaryScreenOnly();
        primaryDisplay->setChecked(primary);
        currentDisplay->setChecked(!primary);
        QSignalBlocker b1(globalFollowSpace);
        globalFollowSpace->setChecked(allFollowSpaces());
        QSignalBlocker b2(globalTopLayer);
        globalTopLayer->setChecked(allAlwaysOnTop());
    });
    connect(displayPolicies, &QActionGroup::triggered, this, [this, primaryDisplay](QAction* selected) {
        setPrimaryScreenOnly(selected == primaryDisplay);
    });

    auto* clickThroughAction = target.addAction(
        menuGlyphIcon(MenuGlyph::ClickThrough),
        QCoreApplication::translate("DesktopMenu", "鼠标穿透"));
    clickThroughAction->setCheckable(true);
    connect(clickThroughAction, &QAction::triggered, this, [this](bool checked) {
        setAllClickThrough(checked);
    });

    auto* visibilityAction = target.addAction(
        menuGlyphIcon(MenuGlyph::Visibility),
        QCoreApplication::translate("DesktopMenu", "隐藏桌宠"));
    connect(visibilityAction, &QAction::triggered, this, &DesktopPetManager::toggleDesktopVisibility);

    connect(&target, &QMenu::aboutToShow, this, [this, visibilityAction, clickThroughAction] {
        visibilityAction->setText(hidden_
            ? QCoreApplication::translate("DesktopMenu", "显示桌宠")
            : QCoreApplication::translate("DesktopMenu", "隐藏桌宠"));
        QSignalBlocker block(clickThroughAction);
        clickThroughAction->setChecked(anyClickThrough());
    });

    target.addSeparator();

    auto* assetsMenu = target.addMenu(menuGlyphIcon(MenuGlyph::ImportBackup), QStringLiteral("导入与备份"));
    connect(assetsMenu->addAction(QStringLiteral("导入图片或动图（智能抠图）…")),
            &QAction::triggered, this, [this] { pickAndImportAsset(true); });
    connect(assetsMenu->addAction(QStringLiteral("原样导入并播放…")),
            &QAction::triggered, this, [this] { pickAndImportAsset(false); });
    assetsMenu->addSeparator();
    connect(assetsMenu->addAction(QStringLiteral("备份本地角色库…")),
            &QAction::triggered, this, [this] { backupLibrary(); });

    connect(assetsMenu->addAction(QStringLiteral("本机千问语音服务…")),
            &QAction::triggered, this, [this] {
                if (!pets_.empty()) {
                    showVoiceService(pets_.front()->window());
                }
            });

    auto* appSettingsMenu = target.addMenu(menuGlyphIcon(MenuGlyph::Settings), QStringLiteral("设置"));
    populateAppSettingsMenu(*appSettingsMenu);

    auto* quitAction = target.addAction(
        menuGlyphIcon(MenuGlyph::Quit),
        QCoreApplication::translate("DesktopMenu", "退出 AuraPet"));
    quitAction->setMenuRole(QAction::QuitRole);
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    applyAuraMenuTheme(target);
}
