#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>

class PetWindow;
class QWidget;

struct ManagedPetLayer {
    QPointer<PetWindow> pet;
    bool followAllSpaces = true;
    bool alwaysOnTop = true;
    bool activelyInteracting = false;
    bool stickyFullScreenFallback = false;
};

bool macWindowJoinsAllSpaces(const QWidget& widget);
bool macWindowIsSpaceManaged(const QWidget& widget);
bool macWindowMovesToActiveSpace(const QWidget& widget);
int macWindowNativeLevel(const QWidget& widget);

class MacWindowLayerCoordinator : public QObject {
public:
    explicit MacWindowLayerCoordinator(QObject* parent = nullptr);
    ~MacWindowLayerCoordinator() override;

    void registerPet(PetWindow& pet, bool followAllSpaces, bool alwaysOnTop);
    void unregisterPet(PetWindow& pet);
    void updatePetPolicy(PetWindow& pet, bool followAllSpaces, bool alwaysOnTop);
    bool isFollowingAllSpaces(const PetWindow& pet) const;
    bool isCurrentSpaceFullScreen() const { return lastSpaceWasFullScreen_; }
    void movePetToActiveSpace(PetWindow& pet, const QPoint& entryPos, bool targetIsFullScreen = false);
    void triggerSpaceSwitchForTesting(int spaceDelta);
    void setPetInteracting(PetWindow& pet, bool interacting);
    void raisePetTemporarily(PetWindow& pet);
    void syncNow();

    std::function<void(const QList<PetWindow*>& pets, int spaceDelta)> spaceSwitched;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void watchWidgetTree(QWidget& root);
    void applySpaceBehavior(QWidget& petWidget, bool canJoinAllSpaces, bool moveToActiveSpace);
    int resolveCurrentSpaceIndex(bool allowDefer, bool& outNeedsRetry, bool& outIsFullScreen,
                                 uint32_t& outWallpaperId, QString& outSpaceKey);
    void handleSpaceChangeNotification(int retryAttempt, quint64 generation);
    void reconcileSpaceMapping(quint64 generation);
    void refreshOrderedSpaceKeys();
    void syncAll(bool ignoreSelfFrontmost = false);
    void updatePollingState();

    QList<ManagedPetLayer> entries_;
    QTimer pollTimer_;
    QTimer settleTimer_;
    QTimer spaceOrderRefreshTimer_;
    void* appActivateObserver_ = nullptr;
    void* spaceChangeObserver_ = nullptr;
    QStringList lastOrderedSpaceKeys_;
    QHash<uint32_t, QString> wallpaperIdToKey_;
    uint32_t lastWallpaperId_ = 0;
    QString lastSpaceKey_;
    int lastSpaceIndex_ = -1;
    int lastNonZeroDelta_ = 1;
    bool lastSpaceWasFullScreen_ = false;
    quint64 spaceChangeGeneration_ = 0;
};
