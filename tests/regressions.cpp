#include "archive_sdk.h"
#include "desktop_application.h"
#include "overlay_interaction.h"
#include "random_speech.h"
#include "role_package.h"

#include <QBuffer>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QProcess>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimer>
#include <QtConcurrentRun>

#ifdef Q_OS_MACOS
#include "chat_credentials.h"
#include "chat_session.h"
#include "frame_sequence.h"
#include "mac_window_layer.h"
#include "pet_asset.h"
#include "pet_motion_rules.h"
#include "pet_window.h"
#include "role_actions.h"
#include "role_library_dialog.h"
#include "settings_store.h"
#include "space_transit.h"
#include "speech_bubble.h"
#include "speech_playback.h"
#include "speech_text.h"
#include <QCheckBox>
#include <QDialog>
#include <QGuiApplication>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUuid>
#endif

#include <cmath>

namespace {
void expect(bool condition, const QString& message)
{
    if (!condition) {
        qFatal("%s", qPrintable(message));
    }
}

void sql(QSqlQuery& query, const QString& statement)
{
    const auto ok = query.exec(statement);
    expect(ok, statement + u' ' + query.lastError().text());
}

QByteArray image()
{
    QImage png(8, 8, QImage::Format_RGBA8888);
    png.fill(Qt::red);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    expect(buffer.open(QIODevice::WriteOnly) && png.save(&buffer, "PNG"), QStringLiteral("PNG fixture"));
    return bytes;
}

void testStorage(const QString& root)
{
    const auto path = QDir(root).filePath(QStringLiteral("roles.sqlite3"));
    const auto copy = QDir(root).filePath(QStringLiteral("manual.sqlite3"));
    const auto png = image();
    const auto id = QStringLiteral("old-role");
    {
        auto legacy = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("legacy"));
        legacy.setDatabaseName(path);
        expect(legacy.open(), legacy.lastError().text());
        QSqlQuery q(legacy);
        sql(q, QStringLiteral("CREATE TABLE roles (id TEXT PRIMARY KEY NOT NULL, name TEXT NOT NULL, asset BLOB NOT NULL)"));
        sql(q, QStringLiteral("CREATE TABLE chat_messages (id INTEGER PRIMARY KEY, role_id TEXT, speaker TEXT, text TEXT, created_at TEXT DEFAULT CURRENT_TIMESTAMP)"));
        q.prepare(QStringLiteral("INSERT INTO roles VALUES (?,'old name',?)"));
        q.addBindValue(id);
        q.addBindValue(png);
        expect(q.exec(), q.lastError().text());
        sql(q, QStringLiteral("INSERT INTO chat_messages(role_id,speaker,text) VALUES ('old-role','user','kept message')"));
    }
    QSqlDatabase::removeDatabase(QStringLiteral("legacy"));
    QString error;
    {
        RoleLibrary library;
        expect(library.open(path, error), error);
        expect(library.assetFor(id, error) == png, QStringLiteral("Upgrade lost the original asset"));
        const auto rows = library.chatHistory(id, 20, error);
        expect(rows.size() == 1 && rows.first().text == QStringLiteral("kept message"), QStringLiteral("Upgrade lost the conversation"));
        expect(QDir(QDir(root).filePath(QStringLiteral("backups"))).entryList({QStringLiteral("before-upgrade-*.sqlite3")}, QDir::Files).size() == 1,
               QStringLiteral("Missing consistent pre-migration backup"));
        expect(library.backupTo(copy, error), error);
        auto read = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("reader"));
        read.setDatabaseName(path);
        expect(read.open(), read.lastError().text());
        {
            QSqlQuery q(read);
            sql(q, QStringLiteral("PRAGMA user_version"));
            expect(q.next() && q.value(0).toInt() == 1, QStringLiteral("Unexpected migration version"));
            sql(q, QStringLiteral("PRAGMA journal_mode"));
            const bool wal = q.next() && q.value(0).toString() == QStringLiteral("wal");
            q.finish();
            if (wal) {
                expect(read.transaction(), read.lastError().text());
                sql(q, QStringLiteral("SELECT text FROM chat_messages"));
                expect(q.next(), q.lastError().text());
                expect(library.saveExchange(id, QStringLiteral("parallel write"), QStringLiteral("OK"), error), error);
                q.finish();
                read.rollback();
                qInfo("Verified WAL writer with an independent active reader");
            }
        }
        read = QSqlDatabase{};
        QSqlDatabase::removeDatabase(QStringLiteral("reader"));
        expect(library.clearConversation(id, error), error);
        expect(library.chatHistory(id, 20, error).isEmpty() && library.assetFor(id, error) == png,
               QStringLiteral("Clearing conversations changed other role data"));
    }
    expect(RoleLibrary::restoreBackup(copy, path, error), error);
    {
        RoleLibrary library;
        expect(library.open(path, error), error);
        const auto rows = library.chatHistory(id, 20, error);
        expect(rows.size() == 1 && rows.first().text == QStringLiteral("kept message"), QStringLiteral("Restore did not recover saved data"));
    }
    expect(!RoleLibrary::restoreBackup(path, path, error), QStringLiteral("Restore accepted the current database as its own backup"));
}

void zipManifest(const QString& path, const QJsonObject& manifest, const QByteArray& png, const QByteArray& assetName)
{
    QFile output(path);
    expect(output.open(QIODevice::WriteOnly), output.errorString());
    auto* zip = archive_write_new();
    expect(zip && archive_write_set_format_zip(zip) == ARCHIVE_OK && archive_write_open_fd(zip, output.handle()) == ARCHIVE_OK,
           QStringLiteral("Cannot create external ZIP fixture"));
    for (const auto& [name, bytes] : {qMakePair(QByteArray("manifest.json"), QJsonDocument(manifest).toJson()), qMakePair(assetName, png)}) {
        auto* entry = archive_entry_new();
        archive_entry_set_pathname(entry, name.constData());
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644);
        archive_entry_set_size(entry, bytes.size());
        expect(archive_write_header(zip, entry) == ARCHIVE_OK && archive_write_data(zip, bytes.constData(), bytes.size()) == bytes.size(),
               QStringLiteral("Cannot write external ZIP fixture"));
        archive_entry_free(entry);
    }
    expect(archive_write_close(zip) == ARCHIVE_OK, QStringLiteral("Cannot finalize external ZIP fixture"));
    archive_write_free(zip);
}

void testPackages(const QString& root)
{
    const QDir dir(root);
    RolePackage sample;
    sample.id = QStringLiteral("56664dc2-253c-4c40-ace1-c5f4e5800502");
    sample.name = QStringLiteral("Round trip");
    sample.asset = image();
    sample.share.card = {{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("identityAndStance"), QStringLiteral("A companion")}};
    const auto complete = dir.filePath(QStringLiteral("good.gifpet"));
    QString error;
    expect(writeRolePackage(complete, sample, true, error), error);
    auto worker = QtConcurrent::run([&] { return readRolePackage(complete, error); });
    worker.waitForFinished();
    const auto result = worker.takeResult();
    expect(result && result->share.card == sample.share.card && !result->cover.isNull(), error);
    const auto canceled = dir.filePath(QStringLiteral("canceled.gifpet"));
    expect(!writeRolePackage(canceled, sample, true, error, [](int value) { return value < 35; }) && !QFile::exists(canceled),
           QStringLiteral("Canceled export wrote a destination file"));
    error.clear();
    QJsonObject manifest{{QStringLiteral("format"), QStringLiteral("gif-pet")}, {QStringLiteral("schemaVersion"), 1},
                         {QStringLiteral("minimumAppVersion"), QStringLiteral("9.0.0")}};
    const auto newer = dir.filePath(QStringLiteral("newer.gifpet"));
    zipManifest(newer, manifest, sample.asset, "assets/default.png");
    expect(!readRolePackage(newer, error) && error.contains(QStringLiteral("9.0.0")), QStringLiteral("Minimum app version wasn't enforced"));
    error.clear();
    const auto unsafe = dir.filePath(QStringLiteral("unsafe.gifpet"));
    zipManifest(unsafe, manifest, sample.asset, "../default.png");
    expect(!readRolePackage(unsafe, error), QStringLiteral("ZIP accepted an unrecognized path"));
}

