#include "role_library.h"

#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <utility>

namespace {
constexpr int recentMessages = 16;

bool execute(QSqlQuery& query, QString& error)
{
    if (query.exec()) {
        return true;
    }
    error = query.lastError().text();
    return false;
}

QSqlQuery forRole(const QSqlDatabase& db, const QString& sql, const QString& id)
{
    QSqlQuery query(db);
    query.prepare(sql);
    query.addBindValue(id);
    return query;
}

bool endTransaction(QSqlDatabase& db, bool success, QString& error)
{
    if (success && db.commit()) {
        return true;
    }
    if (success) {
        error = db.lastError().text();
    }
    db.rollback();
    return false;
}
}

bool RoleLibrary::initMemorySchema(QString& error)
{
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS chat_archives ("
                                   "role_id TEXT NOT NULL, first_id INTEGER NOT NULL, last_id INTEGER NOT NULL, "
                                   "text TEXT NOT NULL, excluded INTEGER NOT NULL DEFAULT 0, "
                                   "PRIMARY KEY(role_id,last_id))"))
        || !query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS role_memory ("
                                      "role_id TEXT NOT NULL, key TEXT NOT NULL, text TEXT NOT NULL, source_id INTEGER, "
                                      "edited INTEGER NOT NULL DEFAULT 0, forgotten INTEGER NOT NULL DEFAULT 0, "
                                      "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY(role_id,key))"))
        || !query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS archive_memory ("
                                      "role_id TEXT NOT NULL, last_id INTEGER NOT NULL, key TEXT NOT NULL, "
                                      "PRIMARY KEY(role_id,last_id,key))"))
        || !query.exec(QStringLiteral("CREATE TRIGGER IF NOT EXISTS remove_role_memory AFTER DELETE ON roles BEGIN "
                                      "DELETE FROM chat_archives WHERE role_id = OLD.id; "
                                      "DELETE FROM archive_memory WHERE role_id = OLD.id; "
                                      "DELETE FROM role_memory WHERE role_id = OLD.id; END"))) {
        error = query.lastError().text();
        return false;
    }
    return true;
}

qint64 RoleLibrary::summarizedThrough(const QString& id, QString& error) const
{
    auto query = forRole(database_, QStringLiteral("SELECT COALESCE(MAX(last_id),0) FROM chat_archives WHERE role_id = ?"), id);
    return execute(query, error) && query.next() ? query.value(0).toLongLong() : 0;
}

std::optional<SummaryBatch> RoleLibrary::summaryBatch(const QString& id, bool manual, QString& error) const
{
    const qint64 through = summarizedThrough(id, error);
    if (!error.isEmpty()) {
        return std::nullopt;
    }
    auto stats = forRole(database_, QStringLiteral("SELECT COUNT(*), MIN(created_at) <= datetime('now','-7 days'), "
                                                 "COALESCE(SUM(length(text)),0) FROM chat_messages "
                                                 "WHERE role_id = ? AND id > ? AND proactive=0"), id);
    stats.addBindValue(through);
    if (!execute(stats, error) || !stats.next()) {
        return std::nullopt;
    }
    const auto count = stats.value(0).toInt();
    const bool due = manual || count >= 48 || stats.value(1).toBool() || stats.value(2).toLongLong() >= 12000;
    if (count <= recentMessages || !due) {
        return std::nullopt;
    }
    auto rows = forRole(database_, QStringLiteral("SELECT speaker,text,id,created_at FROM chat_messages "
                                                 "WHERE role_id = ? AND id > ? AND proactive=0 ORDER BY id LIMIT ?"), id);
    rows.addBindValue(through);
    rows.addBindValue(qMin(32, count - recentMessages));
    if (!execute(rows, error)) {
        return std::nullopt;
    }
    SummaryBatch batch{through, {}};
    while (rows.next()) {
        batch.messages.append({rows.value(0).toString(), rows.value(1).toString(),
                               rows.value(2).toLongLong(), rows.value(3).toString()});
    }
    if (!batch.messages.isEmpty() && batch.messages.last().speaker == QStringLiteral("user")) {
        batch.messages.removeLast();
    }
    return batch.messages.isEmpty() ? std::nullopt : std::make_optional(std::move(batch));
}

