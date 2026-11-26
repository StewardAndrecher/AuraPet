#include "desktop_application.h"
#include "desktop_pet_manager.h"
#include "focus_session.h"
#include "role_library.h"
#include "role_package.h"
#include "settings_store.h"
#include "voice_training.h"

#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
#include "global_hotkeys.h"
#endif

#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QTranslator>

namespace {
bool isRolePackage(const QString& path)
{
    return QFileInfo(path).suffix().compare(rolePackageSuffix, Qt::CaseInsensitive) == 0;
}

QString soleGifInWorkingDirectory()
{
    QStringList gifs;
    const auto files = QDir::current().entryInfoList(QDir::Files);
    for (const QFileInfo& file : files) {
        const QString extension = file.suffix().toLower();
        if (extension == QStringLiteral("gif") || extension == QStringLiteral("agif")) {
            gifs.append(file.absoluteFilePath());
        }
    }
    return gifs.size() == 1 ? gifs.constFirst() : QString();
}

QIcon menuBarIcon()
{
    QIcon icon;
    for (const int scale : {1, 2}) {
        QPixmap pixmap(22 * scale, 22 * scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        painter.drawEllipse(QRectF(5.5, 10, 11, 8));
        painter.drawEllipse(QRectF(3, 6.5, 4, 5));
        painter.drawEllipse(QRectF(7, 3.5, 3.5, 5));
        painter.drawEllipse(QRectF(11.5, 3.5, 3.5, 5));
        painter.drawEllipse(QRectF(15, 6.5, 4, 5));
        painter.end();
        icon.addPixmap(pixmap);
    }
    icon.setIsMask(true);
    return icon;
}
}

int main(int argc, char* argv[])
{
    DesktopApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("AuraPet"));
    app.setApplicationDisplayName(QStringLiteral("AuraPet"));
    app.setOrganizationName(QStringLiteral("AuraPet"));
    app.setApplicationVersion(QStringLiteral(AURAPET_VERSION));
    app.setQuitOnLastWindowClosed(false);
    QTranslator english;
    if (QLocale::system().language() == QLocale::English && english.load(QStringLiteral(":/i18n/aurapet_en.qm"))) {
        app.installTranslator(&english);
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("在桌面播放图片和 GIF。"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption restoreLibrary(QStringLiteral("restore-role-library"),
        QStringLiteral("退出桌宠后，从指定备份恢复本地角色库。"), QStringLiteral("备份路径"));
    parser.addOption(restoreLibrary);
    parser.addPositionalArgument(QStringLiteral("素材"), QStringLiteral("图片或 GIF 的路径。"),
                                 QStringLiteral("[素材]"));
    parser.process(app);
    const auto arguments = parser.positionalArguments();
    if (arguments.size() > 1) {
        parser.showHelp(2);
    }

    SettingsStore stored;
    auto& settings = stored.native();
    const auto dataPath = SettingsStore::dataPath();
    const QDir dataDir(dataPath);
    const auto databasePath = dataDir.filePath(QStringLiteral("roles.sqlite3"));
    QString instanceError;
    const bool restoring = parser.isSet(restoreLibrary);
    if (restoring && !arguments.isEmpty()) {
        parser.showHelp(2);
    }
    const auto instance = app.claimProfile(dataPath, arguments.value(0), !restoring, instanceError);
    if (instance != DesktopApplication::Instance::Primary) {
        if (instance == DesktopApplication::Instance::Failed) {
            qCritical().noquote() << instanceError;
            return 1;
        }
        return 0;
    }
    if (restoring) {
        if (!RoleLibrary::restoreBackup(parser.value(restoreLibrary), databasePath, instanceError)) {
            qCritical().noquote() << instanceError;
            return 1;
        }
        qInfo().noquote() << QStringLiteral("角色库已恢复。现在可以启动 AuraPet。");
        return 0;
    }

    FocusSession focus(settings);
    const auto legacyPath = dataDir.filePath(QStringLiteral("active-pet"));
    RoleLibrary library;
    QString libraryError;
    if (library.open(databasePath, libraryError)) {
        QString migrationError;
        if (!library.migrateLegacy(legacyPath, migrationError)) {
            qWarning().noquote() << QStringLiteral("无法迁移上次导入的素材：") << migrationError;
        }
    }
    if (!library.available()) {
        qWarning().noquote() << QStringLiteral("无法打开本地角色库：") << libraryError;
    }

    VoiceTraining voiceTraining(dataPath);
    DesktopPetManager* activeManager = nullptr;
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    GlobalHotkeys hotkeys([&](GlobalHotkeys::Action action) {
        switch (action) {
        case GlobalHotkeys::Visibility:
            if (activeManager) {
                activeManager->toggleDesktopVisibility();
            }
            break;
        case GlobalHotkeys::Focus: focus.start(std::chrono::minutes(25)); break;
        case GlobalHotkeys::Quiet: focus.setDoNotDisturb(!focus.doNotDisturb()); break;
        }
    });
    const auto hotkeysKey = QStringLiteral("pet/globalHotkeys");
    hotkeys.setEnabled(settings.value(hotkeysKey, true).toBool());
#endif

    DesktopPetManager manager(library, libraryError, stored, focus, voiceTraining, {});
    activeManager = &manager;

#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    manager.setHotkeyMenuBuilder([&](QMenu& target) {
        auto* keyMenu = target.addMenu(QStringLiteral("全局快捷键"));
        auto* enabledKeys = keyMenu->addAction(QStringLiteral("启用全局快捷键"));
        enabledKeys->setCheckable(true);
        enabledKeys->setChecked(settings.value(hotkeysKey, true).toBool());
        auto* hotkeyStatus = keyMenu->addAction(QString{});
        hotkeyStatus->setEnabled(false);
        for (const auto& [action, label] : {
                 std::pair{GlobalHotkeys::Visibility, QStringLiteral("显示／隐藏桌宠")},
                 std::pair{GlobalHotkeys::Focus, QStringLiteral("开始 25 分钟专注")},
                 std::pair{GlobalHotkeys::Quiet, QStringLiteral("切换免打扰")}}) {
            keyMenu->addAction(GlobalHotkeys::chord(action) + QStringLiteral("   ") + label)->setEnabled(false);
        }
        const auto syncStatus = [&, enabledKeys, hotkeyStatus, hotkeysKey](bool enabled) {
            const auto unavailable = hotkeys.setEnabled(enabled);
            hotkeyStatus->setVisible(!unavailable.isEmpty());
            hotkeyStatus->setText(QStringLiteral("未能注册 %1").arg(unavailable.join(QStringLiteral("、"))));
            settings.setValue(hotkeysKey, enabled);
            QSignalBlocker block(enabledKeys);
            enabledKeys->setChecked(enabled);
        };
        QObject::connect(enabledKeys, &QAction::toggled, keyMenu, syncStatus);
        QObject::connect(keyMenu, &QMenu::aboutToShow, keyMenu, [&, enabledKeys, hotkeyStatus, hotkeysKey] {
            const bool enabled = settings.value(hotkeysKey, true).toBool();
            QSignalBlocker block(enabledKeys);
            enabledKeys->setChecked(enabled);
            const auto unavailable = hotkeys.setEnabled(enabled);
            hotkeyStatus->setVisible(!unavailable.isEmpty());
            hotkeyStatus->setText(QStringLiteral("未能注册 %1").arg(unavailable.join(QStringLiteral("、"))));
        });
    });
#endif

