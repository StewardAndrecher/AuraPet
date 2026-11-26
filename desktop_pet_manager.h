#pragma once

#include "companion_events.h"
#include "companion_menu.h"
#include "activity_meter.h"
#include "pet_tether.h"
#include "pet_treats.h"
#include "pet_window.h"
#include "random_movement.h"
#include "role_library.h"
#include "space_transit.h"
#include "chat_session.h"
#include "pet_chat_overlay.h"
#include "random_speech.h"
#include "speech_playback.h"

#ifdef Q_OS_MACOS
#include "mac_window_layer.h"
#endif

#include <QElapsedTimer>
#include <QJsonObject>
#include <QList>
#include <QMenu>
#include <QObject>
#include <QQueue>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

class FocusSession;
class PetQuickControls;
class PetStatusBanner;
class QAction;
class QScreen;
class SettingsStore;
class VoiceTraining;
struct ForegroundContext;

class DesktopPetManager;

class PetInstance : public QObject {
public:
    PetInstance(DesktopPetManager& manager, const QString& roleId);
    ~PetInstance() override;

    const QString& roleId() const { return roleId_; }
    const QString& roleName() const { return roleName_; }
    PetWindow& window() { return window_; }
    const PetWindow& window() const { return window_; }
    double activityPercent() const { return movement_.activity(); }

    bool initialize(const QList<QRect>& occupiedRects, QString& error);
    void refreshFromLibrary(bool reloadIdleAsset);
    void saveState(bool force = true);

    void syncQuietMode();
    void applyPrimaryOnly(bool primaryOnly);
    void clampToScreen();
    void moveToDisplay(const QScreen* screen);

    bool clickThrough() const;
    void setClickThrough(bool enabled);
    bool followAllSpaces() const { return followAllSpaces_; }
    void setFollowAllSpaces(bool enabled);
#ifdef Q_OS_MACOS
    void prepareSpaceTransit(const QPoint& landingPos);
    void startSpaceTransit(const SpaceTransitPlan& plan);
    void cancelSpaceTransit(bool clampToBounds = true);
    bool isSpaceTransiting() const { return spaceTransitActive_; }
    QPoint preferredSpaceLandingPos() const {
        return spaceTransitActive_ ? spaceTransitPlan_.landingPos.toPoint() : window_.pos();
    }
#endif
    bool alwaysOnTop() const { return alwaysOnTop_; }
    void setAlwaysOnTop(bool enabled);
    bool roaming() const;
    void setRoaming(bool enabled);
    void setActivityMeterEnabled(bool enabled);
    CompanionMode companionMode() const { return companionMode_; }
    EffectiveRhythm effectiveRhythm() const { return effectiveRhythm_; }
    void setCompanionMode(CompanionMode mode, bool persist = true);
    void syncCompanionRhythm();
    void syncCompanionRhythm(const ForegroundContext& fg, const QDateTime& now);
    std::optional<PetTreat> loadedKibble() const { return loadedKibble_; }

    void useTreat(PetTreat treat);
    bool playRoleAction(ActionSlot slot, bool explainMissing);
    bool playEventAction(const QJsonObject& binding);
    bool sayEventLine(const QJsonObject& binding, bool atFocusStart = false);
    bool runBinding(const QJsonObject& binding, bool atFocusStart = false);
    bool runEvent(CompanionEvent event);
    void handleFocusCompleted(bool announceBanner, bool allowFallbackVoice);
    void announceBanner(const QString& text);
    bool pollReminder(const QDateTime& now, bool anyVoiceBusy);
    bool isVoiceBusy() const;

    void openRoleCardDialog();
    void openActionsDialog();
    void resizePet(int scalePercent);
    void configureVoice();
    void testVoice();
    void openChatHistory();

    void populateSubmenu(QMenu& sub);

private:
    void buildContextMenu();
    void selectKibble(const QString& kibble, bool persist);
    void scheduleNextProactive(const QDateTime& now);
    bool triggerProactiveSpeech(const QDateTime& now);
    QString activeOverrideTag() const;
    void updateMenuState();
    QString resolveRoleName() const;
#ifdef Q_OS_MACOS
    void tickSpaceTransit();
#endif

    DesktopPetManager& manager_;
    QString roleId_;
    QString roleName_;
    PetWindow window_;
    FocusNotice focusNotice_;
    ActivityMeter activityMeter_;
    TreatEffect treatEffect_;
    RandomMovement movement_;
    PetTether tether_;
    CompanionEvents companionEvents_;
    CompanionMode companionMode_ = CompanionMode::Auto;
    EffectiveRhythm effectiveRhythm_ = EffectiveRhythm::Daily;
    QDateTime nextProactiveAt_;
    std::optional<PetTreat> loadedKibble_;
    QJsonObject roleBehavior_;

    ChatSession chat_;
    SpeechPlayback voice_;
    PetChatOverlay chatOverlay_;
    RandomSpeechDraw speechDraw_;
    bool chatActivity_ = false;
    bool historyOpen_ = false;
    QAction* voiceTestAction_ = nullptr;
#ifdef Q_OS_MACOS
    QTimer spaceTransitTimer_;
    QElapsedTimer spaceTransitClock_;
    SpaceTransitPlan spaceTransitPlan_;
    bool spaceTransitActive_ = false;
#endif

