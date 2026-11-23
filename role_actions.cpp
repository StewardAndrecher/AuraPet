#include "role_actions.h"

#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <utility>

QString actionSlotName(ActionSlot slot)
{
    switch (slot) {
    case ActionSlot::Idle: return QStringLiteral("待机");
    case ActionSlot::Happy: return QStringLiteral("开心");
    case ActionSlot::Sleepy: return QStringLiteral("困倦");
    }
    return {};
}

bool RoleLibrary::initActionSchema(QString& error)
{
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS role_actions ("
                                   "role_id TEXT NOT NULL, slot INTEGER NOT NULL, "
                                   "name TEXT NOT NULL, asset BLOB NOT NULL, recipe BLOB, PRIMARY KEY(role_id,slot))"))
        || !query.exec(QStringLiteral("CREATE TRIGGER IF NOT EXISTS remove_role_actions AFTER DELETE ON roles "
                                      "BEGIN DELETE FROM role_actions WHERE role_id = OLD.id; END"))) {
        error = query.lastError().text();
        return false;
    }
    const auto check = QStringLiteral("SELECT recipe FROM role_actions LIMIT 0");
    if (query.exec(check) || query.exec(QStringLiteral("ALTER TABLE role_actions ADD COLUMN recipe BLOB"))
        || query.exec(check)) {
        return true;
    }
    error = query.lastError().text();
    return false;
}

std::optional<ActionAsset> RoleLibrary::actionFor(const QString& id, ActionSlot slot, QString& error) const
{
    if (!available()) {
        return std::nullopt;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT name,asset,recipe FROM role_actions WHERE role_id=? AND slot=?"));
    query.addBindValue(id);
    query.addBindValue(static_cast<int>(slot));
    if (!query.exec()) {
        error = query.lastError().text();
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }
    return ActionAsset{query.value(0).toString(), query.value(1).toByteArray(),
                       QJsonDocument::fromJson(query.value(2).toByteArray()).object()};
}

bool RoleLibrary::saveAction(const QString& id, ActionSlot slot, const ActionAsset& asset, QString& error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("INSERT INTO role_actions(role_id,slot,name,asset,recipe) "
                                  "SELECT ?,?,?,?,? WHERE (? OR EXISTS(SELECT 1 FROM roles WHERE id=?)) "
                                  "ON CONFLICT(role_id,slot) DO UPDATE SET name=excluded.name,asset=excluded.asset,recipe=excluded.recipe"));
    query.addBindValue(id);
    query.addBindValue(static_cast<int>(slot));
    query.addBindValue(asset.name);
    query.addBindValue(asset.bytes);
    query.addBindValue(QJsonDocument(asset.recipe).toJson(QJsonDocument::Compact));
    query.addBindValue(id == builtInId());
    query.addBindValue(id);
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

bool RoleLibrary::removeAction(const QString& id, ActionSlot slot, QString& error)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM role_actions WHERE role_id=? AND slot=?"));
    query.addBindValue(id);
    query.addBindValue(static_cast<int>(slot));
    if (!query.exec()) {
        error = query.lastError().text();
        return false;
    }
    return true;
}

std::optional<PetAsset> loadRoleAction(const RoleLibrary& library, const QString& id, ActionSlot slot,
                                      const QString& builtInAsset, QString& error)
{
    if (auto assigned = library.actionFor(id, slot, error)) {
        return decodePetAsset(std::move(assigned->bytes), error);
    }
    if (!error.isEmpty() || slot != ActionSlot::Idle) {
        return std::nullopt;
    }
    if (id == RoleLibrary::builtInId()) {
        return loadPetAsset(builtInAsset, error);
    }
    auto bytes = library.assetFor(id, error);
    return !bytes.isEmpty() ? decodePetAsset(std::move(bytes), error) : std::nullopt;
}
