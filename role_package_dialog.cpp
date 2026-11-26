#include "role_package.h"
#include "background_task.h"
#include "role_card_editor.h"
#include "role_actions.h"
#include "companion_events.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTreeWidget>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <array>
#include <tuple>
#include <utility>

namespace {
QDir documents()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
}

QByteArray readBounded(const QString& path, qint64 limit)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.isSequential() || file.size() > limit) {
        return {};
    }
    const auto bytes = file.read(limit + 1);
    return file.error() == QFileDevice::NoError && bytes.size() <= limit ? bytes : QByteArray{};
}

QString languageLabel(const QString& language)
{
    return language == QStringLiteral("zh") ? QStringLiteral("中文") : QStringLiteral("日文");
}

QString corpusPrefix(const QString& language)
{
    return QStringLiteral("voice/corpus/%1/").arg(language);
}

qsizetype corpusCount(const RoleShare& share, const QString& prefix = QStringLiteral("voice/corpus/"))
{
    qsizetype count = 0;
    for (auto it = share.files.cbegin(); it != share.files.cend(); ++it) {
        count += it.key().startsWith(prefix);
    }
    return count;
}

QStringList sections(const RolePackage& package)
{
    QStringList result{QStringLiteral("待机素材")};
    for (const auto slot : {ActionSlot::Happy, ActionSlot::Sleepy}) {
        if (package.actions.contains(slot)) {
            result.append(actionSlotName(slot) + QStringLiteral("动作"));
        }
    }
    if (!package.share.card.isEmpty()) {
        result.append(QStringLiteral("AI 角色卡"));
    }
    for (const auto& language : package.share.voices.keys()) {
        result.append(languageLabel(language) + QStringLiteral("参考音色"));
    }
    if (!package.share.behavior.isEmpty()) {
        result.append(QStringLiteral("猫粮与行为预设"));
    }
    return result;
}

QComboBox* options(QWidget* parent, const QList<QPair<QString, QString>>& items, const QString& selected)
{
    auto* box = new QComboBox(parent);
    for (const auto& [label, key] : items) {
        box->addItem(label, key);
    }
    box->setCurrentIndex(qMax(0, box->findData(selected)));
    return box;
}
QTreeWidget* importContents(QWidget& parent, const RolePackage& package)
{
    auto* tree = new QTreeWidget(&parent);
    tree->setObjectName(QStringLiteral("rolePackageContents"));
    tree->setHeaderLabels({QStringLiteral("将导入"), QStringLiteral("内容")});
    tree->setRootIsDecorated(false);
    tree->setMinimumHeight(170);
    const auto row = [&](const QString& name, const QString& detail) {
        auto* item = new QTreeWidgetItem(tree, {name, detail.simplified()});
        item->setToolTip(1, QStringLiteral("<qt>%1</qt>").arg(detail.toHtmlEscaped()));
    };
    row(QStringLiteral("外观素材"), QStringLiteral("原始待机素材"));
    for (const auto slot : {ActionSlot::Idle, ActionSlot::Happy, ActionSlot::Sleepy}) {
        if (package.actions.contains(slot)) {
            const auto item = package.actions.value(slot);
            row(actionSlotName(slot) + QStringLiteral("动作"), item.name);
        }
    }
    if (!package.share.card.isEmpty()) {
        row(QStringLiteral("AI 角色卡"), package.share.card.value(QStringLiteral("displayName")).toString(package.name));
    }
    for (const auto& language : package.share.voices.keys()) {
        row(languageLabel(language) + QStringLiteral("音色"), QStringLiteral("参考录音与文字转写"));
    }
    for (const auto& language : {QStringLiteral("zh"), QStringLiteral("ja")}) {
        const auto count = corpusCount(package.share, corpusPrefix(language));
        if (count) {
            row(languageLabel(language) + QStringLiteral("语料"), QStringLiteral("%1 段 WAV").arg(count));
        }
    }
    for (const auto& [label, description] : describeRoleBehavior(package.share.behavior)) {
        row(label, description);
    }
    tree->resizeColumnToContents(0);
    return tree;
}

}

