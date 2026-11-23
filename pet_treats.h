#pragma once

#include <QElapsedTimer>
#include <QIcon>
#include <QTimer>
#include <QWidget>

#include <array>
#include <functional>
#include <optional>

class QLabel;
class QPropertyAnimation;
class QStackedWidget;
class PetWindow;

enum class PetTreat { SlowKibble, HighKibble, YarnBall, Catnip };

constexpr std::array petTreats{PetTreat::SlowKibble, PetTreat::HighKibble, PetTreat::YarnBall, PetTreat::Catnip};
constexpr int yarnActivityCost = 18;
constexpr int catnipActivityGain = 24;
QString treatName(PetTreat treat);
QString treatDescription(PetTreat treat);
QIcon treatIcon(PetTreat treat);

class TreatCard : public QWidget {
public:
    TreatCard();
    void setLoaded(std::optional<PetTreat> treat);
    std::function<void(PetTreat)> picked;

protected:
    void hideEvent(QHideEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void showDetails(std::optional<PetTreat> treat);

    std::array<QWidget*, petTreats.size()> buttons_{};
    QLabel* status_;
    QLabel* details_;
    QStackedWidget* footer_;
    QPropertyAnimation* fade_;
    std::optional<PetTreat> hovered_;
};

class TreatEffect : public QWidget {
public:
    explicit TreatEffect(PetWindow& pet);
    void play(PetTreat treat);
    void clear();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void syncPosition();

    PetWindow& pet_;
    PetTreat treat_ = PetTreat::Catnip;
    QTimer frames_;
    QElapsedTimer elapsed_;
};
