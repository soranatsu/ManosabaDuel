#pragma once

#include "CoreMinimal.h"

class AActor;
class USceneComponent;
class UInstancedStaticMeshComponent;
struct FMinimalViewInfo;

namespace DuelNorthExtractionVisual
{
    inline const FVector PickupMark(6350, -300, 3400);
    inline const FVector LadderFoot(6650, 0, 3407);
    inline const FVector BoardingPoint(6650, 0, 4335);
    inline constexpr double AirframeScale = 1.55;
    inline const FVector NearSeatLocal = FVector(-65, -59, -35) * AirframeScale;
    inline const FVector FarSeatLocal = FVector(-65, 59, -35) * AirframeScale;
    inline const FRotator LadderFacing(0, 0, 0);
    inline constexpr float RungSpacing = 57.f;
    inline constexpr float ApproachSeconds = 17.f;
    inline constexpr float DeploySeconds = 4.f;
    inline constexpr float RetractSeconds = 3.2f;
    inline constexpr float DepartureSeconds = 19.f;

    enum class EPhase : uint8 { Hidden, Approach, Deploy, Hold, Retract, Departure };

    struct FVisual
    {
        TWeakObjectPtr<USceneComponent> Root, MainRotor, TailRotor, CabinLight, Ladder, Ropes;
        TWeakObjectPtr<UInstancedStaticMeshComponent> Rungs;
        float LastExtension = -1.f;
        bool bHidden = true;
    };

    FVisual Build(AActor* Owner, USceneComponent* Parent);
    USceneComponent* BuildCallMarker(AActor* Owner, USceneComponent* Parent);
    // Tower coordinates match world coordinates. Seat and ladder vectors describe feet.
    FTransform FlightTransform(EPhase Phase, float Progress01);
    FMinimalViewInfo FlightCamera(const FTransform& Vehicle, float DepartureProgress);
    void SetPose(FVisual& Visual, EPhase Phase, float Progress01, float RotorSeconds);
}
