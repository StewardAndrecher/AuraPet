#include "pet_asset.h"
#include "pet_tether.h"
#include "pet_treats.h"
#include "pet_window.h"
#include "action_editor.h"
#include "activity_meter.h"
#include "companion_menu.h"
#include "companion_events.h"
#include "focus_session.h"
#include "random_movement.h"
#include "role_actions.h"
#include "role_library.h"
#include "role_library_dialog.h"
#include "role_package.h"

#ifdef Q_OS_MACOS
#include "chat_dialog.h"
#include "global_hotkeys.h"
#include "chat_session.h"
#include "import_preview.h"
#include "pet_chat_overlay.h"
#include "speech_playback.h"
#endif

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCommandLineParser>
#include <QCursor>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QIcon>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QScreen>
#include <QScopedValueRollback>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QWidgetAction>

#include <functional>
#include <utility>

namespace {
const QString defaultPetPath = QStringLiteral(":/default/凯猫.gif");

bool isRolePackage(const QString& path)
{
    return QFileInfo(path).suffix().compare(rolePackageSuffix, Qt::CaseInsensitive) == 0;
}

class GifPetApplication : public QApplication {
public:
    using QApplication::QApplication;
    std::function<void(const QString&)> openPackage;
    QStringList pendingPackages;

protected:
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::FileOpen) {
            const auto path = static_cast<QFileOpenEvent*>(event)->file();
            if (isRolePackage(path)) {
                if (openPackage) {
                    openPackage(path);
                } else {
                    pendingPackages.append(path);
                }
                return true;
            }
        }
        return QApplication::event(event);
    }
};


QString roleSettingsKey(const QString& id)
{
    return QStringLiteral("roles/") + id;
}

QPoint initialPetPosition(const PetWindow& window)
{
    if (const auto* screen = QGuiApplication::primaryScreen()) {
        return screen->availableGeometry().bottomRight() - QPoint(window.width() + 24, window.height() + 24);
    }
    return QPoint(24, 24);
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
    GifPetApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("AuraPet"));
    app.setApplicationDisplayName(QStringLiteral("AuraPet"));
    app.setOrganizationName(QStringLiteral("AuraPet"));
    app.setApplicationVersion(QStringLiteral("1.0.27"));
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("在桌面播放图片和 GIF。"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("素材"), QStringLiteral("图片或 GIF 的路径。"),
                                 QStringLiteral("[素材]"));
    parser.process(app);
    const auto arguments = parser.positionalArguments();
    if (arguments.size() > 1) {
        parser.showHelp(2);
    }

    // Reuse the existing settings and data locations after the app rename.
    const auto legacyName = QStringLiteral("Gif-Pet");
    QSettings settings(QSettings::NativeFormat, QSettings::UserScope, legacyName, legacyName);
    FocusSession focus(settings);
    CompanionEvents companionEventsState(settings);
    const auto support = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString dataPath = support.isEmpty() ? QString{} : QDir(support).filePath(legacyName + u'/' + legacyName);
    const QDir dataDir(dataPath);
    const QString legacyPath = dataDir.filePath(QStringLiteral("active-pet"));
    const QString lastRoleKey = QStringLiteral("pet/activeRoleId");
    const QString builtInId = RoleLibrary::builtInId();
    RoleLibrary library;
    QString libraryError;
    if (dataPath.isEmpty() || !QDir().mkpath(dataPath)) {
        libraryError = QStringLiteral("无法创建桌宠的本地数据目录。");
    } else if (library.open(dataDir.filePath(QStringLiteral("roles.sqlite3")), libraryError)) {
        QString migrationError;
        if (!library.migrateLegacy(legacyPath, migrationError)) {
            qWarning().noquote() << QStringLiteral("无法迁移上次导入的素材：") << migrationError;
        }
    }
    if (!library.available()) {
        qWarning().noquote() << QStringLiteral("无法打开本地角色库：") << libraryError;
    }

    PetWindow window;
    const auto screenPolicyKey = QStringLiteral("pet/screenPrimaryOnly");
    window.setPrimaryOnly(settings.value(screenPolicyKey, false).toBool());
    const auto recordInteraction = [&] {
        companionEventsState.interacted(QDateTime::currentDateTime());
        focus.recordInteraction();
    };
    FocusNotice focusNotice(window);
    ActivityMeter activityMeter(window);
    TreatEffect treatEffect(window);
    RandomMovement movement(window, [&activityMeter](double percent) { activityMeter.setActivity(percent); });
    const QString kibbleKey = QStringLiteral("pet/loadedKibble");
    const QString randomMovementKey = QStringLiteral("pet/randomMovement");
    std::optional<PetTreat> loadedKibble;
    QJsonObject roleBehavior;
    TreatCard* treatCard = nullptr;
    QAction* movementAction = nullptr;
    const auto selectKibble = [&](const QString& kibble) {
        loadedKibble = kibble == QStringLiteral("high") ? std::optional{PetTreat::HighKibble}
            : kibble == QStringLiteral("slow") ? std::optional{PetTreat::SlowKibble} : std::nullopt;
        movement.setKibble(loadedKibble == PetTreat::HighKibble ? Kibble::High
            : loadedKibble == PetTreat::SlowKibble ? Kibble::Slow : Kibble::None);
        if (treatCard) {
            treatCard->setLoaded(loadedKibble);
        }
    };
    const auto roleKibbleKey = [&](const QString& role) {
        return roleSettingsKey(role) + QStringLiteral("/loadedKibble");
    };
    const auto roleMovementKey = [&](const QString& role) {
        return roleSettingsKey(role) + QStringLiteral("/randomMovement");
    };
