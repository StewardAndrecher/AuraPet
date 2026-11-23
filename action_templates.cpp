#include "action_templates.h"

#include <QList>
#include <QPainter>
#include <QTransform>

#include <cmath>

namespace {
constexpr int framesPerSecond = 20;
constexpr double fullTurn = 6.283185307179586;

QRect visibleBounds(const QImage& image)
{
    int left = image.width();
    int right = -1;
    int top = image.height();
    int bottom = -1;
    for (int y = 0; y < image.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(row[x]) > 0) {
                left = qMin(left, x);
                right = qMax(right, x);
                top = qMin(top, y);
                bottom = y;
            }
        }
    }
    return right < left ? QRect{} : QRect(QPoint(left, top), QPoint(right, bottom));
}

QTransform motionAt(ActionTemplate kind, double phase, double intensity, double height)
{
    QTransform motion;
    switch (kind) {
    case ActionTemplate::Hop: {
        const double rise = intensity * std::sin(phase / 2.0);
        motion.translate(0, -0.26 * height * rise);
        motion.scale(1.0 - 0.04 * rise, 1.0 + 0.06 * rise);
        break;
    }
    case ActionTemplate::Sway:
        motion.rotate(14.0 * intensity * std::sin(phase));
        break;
    case ActionTemplate::Breathe: {
        const double breath = 0.5 * intensity * (1.0 - std::cos(phase));
        motion.scale(1.0 + 0.07 * breath, 1.0 + 0.14 * breath);
        break;
    }
    }
    return motion;
}
}

QString actionTemplateName(ActionTemplate kind)
{
    switch (kind) {
    case ActionTemplate::Hop: return QStringLiteral("轻跳");
    case ActionTemplate::Sway: return QStringLiteral("摇摆");
    case ActionTemplate::Breathe: return QStringLiteral("呼吸");
    }
    return {};
}

int actionTemplateFrameCount(int durationMs)
{
    return qRound(durationMs * (framesPerSecond / 1000.0));
}

FrameSequenceResult renderActionTemplate(const QImage& idleFrame, const ActionTemplateOptions& options,
                                         const std::function<bool(int done)>& progress)
{
    if (options.intensity < 1 || options.intensity > 100
        || options.durationMs < minTemplateDurationMs || options.durationMs > maxTemplateDurationMs) {
        return {{}, QStringLiteral("动作幅度或单轮时长超出范围。")};
    }
    if (!progress(0)) {
        return {};
    }
    QImage idle = idleFrame;
    if (qMax(idle.width(), idle.height()) > maxSequenceCanvasSide) {
        const QSize fit = idle.size().scaled(maxSequenceCanvasSide, maxSequenceCanvasSide, Qt::KeepAspectRatio)
                                    .expandedTo(QSize(1, 1));
        idle = idle.scaled(fit, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    idle.convertTo(QImage::Format_ARGB32_Premultiplied);
    const QRect visible = visibleBounds(idle);
    if (visible.isEmpty()) {
        return {{}, QStringLiteral("待机首帧没有可见画面，请先更换待机素材。")};
    }

    const QImage sprite = idle.copy(visible);
    const QRectF fromFeet(-visible.width() / 2.0, -visible.height(), visible.width(), visible.height());
    const int count = actionTemplateFrameCount(options.durationMs);
    const double strength = options.intensity / 100.0;
    QList<QTransform> motion;
    motion.reserve(count);
    QRectF motionBounds;
    for (int index = 0; index < count; ++index) {
        const double phase = fullTurn * index / count;
        const auto pose = motionAt(options.kind, phase, strength, visible.height());
        motionBounds = motionBounds.united(pose.mapRect(fromFeet));
        motion.append(pose);
    }

    // Fit all poses together so the sprite keeps its scale throughout the cycle.
    const QSize canvas = idle.size();
    const double scale = qMin(1.0, qMin(canvas.width() / motionBounds.width(),
                                       canvas.height() / motionBounds.height()));
    const QPointF foot(visible.x() + visible.width() / 2.0, visible.y() + visible.height());
    const double left = -scale * motionBounds.left();
    const double top = -scale * motionBounds.top();
    const double right = canvas.width() - scale * motionBounds.right();
    const double bottom = canvas.height() - scale * motionBounds.bottom();
    QTransform placement;
    placement.translate(qBound(left, foot.x(), qMax(left, right)),
                        qBound(top, foot.y(), qMax(top, bottom)));
    placement.scale(scale, scale);

    return encodeLoopingGif(count, framesPerSecond, [&](int index) {
        QImage frame(canvas, QImage::Format_ARGB32_Premultiplied);
        frame.fill(Qt::transparent);
        QPainter painter(&frame);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setWorldTransform(motion.at(index) * placement);
        painter.drawImage(fromFeet, sprite);
        painter.end();
        return frame;
    }, progress);
}
