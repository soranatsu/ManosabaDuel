#include "DuelGame.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "RenderResource.h"
#include "RenderUtils.h"
#include "RenderingThread.h"

namespace
{
    enum class EGraphicsMenuCase { Write, Restart, Null };

    constexpr const TCHAR* GraphicsSection = TEXT("ManosabaDuel.Graphics");
    constexpr const TCHAR* PrimaryName = TEXT("r.ScreenPercentage");
    constexpr const TCHAR* OutputName = TEXT("r.SecondaryScreenPercentage.GameViewport");
    constexpr const TCHAR* RayTracingName = TEXT("r.RayTracing.Enable");

    struct FDiskGraphics
    {
        int32 Preset = INDEX_NONE;
        int32 Hardware = INDEX_NONE;
        bool bPresent = false;
    };

    FDiskGraphics ReadDiskGraphics(const FString& Ini)
    {
        FDiskGraphics State;
        if (IFileManager::Get().FileExists(*Ini))
        {
            FConfigFile Disk;
            Disk.Read(Ini);
            State.bPresent = Disk.FindSection(GraphicsSection) != nullptr;
            Disk.GetInt(GraphicsSection, TEXT("TSRPreset"), State.Preset);
            Disk.GetInt(GraphicsSection, TEXT("HardwareLumen"), State.Hardware);
        }
        return State;
    }

    float ReadCVar(const TCHAR* Name)
    {
        const auto* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
        return Variable ? Variable->GetFloat() : -1.f;
    }

    bool HasLumenRayDevice()
    {
#if RHI_RAYTRACING
        // Check the device independently of the in-game availability test.
        return GRHISupportsRayTracing && RHISupportsRayTracing(GMaxRHIShaderPlatform) &&
            (GRHISupportsInlineRayTracing || (GRHISupportsRayTracingShaders && GRHISupportsRayTracingDispatchIndirect));
#else
        return false;
#endif
    }

