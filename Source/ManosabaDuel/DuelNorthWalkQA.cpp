#include "DuelNorthTower.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DuelArena.h"
#include "DuelGame.h"
#include "DuelLiveCombatQA.h"
#include "DuelNorthExtractionVisual.h"
#include "DuelNorthInteractables.h"
#include "DuelNorthNarrative.h"
#include "DuelNorthInvestigationQA.h"
#include "CollisionShape.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/SecureHash.h"

namespace
{
    // Shared by Walk, Journey and StairStress. Input axes and the production
    // CharacterMovement component remain the only walking driver.
    void AimNorthWalk(const FVector& Offset, float& Forward, float& Right, float& ViewYaw)
    {
        const FVector Heading = Offset.GetSafeNormal2D();
        const float InputScale = Offset.IsNearlyZero() ? 0.f : FMath::Clamp(float(Offset.Size2D() / 85.), .18f, 1.f);
        ViewYaw = 0; Forward = Heading.X * InputScale; Right = Heading.Y * InputScale;
    }

    void DriveNorthWalk(ADuelFighter& Player, const FVector& Offset, float Step,
        float& Forward, float& Right, float& ViewYaw)
    {
        AimNorthWalk(Offset, Forward, Right, ViewYaw);
        Player.Tick(Step);
        Player.GetCharacterMovement()->TickComponent(Step, LEVELTICK_All, nullptr);
    }

    FString CheckNorthWalkStep(ADuelFighter& Player, const FVector& Before, const FVector& After,
        double ExpectedFeetZ, const FCollisionQueryParams& Query, int32& FallingFrames,
        int32& MaxFallingFrames, int32& PenetratingFrames)
    {
        auto& Motion = *Player.GetCharacterMovement();
        const float Radius = Player.GetCapsuleComponent()->GetScaledCapsuleRadius();
        const float Half = Player.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        const FVector Feet = After - FVector(0, 0, Half);
        const FCollisionShape Inner = FCollisionShape::MakeCapsule(FMath::Max(1.f, Radius - 1.f), FMath::Max(Radius, Half - 2.f));
        if (After.ContainsNaN() || FVector::Dist(Before, After) > 100. || Feet.Z < ExpectedFeetZ - 65. || Feet.Z > ExpectedFeetZ + Motion.MaxStepHeight + 35.)
            return FString::Printf(TEXT("discontinuity: before=%s after=%s expected feet Z=%.2f"), *Before.ToString(), *After.ToString(), ExpectedFeetZ);
        FallingFrames = Motion.IsFalling() ? FallingFrames + 1 : 0;
        MaxFallingFrames = FMath::Max(MaxFallingFrames, FallingFrames);
        if (FallingFrames > 30) return FString::Printf(TEXT("lost walkable ground for >0.5 simulated seconds at %s"), *Feet.ToString());
        TArray<FOverlapResult> Overlaps;
        const bool Penetrating = Player.GetWorld()->OverlapMultiByChannel(Overlaps, After, FQuat::Identity, ECC_Pawn, Inner, Query);
        PenetratingFrames = Penetrating ? PenetratingFrames + 1 : 0;
        if (PenetratingFrames > 2)
        {
            const auto* Blocking = Overlaps.FindByPredicate([](const FOverlapResult& O) { return O.bBlockingHit; });
            return FString::Printf(TEXT("sustained capsule penetration at %s in %s/%s"), *After.ToString(),
                Blocking ? *GetNameSafe(Blocking->GetActor()) : TEXT("unknown"), Blocking ? *GetNameSafe(Blocking->GetComponent()) : TEXT("unknown"));
        }
        FHitResult Wall;
        const FVector TorsoOffset(0, 0, Half * .35f);
        if (Player.GetWorld()->SweepSingleByChannel(Wall, Before + TorsoOffset, After + TorsoOffset, FQuat::Identity,
            ECC_Pawn, FCollisionShape::MakeSphere(FMath::Max(1.f, Radius - 1.f)), Query) && !Wall.bStartPenetrating && Wall.Time < .98f)
            return FString::Printf(TEXT("crossed blocking torso geometry %s/%s at %s"),
                *GetNameSafe(Wall.GetActor()), *GetNameSafe(Wall.GetComponent()), *Wall.ImpactPoint.ToString());
        return {};
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthWalkTest, "ManosabaDuel.NorthTower.Walk",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthWalkTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace DuelNorthLayout;
    const TCHAR* Command = FCommandLine::Get();
    if (!TestTrue(TEXT("Walking QA requires an isolated North Tower with disk saves disabled"),
        FParse::Param(Command, TEXT("DuelNorthTower")) && FParse::Param(Command, TEXT("DuelNoSave")) &&
        !FParse::Param(Command, TEXT("DuelNorthSaveTest")))) return false;

    ADuelMode* Mode = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
        if (Context.World() && (Mode = Context.World()->GetAuthGameMode<ADuelMode>())) break;
    if (!TestTrue(TEXT("Actual North Tower, player and arena are running"), Mode && Mode->NorthTowerActor &&
        Mode->PlayerFighter && Mode->BossFighter && Mode->Arena)) return false;
    const FRoute* Route = FindRoute(TEXT("MainWalk"));
    if (!TestNotNull(TEXT("Use the authoritative MainWalk, not a test-only shortcut"), Route)) return false;
    if (!TestTrue(TEXT("MainWalk contains the full authored route"), Route->Points.Num() >= 140)) return false;
    if (!TestEqual(TEXT("All authored switchback flights are present"), Stairs().Num(), 21)) return false;

    ADuelNorthTower& Tower = *Mode->NorthTowerActor;
    ADuelFighter& Player = *Mode->PlayerFighter;
    ADuelFighter& Boss = *Mode->BossFighter;
    UWorld* World = Mode->GetWorld();
    UCharacterMovementComponent& Motion = *Player.GetCharacterMovement();
    const UCapsuleComponent& Capsule = *Player.GetCapsuleComponent();
    if (!TestTrue(TEXT("Start from an idle, living player outside combat"), Player.IsAlive() &&
        Player.Move == EDuelMove::Idle && (Mode->Result == EDuelOutcome::Ready || Mode->Result == EDuelOutcome::Title))) return false;

    constexpr float Step = 1.f / 60.f;
    constexpr float ArrivalRadius = 12.f;
    constexpr float ArrivalHeight = 28.f; // One authored riser plus the CharacterMovement floor gap.
    const float Radius = Capsule.GetScaledCapsuleRadius();
    const float HalfHeight = Capsule.GetScaledCapsuleHalfHeight();
    const FDuelNorthProgress SavedState = Tower.State;
    const auto SavedRooms = Tower.Explored;
    const auto SavedGates = Tower.OpenGates;
    const auto SavedLocation = Tower.Location;
    const auto SavedOutcome = Mode->Result;
    const auto SavedTransform = Player.GetActorTransform();
    const auto SavedArenaOrigin = Mode->Arena->CourtOrigin();
    const auto SavedVelocity = Motion.Velocity;
    const EMovementMode SavedMovementMode = Motion.MovementMode;
    const uint8 SavedCustomMode = Motion.CustomMovementMode;
    const float SavedWalkSpeed = Motion.MaxWalkSpeed;
    const float SavedForward = Player.ForwardAxis, SavedRight = Player.RightAxis;
    const float SavedYaw = Player.ViewYaw, SavedPitch = Player.ViewPitch;
    const float SavedStamina = Player.Stamina, SavedRecovery = Player.RecoveryDelay;
    const auto SavedShift = Player.ShiftPressedAt;
    const bool SavedLock = Player.bLockOn, SavedCombatOver = Player.bCombatOver;
    const bool SavedBossCollision = Boss.GetActorEnableCollision();
    const bool SavedMap = Tower.bMap, SavedGateOpen = Mode->bGateOpen;
    const int32 SavedFight = Tower.ActiveFight, SavedFigureView = Mode->FigureView;
    const FName SavedNearby = Tower.Nearby;
    const FString SavedNotice = Tower.Notice;
    const double SavedNoticeUntil = Tower.NoticeUntil;
    const bool SavedPaused = UGameplayStatics::IsGamePaused(World);

    const FString OutputDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("NorthTower200/WalkQA") /
        FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")));
    if (!TestTrue(TEXT("A separate walking evidence directory is writable"), IFileManager::Get().MakeDirectory(*OutputDir, true))) return false;
    TArray<FString> Arrivals{TEXT("direction,route_index,start_frame,arrival_frame,simulated_seconds,target_x,target_y,target_z,actual_x,actual_y,feet_z,xy_error,z_error,room,stair")};
    TArray<FString> Samples{TEXT("frame,simulated_seconds,direction,target_index,x,y,feet_z,speed,falling,room,stair")};
    TArray<FString> Authored{TEXT("route_index,x,y,z")};
    for (int32 I = 0; I < Route->Points.Num(); ++I)
    {
        const FVector& P = Route->Points[I];
        Authored.Add(FString::Printf(TEXT("%d,%.3f,%.3f,%.3f"), I, P.X, P.Y, P.Z));
    }
    int64 LogicalFrames = 0;
    int32 Arrived = 0, FallingFrames = 0, MaxFallingFrames = 0, PenetratingFrames = 0;
    double WalkedCm = 0, AuthoredCm = 0;
    for (int32 I = 1; I < Route->Points.Num(); ++I) AuthoredCm += FVector::Dist(Route->Points[I - 1], Route->Points[I]);
    bool bComplete = false;
    FString Failure = TEXT("Stopped before completing both directions");
    struct FFlightCoverage
    {
        double UpMin = 1, UpMax = 0, DownMin = 1, DownMax = 0;
        int32 UpFrames = 0, DownFrames = 0;
    };
    TArray<FFlightCoverage> Coverage;
    Coverage.SetNum(Stairs().Num());
    ON_SCOPE_EXIT
    {
        Player.ForwardAxis = Player.RightAxis = 0;
        Player.ConsumeMovementInputVector();
        Motion.StopMovementImmediately();
        Tower.State = SavedState; Tower.Explored = SavedRooms;
        Tower.ActiveFight = SavedFight; Tower.bMap = SavedMap;
        Tower.Location = SavedLocation; Tower.Nearby = SavedNearby;
        Tower.Notice = SavedNotice; Tower.NoticeUntil = SavedNoticeUntil;
        Mode->Result = SavedOutcome; Mode->bGateOpen = SavedGateOpen; Mode->FigureView = SavedFigureView;
        for (const FPortal& P : Portals()) Tower.SetGateOpen(P.Id, SavedGates.Contains(P.Id));
        Boss.SetActorEnableCollision(SavedBossCollision);
        Player.SetActorTransform(SavedTransform, false, nullptr, ETeleportType::TeleportPhysics);
        Motion.SetMovementMode(SavedMovementMode, SavedCustomMode); Motion.Velocity = SavedVelocity;
        Motion.MaxWalkSpeed = SavedWalkSpeed;
        Player.ForwardAxis = SavedForward; Player.RightAxis = SavedRight;
        Player.ViewYaw = SavedYaw; Player.ViewPitch = SavedPitch;
        Player.Stamina = SavedStamina; Player.RecoveryDelay = SavedRecovery; Player.ShiftPressedAt = SavedShift;
        Player.bLockOn = SavedLock; Player.bCombatOver = SavedCombatOver;
        Mode->Arena->SetNorthOrigin(SavedArenaOrigin);
        if (Tower.NarrativeActor) Tower.NarrativeActor->Restore(Tower.State, Player, *Mode->Arena);
        DuelNorthInteractables::Update(&Tower, Tower.State, SavedNearby, 0);
        UGameplayStatics::SetGamePaused(World, SavedPaused);
        TArray<FString> FlightRows{TEXT("flight,up_frames,up_min_fraction,up_max_fraction,down_frames,down_min_fraction,down_max_fraction")};
        for (int32 I = 0; I < Stairs().Num(); ++I)
        {
            const auto& C = Coverage[I];
            FlightRows.Add(FString::Printf(TEXT("%s,%d,%.5f,%.5f,%d,%.5f,%.5f"), *Stairs()[I].Id.ToString(),
                C.UpFrames, C.UpMin, C.UpMax, C.DownFrames, C.DownMin, C.DownMax));
        }
        const FString Summary = FString::Printf(TEXT("# North Tower continuous walking QA\n\nResult: %s\n%s\n\n")
            TEXT("Logical frames: %lld at fixed dt = 1/60 s. Simulated walking duration: %.3f s; this is not measured FPS or wall-clock performance.\n")
            TEXT("Reached waypoint visits: %d / %d. Authored one-way centreline: %.3f m. Actual accumulated displacement: %.3f m.\n")
            TEXT("Capsule radius / half-height: %.2f / %.2f cm. Longest falling interval: %d logical frames.\n\n")
            TEXT("Method: authored MainWalk forward then backward; one initial placement only; normal Forward/Right axes through Fighter Tick, CharacterMovement Tick and Mode Tick. All authored gates temporarily open; active encounter disabled; actual static geometry and interactable collision retained. No per-waypoint teleport, flying, jump, sprint, collision disabling on the player, or changes to movement parameters. Arrival tolerance 12 cm horizontal / 28 cm vertical. Every waypoint and every stair direction checked.\n\n")
            TEXT("Scope: geometry and locomotion traversal, not story progression, boss combat, rendered appearance or performance. World time is not advanced by these manual fixed steps. Door/progress/player state restored after the test.\n"),
            bComplete ? TEXT("PASS") : TEXT("FAIL"), *Failure, LogicalFrames, LogicalFrames * double(Step),
            Arrived, Route->Points.Num() * 2, AuthoredCm / 100., WalkedCm / 100., Radius, HalfHeight, MaxFallingFrames);
        bool Written = FFileHelper::SaveStringArrayToFile(Authored, *(OutputDir / TEXT("authored-route.csv")));
        Written &= FFileHelper::SaveStringArrayToFile(Arrivals, *(OutputDir / TEXT("waypoint-arrivals.csv")));
        Written &= FFileHelper::SaveStringArrayToFile(Samples, *(OutputDir / TEXT("walk-samples-10hz.csv")));
        Written &= FFileHelper::SaveStringArrayToFile(FlightRows, *(OutputDir / TEXT("stair-coverage.csv")));
        Written &= FFileHelper::SaveStringToFile(Summary, *(OutputDir / TEXT("README.md")));
        if (!Written) AddError(TEXT("Could not write all continuous-walk evidence files"));
    };

    // Only the barriers are opened. State.Wins/Relics/Doors are not forged and no F
    // interaction or combat outcome is invoked: this test is explicitly geometry QA.
    UGameplayStatics::SetGamePaused(World, false);
    if (auto* PC = Cast<APlayerController>(Player.GetController())) PC->FlushPressedKeys();
    Mode->Result = EDuelOutcome::Ready; Mode->bGateOpen = false; Mode->FigureView = 0;
    Tower.ActiveFight = INDEX_NONE; Tower.bMap = false;
    for (const FPortal& P : Portals()) Tower.SetGateOpen(P.Id, true);
    Boss.SetActorEnableCollision(false);
    if (Tower.NarrativeActor) Tower.NarrativeActor->Restore(Tower.State, Player, *Mode->Arena);
    Player.EndCombat(); Player.PrepareExploration();
    Player.ForwardAxis = Player.RightAxis = 0; Player.ShiftPressedAt = -1;
    Player.ConsumeMovementInputVector();
    Motion.SetMovementMode(MOVE_Walking);
    Player.SetActorLocation(Route->Points[0] + FVector(0, 0, HalfHeight + 2.f), false, nullptr, ETeleportType::TeleportPhysics);

    FCollisionQueryParams Query(SCENE_QUERY_STAT(NorthTowerWalk), false, &Player);
    Query.AddIgnoredActor(&Boss);
    // Shrink by 1-2 cm solely for penetration queries, not for actual movement.
    // The actual capsule retains its production radius and half height throughout.
    const FCollisionShape InnerCapsule = FCollisionShape::MakeCapsule(FMath::Max(1.f, Radius - 1.f), FMath::Max(Radius, HalfHeight - 2.f));
    auto Fail = [&](const FString& Why)
    {
        Failure = Why;
        AddError(Why);
        AddInfo(FString::Printf(TEXT("Walking evidence: %s"), *OutputDir));
        return false;
    };

    for (int32 Pass = 0; Pass < 2; ++Pass)
    {
        const TCHAR* Direction = Pass == 0 ? TEXT("forward") : TEXT("reverse");
        for (int32 Visit = 0; Visit < Route->Points.Num(); ++Visit)
        {
            const int32 Index = Pass == 0 ? Visit : Route->Points.Num() - 1 - Visit;
            const FVector Target = Route->Points[Index];
            const FVector SegmentStart = Visit > 0 ? Route->Points[Pass == 0 ? Index - 1 : Index + 1] : Target;
            const FVector Segment = Target - SegmentStart;
            const double SegmentXY2 = Segment.SizeSquared2D();
            const int64 StartFrame = LogicalFrames;
            const int32 Budget = FMath::CeilToInt((FVector::Dist(SegmentStart, Target) / 120.f + 5.f) / Step);
            double BestDistance = TNumericLimits<double>::Max();
            int64 LastProgress = LogicalFrames;
            bool Reached = false;
            for (int32 SegmentFrame = 0; SegmentFrame <= Budget; ++SegmentFrame)
            {
                const FVector At = Player.GetActorLocation();
                const FVector Feet = At - FVector(0, 0, HalfHeight);
                const FVector Offset = Target - Feet;
                const FLocation Located = Locate(At, HalfHeight);
                if (Offset.Size2D() <= ArrivalRadius && FMath::Abs(Offset.Z) <= ArrivalHeight && !Motion.IsFalling())
                {
                    ++Arrived; Reached = true;
                    Arrivals.Add(FString::Printf(TEXT("%s,%d,%lld,%lld,%.5f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%s,%s"),
                        Direction, Index, StartFrame, LogicalFrames, LogicalFrames * double(Step), Target.X, Target.Y, Target.Z,
                        Feet.X, Feet.Y, Feet.Z, Offset.Size2D(), Offset.Z, *Located.Room.ToString(), *Located.Stair.ToString()));
                    if (Visit % 20 == 0 || Visit == Route->Points.Num() - 1)
                        UE_LOG(LogTemp, Display, TEXT("NorthWalk %s: %d/%d waypoints, %lld logical frames / %.2f simulated seconds, at %s"),
                            Direction, Visit + 1, Route->Points.Num(), LogicalFrames, LogicalFrames * double(Step), *Feet.ToString());
                    break;
                }
                const double Distance = Offset.Size();
                if (Distance < BestDistance - 4.) { BestDistance = Distance; LastProgress = LogicalFrames; }
                if (LogicalFrames - LastProgress > 180 || SegmentFrame == Budget)
                {
                    FHitResult Hit;
                    World->SweepSingleByChannel(Hit, At, At + Offset.GetSafeNormal2D() * 120., FQuat::Identity, ECC_Pawn, InnerCapsule, Query);
                    return Fail(FString::Printf(TEXT("NorthWalk %s stuck before waypoint %d (from %s to %s), feet=%s remaining=%.1f cm; blocker=%s/%s, floor=%s, movement=%d, frame=%lld"),
                        Direction, Index, *SegmentStart.ToString(), *Target.ToString(), *Feet.ToString(), Distance,
                        *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()), *GetNameSafe(Motion.CurrentFloor.HitResult.GetComponent()),
                        int32(Motion.MovementMode), LogicalFrames));
                }

                // Feed the same camera-relative movement axes as Forward/Right bindings.
                // Slowing within 85 cm allows real braking/turning instead of snapping at a node.
                DriveNorthWalk(Player, Offset, Step, Player.ForwardAxis, Player.RightAxis, Player.ViewYaw);
                ++LogicalFrames;
                const FVector After = Player.GetActorLocation(), AfterFeet = After - FVector(0, 0, HalfHeight);
                const FVector Travel = After - At;
                WalkedCm += Travel.Size();
                const double Along = SegmentXY2 > 1 ? FMath::Clamp(FVector::DotProduct(FVector(AfterFeet.X - SegmentStart.X, AfterFeet.Y - SegmentStart.Y, 0),
                    FVector(Segment.X, Segment.Y, 0)) / SegmentXY2, 0., 1.) : 1.;
                const double ExpectedZ = FMath::Lerp(SegmentStart.Z, Target.Z, Along);
                const FString WalkError = CheckNorthWalkStep(Player, At, After, ExpectedZ, Query,
                    FallingFrames, MaxFallingFrames, PenetratingFrames);
                if (!WalkError.IsEmpty()) return Fail(FString::Printf(TEXT("NorthWalk %s waypoint %d frame=%lld: %s"),
                    Direction, Index, LogicalFrames, *WalkError));

                const FLocation AfterLocation = Locate(After, HalfHeight);
                if (AfterLocation.bOnStair)
                {
                    const int32 FlightIndex = Stairs().IndexOfByPredicate([&](const FStairFlight& S) { return S.Id == AfterLocation.Stair; });
                    if (FlightIndex != INDEX_NONE)
                    {
                        const auto& Flight = Stairs()[FlightIndex];
                        const FVector Axis = (Flight.End - Flight.Start).GetSafeNormal2D();
                        const double T = FMath::Clamp(FVector::DotProduct(AfterFeet - Flight.Start, Axis) / FVector::Dist2D(Flight.Start, Flight.End), 0., 1.);
                        auto& C = Coverage[FlightIndex];
                        const double ForwardTravel = FVector::DotProduct(Travel, Axis);
                        if (ForwardTravel > .01) { ++C.UpFrames; C.UpMin = FMath::Min(C.UpMin, T); C.UpMax = FMath::Max(C.UpMax, T); }
                        if (ForwardTravel < -.01) { ++C.DownFrames; C.DownMin = FMath::Min(C.DownMin, T); C.DownMax = FMath::Max(C.DownMax, T); }
                    }
                }
                if (LogicalFrames % 6 == 0)
                    Samples.Add(FString::Printf(TEXT("%lld,%.5f,%s,%d,%.3f,%.3f,%.3f,%.3f,%d,%s,%s"),
                        LogicalFrames, LogicalFrames * double(Step), Direction, Index, AfterFeet.X, AfterFeet.Y, AfterFeet.Z,
                        Motion.Velocity.Size(), Motion.IsFalling() ? 1 : 0, *AfterLocation.Room.ToString(), *AfterLocation.Stair.ToString()));
                Mode->Tick(Step);
                if (Mode->Outcome() != EDuelOutcome::Ready || !Mode->CanExplore() || !Player.GetActorLocation().Equals(After, .01))
                    return Fail(FString::Printf(TEXT("NorthWalk %s was interrupted or repositioned by Mode near waypoint %d, frame=%lld; outcome=%d"),
                        Direction, Index, LogicalFrames, int32(Mode->Outcome())));
            }
            if (!Reached) return Fail(FString::Printf(TEXT("NorthWalk %s did not reach waypoint %d"), Direction, Index));
        }
        // Do not reset, stop, teleport or restore velocity here. Reverse from the
        // actual garden endpoint so the first reverse segment also tests turning.
    }
    for (int32 I = 0; I < Stairs().Num(); ++I)
    {
        const auto& C = Coverage[I];
        if (C.UpFrames < 5 || C.DownFrames < 5 || C.UpMin > .10 || C.UpMax < .90 || C.DownMin > .10 || C.DownMax < .90)
            return Fail(FString::Printf(TEXT("Stair %s lacks a full physical ascent/descent: up %d frames %.3f..%.3f, down %d frames %.3f..%.3f"),
                *Stairs()[I].Id.ToString(), C.UpFrames, C.UpMin, C.UpMax, C.DownFrames, C.DownMin, C.DownMax));
    }
    bComplete = true; Failure = TEXT("Every authored waypoint reached in both directions; all 21 flights covered uphill and downhill.");
    AddInfo(FString::Printf(TEXT("North continuous walk: %d waypoint visits, %lld fixed logical frames, %.3f simulated seconds, %.3f m actual travel; all 21 stair flights uphill/downhill. Not an FPS or story-flow result. Evidence: %s"),
        Arrived, LogicalFrames, LogicalFrames * double(Step), WalkedCm / 100., *OutputDir));
    return true;
}

