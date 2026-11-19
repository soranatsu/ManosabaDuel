#pragma once

#include "CoreMinimal.h"

class AActor;
class USceneComponent;
class UProceduralMeshComponent;
struct FDuelNorthProgress;

namespace DuelNorthInteractables
{
    enum class EKind : uint8 { Relic, Clue, Rest, Lock };
    enum class ECueTone : uint8 { Reading, Pickup, Rest, Treasure };
    inline constexpr double LedgerDeckHeight = 84.;
    inline constexpr double LedgerReach = 158.;

    struct FPlacement
    {
        FName Id;
        EKind Kind = EKind::Clue;
        uint8 Index = 0;
        FVector Position = FVector::ZeroVector;
        FRotator Rotation = FRotator::ZeroRotator;
        // Local footprint half-size, plus total prop height in Z, in centimetres.
        FVector Size = FVector(30, 25, 70);
        const TCHAR* LedgerTitle = nullptr;
        double MountHeight = 0.;
        bool bOptional = false;

        bool IsArchiveVolume() const { return MountHeight > 0.; }
        FVector Front() const { return Rotation.RotateVector(FVector::RightVector); }
        FVector Approach() const { return LedgerTitle ? Position + Front() * (Size.Y + 72.) : Position; }
        FVector ReadingPoint() const
        {
            const FVector Local = IsArchiveVolume() ? FVector(0, Size.Y + 2.8, MountHeight + Size.Z * .65) :
                FVector(0, Size.Y * .37, LedgerDeckHeight + 6.2);
            return Position + Rotation.RotateVector(Local);
        }
    };

    // Original small meshes reuse the project's stone, paper and metal materials.
    // Build once on the tower. Parent shares NorthLayout's coordinate system.
    void Build(AActor* Owner, USceneComponent* Parent);
    void Update(AActor* Owner, const FDuelNorthProgress& State, FName Nearby, float DeltaSeconds, bool bShowCues = true);
    void PullArchiveVolume(AActor* Owner, FName Id);
    UProceduralMeshComponent* BuildStoryCue(AActor* Owner, USceneComponent* Parent, FName Id,
        const FVector& Origin = FVector::ZeroVector);
    void UpdateStoryCue(UProceduralMeshComponent* Cue, bool bShow, ECueTone Tone, float Strength, float Seconds);
    const TArray<FPlacement>& Placements();
    const FPlacement* Ledger(FName Id);
    bool IsVisible(const FPlacement& Placement, const FDuelNorthProgress& State);
    bool Validate(TArray<FString>& Errors);
}
