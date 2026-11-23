#include "memory_dialog.h"
#include "role_library.h"

#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTextCursor>
#include <QVBoxLayout>

bool showRoleMemory(RoleLibrary& library, const QString& roleId, const QString& roleName, QWidget* parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("%1的记忆").arg(roleName));
    dialog.resize(530, 460);
    auto* layout = new QVBoxLayout(&dialog);
    auto* tabs = new QTabWidget(&dialog);
    auto* facts = new QWidget(tabs);
    auto* factLayout = new QVBoxLayout(facts);
    auto* list = new QListWidget(facts);
    auto* details = new QPlainTextEdit(facts);
    details->setReadOnly(true);
    details->setPlaceholderText(QStringLiteral("整理旧对话后，可在这里查看记忆及其来源。"));
    auto* editButtons = new QDialogButtonBox(facts);
    auto* edit = editButtons->addButton(QStringLiteral("编辑…"), QDialogButtonBox::ActionRole);
    auto* forget = editButtons->addButton(QStringLiteral("忘记…"), QDialogButtonBox::DestructiveRole);
    factLayout->addWidget(list, 1);
    factLayout->addWidget(details, 2);
    factLayout->addWidget(editButtons);
    tabs->addTab(facts, QStringLiteral("长期记忆"));
    auto* summaries = new QPlainTextEdit(tabs);
    summaries->setReadOnly(true);
    summaries->setPlaceholderText(QStringLiteral("还没有旧对话摘要。"));
    tabs->addTab(summaries, QStringLiteral("会话摘要"));
    layout->addWidget(tabs);

    auto* buttons = new QDialogButtonBox(&dialog);
    auto* organize = buttons->addButton(QStringLiteral("立即整理旧对话"), QDialogButtonBox::ActionRole);
    buttons->addButton(QStringLiteral("关闭"), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);

    QList<MemoryItem> items;
    const auto render = [&] {
        const int row = list->currentRow();
        const bool current = row >= 0 && row < items.size();
        edit->setEnabled(current);
        forget->setEnabled(current);
        details->clear();
        if (current) {
            const auto& item = items[row];
            QDateTime at = QDateTime::fromString(item.sourceTime + QStringLiteral("Z"), Qt::ISODate);
            const QString time = at.isValid() ? at.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QString{};
            details->setPlainText(item.text + QStringLiteral("\n\n来源 · 消息 #%1 %2\n").arg(item.sourceId)
                                  .arg(time) + item.source);
        }
    };
    const auto refresh = [&] {
        QString error;
        items = library.memoryItems(roleId, error);
        const auto old = library.summaries(roleId, true, error);
        if (!error.isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("无法读取记忆"), error);
            return;
        }
        list->clear();
        for (const auto& item : items) {
            list->addItem(item.text.simplified().left(75));
        }
        list->setCurrentRow(items.isEmpty() ? -1 : 0);
        render();
        QString display;
        for (const auto& summary : old) {
            display += QStringLiteral("消息 #%1–#%2").arg(summary.firstId).arg(summary.lastId);
            if (summary.excluded) {
                display += QStringLiteral("  ·  已退出聊天上下文");
            }
            display += QStringLiteral("\n") + summary.text + QStringLiteral("\n\n");
        }
        summaries->setPlainText(display);
        summaries->moveCursor(QTextCursor::Start);
    };
    QObject::connect(list, &QListWidget::currentRowChanged, &dialog, render);
    QObject::connect(edit, &QPushButton::clicked, &dialog, [&] {
        const auto item = items.at(list->currentRow());
        bool ok = false;
        const auto text = QInputDialog::getMultiLineText(&dialog, QStringLiteral("编辑长期记忆"),
                                                       QStringLiteral("记忆内容"), item.text, &ok);
        if (!ok) {
            return;
        }
        QString error;
        if (library.editMemory(roleId, item.key, text, false, error)) {
            refresh();
        } else {
            QMessageBox::warning(&dialog, QStringLiteral("无法保存记忆"), error);
        }
    });
    QObject::connect(forget, &QPushButton::clicked, &dialog, [&] {
        const auto item = items.at(list->currentRow());
        if (QMessageBox::question(&dialog, QStringLiteral("忘记长期记忆"),
            QStringLiteral("把这条记忆从后续聊天移除？原对话仍可在聊天历史中查看。"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
            return;
        }
        QString error;
        if (library.editMemory(roleId, item.key, {}, true, error)) {
            refresh();
        } else {
            QMessageBox::warning(&dialog, QStringLiteral("无法移除记忆"), error);
        }
    });
    QObject::connect(organize, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    refresh();
    return dialog.exec() == QDialog::Accepted;
}