void testRandomSpeech(const QString& root)
{
    const auto poolKey = QStringLiteral("randomSpeech");
    const auto path = QStringLiteral("voice/corpus/zh/001.wav");
    RoleShare share;
    share.behavior = {{QStringLiteral("events"), QJsonObject{
        {QStringLiteral("doubleClick"), QJsonObject{{QStringLiteral("line"), QStringLiteral("旧双击台词")}}}
    }}};
    const auto defaults = randomSpeechChoices(share);
    expect(defaults.size() > 20 && defaults.last().text == QStringLiteral("旧双击台词"),
           QStringLiteral("Default random speech did not retain the old double-click line"));
    RandomSpeechDraw draw;
    QSet<QString> round;
    QString previous;
    for (qsizetype i = 0; i < defaults.size() * 3; ++i) {
        if (i % defaults.size() == 0) {
            round.clear();
        }
        const auto choice = draw.take(QStringLiteral("test-role"), defaults);
        expect(choice.id != previous && !round.contains(choice.id), QStringLiteral("Random speech repeated too early"));
        round.insert(choice.id);
        previous = choice.id;
    }

    const auto wave = QByteArray::fromHex("524946462800000057415645666d74201000000001000100c05d000080bb000002001000646174610400000001000200");
    share.files.insert(path, wave);
    share.corpusNames.insert(path, QStringLiteral("001-任命助理.wav"));
    share.corpusTexts.insert(path, QStringLiteral("角色原声台词"));
    QString error;
    RoleLibrary library;
    expect(library.open(QDir(root).filePath(QStringLiteral("random-speech.sqlite3")), error), error);
    const auto role = library.add(QStringLiteral("Random speech role"), image(), error);
    expect(!role.isEmpty() && library.saveShare(role, share, error), error);
    const RandomSpeechConfig config{{}, {{QStringLiteral("custom-1"), QStringLiteral("新台词"), true},
                                          {QStringLiteral("custom-2"), QStringLiteral("禁用台词"), false}},
                                    {{path, recordingDigest(wave)}}, false};
    expect(library.saveRandomSpeech(role, encodeRandomSpeech(config), error), error);
    auto persisted = library.shareFor(role, error);
    const auto choices = randomSpeechChoices(persisted);
    expect(choices.size() == 2 && choices.first().text == QStringLiteral("新台词")
        && choices.last().text == QStringLiteral("角色原声台词")
        && persisted.behavior.contains(QStringLiteral("events"))
        && library.roleRecording(role, path, error) == wave, QStringLiteral("Per-role speech settings lost unrelated data"));

    RolePackage bundle;
    bundle.id = role;
    bundle.name = QStringLiteral("Speech test");
    bundle.asset = image();
    bundle.share = persisted;
    for (const bool includeAudio : {false, true}) {
        const auto file = QDir(root).filePath(includeAudio ? QStringLiteral("with-speech-audio.gifpet")
                                                         : QStringLiteral("speech-text-only.gifpet"));
        expect(writeRolePackage(file, bundle, includeAudio, error), error);
        const auto reopened = readRolePackage(file, error);
        expect(reopened && randomSpeechChoices(reopened->share).size() == (includeAudio ? 2 : 1)
            && !randomSpeechConfig(reopened->share.behavior).withAi,
               QStringLiteral("Role export lost speech settings or preserved an omitted recording"));
    }
    auto replacement = wave;
    replacement[44] = 42;
    persisted.files.insert(path, replacement);
    expect(library.saveShare(role, persisted, error), error);
    expect(randomSpeechChoices(library.shareFor(role, error)).size() == 1,
           QStringLiteral("Replacing WAV silently enabled a different recording"));
    expect(library.saveRandomSpeech(role, encodeRandomSpeech({}), error)
        && randomSpeechChoices(library.shareFor(role, error)).isEmpty(), QStringLiteral("Disabled speech still produced a line"));
}

void testInstance(DesktopApplication& app, const QString& root)
{
    QString error;
    expect(app.claimProfile(root, {}, false, error) == DesktopApplication::Instance::Primary, error);
    app.startFileDelivery();
    QProcess client;
    QEventLoop loop;
    QString delivered;
    QObject::connect(&app, &DesktopApplication::fileRequested, &loop, [&](const QString& path) { delivered = path; });
    QObject::connect(&client, &QProcess::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    const auto file = QDir(root).filePath(QStringLiteral("素材.gifpet"));
    client.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--forward"), root, file});
    loop.exec();
    expect(client.state() == QProcess::NotRunning && client.exitCode() == 0 && delivered == file,
           QStringLiteral("Second process failed to route the file to the original instance"));
}

#ifdef Q_OS_MACOS
void testChatPrivacy(const QString& root)
{
    using namespace ChatSettings;
    QString error;
    RoleLibrary library;
    expect(library.open(QDir(root).filePath(QStringLiteral("privacy.sqlite3")), error), error);
    const auto id = library.add(QStringLiteral("Privacy"), image(), error);
    expect(!id.isEmpty() && library.saveExchange(id, QStringLiteral("old exchange"), QStringLiteral("old reply"), error), error);
    const auto old = library.chatHistory(id, 2, error);
    expect(old.size() == 2 && library.saveSummary(id, {0, old}, QStringLiteral("retained memory"),
        {}, error), error);
    expect(!id.isEmpty() && library.saveExchange(id, QStringLiteral("private history marker"), QStringLiteral("reply"), error), error);
    expect(library.saveRoleCard(id, {{QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("identityAndStance"), QStringLiteral("role identity marker")},
        {QStringLiteral("contextBlocks"), QJsonArray{QJsonObject{
            {QStringLiteral("tags"), QJsonArray{QStringLiteral("private history marker")}},
            {QStringLiteral("text"), QStringLiteral("recent topic marker")}
        }}}}, error), error);
    QSettings settings(QDir(root).filePath(QStringLiteral("chat.ini")), QSettings::IniFormat);
    QTcpServer server;
    expect(server.listen(QHostAddress::LocalHost), server.errorString());
    const auto local = QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort());
    settings.setValue(baseUrlSetting, local);
    settings.setValue(modelSetting, QStringLiteral("test"));
    ChatSession chat(library, settings);
    chat.setRole(id);
    expect(chatEndpoint(QStringLiteral("http://example.invalid/v1")).isEmpty(), QStringLiteral("Remote plaintext accepted"));
    expect(!chat.send(QStringLiteral("Before consent"), error), QStringLiteral("Consent was not enforced"));
    expect(!chat.speak(), QStringLiteral("Proactive speech ignored data consent"));
    settings.setValue(consentEndpointSetting, chatEndpoint(local).toString(QUrl::FullyEncoded));
    settings.setValue(privateChatSetting, true);
    QByteArray payload;
    bool failNext = false;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
        auto* socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, request = QByteArray{}]() mutable {
            request += socket->readAll();
            const auto start = request.indexOf("\r\n\r\n");
            const auto length = QRegularExpression(QStringLiteral("Content-Length: (\\d+)"),
                QRegularExpression::CaseInsensitiveOption).match(QString::fromLatin1(request.left(start)));
            if (start < 0 || !length.hasMatch() || request.size() - start - 4 < length.captured(1).toInt()) {
                return;
            }
            payload = request.mid(start + 4);
            if (failNext) {
                socket->write("HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                socket->disconnectFromHost();
                return;
            }
            const QByteArray events = "data: {\"choices\":[{\"delta\":{\"content\":\"Test reply\"}}]}\n\ndata: [DONE]\n\n";
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Length: "
                + QByteArray::number(events.size()) + "\r\nConnection: close\r\n\r\n" + events);
            socket->disconnectFromHost();
        });
    });
    QEventLoop reply;
    QTimer limit;
    limit.setSingleShot(true);
    QObject::connect(&limit, &QTimer::timeout, &reply, &QEventLoop::quit);
    bool succeeded = false;
    QObject::connect(&chat, &ChatSession::replyFinished, &reply, [&](const QString& text, const QString& failure) {
        succeeded = failure.isEmpty() && text == QStringLiteral("Test reply");
        reply.quit();
    });
    error.clear();
    expect(chat.send(QStringLiteral("Current question"), error), error);
    const auto waitForReply = [&] {
        limit.start(5000);
        reply.exec();
        limit.stop();
    };
    waitForReply();
    expect(succeeded && payload.contains("Current question") && !payload.contains("private history marker"),
           QStringLiteral("Private mode included historical context"));
    expect(library.chatHistory(id, 20, error).size() == 4, QStringLiteral("Private conversation was persisted"));

    succeeded = false;
    expect(chat.speak(), QStringLiteral("Private proactive speech did not use the chat service"));
    waitForReply();
    expect(succeeded && !payload.contains("private history marker") && !payload.contains("retained memory")
        && library.chatHistory(id, 20, error).size() == 4, QStringLiteral("Private proactive speech reused or saved history"));

    succeeded = false;
    settings.setValue(privateChatSetting, false);
    expect(chat.speak(), QStringLiteral("Proactive speech did not use the chat service"));
    waitForReply();
    const auto history = library.chatHistory(id, 20, error);
    expect(succeeded && payload.contains("private history marker") && payload.contains("retained memory")
        && payload.contains("role identity marker") && payload.contains("recent topic marker"),
        QStringLiteral("Proactive speech did not get the role, recent topic and memory"));
    expect(history.size() == 5 && history.last().proactive && history.last().text == QStringLiteral("Test reply"),
        QStringLiteral("Proactive speech did not save just the spoken reply"));
    failNext = true;
    int fallbacks = 0;
    QObject::connect(&chat, &ChatSession::proactiveFailed, &reply, [&] { ++fallbacks; });
    expect(chat.speak(), QStringLiteral("Failure scenario did not start"));
    waitForReply();
    expect(fallbacks == 1 && library.chatHistory(id, 20, error).size() == 5,
           QStringLiteral("Proactive failure did not request the local fallback"));
}

