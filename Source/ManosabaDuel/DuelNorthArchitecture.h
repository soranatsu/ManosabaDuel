#pragma once

#include "CoreMinimal.h"

class AActor;
class USceneComponent;

namespace DuelNorthArchitecture
{
    // Surface-level boundary. Inward points toward the walkable side. Units: cm.
    struct FGuardEdge
    {
        FName Id;
        FVector Start;
        FVector End;
        FVector Inward;
        float Height = 140.f;
        float Thickness = 12.f;
        bool bStonePosts = false;
    };

    const TArray<FGuardEdge>& GuardEdges();
    // Uses local coordinates of Parent, which must share the layout coordinate system.
    // Call once after the structural shell. All guard collision carries NorthTowerGuard.
    void Build(AActor* Owner, USceneComponent* Parent);
    // Data check only; actual collision coverage is NorthTower.EdgeGuards.
    bool Validate(TArray<FString>& Errors);
}
