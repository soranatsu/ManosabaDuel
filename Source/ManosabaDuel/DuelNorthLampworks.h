#pragma once

#include "CoreMinimal.h"

class AActor;
class UMaterialInterface;
class USceneComponent;

namespace DuelNorthLampworks
{
    extern const TCHAR* Stations[4];

    enum class ETableView : uint8 { Idle, SelectBag, SelectRing, WrongRing, Open };

    int32 Index(FName Id);
    FVector Surface(int32 Index);
    void Build(AActor* Owner, USceneComponent* Parent, TArray<TObjectPtr<USceneComponent>>& EvidenceTableParts);
    void ShowEvidenceTable(TArray<TObjectPtr<USceneComponent>>& Parts, ETableView View, int32 Choice = INDEX_NONE);

    void BuildEnvelope(AActor* Owner, USceneComponent* Parent, const FVector& Center, const FVector2D& Size,
        UMaterialInterface* Paper, UMaterialInterface* Fold);
    void BuildWaxSeal(AActor* Owner, USceneComponent* Parent, const FVector& Center, double Radius,
        UMaterialInterface* Wax, UMaterialInterface* Impression, bool bChipped);

    inline const FVector2D TopPetal[] = {{0, 0}, {-.26, -.38}, {0, -.98}, {.26, -.38}, {0, 0}};
    inline const FVector2D LeftPetal[] = {{0, 0}, {-.46, -.12}, {-.96, .30}, {-.41, .52}, {0, 0}};
    inline const FVector2D RightPetal[] = {{0, 0}, {.46, -.12}, {.96, .30}, {.41, .52}, {0, 0}};
    inline const FVector2D ChippedPetal[] = {{0, 0}, {.46, -.12}, {.96, .30}, {.73, .39},
        {.52, .22}, {.52, .48}, {.41, .52}, {0, 0}};
}
