#include "action_editor.h"
#include "role_actions.h"

#ifdef Q_OS_MACOS
#include "sequence_dialog.h"
#include "template_dialog.h"
#endif

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

bool editRoleActions(RoleLibrary& library, const QString& roleId, const QString& builtInAsset, QWidget* parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("角色动作"));
    dialog.resize(505, 380);
    auto* layout = new QVBoxLayout(&dialog);
    auto* slotsList = new QComboBox(&dialog);
    for (const auto slot : {ActionSlot::Idle, ActionSlot::Happy, ActionSlot::Sleepy}) {
        slotsList->addItem(actionSlotName(slot), static_cast<int>(slot));
    }
    auto* filename = new QLabel(&dialog);
    filename->setTextFormat(Qt::PlainText);
    filename->setWordWrap(true);
    auto* preview = new QLabel(&dialog);
    preview->setMinimumSize(330, 245);
    preview->setAlignment(Qt::AlignCenter);
    preview->setStyleSheet(QStringLiteral("background-color: palette(base);"));
    auto* buttons = new QDialogButtonBox(&dialog);
    auto* choose = buttons->addButton(QStringLiteral("选择素材…"), QDialogButtonBox::ActionRole);
#ifdef Q_OS_MACOS
    auto* sequence = buttons->addButton(QStringLiteral("导入序列帧…"), QDialogButtonBox::ActionRole);
    auto* templates = buttons->addButton(QStringLiteral("动作模板…"), QDialogButtonBox::ActionRole);
#endif
    auto* remove = buttons->addButton(QStringLiteral("移除素材"), QDialogButtonBox::DestructiveRole);
    buttons->addButton(QStringLiteral("完成"), QDialogButtonBox::AcceptRole);
    layout->addWidget(slotsList);
    layout->addWidget(filename);
    layout->addWidget(preview, 1);
    layout->addWidget(buttons);

    std::optional<PetAsset> currentPreview;
    bool idleChanged = false;
    const auto selectedSlot = [&] { return static_cast<ActionSlot>(slotsList->currentData().toInt()); };
    const auto refresh = [&] {
        const auto slot = selectedSlot();
        preview->setMovie(nullptr);
        preview->clear();
        currentPreview.reset();
        QString error;
        const auto assigned = library.actionFor(roleId, slot, error);
        remove->setEnabled(assigned.has_value());
        remove->setText(slot == ActionSlot::Idle ? QStringLiteral("恢复原待机") : QStringLiteral("移除素材"));
        if (!error.isEmpty()) {
            filename->setText(error);
            return;
        }
        if (assigned) {
            filename->setText(assigned->name);
            currentPreview = decodePetAsset(assigned->bytes, error);
        } else if (slot == ActionSlot::Idle) {
            filename->setText(QStringLiteral("使用角色的原素材"));
            currentPreview = loadRoleAction(library, roleId, slot, builtInAsset, error);
        } else {
            filename->setText(QStringLiteral("尚未设置动作素材"));
        }
        if (!currentPreview) {
            preview->setText(error.isEmpty() ? QStringLiteral("选择图片或动图查看预览") : error);
            return;
        }
        const QSize fit = currentPreview->firstFrame.size().scaled(306, 221, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
        if (currentPreview->movie) {
            currentPreview->movie->setScaledSize(fit);
            preview->setMovie(currentPreview->movie.get());
            currentPreview->movie->start();
        } else {
            preview->setPixmap(currentPreview->firstFrame.scaled(fit, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    };
    const auto saveAction = [&](ActionSlot slot, const ActionAsset& asset) {
        QString error;
        if (!library.saveAction(roleId, slot, asset, error)) {
            QMessageBox::warning(&dialog, QStringLiteral("无法设置动作"), error);
            return;
        }
        idleChanged |= slot == ActionSlot::Idle;
        refresh();
    };
    QObject::connect(slotsList, &QComboBox::currentIndexChanged, &dialog, refresh);
    QObject::connect(choose, &QPushButton::clicked, &dialog, [&] {
        const auto slot = selectedSlot();
        const auto path = QFileDialog::getOpenFileName(&dialog, QStringLiteral("选择动作素材"), {},
            QStringLiteral("桌宠素材 (*.png *.jpg *.jpeg *.gif *.agif *.avif);;所有文件 (*)"));
        if (path.isEmpty()) {
            return;
        }
        QString error;
        const auto asset = loadPetAsset(path, error);
        if (!asset) {
            QMessageBox::warning(&dialog, QStringLiteral("无法设置动作"), error);
            return;
        }
        saveAction(slot, {QFileInfo(path).fileName(), asset->source});
    });
#ifdef Q_OS_MACOS
    QObject::connect(sequence, &QPushButton::clicked, &dialog, [&] {
        const auto slot = selectedSlot();
        if (const auto asset = importFrameSequence(slot, &dialog)) {
            saveAction(slot, *asset);
        }
    });
    QObject::connect(templates, &QPushButton::clicked, &dialog, [&] {
        const auto slot = selectedSlot();
        QString error;
        const auto idle = loadRoleAction(library, roleId, ActionSlot::Idle, builtInAsset, error);
        if (!idle) {
            QMessageBox::warning(&dialog, QStringLiteral("无法打开动作模板"),
                error.isEmpty() ? QStringLiteral("请先为角色设置可用的待机素材。") : error);
            return;
        }
        const auto previous = library.actionFor(roleId, slot, error);
        if (const auto asset = createTemplateAction(slot, idle->firstFrame.toImage(), &dialog,
                                                     previous ? previous->recipe : QJsonObject{})) {
            saveAction(slot, *asset);
        }
    });
#endif
    QObject::connect(remove, &QPushButton::clicked, &dialog, [&] {
        const auto slot = selectedSlot();
        QString error;
        if (!library.removeAction(roleId, slot, error)) {
            QMessageBox::warning(&dialog, QStringLiteral("无法移除动作"), error);
            return;
        }
        idleChanged |= slot == ActionSlot::Idle;
        refresh();
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    refresh();
    dialog.exec();
    preview->setMovie(nullptr);
    return idleChanged;
}
