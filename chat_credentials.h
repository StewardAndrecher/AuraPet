#pragma once

#include <QString>

bool readChatKey(const QString& endpoint, bool local, QString& key, QString& error);
bool saveChatKey(const QString& endpoint, const QString& key, QString& error);