    bool IsRTActiveOnRenderThread()
    {
        bool bActive = false;
        ENQUEUE_RENDER_COMMAND(DuelGraphicsReadRayTracing)([&bActive](FRHICommandListImmediate&)
        {
            bActive = IsRayTracingEnabled(GMaxRHIShaderPlatform);
        });
        FlushRenderingCommands();
        return bActive;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelGraphicsMenuQA, "ManosabaDuel.GraphicsMenu",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelGraphicsMenuQA::RunTest(const FString& Parameters)
{
    const TCHAR* Command = FCommandLine::Get();
    FString CaseName, UserDir;
    if (!TestTrue(TEXT("Use -DuelGraphicsQA=Write|Restart|Null, an absolute isolated -userdir, and -DuelNoSave"),
        FParse::Value(Command, TEXT("DuelGraphicsQA="), CaseName) &&
        (CaseName == TEXT("Write") || CaseName == TEXT("Restart") || CaseName == TEXT("Null")) &&
        FParse::Value(Command, TEXT("userdir="), UserDir) && !UserDir.IsEmpty() && !FPaths::IsRelative(UserDir) &&
        FParse::Param(Command, TEXT("DuelNoSave")) && !FParse::Param(Command, TEXT("DuelPerfProbe")))) return false;

    FString ActiveUserDir = FPaths::ProjectUserDir();
    FPaths::NormalizeDirectoryName(UserDir);
    FPaths::NormalizeDirectoryName(ActiveUserDir);
    const FString ExpectedIni = FPaths::Combine(UserDir, TEXT("Saved/DuelGraphics.ini"));
    const FString Ini = FDuelGraphicsSettings::PreferencesPath();
    if (!TestTrue(TEXT("Duel graphics preferences use the explicitly isolated user profile"),
        FPaths::IsSamePath(ActiveUserDir, UserDir) && FPaths::IsSamePath(Ini, ExpectedIni)))
    {
        AddInfo(FString::Printf(TEXT("Duel GraphicsQA %s userdir=%s expected=%s actual=%s"),
            *CaseName, *ActiveUserDir, *ExpectedIni, *Ini));
        return false;
    }
    AddInfo(FString::Printf(TEXT("Duel GraphicsQA %s settings_ini=%s"), *CaseName, *Ini));

    const auto Case = CaseName == TEXT("Write") ? EGraphicsMenuCase::Write :
        CaseName == TEXT("Restart") ? EGraphicsMenuCase::Restart : EGraphicsMenuCase::Null;
    const bool bNull = Case == EGraphicsMenuCase::Null;
    const bool bWrite = Case == EGraphicsMenuCase::Write;
    if (!TestTrue(TEXT("Requested renderer matches this process"), bNull ?
        FParse::Param(Command, TEXT("NullRHI")) && !FApp::CanEverRender() :
        FApp::CanEverRender() && GIsRHIInitialized && GDynamicRHI && RHIGetInterfaceType() == ERHIInterfaceType::D3D12 &&
        GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM6)) return false;

    ADuelMode* Mode = nullptr;
    if (GEngine)
    {
        for (const auto& Context : GEngine->GetWorldContexts())
        {
            UWorld* World = Context.World();
            if (World && World->WorldType == EWorldType::Game && (Mode = World->GetAuthGameMode<ADuelMode>())) break;
        }
    }
    if (!TestTrue(TEXT("A normal North Tower game is open in exploration with no pause menu"), Mode && Mode->UsesNorthTower() &&
        Mode->Outcome() == EDuelOutcome::Ready && !UGameplayStatics::IsGamePaused(Mode) &&
        (bNull || Mode->GetWorld()->GetGameViewport()))) return false;

    const FDiskGraphics Before = ReadDiskGraphics(Ini);
    const bool bRayDevice = !bNull && HasLumenRayDevice();
    if (bNull || bWrite)
    {
        if (!TestFalse(TEXT("Fresh write and headless cases start without a graphics preference file"),
            IFileManager::Get().FileExists(*Ini))) return false;
    }
    else if (!TestTrue(TEXT("Fresh restart sees the first process's written graphics section"),
        Before.bPresent && Before.Preset == 2 && Before.Hardware == (bRayDevice ? 1 : INDEX_NONE))) return false;

    const float PrimaryBefore = ReadCVar(PrimaryName);
    const float OutputBefore = ReadCVar(OutputName);
    const float RTBefore = ReadCVar(RayTracingName);
    if (!bNull)
    {
        if (!TestTrue(TEXT("Project opt-in compiles hardware Lumen and supports the dynamic runtime mode"),
            ReadCVar(TEXT("r.RayTracing")) == 1.f && ReadCVar(TEXT("r.SkinCache.CompileShaders")) == 1.f &&
            ReadCVar(TEXT("r.Lumen.HardwareRayTracing")) == 1.f && ReadCVar(TEXT("r.RayTracing.EnableOnDemand")) == 1.f &&
            (!bRayDevice || GetRayTracingMode() == ERayTracingMode::Dynamic))) return false;
        if (!TestTrue(TEXT("Startup restores the real primary and output render fractions"),
            FMath::IsNearlyEqual(PrimaryBefore, 67.f, .01f) && FMath::IsNearlyEqual(OutputBefore, 100.f, .01f))) return false;
        if (!TestTrue(TEXT("Default balanced scene retains volumetric fog and reduced shadow cost"),
            ReadCVar(TEXT("sg.ShadowQuality")) == 1.f && ReadCVar(TEXT("r.VolumetricFog")) == 1.f &&
            ReadCVar(TEXT("r.Shadow.Virtual.MaxPhysicalPages")) == 1024.f)) return false;
        if (!TestTrue(TEXT("Runtime ray tracing starts off or restores the selected on-state"),
            (!bRayDevice || RTBefore == (bWrite ? 0.f : 1.f)) && IsRTActiveOnRenderThread() == (!bWrite && bRayDevice))) return false;
    }

    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Game = TWeakObjectPtr<ADuelMode>(Mode), CaseName, Ini,
        bNull, bWrite, bRayDevice, PrimaryBefore, OutputBefore, RTBefore, Step = 0, NextFrame = uint64(0)]() mutable
    {
        if (GFrameCounter < NextFrame) return false;
        NextFrame = GFrameCounter + 3;
        ADuelMode* WorldMode = Game.Get();
        APlayerController* Controller = WorldMode ? WorldMode->GetWorld()->GetFirstPlayerController() : nullptr;
        const auto* Player = Controller ? Cast<ADuelFighter>(Controller->GetPawn()) : nullptr;
        if (!TestTrue(TEXT("Actual player key bindings are available"), Controller && Controller->PlayerInput &&
            Player && Player->InputComponent)) return true;
        const TArray<UInputComponent*> Stack{Player->InputComponent};
        const auto Tap = [&](FKey Key)
        {
            for (const EInputEvent Event : {IE_Pressed, IE_Released})
            {
                Controller->InputKey(FInputKeyParams(Key, Event, Event == IE_Pressed ? 1.0 : 0.0));
                Controller->PlayerInput->ProcessInputStack(Stack, 1.f / 60.f, UGameplayStatics::IsGamePaused(WorldMode));
            }
        };
        const auto Close = [&]()
        {
            if (WorldMode->ShowingGraphics()) Tap(EKeys::Escape);
            if (UGameplayStatics::IsGamePaused(WorldMode)) Tap(EKeys::Escape);
            Controller->FlushPressedKeys();
        };
        const auto CheckTSR = [&](float Primary, const TCHAR* Label)
        {
            return TestTrue(TEXT("TSR menu label, actual primary scale and full output scale agree"),
                FMath::IsNearlyEqual(ReadCVar(PrimaryName), Primary, .01f) && FMath::IsNearlyEqual(ReadCVar(OutputName), 100.f, .01f) &&
                WorldMode->Graphics().Value(EDuelGraphicsRow::TSR) == Label && !WorldMode->Graphics().SaveFailed());
        };
        const auto CheckRT = [&](bool bEnabled)
        {
            return TestTrue(TEXT("Lumen ray tracing changed in the cvar and renderer and was written to disk"),
                ReadCVar(RayTracingName) == float(bEnabled) && IsRTActiveOnRenderThread() == bEnabled &&
                WorldMode->Graphics().Value(EDuelGraphicsRow::HardwareLumen) == (bEnabled ? TEXT("开") : TEXT("关")) &&
                ReadDiskGraphics(Ini).Hardware == int32(bEnabled) && !WorldMode->Graphics().SaveFailed());
        };

        bool bOkay = true;
        switch (Step++)
        {
        case 0:
            Tap(EKeys::Escape);
            for (int32 I = 0; I < WorldMode->PauseOptionCount() && WorldMode->PauseOption(WorldMode->Choice()) != EDuelPause::Graphics; ++I)
                Tap(EKeys::Down);
            if (!TestTrue(TEXT("Escape and arrow keys reach the graphics pause item"), UGameplayStatics::IsGamePaused(WorldMode) &&
                WorldMode->PauseOption(WorldMode->Choice()) == EDuelPause::Graphics)) { Close(); return true; }
            Tap(EKeys::F);
            bOkay = TestTrue(TEXT("F opens the same graphics panel used by players"), WorldMode->ShowingGraphics() &&
                WorldMode->GraphicsChoice() == int32(EDuelGraphicsRow::TSR));
            bOkay &= TestTrue(TEXT("TSR control is available only in the rendered run"),
                WorldMode->Graphics().CanAdjust(EDuelGraphicsRow::TSR) == !bNull);
            if (!bNull) bOkay &= CheckTSR(67.f, bWrite && GRHISupportsDynamicResolution ? TEXT("自适应 60 FPS") : TEXT("Balanced 67%"));
            if (bWrite) Tap(EKeys::D);
            break;
        case 1:
            if (bWrite) bOkay = CheckTSR(100.f, TEXT("Native 100%"));
            if (bWrite || bNull) Tap(EKeys::D);
            break;
        case 2:
            if (bWrite)
            {
                bOkay = CheckTSR(75.f, TEXT("Quality 75%"));
                bOkay &= TestEqual(TEXT("Quality preset reached the isolated file"), ReadDiskGraphics(Ini).Preset, 1);
            }
            if (bWrite || bNull) Tap(EKeys::F);
            break;
        case 3:
            if (bWrite)
            {
                bOkay = CheckTSR(67.f, TEXT("Balanced 67%"));
                bOkay &= TestEqual(TEXT("Balanced preset reached the isolated file"), ReadDiskGraphics(Ini).Preset, 2);
            }
            Tap(EKeys::Down);
            bOkay &= TestTrue(TEXT("Down selects the hardware Lumen row with correct availability"),
                WorldMode->GraphicsChoice() == int32(EDuelGraphicsRow::HardwareLumen) &&
                WorldMode->Graphics().CanAdjust(EDuelGraphicsRow::HardwareLumen) == bRayDevice);
            if (bWrite || bNull) Tap(EKeys::F);
            break;
        case 4:
            if (bRayDevice && bWrite)
            {
                bOkay = CheckRT(true);
                Tap(EKeys::F);
            }
            break;
        case 5:
            if (bRayDevice && bWrite)
            {
                bOkay = CheckRT(false);
                Tap(EKeys::D); // Leave the preference on for the fresh restart case.
            }
            break;
        default:
        {
            const FDiskGraphics After = ReadDiskGraphics(Ini);
            if (bNull)
            {
                bOkay &= TestFalse(TEXT("Attempting both disabled controls creates no graphics file on NullRHI"),
                    IFileManager::Get().FileExists(*Ini));
                bOkay &= TestTrue(TEXT("NullRHI input changes no render cvar"), ReadCVar(PrimaryName) == PrimaryBefore &&
                    ReadCVar(OutputName) == OutputBefore && ReadCVar(RayTracingName) == RTBefore);
            }
            else
            {
                bOkay &= CheckTSR(67.f, TEXT("Balanced 67%"));
                bOkay &= TestTrue(TEXT("Isolated file carries the exact live preferences"),
                    After.bPresent && After.Preset == 2 && After.Hardware == (bRayDevice ? 1 : INDEX_NONE));
            }
            if (bRayDevice) bOkay &= CheckRT(true);
            else bOkay &= TestTrue(TEXT("Unsupported hardware Lumen stays unavailable without changing its cvar"),
                WorldMode->Graphics().Value(EDuelGraphicsRow::HardwareLumen) == TEXT("当前不可用") &&
                ReadCVar(RayTracingName) == RTBefore);

            Tap(EKeys::Escape);
            bOkay &= TestTrue(TEXT("First Escape closes only the graphics panel"), !WorldMode->ShowingGraphics() &&
                UGameplayStatics::IsGamePaused(WorldMode));
            Tap(EKeys::Escape);
            bOkay &= TestFalse(TEXT("Second Escape resumes gameplay"), UGameplayStatics::IsGamePaused(WorldMode));
            Close();
            if (bOkay) AddInfo(FString::Printf(TEXT("Duel GraphicsQA %s supported=%d primary=%.0f output=%.0f rt=%.0f disk_preset=%d disk_rt=%d"),
                *CaseName, int32(bRayDevice), ReadCVar(PrimaryName), ReadCVar(OutputName), ReadCVar(RayTracingName), After.Preset, After.Hardware));
            return true;
        }
        }
        if (bOkay) return false;
        Close();
        return true;
    }));
    return true;
}

#endif
