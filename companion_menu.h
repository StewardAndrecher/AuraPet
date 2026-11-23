#pragma once

#include <QLabel>
#include <QTimer>

class FocusSession;
class QMenu;

QMenu* addCompanionMenu(QMenu& parent, QWidget& pet, FocusSession& focus);

class FocusNotice : public QLabel {
public:
    explicit FocusNotice(QWidget& pet);
    void announce(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void placeBesidePet();

    QWidget& pet_;
    QTimer expiry_;
};
