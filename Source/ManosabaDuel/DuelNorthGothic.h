#pragma once

#include "CoreMinimal.h"

class AActor;
class USceneComponent;
class UMaterialInterface;

namespace DuelNorthGothic
{
    struct FPiece
    {
        FName Mesh;
        FName Room;
        FTransform Transform;
        bool bSolid = false;
    };

    // Source kit is authored in centimetres; placements use the same local space
    // as the tower shell. The sealed shell remains behind the dressed stone.
    const TArray<FPiece>& Pieces();
    void Build(AActor* Owner, USceneComponent* Parent, UMaterialInterface* WallStone);
    bool Validate(TArray<FString>& Errors);
}
