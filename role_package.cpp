#include "role_package.h"
#include "archive_sdk.h"
#include "companion_events.h"
#include "pet_asset.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

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

QString archiveError(archive* zip)
{
    return QString::fromUtf8(archive_error_string(zip));
}

bool allowedPath(const QString& path)
{
    static const QRegularExpression pattern(QStringLiteral(
        R"(\A(?:manifest\.json|role/card\.json|behavior\.json|voice/(?:voices|corpus)\.json|assets/(?:default|idle|happy|sleepy)\.(?:gif|png|jpg|avif)|voice/(?:ref-(?:zh|ja)|corpus/(?:zh|ja)/[0-9]{3})\.wav)\z)"));
    return pattern.match(path).hasMatch();
}

qint64 fileLimit(const QString& path)
{
    if (path.endsWith(QStringLiteral(".json"))) {
        return maxJsonBytes;
    }
    return path.startsWith(QStringLiteral("voice/")) ? maxVoiceBytes : maxPetAssetBytes;
}

std::optional<Files> readZip(const QString& path, QString& error)
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
            const auto count = archive_read_data(zip.get(), bytes.data() + received, size - received);
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

bool writeZip(const QString& path, const Files& files, QString& error)
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
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        archive_entry_set_pathname(entry.get(), it.key().toLatin1().constData());
        archive_entry_set_filetype(entry.get(), AE_IFREG);
        archive_entry_set_perm(entry.get(), 0644);
        archive_entry_set_size(entry.get(), it.value().size());
        if (archive_write_header(zip.get(), entry.get()) != ARCHIVE_OK
            || archive_write_data(zip.get(), it.value().constData(), it.value().size()) != it.value().size()) {
            error = QStringLiteral("写入角色包失败。") + archiveError(zip.get());
            return false;
        }
    }
    if (archive_write_close(zip.get()) != ARCHIVE_OK) {
        error = QStringLiteral("无法完成角色包。") + archiveError(zip.get());
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

bool validateRoleShare(const RoleShare& share, QString& error)
{
    for (const auto& object : {share.card, share.behavior, share.voices, share.corpusNames}) {
        if (QJsonDocument(object).toJson(QJsonDocument::Compact).size() > maxJsonBytes) {
            error = QStringLiteral("角色卡、行为预设或语音清单超过 128 KiB。");
            return false;
        }
    }
    if (!share.card.isEmpty()) {
        const auto core = share.card.value(QStringLiteral("corePrompt")).toArray();
        qsizetype characters = 0;
        for (const auto& line : core) {
            if (!line.isString() || line.toString().trimmed().isEmpty()) {
                error = QStringLiteral("角色卡的 corePrompt 应由非空文本组成。");
                return false;
            }
            characters += line.toString().size();
        }
        if (share.card.value(QStringLiteral("schemaVersion")).toInt() != 1 || characters == 0 || characters > 4000) {
            error = QStringLiteral("角色卡需使用版本 1，并包含最多 4000 字符的核心设定。");
            return false;
        }
    }
    const auto behavior = share.behavior;
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

std::optional<RolePackage> readRolePackage(const QString& path, QString& error)
{
    auto files = readZip(path, error);
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
    if (!validId(id) || name.isEmpty() || name.size() > 80 || declared.size() != files->size()) {
        error = QStringLiteral("角色包信息或素材清单不完整。");
        return std::nullopt;
    }
    for (auto it = files->cbegin(); it != files->cend(); ++it) {
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
    const auto asset = decodePetAsset(package.asset, error);
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
        const auto action = decodePetAsset(bytes, error);
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
                                Section{voicesPath, &RoleShare::voices}, Section{corpusPath, &RoleShare::corpusNames}}) {
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
    if (!validateRoleShare(package.share, error)) {
        return std::nullopt;
    }
    return package;
}

bool writeRolePackage(const QString& path, const RolePackage& package, bool includeCorpus, QString& error)
{
    if (!validId(package.id) || package.name.trimmed().isEmpty() || package.name.size() > 80
        || !decodePetAsset(package.asset, error) || !validateRoleShare(package.share, error)) {
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
        if (!decodePetAsset(it->bytes, error)) {
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
    addJson(behaviorPath, package.share.behavior);
    addJson(voicesPath, package.share.voices);
    if (includeCorpus) {
        addJson(corpusPath, package.share.corpusNames);
    }
    for (auto it = package.share.files.cbegin(); it != package.share.files.cend(); ++it) {
        if (includeCorpus || !it.key().startsWith(QStringLiteral("voice/corpus/"))) {
            files.insert(it.key(), it.value());
        }
    }
    QJsonObject list;
    qint64 total = 0;
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        list.insert(it.key(), QString::fromLatin1(QCryptographicHash::hash(it.value(), QCryptographicHash::Sha256).toHex()));
        total += it.value().size();
    }
    files.insert(manifestPath, QJsonDocument(QJsonObject{
        {QStringLiteral("format"), QStringLiteral("gif-pet")}, {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("minimumAppVersion"), QStringLiteral("1.0.27")}, {QStringLiteral("id"), package.id},
        {QStringLiteral("name"), package.name}, {QStringLiteral("asset"), basePath}, {QStringLiteral("actions"), actions},
        {QStringLiteral("files"), list},
    }).toJson(QJsonDocument::Indented));
    if (total + files.value(manifestPath).size() > maxPackageBytes - 1024 * 1024) {
        error = QStringLiteral("角色包大小将超过 256 MiB。");
        return false;
    }
    return writeZip(path, files, error);
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
    for (const auto& value : card.value(QStringLiteral("examples")).toArray()) {
        const auto example = value.toObject();
        if (!matches(example, text)) {
            continue;
        }
        QStringList lines;
        for (const auto& part : example.value(QStringLiteral("messages")).toArray()) {
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
            break;
        }
    }
    return context;
}

QString roleVoiceAlias(const QString& localId, const QString& language)
{
    return QStringLiteral("gifpet-role-%1-%2").arg(localId, language);
}
