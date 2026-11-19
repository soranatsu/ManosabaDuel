#include "DuelGame.h"
#include "DuelNorthTower.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "TimerManager.h"

namespace
{
    // Explicit release QA only: render the actual Shipping title and Continue/New Game
    // with an isolated profile, without desktop input, visible windows or focus changes.
    bool StartReleaseSmoke347(float)
    {
        const TCHAR* Command = FCommandLine::Get();
        FString Scene;
        if (!FParse::Value(Command, TEXT("DuelReleaseSmoke="), Scene)) return false;
        FString Output, Profile;
        const bool bArgs = FParse::Value(Command, TEXT("DuelReleaseSmokeOutput="), Output) &&
            FParse::Value(Command, TEXT("UserDir="), Profile);
        FPaths::NormalizeDirectoryName(Output);
        FPaths::NormalizeDirectoryName(Profile);
        FString ActiveProfile = FPaths::ProjectUserDir();
        FPaths::NormalizeDirectoryName(ActiveProfile);
        FString ExpectedProfile = Output / TEXT("User");
        FPaths::NormalizeDirectoryName(ExpectedProfile);
        const bool bContinue = Scene == TEXT("Continue") || Scene == TEXT("ContinueNoRay");
        const bool bNew = Scene == TEXT("NewHiro") || Scene == TEXT("NewEma");
        FString GameDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
        FPaths::NormalizeDirectoryName(GameDirectory);
        FString CaseGameDirectory = Output / TEXT("Game/ManosabaDuel");
        FPaths::NormalizeDirectoryName(CaseGameDirectory);
        const bool bAbsoluteCaseProfile = !FPaths::IsRelative(Profile) &&
            FPaths::IsSamePath(Profile, ExpectedProfile) && FPaths::IsSamePath(Profile, ActiveProfile);
        // The normal bootstrap's embedded relative profile is allowed only in a
        // full package copy inside this Verification case, never the player's installation.
        const bool bBootstrapCaseProfile = (Profile == TEXT("NorthTower347User") || Profile == TEXT("NorthTower347Story")) &&
            FPaths::IsSamePath(GameDirectory, CaseGameDirectory) && FPaths::IsSamePath(ActiveProfile, GameDirectory / Profile);
        const bool bSafe = bArgs && (bContinue || bNew) && FParse::Param(Command, TEXT("RenderOffscreen")) &&
            FParse::Param(Command, TEXT("DuelNorthTower")) && !FParse::Param(Command, TEXT("DuelNoTitle")) &&
            !FParse::Param(Command, TEXT("DuelNoSave")) && !FParse::Param(Command, TEXT("DuelNorthSaveTest")) &&
            !FPaths::IsRelative(Output) &&
            Output.Contains(TEXT("/v3.4.7-Materials/Verification/")) &&
            (bAbsoluteCaseProfile || bBootstrapCaseProfile);
        if (!bSafe)
        {
            UE_LOG(LogTemp, Error, TEXT("ReleaseSmoke347 requires RenderOffscreen and a dedicated absolute Verification case/User profile."));
            FPlatformMisc::RequestExitWithStatus(false, 2);
            return false;
        }
        UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
        auto* Mode = World ? World->GetAuthGameMode<ADuelMode>() : nullptr;
        if (!Mode || !Mode->NorthTower() || !Mode->GetPlayerFighter()) return true;

        const FString Receipt = Output / (Scene + TEXT(".txt"));
        const FString Title = Output / (Scene + TEXT("-Title.png")), Entered = Output / (Scene + TEXT("-Entered.png"));
        const FString Checkpoint = FPaths::ProjectSavedDir() / TEXT("SaveGames/NorthTower200Checkpoint.sav");
        if (IFileManager::Get().FileExists(*Receipt) || IFileManager::Get().FileExists(*Title) ||
            IFileManager::Get().FileExists(*Entered) || !IFileManager::Get().MakeDirectory(*Output, true))
        {
            UE_LOG(LogTemp, Error, TEXT("ReleaseSmoke347 never overwrites existing case evidence."));
            FPlatformMisc::RequestExitWithStatus(false, 2);
            return false;
        }
        const auto Fail = [Receipt](const TCHAR* Reason)
        {
            FFileHelper::SaveStringToFile(FString(TEXT("passed=0\nerror=")) + Reason + TEXT("\n"), *Receipt,
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            UE_LOG(LogTemp, Error, TEXT("ReleaseSmoke347: %s"), Reason);
            FPlatformMisc::RequestExitWithStatus(false, 1);
        };
        FString Version;
        if (!GConfig || !GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), Version, GGameIni) ||
            Version != TEXT("3.4.7"))
        { Fail(TEXT("Cooked configuration is not version 3.4.7")); return false; }
        TArray<uint8> OriginalBytes;
        FDuelNorthProgress Expected;
        if (bContinue)
        {
            if (!FFileHelper::LoadFileToArray(OriginalBytes, *Checkpoint))
            { Fail(TEXT("Continue requires an existing copied checkpoint in the isolated profile")); return false; }
            const auto* Saved = Cast<UDuelNorthSave>(UGameplayStatics::LoadGameFromMemory(OriginalBytes));
            FString Reason;
            if (!Saved || !Saved->State.Validate(Reason))
            { Fail(TEXT("Copied checkpoint does not deserialize or validate")); return false; }
            Expected = Saved->State;
        }
        else if (IFileManager::Get().FileExists(*Checkpoint))
        { Fail(TEXT("Fresh-story smoke refuses a profile that already contains a checkpoint")); return false; }

