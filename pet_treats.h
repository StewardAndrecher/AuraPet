#pragma once

#include "pet_motion_rules.h"

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
constexpr std::array companionModes{CompanionMode::Auto, CompanionMode::Play, CompanionMode::Daily, CompanionMode::Focus};
constexpr int yarnActivityCost = 18;
constexpr int catnipActivityGain = 24;

QString treatName(PetTreat treat);
QString treatDescription(PetTreat treat);
QIcon treatIcon(PetTreat treat);
QString companionModeName(CompanionMode mode);
QString companionModeShortName(CompanionMode mode);
QString companionModeDescription(CompanionMode mode);
QString effectiveRhythmName(EffectiveRhythm rhythm);

class TreatCard : public QWidget {
public:
    TreatCard();
    void setLoaded(std::optional<PetTreat> treat);
    void setCompanionState(CompanionMode userMode, EffectiveRhythm effectiveRhythm,
                           const QString& overrideTag = {});
    std::function<void(PetTreat)> picked;
    std::function<void(CompanionMode)> modeSelected;

protected:
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void showHintText(const QString& text);
    void updateThemeColors(bool force = false);

    std::array<QWidget*, companionModes.size()> modeButtons_{};
    QWidget* teaseButton_ = nullptr;
    QWidget* rechargeButton_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* statusBadge_ = nullptr;
    QLabel* hint_ = nullptr;
    QLabel* details_ = nullptr;
    QStackedWidget* footer_ = nullptr;
    QPropertyAnimation* fade_ = nullptr;
    CompanionMode userMode_ = CompanionMode::Auto;
    EffectiveRhythm effectiveRhythm_ = EffectiveRhythm::Daily;
    QString overrideTag_;
    QString hoveredKey_;
    bool themeInitialized_ = false;
    bool lastDark_ = false;
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
