#pragma once

#include "pet_asset.h"
#include "role_library.h"

QString actionSlotName(ActionSlot slot);
std::optional<PetAsset> loadRoleAction(const RoleLibrary& library, const QString& id, ActionSlot slot,
                                      const QString& builtInAsset, QString& error);
