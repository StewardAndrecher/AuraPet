#include "mac_window_layer.h"
#include "pet_window.h"

#include <QApplication>
#include <QChildEvent>
#include <QEvent>
#include <QGuiApplication>
#include <QHash>
#include <QSet>
#include <QWidget>
#include <QWindow>

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#include <unistd.h>

namespace {
struct SpacesPlistLayout {
    QStringList orderedKeys;
    QList<bool> orderedFullScreen;
    QHash<QString, int> keyToIndex;
    int plistCurrentIndex = -1;
    QString plistCurrentKey;
    bool plistFullScreen = false;
    QHash<uint32_t, int> windowSpaceCount;
    QHash<uint32_t, QString> windowUniqueSpace;
    bool valid = false;
};

struct LiveWindowServerScan {
    uint32_t activeWallpaperId = 0;
    QSet<uint32_t> aliveWallpaperIds;
    QHash<uint32_t, QString> wallpaperKeyHint;
    QSet<uint32_t> aliveNormalWins;
    QSet<uint32_t> onScreenNormalWins;
};

bool isCocoaPlatform()
{
    return QGuiApplication::platformName() == QLatin1String("cocoa");
}

QString normalizedUuidString(NSString* uuid)
{
    if (![uuid isKindOfClass:[NSString class]]) {
        return {};
    }
    return uuid.length > 0 ? QString::fromNSString(uuid) : QStringLiteral("__primary_empty_uuid__");
}

QString spaceIdentifierFromDict(NSDictionary* dict)
{
    if (![dict isKindOfClass:[NSDictionary class]]) {
        return {};
    }
    NSString* uuid = dict[@"uuid"];
    if ([uuid isKindOfClass:[NSString class]]) {
        return normalizedUuidString(uuid);
    }
    NSNumber* idNum = dict[@"ManagedSpaceID"] ?: dict[@"id64"];
    if ([idNum isKindOfClass:[NSNumber class]]) {
        return QString::number([idNum longLongValue]);
    }
    return {};
}

SpacesPlistLayout readSpacesPlistLayout()
{
    SpacesPlistLayout layout;
    if (!isCocoaPlatform()) {
        return layout;
    }

    CFPreferencesAppSynchronize(CFSTR("com.apple.spaces"));
    CFPropertyListRef rawConfig =
        CFPreferencesCopyAppValue(CFSTR("SpacesDisplayConfiguration"), CFSTR("com.apple.spaces"));
    if (!rawConfig) {
        return layout;
    }
    NSDictionary* cfg = (__bridge_transfer NSDictionary*)rawConfig;
    if (![cfg isKindOfClass:[NSDictionary class]]) {
        return layout;
    }

    NSDictionary* mgmt = cfg[@"Management Data"];
    NSArray* monitors = [mgmt isKindOfClass:[NSDictionary class]] ? mgmt[@"Monitors"] : nil;
    if (![monitors isKindOfClass:[NSArray class]]) {
        return layout;
    }

    NSDictionary* chosenMonitor = nil;
    for (NSDictionary* mon in monitors) {
        if (![mon isKindOfClass:[NSDictionary class]]) {
            continue;
        }
        if ([mon[@"Spaces"] isKindOfClass:[NSArray class]]) {
            chosenMonitor = mon;
            if ([mon[@"Display Identifier"] isEqualToString:@"Main"]) {
                break;
            }
        }
    }
    if (!chosenMonitor) {
        return layout;
    }

    NSArray* spacesList = chosenMonitor[@"Spaces"];
    NSDictionary* currentSpace = chosenMonitor[@"Current Space"];
    for (NSDictionary* sp in spacesList) {
        const QString key = spaceIdentifierFromDict(sp);
        if (key.isEmpty()) {
            continue;
        }
        const int idx = static_cast<int>(layout.orderedKeys.size());
        layout.orderedKeys.append(key);
        const int type = [sp[@"type"] isKindOfClass:[NSNumber class]] ? [sp[@"type"] intValue] : 0;
        layout.orderedFullScreen.append(type != 0);
        layout.keyToIndex.insert(key, idx);
    }

    layout.plistCurrentKey = spaceIdentifierFromDict(currentSpace);
    layout.plistCurrentIndex = layout.keyToIndex.value(layout.plistCurrentKey, -1);
    layout.plistFullScreen = (layout.plistCurrentIndex >= 0)
        ? layout.orderedFullScreen[layout.plistCurrentIndex]
        : ([currentSpace[@"type"] isKindOfClass:[NSNumber class]] && [currentSpace[@"type"] intValue] != 0);

    NSArray* spaceProps = cfg[@"Space Properties"];
    if ([spaceProps isKindOfClass:[NSArray class]] && !layout.keyToIndex.isEmpty()) {
        for (NSDictionary* prop in spaceProps) {
            if (![prop isKindOfClass:[NSDictionary class]]) {
                continue;
            }
            NSString* name = prop[@"name"];
            NSArray* winIds = prop[@"windows"];
            if (![name isKindOfClass:[NSString class]] || ![winIds isKindOfClass:[NSArray class]]) {
                continue;
            }
            const QString spKey = normalizedUuidString(name);
            if (!layout.keyToIndex.contains(spKey)) {
                continue;
            }
            for (NSNumber* num in winIds) {
                if ([num isKindOfClass:[NSNumber class]]) {
                    const uint32_t wid = [num unsignedIntValue];
                    layout.windowSpaceCount[wid] += 1;
                    layout.windowUniqueSpace.insert(wid, spKey);
                }
            }
        }
    }

    layout.valid = !layout.orderedKeys.isEmpty();
    return layout;
}

LiveWindowServerScan scanLiveWindows(pid_t selfPid)
{
    LiveWindowServerScan scan;
    const int wallpaperLayer = static_cast<int>(CGWindowLevelForKey(kCGDesktopWindowLevelKey)) - 1;
    CFArrayRef allList = CGWindowListCopyWindowInfo(kCGWindowListOptionAll, kCGNullWindowID);
    if (!allList) {
        return scan;
    }

    uint32_t fallbackOnScreenWallpaper = 0;
    const CFIndex count = CFArrayGetCount(allList);
    for (CFIndex i = 0; i < count; ++i) {
        NSDictionary* info = (__bridge NSDictionary*)CFArrayGetValueAtIndex(allList, i);
        const int layer = [info[(__bridge NSString*)kCGWindowLayer] intValue];
        const uint32_t wid = [info[(__bridge NSString*)kCGWindowNumber] unsignedIntValue];
        if (wid == 0) {
            continue;
        }
        const BOOL onScreen = [info[(__bridge NSString*)kCGWindowIsOnscreen] boolValue];
        CGRect bounds = CGRectZero;
        CFDictionaryRef boundsDict = (__bridge CFDictionaryRef)info[(__bridge NSString*)kCGWindowBounds];
        const bool hasBounds = boundsDict && CGRectMakeWithDictionaryRepresentation(boundsDict, &bounds);

        if (layer == wallpaperLayer) {
            NSString* name = info[(__bridge NSString*)kCGWindowName];
            const bool isNamedWallpaper = [name isKindOfClass:[NSString class]] && [name hasPrefix:@"Wallpaper-"];
            NSString* ownerName = info[(__bridge NSString*)kCGWindowOwnerName];
            if (!isNamedWallpaper && [ownerName isKindOfClass:[NSString class]]
                && !(hasBounds && bounds.size.width >= 300.0 && bounds.size.height >= 200.0)) {
                continue;
            }
            scan.aliveWallpaperIds.insert(wid);
            if (isNamedWallpaper) {
                NSString* suffix = [name substringFromIndex:10];
                scan.wallpaperKeyHint.insert(wid, normalizedUuidString(suffix));
            }
            if (onScreen) {
                if (hasBounds && std::abs(bounds.origin.x) < 1.5 && std::abs(bounds.origin.y) < 1.5) {
                    scan.activeWallpaperId = wid;
                } else if (fallbackOnScreenWallpaper == 0) {
                    fallbackOnScreenWallpaper = wid;
                }
            }
            continue;
        }

        if (layer != kCGNormalWindowLevel) {
            continue;
        }
        const pid_t ownerPid = [info[(__bridge NSString*)kCGWindowOwnerPID] intValue];
        if (ownerPid == selfPid) {
            continue;
        }
        if (!hasBounds || bounds.size.width < 60.0 || bounds.size.height < 60.0) {
            continue;
        }
        scan.aliveNormalWins.insert(wid);
        if (onScreen) {
            scan.onScreenNormalWins.insert(wid);
        }
    }

    if (scan.activeWallpaperId == 0) {
        scan.activeWallpaperId = fallbackOnScreenWallpaper;
    }
    CFRelease(allList);
    return scan;
}

void pruneDeadWallpapers(QHash<uint32_t, QString>& map, const QSet<uint32_t>& aliveWallpapers)
{
    if (aliveWallpapers.isEmpty()) {
        return;
    }
    for (auto it = map.begin(); it != map.end();) {
        if (!aliveWallpapers.contains(it.key())) {
            it = map.erase(it);
        } else {
            ++it;
        }
    }
}

NSWindow* nativeWindowFor(const QWidget& widget)
{
    if (!isCocoaPlatform() || !widget.testAttribute(Qt::WA_WState_Created)) {
        return nil;
    }
    const WId handle = const_cast<QWidget&>(widget).internalWinId();
    if (!handle) {
        return nil;
    }
    NSView* view = (__bridge NSView*)reinterpret_cast<void*>(handle);
    return view ? [view window] : nil;
}

void setWindowNativeLevel(NSWindow* nsWin, NSWindowLevel targetLevel)
{
    if (!nsWin) {
        return;
    }
    if ([nsWin isKindOfClass:[NSPanel class]]) {
        NSPanel* panel = static_cast<NSPanel*>(nsWin);
        const BOOL shouldFloat = (targetLevel >= NSFloatingWindowLevel);
        if ([panel isFloatingPanel] != shouldFloat) {
            [panel setFloatingPanel:shouldFloat];
        }
        if ([panel hidesOnDeactivate]) {
            [panel setHidesOnDeactivate:NO];
        }
        if (([panel styleMask] & NSWindowStyleMaskNonactivatingPanel) || ![panel canBecomeKeyWindow]) {
            [panel setBecomesKeyOnlyIfNeeded:YES];
        }
    }
    if ([nsWin level] != targetLevel) {
        [nsWin setLevel:targetLevel];
    }
}

void setWindowSpaceCollection(NSWindow* nsWin, bool canJoinAllSpaces, bool moveToActiveSpace)
{
    if (!nsWin) {
        return;
    }
    const NSWindowCollectionBehavior behavior = [nsWin collectionBehavior];
    const NSWindowCollectionBehavior clearMask =
        NSWindowCollectionBehaviorCanJoinAllSpaces
        | NSWindowCollectionBehaviorMoveToActiveSpace
        | NSWindowCollectionBehaviorManaged
        | NSWindowCollectionBehaviorTransient
        | NSWindowCollectionBehaviorStationary
        | NSWindowCollectionBehaviorParticipatesInCycle
        | NSWindowCollectionBehaviorIgnoresCycle
        | NSWindowCollectionBehaviorFullScreenPrimary
        | NSWindowCollectionBehaviorFullScreenAuxiliary
        | NSWindowCollectionBehaviorFullScreenNone
        | NSWindowCollectionBehaviorFullScreenAllowsTiling
        | NSWindowCollectionBehaviorFullScreenDisallowsTiling;

    NSWindowCollectionBehavior desired = behavior & ~clearMask;
    if (canJoinAllSpaces) {
        desired |= (NSWindowCollectionBehaviorCanJoinAllSpaces
                    | NSWindowCollectionBehaviorTransient
                    | NSWindowCollectionBehaviorIgnoresCycle
                    | NSWindowCollectionBehaviorFullScreenAuxiliary
                    | NSWindowCollectionBehaviorFullScreenDisallowsTiling);
    } else {
        desired |= (NSWindowCollectionBehaviorManaged
                    | NSWindowCollectionBehaviorIgnoresCycle
                    | NSWindowCollectionBehaviorFullScreenNone
                    | NSWindowCollectionBehaviorFullScreenDisallowsTiling);
        if (moveToActiveSpace) {
            desired |= NSWindowCollectionBehaviorMoveToActiveSpace;
        }
    }
    if (desired != behavior) {
        [nsWin setCollectionBehavior:desired];
    }
}

QList<QWidget*> childOverlayWindows(QWidget& petWidget)
{
    QList<QWidget*> overlays;
    for (QObject* child : petWidget.children()) {
        auto* w = qobject_cast<QWidget*>(child);
        if (!w || !w->isWindow()) {
            continue;
        }
        const auto type = w->windowType();
        if (type == Qt::Dialog || type == Qt::Sheet || type == Qt::Popup || type == Qt::SplashScreen) {
            continue;
        }
        overlays.append(w);
    }
    return overlays;
}
}

