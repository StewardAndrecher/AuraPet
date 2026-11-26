#include "mac_companion_services.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QUrl>
#include <QWidget>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {
#ifdef Q_OS_WIN
const QString runKeyPath = QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
const QString runValueName = QStringLiteral("AuraPet");

QString launchCommand()
{
    return QLatin1Char('"') + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + QLatin1Char('"');
}
#endif
}

LoginItemState loginItemState()
{
#ifdef Q_OS_WIN
    const QSettings run(runKeyPath, QSettings::NativeFormat);
    return run.contains(runValueName) ? LoginItemState::Enabled : LoginItemState::Off;
#else
    return LoginItemState::Unavailable;
#endif
}

bool setLoginItem(bool enabled, QString& error)
{
#ifdef Q_OS_WIN
    QSettings run(runKeyPath, QSettings::NativeFormat);
    if (enabled) {
        run.setValue(runValueName, launchCommand());
    } else {
        run.remove(runValueName);
    }
    run.sync();
    if (run.status() != QSettings::NoError) {
        error = QStringLiteral("无法写入 Windows 开机启动注册表项。");
        return false;
    }
    return true;
#else
    Q_UNUSED(enabled);
    error = QStringLiteral("当前系统暂不支持开机启动设置。");
    return false;
#endif
}

void openLoginItemSettings()
{
#ifdef Q_OS_WIN
    QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:startupapps")));
#endif
}

void requestReminderPermission()
{
}

void sendCompanionReminder(const QString& text)
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }
    for (QObject* child : QCoreApplication::instance() ? QCoreApplication::instance()->children() : QObjectList{}) {
        if (auto* tray = qobject_cast<QSystemTrayIcon*>(child)) {
            tray->showMessage(QStringLiteral("AuraPet"), text, QSystemTrayIcon::Information, 5000);
            return;
        }
    }
}

void activateAppForPopup(QWidget& owner)
{
#ifdef Q_OS_WIN
    if (const WId wid = owner.winId()) {
        SetForegroundWindow(reinterpret_cast<HWND>(wid));
    }
#endif
    owner.activateWindow();
}

void preparePopupWindow(QWidget& popup)
{
#ifdef Q_OS_WIN
    if (const WId wid = popup.winId()) {
        SetForegroundWindow(reinterpret_cast<HWND>(wid));
    }
#endif
    popup.activateWindow();
    popup.raise();
}

ForegroundContext queryForegroundContext()
{
    ForegroundContext ctx;
#ifdef Q_OS_WIN
    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        ctx.selfAppActive = (pid == GetCurrentProcessId());
        if (!ctx.selfAppActive) {
            wchar_t cls[64] = {};
            GetClassNameW(fg, cls, 64);
            const QString className = QString::fromWCharArray(cls);
            if (className != QLatin1String("Progman") && className != QLatin1String("WorkerW")) {
                RECT wr{};
                MONITORINFO mi{};
                mi.cbSize = sizeof(mi);
                if (GetWindowRect(fg, &wr) && GetMonitorInfoW(MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST), &mi)) {
                    ctx.externalFullScreen = (wr.left <= mi.rcMonitor.left && wr.top <= mi.rcMonitor.top
                                              && wr.right >= mi.rcMonitor.right && wr.bottom >= mi.rcMonitor.bottom);
                }
            }
        }
    }
#endif
    return ctx;
}
