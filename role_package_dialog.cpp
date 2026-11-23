#include "role_package.h"
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
#include <QJsonArray>
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
bool editCard(QWidget& parent, QJsonObject& card, const QString& roleId, const QString& roleName)
{
    QDialog dialog(&parent);
    dialog.setWindowTitle(QStringLiteral("编辑角色性格与人设"));
    dialog.resize(570, 410);
    auto* form = new QFormLayout(&dialog);
    auto* name = new QLineEdit(card.value(QStringLiteral("displayName")).toString(roleName), &dialog);
    name->setMaxLength(80);
    QStringList lines;
    for (const auto& value : card.value(QStringLiteral("corePrompt")).toArray()) {
        lines.append(value.toString());
    }
    auto* core = new QPlainTextEdit(lines.join(u'\n'), &dialog);
    core->setPlaceholderText(QStringLiteral("写下角色的身份、性格、与用户的关系、语言习惯和表达边界。可以每行写一条。"));
    core->setAccessibleName(QStringLiteral("角色核心提示词"));
    auto* help = new QLabel(QStringLiteral("核心人设会用于每轮 AI 对话。已有的背景资料、对话示例和音色设置会保留。"), &dialog);
    help->setWordWrap(true);
    auto* count = new QLabel(&dialog);
    const auto refreshLength = [&] {
        count->setText(QStringLiteral("%1 / 4000 字符").arg(core->toPlainText().size()));
    };
    QObject::connect(core, &QPlainTextEdit::textChanged, &dialog, refreshLength);
    form->addRow(help);
    form->addRow(QStringLiteral("角色卡名称"), name);
    form->addRow(QStringLiteral("核心人设"), core);
    form->addRow(count);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    refreshLength();
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (name->text().trimmed().isEmpty() || core->toPlainText().trimmed().isEmpty() || core->toPlainText().size() > 4000) {
            QMessageBox::information(&dialog, QStringLiteral("请补全角色卡"),
                                     QStringLiteral("填写角色卡名称，以及不超过 4000 字符的核心人设。"));
            return;
        }
        auto updated = card;
        updated.insert(QStringLiteral("schemaVersion"), 1);
        if (!updated.contains(QStringLiteral("id"))) {
            updated.insert(QStringLiteral("id"), roleId);
        }
        updated.insert(QStringLiteral("displayName"), name->text().trimmed());
        QJsonArray prompt;
        for (const auto& line : core->toPlainText().split(u'\n')) {
            if (!line.trimmed().isEmpty()) {
                prompt.append(line.trimmed());
            }
        }
        updated.insert(QStringLiteral("corePrompt"), prompt);
        card = std::move(updated);
        dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted;
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
    const auto behavior = package.share.behavior;
    if (!behavior.isEmpty()) {
        const auto kibble = behavior.value(QStringLiteral("kibble")).toString();
        if (!kibble.isEmpty()) {
            row(QStringLiteral("默认猫粮"), kibble == QStringLiteral("slow") ? QStringLiteral("慢食猫粮")
                : kibble == QStringLiteral("high") ? QStringLiteral("高能猫粮") : QStringLiteral("无"));
        }
        if (behavior.value(QStringLiteral("randomMovement")).isBool()) {
            row(QStringLiteral("随机移动"), behavior.value(QStringLiteral("randomMovement")).toBool()
                ? QStringLiteral("开启") : QStringLiteral("关闭"));
        }
        const auto events = behavior.value(QStringLiteral("events")).toObject();
        for (const auto& entry : companionEvents) {
            if (events.contains(QLatin1String(entry.key))) {
                const auto event = events.value(QLatin1String(entry.key)).toObject();
                const auto action = event.value(QStringLiteral("action")).toString();
                const QMap<QString, QString> actionNames{
                    {QStringLiteral("none"), QStringLiteral("不播动作")}, {QStringLiteral("idle"), QStringLiteral("回到待机")},
                    {QStringLiteral("happy"), QStringLiteral("开心")}, {QStringLiteral("sleepy"), QStringLiteral("困倦")},
                    {QStringLiteral("bounce"), QStringLiteral("轻跳")},
                };
                QStringList details;
                if (actionNames.contains(action)) {
                    details.append(actionNames.value(action));
                }
                const auto line = event.value(QStringLiteral("line")).toString();
                if (!line.isEmpty()) {
                    details.append(QStringLiteral("「%1」").arg(line));
                }
                if (!details.isEmpty()) {
                    row(QString::fromUtf8(entry.label), details.join(QStringLiteral(" · ")));
                }
            }
        }
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
    const auto package = readRolePackage(path, error);
    if (!package) {
        QMessageBox::warning(&parent, QStringLiteral("无法导入角色包"), error);
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
    cover->setPixmap(package->cover.scaled(148, 148, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    layout->addWidget(cover);
    auto* name = new QLabel(package->name, &preview);
    name->setTextFormat(Qt::PlainText);
    name->setAlignment(Qt::AlignCenter);
    layout->addWidget(name);
    layout->addWidget(importContents(preview, *package), 1);
    if (!package->share.card.isEmpty()) {
        auto* inspect = new QPushButton(QStringLiteral("查看角色提示词…"), &preview);
        QObject::connect(inspect, &QPushButton::clicked, &preview, [&] {
            QStringList prompt;
            for (const auto& line : package->share.card.value(QStringLiteral("corePrompt")).toArray()) {
                prompt.append(line.toString());
            }
            QDialog dialog(&preview);
            dialog.setWindowTitle(QStringLiteral("角色提示词"));
            dialog.resize(490, 320);
            auto* content = new QVBoxLayout(&dialog);
            auto* text = new QPlainTextEdit(prompt.join(u'\n'), &dialog);
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
        package.share = library.shareFor(id, error);
    }
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
    const auto count = corpusCount(package.share);
    if (count) {
        include->setText(QStringLiteral("包含完整语音语料（%1 份）").arg(count));
        layout->addWidget(include);
    } else {
        include->hide();
    }
    const auto showSize = [&] {
        qint64 bytes = package.asset.size();
        for (const auto& action : package.actions) {
            bytes += action.bytes.size();
        }
        for (auto it = package.share.files.cbegin(); it != package.share.files.cend(); ++it) {
            if (include->isChecked() || !it.key().startsWith(QStringLiteral("voice/corpus/"))) {
                bytes += it.value().size();
            }
        }
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
    if (!writeRolePackage(file, package, include->isChecked(), error)) {
        QMessageBox::warning(&parent, QStringLiteral("导出角色失败"), error);
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
    auto* modifyCard = new QPushButton(QStringLiteral("编辑性格与人设…"), &editor);
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
        if (editCard(editor, share.card, id, name)) {
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
        QObject::connect(corpus, &QPushButton::clicked, &editor, [&, prefix, clear, refresh] {
            const auto path = QFileDialog::getExistingDirectory(&editor, QStringLiteral("选择 WAV 语料目录"), documents().path());
            if (path.isEmpty()) {
                return;
            }
            const QDir folder(path);
            const auto wavs = folder.entryInfoList({QStringLiteral("*.wav"), QStringLiteral("*.WAV")},
                QDir::Files | QDir::Readable | QDir::NoSymLinks, QDir::Name);
            if (wavs.isEmpty() || wavs.size() > 200) {
                QMessageBox::warning(&editor, QStringLiteral("无法关联语料"), QStringLiteral("请选择包含 1 到 200 个 WAV 的文件夹。"));
                return;
            }
            auto previous = share.files;
            auto previousNames = share.corpusNames;
            clear();
            qint64 total = 0;
            int index = 1;
            for (const auto& wav : wavs) {
                total += wav.size();
                if (total > 128 * 1024 * 1024 || wav.size() > 16 * 1024 * 1024) {
                    break;
                }
                const auto name = prefix + QStringLiteral("%1.wav").arg(index++, 3, 10, u'0');
                share.files.insert(name, readBounded(wav.absoluteFilePath(), 16 * 1024 * 1024));
                share.corpusNames.insert(name, wav.fileName());
            }
            QString problem;
            if (index - 1 != wavs.size() || !validateRoleShare(share, problem)) {
                share.files = std::move(previous);
                share.corpusNames = std::move(previousNames);
                QMessageBox::warning(&editor, QStringLiteral("无法关联语料"), problem.isEmpty()
                    ? QStringLiteral("每个 WAV 上限为 16 MiB，全部语料上限为 128 MiB。") : problem);
                return;
            }
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
        layout->addWidget(entry.line, 1);
        form->addRow(entry.label, row);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &editor);
    outer->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &editor, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &editor, [&] {
        const auto kibbleValue = kibble->currentData().toString();
        const auto movementValue = roam->currentData().toString();
        share.behavior = {};
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