void testSpeechBubble()
{
    QWidget pet;
    SpeechBubble bubble(&pet);
    const QRect screen(0, 0, 1000, 800);
    const QRect anchor(400, 550, 90, 90);
    const auto hasText = [&](int firstRow, int lastRow) {
        QImage image(bubble.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        bubble.render(&image);
        for (int y = firstRow; y <= lastRow; ++y) {
            for (int x = 31; x < image.width() - 31; ++x) {
                const auto pixel = image.pixel(x, y);
                if (qAlpha(pixel) > 200 && qRed(pixel) < 110 && qGreen(pixel) < 110) {
                    return true;
                }
            }
        }
        return false;
    };
    bubble.beginReply();
    bubble.setMessage(QStringLiteral("BEGIN"));
    bubble.place(screen, anchor, anchor);
    const auto shortHeight = bubble.height();
    expect(hasText(22, shortHeight - 22), QStringLiteral("Short reply did not render"));
    bubble.setMessage(QStringLiteral("BEGIN\n") + QString(42, u'\n') + QStringLiteral("TAIL"));
    bubble.place(screen, anchor, anchor);
    expect(bubble.height() > shortHeight && bubble.height() <= 260 && !pet.findChild<QPushButton*>(),
           QStringLiteral("Long text sizing or text-only bubble failed"));
    QEventLoop scroll;
    QTimer::singleShot(440, &scroll, &QEventLoop::quit);
    scroll.exec();
    expect(!hasText(22, 45) && hasText(bubble.height() - 43, bubble.height() - 22),
           QStringLiteral("Bubble did not scroll to the last output line"));
    bubble.beginReply();
    bubble.setMessage(QStringLiteral("NEW"));
    bubble.place(screen, anchor, anchor);
    expect(bubble.height() == shortHeight && hasText(22, shortHeight - 22),
           QStringLiteral("Next reply inherited the scroll of the previous bubble"));
}

class TestPcm : public PcmOutput {
public:
    bool append(const QByteArray& pcm) override { received += pcm.size() / 2; return true; }
    qint64 playedFrames() const override { return played; }
    void reset() override { received = played = 0; }

    qint64 received = 0;
    qint64 played = 0;
};

class TestSystemSpeech : public SystemSpeechOutput {
public:
    bool speak(const QString& text, const QString&) override
    {
        utterance = text;
        reading = true;
        return true;
    }
    bool active() const override { return reading; }
    std::optional<Range> spokenRange() const override { return range; }
    void reset() override { reading = false; range.reset(); }

    QString utterance;
    bool reading = false;
    std::optional<Range> range;
};

class TestRecording : public RecordingOutput {
public:
    bool play(const QByteArray&) override { playing = true; return true; }
    bool active() const override { return playing; }
    qreal progress() const override { return fraction; }
    void reset() override { playing = false; fraction = 0; }

    bool playing = false;
    qreal fraction = 0;
};

bool waitUntil(const std::function<bool()>& ready, int timeoutMs = 2000)
{
    if (ready()) {
        return true;
    }
    QEventLoop loop;
    QTimer check;
    QObject::connect(&check, &QTimer::timeout, &loop, [&] {
        if (ready()) {
            loop.quit();
        }
    });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    check.start(10);
    loop.exec();
    return ready();
}

void testLocalKey()
{
    QStandardPaths::setTestModeEnabled(true);
    const auto endpoint = QStringLiteral("https://aurapet.invalid/") + QUuid::createUuid().toString();
    const auto key = QStringLiteral("synthetic-test-key");
    QString value;
    QString error;
    expect(saveChatKey(endpoint, key, error), error);
    expect(readChatKey(endpoint, true, value, error) && value == key, QStringLiteral("Local key could not be read again"));
    expect(readChatKey(endpoint + u'2', true, value, error) && value.isEmpty(),
           QStringLiteral("The credential leaked to another endpoint"));
    expect(saveChatKey(endpoint, {}, error), error);
    expect(readChatKey(endpoint, true, value, error) && value.isEmpty(), QStringLiteral("A removed key was still available"));
    QStandardPaths::setTestModeEnabled(false);
}

void testSpeechText()
{
    const auto clean = [](const QString& text, bool finished = true) { return textForSpeech(QStringView(text), finished); };
    const QString original = QStringLiteral("**（合上档案。Mon3tr（停在门口））**博士，(停顿)请坐。🌿👩🏽‍⚕️ 🇯🇵 1️⃣ ^_^ T_T :D :sparkles: ヽ(・∀・)ﾉ (•̀ᴗ•́)و ಠ_ಠ");
    const auto result = clean(original);
    expect(result.text.trimmed() == QStringLiteral("博士，请坐。"), QStringLiteral("A nonspoken expression was kept: %1").arg(result.text));
    expect(original.at(result.sourceEnds.at(0) - 1) == u'博'
        && original.at(result.sourceEnds.at(3) - 1) == u'请', QStringLiteral("Speech lost the original caption offsets"));
    expect(result.asides.size() == 2 && result.asides.at(0).closed && result.asides.at(1).closed
        && result.asides.at(0).textOffset == 0 && result.asides.at(1).textOffset == 3
        && result.asides.at(0).pauseMs > result.asides.at(1).pauseMs
        && result.asides.at(1).pauseMs >= 450 && result.asides.at(1).pauseMs <= 900,
        QStringLiteral("Aside pause metadata did not match nested and mid-sentence actions (kaomoji must not create asides)"));
    expect(asidePauseDurationMs(QStringLiteral("   ")) == 0, QStringLiteral("Empty aside should not pause"));
    const int shortPause = asidePauseDurationMs(QStringLiteral("点头"));
    const int mediumPause = asidePauseDurationMs(QStringLiteral("停下手中的笔，抬起眼皮审视了你几秒，眉头微蹙"));
    const int longPause = asidePauseDurationMs(QString(120, u'看'));
    expect(shortPause >= 450 && shortPause < mediumPause && mediumPause < longPause && longPause == 3600,
           QStringLiteral("Aside pause duration curve out of expected bounds"));
    const auto unfinished = clean(QStringLiteral("博士。（Mon3tr 在阴影中。\n请勿朗读"), false);
    expect(unfinished.text == QStringLiteral("博士。") && unfinished.asides.size() == 1 && !unfinished.asides.first().closed,
           QStringLiteral("An unfinished aside leaked at a stream boundary"));
    expect(clean(QStringLiteral("博士。（Mon3tr 在阴影中。\n请勿朗读"), true).text == QStringLiteral("博士。"),
           QStringLiteral("An unclosed aside leaked at the end of a reply"));
    expect(clean(QStringLiteral("博士，请查看「第 2 份报告」。")).text == QStringLiteral("博士，请查看「第 2 份报告」。"),
           QStringLiteral("The spoken quote or numeral was lost"));
}

void testVoiceCaptions(const QString& root)
{
    QSettings settings(QDir(root).filePath(QStringLiteral("speech.ini")), QSettings::IniFormat);
    settings.setValue(QStringLiteral("speech/enabled"), true);
    settings.setValue(QStringLiteral("speech/useSystemVoice"), true);
    auto pcm = std::make_unique<TestPcm>();
    auto system = std::make_unique<TestSystemSpeech>();
    auto recording = std::make_unique<TestRecording>();
    auto* device = pcm.get();
    auto* synthesizer = system.get();
    auto* original = recording.get();
    SpeechPlayback voice(settings, std::move(pcm), std::move(system), std::move(recording));
    QStringList captions;
    int ended = 0;
    QObject::connect(&voice, &SpeechPlayback::captionChanged, &voice, [&](const QString& text) { captions.append(text); });
    QObject::connect(&voice, &SpeechPlayback::ended, &voice, [&] { ++ended; });
    voice.begin(QStringLiteral("demo"));
    const auto source = QStringLiteral("**博士**，e\u0301好。");
    voice.submit(source, true);
    expect(synthesizer->utterance == QStringLiteral("博士，e\u0301好。") && captions.isEmpty(),
           QStringLiteral("System subtitle showed before a speech event"));
    synthesizer->range = TestSystemSpeech::Range{0, 1};
    expect(waitUntil([&] { return captions.value(captions.size() - 1) == QStringLiteral("**博"); }),
           QStringLiteral("Speech range did not map to its original characters"));
    synthesizer->range = TestSystemSpeech::Range{1, 4};
    expect(waitUntil([&] { return captions.value(captions.size() - 1) == QStringLiteral("**博士**，e\u0301"); }),
           QStringLiteral("System subtitle failed to progress within a spoken range"));
    expect(!captions.contains(QStringLiteral("**博士**，e")), QStringLiteral("Caption split a combining character"));
    synthesizer->reading = false;
    expect(waitUntil([&] { return ended == 1; }) && captions.last() == source,
           QStringLiteral("System subtitle did not finish the sentence"));

    captions.clear();
    const auto sentence = QStringLiteral("桌宠欢迎你。");
    expect(voice.playRecording("fixture", sentence) && captions.isEmpty(), QStringLiteral("Original caption led its audio"));
    original->fraction = .5;
    expect(waitUntil([&] { return captions.size() == 3; }) && captions.last() == sentence.left(3),
           QStringLiteral("Original caption did not track playback duration one glyph at a time"));
    original->playing = false;
    expect(waitUntil([&] { return ended == 2; }) && captions.last() == sentence,
           QStringLiteral("Original caption did not finish"));

    QTcpServer service;
    expect(service.listen(QHostAddress::LocalHost), service.errorString());
    bool unavailable = false;
    QStringList requests;
    QObject::connect(&service, &QTcpServer::newConnection, &service, [&] {
        auto* socket = service.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &unavailable, &requests, request = QByteArray{}]() mutable {
            request += socket->readAll();
            const auto head = request.indexOf("\r\n\r\n");
            const auto length = QRegularExpression(QStringLiteral("Content-Length: (\\d+)"),
                QRegularExpression::CaseInsensitiveOption).match(QString::fromLatin1(request.left(head)));
            if (head < 0 || !length.hasMatch() || request.size() - head - 4 < length.captured(1).toInt()) {
                return;
            }
            requests.append(QJsonDocument::fromJson(request.mid(head + 4)).object().value(QStringLiteral("input")).toString());
            if (unavailable) {
                socket->write("HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            } else {
                const QByteArray body(SpeechPlayback::sampleRate * 2, '\0');
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: audio/pcm\r\nContent-Length: " +
                    QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
            }
            socket->disconnectFromHost();
        });
    });
    settings.setValue(QStringLiteral("speech/useSystemVoice"), false);
    settings.setValue(QStringLiteral("speech/endpoint"), QStringLiteral("http://127.0.0.1:%1/v1/audio/speech").arg(service.serverPort()));
    captions.clear();
    voice.begin(QStringLiteral("demo"));
    voice.submit(sentence, true);
    expect(waitUntil([&] { return device->received == SpeechPlayback::sampleRate; }) && captions.isEmpty(),
           QStringLiteral("Streaming caption led the actual audio frames"));
    device->played = SpeechPlayback::sampleRate / 2;
    expect(waitUntil([&] { return captions.size() == 3; }) && captions.last() == sentence.left(3),
           QStringLiteral("Streaming caption did not follow played frames"));
    device->played = SpeechPlayback::sampleRate;
    expect(waitUntil([&] { return ended == 3; }) && captions.size() == sentence.size() && captions.last() == sentence,
           QStringLiteral("Streaming caption skipped or lost characters"));
    captions.clear();
    voice.begin(QStringLiteral("demo"));
    const auto partial = QStringLiteral("（把档案合上。\nMon3tr（转向门口）");
    voice.submit(partial, false);
    expect(captions.isEmpty() && requests.size() == 1,
           QStringLiteral("Unclosed aside prematurely revealed or triggered TTS"));
    const auto withAside = partial + QStringLiteral("）桌宠🌟欢迎(停顿)你。:) ");
    QElapsedTimer asideTimer;
    asideTimer.start();
    voice.submit(withAside, true);
    // Leading aside must reveal in bubble BEFORE the first spoken segment is sent to TTS
    expect(waitUntil([&] { return !captions.isEmpty() && captions.last().contains(QStringLiteral("把档案合上")); }, 1500)
        && requests.size() == 1,
        QStringLiteral("Leading aside did not pause before launching TTS"));
    expect(waitUntil([&] { return device->received == SpeechPlayback::sampleRate; }, 4000)
        && asideTimer.elapsed() >= 1500
        && requests == QStringList{sentence, QStringLiteral("桌宠欢迎")},
        QStringLiteral("A Qwen request contained nonspoken text or skipped leading aside pause"));
    device->played = SpeechPlayback::sampleRate;
    // Mid-sentence (停顿) must pause after first spoken chunk before requesting the next
    QElapsedTimer midPause;
    midPause.start();
    expect(waitUntil([&] { return device->received == 2 * SpeechPlayback::sampleRate; }, 2500)
        && midPause.elapsed() >= 400
        && requests == QStringList{sentence, QStringLiteral("桌宠欢迎"), QStringLiteral("你。")},
        QStringLiteral("Mid-sentence aside did not insert a pause between spoken segments"));
    device->played = 2 * SpeechPlayback::sampleRate;
    expect(waitUntil([&] { return ended == 4; }) && captions.last() == withAside,
           QStringLiteral("Filtering speech changed the original bubble text"));
    QElapsedTimer pureAsideTimer;
    pureAsideTimer.start();
    captions.clear();
    voice.begin(QStringLiteral("demo"));
    voice.submit(QStringLiteral("（点头）👋 ^_^"), true);
    expect(waitUntil([&] { return ended == 5; }, 2000) && pureAsideTimer.elapsed() >= 450
        && requests.size() == 3 && captions.last() == QStringLiteral("（点头）👋 ^_^"),
        QStringLiteral("A pure-aside reply skipped its reading pause or sent a TTS request"));
    unavailable = true;
    captions.clear();
    voice.begin(QStringLiteral("demo"));
    voice.submit(sentence, true);
    expect(waitUntil([&] { return ended == 6; }) && captions == QStringList{sentence},
           QStringLiteral("A voice service error lost the text"));

    // System Speech: leading + middle + trailing asides & stopReading() cancellation
    settings.setValue(QStringLiteral("speech/useSystemVoice"), true);
    captions.clear();
    synthesizer->utterance.clear();
    synthesizer->range.reset();
    voice.begin(QStringLiteral("demo"));
    const auto sysReply = QStringLiteral("（抬头）博士，（停顿）请坐。（合上档案）");
    voice.submit(sysReply, true);
    expect(synthesizer->utterance.isEmpty(), QStringLiteral("System speech spoke before leading aside pause"));
    expect(waitUntil([&] { return synthesizer->utterance == QStringLiteral("博士，"); }, 2000)
        && captions.last().contains(QStringLiteral("（抬头）")),
        QStringLiteral("System speech did not resume after leading aside pause"));
    synthesizer->reading = false;
    expect(waitUntil([&] { return synthesizer->utterance == QStringLiteral("请坐。"); }, 2000)
        && captions.last().contains(QStringLiteral("（停顿）")),
        QStringLiteral("System speech did not pause for mid-reply aside"));
    synthesizer->reading = false;
    QElapsedTimer trailingTimer;
    trailingTimer.start();
    expect(waitUntil([&] { return ended == 7; }, 2500) && trailingTimer.elapsed() >= 600
        && captions.last() == sysReply,
        QStringLiteral("System speech did not pause for trailing aside before ending"));

    // Cancellation during active aside pause must reveal full text immediately
    captions.clear();
    voice.begin(QStringLiteral("demo"));
    const auto cancelReply = QStringLiteral("（停下手中的笔，抬起眼皮审视了你几秒）请休息。");
    voice.submit(cancelReply, true);
    voice.stopReading();
    expect(ended == 8 && captions.last() == cancelReply && !voice.busy(),
           QStringLiteral("stopReading() did not cleanly cancel an active aside pause"));
}

