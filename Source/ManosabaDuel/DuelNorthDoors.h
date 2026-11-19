#pragma once

#include "CoreMinimal.h"

class AActor;
class USceneComponent;
namespace DuelNorthLayout { struct FPortal; }

namespace DuelNorthDoors
{
    // Three keyed doors and two one-sided shortcut grilles. The original blocker owns every visible part,
    // so ADuelNorthTower::SetGateOpen remains the sole gate visibility/collision path.
    bool Build(AActor* Owner, USceneComponent* Blocker, const DuelNorthLayout::FPortal& Portal);
}
