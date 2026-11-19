#include "DuelCombatMechanics.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "DuelArena.h"
#include "DuelBossDefense.h"
#include "DuelGame.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelMirrorReflectionQA, "ManosabaDuel.MirrorReflection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDuelMirrorReflectionQA::RunTest(const FString&)
{
    for (float Side : {-1.f, 1.f})
    {
        FVector To(80, Side * 160, 100), Velocity(80, Side * 160, 0), Bounce;
        const double Speed = Velocity.Size();
        TestTrue(TEXT("Both mirror faces reflect a crossing flight"),
            DuelCombatMechanics::ReflectMirrorSegment(FVector::ZeroVector, 100.f, 500.f,
                FVector(0, 0, 100), To, Velocity, Bounce));
        TestTrue(TEXT("Reflection preserves the unspent frame travel"), To.Equals(FVector(80, Side * 40, 100), .001));
        TestTrue(TEXT("Bounce lies on the actual mirror plane"), Bounce.Equals(FVector(50, Side * 100, 100), .001));
        TestTrue(TEXT("Specular reflection conserves projectile speed"), FMath::IsNearlyEqual(Velocity.Size(), Speed, .001));
        TestTrue(TEXT("Only the normal component reverses"), Velocity.Equals(FVector(80, -Side * 160, 0), .001));
    }
    FVector To(100, 0, 100), Velocity(100, 0, 0), Bounce;
    TestFalse(TEXT("A parallel projectile does not bounce"),
        DuelCombatMechanics::ReflectMirrorSegment(FVector::ZeroVector, 100.f, 500.f, FVector(0, 0, 100), To, Velocity, Bounce));
    for (double Side : {-1.,1.})
    {
        To=FVector(Side*575.,45.,100.); Velocity=To-FVector(0,0,100.);
        TestTrue(TEXT("End mirrors reflect oblique shots along their visible normal"),
            DuelCombatMechanics::ReflectMirrorSegment(FVector::ZeroVector,100.f,500.f,FVector(0,0,100),To,Velocity,Bounce)
            && FMath::IsNearlyEqual(To.X,Side*425.) && FMath::IsNearlyEqual(To.Y,45.) && Velocity.X*Side < 0.);
    }
    To = FVector(800, 200, 100); Velocity = FVector(0, 200, 0);
    TestFalse(TEXT("The wall does not reflect beyond its finite ends"),
        DuelCombatMechanics::ReflectMirrorSegment(FVector::ZeroVector, 100.f, 500.f, FVector(800, 0, 100), To, Velocity, Bounce));
    To = FVector(0, 200, 600);
    TestFalse(TEXT("A projectile above the glass does not bounce"),
        DuelCombatMechanics::ReflectMirrorSegment(FVector::ZeroVector, 100.f, 500.f, FVector(0, 0, 600), To, Velocity, Bounce));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelCombatMechanicsQA, "ManosabaDuel.CombatRevision",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDuelCombatMechanicsQA::RunTest(const FString&)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")))
    {
        AddError(TEXT("CombatRevision requires -DuelNoSave and an isolated user directory."));
        return false;
    }
    ADuelMode* Mode = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
        if (UWorld* World = Context.World())
            if ((Mode = World->GetAuthGameMode<ADuelMode>())) break;
    if (!TestNotNull(TEXT("Duel world"), Mode)) return false;
    ADuelFighter* Player = Mode->PlayerFighter;
    ADuelFighter* Boss = Mode->BossFighter;
    ADuelArena* Arena = Mode->Arena;
    if (!TestNotNull(TEXT("Player"), Player) || !TestNotNull(TEXT("Boss"), Boss) || !TestNotNull(TEXT("Arena"), Arena))
        return false;
    const bool SavedNorth = Mode->bNorthTowerRoute;
    const bool SavedOriginFlag = Arena->bNorthOrigin;
    const FVector SavedOrigin = Arena->NorthOrigin;
    const EDuelCharacter SavedBoss = Boss->Character;
    const int32 SavedEncounter = Boss->BossEncounter;
    const EDuelWeapon SavedRight = Player->Equipped;
    const EDuelWeapon SavedLeft = Player->Offhand;
    ON_SCOPE_EXIT
    {
        Arena->ClearCombatEffects();
        Mode->bNorthTowerRoute = SavedNorth;
        Arena->bNorthOrigin = SavedOriginFlag;
        Arena->NorthOrigin = SavedOrigin;
        Boss->Character = SavedBoss;
        Boss->BossEncounter = SavedEncounter;
        Player->SetWeapon(SavedRight);
        Player->SetWeapon(SavedLeft, EDuelHand::Left);
        Mode->ResetEncounter();
    };
    Mode->bNorthTowerRoute = false;
    Arena->bNorthOrigin = false;
    const FVector Origin = Arena->CourtOrigin();
    const auto Reset = [&]()
    {
        Arena->ClearCombatEffects();
        Mode->Result = EDuelOutcome::Fighting;
        Player->ResetFighter(true);
        Boss->ResetFighter(true);
        Player->SetActorLocationAndRotation(Origin + FVector(-600, 0, 62), FRotator::ZeroRotator);
        Boss->SetActorLocationAndRotation(Origin + FVector(600, 0, 103), FRotator(0, 180, 0));
        Player->ForwardAxis = Player->RightAxis = 0.f;
        Player->bLockOn = true;
        Player->ConsumeMovementInputVector();
    };
    const auto Walk = [&](const FVector& Input, int32 Frames)
    {
        Player->ForwardAxis = Input.X;
        Player->RightAxis = Input.Y;
        bool bInputPreserved = true;
        for (int32 Frame = 0; Frame < Frames; ++Frame)
        {
            Player->TickPlayer(.02f);
            bInputPreserved &= Player->ConsumeMovementInputVector().Equals(Input, .001f);
        }
        return bInputPreserved;
    };
    for (EDuelHand Hand : {EDuelHand::Left, EDuelHand::Right})
    {
        Reset();
        Player->SetWeapon(EDuelWeapon::Greatshield, Hand);
        Player->SetActorRotation(FRotator(0, 135, 0));
        Player->PoseFigure(.02f);
        const FRotator Before = Player->GetActorRotation();
        Player->PressHand(Hand);
        TestTrue(TEXT("Pressing a locked shield does not snap the actor"), Player->GetActorRotation().Equals(Before, .001f));
        Player->TickPlayer(.02f);
        TestTrue(TEXT("A raised locked shield turns smoothly toward the boss"),
            Player->GetActorRotation().Yaw > 0.f && Player->GetActorRotation().Yaw < Before.Yaw);
        Player->PoseFigure(.015f);
        const uint8 Index = static_cast<uint8>(Hand);
        TestTrue(TEXT("The first shield frame is partial"), Player->GuardPose[Index] > 0.f && Player->GuardPose[Index] < .15f);
        float Previous = Player->GuardPose[Index];
        for (int32 Frame = 0; Frame < 12; ++Frame)
        {
            Player->PoseFigure(.015f);
            TestTrue(TEXT("Raise progresses monotonically without overshoot"),
                Player->GuardPose[Index] >= Previous && Player->GuardPose[Index] <= 1.f);
            Previous = Player->GuardPose[Index];
        }
        USceneComponent* Grip = Hand == EDuelHand::Right ? Player->Weapon.Get() : Player->LeftWeapon.Get();
        const FVector Face = Player->GetActorTransform().InverseTransformPosition(
            Grip->GetComponentTransform().TransformPosition(FVector(6, 0, 9)));
        TestTrue(TEXT("The settled shield stays ahead of the torso"), Face.X > 35.f && Face.Z > 6.f && Face.Z < 76.f);
        const FRotator BeforeParry = Player->GetActorRotation();
        TestTrue(TEXT("A precise projectile parry preserves the player's chosen facing"),
            Player->TryShieldParry(Player->GetActorForwardVector(), true) && Player->GetActorRotation().Equals(BeforeParry, .001f));
        Player->ReleaseHand(Hand);
        Player->PoseFigure(.015f);
        TestTrue(TEXT("Release lowers smoothly instead of snapping"), Player->GuardPose[Index] > .8f && Player->GuardPose[Index] < 1.f);
        Player->PoseFigure(.20f);
        TestTrue(TEXT("The shield finishes lowering"), FMath::IsNearlyZero(Player->GuardPose[Index]));

        for (const FVector& Input : {FVector::ZeroVector, -FVector::ForwardVector, FVector::RightVector, -FVector::RightVector})
        {
            Reset();
            Player->SetWeapon(EDuelWeapon::Greatshield, Hand);
            Player->PressHand(Hand);
            bool bInputPreserved = true;
            for (int32 Frame = 1; Frame <= 30; ++Frame)
            {
                Boss->SetActorLocation(Player->GetActorLocation() + FRotator(0, Frame * 1.5f, 0).Vector() * 1000.f + FVector(0, 0, 41));
                bInputPreserved &= Walk(Input, 1);
            }
            TestTrue(TEXT("Locked idle, backward and lateral guards track a moving boss without redirecting movement"),
                Player->IsGuarding(Hand) && bInputPreserved && FVector::DotProduct(Player->GetActorForwardVector(),
                    (Boss->GetActorLocation() - Player->GetActorLocation()).GetSafeNormal2D()) > .99f);
        }
        Player->bLockOn = false;
        TestTrue(TEXT("Unlocking while guarding restores ordinary movement facing"),
            Walk(-FVector::ForwardVector, 25) && Player->IsGuarding(Hand) &&
            Player->GetActorForwardVector().Equals(-FVector::ForwardVector, .01f));
        Player->bLockOn = true;
        Walk(FVector::ZeroVector, 25);
        Player->ReleaseHand(Hand);
        TestTrue(TEXT("Releasing a locked shield restores ordinary movement facing"),
            Walk(-FVector::ForwardVector, 25) && !Player->IsGuarding() &&
            Player->GetActorForwardVector().Equals(-FVector::ForwardVector, .01f));

        Reset();
        Player->SetWeapon(EDuelWeapon::Greatshield, Hand);
        Player->Stamina = DuelCourt::MaxStamina;
        Player->PressHand(Hand);
        Walk(-FVector::ForwardVector, 1);
        Player->PressShift();
        Player->ReleaseShift();
        Boss->SetActorLocation(Player->GetActorLocation() + FVector(0, 1000, 41));
        Player->TickPlayer(.1f);
        TestTrue(TEXT("A roll from locked guard follows its input instead of the moving boss"),
            Player->Move == EDuelMove::Roll && !Player->IsGuarding() &&
            Player->ActionDirection.Equals(-FVector::ForwardVector, .01f) &&
            Player->GetActorForwardVector().Equals(-FVector::ForwardVector, .01f));
        Player->TickPlayer(.4f);
        Walk(FVector::ZeroVector, 25);
        TestTrue(TEXT("Holding the shield resumes target tracking after roll recovery"),
            Player->Move == EDuelMove::Idle && Player->IsGuarding(Hand) &&
            FVector::DotProduct(Player->GetActorForwardVector(),
                (Boss->GetActorLocation() - Player->GetActorLocation()).GetSafeNormal2D()) > .999f);
        Player->ReleaseHand(Hand);
    }

    Reset(); Boss->Character = EDuelCharacter::Ema; Boss->BossPhase = 2;
    if (!TestTrue(TEXT("Ema starts the visible pursuit dash"), Boss->StartMove(EDuelMove::Pursuit))) return false;
    const auto Dash = DuelCourt::Timing(EDuelMove::Pursuit, true);
    TestTrue(TEXT("Ema completes the actual dash during its visible wing trail"),
        FMath::IsNearlyEqual(Boss->TravelProgress(Dash.Windup + DuelBossDefense::EmaDashTime, Dash), 1.f));
    for (int32 Sample = 0; Sample <= 20; ++Sample)
    {
        Boss->MoveAge = Dash.Windup + DuelBossDefense::EmaDashTime * Sample / 20.f;
        Boss->PoseFigure(.015f);
        TestTrue(TEXT("Ema's dash pose remains above the floor"),
            Boss->Figure->GetRelativeLocation().Z > -Boss->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() - 20.f);
    }

    Reset(); Boss->Character = EDuelCharacter::Yuki; Boss->BossPhase = 3;
    if (!TestTrue(TEXT("Four-snowflake record starts"), Boss->StartMove(EDuelMove::BrokenRecord))) return false;
    TestEqual(TEXT("Final-phase record has four independently warned snowflakes"), Boss->MapWaves.Num(), 4);
    for (int32 I = 0; I < Boss->MapWaves.Num(); ++I)
    {
        const auto& Wave = Boss->MapWaves[I];
        TestTrue(TEXT("Each snowflake has its own readable warning"), Wave.ImpactTime - Wave.WarnTime >= .42f);
        if (I > 0) TestTrue(TEXT("Snowflakes explode in their drawn order with a complete defense gap"),
            Wave.ImpactTime - Boss->MapWaves[I - 1].ImpactTime >= DuelBossDefense::HitGap - .0001f);
    }
    const float LastWarning = Boss->MapWaves.Last().WarnTime;
    while (Boss->MoveAge < LastWarning + .01f)
        Boss->TickMove(FMath::Min(.02f, LastWarning + .01f - Boss->MoveAge));
    for (const auto& Wave : Boss->MapWaves)
        TestTrue(TEXT("Every snowflake acquires a visible mark before the first explosion"), Wave.Marks.Num() == 1 && !Wave.bHitDelivered);
    Player->SetActorLocation(Origin + FVector(1200, 900, 62));
    for (int32 I = 0; I < Boss->MapWaves.Num(); ++I)
    {
        Boss->TickMove(Boss->MapWaves[I].ImpactTime - Boss->MoveAge + .001f);
        for (int32 J = 0; J < Boss->MapWaves.Num(); ++J)
            TestEqual(TEXT("Only the scheduled snowflakes have detonated"), Boss->MapWaves[J].bHitDelivered, J <= I);
    }

    Reset(); Boss->Character = EDuelCharacter::Yuki; Boss->BossPhase = 3;
    if (!TestTrue(TEXT("Final Sentence starts"), Boss->StartMove(EDuelMove::FinalSentence))) return false;
    if (!TestEqual(TEXT("Final Sentence owns two complementary verdicts"), Boss->MapWaves.Num(), 2)) return false;
    TestTrue(TEXT("The court has a real checkerboard of distinct cells"),
        Boss->MapWaves[0].Marks.Num() >= 8 && Boss->MapWaves[1].Marks.Num() >= 8 &&
        !Boss->MapWaves[0].Marks.Contains(Boss->MapWaves[1].Marks[0]));
    TestTrue(TEXT("The second verdict receives its own warning after the first lands"),
        Boss->MapWaves[1].WarnTime > Boss->MapWaves[0].ImpactTime &&
        Boss->MapWaves[1].ImpactTime - Boss->MapWaves[1].WarnTime >= DuelBossDefense::SentenceWarning - .0001f);
    TestTrue(TEXT("Cells fit within one roll's travel"),
        Boss->MapWaves[0].HalfLength < DuelCourt::RollDistance && Boss->MapWaves[0].Reach < DuelCourt::RollDistance);
    const FVector FirstTile = Boss->MapWaves[0].Marks[0];
    Player->SetActorLocation(FirstTile + FVector(0, 0, 54));
    Boss->TickMove(Boss->MapWaves[0].ImpactTime - .01f);
    TestTrue(TEXT("A bright verdict cell stays harmless throughout its warning"),
        FMath::IsNearlyEqual(Player->Health, DuelCourt::PlayerMaxHealth));
    Boss->TickMove(.02f);
    TestTrue(TEXT("The first verdict deals real damage once"),
        Player->Health < DuelCourt::PlayerMaxHealth && Boss->MapWaves[0].bHitDelivered && !Boss->MapWaves[1].bHitDelivered);
    const float HealthAfterFirst = Player->Health;
    Boss->TickMove(Boss->MapWaves[1].ImpactTime - Boss->MoveAge + .001f);
    TestTrue(TEXT("The same cell becomes safe for the complementary verdict"),
        FMath::IsNearlyEqual(Player->Health, HealthAfterFirst) && Boss->MapWaves[1].bHitDelivered);

    Reset(); Mode->bNorthTowerRoute = true; Arena->SetNorthOrigin(FVector(0, 0, 1700));
    Boss->Character = EDuelCharacter::Hiro; Boss->BossEncounter = 1; Boss->BossPhase = 1;
    Arena->TickMirrorCourt(Boss, .1f);
    TestTrue(TEXT("The first phase leaves the mirror court open"), Arena->MirrorWalls.IsEmpty());
    Boss->BossPhase = 2; Arena->TickMirrorCourt(Boss, .1f);
    TestEqual(TEXT("Phase two surrounds both fighters with four mirrors"), Arena->MirrorWalls.Num(), 4);
    const float OriginalWidth = Arena->MirrorHalfWidth;
    TestTrue(TEXT("Mirrors rise from the floor before they can reflect a hit"),
        Arena->MirrorWalls[0]->GetChildComponent(0)->GetRelativeLocation().Z < 20. && Arena->MirrorPressureAge < DuelCombatMechanics::MirrorFormTime);
    Arena->TickMirrorCourt(Boss, 5.f);
    TestTrue(TEXT("A fully formed mirror stays on the same footprint"),
        FMath::IsNearlyEqual(Arena->MirrorHalfWidth, OriginalWidth) && Arena->MirrorWalls[0]->GetChildComponent(0)->GetRelativeLocation().Z > 275.);
    const FVector Center = Arena->MirrorCenter;
    Player->SetActorLocation(Center + FVector(0, OriginalWidth + 40.f, 62));
    Player->ClampToCourt();
    TestTrue(TEXT("A moving mirror wall cannot leave the player embedded outside its inner face"),
        Player->GetActorLocation().Y <= Center.Y + Arena->MirrorHalfWidth - Player->GetCapsuleComponent()->GetScaledCapsuleRadius() + .01f);
    Player->SetActorLocation(Center + FVector(-700, 0, 62));
    Boss->SetActorLocation(Center + FVector(700, 0, 103));
    Arena->LaunchThread(Boss, Center + FVector(0, Arena->MirrorHalfWidth - 80.f, 100), FVector(0, 1, 0),
        5.f, 0.f, FLinearColor::Blue, 400.f, 20.f, 2.f);
    Arena->TickOrbs(.3f);
    TestTrue(TEXT("A player's real spell reflects off the north mirror"),
        Arena->Orbs.Num() == 1 && Arena->Orbs[0].Velocity.Y < 0.f && Arena->Orbs[0].MirrorBounces == 1);
    Arena->LaunchThread(Player, Center + FVector(100, -Arena->MirrorHalfWidth + 80.f, 100), FVector(0, -1, 0),
        5.f, 0.f, FLinearColor::Red, 400.f, 20.f, 2.f);
    Arena->TickOrbs(.3f);
    TestTrue(TEXT("An enemy's real spell reflects off the south mirror"),
        Arena->Orbs.Num() == 2 && Arena->Orbs.Last().Velocity.Y > 0.f && Arena->Orbs.Last().MirrorBounces == 1);
    Boss->BossPhase = 3; Arena->TickMirrorCourt(Boss, 3.f);
    TestTrue(TEXT("Phase three does not shove the fighters by moving room walls"), FMath::IsNearlyEqual(Arena->MirrorHalfWidth, OriginalWidth));
    Arena->ClearCombatEffects();
    TestTrue(TEXT("Victory and retry remove mirror collision and projectiles"),
        Arena->MirrorWalls.IsEmpty() && Arena->MirrorPhase == 0 && Arena->Orbs.IsEmpty());
    Boss->SetActorLocation(Center+FVector(450,0,103));
    Player->SetActorLocation(Center+FVector(-450,0,62));
    Arena->TickMirrorCourt(Boss,2.f);
    Arena->TickMirrorCourt(Boss,2.1f);
    TestTrue(TEXT("The boss marks a bank shot before releasing it"), Arena->bMirrorShotArmed && Arena->Orbs.IsEmpty());
    Arena->TickMirrorCourt(Boss,.9f);
    TestTrue(TEXT("Both travel legs remain harmless through the warning"),Arena->Orbs.IsEmpty());
    Arena->TickMirrorCourt(Boss,.21f);
    const float BeforeVolley=Player->Health;
    bool bRicochet=false;
    for (int32 Frame=0; Frame<200 && !Arena->Orbs.IsEmpty(); ++Frame)
    {
        Arena->TickOrbs(1.f/90.f);
        if (!Arena->Orbs.IsEmpty()) bRicochet |= Arena->Orbs[0].MirrorBounces>0;
    }
    TestTrue(TEXT("The authored bank shot reaches a stationary target after bouncing on the glass"),
        bRicochet && Player->Health < BeforeVolley);
    return true;
}
#endif