void testMultiPetAndRoleLibraryGrid(const QString& root)
{
    // Minimal valid 2-frame 1x1 animated GIF89a bytes
    const QByteArray tinyGif = QByteArray::fromHex(
        "47494638396101000100800000ffffff00000021ff0b4e45545343415045322e300301000000"
        "21f90400050000002c0000000001000100000202440100"
        "21f90400050000002c00000000010001000002024c01003b");
    const QString builtInGifFile = QDir(root).filePath(QStringLiteral("builtin.gif"));
    {
        QFile out(builtInGifFile);
        expect(out.open(QIODevice::WriteOnly) && out.write(tinyGif) == tinyGif.size(), QStringLiteral("Write GIF fixture"));
    }

    QImage transparentSprite(32, 32, QImage::Format_RGBA8888);
    transparentSprite.fill(Qt::transparent);
    for (int y = 12; y < 20; ++y) {
        for (int x = 12; x < 20; ++x) {
            transparentSprite.setPixelColor(x, y, Qt::cyan);
        }
    }
    QByteArray transparentPng;
    {
        QBuffer buf(&transparentPng);
        expect(buf.open(QIODevice::WriteOnly) && transparentSprite.save(&buf, "PNG"), QStringLiteral("Transparent PNG fixture"));
    }

    // Build a 2-frame moving transparent GIF (Frame 0: RED on left, Frame 1: BLUE on right)
    const auto movingGifResult = encodeLoopingGif(2, 20, [](int frameIndex) {
        QImage frame(32, 32, QImage::Format_ARGB32_Premultiplied);
        frame.fill(Qt::transparent);
        for (int y = 10; y < 22; ++y) {
            for (int x = (frameIndex == 0 ? 2 : 20); x < (frameIndex == 0 ? 12 : 30); ++x) {
                frame.setPixelColor(x, y, frameIndex == 0 ? QColor(255, 0, 0) : QColor(0, 0, 255));
            }
        }
        return frame;
    }, [](int) { return true; });
    expect(!movingGifResult.gif.isEmpty(), movingGifResult.error);

    QString error;
    RoleLibrary library;
    expect(library.open(QDir(root).filePath(QStringLiteral("multipet.sqlite3")), error), error);

    const QString staticRole = library.add(QStringLiteral("静态角色A"), transparentPng, error);
    const QString gifRole = library.add(QStringLiteral("动图角色B"), movingGifResult.gif, error);
    expect(!staticRole.isEmpty() && !gifRole.isEmpty(), error);

    // Verify independent Role Cards, Actions, and Chat Histories
    expect(library.saveRoleCard(staticRole, {{QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("identityAndStance"), QStringLiteral("我是冷静的静态角色A")}}, error), error);
    expect(library.saveRoleCard(gifRole, {{QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("identityAndStance"), QStringLiteral("我是活泼的动图角色B")}}, error), error);

    const QString clonedRole = library.duplicate(staticRole, builtInGifFile, error);
    expect(!clonedRole.isEmpty() && clonedRole != staticRole, error);
    expect(library.saveRoleCard(clonedRole, {{QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("identityAndStance"), QStringLiteral("我是修改后的独立副本C")}}, error), error);

    expect(library.shareFor(staticRole, error, false).card.value(QStringLiteral("identityAndStance")).toString()
               == QStringLiteral("我是冷静的静态角色A"),
           QStringLiteral("Editing duplicated role card mutated original role"));
    expect(library.shareFor(gifRole, error, false).card.value(QStringLiteral("identityAndStance")).toString()
               == QStringLiteral("我是活泼的动图角色B"),
           QStringLiteral("Role B card was affected"));

    expect(library.saveExchange(staticRole, QStringLiteral("hi A"), QStringLiteral("hello from A"), error), error);
    expect(library.chatHistory(gifRole, 10, error).isEmpty() && library.chatHistory(clonedRole, 10, error).isEmpty(),
           QStringLiteral("Chat history leaked across independent pets"));

    const QString asyncBackup = QDir(root).filePath(QStringLiteral("async-snapshot.sqlite3"));
    expect(RoleLibrary::backupDatabaseFile(library.databasePath(), asyncBackup, error) && QFile::exists(asyncBackup),
           QStringLiteral("Background read-only database backup failed: ") + error);

    // Verify per-role independent numerical values in SettingsStore
    QStandardPaths::setTestModeEnabled(true);
    {
        SettingsStore store;
        const QVariant prevActiveId = store.native().value(QStringLiteral("pet/activeRoleId"));
        const bool hadActiveId = store.native().contains(QStringLiteral("pet/activeRoleId"));
        const QVariant prevActiveIds = store.native().value(QStringLiteral("pet/activeRoleIds"));
        const bool hadActiveIds = store.native().contains(QStringLiteral("pet/activeRoleIds"));
        const QVariant prevMulti = store.native().value(QStringLiteral("pet/multiPetEnabled"));
        const bool hadMulti = store.native().contains(QStringLiteral("pet/multiPetEnabled"));

        store.setMultiPetEnabled(true);
        store.setActiveRoles({staticRole, gifRole, clonedRole});
        store.setActivity(staticRole, 25.0);
        store.setActivity(gifRole, 92.5);
        store.setActivity(clonedRole, 60.0);
        store.setKibble(staticRole, QStringLiteral("slow"));
        store.setKibble(gifRole, QStringLiteral("high"));
        store.setRoaming(staticRole, false);
        store.setRoaming(gifRole, true);
        store.saveRoleLayout(staticRole, QPoint(120, 340), 80);
        store.saveRoleLayout(gifRole, QPoint(520, 340), 140);
        store.setClickThrough(staticRole, true);
        store.setClickThrough(gifRole, false);

        expect(store.multiPetEnabled() && store.activeRoles() == QStringList({staticRole, gifRole, clonedRole}),
               QStringLiteral("Multi-pet active role list was not persisted"));
        expect(qFuzzyCompare(store.activity(staticRole), 25.0) && qFuzzyCompare(store.activity(gifRole), 92.5)
                   && qFuzzyCompare(store.activity(clonedRole), 60.0),
               QStringLiteral("Per-pet activity values interfered with each other"));
        expect(store.kibble(staticRole, {}) == QStringLiteral("slow")
                   && store.kibble(gifRole, {}) == QStringLiteral("high")
                   && store.kibble(clonedRole, {}).isEmpty(),
               QStringLiteral("Per-pet kibble values interfered with each other"));
        expect(!store.roaming(staticRole, {}) && store.roaming(gifRole, {}),
               QStringLiteral("Per-pet roaming states interfered with each other"));
        expect(store.scale(staticRole) == 80 && store.scale(gifRole) == 140 && store.scale(clonedRole) == 100,
               QStringLiteral("Per-pet scale values interfered with each other"));
        expect(store.clickThrough(staticRole) && !store.clickThrough(gifRole),
               QStringLiteral("Per-pet click-through interfered with each other"));

        store.removeRole(clonedRole);
        expect(store.activeRoles() == QStringList({staticRole, gifRole})
                   && qFuzzyCompare(store.activity(staticRole), 25.0)
                   && qFuzzyCompare(store.activity(gifRole), 92.5),
               QStringLiteral("Removing one role affected remaining pets"));

        store.removeRole(staticRole);
        store.removeRole(gifRole);
        if (hadActiveId) {
            store.native().setValue(QStringLiteral("pet/activeRoleId"), prevActiveId);
        } else {
            store.native().remove(QStringLiteral("pet/activeRoleId"));
        }
        if (hadActiveIds) {
            store.native().setValue(QStringLiteral("pet/activeRoleIds"), prevActiveIds);
        } else {
            store.native().remove(QStringLiteral("pet/activeRoleIds"));
        }
        if (hadMulti) {
            store.native().setValue(QStringLiteral("pet/multiPetEnabled"), prevMulti);
        } else {
            store.native().remove(QStringLiteral("pet/multiPetEnabled"));
        }
        store.native().sync();
    }
    QStandardPaths::setTestModeEnabled(false);

    // Verify Grid Role Library layout (left-to-right, top-to-bottom), GIF vs Static preview, and multi-pet selection
    QStringList liveSelection;
    bool liveMulti = false;
    RoleLibraryContext ctx;
    ctx.builtInAsset = builtInGifFile;
    ctx.preferredRole = clonedRole;
    ctx.activeRoles = {clonedRole};
    ctx.multiPetEnabled = false;
    ctx.selectionChanged = [&](const QStringList& selected, bool multi) {
        liveSelection = selected;
        liveMulti = multi;
    };

    QTimer::singleShot(60, qApp, [&] {
        QDialog* activeDialog = nullptr;
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (auto* dlg = qobject_cast<QDialog*>(widget); dlg && dlg->isVisible()) {
                activeDialog = dlg;
                break;
            }
        }
        expect(activeDialog != nullptr, QStringLiteral("Role Library dialog did not open"));

        auto* staticTile = activeDialog->findChild<QWidget*>(QStringLiteral("roleTile_") + staticRole);
        auto* gifTile = activeDialog->findChild<QWidget*>(QStringLiteral("roleTile_") + gifRole);
        auto* clonedTile = activeDialog->findChild<QWidget*>(QStringLiteral("roleTile_") + clonedRole);
        expect(staticTile && gifTile && clonedTile, QStringLiteral("Missing role tiles in grid"));

        expect(waitUntil([&] {
            return staticTile->property("assetLoaded").toBool()
                && gifTile->property("assetLoaded").toBool()
                && clonedTile->property("assetLoaded").toBool();
        }), QStringLiteral("Async Role Library card hydration did not complete"));

        // Verify GIF roles report animatedGif=true and static PNG roles report animatedGif=false
        expect(gifTile->property("animatedGif").toBool(),
               QStringLiteral("GIF roles were not recognized as animated in Role Library"));
        expect(!staticTile->property("animatedGif").toBool() && !clonedTile->property("animatedGif").toBool(),
               QStringLiteral("Static PNG roles should display as static images"));

        // Verify no checkerboard (uniform window surface around transparent sprite)
        const QImage staticGrab = staticTile->grab().toImage();
        expect(staticGrab.pixel(28, 65) == staticGrab.pixel(40, 65),
               QStringLiteral("Role Library tile painted a checkerboard pattern behind transparent character"));

        // Verify zero GIF frame ghosting on backing store across multiple frames:
        // Let the 2-frame moving GIF play for several frame transitions, then grab the live backing store
        // and verify red (Frame 0 left) and blue (Frame 1 right) NEVER coexist on the same grabbed frame!
        for (int sample = 0; sample < 6; ++sample) {
            QEventLoop tick;
            QTimer::singleShot(35, &tick, &QEventLoop::quit);
            tick.exec();
            const QImage frameGrab = gifTile->grab().toImage();
            bool hasRedLeft = false;
            bool hasBlueRight = false;
            for (int y = 45; y < 135; ++y) {
                for (int x = 20; x < frameGrab.width() / 2 - 8; ++x) {
                    const QColor c = frameGrab.pixelColor(x, y);
                    if (c.red() > 200 && c.green() < 60 && c.blue() < 60) {
                        hasRedLeft = true;
                    }
                }
                for (int x = frameGrab.width() / 2 + 8; x < frameGrab.width() - 20; ++x) {
                    const QColor c = frameGrab.pixelColor(x, y);
                    if (c.blue() > 200 && c.red() < 60 && c.green() < 60) {
                        hasBlueRight = true;
                    }
                }
            }
            expect(!(hasRedLeft && hasBlueRight),
                   QStringLiteral("GIF playback ghosted previous frame onto current frame in Role Library card"));
        }

        // Verify left-to-right ordering on the first row (entries order: clonedRole, gifRole, staticRole)
        expect(clonedTile->y() == gifTile->y() && clonedTile->x() < gifTile->x()
                   && gifTile->x() < staticTile->x(),
               QStringLiteral("Role tiles are not arranged left-to-right"));

        // Enable multi-pet checkbox and select both gifRole and staticRole
        auto* multiCheck = activeDialog->findChild<QCheckBox*>(QStringLiteral("allowMultiplePets"));
        auto* toggleGif = activeDialog->findChild<QPushButton*>(QStringLiteral("toggleRole_") + gifRole);
        auto* toggleStatic = activeDialog->findChild<QPushButton*>(QStringLiteral("toggleRole_") + staticRole);
        expect(multiCheck && toggleGif && toggleStatic, QStringLiteral("Missing multi-pet controls in Role Library"));

        multiCheck->setChecked(true);
        toggleGif->click();
        toggleStatic->click();
        expect(liveMulti && liveSelection.size() == 3 && liveSelection.contains(gifRole) && liveSelection.contains(staticRole),
               QStringLiteral("Multi-pet selection in Role Library did not activate multiple pets"));

        activeDialog->accept();
    });

    const auto choice = manageRoles(library, ctx, nullptr);
    expect(choice.multiPetEnabled && choice.activeRoles.size() == 3
               && choice.activeRoles.contains(clonedRole)
               && choice.activeRoles.contains(gifRole)
               && choice.activeRoles.contains(staticRole),
           QStringLiteral("Role Library dialog did not return the chosen multiple desktop pets"));
}

