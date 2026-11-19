#include "DuelArena.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "DuelGame.h"
#include "DuelNorthLayout.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelCourtInteractionsQA, "ManosabaDuel.NorthTower.CourtInteractions346",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelCourtInteractionsQA::RunTest(const FString&)
{
    if (!TestTrue(TEXT("Court QA uses an isolated North Tower without saves"),
        FParse::Param(FCommandLine::Get(), TEXT("DuelNorthTower")) && FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")))) return false;
    ADuelMode* Mode = nullptr;
    for (const auto& Context : GEngine->GetWorldContexts())
        if (Context.World() && (Mode = Context.World()->GetAuthGameMode<ADuelMode>())) break;
    if (!TestTrue(TEXT("Actual arena and fighters exist"), Mode && Mode->PlayerFighter && Mode->BossFighter && Mode->Arena)) return false;
    auto* Arena = Mode->Arena.Get();
    auto* Player = Mode->PlayerFighter.Get();
    auto* Boss = Mode->BossFighter.Get();
    const auto SavedBoss = Boss->Character;
    const int32 SavedEncounter = Boss->BossEncounter;
    const FVector SavedOrigin = Arena->NorthOrigin;
    const bool SavedNorthOrigin = Arena->bNorthOrigin;
    const auto Right = Player->Equipped, Left = Player->Offhand;
    ON_SCOPE_EXIT
    {
        Arena->ClearCombatEffects();
        Arena->NorthOrigin = SavedOrigin;
        Arena->bNorthOrigin = SavedNorthOrigin;
        Boss->Character = SavedBoss;
        Boss->BossEncounter = SavedEncounter;
        Boss->BuildFigure();
        Player->SetWeapon(Right);
        Player->SetWeapon(Left, EDuelHand::Left);
        Mode->ReturnToCheckpoint();
    };
    const auto Reset = [&](int32 Encounter, EDuelCharacter Character)
    {
        Arena->ClearCombatEffects();
        Mode->Result = EDuelOutcome::Fighting;
        Arena->SetNorthOrigin(DuelNorthLayout::CourtOrigin(Encounter));
        Player->ResetFighter(true); Player->RestAtCheckpoint();
        Boss->Character = Character; Boss->BossEncounter = Encounter;
        Boss->ResetFighter(true); Boss->BossPhase = 2;
        const FVector Origin = Arena->CourtOrigin();
        Player->SetActorLocationAndRotation(Origin + FVector(-460, 0, 62), FRotator::ZeroRotator);
        Boss->SetActorLocationAndRotation(Origin + FVector(460, 0, 103), FRotator(0, 180, 0));
        Player->ConsumeMovementInputVector(); Boss->ConsumeMovementInputVector();
    };
    for (const auto Character : {EDuelCharacter::Ema, EDuelCharacter::Hiro})
    {
        Reset(0, Character);
        Player->SetActorLocation(Arena->CourtOrigin() + FVector(-950, 600, 62));
        Player->ConfirmDuel();
        TestTrue(TEXT("Combat F cannot dismantle the new projectile-chain mechanic or open a door/menu"),
            Mode->Outcome() == EDuelOutcome::Fighting && Arena->CourtProgress == 0 && !UGameplayStatics::IsGamePaused(Mode));
        UGameplayStatics::SetGamePaused(Mode, true);
        TestFalse(TEXT("Paused F remains available to ordinary menu confirmation"), Arena->TryCourtInteraction(*Player));
        UGameplayStatics::SetGamePaused(Mode, false);
    }
    // Versioned mechanisms have their own real-contact suites; this retained alias covers shared input and mirror AI.
    const auto Ranged = [](EDuelMove M)
    {
        return M == EDuelMove::OssuaryShears || M == EDuelMove::Seals || M == EDuelMove::AtlasRain ||
            M == EDuelMove::Cross || M == EDuelMove::WingVerdict || M == EDuelMove::Tribunal ||
            M == EDuelMove::BoneProcession || M == EDuelMove::WrittenVerdict || M == EDuelMove::NightLanes ||
            M == EDuelMove::ShadowSnare || M == EDuelMove::ShadowVolley;
    };
    for (const auto Character : {EDuelCharacter::Ema, EDuelCharacter::Hiro})
        for (int32 Phase : {2, 3})
            for (float Distance : {420.f, 800.f})
            {
                Reset(1, Character); Boss->BossPhase = Phase;
                int32 Counts[2] = {};
                for (int32 Mirror = 0; Mirror < 2; ++Mirror)
                {
                    Boss->BossEncounter = Mirror;
                    Boss->BossRandom.Initialize(3461701);
                    for (int32 Sample = 0; Sample < 3000; ++Sample) Counts[Mirror] += Ranged(Boss->PickBossAttack(Distance));
                }
                AddInfo(FString::Printf(TEXT("Ranged preference character=%d phase=%d distance=%.0f baseline=%d mirror=%d /3000"),
                    int32(Character), Phase, Distance, Counts[0], Counts[1]));
                TestTrue(TEXT("Both mirror identities strongly prefer active ranged attacks in phases two and three"),
                    Counts[1] > 1850 && Counts[1] > Counts[0] + 600);
            }
    for (const auto Character : {EDuelCharacter::Ema, EDuelCharacter::Hiro})
    {
        Reset(1, Character);
        const FVector Origin = Arena->CourtOrigin();
        Player->SetActorLocation(Origin + FVector(0, 0, 62));
        Boss->SetActorLocation(Origin + FVector(580, 0, 103));
        Boss->BossPause = 10.f;
        const FVector Toward = (Player->GetActorLocation() - Boss->GetActorLocation()).GetSafeNormal2D();
        Boss->TickBoss(.01f);
        const FVector RangedStep = Boss->ConsumeMovementInputVector();
        TestTrue(TEXT("A mirror caster at medium distance strafes and opens room for ranged attacks"),
            FVector::DotProduct(RangedStep, Toward) < -.1f && FMath::Abs(FVector::CrossProduct(RangedStep, Toward).Z) > .5f);
        Boss->BossPhase = 1;
        Boss->TickBoss(.01f);
        TestTrue(TEXT("First-phase movement retains its original approach"),
            FVector::DotProduct(Boss->ConsumeMovementInputVector(), Toward) > .5f);
        for (int32 Phase : {2, 3})
        {
            Boss->BossPhase = Phase;
            const auto From = Character == EDuelCharacter::Ema ? EDuelMove::FlankLeap : EDuelMove::Reversal;
            const float Distance = Character == EDuelCharacter::Ema ? 420.f : 580.f;
            int32 Counts[2] = {};
            for (int32 Mirror = 0; Mirror < 2; ++Mirror)
            {
                Boss->BossEncounter = Mirror;
                Boss->BossRandom.Initialize(3461712);
                for (int32 Sample = 0; Sample < 3000; ++Sample) Counts[Mirror] += Ranged(Boss->PickBossFollowup(From, Distance));
            }
            AddInfo(FString::Printf(TEXT("Ranged followup character=%d phase=%d baseline=%d mirror=%d /3000"),
                int32(Character), Phase, Counts[0], Counts[1]));
            TestTrue(TEXT("The next combo link also favours a real ranged move rather than resuming a melee rush"), Counts[1] > Counts[0] + 500);
        }
    }
    return !HasAnyErrors();
}
#endif
