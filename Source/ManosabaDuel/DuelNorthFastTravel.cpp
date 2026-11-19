#include "DuelNorthTower.h"

#include "DuelArena.h"
#include "DuelGame.h"
#include "DuelNorthLighting.h"
#include "DuelNorthNarrative.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"

bool ADuelNorthTower::CanFastTravel() const
{
    const auto* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    return bMap && Mode && !UGameplayStatics::IsGamePaused(this) &&
        (Mode->Outcome() == EDuelOutcome::Ready || Mode->Outcome() == EDuelOutcome::Victory) &&
        Mode->GetPlayerFighter() && Mode->GetPlayerFighter()->IsAlive() && Mode->FigureView == 0 &&
        ActiveFight == INDEX_NONE && ActivePuzzle == INDEX_NONE && !bBossDefeatPending &&
        (!NarrativeActor || !NarrativeActor->BlocksInput());
}

bool ADuelNorthTower::CanTravelTo(DuelNorthLayout::EFloor Floor) const
{
    return CanFastTravel() && State.IsCheckpointActivated(static_cast<EDuelNorthFloor>(Floor));
}

const TCHAR* ADuelNorthTower::TravelPointName(DuelNorthLayout::EFloor Floor)
{
    using DuelNorthLayout::EFloor;
    switch (Floor)
    {
    case EFloor::Dungeon: return TEXT("B1 禁闭区休整点");
    case EFloor::Prison: return TEXT("1F 大厅休整点");
    case EFloor::Chapel: return TEXT("2F 档案室休整点");
    case EFloor::Mirror: return TEXT("3F 镜厅休整点");
    case EFloor::Final: return TEXT("4F 终审厅休整点");
    default: return TEXT("未点亮的休整点");
    }
}

bool ADuelNorthTower::SelectTravelPoint(DuelNorthLayout::EFloor Floor)
{
    if (!CanTravelTo(Floor)) return false;
    TravelSelection = Floor;
    TravelFeedback.Reset();
    return true;
}

void ADuelNorthTower::NavigateTravelPoints(ADuelMode& Mode, int32 Step)
{
    using namespace DuelNorthLayout;
    TArray<EFloor, TInlineAllocator<5>> Available;
    for (const auto Floor : MapOrder) if (CanTravelTo(Floor)) Available.Add(Floor);
    if (Available.IsEmpty()) return;
    int32 Index = Available.IndexOfByKey(TravelSelection);
    if (Index == INDEX_NONE)
    {
        Index = Available.IndexOfByKey(static_cast<EFloor>(State.Checkpoint));
        if (Index == INDEX_NONE) Index = Step < 0 ? Available.Num() - 1 : 0;
    }
    else Index = (Index + (Step < 0 ? -1 : 1) + Available.Num()) % Available.Num();
    if (SelectTravelPoint(Available[Index])) Mode.BrowsedMapFloor = static_cast<int32>(Available[Index]);
}

bool ADuelNorthTower::ConfirmTravel(ADuelMode& Mode)
{
    using namespace DuelNorthLayout;
    if (!CanFastTravel()) { TravelFeedback = TEXT("当前无法旅行。"); return false; }
    const auto* Rest = Checkpoints().FindByPredicate([&](const FCheckpoint& Point) { return Point.Floor == TravelSelection; });
    if (!Rest || !CanTravelTo(Rest->Floor))
    {
        TravelFeedback = TEXT("先选择一个已点亮的休整点。");
        return false;
    }
    auto& Player = *Mode.PlayerFighter;
    const auto* Capsule = Player.GetCapsuleComponent();
    const float Half = Capsule->GetScaledCapsuleHalfHeight();
    const auto Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Half);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(NorthFastTravelLanding), false, &Player);
    Query.AddIgnoredActor(Mode.BossFighter);
    if (NarrativeActor) Query.AddIgnoredActor(NarrativeActor->Companion());
    TOptional<FVector> Landing;
    for (const FVector Offset : {FVector::ZeroVector, FVector(85, 0, 0), FVector(-85, 0, 0),
        FVector(0, 85, 0), FVector(0, -85, 0), FVector(100, 100, 0), FVector(-100, -100, 0)})
    {
        const FVector Around = Rest->Position + Offset;
        FHitResult Floor;
        if (!GetWorld()->LineTraceSingleByChannel(Floor, Around + FVector(0, 0, 140), Around - FVector(0, 0, 45), ECC_Pawn, Query) ||
            Floor.ImpactNormal.Z < .7f || FMath::Abs(Floor.ImpactPoint.Z - Rest->Position.Z) > 8.f ||
            (Floor.GetComponent() && Floor.GetComponent()->ComponentHasTag(TEXT("NorthStandingFurniture")))) continue;
        const FVector At(Around.X, Around.Y, Floor.ImpactPoint.Z + Half + 2.f);
        if (GetWorld()->OverlapBlockingTestByChannel(At, FQuat::Identity, ECC_Pawn, Shape, Query)) continue;
        Landing = At;
        break;
    }
    if (!Landing.IsSet()) { TravelFeedback = TEXT("休整点附近被挡住了，请稍后再试。"); return false; }

    const auto Destination = Rest->Floor;
    // Travel changes the respawn choice, never the activation set, keys, gates or boss victories.
    State.Checkpoint = static_cast<uint8>(Destination);
    Respawn(Mode, &Landing.GetValue());
    Location = Locate(Landing.GetValue());
    if (!Location.Room.IsNone()) Explored.Add(Location.Room);
    State.ExploreFloor(static_cast<EDuelNorthFloor>(Destination));
    auto* Motion = Player.GetCharacterMovement();
    Motion->SetMovementMode(MOVE_Walking);
    Motion->bForceNextFloorCheck = true;

    // Reset the lag history at the destination before restoring normal camera smoothing.
    const bool bLag = Player.CameraBoom->bEnableCameraLag;
    Player.CameraBoom->bEnableCameraLag = false;
    Player.TickCamera(1.f);
    Player.CameraBoom->TickComponent(0.f, LEVELTICK_All, nullptr);
    Player.CameraBoom->bEnableCameraLag = bLag;
    Mode.Arena->SetNorthExplorationScore(Destination == EFloor::Dungeon || Destination == EFloor::Prison ?
        TEXT("UnderStone") : TEXT("MarginNotes"));
    DuelNorthLighting::Update(this, Landing.GetValue(), 1.f, &Location);
    Nearby = Rest->Id;
    Notice = FString::Printf(TEXT("已到达%s。"), TravelPointName(Destination));
    NoticeUntil = GetWorld()->GetTimeSeconds() + 4.;
    Save();
    Mode.RefreshMenuPointer();
    return true;
}
