#pragma once

#include <QString>

class ChatSession;
class RoleLibrary;
class QSettings;
class QWidget;

void showRoleChatHistory(ChatSession& chat, RoleLibrary& library, QSettings& settings, QWidget* parent);
