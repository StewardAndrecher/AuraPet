#pragma once

#include <QObject>
#include <QQueue>
#include <QString>

class RoleActivation;
class RoleLibrary;
class QWidget;

class PetImportController : public QObject {
public:
    PetImportController(QWidget& parent, RoleLibrary& library, RoleActivation& roles);
    void enqueue(const QString& path, bool preview);

private:
    void drain();
    void import(const QString& path, bool preview);

    QWidget& window_;
    RoleLibrary& library_;
    RoleActivation& roles_;
    QQueue<QPair<QString, bool>> pending_;
    QString working_;
    bool draining_ = false;
};
