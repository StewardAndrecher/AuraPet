#include "role_library_dialog.h"
#include "action_editor.h"
#include "background_task.h"
#include "pet_asset.h"
#include "role_actions.h"
#include "role_library.h"
#include "role_package.h"
#include "voice_training.h"

#ifdef Q_OS_MACOS
#include "import_preview.h"
#endif

#include <QCheckBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QQueue>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include <optional>
#include <utility>

namespace {
constexpr int tileWidth = 202;
constexpr int tileHeight = 258;
constexpr int gridSpacing = 16;
constexpr int gridMargin = 18;

bool isDarkPalette(const QWidget& widget)
{
    return widget.palette().color(QPalette::Window).lightness() < 128;
}

QThreadPool* galleryDecodePool()
{
    static QThreadPool* pool = [] {
        auto* instance = new QThreadPool;
        instance->setMaxThreadCount(2);
        instance->setThreadPriority(QThread::LowPriority);
        instance->setExpiryTimeout(15000);
        return instance;
    }();
    return pool;
}

bool isSupportedDropFile(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == rolePackageSuffix
        || suffix == QStringLiteral("png")
        || suffix == QStringLiteral("jpg")
        || suffix == QStringLiteral("jpeg")
        || suffix == QStringLiteral("gif")
        || suffix == QStringLiteral("agif")
        || suffix == QStringLiteral("webp")
        || suffix == QStringLiteral("avif");
}

// Modern macOS-style pill toggle switch (inherits QCheckBox for full API compatibility)
class PillSwitchCheckBox : public QCheckBox {
public:
    explicit PillSwitchCheckBox(const QString& text, QWidget* parent = nullptr)
        : QCheckBox(text, parent)
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
    }

    QSize sizeHint() const override
    {
        const int textWidth = fontMetrics().horizontalAdvance(text());
        return QSize(38 + 8 + textWidth + 6, qMax(24, fontMetrics().height() + 6));
    }

protected:
    bool hitButton(const QPoint& pos) const override
    {
        return rect().contains(pos);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool dark = isDarkPalette(*this);
        const bool on = isChecked();
        const bool hovered = underMouse();

        const QRectF track(1, (height() - 20) / 2.0, 34, 20);
        QColor trackColor = on ? palette().color(QPalette::Highlight)
                               : (dark ? QColor(255, 255, 255, hovered ? 48 : 34)
                                       : QColor(0, 0, 0, hovered ? 38 : 26));
        if (on && hovered) {
            trackColor = trackColor.lighter(108);
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(trackColor);
        painter.drawRoundedRect(track, 10, 10);

        const qreal knobX = on ? (track.right() - 18) : (track.left() + 2);
        const QRectF knob(knobX, track.top() + 2, 16, 16);
        painter.setBrush(QColor(0, 0, 0, 35));
        painter.drawEllipse(knob.translated(0, 0.8));
        painter.setBrush(QColor(255, 255, 255));
        painter.drawEllipse(knob);

        painter.setPen(palette().color(QPalette::WindowText));
        const QRect textRect(43, 0, width() - 43, height());
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text());
    }
};

// Sleek toolbar / footer pill button
class ToolbarPillButton : public QPushButton {
public:
    explicit ToolbarPillButton(const QString& label, bool primary = false, QWidget* parent = nullptr)
        : QPushButton(label, parent), primary_(primary)
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setMinimumHeight(30);
    }

    QSize sizeHint() const override
    {
        return QSize(fontMetrics().horizontalAdvance(text()) + 28, 30);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool dark = isDarkPalette(*this);
        const bool hovered = underMouse() && isEnabled();
        const bool pressed = isDown() && isEnabled();

        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        if (primary_) {
            QColor bg = palette().color(QPalette::Highlight);
            if (pressed) {
                bg = bg.darker(112);
            } else if (hovered) {
                bg = bg.lighter(108);
            }
            painter.setPen(Qt::NoPen);
            painter.setBrush(bg);
            painter.drawRoundedRect(box, 8, 8);
            painter.setPen(palette().color(QPalette::HighlightedText));
        } else {
            QColor bg = dark ? QColor(255, 255, 255, pressed ? 36 : (hovered ? 26 : 16))
                             : QColor(0, 0, 0, pressed ? 24 : (hovered ? 14 : 7));
            QColor border = dark ? QColor(255, 255, 255, hovered ? 55 : 32)
                                 : QColor(0, 0, 0, hovered ? 38 : 22);
            painter.setPen(QPen(border, 1.0));
            painter.setBrush(bg);
            painter.drawRoundedRect(box, 8, 8);
            painter.setPen(palette().color(QPalette::WindowText));
        }
        QFont f = font();
        if (primary_) {
            f.setBold(true);
        }
        painter.setFont(f);
        painter.drawText(rect(), Qt::AlignCenter, text());
    }

private:
    bool primary_ = false;
};

