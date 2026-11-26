#pragma once

#include "pet_appearance.h"

#include <QCache>
#include <QVariantAnimation>
#include <QWidget>

class QPainter;
class PetWindow;
struct PetAsset;
template <typename T> class QFutureWatcher;

class ActivityMeter : public QWidget {
public:
    enum class Feedback { Raise, Lower, Neutral };

    explicit ActivityMeter(PetWindow& pet);
    ~ActivityMeter() override;
    void setAsset(const PetAsset& asset);
    void setActivity(double percent);
    void setDisplayEnabled(bool enabled);
    void setSuppressed(bool suppressed);
    void setRevealed(bool revealed);
    void flash(Feedback feedback);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void syncPosition();
    void updateVisibility();
    double displayedActivity() const;
    void drawFeedback(QPainter& painter, QPointF tip);

    PetWindow& pet_;
    QByteArray assetKey_;
    std::optional<PetAppearance> appearance_;
    QFutureWatcher<PetAppearance>* analysis_ = nullptr;
    bool displayEnabled_ = true;
    bool suppressed_ = false;
    bool revealed_ = false;
    int activity_ = 0;
    QVariantAnimation feedbackAnimation_;
    Feedback feedback_ = Feedback::Neutral;
    double feedbackStart_ = 0;
    double feedbackProgress_ = 1;
};
