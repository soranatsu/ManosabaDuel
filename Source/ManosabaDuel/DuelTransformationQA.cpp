#include "DuelGame.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "DuelArena.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace
{
    struct FTransformCapture
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<ADuelFighter> Boss;
        TWeakObjectPtr<ADuelArena> Arena;
        FDelegateHandle TickHandle;
        FString Directory;
        FString Samples = TEXT("frame,simulation_seconds,phase,actor_z,lockpoint_z,world_seconds,delta_seconds\n");
        FVector Source;
        int32 Frame = 0;
        int32 LastFrame = 0;
        int32 DamageStage = 0;
        bool bPreviousFixed = false;
        double PreviousStep = 0.0;
        double CaptureStart = 0.0;

        void Begin()
        {
            UWorld* W = World.Get();
            ADuelFighter* B = Boss.Get();
            if (!W || !B) return;
            Arena->ClearCombatEffects();
            Directory = FPaths::ConvertRelativePathToFull(Directory);
            IFileManager::Get().MakeDirectory(*Directory, true);
            bPreviousFixed = FApp::UseFixedTimeStep();
            PreviousStep = FApp::GetFixedDeltaTime();
            FApp::SetFixedDeltaTime(1.0 / 30.0);
            FApp::SetUseFixedTimeStep(true);
            if (GEngine->GameViewport && GEngine->GameViewport->Viewport)
            {
                const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
                UE_LOG(LogTemp, Display, TEXT("NorthTransform viewport: %dx%d"), Size.X, Size.Y);
            }
            UE_LOG(LogTemp, Display, TEXT("NorthTransform capture started: %s"), *Directory);
        }

        bool FrameStep(float DeltaSeconds)
        {
            ADuelFighter* B = Boss.Get();
            UWorld* W = World.Get();
            if (!B || !W) return false;
            if (Frame == 0) CaptureStart = W->GetTimeSeconds();
            if (Frame == 9)
            {
                B->ReceiveDuelDamage(DuelCourt::BossStageHealth[DamageStage] + 1.f, Source);
                UE_LOG(LogTemp, Display, TEXT("NorthTransform damage-trigger frame=%d phase=%d"), Frame, B->Phase());
            }
            const FVector Lock = B->LockPoint();
            Samples += FString::Printf(TEXT("%d,%.6f,%d,%.3f,%.3f,%.6f,%.6f\n"), Frame,
                W->GetTimeSeconds() - CaptureStart, B->Phase(), B->GetActorLocation().Z, Lock.Z,
                W->GetTimeSeconds(), DeltaSeconds);
            const FString File = Directory / FString::Printf(TEXT("frame-%04d.png"), Frame);
            FScreenshotRequest::RequestScreenshot(File, false, false);
            if (++Frame <= LastFrame) return true;
            FFileHelper::SaveStringToFile(Samples, *(Directory / TEXT("samples.csv")));
            FApp::SetUseFixedTimeStep(bPreviousFixed);
            FApp::SetFixedDeltaTime(PreviousStep);
            UE_LOG(LogTemp, Display, TEXT("NorthTransform capture complete: frames=%d phase=%d directory=%s"),
                Frame, B->Phase(), *Directory);
            return false;
        }
    };

    void CaptureTransformation(const TArray<FString>& Args, UWorld* World)
    {
        if (!World || Args.Num() < 2 || Args.Num() > 3 ||
            !FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")) ||
            !FParse::Param(FCommandLine::Get(), TEXT("DuelLinearRoute")))
        {
            UE_LOG(LogTemp, Warning, TEXT("Duel.CaptureTransform Ema|Hiro|Yuki Awaken|Eclipse [Front|Side] requires -DuelLinearRoute -DuelNoSave"));
            return;
        }
        EDuelCharacter Character;
        if (Args[0] == TEXT("Ema")) Character = EDuelCharacter::Ema;
        else if (Args[0] == TEXT("Hiro")) Character = EDuelCharacter::Hiro;
        else if (Args[0] == TEXT("Yuki")) Character = EDuelCharacter::Yuki;
        else return;
        if (Args[1] != TEXT("Awaken") && Args[1] != TEXT("Eclipse")) return;
        const bool bEclipse = Args[1] == TEXT("Eclipse");
        const bool bSide = Args.Num() == 3 && Args[2] == TEXT("Side");
        ADuelMode* Mode = World->GetAuthGameMode<ADuelMode>();
        APlayerController* PC = World->GetFirstPlayerController();
        ADuelArena* Arena = nullptr;
        for (TActorIterator<ADuelArena> It(World); It; ++It) { Arena = *It; break; }
        if (!Mode || !PC || !Arena || !Mode->GetPlayerFighter()) return;
        UGameUserSettings::RequestResolutionChange(1920, 1080, EWindowMode::Windowed, false);
        Mode->ResetEncounter();
        ADuelFighter* Player = const_cast<ADuelFighter*>(Mode->GetPlayerFighter());
        if (ADuelFighter* PreviousBoss = const_cast<ADuelFighter*>(Mode->GetBossFighter()))
        {
            PreviousBoss->EndCombat();
            PreviousBoss->SetActorHiddenInGame(true);
            PreviousBoss->SetActorEnableCollision(false);
            PreviousBoss->SetActorTickEnabled(false);
        }
        const FVector Origin = Arena->CourtOrigin();
        Player->SetActorTickEnabled(false);
        Player->GetCharacterMovement()->DisableMovement();
        Player->SetActorHiddenInGame(true);
        Player->SetActorLocation(Origin + FVector(-12000, 0, 62));
        const FTransform Transform(FRotator(0, 180, 0), Origin + FVector(0, 0, 103));
        ADuelFighter* Boss = World->SpawnActorDeferred<ADuelFighter>(ADuelFighter::StaticClass(), Transform,
            nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Boss) return;
        Boss->ConfigureBoss(Character);
        Boss->SetEncounter(Player, Arena);
        UGameplayStatics::FinishSpawningActor(Boss, Transform);
        Boss->SetActorLocationAndRotation(Transform.GetLocation(), Transform.Rotator());
        Boss->GetCharacterMovement()->DisableMovement();
        Player->SetEncounter(Boss, Arena);
        ACameraActor* Camera = World->SpawnActor<ACameraActor>();
        const FVector CameraAt = Origin + (bSide ? FVector(0, -1280, 450) : FVector(-1180, -420, 455));
        Camera->SetActorLocationAndRotation(CameraAt, (Origin + FVector(0, 0, 175) - CameraAt).Rotation());
        Camera->GetCameraComponent()->SetFieldOfView(45.f);
        PC->SetViewTarget(Camera);
        if (AHUD* HUD = PC->GetHUD()) HUD->bShowHUD = false;

        const TSharedRef<FTransformCapture> Capture = MakeShared<FTransformCapture>();
        Capture->World = World;
        Capture->Boss = Boss;
        Capture->Arena = Arena;
        Capture->Source = Player->GetActorLocation();
        Capture->DamageStage = bEclipse ? 1 : 0;
        const EDuelMove Move = bEclipse ? EDuelMove::Eclipse : EDuelMove::Awaken;
        Capture->LastFrame = 9 + FMath::CeilToInt((DuelCourt::Timing(Move, bEclipse).Total() + .70f) * 30.f);
        FString CaptureRoot;
        if (!FParse::Value(FCommandLine::Get(), TEXT("DuelTransformOutput="), CaptureRoot))
            CaptureRoot = FPaths::ProjectSavedDir() / TEXT("NorthTower200/Transform");
        Capture->Directory = CaptureRoot / (Args[0] + TEXT("-") + Args[1] + (bSide ? TEXT("-Side") : TEXT("-Front")));
        if (bEclipse) Boss->ReceiveDuelDamage(DuelCourt::BossStageHealth[0] + 1.f, Capture->Source);
        FTimerHandle Warmup;
        World->GetTimerManager().SetTimer(Warmup, [Capture]()
        {
            if (!Capture->World.IsValid() || !Capture->Boss.IsValid()) return;
            Capture->Begin();
            Capture->TickHandle = FWorldDelegates::OnWorldPostActorTick.AddLambda([Capture](UWorld* TickedWorld, ELevelTick, float DeltaSeconds)
            {
                if (TickedWorld != Capture->World.Get()) return;
                if (!Capture->FrameStep(DeltaSeconds))
                {
                    FWorldDelegates::OnWorldPostActorTick.Remove(Capture->TickHandle);
                    if (UWorld* W = Capture->World.Get())
                    {
                        if (FParse::Param(FCommandLine::Get(), TEXT("DuelQAQuit")))
                        {
                            FTimerHandle QuitTimer;
                            W->GetTimerManager().SetTimer(QuitTimer, [W]() { GEngine->Exec(W, TEXT("QUIT")); }, .25f, false);
                        }
                    }
                }
            });
        }, bEclipse ? DuelCourt::Timing(EDuelMove::Awaken, false).Total() + 1.2f : 1.2f, false);
    }

    FAutoConsoleCommandWithWorldAndArgs CaptureTransformCommand(TEXT("Duel.CaptureTransform"),
        TEXT("Capture a real witch transformation at fixed 30 Hz in an isolated, non-saving linear QA run."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CaptureTransformation));
}
#endif
