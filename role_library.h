#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

#include <optional>

struct RoleEntry {
    QString id;
    QString name;
};

enum class ActionSlot { Idle = 0, Happy = 1, Sleepy = 2 };

struct ActionAsset {
    QString name;
    QByteArray bytes;
    QJsonObject recipe;
};

struct RoleShare {
    QJsonObject card;
    QJsonObject behavior;
    QJsonObject voices;
    QMap<QString, QByteArray> files;
    QJsonObject corpusNames;
    QJsonObject corpusTexts;
};

struct ShareFileStats {
    int corpusCount = 0;
    qint64 corpusBytes = 0;
    qint64 otherBytes = 0;
};

struct RolePackage;

struct ChatMessage {
    QString speaker;
    QString text;
    qint64 id = 0;
    QString createdAt;
    bool proactive = false;
};

struct SummaryBatch {
    qint64 previousThrough = 0;
    QList<ChatMessage> messages;
};

struct MemoryItem {
    QString key;
    QString text;
    qint64 sourceId = 0;
    QString source;
    QString sourceTime;
};

struct ConversationSummary {
    qint64 firstId;
    qint64 lastId;
    QString text;
    bool excluded;
};

class RoleLibrary {
public:
    RoleLibrary() = default;
    ~RoleLibrary();
    RoleLibrary(const RoleLibrary&) = delete;
    RoleLibrary& operator=(const RoleLibrary&) = delete;

    static QString builtInId();
    static QString builtInName();
    static QString legacyId();
    bool open(const QString& databasePath, QString& error);
    QString databasePath() const { return database_.databaseName(); }
    bool backupTo(const QString& file, QString& error) const;
    static bool backupDatabaseFile(const QString& sourceDatabasePath, const QString& destinationFile, QString& error);
    static bool restoreBackup(const QString& file, const QString& databasePath, QString& error);
    bool available() const;
    bool migrateLegacy(const QString& assetPath, QString& error);
    QList<RoleEntry> entries(QString& error) const;
    QByteArray assetFor(const QString& id, QString& error) const;
    QString add(const QString& name, const QByteArray& asset, QString& error);
    QString duplicate(const QString& sourceId, const QString& builtInAsset, QString& error);
    bool rename(const QString& id, const QString& name, QString& error);
    bool remove(const QString& id, QString& error);
    std::optional<ActionAsset> actionFor(const QString& id, ActionSlot slot, QString& error) const;
    bool saveAction(const QString& id, ActionSlot slot, const ActionAsset& asset, QString& error);
    bool removeAction(const QString& id, ActionSlot slot, QString& error);
    QStringList corpusPaths(const QString& id, QString& error) const;
    QByteArray roleRecording(const QString& id, const QString& path, QString& error) const;
    ShareFileStats shareFileStats(const QString& id, QString& error) const;
    QMap<QString, QByteArray> shareFilesFor(const QString& id, bool includeCorpus, QString& error) const;
    RoleShare shareFor(const QString& id, QString& error, bool includeFiles = true) const;
    bool saveRoleCard(const QString& id, const QJsonObject& card, QString& error);
    bool saveRandomSpeech(const QString& id, const QJsonObject& config, QString& error);
    bool saveShare(const QString& id, const RoleShare& share, QString& error);
    QString matchingPackage(const QString& packageId, QString& error) const;
    QString importPackage(const RolePackage& package, const QString& replace, QString& error);
    QString packageIdFor(const QString& id, QString& error) const;
    QList<ChatMessage> chatHistory(const QString& id, int limit, QString& error,
                                  qint64 after = 0, bool includeProactive = true) const;
    bool saveExchange(const QString& id, const QString& user, const QString& assistant, QString& error);
    bool saveProactiveSpeech(const QString& id, const QString& text, QString& error);
    bool clearConversation(const QString& id, QString& error);
    qint64 summarizedThrough(const QString& id, QString& error) const;
    std::optional<SummaryBatch> summaryBatch(const QString& id, bool manual, QString& error) const;
    bool saveSummary(const QString& id, const SummaryBatch& batch, const QString& text,
                     const QList<MemoryItem>& candidates, QString& error);
    QList<ConversationSummary> summaries(const QString& id, bool includeExcluded, QString& error) const;
    QList<MemoryItem> memoryItems(const QString& id, QString& error) const;
    QStringList protectedMemoryKeys(const QString& id, QString& error) const;
    bool editMemory(const QString& id, const QString& key, const QString& text, bool forget, QString& error);

private:
    bool initProactiveSpeech(QString& error);
    bool migrateSchema(QString& error);
    bool insertChatMessage(const QString& id, const QString& speaker, const QString& text,
                           bool proactive, QString& error);
    bool initMemorySchema(QString& error);
    bool initActionSchema(QString& error);
    bool initShareSchema(QString& error);
    bool writeShare(const QString& id, const RoleShare& share, QString& error);
    bool insert(const QString& id, const QString& name, const QByteArray& asset, QString& error);
    QSqlDatabase database_;
    bool ready_ = false;
};