namespace
{
    enum class ENorthJourneyAction : uint8 { Walk, Dialogue, Clue, Rest, Fog, Relic, Door, Shortcut, Ending };
    struct FNorthJourneyStop
    {
        FVector Surface;
        FName Marker;
        ENorthJourneyAction Action = ENorthJourneyAction::Walk;
        int32 Value = INDEX_NONE;
        int32 AuthoredIndex = INDEX_NONE;
    };

    TArray<FNorthJourneyStop> NorthJourneyPlan(const DuelNorthLayout::FRoute& Route)
    {
        using namespace DuelNorthLayout;
        TMap<int32, TArray<FNorthJourneyStop>> Visits;
        const auto Attach = [&](FName Marker, ENorthJourneyAction Action, int32 Value)
        {
            const auto* Landmark = FindLandmark(Marker);
            const auto* Rest = Checkpoints().FindByPredicate([&](const FCheckpoint& C) { return C.Id == Marker; });
            check(Landmark || Rest);
            const FVector At = Landmark ? Landmark->Position : Rest->Position;
            int32 Closest = INDEX_NONE; double Distance = TNumericLimits<double>::Max();
            for (int32 I = 0; I < Route.Points.Num(); ++I)
            {
                if (FMath::Abs(Route.Points[I].Z - At.Z) > 1.f) continue;
                const double D = FVector::DistSquared2D(Route.Points[I], At);
                if (D < Distance) { Distance = D; Closest = I; }
            }
            check(Closest != INDEX_NONE);
            const bool bReunion = Marker == TEXT("RescuedCompanion") || Marker == TEXT("MirrorExitCompanion") ||
                Marker == TEXT("FinalExitCompanion");
            FVector Approach = At;
            if (bReunion)
            {
                const FVector TowardRoute = (Route.Points[Closest] - At).GetSafeNormal2D();
                Approach += (TowardRoute.IsNearlyZero() ? FVector(-1, 0, 0) : TowardRoute) * 95.f;
            }
            if (Marker == TEXT("OpeningDocket")) Approach.Y -= 95.f;
            Visits.FindOrAdd(Closest).Add({Approach, Marker, Action, Value, Closest});
        };
        const FName DialogueIds[] = {TEXT("NameWall"), TEXT("LongTable"), TEXT("RescuedCompanion"), TEXT("SeaBench")};
        for (FName Id : DialogueIds) Attach(Id, ENorthJourneyAction::Dialogue, int32(ADuelNorthNarrative::DialogueAt(Id)));
        for (const auto Dialogue : {EDuelNorthDialogue::MirrorExit, EDuelNorthDialogue::FinalExit})
            Attach(ADuelNorthNarrative::MarkerFor(Dialogue), ENorthJourneyAction::Dialogue, int32(Dialogue));
        const FName ClueIds[] = {TEXT("CellVerdict"), TEXT("WaterGateLog"), TEXT("ChapelJudgments"), TEXT("SeaMark"), TEXT("OwnVerdict"), TEXT("Signature"), TEXT("FinalSeal")};
        for (int32 I = 0; I < UE_ARRAY_COUNT(ClueIds); ++I) Attach(ClueIds[I], ENorthJourneyAction::Clue, I);
        Attach(TEXT("OpeningDocket"), ENorthJourneyAction::Clue, int32(EDuelNorthClue::OpeningDocket));
        for (const auto& Rest : Checkpoints()) Attach(Rest.Id, ENorthJourneyAction::Rest, int32(Rest.Floor));
        const FName RelicIds[] = {TEXT("RustKey"), TEXT("MirrorKey"), TEXT("NameTag")};
        for (int32 I = 0; I < UE_ARRAY_COUNT(RelicIds); ++I) Attach(RelicIds[I], ENorthJourneyAction::Relic, I);
        Attach(TEXT("GardenEdge"), ENorthJourneyAction::Ending, 0);
        const FName FogIds[] = {TEXT("DungeonGate"), TEXT("MirrorGate"), TEXT("FinalGate")};
        const FName DoorIds[] = {TEXT("ChapelDoor"), TEXT("FinalStairDoor"), TEXT("TowerExit")};
        TArray<FNorthJourneyStop> Plan;
        TSet<FName> Approached;
        for (int32 I = 0; I < Route.Points.Num(); ++I)
        {
            if (I > 0) for (const auto& Portal : Portals())
            {
                if (Portal.bWindow || Approached.Contains(Portal.Id)) continue;
                int32 Fight = INDEX_NONE, Door = INDEX_NONE;
                for (int32 J = 0; J < 3; ++J) { if (Portal.Id == FogIds[J]) Fight = J; if (Portal.Id == DoorIds[J]) Door = J; }
                const auto* Lever = ShortcutLevers().FindByPredicate([&](const FShortcutLever& L) { return L.Portal == Portal.Id; });
                if (Fight == INDEX_NONE && Door == INDEX_NONE && !Lever && Portal.Id != TEXT("CellDoor")) continue;
                const FVector A = Route.Points[I - 1], B = Route.Points[I];
                const double DA = FVector::DotProduct(A - Portal.Center, Portal.Normal), DB = FVector::DotProduct(B - Portal.Center, Portal.Normal);
                if (DA * DB > 0 || FMath::IsNearlyEqual(DA, DB)) continue;
                const FVector Crossing = FMath::Lerp(A, B, DA / (DA - DB));
                const FVector Side = FVector::CrossProduct(Portal.Normal, FVector::UpVector);
                if (FMath::Abs(Crossing.Z - Portal.Center.Z) > 2.f ||
                    FMath::Abs(FVector::DotProduct(Crossing - Portal.Center, Side)) > Portal.Width * .5f) continue;
                const auto* Marker = FindLandmark(Lever ? Lever->Id : Portal.Id);
                check(Marker);
                Plan.Add({Marker->Position, Marker->Id, Lever ? ENorthJourneyAction::Shortcut : Fight != INDEX_NONE ? ENorthJourneyAction::Fog : ENorthJourneyAction::Door,
                    Lever ? Lever->ProgressIndex : Fight != INDEX_NONE ? Fight : Door, I});
                Approached.Add(Portal.Id);
            }
            Plan.Add({Route.Points[I], NAME_None, ENorthJourneyAction::Walk, INDEX_NONE, I});
            if (const auto* Stops = Visits.Find(I)) for (const auto& Stop : *Stops)
            {
                // Small in-room detours visit objects beside the centreline; return
                // to the same authored point before following the next segment.
                Plan.Add(Stop);
                if (Stop.Marker == TEXT("SeaBench"))
                {
                    // Open from inside SeaGallery, then physically test the east loop
                    // through the chapel's already traversed hall route and return.
                    const auto* Lever = FindShortcutLever(TEXT("SeaEastLever"));
                    check(Lever);
                    Plan.Add({Lever->Position, Lever->Id, ENorthJourneyAction::Shortcut, Lever->ProgressIndex, I});
                    for (const FVector& Point : {FVector(2600,-2125,700),FVector(3500,-2125,700),FVector(3500,0,700),
                        FVector(2200,0,700),FVector(0,0,700),FVector(0,650,700),FVector(0,0,700),FVector(0,-2125,700),FVector(1400,-2125,700)})
                        Plan.Add({Point, NAME_None, ENorthJourneyAction::Walk, INDEX_NONE, I});
                }
                if (!Stop.Surface.Equals(Route.Points[I], .1f)) Plan.Add({Route.Points[I], NAME_None, ENorthJourneyAction::Walk, INDEX_NONE, I});
            }
        }
        return Plan;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthJourneyTest, "ManosabaDuel.NorthTower.Journey",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthJourneyTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace DuelNorthLayout;
    const TCHAR* Command = FCommandLine::Get();
    FString CombatHero;
    const bool bLive = FParse::Value(Command, TEXT("DuelJourneyCombat="), CombatHero);
    const bool bExtraction = FParse::Param(Command, TEXT("DuelJourneyExtraction"));
    if (!TestTrue(TEXT("Live journey parameter is -DuelJourneyCombat=Ema or Hiro"),
        bLive ? CombatHero == TEXT("Ema") || CombatHero == TEXT("Hiro") : !FParse::Param(Command, TEXT("DuelJourneyCombat")))) return false;
    if (bLive && FParse::Param(Command, TEXT("DuelNoTitle")))
    {
        AddError(TEXT("Live journey requires the New Game title flow; remove -DuelNoTitle"));
        return false;
    }
    const EDuelCharacter LiveCharacter = CombatHero == TEXT("Ema") ? EDuelCharacter::Ema : EDuelCharacter::Hiro;
    const TArray<EDuelCharacter> Characters = bLive ? TArray<EDuelCharacter>{LiveCharacter} :
        TArray<EDuelCharacter>{EDuelCharacter::Hiro, EDuelCharacter::Ema};
    if (!TestTrue(TEXT("Journey requires North Tower with saves disabled and no disk-save QA"),
        FParse::Param(FCommandLine::Get(), TEXT("DuelNorthTower")) && FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("DuelNorthSaveTest")))) return false;
    ADuelMode* Mode = nullptr;
    if (GEngine) for (const auto& Context : GEngine->GetWorldContexts())
        if (Context.World() && (Mode = Context.World()->GetAuthGameMode<ADuelMode>())) break;
    if (!TestTrue(TEXT("Journey has an actual North Tower world"), Mode && Mode->UsesNorthTower() &&
        Mode->NorthTowerActor && Mode->PlayerFighter && Mode->BossFighter && Mode->Arena)) return false;
    auto& Tower = *Mode->NorthTowerActor;
    auto& Player = *Mode->PlayerFighter;
    auto* Controller = Cast<APlayerController>(Player.GetController());
    const auto* Route = FindRoute(TEXT("MainWalk"));
    if (!TestTrue(TEXT("Actual input bindings and authored route exist"), Controller && Controller->PlayerInput && Player.InputComponent && Route)) return false;
    const auto Plan = NorthJourneyPlan(*Route);
    TSet<int32> AuthoredVisits;
    for (const auto& Stop : Plan)
        if (Stop.Action == ENorthJourneyAction::Walk && Stop.Surface.Equals(Route->Points[Stop.AuthoredIndex], .1f)) AuthoredVisits.Add(Stop.AuthoredIndex);
    if (!TestEqual(TEXT("Journey retains all original authored route points"), AuthoredVisits.Num(), Route->Points.Num())) return false;
    if (!TestEqual(TEXT("Journey physically operates both shortcut levers"), Plan.FilterByPredicate([](const auto& P)
        { return P.Action == ENorthJourneyAction::Shortcut; }).Num(), 2)) return false;
    if (!TestEqual(TEXT("Plan approaches all seven locked thresholds before crossing"), Plan.FilterByPredicate([](const auto& P)
        { return P.Action == ENorthJourneyAction::Fog || P.Action == ENorthJourneyAction::Door; }).Num(), 7)) return false;
    const int32 Docket = Plan.IndexOfByPredicate([](const auto& P) { return P.Marker == TEXT("OpeningDocket"); });
    const int32 CellDoor = Plan.IndexOfByPredicate([](const auto& P) { return P.Marker == TEXT("CellDoor"); });
    const int32 FirstCrossing = Plan.IndexOfByPredicate([](const auto& P)
        { return P.Action == ENorthJourneyAction::Walk && P.AuthoredIndex == 1; });
    if (!TestTrue(TEXT("The authored route reads the in-cell docket and unlocks the door before the first crossing"),
        Docket >= 0 && Docket < CellDoor && CellDoor < FirstCrossing)) return false;

