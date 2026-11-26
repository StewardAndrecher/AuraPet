#include "role_package.h"
#include "archive_sdk.h"
#include "companion_events.h"
#include "pet_asset.h"
#include "random_speech.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <QVersionNumber>

#include <algorithm>
#include <array>
#include <memory>
#include <utility>

namespace {
using Files = QMap<QString, QByteArray>;
constexpr qint64 maxPackageBytes = 256 * 1024 * 1024;
constexpr qint64 maxJsonBytes = 128 * 1024;
constexpr qint64 maxVoiceBytes = 16 * 1024 * 1024;
constexpr int maxEntries = 220;

const QString manifestPath = QStringLiteral("manifest.json");
const QString cardPath = QStringLiteral("role/card.json");
const QString behaviorPath = QStringLiteral("behavior.json");
const QString voicesPath = QStringLiteral("voice/voices.json");
const QString corpusPath = QStringLiteral("voice/corpus.json");
const QString transcriptsPath = QStringLiteral("voice/transcripts.json");

QString archiveError(archive* zip)
{
    return QString::fromUtf8(archive_error_string(zip));
}

bool allowedPath(const QString& path)
{
    static const QRegularExpression pattern(QStringLiteral(
        R"(\A(?:manifest\.json|role/card\.json|behavior\.json|voice/(?:voices|corpus|transcripts)\.json|assets/(?:default|idle|happy|sleepy)\.(?:gif|png|jpg|avif)|voice/(?:ref-(?:zh|ja)|corpus/(?:zh|ja)/[0-9]{3})\.wav)\z)"));
    return pattern.match(path).hasMatch();
}

qint64 fileLimit(const QString& path)
{
    if (path.endsWith(QStringLiteral(".json"))) {
        return maxJsonBytes;
    }
    return path.startsWith(QStringLiteral("voice/")) ? maxVoiceBytes : maxPetAssetBytes;
}

bool report(const RolePackageProgress& progress, int value, QString& error)
{
    if (!progress || progress(value)) {
        return true;
    }
    error = QStringLiteral("操作已取消。");
    return false;
}

std::optional<Files> readZip(const QString& path, QString& error, const RolePackageProgress& progress)
{
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly) || input.size() > maxPackageBytes || input.isSequential()) {
        error = QStringLiteral("角色包无法打开，或大小超过 256 MiB。");
        return std::nullopt;
    }
    const std::unique_ptr<archive, decltype(&archive_read_free)> zip(archive_read_new(), archive_read_free);
    if (!zip || archive_read_support_format_zip_seekable(zip.get()) != ARCHIVE_OK
        || archive_read_open_fd(zip.get(), input.handle(), 64 * 1024) != ARCHIVE_OK) {
        error = QStringLiteral("文件不是有效的 AuraPet ZIP 角色包。");
        return std::nullopt;
    }
    Files files;
    qint64 total = 0;
    archive_entry* entry = nullptr;
    int status;
    while ((status = archive_read_next_header(zip.get(), &entry)) == ARCHIVE_OK) {
        const QString key = QString::fromUtf8(archive_entry_pathname(entry));
        const auto size = archive_entry_size(entry);
        if (!allowedPath(key) || archive_entry_filetype(entry) != AE_IFREG || archive_entry_hardlink(entry)
            || files.contains(key) || !archive_entry_size_is_set(entry) || size <= 0 || size > fileLimit(key)
            || files.size() >= maxEntries || total + size > maxPackageBytes) {
            error = QStringLiteral("角色包包含重复、超限或不支持的文件。");
            return std::nullopt;
        }
        QByteArray bytes(size, Qt::Uninitialized);
        qsizetype received = 0;
        while (received < size) {
            if (!report(progress, static_cast<int>((total + received) * 65 / maxPackageBytes), error)) {
                return std::nullopt;
            }
            const auto count = archive_read_data(zip.get(), bytes.data() + received, qMin<qint64>(size - received, 1024 * 1024));
            if (count <= 0) {
                error = QStringLiteral("角色包内的素材损坏或读取不完整。");
                return std::nullopt;
            }
            received += count;
        }
        char extra;
        if (archive_read_data(zip.get(), &extra, 1) != 0) {
            error = QStringLiteral("角色包内的素材长度或校验不一致。");
            return std::nullopt;
        }
        total += size;
        files.insert(key, std::move(bytes));
    }
    if (status != ARCHIVE_EOF) {
        error = QStringLiteral("角色包读取失败。") + archiveError(zip.get());
        return std::nullopt;
    }
    return files;
}

