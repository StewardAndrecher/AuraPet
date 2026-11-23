#include "chat_dialog.h"
#include "chat_credentials.h"
#include "chat_session.h"
#include "memory_dialog.h"
#include "role_library.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

using namespace ChatSettings;

namespace {
void configureChat(QSettings& settings, QWidget* parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("AI 接口"));
    dialog.resize(460, 190);
    auto* form = new QFormLayout(&dialog);
    auto* base = new QLineEdit(settings.value(baseUrlSetting).toString(), &dialog);
    base->setPlaceholderText(QStringLiteral("https://你的服务地址/v1"));
    auto* model = new QLineEdit(settings.value(modelSetting).toString(), &dialog);
    auto* secret = new QLineEdit(&dialog);
    secret->setEchoMode(QLineEdit::Password);
    auto* removeKey = new QCheckBox(QStringLiteral("移除已保存的 API Key"), &dialog);
    auto* status = new QLabel(&dialog);
    status->setWordWrap(true);
    status->setTextFormat(Qt::PlainText);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    form->addRow(QStringLiteral("Base URL"), base);
    form->addRow(QStringLiteral("模型 ID"), model);
    form->addRow(QStringLiteral("API Key（选填）"), secret);
    form->addRow(removeKey);
    form->addRow(status);
    form->addRow(buttons);

    const auto initialEndpoint = chatEndpoint(base->text());
    auto loadedEndpoint = initialEndpoint;
    const auto showKeyPlaceholder = [&] {
        const bool hasSavedKey = loadedEndpoint == initialEndpoint && settings.value(usesKeySetting).toBool();
        removeKey->setChecked(false);
        removeKey->setVisible(hasSavedKey);
        secret->setPlaceholderText(hasSavedKey ? QStringLiteral("已保存，留空继续使用")
                                               : QStringLiteral("使用本地无鉴权接口时可留空"));
    };
    showKeyPlaceholder();
    QObject::connect(base, &QLineEdit::textChanged, &dialog, [&] {
        const auto endpoint = chatEndpoint(base->text());
        if (loadedEndpoint != endpoint) {
            loadedEndpoint = endpoint;
            secret->clear();
            showKeyPlaceholder();
        }
    });
    QObject::connect(removeKey, &QCheckBox::toggled, secret, &QWidget::setDisabled);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const auto endpoint = chatEndpoint(base->text());
        if (endpoint.isEmpty() || model->text().trimmed().isEmpty()) {
            status->setText(QStringLiteral("请填写有效的 HTTP(S) Base URL 和模型 ID。"));
            return;
        }
        const QString key = removeKey->isChecked() ? QString{} : secret->text().trimmed();
        QString error;
        if ((!key.isEmpty() || removeKey->isChecked()) && !saveChatKey(endpoint.toString(QUrl::FullyEncoded), key, error)) {
            status->setText(error);
            return;
        }
        settings.setValue(usesKeySetting, !removeKey->isChecked() &&
                          (!key.isEmpty() || (initialEndpoint == endpoint && settings.value(usesKeySetting).toBool())));
        settings.setValue(baseUrlSetting, base->text().trimmed());
        settings.setValue(modelSetting, model->text().trimmed());
        settings.sync();
        dialog.accept();
    });
    dialog.exec();
}
}

void showRoleChatHistory(ChatSession& chat, RoleLibrary& library, QSettings& settings, QWidget* parent)
{
    const QString roleId = chat.roleId();
    const QString roleName = chat.roleName();
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("%1 · 聊天记录").arg(roleName));
    dialog.resize(530, 485);
    auto* layout = new QVBoxLayout(&dialog);
    auto* transcript = new QPlainTextEdit(&dialog);
    transcript->setReadOnly(true);
    transcript->setPlaceholderText(QStringLiteral("这个角色还没有聊天记录。"));
    auto* status = new QLabel(&dialog);
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    auto* summary = new QLabel(&dialog);
    summary->setTextFormat(Qt::PlainText);
    summary->setWordWrap(true);
    auto* nav = new QHBoxLayout;
    auto* earlier = new QPushButton(QStringLiteral("加载更早记录"), &dialog);
    auto* configure = new QPushButton(QStringLiteral("AI 接口…"), &dialog);
    auto* memory = new QPushButton(QStringLiteral("记忆…"), &dialog);
    auto* automatic = new QCheckBox(QStringLiteral("自动整理"), &dialog);
    automatic->setChecked(settings.value(automaticSummarySetting, true).toBool());
    auto* close = new QPushButton(QStringLiteral("关闭"), &dialog);
    nav->addWidget(earlier);
    nav->addStretch();
    nav->addWidget(configure);
    nav->addWidget(memory);
    nav->addWidget(automatic);
    nav->addWidget(close);
    layout->addWidget(transcript, 1);
    layout->addWidget(status);
    layout->addWidget(summary);
    layout->addLayout(nav);

    int limit = 100;
    const auto render = [&](bool preserveScroll) {
        QString error;
        auto rows = library.chatHistory(roleId, limit + 1, error);
        const bool more = rows.size() > limit;
        if (more) {
            rows.removeFirst();
        }
        QString text;
        for (const auto& message : rows) {
            const auto speaker = message.speaker == QStringLiteral("user") ? QStringLiteral("你")
                : (message.proactive ? roleName + QStringLiteral("（主动发言）") : roleName);
            text += speaker + QLatin1Char('\n') + message.text + QStringLiteral("\n\n");
        }
        auto* scrollbar = transcript->verticalScrollBar();
        const int fromBottom = scrollbar->maximum() - scrollbar->value();
        transcript->setPlainText(text);
        if (preserveScroll) {
            scrollbar->setValue(scrollbar->maximum() - fromBottom);
        } else {
            transcript->moveCursor(QTextCursor::End);
        }
        if (error.isEmpty() && settings.value(modelSetting).toString().isEmpty()) {
            error = QStringLiteral("在「AI 接口」配置服务地址和模型后，即可单击桌宠聊天。");
        }
        status->setText(error);
        status->setVisible(!error.isEmpty());
        earlier->setVisible(more);
    };
    const auto updateSummary = [&](const QString& text) {
        summary->setText(text);
        summary->setVisible(!text.isEmpty());
    };
    QObject::connect(&chat, &ChatSession::historyChanged, &dialog, [&] { render(true); });
    QObject::connect(&chat, &ChatSession::summaryStatusChanged, &dialog, updateSummary);
    QObject::connect(earlier, &QPushButton::clicked, &dialog, [&] {
        limit += 100;
        render(true);
    });
    QObject::connect(configure, &QPushButton::clicked, &dialog, [&] {
        configureChat(settings, &dialog);
        render(true);
        chat.organize(false);
    });
    QObject::connect(memory, &QPushButton::clicked, &dialog, [&] {
        if (showRoleMemory(library, roleId, roleName, &dialog)) {
            chat.organize(true);
        }
    });
    QObject::connect(automatic, &QCheckBox::toggled, &dialog, [&](bool enabled) {
        settings.setValue(automaticSummarySetting, enabled);
        if (enabled) {
            chat.organize(false);
        }
    });
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    QTimer clock(&dialog);
    QObject::connect(&clock, &QTimer::timeout, &dialog, [&] { chat.organize(false); });
    clock.start(60000);
    QTimer::singleShot(0, &dialog, [&] { chat.organize(false); });
    render(false);
    updateSummary(chat.summaryStatus());
    dialog.exec();
}