bool macWindowJoinsAllSpaces(const QWidget& widget)
{
    NSWindow* nsWin = nativeWindowFor(widget);
    if (!nsWin) {
        return false;
    }
    return ([nsWin collectionBehavior] & NSWindowCollectionBehaviorCanJoinAllSpaces) != 0;
}

bool macWindowIsSpaceManaged(const QWidget& widget)
{
    NSWindow* nsWin = nativeWindowFor(widget);
    if (!nsWin) {
        return false;
    }
    return ([nsWin collectionBehavior] & NSWindowCollectionBehaviorManaged) != 0;
}

bool macWindowMovesToActiveSpace(const QWidget& widget)
{
    NSWindow* nsWin = nativeWindowFor(widget);
    if (!nsWin) {
        return false;
    }
    return ([nsWin collectionBehavior] & NSWindowCollectionBehaviorMoveToActiveSpace) != 0;
}

int macWindowNativeLevel(const QWidget& widget)
{
    NSWindow* nsWin = nativeWindowFor(widget);
    return nsWin ? static_cast<int>([nsWin level]) : 0;
}

MacWindowLayerCoordinator::MacWindowLayerCoordinator(QObject* parent)
    : QObject(parent)
{
    pollTimer_.setTimerType(Qt::CoarseTimer);
    pollTimer_.setInterval(320);
    connect(&pollTimer_, &QTimer::timeout, this, [this] { syncAll(false); });

    settleTimer_.setSingleShot(true);
    settleTimer_.setInterval(140);
    connect(&settleTimer_, &QTimer::timeout, this, [this] { syncAll(false); });

    spaceOrderRefreshTimer_.setSingleShot(true);
    spaceOrderRefreshTimer_.setInterval(650);
    connect(&spaceOrderRefreshTimer_, &QTimer::timeout, this, [this] { refreshOrderedSpaceKeys(); });

    if (!isCocoaPlatform()) {
        return;
    }

    const SpacesPlistLayout initialPlist = readSpacesPlistLayout();
    if (initialPlist.valid) {
        lastOrderedSpaceKeys_ = initialPlist.orderedKeys;
    }
    bool dummyRetry = false;
    bool dummyFullScreen = false;
    uint32_t initialWallpaper = 0;
    QString initialKey;
    const int initialIndex = resolveCurrentSpaceIndex(false, dummyRetry, dummyFullScreen, initialWallpaper, initialKey);
    lastWallpaperId_ = initialWallpaper;
    lastSpaceKey_ = initialKey;
    lastSpaceIndex_ = initialIndex;
    lastSpaceWasFullScreen_ = dummyFullScreen && (initialWallpaper == 0);

    NSNotificationCenter* wsCenter = [[NSWorkspace sharedWorkspace] notificationCenter];
    MacWindowLayerCoordinator* selfPtr = this;

    id activateToken = [wsCenter addObserverForName:NSWorkspaceDidActivateApplicationNotification
                                             object:nil
                                              queue:[NSOperationQueue mainQueue]
                                         usingBlock:^(NSNotification*) {
                                             selfPtr->syncAll(false);
                                             selfPtr->settleTimer_.start();
                                             selfPtr->spaceOrderRefreshTimer_.start();
                                         }];
    appActivateObserver_ = (__bridge_retained void*)activateToken;

    id spaceToken = [wsCenter addObserverForName:NSWorkspaceActiveSpaceDidChangeNotification
                                          object:nil
                                           queue:[NSOperationQueue mainQueue]
                                      usingBlock:^(NSNotification*) {
                                          selfPtr->handleSpaceChangeNotification(0, ++selfPtr->spaceChangeGeneration_);
                                      }];
    spaceChangeObserver_ = (__bridge_retained void*)spaceToken;
}

