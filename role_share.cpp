#include "role_package.h"
#include "random_speech.h"

#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace {
const QString builtInPackageId = QStringLiteral("95888180-5519-4bab-a814-5a5acc10c24d");
const QString legacyPackageId = QStringLiteral("a05d6e07-ad76-4927-90fe-88c01d067e28");

bool exec(QSqlQuery& query, QString& error)
{
    if (query.exec()) {
        return true;
    }
    error = query.lastError().text();
    return false;
}

bool finish(QSqlDatabase& database, bool ok, QString& error)
{
    if (ok && database.commit()) {
        return true;
    }
    if (ok) {
        error = database.lastError().text();
    }
    database.rollback();
    return false;
}
}

bool RoleLibrary::initShareSchema(QString& error)
{
    QSqlQuery query(database_);
    if (query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS role_share ("
                                  "role_id TEXT PRIMARY KEY, card BLOB NOT NULL, behavior BLOB NOT NULL, voices BLOB NOT NULL, corpus BLOB NOT NULL, transcripts BLOB NOT NULL DEFAULT '{}')"))
        && query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS role_share_files ("
                                     "role_id TEXT NOT NULL, path TEXT NOT NULL, bytes BLOB NOT NULL, PRIMARY KEY(role_id,path))"))
        && query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS role_package_origin ("
                                     "role_id TEXT PRIMARY KEY, package_id TEXT NOT NULL)"))
        && query.exec(QStringLiteral("CREATE TRIGGER IF NOT EXISTS remove_role_share AFTER DELETE ON roles BEGIN "
                                     "DELETE FROM role_share WHERE role_id=OLD.id; "
                                     "DELETE FROM role_share_files WHERE role_id=OLD.id; "
                                     "DELETE FROM role_package_origin WHERE role_id=OLD.id; END"))) {
        if (query.exec(QStringLiteral("SELECT transcripts FROM role_share LIMIT 0"))
            || query.exec(QStringLiteral("ALTER TABLE role_share ADD COLUMN transcripts BLOB NOT NULL DEFAULT '{}'"))
            || query.exec(QStringLiteral("SELECT transcripts FROM role_share LIMIT 0"))) {
            return true;
        }
    }
    error = query.lastError().text();
    return false;
}

QStringList RoleLibrary::corpusPaths(const QString& id, QString& error) const
{
    if (!ready_) {
        return {};
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT path FROM role_share_files WHERE role_id=? AND path LIKE 'voice/corpus/%' ORDER BY path"));
    query.addBindValue(id);
    QStringList paths;
    if (exec(query, error)) {
        while (query.next()) {
            paths.append(query.value(0).toString());
        }
    }
    return paths;
}

QByteArray RoleLibrary::roleRecording(const QString& id, const QString& path, QString& error) const
{
    if (!ready_ || !isRandomSpeechRecording(path)) {
        error = QStringLiteral("无法读取此角色的原声。");
        return {};
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT bytes FROM role_share_files WHERE role_id=? AND path=? "
                                 "AND length(bytes) BETWEEN 45 AND 16777216"));
    query.addBindValue(id);
    query.addBindValue(path);
    if (!exec(query, error)) {
        return {};
    }
    if (!query.next()) {
        error = QStringLiteral("这段原声已经移除，或无法读取。");
        return {};
    }
    return query.value(0).toByteArray();
}

ShareFileStats RoleLibrary::shareFileStats(const QString& id, QString& error) const
{
    if (!ready_) {
        return {};
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT COALESCE(SUM(CASE WHEN path LIKE 'voice/corpus/%' THEN 1 ELSE 0 END), 0), "
        "COALESCE(SUM(CASE WHEN path LIKE 'voice/corpus/%' THEN length(bytes) ELSE 0 END), 0), "
        "COALESCE(SUM(CASE WHEN path NOT LIKE 'voice/corpus/%' THEN length(bytes) ELSE 0 END), 0) "
        "FROM role_share_files WHERE role_id=?"));
    query.addBindValue(id);
    if (!exec(query, error) || !query.next()) {
        return {};
    }
    return ShareFileStats{query.value(0).toInt(), query.value(1).toLongLong(), query.value(2).toLongLong()};
}