bool writeZip(const QString& path, const Files& files, QString& error, const RolePackageProgress& progress)
{
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        error = output.errorString();
        return false;
    }
    const std::unique_ptr<archive, decltype(&archive_write_free)> zip(archive_write_new(), archive_write_free);
    if (!zip || archive_write_set_format_zip(zip.get()) != ARCHIVE_OK
        || archive_write_open_fd(zip.get(), output.handle()) != ARCHIVE_OK) {
        error = QStringLiteral("无法创建 ZIP 角色包。");
        return false;
    }
    const std::unique_ptr<archive_entry, decltype(&archive_entry_free)> entry(archive_entry_new(), archive_entry_free);
    if (!entry) {
        error = QStringLiteral("无法创建 ZIP 文件记录。");
        return false;
    }
    int finished = 0;
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        const int value = 35 + 63 * finished / files.size();
        if (!report(progress, value, error)) {
            return false;
        }
        archive_entry_set_pathname(entry.get(), it.key().toLatin1().constData());
        archive_entry_set_filetype(entry.get(), AE_IFREG);
        archive_entry_set_perm(entry.get(), 0644);
        archive_entry_set_size(entry.get(), it.value().size());
        if (archive_write_header(zip.get(), entry.get()) != ARCHIVE_OK) {
            error = QStringLiteral("写入角色包失败。") + archiveError(zip.get());
            return false;
        }
        for (qsizetype bytesWritten = 0; bytesWritten < it.value().size();) {
            if (!report(progress, value, error)) {
                return false;
            }
            const auto chunk = qMin<qsizetype>(it.value().size() - bytesWritten, 1024 * 1024);
            const auto count = archive_write_data(zip.get(), it.value().constData() + bytesWritten, chunk);
            if (count != chunk) {
                error = QStringLiteral("写入角色包失败。") + archiveError(zip.get());
                return false;
            }
            bytesWritten += count;
        }
        ++finished;
    }
    if (archive_write_close(zip.get()) != ARCHIVE_OK) {
        error = QStringLiteral("无法完成角色包。") + archiveError(zip.get());
        return false;
    }
    if (!report(progress, 99, error)) {
        return false;
    }
    if (!output.commit()) {
        error = output.errorString();
        return false;
    }
    return true;
}

bool validId(const QString& id)
{
    const QUuid uuid(id);
    return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == id;
}

QString slotKey(ActionSlot slot)
{
    switch (slot) {
    case ActionSlot::Idle: return QStringLiteral("idle");
    case ActionSlot::Happy: return QStringLiteral("happy");
    case ActionSlot::Sleepy: return QStringLiteral("sleepy");
    }
    return {};
}

bool assetPath(const QString& path, const QString& key)
{
    return allowedPath(path) && path.startsWith(QStringLiteral("assets/") + key + u'.');
}

QString assetFilename(const QString& key, const QByteArray& bytes)
{
    QString ext = QStringLiteral("avif");
    if (bytes.startsWith("GIF8")) {
        ext = QStringLiteral("gif");
    } else if (bytes.startsWith("\x89PNG\r\n\x1a\n")) {
        ext = QStringLiteral("png");
    } else if (bytes.startsWith("\xff\xd8")) {
        ext = QStringLiteral("jpg");
    }
    return QStringLiteral("assets/%1.%2").arg(key, ext);
}