#ifdef Q_OS_MACOS
    ChatSession chat(library, settings);
    SpeechPlayback voice(settings);
    PetChatOverlay chatOverlay(window, chat, &voice);
    chatOverlay.zoomRequested = [&window](const QKeyEvent& key) { return window.applyZoomKey(key); };
    bool chatActivity = false;
    QObject::connect(&chatOverlay, &PetChatOverlay::activityChanged, &window,
                     [&chatActivity](bool active) { chatActivity = active; });
    QObject::connect(&chatOverlay, &PetChatOverlay::interactionSent, &window, recordInteraction);
    QObject::connect(&chatOverlay, &PetChatOverlay::activityChanged, &movement, &RandomMovement::setSuspended);
    QObject::connect(&chatOverlay, &PetChatOverlay::inputVisibilityChanged, &activityMeter, &ActivityMeter::setRevealed);
#else
    activityMeter.setRevealed(true);
#endif
    const auto syncQuietMode = [&] {
        movement.setQuietMode(focus.quiet());
#ifdef Q_OS_MACOS
        voice.setQuiet(focus.quiet());
        chat.setVoiceFirst(voice.available());
        chat.setProactiveEnabled(!focus.quiet());
        if (focus.quiet()) {
            chatOverlay.dismissProactiveSpeech();
        }
#endif
    };
    QObject::connect(&focus, &FocusSession::changed, &window, syncQuietMode);
    syncQuietMode();

    PetTether tether(window, movement);
#ifdef Q_OS_MACOS
    tether.aimingStarted = [&chatOverlay] { chatOverlay.closeInput(); };
