#include "random_speech.h"
#include "role_library.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

const QList<RandomSpeechLine>& randomSpeechExamples()
{
    static const QList<RandomSpeechLine> examples{
        {QStringLiteral("here"), QStringLiteral("我在这儿。你忙完再找我也可以。")},
        {QStringLiteral("continue"), QStringLiteral("刚才做到哪一步了？接着来吧。")},
        {QStringLiteral("water"), QStringLiteral("水杯还在手边吗？喝一口再继续。")},
        {QStringLiteral("eyes"), QStringLiteral("抬头看看远处，让眼睛休息一会儿。")},
        {QStringLiteral("shoulders"), QStringLiteral("放松一下肩膀。你已经保持那个姿势很久了。")},
        {QStringLiteral("save"), QStringLiteral("先保存一下。花不了多少时间。")},
        {QStringLiteral("priority"), QStringLiteral("现在最值得做的是哪一件事？")},
        {QStringLiteral("small-step"), QStringLiteral("如果不知道从哪儿开始，就挑最小的一步。")},
        {QStringLiteral("one-thing"), QStringLiteral("一次做一件事，剩下的先记下来。")},
        {QStringLiteral("break"), QStringLiteral("可以停一会儿。我会在这儿等你。")},
        {QStringLiteral("pace"), QStringLiteral("不用急，按你自己的节奏来。")},
        {QStringLiteral("listen"), QStringLiteral("有话想说的话，我可以听。")},
        {QStringLiteral("check"), QStringLiteral("提交以前，再核对一遍手头的记录。")},
        {QStringLiteral("stretch"), QStringLiteral("要不要站起来走两步？")},
        {QStringLiteral("light"), QStringLiteral("屏幕看着刺眼的话，调一点亮度试试。")},
        {QStringLiteral("note"), QStringLiteral("刚想到的点子可以先记下来，晚点再展开。")},
        {QStringLiteral("focus"), QStringLiteral("选一个目标，给它一段完整的时间。")},
        {QStringLiteral("back"), QStringLiteral("回来了？我们接着上次的进度。")},
        {QStringLiteral("progress"), QStringLiteral("看看已经做完的部分，再决定下一步。")},
        {QStringLiteral("quiet"), QStringLiteral("我先安静待一会儿，你专心就好。")},
        {QStringLiteral("breathe"), QStringLiteral("先缓一口气，再看看问题卡在哪儿。")},
        {QStringLiteral("desk"), QStringLiteral("我在桌面上留个位置，方便你随时找我。")},
        {QStringLiteral("simplify"), QStringLiteral("如果步骤太多，先去掉一件此刻不必做的事。")},
        {QStringLiteral("wrap-up"), QStringLiteral("收尾时记一下下一步，下次回来就能接上。")},
        {QStringLiteral("company"), QStringLiteral("就算不聊天，也可以一起待一会儿。")},
        {QStringLiteral("meal"), QStringLiteral("手头的事可以稍等。别把吃饭忘了。")},
        {QStringLiteral("air"), QStringLiteral("有空可以透透气，回来再看看这个问题。")},
        {QStringLiteral("clear"), QStringLiteral("把无关的窗口收一收，给眼前的事留点空间。")},
    };
    return examples;
}

bool isRandomSpeechRecording(const QString& path)
{
    static const QRegularExpression syntax(QStringLiteral(R"(\Avoice/corpus/(zh|ja)/[0-9]{3}\.wav\z)"));
    return syntax.match(path).hasMatch();
}

QString recordingDigest(const QByteArray& audio)
{
    return QString::fromLatin1(QCryptographicHash::hash(audio, QCryptographicHash::Sha256).toHex());
}

RandomSpeechConfig randomSpeechConfig(const QJsonObject& behavior)
{
    RandomSpeechConfig config;
    const auto value = behavior.value(QStringLiteral("randomSpeech"));
    if (value.isUndefined()) {
        for (const auto& sample : randomSpeechExamples()) {
            config.examples.append(sample.id);
        }
        const auto line = behavior.value(QStringLiteral("events")).toObject().value(QStringLiteral("doubleClick"))
            .toObject().value(QStringLiteral("line")).toString().trimmed();
        if (!line.isEmpty()) {
            config.lines.append({QStringLiteral("legacy-double-click"), line, true});
        }
        return config;
    }
    const auto object = value.toObject();
    config.withAi = object.value(QStringLiteral("withAi")).toBool(true);
    for (const auto& example : object.value(QStringLiteral("examples")).toArray()) {
        config.examples.append(example.toString());
    }
    for (const auto& entry : object.value(QStringLiteral("lines")).toArray()) {
        const auto line = entry.toObject();
        config.lines.append({line.value(QStringLiteral("id")).toString(), line.value(QStringLiteral("text")).toString(),
                             line.value(QStringLiteral("enabled")).toBool()});
    }
    config.recordings = object.value(QStringLiteral("recordings")).toObject();
    return config;
}