bool matches(const QJsonObject& object, const QString& text)
{
    for (const auto& tag : object.value(QStringLiteral("tags")).toArray()) {
        const QString term = tag.toString().trimmed();
        if (!term.isEmpty() && text.contains(term, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

bool supportedChoice(const QJsonValue& value, const QStringList& choices)
{
    return value.isUndefined() || (value.isString() && choices.contains(value.toString()));
}

bool validRecipe(const QJsonObject& recipe)
{
    const int kind = recipe.value(QStringLiteral("kind")).toInt(-1);
    const int intensity = recipe.value(QStringLiteral("intensity")).toInt(0);
    const int duration = recipe.value(QStringLiteral("durationMs")).toInt(0);
    return recipe.isEmpty() || (kind >= 0 && kind <= 2 && intensity >= 1 && intensity <= 100
                                && duration >= 600 && duration <= 4000);
}

bool validWav(const QByteArray& bytes)
{
    return bytes.size() > 44 && bytes.size() <= maxVoiceBytes && bytes.startsWith("RIFF") && bytes.mid(8, 4) == "WAVE";
}
}

bool validateRoleCard(const QJsonObject& card, QString& error)
{
    if (card.isEmpty()) {
        return true;
    }
    if (QJsonDocument(card).toJson(QJsonDocument::Compact).size() > maxJsonBytes) {
        error = QStringLiteral("角色卡超过 128 KiB。");
        return false;
    }
    const auto prompt = card.value(QStringLiteral("corePrompt"));
    const auto core = prompt.toArray();
    qsizetype characters = 0;
    for (const auto& line : core) {
        if (!line.isString() || line.toString().trimmed().isEmpty()) {
            error = QStringLiteral("AI 聊天角色设定应由非空文本组成。");
            return false;
        }
        characters += line.toString().size();
    }
    const auto identity = card.value(QStringLiteral("identityAndStance"));
    if (card.value(QStringLiteral("schemaVersion")).toInt() != 1 || characters > 4000
        || (!prompt.isUndefined() && !prompt.isArray())
        || (!identity.isUndefined() && (!identity.isString() || identity.toString().size() > 4000))) {
        error = QStringLiteral("角色卡需使用版本 1。身份与立场、AI 聊天角色设定各最多 4000 字符。");
        return false;
    }
    return true;
}

bool validateRoleShare(const RoleShare& share, QString& error)
{
    if (!validateRoleCard(share.card, error)) {
        return false;
    }
    for (const auto& object : {share.behavior, share.voices, share.corpusNames, share.corpusTexts}) {
        if (QJsonDocument(object).toJson(QJsonDocument::Compact).size() > maxJsonBytes) {
            error = QStringLiteral("行为预设或语音清单超过 128 KiB。");
            return false;
        }
    }
    const auto behavior = share.behavior;
    const auto randomSpeech = behavior.value(QStringLiteral("randomSpeech"));
    if (!randomSpeech.isUndefined() && !validateRandomSpeech(randomSpeech.toObject(), error)) {
        return false;
    }
    if (!supportedChoice(behavior.value(QStringLiteral("kibble")), {QStringLiteral("none"), QStringLiteral("slow"), QStringLiteral("high")})
        || (behavior.contains(QStringLiteral("randomMovement")) && !behavior.value(QStringLiteral("randomMovement")).isBool())
        || (behavior.contains(QStringLiteral("events")) && !behavior.value(QStringLiteral("events")).isObject())) {
        error = QStringLiteral("行为预设中的猫粮、随机移动或事件配置无效。");
        return false;
    }
    const auto bindings = behavior.value(QStringLiteral("events")).toObject();
    for (auto it = bindings.constBegin(); it != bindings.constEnd(); ++it) {
        const auto binding = it.value().toObject();
        const auto line = binding.value(QStringLiteral("line"));
        if (eventLabel(it.key()).isEmpty() || !it.value().isObject()
            || !supportedChoice(binding.value(QStringLiteral("action")), {QStringLiteral("none"), QStringLiteral("idle"),
                 QStringLiteral("happy"), QStringLiteral("sleepy"), QStringLiteral("bounce")})
            || (!line.isUndefined() && (!line.isString() || line.toString().size() > 280))) {
            error = QStringLiteral("行为预设包含不支持的动作或过长的台词。");
            return false;
        }
    }

    QSet<QString> referenceFiles;
    for (auto it = share.voices.constBegin(); it != share.voices.constEnd(); ++it) {
        const QString tag = it.key();
        const auto profile = it.value().toObject();
        const QString path = QStringLiteral("voice/ref-%1.wav").arg(tag);
        const QString transcript = profile.value(QStringLiteral("ref_text")).toString().trimmed();
        if ((tag != QStringLiteral("zh") && tag != QStringLiteral("ja"))
            || profile.value(QStringLiteral("ref_audio")).toString() != path
            || transcript.isEmpty() || transcript.size() > 800 || !validWav(share.files.value(path))) {
            error = QStringLiteral("音色需要对应语言的参考 WAV 和准确转写。");
            return false;
        }
        referenceFiles.insert(path);
    }
    for (auto it = share.corpusNames.constBegin(); it != share.corpusNames.constEnd(); ++it) {
        const auto name = it.value().toString();
        if (!it.key().startsWith(QStringLiteral("voice/corpus/")) || !share.files.contains(it.key())
            || name.isEmpty() || name.size() > 240 || name.contains(u'/') || name.contains(u'\\')
            || !name.endsWith(QStringLiteral(".wav"), Qt::CaseInsensitive)) {
            error = QStringLiteral("语音语料的原文件名记录无效。");
            return false;
        }
    }
    for (auto it = share.corpusTexts.constBegin(); it != share.corpusTexts.constEnd(); ++it) {
        const auto text = it.value().toString().trimmed();
        if (!it.key().startsWith(QStringLiteral("voice/corpus/")) || !share.files.contains(it.key())
            || text.isEmpty() || text.size() > 4000) {
            error = QStringLiteral("语音台词需要对应包内的录音，且每条最多 4000 字符。");
            return false;
        }
    }
    qint64 total = 0;
    for (auto it = share.files.cbegin(); it != share.files.cend(); ++it) {
        if (!allowedPath(it.key())
            || (!referenceFiles.contains(it.key()) && !it.key().startsWith(QStringLiteral("voice/corpus/")))
            || !validWav(it.value())) {
            error = QStringLiteral("语音资料包含无法识别的 WAV。");
            return false;
        }
        total += it.value().size();
    }
    if (total > 128 * 1024 * 1024 || share.files.size() > 200) {
        error = QStringLiteral("语音文件合计上限为 128 MiB 和 200 个文件。");
        return false;
    }
    return true;
}

bool replaceRoleCorpusFolder(RoleShare& share, const QString& language, const QString& path, QString& error)
{
    const QDir folder(path);
    const auto wavs = folder.entryInfoList({QStringLiteral("*.wav")}, QDir::Files | QDir::Readable | QDir::NoSymLinks,
                                           QDir::Name | QDir::IgnoreCase);
    if ((language != QStringLiteral("zh") && language != QStringLiteral("ja")) || wavs.isEmpty() || wavs.size() > 200) {
        error = QStringLiteral("请选择包含 1 到 200 段 WAV 的语音文件夹。");
        return false;
    }
    auto candidate = share;
    const auto prefix = QStringLiteral("voice/corpus/%1/").arg(language);
    QMap<QString, QString> previousPaths;
    auto it = candidate.files.lowerBound(prefix);
    while (it != candidate.files.end() && it.key().startsWith(prefix)) {
        previousPaths.insert(candidate.corpusNames.value(it.key()).toString(), it.key());
        candidate.corpusNames.remove(it.key());
        candidate.corpusTexts.remove(it.key());
        it = candidate.files.erase(it);
    }
    qint64 total = 0;
    for (const auto& audio : candidate.files) {
        total += audio.size();
    }
    int index = 0;
    for (const auto& wav : wavs) {
        QFile file(wav.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly) || file.size() > maxVoiceBytes) {
            error = QStringLiteral("无法读取「%1」，或文件超过 16 MiB。").arg(wav.fileName());
            return false;
        }
        const auto audio = file.read(maxVoiceBytes + 1);
        total += audio.size();
        if (!validWav(audio) || total > 128 * 1024 * 1024) {
            error = QStringLiteral("「%1」不是有效的 WAV，或语音合计超过 128 MiB。").arg(wav.fileName());
            return false;
        }
        const auto key = prefix + QStringLiteral("%1.wav").arg(++index, 3, 10, u'0');
        const auto oldKey = previousPaths.value(wav.fileName());
        if (share.corpusTexts.contains(oldKey) && audio == share.files.value(oldKey)) {
            candidate.corpusTexts.insert(key, share.corpusTexts.value(oldKey));
        }
        candidate.files.insert(key, audio);
        candidate.corpusNames.insert(key, wav.fileName());
    }
    if (!validateRoleShare(candidate, error)) {
        return false;
    }
    share = std::move(candidate);
    return true;
}

QList<QPair<QString, QString>> describeRoleBehavior(const QJsonObject& behavior)
{
    QList<QPair<QString, QString>> rows;
    const auto kibble = behavior.value(QStringLiteral("kibble")).toString();
    if (!kibble.isEmpty()) {
        rows.append({QStringLiteral("默认猫粮"), kibble == QStringLiteral("slow") ? QStringLiteral("慢食猫粮")
            : kibble == QStringLiteral("high") ? QStringLiteral("高能猫粮") : QStringLiteral("无")});
    }
    const auto movement = behavior.value(QStringLiteral("randomMovement"));
    if (movement.isBool()) {
        rows.append({QStringLiteral("随机移动"), movement.toBool() ? QStringLiteral("开启") : QStringLiteral("关闭")});
    }
    const auto events = behavior.value(QStringLiteral("events")).toObject();
    const QMap<QString, QString> actionNames{
        {QStringLiteral("none"), QStringLiteral("不播动作")}, {QStringLiteral("idle"), QStringLiteral("回到待机")},
        {QStringLiteral("happy"), QStringLiteral("开心")}, {QStringLiteral("sleepy"), QStringLiteral("困倦")},
        {QStringLiteral("bounce"), QStringLiteral("轻跳")},
    };
    for (const auto& entry : companionEvents) {
        const auto event = events.value(QLatin1String(entry.key)).toObject();
        const auto action = actionNames.value(event.value(QStringLiteral("action")).toString());
        const auto line = event.value(QStringLiteral("line")).toString();
        QStringList details;
        if (!action.isEmpty()) {
            details.append(action);
        }
        if (!line.isEmpty() && entry.event != CompanionEvent::DoubleClick) {
            details.append(QStringLiteral("「%1」").arg(line));
        }
        if (!details.isEmpty()) {
            rows.append({QString::fromUtf8(entry.label), details.join(QStringLiteral(" · "))});
        }
    }
    return rows;
}

std::optional<RolePackage> readRolePackage(const QString& path, QString& error, const RolePackageProgress& progress)
{
    auto files = readZip(path, error, progress);
    if (!files) {
        return std::nullopt;
    }
    const auto manifest = QJsonDocument::fromJson(files->take(manifestPath)).object();
    const auto id = manifest.value(QStringLiteral("id")).toString();
    const auto name = manifest.value(QStringLiteral("name")).toString().trimmed();
    const auto declared = manifest.value(QStringLiteral("files")).toObject();
    if (manifest.value(QStringLiteral("format")).toString() != QStringLiteral("gif-pet")
        || manifest.value(QStringLiteral("schemaVersion")).toInt() != 1) {
        error = QStringLiteral("无法识别此角色包的格式版本。");
        return std::nullopt;
    }
    const auto minimum = manifest.value(QStringLiteral("minimumAppVersion"));
    if (!minimum.isUndefined()) {
        const auto required = minimum.toString();
        static const QRegularExpression versionSyntax(QStringLiteral(R"(\A[0-9]{1,6}\.[0-9]{1,6}\.[0-9]{1,6}\z)"));
        if (!versionSyntax.match(required).hasMatch()) {
            error = QStringLiteral("角色包的最低应用版本格式无效。");
            return std::nullopt;
        }
        if (QVersionNumber::fromString(required) > QVersionNumber::fromString(QStringLiteral(AURAPET_VERSION))) {
            error = QStringLiteral("此角色包需要 AuraPet %1 或更新版本。").arg(required);
            return std::nullopt;
        }
    }
    if (!validId(id) || name.isEmpty() || name.size() > 80 || declared.size() != files->size()) {
        error = QStringLiteral("角色包信息或素材清单不完整。");
        return std::nullopt;
    }
    int checked = 0;
    for (auto it = files->cbegin(); it != files->cend(); ++it) {
        if (!report(progress, 65 + 20 * checked++ / files->size(), error)) {
            return std::nullopt;
        }
        const auto hash = QCryptographicHash::hash(it.value(), QCryptographicHash::Sha256).toHex();
        if (declared.value(it.key()).toString().toLatin1() != hash) {
            error = QStringLiteral("素材清单中的校验值不一致。");
            return std::nullopt;
        }
    }
    RolePackage package;
    package.id = id;
    package.name = name;
    const auto basePath = manifest.value(QStringLiteral("asset")).toString();
    if (!assetPath(basePath, QStringLiteral("default"))) {
        error = QStringLiteral("角色包缺少原始待机素材。");
        return std::nullopt;
    }
    package.asset = files->take(basePath);
    if (!report(progress, 86, error)) {
        return std::nullopt;
    }
    const auto asset = decodePetPixels(package.asset, error);
    if (!asset) {
        return std::nullopt;
    }
    package.cover = asset->firstFrame;
    const auto actions = manifest.value(QStringLiteral("actions")).toObject();
    for (const auto slot : {ActionSlot::Idle, ActionSlot::Happy, ActionSlot::Sleepy}) {
        if (!actions.contains(slotKey(slot))) {
            continue;
        }
        const auto info = actions.value(slotKey(slot)).toObject();
        const auto actionPath = info.value(QStringLiteral("path")).toString();
        const auto actionName = info.value(QStringLiteral("name")).toString();
        const auto recipe = info.value(QStringLiteral("recipe"));
        if (!assetPath(actionPath, slotKey(slot)) || actionName.size() > 160
            || (!recipe.isUndefined() && (!recipe.isObject() || !validRecipe(recipe.toObject())))) {
            error = QStringLiteral("角色包中的动作配置无效。");
            return std::nullopt;
        }
        auto bytes = files->take(actionPath);
        if (!report(progress, 90 + static_cast<int>(slot), error)) {
            return std::nullopt;
        }
        const auto action = decodePetPixels(bytes, error);
        if (!action) {
            return std::nullopt;
        }
        if (slot == ActionSlot::Idle) {
            package.cover = action->firstFrame;
        }
        package.actions.insert(slot, {actionName, std::move(bytes), recipe.toObject()});
    }
    struct Section { QString path; QJsonObject RoleShare::* field; };
    for (const auto& section : {Section{cardPath, &RoleShare::card}, Section{behaviorPath, &RoleShare::behavior},
                                Section{voicesPath, &RoleShare::voices}, Section{corpusPath, &RoleShare::corpusNames},
                                Section{transcriptsPath, &RoleShare::corpusTexts}}) {
        if (files->contains(section.path)) {
            const auto doc = QJsonDocument::fromJson(files->take(section.path));
            if (!doc.isObject()) {
                error = QStringLiteral("角色包中的配置不是有效的 JSON 对象。");
                return std::nullopt;
            }
            package.share.*(section.field) = doc.object();
        }
    }
    package.share.files = std::move(*files);
    if (!report(progress, 98, error) || !validateRoleShare(package.share, error)) {
        return std::nullopt;
    }
    return package;
}

bool writeRolePackage(const QString& path, const RolePackage& package, bool includeCorpus, QString& error,
                      const RolePackageProgress& progress)
{
    if (!report(progress, 0, error) || !validId(package.id) || package.name.trimmed().isEmpty() || package.name.size() > 80
        || !decodePetPixels(package.asset, error) || !validateRoleShare(package.share, error)) {
        if (error.isEmpty()) {
            error = QStringLiteral("角色信息或素材不完整。");
        }
        return false;
    }
    Files files;
    const auto basePath = assetFilename(QStringLiteral("default"), package.asset);
    files.insert(basePath, package.asset);
    QJsonObject actions;
    for (auto it = package.actions.cbegin(); it != package.actions.cend(); ++it) {
        if (!report(progress, 5 + static_cast<int>(it.key()), error) || !decodePetPixels(it->bytes, error)) {
            return false;
        }
        const auto key = slotKey(it.key());
        const auto path = assetFilename(key, it->bytes);
        files.insert(path, it->bytes);
        QJsonObject description{{QStringLiteral("name"), it->name.left(160)}, {QStringLiteral("path"), path}};
        if (!it->recipe.isEmpty()) {
            if (!validRecipe(it->recipe)) {
                error = QStringLiteral("动作模板的参数无效。");
                return false;
            }
            description.insert(QStringLiteral("recipe"), it->recipe);
        }
        actions.insert(key, description);
    }
    const auto addJson = [&](const QString& name, const QJsonObject& object) {
        if (!object.isEmpty()) {
            files.insert(name, QJsonDocument(object).toJson(QJsonDocument::Compact));
        }
    };
    addJson(cardPath, package.share.card);
    auto behavior = package.share.behavior;
    if (!includeCorpus && behavior.contains(QStringLiteral("randomSpeech"))) {
        auto config = randomSpeechConfig(behavior);
        config.recordings = {};
        behavior.insert(QStringLiteral("randomSpeech"), encodeRandomSpeech(config));
    }
    addJson(behaviorPath, behavior);
    addJson(voicesPath, package.share.voices);
    if (includeCorpus) {
        addJson(corpusPath, package.share.corpusNames);
        addJson(transcriptsPath, package.share.corpusTexts);
    }
    for (auto it = package.share.files.cbegin(); it != package.share.files.cend(); ++it) {
        if (includeCorpus || !it.key().startsWith(QStringLiteral("voice/corpus/"))) {
            files.insert(it.key(), it.value());
        }
    }
    QJsonObject list;
    qint64 total = 0;
    int hashed = 0;
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        if (!report(progress, 10 + 25 * hashed++ / files.size(), error)) {
            return false;
        }
        list.insert(it.key(), QString::fromLatin1(QCryptographicHash::hash(it.value(), QCryptographicHash::Sha256).toHex()));
        total += it.value().size();
    }
    const bool extendedCard = !package.share.card.isEmpty()
        && (package.share.card.contains(QStringLiteral("identityAndStance"))
            || package.share.card.value(QStringLiteral("corePrompt")).toArray().isEmpty());
    const bool withTranscripts = includeCorpus && !package.share.corpusTexts.isEmpty();
    const auto speech = behavior.value(QStringLiteral("randomSpeech")).toObject();
    const auto minimumVersion = speech.contains(QStringLiteral("withAi")) ? QStringLiteral("1.0.32")
        : behavior.contains(QStringLiteral("randomSpeech")) ? QStringLiteral("1.0.31")
        : extendedCard ? QStringLiteral("1.0.29")
        : withTranscripts ? QStringLiteral("1.0.28") : QStringLiteral("1.0.27");
    files.insert(manifestPath, QJsonDocument(QJsonObject{
        {QStringLiteral("format"), QStringLiteral("gif-pet")}, {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("minimumAppVersion"), minimumVersion}, {QStringLiteral("id"), package.id},
        {QStringLiteral("name"), package.name}, {QStringLiteral("asset"), basePath}, {QStringLiteral("actions"), actions},
        {QStringLiteral("files"), list},
    }).toJson(QJsonDocument::Indented));
    if (total + files.value(manifestPath).size() > maxPackageBytes - 1024 * 1024) {
        error = QStringLiteral("角色包大小将超过 256 MiB。");
        return false;
    }
    return writeZip(path, files, error, progress);
}

QStringList rolePromptContext(const QJsonObject& card, const QString& text)
{
    QStringList context;
    for (const auto& value : card.value(QStringLiteral("contextBlocks")).toArray()) {
        const auto block = value.toObject();
        if (matches(block, text)) {
            const auto detail = block.value(QStringLiteral("text")).toString().left(650);
            if (!detail.isEmpty()) {
                context.append(detail);
                break;
            }
        }
    }
    QJsonObject selectedExample;
    for (const auto& value : card.value(QStringLiteral("examples")).toArray()) {
        const auto example = value.toObject();
        if (matches(example, text)) {
            selectedExample = example;
            break;
        }
        if (selectedExample.isEmpty() && example.value(QStringLiteral("tags")).toArray().isEmpty()) {
            selectedExample = example;
        }
    }
    if (!selectedExample.isEmpty()) {
        QStringList lines;
        for (const auto& part : selectedExample.value(QStringLiteral("messages")).toArray()) {
            const auto message = part.toObject();
            const auto content = message.value(QStringLiteral("content")).toString().left(650);
            if (!content.isEmpty()) {
                lines.append((message.value(QStringLiteral("role")).toString() == QStringLiteral("user")
                    ? QStringLiteral("用户：") : QStringLiteral("角色：")) + content);
            }
            if (lines.size() == 2) {
                break;
            }
        }
        if (lines.size() == 2) {
            context.append(QStringLiteral("语气示例（只参考表达风格，不将其中情节或数值用作当前事实）：\n")
                           + lines.join(u'\n'));
        }
    }
    return context;
}

QString roleVoiceAlias(const QString& localId, const QString& language)
{
    return QStringLiteral("gifpet-role-%1-%2").arg(localId, language);
}
