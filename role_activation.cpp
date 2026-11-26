#include "role_activation.h"
#include "pet_asset.h"
#include "pet_window.h"
#include "role_actions.h"
#include "role_library.h"
#include "settings_store.h"

#include <QDebug>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <utility>

namespace {
QPoint initialPosition(const PetWindow& window)
{
    if (const auto* screen = QGuiApplication::primaryScreen()) {
        return screen->availableGeometry().bottomRight() - QPoint(window.width() + 24, window.height() + 24);
    }
    return QPoint(24, 24);
}
}

RoleActivation::RoleActivation(RoleLibrary& library, SettingsStore& settings, PetWindow& window, QString builtInPath)
    : library_(library), settings_(settings), window_(window), builtInPath_(std::move(builtInPath))
{
}

void RoleActivation::saveLayout()
{
    settings_.saveLayout(id_, window_.pos(), window_.scalePercent());
}

void RoleActivation::display(const QString& role, PetAsset asset)
{
    QString shareError;
    const auto share = library_.shareFor(role, shareError, false);
    if (!shareError.isEmpty()) {
        qWarning().noquote() << shareError;
    }
    if (beforeChange) {
        beforeChange(role, share);
    }
    const auto previous = window_.pos();
    const bool preferPrevious = !id_.isEmpty() && role != RoleLibrary::legacyId();
    saveLayout();
    id_ = role;
    if (beforeDisplay) {
        beforeDisplay(asset);
    }
    window_.setAsset(std::move(asset), settings_.scale(id_));
    window_.placeAt(settings_.position(id_, preferPrevious ? previous : initialPosition(window_), !preferPrevious));
    saveLayout();
}

bool RoleActivation::select(const QString& role, QString& error)
{
    if (role != RoleLibrary::builtInId() && !library_.available()) {
        error = QStringLiteral("角色库尚未打开。");
        return false;
    }
    auto asset = loadRoleAction(library_, role, ActionSlot::Idle, builtInPath_, error);
    if (!asset && role == RoleLibrary::builtInId() && !error.isEmpty()) {
        qWarning().noquote() << QStringLiteral("无法使用自定义待机素材：") << error;
        error.clear();
        asset = loadPetAsset(builtInPath_, error);
    }
    if (!asset) {
        return false;
    }
    display(role, std::move(*asset));
    return true;
}

bool RoleActivation::add(const QString& path, PetAsset asset, QString& error)
{
    if (!library_.available()) {
        error = QStringLiteral("角色库尚未打开。");
        return false;
    }
    const auto id = library_.add(QFileInfo(path).completeBaseName(), asset.source, error);
    if (id.isEmpty()) {
        return false;
    }
    display(id, std::move(asset));
    return true;
}