MacWindowLayerCoordinator::~MacWindowLayerCoordinator()
{
    if (!appActivateObserver_ && !spaceChangeObserver_) {
        return;
    }
    NSNotificationCenter* wsCenter = [[NSWorkspace sharedWorkspace] notificationCenter];
    if (appActivateObserver_) {
        id token = (__bridge_transfer id)appActivateObserver_;
        [wsCenter removeObserver:token];
        appActivateObserver_ = nullptr;
    }
    if (spaceChangeObserver_) {
        id token = (__bridge_transfer id)spaceChangeObserver_;
        [wsCenter removeObserver:token];
        spaceChangeObserver_ = nullptr;
    }
}

void MacWindowLayerCoordinator::watchWidgetTree(QWidget& root)
{
    root.removeEventFilter(this);
    root.installEventFilter(this);
    for (auto* overlay : childOverlayWindows(root)) {
        overlay->removeEventFilter(this);
        overlay->installEventFilter(this);
    }
}

void MacWindowLayerCoordinator::registerPet(PetWindow& pet, bool followAllSpaces, bool alwaysOnTop)
{
    unregisterPet(pet);
    entries_.append({&pet, followAllSpaces, alwaysOnTop, false, false});
    watchWidgetTree(pet);
    pet.setAlwaysOnTop(alwaysOnTop);
    // Keep resting collection behavior non-sticky on normal desktops so Space swipes animate naturally
    applySpaceBehavior(pet, false, false);
    updatePollingState();
    syncAll();
}

void MacWindowLayerCoordinator::unregisterPet(PetWindow& pet)
{
    pet.removeEventFilter(this);
    for (auto* overlay : childOverlayWindows(pet)) {
        overlay->removeEventFilter(this);
    }
    entries_.removeIf([&pet](const ManagedPetLayer& item) {
        return !item.pet || item.pet == &pet;
    });
    updatePollingState();
}

