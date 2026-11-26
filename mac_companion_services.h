#pragma once

#include <QString>

class QWidget;

enum class LoginItemState { Off, Enabled, NeedsApproval, Unavailable };
LoginItemState loginItemState();
bool setLoginItem(bool enabled, QString& error);
void openLoginItemSettings();
void requestReminderPermission();
void sendCompanionReminder(const QString& text);
struct ForegroundContext {
    bool selfAppActive = false;
    bool externalFullScreen = false;
};

void activateAppForPopup(QWidget& owner);
void preparePopupWindow(QWidget& popup);
ForegroundContext queryForegroundContext();
