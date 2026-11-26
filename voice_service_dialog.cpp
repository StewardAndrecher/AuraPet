#include "voice_service_dialog.h"
#include "settings_store.h"

#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#ifdef Q_OS_MACOS
#include <unistd.h>
#endif

void showVoiceService(QWidget& parent)
{
    QDialog dialog(&parent);
    dialog.setWindowTitle(QStringLiteral("本机千问语音服务"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* status = new QLabel(&dialog);
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    layout->addWidget(status);
    auto* help = new QLabel(QStringLiteral("朗读功能连接本机语音服务。语音服务关闭时，桌宠仍可使用文字聊天。"), &dialog);
    help->setWordWrap(true);
    layout->addWidget(help);
    auto* buttons = new QHBoxLayout;
    auto* start = new QPushButton(QStringLiteral("启动"), &dialog);
    auto* stop = new QPushButton(QStringLiteral("停止"), &dialog);
    auto* refresh = new QPushButton(QStringLiteral("刷新"), &dialog);
    auto* install = new QPushButton(QStringLiteral("安装说明"), &dialog);
    auto* close = new QPushButton(QStringLiteral("关闭"), &dialog);
    for (auto* button : {start, stop, refresh, install, close}) {
        buttons->addWidget(button);
    }
    layout->addLayout(buttons);
    dialog.resize(530, dialog.sizeHint().height());

    QNetworkAccessManager network;
    network.setProxy(QNetworkProxy::NoProxy);
    bool probing = false;
    bool ready = false;
    bool starting = false;
    int probes = 0;
    QString detail;

#ifdef Q_OS_MACOS
    const QDir agents(QDir::home().filePath(QStringLiteral("Library/LaunchAgents")));
    const auto domain = QStringLiteral("gui/%1").arg(getuid());
    QString label;
    for (const auto& candidate : {QStringLiteral("com.aurapet.qwen-tts"), QStringLiteral("com.gifpet.qwen-tts")}) {
        if (QFileInfo::exists(agents.filePath(candidate + QStringLiteral(".plist")))) {
            label = candidate;
            break;
        }
    }
    const auto service = domain + u'/' + label;
    QProcess launch;
    launch.setProgram(QStringLiteral("/bin/launchctl"));
    const auto run = [&](const QStringList& arguments) {
        launch.setArguments(arguments);
        launch.start();
    };
    bool bootstrapping = false;
    const auto canManage = [&] { return !label.isEmpty(); };
    const auto isBusy = [&] { return launch.state() != QProcess::NotRunning || starting; };
    const auto canStop = [&] { return !label.isEmpty() && launch.state() == QProcess::NotRunning; };
#elif defined(Q_OS_WIN)
    const QDir voiceRoot(QDir(SettingsStore::dataPath()).filePath(QStringLiteral("voice")));
    const QString winStartScript = voiceRoot.filePath(QStringLiteral("start-voice.cmd"));
    const QString winStopScript = voiceRoot.filePath(QStringLiteral("stop-voice.cmd"));
    const bool hasManagedScripts = QFileInfo::exists(winStartScript);
    QProcess launch;
    const auto canManage = [&] { return hasManagedScripts; };
    const auto isBusy = [&] { return launch.state() != QProcess::NotRunning || starting; };
    const auto canStop = [&] { return hasManagedScripts && QFileInfo::exists(winStopScript) && launch.state() == QProcess::NotRunning; };
#else
    const auto canManage = [] { return false; };
    const auto isBusy = [&] { return starting; };
    const auto canStop = [] { return false; };
#endif

    const auto updateButtons = [&] {
        start->setEnabled(canManage() && !ready && !isBusy() && !probing);
        stop->setEnabled(canStop());
        refresh->setEnabled(!probing);
    };
    QTimer poll;
    poll.setSingleShot(true);
    const auto probe = [&] {
        if (probing) {
            return;
        }
        probing = true;
        status->setText(starting ? QStringLiteral("正在等待千问加载模型…") : QStringLiteral("正在检查本机千问服务…"));
        updateButtons();
        QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:17860/health")));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(1200);
        auto* reply = network.get(request);
        reply->setReadBufferSize(4096);
        QObject::connect(reply, &QNetworkReply::finished, &dialog, [&, reply] {
            probing = false;
            const auto body = QJsonDocument::fromJson(reply->read(4096)).object();
            ready = reply->error() == QNetworkReply::NoError && body.value(QStringLiteral("ready")).toBool()
                && body.value(QStringLiteral("sample_rate")).toInt() == 24000;
            reply->deleteLater();
            if (starting && !ready && ++probes < 30) {
                poll.start(1000);
            } else {
                const bool timedOut = starting && !ready;
                starting = false;
                if (ready) {
                    status->setText(canManage() ? QStringLiteral("本机服务已就绪。")
                                                : QStringLiteral("本机服务已就绪，由外部程序管理。"));
                } else if (!detail.isEmpty() || timedOut) {
                    status->setText(timedOut ? QStringLiteral("千问在等待时间内未就绪，请查看语音服务日志。") : detail);
                } else {
                    status->setText(canManage() ? QStringLiteral("本机千问服务当前未就绪。")
                                                : QStringLiteral("尚未发现本机千问服务。可按需运行安装包中的语音部署脚本。"));
                }
            }
            updateButtons();
        });
    };
    QObject::connect(&poll, &QTimer::timeout, &dialog, probe);

#ifdef Q_OS_MACOS
    QObject::connect(&launch, &QProcess::stateChanged, &dialog, updateButtons);
    QObject::connect(&launch, &QProcess::errorOccurred, &dialog, [&](QProcess::ProcessError failure) {
        if (failure == QProcess::FailedToStart) {
            starting = false;
            detail = launch.errorString();
            probe();
        }
    });
    QObject::connect(&launch, &QProcess::finished, &dialog, [&](int code, QProcess::ExitStatus state) {
        const auto stderrText = QString::fromUtf8(launch.readAllStandardError()).trimmed().left(400);
        if (starting && (code != 0 || state != QProcess::NormalExit) && !bootstrapping) {
            bootstrapping = true;
            run({QStringLiteral("bootstrap"), domain, agents.filePath(label + QStringLiteral(".plist"))});
            return;
        }
        if (code != 0 || state != QProcess::NormalExit) {
            starting = false;
            detail = stderrText.isEmpty() ? QStringLiteral("系统未能执行语音服务命令。") : stderrText;
        }
        probe();
    });
    QObject::connect(start, &QPushButton::clicked, &dialog, [&] {
        starting = true;
        bootstrapping = false;
        probes = 0;
        detail.clear();
        run({QStringLiteral("kickstart"), service});
    });
    QObject::connect(stop, &QPushButton::clicked, &dialog, [&] {
        starting = false;
        poll.stop();
        detail.clear();
        run({QStringLiteral("bootout"), service});
    });
#elif defined(Q_OS_WIN)
    QObject::connect(&launch, &QProcess::stateChanged, &dialog, updateButtons);
    QObject::connect(&launch, &QProcess::finished, &dialog, [&](int, QProcess::ExitStatus) { probe(); });
    QObject::connect(start, &QPushButton::clicked, &dialog, [&] {
        starting = true;
        probes = 0;
        detail.clear();
        if (!QProcess::startDetached(QStringLiteral("cmd.exe"), {QStringLiteral("/C"), winStartScript}, voiceRoot.absolutePath())) {
            starting = false;
            detail = QStringLiteral("无法启动本机千问语音脚本。");
        }
        probe();
    });
    QObject::connect(stop, &QPushButton::clicked, &dialog, [&] {
        starting = false;
        poll.stop();
        detail.clear();
        launch.start(QStringLiteral("cmd.exe"), {QStringLiteral("/C"), winStopScript});
    });
#endif

    QObject::connect(refresh, &QPushButton::clicked, &dialog, probe);
    QObject::connect(install, &QPushButton::clicked, &dialog, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/StewardAndrecher/AuraPet/releases")));
    });
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    probe();
    dialog.exec();
}