void MacWindowLayerCoordinator::updatePetPolicy(PetWindow& pet, bool followAllSpaces, bool alwaysOnTop)
{
    for (auto& entry : entries_) {
        if (entry.pet == &pet) {
            const bool disablingFollow = entry.followAllSpaces && !followAllSpaces;
            const bool enablingTop = !entry.alwaysOnTop && alwaysOnTop;
            const bool disablingTop = entry.alwaysOnTop && !alwaysOnTop;
            entry.followAllSpaces = followAllSpaces;
            entry.alwaysOnTop = alwaysOnTop;
            if (!followAllSpaces) {
                entry.stickyFullScreenFallback = false;
            }
            pet.setAlwaysOnTop(alwaysOnTop);
            watchWidgetTree(pet);
            applySpaceBehavior(pet, entry.stickyFullScreenFallback, disablingFollow);
            if (disablingFollow) {
                QPointer<PetWindow> guarded(&pet);
                QTimer::singleShot(80, this, [this, guarded] {
                    if (guarded) {
                        for (const auto& item : std::as_const(entries_)) {
                            if (item.pet == guarded && !item.stickyFullScreenFallback) {
                                applySpaceBehavior(*guarded, false, false);
                            }
                        }
                    }
                });
            }
            if (enablingTop) {
                if (NSWindow* nsWin = nativeWindowFor(pet)) {
                    setWindowNativeLevel(nsWin, NSFloatingWindowLevel);
                    [nsWin orderFrontRegardless];
                }
            }
            updatePollingState();
            syncAll(disablingTop);
            if (disablingTop) {
                QTimer::singleShot(60, this, [this] { syncAll(true); });
            }
            return;
        }
    }
    registerPet(pet, followAllSpaces, alwaysOnTop);
}

bool MacWindowLayerCoordinator::isFollowingAllSpaces(const PetWindow& pet) const
{
    for (const auto& entry : entries_) {
        if (entry.pet == &pet) {
            return entry.followAllSpaces;
        }
    }
    return false;
}

void MacWindowLayerCoordinator::movePetToActiveSpace(PetWindow& pet, const QPoint& entryPos, bool targetIsFullScreen)
{
    ManagedPetLayer* targetEntry = nullptr;
    for (auto& entry : entries_) {
        if (entry.pet == &pet) {
            targetEntry = &entry;
            break;
        }
    }
    if (!targetEntry) {
        return;
    }

    pet.setAttribute(Qt::WA_ShowWithoutActivating, true);
    pet.move(entryPos);
    NSWindow* petWin = nativeWindowFor(pet);
    if (!petWin) {
        return;
    }

    if (targetIsFullScreen) {
        targetEntry->stickyFullScreenFallback = true;
        setWindowSpaceCollection(petWin, true, false);
        setWindowNativeLevel(petWin, NSFloatingWindowLevel);
        [petWin orderFrontRegardless];
        for (auto* overlay : childOverlayWindows(pet)) {
            if (NSWindow* ovWin = nativeWindowFor(*overlay); ovWin && ovWin != petWin) {
                setWindowSpaceCollection(ovWin, true, false);
                if (overlay->isVisible()) {
                    setWindowNativeLevel(ovWin, NSFloatingWindowLevel);
                    [ovWin orderFrontRegardless];
                }
            }
        }
        return;
    }

    targetEntry->stickyFullScreenFallback = false;
    const NSWindowLevel desiredLevel = targetEntry->alwaysOnTop ? NSFloatingWindowLevel : NSNormalWindowLevel;

    // Relocate from outgoing Space directly onto the currently active Space without stealing app focus
    setWindowSpaceCollection(petWin, false, true);
    [petWin orderOut:nil];
    setWindowNativeLevel(petWin, desiredLevel);
    [petWin orderFrontRegardless];

    for (auto* overlay : childOverlayWindows(pet)) {
        if (NSWindow* ovWin = nativeWindowFor(*overlay); ovWin && ovWin != petWin) {
            setWindowSpaceCollection(ovWin, false, true);
            if (overlay->isVisible()) {
                [ovWin orderOut:nil];
                setWindowNativeLevel(ovWin, desiredLevel);
                [ovWin orderFrontRegardless];
            } else if (![ovWin isOnActiveSpace]) {
                if (QWindow* ovHandle = overlay->windowHandle()) {
                    ovHandle->setTransientParent(nullptr);
                    ovHandle->destroy();
                }
            }
        }
    }

    const bool landedOnActive = [petWin isOnActiveSpace];
    // Immediately clear MoveToActiveSpace so WindowServer anchors this window to the new Space cleanly
    setWindowSpaceCollection(petWin, false, false);

    // If MoveToActiveSpace was ignored while AuraPet is inactive or mid-swipe, recreate platform handle on active Space
    if (!landedOnActive) {
        struct TransientHideGuard {
            PetWindow& target;
            explicit TransientHideGuard(PetWindow& w) : target(w)
            {
                target.setProperty("auraTransientHide", true);
            }
            ~TransientHideGuard()
            {
                target.setProperty("auraTransientHide", false);
            }
        } guard(pet);

        QList<QWidget*> visibleOverlays;
        for (auto* overlay : childOverlayWindows(pet)) {
            if (overlay->isVisible()) {
                visibleOverlays.append(overlay);
                overlay->hide();
            }
            if (QWindow* ovHandle = overlay->windowHandle()) {
                ovHandle->setTransientParent(nullptr);
            }
        }
        pet.hide();
        if (QWindow* handle = pet.windowHandle()) {
            handle->destroy();
        }
        pet.setAttribute(Qt::WA_ShowWithoutActivating, true);
        pet.move(entryPos);
        (void)pet.winId();
        if (NSWindow* freshWin = nativeWindowFor(pet)) {
            setWindowSpaceCollection(freshWin, false, false);
            setWindowNativeLevel(freshWin, desiredLevel);
        }
        pet.show();
        if (NSWindow* freshWin = nativeWindowFor(pet)) {
            setWindowSpaceCollection(freshWin, false, false);
            setWindowNativeLevel(freshWin, desiredLevel);
            [freshWin orderFrontRegardless];
        }
        for (auto* overlay : childOverlayWindows(pet)) {
            if (QWindow* ovHandle = overlay->windowHandle()) {
                ovHandle->setTransientParent(pet.windowHandle());
            }
        }
        for (auto* overlay : visibleOverlays) {
            overlay->show();
            if (NSWindow* ovWin = nativeWindowFor(*overlay)) {
                setWindowSpaceCollection(ovWin, false, true);
                setWindowNativeLevel(ovWin, desiredLevel);
                [ovWin orderFrontRegardless];
            }
        }
    }

    if (!targetEntry->alwaysOnTop) {
        syncAll(true);
    }
}

void MacWindowLayerCoordinator::triggerSpaceSwitchForTesting(int spaceDelta)
{
    QList<PetWindow*> followers;
    for (const auto& entry : std::as_const(entries_)) {
        if (entry.pet && entry.pet->isVisible() && entry.followAllSpaces) {
            followers.append(entry.pet);
        }
    }
    if (spaceSwitched && !followers.isEmpty()) {
        spaceSwitched(followers, spaceDelta);
    }
    syncAll(false);
}