void testWindowSpaceAndLayerPolicy()
{
    {
        SettingsStore store;
        // Clean up any leftover fixed test keys
        store.removeRole(QStringLiteral("space-role-a"));
        store.removeRole(QStringLiteral("space-role-b"));
        store.removeRole(QStringLiteral("space-role-c"));

        const bool hadGlobalSpaces = store.native().contains(QStringLiteral("pet/followAllSpaces"));
        const QVariant prevGlobalSpaces = store.native().value(QStringLiteral("pet/followAllSpaces"));
        const bool hadGlobalTop = store.native().contains(QStringLiteral("pet/alwaysOnTop"));
        const QVariant prevGlobalTop = store.native().value(QStringLiteral("pet/alwaysOnTop"));
        const bool hadTransitMode = store.native().contains(QStringLiteral("pet/spaceTransitMode"));
        const QVariant prevTransitMode = store.native().value(QStringLiteral("pet/spaceTransitMode"));
        store.native().remove(QStringLiteral("pet/followAllSpaces"));
        store.native().remove(QStringLiteral("pet/alwaysOnTop"));
        store.native().remove(QStringLiteral("pet/spaceTransitMode"));

        expect(store.spaceTransitMode() == SpaceTransitMode::MixedRandom,
               QStringLiteral("Default cross-desktop entry mode should be MixedRandom"));
        store.setSpaceTransitMode(SpaceTransitMode::JumpOnly);
        expect(store.spaceTransitMode() == SpaceTransitMode::JumpOnly,
               QStringLiteral("JumpOnly space transit mode was not persisted"));
        store.setSpaceTransitMode(SpaceTransitMode::WalkOnly);
        expect(store.spaceTransitMode() == SpaceTransitMode::WalkOnly,
               QStringLiteral("WalkOnly space transit mode was not persisted"));
        store.setSpaceTransitMode(SpaceTransitMode::MixedRandom);
        expect(store.spaceTransitMode() == SpaceTransitMode::MixedRandom,
               QStringLiteral("MixedRandom space transit mode was not persisted"));

        const QString roleA = QStringLiteral("test-space-a-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString roleB = QStringLiteral("test-space-b-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString roleC = QStringLiteral("test-space-c-") + QUuid::createUuid().toString(QUuid::WithoutBraces);

        expect(store.followAllSpaces(roleA) && store.alwaysOnTop(roleA),
               QStringLiteral("Default space/layer policy should follow spaces and float on top"));

        store.setFollowAllSpaces(roleA, false);
        store.setAlwaysOnTop(roleA, false);
        store.setFollowAllSpaces(roleB, true);
        store.setAlwaysOnTop(roleB, true);

        expect(!store.followAllSpaces(roleA) && !store.alwaysOnTop(roleA),
               QStringLiteral("Role A space/layer policy was not persisted"));
        expect(store.followAllSpaces(roleB) && store.alwaysOnTop(roleB),
               QStringLiteral("Role A space/layer policy leaked to Role B"));

        store.native().setValue(QStringLiteral("pet/followAllSpaces"), false);
        store.native().setValue(QStringLiteral("pet/alwaysOnTop"), false);
        expect(!store.followAllSpaces(roleC) && !store.alwaysOnTop(roleC),
               QStringLiteral("Global default fallback for space/layer policy did not apply to unset role"));
        expect(store.followAllSpaces(roleB) && store.alwaysOnTop(roleB),
               QStringLiteral("Explicit per-role space/layer policy was overridden by global default"));

        store.removeRole(roleA);
        store.removeRole(roleB);
        store.removeRole(roleC);
        if (hadGlobalSpaces) {
            store.native().setValue(QStringLiteral("pet/followAllSpaces"), prevGlobalSpaces);
        } else {
            store.native().remove(QStringLiteral("pet/followAllSpaces"));
        }
        if (hadGlobalTop) {
            store.native().setValue(QStringLiteral("pet/alwaysOnTop"), prevGlobalTop);
        } else {
            store.native().remove(QStringLiteral("pet/alwaysOnTop"));
        }
        if (hadTransitMode) {
            store.native().setValue(QStringLiteral("pet/spaceTransitMode"), prevTransitMode);
        } else {
            store.native().remove(QStringLiteral("pet/spaceTransitMode"));
        }
        store.native().sync();
    }

    QString error;
    auto assetA = decodePetAsset(image(), error);
    auto assetB = decodePetAsset(image(), error);
    expect(assetA.has_value() && assetB.has_value(), error);

    PetWindow petA;
    PetWindow petB;
    petA.setWindowOpacity(0.02);
    petB.setWindowOpacity(0.02);
    petA.setAsset(std::move(*assetA), 100);
    petB.setAsset(std::move(*assetB), 100);
    petA.placeAt(QPoint(120, 120), false);
    petB.placeAt(QPoint(260, 120), false);

    SpeechBubble overlayA(&petA);
    overlayA.setWindowOpacity(0.02);
    overlayA.beginReply();
    overlayA.setMessage(QStringLiteral("Layer sync"));

    petA.show();
    overlayA.place(QRect(0, 0, 800, 600), petA.frameGeometry(), petA.frameGeometry());
    overlayA.show();
    petB.show();

    MacWindowLayerCoordinator coordinator;
    coordinator.registerPet(petA, true, true);
    coordinator.registerPet(petB, false, false);

    expect(petA.alwaysOnTop() && petA.windowFlags().testFlag(Qt::WindowStaysOnTopHint),
           QStringLiteral("Topmost pet lost WindowStaysOnTopHint"));
    expect(!petB.alwaysOnTop() && !petB.windowFlags().testFlag(Qt::WindowStaysOnTopHint),
           QStringLiteral("Second-layer pet retained WindowStaysOnTopHint"));
    expect(coordinator.isFollowingAllSpaces(petA) && !coordinator.isFollowingAllSpaces(petB),
           QStringLiteral("Coordinator did not record per-pet followAllSpaces policies"));

    QList<PetWindow*> dispatchedPets;
    int dispatchedDelta = 0;
    coordinator.spaceSwitched = [&](const QList<PetWindow*>& pets, int delta) {
        dispatchedPets = pets;
        dispatchedDelta = delta;
    };
    coordinator.triggerSpaceSwitchForTesting(+1);
    expect(dispatchedPets.size() == 1 && dispatchedPets.constFirst() == &petA && dispatchedDelta == 1,
           QStringLiteral("Space switch dispatched non-following pet or wrong delta"));

    if (QGuiApplication::platformName() == QLatin1String("cocoa")) {
        QEventLoop settle;
        QTimer::singleShot(40, &settle, &QEventLoop::quit);
        settle.exec();

        expect(macWindowIsSpaceManaged(petA) && macWindowIsSpaceManaged(petB) && macWindowIsSpaceManaged(overlayA),
               QStringLiteral("Both floating and normal pets + overlays must have NSWindowCollectionBehaviorManaged set"));
        expect(!macWindowMovesToActiveSpace(petA) && macWindowMovesToActiveSpace(overlayA),
               QStringLiteral("Child overlays must retain MoveToActiveSpace so showing hidden panels never switches Spaces"));
        expect(!macWindowJoinsAllSpaces(petA) && !macWindowJoinsAllSpaces(petB) && !macWindowJoinsAllSpaces(overlayA),
               QStringLiteral("Normal-desktop pets and overlays must not set CanJoinAllSpaces"));
        expect(macWindowNativeLevel(petA) > macWindowNativeLevel(petB) && macWindowNativeLevel(petB) == 0,
               QStringLiteral("Native window levels did not separate topmost vs second-layer pets"));
        expect(macWindowNativeLevel(overlayA) == macWindowNativeLevel(petA),
               QStringLiteral("Companion overlay level did not match topmost pet"));

        // Verify hidden PetTether aim overlay stays MoveToActiveSpace | Managed across Space relocation
        QWidget tetherOverlay(&petA, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                                         | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
        tetherOverlay.setObjectName(QStringLiteral("petTetherVisual"));
        tetherOverlay.setAttribute(Qt::WA_TranslucentBackground);
        tetherOverlay.setAttribute(Qt::WA_ShowWithoutActivating);
        tetherOverlay.setAttribute(Qt::WA_MacAlwaysShowToolWindow);
        tetherOverlay.setGeometry(0, 0, 200, 200);
        tetherOverlay.show();
        tetherOverlay.hide();

        coordinator.movePetToActiveSpace(petA, QPoint(40, 120), false);
        expect(macWindowIsSpaceManaged(petA) && !macWindowJoinsAllSpaces(petA) && macWindowNativeLevel(petA) > 0,
               QStringLiteral("Relocating topmost pet onto normal Space lost Managed flag or altered floating level"));

        tetherOverlay.show();
        tetherOverlay.raise();
        expect(macWindowIsSpaceManaged(tetherOverlay) && macWindowMovesToActiveSpace(tetherOverlay)
                   && !macWindowJoinsAllSpaces(tetherOverlay),
               QStringLiteral("Showing hidden PetTether overlay failed to keep MoveToActiveSpace | Managed"));
        tetherOverlay.hide();

        // Dynamically flip policies on live Cocoa windows
        coordinator.updatePetPolicy(petA, false, false);
        coordinator.updatePetPolicy(petB, true, true);

        QTimer::singleShot(120, &settle, &QEventLoop::quit);
        settle.exec();

        expect(!coordinator.isFollowingAllSpaces(petA) && coordinator.isFollowingAllSpaces(petB),
               QStringLiteral("Dynamic followAllSpaces policy swap failed"));
        expect(macWindowIsSpaceManaged(petA) && macWindowIsSpaceManaged(petB) && !macWindowJoinsAllSpaces(petB),
               QStringLiteral("Policy update lost Managed behavior or wrongly enabled CanJoinAllSpaces"));
        expect(macWindowNativeLevel(petA) == 0 && macWindowNativeLevel(overlayA) == 0,
               QStringLiteral("Disabling topmost did not drop pet and NSPanel overlay to NSNormalWindowLevel"));
        expect(macWindowNativeLevel(petB) > 0,
               QStringLiteral("Enabling topmost did not raise pet to NSFloatingWindowLevel"));

        dispatchedPets.clear();
        coordinator.triggerSpaceSwitchForTesting(-1);
        expect(dispatchedPets.size() == 1 && dispatchedPets.constFirst() == &petB && dispatchedDelta == -1,
               QStringLiteral("Flipped Space switch did not follow updated policy"));
    }

    coordinator.unregisterPet(petA);
    coordinator.unregisterPet(petB);
}

void testSpaceTransitRulesAndAnimation()
{
    const QRect workArea(0, 25, 1440, 875);
    const QSize petSize(140, 140);

    // 1. Rightward switch (+1): enters from left, lands in left near-end
    const SpaceTransitPlan rightSwitch = planSpaceTransit({workArea, petSize, QPoint(1100, 420), +1, 82.0});
    expect(rightSwitch.side == SpaceEntrySide::FromLeft,
           QStringLiteral("Switching rightward should enter from the left edge"));
    expect(rightSwitch.startPos.x() < workArea.left(),
           QStringLiteral("Left entry should start slightly outside the left border"));
    expect(rightSwitch.landingPos.x() >= workArea.left() + 28 && rightSwitch.landingPos.x() <= workArea.left() + 200,
           QStringLiteral("Rightward Space switch should land at the left near-end of the new desktop"));

    // 2. Leftward switch (-1, -2) even when pet stood on the LEFT side:
    //    must ALWAYS enter from the RIGHT border and land in the RIGHT near-end!
    for (const int leftDelta : {-1, -2}) {
        const SpaceTransitPlan leftSwitch = planSpaceTransit({workArea, petSize, QPoint(60, 420), leftDelta, 82.0});
        expect(leftSwitch.side == SpaceEntrySide::FromRight,
               QStringLiteral("Switching leftward must always enter from the right edge regardless of previous X"));
        expect(leftSwitch.startPos.x() > workArea.right() + 1 - petSize.width(),
               QStringLiteral("Right entry should start slightly outside the right border"));
        expect(leftSwitch.landingPos.x() >= workArea.right() + 1 - petSize.width() - 200
                   && leftSwitch.landingPos.x() <= workArea.right() + 1 - petSize.width() - 28,
               QStringLiteral("Leftward Space switch should land at the right near-end of the new desktop"));
    }

    // 3. Multi-space jump (+3) increases urgency & overshoot vs single-space (+1)
    const SpaceTransitPlan fastJump = planSpaceTransit({workArea, petSize, QPoint(700, 420), +3, 82.0});
    expect(fastJump.overshootPx > rightSwitch.overshootPx && fastJump.durationSeconds < rightSwitch.durationSeconds,
           QStringLiteral("Multi-space jump did not scale hop urgency and overshoot"));

    // 4. Multi-pet proximity ordering and anti-overlap
    const auto groupPlans = planSpaceTransits({
        {workArea, petSize, QPoint(150, 420), +1, 75.0},
        {workArea, petSize, QPoint(950, 420), +1, 75.0},
        {workArea, petSize, QPoint(500, 420), +1, 75.0},
    });
    expect(groupPlans.size() == 3, QStringLiteral("Group space transit returned wrong plan count"));
    expect(groupPlans[1].delaySeconds < groupPlans[2].delaySeconds
               && groupPlans[2].delaySeconds < groupPlans[0].delaySeconds,
           QStringLiteral("Pet closest to the shared border should enter first"));
    for (size_t i = 0; i < groupPlans.size(); ++i) {
        expect(groupPlans[i].landingPos.x() <= workArea.left() + 200,
               QStringLiteral("Grouped pet landed outside near-end band"));
        for (size_t j = i + 1; j < groupPlans.size(); ++j) {
            const double dx = groupPlans[i].landingPos.x() - groupPlans[j].landingPos.x();
            const double dy = groupPlans[i].landingPos.y() - groupPlans[j].landingPos.y();
            expect(std::hypot(dx, dy) >= 60.0,
                   QStringLiteral("Multi-pet near-end landings overlapped"));
        }
    }

    // 5. 4-phase curve sampling: peak hop height, forward lean, overshoot, braking counter-lean, exact final settle
    double minY = 1e9;
    double maxX = -1e9;
    double maxForwardLean = -1e9;
    double minBrakingLean = 1e9;
    const double totalWindow = rightSwitch.delaySeconds + rightSwitch.durationSeconds + 0.05;
    for (int step = 0; step <= 80; ++step) {
        const double t = totalWindow * step / 80.0;
        const SpaceTransitSample s = sampleSpaceTransit(rightSwitch, t);
        if (!s.waitingDelay && !s.finished) {
            minY = qMin(minY, s.position.y());
            maxX = qMax(maxX, s.position.x());
            maxForwardLean = qMax(maxForwardLean, double(s.leanDegrees));
            minBrakingLean = qMin(minBrakingLean, double(s.leanDegrees));
        }
    }
    expect(minY < rightSwitch.landingPos.y() - 4.0, QStringLiteral("Space transit did not produce upward hop arc"));
    expect(maxX > rightSwitch.landingPos.x() + 3.0, QStringLiteral("Space transit lacked elastic braking overshoot"));
    expect(maxForwardLean > 4.0 && minBrakingLean < -1.0,
           QStringLiteral("Space transit lacked velocity-coupled forward and braking lean"));

    const SpaceTransitSample done = sampleSpaceTransit(rightSwitch, totalWindow);
    expect(done.finished && done.position == rightSwitch.landingPos
               && qFuzzyIsNull(done.leanDegrees)
               && qFuzzyCompare(done.scaleX, 1.0)
               && qFuzzyCompare(done.scaleY, 1.0),
           QStringLiteral("Completed Space transit did not settle cleanly at rest pose"));

    // 6. Verify all 3 configurable entry modes (MixedRandom, JumpOnly, WalkOnly) and WalkOnly trajectory
    QRandomGenerator deterministicRng(20261125U);
    int sawJump = 0;
    int sawWalk = 0;
    for (int i = 0; i < 64; ++i) {
        if (resolveSpaceMotionStyle(SpaceTransitMode::MixedRandom, deterministicRng) == SpaceMotionStyle::Jump) {
            ++sawJump;
        } else {
            ++sawWalk;
        }
        expect(resolveSpaceMotionStyle(SpaceTransitMode::JumpOnly, deterministicRng) == SpaceMotionStyle::Jump,
               QStringLiteral("JumpOnly mode must always select Jump"));
        expect(resolveSpaceMotionStyle(SpaceTransitMode::WalkOnly, deterministicRng) == SpaceMotionStyle::Walk,
               QStringLiteral("WalkOnly mode must always select Walk"));
    }
    expect(sawJump > 0 && sawWalk > 0, QStringLiteral("MixedRandom mode should randomly combine Jump and Walk"));

    const SpaceTransitPlan walkLeftPlan = planSpaceTransit(
        {workArea, petSize, QPoint(60, 420), -1, 82.0, SpaceMotionStyle::Walk});
    expect(walkLeftPlan.side == SpaceEntrySide::FromRight && walkLeftPlan.style == SpaceMotionStyle::Walk,
           QStringLiteral("WalkOnly leftward transit plan mismatch"));
    double prevWalkX = walkLeftPlan.startPos.x();
    const double walkWindow = walkLeftPlan.delaySeconds + walkLeftPlan.durationSeconds + 0.05;
    for (int step = 0; step <= 60; ++step) {
        const double t = walkWindow * step / 60.0;
        const SpaceTransitSample s = sampleSpaceTransit(walkLeftPlan, t);
        expect(qFuzzyCompare(s.position.y(), walkLeftPlan.landingPos.y()),
               QStringLiteral("WalkOnly space transit should move along flat Y without jumping"));
        expect(qFuzzyIsNull(s.leanDegrees) && qFuzzyCompare(s.scaleX, 1.0) && qFuzzyCompare(s.scaleY, 1.0),
               QStringLiteral("WalkOnly space transit should keep upright rest scale"));
        expect(s.position.x() <= prevWalkX + 1e-6 && s.position.x() >= walkLeftPlan.landingPos.x() - 1e-6,
               QStringLiteral("WalkOnly leftward transit must move monotonically right-to-left without overshoot"));
        prevWalkX = s.position.x();
    }
    const SpaceTransitSample walkDone = sampleSpaceTransit(walkLeftPlan, walkWindow);
    expect(walkDone.finished && walkDone.position == walkLeftPlan.landingPos,
           QStringLiteral("WalkOnly transit did not finish at landingPos"));
}

void testCompanionRhythmAndFocusPolicy()
{
    {
        RhythmContext ctx;
        ctx.userMode = CompanionMode::Auto;
        expect(resolveEffectiveRhythm(ctx) == EffectiveRhythm::Daily,
               QStringLiteral("Auto mode with no activity or fullscreen should resolve to Daily"));

        ctx.appActive = true;
        expect(resolveEffectiveRhythm(ctx) == EffectiveRhythm::Play,
               QStringLiteral("Auto mode with AuraPet foreground should resolve to Play"));

        ctx.appActive = false;
        ctx.recentInteraction = true;
        expect(resolveEffectiveRhythm(ctx) == EffectiveRhythm::Play,
               QStringLiteral("Auto mode with recent interaction should stay in Play"));

        ctx.recentInteraction = false;
        ctx.externalFullScreen = true;
        expect(resolveEffectiveRhythm(ctx) == EffectiveRhythm::Focus,
               QStringLiteral("Auto mode with external fullscreen app should switch to Focus"));

        ctx.userMode = CompanionMode::Play;
        ctx.externalFullScreen = true;
        expect(resolveEffectiveRhythm(ctx) == EffectiveRhythm::Play,
               QStringLiteral("Manual Play should not be overridden by external fullscreen"));

        ctx.focusTimerActive = true;
        expect(resolveEffectiveRhythm(ctx) == EffectiveRhythm::Focus,
               QStringLiteral("Active Focus timer must override manual Play to Focus"));

        ctx.focusTimerActive = false;
        ctx.breakTimerActive = true;
        ctx.userMode = CompanionMode::Focus;
        expect(resolveEffectiveRhythm(ctx) == EffectiveRhythm::Daily,
               QStringLiteral("Break timer should lift rhythm to Daily"));

        ctx.doNotDisturb = true;
        expect(resolveEffectiveRhythm(ctx) == EffectiveRhythm::Focus,
               QStringLiteral("Do-Not-Disturb must always force Focus rhythm"));
    }

    {
        QRandomGenerator rng(20261125U);
        PetMotionRules rules(rng);
        const QPointF anchor(600.0, 420.0);
        rules.resumeAt(anchor, 0.2);
        rules.setAnchor(anchor);
        rules.setRhythm(EffectiveRhythm::Focus);
        rules.setActivity(95.0);
        const MotionSpace space{QRectF(0, 0, 1440, 900), QSizeF(140, 140), QPointF(630, 440)};
        double maxDist = 0.0;
        for (int step = 0; step < 300; ++step) {
            rules.advance(0.1, space);
            const double d = std::hypot(rules.position().x() - anchor.x(), rules.position().y() - anchor.y());
            maxDist = qMax(maxDist, d);
            expect(rules.state() != MotionState::Follow && rules.state() != MotionState::Flee
                       && rules.state() != MotionState::EdgePlay,
                   QStringLiteral("Focus rhythm must not trigger Follow, Flee, or EdgePlay"));
        }
        expect(maxDist <= 102.0,
               QStringLiteral("Focus rhythm drifted beyond anchor radius: %1").arg(maxDist));
    }

    {
        SettingsStore store;
        const QString roleA = QStringLiteral("test-rhythm-a-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString roleB = QStringLiteral("test-rhythm-b-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        const bool hadPolicy = store.native().contains(QStringLiteral("focus/motionPolicy"));
        const QVariant prevPolicy = store.native().value(QStringLiteral("focus/motionPolicy"));

        expect(store.companionMode(roleA, {}) == CompanionMode::Auto,
               QStringLiteral("Unset role should default to CompanionMode::Auto"));
        expect(store.companionMode(roleA, QStringLiteral("high")) == CompanionMode::Play,
               QStringLiteral("Legacy high kibble preset should migrate to Play"));
        expect(store.companionMode(roleA, QStringLiteral("slow")) == CompanionMode::Focus,
               QStringLiteral("Legacy slow kibble preset should migrate to Focus"));

        store.setCompanionMode(roleA, CompanionMode::Play);
        store.setCompanionMode(roleB, CompanionMode::Focus);
        expect(store.companionMode(roleA) == CompanionMode::Play
                   && store.companionMode(roleB) == CompanionMode::Focus,
               QStringLiteral("Per-role companionMode values interfered or failed to persist"));

        store.native().remove(QStringLiteral("focus/motionPolicy"));
        expect(store.focusMotionPolicy() == FocusMotionPolicy::GentleMicroMove,
               QStringLiteral("Default FocusMotionPolicy should be GentleMicroMove"));
        store.setFocusMotionPolicy(FocusMotionPolicy::CompletelyStill);
        expect(store.focusMotionPolicy() == FocusMotionPolicy::CompletelyStill,
               QStringLiteral("FocusMotionPolicy::CompletelyStill was not persisted"));
        store.setFocusMotionPolicy(FocusMotionPolicy::GentleMicroMove);
        expect(store.focusMotionPolicy() == FocusMotionPolicy::GentleMicroMove,
               QStringLiteral("FocusMotionPolicy::GentleMicroMove was not persisted"));

        store.removeRole(roleA);
        store.removeRole(roleB);
        if (hadPolicy) {
            store.native().setValue(QStringLiteral("focus/motionPolicy"), prevPolicy);
        } else {
            store.native().remove(QStringLiteral("focus/motionPolicy"));
        }
        store.native().sync();
    }
}
#endif
}

int main(int argc, char** argv)
{
    DesktopApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.value(1) == QStringLiteral("--forward")) {
        QString error;
        return app.claimProfile(args.value(2), args.value(3), true, error) == DesktopApplication::Instance::Forwarded ? 0 : 1;
    }
#ifdef Q_OS_MACOS
    if (args.value(1) == QStringLiteral("--rhythm-smoke")) {
        testCompanionRhythmAndFocusPolicy();
        qInfo("Companion rhythm resolution, Focus anchor micro-move clamping, and SettingsStore migration passed");
        return 0;
    }
    if (args.value(1) == QStringLiteral("--cocoa-layer-smoke")) {
        testSpaceTransitRulesAndAnimation();
        testWindowSpaceAndLayerPolicy();
        qInfo("Native Cocoa window Space re-homing, transit choreography, and Second-Layer transitions passed");
        return 0;
    }
    if (args.value(1) == QStringLiteral("--speech-smoke")) {
        QTemporaryDir speechFixture;
        expect(speechFixture.isValid(), QStringLiteral("Cannot create speech test directory"));
        testSpeechText();
        testVoiceCaptions(speechFixture.path());
        qInfo("Speech text filtering, adaptive aside pause durations, and dual-engine caption/audio rhythm passed");
        return 0;
    }
#endif
    QTemporaryDir fixture;
    expect(fixture.isValid(), QStringLiteral("Cannot create test directory"));
    OverlayInteraction overlay;
    using Esc = OverlayInteraction::Escape;
    expect(overlay.escape(false, false, false) == Esc::Ignore, QStringLiteral("Idle Escape must be harmless"));
    overlay.input = OverlayInteraction::Input::Visible;
    expect(overlay.escape(true, true, true) == Esc::Ignore && overlay.escape(false, true, true) == Esc::StopReply
        && overlay.escape(false, false, true) == Esc::StopSpeech && overlay.escape(false, false, false) == Esc::CloseInput,
        QStringLiteral("Unexpected Escape priority while composing or speaking"));

    // Verify declarative interaction projection: activity meter setting & drag/aim suppression
    {
        OverlayInteraction state;
        state.input = OverlayInteraction::Input::Visible;
        state.activityMeterEnabled = true;
        auto view = state.resolve();
        expect(view.showInput && view.showActivityMeter && view.allowActivityFlash,
               QStringLiteral("Hover/focus with meter enabled should reveal input and activity meter"));

        // Disabling activity meter hides meter & flash across all interactions while keeping input usable
        state.activityMeterEnabled = false;
        view = state.resolve();
        expect(view.showInput && !view.showActivityMeter && !view.allowActivityFlash,
               QStringLiteral("Disabled activity meter must never show or flash"));

        // Left-button drag suppresses both input box and activity meter for immersive dragging
        state.activityMeterEnabled = true;
        state.dragging = true;
        view = state.resolve();
        expect(!view.canOpenInput && !view.showInput && !view.showActivityMeter && !view.allowActivityFlash,
               QStringLiteral("Dragging pet must cancel input and activity meter"));

        // Releasing drag restores normal eligibility
        state.dragging = false;
        view = state.resolve();
        expect(view.canOpenInput && view.showInput && view.showActivityMeter,
               QStringLiteral("Ending drag should restore normal overlay projection"));

        // Right-button tether aiming suppresses both input and activity meter
        state.aiming = true;
        view = state.resolve();
        expect(!view.canOpenInput && !view.showInput && !view.showActivityMeter && !view.allowActivityFlash,
               QStringLiteral("Aiming tether must suppress input and activity meter"));
    }
    testStorage(fixture.path());
    testPackages(fixture.path());
    testRandomSpeech(fixture.path());
    testInstance(app, fixture.path());
#ifdef Q_OS_MACOS
    testChatPrivacy(fixture.path());
    testLocalKey();
    testSpeechText();
    testSpeechBubble();
    testVoiceCaptions(fixture.path());
    testMultiPetAndRoleLibraryGrid(fixture.path());
    testSpaceTransitRulesAndAnimation();
    testWindowSpaceAndLayerPolicy();
    testCompanionRhythmAndFocusPolicy();
#endif
    qInfo("Upgrade and restore, scoped data deletion, package safety and cancellation, and single-instance forwarding passed");
}