#endif
    QString currentRole;
    const auto saveLayout = [&] {
        if (!currentRole.isEmpty()) {
            const auto key = roleSettingsKey(currentRole);
            settings.setValue(key + QStringLiteral("/position"), window.pos());
            settings.setValue(key + QStringLiteral("/scale"), window.scalePercent());
            settings.setValue(lastRoleKey, currentRole);
        }
    };
    const auto displayRole = [&](const QString& id, PetAsset asset) {
        tether.cancel();
        treatEffect.clear();
        QString shareError;
        const auto roleShare = library.shareFor(id, shareError, false);
        if (!shareError.isEmpty()) {
            qWarning().noquote() << shareError;
        }
        roleBehavior = roleShare.behavior;
        companionEventsState.setRole(id, roleBehavior, QDateTime::currentDateTime());
        selectKibble(settings.value(roleKibbleKey(id), roleBehavior.value(QStringLiteral("kibble")).toString(
            settings.value(kibbleKey).toString())).toString());
        const bool roaming = settings.value(roleMovementKey(id), roleBehavior.value(QStringLiteral("randomMovement"))
            .toBool(settings.value(randomMovementKey, true).toBool())).toBool();
        movement.setEnabled(roaming);
        if (movementAction) {
            QSignalBlocker updating(movementAction);
            movementAction->setChecked(roaming);
        }
#ifdef Q_OS_MACOS
        chatOverlay.reset();
        chat.setRole(id, true);
        voice.setPackagedVoices(id, roleShare.voices);
#endif
        const QPoint previousPosition = window.pos();
        const bool hadRole = !currentRole.isEmpty();
        saveLayout();
        currentRole = id;
        const QString key = roleSettingsKey(id);
        activityMeter.setAsset(asset);
        window.setAsset(std::move(asset), settings.value(key + QStringLiteral("/scale"), 100).toInt());
        const QPoint fallback = hadRole && id != RoleLibrary::legacyId() ? previousPosition
            : settings.value(QStringLiteral("pet/position"), initialPetPosition(window)).toPoint();
        window.placeAt(settings.value(key + QStringLiteral("/position"), fallback).toPoint());
        saveLayout();
    };
    const auto switchRole = [&](const QString& id, QString& error) {
        if (id != builtInId && !library.available()) {
            error = libraryError;
            return false;
        }
        auto asset = loadRoleAction(library, id, ActionSlot::Idle, defaultPetPath, error);
        if (!asset && id == builtInId && !error.isEmpty()) {
            qWarning().noquote() << QStringLiteral("无法使用自定义待机素材：") << error;
            error.clear();
            asset = loadPetAsset(defaultPetPath, error);
        }
        if (!asset) {
            return false;
        }
        displayRole(id, std::move(*asset));
        return true;
    };
    const auto addRole = [&](const QString& path, PetAsset asset, QString& error) {
        if (!library.available()) {
            error = libraryError;
            return false;
        }
        const auto id = library.add(QFileInfo(path).completeBaseName(), asset.source, error);
        if (id.isEmpty()) {
            return false;
        }
        displayRole(id, std::move(asset));
        return true;
    };
    const auto addFile = [&](const QString& path, QString& error) {
        auto asset = loadPetAsset(path, error);
        return asset && addRole(path, std::move(*asset), error);
    };

    QString error;
    if (!arguments.isEmpty() && !isRolePackage(arguments.constFirst())) {
        if (!addFile(arguments.constFirst(), error)) {
            qCritical().noquote() << QStringLiteral("无法打开素材：") << error;
            return 1;
        }
    } else if (!GIFPET_START_WITH_KAIMAO && library.available()) {
        const QString legacyRole = QFileInfo::exists(legacyPath) ? RoleLibrary::legacyId() : QString{};
        const QString lastRole = settings.value(lastRoleKey, legacyRole).toString();
        if (!lastRole.isEmpty()) {
            if (!switchRole(lastRole, error)) {
                qWarning().noquote() << QStringLiteral("无法恢复上次角色：") << error;
            }
        } else if (const auto path = soleGifInWorkingDirectory(); !path.isEmpty() && !addFile(path, error)) {
            qWarning().noquote() << QStringLiteral("无法导入当前目录的动图：") << error;
        }
    }

    if (currentRole.isEmpty() && !switchRole(builtInId, error)) {
        qCritical().noquote() << QStringLiteral("无法打开内置素材：") << error;
        return 1;
    }
    window.layoutChanged = saveLayout;
    const auto clampToScreen = [&] {
        tether.cancel();
        window.placeAt(window.pos(), true);
        saveLayout();
    };
    const auto watchScreen = [&](QScreen* screen) {
        QObject::connect(screen, &QScreen::availableGeometryChanged, &window, clampToScreen, Qt::QueuedConnection);
    };
    for (auto* screen : QGuiApplication::screens()) {
        watchScreen(screen);
    }
    QObject::connect(&app, &QGuiApplication::screenAdded, &window, watchScreen);
    QObject::connect(&app, &QGuiApplication::screenRemoved, &window, clampToScreen, Qt::QueuedConnection);
    QObject::connect(&app, &QGuiApplication::primaryScreenChanged, &window, [&](QScreen*) {
        window.setPrimaryOnly(settings.value(screenPolicyKey, false).toBool());
        clampToScreen();
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &window, [&] {
        saveLayout();
        settings.sync();
    });

    bool importInProgress = false;
    const auto importPet = [&](const QString& path, bool preview) {
        if (importInProgress) {
            return;
        }
        QScopedValueRollback<bool> importing(importInProgress, true);
        QString importError;
        if (isRolePackage(path)) {
            const auto id = importRolePackage(window, library, path);
            if (id.isEmpty()) {
                return;
            }
            if (!switchRole(id, importError)) {
                QMessageBox::warning(&window, QStringLiteral("无法切换角色"), importError);
            }
            window.show();
            return;
        }
        auto asset = loadPetAsset(path, importError);
#ifdef Q_OS_MACOS
        if (asset && preview) {
            asset = previewPetImport(std::move(*asset), &window, importError);
        }
#else
        Q_UNUSED(preview);
#endif
        if (!asset || !addRole(path, std::move(*asset), importError)) {
            if (!importError.isEmpty()) {
                QMessageBox::warning(&window, QStringLiteral("导入失败"), importError);
            }
            return;
        }
        window.show();
    };
    window.importFile = [&](const QString& path) { importPet(path, true); };

    QMenu menu;
    auto* treatHeader = new QWidgetAction(&menu);
    treatCard = new TreatCard;
    treatCard->setLoaded(loadedKibble);
    treatHeader->setDefaultWidget(treatCard);
    menu.addAction(treatHeader);
    menu.addSeparator();
    const auto* companion = addCompanionMenu(menu, window, focus)->menuAction();
    menu.addSeparator();
    const auto pickAsset = [&](bool preview) {
        const auto path = QFileDialog::getOpenFileName(
            &window, QStringLiteral("导入桌宠素材"), {},
            QStringLiteral("桌宠素材 (*.png *.jpg *.jpeg *.gif *.agif *.avif);;所有文件 (*)"));
        if (!path.isEmpty()) {
            importPet(path, preview);
        }
    };
    auto* importAction = menu.addAction(QStringLiteral("导入图片或动图…"));
    QObject::connect(importAction, &QAction::triggered, &window, [&] { pickAsset(true); });
    auto* originalAction = menu.addAction(QStringLiteral("原样导入并播放…"));
    QObject::connect(originalAction, &QAction::triggered, &window, [&] { pickAsset(false); });
    auto* libraryAction = menu.addAction(QStringLiteral("角色库…"));
    QObject::connect(libraryAction, &QAction::triggered, &window, [&] {
        if (!library.available()) {
            QMessageBox::warning(&window, QStringLiteral("无法打开角色库"), libraryError);
            return;
        }
        const auto choice = manageRoles(library, currentRole, &window);
        for (const auto& id : choice.removed) {
            settings.remove(roleSettingsKey(id));
        }
        if (choice.removed.contains(currentRole)) {
            currentRole.clear();
        }
        const auto requested = !choice.selected.isEmpty() ? choice.selected
            : currentRole.isEmpty() ? builtInId : choice.changed.contains(currentRole) ? currentRole : QString{};
        if (requested.isEmpty() || (requested == currentRole && !choice.changed.contains(currentRole))) {
            return;
        }
        QString switchError;
        if (!switchRole(requested, switchError)) {
            QMessageBox::warning(&window, QStringLiteral("无法切换角色"), switchError);
            if (currentRole.isEmpty()) {
                switchRole(builtInId, switchError);
            }
        }
        window.show();
    });
    auto* editActions = menu.addAction(QStringLiteral("角色动作…"));
    bool editingActions = false;
    QObject::connect(editActions, &QAction::triggered, &window, [&] {
        if (editingActions || currentRole.isEmpty()) {
            return;
        }
        if (!library.available()) {
            QMessageBox::warning(&window, QStringLiteral("无法打开角色动作"), libraryError);
            return;
        }
        QScopedValueRollback<bool> editing(editingActions, true);
        const QString roleId = currentRole;
        if (editRoleActions(library, roleId, defaultPetPath, &window) && roleId == currentRole) {
            QString actionError;
            if (!switchRole(roleId, actionError)) {
                QMessageBox::warning(&window, QStringLiteral("无法应用待机动作"), actionError);
            }
        }
    });
    const auto playRoleAction = [&](ActionSlot slot, bool explainMissing) -> bool {
        QString actionError;
        auto asset = loadRoleAction(library, currentRole, slot, defaultPetPath, actionError);
        if (!asset) {
            if (explainMissing) {
                QMessageBox::information(&window, QStringLiteral("无法播放动作"), actionError.isEmpty()
                    ? QStringLiteral("可以在「角色动作」中为%1设置素材。").arg(actionSlotName(slot)) : actionError);
            }
            return false;
        }
        window.playAction(std::move(*asset), slot == ActionSlot::Sleepy ? 2200 : 1200);
        window.show();
        return true;
    };
    const auto playEventAction = [&](const QJsonObject& binding) {
        const auto action = binding.value(QStringLiteral("action")).toString();
        if (action.isEmpty()) {
            return false;
        }
        if (action == QStringLiteral("idle")) {
            window.returnToIdle();
        } else if (action == QStringLiteral("bounce")
            || (action == QStringLiteral("happy") && !playRoleAction(ActionSlot::Happy, false))
            || (action == QStringLiteral("sleepy") && !playRoleAction(ActionSlot::Sleepy, false))) {
            window.playBounce();
        }
        return true;
    };
    const auto sayEventLine = [&](const QJsonObject& binding, bool atFocusStart = false) {
#ifdef Q_OS_MACOS
        const auto line = binding.value(QStringLiteral("line")).toString();
        if ((!focus.quiet() || (atFocusStart && !focus.doNotDisturb())) && !line.isEmpty()) {
            chatOverlay.sayFixed(line);
            return true;
        }
#else
        Q_UNUSED(binding);
        Q_UNUSED(atFocusStart);
#endif
        return false;
    };
    const auto runBinding = [&](const QJsonObject& binding, bool atFocusStart = false) {
        sayEventLine(binding, atFocusStart);
        return playEventAction(binding);
    };
    const auto runEvent = [&](CompanionEvent event) {
        return runBinding(companionEventsState.binding(event));
    };
    QObject::connect(&focus, &FocusSession::focusStarted, &window, [&] {
        if (window.isVisible() && !focus.doNotDisturb()) {
            runBinding(companionEventsState.binding(CompanionEvent::FocusStarted), true);
        }
    });
    QObject::connect(&focus, &FocusSession::breakStarted, &window, [&] {
        if (window.isVisible() && !focus.doNotDisturb()) {
            runEvent(CompanionEvent::BreakStarted);
        }
    });
    QObject::connect(&focus, &FocusSession::breakCompleted, &window, [&] {
        if (window.isVisible() && !focus.doNotDisturb()) {
            focusNotice.announce(QStringLiteral("休息结束 · 可以开始下一轮专注"));
            runEvent(CompanionEvent::BreakFinished);
        }
    });
    QTimer checkReminders;
    checkReminders.setInterval(5000);
    QObject::connect(&checkReminders, &QTimer::timeout, &window, [&] {
        bool available = window.isVisible() && !focus.quiet() && !QApplication::activeModalWidget()
            && !QApplication::activePopupWidget();
#ifdef Q_OS_MACOS
        available = available && !chatActivity && !chat.busy() && !voice.busy();
#endif
        runBinding(companionEventsState.nextReminder(QDateTime::currentDateTime(), movement.activity(), available));
    });
    checkReminders.start();
    QObject::connect(&focus, &FocusSession::completed, &window, [&] {
        if (!window.isVisible() || focus.doNotDisturb()) {
            return;
        }
        focusNotice.announce(QStringLiteral("专注结束 · 建议休息 %1 分钟").arg(focus.recommendedBreakMinutes()));
        const auto action = focus.endAction();
        const auto binding = companionEventsState.binding(CompanionEvent::FocusFinished);
        const bool hasLine = sayEventLine(binding);
#ifdef Q_OS_MACOS
        if (!hasLine && action != FocusSession::EndAction::Text && voice.available()) {
            chatOverlay.sayFixed(QStringLiteral("专注结束，休息一下吧。"));
        }
#else
        Q_UNUSED(hasLine);
#endif
        if (playEventAction(binding)) {
            return;
        }
        if (action == FocusSession::EndAction::Bounce
            || (action == FocusSession::EndAction::Happy && !playRoleAction(ActionSlot::Happy, false))) {
            window.playBounce();
        }
    });
    const auto useTreat = [&](PetTreat treat) {
        recordInteraction();
        using Feedback = ActivityMeter::Feedback;
        if (treat == PetTreat::SlowKibble || treat == PetTreat::HighKibble) {
            if (loadedKibble == treat) {
                loadedKibble.reset();
                movement.setKibble(Kibble::None);
                settings.setValue(roleKibbleKey(currentRole), QStringLiteral("none"));
                activityMeter.flash(Feedback::Neutral);
            } else {
                const bool high = treat == PetTreat::HighKibble;
                loadedKibble = treat;
                movement.setKibble(high ? Kibble::High : Kibble::Slow);
                const auto selected = high ? QStringLiteral("high") : QStringLiteral("slow");
                settings.setValue(roleKibbleKey(currentRole), selected);
                activityMeter.flash(high ? Feedback::Raise : Feedback::Lower);
                runEvent(high ? CompanionEvent::HighKibble : CompanionEvent::SlowKibble);
            }
            treatCard->setLoaded(loadedKibble);
            return;
        }

        if (treat == PetTreat::Catnip) {
            activityMeter.flash(movement.activity() < 100 ? Feedback::Raise : Feedback::Neutral);
            movement.adjustActivity(catnipActivityGain);
            treatEffect.play(treat);
            runEvent(CompanionEvent::Catnip);
        } else if (movement.activity() > 0) {
            activityMeter.flash(Feedback::Lower);
            movement.adjustActivity(-yarnActivityCost);
            treatEffect.play(treat);
            if (!runEvent(CompanionEvent::Yarn) && window.isVisible()
                && !playRoleAction(ActionSlot::Happy, false)) {
                window.playBounce();
            }
        } else {
            activityMeter.flash(Feedback::Neutral);
        }
    };
    treatCard->picked = [&](PetTreat treat) {
        menu.hide();
        useTreat(treat);
    };
    auto* happyAction = menu.addAction(QStringLiteral("播放开心"));
    QObject::connect(happyAction, &QAction::triggered, &window, [&] { playRoleAction(ActionSlot::Happy, true); });
    auto* sleepyAction = menu.addAction(QStringLiteral("播放困倦"));
    QObject::connect(sleepyAction, &QAction::triggered, &window, [&] { playRoleAction(ActionSlot::Sleepy, true); });
    auto* idleAction = menu.addAction(QStringLiteral("回到待机"));
    QObject::connect(idleAction, &QAction::triggered, &window, &PetWindow::returnToIdle);
#ifdef Q_OS_MACOS
    window.clickRequested = [&] {
        recordInteraction();
        chat.setRole(currentRole);
        chatOverlay.focusInput();
    };
    window.doubleClickRequested = [&] {
        if (focus.quiet()) {
            return;
        }
        chatOverlay.releaseInputFocus();
        chat.setRole(currentRole);
        const auto binding = companionEventsState.binding(CompanionEvent::DoubleClick);
        playEventAction(binding);
        if (!sayEventLine(binding)) {
            chat.speak();
        }
    };
    auto* voiceAction = menu.addAction(QStringLiteral("回复语音…"));
    QObject::connect(voiceAction, &QAction::triggered, &window, [&] {
        voice.configure(window, currentRole);
        chat.setVoiceFirst(voice.available());
    });
    auto* voiceTestAction = menu.addAction(QStringLiteral("试听发音"));
    voiceTestAction->setObjectName(QStringLiteral("testReplyVoice"));
    QObject::connect(voiceTestAction, &QAction::triggered, &window, [&] {
        chatOverlay.sayFixed(QStringLiteral("你好，桌宠语音已经就绪。"));
    });
    bool historyOpen = false;
    auto* chatAction = menu.addAction(QStringLiteral("聊天记录…"));
    QObject::connect(chatAction, &QAction::triggered, &window, [&] {
        if (historyOpen || currentRole.isEmpty()) {
            return;
        }
        if (!library.available()) {
            QMessageBox::warning(&window, QStringLiteral("无法读取聊天记录"), libraryError);
            return;
        }
        QScopedValueRollback<bool> opening(historyOpen, true);
        chatOverlay.closeInput();
        chat.setRole(currentRole);
        showRoleChatHistory(chat, library, settings, &window);
    });
#else
    window.clickRequested = recordInteraction;
    window.doubleClickRequested = [&] {
        if (!runEvent(CompanionEvent::DoubleClick)) {
            playRoleAction(ActionSlot::Happy, false);
        }
    };
#endif
    menu.addSeparator();
    const auto resizePet = [&](int scale) {
        window.setScalePercent(scale);
        saveLayout();
    };
    auto* zoomIn = menu.addAction(QStringLiteral("放大桌宠"));
    QObject::connect(zoomIn, &QAction::triggered, &window, [&] { resizePet(window.scalePercent() + 10); });
    auto* zoomOut = menu.addAction(QStringLiteral("缩小桌宠"));
    QObject::connect(zoomOut, &QAction::triggered, &window, [&] { resizePet(window.scalePercent() - 10); });
    auto* resetZoom = menu.addAction(QStringLiteral("恢复默认大小"));
    QObject::connect(resetZoom, &QAction::triggered, &window, [&] { resizePet(100); });
    movementAction = menu.addAction(QStringLiteral("随机移动"));
    movementAction->setCheckable(true);
    movementAction->setChecked(settings.value(roleMovementKey(currentRole),
        roleBehavior.value(QStringLiteral("randomMovement")).toBool(settings.value(randomMovementKey, true).toBool())).toBool());
    QObject::connect(movementAction, &QAction::toggled, &window, [&](bool enabled) {
        movement.setEnabled(enabled);
        settings.setValue(roleMovementKey(currentRole), enabled);
    });
    auto* displays = menu.addMenu(QStringLiteral("显示器"));
    auto* displayPolicies = new QActionGroup(displays);
    auto* currentDisplay = displays->addAction(QStringLiteral("停留在当前所在屏幕"));
    auto* primaryDisplay = displays->addAction(QStringLiteral("固定在主屏幕"));
    for (auto* action : {currentDisplay, primaryDisplay}) {
        action->setCheckable(true);
        displayPolicies->addAction(action);
    }
    primaryDisplay->setChecked(settings.value(screenPolicyKey, false).toBool());
    currentDisplay->setChecked(!primaryDisplay->isChecked());
    QObject::connect(displayPolicies, &QActionGroup::triggered, &window, [&](QAction* selected) {
        const bool primary = selected == primaryDisplay;
        settings.setValue(screenPolicyKey, primary);
        window.setPrimaryOnly(primary);
        clampToScreen();
    });
    displays->addSeparator();
    auto* toCursor = displays->addAction(QStringLiteral("移到鼠标所在屏幕"));
    QObject::connect(toCursor, &QAction::triggered, &window, [&] {
        if (const auto* screen = QGuiApplication::screenAt(QCursor::pos())) {
            if (primaryDisplay->isChecked()) {
                currentDisplay->trigger();
            }
            window.moveToDisplay(screen);
            saveLayout();
        }
    });
    auto* toPrimary = displays->addAction(QStringLiteral("移到主屏幕"));
    QObject::connect(toPrimary, &QAction::triggered, &window, [&] {
        window.moveToDisplay(QGuiApplication::primaryScreen());
        saveLayout();
    });
    auto* visibilityAction = menu.addAction(QStringLiteral("隐藏桌宠"));
    const auto toggleVisibility = [&] {
        if (window.isVisible()) {
            window.hide();
        } else {
            window.placeAt(window.pos());
            window.show();
        }
    };
    QObject::connect(visibilityAction, &QAction::triggered, &window, toggleVisibility);
#ifdef Q_OS_MACOS
    GlobalHotkeys hotkeys([&](GlobalHotkeys::Action action) {
        switch (action) {
        case GlobalHotkeys::Visibility: toggleVisibility(); break;
        case GlobalHotkeys::Focus: focus.start(std::chrono::minutes(25)); break;
        case GlobalHotkeys::Quiet: focus.setDoNotDisturb(!focus.doNotDisturb()); break;
        }
    });
    auto* keyMenu = menu.addMenu(QStringLiteral("全局快捷键"));
    auto* enabledKeys = keyMenu->addAction(QStringLiteral("启用全局快捷键"));
    enabledKeys->setCheckable(true);
    const auto hotkeysKey = QStringLiteral("pet/globalHotkeys");
    enabledKeys->setChecked(settings.value(hotkeysKey, true).toBool());
    auto* hotkeyStatus = keyMenu->addAction(QString{});
    hotkeyStatus->setEnabled(false);
    for (const auto& [action, label] : {std::pair{GlobalHotkeys::Visibility, QStringLiteral("显示／隐藏桌宠")},
            std::pair{GlobalHotkeys::Focus, QStringLiteral("开始 25 分钟专注")},
            std::pair{GlobalHotkeys::Quiet, QStringLiteral("切换免打扰")}}) {
        keyMenu->addAction(GlobalHotkeys::chord(action) + QStringLiteral("   ") + label)->setEnabled(false);
    }
    const auto updateKeys = [&](bool enabled) {
        const auto unavailable = hotkeys.setEnabled(enabled);
        hotkeyStatus->setVisible(!unavailable.isEmpty());
        hotkeyStatus->setText(QStringLiteral("未能注册 %1").arg(unavailable.join(QStringLiteral("、"))));
        settings.setValue(hotkeysKey, enabled);
    };
    QObject::connect(enabledKeys, &QAction::toggled, &window, updateKeys);
    updateKeys(enabledKeys->isChecked());
#endif
    const auto updateVisibility = [&] {
        movement.postpone();
#ifdef Q_OS_MACOS
        voiceTestAction->setEnabled(window.isVisible() && voice.available());
#endif
        visibilityAction->setText(window.isVisible() ? QStringLiteral("隐藏桌宠") : QStringLiteral("显示桌宠"));
        zoomIn->setEnabled(window.scalePercent() < 200);
        zoomOut->setEnabled(window.scalePercent() > 50);
        resetZoom->setEnabled(window.scalePercent() != 100);
    };
    QObject::connect(&menu, &QMenu::aboutToShow, &window, updateVisibility);
    auto* clickThroughAction = menu.addAction(QStringLiteral("鼠标穿透"));
    clickThroughAction->setCheckable(true);
    QObject::connect(clickThroughAction, &QAction::toggled, &window, &PetWindow::setClickThrough);
    menu.addSeparator();
    auto* quitAction = menu.addAction(QStringLiteral("退出 AuraPet"));
    quitAction->setMenuRole(QAction::NoRole);
    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);

    tether.menuRequested = [&menu](QPoint position) { menu.popup(position); };

    const auto setupNativeMenu = [&](QMenu& target) {
        auto* treats = target.addMenu(QStringLiteral("小食与玩具"));
        treats->setToolTipsVisible(true);
        for (const auto treat : petTreats) {
            auto* action = treats->addAction(treatIcon(treat), treatName(treat));
            action->setToolTip(treatDescription(treat));
            const bool kibble = treat == PetTreat::SlowKibble || treat == PetTreat::HighKibble;
            action->setCheckable(kibble);
            QObject::connect(action, &QAction::triggered, &window, [&, treat] { useTreat(treat); });
            if (kibble) {
                QObject::connect(treats, &QMenu::aboutToShow, action, [&, action, treat] {
                    action->setChecked(loadedKibble == treat);
                });
            }
        }
        target.addSeparator();
        for (auto* action : menu.actions().mid(2)) {
            if (action == companion) {
                addCompanionMenu(target, window, focus);
            } else if (action->isSeparator()) {
                target.addSeparator();
            } else {
                target.addAction(action);
            }
        }
        QObject::connect(&target, &QMenu::aboutToShow, &window, updateVisibility);
    };

#ifdef Q_OS_MACOS
    QMenuBar appMenuBar;
    appMenuBar.setNativeMenuBar(true);
    auto* petMenu = appMenuBar.addMenu(QStringLiteral("桌宠"));
    setupNativeMenu(*petMenu);
#endif

    QMenu trayMenu;
    setupNativeMenu(trayMenu);
    QSystemTrayIcon tray(menuBarIcon());
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
#ifndef Q_OS_MACOS
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        visibilityAction->setEnabled(false);
        clickThroughAction->setEnabled(false);
    }
#endif

    window.show();
    app.openPackage = [&](const QString& path) {
        QTimer::singleShot(0, &window, [&, path] { importPet(path, false); });
    };
    if (!arguments.isEmpty() && isRolePackage(arguments.constFirst())) {
        app.pendingPackages.append(arguments.constFirst());
    }
    app.pendingPackages.removeDuplicates();
    for (const auto& path : std::as_const(app.pendingPackages)) {
        app.openPackage(path);
    }
    return app.exec();
}