        FTimerHandle Warmup;
        World->GetTimerManager().SetTimer(Warmup, [World, ModeRef = TWeakObjectPtr<ADuelMode>(Mode), Fail, Title, Entered,
            Receipt, Scene, Checkpoint, Expected, OriginalBytes = MoveTemp(OriginalBytes), bContinue]()
        {
            auto* Game = ModeRef.Get();
            if (!Game || Game->MenuPage() != EDuelMenuPage::Title || Game->CanContinue() != bContinue)
            { Fail(TEXT("The actual title has an incorrect Continue availability")); return; }
            FScreenshotRequest::RequestScreenshot(Title, true, false);
            FTimerHandle Action;
            World->GetTimerManager().SetTimer(Action, [World, ModeRef, Fail, Title, Entered, Receipt, Scene, Checkpoint,
                Expected, OriginalBytes, bContinue]()
            {
                auto* Active = ModeRef.Get();
                if (!Active || IFileManager::Get().FileSize(*Title) <= 1000)
                { Fail(TEXT("Could not capture the actual rendered title")); return; }
                if (!bContinue)
                {
                    auto* Hero = const_cast<ADuelFighter*>(Active->GetPlayerFighter());
                    if (!Hero || !Hero->SelectCharacter(Scene == TEXT("NewEma") ? EDuelCharacter::Ema : EDuelCharacter::Hiro))
                    { Fail(TEXT("Could not select the requested protagonist")); return; }
                }
                const int32 Choice = bContinue ? 0 : 1;
                for (int32 I = 0; Active->Choice() != Choice && I < 3; ++I) Active->Navigate(1);
                if (Active->Choice() != Choice) { Fail(TEXT("Could not select the ordinary title action")); return; }
                Active->Confirm();
                FTimerHandle Settle;
                World->GetTimerManager().SetTimer(Settle, [World, ModeRef, Fail, Entered, Receipt, Scene, Checkpoint,
                    Expected, OriginalBytes, bContinue]()
                {
                    auto* Running = ModeRef.Get();
                    if (!Running || Running->Outcome() != EDuelOutcome::Ready || !Running->CanExplore() ||
                        !Running->GetPlayerFighter() || !Running->GetPlayerFighter()->IsAlive())
                    { Fail(TEXT("The normal title action did not restore ordinary exploration")); return; }
                    const auto* Saved = Cast<UDuelNorthSave>(UGameplayStatics::LoadGameFromSlot(TEXT("NorthTower200Checkpoint"), 0));
                    FString Reason;
                    if (!Saved || !Saved->State.Validate(Reason))
                    { Fail(TEXT("The real profile checkpoint is not readable after entering the game")); return; }
                    const auto& State = Saved->State;
                    const auto Player = Running->GetPlayerFighter()->SelectedCharacter();
                    if ((bContinue && (Player != Expected.Player() || State.Player() != Expected.Player() || State.Wins != Expected.Wins || State.Relics != Expected.Relics ||
                        State.Doors != Expected.Doors || State.Checkpoint != Expected.Checkpoint)) ||
                        (!bContinue && (Player != (Scene == TEXT("NewEma") ? EDuelCharacter::Ema : EDuelCharacter::Hiro) || State.Player() != Player ||
                            State.Wins || State.Relics || State.Doors || !State.bOpeningCellRequired || State.bOpeningCellUnlocked ||
                            Running->NorthTower()->CurrentFloor() != DuelNorthLayout::EFloor::Prison)))
                    { Fail(TEXT("The restored progress or fresh starting-cell state is incorrect")); return; }
                    FScreenshotRequest::RequestScreenshot(Entered, true, false);
                    const FString Result = FString::Printf(TEXT("passed=1\nversion=3.4.7\nbuild=%s\nscene=%s\nrender_offscreen=1\ndesktop_input=none\ncontinue_was_available=%d\nplayer=%s\nfloor=%d\ncheckpoint=%d\nwins=%d\nrelics=%d\ndoors=%d\nstate_format=%d\nobjective=%s\ninitial_checkpoint_sha1=%s\n"),
                        UE_BUILD_SHIPPING ? TEXT("Shipping") : TEXT("Development"), *Scene, bContinue,
                        Running->GetPlayerFighter()->CharacterName(), int32(Running->NorthTower()->CurrentFloor()), State.Checkpoint,
                        State.Wins, State.Relics, State.Doors, State.Format, *Running->NorthTower()->Objective(false),
                        OriginalBytes.IsEmpty() ? TEXT("none") : *FSHA1::HashBuffer(OriginalBytes.GetData(), OriginalBytes.Num()).ToString());
                    FTimerHandle Finish;
                    World->GetTimerManager().SetTimer(Finish, [Fail, Entered, Receipt, Result]()
                    {
                        if (IFileManager::Get().FileSize(*Entered) <= 1000 || !FFileHelper::SaveStringToFile(Result, *Receipt,
                            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
                        { Fail(TEXT("Could not persist the entered-game screenshot and receipt")); return; }
                        FPlatformMisc::RequestExitWithStatus(false, 0);
                    }, 1.f, false);
                }, 2.f, false);
            }, 1.f, false);
        }, 4.f, false);
        return false;
    }

    [[maybe_unused]] const auto ReleaseSmoke347Ticker =
        FTSTicker::GetCoreTicker().AddTicker(TEXT("DuelReleaseSmoke347"), .1f, &StartReleaseSmoke347);
}