int MacWindowLayerCoordinator::resolveCurrentSpaceIndex(bool allowDefer, bool& outNeedsRetry,
                                                        bool& outIsFullScreen,
                                                        uint32_t& outWallpaperId, QString& outSpaceKey)
{
    outNeedsRetry = false;
    outIsFullScreen = false;
    outWallpaperId = 0;
    outSpaceKey.clear();

    const SpacesPlistLayout plist = readSpacesPlistLayout();
    const LiveWindowServerScan live = scanLiveWindows(getpid());
    outWallpaperId = live.activeWallpaperId;

    if (!plist.valid) {
        outIsFullScreen = plist.plistFullScreen;
        outSpaceKey = plist.plistCurrentKey;
        return -1;
    }

    const int count = static_cast<int>(plist.orderedKeys.size());
    pruneDeadWallpapers(wallpaperIdToKey_, live.aliveWallpaperIds);
    for (auto it = wallpaperIdToKey_.begin(); it != wallpaperIdToKey_.end();) {
        if (!plist.keyToIndex.contains(it.value())) {
            it = wallpaperIdToKey_.erase(it);
        } else {
            ++it;
        }
    }

    // Opportunistically learn wallpaper ID -> Space UUID mappings (invariant under macOS MRU Space reordering)
    for (auto it = live.wallpaperKeyHint.cbegin(); it != live.wallpaperKeyHint.cend(); ++it) {
        if (plist.keyToIndex.contains(it.value())) {
            wallpaperIdToKey_.insert(it.key(), it.value());
        }
    }

    QList<int> aliveUniqueCount(count, 0);
    QList<int> onScreenMatchCount(count, 0);
    for (auto it = plist.windowUniqueSpace.cbegin(); it != plist.windowUniqueSpace.cend(); ++it) {
        const uint32_t wid = it.key();
        if (plist.windowSpaceCount.value(wid, 0) != 1 || !live.aliveNormalWins.contains(wid)) {
            continue;
        }
        const int idx = plist.keyToIndex.value(it.value(), -1);
        if (idx >= 0 && idx < count) {
            aliveUniqueCount[idx] += 1;
            if (live.onScreenNormalWins.contains(wid)) {
                onScreenMatchCount[idx] += 1;
            }
        }
    }

    auto contradicted = [&](int idx) {
        return idx < 0 || idx >= count || (aliveUniqueCount[idx] > 0 && onScreenMatchCount[idx] == 0);
    };

    int resolvedIdx = -1;

    // Priority 1: Live unique window proof
    int bestVotes = 0;
    int bestIdx = -1;
    bool tied = false;
    for (int idx = 0; idx < count; ++idx) {
        if (onScreenMatchCount[idx] > bestVotes) {
            bestVotes = onScreenMatchCount[idx];
            bestIdx = idx;
            tied = false;
        } else if (onScreenMatchCount[idx] > 0 && onScreenMatchCount[idx] == bestVotes) {
            tied = true;
        }
    }
    if (!tied && bestVotes > 0 && bestIdx >= 0) {
        resolvedIdx = bestIdx;
    }

    // Priority 2: Cached wallpaper-ID -> Space UUID bijection (immune to macOS MRU reordering!)
    if (resolvedIdx < 0 && live.activeWallpaperId != 0 && wallpaperIdToKey_.contains(live.activeWallpaperId)) {
        const QString cachedKey = wallpaperIdToKey_.value(live.activeWallpaperId);
        const int cachedIdx = plist.keyToIndex.value(cachedKey, -1);
        if (cachedIdx >= 0 && !contradicted(cachedIdx)) {
            resolvedIdx = cachedIdx;
        } else {
            wallpaperIdToKey_.remove(live.activeWallpaperId);
        }
    }

    // Priority 3: Elimination + Dock plist / adjacent Space deduction
    if (resolvedIdx < 0) {
        QSet<int> takenByOtherWallpaper;
        for (auto it = wallpaperIdToKey_.cbegin(); it != wallpaperIdToKey_.cend(); ++it) {
            if (it.key() != live.activeWallpaperId) {
                const int takenIdx = plist.keyToIndex.value(it.value(), -1);
                if (takenIdx >= 0) {
                    takenByOtherWallpaper.insert(takenIdx);
                }
            }
        }
        QList<int> candidates;
        for (int idx = 0; idx < count; ++idx) {
            if (!contradicted(idx) && !takenByOtherWallpaper.contains(idx)) {
                candidates.append(idx);
            }
        }
        if (candidates.isEmpty()) {
            for (int idx = 0; idx < count; ++idx) {
                if (!contradicted(idx)) {
                    candidates.append(idx);
                }
            }
        }

        const int refPrevIdx = (!lastSpaceKey_.isEmpty() && plist.keyToIndex.contains(lastSpaceKey_))
            ? plist.keyToIndex.value(lastSpaceKey_)
            : lastSpaceIndex_;
        if (candidates.size() == 1) {
            resolvedIdx = candidates.front();
        } else if (plist.plistCurrentIndex >= 0
                   && plist.orderedKeys[plist.plistCurrentIndex] != lastSpaceKey_
                   && candidates.contains(plist.plistCurrentIndex)) {
            resolvedIdx = plist.plistCurrentIndex;
        } else {
            const bool canRight = (refPrevIdx >= 0) && candidates.contains(refPrevIdx + 1);
            const bool canLeft = (refPrevIdx >= 0) && candidates.contains(refPrevIdx - 1);
            if (canRight && !canLeft) {
                resolvedIdx = refPrevIdx + 1;
            } else if (canLeft && !canRight) {
                resolvedIdx = refPrevIdx - 1;
            } else if (allowDefer) {
                outNeedsRetry = true;
                return -1;
            } else if (plist.plistCurrentIndex >= 0 && candidates.contains(plist.plistCurrentIndex)) {
                resolvedIdx = plist.plistCurrentIndex;
            } else if (canRight || canLeft) {
                resolvedIdx = (lastNonZeroDelta_ >= 0)
                    ? (canRight ? refPrevIdx + 1 : refPrevIdx - 1)
                    : (canLeft ? refPrevIdx - 1 : refPrevIdx + 1);
            } else if (!candidates.isEmpty()) {
                resolvedIdx = candidates.front();
            }
        }
    }

    if (resolvedIdx >= 0 && resolvedIdx < count) {
        outSpaceKey = plist.orderedKeys[resolvedIdx];
        if (live.activeWallpaperId != 0) {
            wallpaperIdToKey_.insert(live.activeWallpaperId, outSpaceKey);
        }
        outIsFullScreen = plist.orderedFullScreen[resolvedIdx];
        return resolvedIdx;
    }

    outSpaceKey = plist.plistCurrentKey;
    outIsFullScreen = plist.plistFullScreen;
    return -1;
}

