#include "DuelNorthTower.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DuelGame.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthExistingSaveTest, "ManosabaDuel.NorthTower.ExistingSaveReadback",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthExistingSaveTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    const TCHAR* Command = FCommandLine::Get();
    FString Profile, Receipt;
    if (!FParse::Param(Command, TEXT("DuelExistingSaveQA")))
    {
        AddInfo(TEXT("Existing checkpoint readback is opt-in and uses only a Verification copy."));
        return true;
    }
    const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Verification/SaveReadback347/"));
    const bool bHasProfile = FParse::Value(Command, TEXT("UserDir="), Profile);
    FString ActiveProfile = FPaths::ProjectUserDir();
    FPaths::NormalizeDirectoryName(Profile);
    FPaths::NormalizeDirectoryName(ActiveProfile);
    if (!TestTrue(TEXT("Readback requires a dedicated Verification profile and its own receipt"),
        FParse::Param(Command, TEXT("DuelNorthTower")) &&
        !FParse::Param(Command, TEXT("DuelNoSave")) && !FParse::Param(Command, TEXT("DuelNoTitle")) &&
        !FParse::Param(Command, TEXT("DuelNorthSaveTest")) &&
        bHasProfile && !FPaths::IsRelative(Profile) &&
        FPaths::IsUnderDirectory(Profile, Root) && FPaths::IsSamePath(Profile, ActiveProfile) &&
        FParse::Value(Command, TEXT("DuelReadbackReceipt="), Receipt) && !FPaths::IsRelative(Receipt) &&
        FPaths::IsUnderDirectory(Receipt, Root) && !IFileManager::Get().FileExists(*Receipt))) return false;

    const FString File = FPaths::ProjectSavedDir() / TEXT("SaveGames/NorthTower200Checkpoint.sav");
    TArray<uint8> Bytes;
    if (!TestTrue(TEXT("The copied file has the expected Unreal GVAS v3 save header"),
        FFileHelper::LoadFileToArray(Bytes, *File) && Bytes.Num() >= 8 &&
        FMemory::Memcmp(Bytes.GetData(), "GVAS", 4) == 0 && Bytes[4] == 3 &&
        Bytes[5] == 0 && Bytes[6] == 0 && Bytes[7] == 0)) return false;
    auto* Save = Cast<UDuelNorthSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
    FString Reason;
    if (!TestTrue(TEXT("The real engine deserializes the current save class and accepts the complete old story state"),
        Save && Save->State.Validate(Reason)))
    {
        AddError(Reason);
        return false;
    }
    ADuelMode* Mode = nullptr;
    for (const auto& Context : GEngine->GetWorldContexts())
        if (Context.World() && (Mode = Context.World()->GetAuthGameMode<ADuelMode>())) break;
    if (!TestTrue(TEXT("The ordinary title offers Continue for this copied checkpoint"),
        Mode && Mode->NorthTower() && Mode->MenuPage() == EDuelMenuPage::Title &&
        Mode->CanContinue() && Mode->NorthTower()->HasCheckpoint())) return false;
    for (int32 Step = 0; Mode->Choice() != 0 && Step < 3; ++Step) Mode->Navigate(-1);
    if (!TestEqual(TEXT("The ordinary title selection targets Continue"), Mode->Choice(), 0)) return false;
    Mode->Confirm();
    if (!TestTrue(TEXT("The ordinary Continue action restores the saved hero and enters exploration"),
        Mode->Outcome() == EDuelOutcome::Ready && Mode->CanExplore() && Mode->GetPlayerFighter() &&
        Mode->GetPlayerFighter()->IsAlive() && Mode->GetPlayerFighter()->SelectedCharacter() == Save->State.Player())) return false;
    for (int32 Relic = 0; Relic < 3; ++Relic)
        TestEqual(TEXT("Continue preserves the saved relic set"), Mode->NorthTower()->HasRelic(static_cast<EDuelNorthRelic>(Relic)),
            (Save->State.Relics & (1 << Relic)) != 0);
    const auto& State = Save->State;
    const FString Result = FString::Printf(TEXT("header=GVAS3\nstate_format=%d\nbytes=%d\nvalid=1\ncontinue_available=1\ncontinue_entered=1\nplayer=%d\nwins=%d\nrelics=%d\ndoors=%d\nclues=%d\nside_clues=%d\ncheckpoint=%d\nvisited_checkpoints=%d\nactivated_checkpoints=%d\nexplored_floors=%d\nopening_required=%d\nopening_unlocked=%d\ndialogues=%d\nreunions=%d\nbeacon_bits=%d\ncache_bits=%u\n"),
        State.Format, Bytes.Num(), State.PlayerCharacter, State.Wins, State.Relics, State.Doors, State.Clues, State.SideClues,
        State.Checkpoint, State.VisitedCheckpoints, State.ActivatedCheckpoints, State.ExploredFloors, State.bOpeningCellRequired,
        State.bOpeningCellUnlocked, State.Dialogues, State.Reunions, State.BeaconBits, State.CacheBits);
    TestTrue(TEXT("Save readback writes a separate evidence receipt"),
        FFileHelper::SaveStringToFile(Result, *Receipt, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    AddInfo(Result);
    return !HasAnyErrors();
}

#endif
