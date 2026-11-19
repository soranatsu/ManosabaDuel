#include "DuelGame.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "DuelArena.h"
#include "DuelNorthLayout.h"
#include "DuelNorthTower.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputKeyEventArgs.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

namespace
{
    constexpr float NorthCameraStep = 1.f / 60.f;

    struct FNorthCameraSampleCount
    {
        int32 Frames = 0;
        int32 WallSweeps = 0;
        int32 WallRetractions = 0;
        int32 CornerRetractions = 0;
        int32 InvalidViews = 0;
        int32 EyeOverlaps = 0;
        int32 ClearTargetFrames = 0;
        int32 TargetAndGroundInFrame = 0;
        int32 YawWraps = 0;
        double MaxViewYawStep = 0.0;
        double MaxRoll = 0.0;
    };

    bool InsideNorthCamera(const FMinimalViewInfo& View, double Aspect, const FVector& Point)
    {
        const FVector Local = View.Rotation.UnrotateVector(Point - View.Location);
        const double HalfWidth = Local.X * FMath::Tan(FMath::DegreesToRadians(View.FOV * .5));
        return Local.X > 0.0 && FMath::Abs(Local.Y) <= HalfWidth && FMath::Abs(Local.Z) <= HalfWidth / Aspect;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthCombatCameraTest, "ManosabaDuel.NorthTower.CombatCamera",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthCombatCameraTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    const TCHAR* Cmd = FCommandLine::Get();
    if (!FParse::Param(Cmd, TEXT("DuelNorthCombatCamera")))
    {
        AddInfo(TEXT("CombatCamera requires an explicit -DuelNorthCombatCamera run."));
        return true;
    }

    FString UserDir;
    if (!TestTrue(TEXT("Run in a separate profile with -DuelNorthTower -DuelNoTitle -DuelNoSave"),
        FParse::Param(Cmd, TEXT("DuelNorthTower")) && FParse::Param(Cmd, TEXT("DuelNoTitle")) &&
        FParse::Param(Cmd, TEXT("DuelNoSave")) && FParse::Value(Cmd, TEXT("userdir="), UserDir) &&
        !UserDir.IsEmpty() && !FPaths::IsRelative(UserDir))) return false;

    FString ActualUserDir = FPaths::ProjectUserDir();
    FString ProjectDir = FPaths::ProjectDir();
    FPaths::NormalizeDirectoryName(UserDir);
    FPaths::NormalizeDirectoryName(ActualUserDir);
    FPaths::NormalizeDirectoryName(ProjectDir);
    if (!TestTrue(TEXT("Unreal uses the requested isolated user directory"),
        FPaths::IsSamePath(UserDir, ActualUserDir) && !FPaths::IsSamePath(ActualUserDir, ProjectDir))) return false;

    ADuelMode* Mode = nullptr;
    if (GEngine)
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
            if (Context.World() && (Mode = Context.World()->GetAuthGameMode<ADuelMode>())) break;
    if (!TestTrue(TEXT("Idle authored North Tower world"), Mode && Mode->NorthTowerActor && Mode->Arena &&
        Mode->Result == EDuelOutcome::Ready && Mode->PlayerFighter && Mode->BossFighter)) return false;

    auto& Player = *Mode->PlayerFighter;
    auto& Boss = *Mode->BossFighter;
    auto* PC = Cast<APlayerController>(Player.GetController());
    if (!TestTrue(TEXT("Local pawn camera, live target and bound inputs"), PC && PC->PlayerInput &&
        Player.InputComponent && Player.Camera && Player.CameraBoom && Player.CameraBoom->bDoCollisionTest &&
        Player.Target == &Boss && Player.IsAlive() && Boss.IsAlive())) return false;
    auto& Boom = *Player.CameraBoom;
    UWorld& World = *Mode->GetWorld();

    const FTransform PlayerTransform = Player.GetActorTransform(), BossTransform = Boss.GetActorTransform();
    const FVector ArenaOrigin = Mode->Arena->CourtOrigin();
    const bool bLock = Player.bLockOn, bCombatOver = Player.bCombatOver, bFocusReady = Player.bCameraFocusReady;
    const float Yaw = Player.ViewYaw, Pitch = Player.ViewPitch, Impulse = Player.CameraImpulse;
    const float Forward = Player.ForwardAxis, Right = Player.RightAxis, Hazard = Player.HazardFraming;
    const FVector Focus = Player.CameraFocus, TargetOffset = Boom.TargetOffset, SocketOffset = Boom.SocketOffset;
    const FRotator BoomRotation = Boom.GetComponentRotation(), CameraRotation = Player.Camera->GetRelativeRotation();
    const float Arm = Boom.TargetArmLength, LagDistance = Boom.CameraLagMaxDistance, LagSpeed = Boom.CameraLagSpeed;
    const bool bLag = Boom.bEnableCameraLag, bRotationLag = Boom.bEnableCameraRotationLag;
    ON_SCOPE_EXIT
    {
        PC->FlushPressedKeys();
        Mode->Result = EDuelOutcome::Ready;
        Mode->Arena->SetNorthOrigin(ArenaOrigin);
        Player.SetActorTransform(PlayerTransform, false, nullptr, ETeleportType::TeleportPhysics);
        Boss.SetActorTransform(BossTransform, false, nullptr, ETeleportType::TeleportPhysics);
        Player.bLockOn = bLock; Player.bCombatOver = bCombatOver; Player.bCameraFocusReady = bFocusReady;
        Player.ViewYaw = Yaw; Player.ViewPitch = Pitch; Player.CameraImpulse = Impulse; Player.HazardFraming = Hazard;
        Player.ForwardAxis = Forward; Player.RightAxis = Right; Player.CameraFocus = Focus;
        Boom.TargetOffset = TargetOffset; Boom.SocketOffset = SocketOffset; Boom.SetWorldRotation(BoomRotation);
        Boom.TargetArmLength = Arm; Boom.CameraLagMaxDistance = LagDistance; Boom.CameraLagSpeed = LagSpeed;
        Player.Camera->SetRelativeRotation(CameraRotation);
        Boom.bEnableCameraLag = false;
        Boom.bEnableCameraRotationLag = false;
        Boom.TickComponent(NorthCameraStep, LEVELTICK_All, nullptr);
        Boom.bEnableCameraLag = bLag;
        Boom.bEnableCameraRotationLag = bRotationLag;
    };

    PC->FlushPressedKeys();
    Mode->Result = EDuelOutcome::Fighting;
    Player.bCombatOver = false;
    Player.CameraImpulse = 0.f;
    Player.HazardFraming = 0.f;
    TArray<UInputComponent*> InputStack{Player.InputComponent.Get()};
    const auto ProcessInput = [&] { PC->PlayerInput->ProcessInputStack(InputStack, NorthCameraStep, false); };
    const auto PressLock = [&]
    {
        for (const EInputEvent Event : {IE_Pressed, IE_Released})
        {
            PC->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, EKeys::Q, Event,
                Event == IE_Pressed ? 1.f : 0.f, false, 0));
            ProcessInput();
        }
    };
    const auto MoveMouse = [&](float Counts)
    {
        const double PreviousYaw = Player.ViewYaw;
        PC->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, EKeys::MouseX, Counts, NorthCameraStep, 1, 0));
        ProcessInput();
        return FMath::FindDeltaAngleDegrees(PreviousYaw, static_cast<double>(Player.ViewYaw));
    };
    const auto AdvanceCamera = [&]
    {
        Player.TickCamera(NorthCameraStep);
        Boom.TickComponent(NorthCameraStep, LEVELTICK_All, nullptr);
        FMinimalViewInfo View;
        Player.CalcCamera(NorthCameraStep, View);
        return View;
    };
    const auto SettleCamera = [&]
    {
        FMinimalViewInfo View;
        for (int32 Frame = 0; Frame < 60; ++Frame) View = AdvanceCamera();
        return View;
    };

    int32 Width = 0, Height = 0;
    PC->GetViewportSize(Width, Height);
    const double Aspect = Width > 0 && Height > 0 ? static_cast<double>(Width) / Height : Player.Camera->AspectRatio;
    if (!TestTrue(TEXT("A finite camera aspect ratio"), FMath::IsFinite(Aspect) && Aspect >= 1.0)) return false;

    FCollisionQueryParams Query(SCENE_QUERY_STAT(DuelNorthCombatCameraQA), false, &Player);
    Query.AddIgnoredActor(&Boss);
    const auto Record = [&](FNorthCameraSampleCount& Stats, const FMinimalViewInfo& View,
        const FMinimalViewInfo& PreviousView, bool bLocked, bool bEdge, bool bCorner)
    {
        ++Stats.Frames;
        if (View.Location.ContainsNaN() || View.Rotation.ContainsNaN() || !FMath::IsFinite(View.FOV) ||
            View.FOV <= 0.f || View.FOV >= 170.f)
        {
            ++Stats.InvalidViews;
            return;
        }
        Stats.MaxRoll = FMath::Max(Stats.MaxRoll, FMath::Abs(View.Rotation.Roll));
        Stats.MaxViewYawStep = FMath::Max(Stats.MaxViewYawStep,
            FMath::Abs(FMath::FindDeltaAngleDegrees(PreviousView.Rotation.Yaw, View.Rotation.Yaw)));
        Stats.EyeOverlaps += World.OverlapBlockingTestByChannel(View.Location, FQuat::Identity,
            Boom.ProbeChannel, FCollisionShape::MakeSphere(8.f), Query) ? 1 : 0;

        const FVector At = Player.GetActorLocation();
        if (bEdge)
        {
            const FVector Pivot = At + FVector(0, 0, bLocked ? 205.0 : 115.0);
            const double FullArm = bLocked ? 803.0 : 664.0;
            const FVector UnobstructedEye = Pivot + Boom.GetComponentRotation().RotateVector(FVector(-FullArm, 76, SocketOffset.Z));
            FHitResult Hit;
            if (World.SweepSingleByChannel(Hit, Pivot, UnobstructedEye, FQuat::Identity, Boom.ProbeChannel,
                FCollisionShape::MakeSphere(Boom.ProbeSize), Query) && Hit.GetActor() == Mode->NorthTowerActor.Get())
            {
                ++Stats.WallSweeps;
                if (Boom.TargetArmLength < FullArm - 12.0)
                {
                    ++Stats.WallRetractions;
                    Stats.CornerRetractions += bCorner ? 1 : 0;
                }
            }
        }
        if (bLocked)
        {
            const FVector Target = Boss.LockPoint();
            FHitResult Occlusion;
            if (!World.LineTraceSingleByChannel(Occlusion, View.Location, Target, Boom.ProbeChannel, Query))
            {
                ++Stats.ClearTargetFrames;
                const FVector Feet = At - FVector(0, 0, Player.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 14.f);
                Stats.TargetAndGroundInFrame += InsideNorthCamera(View, Aspect, Target) && InsideNorthCamera(View, Aspect, Feet) ? 1 : 0;
            }
        }
    };

    const TCHAR* Rooms[] = {TEXT("DungeonCourt"), TEXT("MirrorCourt"), TEXT("FinalCourt")};
    constexpr double West = DuelCourt::GateCrossX + 12.0, East = DuelCourt::HalfLength - 12.0;
    constexpr double Side = DuelCourt::HalfWidth - 12.0;
    const FVector2D Stations[] = {{0, 0}, {West, 0}, {East, 0}, {0, -Side}, {0, Side},
        {West, -Side}, {West, Side}, {East, -Side}, {East, Side}};

    bool bPassed = true;
    float MouseCounts = 0.f;
    for (int32 Court = 0; Court < UE_ARRAY_COUNT(Rooms); ++Court)
    {
        using namespace DuelNorthLayout;
        const FRoom* Room = FindRoom(FName(Rooms[Court]));
        const FVector Origin = CourtOrigin(Court);
        if (!TestTrue(FString::Printf(TEXT("%s has its authored combat bounds"), Rooms[Court]),
            Room && Room->Kind == ERoomKind::Combat && FMath::IsNearlyEqual(Room->Bounds.Min.Z, Origin.Z))) return false;
        Mode->Arena->SetNorthOrigin(Origin);
        const double PlayerHeight = Player.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.0;
        const double BossHeight = Boss.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.0;
        FNorthCameraSampleCount Free, Locked, CenterLock;
        double MinMouseYaw = 18.0, MaxMouseYaw = 0.0;
        double ShortestTurn = TNumericLimits<double>::Max();
        for (int32 Station = 0; Station < UE_ARRAY_COUNT(Stations); ++Station)
        {
            const FVector2D XY = Stations[Station];
            const bool bEdge = Station != 0;
            const bool bCorner = XY.X != 0.0 && XY.Y != 0.0;
            const FVector At = Origin + FVector(XY.X, XY.Y, PlayerHeight);
            if (!TestTrue(TEXT("Camera station inside authored room"), Room->Bounds.IsInside(At))) return false;
            Player.SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
            if (Player.bLockOn) PressLock();
            if (!TestFalse(TEXT("Q releases lock for the free orbit"), Player.bLockOn)) return false;
            Player.ViewYaw = 170.f;
            Player.ViewPitch = -8.f;
            if (Court == 0 && Station == 0)
            {
                // Unreal scales MouseX by the current view FOV as well as input sensitivity.
                constexpr float ProbeCounts = 8.f;
                const double ProbeYaw = MoveMouse(ProbeCounts);
                const float Axis = PC->PlayerInput->GetKeyValue(EKeys::MouseX);
                const float InputFOV = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetFOVAngle() : 0.f;
                AddInfo(FString::Printf(TEXT("NorthCombatCameraInput probe rawX=%.3f axis=%.6f yawDeg=%.6f cameraManagerFOV=%.3f"),
                    ProbeCounts, Axis, ProbeYaw, InputFOV));
                if (!TestTrue(TEXT("Calibration through bound MouseX produces a finite positive turn"),
                    FMath::IsFinite(ProbeYaw) && ProbeYaw > 0.0 && ProbeYaw <= 18.0)) return false;
                MouseCounts = static_cast<float>(FMath::Clamp(2.0 * ProbeCounts / ProbeYaw, 1.0, 64.0));
            }
            FMinimalViewInfo Previous = SettleCamera();
            double Turn = 0.0;
            for (int32 Frame = 0; Frame < 480 && Turn < 360.0; ++Frame)
            {
                const float LastYaw = Player.ViewYaw;
                const double Delta = MoveMouse(MouseCounts);
                if (!TestTrue(TEXT("Bound MouseX advances yaw by a small positive angle"), Delta > 0.0 && Delta <= 18.0)) return false;
                MinMouseYaw = FMath::Min(MinMouseYaw, Delta);
                MaxMouseYaw = FMath::Max(MaxMouseYaw, Delta);
                Turn += Delta;
                Free.YawWraps += LastYaw > 0.f && Player.ViewYaw < 0.f ? 1 : 0;
                const FMinimalViewInfo View = AdvanceCamera();
                Record(Free, View, Previous, false, bEdge, bCorner);
                Previous = View;
            }
            if (!TestTrue(FString::Printf(TEXT("%s station %d completes 360 degrees of free camera yaw (observed %.3f at rawX %.3f)"),
                Rooms[Court], Station, Turn, MouseCounts), Turn >= 360.0)) return false;
            ShortestTurn = FMath::Min(ShortestTurn, Turn);

            const auto MoveTarget = [&](double Degrees)
            {
                const double Radians = FMath::DegreesToRadians(Degrees);
                Boss.SetActorLocation(Origin + FVector(460.0 * FMath::Cos(Radians), 460.0 * FMath::Sin(Radians), BossHeight),
                    false, nullptr, ETeleportType::TeleportPhysics);
            };
            MoveTarget(160.0);
            PressLock();
            if (!TestTrue(TEXT("Q acquires the real target"), Player.bLockOn)) return false;
            Player.ViewYaw = (Boss.GetActorLocation() - At).Rotation().Yaw;
            Previous = SettleCamera();
            FNorthCameraSampleCount& Stats = bEdge ? Locked : CenterLock;
            for (int32 Frame = 1; Frame <= 96; ++Frame)
            {
                const float LastYaw = Player.ViewYaw;
                const double MouseYaw = MoveMouse(MouseCounts);
                if (!TestTrue(TEXT("Mouse yaw is ignored while locked"), Player.bLockOn && MouseYaw == 0.0)) return false;
                MoveTarget(160.0 + 1.25 * Frame);
                const FMinimalViewInfo View = AdvanceCamera();
                Stats.YawWraps += LastYaw > 175.f && Player.ViewYaw < -175.f ? 1 : 0;
                Record(Stats, View, Previous, true, bEdge, bCorner);
                Previous = View;
            }
        }

        const auto Check = [&](const TCHAR* Name, const FNorthCameraSampleCount& Stats, bool bRequireWalls)
        {
            const FString Label = FString::Printf(TEXT("%s %s"), Rooms[Court], Name);
            AddInfo(FString::Printf(TEXT("NorthCombatCamera %s frames=%d wall=%d retracted=%d cornerRetracted=%d invalid=%d eyeBlock=%d clearTarget=%d targetAndGround=%d wraps=%d maxYawStep=%.2f maxRoll=%.3f"),
                *Label, Stats.Frames, Stats.WallSweeps, Stats.WallRetractions, Stats.CornerRetractions, Stats.InvalidViews, Stats.EyeOverlaps,
                Stats.ClearTargetFrames, Stats.TargetAndGroundInFrame, Stats.YawWraps, Stats.MaxViewYawStep, Stats.MaxRoll));
            bPassed &= TestTrue(Label + TEXT(" actual view is finite, clear of collision and level"),
                Stats.Frames > 0 && Stats.InvalidViews == 0 && Stats.EyeOverlaps == 0 && Stats.MaxRoll < .05);
            bPassed &= TestTrue(Label + TEXT(" has no abrupt rendered yaw jump across the sweep"), Stats.MaxViewYawStep < 35.0);
            if (bRequireWalls)
                bPassed &= TestTrue(Label + TEXT(" retracts at authored wall and corner stations"),
                    Stats.CornerRetractions > 0 && Stats.WallRetractions > Stats.CornerRetractions);
        };
        Check(TEXT("free"), Free, true);
        Check(TEXT("edge lock"), Locked, true);
        Check(TEXT("center lock"), CenterLock, false);
        AddInfo(FString::Printf(TEXT("NorthCombatCameraInput %s rawX=%.3f freeFrames=%d minYawDeg=%.6f maxYawDeg=%.6f shortestOrbitDeg=%.6f"),
            Rooms[Court], MouseCounts, Free.Frames, MinMouseYaw, MaxMouseYaw, ShortestTurn));
        bPassed &= TestTrue(FString::Printf(TEXT("%s free and center lock both cross the signed yaw seam"), Rooms[Court]),
            Free.YawWraps >= UE_ARRAY_COUNT(Stations) && CenterLock.YawWraps > 0);
        bPassed &= TestTrue(FString::Printf(TEXT("%s centered lock keeps visible boss and player feet in frame"), Rooms[Court]),
            CenterLock.ClearTargetFrames == CenterLock.Frames && CenterLock.TargetAndGroundInFrame == CenterLock.Frames);
    }
    return bPassed;
}

#endif