// Pure backgroundless character preview stage (pre-scaled & viewport-throttled)
class RolePreviewStage : public QWidget {
public:
    explicit RolePreviewStage(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(174, 150);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void setLoading()
    {
        movie_.reset();
        firstFrame_ = {};
        scaledStatic_ = {};
        targetRect_ = {};
        animated_ = false;
        loading_ = true;
        movieStarted_ = false;
        requestCleanRepaint();
    }

    void setAsset(std::optional<PetAsset> asset)
    {
        movie_.reset();
        firstFrame_ = {};
        scaledStatic_ = {};
        targetRect_ = {};
        animated_ = false;
        loading_ = false;
        movieStarted_ = false;

        if (asset && !asset->firstFrame.isNull()) {
            firstFrame_ = std::move(asset->firstFrame);
            const QRect stage = rect().adjusted(8, 22, -8, -4);
            const QSize logical = firstFrame_.deviceIndependentSize().toSize().expandedTo(QSize(1, 1));
            const QSize fit = logical.scaled(stage.size(), Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
            targetRect_ = QRect(stage.x() + (stage.width() - fit.width()) / 2,
                                stage.y() + (stage.height() - fit.height()) / 2,
                                fit.width(), fit.height());
            const qreal dpr = qMax(1.0, devicePixelRatioF());
            const QSize pixelFit = (QSizeF(fit) * dpr).toSize().expandedTo(QSize(1, 1));

            if (asset->movie) {
                movie_ = std::move(asset->movie);
                movie_->setScaledSize(pixelFit);
                animated_ = true;
                connect(movie_.get(), &QMovie::frameChanged, this, [this] { requestCleanRepaint(); });
                if (isVisible() && inViewport_) {
                    startOrResume();
                }
            } else {
                scaledStatic_ = firstFrame_.scaled(pixelFit, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                scaledStatic_.setDevicePixelRatio(dpr);
            }
        }
        requestCleanRepaint();
    }

    void setViewportVisible(bool visible)
    {
        if (inViewport_ == visible) {
            return;
        }
        inViewport_ = visible;
        if (!movie_) {
            return;
        }
        if (inViewport_ && isVisible()) {
            startOrResume();
        } else if (movie_->state() == QMovie::Running) {
            movie_->setPaused(true);
        }
    }

    void setActiveBadge(bool active)
    {
        if (active_ != active) {
            active_ = active;
            requestCleanRepaint();
        }
    }

    bool isAnimated() const { return animated_; }
    bool isLoading() const { return loading_; }
    int currentFrameNumber() const { return movie_ ? movie_->currentFrameNumber() : 0; }

protected:
    void showEvent(QShowEvent* event) override
    {
        QWidget::showEvent(event);
        if (inViewport_) {
            startOrResume();
        }
    }

    void hideEvent(QHideEvent* event) override
    {
        if (movie_ && movie_->state() == QMovie::Running) {
            movie_->setPaused(true);
        }
        QWidget::hideEvent(event);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        const bool dark = isDarkPalette(*this);
        if (loading_) {
            painter.setPen(palette().color(QPalette::PlaceholderText));
            QFont f = painter.font();
            f.setPointSizeF(qMax(9.0, f.pointSizeF() - 1.5));
            painter.setFont(f);
            painter.drawText(rect().adjusted(0, 12, 0, 0), Qt::AlignCenter, QStringLiteral("加载中…"));
        } else if (movie_) {
            QPixmap current = movie_->currentPixmap();
            if (!current.isNull()) {
                current.setDevicePixelRatio(qMax(1.0, devicePixelRatioF()));
                painter.drawPixmap(targetRect_, current);
            } else if (!firstFrame_.isNull()) {
                painter.drawPixmap(targetRect_, firstFrame_);
            }
        } else if (!scaledStatic_.isNull()) {
            painter.drawPixmap(targetRect_, scaledStatic_);
        } else {
            painter.setPen(palette().color(QPalette::PlaceholderText));
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("暂无预览"));
        }

        if (!loading_) {
            const QColor mediaBg = animated_
                ? (dark ? QColor(126, 97, 178, 130) : QColor(115, 86, 168, 34))
                : (dark ? QColor(255, 255, 255, 28) : QColor(0, 0, 0, 18));
            const QColor mediaFg = animated_
                ? (dark ? QColor(238, 228, 255) : QColor(92, 62, 148))
                : (dark ? QColor(215, 220, 228) : QColor(88, 94, 106));
            drawPillTag(painter, QRectF(2, 2, 36, 18),
                        animated_ ? QStringLiteral("GIF") : QStringLiteral("图片"),
                        mediaBg, mediaFg, false);
        }

        if (active_) {
            const QColor accentBg = dark ? QColor(62, 168, 118, 165) : QColor(38, 150, 98, 34);
            const QColor accentFg = dark ? QColor(235, 255, 244) : QColor(24, 118, 74);
            drawPillTag(painter, QRectF(width() - 64, 2, 62, 18),
                        QStringLiteral("展示中"), accentBg, accentFg, true);
        }
    }

private:
    static void drawPillTag(QPainter& painter, const QRectF& box, const QString& text,
                            const QColor& bg, const QColor& fg, bool withDot)
    {
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        painter.drawRoundedRect(box, 9, 9);

        QFont font = painter.font();
        font.setPointSizeF(qMax(8.5, font.pointSizeF() - 2.0));
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(fg);

        if (withDot) {
            painter.setBrush(fg);
            painter.drawEllipse(QPointF(box.left() + 9.5, box.center().y()), 2.6, 2.6);
            painter.drawText(box.adjusted(13, 0, -2, 0), Qt::AlignCenter, text);
        } else {
            painter.drawText(box, Qt::AlignCenter, text);
        }
        painter.restore();
    }

    void startOrResume()
    {
        if (!movie_) {
            return;
        }
        if (!movieStarted_) {
            movie_->start();
            movieStarted_ = true;
        } else if (movie_->state() == QMovie::Paused) {
            movie_->setPaused(false);
        }
    }

    void requestCleanRepaint()
    {
        if (auto* p = parentWidget()) {
            p->update(geometry());
        }
        update();
    }

    QPixmap firstFrame_;
    QPixmap scaledStatic_;
    QRect targetRect_;
    std::unique_ptr<QMovie> movie_;
    bool animated_ = false;
    bool loading_ = false;
    bool inViewport_ = true;
    bool movieStarted_ = false;
    bool active_ = false;
};

class RoleActionButton : public QPushButton {
public:
    explicit RoleActionButton(QWidget* parent = nullptr)
        : QPushButton(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setFixedHeight(28);
    }

    void setActiveVisual(bool active)
    {
        if (active_ != active) {
            active_ = active;
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool dark = isDarkPalette(*this);
        const bool hovered = underMouse();
        const bool pressed = isDown();
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

        if (active_) {
            QColor bg = palette().color(QPalette::Highlight);
            bg.setAlpha(dark ? (hovered ? 215 : 185) : (hovered ? 235 : 215));
            if (pressed) {
                bg = bg.darker(112);
            }
            painter.setPen(Qt::NoPen);
            painter.setBrush(bg);
            painter.drawRoundedRect(box, 8, 8);
            painter.setPen(palette().color(QPalette::HighlightedText));
        } else {
            QColor bg = dark ? QColor(255, 255, 255, pressed ? 34 : (hovered ? 22 : 11))
                             : QColor(0, 0, 0, pressed ? 20 : (hovered ? 12 : 5));
            QColor border = dark ? QColor(255, 255, 255, hovered ? 48 : 24)
                                 : QColor(0, 0, 0, hovered ? 32 : 16);
            painter.setPen(QPen(border, 1.0));
            painter.setBrush(bg);
            painter.drawRoundedRect(box, 8, 8);
            painter.setPen(palette().color(QPalette::WindowText));
        }

        QFont f = font();
        f.setPointSizeF(qMax(10.0, f.pointSizeF() - 0.5));
        f.setBold(active_);
        painter.setFont(f);
        painter.drawText(rect(), Qt::AlignCenter, text());
    }

private:
    bool active_ = false;
};

class RoleMoreButton : public QToolButton {
public:
    explicit RoleMoreButton(QWidget* parent = nullptr)
        : QToolButton(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setFixedSize(28, 28);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool dark = isDarkPalette(*this);
        const bool hovered = underMouse();
        const bool pressed = isDown();
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

        if (hovered || pressed) {
            QColor bg = dark ? QColor(255, 255, 255, pressed ? 38 : 24)
                             : QColor(0, 0, 0, pressed ? 22 : 12);
            painter.setPen(Qt::NoPen);
            painter.setBrush(bg);
            painter.drawRoundedRect(box, 8, 8);
        }

        QColor dotColor = palette().color(QPalette::WindowText);
        dotColor.setAlpha(hovered ? 230 : 155);
        painter.setPen(Qt::NoPen);
        painter.setBrush(dotColor);
        const qreal cy = height() / 2.0;
        const qreal cx = width() / 2.0;
        for (const qreal dx : {-5.5, 0.0, 5.5}) {
            painter.drawEllipse(QPointF(cx + dx, cy), 1.6, 1.6);
        }
    }
};

class RoleTileWidget : public QWidget {
public:
    RoleTileWidget(const RoleEntry& role, QWidget* parent = nullptr)
        : QWidget(parent), roleId_(role.id), displayName_(role.name)
    {
        setObjectName(QStringLiteral("roleTile_") + role.id);
        setProperty("roleId", role.id);
        setProperty("assetLoaded", false);
        setProperty("animatedGif", false);
        setFixedSize(tileWidth, tileHeight);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setAttribute(Qt::WA_OpaquePaintEvent);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(14, 12, 14, 14);
        root->setSpacing(6);

        stage_ = new RolePreviewStage(this);
        stage_->setLoading();
        root->addWidget(stage_, 0, Qt::AlignHCenter);

        root->addStretch(1);

        nameLabel_ = new QLabel(this);
        nameLabel_->setAlignment(Qt::AlignCenter);
        nameLabel_->setTextFormat(Qt::PlainText);
        nameLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
        QFont titleFont = nameLabel_->font();
        titleFont.setBold(true);
        nameLabel_->setFont(titleFont);
        setDisplayName(role.name);
        root->addWidget(nameLabel_);

        auto* actionsRow = new QHBoxLayout;
        actionsRow->setContentsMargins(0, 2, 0, 0);
        actionsRow->setSpacing(6);

        toggleButton_ = new RoleActionButton(this);
        toggleButton_->setObjectName(QStringLiteral("toggleRole_") + role.id);
        connect(toggleButton_, &QPushButton::clicked, this, [this] {
            if (onToggleRequested) {
                onToggleRequested(false);
            }
        });
        actionsRow->addWidget(toggleButton_, 1);

        menuButton_ = new RoleMoreButton(this);
        menuButton_->setObjectName(QStringLiteral("manageRole_") + role.id);
        menuButton_->setToolTip(QStringLiteral("角色管理（角色卡、动作、分享设置、导出、复制、重命名、删除）"));
        connect(menuButton_, &QToolButton::clicked, this, [this] {
            if (onMenuRequested) {
                onMenuRequested(menuButton_->mapToGlobal(QPoint(0, menuButton_->height() + 4)));
            }
        });
        actionsRow->addWidget(menuButton_);
        root->addLayout(actionsRow);
    }

    const QString& roleId() const { return roleId_; }
    const QString& displayName() const { return displayName_; }

    void setDisplayName(const QString& name)
    {
        displayName_ = name;
        const QFontMetrics metrics(nameLabel_->font());
        nameLabel_->setText(metrics.elidedText(name, Qt::ElideMiddle, tileWidth - 28));
        setToolTip(name + QStringLiteral("\n点击卡片展示或收起桌宠 · 右下角「⋯」管理独立角色卡与动作"));
    }

    void reloadAsset(std::optional<PetAsset> asset)
    {
        stage_->setAsset(std::move(asset));
        setProperty("animatedGif", stage_->isAnimated());
        setProperty("assetLoaded", true);
    }

    void setViewportVisible(bool visible)
    {
        stage_->setViewportVisible(visible);
    }

    void updateSelectionState(bool active, bool multiPetMode)
    {
        active_ = active;
        multiPetMode_ = multiPetMode;
        setProperty("activeOnDesktop", active);
        stage_->setActiveBadge(active);
        toggleButton_->setActiveVisual(active);
        if (multiPetMode) {
            toggleButton_->setText(active ? QStringLiteral("✓ 已在桌面") : QStringLiteral("+ 展示桌宠"));
        } else {
            toggleButton_->setText(active ? QStringLiteral("✓ 当前桌宠") : QStringLiteral("设为桌宠"));
        }
        update();
    }

    std::function<void(bool forceMulti)> onToggleRequested;
    std::function<void(const QPoint& globalPos)> onMenuRequested;

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            pressed_ = true;
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && pressed_) {
            pressed_ = false;
            if (rect().contains(event->position().toPoint()) && onToggleRequested) {
                const bool forceMulti = event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier);
                onToggleRequested(forceMulti);
            }
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void contextMenuEvent(QContextMenuEvent* event) override
    {
        if (onMenuRequested) {
            onMenuRequested(event->globalPos());
            event->accept();
            return;
        }
        QWidget::contextMenuEvent(event);
    }

    void paintEvent(QPaintEvent* event) override
    {
        QPainter painter(this);
        const auto pal = palette();
        // Always erase the dirty region to the exact window background first so
        // transparent GIF frames and translucent overlays never ghost or accumulate.
        painter.fillRect(event->rect(), pal.color(QPalette::Window));

        painter.setRenderHint(QPainter::Antialiasing);
        const bool dark = isDarkPalette(*this);
        const bool hovered = underMouse();

        // Backgroundless gallery card: no solid backdrop behind character;
        // only a subtle translucent hover aura or active accent ring.
        const QRectF card = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
        if (active_) {
            QColor tint = pal.color(QPalette::Highlight);
            tint.setAlpha(dark ? 26 : 16);
            painter.setPen(Qt::NoPen);
            painter.setBrush(tint);
            painter.drawRoundedRect(card, 16, 16);

            QColor ring = pal.color(QPalette::Highlight);
            ring.setAlpha(hovered ? 235 : 190);
            painter.setPen(QPen(ring, 1.6));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(card, 16, 16);
        } else if (hovered) {
            QColor hoverWash = dark ? QColor(255, 255, 255, 13) : QColor(0, 0, 0, 7);
            QColor hoverRing = dark ? QColor(255, 255, 255, 45) : QColor(0, 0, 0, 28);
            painter.setPen(QPen(hoverRing, 1.0));
            painter.setBrush(hoverWash);
            painter.drawRoundedRect(card, 16, 16);
        } else {
            QColor hairline = dark ? QColor(255, 255, 255, 20) : QColor(0, 0, 0, 13);
            painter.setPen(QPen(hairline, 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(card, 16, 16);
        }
    }

private:
    QString roleId_;
    QString displayName_;
    RolePreviewStage* stage_ = nullptr;
    QLabel* nameLabel_ = nullptr;
    RoleActionButton* toggleButton_ = nullptr;
    RoleMoreButton* menuButton_ = nullptr;
    bool active_ = false;
    bool multiPetMode_ = false;
    bool pressed_ = false;
};

class RoleGridCanvas : public QWidget {
public:
    explicit RoleGridCanvas(QWidget* parent = nullptr)
        : QWidget(parent), grid_(new QGridLayout(this))
    {
        setBackgroundRole(QPalette::Window);
        setAutoFillBackground(true);
        grid_->setContentsMargins(gridMargin, gridMargin, gridMargin, gridMargin);
        grid_->setHorizontalSpacing(gridSpacing);
        grid_->setVerticalSpacing(gridSpacing);
        grid_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    }

    void setTiles(QList<RoleTileWidget*> tiles)
    {
        clearLayout();
        for (auto* old : std::as_const(tiles_)) {
            old->deleteLater();
        }
        tiles_ = std::move(tiles);
        columns_ = 0;
        relayout(true);
    }

    const QList<RoleTileWidget*>& tiles() const { return tiles_; }

    void relayoutForWidth(int availableWidth)
    {
        viewportWidth_ = availableWidth;
        relayout(false);
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        relayout(false);
    }

private:
    void clearLayout()
    {
        while (auto* item = grid_->takeAt(0)) {
            delete item;
        }
    }

    void relayout(bool force)
    {
        const int effectiveWidth = viewportWidth_ > 0 ? viewportWidth_ : width();
        const int usable = qMax(tileWidth, effectiveWidth - gridMargin * 2);
        const int cols = qMax(1, (usable + gridSpacing) / (tileWidth + gridSpacing));
        if (!force && cols == columns_) {
            return;
        }
        columns_ = cols;
        clearLayout();
        for (int c = 0; c <= columns_; ++c) {
            grid_->setColumnStretch(c, c == columns_ ? 1 : 0);
        }
        for (qsizetype i = 0; i < tiles_.size(); ++i) {
            const int row = static_cast<int>(i) / columns_;
            const int col = static_cast<int>(i) % columns_;
            grid_->addWidget(tiles_[i], row, col, Qt::AlignTop | Qt::AlignLeft);
        }
        const int totalRows = tiles_.isEmpty() ? 0 : (static_cast<int>(tiles_.size()) + columns_ - 1) / columns_;
        grid_->setRowStretch(totalRows, 1);
    }

    QGridLayout* grid_ = nullptr;
    QList<RoleTileWidget*> tiles_;
    int columns_ = 0;
    int viewportWidth_ = 0;
};

class HairlineDivider : public QWidget {
public:
    explicit HairlineDivider(QWidget* parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(1);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        const bool dark = isDarkPalette(*this);
        painter.fillRect(rect(), dark ? QColor(255, 255, 255, 22) : QColor(0, 0, 0, 15));
    }
};

class RoleDropDialog : public QDialog {
public:
    using QDialog::QDialog;
    std::function<void(const QString&)> fileDropped;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override { acceptSupported(event); }
    void dragMoveEvent(QDragMoveEvent* event) override { acceptSupported(event); }

    void dropEvent(QDropEvent* event) override
    {
        const QString path = acceptSupported(event);
        if (!path.isEmpty() && fileDropped) {
            QTimer::singleShot(0, this, [this, path] { fileDropped(path); });
        }
    }

private:
    static QString acceptSupported(QDropEvent* event)
    {
        const auto urls = event->mimeData()->urls();
        if (urls.size() == 1 && urls.constFirst().isLocalFile()
            && event->possibleActions().testFlag(Qt::CopyAction)) {
            const QString path = urls.constFirst().toLocalFile();
            if (isSupportedDropFile(path)) {
                event->setDropAction(Qt::CopyAction);
                event->accept();
                return path;
            }
        }
        event->ignore();
        return {};
    }
};

class ViewportResizeWatcher : public QObject {
public:
    ViewportResizeWatcher(QWidget* viewport, RoleGridCanvas* canvas)
        : QObject(viewport), viewport_(viewport), canvas_(canvas)
    {
        viewport_->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == viewport_ && event->type() == QEvent::Resize) {
            canvas_->relayoutForWidth(viewport_->width());
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QWidget* viewport_ = nullptr;
    RoleGridCanvas* canvas_ = nullptr;
};

QString rawRoleName(const RoleEntry& entry)
{
    if (entry.id == RoleLibrary::builtInId()) {
        return RoleLibrary::builtInName();
    }
    return entry.name;
}
}

RoleLibraryChoice manageRoles(RoleLibrary& library, const RoleLibraryContext& context, QWidget* parent)
{
    RoleDropDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("角色库"));
    dialog.setAcceptDrops(true);
    dialog.resize(888, 630);
    dialog.setMinimumSize(660, 460);

    RoleLibraryChoice choice;
    choice.multiPetEnabled = context.multiPetEnabled || context.activeRoles.size() > 1;
    for (const auto& id : context.activeRoles) {
        if (!id.isEmpty() && id != RoleLibrary::builtInId() && !choice.activeRoles.contains(id)) {
            choice.activeRoles.append(id);
        }
    }
    if (choice.activeRoles.isEmpty() && !context.preferredRole.isEmpty()
        && context.preferredRole != RoleLibrary::builtInId()) {
        choice.activeRoles.append(context.preferredRole);
    }

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Refined Header Section
    auto* headerWidget = new QWidget(&dialog);
    auto* headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(22, 16, 22, 14);
    headerLayout->setSpacing(14);

    auto* titleColumn = new QVBoxLayout;
    titleColumn->setContentsMargins(0, 0, 0, 0);
    titleColumn->setSpacing(2);
    auto* headingLabel = new QLabel(QStringLiteral("角色画廊"), headerWidget);
    QFont headingFont = headingLabel->font();
    headingFont.setPointSizeF(headingFont.pointSizeF() + 3.0);
    headingFont.setBold(true);
    headingLabel->setFont(headingFont);
    titleColumn->addWidget(headingLabel);

    auto* statusLabel = new QLabel(headerWidget);
    statusLabel->setObjectName(QStringLiteral("roleLibraryStatus"));
    QFont statusFont = statusLabel->font();
    statusFont.setPointSizeF(qMax(9.5, statusFont.pointSizeF() - 1.0));
    statusLabel->setFont(statusFont);
    statusLabel->setForegroundRole(QPalette::PlaceholderText);
    titleColumn->addWidget(statusLabel);
    headerLayout->addLayout(titleColumn);

    headerLayout->addStretch(1);

    auto* multiPetCheck = new PillSwitchCheckBox(QStringLiteral("多桌宠同屏"), headerWidget);
    multiPetCheck->setObjectName(QStringLiteral("allowMultiplePets"));
    multiPetCheck->setChecked(choice.multiPetEnabled);
    multiPetCheck->setToolTip(QStringLiteral("开启后可自由点选多个角色同时在桌面独立运行；关闭时桌面仅展示单个角色。"));
    headerLayout->addWidget(multiPetCheck);

    headerLayout->addSpacing(6);

    auto* addAssetButton = new ToolbarPillButton(QStringLiteral("＋ 添加素材…"), false, headerWidget);
    addAssetButton->setObjectName(QStringLiteral("addRoleAsset"));
    auto* importPackageButton = new ToolbarPillButton(QStringLiteral("导入角色包…"), false, headerWidget);
    importPackageButton->setObjectName(QStringLiteral("importRolePackage"));
    headerLayout->addWidget(addAssetButton);
    headerLayout->addWidget(importPackageButton);
    root->addWidget(headerWidget);
    root->addWidget(new HairlineDivider(&dialog));

    // Backgroundless Gallery Scroll Area (matches window surface cleanly without breaking backing store)
    auto* scroll = new QScrollArea(&dialog);
    scroll->setObjectName(QStringLiteral("roleLibraryScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setBackgroundRole(QPalette::Window);
    scroll->viewport()->setAutoFillBackground(true);
    scroll->verticalScrollBar()->setStyleSheet(QStringLiteral(
        "QScrollBar:vertical { background: transparent; width: 8px; margin: 4px 2px 4px 0px; }"
        "QScrollBar::handle:vertical { background: rgba(128, 128, 128, 85); min-height: 28px; border-radius: 4px; }"
        "QScrollBar::handle:vertical:hover { background: rgba(128, 128, 128, 135); }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"));
    auto* canvas = new RoleGridCanvas(scroll);
    scroll->setWidget(canvas);
    new ViewportResizeWatcher(scroll->viewport(), canvas);
    root->addWidget(scroll, 1);

    root->addWidget(new HairlineDivider(&dialog));

    // Clean Footer Section
    auto* footerWidget = new QWidget(&dialog);
    auto* footerRow = new QHBoxLayout(footerWidget);
    footerRow->setContentsMargins(22, 12, 22, 14);
    footerRow->setSpacing(14);

    auto* hintLabel = new QLabel(
        QStringLiteral("点击角色即可在桌面展示或收起 · 支持直接拖入 PNG / GIF / AVIF / .gifpet 文件 · 各桌宠数值与角色卡完全独立"),
        footerWidget);
    hintLabel->setObjectName(QStringLiteral("roleLibraryHint"));
    hintLabel->setWordWrap(true);
    QFont hintFont = hintLabel->font();
    hintFont.setPointSizeF(qMax(9.5, hintFont.pointSizeF() - 1.0));
    hintLabel->setFont(hintFont);
    hintLabel->setForegroundRole(QPalette::PlaceholderText);
    footerRow->addWidget(hintLabel, 1);

    auto* doneButton = new ToolbarPillButton(QStringLiteral("完成"), true, footerWidget);
    doneButton->setMinimumWidth(84);
    QObject::connect(doneButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    footerRow->addWidget(doneButton);
    root->addWidget(footerWidget);

    QString preferredPrimary = !context.preferredRole.isEmpty() ? context.preferredRole
        : (!choice.activeRoles.isEmpty() ? choice.activeRoles.constFirst() : QString{});

    const auto notifySelection = [&] {
        if (context.selectionChanged) {
            context.selectionChanged(choice.activeRoles, choice.multiPetEnabled);
        }
    };

    const auto syncTileStates = [&] {
        const auto& tiles = canvas->tiles();
        for (auto* tile : tiles) {
            tile->updateSelectionState(choice.activeRoles.contains(tile->roleId()), choice.multiPetEnabled);
        }
        if (tiles.isEmpty()) {
            statusLabel->setText(QStringLiteral("暂无角色 · 点击右上角添加素材或直接拖入 GIF / PNG / .gifpet"));
        } else {
            statusLabel->setText(QStringLiteral("共 %1 个角色  ·  桌面展示中 %2 个")
                                     .arg(tiles.size())
                                     .arg(choice.activeRoles.size()));
        }
    };

    const auto markChanged = [&](const QString& roleId) {
        if (!choice.changed.contains(roleId)) {
            choice.changed.append(roleId);
        }
        if (context.roleUpdated) {
            context.roleUpdated(roleId);
        }
    };

    const auto syncViewportPlayback = [&] {
        const QRect viewportBox(QPoint(0, scroll->verticalScrollBar()->value()),
                                scroll->viewport()->size());
        const QRect activeZone = viewportBox.adjusted(0, -90, 0, 90);
        for (auto* tile : canvas->tiles()) {
            const bool visible = activeZone.isEmpty() || activeZone.intersects(tile->geometry());
            tile->setViewportVisible(visible);
        }
    };
    QObject::connect(scroll->verticalScrollBar(), &QScrollBar::valueChanged, &dialog, syncViewportPlayback);

    int loadGeneration = 0;
    QQueue<QPointer<RoleTileWidget>> hydrationQueue;
    int activeDecoders = 0;
    std::function<void()> pumpHydration;

    pumpHydration = [&] {
        while (activeDecoders < 2 && !hydrationQueue.isEmpty()) {
            QPointer<RoleTileWidget> tile = hydrationQueue.dequeue();
            if (!tile) {
                continue;
            }
            const int expectedGen = loadGeneration;
            const QString id = tile->roleId();
            QString byteErr;
            QByteArray sourceBytes;
            QString builtInFile;

            if (auto assigned = library.actionFor(id, ActionSlot::Idle, byteErr)) {
                sourceBytes = std::move(assigned->bytes);
            } else if (byteErr.isEmpty()) {
                if (id == RoleLibrary::builtInId()) {
                    builtInFile = context.builtInAsset;
                } else {
                    sourceBytes = library.assetFor(id, byteErr);
                }
            }

            if (sourceBytes.isEmpty() && builtInFile.isEmpty()) {
                tile->reloadAsset(std::nullopt);
                continue;
            }

            ++activeDecoders;
            auto* watcher = new QFutureWatcher<std::optional<PreparedPetAsset>>(&dialog);
            QObject::connect(watcher, &QFutureWatcher<std::optional<PreparedPetAsset>>::finished, &dialog,
                [&, watcher, tile, expectedGen] {
                    --activeDecoders;
                    if (expectedGen == loadGeneration && tile && !watcher->isCanceled()
                        && watcher->future().resultCount() > 0) {
                        auto prepared = watcher->result();
                        tile->reloadAsset(prepared ? std::optional(makePlayableAsset(std::move(*prepared)))
                                                   : std::nullopt);
                        syncViewportPlayback();
                    }
                    watcher->deleteLater();
                    QTimer::singleShot(0, &dialog, pumpHydration);
                });

            const QString fallbackBuiltIn = context.builtInAsset;
            watcher->setFuture(QtConcurrent::run(galleryDecodePool(),
                [sourceBytes = std::move(sourceBytes), builtInFile, fallbackBuiltIn, isBuiltIn = (id == RoleLibrary::builtInId())]() mutable -> std::optional<PreparedPetAsset> {
                    QString decodeErr;
                    if (!sourceBytes.isEmpty()) {
                        auto decoded = decodePetPixels(std::move(sourceBytes), decodeErr);
                        if (decoded || !isBuiltIn) {
                            return decoded;
                        }
                    }
                    if (!builtInFile.isEmpty() || isBuiltIn) {
                        return loadPetPixels(builtInFile.isEmpty() ? fallbackBuiltIn : builtInFile, decodeErr);
                    }
                    return std::nullopt;
                }));
        }
    };

    std::function<bool()> rebuildGrid;

    const auto importImageOrGif = [&](const QString& path, bool previewCutout) {
        using Loading = QPair<std::optional<PreparedPetAsset>, QString>;
        auto loaded = waitForBackground<Loading>(dialog, QStringLiteral("正在读取图片或动图…"),
            [path](QPromise<Loading>&) {
                Loading result;
                result.first = loadPetPixels(path, result.second);
                return result;
            });
        if (!loaded) {
            return;
        }
        if (!loaded->first) {
            QMessageBox::warning(&dialog, QStringLiteral("导入失败"), loaded->second);
            return;
        }
        std::optional<PetAsset> asset(makePlayableAsset(std::move(*loaded->first)));
        QString error;
#ifdef Q_OS_MACOS
        if (previewCutout) {
            asset = previewPetImport(std::move(*asset), &dialog, error);
        }
#else
        Q_UNUSED(previewCutout);
#endif
        if (!asset) {
            if (!error.isEmpty()) {
                QMessageBox::warning(&dialog, QStringLiteral("导入失败"), error);
            }
            return;
        }
        const QString newId = library.add(QFileInfo(path).completeBaseName(), asset->source, error);
        if (newId.isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("无法加入角色库"), error);
            return;
        }
        choice.changed.append(newId);
        rebuildGrid();
    };

    const auto importPackagePath = [&](const QString& path) {
        const QString id = importRolePackage(dialog, library, path);
        if (!id.isEmpty()) {
            markChanged(id);
            rebuildGrid();
        }
    };

    const auto toggleRoleOnDesktop = [&](const QString& roleId, bool forceMulti) {
        if (forceMulti && !choice.multiPetEnabled) {
            choice.multiPetEnabled = true;
            QSignalBlocker block(multiPetCheck);
            multiPetCheck->setChecked(true);
        }
        if (choice.multiPetEnabled) {
            if (choice.activeRoles.contains(roleId)) {
                if (choice.activeRoles.size() <= 1) {
                    hintLabel->setText(QStringLiteral("桌面至少保留 1 个角色；如需临时隐藏全部桌宠，可使用菜单中的「隐藏桌宠」。"));
                    return;
                }
                choice.activeRoles.removeAll(roleId);
                if (preferredPrimary == roleId) {
                    preferredPrimary = choice.activeRoles.constFirst();
                }
            } else {
                choice.activeRoles.append(roleId);
                preferredPrimary = roleId;
            }
        } else {
            if (choice.activeRoles.size() == 1 && choice.activeRoles.constFirst() == roleId) {
                return;
            }
            choice.activeRoles = {roleId};
            preferredPrimary = roleId;
        }
        syncTileStates();
        notifySelection();
    };

    rebuildGrid = [&]() -> bool {
        QString error;
        const auto entries = library.entries(error);
        if (!error.isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("无法读取角色库"), error);
            return false;
        }

        QStringList validIds;
        for (const auto& entry : entries) {
            validIds.append(entry.id);
        }
        choice.activeRoles.removeIf([&](const QString& id) { return !validIds.contains(id); });
        if (choice.activeRoles.isEmpty() && !validIds.isEmpty()) {
            choice.activeRoles.append(validIds.constFirst());
        }

        ++loadGeneration;
        hydrationQueue.clear();

        QList<RoleTileWidget*> tiles;
        tiles.reserve(entries.size());
        for (const auto& entry : entries) {
            auto* tile = new RoleTileWidget(entry, canvas);
            const QString id = entry.id;
            const QString exportName = rawRoleName(entry);

            tile->onToggleRequested = [&, id](bool forceMulti) {
                toggleRoleOnDesktop(id, forceMulti);
            };

            tile->onMenuRequested = [&, tile, id, exportName](const QPoint& globalPos) {
                QMenu menu(&dialog);
                const bool active = choice.activeRoles.contains(id);
                if (choice.multiPetEnabled) {
                    auto* toggleAct = menu.addAction(active ? QStringLiteral("从桌面收起此桌宠")
                                                            : QStringLiteral("在桌面展示此桌宠"));
                    toggleAct->setEnabled(!active || choice.activeRoles.size() > 1);
                    QObject::connect(toggleAct, &QAction::triggered, &dialog, [&, id] {
                        toggleRoleOnDesktop(id, false);
                    });
                    auto* onlyAct = menu.addAction(QStringLiteral("仅在桌面展示此角色"));
                    QObject::connect(onlyAct, &QAction::triggered, &dialog, [&, id] {
                        choice.activeRoles = {id};
                        preferredPrimary = id;
                        syncTileStates();
                        notifySelection();
                    });
                } else {
                    auto* switchAct = menu.addAction(active ? QStringLiteral("当前已在桌面展示")
                                                            : QStringLiteral("切换为此桌宠"));
                    switchAct->setEnabled(!active);
                    QObject::connect(switchAct, &QAction::triggered, &dialog, [&, id] {
                        toggleRoleOnDesktop(id, false);
                    });
                    if (!active) {
                        auto* addMultiAct = menu.addAction(QStringLiteral("同时在桌面展示（开启多桌宠）"));
                        QObject::connect(addMultiAct, &QAction::triggered, &dialog, [&, id] {
                            toggleRoleOnDesktop(id, true);
                        });
                    }
                }

                menu.addSeparator();
                if (context.voiceTraining) {
                    auto* cardAct = menu.addAction(QStringLiteral("角色卡与音色训练…"));
                    QObject::connect(cardAct, &QAction::triggered, &dialog, [&, id] {
                        showRoleCard(dialog, library, id, *context.voiceTraining);
                        markChanged(id);
                    });
                }
                auto* actionsAct = menu.addAction(QStringLiteral("配置角色动作…"));
                QObject::connect(actionsAct, &QAction::triggered, &dialog, [&, tile, id] {
                    if (editRoleActions(library, id, context.builtInAsset, &dialog)) {
                        QString reloadErr;
                        tile->reloadAsset(loadRoleAction(library, id, ActionSlot::Idle, context.builtInAsset, reloadErr));
                        markChanged(id);
                    }
                });
                auto* shareAct = menu.addAction(QStringLiteral("分享与行为预设…"));
                QObject::connect(shareAct, &QAction::triggered, &dialog, [&, id] {
                    if (editRoleShare(dialog, library, id)) {
                        markChanged(id);
                    }
                });

                menu.addSeparator();
                auto* duplicateAct = menu.addAction(QStringLiteral("复制为独立新角色"));
                QObject::connect(duplicateAct, &QAction::triggered, &dialog, [&, id] {
                    QString dupErr;
                    const QString copyId = library.duplicate(id, context.builtInAsset, dupErr);
                    if (copyId.isEmpty()) {
                        QMessageBox::warning(&dialog, QStringLiteral("无法复制角色"), dupErr);
                        return;
                    }
                    choice.changed.append(copyId);
                    rebuildGrid();
                });
                auto* exportAct = menu.addAction(QStringLiteral("导出分享包 (.gifpet)…"));
                QObject::connect(exportAct, &QAction::triggered, &dialog, [&, id, exportName] {
                    exportRolePackage(dialog, library, id, exportName);
                });

                const bool editable = id != RoleLibrary::builtInId();
                menu.addSeparator();
                auto* renameAct = menu.addAction(QStringLiteral("重命名…"));
                renameAct->setEnabled(editable);
                QObject::connect(renameAct, &QAction::triggered, &dialog, [&, tile, id] {
                    bool confirmed = false;
                    const QString updatedName = QInputDialog::getText(
                        &dialog, QStringLiteral("重命名角色"), QStringLiteral("角色名称"),
                        QLineEdit::Normal, tile->displayName(), &confirmed);
                    if (!confirmed) {
                        return;
                    }
                    QString renameErr;
                    if (library.rename(id, updatedName, renameErr)) {
                        tile->setDisplayName(updatedName.trimmed().left(80));
                        markChanged(id);
                    } else {
                        QMessageBox::warning(&dialog, QStringLiteral("无法重命名"), renameErr);
                    }
                });

                auto* removeAct = menu.addAction(QStringLiteral("删除角色…"));
                removeAct->setEnabled(editable);
                QObject::connect(removeAct, &QAction::triggered, &dialog, [&, tile, id] {
                    const auto answer = QMessageBox::question(
                        &dialog, QStringLiteral("删除角色"),
                        QStringLiteral("删除「%1」及其专属动作、角色卡与本地聊天记录？").arg(tile->displayName()),
                        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
                    if (answer != QMessageBox::Yes) {
                        return;
                    }
                    QString removeErr;
                    if (!library.remove(id, removeErr)) {
                        QMessageBox::warning(&dialog, QStringLiteral("无法删除"), removeErr);
                        return;
                    }
                    choice.removed.append(id);
                    const bool wasActive = choice.activeRoles.removeAll(id) > 0;
                    if (preferredPrimary == id) {
                        preferredPrimary = choice.activeRoles.isEmpty() ? QString{} : choice.activeRoles.constFirst();
                    }
                    if (context.roleRemoved) {
                        context.roleRemoved(id);
                    }
                    rebuildGrid();
                    if (wasActive) {
                        notifySelection();
                    }
                });

                menu.exec(globalPos);
            };

            tiles.append(tile);
        }

        canvas->relayoutForWidth(scroll->viewport()->width());
        canvas->setTiles(std::move(tiles));
        syncTileStates();
        for (auto* tile : canvas->tiles()) {
            hydrationQueue.enqueue(tile);
        }
        QTimer::singleShot(0, &dialog, [&] {
            syncViewportPlayback();
            pumpHydration();
        });
        return true;
    };

    QObject::connect(multiPetCheck, &QCheckBox::toggled, &dialog, [&](bool enabled) {
        choice.multiPetEnabled = enabled;
        if (!enabled && choice.activeRoles.size() > 1) {
            const QString keep = choice.activeRoles.contains(preferredPrimary)
                ? preferredPrimary
                : choice.activeRoles.constFirst();
            choice.activeRoles = {keep};
        }
        syncTileStates();
        notifySelection();
    });

    QObject::connect(addAssetButton, &QPushButton::clicked, &dialog, [&] {
        const QString path = QFileDialog::getOpenFileName(
            &dialog, QStringLiteral("添加角色素材"), {},
            QStringLiteral("桌宠素材 (*.png *.jpg *.jpeg *.gif *.agif *.webp *.avif);;所有文件 (*)"));
        if (!path.isEmpty()) {
            importImageOrGif(path, true);
        }
    });

    QObject::connect(importPackageButton, &QPushButton::clicked, &dialog, [&] {
        const QString path = QFileDialog::getOpenFileName(
            &dialog, QStringLiteral("导入角色包"), {},
            QStringLiteral("AuraPet 角色包 (*.gifpet)"));
        if (!path.isEmpty()) {
            importPackagePath(path);
        }
    });

    dialog.fileDropped = [&](const QString& path) {
        if (QFileInfo(path).suffix().compare(rolePackageSuffix, Qt::CaseInsensitive) == 0) {
            importPackagePath(path);
        } else {
            importImageOrGif(path, true);
        }
    };

    if (!rebuildGrid()) {
        return choice;
    }

    QTimer::singleShot(0, &dialog, [&] {
        canvas->relayoutForWidth(scroll->viewport()->width());
    });

    dialog.exec();
    return choice;
}
