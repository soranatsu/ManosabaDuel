#include "DuelNorthInvestigationQA.h"
#include "DuelGame.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DuelArena.h"
#include "DuelNorthTower.h"
#include "DuelNorthNarrative.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "DynamicResolutionState.h"
#include "DynamicRHI.h"
#include "RHIStats.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include <cfloat>

// A real-time, single-world workload. It never manually ticks a world, sets a
// fixed delta, captures frames, edits combat parameters, or writes a game save.
// This friend exists only in development builds; fixtures are explicitly logged.
class FDuelNorthPerformanceProbe final
{
    enum class EStage : uint8 { Warmup, Approach, Prime, Measure };
    struct FSample
    {
        uint64 Frame = 0;
        double At = 0, WallMs = 0, World = 0;
        float Delta = 0, GT = -1, RT = -1, RHI = -1, GPU = -1, Unit = -1;
        FVector Position = FVector::ZeroVector;
        float ViewYaw = 0, ViewPitch = 0;
        int32 Segment = 0, Phase = 0, BossMove = 0, PlayerMove = 0, StatIndex = -1;
        bool bStatFresh = false;
    };
    struct FCVarRestore { IConsoleVariable* Var; FString Value; };

    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<ADuelMode> Mode;
    FDelegateHandle TickHandle;
    TArray<FSample> Samples;
    TArray<FString> Events;
    TArray<FCVarRestore> CVars;
    TSet<EDuelMove> Attacks;
    TSet<FName> StairFlights;
    TArray<FVector> Route;
    FString Scenario, Output, Error;
    EStage Stage = EStage::Warmup;
    double Began = 0, LastFrameAt = 0, MeasuredAt = 0, StageAt = 0, LastProgress = 0;
    double NextConfirm = 0, NextAction = 0, NextArt = 0, BestDistance = DBL_MAX;
    float Duration = 35, StartYaw = 0, MinFeet = FLT_MAX, MaxFeet = -FLT_MAX;
    int32 PrimaryPercent = 100;
    bool bAdaptive = false;
    float MinMeasuredPercent = 100.f, MaxMeasuredPercent = 0.f;
    uint64 PeakLocalBytes = 0;
    double NextMemorySample = 0.;
    int32 Fight = 0, RouteIndex = 1, RouteDirection = 1, Traversals = 0, AttackStarts = 0, LastStatIndex = -1;
    int32 Seed = 0, PrimeHits = 0;
    EDuelMove LastMove = EDuelMove::Idle;
    FName Gate;
    bool bFogOpened = false, bCrossed = false, bFinished = false, bWasSmooth = false, bWasUnit = false;

    static TUniquePtr<FDuelNorthPerformanceProbe> Active;

