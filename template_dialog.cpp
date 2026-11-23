#include "template_dialog.h"
#include "action_templates.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include <utility>

std::optional<ActionAsset> createTemplateAction(ActionSlot slot, const QImage& idleFrame, QWidget* parent,
                                                const QJsonObject& recipe)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("动作模板 · %1").arg(actionSlotName(slot)));
    auto* layout = new QVBoxLayout(&dialog);
    auto* columns = new QHBoxLayout;
    auto* settingsColumn = new QVBoxLayout;
    auto* settings = new QFormLayout;
    auto* kind = new QComboBox(&dialog);
    for (const auto motion : {ActionTemplate::Hop, ActionTemplate::Sway, ActionTemplate::Breathe}) {
        kind->addItem(actionTemplateName(motion), static_cast<int>(motion));
    }
    if (slot == ActionSlot::Idle || slot == ActionSlot::Sleepy) {
        kind->setCurrentIndex(kind->findData(static_cast<int>(ActionTemplate::Breathe)));
    }
    settings->addRow(QStringLiteral("模板"), kind);
    auto* intensity = new QSpinBox(&dialog);
    intensity->setRange(1, 100);
    intensity->setValue(60);
    intensity->setSuffix(QStringLiteral(" %"));
    settings->addRow(QStringLiteral("动作幅度"), intensity);
    auto* duration = new QDoubleSpinBox(&dialog);
    duration->setDecimals(1);
    duration->setRange(minTemplateDurationMs / 1000.0, maxTemplateDurationMs / 1000.0);
    duration->setSingleStep(0.1);
    duration->setValue(1.4);
    duration->setSuffix(QStringLiteral(" 秒"));
    settings->addRow(QStringLiteral("单轮时长"), duration);
    if (!recipe.isEmpty()) {
        kind->setCurrentIndex(qMax(0, kind->findData(recipe.value(QStringLiteral("kind")).toInt())));
        intensity->setValue(recipe.value(QStringLiteral("intensity")).toInt(60));
        duration->setValue(recipe.value(QStringLiteral("durationMs")).toInt(1400) / 1000.0);
    }
    settingsColumn->addLayout(settings);
    auto* source = new QLabel(QStringLiteral("素材：当前待机的第一帧"), &dialog);
    source->setWordWrap(true);
    settingsColumn->addWidget(source);
    settingsColumn->addStretch(1);
    columns->addLayout(settingsColumn, 1);

    auto* previewColumn = new QVBoxLayout;
    auto* preview = new QLabel(&dialog);
    preview->setAlignment(Qt::AlignCenter);
    preview->setFixedSize(310, 245);
    auto* dark = new QCheckBox(QStringLiteral("深色预览背景"), &dialog);
    const auto setBackground = [preview](bool enabled) {
        preview->setStyleSheet(enabled ? QStringLiteral("background-color: #26272b;")
                                       : QStringLiteral("background-color: palette(base);"));
    };
    setBackground(false);
    QObject::connect(dark, &QCheckBox::toggled, &dialog, setBackground);
    previewColumn->addWidget(preview);
    previewColumn->addWidget(dark);
    columns->addLayout(previewColumn);
    layout->addLayout(columns);

    auto* status = new QLabel(&dialog);
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    layout->addWidget(status);
    auto* buttons = new QDialogButtonBox(&dialog);
    auto* generate = buttons->addButton(QStringLiteral("生成预览"), QDialogButtonBox::ActionRole);
    auto* save = buttons->addButton(QStringLiteral("保存到%1").arg(actionSlotName(slot)), QDialogButtonBox::AcceptRole);
    buttons->addButton(QStringLiteral("取消"), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);

    const auto draftOptions = [&] {
        return ActionTemplateOptions{static_cast<ActionTemplate>(kind->currentData().toInt()),
                                     intensity->value(), qRound(duration->value() * 1000)};
    };
    const QSize previewLimit = preview->size() - QSize(16, 16);
    const QPixmap sourcePreview = QPixmap::fromImage(idleFrame.scaled(previewLimit, Qt::KeepAspectRatio,
                                                                    Qt::SmoothTransformation));
    std::optional<PetAsset> rendered;
    QFutureWatcher<FrameSequenceResult> job;
    const auto resetPreview = [&] {
        preview->setMovie(nullptr);
        rendered.reset();
        preview->setPixmap(sourcePreview);
        save->setEnabled(false);
        status->setText(QStringLiteral("生成预览后可保存。"));
    };
    const auto setGenerating = [&](bool generating) {
        kind->setEnabled(!generating);
        intensity->setEnabled(!generating);
        duration->setEnabled(!generating);
        generate->setEnabled(!generating);
    };
    QObject::connect(kind, &QComboBox::currentIndexChanged, &dialog, resetPreview);
    QObject::connect(intensity, &QSpinBox::valueChanged, &dialog, resetPreview);
    QObject::connect(duration, &QDoubleSpinBox::valueChanged, &dialog, resetPreview);
    QObject::connect(generate, &QPushButton::clicked, &dialog, [&] {
        resetPreview();
        setGenerating(true);
        status->setText(QStringLiteral("正在生成动作…"));
        const auto options = draftOptions();
        job.setFuture(QtConcurrent::run([idleFrame, options](QPromise<FrameSequenceResult>& promise) {
            promise.setProgressRange(0, actionTemplateFrameCount(options.durationMs));
            auto result = renderActionTemplate(idleFrame, options, [&promise](int done) {
                promise.setProgressValue(done);
                return !promise.isCanceled();
            });
            if (!promise.isCanceled()) {
                promise.addResult(std::move(result));
            }
        }));
    });
    QObject::connect(&job, &QFutureWatcher<FrameSequenceResult>::progressValueChanged, &dialog, [&](int done) {
        status->setText(QStringLiteral("正在生成动作，已完成 %1 / %2 帧…").arg(done).arg(job.progressMaximum()));
    });
    QObject::connect(&job, &QFutureWatcher<FrameSequenceResult>::finished, &dialog, [&] {
        setGenerating(false);
        if (job.isCanceled()) {
            return;
        }
        auto result = job.result();
        if (result.error.isEmpty()) {
            rendered = decodePetAsset(std::move(result.gif), result.error);
        }
        const int count = actionTemplateFrameCount(draftOptions().durationMs);
        if (!rendered || !rendered->movie || rendered->movie->frameCount() != count) {
            rendered.reset();
            status->setText(result.error.isEmpty() ? QStringLiteral("无法播放生成的动作。") : result.error);
            return;
        }
        auto* movie = rendered->movie.get();
        movie->setScaledSize(rendered->firstFrame.size().scaled(previewLimit, Qt::KeepAspectRatio)
                                                              .expandedTo(QSize(1, 1)));
        preview->setMovie(movie);
        movie->start();
        status->setText(QStringLiteral("正在循环预览 · %1 帧").arg(count));
        save->setEnabled(true);
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    resetPreview();
    const int choice = dialog.exec();
    job.cancel();
    preview->setMovie(nullptr);
    if (choice == QDialog::Accepted && rendered) {
        const auto options = draftOptions();
        return ActionAsset{QStringLiteral("动作模板 · %1").arg(actionTemplateName(options.kind)),
                           std::move(rendered->source),
                           {{QStringLiteral("kind"), static_cast<int>(options.kind)},
                            {QStringLiteral("intensity"), options.intensity},
                            {QStringLiteral("durationMs"), options.durationMs}}};
    }
    return std::nullopt;
}