void MacWindowLayerCoordinator::refreshOrderedSpaceKeys()
{
    if (!isCocoaPlatform()) {
        return;
    }
    const SpacesPlistLayout plist = readSpacesPlistLayout();
    if (!plist.valid) {
        return;
    }
    lastOrderedSpaceKeys_ = plist.orderedKeys;
    if (!lastSpaceKey_.isEmpty() && plist.keyToIndex.contains(lastSpaceKey_)) {
        lastSpaceIndex_ = plist.keyToIndex.value(lastSpaceKey_);
    }
}

void MacWindowLayerCoordinator::reconcileSpaceMapping(quint64 generation)
{
    if (!isCocoaPlatform() || generation != spaceChangeGeneration_) {
        return;
    }
    const SpacesPlistLayout plist = readSpacesPlistLayout();
    const LiveWindowServerScan live = scanLiveWindows(getpid());
    if (!plist.valid) {
        return;
    }
    // Always adopt the latest Dock MRU Space ordering once the swipe has settled, so the NEXT swipe compares against what's on screen
    lastOrderedSpaceKeys_ = plist.orderedKeys;
    if (plist.plistCurrentIndex < 0 || live.activeWallpaperId == 0) {
        if (!lastSpaceKey_.isEmpty() && plist.keyToIndex.contains(lastSpaceKey_)) {
            lastSpaceIndex_ = plist.keyToIndex.value(lastSpaceKey_);
        }
        return;
    }

    bool contradicted = false;
    for (auto it = plist.windowUniqueSpace.cbegin(); it != plist.windowUniqueSpace.cend(); ++it) {
        const uint32_t wid = it.key();
        if (plist.windowSpaceCount.value(wid, 0) != 1 || !live.aliveNormalWins.contains(wid)) {
            continue;
        }
        if (plist.keyToIndex.value(it.value(), -1) == plist.plistCurrentIndex
            && !live.onScreenNormalWins.contains(wid)) {
            contradicted = true;
            break;
        }
    }
    if (!contradicted && live.activeWallpaperId == lastWallpaperId_) {
        const QString settledKey = plist.orderedKeys[plist.plistCurrentIndex];
        wallpaperIdToKey_.insert(live.activeWallpaperId, settledKey);
        lastSpaceIndex_ = plist.plistCurrentIndex;
        lastSpaceKey_ = settledKey;
    } else if (!lastSpaceKey_.isEmpty() && plist.keyToIndex.contains(lastSpaceKey_)) {
        lastSpaceIndex_ = plist.keyToIndex.value(lastSpaceKey_);
    }
}

void MacWindowLayerCoordinator::handleSpaceChangeNotification(int retryAttempt, quint64 generation)
{
    if (generation != 0 && generation != spaceChangeGeneration_) {
        return;
    }
    entries_.removeIf([](const ManagedPetLayer& item) { return item.pet.isNull(); });

    const QStringList preSwipeOrder = lastOrderedSpaceKeys_;
    bool needsRetry = false;
    bool isFullScreen = false;
    uint32_t wallpaperId = 0;
    QString spaceKey;
    const int newIdx = resolveCurrentSpaceIndex(retryAttempt < 3, needsRetry, isFullScreen, wallpaperId, spaceKey);
    const bool genuineFullScreen = isFullScreen && (wallpaperId == 0);
    lastSpaceWasFullScreen_ = genuineFullScreen;

    QList<PetWindow*> followers;
    for (auto& entry : entries_) {
        if (!entry.pet || !entry.pet->isVisible() || !entry.followAllSpaces) {
            continue;
        }
        NSWindow* petWin = nativeWindowFor(*entry.pet);
        if (!genuineFullScreen && entry.stickyFullScreenFallback) {
            entry.stickyFullScreenFallback = false;
            if (petWin) {
                setWindowSpaceCollection(petWin, false, false);
                [petWin orderOut:nil];
            }
        }
        if (!petWin || ![petWin isOnActiveSpace] || entry.stickyFullScreenFallback
            || (wallpaperId != 0 && lastWallpaperId_ != 0 && wallpaperId != lastWallpaperId_)
            || (!spaceKey.isEmpty() && !lastSpaceKey_.isEmpty() && spaceKey != lastSpaceKey_)) {
            followers.append(entry.pet);
        }
    }

    if (needsRetry && !followers.isEmpty()) {
        QTimer::singleShot(55, this, [this, retryAttempt, generation] {
            handleSpaceChangeNotification(retryAttempt + 1, generation);
        });
        return;
    }

    // Compute swipe direction using the PRE-SWIPE Mission Control order first so macOS MRU Space rearrangement
    // cannot invert the direction when the destination Space is promoted to the front.
    int delta = 0;
    const int prevOldIdx = !lastSpaceKey_.isEmpty() ? preSwipeOrder.indexOf(lastSpaceKey_) : -1;
    const int prevNewIdx = !spaceKey.isEmpty() ? preSwipeOrder.indexOf(spaceKey) : -1;
    if (prevOldIdx >= 0 && prevNewIdx >= 0 && prevNewIdx != prevOldIdx) {
        delta = prevNewIdx - prevOldIdx;
    } else {
        const SpacesPlistLayout latestPlist = readSpacesPlistLayout();
        const int curOldIdx = (!lastSpaceKey_.isEmpty() && latestPlist.valid)
            ? latestPlist.keyToIndex.value(lastSpaceKey_, -1)
            : lastSpaceIndex_;
        if (curOldIdx >= 0 && newIdx >= 0 && newIdx != curOldIdx) {
            delta = newIdx - curOldIdx;
        } else if (!followers.isEmpty()) {
            delta = (lastNonZeroDelta_ != 0) ? lastNonZeroDelta_ : 1;
        }
    }

    const SpacesPlistLayout postPlist = readSpacesPlistLayout();
    if (postPlist.valid) {
        lastOrderedSpaceKeys_ = postPlist.orderedKeys;
    }
    if (wallpaperId != 0) {
        lastWallpaperId_ = wallpaperId;
    }
    if (!spaceKey.isEmpty()) {
        lastSpaceKey_ = spaceKey;
    }
    if (!lastSpaceKey_.isEmpty() && postPlist.valid && postPlist.keyToIndex.contains(lastSpaceKey_)) {
        lastSpaceIndex_ = postPlist.keyToIndex.value(lastSpaceKey_);
    } else if (newIdx >= 0) {
        lastSpaceIndex_ = newIdx;
    }
    if (delta != 0) {
        lastNonZeroDelta_ = (delta > 0) ? 1 : -1;
    }

    for (auto& entry : entries_) {
        if (entry.pet && entry.followAllSpaces && !genuineFullScreen) {
            entry.stickyFullScreenFallback = false;
        }
    }

    if (spaceSwitched && !followers.isEmpty()) {
        spaceSwitched(followers, delta);
    }
    syncAll(false);
    settleTimer_.start();

    QTimer::singleShot(1200, this, [this, generation] {
        reconcileSpaceMapping(generation);
    });
}