QMap<QString, QByteArray> RoleLibrary::shareFilesFor(const QString& id, bool includeCorpus, QString& error) const
{
    if (!ready_) {
        return {};
    }
    QSqlQuery query(database_);
    query.prepare(includeCorpus
        ? QStringLiteral("SELECT path,bytes FROM role_share_files WHERE role_id=? ORDER BY path")
        : QStringLiteral("SELECT path,bytes FROM role_share_files WHERE role_id=? AND path NOT LIKE 'voice/corpus/%' ORDER BY path"));
    query.addBindValue(id);
    QMap<QString, QByteArray> files;
    if (!exec(query, error)) {
        return {};
    }
    while (query.next()) {
        files.insert(query.value(0).toString(), query.value(1).toByteArray());
    }
    return files;
}

RoleShare RoleLibrary::shareFor(const QString& id, QString& error, bool includeFiles) const
{
    if (!ready_) {
        return {};
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT card,behavior,voices,corpus,transcripts FROM role_share WHERE role_id=?"));
    query.addBindValue(id);
    if (!exec(query, error) || !query.next()) {
        return {};
    }
    RoleShare share{QJsonDocument::fromJson(query.value(0).toByteArray()).object(),
                    QJsonDocument::fromJson(query.value(1).toByteArray()).object(),
                    QJsonDocument::fromJson(query.value(2).toByteArray()).object(), {},
                    QJsonDocument::fromJson(query.value(3).toByteArray()).object(),
                    QJsonDocument::fromJson(query.value(4).toByteArray()).object()};
    if (includeFiles) {
        query.prepare(QStringLiteral("SELECT path,bytes FROM role_share_files WHERE role_id=? ORDER BY path"));
        query.addBindValue(id);
        if (!exec(query, error)) {
            return {};
        }
        while (query.next()) {
            share.files.insert(query.value(0).toString(), query.value(1).toByteArray());
        }
    }
    return share;
}

bool RoleLibrary::writeShare(const QString& id, const RoleShare& share, QString& error)
{
    auto normalized = share;
    removeChangedRandomRecordings(normalized);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("INSERT INTO role_share(role_id,card,behavior,voices,corpus,transcripts) "
                                 "SELECT ?,?,?,?,?,? WHERE (? OR EXISTS(SELECT 1 FROM roles WHERE id=?)) "
                                 "ON CONFLICT(role_id) DO UPDATE SET card=excluded.card,behavior=excluded.behavior,voices=excluded.voices,corpus=excluded.corpus,transcripts=excluded.transcripts"));
    query.addBindValue(id);
    for (const auto& object : {share.card, normalized.behavior, share.voices, share.corpusNames, share.corpusTexts}) {
        query.addBindValue(QJsonDocument(object).toJson(QJsonDocument::Compact));
    }
    query.addBindValue(id == builtInId());
    query.addBindValue(id);
    if (!exec(query, error)) {
        return false;
    }
    if (query.numRowsAffected() != 1) {
        error = QStringLiteral("这个角色已经不在角色库里。");
        return false;
    }
    query.prepare(QStringLiteral("DELETE FROM role_share_files WHERE role_id=?"));
    query.addBindValue(id);
    if (!exec(query, error)) {
        return false;
    }

    query.prepare(QStringLiteral("INSERT INTO role_share_files(role_id,path,bytes) VALUES (?,?,?)"));
    for (auto it = share.files.cbegin(); it != share.files.cend(); ++it) {
        query.bindValue(0, id);
        query.bindValue(1, it.key());
        query.bindValue(2, it.value());
        if (!exec(query, error)) {
            return false;
        }
    }
    return true;
}

bool RoleLibrary::saveShare(const QString& id, const RoleShare& share, QString& error)
{
    if (!validateRoleShare(share, error)) {
        return false;
    }
    if (!database_.transaction()) {
        error = database_.lastError().text();
        return false;
    }
    return finish(database_, writeShare(id, share, error), error);
}

bool RoleLibrary::saveRoleCard(const QString& id, const QJsonObject& card, QString& error)
{
    if (!validateRoleCard(card, error)) {
        return false;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("INSERT INTO role_share(role_id,card,behavior,voices,corpus,transcripts) "
                                 "SELECT ?,?,'{}','{}','{}','{}' WHERE (? OR EXISTS(SELECT 1 FROM roles WHERE id=?)) "
                                 "ON CONFLICT(role_id) DO UPDATE SET card=excluded.card"));
    query.addBindValue(id);
    query.addBindValue(QJsonDocument(card).toJson(QJsonDocument::Compact));
    query.addBindValue(id == builtInId());
    query.addBindValue(id);
    if (!exec(query, error)) {
        return false;
    }
    if (query.numRowsAffected() != 1) {
        error = QStringLiteral("这个角色已经不在角色库里。");
        return false;
    }
    return true;
}

