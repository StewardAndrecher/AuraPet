#include "role_library.h"
#include "pet_asset.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
#include <QVersionNumber>

#include <algorithm>

namespace {
constexpr int schemaVersion = 1;

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool updateRole(QSqlQuery& query, QString& error)
{
    if (!query.exec()) {
        error = query.lastError().text();
        return false;
    }
    if (query.numRowsAffected() != 1) {
        error = QStringLiteral("这个角色已经不在角色库里。");
        return false;
    }
    return true;
}

bool sql(QSqlQuery& query, const QString& statement, QString& error)
{
    if (query.exec(statement)) {
        return true;
    }
    error = query.lastError().text();
    return false;
}

QString backupPath(const QString& databasePath, const QString& reason)
{
    const QDir folder(QFileInfo(databasePath).absoluteDir().filePath(QStringLiteral("backups")));
    return folder.filePath(QStringLiteral("%1-%2-%3.sqlite3").arg(reason,
        QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")), newId().left(8)));
}

bool snapshot(const QSqlDatabase& db, const QString& path, QString& error)
{
    if (QFileInfo::exists(path) || !QDir().mkpath(QFileInfo(path).absolutePath())) {
        error = QStringLiteral("备份目标已存在，或无法创建备份目录。");
        return false;
    }
    QSqlQuery query(db);
    query.prepare(QStringLiteral("VACUUM INTO ?"));
    query.addBindValue(path);
    if (!query.exec()) {
        error = query.lastError().text();
        QFile::remove(path);
        return false;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

bool checkBackup(const QSqlDatabase& db, QString& error)
{
    QSqlQuery query(db);
    if (!sql(query, QStringLiteral("PRAGMA quick_check"), error) || !query.next()
        || query.value(0).toString() != QStringLiteral("ok")
        || !sql(query, QStringLiteral("PRAGMA user_version"), error) || !query.next()
        || query.value(0).toInt() > schemaVersion
        || !sql(query, QStringLiteral("SELECT id,name,asset FROM roles LIMIT 0"), error)
        || !sql(query, QStringLiteral("SELECT role_id,speaker,text FROM chat_messages LIMIT 0"), error)) {
        error = QStringLiteral("此文件不是完整、兼容的 AuraPet 角色库备份。");
        return false;
    }
    return true;
}
}

RoleLibrary::~RoleLibrary()
{
    const auto connection = database_.connectionName();
    if (!connection.isEmpty()) {
        database_.close();
        database_ = QSqlDatabase{};
        QSqlDatabase::removeDatabase(connection);
    }
}

QString RoleLibrary::builtInId()
{
    return QStringLiteral("builtin-kaimao");
}

QString RoleLibrary::builtInName()
{
#if AURAPET_RELEASE_ASSETS
    return QStringLiteral("光团");
#else
    return QStringLiteral("凯猫");
#endif
}

QString RoleLibrary::legacyId()
{
    return QStringLiteral("legacy-active-pet");
}

bool RoleLibrary::open(const QString& databasePath, QString& error)
{
    const bool existing = QFileInfo(databasePath).size() > 0;
    database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), newId());
    database_.setDatabaseName(databasePath);
    database_.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=3000"));
    if (!database_.open()) {
        error = database_.lastError().text();
        return false;
    }
    QSqlQuery query(database_);
    if (!sql(query, QStringLiteral("PRAGMA user_version"), error) || !query.next()) {
        return false;
    }
    const int current = query.value(0).toInt();
    query.finish();
    if (current > schemaVersion) {
        error = QStringLiteral("角色库由更高版本的 AuraPet 创建，请更新应用后再打开。");
        return false;
    }
    if (current < schemaVersion) {
        if (existing && !snapshot(database_, backupPath(databasePath, QStringLiteral("before-upgrade")), error)) {
            error.prepend(QStringLiteral("升级前备份失败："));
            return false;
        }
        if (!database_.transaction()) {
            error = database_.lastError().text();
            return false;
        }
        if (!migrateSchema(error) || !sql(query, QStringLiteral("PRAGMA user_version = %1").arg(schemaVersion), error)
            || !database_.commit()) {
            if (error.isEmpty()) {
                error = database_.lastError().text();
            }
            database_.rollback();
            return false;
        }
    }
    if (query.exec(QStringLiteral("SELECT sqlite_version()")) && query.next()) {
        const auto version = QVersionNumber::fromString(query.value(0).toString());
        query.finish();
        // Keep older SQLite builds out of WAL mode when AuraPet controls the database.
        if (version >= QVersionNumber(3, 51, 3) && databasePath != QStringLiteral(":memory:")) {
            query.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
        }
    }
    ready_ = true;
    return true;
}

bool RoleLibrary::migrateSchema(QString& error)
{
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS roles ("
                                   "id TEXT PRIMARY KEY NOT NULL, name TEXT NOT NULL, asset BLOB NOT NULL)"))
        || !query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS library_migrations (key TEXT PRIMARY KEY NOT NULL)"))
        || !query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS chat_messages ("
                                      "id INTEGER PRIMARY KEY, role_id TEXT NOT NULL, "
                                      "speaker TEXT NOT NULL CHECK(speaker IN ('user','assistant')), "
                                      "text TEXT NOT NULL, created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)"))
        || !query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS chat_role_id ON chat_messages (role_id, id)"))
        || !query.exec(QStringLiteral("CREATE TRIGGER IF NOT EXISTS remove_role_chat AFTER DELETE ON roles "
                                      "BEGIN DELETE FROM chat_messages WHERE role_id = OLD.id; END"))) {
        error = query.lastError().text();
        return false;
    }
    return initProactiveSpeech(error) && initMemorySchema(error) && initActionSchema(error) && initShareSchema(error);
}

