#pragma once

#include <QDialog>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QtConcurrentRun>
#include <QVBoxLayout>

#include <optional>
#include <utility>

template<typename Result, typename Task>
std::optional<Result> waitForBackground(QWidget& parent, const QString& title, Task task)
{
    QDialog dialog(&parent);
    dialog.setWindowTitle(title);
    dialog.setWindowModality(parent.isVisible() ? Qt::WindowModal : Qt::ApplicationModal);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(10);
    auto* label = new QLabel(title, &dialog);
    auto* bar = new QProgressBar(&dialog);
    bar->setRange(0, 0);
    auto* cancel = new QPushButton(QStringLiteral("取消"), &dialog);
    layout->addWidget(label);
    layout->addWidget(bar);
    layout->addWidget(cancel, 0, Qt::AlignRight);
    dialog.resize(360, dialog.sizeHint().height());

    QFutureWatcher<Result> watcher;
    QEventLoop loop;
    QTimer revealDelay;
    revealDelay.setSingleShot(true);
    revealDelay.setInterval(160);

    QObject::connect(&watcher, &QFutureWatcher<Result>::finished, &loop, [&] {
        revealDelay.stop();
        loop.quit();
    });
    QObject::connect(&revealDelay, &QTimer::timeout, &dialog, [&] {
        if (!watcher.future().isFinished()) {
            dialog.show();
        }
    });
    QObject::connect(&watcher, &QFutureWatcher<Result>::progressRangeChanged, bar, &QProgressBar::setRange);
    QObject::connect(&watcher, &QFutureWatcher<Result>::progressValueChanged, bar, &QProgressBar::setValue);
    const auto stop = [&] {
        watcher.cancel();
        label->setText(QStringLiteral("正在取消…"));
        cancel->setEnabled(false);
    };
    QObject::connect(cancel, &QPushButton::clicked, &dialog, stop);
    QObject::connect(&dialog, &QDialog::rejected, &dialog, stop);

    watcher.setFuture(QtConcurrent::run([job = std::move(task)](QPromise<Result>& promise) mutable {
        auto value = job(promise);
        if (!promise.isCanceled()) {
            promise.addResult(std::move(value));
        }
    }));
    if (!watcher.future().isFinished()) {
        revealDelay.start();
        loop.exec();
    }
    watcher.waitForFinished();
    auto future = watcher.future();
    if (future.isCanceled() || future.resultCount() != 1) {
        return std::nullopt;
    }
    return future.takeResult();
}