void MacWindowLayerCoordinator::setPetInteracting(PetWindow& pet, bool interacting)
{
    for (auto& entry : entries_) {
        if (entry.pet == &pet) {
            if (entry.activelyInteracting != interacting) {
                entry.activelyInteracting = interacting;
                if (interacting) {
                    raisePetTemporarily(pet);
                } else {
                    settleTimer_.start();
                }
            }
            return;
        }
    }
}

void MacWindowLayerCoordinator::raisePetTemporarily(PetWindow& pet)
{
    if (NSWindow* nsWin = nativeWindowFor(pet)) {
        [nsWin orderFrontRegardless];
        for (auto* overlay : childOverlayWindows(pet)) {
            if (overlay->isVisible()) {
                if (NSWindow* ovWin = nativeWindowFor(*overlay); ovWin && ovWin != nsWin) {
                    if (![[nsWin childWindows] containsObject:ovWin]) {
                        [ovWin orderWindow:NSWindowAbove relativeTo:[nsWin windowNumber]];
                    }
                }
            }
        }
    }
}

void MacWindowLayerCoordinator::syncNow()
{
    syncAll(false);
}

bool MacWindowLayerCoordinator::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::ChildAdded: {
        auto* ce = static_cast<QChildEvent*>(event);
        if (auto* childWidget = qobject_cast<QWidget*>(ce->child()); childWidget && childWidget->isWindow()) {
            childWidget->removeEventFilter(this);
            childWidget->installEventFilter(this);
        }
        break;
    }
    case QEvent::Show:
    case QEvent::ShowToParent:
    case QEvent::WinIdChange:
        if (auto* w = qobject_cast<QWidget*>(watched)) {
            PetWindow* ownerPet = dynamic_cast<PetWindow*>(w);
            if (!ownerPet) {
                ownerPet = dynamic_cast<PetWindow*>(w->parentWidget());
            }
            if (ownerPet) {
                for (const auto& entry : std::as_const(entries_)) {
                    if (entry.pet == ownerPet) {
                        applySpaceBehavior(*ownerPet, entry.stickyFullScreenFallback, false);
                        break;
                    }
                }
            }
        }
        QTimer::singleShot(0, this, [this] { syncAll(false); });
        break;
    case QEvent::MouseButtonPress:
        if (auto* pet = dynamic_cast<PetWindow*>(watched)) {
            raisePetTemporarily(*pet);
        }
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void MacWindowLayerCoordinator::applySpaceBehavior(QWidget& petWidget, bool canJoinAllSpaces, bool moveToActiveSpace)
{
    if (NSWindow* nsWin = nativeWindowFor(petWidget)) {
        setWindowSpaceCollection(nsWin, canJoinAllSpaces, moveToActiveSpace);
    }
    for (auto* overlay : childOverlayWindows(petWidget)) {
        if (NSWindow* ovWin = nativeWindowFor(*overlay)) {
            setWindowSpaceCollection(ovWin, canJoinAllSpaces, !canJoinAllSpaces);
        }
    }
}

void MacWindowLayerCoordinator::updatePollingState()
{
    bool needsSecondLayerWatch = false;
    for (const auto& entry : std::as_const(entries_)) {
        if (entry.pet && !entry.alwaysOnTop) {
            needsSecondLayerWatch = true;
            break;
        }
    }
    if (needsSecondLayerWatch && !pollTimer_.isActive()) {
        pollTimer_.start();
    } else if (!needsSecondLayerWatch && pollTimer_.isActive()) {
        pollTimer_.stop();
    }
}

