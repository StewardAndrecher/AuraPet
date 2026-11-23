#pragma once

#include "pet_asset.h"

class QWidget;

std::optional<PetAsset> previewPetImport(PetAsset asset, QWidget* parent, QString& error);