    constexpr float Step = 1.f / 60.f;
    if (!TestTrue(TEXT("Journey starts in title or peaceful exploration"), Mode->Outcome() == EDuelOutcome::Title ||
        Mode->Outcome() == EDuelOutcome::Ready)) return false;
    const auto OriginalState = Tower.State;
    const auto OriginalRooms = Tower.Explored;
    const auto OriginalCharacter = Player.SelectedCharacter();
    const auto OriginalBossCharacter = Mode->BossFighter->SelectedCharacter();
    const auto OriginalOutcome = Mode->Result;
    const auto OriginalTransform = Player.GetActorTransform();
    const auto OriginalWeapons = TArray<EDuelWeapon>{Player.Equipped, Player.Offhand, Player.RightReserve, Player.LeftReserve};
    const auto OriginalArt = Player.ArtSelection;
    const auto OriginalSpells = Player.PreparedSpells;
    const auto OriginalActiveSpell = Player.ActiveSpellSlot;
    const auto OriginalFlasks = Player.HealthFlaskAllocation;
    const bool OriginalCheckpoint = Tower.bCheckpointAvailable;
    const bool OriginalPaused = UGameplayStatics::IsGamePaused(Mode);
    const FString OutputDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("NorthTower200/JourneyQA") /
        FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")));
    if (!TestTrue(TEXT("Journey evidence folder is writable"), IFileManager::Get().MakeDirectory(*OutputDir, true))) return false;
    TArray<FString> Versions{TEXT("file,bytes,md5,last_write_utc")};
    for (const TCHAR* Relative : {TEXT("Binaries/Win64/UnrealEditor-ManosabaDuel.dll"), TEXT("Source/ManosabaDuel/DuelNorthWalkQA.cpp"),
        TEXT("Source/ManosabaDuel/DuelNorthLayout.cpp"), TEXT("Source/ManosabaDuel/DuelNorthTower.cpp"),
        TEXT("Source/ManosabaDuel/DuelSmoke.cpp"), TEXT("Source/ManosabaDuel/DuelLiveCombatQA.h"),
        TEXT("Source/ManosabaDuel/DuelNorthNarrative.cpp"), TEXT("Source/ManosabaDuel/DuelNorthProgress.cpp"),
        TEXT("Config/DefaultEngine.ini"), TEXT("Config/DefaultInput.ini"), TEXT("Config/DefaultGame.ini")})
    {
        const FString File = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / Relative);
        Versions.Add(FString::Printf(TEXT("%s,%lld,%s,%s"), Relative, IFileManager::Get().FileSize(*File),
            *LexToString(FMD5Hash::HashFile(*File)), *IFileManager::Get().GetTimeStamp(*File).ToIso8601()));
    }
    TArray<FString> Events{TEXT("route,walk_frame,walk_logic_seconds,event,marker,x,y,feet_z,wins,relics,doors,dialogues,clues,checkpoint,shortcuts,boss_character,world_seconds")};
    TArray<FString> Fights{TEXT("route,battle,boss,result,seed,world_seconds,phase_mask,attack_types,damage_frames,dodges,parries,player_hp,boss_hp")};
    TArray<FString> Samples{TEXT("route,frame,simulated_seconds,plan_index,authored_index,x,y,feet_z,speed,room,stair")};
    TArray<FString> Planned{TEXT("plan_index,authored_index,action,marker,x,y,z")};
    for (int32 I = 0; I < Plan.Num(); ++I) Planned.Add(FString::Printf(TEXT("%d,%d,%d,%s,%.3f,%.3f,%.3f"), I,
        Plan[I].AuthoredIndex, int32(Plan[I].Action), *Plan[I].Marker.ToString(), Plan[I].Surface.X, Plan[I].Surface.Y, Plan[I].Surface.Z));
    int64 Frames = 0;
    int32 Injections = 0, FPresses = 0, RouteCompletions = 0, Visited = 0;
    int32 FallingFrames = 0, MaxFallingFrames = 0, PenetratingFrames = 0;
    double WalkedCm = 0;
    bool bPassed = false;
    FString Failure = TEXT("Journey stopped before completing its expected route or routes");
    FString RouteName;
    int32 CurrentStop = 0, Battles = 0;
    enum class EJourneyUpdate { Fighting, Failed, Complete };
    const TWeakObjectPtr<ADuelMode> Game(Mode);
    auto Advance = [this, Game, Plan, Characters, bLive, bExtraction, OutputDir, Versions = MoveTemp(Versions), Planned = MoveTemp(Planned),
        Events = MoveTemp(Events), Samples = MoveTemp(Samples), Fights = MoveTemp(Fights),
        OriginalState, OriginalRooms, OriginalCharacter, OriginalBossCharacter, OriginalOutcome, OriginalTransform,
        OriginalWeapons, OriginalArt, OriginalSpells, OriginalActiveSpell, OriginalFlasks, OriginalCheckpoint, OriginalPaused,
        Frames, Injections, FPresses, RouteCompletions, Visited, FallingFrames, MaxFallingFrames, PenetratingFrames,
        WalkedCm, bPassed, Failure = MoveTemp(Failure), RouteName = MoveTemp(RouteName), CurrentStop, Battles,
        bRouteStarted = false, bExitedCell = false, LiveVictories = 0, Fight = TOptional<FDuelLiveCombat>()]() mutable -> EJourneyUpdate
    {
        ADuelMode* Mode = Game.Get();
        if (!Mode || !Mode->PlayerFighter || !Mode->BossFighter || !Mode->NorthTowerActor || !Mode->Arena)
        {
            AddError(TEXT("North journey world unloaded before completion"));
            return EJourneyUpdate::Failed;
        }
        auto& Tower = *Mode->NorthTowerActor;
        auto& Player = *Mode->PlayerFighter;
        auto& Motion = *Player.GetCharacterMovement();
        auto* Controller = Cast<APlayerController>(Player.GetController());
        if (!Controller || !Controller->PlayerInput || !Player.InputComponent)
        {
            AddError(TEXT("North journey lost its bound player controls"));
            return EJourneyUpdate::Failed;
        }
        const TArray<UInputComponent*> InputStack{Player.InputComponent};
        const auto Record = [&](const TCHAR* Event, FName Marker)
        {
            const FVector At = Player.GetActorLocation() - FVector(0, 0, Player.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
            Events.Add(FString::Printf(TEXT("%s,%lld,%.5f,%s,%s,%.3f,%.3f,%.3f,%u,%u,%u,%u,%u,%u,%u,%u,%.5f"),
                *RouteName, Frames, Frames * double(Step), Event, *Marker.ToString(), At.X, At.Y, At.Z,
                Tower.State.Wins, Tower.State.Relics, Tower.State.Doors, Tower.State.Dialogues, Tower.State.Clues, Tower.State.Checkpoint, Tower.State.Shortcuts, uint8(Mode->BossFighter->SelectedCharacter()),
                Mode->GetWorld()->GetTimeSeconds()));
        };
        const auto Fail = [&](const FString& Why)
        {
            Failure = Why; AddError(Why); Record(TEXT("FAIL"), CurrentStop < Plan.Num() ? Plan[CurrentStop].Marker : NAME_None);
            AddInfo(FString::Printf(TEXT("Journey evidence: %s"), *OutputDir)); return false;
        };
        bool bYieldingToWorld = false;
        ON_SCOPE_EXIT
        {
            if (!bYieldingToWorld)
            {
                Controller->FlushPressedKeys();
                UGameplayStatics::SetGamePaused(Mode, false);
                Player.EndCombat(); Player.SelectCharacter(OriginalCharacter);
                Player.SetWeapon(OriginalWeapons[0]); Player.SetWeapon(OriginalWeapons[1], EDuelHand::Left);
                Player.RightReserve = OriginalWeapons[2]; Player.LeftReserve = OriginalWeapons[3];
                Player.ArtSelection = OriginalArt; Player.PreparedSpells = OriginalSpells;
                Player.ActiveSpellSlot = OriginalActiveSpell; Player.HealthFlaskAllocation = OriginalFlasks;
                Mode->SetBoss(OriginalBossCharacter);
                Tower.State = OriginalState; Tower.Explored = OriginalRooms; Tower.Respawn(*Mode);
                Player.SetActorTransform(OriginalTransform, false, nullptr, ETeleportType::TeleportPhysics);
                Mode->Result = OriginalOutcome; Tower.bCheckpointAvailable = OriginalCheckpoint;
                if (Tower.NarrativeActor) Tower.NarrativeActor->Restore(Tower.State, Player, *Mode->Arena);
                UGameplayStatics::SetGamePaused(Mode, OriginalPaused);
                const FString Summary = FString::Printf(TEXT("# North Tower gated walking journey\n\nResult: %s\n%s\n\n")
                    TEXT("Completed protagonists: %d / %d; live victories: %d; scripted victory injections: %d; F press/release pairs: %d; waypoint visits: %d.\n")
                    TEXT("Fixed logic frames: %lld at 1/60 s, simulated duration %.3f s. Actual accumulated movement %.3f m; longest falling interval %d frames. These are not render FPS or wall-clock performance measurements.\n\n")
                    TEXT("Method: actual title New Game per protagonist, all MainWalk points plus logged in-room object/rest detours and gate-front stops, production Forward/Right movement and capsule sweeps, real F through PlayerController input bindings. Continuous movement from spawn to the garden. The three investigations are seeded only at their respective fog gates for this walking test. The final fog is also checked by pressing F before seeding the lamp trial.\n\n")
                    TEXT("%s\nFormal saves untouched (-DuelNoSave); test state restored on exit.\n"),
                    bPassed ? TEXT("PASS") : TEXT("FAIL"), *Failure, RouteCompletions, Characters.Num(), LiveVictories, Injections, FPresses, Visited,
                    Frames, Frames * double(Step), WalkedCm / 100., MaxFallingFrames,
                    bLive ? TEXT("Live opponents, damage, resources and transitions run on Unreal world frames, using the same automated action policy as LiveMatches. Loadouts are set through bound pause/menu keys. NullRHI initializes the same title state before bound A/D/F input. Walking uses fixed manual movement steps. Battle time and counters are in fights.csv; those seconds are separate from the walking clock.") :
                            TEXT("Traversal fixture injects one explicit victory at each real fog crossing. Combat is covered by the live journey option."));
                bool Written = FFileHelper::SaveStringToFile(Summary, *(OutputDir / TEXT("README.md")));
                Written &= FFileHelper::SaveStringArrayToFile(Events, *(OutputDir / TEXT("events.csv")));
                Written &= FFileHelper::SaveStringArrayToFile(Samples, *(OutputDir / TEXT("walking-10hz.csv")));
                Written &= FFileHelper::SaveStringArrayToFile(Planned, *(OutputDir / TEXT("authored-journey.csv")));
                Written &= FFileHelper::SaveStringArrayToFile(Versions, *(OutputDir / TEXT("build-and-config.csv")));
                if (bLive) Written &= FFileHelper::SaveStringArrayToFile(Fights, *(OutputDir / TEXT("fights.csv")));
                if (!Written) AddError(TEXT("Could not write the complete Journey evidence"));
            }
        };

        const auto Key = [&](FKey KeyCode, EInputEvent Event)
        {
            Controller->InputKey(FInputKeyParams(KeyCode, Event, Event == IE_Pressed ? 1.0 : 0.0));
            Controller->PlayerInput->ProcessInputStack(InputStack, Step, UGameplayStatics::IsGamePaused(Mode));
            Mode->Tick(Step); ++Frames;
        };
        const auto Tap = [&](FKey KeyCode, bool bAllowSpawn = false)
        {
            const FVector Before = Player.GetActorLocation();
            Key(KeyCode, IE_Pressed); Key(KeyCode, IE_Released);
            if (KeyCode == EKeys::F)
            {
                ++FPresses;
                Record(TEXT("F_PRESS_RELEASE"), CurrentStop < Plan.Num() ? Plan[CurrentStop].Marker : NAME_None);
            }
            return bAllowSpawn || Player.GetActorLocation().Equals(Before, .01f);
        };
        const auto SetLiveLoadout = [&](EDuelCharacter Opponent)
        {
            if (!Tap(EKeys::Escape) || !UGameplayStatics::IsGamePaused(Mode)) return false;
            for (int32 I = 0; I < Mode->PauseOptionCount() && Mode->PauseOption(Mode->MenuSelection) != EDuelPause::Loadout; ++I)
                if (!Tap(EKeys::Down)) return false;
            if (Mode->PauseOption(Mode->MenuSelection) != EDuelPause::Loadout || !Tap(EKeys::F) || !Mode->bLoadout) return false;
            const auto FocusRow = [&](int32 Row)
            {
                for (int32 I = 0; I < ADuelMode::LoadoutRows && Mode->LoadoutChoice() != Row; ++I)
                    if (!Tap(EKeys::Down)) return false;
                return Mode->LoadoutChoice() == Row;
            };
            const auto Select = [&](int32 Row, auto Before, auto Desired)
            {
                if (!FocusRow(Row)) return false;
                const auto Initial = Before();
                while (Before() != Desired)
                {
                    if (!Tap(EKeys::D) || Before() == Initial) return false;
                }
                return true;
            };
            const auto Weapon = [&](EDuelHand Hand, EDuelWeapon Desired)
            {
                const int32 Row = Hand == EDuelHand::Right ? 0 : 1;
                const EDuelWeapon Reserve = Hand == EDuelHand::Right ? Player.RightReserve : Player.LeftReserve;
                // The menu excludes the reserve item from the armed slot's choices.
                if (Player.EquippedWeapon(Hand) != Desired && Reserve == Desired && (!FocusRow(Row + 2) || !Tap(EKeys::D))) return false;
                return Select(Row, [&]() { return Player.EquippedWeapon(Hand); }, Desired);
            };
            const EDuelWeapon Main = Opponent == EDuelCharacter::Hiro ? EDuelWeapon::Rapier : EDuelWeapon::Greatblade;
            const EDuelArt Art = Player.SelectedCharacter() == EDuelCharacter::Ema ? EDuelArt::EmaLight : EDuelArt::HiroThreads;
            if (!Weapon(EDuelHand::Right, Main) || !Weapon(EDuelHand::Left, EDuelWeapon::Greatshield) ||
                !Select(ADuelMode::ArtLoadoutRow, [&]() { return Player.SelectedArt(); }, Art) ||
                !Tap(EKeys::Escape) || Mode->bLoadout || !Tap(EKeys::Escape) || UGameplayStatics::IsGamePaused(Mode)) return false;
            Record(TEXT("LOADOUT_BY_BOUND_KEYS"), Plan[CurrentStop].Marker);
            return true;
        };
        const auto TickCompanion = [&]()
        {
            if (const auto* Narrative = Tower.Narrative())
                if (auto* Companion = const_cast<ADuelFighter*>(Narrative->Companion()); Companion && Companion->IsActorTickEnabled())
                {
                    Companion->Tick(Step);
                    Companion->GetCharacterMovement()->TickComponent(Step, LEVELTICK_All, nullptr);
                }
        };
        const FName FogIds[] = {TEXT("DungeonGate"), TEXT("MirrorGate"), TEXT("FinalGate")};
        // Match the successful NorthLive-06 seeds for each protagonist.
        const int32 FirstFightSeed = Characters[0] == EDuelCharacter::Ema ? 27141 : 27131;
        const auto CloseBattleDialogue = [&](int32 Battle)
        {
            // Injected wins bypass the tower tick that normally advances the narrative out of combat.
            Mode->Tick(0.f);
            const auto* Story = Tower.Narrative();
            return Story && Story->IsTalking() && Story->Subtitle().Line == 1 &&
                Story->Subtitle().Character == Tower.State.BossForBattle(Battle) &&
                !Mode->CanExplore() && Tap(EKeys::Escape) && !Story->IsTalking() && Mode->CanExplore();
        };
        const auto HandleFogCrossing = [&]()
        {
            if (Mode->Outcome() != EDuelOutcome::Fighting) return true;
            if (Battles >= 3 || Tower.ActiveFight != Battles || Mode->BossFighter->SelectedCharacter() != Tower.State.BossForBattle(Battles) ||
                Tower.IsGateOpen(FogIds[Battles]) || Tower.State.HasWon(Battles)) return Fail(TEXT("Fog crossing entered an incorrect/repeated encounter or did not close its gate"));
            Record(TEXT("REAL_FOG_CROSSING"), FogIds[Battles]);
            if (!Tap(EKeys::M) || Tower.MapVisible()) return Fail(TEXT("Map changed input or opened during the real encounter entry"));
            if (bLive)
            {
                if (Mode->BossFighter->Phase() != 1 || !Mode->BossFighter->IsAlive() ||
                    !Mode->Arena->CourtOrigin().Equals(DuelNorthLayout::CourtOrigin(Battles), 1.f))
                    return Fail(TEXT("Live crossing did not start a healthy first-phase boss at the authored court"));
                Fight.Emplace();
                Fight->Begin(*Mode, FirstFightSeed + Battles);
                Record(TEXT("LIVE_FIGHT_BEGIN"), FogIds[Battles]);
                return true;
            }
            const FVector Before = Player.GetActorLocation();
            Mode->FighterFell(Mode->BossFighter); // Deliberate test-only injection; never claimed as AI victory.
            ++Injections;
            Record(TEXT("INJECTED_VICTORY_NOT_COMBAT_TEST"), FogIds[Battles]);
            if (!Player.GetActorLocation().Equals(Before, .01f) || Mode->Outcome() != EDuelOutcome::Victory || !Tower.State.HasWon(Battles) ||
                Tower.State.HasRelic(static_cast<EDuelNorthRelic>(Battles)) || Tower.State.IsDoorOpen(static_cast<EDuelNorthDoor>(Battles)))
                return Fail(TEXT("Victory teleported the player, granted an uncollected relic or unlocked a door prematurely"));
            if (!CloseBattleDialogue(Battles)) return Fail(TEXT("Escape did not resume the journey after the boss dialogue"));
            ++Battles;
            return true;
        };

        const auto WalkTo = [&](const FVector& Target)
        {
            const float Half = Player.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            const FVector Start = Player.GetActorLocation() - FVector(0, 0, Half);
            const FVector Segment = Target - Start;
            const double XY2 = Segment.SizeSquared2D();
            const int32 Budget = FMath::CeilToInt((Segment.Size() / 120. + 6.) / Step);
            int64 LastProgress = Frames; double Best = TNumericLimits<double>::Max();
            const int32 AuthoredIndex = Plan.IsValidIndex(CurrentStop) ? Plan[CurrentStop].AuthoredIndex : INDEX_NONE;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(NorthJourneyWalk), false, &Player);
            Query.AddIgnoredActor(Mode->BossFighter);
            for (int32 Frame = 0; Frame < Budget; ++Frame)
            {
                const FVector Before = Player.GetActorLocation();
                const FVector Offset = Target - (Before - FVector(0, 0, Half));
                if (Offset.Size2D() <= 12.f && FMath::Abs(Offset.Z) <= 28.f && !Motion.IsFalling()) return true;
                if (Offset.Size() < Best - 4.) { Best = Offset.Size(); LastProgress = Frames; }
                if (Frames - LastProgress > 180) return Fail(FString::Printf(TEXT("%s stuck on plan %d (MainWalk %d), feet %s, target %s, selected %s, gate bits %u"),
                    *RouteName, CurrentStop, AuthoredIndex, *(Before - FVector(0,0,Half)).ToString(), *Target.ToString(), *Tower.Nearby.ToString(), Tower.State.Doors));
                DriveNorthWalk(Player, Offset, Step, Player.ForwardAxis, Player.RightAxis, Player.ViewYaw);
                ++Frames;
                const FVector After = Player.GetActorLocation(), Feet = After - FVector(0, 0, Half);
                WalkedCm += FVector::Dist(Before, After);
                const double Along = XY2 > 1. ? FMath::Clamp(FVector::DotProduct(FVector(Feet.X - Start.X, Feet.Y - Start.Y, 0), FVector(Segment.X, Segment.Y, 0)) / XY2, 0., 1.) : 1.;
                const FString Error = CheckNorthWalkStep(Player, Before, After, FMath::Lerp(Start.Z, Target.Z, Along), Query,
                    FallingFrames, MaxFallingFrames, PenetratingFrames);
                if (!Error.IsEmpty()) return Fail(FString::Printf(TEXT("%s plan %d frame %lld: %s"), *RouteName, CurrentStop, Frames, *Error));
                Mode->Tick(Step);
                if (!Player.GetActorLocation().Equals(After, .01f)) return Fail(TEXT("Mode repositioned the player while walking or entering a fog gate"));
                if (!HandleFogCrossing() || Fight.IsSet()) return false;
                TickCompanion();
                if (Mode->Outcome() != EDuelOutcome::Ready && Mode->Outcome() != EDuelOutcome::Victory) return Fail(TEXT("Unexpected world state interrupted the walking journey"));
                if (Frames % 6 == 0)
                {
                    const auto At = Locate(After, Half);
                    Samples.Add(FString::Printf(TEXT("%s,%lld,%.5f,%d,%d,%.3f,%.3f,%.3f,%.3f,%s,%s"), *RouteName, Frames, Frames * double(Step),
                        CurrentStop, AuthoredIndex, Feet.X, Feet.Y, Feet.Z, Motion.Velocity.Size(), *At.Room.ToString(), *At.Stair.ToString()));
                }
            }
            return Fail(FString::Printf(TEXT("Journey exceeded the physical walking budget at plan %d"), CurrentStop));
        };

        const auto RunExtraction = [&](const ADuelFighter& Companion)
        {
            using namespace DuelNorthExtractionVisual;
            const auto& Narrative = *Tower.Narrative();
            const auto Feet = [](const ADuelFighter& Fighter)
            {
                return Fighter.GetActorLocation() - FVector(0, 0, Fighter.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
            };
            if (!WalkTo(PickupMark)) return false;
            if (Tower.State.bExtractionSeen || !Tower.Prompt().Contains(TEXT("用海岸电台呼叫直升机")))
                return Fail(TEXT("Walking to the coastal radio did not expose the first-call prompt"));
            if (!Tap(EKeys::F) || !Tower.Prompt().Contains(TEXT("直升机正在接近")) || !Mode->CanExplore())
                return Fail(TEXT("Radio F did not start a helicopter approach with walking controls available"));
            Record(TEXT("EXTRACTION_RADIO_F"), TEXT("CoastalRadio"));

            // Leave room in front of the ladder and use the walking deck height.
            if (!WalkTo(FVector(LadderFoot.X - 85., LadderFoot.Y, PickupMark.Z))) return false;
            const int32 LadderBudget = FMath::CeilToInt((ApproachSeconds + DeploySeconds + 5.f) / Step);
            const auto LadderReady = [&]() { return Tower.Prompt().Contains(TEXT("登上绳梯")); };
            for (int32 Frame = 0; Frame < LadderBudget && (Frame < 16 || !LadderReady()); ++Frame)
            {
                if (Frame < 16) DriveNorthWalk(Player, FVector::ZeroVector, Step, Player.ForwardAxis, Player.RightAxis, Player.ViewYaw);
                Mode->Tick(Step); TickCompanion(); ++Frames;
            }
            if (!LadderReady())
                return Fail(FString::Printf(TEXT("Ladder prompt did not become ready at player %s and companion %s: %s"),
                    *Feet(Player).ToString(), *Feet(Companion).ToString(), *Tower.Prompt()));
            Record(TEXT("EXTRACTION_LADDER_READY"), TEXT("RopeLadder"));
            const FVector PlayerBefore = Feet(Player), CompanionBefore = Feet(Companion);
            if (!Tap(EKeys::F, true) || !Narrative.ExtractionInProgress() || Mode->CanExplore())
                return Fail(TEXT("Ladder F did not begin the boarding scene"));
            Record(TEXT("EXTRACTION_BOARD_F"), TEXT("RopeLadder"));

            bool bBothAirborne = false;
            int32 SeatedFlightFrames = 0;
            const int32 FlightBudget = FMath::CeilToInt((RetractSeconds + DepartureSeconds + 14.f) / Step);
            for (int32 Frame = 0; Frame < FlightBudget && !Narrative.ExtractionEndingVisible(); ++Frame)
            {
                Mode->Tick(Step); ++Frames;
                if (Feet(Player).Z > BoardingPoint.Z+150. && Feet(Companion).Z > BoardingPoint.Z+150. &&
                    FMath::IsNearlyEqual(FVector::Distance(Feet(Player),Feet(Companion)),FVector::Distance(NearSeatLocal,FarSeatLocal),1.))
                    ++SeatedFlightFrames;
                if (!bBothAirborne && !Tower.State.bExtractionSeen &&
                    Feet(Player).Z > LadderFoot.Z + 300. && Feet(Companion).Z > LadderFoot.Z + 300.)
                {
                    bBothAirborne = true;
                    Record(TEXT("EXTRACTION_BOTH_AIRBORNE"), TEXT("Helicopter"));
                    AddInfo(FString::Printf(TEXT("North Journey %s airborne feet Z: protagonist %.2f cm, companion %.2f cm."),
                        *RouteName, Feet(Player).Z, Feet(Companion).Z));
                }
            }
            if (!bBothAirborne || SeatedFlightFrames < 30 || !Narrative.ExtractionEndingVisible() || !Tower.State.bExtractionSeen || !Tower.State.bEndingSeen)
                return Fail(TEXT("Both passengers must ascend before the optional ending, with both ending flags recorded"));
            Record(TEXT("EXTRACTION_ENDING"), TEXT("Helicopter"));
            if (Player.SelectedCharacter() == EDuelCharacter::Hiro)
            {
                for (int32 Frame = 0; Frame < FMath::CeilToInt(17.f / Step) && Mode->Outcome() != EDuelOutcome::Title; ++Frame)
                {
                    Mode->Tick(Step);
                    ++Frames;
                }
            }
            else if (!Tap(EKeys::F, true)) return Fail(TEXT("Epilogue confirm was not accepted"));
            if (Narrative.EndingVisible() || Narrative.ExtractionInProgress() || Mode->Outcome() != EDuelOutcome::Title)
                return Fail(TEXT("Completed flight must return to title on timeout or confirm"));
            // Both return paths must also survive the next ordinary title frame.
            Mode->Tick(Step); ++Frames;
            const FVector PlayerDelta = Feet(Player) - PlayerBefore, CompanionDelta = Feet(Companion) - CompanionBefore;
            if (PlayerDelta.Size2D() > 12. || CompanionDelta.Size2D() > 12. || FMath::Abs(PlayerDelta.Z) > 10. ||
                FMath::Abs(CompanionDelta.Z) > 10. || !Motion.IsMovingOnGround() || !Companion.GetCharacterMovement()->IsMovingOnGround() ||
                !Player.GetActorEnableCollision() || Companion.GetActorEnableCollision() || !Companion.IsHidden())
                return Fail(FString::Printf(TEXT("Title return restores both Garden footings and hides the inactive companion; offsets %s and %s"),
                    *PlayerDelta.ToString(), *CompanionDelta.ToString()));
            Record(Player.SelectedCharacter() == EDuelCharacter::Hiro ? TEXT("EXTRACTION_RETURN_TIMEOUT") : TEXT("EXTRACTION_RETURN_F"), TEXT("RopeLadder"));
            AddInfo(FString::Printf(TEXT("North Journey %s Garden return offsets in cm: protagonist %s, companion %s."),
                *RouteName, *PlayerDelta.ToString(), *CompanionDelta.ToString()));
            return true;
        };

        const auto AdvanceWalking = [&]() -> bool
        {
            while (RouteCompletions < Characters.Num())
            {
                const EDuelCharacter Character = Characters[RouteCompletions];
                if (!bRouteStarted)
                {
                    RouteName = Character == EDuelCharacter::Hiro ? TEXT("Hiro") : TEXT("Ema");
                    CurrentStop = Battles = FallingFrames = PenetratingFrames = 0;
                    bExitedCell = false;
                    Controller->FlushPressedKeys(); UGameplayStatics::SetGamePaused(Mode, false);
                    if (bLive)
                    {
                        // A headless start skips the visual title in StartPlay; arm that same menu state.
                        if (!FApp::CanEverRender() && Mode->Outcome() == EDuelOutcome::Ready)
                        {
                            Mode->Result = EDuelOutcome::Title;
                            Mode->MenuSelection = Mode->CanContinue() ? 0 : 1;
                            Record(TEXT("HEADLESS_TITLE_SETUP"), NAME_None);
                        }
                        if (Mode->Outcome() != EDuelOutcome::Title || Mode->MenuSelection != 1 ||
                            !Tap(Character == EDuelCharacter::Ema ? EKeys::A : EKeys::D) || Player.SelectedCharacter() != Character)
                            return Fail(TEXT("A/D must select the requested hero on a fresh title's New Game row"));
                    }
                    else
                    {
                        Player.EndCombat(); Player.SelectCharacter(Character);
                        Mode->Result = EDuelOutcome::Title; Mode->MenuSelection = 1;
                    }
                    if (!Tap(EKeys::F, true) || Mode->Outcome() != EDuelOutcome::Ready || Tower.State.Player() != Character ||
                        Tower.State.Wins || Tower.State.Relics || Tower.State.Doors || Tower.State.IsOpeningCellOpen() ||
                        Tower.IsGateOpen(TEXT("CellDoor")) || Tower.State.HasClue(EDuelNorthClue::OpeningDocket) ||
                        !Tower.State.UsesStartingCellCheckpoint() || FVector::DistSquared2D(Player.GetActorLocation(), SpawnSurface()) > 1.)
                        return Fail(TEXT("Real F on title New Game did not spawn the chosen hero inside the locked cell"));
                    Record(TEXT("TITLE_NEW_GAME_SPAWN"), NAME_None);
                    Player.ForwardAxis = Player.RightAxis = 0; Player.ShiftPressedAt = -1;
                    bRouteStarted = true;
                }
                for (; CurrentStop < Plan.Num(); ++CurrentStop)
                {
                    const auto& Stop = Plan[CurrentStop];
                    if (!WalkTo(Stop.Surface)) return false;
                    ++Visited;
                    if (Stop.Action == ENorthJourneyAction::Walk)
                    {
                        if (!bExitedCell && Locate(Player.GetActorLocation(),
                            Player.GetCapsuleComponent()->GetScaledCapsuleHalfHeight()).Room == TEXT("PrisonCellPassage"))
                        {
                            if (!Tower.State.HasClue(EDuelNorthClue::OpeningDocket) || !Tower.State.IsOpeningCellOpen() ||
                                !Tower.IsGateOpen(TEXT("CellDoor"))) return Fail(TEXT("Journey left the cell before reading and unlocking it"));
                            bExitedCell = true;
                            Record(TEXT("STARTING_CELL_EXIT"), TEXT("CellDoor"));
                        }
                        continue;
                    }
                    // Stop via ordinary braking before pressing F. No position snapping.
                    for (int32 I = 0; I < 16; ++I)
                    {
                        const FVector Before = Player.GetActorLocation();
                        DriveNorthWalk(Player, FVector::ZeroVector, Step, Player.ForwardAxis, Player.RightAxis, Player.ViewYaw);
                        ++Frames;
                        const FVector After = Player.GetActorLocation();
                        FCollisionQueryParams Query(SCENE_QUERY_STAT(NorthJourneyBrake), false, &Player);
                        Query.AddIgnoredActor(Mode->BossFighter);
                        const FString Error = CheckNorthWalkStep(Player, Before, After, Stop.Surface.Z, Query,
                            FallingFrames, MaxFallingFrames, PenetratingFrames);
                        if (!Error.IsEmpty()) return Fail(TEXT("Braking before interaction: ") + Error);
                        Mode->Tick(Step); TickCompanion();
                        WalkedCm += FVector::Dist(Before, After);
                        if (!Player.GetActorLocation().Equals(After, .01f)) return Fail(TEXT("Mode relocated the player while braking for interaction"));
                    }
                    if (Tower.Nearby != Stop.Marker)
                        return Fail(FString::Printf(TEXT("%s physically reached %s but proximity selected %s at %s; no synthetic F target substituted"),
                            *RouteName, *Stop.Marker.ToString(), *Tower.Nearby.ToString(), *Player.GetActorLocation().ToString()));
                    if (Stop.Action == ENorthJourneyAction::Fog && !Tower.State.HasInvestigation(Stop.Value))
                    {
                        if (Stop.Value == 2 && (!Tap(EKeys::F) || Mode->GateOpen() || Tower.IsGateOpen(Stop.Marker) ||
                            Tower.State.CanEnterBattle(2) || Tower.State.NextObjective().Target != TEXT("BeaconChart")))
                            return Fail(TEXT("F at the final fog must lead to the lamp rooms while the trial is still unsolved"));
                        if (!DuelNorthInvestigationQA::Prepare(Tower.State, Stop.Value))
                            return Fail(TEXT("Could not seed the required investigation after walking to its floor"));
                        Record(TEXT("COMPLETED_INVESTIGATION_FIXTURE"), Stop.Marker);
                    }
                    if (bLive && Stop.Action == ENorthJourneyAction::Fog && !SetLiveLoadout(Tower.State.BossForBattle(Stop.Value)))
                        return Fail(TEXT("Could not choose the established duel loadout through the actual pause menu"));
                    if (!Tap(EKeys::F)) return Fail(TEXT("F interaction unexpectedly relocated the player"));
                    if (Tower.Narrative()->IsReadingDocument())
                    {
                        if (!Tap(EKeys::F)) return Fail(TEXT("Revealing the paper relocated the player"));
                        if (Tower.Narrative()->IsReadingDocument() && !Tap(EKeys::F))
                            return Fail(TEXT("Starting the paper analysis relocated the player"));
                    }
                    const int32 DialogueLines = Tower.Narrative()->Subtitle().Count;
                    for (int32 Line = 0; Line < DialogueLines && Tower.Narrative()->IsTalking(); ++Line)
                        if (!Tap(EKeys::F)) return Fail(TEXT("Advancing dialogue relocated the player"));
                    if (Tower.Narrative()->IsTalking()) return Fail(TEXT("Dialogue did not close within its authored line count"));
                    if (Stop.Action == ENorthJourneyAction::Fog)
                    {
                        if (!Mode->GateOpen() && !Tap(EKeys::F)) return Fail(TEXT("Fog confirmation relocated the player"));
                        if (Mode->Outcome() == EDuelOutcome::Fighting || !Mode->GateOpen() || !Tower.IsGateOpen(Stop.Marker) || Tower.ActiveFight != Stop.Value)
                            return Fail(TEXT("F must open the correct fog while waiting for an actual walking crossing"));
                    }
                    if (Stop.Action == ENorthJourneyAction::Dialogue && !Tower.State.HasDialogue(static_cast<EDuelNorthDialogue>(Stop.Value))) return Fail(TEXT("Dialogue did not persist on the last F"));
                    if (Stop.Action == ENorthJourneyAction::Clue && !Tower.State.HasClue(static_cast<EDuelNorthClue>(Stop.Value))) return Fail(TEXT("F did not read the authored clue"));
                    if (Stop.Marker == TEXT("OpeningDocket") && (Tower.State.IsOpeningCellOpen() || Tower.IsGateOpen(TEXT("CellDoor"))))
                        return Fail(TEXT("Reading the docket opened the cell without a separate door interaction"));
                    if (Stop.Action == ENorthJourneyAction::Rest && Tower.State.Checkpoint != Stop.Value) return Fail(TEXT("F did not activate the reached rest point"));
                    if (Stop.Action == ENorthJourneyAction::Relic && (!Tower.State.HasRelic(static_cast<EDuelNorthRelic>(Stop.Value)) ||
                        Tower.State.IsDoorOpen(static_cast<EDuelNorthDoor>(Stop.Value)))) return Fail(TEXT("Picking a relic did not persist or silently opened its later lock"));
                    if (Stop.Action == ENorthJourneyAction::Door)
                    {
                        const bool bOpened = Stop.Marker == TEXT("CellDoor") ? Tower.State.IsOpeningCellOpen() :
                            Tower.State.IsDoorOpen(static_cast<EDuelNorthDoor>(Stop.Value));
                        if (!bOpened || !Tower.IsGateOpen(Stop.Marker))
                            return Fail(TEXT("F did not open the physically approached locked door"));
                    }
                    if (Stop.Action == ENorthJourneyAction::Shortcut)
                    {
                        const auto* Lever = FindShortcutLever(Stop.Marker);
                        if (!Lever || !CanReachShortcutLever(*Lever, Player.GetActorLocation(), Player.GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) ||
                            !Tower.State.IsShortcutOpen(static_cast<EDuelNorthShortcut>(Stop.Value)) || !Tower.IsGateOpen(Lever->Portal))
                            return Fail(TEXT("Actual one-sided F did not persist and open its shortcut"));
                        auto* Snapshot = NewObject<UDuelNorthSave>(); Snapshot->State = Tower.State;
                        TArray<uint8> Bytes;
                        if (!UGameplayStatics::SaveGameToMemory(Snapshot, Bytes)) return Fail(TEXT("Shortcut checkpoint memory serialization failed"));
                        const auto* Restored = Cast<UDuelNorthSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
                        if (!Restored || Restored->State.Shortcuts != Tower.State.Shortcuts)
                            return Fail(TEXT("Shortcut bits did not survive the actual SaveGame format"));
                    }
                    if (Stop.Action == ENorthJourneyAction::Relic && Stop.Value == 0 && !Tower.Objective().Contains(TEXT("回升")))
                        return Fail(TEXT("B1 rust-key objective did not point to the still-closed return lever"));
                    if (Stop.Action == ENorthJourneyAction::Ending)
                    {
                        if (!Tower.State.bEndingSeen || !Tower.Narrative()->EndingVisible()) return Fail(TEXT("GardenEdge F did not open the deliberate ending"));
                        if (!Tap(EKeys::M) || Tower.MapVisible()) return Fail(TEXT("M overrode the active ending page"));
                        if (!Tap(EKeys::F) || Tower.Narrative()->EndingVisible()) return Fail(TEXT("F did not close the ending page back to walking"));
                    }
                    if (Stop.Action == ENorthJourneyAction::Rest || Stop.Action == ENorthJourneyAction::Relic)
                    {
                        if (!Tap(EKeys::M) || !Tower.MapVisible() || Mode->CanExplore()) return Fail(TEXT("M did not open the map and block walking after a milestone"));
                        if (!Tap(EKeys::M) || Tower.MapVisible() || !Mode->CanExplore()) return Fail(TEXT("Closing the map failed to restore journey controls"));
                    }
                    FString Reason;
                    if (!Tower.State.Validate(Reason)) return Fail(TEXT("Story became inconsistent after F: ") + Reason);
                    Record(TEXT("F_MILESTONE_COMPLETE"), Stop.Marker);
                    UE_LOG(LogTemp, Display, TEXT("NorthJourney %s: F %s complete, MainWalk=%d, fixed frame=%lld"), *RouteName, *Stop.Marker.ToString(), Stop.AuthoredIndex, Frames);
                }
                for (uint8 Index = 0; Index < static_cast<uint8>(EDuelNorthDialogue::CellEcho); ++Index)
                    if (Index != static_cast<uint8>(EDuelNorthDialogue::Garden) && !Tower.State.HasDialogue(static_cast<EDuelNorthDialogue>(Index)))
                        return Fail(TEXT("Final journey missed an authored conversation"));
                if (!Tower.State.HasFinalConversation() || Tower.State.HasDialogue(EDuelNorthDialogue::Garden))
                    return Fail(TEXT("The final reunion must happen exactly once, in the court"));
                constexpr uint8 MainFloors = (1u << static_cast<uint8>(EDuelNorthFloor::Beacon)) - 1u;
                if (!bExitedCell || !Tower.State.IsOpeningCellOpen() || !Tower.State.HasClue(EDuelNorthClue::OpeningDocket) ||
                    !Tower.State.bPrisonRestActivated || Battles != 3 || Tower.State.Wins != 7 || Tower.State.Relics != 7 || Tower.State.Doors != 7 ||
                    Tower.State.Clues != 127 || Tower.State.VisitedCheckpoints != 31 ||
                    (Tower.State.ExploredFloors & MainFloors) != MainFloors || !Tower.State.HasBeacon(TEXT("BeaconSwitch")) ||
                    Tower.State.Shortcuts != 3 || !Tower.State.bEndingSeen || !Tower.State.bCompanionFollowing)
                    return Fail(TEXT("Final journey is missing a battle, physical pickup, lock, main-route clue or the ending"));
                const auto* Companion = Tower.Narrative()->Companion();
                if (!Companion || Companion->IsHidden() || FVector::DistSquared2D(Companion->GetActorLocation(), Player.GetActorLocation()) > FMath::Square(500.f))
                    return Fail(TEXT("Recovered companion did not physically follow the final garden walk"));
                if (bExtraction && !RunExtraction(*Companion)) return false;
                ++RouteCompletions; Record(TEXT("ROUTE_COMPLETE"), TEXT("GardenEdge"));
                bRouteStarted = false;
            }
            return true;
        };

        if (Fight.IsSet())
        {
            const double Now = Mode->GetWorld()->GetTimeSeconds();
            Fight->Observe(*Mode, Battles + 1);
            if (Mode->Outcome() == EDuelOutcome::Fighting && Now - Fight->Started <= FDuelLiveCombat::TimeLimit)
            {
                Fight->Drive(*Mode, Now, Battles + 1);
                bYieldingToWorld = true;
                return EJourneyUpdate::Fighting;
            }
            const auto& Boss = *Mode->BossFighter;
            const auto Relic = static_cast<EDuelNorthRelic>(Battles);
            const bool bWon = Mode->Outcome() == EDuelOutcome::Victory && !Boss.IsAlive() && Boss.bCombatOver &&
                Fight->Phases == 7 && Fight->Hits >= 3 && Fight->Moves.Num() >= 3 && Tower.State.HasWon(Battles) &&
                Tower.State.RelicAvailable(Relic) && !Tower.State.HasRelic(Relic) &&
                !Tower.State.IsDoorOpen(static_cast<EDuelNorthDoor>(Battles));
            Fights.Add(FString::Printf(TEXT("%s,%d,%d,%s,%d,%.5f,%u,%d,%d,%d,%d,%.2f,%.2f"), *RouteName, Battles + 1,
                int32(Boss.SelectedCharacter()), bWon ? TEXT("WIN") : TEXT("FAIL"), FirstFightSeed + Battles, Now - Fight->Started,
                uint32(Fight->Phases), Fight->Moves.Num(), Fight->Hits, Fight->Dodged, Fight->Parried, Player.Health, Boss.Health));
            if (!bWon)
            {
                Fail(FString::Printf(TEXT("%s battle %d did not reach a real three-phase victory with an unclaimed relic: %.1f s, phase mask=%u, boss HP=%.1f, player HP=%.1f, moves=%d, damage frames=%d"),
                    *RouteName, Battles + 1, Now - Fight->Started, uint32(Fight->Phases), Boss.Health, Player.Health, Fight->Moves.Num(), Fight->Hits));
                return EJourneyUpdate::Failed;
            }
            Record(TEXT("LIVE_FIGHT_WON"), FogIds[Battles]);
            if (!CloseBattleDialogue(Battles))
            {
                Fail(TEXT("Escape did not resume the live journey after the boss dialogue"));
                return EJourneyUpdate::Failed;
            }
            Fight.Reset();
            ++Battles; ++LiveVictories;
            // WalkTo will recompute the remaining segment from the actual victory position.
        }
        else if (Mode->Outcome() == EDuelOutcome::Fighting)
        {
            Fail(TEXT("Unexpected active encounter without a logged fog crossing"));
            return EJourneyUpdate::Failed;
        }
        if (!AdvanceWalking())
        {
            bYieldingToWorld = Fight.IsSet();
            return bYieldingToWorld ? EJourneyUpdate::Fighting : EJourneyUpdate::Failed;
        }
        if (bLive && (LiveVictories != 3 || Injections != 0))
        {
            Fail(TEXT("A live journey must contain exactly three genuine victories and zero injected wins"));
            return EJourneyUpdate::Failed;
        }
        bPassed = true;
        Failure = bLive ? TEXT("The protagonist followed the authored path, won all three live fights and reached the deliberate ending.") :
            TEXT("Both protagonists completed the authored path and ending with six explicitly injected victories.");
        if (bExtraction) Failure += TEXT(" The coastal radio, rope ladder, optional ending and return were operated with F.");
        AddInfo(FString::Printf(TEXT("North Journey PASS: %d routes, %d live victories, %d injected victories, %d F presses, %d waypoints, %lld walking logic frames / %.3f simulated seconds. Evidence: %s"),
            RouteCompletions, LiveVictories, Injections, FPresses, Visited, Frames, Frames * double(Step), *OutputDir));
        return EJourneyUpdate::Complete;
    };
    if (bLive)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Advance = MoveTemp(Advance)]() mutable
        {
            return Advance() != EJourneyUpdate::Fighting;
        }));
        return true;
    }
    return Advance() == EJourneyUpdate::Complete;
}