bool RoleLibrary::backupTo(const QString& file, QString& error) const
{
    if (!ready_) {
        error = QStringLiteral("角色库尚未打开。");
        return false;
    }
    return snapshot(database_, file, error);
}

bool RoleLibrary::backupDatabaseFile(const QString& sourceDatabasePath, const QString& destinationFile, QString& error)
{
    if (sourceDatabasePath.isEmpty() || sourceDatabasePath == QStringLiteral(":memory:")) {
        error = QStringLiteral("角色库尚未打开。");
        return false;
    }
    const QString connectionName = QStringLiteral("backup-") + newId();
    bool ok = false;
    {
        auto reader = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        reader.setDatabaseName(sourceDatabasePath);
        reader.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=5000"));
        if (reader.open()) {
            ok = snapshot(reader, destinationFile, error);
        } else {
            error = reader.lastError().text();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

bool RoleLibrary::restoreBackup(const QString& file, const QString& databasePath, QString& error)
{
    const QFileInfo source(file);
    const QFileInfo destination(databasePath);
    if (!source.isFile() || source.canonicalFilePath() == destination.canonicalFilePath()) {
        error = QStringLiteral("请选择一份独立的 AuraPet 角色库备份。");
        return false;
    }
    const auto name = newId();
    const auto staged = destination.absoluteDir().filePath(QStringLiteral(".restore-%1.sqlite3").arg(name));
    bool ok = false;
    {
        auto backup = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        backup.setDatabaseName(source.absoluteFilePath());
        backup.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=3000"));
        if (backup.open()) {
            ok = checkBackup(backup, error) && snapshot(backup, staged, error);
        } else {
            error = backup.lastError().text();
        }
    }
    QSqlDatabase::removeDatabase(name);
    if (!ok) {
        return false;
    }
    if (destination.exists()) {
        const auto currentName = newId();
        {
            auto current = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), currentName);
            current.setDatabaseName(databasePath);
            current.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=3000"));
            if (current.open()) {
                ok = snapshot(current, backupPath(databasePath, QStringLiteral("before-restore")), error);
                if (ok) {
                    QSqlQuery query(current);
                    ok = sql(query, QStringLiteral("PRAGMA journal_mode=DELETE"), error) && query.next()
                        && query.value(0).toString() == QStringLiteral("delete");
                }
            } else {
                error = current.lastError().text();
                ok = false;
            }
        }
        QSqlDatabase::removeDatabase(currentName);
    }
    if (ok) {
        QFile input(staged);
        QSaveFile output(databasePath);
        ok = input.open(QIODevice::ReadOnly) && output.open(QIODevice::WriteOnly);
        if (!ok) {
            error = input.isOpen() ? output.errorString() : input.errorString();
        }
        while (ok && !input.atEnd()) {
            const auto bytes = input.read(1024 * 1024);
            if (bytes.isEmpty() || output.write(bytes) != bytes.size()) {
                error = bytes.isEmpty() ? input.errorString() : output.errorString();
                ok = false;
            }
        }
        if (ok && !output.commit()) {
            error = output.errorString();
            ok = false;
        }
        if (ok) {
            QFile::setPermissions(databasePath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        }
    }
    QFile::remove(staged);
    if (!ok && error.isEmpty()) {
        error = QStringLiteral("无法独占角色库。请退出应用及后台任务后重新恢复。");
    }
    return ok;
}

bool RoleLibrary::available() const
{
    return ready_;
}

bool RoleLibrary::migrateLegacy(const QString& assetPath, QString& error)
{
    if (!QFileInfo::exists(assetPath)) {
        return true;
    }
    QSqlQuery migration(database_);
    if (!migration.exec(QStringLiteral("SELECT 1 FROM library_migrations WHERE key = 'active-pet'"))) {
        error = migration.lastError().text();
        return false;
    }
    if (migration.next()) {
        return true;
    }
    migration.finish();
    auto asset = loadPetAsset(assetPath, error);
    if (!asset) {
        return false;
    }
    if (!database_.transaction()) {
        error = database_.lastError().text();
        return false;
    }
    if (!migration.exec(QStringLiteral("INSERT OR IGNORE INTO library_migrations (key) VALUES ('active-pet')"))) {
        error = migration.lastError().text();
        database_.rollback();
        return false;
    }
    if (migration.numRowsAffected() != 0
        && !insert(legacyId(), QStringLiteral("上次导入的素材"), asset->source, error)) {
        database_.rollback();
        return false;
    }
    if (!database_.commit()) {
        error = database_.lastError().text();
        database_.rollback();
        return false;
    }
    return true;
}

QList<RoleEntry> RoleLibrary::entries(QString& error) const
{
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("SELECT id, name FROM roles ORDER BY rowid DESC"))) {
        error = query.lastError().text();
        return {};
    }
    QList<RoleEntry> roles;
    while (query.next()) {
        roles.append({query.value(0).toString(), query.value(1).toString()});
    }
    return roles;
}

