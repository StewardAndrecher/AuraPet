#include "voice_training.h"
#include "background_task.h"
#include "role_card_editor.h"
#include "role_library.h"
#include "role_package.h"
#include "random_speech_dialog.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QTextCursor>
#include <QTreeWidget>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <tuple>

namespace {
QString pythonForTraining(const QDir& root)
{
    const auto custom = qEnvironmentVariable("AURAPET_TRAIN_PYTHON");
    const QStringList candidates = {
        custom, root.filePath(QStringLiteral("voice/runtime/python/bin/python3.12")),
        QStringLiteral("/opt/homebrew/bin/python3.12"), QStandardPaths::findExecutable(QStringLiteral("python3.12")),
        QStringLiteral("/opt/homebrew/bin/python3"), QStandardPaths::findExecutable(QStringLiteral("python3")),
    };
    for (const auto& path : candidates) {
        const QFileInfo file(path);
        if (file.isFile() && file.isExecutable()) {
            return file.absoluteFilePath();
        }
    }
    return {};
}

QByteArray readDatasetFile(const QString& root, const QString& relative, qint64 limit)
{
    const auto resolved = QFileInfo(QDir(root).filePath(relative)).canonicalFilePath();
    if (!resolved.startsWith(root + u'/')) {
        return {};
    }
    QFile file(resolved);
    if (!file.open(QIODevice::ReadOnly) || file.size() > limit) {
        return {};
    }
    return file.read(limit + 1);
}

bool importTranscriptDataset(RoleShare& share, const QString& chosen, QString& error)
{
    const auto root = QFileInfo(chosen).canonicalFilePath();
    if (root.isEmpty()) {
        return false;
    }
    bool found = false;
    for (const auto& tag : {QStringLiteral("zh"), QStringLiteral("ja")}) {
        QList<QJsonObject> rows;
        for (const auto& name : {QStringLiteral("train_raw.jsonl"), QStringLiteral("validation_raw.jsonl")}) {
            const auto filename = tag + u'/' + name;
            const auto bytes = readDatasetFile(root, filename, 1024 * 1024);
            for (const auto& line : bytes.split('\n')) {
                if (line.trimmed().isEmpty()) {
                    continue;
                }
                const auto parsed = QJsonDocument::fromJson(line);
                if (!parsed.isObject()) {
                    error = QStringLiteral("语料 JSONL 存在格式错误。");
                    return false;
                }
                rows.append(parsed.object());
            }
        }
        if (rows.isEmpty()) {
            continue;
        }
        if (rows.size() > 198) {
            error = QStringLiteral("此语种的语音条数超过上限。");
            return false;
        }
        found = true;
        const auto prefix = QStringLiteral("voice/corpus/%1/").arg(tag);
        auto it = share.files.lowerBound(prefix);
        while (it != share.files.end() && it.key().startsWith(prefix)) {
            share.corpusNames.remove(it.key());
            share.corpusTexts.remove(it.key());
            it = share.files.erase(it);
        }
        int index = 0;
        qint64 total = 0;
        QMap<QString, QString> transcripts;
        for (const auto& row : rows) {
            const auto source = row.value(QStringLiteral("audio")).toString();
            const auto text = row.value(QStringLiteral("text")).toString().trimmed();
            const auto asset = readDatasetFile(root, source, 16 * 1024 * 1024);
            total += asset.size();
            if (asset.isEmpty() || text.isEmpty() || transcripts.contains(source) || total > 128 * 1024 * 1024) {
                error = QStringLiteral("语料含缺失音频、重复文件、缺失台词或超出容量上限。");
                return false;
            }
            const auto key = prefix + QStringLiteral("%1.wav").arg(++index, 3, 10, u'0');
            share.files.insert(key, asset);
            const auto name = row.value(QStringLiteral("name")).toString(QFileInfo(source).fileName());
            share.corpusNames.insert(key, name.endsWith(QStringLiteral(".wav"), Qt::CaseInsensitive) ? name : key.section(u'/', -1));
            share.corpusTexts.insert(key, text);
            transcripts.insert(source, text);
        }
        const auto reference = rows.first().value(QStringLiteral("ref_audio")).toString();
        if (!share.voices.contains(tag) && transcripts.contains(reference) && transcripts.value(reference).size() <= 800) {
            const auto refPath = QStringLiteral("voice/ref-%1.wav").arg(tag);
            share.files.insert(refPath, readDatasetFile(root, reference, 16 * 1024 * 1024));
            share.voices.insert(tag, QJsonObject{
                {QStringLiteral("ref_audio"), refPath}, {QStringLiteral("ref_text"), transcripts.value(reference)},
            });
        }
    }
    if (!found) {
        error = QStringLiteral("所选目录需要包含 zh 或 ja 子目录，以及 train_raw.jsonl 或 validation_raw.jsonl。");
    }
    return found && validateRoleShare(share, error);
}
}

