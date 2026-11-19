#include "DuelNorthNarrative.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DuelArena.h"
#include "DuelGame.h"
#include "DuelNorthArchive.h"
#include "DuelNorthInvestigationQA.h"
#include "DuelNorthTower.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthFollowFurnitureTest, "ManosabaDuel.NorthTower.FollowFurniture",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthFollowFurnitureTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    if (!TestTrue(TEXT("Furniture follow uses an isolated tower without disk saves"),
        FParse::Param(FCommandLine::Get(), TEXT("DuelNorthTower")) &&
        FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("DuelNorthSaveTest")))) return false;
    ADuelMode* Mode = nullptr;
    for (const auto& Context : GEngine->GetWorldContexts())
        if (Context.World() && (Mode = Context.World()->GetAuthGameMode<ADuelMode>())) break;
    if (!TestTrue(TEXT("Actual furnished tower, player and arena exist"),
        Mode && Mode->NorthTowerActor && Mode->PlayerFighter && Mode->Arena)) return false;
    auto* World = Mode->GetWorld();
    auto& Player = *Mode->PlayerFighter;
    auto* PlayerMotion = Player.GetCharacterMovement();
    const auto SavedTransform = Player.GetActorTransform();
    const auto SavedVelocity = PlayerMotion->Velocity;
    const auto SavedMovement = PlayerMotion->MovementMode;
    const auto SavedCustom = PlayerMotion->CustomMovementMode;
    ADuelNorthNarrative* Narrative = World->SpawnActor<ADuelNorthNarrative>();
    ON_SCOPE_EXIT
    {
        if (IsValid(Narrative)) Narrative->Destroy();
        Player.SetActorTransform(SavedTransform, false, nullptr, ETeleportType::TeleportPhysics);
        PlayerMotion->StopMovementImmediately();
        PlayerMotion->Velocity = SavedVelocity;
        PlayerMotion->SetMovementMode(SavedMovement, SavedCustom);
        PlayerMotion->bForceNextFloorCheck = true;
    };
    if (!TestNotNull(TEXT("Independent follow adapter exists"), Narrative)) return false;

    // These are the installed red/blue reading tables and their two benches, not substitute QA boxes.
    const FTransform Tables[] = {
        FTransform(FRotator(0, -9, 0), FVector(-435, 495, DuelNorthArchive::FloorZ + DuelNorthArchive::ShelfFoot)),
        FTransform(FRotator(0, 187, 0), FVector(575, 1020, DuelNorthArchive::FloorZ + DuelNorthArchive::ShelfFoot))
    };
    constexpr float Step = 1.f / 60.f;
    const FName FurnitureTag(TEXT("NorthStandingFurniture"));
    for (const auto& Table : Tables)
    {
        for (const FVector Local : {FVector(0, 0, 42), FVector(0, -92, 22), FVector(0, 92, 22)})
        {
            bool bInstalled = false;
            for (const auto* Box : TInlineComponentArray<UBoxComponent*>(Mode->NorthTowerActor))
                bInstalled |= Box->ComponentHasTag(FurnitureTag) &&
                    Box->GetComponentLocation().Equals(Table.TransformPosition(Local), 1.f);
            if (!TestTrue(TEXT("The exact authored table and both benches are installed"), bInstalled)) return false;
        }
        for (const auto Route : {EDuelCharacter::Hiro, EDuelCharacter::Ema})
        {
            FDuelNorthProgress State;
            State.Reset(Route);
            if (!TestTrue(TEXT("Rescue fixture completes the real meeting"),
                DuelNorthInvestigationQA::Prepare(State, 0) && State.WinBattle(0) &&
                State.CompleteDialogue(EDuelNorthDialogue::RescuedCompanion))) return false;
            const float PlayerHalf = Player.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            Player.SetActorLocation(Table.TransformPosition(FVector(0, 230, PlayerHalf + 2.f - DuelNorthArchive::ShelfFoot)),
                false, nullptr, ETeleportType::TeleportPhysics);
            Player.SetActorRotation(Table.Rotator());
            PlayerMotion->StopMovementImmediately();
            PlayerMotion->SetMovementMode(MOVE_Walking);
            PlayerMotion->bForceNextFloorCheck = true;
            Narrative->Restore(State, Player, *Mode->Arena);
            auto* Follower = Narrative->RealCompanion.Get();
            if (!TestNotNull(TEXT("Selected route has a real follower"), Follower)) return false;
            TestEqual(TEXT("Both companion identities use the same follow behavior"), Follower->SelectedCharacter(), State.Companion());
            auto* Motion = Follower->GetCharacterMovement();
            const float Half = Follower->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            const float Radius = Follower->GetCapsuleComponent()->GetScaledCapsuleRadius();
            TestTrue(TEXT("Stairs remain stepable while benches cannot be climbed"), Motion->MaxStepHeight >= 18.f && Motion->MaxStepHeight < 44.f);
            FCollisionQueryParams Query(SCENE_QUERY_STAT(NorthFollowFurnitureQA), false, Follower);
            Query.AddIgnoredActor(&Player);
            const auto ClearOnFloor = [&](const FVector& At)
            {
                if (FMath::Abs(At.Z - Half - DuelNorthArchive::FloorZ - 2.f) > 7.f) return false;
                return !World->OverlapBlockingTestByChannel(At, FQuat::Identity, ECC_Pawn,
                    FCollisionShape::MakeCapsule(Radius - 1.f, Half - 2.f), Query);
            };
            const auto Drive = [&](int32 Frames, bool& bStayedGrounded, double& MaxSide)
            {
                for (int32 Frame = 0; Frame < Frames; ++Frame)
                {
                    Narrative->Update(State, Player, Step, true);
                    Follower->Tick(Step);
                    Motion->TickComponent(Step, LEVELTICK_All, nullptr);
                    const FVector At = Follower->GetActorLocation();
                    bStayedGrounded &= ClearOnFloor(At) &&
                        !(Motion->CurrentFloor.HitResult.GetComponent() &&
                            Motion->CurrentFloor.HitResult.GetComponent()->ComponentHasTag(FurnitureTag));
                    MaxSide = FMath::Max(MaxSide, FMath::Abs(Table.InverseTransformPosition(At).X));
                }
            };
            FVector Start = Table.TransformPosition(FVector(0, -230, Half + 2.f - DuelNorthArchive::ShelfFoot));
            FVector Verified;
            if (!TestTrue(TEXT("Both ends of the real table route have capsule clearance"),
                Narrative->FindFollowFloor(Start, Verified) && ClearOnFloor(Player.GetActorLocation()))) return false;
            Follower->SetActorLocation(Start, false, nullptr, ETeleportType::TeleportPhysics);
            Motion->StopMovementImmediately();
            Motion->SetMovementMode(MOVE_Walking);
            Motion->bForceNextFloorCheck = true;
            Narrative->ResetFollow();
            bool bGrounded = true;
            double MaxSide = 0.;
            Drive(600, bGrounded, MaxSide);
            TestTrue(TEXT("Follower walks around the table ends instead of pressing into the bench gap"), MaxSide > 125.);
            TestTrue(TEXT("The whole detour stays on the real room floor without furniture support or penetration"), bGrounded);
            TestTrue(TEXT("Follower resumes and reaches the player across the reading table"),
                FVector::Dist2D(Follower->GetActorLocation(), Player.GetActorLocation()) < 185.f);

            for (const FVector Trapped : {FVector(0, 65, 44.f), FVector(0, 92, 44.f), FVector(0, 0, 84.f)})
            {
                const FVector Bad = Table.TransformPosition(Trapped + FVector(0, 0, Half + 2.f));
                Follower->SetActorLocation(Bad, false, nullptr, ETeleportType::TeleportPhysics);
                Motion->StopMovementImmediately();
                Motion->SetMovementMode(MOVE_Walking);
                Motion->bForceNextFloorCheck = true;
                Narrative->ResetFollow();
                Narrative->Update(State, Player, Step, true);
                const FVector Recovered = Follower->GetActorLocation();
                TestTrue(TEXT("A follower already in the gap, on a bench or on the tabletop returns to clear floor"),
                    ClearOnFloor(Recovered));
                TestTrue(TEXT("Furniture recovery is a bounded local correction"),
                    FVector::Dist2D(Bad, Recovered) <= 385.f);
                bGrounded = true; MaxSide = 0.;
                Drive(480, bGrounded, MaxSide);
                TestTrue(TEXT("After recovery, ordinary following continues on the floor"),
                    bGrounded && FVector::Dist2D(Follower->GetActorLocation(), Player.GetActorLocation()) < 185.f);
            }

            Player.SetActorLocation(Table.TransformPosition(FVector(0, 92, 44.f + PlayerHalf + 2.f)),
                false, nullptr, ETeleportType::TeleportPhysics);
            Narrative->Restore(State, Player, *Mode->Arena);
            TestTrue(TEXT("Checkpoint reconstruction beside furniture also chooses clear ground"),
                ClearOnFloor(Follower->GetActorLocation()) &&
                FVector::Dist2D(Follower->GetActorLocation(), Player.GetActorLocation()) < 550.f);
        }
    }
    return !HasAnyErrors();
}

#endif