// Optional developer-only fixture. All motion runs on ordinary world ticks.
namespace
{
    struct FNorthStressPoint
    {
        FVector At;
        FName Core;
        int32 Repeat = 0; // 0 = connecting MainWalk, 1..3 = scored round trip.
        bool bSprint = false;
        int32 Flight = INDEX_NONE;
        int32 Direction = 0; // +1 up, -1 down.
    };
    struct FNorthStressRun
    {
        TArray<FNorthStressPoint> Plan;
        TArray<FString> BuildRows{TEXT("file,bytes,md5,last_write_utc")};
        TArray<FString> PlannedRows{TEXT("point,core,repeat,mode,flight,direction,x,y,z")};
        TArray<FString> Events{TEXT("point,core,repeat,mode,flight,direction,start_world,end_world,frames,min_fraction,max_fraction,sprint_frames,fast_frames,max_speed2d,result")};
        TArray<FString> Samples{TEXT("world_seconds,point,repeat,mode,flight,x,y,feet_z,speed2d,sprint,stair")};
        int32 Counts[21][2][2] = {}; // flight, walk/sprint, down/up
        int32 Point = 0, Frames = 0, SprintFrames = 0, FastFrames = 0, WalkFrames = 0;
        int32 Falling = 0, MaxFalling = 0, Penetrating = 0;
        double MinFraction = 1, MaxFraction = 0, MaxSpeed = 0;
        double Started = 0, SegmentStarted = 0, LastWorld = 0, NextSample = 0;
        double BestDistance = TNumericLimits<double>::Max(), LastProgress = 0;
        double ShiftDue = 0, DistanceCm = 0, StartedWall = 0, LastAdvanceWall = 0;
        FVector Previous = FVector::ZeroVector;
        bool bShiftHeld = false, bFinished = false, bCancelled = false;
        FString Output;
        TFunction<void(bool, const FString&)> Finish;
        ~FNorthStressRun() { if (!bFinished && Finish) { bCancelled = true; Finish(false, TEXT("Latent command cancelled before completion")); } }
    };