    static FString Number(float V) { return V >= 0 && FMath::IsFinite(V) ? FString::Printf(TEXT("%.6f"), V) : FString(); }
    bool IsBattle() const { return Scenario == TEXT("Fog") || Scenario.EndsWith(TEXT("P3")); }
    void Event(const TCHAR* Name)
    {
        Events.Add(FString::Printf(TEXT("%.6f,%s"), FPlatformTime::Seconds() - Began, Name));
        UE_LOG(LogTemp, Display, TEXT("NorthPerf %s: %s"), *Scenario, Name);
    }
    void SetCVar(const TCHAR* Name, const TCHAR* Value)
    {
        if (auto* Var = IConsoleManager::Get().FindConsoleVariable(Name))
        { CVars.Add({Var, Var->GetString()}); Var->Set(Value, ECVF_SetByCode); }
    }
    void Drive(const FVector& Direction)
    {
        auto& P = *Mode->PlayerFighter;
        const FRotationMatrix Axes(FRotator(0, P.ViewYaw, 0));
        P.ForwardAxis = FVector::DotProduct(Direction, Axes.GetUnitAxis(EAxis::X));
        P.RightAxis = FVector::DotProduct(Direction, Axes.GetUnitAxis(EAxis::Y));
    }
    void BeginMeasure(double Now)
    {
        MeasuredAt = Now; StageAt = Now; Stage = EStage::Measure;
        Event(TEXT("measurement_begin"));
    }
    bool SettingsValid(FString& Reason) const
    {
        if (!World.IsValid() || !Mode.IsValid()) { Reason = TEXT("world_unloaded"); return false; }
        if (FApp::UseFixedTimeStep() || FApp::IsBenchmarking() || GEngine->bUseFixedFrameRate)
        { Reason = TEXT("fixed_timestep_or_benchmarking_enabled"); return false; }
        if (!FMath::IsNearlyEqual(World->GetWorldSettings()->GetEffectiveTimeDilation(), 1.f, .0001f))
        { Reason = TEXT("world_time_dilation_changed"); return false; }
        auto* Viewport = World->GetGameViewport();
        if (!Viewport || !Viewport->Viewport || Viewport->Viewport->GetSizeXY() != FIntPoint(1920, 1080))
        { Reason = TEXT("viewport_is_not_1920x1080"); return false; }
        const auto MatchesScale = [](const TCHAR* Name, float Expected)
        {
            const auto* Var = IConsoleManager::Get().FindConsoleVariable(Name);
            return Var && FMath::IsNearlyEqual(Var->GetFloat(), Expected);
        };
        if (!MatchesScale(TEXT("r.ScreenPercentage"), static_cast<float>(PrimaryPercent)) ||
            !MatchesScale(TEXT("r.SecondaryScreenPercentage.GameViewport"), 100.f))
        { Reason = TEXT("render_screen_percentage_different_from_requested"); return false; }
        if (auto* V = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DynamicRes.OperationMode")); V && V->GetInt() != (bAdaptive ? 2 : 0))
        { Reason = TEXT("unexpected_dynamic_resolution_mode"); return false; }
        return true;
    }
    void Record(double Now, float Delta)
    {
        auto& M = *Mode.Get(); auto& P = *M.PlayerFighter; auto& B = *M.BossFighter;
        FSample S;
        S.Frame = GFrameCounter; S.At = Now - Began; S.WallMs = (Now - LastFrameAt) * 1000.;
        S.World = World->GetTimeSeconds(); S.Delta = Delta; S.Position = P.GetActorLocation();
        S.ViewYaw = P.ViewYaw; S.ViewPitch = P.ViewPitch;
        S.Segment = int32(Stage); S.Phase = B.Phase(); S.BossMove = int32(B.Move); S.PlayerMove = int32(P.Move);
        if (Now >= NextMemorySample && GDynamicRHI)
        {
            FRHIMemoryStats Memory;
            RHIGetMemoryStats(Memory);
            PeakLocalBytes = FMath::Max(PeakLocalBytes, Memory.UsedLocal);
            NextMemorySample = Now + .25;
        }
        if (bAdaptive && Stage == EStage::Measure)
        {
            FDynamicResolutionStateInfos Infos;
            GEngine->GetDynamicResolutionCurrentStateInfos(Infos);
            if (Infos.Status == EDynamicResolutionStatus::Enabled)
            {
                const float Percent = Infos.ResolutionFractionApproximations[GDynamicPrimaryResolutionFraction] * 100.f;
                MinMeasuredPercent = FMath::Min(MinMeasuredPercent, Percent);
                MaxMeasuredPercent = FMath::Max(MaxMeasuredPercent, Percent);
            }
        }
        if (auto* Stats = World->GetGameViewport()->GetStatUnitData())
        {
            S.StatIndex = Stats->CurrentIndex;
            S.bStatFresh = S.StatIndex != LastStatIndex;
            LastStatIndex = S.StatIndex;
            if (S.bStatFresh)
            {
                S.GT = Stats->RawGameThreadTime > 0 ? Stats->RawGameThreadTime : -1;
                S.RT = Stats->RawRenderThreadTime > 0 ? Stats->RawRenderThreadTime : -1;
                S.RHI = Stats->RawRHITTime > 0 ? Stats->RawRHITTime : -1;
                S.GPU = Stats->RawGPUFrameTime[0] > 0 ? Stats->RawGPUFrameTime[0] : -1;
                S.Unit = Stats->RawFrameTime > 0 ? Stats->RawFrameTime : -1;
            }
        }
        Samples.Add(S); LastFrameAt = Now;
    }
    void KeepWorkloadAlive()
    {
        // Explicit performance fixture, not a win-rate/balance test. Actual hit
        // reactions and posture remain active; resources recover between frames.
        auto& P = *Mode->PlayerFighter; auto& B = *Mode->BossFighter;
        P.Health = DuelCourt::PlayerMaxHealth;
        P.Stamina = DuelCourt::MaxStamina; P.Focus = DuelCourt::MaxFocus;
        if (Stage == EStage::Measure && Scenario.EndsWith(TEXT("P3")))
            B.Health = DuelCourt::BossStageHealth[2];
    }
    void FightInput(double Now)
    {
        auto& M = *Mode.Get(); auto& P = *M.PlayerFighter; auto& B = *M.BossFighter;
        const FVector To = B.GetActorLocation() - P.GetActorLocation();
        const FVector Toward = To.GetSafeNormal2D();
        const FVector Side = FVector::CrossProduct(FVector::UpVector, Toward);
        const double T = Now - MeasuredAt;
        const double Wanted = 470. + 220. * FMath::Sin(T * .4);
        FVector Input = (Toward * FMath::Clamp((To.Size2D() - Wanted) / 180., -.7, 1.) + Side * .6).GetClampedToMaxSize(1.);
        const FVector Local = P.GetActorLocation() - M.Arena->CourtOrigin();
        if (FMath::Abs(Local.X) > DuelCourt::HalfLength * .83f || FMath::Abs(Local.Y) > DuelCourt::HalfWidth * .83f)
            Input = (-Local).GetSafeNormal2D();
        Drive(Input);
        if (B.Move != LastMove)
        {
            LastMove = B.Move;
            if (B.Move >= EDuelMove::Paw && B.Move != EDuelMove::Awaken && B.Move != EDuelMove::Eclipse && !B.IsStaggered())
            { Attacks.Add(B.Move); ++AttackStarts; }
        }
        if (Now >= NextAction)
        {
            NextAction = Now + .9;
            if (FMath::Fmod(T, 5.) < 1.8)
            { P.PressShift(); P.ReleaseShift(); }
            else if (To.Size2D() < 350.) P.Cut();
            else if (Now >= NextArt)
            { P.Crimson(); NextArt = Now + 6.; }
        }
    }
    void WalkStairs(double Now)
    {
        auto& P = *Mode->PlayerFighter;
        const float Half = P.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        const FVector Feet = P.GetActorLocation() - FVector(0, 0, Half);
        MinFeet = FMath::Min(MinFeet, float(Feet.Z)); MaxFeet = FMath::Max(MaxFeet, float(Feet.Z));
        const auto Located = DuelNorthLayout::Locate(P.GetActorLocation(), Half);
        if (!Located.Stair.IsNone()) StairFlights.Add(Located.Stair);
        FVector Offset = Route[RouteIndex] - Feet;
        if (Offset.Size2D() < 16 && FMath::Abs(Offset.Z) < 28 && !P.GetCharacterMovement()->IsFalling())
        {
            RouteIndex += RouteDirection; BestDistance = DBL_MAX; LastProgress = Now;
            if (RouteIndex >= Route.Num() || RouteIndex < 0)
            {
                ++Traversals; Event(TEXT("stair_endpoint_reached")); RouteDirection *= -1;
                RouteIndex = RouteIndex < 0 ? 1 : Route.Num() - 2;
            }
            Offset = Route[RouteIndex] - Feet;
        }
        if (Offset.Size() < BestDistance - 4.) { BestDistance = Offset.Size(); LastProgress = Now; }
        if (Now - LastProgress > 8.) { Finish(false, TEXT("stair_route_stuck")); return; }
        const FVector Heading = Offset.GetSafeNormal2D();
        // Camera follows each actual switchback, while movement still uses the
        // production camera-relative axes and CharacterMovement collision.
        P.ViewYaw = FMath::FixedTurn(P.ViewYaw, Heading.Rotation().Yaw, float(FMath::Min(Now - LastCameraAt, .1) * 110.));
        LastCameraAt = Now;
        Drive(Heading * FMath::Clamp(Offset.Size2D() / 85., .18, 1.));
    }
    double LastCameraAt = 0;

    void Tick(UWorld* TickedWorld, ELevelTick Type, float Delta)
    {
        if (bFinished || TickedWorld != World.Get() || Type != LEVELTICK_All) return;
        const double Now = FPlatformTime::Seconds();
        if (!Mode.IsValid() || !Mode->PlayerFighter || !Mode->BossFighter)
        { Finish(false, TEXT("missing_fighter")); return; }
        if (Now - Began > Duration + 100. || Samples.Num() >= 180000)
        { Finish(false, TEXT("probe_time_or_sample_limit")); return; }
        if (Stage != EStage::Warmup && !SettingsValid(Error)) { Finish(false, *Error); return; }
        if (LastFrameAt > 0 && World->GetGameViewport()) Record(Now, Delta); else LastFrameAt = Now;
        auto& M = *Mode.Get(); auto& P = *M.PlayerFighter; auto& B = *M.BossFighter;

        if (Stage == EStage::Warmup)
        {
            if (Now - Began < 5.) return;
            if (!SettingsValid(Error)) { Finish(false, *Error); return; }
            Event(TEXT("warmup_complete")); LastCameraAt = Now; LastProgress = Now;
            if (!IsBattle()) BeginMeasure(Now);
            else
            {
                Stage = EStage::Approach; StageAt = Now;
                if (Scenario == TEXT("Fog")) { MeasuredAt = Now; Event(TEXT("measurement_begin")); }
                Event(TEXT("first_fog_approach"));
            }
        }
        if (Stage == EStage::Approach)
        {
            const auto* Marker = DuelNorthLayout::FindLandmark(Gate);
            if (!Marker) { Finish(false, TEXT("missing_fog_landmark")); return; }
            if (!bFogOpened && M.NorthTowerActor->Nearby == Gate && Now >= NextConfirm)
            { M.Confirm(); NextConfirm = Now + .8; Event(TEXT("confirm_at_fog")); }
            if (!bFogOpened && M.NorthTowerActor->IsGateOpen(Gate))
            { bFogOpened = true; Event(TEXT("fog_opened")); }
            const FVector Goal = bFogOpened ? DuelNorthLayout::CourtOrigin(Fight) + FVector(-1850, 0, 0) : Marker->Position;
            const FVector Offset = Goal - P.GetActorLocation();
            Drive(Offset.Size2D() < 65 || M.NorthTowerActor->BlocksPlayerInput() ? FVector::ZeroVector : Offset.GetSafeNormal2D());
            if (M.Outcome() == EDuelOutcome::Fighting)
            {
                bCrossed = true; Event(TEXT("first_fog_crossed_combat_started"));
                B.BossRandom.Initialize(Seed); P.bLockOn = true; StageAt = Now;
                if (Scenario == TEXT("Fog")) Stage = EStage::Measure;
                else { Stage = EStage::Prime; Event(TEXT("phase_fixture_begin")); }
            }
            else if (Now - StageAt > 25.) { Finish(false, TEXT("fog_admission_timeout")); return; }
        }
        if (Stage == EStage::Prime)
        {
            Drive(FVector::ZeroVector);
            if (M.Outcome() != EDuelOutcome::Fighting) { Finish(false, TEXT("battle_ended_during_fixture")); return; }
            KeepWorkloadAlive();
            if (B.Phase() < 3 && B.Move == EDuelMove::Idle && PrimeHits < B.Phase())
            {
                if (B.ReceiveDuelDamage(DuelCourt::BossStageHealth[B.Phase() - 1] + 1.f, P.GetActorLocation()))
                { ++PrimeHits; Event(TEXT("phase_threshold_damage_fixture")); }
            }
            if (B.Phase() == 3 && B.Move != EDuelMove::Eclipse && B.Move != EDuelMove::Awaken)
            { BeginMeasure(Now); B.BossRandom.Initialize(Seed); }
            else if (Now - StageAt > 40.) { Finish(false, TEXT("phase_three_timeout")); return; }
        }
        if (Stage != EStage::Measure) return;
        if (IsBattle())
        {
            if (M.Outcome() != EDuelOutcome::Fighting) { Finish(false, TEXT("battle_ended_during_measurement")); return; }
            KeepWorkloadAlive(); FightInput(Now);
        }
        else if (Scenario == TEXT("Orbit") || Scenario == TEXT("Archive"))
        {
            Drive(FVector::ZeroVector);
            const double T = Now - MeasuredAt;
            P.ViewYaw = FRotator::NormalizeAxis(StartYaw + float(T * 60.));
            P.ViewPitch = -19.f + 8.f * FMath::Sin(float(T) * .7f);
        }
        else WalkStairs(Now);
        if (bFinished) return;
        if (Now - MeasuredAt >= Duration)
        {
            const bool Covered = !IsBattle() ? (Scenario != TEXT("Stairs") || (Traversals >= 1 && StairFlights.Num() >= 3 && MaxFeet - MinFeet > 1500)) :
                (bCrossed && Attacks.Num() >= 3 && (!Scenario.EndsWith(TEXT("P3")) || B.Phase() == 3));
            Finish(Covered, Covered ? TEXT("complete") : TEXT("insufficient_scenario_coverage"));
        }
    }
    void Finish(bool bPassed, const TCHAR* Reason)
    {
        if (bFinished) return;
        bFinished = true; Event(bPassed ? TEXT("complete") : TEXT("failed"));
        FWorldDelegates::OnWorldPostActorTick.Remove(TickHandle);
        if (Mode.IsValid() && Mode->PlayerFighter) Drive(FVector::ZeroVector);
        FString CSV(TEXT("frame,wall_elapsed_s,wall_frame_ms,world_s,world_delta_s,segment,game_thread_ms,render_thread_ms,rhi_thread_ms,gpu0_ms,unit_frame_ms,stat_fresh,stat_index,player_x_cm,player_y_cm,player_z_cm,camera_yaw_deg,camera_pitch_deg,boss_phase,boss_move,player_move\n"));
        CSV.Reserve(Samples.Num() * 250);
        for (const auto& S : Samples)
            CSV += FString::Printf(TEXT("%llu,%.6f,%.6f,%.6f,%.6f,%d,%s,%s,%s,%s,%s,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d\n"),
                S.Frame, S.At, S.WallMs, S.World, S.Delta, S.Segment, *Number(S.GT), *Number(S.RT), *Number(S.RHI), *Number(S.GPU), *Number(S.Unit),
                S.bStatFresh, S.StatIndex, S.Position.X, S.Position.Y, S.Position.Z, S.ViewYaw, S.ViewPitch, S.Phase, S.BossMove, S.PlayerMove);
        FString Meta = FString::Printf(TEXT("schema=north-performance-1\nscenario=%s\nstatus=%s\nreason=%s\nengine=%s\nwidth=1920\nheight=1080\nduration_requested_s=%.3f\nmeasurement_start_s=%.6f\nsamples=%d\nseed=%d\nattack_types=%d\nattack_starts=%d\nfirst_fog_opened=%d\nfirst_fog_crossed=%d\nstair_traversals=%d\nstair_flights=%d\nstair_height_span_cm=%.3f\nfixed_timestep=false\nreal_rendering=true\nresources_refilled_between_frames=%d\nphase_threshold_fixture_hits=%d\nstat_unit_enabled=true\nrender_offscreen=%d\nasync_compute_disabled_argument=%d\n"),
            *Scenario, bPassed ? TEXT("PASS") : TEXT("FAIL"), Reason, *FEngineVersion::Current().ToString(), Duration,
            MeasuredAt > 0 ? MeasuredAt - Began : -1., Samples.Num(), Seed, Attacks.Num(), AttackStarts, bFogOpened, bCrossed,
            Traversals, StairFlights.Num(), MinFeet < FLT_MAX ? MaxFeet - MinFeet : 0, IsBattle(), PrimeHits,
            FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen")), FParse::Param(FCommandLine::Get(), TEXT("DisableAsyncCompute")));
        Meta += FString::Printf(TEXT("requested_primary_percentage=%d\n"), PrimaryPercent);
        Meta += FString::Printf(TEXT("sampled_peak_local_vram_mib=%.1f\n"), double(PeakLocalBytes) / (1024.*1024.));
        if (bAdaptive) Meta += FString::Printf(TEXT("min_adaptive_percent=%.2f\nmax_adaptive_percent=%.2f\n"), MinMeasuredPercent, MaxMeasuredPercent);
        for (const TCHAR* Name : {TEXT("r.ScreenPercentage"), TEXT("r.SecondaryScreenPercentage.GameViewport"), TEXT("r.RayTracing.Enable"), TEXT("r.DynamicRes.OperationMode"), TEXT("r.VSync"), TEXT("t.MaxFPS"),
            TEXT("t.IdleWhenNotForeground"), TEXT("sg.ViewDistanceQuality"), TEXT("sg.ShadowQuality"), TEXT("sg.EffectsQuality"), TEXT("sg.GlobalIlluminationQuality"), TEXT("sg.ReflectionQuality")})
            if (auto* Var = IConsoleManager::Get().FindConsoleVariable(Name)) Meta += FString::Printf(TEXT("%s=%s\n"), Name, *Var->GetString());
        Meta += FString::Printf(TEXT("cpu_brand=%s\nplatform_primary_gpu=%s\n"), *FPlatformMisc::GetCPUBrand(), *FPlatformMisc::GetPrimaryGPUBrand());
        const bool Written = FFileHelper::SaveStringToFile(CSV, *(Output / TEXT("frames.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) &&
            FFileHelper::SaveStringArrayToFile(Events, *(Output / TEXT("events.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) &&
            FFileHelper::SaveStringToFile(Meta, *(Output / TEXT("run.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        for (const auto& C : CVars) C.Var->Set(*C.Value, ECVF_SetByCode);
        if (GEngine)
        {
            GEngine->bSmoothFrameRate = bWasSmooth;
            if (World.IsValid() && World->GetGameViewport()) GEngine->SetEngineStat(World.Get(), World->GetGameViewport(), TEXT("Unit"), bWasUnit);
        }
        UE_LOG(LogTemp, Display, TEXT("NorthPerf finished: %s %s written=%d samples=%d output=%s"), bPassed ? TEXT("PASS") : TEXT("FAIL"), Reason, Written, Samples.Num(), *Output);
        if (World.IsValid())
        {
            if (bPassed && Scenario == TEXT("Archive") && FParse::Param(FCommandLine::Get(), TEXT("DuelPerfHudShot")))
                FScreenshotRequest::RequestScreenshot(Output / TEXT("ArchiveHUD.png"), false, false);
            FTimerHandle Quit;
            World->GetTimerManager().SetTimer(Quit, [bPassed, Written]() { FPlatformMisc::RequestExitWithStatus(false, bPassed && Written ? 0 : 2); }, .5f, false);
        }
    }

public:
    static void Start(const TArray<FString>& Args, UWorld* W)
    {
        const TCHAR* Cmd = FCommandLine::Get();
        const bool Allowed = W && W->WorldType == EWorldType::Game && FApp::CanEverRender() &&
            FParse::Param(Cmd, TEXT("DuelPerfProbe")) && FParse::Param(Cmd, TEXT("DuelNorthTower")) &&
            FParse::Param(Cmd, TEXT("DuelNoSave")) && !FParse::Param(Cmd, TEXT("DuelNorthSaveTest")) &&
            !FParse::Param(Cmd, TEXT("NullRHI")) && !FApp::UseFixedTimeStep() && !FApp::IsBenchmarking() && GEngine && !GEngine->bUseFixedFrameRate;
        if (!Allowed || Active || Args.Num() < 1 || Args.Num() > 2)
        { UE_LOG(LogTemp, Error, TEXT("NorthPerf refused: requires fresh rendered -game, -DuelPerfProbe -DuelNorthTower -DuelNoSave, no fixed time or save test. Usage: Duel.PerfNorth Orbit|Stairs|Fog|EmaP3|HiroP3|YukiP3 [10..90 seconds]")); return; }
        static const TCHAR* Scenarios[] = {TEXT("Orbit"),TEXT("Archive"),TEXT("Stairs"),TEXT("Fog"),TEXT("EmaP3"),TEXT("HiroP3"),TEXT("YukiP3")};
        if (!MakeArrayView(Scenarios).ContainsByPredicate([&](const TCHAR* V){return Args[0] == V;}))
        { UE_LOG(LogTemp, Error, TEXT("NorthPerf unknown scenario")); return; }
        FString Out, User;
        const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Saved/NorthTower200/Performance"));
        if (!FParse::Value(Cmd, TEXT("DuelPerfOutput="), Out) || !FParse::Value(Cmd, TEXT("userdir="), User))
        { UE_LOG(LogTemp, Error, TEXT("NorthPerf requires explicit fresh output and isolated userdir")); return; }
        Out = FPaths::ConvertRelativePathToFull(Out); User = FPaths::ConvertRelativePathToFull(User);
        FPaths::NormalizeDirectoryName(Out); FPaths::CollapseRelativeDirectories(Out);
        FPaths::NormalizeDirectoryName(User); FPaths::CollapseRelativeDirectories(User);
        if (!FPaths::IsUnderDirectory(Out, Root) || !FPaths::IsUnderDirectory(User, Out) ||
            IFileManager::Get().FileExists(*(Out / TEXT("probe.started"))))
        { UE_LOG(LogTemp, Error, TEXT("NorthPerf refuses nonisolated paths or reuse; use Saved/NorthTower200/Performance/<new-run>/User")); return; }
        auto* M = W->GetAuthGameMode<ADuelMode>();
        if (!M || !M->NorthTowerActor || !M->PlayerFighter || !M->BossFighter || !M->Arena || !W->GetGameViewport())
        { UE_LOG(LogTemp, Error, TEXT("NorthPerf running North Tower world is not ready")); return; }
        const float Seconds = Args.Num() == 2 ? FCString::Atof(*Args[1]) : 35.f;
        if (!FMath::IsFinite(Seconds) || Seconds < 10 || Seconds > 90) return;
        int32 PrimaryPercent = 100;
        FParse::Value(Cmd, TEXT("DuelPerfPrimary="), PrimaryPercent);
        if (PrimaryPercent != 67 && PrimaryPercent != 75 && PrimaryPercent != 100)
        {
            UE_LOG(LogTemp, Error, TEXT("DuelPerfPrimary must be 67, 75 or 100 percent."));
            return;
        }
        if (!IFileManager::Get().MakeDirectory(*Out, true) || !FFileHelper::SaveStringToFile(TEXT("Fresh process only; never reuse this output.\n"), *(Out / TEXT("probe.started")))) return;
        Active = MakeUnique<FDuelNorthPerformanceProbe>();
        auto& Q = *Active; Q.World = W; Q.Mode = M; Q.Output = Out; Q.Scenario = Args[0]; Q.Duration = Seconds;
        Q.PrimaryPercent = PrimaryPercent;
        Q.bAdaptive = FParse::Param(Cmd, TEXT("DuelPerfAdaptive"));
        Q.Fight = Q.Scenario == TEXT("HiroP3") ? 1 : Q.Scenario == TEXT("YukiP3") ? 2 : 0;
        Q.Seed = 20019 + Q.Fight * 1009;
        Q.Gate = Q.Fight == 0 ? TEXT("DungeonGate") : Q.Fight == 1 ? TEXT("MirrorGate") : TEXT("FinalGate");
        Q.Samples.Reserve(90000); Q.Events.Add(TEXT("wall_elapsed_s,event"));
        Q.bWasSmooth = GEngine->bSmoothFrameRate; GEngine->bSmoothFrameRate = false;
        Q.bWasUnit = W->GetGameViewport()->IsStatEnabled(TEXT("Unit")); GEngine->SetEngineStat(W, W->GetGameViewport(), TEXT("Unit"), true);
        Q.SetCVar(TEXT("r.VSync"), TEXT("0")); Q.SetCVar(TEXT("t.MaxFPS"), TEXT("0"));
        Q.SetCVar(TEXT("t.IdleWhenNotForeground"), TEXT("0"));
        if (!Q.bAdaptive) Q.SetCVar(TEXT("r.DynamicRes.OperationMode"), TEXT("0"));
        Q.SetCVar(TEXT("r.ScreenPercentage"), *FString::FromInt(PrimaryPercent));
        Q.SetCVar(TEXT("r.SecondaryScreenPercentage.GameViewport"), TEXT("100"));
        UGameUserSettings::RequestResolutionChange(1920, 1080, EWindowMode::Windowed, false);
        M->PlayerFighter->EndCombat(); M->PlayerFighter->SelectCharacter(EDuelCharacter::Hiro);
        M->NorthTowerActor->NewGame(*M);
        auto& Tower = *M->NorthTowerActor;
        if (Q.Scenario == TEXT("Archive"))
        {
            Tower.State.ReadClue(EDuelNorthClue::OpeningDocket);
            Tower.State.OpenStartingCell();
        }
        // Later-floor fixtures use the existing progression transitions. They do
        // not claim to benchmark the skipped exploration or earlier victories.
        const int32 PriorWins = Q.Scenario == TEXT("Stairs") ? 2 : Q.Scenario == TEXT("Archive") ? 1 : Q.Fight;
        for (int32 I = 0; I < PriorWins; ++I)
        { if (!DuelNorthInvestigationQA::Prepare(Tower.State,I) || !Tower.State.WinBattle(I)) { Q.Finish(false,TEXT("prior_encounter_fixture")); return; } Tower.State.PickupRelic(I == 0 ? EDuelNorthRelic::RustKey : EDuelNorthRelic::MirrorKey); Tower.State.OpenDoor(I == 0 ? EDuelNorthDoor::Chapel : EDuelNorthDoor::FinalStair); }
        if (Q.IsBattle() && !DuelNorthInvestigationQA::Prepare(Tower.State, Q.Fight)) { Q.Finish(false,TEXT("current_encounter_fixture")); return; }
        Tower.Respawn(*M);
        auto& P = *M->PlayerFighter;
        P.DisableInput(W->GetFirstPlayerController());
        P.bLockOn = false; P.ViewYaw = 0; P.ViewPitch = -19; Q.StartYaw = 0;
        P.SetWeapon(EDuelWeapon::Greatblade); P.ArtSelection = EDuelArt::HiroThreads;
        FVector Start = DuelNorthLayout::SpawnSurface();
        if (Q.Scenario == TEXT("Stairs"))
        {
            const auto* R = DuelNorthLayout::FindRoute(TEXT("FinalStair"));
            if (!R || R->Points.Num() < 3) { Q.Began = FPlatformTime::Seconds(); Q.Finish(false, TEXT("missing_stair_route")); return; }
            Q.Route = R->Points; Start = Q.Route[0]; P.ViewYaw = 180;
        }
        else if (Q.Scenario == TEXT("Archive"))
        {
            const auto* Room = DuelNorthLayout::FindRoom(TEXT("ChapelHall"));
            check(Room);
            Start = FVector(0, 0, Room->Bounds.Min.Z);
        }
        else if (Q.IsBattle()) Start = DuelNorthLayout::CourtOrigin(Q.Fight) + FVector(-2850, 0, 0);
        P.SetActorLocation(Start + FVector(0, 0, P.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2), false, nullptr, ETeleportType::TeleportPhysics);
        P.GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        Q.Began = FPlatformTime::Seconds(); Q.StageAt = Q.Began;
        Q.Event(TEXT("fixture_ready_warmup_begin"));
        Q.TickHandle = FWorldDelegates::OnWorldPostActorTick.AddRaw(&Q, &FDuelNorthPerformanceProbe::Tick);
    }
};

TUniquePtr<FDuelNorthPerformanceProbe> FDuelNorthPerformanceProbe::Active;
static FAutoConsoleCommandWithWorldAndArgs NorthPerformanceCommand(TEXT("Duel.PerfNorth"),
    TEXT("Real-time 1080p performance fixture: Orbit|Stairs|Fog|EmaP3|HiroP3|YukiP3 [seconds]. Requires isolated -DuelPerfProbe -DuelNorthTower -DuelNoSave."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FDuelNorthPerformanceProbe::Start));

#endif