bool RoleLibrary::saveSummary(const QString& id, const SummaryBatch& batch, const QString& text,
                              const QList<MemoryItem>& candidates, QString& error)
{
    if (batch.messages.isEmpty() || text.trimmed().isEmpty()) {
        error = QStringLiteral("整理结果缺少摘要。");
        return false;
    }
    if (!database_.transaction()) {
        error = database_.lastError().text();
        return false;
    }
    const qint64 last = batch.messages.last().id;
    auto archive = forRole(database_, QStringLiteral("INSERT INTO chat_archives(role_id,first_id,last_id,text) "
        "SELECT ?,?,?,? WHERE (? OR EXISTS(SELECT 1 FROM roles WHERE id=?)) "
        "AND (SELECT COALESCE(MAX(last_id),0) FROM chat_archives WHERE role_id=?)=?"), id);
    archive.addBindValue(batch.messages.first().id);
    archive.addBindValue(last);
    archive.addBindValue(text.trimmed().left(1800));
    archive.addBindValue(id == builtInId());
    archive.addBindValue(id);
    archive.addBindValue(id);
    archive.addBindValue(batch.previousThrough);
    if (!execute(archive, error) || archive.numRowsAffected() != 1) {
        if (error.isEmpty()) {
            error = QStringLiteral("这段对话的整理状态已经更新。");
        }
        return endTransaction(database_, false, error);
    }
    static const QRegularExpression keyFormat(QStringLiteral("^[a-z][a-z0-9_.:-]{0,63}$"));
    for (const auto& fact : candidates) {
        bool validSource = false;
        for (const auto& message : batch.messages) {
            if (message.id == fact.sourceId && message.speaker == QStringLiteral("user")) {
                validSource = true;
                break;
            }
        }
        if (!validSource || !keyFormat.match(fact.key).hasMatch() || fact.text.trimmed().isEmpty()) {
            continue;
        }
        auto item = forRole(database_, QStringLiteral("INSERT INTO role_memory(role_id,key,text,source_id) VALUES (?,?,?,?) "
            "ON CONFLICT(role_id,key) DO UPDATE SET text=excluded.text,source_id=excluded.source_id, "
            "updated_at=CURRENT_TIMESTAMP WHERE role_memory.edited=0 AND role_memory.forgotten=0 "
            "AND excluded.source_id>role_memory.source_id"), id);
        item.addBindValue(fact.key);
        item.addBindValue(fact.text.trimmed().left(500));
        item.addBindValue(fact.sourceId);
        auto link = forRole(database_, QStringLiteral("INSERT OR IGNORE INTO archive_memory(role_id,last_id,key) VALUES (?,?,?)"), id);
        link.addBindValue(last);
        link.addBindValue(fact.key);
        if (!execute(item, error) || !execute(link, error)) {
            return endTransaction(database_, false, error);
        }
    }
    auto suppress = forRole(database_, QStringLiteral("UPDATE chat_archives SET excluded=1 WHERE role_id=? AND last_id=? "
        "AND EXISTS(SELECT 1 FROM archive_memory a JOIN role_memory m ON m.role_id=a.role_id AND m.key=a.key "
        "WHERE a.role_id=chat_archives.role_id AND a.last_id=chat_archives.last_id AND (m.edited=1 OR m.forgotten=1))"), id);
    suppress.addBindValue(last);
    return endTransaction(database_, execute(suppress, error), error);
}

QList<ConversationSummary> RoleLibrary::summaries(const QString& id, bool includeExcluded, QString& error) const
{
    auto rows = forRole(database_, QStringLiteral("SELECT first_id,last_id,text,excluded FROM chat_archives "
        "WHERE role_id=? AND (? OR excluded=0) ORDER BY last_id DESC LIMIT ?"), id);
    rows.addBindValue(includeExcluded);
    rows.addBindValue(includeExcluded ? 50 : 3);
    QList<ConversationSummary> result;
    if (execute(rows, error)) {
        while (rows.next()) {
            result.append({rows.value(0).toLongLong(), rows.value(1).toLongLong(),
                           rows.value(2).toString(), rows.value(3).toBool()});
        }
    }
    return result;
}

QList<MemoryItem> RoleLibrary::memoryItems(const QString& id, QString& error) const
{
    auto rows = forRole(database_, QStringLiteral("SELECT m.key,m.text,m.source_id,c.text,c.created_at FROM role_memory m "
        "LEFT JOIN chat_messages c ON c.id=m.source_id AND c.role_id=m.role_id "
        "WHERE m.role_id=? AND m.forgotten=0 ORDER BY m.updated_at DESC,m.key"), id);
    QList<MemoryItem> result;
    if (execute(rows, error)) {
        while (rows.next()) {
            result.append({rows.value(0).toString(), rows.value(1).toString(), rows.value(2).toLongLong(),
                           rows.value(3).toString(), rows.value(4).toString()});
        }
    }
    return result;
}

QStringList RoleLibrary::protectedMemoryKeys(const QString& id, QString& error) const
{
    auto rows = forRole(database_, QStringLiteral("SELECT key FROM role_memory WHERE role_id=? AND (forgotten=1 OR edited=1)"), id);
    QStringList keys;
    if (execute(rows, error)) {
        while (rows.next()) {
            keys.append(rows.value(0).toString());
        }
    }
    return keys;
}

bool RoleLibrary::editMemory(const QString& id, const QString& key, const QString& text, bool forget, QString& error)
{
    if (!forget && text.trimmed().isEmpty()) {
        error = QStringLiteral("请输入记忆内容。");
        return false;
    }
    if (!database_.transaction()) {
        error = database_.lastError().text();
        return false;
    }
    QSqlQuery change(database_);
    change.prepare(QStringLiteral("UPDATE role_memory SET text=?,edited=1,forgotten=?,updated_at=CURRENT_TIMESTAMP "
                                  "WHERE role_id=? AND key=? AND forgotten=0"));
    change.addBindValue(forget ? QStringLiteral("") : text.trimmed().left(500));
    change.addBindValue(forget);
    change.addBindValue(id);
    change.addBindValue(key);
    if (!execute(change, error) || change.numRowsAffected() != 1) {
        if (error.isEmpty()) {
            error = QStringLiteral("这条记忆已经被移除。");
        }
        return endTransaction(database_, false, error);
    }
    auto exclude = forRole(database_, QStringLiteral("UPDATE chat_archives SET excluded=1 WHERE role_id=? AND last_id IN "
        "(SELECT last_id FROM archive_memory WHERE role_id=? AND key=?)"), id);
    exclude.addBindValue(id);
    exclude.addBindValue(key);
    return endTransaction(database_, execute(exclude, error), error);
}