    QString startError;
    const QString directAsset = (!arguments.isEmpty() && !isRolePackage(arguments.constFirst()))
        ? arguments.constFirst()
        : QString{};
    if (!manager.startInitialPets(directAsset, legacyPath, soleGifInWorkingDirectory(), startError)) {
        qCritical().noquote() << QStringLiteral("无法打开桌宠素材：") << startError;
        return 1;
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &manager, [&manager] {
        manager.saveAllStates();
    });

#ifdef Q_OS_MACOS
    QMenuBar appMenuBar;
    appMenuBar.setNativeMenuBar(true);
    auto* petMenu = appMenuBar.addMenu(QCoreApplication::translate("DesktopMenu", "桌宠"));
    manager.setupGlobalMenu(*petMenu);
#endif

    QMenu trayMenu;
    manager.setupGlobalMenu(trayMenu);
    QSystemTrayIcon tray(menuBarIcon(), &app);
    const auto updateTrayStatus = [&] {
        QString status = QStringLiteral("AuraPet");
        if (focus.phase() == FocusSession::Phase::Completed) {
            status += focus.kind() == FocusSession::TimerKind::Focus ? QStringLiteral(" · 专注已结束")
                                                                    : QStringLiteral(" · 休息已结束");
        } else if (focus.active()) {
            status += (focus.phase() == FocusSession::Phase::Paused ? QStringLiteral(" · 暂停 ")
                                                                     : QStringLiteral(" · 专注 "))
                + focus.remainingLabel();
        } else if (focus.doNotDisturb()) {
            status += QStringLiteral(" · 免打扰");
        }
        tray.setToolTip(status);
    };
    QObject::connect(&focus, &FocusSession::changed, &tray, updateTrayStatus);
    updateTrayStatus();
    tray.setContextMenu(&trayMenu);
    tray.show();

    QObject::connect(&app, &DesktopApplication::showRequested, &manager, &DesktopPetManager::showAndRaiseAll);
    QObject::connect(&app, &DesktopApplication::fileRequested, &manager, [&](const QString& path) {
        manager.enqueueImport(path, false);
    });
    const bool importingCliPackage = !arguments.isEmpty() && isRolePackage(arguments.constFirst());
    if (importingCliPackage) {
        manager.enqueueImport(arguments.constFirst(), false);
    } else if (manager.petCount() == 0) {
        QTimer::singleShot(150, &manager, [&manager] { manager.promptFirstPetImport(); });
    }
    app.startFileDelivery();
    return app.exec();
}