VoiceTraining::VoiceTraining(const QString& dataPath) : dataPath_(dataPath)
{
    process_.setProcessChannelMode(QProcess::MergedChannels);
    connect(&process_, &QProcess::readyReadStandardOutput, this, &VoiceTraining::readOutput);
    connect(&process_, &QProcess::stateChanged, this, [this] { emit changed(); });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            output_ += QStringLiteral("\n无法启动训练脚本。%1").arg(process_.errorString());
            emit changed();
        }
    });
    connect(&process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus state) {
        readOutput();
        output_ += stopping_ ? QStringLiteral("\n训练已取消。")
            : code == 0 && state == QProcess::NormalExit ? QStringLiteral("\n训练成功，结果已保存。")
                                                         : QStringLiteral("\n训练未完成，请查看上方日志。");
        emit changed();
    });
}

VoiceTraining::~VoiceTraining()
{
    cancel();
    if (process_.state() != QProcess::NotRunning && !process_.waitForFinished(2000)) {
        process_.kill();
        process_.waitForFinished(1000);
    }
}

bool VoiceTraining::running() const
{
    return process_.state() != QProcess::NotRunning;
}

QString VoiceTraining::folderFor(const QString& roleId) const
{
    static const QRegularExpression safe(QStringLiteral("\\A[a-z0-9-]{1,40}\\z"));
    if (dataPath_.isEmpty() || !safe.match(roleId).hasMatch()) {
        return {};
    }
    if (roleId == roleId_ && QFileInfo(folder_).isDir()) {
        return folder_;
    }
    const auto root = QDir(dataPath_).filePath(QStringLiteral("voice-training/jobs/") + roleId);
    const auto jobs = QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name | QDir::Reversed);
    return jobs.isEmpty() ? QString{} : QDir(root).filePath(jobs.first());
}

QString VoiceTraining::logFor(const QString& roleId) const
{
    if (roleId == roleId_) {
        return output_;
    }
    const auto path = folderFor(roleId);
    if (path.isEmpty()) {
        return running() ? QStringLiteral("另一个角色正在训练。") : QString{};
    }
    QFile file(QDir(path).filePath(QStringLiteral("training.log")));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    file.seek(qMax<qint64>(0, file.size() - 48000));
    return QString::fromUtf8(file.readAll());
}

void VoiceTraining::readOutput()
{
    const auto raw = process_.readAllStandardOutput();
    if (!raw.isEmpty()) {
        output_ = (output_ + QString::fromUtf8(raw)).right(48000);
        QFile log(QDir(folder_).filePath(QStringLiteral("training.log")));
        if (log.open(QIODevice::WriteOnly | QIODevice::Append)) {
            log.write(raw);
        }
        emit changed();
    }
}

bool VoiceTraining::start(const QString& roleId, QString& error)
{
    if (running() || dataPath_.isEmpty()) {
        error = QStringLiteral("已有训练任务在运行，或本机数据目录不可用。");
        return false;
    }
    const QDir data(dataPath_);
    const auto python = pythonForTraining(data);
    const auto script = QFileInfo(QDir(QCoreApplication::applicationDirPath())
         .filePath(QStringLiteral("../Resources/voice/train_role.py"))).canonicalFilePath();
    if (python.isEmpty() || script.isEmpty()) {
        error = QStringLiteral("训练需要 Python 3.11 或 3.12。安装语音运行时或 Homebrew Python 后重试。");
        return false;
    }
    const auto key = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))
        + u'-' + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    roleId_ = roleId;
    static const QRegularExpression safeRole(QStringLiteral("\\A[a-z0-9-]{1,40}\\z"));
    if (!safeRole.match(roleId).hasMatch()) {
        error = QStringLiteral("无法识别角色编号。");
        return false;
    }
    folder_ = data.filePath(QStringLiteral("voice-training/jobs/") + roleId + u'/' + key);
    output_ = QStringLiteral("正在检查语料和训练环境…\n");
    stopping_ = false;
    process_.start(python, {QStringLiteral("-u"), script, QStringLiteral("--database"),
        data.filePath(QStringLiteral("roles.sqlite3")), QStringLiteral("--role"), roleId,
        QStringLiteral("--job"), folder_, QStringLiteral("--cache"), data.filePath(QStringLiteral("voice-training/runtime"))});
    return true;
}

