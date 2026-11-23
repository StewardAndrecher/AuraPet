#include "role_library_dialog.h"
#include "role_library.h"
#include "role_package.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

namespace {
class RoleList : public QListWidget {
public:
    using QListWidget::QListWidget;
    std::function<void(const QString&)> import;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override { acceptPackage(event); }
    void dragMoveEvent(QDragMoveEvent* event) override { acceptPackage(event); }

    void dropEvent(QDropEvent* event) override
    {
        const auto path = acceptPackage(event);
        if (!path.isEmpty() && import) {
            QTimer::singleShot(0, this, [this, path] { import(path); });
        }
    }

private:
    static QString acceptPackage(QDropEvent* event)
    {
        const auto urls = event->mimeData()->urls();
        if (urls.size() == 1 && urls.first().isLocalFile() && event->possibleActions().testFlag(Qt::CopyAction)) {
            const auto path = urls.first().toLocalFile();
            if (QFileInfo(path).suffix().compare(rolePackageSuffix, Qt::CaseInsensitive) == 0) {
                event->setDropAction(Qt::CopyAction);
                event->accept();
                return path;
            }
        }
        event->ignore();
        return {};
    }
};
}

RoleLibraryChoice manageRoles(RoleLibrary& library, const QString& current, QWidget* parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("本地角色库"));
    dialog.resize(500, 340);
    auto* layout = new QVBoxLayout(&dialog);
    auto* list = new RoleList(&dialog);
    list->setAcceptDrops(true);
    list->setDropIndicatorShown(false);
    layout->addWidget(list);

    auto* shareButtons = new QDialogButtonBox(&dialog);
    auto* importButton = shareButtons->addButton(QStringLiteral("导入角色包…"), QDialogButtonBox::ActionRole);
    auto* exportButton = shareButtons->addButton(QStringLiteral("导出分享包…"), QDialogButtonBox::ActionRole);
    auto* shareButton = shareButtons->addButton(QStringLiteral("角色分享设置…"), QDialogButtonBox::ActionRole);
    layout->addWidget(shareButtons);
    auto* buttons = new QDialogButtonBox(&dialog);
    auto* switchButton = buttons->addButton(QStringLiteral("切换角色"), QDialogButtonBox::AcceptRole);
    auto* renameButton = buttons->addButton(QStringLiteral("重命名…"), QDialogButtonBox::ActionRole);
    auto* removeButton = buttons->addButton(QStringLiteral("删除…"), QDialogButtonBox::DestructiveRole);
    buttons->addButton(QStringLiteral("关闭"), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);

    RoleLibraryChoice choice;
    const auto updateButtons = [&] {
        const auto* item = list->currentItem();
        const bool editable = item && item->data(Qt::UserRole).toString() != RoleLibrary::builtInId();
        exportButton->setEnabled(item != nullptr);
        shareButton->setEnabled(item != nullptr);
        switchButton->setEnabled(item != nullptr);
        renameButton->setEnabled(editable);
        removeButton->setEnabled(editable);
    };
    QObject::connect(list, &QListWidget::currentItemChanged, &dialog, updateButtons);

    const auto refresh = [&](const QString& selected) {
        QString error;
        const auto entries = library.entries(error);
        if (!error.isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("无法读取角色库"), error);
            return false;
        }
        list->clear();
        for (const auto& role : entries) {
            const bool active = role.id == current;
            auto* item = new QListWidgetItem(active ? role.name + QStringLiteral("  ·  当前角色") : role.name, list);
            item->setData(Qt::UserRole, role.id);
            item->setData(Qt::UserRole + 1, role.name);
            if (role.id == selected) {
                list->setCurrentItem(item);
            }
        }
        if (!list->currentItem()) {
            list->setCurrentRow(0);
        }
        updateButtons();
        return true;
    };
    if (!refresh(current)) {
        return {};
    }

    const auto choose = [&] {
        if (const auto* item = list->currentItem()) {
            choice.selected = item->data(Qt::UserRole).toString();
            dialog.accept();
        }
    };
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, choose);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog, choose);

    list->import = [&](const QString& path) {
        const auto id = importRolePackage(dialog, library, path);
        if (!id.isEmpty()) {
            choice.changed.append(id);
            refresh(id);
        }
    };
    QObject::connect(importButton, &QPushButton::clicked, &dialog, [&] {
        const auto path = QFileDialog::getOpenFileName(&dialog, QStringLiteral("导入角色包"), {},
            QStringLiteral("AuraPet 角色包 (*.gifpet)"));
        if (!path.isEmpty()) {
            list->import(path);
        }
    });
    QObject::connect(exportButton, &QPushButton::clicked, &dialog, [&] {
        const auto* item = list->currentItem();
        if (item) {
            exportRolePackage(dialog, library, item->data(Qt::UserRole).toString(),
                              item->data(Qt::UserRole + 1).toString());
        }
    });
    QObject::connect(shareButton, &QPushButton::clicked, &dialog, [&] {
        const auto* item = list->currentItem();
        if (item && editRoleShare(dialog, library, item->data(Qt::UserRole).toString())) {
            choice.changed.append(item->data(Qt::UserRole).toString());
        }
    });

    QObject::connect(renameButton, &QPushButton::clicked, &dialog, [&] {
        const auto* item = list->currentItem();
        const auto id = item->data(Qt::UserRole).toString();
        bool confirmed = false;
        const auto name = QInputDialog::getText(&dialog, QStringLiteral("重命名角色"), QStringLiteral("角色名称"),
                                               QLineEdit::Normal, item->data(Qt::UserRole + 1).toString(), &confirmed);
        if (!confirmed) {
            return;
        }
        QString error;
        if (library.rename(id, name, error)) {
            choice.changed.append(id);
            refresh(id);
        } else {
            QMessageBox::warning(&dialog, QStringLiteral("无法重命名"), error);
        }
    });
    QObject::connect(removeButton, &QPushButton::clicked, &dialog, [&] {
        const auto* item = list->currentItem();
        const auto id = item->data(Qt::UserRole).toString();
        const auto name = item->data(Qt::UserRole + 1).toString();
        const auto answer = QMessageBox::question(&dialog, QStringLiteral("删除角色"),
            QStringLiteral("删除「%1」及其本地聊天记录？").arg(name), QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
            return;
        }
        QString error;
        if (library.remove(id, error)) {
            choice.removed.append(id);
            refresh(RoleLibrary::builtInId());
        } else {
            QMessageBox::warning(&dialog, QStringLiteral("无法删除"), error);
        }
    });

    dialog.exec();
    return choice;
}
