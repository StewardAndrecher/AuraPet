#include "import_preview.h"
#include "background_removal.h"
#include "gif_background.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QtConcurrentRun>

#include <utility>

namespace {
struct CutoutPreview {
    QImage stillImage;
    QByteArray gif;
    QString error;
};

QLabel* previewPane(QWidget* parent)
{
    auto* label = new QLabel(parent);
    label->setFixedSize(250, 220);
    label->setAlignment(Qt::AlignCenter);
    return label;
}

void showImage(QLabel* pane, const QImage& image)
{
    const QSize size = pane->size() * pane->devicePixelRatioF();
    QPixmap pixmap = QPixmap::fromImage(image.scaled(size, Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation));
    pixmap.setDevicePixelRatio(pane->devicePixelRatioF());
    pane->setPixmap(pixmap);
}
}

std::optional<PetAsset> previewPetImport(PetAsset asset, QWidget* parent, QString& error)
{
    const QImage original = asset.firstFrame.toImage();
    if (hasTransparentPixels(original)) {
        return asset;
    }
    const bool animated = static_cast<bool>(asset.movie);
    std::optional<PetAsset> originalPreview;
    if (animated) {
        originalPreview = decodePetAsset(asset.source, error);
        if (!originalPreview) {
            return std::nullopt;
        }
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("桌宠素材预览"));
    auto* layout = new QGridLayout(&dialog);
    layout->addWidget(new QLabel(animated ? QStringLiteral("原动图") : QStringLiteral("原图"), &dialog), 0, 0);
    layout->addWidget(new QLabel(QStringLiteral("抠图预览"), &dialog), 0, 1);
    auto* before = previewPane(&dialog);
    auto* after = previewPane(&dialog);
    layout->addWidget(before, 1, 0);
    layout->addWidget(after, 1, 1);
    showImage(before, original);

    auto* dark = new QCheckBox(QStringLiteral("深色预览背景"), &dialog);
    auto* status = new QLabel(QStringLiteral("正在本机识别主体…"), &dialog);
    status->setWordWrap(true);
    layout->addWidget(dark, 2, 0, 1, 2);
    layout->addWidget(status, 3, 0, 1, 2);
    const auto setBackground = [before, after](bool darkMode) {
        const QString style = darkMode ? QStringLiteral("background-color: #26272b;")
                                       : QStringLiteral("background-color: #e5e8ec;");
        before->setStyleSheet(style);
        after->setStyleSheet(style);
    };
    setBackground(false);
    QObject::connect(dark, &QCheckBox::toggled, &dialog, setBackground);

    constexpr int useOriginal = 2;
    auto* buttons = new QDialogButtonBox(&dialog);
    auto* keep = buttons->addButton(animated ? QStringLiteral("保留原动图") : QStringLiteral("保留原图"),
                                    QDialogButtonBox::ActionRole);
    auto* apply = buttons->addButton(QStringLiteral("应用抠图"), QDialogButtonBox::AcceptRole);
    auto* cancel = buttons->addButton(QStringLiteral("取消导入"), QDialogButtonBox::RejectRole);
    apply->setEnabled(false);
    cancel->setDefault(true);
    layout->addWidget(buttons, 4, 0, 1, 2);
    QObject::connect(keep, &QPushButton::clicked, &dialog, [&dialog] { dialog.done(useOriginal); });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QMovie* originalMovie = originalPreview ? originalPreview->movie.get() : nullptr;
    std::optional<PetAsset> cutoutPreview;
    if (originalMovie) {
        auto* replay = buttons->addButton(QStringLiteral("重新播放"), QDialogButtonBox::ActionRole);
        QObject::connect(replay, &QPushButton::clicked, &dialog, [&] {
            auto* movie = cutoutPreview ? cutoutPreview->movie.get() : originalMovie;
            movie->stop();
            movie->start();
        });
        QObject::connect(originalMovie, &QMovie::frameChanged, &dialog, [before, originalMovie] {
            showImage(before, originalMovie->currentPixmap().toImage());
        });
        originalMovie->start();
    }

    CutoutPreview result;
    QFutureWatcher<CutoutPreview> job;
    QObject::connect(&job, &QFutureWatcher<CutoutPreview>::progressValueChanged, &dialog, [&](int done) {
        status->setText(QStringLiteral("正在本机处理动图，已完成 %1 / %2 帧…")
                        .arg(done).arg(job.progressMaximum()));
    });
    QObject::connect(&job, &QFutureWatcher<CutoutPreview>::finished, &dialog, [&] {
        if (job.isCanceled()) {
            return;
        }
        result = job.result();
        if (!result.error.isEmpty()) {
            status->setText(result.error);
            return;
        }
        if (originalMovie) {
            QString previewError;
            cutoutPreview = decodePetAsset(std::move(result.gif), previewError);
            if (!cutoutPreview || !cutoutPreview->movie) {
                status->setText(previewError.isEmpty() ? QStringLiteral("无法播放抠图后的 GIF。") : previewError);
                cutoutPreview.reset();
                return;
            }
            originalMovie->stop();
            auto* cutoutMovie = cutoutPreview->movie.get();
            // One playback clock presents the same frame from both versions.
            QObject::connect(cutoutMovie, &QMovie::frameChanged, &dialog, [=](int frame) {
                originalMovie->jumpToFrame(frame);
                showImage(after, cutoutMovie->currentPixmap().toImage());
            });
            cutoutMovie->start();
        } else {
            showImage(after, result.stillImage);
        }
        apply->setEnabled(true);
        apply->setDefault(true);
        status->setText(QStringLiteral("切换深浅背景可以检查主体边缘。"));
    });
    const QByteArray gif = animated ? asset.source : QByteArray{};
    job.setFuture(QtConcurrent::run([original, gif](QPromise<CutoutPreview>& promise) {
        if (promise.isCanceled()) {
            return;
        }
        if (!gif.isEmpty()) {
            auto cutout = removeGifBackground(gif, [&promise](int done, int total) {
                if (done == 0) {
                    promise.setProgressRange(0, total);
                }
                promise.setProgressValue(done);
                return !promise.isCanceled();
            });
            promise.addResult(CutoutPreview{{}, std::move(cutout.gif), std::move(cutout.error)});
        } else {
            auto cutout = removeStaticBackground(original);
            promise.addResult(CutoutPreview{std::move(cutout.image), {}, std::move(cutout.error)});
        }
    }));

    const int choice = dialog.exec();
    job.cancel();
    if (choice == useOriginal) {
        return asset;
    }
    if (choice == QDialog::Accepted) {
        if (cutoutPreview) {
            return decodePetAsset(std::move(cutoutPreview->source), error);
        }
        if (!result.stillImage.isNull()) {
            return makePngPetAsset(result.stillImage, error);
        }
    }
    return std::nullopt;
}
