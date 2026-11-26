#include "random_speech_dialog.h"
#include "random_speech.h"
#include "role_library.h"

#include "speech_playback.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>

namespace {
constexpr int keyRole = Qt::UserRole;
constexpr int textRole = Qt::UserRole + 1;

QString editLine(QWidget& parent, const QString& before)
{
    bool ok = false;
    const auto text = QInputDialog::getMultiLineText(&parent, QStringLiteral("编辑随机发言"),
        QStringLiteral("填写台词，最多 280 字"), before, &ok).trimmed();
    if (ok && (text.isEmpty() || text.size() > 280)) {
        QMessageBox::information(&parent, QStringLiteral("请检查台词"), QStringLiteral("台词需填写 1 至 280 字。"));
        return {};
    }
    return ok ? text : QString{};
}

void setLineText(QTreeWidgetItem& item, const QString& text)
{
    item.setData(1, textRole, text);
    item.setText(1, text.simplified());
    item.setToolTip(1, text.toHtmlEscaped());
}
}

void editRandomSpeech(QWidget& parent, RoleLibrary& library, const QString& roleId)
{
    QString error;
    const auto share = library.shareFor(roleId, error, false);
    const auto paths = error.isEmpty() ? library.corpusPaths(roleId, error) : QStringList{};
    if (!error.isEmpty()) {
        QMessageBox::warning(&parent, QStringLiteral("无法读取随机发言"), error);
        return;
    }
    const auto original = randomSpeechConfig(share.behavior);
    QDialog dialog(&parent);
    dialog.setWindowTitle(QStringLiteral("随机发言"));
    dialog.resize(800, 555);
    auto* layout = new QVBoxLayout(&dialog);
    auto* withAi = new QCheckBox(QStringLiteral("已接入 AI 时，优先生成自动发言"), &dialog);
    withAi->setChecked(original.withAi);
    layout->addWidget(withAi);
    auto* hint = new QLabel(QStringLiteral("AI 不可用或关闭此选项时，双击会从已勾选的台词和原声中随机取一条。"), &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto* tabs = new QTabWidget(&dialog);
    layout->addWidget(tabs, 1);

    auto* examples = new QListWidget(tabs);
    examples->setObjectName(QStringLiteral("randomSpeechExamples"));
    examples->setAlternatingRowColors(true);
    examples->setWordWrap(true);
    for (const auto& sample : randomSpeechExamples()) {
        auto* item = new QListWidgetItem(sample.text, examples);
        item->setData(keyRole, sample.id);
        item->setCheckState(original.examples.contains(sample.id) ? Qt::Checked : Qt::Unchecked);
    }
    tabs->addTab(examples, QStringLiteral("内置示例（%1）").arg(examples->count()));

    auto* customPage = new QWidget(tabs);
    auto* customLayout = new QVBoxLayout(customPage);
    auto* custom = new QTreeWidget(customPage);
    custom->setObjectName(QStringLiteral("randomSpeechCustom"));
    custom->setHeaderLabels({QStringLiteral("启用"), QStringLiteral("台词")});
    custom->header()->resizeSection(0, 65);
    custom->setRootIsDecorated(false);
    custom->setAlternatingRowColors(true);
    customLayout->addWidget(custom, 1);
    const auto addCustom = [&](const RandomSpeechLine& line) {
        auto* item = new QTreeWidgetItem(custom);
        item->setData(0, keyRole, line.id);
        item->setCheckState(0, line.enabled ? Qt::Checked : Qt::Unchecked);
        setLineText(*item, line.text);
        return item;
    };
    for (const auto& line : original.lines) {
        addCustom(line);
    }
    auto* customButtons = new QHBoxLayout;
    auto* add = new QPushButton(QStringLiteral("添加台词…"), customPage);
    auto* edit = new QPushButton(QStringLiteral("编辑…"), customPage);
    auto* remove = new QPushButton(QStringLiteral("删除"), customPage);
    for (auto* button : {add, edit, remove}) {
        customButtons->addWidget(button);
    }
    customButtons->addStretch();
    customLayout->addLayout(customButtons);
    tabs->addTab(customPage, QStringLiteral("自定义台词"));

    auto* audioPage = new QWidget(tabs);
    auto* audioLayout = new QVBoxLayout(audioPage);
    auto* audioHint = new QLabel(paths.isEmpty() ? QStringLiteral("可先在角色卡中导入 WAV 录音。")
        : QStringLiteral("原声在本机直接播放，独立于合成语音开关，仍遵守免打扰。已有台词会显示在气泡里。"), audioPage);
    audioHint->setWordWrap(true);
    audioLayout->addWidget(audioHint);
    auto* audio = new QTreeWidget(audioPage);
    audio->setObjectName(QStringLiteral("randomSpeechRecordings"));
    audio->setHeaderLabels({QStringLiteral("启用"), QStringLiteral("录音原文件名"), QStringLiteral("台词")});
    audio->header()->resizeSection(0, 65);
    audio->header()->resizeSection(1, 235);
    audio->setAlternatingRowColors(true);
    audioLayout->addWidget(audio, 1);
    QList<QTreeWidgetItem*> recordings;
    for (const auto& [tag, language] : {qMakePair(QStringLiteral("zh"), QStringLiteral("中文")),
                                       qMakePair(QStringLiteral("ja"), QStringLiteral("日文"))}) {
        const auto prefix = QStringLiteral("voice/corpus/%1/").arg(tag);
        auto* group = new QTreeWidgetItem(audio);
        for (const auto& path : paths) {
            if (!path.startsWith(prefix) || !isRandomSpeechRecording(path)) {
                continue;
            }
            const auto name = share.corpusNames.value(path).toString(path.section(u'/', -1));
            const auto text = share.corpusTexts.value(path).toString();
            auto* item = new QTreeWidgetItem(group, {QString{}, name, text.simplified()});
            item->setData(0, keyRole, path);
            item->setCheckState(0, original.recordings.contains(path) ? Qt::Checked : Qt::Unchecked);
            item->setToolTip(1, name.toHtmlEscaped());
            item->setToolTip(2, text.toHtmlEscaped());
            recordings.append(item);
        }
        if (group->childCount() == 0) {
            delete group;
        } else {
            group->setText(1, QStringLiteral("%1 · %2 条").arg(language).arg(group->childCount()));
            group->setExpanded(true);
        }
    }
    const auto player = makeRecordingOutput();
    auto* listen = new QPushButton(QStringLiteral("试听所选录音"), audioPage);
    auto* stop = new QPushButton(QStringLiteral("停止试听"), audioPage);
    auto* previewButtons = new QHBoxLayout;
    previewButtons->addWidget(listen);
    previewButtons->addWidget(stop);
    previewButtons->addStretch();
    audioLayout->addLayout(previewButtons);
    QTimer previewTimer;
    previewTimer.setInterval(200);
    const auto updatePreview = [&] {
        listen->setEnabled(audio->currentItem() && !audio->currentItem()->data(0, keyRole).toString().isEmpty());
        stop->setEnabled(player->active());
        if (!player->active()) {
            previewTimer.stop();
        }
    };
    QObject::connect(audio, &QTreeWidget::currentItemChanged, &dialog, updatePreview);
    QObject::connect(&previewTimer, &QTimer::timeout, &dialog, updatePreview);
    QObject::connect(listen, &QPushButton::clicked, &dialog, [&] {
        if (!audio->currentItem()) {
            return;
        }
        QString failure;
        const auto key = audio->currentItem()->data(0, keyRole).toString();
        if (!player->play(library.roleRecording(roleId, key, failure))) {
            QMessageBox::warning(&dialog, QStringLiteral("无法试听"),
                failure.isEmpty() ? QStringLiteral("这段 WAV 录音无法播放。") : failure);
        } else {
            previewTimer.start();
        }
        updatePreview();
    });
    QObject::connect(stop, &QPushButton::clicked, &dialog, [&] { player->reset(); updatePreview(); });
    QObject::connect(tabs, &QTabWidget::currentChanged, &dialog, [&](int index) {
        if (index != 2) {
            player->reset();
            updatePreview();
        }
    });
    updatePreview();
    tabs->addTab(audioPage, QStringLiteral("角色原声（%1）").arg(recordings.size()));

    auto* footer = new QHBoxLayout;
    auto* selectAll = new QPushButton(QStringLiteral("勾选本页"), &dialog);
    auto* selectNone = new QPushButton(QStringLiteral("取消勾选本页"), &dialog);
    auto* summary = new QLabel(&dialog);
    footer->addWidget(selectAll);
    footer->addWidget(selectNone);
    footer->addWidget(summary, 1);
    auto* buttons = new QDialogButtonBox(&dialog);
    buttons->addButton(QStringLiteral("保存"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QStringLiteral("取消"), QDialogButtonBox::RejectRole);
    footer->addWidget(buttons);
    layout->addLayout(footer);

    const auto exampleChoices = [&] {
        QStringList enabled;
        for (int i = 0; i < examples->count(); ++i) {
            const auto* item = examples->item(i);
            if (item->checkState() == Qt::Checked) {
                enabled.append(item->data(keyRole).toString());
            }
        }
        return enabled;
    };
    const auto customChoices = [&] {
        QList<RandomSpeechLine> lines;
        for (int i = 0; i < custom->topLevelItemCount(); ++i) {
            const auto* item = custom->topLevelItem(i);
            lines.append({item->data(0, keyRole).toString(), item->data(1, textRole).toString(),
                          item->checkState(0) == Qt::Checked});
        }
        return lines;
    };
    const auto refresh = [&] {
        int count = exampleChoices().size();
        for (const auto& line : customChoices()) {
            count += line.enabled;
        }
        for (const auto* item : recordings) {
            count += item->checkState(0) == Qt::Checked;
        }
        summary->setText(QStringLiteral("启用 %1 条").arg(count));
        edit->setEnabled(custom->currentItem() != nullptr);
        remove->setEnabled(edit->isEnabled());
        add->setEnabled(custom->topLevelItemCount() < 100);
    };
    const auto editSelected = [&] {
        if (auto* item = custom->currentItem()) {
            const auto text = editLine(dialog, item->data(1, textRole).toString());
            if (!text.isEmpty()) {
                setLineText(*item, text);
            }
        }
    };
    QObject::connect(add, &QPushButton::clicked, &dialog, [&] {
        const auto text = editLine(dialog, {});
        if (!text.isEmpty()) {
            custom->setCurrentItem(addCustom({QUuid::createUuid().toString(QUuid::WithoutBraces), text, true}));
            refresh();
        }
    });
    QObject::connect(edit, &QPushButton::clicked, &dialog, editSelected);
    QObject::connect(custom, &QTreeWidget::itemDoubleClicked, &dialog, [&](QTreeWidgetItem*, int column) {
        if (column == 1) {
            editSelected();
        }
    });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [&] {
        delete custom->currentItem();
        refresh();
    });
    const auto setPageCheck = [&](Qt::CheckState state) {
        switch (tabs->currentIndex()) {
        case 0:
            for (int i = 0; i < examples->count(); ++i) {
                examples->item(i)->setCheckState(state);
            }
            break;
        case 1:
            for (int i = 0; i < custom->topLevelItemCount(); ++i) {
                custom->topLevelItem(i)->setCheckState(0, state);
            }
            break;
        case 2:
            for (auto* item : recordings) {
                item->setCheckState(0, state);
            }
            break;
        }
    };
    QObject::connect(selectAll, &QPushButton::clicked, &dialog, [&] { setPageCheck(Qt::Checked); });
    QObject::connect(selectNone, &QPushButton::clicked, &dialog, [&] { setPageCheck(Qt::Unchecked); });
    QObject::connect(examples, &QListWidget::itemChanged, &dialog, refresh);
    QObject::connect(custom, &QTreeWidget::itemChanged, &dialog, refresh);
    QObject::connect(custom, &QTreeWidget::currentItemChanged, &dialog, refresh);
    QObject::connect(audio, &QTreeWidget::itemChanged, &dialog, refresh);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        RandomSpeechConfig config{exampleChoices(), customChoices(), {}, withAi->isChecked()};
        QString failure;
        for (const auto* item : recordings) {
            if (item->checkState(0) != Qt::Checked) {
                continue;
            }
            const auto key = item->data(0, keyRole).toString();
            auto digest = original.recordings.value(key).toString();
            if (digest.isEmpty()) {
                const auto bytes = library.roleRecording(roleId, key, failure);
                if (bytes.isEmpty()) {
                    QMessageBox::warning(&dialog, QStringLiteral("无法保存随机发言"), failure);
                    return;
                }
                digest = recordingDigest(bytes);
            }
            config.recordings.insert(key, digest);
        }
        if (library.saveRandomSpeech(roleId, encodeRandomSpeech(config), failure)) {
            dialog.accept();
        } else {
            QMessageBox::warning(&dialog, QStringLiteral("无法保存随机发言"), failure);
        }
    });
    refresh();
    dialog.exec();
}