    bool MakeNorthStressPlan(const DuelNorthLayout::FRoute& Main, TArray<FNorthStressPoint>& Plan, FString& Error)
    {
        using namespace DuelNorthLayout;
        TSet<FName> SeenCores;
        const auto Append = [&](const FVector& At, FName Core, int32 Repeat, bool Sprint)
        {
            FNorthStressPoint Next{At, Core, Repeat, Sprint};
            if (Repeat && !Plan.IsEmpty()) for (int32 I = 0; I < Stairs().Num(); ++I)
            {
                const auto& Flight = Stairs()[I];
                const FVector Previous = Plan.Last().At;
                if (Previous.Equals(Flight.Start, .1) && At.Equals(Flight.End, .1)) { Next.Flight = I; Next.Direction = 1; }
                if (Previous.Equals(Flight.End, .1) && At.Equals(Flight.Start, .1)) { Next.Flight = I; Next.Direction = -1; }
            }
            Plan.Add(Next);
        };
        for (int32 I = 0; I < Main.Points.Num(); ++I)
        {
            Append(Main.Points[I], NAME_None, 0, false);
            for (const auto& Local : Routes())
            {
                if (Local.Id == Main.Id || Local.Points.Num() < 4 || SeenCores.Contains(Local.Id)) continue;
                for (int32 Reverse = 0; Reverse < 2; ++Reverse)
                {
                    bool Match = I + Local.Points.Num() <= Main.Points.Num();
                    for (int32 J = 0; Match && J < Local.Points.Num(); ++J)
                        Match = Main.Points[I + J].Equals(Local.Points[Reverse ? Local.Points.Num() - 1 - J : J], .1);
                    if (!Match) continue;
                    SeenCores.Add(Local.Id);
                    for (int32 Mode = 0; Mode < 2; ++Mode) for (int32 Repeat = 1; Repeat <= 3; ++Repeat)
                    {
                        // Start/end are the real landing points 150 cm outside the flight.
                        // Leave room for normal acceleration, braking and a physical U-turn.
                        for (int32 Leg = 0; Leg < 2; ++Leg) for (int32 J = 0; J < Local.Points.Num(); ++J)
                        {
                            const bool Backward = (Reverse != 0) != (Leg != 0);
                            Append(Local.Points[Backward ? Local.Points.Num() - 1 - J : J], Local.Id, Repeat, Mode != 0);
                        }
                    }
                    // We are back at the entry landing. MainWalk itself now crosses
                    // this core once and supplies the real connection to the next core.
                    break;
                }
            }
        }
        if (SeenCores.Num() != 5) { Error = TEXT("MainWalk no longer contains all five exact stair-core paths"); return false; }
        int32 Expected[21][2][2] = {};
        for (const auto& Point : Plan) if (Point.Flight != INDEX_NONE)
            ++Expected[Point.Flight][Point.bSprint ? 1 : 0][Point.Direction > 0 ? 1 : 0];
        for (int32 I = 0; I < 21; ++I) for (int32 M = 0; M < 2; ++M) for (int32 D = 0; D < 2; ++D)
            if (Expected[I][M][D] != 3) { Error = TEXT("Stress plan must contain exactly three scored crossings per flight/mode/direction"); return false; }
        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthStairStressTest, "ManosabaDuel.NorthTower.StairStress",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthStairStressTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace DuelNorthLayout;
    FString UserDir;
    const TCHAR* Cmd = FCommandLine::Get();
    if (!FParse::Param(Cmd, TEXT("DuelNorthStairStress")))
    {
        AddInfo(TEXT("StairStress skipped: run explicitly with -DuelNorthStairStress -UseFixedTimeStep -FPS=60."));
        return true;
    }
    if (!TestTrue(TEXT("Stair stress is explicit, isolated and save-free"),
        FParse::Param(Cmd, TEXT("DuelNorthStairStress")) && FParse::Param(Cmd, TEXT("DuelNorthTower")) &&
        FParse::Param(Cmd, TEXT("DuelNoSave")) && !FParse::Param(Cmd, TEXT("DuelNorthSaveTest")) &&
        FParse::Value(Cmd, TEXT("userdir="), UserDir) && !UserDir.IsEmpty() && !FPaths::IsRelative(UserDir))) return false;
    if (!TestTrue(TEXT("Stair stress requires ordinary world frames fixed at 60 Hz"),
        FApp::UseFixedTimeStep() && FMath::Abs(FApp::GetFixedDeltaTime() - 1. / 60.) < .00001)) return false;
    ADuelMode* Mode = nullptr;
    if (GEngine) for (const auto& Context : GEngine->GetWorldContexts())
        if (Context.World() && (Mode = Context.World()->GetAuthGameMode<ADuelMode>())) break;
    if (!TestTrue(TEXT("Actual idle North Tower world exists"), Mode && Mode->NorthTowerActor && Mode->PlayerFighter &&
        Mode->BossFighter && Mode->Arena && (Mode->Result == EDuelOutcome::Ready || Mode->Result == EDuelOutcome::Title))) return false;
    auto& Player = *Mode->PlayerFighter;
    auto& Tower = *Mode->NorthTowerActor;
    auto* PC = Cast<APlayerController>(Player.GetController());
    const auto* Main = FindRoute(TEXT("MainWalk"));
    if (!TestTrue(TEXT("All 21 flights and bound player inputs exist"), Stairs().Num() == 21 && Main && PC &&
        PC->PlayerInput && Player.InputComponent && Player.IsActorTickEnabled() && Player.IsAlive() && Player.Move == EDuelMove::Idle)) return false;
    auto Run = MakeShared<FNorthStressRun>();
    FString PlanError;
    if (!MakeNorthStressPlan(*Main, Run->Plan, PlanError)) { AddError(PlanError); return false; }
    Run->Output = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("NorthTower200/StairStressQA") /
        FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")));
    if (!TestTrue(TEXT("Separate evidence folder is writable"), IFileManager::Get().MakeDirectory(*Run->Output, true))) return false;
    for (const TCHAR* Relative : {TEXT("Binaries/Win64/UnrealEditor-ManosabaDuel.dll"), TEXT("Source/ManosabaDuel/DuelNorthWalkQA.cpp"),
        TEXT("Source/ManosabaDuel/DuelNorthLayout.cpp"), TEXT("Source/ManosabaDuel/DuelFighter.cpp"), TEXT("Config/DefaultInput.ini")})
    {
        const FString File = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / Relative);
        Run->BuildRows.Add(FString::Printf(TEXT("%s,%lld,%s,%s"), Relative, IFileManager::Get().FileSize(*File),
            *LexToString(FMD5Hash::HashFile(*File)), *IFileManager::Get().GetTimeStamp(*File).ToIso8601()));
    }
    for (int32 I = 0; I < Run->Plan.Num(); ++I)
    {
        const auto& Point = Run->Plan[I];
        Run->PlannedRows.Add(FString::Printf(TEXT("%d,%s,%d,%s,%d,%d,%.3f,%.3f,%.3f"), I, *Point.Core.ToString(),
            Point.Repeat, Point.bSprint ? TEXT("sprint") : TEXT("walk"), Point.Flight, Point.Direction, Point.At.X, Point.At.Y, Point.At.Z));
    }