QString importRolePackage(QWidget& parent, RoleLibrary& library, const QString& path)
{
    QString error;
    if (!library.available()) {
        QMessageBox::warning(&parent, QStringLiteral("无法导入角色"), QStringLiteral("角色库尚未就绪。"));
        return {};
    }
    using Loading = QPair<std::optional<RolePackage>, QString>;
    const auto loaded = waitForBackground<Loading>(parent, QStringLiteral("正在校验并读取角色包…"),
        [path](QPromise<Loading>& promise) {
            promise.setProgressRange(0, 100);
            Loading result;
            result.first = readRolePackage(path, result.second, [&](int progress) {
                promise.setProgressValue(progress);
                return !promise.isCanceled();
            });
            return result;
        });
    if (!loaded) {
        return {};
    }
    const auto& package = loaded->first;
    if (!package) {
        QMessageBox::warning(&parent, QStringLiteral("无法导入角色包"), loaded->second);
        return {};
    }
    const auto match = library.matchingPackage(package->id, error);
    if (!error.isEmpty()) {
        QMessageBox::warning(&parent, QStringLiteral("无法读取角色库"), error);
        return {};
    }
    QDialog preview(&parent);
    preview.setWindowTitle(QStringLiteral("导入角色包"));
    preview.resize(540, 470);
    auto* layout = new QVBoxLayout(&preview);
    auto* cover = new QLabel(&preview);
    cover->setAlignment(Qt::AlignCenter);
    cover->setPixmap(QPixmap::fromImage(package->cover.scaled(148, 148, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    layout->addWidget(cover);
    auto* name = new QLabel(package->name, &preview);
    name->setTextFormat(Qt::PlainText);
    name->setAlignment(Qt::AlignCenter);
    layout->addWidget(name);
    layout->addWidget(importContents(preview, *package), 1);
    if (!package->share.card.isEmpty()) {
        auto* inspect = new QPushButton(QStringLiteral("查看角色卡…"), &preview);
        QObject::connect(inspect, &QPushButton::clicked, &preview, [&] {
            QDialog dialog(&preview);
            dialog.setWindowTitle(QStringLiteral("角色卡预览"));
            dialog.resize(650, 470);
            auto* content = new QVBoxLayout(&dialog);
            auto* text = new QPlainTextEdit(roleCardText(package->share.card), &dialog);
            text->setReadOnly(true);
            content->addWidget(text);
            auto* done = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
            QObject::connect(done, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            content->addWidget(done);
            dialog.exec();
        });
        layout->addWidget(inspect);
    }
    QString safety = QStringLiteral("角色包只读取支持的素材和配置。导入时不会运行脚本或上传文件。");
    if (!package->share.card.isEmpty()) {
        safety += QStringLiteral("启用 AI 聊天后，角色提示词会发送给你配置的 AI 服务。");
    }
    if (!package->share.voices.isEmpty()) {
        safety += QStringLiteral("包内音色需要本机千问；转发录音前请确认拥有分享权限。");
    }
    auto* note = new QLabel(safety, &preview);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &preview);
    auto* copy = buttons->addButton(match.isEmpty() ? QStringLiteral("导入角色") : QStringLiteral("保留为副本"),
                                    QDialogButtonBox::AcceptRole);
    bool replace = false;
    if (!match.isEmpty()) {
        for (const auto& role : library.entries(error)) {
            if (role.id == match) {
                auto* existing = new QLabel(QStringLiteral("更新「%1」时保留它的本机聊天记录。").arg(role.name), &preview);
                existing->setTextFormat(Qt::PlainText);
                existing->setWordWrap(true);
                layout->addWidget(existing);
                break;
            }
        }
        auto* update = buttons->addButton(QStringLiteral("更新角色"), QDialogButtonBox::ActionRole);
        QObject::connect(update, &QPushButton::clicked, &preview, [&] { replace = true; preview.accept(); });
    }
    layout->addWidget(buttons);
    QObject::connect(copy, &QPushButton::clicked, &preview, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &preview, &QDialog::reject);
    if (preview.exec() != QDialog::Accepted) {
        return {};
    }
    const auto id = library.importPackage(*package, replace ? match : QString{}, error);
    if (id.isEmpty()) {
        QMessageBox::warning(&parent, QStringLiteral("导入角色失败"), error);
    }
    return id;
}

void exportRolePackage(QWidget& parent, RoleLibrary& library, const QString& id, const QString& name)
{
    QString error;
    RolePackage package;
    package.id = library.packageIdFor(id, error);
    package.name = name;
    if (id == RoleLibrary::builtInId()) {
        package.asset = readBounded(QStringLiteral(":/default/凯猫.gif"), 32 * 1024 * 1024);
    } else if (error.isEmpty()) {
        package.asset = library.assetFor(id, error);
    }
    if (error.isEmpty()) {
        package.share = library.shareFor(id, error, false);
    }
    const auto fileStats = error.isEmpty() ? library.shareFileStats(id, error) : ShareFileStats{};
    for (const auto slot : {ActionSlot::Idle, ActionSlot::Happy, ActionSlot::Sleepy}) {
        if (!error.isEmpty()) {
            break;
        }
        const auto asset = library.actionFor(id, slot, error);
        if (asset) {
            package.actions.insert(slot, *asset);
        }
    }
    if (!error.isEmpty()) {
        QMessageBox::warning(&parent, QStringLiteral("无法读取角色"), error);
        return;
    }
    QDialog exportDialog(&parent);
    exportDialog.setWindowTitle(QStringLiteral("导出分享包"));
    auto* layout = new QVBoxLayout(&exportDialog);
    auto* details = new QLabel(sections(package).join(QStringLiteral("、")), &exportDialog);
    details->setWordWrap(true);
    layout->addWidget(details);
    auto* include = new QCheckBox(QStringLiteral("包含完整语音语料"), &exportDialog);
    auto* size = new QLabel(&exportDialog);
    layout->addWidget(size);
    const auto count = fileStats.corpusCount;
    if (count) {
        include->setText(QStringLiteral("包含完整语音语料（%1 份）").arg(count));
        layout->addWidget(include);
    } else {
        include->hide();
    }
    qint64 baseBytes = package.asset.size() + fileStats.otherBytes;
    for (const auto& action : package.actions) {
        baseBytes += action.bytes.size();
    }
    const auto showSize = [&] {
        const qint64 bytes = baseBytes + (include->isChecked() ? fileStats.corpusBytes : 0);
        size->setText(QStringLiteral("压缩前素材约 %1 MiB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1));
    };
    QObject::connect(include, &QCheckBox::toggled, &exportDialog, showSize);
    showSize();
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &exportDialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &exportDialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &exportDialog, &QDialog::reject);
    if (exportDialog.exec() != QDialog::Accepted) {
        return;
    }
    QString filename = name;
    filename.replace(u'/', u'-').replace(u'\\', u'-').replace(u':', u'-');
    QString file = QFileDialog::getSaveFileName(&parent, QStringLiteral("保存角色分享包"),
        documents().filePath(filename + QStringLiteral(".gifpet")), QStringLiteral("AuraPet 角色包 (*.gifpet)"));
    if (file.isEmpty()) {
        return;
    }
    if (!file.endsWith(QStringLiteral(".gifpet"), Qt::CaseInsensitive)) {
        file += QStringLiteral(".gifpet");
        if (QFileInfo::exists(file) && QMessageBox::question(&parent, QStringLiteral("替换分享包"),
            QStringLiteral("同名分享包已经存在。要替换吗？"), QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) != QMessageBox::Yes) {
            return;
        }
    }
    const bool includeCorpus = include->isChecked();
    package.share.files = library.shareFilesFor(id, includeCorpus, error);
    if (!error.isEmpty()) {
        QMessageBox::warning(&parent, QStringLiteral("导出角色失败"), error);
        return;
    }
    using Saving = QPair<bool, QString>;
    const auto saved = waitForBackground<Saving>(parent, QStringLiteral("正在压缩并保存角色包…"),
        [package = std::move(package), file, includeCorpus](QPromise<Saving>& promise) {
            promise.setProgressRange(0, 100);
            Saving result;
            result.first = writeRolePackage(file, package, includeCorpus, result.second, [&](int progress) {
                promise.setProgressValue(progress);
                return !promise.isCanceled();
            });
            return result;
        });
    if (!saved) {
        return;
    }
    if (!saved->first) {
        QMessageBox::warning(&parent, QStringLiteral("导出角色失败"), saved->second);
        return;
    }
    QMessageBox::information(&parent, QStringLiteral("分享包已保存"), QDir::toNativeSeparators(file));
}

bool editRoleShare(QWidget& parent, RoleLibrary& library, const QString& id)
{
    QString error;
    auto share = library.shareFor(id, error);
    if (!error.isEmpty()) {
        QMessageBox::warning(&parent, QStringLiteral("无法读取角色配置"), error);
        return false;
    }
    QDialog editor(&parent);
    editor.setWindowTitle(QStringLiteral("角色分享设置"));
    editor.resize(675, 580);
    auto* outer = new QVBoxLayout(&editor);
    auto* scroll = new QScrollArea(&editor);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll, 1);
    auto* body = new QWidget(scroll);
    scroll->setWidget(body);
    auto* form = new QFormLayout(body);
    auto* card = new QPushButton(&editor);
    auto* modifyCard = new QPushButton(QStringLiteral("编辑角色卡…"), &editor);
    const auto refreshCard = [&] {
        card->setText(share.card.isEmpty() ? QStringLiteral("选择角色卡 JSON…") : QStringLiteral("已关联角色卡 · 更换…"));
    };
    refreshCard();
    auto* clearCard = new QPushButton(QStringLiteral("移除角色卡"), &editor);
    auto* cardRow = new QHBoxLayout;
    cardRow->addWidget(modifyCard);
    cardRow->addWidget(card);
    cardRow->addWidget(clearCard);
    form->addRow(QStringLiteral("角色卡"), cardRow);
    QObject::connect(modifyCard, &QPushButton::clicked, &editor, [&] {
        QString name;
        QString ignored;
        for (const auto& role : library.entries(ignored)) {
            if (role.id == id) {
                name = role.name;
                break;
            }
        }
        if (editRoleCard(editor, share.card, id, name)) {
            refreshCard();
        }
    });
    QObject::connect(clearCard, &QPushButton::clicked, &editor, [&] { share.card = {}; refreshCard(); });
    QObject::connect(card, &QPushButton::clicked, &editor, [&] {
        const auto path = QFileDialog::getOpenFileName(&editor, QStringLiteral("选择角色卡"),
            documents().path(), QStringLiteral("角色卡 (*.json)"));
        if (path.isEmpty()) {
            return;
        }
        const auto doc = QJsonDocument::fromJson(readBounded(path, 128 * 1024));
        auto candidate = share;
        candidate.card = doc.object();
        QString problem;
        if (candidate.card.isEmpty() || !validateRoleShare(candidate, problem)) {
            QMessageBox::warning(&editor, QStringLiteral("无法关联角色卡"),
                problem.isEmpty() ? QStringLiteral("请选择有效的角色卡 JSON。") : problem);
        } else {
            share.card = candidate.card;
            refreshCard();
        }
    });

    auto* kibble = options(&editor, {{QStringLiteral("沿用本机设置"), {}}, {QStringLiteral("无"), QStringLiteral("none")},
        {QStringLiteral("慢食猫粮"), QStringLiteral("slow")}, {QStringLiteral("高能猫粮"), QStringLiteral("high")}},
        share.behavior.value(QStringLiteral("kibble")).toString());
    form->addRow(QStringLiteral("默认猫粮"), kibble);
    const auto movement = share.behavior.value(QStringLiteral("randomMovement"));
    auto* roam = options(&editor, {{QStringLiteral("沿用本机设置"), {}}, {QStringLiteral("开启"), QStringLiteral("on")},
        {QStringLiteral("关闭"), QStringLiteral("off")}}, movement.isBool()
        ? (movement.toBool() ? QStringLiteral("on") : QStringLiteral("off")) : QString{});
    form->addRow(QStringLiteral("随机移动"), roam);

    for (const auto& language : {QStringLiteral("zh"), QStringLiteral("ja")}) {
        auto* row = new QWidget(&editor);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* voice = new QPushButton(row);
        auto* clearVoice = new QPushButton(QStringLiteral("移除"), row);
        auto* corpus = new QPushButton(row);
        auto* clearCorpus = new QPushButton(QStringLiteral("清空"), row);
        for (auto* button : {voice, clearVoice, corpus, clearCorpus}) {
            layout->addWidget(button);
        }
        const auto prefix = corpusPrefix(language);
        const auto refresh = [&, language, prefix, voice, clearVoice, corpus, clearCorpus] {
            const bool assigned = share.voices.contains(language);
            const auto count = corpusCount(share, prefix);
            voice->setText(assigned ? QStringLiteral("音色已配置 · 更换…") : QStringLiteral("从 voices.json 选音色…"));
            clearVoice->setEnabled(assigned);
            corpus->setText(count ? QStringLiteral("语料 %1 份 · 更换…").arg(count) : QStringLiteral("选语料目录…"));
            clearCorpus->setEnabled(count > 0);
        };
        const auto clear = [&, prefix] {
            auto it = share.files.lowerBound(prefix);
            while (it != share.files.end() && it.key().startsWith(prefix)) {
                share.corpusNames.remove(it.key());
                share.corpusTexts.remove(it.key());
                it = share.files.erase(it);
            }
        };
        refresh();
        form->addRow(languageLabel(language) + QStringLiteral("语音"), row);
        const QString ref = QStringLiteral("voice/ref-%1.wav").arg(language);
        QObject::connect(clearVoice, &QPushButton::clicked, &editor, [&, language, ref, refresh] {
            share.voices.remove(language);
            share.files.remove(ref);
            refresh();
        });
        QObject::connect(clearCorpus, &QPushButton::clicked, &editor, [clear, refresh] { clear(); refresh(); });
        QObject::connect(voice, &QPushButton::clicked, &editor, [&, language, ref, refresh] {
            const auto path = QFileDialog::getOpenFileName(&editor, QStringLiteral("选择本机 Qwen 音色配置"),
                documents().path(), QStringLiteral("音色配置 (*.json)"));
            if (path.isEmpty()) {
                return;
            }
            const auto profiles = QJsonDocument::fromJson(readBounded(path, 128 * 1024)).object();
            const QString lang = language == QStringLiteral("zh") ? QStringLiteral("Chinese") : QStringLiteral("Japanese");
            QStringList names;
            for (auto it = profiles.constBegin(); it != profiles.constEnd(); ++it) {
                const auto entry = it.value().toObject();
                if (entry.value(QStringLiteral("language")).toString(QStringLiteral("Chinese")) == lang) {
                    names.append(it.key());
                }
            }
            if (names.isEmpty()) {
                QMessageBox::warning(&editor, QStringLiteral("找不到音色"), QStringLiteral("此配置没有对应语言的参考音色。"));
                return;
            }
            bool ok = true;
            const auto preferred = share.card.value(QStringLiteral("voice")).toObject().value(language).toString();
            const auto alias = names.size() == 1 ? names.first() : QInputDialog::getItem(&editor, QStringLiteral("选择音色"),
                languageLabel(language), names, qMax(0, names.indexOf(preferred)), false, &ok);
            if (!ok) {
                return;
            }
            auto profile = profiles.value(alias).toObject();
            const auto source = profile.value(QStringLiteral("ref_audio")).toString();
            profile = QJsonObject{{QStringLiteral("ref_audio"), ref},
                                  {QStringLiteral("ref_text"), profile.value(QStringLiteral("ref_text"))}};
            const QString audio = QDir(QFileInfo(path).absolutePath()).filePath(source);
            auto candidate = share;
            candidate.voices.insert(language, profile);
            candidate.files.insert(ref, readBounded(audio, 16 * 1024 * 1024));
            QString problem;
            if (!validateRoleShare(candidate, problem)) {
                QMessageBox::warning(&editor, QStringLiteral("无法关联音色"), problem);
                return;
            }
            share = std::move(candidate);
            refresh();
        });
        QObject::connect(corpus, &QPushButton::clicked, &editor, [&, language, refresh] {
            const auto path = QFileDialog::getExistingDirectory(&editor, QStringLiteral("选择 WAV 语音目录"), documents().path());
            if (path.isEmpty()) {
                return;
            }
            using CorpusLoad = std::tuple<bool, RoleShare, QString>;
            auto loaded = waitForBackground<CorpusLoad>(editor, QStringLiteral("正在读取语音目录…"),
                [draft = share, language, path](QPromise<CorpusLoad>&) mutable {
                    QString problem;
                    const bool ok = replaceRoleCorpusFolder(draft, language, path, problem);
                    return CorpusLoad{ok, std::move(draft), std::move(problem)};
                });
            if (!loaded) {
                return;
            }
            if (!std::get<0>(*loaded)) {
                QMessageBox::warning(&editor, QStringLiteral("无法关联语料"), std::get<2>(*loaded));
                return;
            }
            share = std::move(std::get<1>(*loaded));
            refresh();
        });
    }

    struct Binding { QString key; QString label; QComboBox* action = nullptr; QLineEdit* line = nullptr; };
    QList<Binding> entries;
    for (const auto& definition : companionEvents) {
        entries.append({QString::fromLatin1(definition.key), QString::fromUtf8(definition.label)});
    }
    const auto saved = share.behavior.value(QStringLiteral("events")).toObject();
    for (auto& entry : entries) {
        auto* row = new QWidget(&editor);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        const auto binding = saved.value(entry.key).toObject();
        entry.action = options(row, {{QStringLiteral("默认动作"), {}}, {QStringLiteral("不播动作"), QStringLiteral("none")},
            {QStringLiteral("回到待机"), QStringLiteral("idle")}, {QStringLiteral("开心"), QStringLiteral("happy")},
            {QStringLiteral("困倦"), QStringLiteral("sleepy")}, {QStringLiteral("轻跳"), QStringLiteral("bounce")}},
            binding.value(QStringLiteral("action")).toString());
        entry.line = new QLineEdit(binding.value(QStringLiteral("line")).toString(), row);
        entry.line->setMaxLength(280);
        entry.line->setPlaceholderText(QStringLiteral("可选固定台词"));
        layout->addWidget(entry.action);
        if (entry.key == QStringLiteral("doubleClick")) {
            entry.line->hide();
            layout->addWidget(new QLabel(QStringLiteral("台词在角色卡的「随机发言」中管理"), row), 1);
        } else {
            layout->addWidget(entry.line, 1);
        }
        form->addRow(entry.label, row);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &editor);
    outer->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &editor, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &editor, [&] {
        const auto kibbleValue = kibble->currentData().toString();
        const auto movementValue = roam->currentData().toString();
        share.behavior.remove(QStringLiteral("kibble"));
        share.behavior.remove(QStringLiteral("randomMovement"));
        share.behavior.remove(QStringLiteral("events"));
        if (!kibbleValue.isEmpty()) {
            share.behavior.insert(QStringLiteral("kibble"), kibbleValue);
        }
        if (!movementValue.isEmpty()) {
            share.behavior.insert(QStringLiteral("randomMovement"), movementValue == QStringLiteral("on"));
        }
        QJsonObject bindings;
        for (const auto& entry : entries) {
            QJsonObject binding;
            const auto action = entry.action->currentData().toString();
            const auto line = entry.line->text().trimmed();
            if (!action.isEmpty()) {
                binding.insert(QStringLiteral("action"), action);
            }
            if (!line.isEmpty()) {
                binding.insert(QStringLiteral("line"), line);
            }
            if (!binding.isEmpty()) {
                bindings.insert(entry.key, binding);
            }
        }
        if (!bindings.isEmpty()) {
            share.behavior.insert(QStringLiteral("events"), bindings);
        }
        QString problem;
        if (!library.saveShare(id, share, problem)) {
            QMessageBox::warning(&editor, QStringLiteral("无法保存角色分享设置"), problem);
        } else {
            editor.accept();
        }
    });
    return editor.exec() == QDialog::Accepted;
}
