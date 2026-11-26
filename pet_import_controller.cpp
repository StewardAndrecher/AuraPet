#include "pet_import_controller.h"
#include "background_task.h"
#include "pet_asset.h"
#include "role_activation.h"
#include "role_package.h"

#ifdef Q_OS_MACOS
#include "import_preview.h"
#endif

#include <QApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QScopedValueRollback>
#include <QTimer>

PetImportController::PetImportController(QWidget& parent, RoleLibrary& library, RoleActivation& roles)
    : window_(parent), library_(library), roles_(roles)
{
}

void PetImportController::enqueue(const QString& path, bool preview)
{
    if (path.isEmpty()) {
        return;
    }
    const auto absolute = QFileInfo(path).absoluteFilePath();
    if (absolute == working_) {
        return;
    }
    for (const auto& entry : std::as_const(pending_)) {
        if (entry.first == absolute) {
            return;
        }
    }
    pending_.enqueue(qMakePair(absolute, preview));
    if (!draining_) {
        QTimer::singleShot(0, this, &PetImportController::drain);
    }
}

void PetImportController::drain()
{
    if (draining_ || pending_.isEmpty()) {
        return;
    }
    if (QApplication::activeModalWidget() || QApplication::activePopupWidget()) {
        QTimer::singleShot(150, this, &PetImportController::drain);
        return;
    }
    QScopedValueRollback<bool> running(draining_, true);
    while (!pending_.isEmpty()) {
        const auto [path, preview] = pending_.dequeue();
        QScopedValueRollback<QString> current(working_, path);
        import(path, preview);
    }
}

void PetImportController::import(const QString& path, bool preview)
{
    QString error;
    if (QFileInfo(path).suffix().compare(rolePackageSuffix, Qt::CaseInsensitive) == 0) {
        const auto id = importRolePackage(window_, library_, path);
        if (id.isEmpty()) {
            return;
        }
        if (!roles_.select(id, error)) {
            QMessageBox::warning(&window_, QStringLiteral("无法切换角色"), error);
        }
        window_.show();
        return;
    }
    using Loading = QPair<std::optional<PreparedPetAsset>, QString>;
    auto loaded = waitForBackground<Loading>(window_, QStringLiteral("正在读取图片或动图…"),
        [path](QPromise<Loading>&) {
            Loading result;
            result.first = loadPetPixels(path, result.second);
            return result;
        });
    if (!loaded) {
        return;
    }
    if (!loaded->first) {
        QMessageBox::warning(&window_, QStringLiteral("导入失败"), loaded->second);
        return;
    }
    std::optional<PetAsset> asset(makePlayableAsset(std::move(*loaded->first)));
#ifdef Q_OS_MACOS
    if (preview) {
        asset = previewPetImport(std::move(*asset), &window_, error);
    }
#else
    Q_UNUSED(preview);
#endif
    if (asset && roles_.add(path, std::move(*asset), error)) {
        window_.show();
    } else if (!error.isEmpty()) {
        QMessageBox::warning(&window_, QStringLiteral("导入失败"), error);
    }
}
