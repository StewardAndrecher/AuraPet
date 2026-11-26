#pragma once

#include <QString>
#include <functional>

class PetWindow;
class RoleLibrary;
class SettingsStore;
struct PetAsset;
struct RoleShare;

class RoleActivation {
public:
    RoleActivation(RoleLibrary& library, SettingsStore& settings, PetWindow& window, QString builtInPath);

    const QString& id() const { return id_; }
    void clearSelection() { id_.clear(); }
    void saveLayout();
    bool select(const QString& role, QString& error);
    bool add(const QString& path, PetAsset asset, QString& error);

    std::function<void(const QString&, const RoleShare&)> beforeChange;
    std::function<void(const PetAsset&)> beforeDisplay;

private:
    void display(const QString& role, PetAsset asset);

    RoleLibrary& library_;
    SettingsStore& settings_;
    PetWindow& window_;
    QString builtInPath_;
    QString id_;
};
