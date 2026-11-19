#include "DuelGame.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "DuelArena.h"
#include "DuelNorthLayout.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelJudgment347QA, "ManosabaDuel.NorthTower.Judgment347",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelJudgment347QA::RunTest(const FString&)
{
    if (!TestTrue(TEXT("Judgment QA is isolated with saves disabled"),
        FParse::Param(FCommandLine::Get(), TEXT("DuelNorthTower")) && FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")))) return false;
    ADuelMode* Mode = nullptr;
    for (const auto& Context : GEngine->GetWorldContexts())
        if (Context.World() && (Mode = Context.World()->GetAuthGameMode<ADuelMode>())) break;
    if (!TestTrue(TEXT("The real North Tower arena and fighters exist"), Mode && Mode->Arena && Mode->PlayerFighter && Mode->BossFighter)) return false;
    auto* Arena = Mode->Arena.Get();
    auto* Player = Mode->PlayerFighter.Get();
    auto* Boss = Mode->BossFighter.Get();
    auto* World = Mode->GetWorld();
    const auto SavedBoss = Boss->Character;
    const int32 SavedEncounter = Boss->BossEncounter;
    const FVector SavedOrigin = Arena->NorthOrigin;
    const bool SavedNorth = Arena->bNorthOrigin;
    const auto Right = Player->Equipped, Left = Player->Offhand;
    ON_SCOPE_EXIT
    {
        UGameplayStatics::SetGamePaused(Mode, false);
        Arena->ClearCombatEffects();
        Arena->NorthOrigin = SavedOrigin; Arena->bNorthOrigin = SavedNorth;
        Boss->Character = SavedBoss; Boss->BossEncounter = SavedEncounter; Boss->BuildFigure();
        Player->SetWeapon(Right); Player->SetWeapon(Left, EDuelHand::Left);
        Mode->ReturnToCheckpoint();
    };
    const auto Reset = [&](bool bOpen)
    {
        Arena->ClearCombatEffects();
        Arena->SetNorthOrigin(DuelNorthLayout::CourtOrigin(2));
        Mode->Result = EDuelOutcome::Fighting;
        Player->ResetFighter(true); Player->RestAtCheckpoint();
        Boss->Character = EDuelCharacter::Yuki; Boss->BossEncounter = 2; Boss->ResetFighter(true); Boss->BossPhase = 2;
        Boss->BossRandom.Initialize(3471977);
        const FVector Origin = Arena->CourtOrigin();
        Player->SetActorLocationAndRotation(Origin + FVector(-500, -160, 62), FRotator::ZeroRotator);
        Boss->SetActorLocationAndRotation(Origin + FVector(650, 280, 103), FRotator(0, 180, 0));
        Player->ConsumeMovementInputVector(); Boss->ConsumeMovementInputVector();
        Arena->TickCourtInteractions(Boss, 5.01f);
        if (!TestTrue(TEXT("A safe random layout enters the complete warning with Yuki protected"),
            Arena->CourtStage == ADuelArena::ECourtStage::Warning && Boss->JudgmentImmune())) return false;
        if (bOpen) Arena->TickCourtInteractions(Boss, 1.21f);
        return true;
    };
    if (!Reset(false)) return false;
    Boss->BuildFigure();
    const auto OriginalPoints = Arena->CourtPoints;
    int32 Variations = 0;
    for (const FVector2D Position : {FVector2D(-1750, -900), FVector2D(1750, 900), FVector2D(0, 0)})
    {
        Player->SetActorLocation(Arena->CourtOrigin() + FVector(Position, 62));
        for (uint32 Seed = 1; Seed <= 24; ++Seed)
        {
            Arena->JudgmentSeed = Seed; Arena->CourtCycle = 1;
            if (!TestTrue(TEXT("Every sampled court region receives three actually reachable seals"), Arena->PlaceJudgmentSeals())) continue;
            const auto First = Arena->CourtPoints;
            TestTrue(TEXT("Identical seed and world reproduce the same placement"), Arena->PlaceJudgmentSeals());
            for (int32 I = 0; I < 3; ++I)
            {
                TestTrue(TEXT("Repeated seed preserves every point"), First[I].Equals(Arena->CourtPoints[I], .01));
                TestTrue(TEXT("Ground art and frames remain inside the inset combat bounds"),
                    Arena->CourtBounds(250.).IsInside(FVector2D(First[I] - Arena->CourtOrigin())));
                for (int32 J = I + 1; J < 3; ++J)
                    TestTrue(TEXT("The three 150 cm ground cues cannot overlap"), FVector::Dist2D(First[I], First[J]) >= 380.);
            }
            Variations += !First[0].Equals(OriginalPoints[0], 1.);
        }
    }
    TestTrue(TEXT("Randomized layouts vary rather than repeating one hard-coded triangle"), Variations >= 60);
    Player->SetActorLocation(Arena->CourtOrigin() + FVector(-500, -160, 62));
    Arena->JudgmentSeed = 3471; Arena->CourtCycle = 1;
    if (!Arena->PlaceJudgmentSeals()) return false;
    const FVector BlockedPoint = Arena->CourtPoints[0];
    auto* Blocker = World->SpawnActor<AActor>();
    auto* Box = NewObject<UBoxComponent>(Blocker);
    Blocker->SetRootComponent(Box); Blocker->AddInstanceComponent(Box);
    Box->SetBoxExtent(FVector(100, 100, 130)); Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent();
    Blocker->SetActorLocation(BlockedPoint);
    const bool bAlternative = Arena->PlaceJudgmentSeals();
    TestTrue(TEXT("An actual blocking solid invalidates the old floor cell and causes a fresh reachable placement"),
        bAlternative && !Arena->CourtPoints[0].Equals(BlockedPoint, 5.));
    for (const FVector& Point : Arena->CourtPoints)
        TestTrue(TEXT("No seal hides inside the physical blocker"), FMath::Abs(Point.X - BlockedPoint.X) > 100. || FMath::Abs(Point.Y - BlockedPoint.Y) > 100.);
    Box->DestroyComponent(); Blocker->Destroy();

    if (!Reset(false)) return false;
    const TCHAR* Tags[] = {TEXT("JudgmentEye347"), TEXT("JudgmentScales347"), TEXT("JudgmentCrown347")};
    int32 ActiveCount = 0;
    for (int32 I = 0; I < 3; ++I)
    {
        auto* Root = Arena->CourtObjects[I].Get();
        TestTrue(TEXT("Each judgment object has a distinct 3D silhouette identity"), Root && Root->ComponentHasTag(Tags[I]));
        if (!Root) continue;
        TArray<USceneComponent*> Children; Root->GetChildrenComponents(true, Children);
        int32 Meshes = 0, Reliefs = 0, GroundCues = 0;
        for (const auto* Child : Children)
        {
            Meshes += Child->IsA<UStaticMeshComponent>();
            Reliefs += Child->ComponentHasTag(TEXT("JudgmentEnamel347"));
            GroundCues += Child->ComponentHasTag(TEXT("JudgmentGroundCue347"));
        }
        ActiveCount += Root->ComponentHasTag(TEXT("JudgmentActiveTarget347"));
        TestTrue(TEXT("All three frames carry the actual relief plus readable ground geometry"), Meshes > 30 && Reliefs == 1 && GroundCues == 1);
    }
    TestEqual(TEXT("Exactly one correct seal has the independent double-gold-ring cue"), ActiveCount, 1);
    TestTrue(TEXT("The boss has a separate visible judgment barrier"), Arena->JudgmentBarrier.IsValid());
    for (const auto Stage : {ADuelArena::ECourtStage::Warning, ADuelArena::ECourtStage::Open,
        ADuelArena::ECourtStage::Retaliation, ADuelArena::ECourtStage::Feedback})
    {
        Arena->CourtStage = Stage;
        const float HP = Boss->Health;
        for (const bool Ranged : {false, true})
            for (const bool Parryable : {false, true})
            {
                const FVector Contact = Boss->GetActorLocation();
                TestFalse(TEXT("Every direct/melee/projectile/ground damage flag combination respects the whole judgment ward"),
                    Boss->ReceiveDuelDamage(120, Player->GetActorLocation(), Parryable, &Contact, Ranged, true, true));
            }
        Boss->DamagePosture(DuelCourt::MaxPosture);
        TestTrue(TEXT("HP, posture and stagger remain unchanged in every protected stage"), Boss->Health == HP && Boss->Posture == 0 && !Boss->IsStaggered());
    }
    Arena->CourtStage = ADuelArena::ECourtStage::Warning;
    const float ProtectedHP = Boss->Health;
    Player->SetWeapon(EDuelWeapon::Buckler, EDuelHand::Left); Player->PressHand(EDuelHand::Left);
    TestTrue(TEXT("Yuki's actual close-chain action starts behind her ward"), Boss->StartMove(EDuelMove::FrostStaffChain));
    const auto GuardedMove = Boss->Move;
    Player->ReceiveDuelDamage(30.f, Boss->GetActorLocation(), true);
    TestTrue(TEXT("A real precise shield guard can defend the player but cannot force the protected boss into GuardReel"),
        Player->Move == EDuelMove::Parry && Boss->Move == GuardedMove && Boss->Posture == 0.f && Boss->Health == ProtectedHP);
    Boss->Move = EDuelMove::Idle;
    for (int32 GearIndex = 0; GearIndex <= int32(EDuelWeapon::RitualSword); ++GearIndex)
    {
        const auto Gear = EDuelWeapon(GearIndex);
        if (DuelCourt::IsShield(Gear) || Gear == EDuelWeapon::Empty) continue;
        for (const auto Action : {EDuelMove::Cut, EDuelMove::Backcut, EDuelMove::Heavy})
        {
            Player->ResetFighter(true); Player->RestAtCheckpoint(); Player->SetWeapon(Gear);
            Player->SetActorLocationAndRotation(Boss->GetActorLocation() + FVector(-180, 0, -41), FRotator::ZeroRotator);
            if (!Player->StartMove(Action)) continue;
            for (int32 Frame = 0; Frame < 200 && Player->Move == Action; ++Frame) Player->TickMove(.01f);
            TestTrue(TEXT("Ordinary weapon light/follow/heavy timelines cannot bypass the ward"), Boss->Health == ProtectedHP && Boss->Posture == 0.f);
        }
    }
    int32 ArtsChecked = 0;
    for (int32 Index = 1; Index <= int32(EDuelArt::MoonCrescent); ++Index)
    {
        Player->ResetFighter(true); Player->RestAtCheckpoint();
        Player->SetActorLocationAndRotation(Boss->GetActorLocation() + FVector(-190, 0, -41), FRotator::ZeroRotator);
        const auto Art = EDuelArt(Index);
        const auto Gear = MakeArrayView(DuelCourt::StaffSpells).Contains(Art) ? EDuelWeapon::Staff : EDuelWeapon::Rapier;
        Player->SetWeapon(Gear);
        Player->Move = EDuelMove::Crimson; Player->MoveWeapon = Gear; Player->MoveArt = Art;
        Player->ActionDirection = Player->CastAxis = FVector::ForwardVector;
        Player->bArtHeld = false; Player->bImpactPlayed = Player->bHitDelivered = false;
        const auto Timing = DuelCourt::Timing(EDuelMove::Crimson, false, Gear, Art);
        Player->PrepareArt(Timing);
        for (int32 Frame = 0; Frame < 500 && Player->Move == EDuelMove::Crimson; ++Frame)
        {
            Player->TickMove(.01f); Arena->TickOrbs(.01f);
        }
        Arena->TickOrbs(5.f); Arena->TickOrbs(.01f); Arena->TickEffects(5.f); Arena->TickParticles(5.f);
        TestTrue(FString::Printf(TEXT("Production art/spell %d, including its delayed echoes, cannot bypass HP or posture immunity"), Index),
            Boss->Health == ProtectedHP && Boss->Posture == 0);
        ++ArtsChecked;
    }
    AddInfo(FString::Printf(TEXT("Whole-ward coverage exercised %d real art/spell action timelines."), ArtsChecked));

    const auto MeleeAt = [&](const FVector& Point)
    {
        Player->Move = EDuelMove::Idle; Player->SetWeapon(EDuelWeapon::Rapier); Player->bLockOn = true;
        Player->SetActorLocationAndRotation(FVector(Point.X - 205, Point.Y, Arena->CourtOrigin().Z + 62), FRotator::ZeroRotator);
        if (!TestTrue(TEXT("The ordinary weapon attack starts"), Player->StartMove(EDuelMove::Cut))) return;
        const int32 Progress = Arena->CourtProgress;
        for (int32 Frame = 0; Frame < 100 && Player->Move == EDuelMove::Cut; ++Frame)
        {
            Player->TickMove(.01f);
            if (Arena->CourtProgress != Progress || Arena->CourtStage == ADuelArena::ECourtStage::Retaliation) break;
        }
    };
    if (!Reset(true)) return false;
    Boss->SetActorLocation(Arena->CourtOrigin() + FVector(1600, 900, 103));
    Player->bLockOn = false;
    const int32 Wrong = (Arena->CourtOrder[0] + 1) % 3;
    const FVector WrongPoint = Arena->CourtPoints[Wrong];
    Arena->LaunchThread(Boss, WrongPoint - FVector(260, 0, 0), FVector::ForwardVector, 30, 10, FLinearColor::Blue, 1300, 20, .6f);
    for (int32 Frame = 0; Frame < 60 && Arena->CourtStage == ADuelArena::ECourtStage::Open; ++Frame) Arena->TickOrbs(.01f);
    TestTrue(TEXT("A real wrong flying spell starts the exact independent 1/5-second sentence"),
        Arena->CourtStage == ADuelArena::ECourtStage::Retaliation && Player->JudgmentMoveLockSeconds() > .99f && Player->JudgmentWeaponLockSeconds() > 4.99f && Boss->JudgmentImmune());
    const int32 BeforeLate = Arena->CourtProgress;
    const FVector Correct = Arena->CourtPoints[Arena->CourtOrder[0]];
    TestFalse(TEXT("A preexisting or delayed projectile cannot sneak a seal hit through the weapon sentence"),
        Arena->HitCourtSeal(*Player, Correct - FVector(150, 0, 0), Correct, 25, true));
    TestEqual(TEXT("Blocked late hits cannot earn progress"), Arena->CourtProgress, BeforeLate);
    const float FailureHP = Player->Health;
    Arena->TickCourtInteractions(Boss, 1.01f);
    TestTrue(TEXT("Failure keeps the boss protected and does not add an unavoidable red-line HP hit"),
        Boss->JudgmentImmune() && Player->Health == FailureHP && Arena->CourtStage == ADuelArena::ECourtStage::Feedback);

    for (const auto Current : {EDuelMove::Cut, EDuelMove::Heavy, EDuelMove::Crimson, EDuelMove::Equip, EDuelMove::Roll})
    {
        Player->ResetFighter(true); Player->RestAtCheckpoint(); Player->SetWeapon(EDuelWeapon::Rapier);
        TestTrue(TEXT("The interrupted action begins before failure"), Player->StartMove(Current));
        Player->QueuedMove = EDuelMove::Heavy;
        Player->ApplyJudgmentPenalty();
        TestTrue(TEXT("Failure atomically cancels the active action, hand holds and already buffered weapon action"),
            Player->Move == EDuelMove::Idle && Player->QueuedMove == EDuelMove::Idle && !Player->bArtHeld &&
            Player->HandPresses[0].Since < 0 && Player->HandPresses[1].Since < 0);
    }
    const float Stamina = Player->Stamina, Focus = Player->Focus;
    for (int32 GearIndex = 0; GearIndex <= int32(EDuelWeapon::RitualSword); ++GearIndex)
        for (const auto Hand : {EDuelHand::Left, EDuelHand::Right})
        {
            Player->SetWeapon(EDuelWeapon(GearIndex), Hand);
            Player->PressHand(Hand); Player->ReleaseHand(Hand);
            for (const auto Action : {EDuelMove::Cut, EDuelMove::Backcut, EDuelMove::Heavy, EDuelMove::Crimson, EDuelMove::Parry, EDuelMove::Equip})
            {
                Player->RequestMove(Action, Hand);
                TestTrue(TEXT("Both hands' attacks, heavy attacks, shields, arts and equipment actions stay blocked without buffering"),
                    Player->Move == EDuelMove::Idle && Player->QueuedMove == EDuelMove::Idle && !Player->IsGuarding(Hand));
            }
        }
    for (const auto Spell : DuelCourt::StaffSpells)
        for (const auto Gear : {EDuelWeapon::Staff, EDuelWeapon::Empty})
        {
            Player->SetWeapon(Gear); Player->PreparedSpells[0] = Spell; Player->ActiveSpellSlot = 0;
            Player->Crimson();
            TestFalse(TEXT("The centralized spell action gate also covers an unarmed/no-staff route"), Player->StartMove(EDuelMove::Crimson));
            const int32 Shots = Arena->Orbs.Num();
            Player->Move = EDuelMove::Crimson; Player->MoveWeapon = Gear; Player->MoveArt = Spell;
            Player->TickMove(.2f);
            TestTrue(TEXT("An already active staff or unarmed spell is cancelled before it can emit its impact"),
                Player->Move == EDuelMove::Idle && Arena->Orbs.Num() == Shots && !Player->bArtHeld);
        }
    TestTrue(TEXT("Forbidden actions do not spend stamina or focus"), Player->Stamina == Stamina && Player->Focus == Focus);
    Player->ForwardAxis = 1.f; Player->bLockOn = false;
    const FVector BeforeMove = Player->GetActorLocation();
    Player->TickPlayer(.05f); Player->GetCharacterMovement()->TickComponent(.05f, LEVELTICK_All, nullptr);
    TestTrue(TEXT("During the first second actual movement input and locomotion remain suppressed"),
        FVector::Dist2D(Player->GetActorLocation(), BeforeMove) < .1 && Player->GetCharacterMovement()->MaxWalkSpeed == 0.f);
    const double Now = World->GetTimeSeconds();
    Player->JudgmentMoveUntil = Now - .01; Player->JudgmentWeaponUntil = Now + 3.8;
    Player->TickPlayer(.016f);
    TestTrue(TEXT("Movement independently recovers while all weapon actions are still sealed"),
        Player->ConsumeMovementInputVector().Size2D() > .8 && Player->GetCharacterMovement()->MaxWalkSpeed > 100.f &&
        Player->JudgmentMoveLockSeconds() == 0.f && Player->JudgmentWeaponLockSeconds() > 3.7f);
    TestTrue(TEXT("A new dodge is usable once only the one-second movement sentence expires"), Player->StartMove(EDuelMove::Roll));
    while (Player->Move == EDuelMove::Roll) Player->TickMove(.01f);
    Player->Health = 60.f; Player->Item = EDuelItem::Flask;
    Player->Move = EDuelMove::Hurt;
    Player->MoveAge = DuelCourt::Timing(EDuelMove::Hurt, false).Total() - .08f;
    Player->Flask(); Player->TickPlayer(.1f);
    TestTrue(TEXT("A buffered potion remains usable during the weapon seal"), Player->Move == EDuelMove::Flask);
    for (int32 Frame = 0; Frame < 150 && Player->Move == EDuelMove::Flask; ++Frame) Player->TickPlayer(.01f);
    TestTrue(TEXT("The ordinary potion really restores HP while weapon input is sealed"), Player->Health > 60.f);
    const float Yaw = Player->ViewYaw; Player->LookYaw(1.f);
    TestTrue(TEXT("Mouse look remains available during weapon sealing"), Player->ViewYaw != Yaw);
    Player->PauseDuel(); TestTrue(TEXT("The ordinary pause menu remains available"), UGameplayStatics::IsGamePaused(Mode)); Player->PauseDuel();
    Player->JudgmentWeaponUntil = World->GetTimeSeconds() - .01;
    Player->Move = EDuelMove::Idle; Player->TickPlayer(.02f);
    TestTrue(TEXT("Expiry never automatically resurrects a discarded weapon buffer"), Player->Move == EDuelMove::Idle && Player->QueuedMove == EDuelMove::Idle);
    Player->SetWeapon(EDuelWeapon::Rapier);
    TestTrue(TEXT("After the actual expiry gate a fresh weapon input starts normally"), Player->StartMove(EDuelMove::Cut));

    if (!Reset(true)) return false;
    const float BeforeCounterHP = Boss->Health;
    MeleeAt(Arena->CourtPoints[Arena->CourtOrder[0]]);
    if (!TestEqual(TEXT("A real blade intersection earns the first seal"), Arena->CourtProgress, 1)) return false;
    while (Player->Move != EDuelMove::Idle) Player->TickMove(.01f);
    for (int32 Progress = 1; Progress < 3; ++Progress)
    {
        Player->bLockOn = true;
        const FVector Point = Arena->CourtPoints[Arena->CourtOrder[Progress]];
        Arena->LaunchThread(Boss, Point - FVector(260, 0, 0), FVector(0, -1, 0), 30, 10, FLinearColor::Blue, 1300, 20, .6f);
        for (int32 Frame = 0; Frame < 60 && Arena->CourtProgress == Progress; ++Frame) Arena->TickOrbs(.01f);
        TestEqual(TEXT("A real cast intersects only the next gold target"), Arena->CourtProgress, Progress + 1);
    }
    TestTrue(TEXT("Only the three correct physical hits remove protection and grant a real stagger"),
        !Boss->JudgmentImmune() && Boss->IsStaggered() && Arena->CourtObjects.IsEmpty() && Boss->Health == BeforeCounterHP);
    TestTrue(TEXT("The normal damage path can punish Yuki after the ward is actually broken"),
        Boss->ReceiveDuelDamage(30.f, Player->GetActorLocation()) && Boss->Health < BeforeCounterHP);
    Player->ResetFighter(true); Player->RestAtCheckpoint(); Player->SetWeapon(EDuelWeapon::Rapier); Player->bLockOn = true;
    Player->SetActorLocationAndRotation(Boss->GetActorLocation() + FVector(-180, 0, -41), FRotator::ZeroRotator);
    Boss->PoseFigure(0.f);
    const float BeforeBladePunish = Boss->Health;
    TestTrue(TEXT("A fresh ordinary blade input starts in the earned punish window"), Player->StartMove(EDuelMove::Cut));
    for (int32 Frame = 0; Frame < 150 && Player->Move == EDuelMove::Cut && Boss->Health == BeforeBladePunish; ++Frame) Player->TickMove(.01f);
    TestTrue(TEXT("Actual blade collision really removes boss HP after all three seals are broken"), Boss->Health < BeforeBladePunish);
    if (!Reset(true)) return false;
    Arena->TickCourtInteractions(Boss, 12.6f);
    TestTrue(TEXT("Timeout applies the same actual sentence and keeps immunity latched"),
        Player->JudgmentMoveLockSeconds() > .99f && Player->JudgmentWeaponLockSeconds() > 4.99f && Boss->JudgmentImmune());
    Arena->TickCourtInteractions(Boss, 8.f);
    TestTrue(TEXT("A new verdict cannot begin before the real weapon sentence expires"), Arena->CourtStage != ADuelArena::ECourtStage::Warning);
    Player->JudgmentMoveUntil = Player->JudgmentWeaponUntil = World->GetTimeSeconds() - .01;
    Arena->TickCourtInteractions(Boss, .01f);
    TestTrue(TEXT("After punishment the next attempt receives a complete fresh warning and active window"),
        Arena->CourtStage == ADuelArena::ECourtStage::Warning && Arena->CourtActiveDeadline - Arena->CourtDeadline >= 12.49f);
    Player->ApplyJudgmentPenalty(); Player->EndCombat();
    TestTrue(TEXT("Ending combat clears both sentence clocks"), Player->JudgmentMoveLockSeconds() == 0.f && Player->JudgmentWeaponLockSeconds() == 0.f);
    Player->ResetFighter(true); Player->ApplyJudgmentPenalty(); Player->ResetFighter(true);
    TestTrue(TEXT("A fighter retry clears both sentence clocks"), Player->JudgmentMoveLockSeconds() == 0.f && Player->JudgmentWeaponLockSeconds() == 0.f);
    Player->ApplyJudgmentPenalty(); Mode->ReturnToCheckpoint();
    TestTrue(TEXT("Checkpoint/death retry clears the sentence and the boss ward"),
        Player->JudgmentMoveLockSeconds() == 0.f && Player->JudgmentWeaponLockSeconds() == 0.f && !Boss->JudgmentImmune());
    return !HasAnyErrors();
}
#endif
