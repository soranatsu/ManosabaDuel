#include "DuelGame.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "DuelArena.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelShieldGroundMagicQA, "ManosabaDuel.ShieldGroundMagic",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelShieldGroundMagicQA::RunTest(const FString&)
{
    if (!TestTrue(TEXT("Ground shield QA uses an isolated linear world with saves disabled"),
        FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")) &&
        FParse::Param(FCommandLine::Get(), TEXT("DuelLinearRoute")))) return false;
    ADuelMode* Mode = nullptr;
    for (const auto& Context : GEngine->GetWorldContexts())
        if (Context.World() && (Mode = Context.World()->GetAuthGameMode<ADuelMode>())) break;
    if (!TestTrue(TEXT("Actual fighters and arena exist"), Mode && Mode->PlayerFighter && Mode->BossFighter && Mode->Arena)) return false;
    auto* Player = Mode->PlayerFighter.Get();
    auto* Boss = Mode->BossFighter.Get();
    const auto SavedBoss = Boss->Character;
    const auto Right = Player->Equipped, Left = Player->Offhand;
    ON_SCOPE_EXIT
    {
        Boss->Character = SavedBoss;
        Player->SetWeapon(Right);
        Player->SetWeapon(Left, EDuelHand::Left);
        Mode->ResetEncounter();
    };
    struct FCase { EDuelCharacter Character; EDuelMove Move; bool bPrecise = true; };
    const FCase Cases[] = {{EDuelCharacter::Ema, EDuelMove::Seals},
        {EDuelCharacter::Ema, EDuelMove::Storm}, {EDuelCharacter::Ema, EDuelMove::AtlasRain},
        {EDuelCharacter::Ema, EDuelMove::Pillar, false}, {EDuelCharacter::Ema, EDuelMove::WingVerdict, false},
        {EDuelCharacter::Hiro, EDuelMove::NightLanes}, {EDuelCharacter::Hiro, EDuelMove::InwardReap},
        {EDuelCharacter::Yuki, EDuelMove::BrokenRecord}, {EDuelCharacter::Yuki, EDuelMove::FinalSentence},
        {EDuelCharacter::Yuki, EDuelMove::ArchiveCollapse}};
    struct FResult { float Damage, Stamina, BossPosture; EDuelMove Move; bool bContact; };
    const auto Run = [&](const FCase& Case, EDuelWeapon Shield, EDuelHand Hand,
        bool bLocked, bool bPrecise, bool bRear, bool bExhausted)
    {
        Mode->ResetEncounter();
        const FVector Origin = Mode->Arena->CourtOrigin();
        Player->SetActorLocationAndRotation(Origin + FVector(0, 0, 62), FRotator::ZeroRotator);
        Boss->SetActorLocationAndRotation(Origin + FVector(550, 0, 103), FRotator(0, 180, 0));
        Boss->Character = Case.Character;
        Boss->BossPhase = 3;
        const bool bStarted = Boss->StartMove(Case.Move);
        TestTrue(TEXT("The real ground spell starts"), bStarted);
        FVector At = Origin;
        if (Case.Move == EDuelMove::Seals) At = Boss->SealCenters[0];
        else if (Case.Move == EDuelMove::Storm) At = Boss->GetActorLocation() + FVector(220, 0, -103);
        else if (Case.Move == EDuelMove::InwardReap)
            At = Boss->CastOrigin + FVector((DuelCourt::ReapInner + DuelCourt::ReapOuter) * .5f, 0, 0);
        else if (Case.Move == EDuelMove::ArchiveCollapse)
            At = Boss->CastOrigin + FVector((DuelCourt::ArchiveInner + DuelCourt::ArchiveOuter) * .5f, 0, 0);
        else if (!Boss->MapWaves.IsEmpty() && !Boss->MapWaves[0].Marks.IsEmpty()) At = Boss->MapWaves[0].Marks[0];
        Player->SetActorLocation(FVector(At.X, At.Y, Origin.Z + 62));
        FVector Source = Boss->GetActorLocation();
        if ((Case.Move == EDuelMove::NightLanes || Case.Move == EDuelMove::WingVerdict) &&
            !Boss->MapWaves.IsEmpty() && !Boss->MapWaves[0].Marks.IsEmpty())
            Source = Boss->MapWaves[0].Marks[0] - Boss->MapWaves[0].Axis * Boss->MapWaves[0].HalfLength;
        const FRotator Facing = (Source - Player->GetActorLocation()).GetSafeNormal2D().Rotation();
        Player->SetActorRotation(Facing + FRotator(0, bRear ? 180 : 0, 0));
        Player->bLockOn = bLocked;
        Player->SetWeapon(Shield, Hand);
        if (DuelCourt::IsShield(Shield))
        {
            Player->PressHand(Hand);
            auto& Press = Player->HandPresses[static_cast<uint8>(Hand)];
            Press.bParryReady = bPrecise;
            if (!bPrecise) Press.Since -= 1.;
        }
        Player->Stamina = bExhausted ? 1.f : DuelCourt::MaxStamina;
        const float BeforeStamina = Player->Stamina;
        for (int32 Frame = 0; bStarted && Frame < 800 && Boss->Move == Case.Move; ++Frame)
        {
            Boss->TickMove(.01f);
            if (Player->Health < DuelCourt::PlayerMaxHealth || Player->Stamina < BeforeStamina || Player->Move == EDuelMove::Parry)
                return FResult{DuelCourt::PlayerMaxHealth - Player->Health, BeforeStamina - Player->Stamina,
                    Boss->Posture, Player->Move, true};
        }
        return FResult{0, 0, Boss->Posture, Player->Move, false};
    };
    for (const auto& Case : Cases)
    {
        const auto Baseline = Run(Case, EDuelWeapon::Empty, EDuelHand::Left, false, false, false, false);
        if (!TestTrue(TEXT("The actual area or wave delivers a control hit"), Baseline.bContact && Baseline.Damage > 0)) continue;
        for (const auto Hand : {EDuelHand::Left, EDuelHand::Right})
            for (const auto Shield : {EDuelWeapon::Buckler, EDuelWeapon::Greatshield})
                for (const bool bLocked : {false, true})
                {
                    const auto Guard = DuelCourt::ShieldResponse(Shield);
                    const auto Normal = Run(Case, Shield, Hand, bLocked, false, false, false);
                    const float Cost = (Guard.CostBase + Baseline.Damage / DuelCourt::BossDamageScale * Guard.CostPerDamage) * DuelCourt::StaminaCostScale;
                    TestTrue(TEXT("A held front shield takes only its own chip and exact stamina cost from the actual ground spell"),
                        Normal.bContact && FMath::IsNearlyEqual(Normal.Damage, Baseline.Damage * Guard.Chip, .05f) &&
                        FMath::IsNearlyEqual(Normal.Stamina, Cost, .05f) && Normal.Move == EDuelMove::Idle);
                    const auto Precise = Run(Case, Shield, Hand, bLocked, true, false, false);
                    if (Case.bPrecise)
                        TestTrue(TEXT("A timed ground-spell guard spends parry stamina, prevents HP loss and cannot remotely stagger the caster"),
                            Precise.bContact && FMath::IsNearlyZero(Precise.Damage) &&
                            FMath::IsNearlyEqual(Precise.Stamina, DuelCourt::ParryStaminaCost, .01f) &&
                            Precise.Move == EDuelMove::Parry && FMath::IsNearlyZero(Precise.BossPosture));
                    else
                        TestTrue(TEXT("An originally block-only spell retains normal chip and stamina even inside the precise timing window"),
                            Precise.bContact && FMath::IsNearlyEqual(Precise.Damage, Normal.Damage, .05f) &&
                            FMath::IsNearlyEqual(Precise.Stamina, Normal.Stamina, .05f) && Precise.Move == EDuelMove::Idle);
                }
        const auto Rear = Run(Case, EDuelWeapon::Greatshield, EDuelHand::Left, false, false, true, false);
        TestTrue(TEXT("Unlocked guarding still requires facing the real incoming source"),
            Rear.bContact && FMath::IsNearlyEqual(Rear.Damage, Baseline.Damage, .05f) && FMath::IsNearlyZero(Rear.Stamina));
        const auto Broken = Run(Case, EDuelWeapon::Greatshield, EDuelHand::Left, true, false, false, true);
        TestTrue(TEXT("Insufficient stamina causes real guard break and full damage rather than free immunity"),
            Broken.bContact && Broken.Move == EDuelMove::GuardBreak &&
            FMath::IsNearlyEqual(Broken.Damage, Baseline.Damage, .05f) && FMath::IsNearlyEqual(Broken.Stamina, 1.f));
    }
    return !HasAnyErrors();
}
#endif
