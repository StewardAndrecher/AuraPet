#pragma once

#include "role_library.h"

#include <QJsonArray>

struct SummaryResult {
    QString summary;
    QList<MemoryItem> facts;
};

QJsonArray summaryPrompt(const SummaryBatch& batch, const QStringList& protectedKeys);
std::optional<SummaryResult> parseSummaryReply(const QByteArray& reply, QString& error);
QString memoryContext(const RoleLibrary& library, const QString& id, QString& error);
