#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

class RoleLibrary;
class QWidget;

class VoiceTraining : public QObject {
    Q_OBJECT
public:
    explicit VoiceTraining(const QString& dataPath);
    ~VoiceTraining() override;
    bool start(const QString& roleId, QString& error);
    void cancel();
    bool running() const;
    QString roleId() const { return roleId_; }
    QString output() const { return output_; }
    QString folderFor(const QString& roleId) const;
    QString logFor(const QString& roleId) const;

signals:
    void changed();

private:
    void readOutput();

    QString dataPath_;
    QString roleId_;
    QString folder_;
    QString output_;
    QProcess process_;
    bool stopping_ = false;
};

void showRoleCard(QWidget& parent, RoleLibrary& library, const QString& roleId, VoiceTraining& training);