    QMenu contextMenu_;
    PetStatusBanner* statusBanner_ = nullptr;
    TreatCard* treatCard_ = nullptr;
    PetQuickControls* quickControls_ = nullptr;
    QAction* scaleInfoAction_ = nullptr;
    QAction* topLayerRadio_ = nullptr;
    QAction* secondLayerRadio_ = nullptr;
    QAction* dismissAction_ = nullptr;
    QAction* visibilityAction_ = nullptr;
    QAction* zoomInAction_ = nullptr;
    QAction* zoomOutAction_ = nullptr;
    QAction* resetZoomAction_ = nullptr;
    QAction* currentDisplayAction_ = nullptr;
    QAction* primaryDisplayAction_ = nullptr;
    QPoint lastSavedPos_{-99999, -99999};
    int lastSavedScale_ = -1;
    double lastSavedActivity_ = -100.0;
    bool roaming_ = true;
    bool followAllSpaces_ = true;
    bool alwaysOnTop_ = true;
    bool editingActions_ = false;
};

class DesktopPetManager : public QObject {
public:
    DesktopPetManager(RoleLibrary& library, const QString& libraryError,
                      SettingsStore& stored, FocusSession& focus,
                      VoiceTraining& voiceTraining, QString builtInPath);
    ~DesktopPetManager() override;

    RoleLibrary& library() { return library_; }
    const QString& libraryError() const { return libraryError_; }
    SettingsStore& stored() { return stored_; }
    FocusSession& focus() { return focus_; }
    VoiceTraining& voiceTraining() { return voiceTraining_; }
    const QString& builtInPath() const { return builtInPath_; }

    bool startInitialPets(const QString& commandLineAsset, const QString& legacyPath,
                          const QString& soleWorkingGif, QString& error);

    qsizetype petCount() const { return static_cast<qsizetype>(pets_.size()); }
    int petIndex(const QString& roleId) const;
    QStringList activeRoleIds() const;
    bool isDesktopHidden() const { return hidden_; }
    void setDesktopHidden(bool hidden);
    void toggleDesktopVisibility();
    void showAndRaiseAll();

    bool anyClickThrough() const;
    void setAllClickThrough(bool enabled);
    bool allFollowSpaces() const;
    void setAllFollowSpaces(bool enabled);
    SpaceTransitMode spaceTransitMode() const;
    void setSpaceTransitMode(SpaceTransitMode mode);
    bool allAlwaysOnTop() const;
    void setAllAlwaysOnTop(bool enabled);
    bool showActivityMeter() const;
    void setShowActivityMeter(bool enabled);
    FocusMotionPolicy focusMotionPolicy() const;
    void setFocusMotionPolicy(FocusMotionPolicy policy);
    ForegroundContext currentForegroundContext() const;
    void setPrimaryScreenOnly(bool primaryOnly);
#ifdef Q_OS_MACOS
    MacWindowLayerCoordinator& windowLayers() { return windowLayers_; }
#endif
    void moveAllToDisplay(const QScreen* screen);
    void clampAllToScreen();
    void saveAllStates();

    void applyActiveRoles(const QStringList& requestedIds, bool multiPetEnabled);
    void dismissRole(const QString& roleId);
    void refreshRole(const QString& roleId, bool reloadIdleAsset = true);
    void refreshAllMetadata();
    void handleRoleRemoved(const QString& roleId);

    void openRoleLibrary(const QString& preferredRole = {});
    void promptFirstPetImport();
    void pickAndImportAsset(bool preview);
    void enqueueImport(const QString& path, bool preview);
    void backupLibrary(QWidget* parent = nullptr);

    void setHotkeyMenuBuilder(std::function<void(QMenu&)> builder) { hotkeyMenuBuilder_ = std::move(builder); }
    void setupGlobalMenu(QMenu& target);
    void populateAppSettingsMenu(QMenu& settingsMenu, QWidget* dialogParent = nullptr);
    void recordInteraction();

private:
    PetInstance* findPet(const QString& roleId) const;
    bool spawnPet(const QString& roleId, QString& error);
    void drainImports();
    void importPath(const QString& path, bool preview);
    void checkReminders();
    void watchScreen(QScreen* screen);

    RoleLibrary& library_;
    QString libraryError_;
    SettingsStore& stored_;
    FocusSession& focus_;
    VoiceTraining& voiceTraining_;
    QString builtInPath_;

    std::vector<std::unique_ptr<PetInstance>> pets_;
#ifdef Q_OS_MACOS
    MacWindowLayerCoordinator windowLayers_;
#endif
    std::function<void(QMenu&)> hotkeyMenuBuilder_;
    QQueue<QPair<QString, bool>> pendingImports_;
    QString workingImport_;
    QTimer reminderTimer_;
    int reminderCursor_ = 0;
    bool drainingImports_ = false;
    bool libraryDialogOpen_ = false;
    bool hidden_ = false;
};