    const auto State = Tower.State; const auto Rooms = Tower.Explored; const auto Gates = Tower.OpenGates;
    const auto Location = Tower.Location; const auto Nearby = Tower.Nearby; const auto Notice = Tower.Notice;
    const auto NoticeUntil = Tower.NoticeUntil; const int32 Fight = Tower.ActiveFight;
    const bool Map = Tower.bMap, GateOpen = Mode->bGateOpen, BossCollision = Mode->BossFighter->GetActorEnableCollision();
    const auto Outcome = Mode->Result; const int32 FigureView = Mode->FigureView;
    const auto Transform = Player.GetActorTransform(); const auto Origin = Mode->Arena->CourtOrigin();
    const auto Velocity = Player.GetCharacterMovement()->Velocity;
    const auto MovementMode = Player.GetCharacterMovement()->MovementMode;
    const auto CustomMovement = Player.GetCharacterMovement()->CustomMovementMode;
    const float WalkSpeed = Player.GetCharacterMovement()->MaxWalkSpeed;
    const float Forward = Player.ForwardAxis, Right = Player.RightAxis, Yaw = Player.ViewYaw, Pitch = Player.ViewPitch;
    const float Stamina = Player.Stamina, Recovery = Player.RecoveryDelay;
    const double ShiftAt = Player.ShiftPressedAt; const FVector ShiftDirection = Player.ShiftDirection;
    const bool SprintCommitted = Player.bShiftCommittedToSprint;
    const auto QueuedMove = Player.QueuedMove; const float MoveAge = Player.MoveAge;
    const bool SprintExhausted = Player.bSprintExhausted, CombatOver = Player.bCombatOver, Lock = Player.bLockOn;
    const bool Paused = UGameplayStatics::IsGamePaused(Mode), ControllerTick = PC->IsActorTickEnabled();
    const TWeakObjectPtr<ADuelMode> WeakMode(Mode);
    const TWeakObjectPtr<APlayerController> WeakPC(PC);
    FNorthStressRun* const R = &Run.Get();
    Run->Finish = [this, R, WeakMode, WeakPC, State, Rooms, Gates, Location, Nearby, Notice, NoticeUntil, Fight, Map,
        GateOpen, BossCollision, Outcome, FigureView, Transform, Origin, Velocity, MovementMode, CustomMovement, WalkSpeed,
        Forward, Right, Yaw, Pitch, Stamina, Recovery, ShiftAt, ShiftDirection, SprintCommitted, QueuedMove, MoveAge, SprintExhausted, CombatOver, Lock, Paused, ControllerTick]
        (bool Success, const FString& Why)
    {
        if (R->bFinished) return;
        R->bFinished = true;
        if (!Success && !R->bCancelled) AddError(Why);
        if (auto* Controller = WeakPC.Get()) { Controller->FlushPressedKeys(); Controller->SetActorTickEnabled(ControllerTick); }
        if (auto* M = WeakMode.Get(); M && M->PlayerFighter && M->NorthTowerActor && M->Arena && M->BossFighter)
        {
            auto& P = *M->PlayerFighter; auto& T = *M->NorthTowerActor; auto& Motion = *P.GetCharacterMovement();
            P.ForwardAxis = P.RightAxis = 0; P.ConsumeMovementInputVector(); Motion.StopMovementImmediately();
            T.State = State; T.Explored = Rooms; T.Location = Location; T.Nearby = Nearby;
            T.Notice = Notice; T.NoticeUntil = NoticeUntil; T.ActiveFight = Fight; T.bMap = Map;
            for (const auto& Portal : Portals()) T.SetGateOpen(Portal.Id, Gates.Contains(Portal.Id));
            M->Result = Outcome; M->bGateOpen = GateOpen; M->FigureView = FigureView;
            M->BossFighter->SetActorEnableCollision(BossCollision);
            P.SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
            Motion.SetMovementMode(MovementMode, CustomMovement); Motion.Velocity = Velocity; Motion.MaxWalkSpeed = WalkSpeed;
            P.ForwardAxis = Forward; P.RightAxis = Right; P.ViewYaw = Yaw; P.ViewPitch = Pitch;
            P.Stamina = Stamina; P.RecoveryDelay = Recovery; P.ShiftPressedAt = ShiftAt; P.ShiftDirection = ShiftDirection;
            P.bShiftCommittedToSprint = SprintCommitted; P.Move = EDuelMove::Idle; P.QueuedMove = QueuedMove; P.MoveAge = MoveAge;
            P.bSprintExhausted = SprintExhausted; P.bCombatOver = CombatOver; P.bLockOn = Lock;
            M->Arena->SetNorthOrigin(Origin);
            if (T.NarrativeActor) T.NarrativeActor->Restore(T.State, P, *M->Arena);
            UGameplayStatics::SetGamePaused(M, Paused);
        }
        TArray<FString> Counts{TEXT("flight,walk_down,walk_up,sprint_down,sprint_up")};
        for (int32 I = 0; I < 21; ++I) Counts.Add(FString::Printf(TEXT("%s,%d,%d,%d,%d"),
            *Stairs()[I].Id.ToString(), R->Counts[I][0][0], R->Counts[I][0][1], R->Counts[I][1][0], R->Counts[I][1][1]));
        const FString Summary = FString::Printf(TEXT("Result: %s\n%s\nCompleted plan points: %d/%d. Distance: %.2f m. World elapsed: %.3f s.\n")
            TEXT("Required: 21 flights x 2 directions x walk/sprint x 3 = 252 separately scored crossings.\n")
            TEXT("Real world ticks and production locomotion; Shift through actual InputKey binding; no backdated Shift timestamp. Controller automatic tick suspended only to prevent its empty input axes overwriting the fixture axes; restored on exit. MainWalk connections and real landing U-turns retained. Gates temporarily open, combat disabled. No movement-speed edits, intermediate teleports or forced stamina refill.\n")
            TEXT("This is developer geometry/locomotion QA, not exact Shipping input, performance, companion-following, camera or door-unlock validation. The required fixed timestep is simulation timing only.\n"),
            Success ? TEXT("PASS") : TEXT("FAIL"), *Why, R->Point, R->Plan.Num(), R->DistanceCm / 100., R->LastWorld - R->Started);
        bool Written = FFileHelper::SaveStringToFile(Summary, *(R->Output / TEXT("README.md")));
        Written &= FFileHelper::SaveStringArrayToFile(Counts, *(R->Output / TEXT("crossing-counts.csv")));
        Written &= FFileHelper::SaveStringArrayToFile(R->Events, *(R->Output / TEXT("crossings.csv")));
        Written &= FFileHelper::SaveStringArrayToFile(R->Samples, *(R->Output / TEXT("samples-10hz.csv")));
        Written &= FFileHelper::SaveStringArrayToFile(R->BuildRows, *(R->Output / TEXT("build-and-config.csv")));
        Written &= FFileHelper::SaveStringArrayToFile(R->PlannedRows, *(R->Output / TEXT("planned-route.csv")));
        if (!Written && !R->bCancelled) AddError(TEXT("StairStress evidence write failed"));
        if (!R->bCancelled) AddInfo(FString::Printf(TEXT("North StairStress %s: %s. Evidence: %s"), Success ? TEXT("PASS") : TEXT("FAIL"), *Why, *R->Output));
    };

