#include "role_library.h"
#include "pet_asset.h"

#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

#include <algorithm>

namespace {
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
    database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), newId());
    database_.setDatabaseName(databasePath);
    database_.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=3000"));
    if (!database_.open()) {
        error = database_.lastError().text();
        return false;
    }
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
    ready_ = initProactiveSpeech(error) && initMemorySchema(error) && initActionSchema(error) && initShareSchema(error);
    return ready_;
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
    QList<RoleEntry> roles{{builtInId(), builtInName() + QStringLiteral("（内置）")}};
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
