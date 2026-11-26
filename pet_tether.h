#pragma once

#include <QObject>
#include <QPointF>
#include <QTimer>

#include <functional>
#include <memory>

class PetWindow;
class RandomMovement;
class TetherView;

class PetTether : public QObject {
public:
    PetTether(PetWindow& pet, RandomMovement& movement);
    ~PetTether() override;
    void cancel();

    std::function<void(QPoint)> menuRequested;
    std::function<void()> aimingStarted;
    std::function<void(bool)> aimingChanged;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class Gesture { None, Pending, Aiming };
    void beginAim();

    PetWindow& pet_;
    RandomMovement& movement_;
    std::unique_ptr<TetherView> view_;
    QTimer hold_;
    Gesture gesture_ = Gesture::None;
    QPointF pressed_;
    QPointF pointer_;
};
