#pragma once

class AActor;
class USceneComponent;

namespace DuelNorthDressing
{
    // Static set dressing in the North layout's local coordinate system.
    void Build(AActor* Owner, USceneComponent* Parent);
}
