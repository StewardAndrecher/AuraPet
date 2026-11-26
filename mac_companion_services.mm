#include "mac_companion_services.h"

#include <QGuiApplication>
#include <QWidget>

#import <AppKit/AppKit.h>
#import <ServiceManagement/ServiceManagement.h>
#import <UserNotifications/UserNotifications.h>

LoginItemState loginItemState()
{
    switch (SMAppService.mainAppService.status) {
    case SMAppServiceStatusEnabled: return LoginItemState::Enabled;
    case SMAppServiceStatusRequiresApproval: return LoginItemState::NeedsApproval;
    case SMAppServiceStatusNotRegistered: return LoginItemState::Off;
    default: return LoginItemState::Unavailable;
    }
}

bool setLoginItem(bool enabled, QString& error)
{
    NSError* failure = nil;
    auto* app = SMAppService.mainAppService;
    const bool ok = enabled ? [app registerAndReturnError:&failure] : [app unregisterAndReturnError:&failure];
    if (!ok && !(enabled && loginItemState() == LoginItemState::NeedsApproval)) {
        error = failure ? QString::fromNSString(failure.localizedDescription) : QStringLiteral("登录项暂不可用。");
        return false;
    }
    return true;
}

void openLoginItemSettings()
{
    [SMAppService openSystemSettingsLoginItems];
}

void requestReminderPermission()
{
    [UNUserNotificationCenter.currentNotificationCenter requestAuthorizationWithOptions:UNAuthorizationOptionAlert
        completionHandler:^(BOOL, NSError*) {}];
}

void sendCompanionReminder(const QString& text)
{
    auto* content = [[UNMutableNotificationContent alloc] init];
    content.title = @"AuraPet";
    content.body = text.toNSString();
    const auto request = [UNNotificationRequest requestWithIdentifier:NSUUID.UUID.UUIDString content:content trigger:nil];
    [UNUserNotificationCenter.currentNotificationCenter addNotificationRequest:request withCompletionHandler:nil];
#if !__has_feature(objc_arc)
    [content release];
#endif
}

void activateAppForPopup(QWidget& owner)
{
    if (QGuiApplication::platformName() != QLatin1String("cocoa")) {
        return;
    }
    if (![NSApp isActive]) {
        if (@available(macOS 14.0, *)) {
            [NSApp activate];
        }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [[NSRunningApplication currentApplication]
            activateWithOptions:NSApplicationActivateIgnoringOtherApps];
#pragma clang diagnostic pop
    }
    owner.activateWindow();
}

void preparePopupWindow(QWidget& popup)
{
    if (QGuiApplication::platformName() != QLatin1String("cocoa")
        || !popup.testAttribute(Qt::WA_WState_Created)) {
        return;
    }
    const WId handle = popup.internalWinId();
    if (!handle) {
        return;
    }
    NSView* view = (__bridge NSView*)reinterpret_cast<void*>(handle);
    NSWindow* win = view ? [view window] : nil;
    if (!win) {
        return;
    }
    [win setAcceptsMouseMovedEvents:YES];
    if ([win level] < NSPopUpMenuWindowLevel) {
        [win setLevel:NSPopUpMenuWindowLevel];
    }
    [win makeKeyAndOrderFront:nil];
    popup.activateWindow();
    popup.raise();
}

ForegroundContext queryForegroundContext()
{
    ForegroundContext ctx;
    if (QGuiApplication::platformName() != QLatin1String("cocoa")) {
        return ctx;
    }
    ctx.selfAppActive = ([NSApp isActive] == YES);
    return ctx;
}