    UGameplayStatics::SetGamePaused(Mode, false); PC->FlushPressedKeys(); PC->SetActorTickEnabled(false);
    Mode->Result = EDuelOutcome::Ready; Mode->bGateOpen = false; Mode->FigureView = 0;
    Tower.ActiveFight = INDEX_NONE; Tower.bMap = false;
    for (const auto& Portal : Portals()) Tower.SetGateOpen(Portal.Id, true);
    Mode->BossFighter->SetActorEnableCollision(false);
    Player.EndCombat(); Player.PrepareExploration(); Player.ForwardAxis = Player.RightAxis = 0;
    Player.ShiftPressedAt = -1; Player.bSprintExhausted = Player.bShiftCommittedToSprint = false; Player.ConsumeMovementInputVector();
    Player.GetCharacterMovement()->StopMovementImmediately(); Player.GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    const float Half = Player.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    Player.SetActorLocation(Run->Plan[0].At + FVector(0, 0, Half + 2), false, nullptr, ETeleportType::TeleportPhysics);
    Run->StartedWall = Run->LastAdvanceWall = FPlatformTime::Seconds();
    Run->Previous = Player.GetActorLocation();
    Run->Started = Run->SegmentStarted = Run->LastProgress = Run->LastWorld = Mode->GetWorld()->GetTimeSeconds();

    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run, WeakMode, WeakPC, Half]() mutable
    {
        auto* M = WeakMode.Get(); auto* Controller = WeakPC.Get();
        if (!M || !Controller || !M->PlayerFighter || !M->NorthTowerActor) { Run->Finish(false, TEXT("World/controller unloaded")); return true; }
        auto& P = *M->PlayerFighter; auto& Motion = *P.GetCharacterMovement();
        const double Now = M->GetWorld()->GetTimeSeconds(), Dt = Now - Run->LastWorld;
        const double WallNow = FPlatformTime::Seconds();
        if (WallNow - Run->StartedWall > 900 || WallNow - Run->LastAdvanceWall > 15)
        { Run->Finish(false, TEXT("900-second wall-clock budget or 15-second world-clock stall exceeded")); return true; }
        if (Dt <= 0) return false;
        Run->LastAdvanceWall = WallNow; Run->LastWorld = Now;
        if (Now - Run->Started > 2700 || FMath::Abs(Dt - 1. / 60.) > .002)
        { Run->Finish(false, TEXT("World-time budget exceeded or observation was not one fixed 60 Hz world frame")); return true; }
        if (Run->Point >= Run->Plan.Num())
        {
            for (int32 I = 0; I < 21; ++I) for (int32 Mode = 0; Mode < 2; ++Mode) for (int32 D = 0; D < 2; ++D)
                if (Run->Counts[I][Mode][D] != 3) { Run->Finish(false, TEXT("At least one flight/mode/direction lacks three proven crossings")); return true; }
            Run->Finish(true, TEXT("All 252 separately scored stair crossings completed")); return true;
        }
        const auto& Target = Run->Plan[Run->Point];
        const FVector At = P.GetActorLocation(), Feet = At - FVector(0, 0, Half), Offset = Target.At - Feet;
        const FVector SegmentStart = Run->Point ? Run->Plan[Run->Point - 1].At : Target.At;
        const FVector Segment = Target.At - SegmentStart;
        const double Along = Segment.SizeSquared2D() > 1 ? FMath::Clamp(FVector::DotProduct(Feet - SegmentStart,
            FVector(Segment.X, Segment.Y, 0)) / Segment.SizeSquared2D(), 0., 1.) : 1.;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(NorthStairStress), false, &P); Query.AddIgnoredActor(M->BossFighter);
        const FString Error = CheckNorthWalkStep(P, Run->Previous, At, FMath::Lerp(SegmentStart.Z, Target.At.Z, Along), Query,
            Run->Falling, Run->MaxFalling, Run->Penetrating);
        Run->DistanceCm += FVector::Dist(At, Run->Previous); Run->Previous = At;
        if (!Error.IsEmpty()) { Run->Finish(false, FString::Printf(TEXT("Point %d: %s"), Run->Point, *Error)); return true; }
        if (M->Outcome() != EDuelOutcome::Ready || !M->CanExplore() || P.Move != EDuelMove::Idle)
        { Run->Finish(false, TEXT("Unexpected combat, roll, pause or narrative interruption")); return true; }

        if (Target.bSprint != Run->bShiftHeld)
        {
            if (!Target.bSprint && !P.bShiftCommittedToSprint)
            { Run->Finish(false, TEXT("Refusing Shift release before production code actually committed to sprint")); return true; }
            Controller->InputKey(FInputKeyParams(EKeys::LeftShift, Target.bSprint ? IE_Pressed : IE_Released, Target.bSprint ? 1. : 0.));
            const TArray<UInputComponent*> Stack{P.InputComponent};
            Controller->PlayerInput->ProcessInputStack(Stack, float(Dt), false);
            Run->bShiftHeld = Target.bSprint; Run->ShiftDue = Now + .10;
            P.ForwardAxis = P.RightAxis = 0;
            return false;
        }
        // Wait for the actual production sprint predicate, not a copied threshold.
        if (Target.bSprint && !P.Sprinting(FVector::ForwardVector))
        {
            P.ForwardAxis = P.RightAxis = 0;
            if (Now - Run->ShiftDue > 1.) { Run->Finish(false, TEXT("Held Shift failed to arm production sprint within one second")); return true; }
            return false;
        }
        if (Now < Run->ShiftDue) { P.ForwardAxis = P.RightAxis = 0; return false; }
        if ((Target.bSprint && P.ShiftPressedAt < 0) || (!Target.bSprint && P.ShiftPressedAt >= 0))
        { Run->Finish(false, TEXT("Bound Shift did not reach the expected held/released state")); return true; }
        const auto Located = Locate(At, Half);
        if (Target.Flight != INDEX_NONE && Located.Stair == Stairs()[Target.Flight].Id)
        {
            const auto& Flight = Stairs()[Target.Flight]; const FVector Axis = (Flight.End - Flight.Start).GetSafeNormal2D();
            const double Fraction = FMath::Clamp(FVector::DotProduct(Feet - Flight.Start, Axis) / FVector::Dist2D(Flight.Start, Flight.End), 0., 1.);
            ++Run->Frames; Run->MinFraction = FMath::Min(Run->MinFraction, Fraction); Run->MaxFraction = FMath::Max(Run->MaxFraction, Fraction);
            const bool Sprint = P.Sprinting(P.MoveInput());
            Run->SprintFrames += Sprint ? 1 : 0;
            const double AlongSpeed = FVector::DotProduct(Motion.Velocity, Axis) * Target.Direction;
            Run->FastFrames += Sprint && P.bShiftCommittedToSprint && Motion.MaxWalkSpeed >= 650 && AlongSpeed > 480 ? 1 : 0;
            Run->WalkFrames += !Sprint && !P.bShiftCommittedToSprint && Motion.MaxWalkSpeed <= 456 ? 1 : 0;
            Run->MaxSpeed = FMath::Max(Run->MaxSpeed, double(Motion.Velocity.Size2D()));
            if (FVector::DotProduct(Motion.Velocity, Axis) * Target.Direction < -20)
            { Run->Finish(false, TEXT("Scored flight reversed direction before reaching its landing")); return true; }
        }
        if (Now >= Run->NextSample)
        {
            Run->NextSample = Now + .1;
            Run->Samples.Add(FString::Printf(TEXT("%.5f,%d,%d,%s,%d,%.3f,%.3f,%.3f,%.3f,%d,%s"), Now, Run->Point,
                Target.Repeat, Target.bSprint ? TEXT("sprint") : TEXT("walk"), Target.Flight, Feet.X, Feet.Y, Feet.Z,
                Motion.Velocity.Size2D(), P.Sprinting(P.MoveInput()) ? 1 : 0, *Located.Stair.ToString()));
        }
        if (Offset.Size2D() <= 12 && FMath::Abs(Offset.Z) <= 28 && !Motion.IsFalling())
        {
            if (Target.Flight != INDEX_NONE)
            {
                const bool Full = Run->Frames >= 5 && Run->MinFraction <= .10 && Run->MaxFraction >= .90;
                const bool ModeOK = Target.bSprint ? Run->SprintFrames == Run->Frames && Run->FastFrames >= 3 :
                    Run->SprintFrames == 0 && Run->WalkFrames == Run->Frames;
                Run->Events.Add(FString::Printf(TEXT("%d,%s,%d,%s,%s,%s,%.5f,%.5f,%d,%.5f,%.5f,%d,%d,%.3f,%s"),
                    Run->Point, *Target.Core.ToString(), Target.Repeat, Target.bSprint ? TEXT("sprint") : TEXT("walk"),
                    *Stairs()[Target.Flight].Id.ToString(), Target.Direction > 0 ? TEXT("up") : TEXT("down"),
                    Run->SegmentStarted, Now, Run->Frames, Run->MinFraction, Run->MaxFraction, Run->SprintFrames,
                    Run->FastFrames, Run->MaxSpeed, Full && ModeOK ? TEXT("PASS") : TEXT("FAIL")));
                if (!Full || !ModeOK) { Run->Finish(false, TEXT("A crossing lacked full extent or actual walk/sprint evidence")); return true; }
                ++Run->Counts[Target.Flight][Target.bSprint ? 1 : 0][Target.Direction > 0 ? 1 : 0];
            }
            ++Run->Point; Run->Frames = Run->SprintFrames = Run->FastFrames = Run->WalkFrames = 0;
            Run->MinFraction = 1; Run->MaxFraction = Run->MaxSpeed = 0;
            Run->SegmentStarted = Run->LastProgress = Now; Run->BestDistance = TNumericLimits<double>::Max();
            if (Run->Point < Run->Plan.Num() && Run->Plan[Run->Point].bSprint == Run->bShiftHeld)
                AimNorthWalk(Run->Plan[Run->Point].At - Feet, P.ForwardAxis, P.RightAxis, P.ViewYaw);
            else P.ForwardAxis = P.RightAxis = 0;
            return false;
        }
        if (Offset.Size() < Run->BestDistance - 4) { Run->BestDistance = Offset.Size(); Run->LastProgress = Now; }
        if (Now - Run->LastProgress > 3 || Now - Run->SegmentStarted > Segment.Size() / 120. + 8.)
        { Run->Finish(false, FString::Printf(TEXT("Stuck at point %d, flight %d, feet %s"), Run->Point, Target.Flight, *Feet.ToString())); return true; }
        AimNorthWalk(Offset, P.ForwardAxis, P.RightAxis, P.ViewYaw);
        return false;
    }));
    return true;
}


#endif
