#pragma once

#include <QString>

bool readChatKey(const QString& endpoint, QString& key, QString& error);
bool saveChatKey(const QString& endpoint, const QString& key, QString& error);