QJsonObject encodeRandomSpeech(const RandomSpeechConfig& config)
{
    QJsonArray samples;
    for (const auto& id : config.examples) {
        samples.append(id);
    }
    QJsonArray custom;
    for (const auto& line : config.lines) {
        custom.append(QJsonObject{{QStringLiteral("id"), line.id}, {QStringLiteral("text"), line.text},
                                  {QStringLiteral("enabled"), line.enabled}});
    }
    return {{QStringLiteral("examples"), samples}, {QStringLiteral("lines"), custom},
            {QStringLiteral("recordings"), config.recordings}, {QStringLiteral("withAi"), config.withAi}};
}

bool validateRandomSpeech(const QJsonObject& config, QString& error)
{
    static const QRegularExpression idSyntax(QStringLiteral(R"(\A[a-z0-9-]{1,48}\z)"));
    static const QRegularExpression digestSyntax(QStringLiteral(R"(\A[0-9a-f]{64}\z)"));
    const auto examples = config.value(QStringLiteral("examples"));
    const auto lines = config.value(QStringLiteral("lines"));
    const auto recordings = config.value(QStringLiteral("recordings"));
    const auto withAi = config.value(QStringLiteral("withAi"));
    if (!examples.isArray() || !lines.isArray() || !recordings.isObject() || examples.toArray().size() > 100
        || lines.toArray().size() > 100 || recordings.toObject().size() > 200
        || (!withAi.isUndefined() && !withAi.isBool())) {
        error = QStringLiteral("随机发言清单格式无效，或自定义台词超过 100 条。");
        return false;
    }
    QSet<QString> ids;
    for (const auto& example : examples.toArray()) {
        const auto id = example.toString();
        if (!idSyntax.match(id).hasMatch() || ids.contains(id)) {
            error = QStringLiteral("内置示例清单含有无效或重复项目。");
            return false;
        }
        ids.insert(id);
    }
    ids.clear();
    for (const auto& entry : lines.toArray()) {
        const auto line = entry.toObject();
        const auto id = line.value(QStringLiteral("id")).toString();
        const auto text = line.value(QStringLiteral("text")).toString();
        if (!idSyntax.match(id).hasMatch() || ids.contains(id) || text.trimmed().isEmpty()
            || text.size() > 280 || !line.value(QStringLiteral("enabled")).isBool()) {
            error = QStringLiteral("自定义台词需要启用状态和 1 至 280 字的正文。");
            return false;
        }
        ids.insert(id);
    }
    const auto selected = recordings.toObject();
    for (auto it = selected.cbegin(); it != selected.cend(); ++it) {
        if (!isRandomSpeechRecording(it.key()) || !digestSyntax.match(it.value().toString()).hasMatch()) {
            error = QStringLiteral("原声选择包含无效的录音。");
            return false;
        }
    }
    return true;
}

void removeChangedRandomRecordings(RoleShare& share)
{
    const auto key = QStringLiteral("randomSpeech");
    if (!share.behavior.contains(key)) {
        return;
    }
    auto config = randomSpeechConfig(share.behavior);
    const auto paths = config.recordings.keys();
    for (const auto& path : paths) {
        const auto audio = share.files.constFind(path);
        if (audio == share.files.cend() || recordingDigest(audio.value()) != config.recordings.value(path).toString()) {
            config.recordings.remove(path);
        }
    }
    share.behavior.insert(key, encodeRandomSpeech(config));
}

QList<RandomSpeechChoice> randomSpeechChoices(const RoleShare& share)
{
    const auto config = randomSpeechConfig(share.behavior);
    QList<RandomSpeechChoice> choices;
    for (const auto& example : randomSpeechExamples()) {
        if (config.examples.contains(example.id)) {
            choices.append({QStringLiteral("example/") + example.id, example.text, {}, {}});
        }
    }
    for (const auto& line : config.lines) {
        if (line.enabled) {
            choices.append({QStringLiteral("line/") + line.id, line.text, {}, {}});
        }
    }
    for (auto it = config.recordings.cbegin(); it != config.recordings.cend(); ++it) {
        const auto path = it.key();
        const auto name = share.corpusNames.value(path).toString(path.section(u'/', -1));
        const auto caption = share.corpusTexts.value(path).toString().trimmed();
        choices.append({path, caption.isEmpty() ? name : caption, path, it.value().toString()});
    }
    return choices;
}

RandomSpeechChoice RandomSpeechDraw::take(const QString& role, const QList<RandomSpeechChoice>& choices)
{
    if (choices.isEmpty()) {
        return {};
    }
    QStringList ids;
    for (const auto& choice : choices) {
        ids.append(choice.id);
    }
    if (role_ != role) {
        role_ = role;
        ids_.clear();
        last_.clear();
    }
    if (ids_ != ids || remaining_.isEmpty()) {
        ids_ = ids;
        remaining_ = ids;
        for (qsizetype i = remaining_.size() - 1; i > 0; --i) {
            remaining_.swapItemsAt(i, QRandomGenerator::global()->bounded(static_cast<int>(i + 1)));
        }
        if (remaining_.size() > 1 && remaining_.last() == last_) {
            remaining_.swapItemsAt(remaining_.size() - 1, 0);
        }
    }
    last_ = remaining_.takeLast();
    return choices.at(ids.indexOf(last_));
}
