#include "sequence_dialog.h"
#include "frame_sequence.h"

#include <QCheckBox>
#include <QCollator>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include <algorithm>
#include <utility>

std::optional<ActionAsset> importFrameSequence(ActionSlot slot, QWidget* parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("导入序列帧 · %1").arg(actionSlotName(slot)));
    auto* layout = new QVBoxLayout(&dialog);
    auto* columns = new QHBoxLayout;
    auto* ordering = new QVBoxLayout;
    auto* list = new QListWidget(&dialog);
    list->setAccessibleName(QStringLiteral("播放顺序"));
    list->setMinimumWidth(255);
    auto* edit = new QHBoxLayout;
    auto* add = new QPushButton(QStringLiteral("添加图片…"), &dialog);
    auto* up = new QPushButton(QStringLiteral("上移"), &dialog);
    auto* down = new QPushButton(QStringLiteral("下移"), &dialog);
    auto* remove = new QPushButton(QStringLiteral("移除"), &dialog);
    for (auto* button : {add, up, down, remove}) {
        edit->addWidget(button);
    }
    ordering->addWidget(new QLabel(QStringLiteral("播放顺序"), &dialog));
    ordering->addWidget(list);
    ordering->addLayout(edit);
    columns->addLayout(ordering, 1);

    auto* previewColumn = new QVBoxLayout;
    auto* preview = new QLabel(&dialog);
    preview->setAlignment(Qt::AlignCenter);
    preview->setFixedSize(310, 240);
    auto* dark = new QCheckBox(QStringLiteral("深色预览背景"), &dialog);
    const auto setBackground = [preview](bool enabled) {
        preview->setStyleSheet(enabled ? QStringLiteral("background-color: #26272b; color: #f2f2f2;")
                                       : QStringLiteral("background-color: palette(base);"));
    };
    setBackground(false);
    QObject::connect(dark, &QCheckBox::toggled, &dialog, setBackground);
    previewColumn->addWidget(new QLabel(QStringLiteral("循环预览"), &dialog));
    previewColumn->addWidget(preview);
    previewColumn->addWidget(dark);
    columns->addLayout(previewColumn);
    layout->addLayout(columns);

    auto* options = new QHBoxLayout;
    auto* fps = new QSpinBox(&dialog);
    fps->setRange(1, maxSequenceFps);
    fps->setValue(12);
    fps->setSuffix(QStringLiteral(" 帧/秒"));
    auto* speed = new QLabel(QStringLiteral("播放速度"), &dialog);
    speed->setBuddy(fps);
    auto* status = new QLabel(&dialog);
    status->setWordWrap(true);
    status->setTextFormat(Qt::PlainText);
    options->addWidget(speed);
    options->addWidget(fps);
    options->addWidget(status, 1);
    layout->addLayout(options);

    auto* buttons = new QDialogButtonBox(&dialog);
    auto* generate = buttons->addButton(QStringLiteral("生成预览"), QDialogButtonBox::ActionRole);
    auto* save = buttons->addButton(QStringLiteral("保存到%1").arg(actionSlotName(slot)), QDialogButtonBox::AcceptRole);
    buttons->addButton(QStringLiteral("取消"), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);

    std::optional<PetAsset> rendered;
    QFutureWatcher<FrameSequenceResult> job;
    bool busy = false;
    const auto updateControls = [&] {
        const int row = list->currentRow();
        const int count = list->count();
        list->setEnabled(!busy);
        add->setEnabled(!busy && count < maxSequenceFrames);
        up->setEnabled(!busy && row > 0);
        down->setEnabled(!busy && row >= 0 && row + 1 < count);
        remove->setEnabled(!busy && row >= 0);
        fps->setEnabled(!busy);
        generate->setEnabled(!busy && count >= minSequenceFrames);
        save->setEnabled(!busy && rendered.has_value());
    };
    const auto resetPreview = [&] {
        preview->setMovie(nullptr);
        rendered.reset();
        preview->setText(QStringLiteral("生成后可在这里预览"));
        status->setText(list->count() == 0 ? QStringLiteral("请选择 %1–%2 张图片。").arg(minSequenceFrames).arg(maxSequenceFrames)
                                         : QStringLiteral("已选择 %1 帧。").arg(list->count()));
        updateControls();
    };
    QObject::connect(add, &QPushButton::clicked, &dialog, [&] {
        auto paths = QFileDialog::getOpenFileNames(&dialog, QStringLiteral("选择序列帧"), {},
            QStringLiteral("图片 (*.png *.jpg *.jpeg *.avif);;所有文件 (*)"));
        if (paths.isEmpty()) {
            return;
        }
        if (paths.size() > maxSequenceFrames - list->count()) {
            status->setText(QStringLiteral("最多支持 %1 帧，请减少这次选择的图片。").arg(maxSequenceFrames));
            return;
        }
        QCollator collator{QLocale(QLocale::English)};
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        std::stable_sort(paths.begin(), paths.end(), [&collator](const QString& left, const QString& right) {
            return collator.compare(QFileInfo(left).fileName(), QFileInfo(right).fileName()) < 0;
        });
        const int firstAdded = list->count();
        for (const auto& path : paths) {
            auto* item = new QListWidgetItem(QFileInfo(path).fileName(), list);
            item->setData(Qt::UserRole, path);
            item->setToolTip(path);
        }
        list->setCurrentRow(firstAdded);
        resetPreview();
    });
    const auto moveFrame = [&](int offset) {
        const int row = list->currentRow();
        list->insertItem(row + offset, list->takeItem(row));
        list->setCurrentRow(row + offset);
        resetPreview();
    };
    QObject::connect(list, &QListWidget::currentRowChanged, &dialog, updateControls);
    QObject::connect(up, &QPushButton::clicked, &dialog, [&] { moveFrame(-1); });
    QObject::connect(down, &QPushButton::clicked, &dialog, [&] { moveFrame(1); });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [&] {
        delete list->takeItem(list->currentRow());
        resetPreview();
    });
    QObject::connect(fps, &QSpinBox::valueChanged, &dialog, resetPreview);

    QObject::connect(generate, &QPushButton::clicked, &dialog, [&] {
        QStringList files;
        for (int row = 0; row < list->count(); ++row) {
            files.append(list->item(row)->data(Qt::UserRole).toString());
        }
        const int rate = fps->value();
        busy = true;
        resetPreview();
        status->setText(QStringLiteral("正在生成 %1 帧的预览…").arg(files.size()));
        job.setFuture(QtConcurrent::run([files = std::move(files), rate](QPromise<FrameSequenceResult>& promise) {
            promise.setProgressRange(0, static_cast<int>(files.size()) * 2);
            auto result = encodeFrameSequence(files, rate, [&promise](int done) {
                promise.setProgressValue(done);
                return !promise.isCanceled();
            });
            if (!promise.isCanceled()) {
                promise.addResult(std::move(result));
            }
        }));
    });
    QObject::connect(&job, &QFutureWatcher<FrameSequenceResult>::progressValueChanged, &dialog, [&](int done) {
        const int count = list->count();
        status->setText(done < count ? QStringLiteral("正在读取 %1 / %2 帧…").arg(done).arg(count)
                                    : QStringLiteral("正在合成 %1 / %2 帧…").arg(done - count).arg(count));
    });
    QObject::connect(&job, &QFutureWatcher<FrameSequenceResult>::finished, &dialog, [&] {
        busy = false;
        if (job.isCanceled()) {
            updateControls();
            return;
        }
        auto result = job.result();
        if (result.error.isEmpty()) {
            rendered = decodePetAsset(std::move(result.gif), result.error);
        }
        if (rendered && rendered->movie && rendered->movie->frameCount() == list->count()) {
            auto* movie = rendered->movie.get();
            const QSize size = rendered->firstFrame.size();
            movie->setScaledSize(size.scaled(294, 224, Qt::KeepAspectRatio).expandedTo(QSize(1, 1)));
            preview->setMovie(movie);
            movie->start();
            status->setText(QStringLiteral("%1 帧 · %2 × %3 像素").arg(list->count()).arg(size.width()).arg(size.height()));
        } else {
            rendered.reset();
            status->setText(result.error.isEmpty() ? QStringLiteral("无法播放生成的动图。") : result.error);
        }
        updateControls();
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    resetPreview();
    const int choice = dialog.exec();
    job.cancel();
    preview->setMovie(nullptr);
    if (choice == QDialog::Accepted && rendered) {
        return ActionAsset{QStringLiteral("序列帧 · %1 帧 / %2 FPS").arg(list->count()).arg(fps->value()),
                           std::move(rendered->source)};
    }
    return std::nullopt;
}