QByteArray RoleLibrary::assetFor(const QString& id, QString& error) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT asset FROM roles WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        error = query.lastError().text();
        return {};
    }
    if (!query.next()) {
        error = QStringLiteral("这个角色已经不在角色库里。");
        return {};
    }
    return query.value(0).toByteArray();
}

QString RoleLibrary::add(const QString& name, const QByteArray& asset, QString& error)
{
    const auto id = newId();
    return insert(id, name, asset, error) ? id : QString{};
}

QString RoleLibrary::duplicate(const QString& sourceId, const QString& builtInAsset, QString& error)
{
    if (!ready_) {
        error = QStringLiteral("角色库尚未打开。");
        return {};
    }
    QByteArray baseAsset;
    QString baseName;
    if (sourceId == builtInId()) {
        QFile file(builtInAsset);
        if (!file.open(QIODevice::ReadOnly)) {
            error = file.errorString();
            return {};
        }
        baseAsset = file.read(maxPetAssetBytes + 1);
        if (baseAsset.isEmpty() || baseAsset.size() > maxPetAssetBytes) {
            error = QStringLiteral("无法读取内置角色素材。");
            return {};
        }
        baseName = builtInName();
    } else {
        baseAsset = assetFor(sourceId, error);
        if (baseAsset.isEmpty()) {
            return {};
        }
        for (const auto& entry : entries(error)) {
            if (entry.id == sourceId) {
                baseName = entry.name;
                break;
            }
        }
        if (!error.isEmpty()) {
            return {};
        }
    }
    const auto share = shareFor(sourceId, error, true);
    if (!error.isEmpty()) {
        return {};
    }
    QMap<ActionSlot, ActionAsset> actions;
    for (const auto slot : {ActionSlot::Idle, ActionSlot::Happy, ActionSlot::Sleepy}) {
        if (auto act = actionFor(sourceId, slot, error)) {
            actions.insert(slot, std::move(*act));
        }
        if (!error.isEmpty()) {
            return {};
        }
    }

    const QString copyName = (baseName.isEmpty() ? QStringLiteral("角色") : baseName.left(72)) + QStringLiteral(" 副本");
    const QString copyId = newId();
    if (!database_.transaction()) {
        error = database_.lastError().text();
        return {};
    }
    bool ok = insert(copyId, copyName, baseAsset, error);
    if (ok && (!share.card.isEmpty() || !share.behavior.isEmpty() || !share.voices.isEmpty()
               || !share.files.isEmpty() || !share.corpusNames.isEmpty() || !share.corpusTexts.isEmpty())) {
        ok = writeShare(copyId, share, error);
    }
    if (ok) {
        for (auto it = actions.cbegin(); it != actions.cend(); ++it) {
            if (!saveAction(copyId, it.key(), it.value(), error)) {
                ok = false;
                break;
            }
        }
    }
    if (ok && database_.commit()) {
        return copyId;
    }
    if (ok && error.isEmpty()) {
        error = database_.lastError().text();
    }
    database_.rollback();
    return {};
}