void MacWindowLayerCoordinator::syncAll(bool ignoreSelfFrontmost)
{
    entries_.removeIf([](const ManagedPetLayer& item) { return item.pet.isNull(); });
    if (entries_.isEmpty()) {
        pollTimer_.stop();
        return;
    }

    QList<ManagedPetLayer> secondLayerPets;
    for (const auto& entry : std::as_const(entries_)) {
        if (!entry.pet) {
            continue;
        }
        applySpaceBehavior(*entry.pet, entry.stickyFullScreenFallback, false);
        if (!entry.pet->isVisible()) {
            continue;
        }
        NSWindow* petWin = nativeWindowFor(*entry.pet);
        if (!petWin) {
            continue;
        }
        if (entry.alwaysOnTop || entry.stickyFullScreenFallback) {
            setWindowNativeLevel(petWin, NSFloatingWindowLevel);
            for (auto* overlay : childOverlayWindows(*entry.pet)) {
                if (overlay->isVisible()) {
                    if (NSWindow* ovWin = nativeWindowFor(*overlay)) {
                        setWindowNativeLevel(ovWin, NSFloatingWindowLevel);
                    }
                }
            }
        } else {
            setWindowNativeLevel(petWin, NSNormalWindowLevel);
            for (auto* overlay : childOverlayWindows(*entry.pet)) {
                if (!overlay->isVisible()) {
                    continue;
                }
                if (overlay->objectName() == QStringLiteral("petTetherVisual")) {
                    continue;
                }
                if (NSWindow* ovWin = nativeWindowFor(*overlay)) {
                    setWindowNativeLevel(ovWin, NSNormalWindowLevel);
                }
            }
            if ([petWin isOnActiveSpace]) {
                secondLayerPets.append(entry);
            }
        }
    }

    if (secondLayerPets.isEmpty()) {
        return;
    }

    // While AuraPet itself is frontmost or user is dragging/chatting/opening menus, keep interacted pets unobstructed
    NSRunningApplication* frontApp = [[NSWorkspace sharedWorkspace] frontmostApplication];
    const pid_t frontPid = frontApp ? [frontApp processIdentifier] : 0;
    const pid_t selfPid = getpid();
    if (!ignoreSelfFrontmost
        && (frontPid == selfPid || QApplication::activeModalWidget() || QApplication::activePopupWidget())) {
        return;
    }
    for (const auto& entry : std::as_const(secondLayerPets)) {
        if (entry.activelyInteracting || entry.pet->isDragging()) {
            return;
        }
    }

    CFArrayRef rawList = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
    if (!rawList) {
        return;
    }

    QSet<NSInteger> managedMainWinNumbers;
    QSet<NSInteger> managedAllWinNumbers;
    for (const auto& entry : std::as_const(secondLayerPets)) {
        if (NSWindow* petWin = nativeWindowFor(*entry.pet)) {
            const NSInteger num = [petWin windowNumber];
            if (num > 0) {
                managedMainWinNumbers.insert(num);
                managedAllWinNumbers.insert(num);
            }
        }
        for (auto* overlay : childOverlayWindows(*entry.pet)) {
            if (overlay->isVisible()) {
                if (NSWindow* ovWin = nativeWindowFor(*overlay); ovWin && [ovWin windowNumber] > 0) {
                    managedAllWinNumbers.insert([ovWin windowNumber]);
                }
            }
        }
    }

    NSInteger frontAppActiveWin = 0;
    NSInteger topmostExternalWin = 0;
    const CFIndex count = CFArrayGetCount(rawList);
    for (CFIndex i = 0; i < count; ++i) {
        NSDictionary* info = (__bridge NSDictionary*)CFArrayGetValueAtIndex(rawList, i);
        const int layer = [info[(__bridge NSString*)kCGWindowLayer] intValue];
        if (layer != kCGNormalWindowLevel) {
            continue;
        }
        const double alpha = [info[(__bridge NSString*)kCGWindowAlpha] doubleValue];
        if (alpha <= 0.01) {
            continue;
        }
        CGRect bounds = CGRectZero;
        CFDictionaryRef boundsDict = (__bridge CFDictionaryRef)info[(__bridge NSString*)kCGWindowBounds];
        if (!boundsDict || !CGRectMakeWithDictionaryRepresentation(boundsDict, &bounds)) {
            continue;
        }
        if (bounds.size.width < 60.0 || bounds.size.height < 60.0) {
            continue;
        }
        const pid_t ownerPid = [info[(__bridge NSString*)kCGWindowOwnerPID] intValue];
        if (ownerPid == selfPid) {
            continue;
        }
        const NSInteger winNum = [info[(__bridge NSString*)kCGWindowNumber] integerValue];
        if (winNum <= 0) {
            continue;
        }
        if (topmostExternalWin == 0) {
            topmostExternalWin = winNum;
        }
        if (ownerPid == frontPid && frontAppActiveWin == 0) {
            frontAppActiveWin = winNum;
            break;
        }
    }

    const NSInteger anchorWin = (frontAppActiveWin != 0) ? frontAppActiveWin : topmostExternalWin;
    if (anchorWin == 0) {
        CFRelease(rawList);
        return;
    }

    // Check if our second-layer pets are ALREADY positioned right after anchorWin and ahead of older windows
    bool anchorSeen = false;
    bool petAboveAnchor = false;
    bool olderWindowAbovePet = false;
    int petsSeenRightBelowAnchor = 0;

    for (CFIndex i = 0; i < count; ++i) {
        NSDictionary* info = (__bridge NSDictionary*)CFArrayGetValueAtIndex(rawList, i);
        if ([info[(__bridge NSString*)kCGWindowLayer] intValue] != kCGNormalWindowLevel) {
            continue;
        }
        if ([info[(__bridge NSString*)kCGWindowAlpha] doubleValue] <= 0.01) {
            continue;
        }
        const NSInteger winNum = [info[(__bridge NSString*)kCGWindowNumber] integerValue];
        if (winNum == anchorWin) {
            anchorSeen = true;
            continue;
        }
        if (managedAllWinNumbers.contains(winNum)) {
            if (!anchorSeen) {
                petAboveAnchor = true;
                break;
            }
            if (managedMainWinNumbers.contains(winNum)) {
                ++petsSeenRightBelowAnchor;
            }
            continue;
        }
        const pid_t ownerPid = [info[(__bridge NSString*)kCGWindowOwnerPID] intValue];
        if (ownerPid == selfPid) {
            continue;
        }
        CGRect bounds = CGRectZero;
        CFDictionaryRef boundsDict = (__bridge CFDictionaryRef)info[(__bridge NSString*)kCGWindowBounds];
        if (!boundsDict || !CGRectMakeWithDictionaryRepresentation(boundsDict, &bounds)
            || bounds.size.width < 60.0 || bounds.size.height < 60.0) {
            continue;
        }
        if (anchorSeen && petsSeenRightBelowAnchor < managedMainWinNumbers.size()) {
            olderWindowAbovePet = true;
            break;
        }
    }
    CFRelease(rawList);

    const bool needsReorder = petAboveAnchor || olderWindowAbovePet
        || (petsSeenRightBelowAnchor < managedMainWinNumbers.size());
    if (!needsReorder) {
        return;
    }

    // Place all second-layer pets immediately below anchorWin (above all older windows) without stealing focus
    for (qsizetype i = secondLayerPets.size() - 1; i >= 0; --i) {
        PetWindow* pet = secondLayerPets[i].pet;
        if (!pet) {
            continue;
        }
        NSWindow* petWin = nativeWindowFor(*pet);
        if (!petWin) {
            continue;
        }
        [petWin orderWindow:NSWindowBelow relativeTo:anchorWin];
        for (auto* overlay : childOverlayWindows(*pet)) {
            if (!overlay->isVisible() || overlay->objectName() == QStringLiteral("petTetherVisual")) {
                continue;
            }
            NSWindow* ovWin = nativeWindowFor(*overlay);
            if (ovWin && ovWin != petWin && ![[petWin childWindows] containsObject:ovWin]) {
                [ovWin orderWindow:NSWindowBelow relativeTo:anchorWin];
                [petWin orderWindow:NSWindowBelow relativeTo:[ovWin windowNumber]];
            }
        }
    }
}
