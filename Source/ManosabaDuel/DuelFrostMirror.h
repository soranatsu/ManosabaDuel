#pragma once

#include "CoreMinimal.h"

class AActor;
class USceneComponent;

namespace DuelFrostMirror
{
    // Builds one cosmetic cluster; +Z is its long axis. Existing prism roots own animation.
    USceneComponent* Create(AActor* Owner, USceneComponent* Parent, int32 Variant, float Length);
}
