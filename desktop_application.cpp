#include "desktop_application.h"
#include "role_package.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QThread>
#include <QTimer>

namespace {
constexpr int maxRequest = 16 * 1024;
}

DesktopApplication::Instance DesktopApplication::claimProfile(const QString& dataPath, const QString& file, bool forward, QString& error)
{
    if (dataPath.isEmpty() || !QDir().mkpath(dataPath)) {
        error = QStringLiteral("无法创建桌宠的本地数据目录。");
        return Instance::Failed;
    }
    const auto canonical = QDir(dataPath).canonicalPath();
    const auto digest = QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha256).toHex();
    const auto endpoint = QStringLiteral("aurapet-") + QString::fromLatin1(digest.left(24));
    lock_ = std::make_unique<QLockFile>(QDir(dataPath).filePath(QStringLiteral("app.lock")));
    lock_->setStaleLockTime(0);
    if (!lock_->tryLock()) {
        if (!forward || lock_->error() != QLockFile::LockFailedError) {
            error = QStringLiteral("无法锁定角色库。请确认目录可写，并退出已经运行的 AuraPet 后重试。");
            return Instance::Failed;
        }
        auto requestedFile = file.isEmpty() ? QString{} : QFileInfo(file).absoluteFilePath();
        if (requestedFile.isEmpty() && !files_.isEmpty()) {
            requestedFile = files_.first();
        }
        const auto request = QJsonDocument(QJsonArray{requestedFile}).toJson(QJsonDocument::Compact) + '\n';
        if (request.size() <= maxRequest) {
            QElapsedTimer timeout;
            timeout.start();
            while (timeout.elapsed() < 1600) {
                QLocalSocket socket;
                socket.connectToServer(endpoint);
                if (socket.waitForConnected(120) && socket.write(request) == request.size()) {
                    socket.flush();
                    if ((socket.bytesAvailable() || socket.waitForReadyRead(1000)) && socket.read(1) == "1") {
                        return Instance::Forwarded;
                    }
                }
                QThread::msleep(35);
            }
        }
        error = QStringLiteral("AuraPet 已运行，但未能接收这次请求。");
        return Instance::Failed;
    }

    QLocalServer::removeServer(endpoint);
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server_.listen(endpoint)) {
        error = server_.errorString();
        return Instance::Failed;
    }
    connect(&server_, &QLocalServer::newConnection, this, [this] {
        while (auto* socket = server_.nextPendingConnection()) {
            socket->setReadBufferSize(maxRequest + 1);
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            QTimer::singleShot(2000, socket, &QLocalSocket::disconnectFromServer);
            const auto readRequest = [this, socket] {
                if (socket->bytesAvailable() > maxRequest) {
                    socket->abort();
                    return;
                }
                if (!socket->canReadLine()) {
                    return;
                }
                const auto request = QJsonDocument::fromJson(socket->readLine(maxRequest)).array();
                if (request.size() != 1 || !request.at(0).isString()) {
                    socket->abort();
                    return;
                }
                const auto path = request.at(0).toString();
                socket->write("1");
                socket->disconnectFromServer();
                if (path.isEmpty()) {
                    emit showRequested();
                } else {
                    deliver(path);
                }
            };
            connect(socket, &QLocalSocket::readyRead, socket, readRequest);
            readRequest();
        }
    });
    return Instance::Primary;
}

bool DesktopApplication::event(QEvent* event)
{
    if (event->type() == QEvent::FileOpen) {
        const auto path = static_cast<QFileOpenEvent*>(event)->file();
        if (QFileInfo(path).suffix().compare(rolePackageSuffix, Qt::CaseInsensitive) == 0) {
            deliver(path);
            return true;
        }
    }
    return QApplication::event(event);
}

void DesktopApplication::deliver(const QString& path)
{
    const auto absolute = QFileInfo(path).absoluteFilePath();
    if (delivering_) {
        emit fileRequested(absolute);
    } else if (!files_.contains(absolute)) {
        files_.append(absolute);
    }
}

void DesktopApplication::startFileDelivery()
{
    delivering_ = true;
    for (const auto& path : std::as_const(files_)) {
        emit fileRequested(path);
    }
    files_.clear();
}
