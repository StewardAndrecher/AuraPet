#pragma once

#include <QApplication>
#include <QLocalServer>
#include <QLockFile>

#include <memory>

class DesktopApplication : public QApplication {
    Q_OBJECT
public:
    using QApplication::QApplication;
    enum class Instance { Primary, Forwarded, Failed };

    Instance claimProfile(const QString& dataPath, const QString& file, bool forward, QString& error);
    void startFileDelivery();

signals:
    void fileRequested(const QString& path);
    void showRequested();

protected:
    bool event(QEvent* event) override;

private:
    void deliver(const QString& path);

    // Close the socket before releasing the profile lock.
    std::unique_ptr<QLockFile> lock_;
    QLocalServer server_;
    QStringList files_;
    bool delivering_ = false;
};