bool RoleLibrary::insert(const QString& id, const QString& name, const QByteArray& asset, QString& error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("INSERT INTO roles (id, name, asset) VALUES (?, ?, ?)"));
    query.addBindValue(id);
    const auto label = name.trimmed().left(80);
    query.addBindValue(label.isEmpty() ? QStringLiteral("新角色") : label);
    query.addBindValue(asset);
    return updateRole(query, error);
}

bool RoleLibrary::rename(const QString& id, const QString& name, QString& error)
{
    const auto label = name.trimmed().left(80);
    if (label.isEmpty()) {
        error = QStringLiteral("请输入角色名称。");
        return false;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("UPDATE roles SET name = ? WHERE id = ?"));
    query.addBindValue(label);
    query.addBindValue(id);
    return updateRole(query, error);
}

bool RoleLibrary::remove(const QString& id, QString& error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM roles WHERE id = ?"));
    query.addBindValue(id);
    return updateRole(query, error);
}

bool RoleLibrary::initProactiveSpeech(QString& error)
{
    QSqlQuery query(database_);
    const auto checkColumn = QStringLiteral("SELECT proactive FROM chat_messages LIMIT 0");
    if (query.exec(checkColumn) || query.exec(QStringLiteral("ALTER TABLE chat_messages "
        "ADD COLUMN proactive INTEGER NOT NULL DEFAULT 0 CHECK(proactive IN (0,1))"))) {
        return true;
    }
    const QString failure = query.lastError().text();
    // A second instance may have added the column while this instance was opening the database.
    if (query.exec(checkColumn)) {
        return true;
    }
    error = failure;
    return false;
}

QList<ChatMessage> RoleLibrary::chatHistory(const QString& id, int limit, QString& error, qint64 after,
                                           bool includeProactive) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT speaker, text, id, created_at, proactive FROM chat_messages "
                                 "WHERE role_id = ? AND id > ? AND (? OR proactive=0) ORDER BY id DESC LIMIT ?"));
    query.addBindValue(id);
    query.addBindValue(after);
    query.addBindValue(includeProactive);
    query.addBindValue(limit);
    if (!query.exec()) {
        error = query.lastError().text();
        return {};
    }
    QList<ChatMessage> history;
    while (query.next()) {
        history.append({query.value(0).toString(), query.value(1).toString(),
                        query.value(2).toLongLong(), query.value(3).toString(), query.value(4).toBool()});
    }
    std::reverse(history.begin(), history.end());
    return history;
}

bool RoleLibrary::saveExchange(const QString& id, const QString& user, const QString& assistant, QString& error)
{
    if (!database_.transaction()) {
        error = database_.lastError().text();
        return false;
    }
    if (!insertChatMessage(id, QStringLiteral("user"), user, false, error)
        || !insertChatMessage(id, QStringLiteral("assistant"), assistant, false, error)) {
        database_.rollback();
        return false;
    }
    if (!database_.commit()) {
        error = database_.lastError().text();
        database_.rollback();
        return false;
    }
    return true;
}

bool RoleLibrary::saveProactiveSpeech(const QString& id, const QString& text, QString& error)
{
    return insertChatMessage(id, QStringLiteral("assistant"), text, true, error);
}

bool RoleLibrary::insertChatMessage(const QString& id, const QString& speaker, const QString& text,
                                    bool proactive, QString& error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("INSERT INTO chat_messages (role_id, speaker, text, proactive) "
                                 "SELECT ?, ?, ?, ? WHERE ? OR EXISTS (SELECT 1 FROM roles WHERE id = ?)"));
    query.addBindValue(id);
    query.addBindValue(speaker);
    query.addBindValue(text);
    query.addBindValue(proactive);
    query.addBindValue(id == builtInId());
    query.addBindValue(id);
    return updateRole(query, error);
}