void VoiceTraining::cancel()
{
    if (running()) {
        stopping_ = true;
        process_.terminate();
    }
}

void showRoleCard(QWidget& parent, RoleLibrary& library, const QString& activeRoleId, VoiceTraining& training)
{
    QDialog dialog(&parent);
    dialog.setWindowTitle(QStringLiteral("角色卡"));
    dialog.resize(885, 780);
    auto* layout = new QVBoxLayout(&dialog);
    auto* roleRow = new QHBoxLayout;
    auto* rolePicker = new QComboBox(&dialog);
    rolePicker->setObjectName(QStringLiteral("roleCardPicker"));
    QString error;
    const auto entries = library.entries(error);
    if (!error.isEmpty()) {
        QMessageBox::warning(&parent, QStringLiteral("无法读取角色库"), error);
        return;
    }
    for (const auto& entry : entries) {
        rolePicker->addItem(entry.id == activeRoleId ? entry.name + QStringLiteral("（当前桌宠）") : entry.name, entry.id);
    }
    rolePicker->setCurrentIndex(qMax(0, rolePicker->findData(activeRoleId)));
    auto* edit = new QPushButton(QStringLiteral("编辑角色卡…"), &dialog);
    edit->setObjectName(QStringLiteral("editRoleCard"));
    auto* randomSpeech = new QPushButton(QStringLiteral("随机发言…"), &dialog);
    randomSpeech->setObjectName(QStringLiteral("editRandomSpeech"));
    roleRow->addWidget(new QLabel(QStringLiteral("查看角色"), &dialog));
    roleRow->addWidget(rolePicker, 1);
    roleRow->addWidget(edit);
    roleRow->addWidget(randomSpeech);
    layout->addLayout(roleRow);
    auto* split = new QSplitter(Qt::Vertical, &dialog);
    split->setChildrenCollapsible(false);
    auto* card = new QPlainTextEdit(split);
    card->setReadOnly(true);
    card->setMinimumHeight(150);
    card->setObjectName(QStringLiteral("roleCardDetails"));
    card->setAccessibleName(QStringLiteral("完整角色资料"));
    split->addWidget(card);
    auto* corpus = new QWidget(split);
    auto* corpusLayout = new QVBoxLayout(corpus);
    corpusLayout->setContentsMargins(0, 4, 0, 0);
    auto* summary = new QLabel(corpus);
    summary->setWordWrap(true);
    corpusLayout->addWidget(summary);
    auto* recordings = new QTreeWidget(corpus);
    recordings->setObjectName(QStringLiteral("roleCorpusList"));
    recordings->setMinimumHeight(170);
    recordings->setAlternatingRowColors(true);
    recordings->setHeaderLabels({QStringLiteral("语言 / 序号"), QStringLiteral("录音原文件名"), QStringLiteral("对应台词")});
    recordings->header()->resizeSection(0, 130);
    recordings->header()->resizeSection(1, 245);
    corpusLayout->addWidget(recordings, 1);
    auto* hint = new QLabel(QStringLiteral("双击录音可编辑台词。台词齐全后可按语言训练音色。"), corpus);
    hint->setWordWrap(true);
    corpusLayout->addWidget(hint);
    split->addWidget(corpus);
    split->setSizes({285, 385});
    layout->addWidget(split, 1);

    auto* log = new QPlainTextEdit(&dialog);
    log->setReadOnly(true);
    log->setMaximumHeight(130);
    log->hide();
    layout->addWidget(log);
    auto* row = new QHBoxLayout;
    auto* import = new QPushButton(QStringLiteral("导入语音…"), &dialog);
    auto* importOptions = new QMenu(import);
    auto* importChinese = importOptions->addAction(QStringLiteral("导入或替换中文 WAV 文件夹…"));
    auto* importJapanese = importOptions->addAction(QStringLiteral("导入或替换日文 WAV 文件夹…"));
    auto* importDataset = importOptions->addAction(QStringLiteral("导入录音与台词（JSONL）…"));
    import->setMenu(importOptions);
    auto* start = new QPushButton(QStringLiteral("一键训练音色"), &dialog);
    start->setObjectName(QStringLiteral("trainRoleVoice"));
    auto* stop = new QPushButton(QStringLiteral("取消训练"), &dialog);
    auto* reveal = new QPushButton(QStringLiteral("打开训练目录"), &dialog);
    auto* showLog = new QPushButton(QStringLiteral("训练日志"), &dialog);
    showLog->setCheckable(true);
    auto* close = new QPushButton(QStringLiteral("关闭"), &dialog);
    for (auto* button : {import, start, stop, reveal, showLog, close}) {
        row->addWidget(button);
    }
    layout->addLayout(row);
    const auto selectedRole = [&] { return rolePicker->currentData().toString(); };
    int count = 0;
    int missing = 0;
    const auto update = [&] {
        const auto roleId = selectedRole();
        const bool current = training.roleId() == roleId;
        start->setEnabled(count > 0 && !missing && !training.running());
        stop->setEnabled(current && training.running());
        import->setEnabled(edit->isEnabled() && !stop->isEnabled());
        reveal->setEnabled(!training.folderFor(roleId).isEmpty());
        if (showLog->isChecked()) {
            const auto message = training.logFor(roleId);
            if (log->toPlainText() != message) {
                log->setPlainText(message);
                log->moveCursor(QTextCursor::End);
            }
        }
    };
    const auto refresh = [&] {
        const auto roleId = selectedRole();
        error.clear();
        const auto share = library.shareFor(roleId, error, false);
        const auto paths = error.isEmpty() ? library.corpusPaths(roleId, error) : QStringList{};
        recordings->clear();
        QStringList behavior;
        for (const auto& [label, description] : describeRoleBehavior(share.behavior)) {
            behavior.append(label + QStringLiteral("  ") + description);
        }
        auto details = roleCardText(share.card);
        if (!behavior.isEmpty()) {
            details += QStringLiteral("\n\n行为预设\n") + behavior.join(u'\n');
        }
        card->setPlainText(error.isEmpty() ? details : error);
        edit->setEnabled(error.isEmpty() && !roleId.isEmpty());
        randomSpeech->setEnabled(edit->isEnabled());
        count = paths.size();
        missing = 0;
        QStringList counts;
        for (const auto& [tag, language] : {qMakePair(QStringLiteral("zh"), QStringLiteral("中文")),
                                           qMakePair(QStringLiteral("ja"), QStringLiteral("日文"))}) {
            const auto prefix = QStringLiteral("voice/corpus/%1/").arg(tag);
            const auto reference = share.voices.value(tag).toObject();
            auto* group = new QTreeWidgetItem(recordings, {language});
            int index = 0;
            for (const auto& key : paths) {
                if (!key.startsWith(prefix)) {
                    continue;
                }
                const auto text = share.corpusTexts.value(key).toString();
                const auto fileName = share.corpusNames.value(key).toString(key.section(u'/', -1));
                missing += text.trimmed().isEmpty();
                auto* item = new QTreeWidgetItem(group, {QString::number(++index), fileName,
                    text.isEmpty() ? QStringLiteral("待填写") : text.simplified()});
                item->setData(0, Qt::UserRole, key);
                item->setToolTip(1, fileName);
                item->setToolTip(2, text);
            }
            if (!reference.isEmpty()) {
                const auto text = reference.value(QStringLiteral("ref_text")).toString();
                auto* ref = new QTreeWidgetItem(group, {QString{}, QStringLiteral("当前参考音色"), text.simplified()});
                ref->setToolTip(2, text);
            }
            if (group->childCount() == 0) {
                delete group;
            } else {
                group->setText(1, QStringLiteral("%1 段训练录音").arg(index));
                group->setExpanded(true);
            }
            counts.append(QStringLiteral("%1 %2 段").arg(language).arg(index));
        }
        summary->setText(count > 0
            ? QStringLiteral("角色语音　%1　·　%2 段待填写台词").arg(counts.join(QStringLiteral("　·　"))).arg(missing)
            : QStringLiteral("角色语音　此角色还没有训练录音，可导入本地 WAV 文件夹，或在上方选择其他角色。"));
        update();
    };
    QObject::connect(&training, &VoiceTraining::changed, &dialog, update);
    QObject::connect(randomSpeech, &QPushButton::clicked, &dialog, [&] {
        editRandomSpeech(dialog, library, selectedRole());
    });
    QObject::connect(rolePicker, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(showLog, &QPushButton::toggled, &dialog, [&](bool visible) {
        log->setVisible(visible);
        update();
    });
    QObject::connect(edit, &QPushButton::clicked, &dialog, [&] {
        const auto roleId = selectedRole();
        QString failure;
        auto cardData = library.shareFor(roleId, failure, false).card;
        const auto name = entries.value(rolePicker->currentIndex()).name;
        if (failure.isEmpty() && !editRoleCard(dialog, cardData, roleId, name)) {
            return;
        }
        if (failure.isEmpty() && library.saveRoleCard(roleId, cardData, failure)) {
            refresh();
        } else {
            QMessageBox::warning(&dialog, QStringLiteral("无法保存角色卡"), failure);
        }
    });
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(stop, &QPushButton::clicked, &dialog, [&] { training.cancel(); });
    QObject::connect(reveal, &QPushButton::clicked, &dialog, [&] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(training.folderFor(selectedRole())));
    });
    QObject::connect(start, &QPushButton::clicked, &dialog, [&] {
        QString failure;
        if (training.start(selectedRole(), failure)) {
            showLog->setChecked(true);
        } else {
            QMessageBox::warning(&dialog, QStringLiteral("无法训练"), failure);
        }
    });
    const auto importFrom = [&](const QString& tag, const QString& title) {
        const auto roleId = selectedRole();
        const auto path = QFileDialog::getExistingDirectory(&dialog, title);
        if (path.isEmpty()) {
            return;
        }
        QString failure;
        auto initialShare = library.shareFor(roleId, failure);
        if (!failure.isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("无法导入语料"), failure);
            return;
        }
        using DatasetLoad = std::tuple<bool, RoleShare, QString>;
        auto loaded = waitForBackground<DatasetLoad>(dialog, QStringLiteral("正在读取语音语料…"),
            [draft = std::move(initialShare), tag, path](QPromise<DatasetLoad>&) mutable {
                QString err;
                const bool ok = tag.isEmpty() ? importTranscriptDataset(draft, path, err)
                                              : replaceRoleCorpusFolder(draft, tag, path, err);
                return DatasetLoad{ok, std::move(draft), std::move(err)};
            });
        if (!loaded) {
            return;
        }
        if (std::get<0>(*loaded) && library.saveShare(roleId, std::get<1>(*loaded), failure)) {
            refresh();
        } else {
            QMessageBox::warning(&dialog, QStringLiteral("无法导入语料"),
                                 std::get<0>(*loaded) ? failure : std::get<2>(*loaded));
        }
    };
    QObject::connect(importChinese, &QAction::triggered, &dialog, [&] {
        importFrom(QStringLiteral("zh"), QStringLiteral("选择中文 WAV 文件夹"));
    });
    QObject::connect(importJapanese, &QAction::triggered, &dialog, [&] {
        importFrom(QStringLiteral("ja"), QStringLiteral("选择日文 WAV 文件夹"));
    });
    QObject::connect(importDataset, &QAction::triggered, &dialog, [&] {
        importFrom({}, QStringLiteral("选择含中日文 JSONL 的语料目录"));
    });
    QObject::connect(recordings, &QTreeWidget::itemDoubleClicked, &dialog, [&](QTreeWidgetItem* item) {
        const auto roleId = selectedRole();
        const auto key = item->data(0, Qt::UserRole).toString();
        if (key.isEmpty() || (training.running() && training.roleId() == roleId)) {
            return;
        }
        QString failure;
        auto share = library.shareFor(roleId, failure);
        if (!failure.isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("无法读取语料"), failure);
            return;
        }
        bool confirmed = false;
        const auto text = QInputDialog::getMultiLineText(&dialog, QStringLiteral("编辑录音台词"), item->text(1),
            share.corpusTexts.value(key).toString(), &confirmed).trimmed();
        if (!confirmed) {
            return;
        }
        if (text.isEmpty()) {
            share.corpusTexts.remove(key);
        } else {
            share.corpusTexts.insert(key, text);
        }
        if (library.saveShare(roleId, share, failure)) {
            refresh();
        } else {
            QMessageBox::warning(&dialog, QStringLiteral("无法保存台词"), failure);
        }
    });
    refresh();
    dialog.exec();
}
