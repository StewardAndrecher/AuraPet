#pragma once

#include <QByteArray>
#include <QString>

class ChatStream {
public:
    QString feed(const QByteArray& bytes);
    QString finish();
    bool complete() const;
    QString error() const;

private:
    QString dispatch();
    QByteArray buffer_;
    QByteArray event_;
    bool completed_ = false;
    QString error_;
};

QString chatApiError(const QByteArray& bytes);
QString chatApiReply(const QByteArray& bytes);
