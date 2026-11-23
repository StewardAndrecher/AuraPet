#pragma once

#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkReply;
class QSettings;
class RoleLibrary;

namespace ChatSettings {
inline const QString baseUrlSetting = QStringLiteral("chat/baseUrl");
inline const QString modelSetting = QStringLiteral("chat/model");
inline const QString usesKeySetting = QStringLiteral("chat/usesKey");
inline const QString automaticSummarySetting = QStringLiteral("chat/automaticSummary");
}

QUrl chatEndpoint(const QString& base);

class ChatSession : public QObject {
    Q_OBJECT

public:
    ChatSession(RoleLibrary& library, QSettings& settings);
    ~ChatSession() override;

    void setRole(const QString& roleId, bool reload = false);
    QString roleId() const { return roleId_; }
    QString roleName() const { return roleName_; }
    QString summaryStatus() const { return summaryStatus_; }
    bool busy() const { return active_ != nullptr; }
    bool proactiveReply() const { return proactiveReply_; }

    bool send(const QString& prompt, QString& error);
    void speak();
    void setProactiveEnabled(bool enabled);
    void setVoiceFirst(bool enabled) { voiceFirst_ = enabled; }
    void stop();
    void organize(bool manual);

signals:
    void replyStarted();
    void replyUpdated(const QString& text);
    void replyFinished(const QString& text, const QString& error);
    void historyChanged();
    void summaryStatusChanged(const QString& text);

private:
    bool request(const QString& prompt, bool proactive, QString& error);
    void finishReply(const QString& prompt, QString text, QString error, bool proactive);
    void cancelRequests();
    void cancelRequest(QNetworkReply*& request);
    void setSummaryStatus(const QString& status);

    RoleLibrary& library_;
    QSettings& settings_;
    QNetworkAccessManager network_;
    QNetworkReply* active_ = nullptr;
    QNetworkReply* summaryReply_ = nullptr;
    QElapsedTimer summaryAttempt_;
    QString roleId_;
    QString roleName_;
    QJsonObject roleCard_;
    QString summaryStatus_;
    bool stopped_ = false;
    bool proactiveEnabled_ = true;
    bool proactiveReply_ = false;
    bool voiceFirst_ = false;
};
