#pragma once

#include "CoreMinimal.h"
#include "DuelNorthExtractionVisual.h"
#include "Camera/CameraTypes.h"
#include "GameFramework/Actor.h"
#include "DuelNorthExtraction.generated.h"

class ADuelFighter;
class APlayerController;
class UAudioComponent;
struct FDuelNorthProgress;
struct FDuelNorthSubtitle;

UCLASS()
class ADuelNorthExtraction final : public AActor
{
    GENERATED_BODY()

public:
    ADuelNorthExtraction();
    static bool CanCall(const ADuelFighter& Player, const FDuelNorthProgress& State);
    static FString CallPrompt(const FDuelNorthProgress& State);
    FString Prompt(const ADuelFighter& Player, const ADuelFighter* Companion, const FDuelNorthProgress& State) const;
    FDuelNorthSubtitle Subtitle() const;
    bool Confirm(ADuelFighter& Player, ADuelFighter* Companion, const FDuelNorthProgress& State);
    bool Update(FDuelNorthProgress& State, ADuelFighter& Player, float DeltaSeconds, bool bExploring);
    bool BlocksInput() const { return Stage >= EStage::Boarding; }
    bool EndingVisible() const { return Stage == EStage::Ending; }
    bool WantsEndingMusic() const { return Stage >= EStage::Departure; }
    void Reset(bool bReturnToLadder);
    virtual void CalcCamera(float DeltaSeconds, FMinimalViewInfo& View) override;

private:
    enum class EStage : uint8 { Idle, Approach, Deploy, Hold, Boarding, Retract, Departure, Ending };
    struct FPassenger
    {
        TWeakObjectPtr<ADuelFighter> Fighter;
        FTransform Before;
        uint8 MovementMode = 0;
        uint8 CustomMovementMode = 0;
        bool bCollision = true;
    };

    DuelNorthExtractionVisual::FVisual Visual;
    EStage Stage = EStage::Idle;
    float StageSeconds = 0.f;
    float RotorSeconds = 0.f;
    bool bPlayerIsEma = false;
    FPassenger Passengers[2];
    TWeakObjectPtr<UAudioComponent> RotorAudio;
    TWeakObjectPtr<APlayerController> CameraController;
    FMinimalViewInfo FlightView;

    void Enter(EStage Next);
    void TakePassenger(FPassenger& Slot, ADuelFighter& Fighter);
    void Board(FPassenger& Slot, const FVector& Seat, float Delay);
    void SeatPassengers(const FTransform& Vehicle);
    void StartRotor();
};