bool RoleLibrary::saveRandomSpeech(const QString& id, const QJsonObject& config, QString& error)
{
    if (!ready_ || !validateRandomSpeech(config, error)) {
        if (error.isEmpty()) {
            error = QStringLiteral("角色库尚未打开。");
        }
        return false;
    }
    auto behavior = shareFor(id, error, false).behavior;
    if (!error.isEmpty()) {
        return false;
    }
    behavior.insert(QStringLiteral("randomSpeech"), config);
    const auto bytes = QJsonDocument(behavior).toJson(QJsonDocument::Compact);
    if (bytes.size() > 128 * 1024) {
        error = QStringLiteral("角色的行为设置超过 128 KiB。");
        return false;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("INSERT INTO role_share(role_id,card,behavior,voices,corpus,transcripts) "
                                 "SELECT ?,'{}',?,'{}','{}','{}' WHERE (? OR EXISTS(SELECT 1 FROM roles WHERE id=?)) "
                                 "ON CONFLICT(role_id) DO UPDATE SET behavior=excluded.behavior"));
    query.addBindValue(id);
    query.addBindValue(bytes);
    query.addBindValue(id == builtInId());
    query.addBindValue(id);
    if (!exec(query, error)) {
        return false;
    }
    if (query.numRowsAffected() != 1) {
        error = QStringLiteral("这个角色已经不在角色库里。");
        return false;
    }
    return true;
}

QString RoleLibrary::packageIdFor(const QString& id, QString& error) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT package_id FROM role_package_origin WHERE role_id=?"));
    query.addBindValue(id);
    if (!exec(query, error)) {
        return {};
    }
    if (query.next()) {
        return query.value(0).toString();
    }
    if (id == builtInId()) {
        return builtInPackageId;
    }
    return id == legacyId() ? legacyPackageId : id;
}

QString RoleLibrary::matchingPackage(const QString& packageId, QString& error) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT r.id FROM roles r LEFT JOIN role_package_origin o ON o.role_id=r.id "
                                 "WHERE r.id=? OR o.package_id=? OR r.id=? "
                                 "ORDER BY (r.id=?) DESC,r.rowid ASC LIMIT 1"));
    query.addBindValue(packageId);
    query.addBindValue(packageId);
    query.addBindValue(packageId == legacyPackageId ? legacyId() : QString{});
    query.addBindValue(packageId);
    return exec(query, error) && query.next() ? query.value(0).toString() : QString{};
}

QString RoleLibrary::importPackage(const RolePackage& package, const QString& replace, QString& error)
{
    if (!database_.transaction()) {
        error = database_.lastError().text();
        return {};
    }
    QString id;
    if (replace.isEmpty()) {
        id = add(package.name, package.asset, error);
    } else {
        const QString current = packageIdFor(replace, error);
        if (current == package.id && replace != builtInId()) {
            QSqlQuery update(database_);
            update.prepare(QStringLiteral("UPDATE roles SET name=?,asset=? WHERE id=?"));
            update.addBindValue(package.name);
            update.addBindValue(package.asset);
            update.addBindValue(replace);
            if (exec(update, error) && update.numRowsAffected() == 1) {
                id = replace;
            }
        }
        if (id.isEmpty() && error.isEmpty()) {
            error = QStringLiteral("角色库已变化，请重新选择要更新的角色。");
        }
    }
    bool ok = !id.isEmpty() && writeShare(id, package.share, error);
    if (ok) {
        for (const auto slot : {ActionSlot::Idle, ActionSlot::Happy, ActionSlot::Sleepy}) {
            const auto action = package.actions.constFind(slot);
            ok = action != package.actions.cend() ? saveAction(id, slot, *action, error) : removeAction(id, slot, error);
            if (!ok) {
                break;
            }
        }
    }
    if (ok && replace.isEmpty()) {
        QSqlQuery origin(database_);
        origin.prepare(QStringLiteral("INSERT INTO role_package_origin(role_id,package_id) VALUES(?,?)"));
        origin.addBindValue(id);
        origin.addBindValue(package.id);
        ok = exec(origin, error);
    }
    return finish(database_, ok, error) ? id : QString{};
}
