#include "DuelNorthTower.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DuelArena.h"
#include "DuelGame.h"
#include "DuelNorthFinds.h"
#include "DuelNorthInteractables.h"
#include "DuelNorthInvestigationQA.h"
#include "DuelNorthNarrative.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthIntegrationTest, "ManosabaDuel.NorthTower.Integration",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthIntegrationTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace DuelNorthLayout;
    constexpr const TCHAR* TestSlot = TEXT("NorthTower200IntegrationQA");
    const TCHAR* Command = FCommandLine::Get();
    const bool bNoSave = FParse::Param(Command, TEXT("DuelNoSave"));
    const bool bDisk = FParse::Param(Command, TEXT("DuelNorthSaveTest"));
    FString RestartStage, OpeningStage;
    const bool bRestart = FParse::Value(Command, TEXT("DuelRestartStage="), RestartStage);
    const bool bOpeningRestart = FParse::Value(Command, TEXT("DuelRestartIntro="), OpeningStage);
    const bool bRead = RestartStage == TEXT("Read");
    const bool bLegacyGear = FParse::Param(Command, TEXT("DuelNorthLegacyLoadout"));
    if (bOpeningRestart && !TestTrue(TEXT("The opening restart phase is Fresh, Clue or Open"), bRestart &&
        (OpeningStage == TEXT("Fresh") || OpeningStage == TEXT("Clue") || OpeningStage == TEXT("Open")))) return false;
    if (bRestart && !TestTrue(TEXT("Restart uses the disk test slot and an explicit Write or Read process"),
        bDisk && (bRead || RestartStage == TEXT("Write")))) return false;
    if (!TestTrue(TEXT("Integration needs North Tower and exactly one isolated persistence mode"),
        FParse::Param(Command, TEXT("DuelNorthTower")) && bNoSave != bDisk)) return false;
    FString UserDirectory;
    const FString SavedDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
    const FString ProjectSaved = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Saved"));
    if (bDisk && !TestTrue(TEXT("Disk QA requires a dedicated UserDir below project Saved, never the player's profile"),
        FParse::Value(Command, TEXT("UserDir="), UserDirectory) && !UserDirectory.IsEmpty() &&
        FPaths::IsUnderDirectory(SavedDirectory, ProjectSaved) && !FPaths::IsSamePath(SavedDirectory, ProjectSaved) &&
        UGameplayStatics::DoesSaveGameExist(TestSlot, 0) == bRead)) return false;

    ADuelMode* Mode = nullptr;
    for (const auto& Context : GEngine->GetWorldContexts())
        if (Context.World() && (Mode = Context.World()->GetAuthGameMode<ADuelMode>())) break;
    if (!TestTrue(TEXT("Actual North Tower Mode and fighters are running"), Mode && Mode->UsesNorthTower() &&
        Mode->NorthTowerActor && Mode->PlayerFighter && Mode->BossFighter && Mode->Arena)) return false;
    ADuelNorthTower& Tower = *Mode->NorthTowerActor;
    ADuelFighter& Player = *Mode->PlayerFighter;
    if (bRestart)
    {
        FString Hero;
        int32 FloorNumber = INDEX_NONE;
        if (!TestTrue(TEXT("Restart case chooses Ema/Hiro and a real floor index (0 B1 through 5 Garden)"),
            FParse::Value(Command, TEXT("DuelRestartHero="), Hero) && (Hero == TEXT("Ema") || Hero == TEXT("Hiro")) &&
            FParse::Value(Command, TEXT("DuelRestartFloor="), FloorNumber) && FloorNumber >= 0 && FloorNumber <= 5)) return false;
        const bool bGarden = FloorNumber == 5;
        const auto Floor = static_cast<EDuelNorthFloor>(FloorNumber);
        const auto RestFloor = bGarden ? EDuelNorthFloor::Final : Floor;
        const auto Character = Hero == TEXT("Ema") ? EDuelCharacter::Ema : EDuelCharacter::Hiro;
        const EDuelWeapon Weapon = Character == EDuelCharacter::Ema ? EDuelWeapon::YukiPen : EDuelWeapon::FirePoker;
        const EDuelArt Art = Character == EDuelCharacter::Ema ? EDuelArt::EmaLight : EDuelArt::HiroThreads;
        if (bOpeningRestart)
        {
            if (!TestTrue(TEXT("Opening restart uses the initial prison floor"), Floor == EDuelNorthFloor::Prison)) return false;
            const FName CellDoor(TEXT("CellDoor")), Docket(TEXT("OpeningDocket"));
            const bool bReadDocket = OpeningStage != TEXT("Fresh");
            const bool bOpened = OpeningStage == TEXT("Open");
            const auto Matches = [&](const FDuelNorthProgress& State)
            {
                return State.Player() == Character && State.Checkpoint == uint8(EDuelNorthFloor::Prison) &&
                    State.bOpeningCellRequired && State.UsesStartingCellCheckpoint() && !State.bPrisonRestActivated &&
                    State.HasClue(EDuelNorthClue::OpeningDocket) == bReadDocket && State.IsOpeningCellOpen() == bOpened;
            };
            if (!bRead)
            {
                if (!TestTrue(TEXT("Choose the opening fixture's protagonist"), Player.SelectCharacter(Character))) return false;
                Tower.NewGame(*Mode);
                const auto ConfirmAt = [&](FName Id)
                {
                    const auto* Marker = FindLandmark(Id);
                    if (!TestNotNull(TEXT("Opening interaction exists"), Marker)) return false;
                    Player.SetActorLocation(Tower.LandmarkPosition(*Marker) + FVector(0, 0, 62),
                        false, nullptr, ETeleportType::TeleportPhysics);
                    Mode->Tick(0);
                    if (!TestEqual(TEXT("Opening fixture uses actual proximity selection"), Tower.Nearby, Id)) return false;
                    Mode->Confirm();
                    if (Id == Docket)
                    {
                        const auto* Story = Tower.Narrative();
                        if (!TestTrue(TEXT("Opening note starts the protagonist's portrait scene"), Story && Story->IsTalking())) return false;
                        if (Story->IsReadingDocument())
                        {
                            Mode->Confirm();
                            if (Story->IsReadingDocument()) Mode->Confirm();
                        }
                        const int32 Count = Story->Subtitle().Count;
                        for (int32 Line = 0; Line < Count && Story->IsTalking(); ++Line) Mode->Confirm();
                        if (!TestFalse(TEXT("Finishing the opening scene returns input to the cell"), Story->IsTalking())) return false;
                    }
                    return true;
                };
                if (bReadDocket && !ConfirmAt(Docket)) return false;
                if (bOpened && !ConfirmAt(CellDoor)) return false;
                const auto* Disk = Cast<UDuelNorthSave>(UGameplayStatics::LoadGameFromSlot(TestSlot, 0));
                if (!TestTrue(TEXT("New Game or the real opening interaction automatically wrote the requested cell state"),
                    Disk && Matches(Disk->State))) return false;
            }
            else
            {
                Mode->Result = EDuelOutcome::Title;
                Mode->MenuSelection = 0;
                if (!TestTrue(TEXT("Fresh process finds the saved opening"), Mode->CanContinue())) return false;
                Mode->Confirm();
                Mode->Tick(0);
                if (!TestTrue(TEXT("Continue restores the correct lock in the starting cell until the first rest"),
                    Mode->Outcome() == EDuelOutcome::Ready && Player.SelectedCharacter() == Character &&
                    Matches(Tower.State) && Tower.IsGateOpen(CellDoor) == bOpened &&
                    Player.GetActorLocation().Equals(SpawnSurface() + FVector(0, 0, 62), .1))) return false;
            }
            AddInfo(FString::Printf(TEXT("OpeningCellRestart phase=%s hero=%s process=%s"), *OpeningStage, *Hero, *RestartStage));
            return !HasAnyErrors();
        }
        const int32 RequiredVictories[] = {0, 0, 1, 1, 2, 3};
        const int32 NeededVictories = RequiredVictories[FloorNumber];
        const uint8 ProgressMask = (1u << NeededVictories) - 1u;
        const FCheckpoint* Rest = Checkpoints().FindByPredicate([&](const FCheckpoint& C) {return uint8(C.Floor) == uint8(RestFloor);});
        if (!TestNotNull(TEXT("Canonical rest point for the restart case"), Rest)) return false;
        if (!bRead)
        {
            if (!TestTrue(TEXT("Select the exact protagonist for the disk fixture"), Player.SelectCharacter(Character))) return false;
            Tower.NewGame(*Mode);
            if (!TestTrue(TEXT("Checkpoint fixture starts after the mandatory opening"),
                Tower.State.ReadClue(EDuelNorthClue::OpeningDocket) && Tower.State.OpenStartingCell())) return false;
            for (int32 I = 0; I < NeededVictories; ++I)
            {
                if (!TestTrue(TEXT("Seed prerequisite investigation and victory for the independent checkpoint"),
                    DuelNorthInvestigationQA::Prepare(Tower.State, I) && Tower.State.WinBattle(I))) return false;
                if (I == 2 && !TestTrue(TEXT("Seed the single court reunion"),
                    Tower.State.CompleteDialogue(EDuelNorthDialogue::FinalExit))) return false;
                if (!TestTrue(TEXT("Seed prerequisite relic and lock for the independent checkpoint"),
                    Tower.State.PickupRelic(static_cast<EDuelNorthRelic>(I)) && Tower.State.OpenDoor(static_cast<EDuelNorthDoor>(I)))) return false;
            }
            Tower.State.bExpeditionLoot = !bLegacyGear;
            Tower.State.CacheBits = bLegacyGear ? 0 : DuelNorthFinds::AllCacheBits();
            Player.SetWeapon(Weapon);
            Player.SetWeapon(EDuelWeapon::Greatshield, EDuelHand::Left);
            Player.RightReserve = EDuelWeapon::RitualSword;
            Player.LeftReserve = EDuelWeapon::Spear;
            Player.ArtSelection = Art;
            Player.PreparedSpells = {EDuelArt::SableBlade, bLegacyGear ? EDuelArt::IceLance : EDuelArt::Default, EDuelArt::WitchRain};
            Player.ActiveSpellSlot = 2;
            Player.HealthFlaskAllocation = 2;
            Tower.RefreshGates();
            Player.SetActorLocation(Rest->Position + FVector(0, 0, 62), false, nullptr, ETeleportType::TeleportPhysics);
            Mode->Tick(0);
            if (!TestEqual(TEXT("Rest station is targeted by the normal proximity path"), Tower.Nearby, Rest->Id)) return false;
            Mode->Confirm();
            if (bGarden)
            {
                if (!TestTrue(TEXT("Prepare a completed garden checkpoint"), Tower.State.ViewEnding())) return false;
                Tower.Save();
            }
            const auto* Disk = Cast<UDuelNorthSave>(UGameplayStatics::LoadGameFromSlot(TestSlot, 0));
            TestTrue(TEXT("Write process leaves the requested checkpoint and equipment on disk"), Disk &&
                Disk->State.Checkpoint == uint8(RestFloor) && Disk->State.Player() == Character && Disk->State.Wins == ProgressMask &&
                Disk->State.bEndingSeen == bGarden && Disk->State.IsOpeningCellOpen() && !Disk->State.UsesStartingCellCheckpoint() &&
                Disk->State.HasClue(EDuelNorthClue::OpeningDocket) && Disk->Weapons.Num() == 4 && Disk->Weapons[0] == uint8(Weapon) &&
                Disk->State.bExpeditionLoot == !bLegacyGear && Disk->State.CacheBits == Tower.State.CacheBits);
            AddInfo(FString::Printf(TEXT("Restart fixture written: %s at floor %d, seed earlier victories=%d"), *Hero, FloorNumber, NeededVictories));
            return !HasAnyErrors();
        }

        Mode->Result = EDuelOutcome::Title;
        Mode->MenuSelection = 0;
        if (!TestTrue(TEXT("Fresh process discovers a previously written Continue slot"), Mode->CanContinue())) return false;
        Mode->Confirm();
        Mode->Tick(0);
        TestTrue(TEXT("Continue restores role, rested floor, encounter, door and relic masks"),
            Mode->Outcome() == EDuelOutcome::Ready && Player.SelectedCharacter() == Character && Tower.CurrentFloor() == static_cast<EFloor>(FloorNumber) &&
            Tower.State.Checkpoint == uint8(RestFloor) && Tower.State.Wins == ProgressMask && Tower.State.Doors == ProgressMask &&
            Tower.State.Relics == ProgressMask && Tower.State.IsOpeningCellOpen() && Tower.IsGateOpen(TEXT("CellDoor")) &&
            Tower.State.HasClue(EDuelNorthClue::OpeningDocket) && !Tower.State.UsesStartingCellCheckpoint());
        TestTrue(TEXT("Continue restores loadout, art, spells and flask allocation"), Player.Equipped == Weapon &&
            Player.Offhand == EDuelWeapon::Greatshield && Player.RightReserve == EDuelWeapon::RitualSword && Player.LeftReserve == EDuelWeapon::Spear &&
            Player.ArtSelection == Art && Player.PreparedSpells[0] == EDuelArt::SableBlade &&
            Player.PreparedSpells[1] == (bLegacyGear ? EDuelArt::IceLance : EDuelArt::Default) &&
            Player.ActiveSpellSlot == 2 && Player.HealthFlaskAllocation == 2);
        TestTrue(TEXT("Continue preserves the save generation and inventory ownership"),
            Tower.State.bExpeditionLoot == !bLegacyGear &&
            Tower.State.CacheBits == (bLegacyGear ? 0 : DuelNorthFinds::AllCacheBits()) &&
            Tower.State.OwnsWeapon(EDuelWeapon::Greatshield) && Tower.State.KnowsSpell(EDuelArt::SableBlade));
        if (bGarden)
        {
            TestTrue(TEXT("Completed title Continue restores the garden and companion state"), Tower.State.bEndingSeen &&
                Tower.State.bCompanionFollowing && Tower.IsGateOpen(TEXT("TowerExit")) && Tower.Narrative());
        }
        else
        {
            // Death fixture tests respawn at each saved station. LiveMatches owns damage/AI win validation.
            Mode->Result = EDuelOutcome::Fighting;
            Player.Health = 0;
            Player.Focus = 0;
            Player.Flasks = 0;
            Mode->FighterFell(&Player);
            if (!TestTrue(TEXT("Defeat opens the actual retry flow"), Mode->Outcome() == EDuelOutcome::Defeat)) return false;
            Mode->Confirm();
            Mode->Tick(0);
            TestTrue(TEXT("F retry restores health, focus, allocated flasks and the saved station"),
                Mode->Outcome() == EDuelOutcome::Ready && Player.Health == DuelCourt::PlayerMaxHealth &&
                Player.Focus == DuelCourt::MaxFocus && Player.Flasks == 2 && Tower.CurrentFloor() == static_cast<EFloor>(FloorNumber) &&
                Player.GetActorLocation().Equals(Rest->Position + FVector(0, 0, 62), .1));
            TestTrue(TEXT("Retry preserves loaded progress and loadout"), Tower.State.Wins == ProgressMask && Tower.State.Doors == ProgressMask &&
                Tower.State.Relics == ProgressMask && Player.Equipped == Weapon && Player.Offhand == EDuelWeapon::Greatshield &&
                Tower.State.CacheBits == (bLegacyGear ? 0 : DuelNorthFinds::AllCacheBits()) &&
                Player.SpellAt(1) == (bLegacyGear ? EDuelArt::IceLance : EDuelArt::Default) && Player.ActiveSpellSlot == 2);
        }
        AddInfo(FString::Printf(TEXT("Restart fixture read via real Continue: %s at floor %d; death fixture=%d"), *Hero, FloorNumber, !bGarden));
        return !HasAnyErrors();
    }
    APlayerController* Controller = Cast<APlayerController>(Player.GetController());
    if (!TestTrue(TEXT("Keyboard input is available"), Controller && Controller->PlayerInput && Player.InputComponent)) return false;
    const TArray<UInputComponent*> InputStack{Player.InputComponent};
    constexpr float Step = 1.f / 60.f;
    const FDuelNorthProgress OriginalState = Tower.State;
    const auto OriginalRooms = Tower.Explored;
    const auto OriginalCharacter = Player.SelectedCharacter();
    const auto OriginalBoss = Mode->BossFighter->SelectedCharacter();
    const auto OriginalOutcome = Mode->Result;
    const auto OriginalTransform = Player.GetActorTransform();
    const auto OriginalGear = TArray<EDuelWeapon>{Player.Equipped, Player.Offhand, Player.RightReserve, Player.LeftReserve};
    const auto OriginalSpells = Player.PreparedSpells;
    const auto OriginalArt = Player.ArtSelection;
    const int32 OriginalSpell = Player.ActiveSpellSlot, OriginalFlasks = Player.HealthFlaskAllocation;
    const bool OriginalCheckpoint = Tower.bCheckpointAvailable;
    const bool bOriginalWayLight = Tower.bWayLightEnabled;
    ON_SCOPE_EXIT
    {
        Controller->FlushPressedKeys();
        UGameplayStatics::SetGamePaused(Mode, false);
        Player.EndCombat();
        Player.SelectCharacter(OriginalCharacter);
        Player.SetWeapon(OriginalGear[0]); Player.SetWeapon(OriginalGear[1], EDuelHand::Left);
        Player.RightReserve = OriginalGear[2]; Player.LeftReserve = OriginalGear[3];
        Player.PreparedSpells = OriginalSpells; Player.ArtSelection = OriginalArt;
        Player.ActiveSpellSlot = OriginalSpell; Player.HealthFlaskAllocation = OriginalFlasks;
        Mode->SetBoss(OriginalBoss);
        Tower.State = OriginalState; Tower.Explored = OriginalRooms;
        Tower.Respawn(*Mode);
        Player.SetActorTransform(OriginalTransform, false, nullptr, ETeleportType::TeleportPhysics);
        Mode->Result = OriginalOutcome;
        Tower.bCheckpointAvailable = OriginalCheckpoint;
        Tower.bWayLightEnabled = bOriginalWayLight;
        Tower.UpdateWayLight(*Mode, false);
        Tower.Nearby = NAME_None;
        Tower.Notice.Reset(); Tower.NoticeUntil = 0;
        if (bDisk) UGameplayStatics::DeleteGameInSlot(TestSlot, 0);
    };

    const auto KeyFrame = [&](FKey Key, EInputEvent Event)
    {
        Controller->InputKey(FInputKeyParams(Key, Event, Event == IE_Pressed ? 1.0 : 0.0));
        Controller->PlayerInput->ProcessInputStack(InputStack, Step, false);
        Mode->Tick(Step);
    };
    const auto TapConfirm = [&](FKey Key = EKeys::F) { KeyFrame(Key, IE_Pressed); KeyFrame(Key, IE_Released); };
    const auto TapMap = [&]() { KeyFrame(EKeys::M, IE_Pressed); KeyFrame(EKeys::M, IE_Released); };
    const auto TapWayLight = [&]() { KeyFrame(EKeys::G, IE_Pressed); KeyFrame(EKeys::G, IE_Released); };
    const auto TapEscape = [&]() { KeyFrame(EKeys::Escape, IE_Pressed); KeyFrame(EKeys::Escape, IE_Released); };
    const auto Place = [&](const FVector& Feet)
    {
        Player.EndCombat(); Player.PrepareExploration();
        Player.GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        Player.SetActorLocation(Feet + FVector(0, 0, Player.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f),
            false, nullptr, ETeleportType::TeleportPhysics);
        Mode->Tick(0.f);
    };
    const auto Touch = [&](FName Marker)
    {
        const auto* At = FindLandmark(Marker);
        if (!TestNotNull(*FString::Printf(TEXT("Interaction marker %s exists"), *Marker.ToString()), At)) return false;
        const auto* Ledger = DuelNorthInteractables::Ledger(Marker);
        Place(Ledger ? Ledger->Approach() : At->Position);
        if (!TestEqual(TEXT("Actual proximity selection finds the requested interaction"), Tower.Nearby, Marker)) return false;
        const FVector Before = Player.GetActorLocation();
        Mode->Confirm();
        TestTrue(TEXT("F interaction preserves the player's physical location"), Player.GetActorLocation().Equals(Before, .01f));
        return true;
    };
    const auto FinishConversation = [&]()
    {
        const auto* Narrative = Tower.Narrative();
        const int32 LineCount = Narrative->Subtitle().Count;
        for (int32 Press = 0; Press < LineCount && Narrative->IsTalking(); ++Press) Mode->Confirm();
        return TestFalse(TEXT("Closing every authored line restores the interaction controls"), Narrative->IsTalking());
    };
    const auto FinishInvestigation = [&](FName Marker)
    {
        const auto* Story = Tower.Narrative();
        if (Story && Story->IsReadingDocument())
        {
            if (!TestTrue(TEXT("The acquired document holds input and appears before the protagonists analyse it"),
                !Story->DocumentText().IsEmpty() && Story->Subtitle().Text.IsEmpty() && !Mode->CanExplore())) return false;
            Mode->Confirm();
            if (Story->IsReadingDocument()) Mode->Confirm();
        }
        const auto Script = Tower.State.InvestigationScript(Marker);
        if (Script.Lines.IsEmpty()) return true;
        if (!TestTrue(TEXT("Reading or solving starts the available deduction before movement resumes"),
            Story && Story->IsTalking() && !Mode->CanExplore())) return false;
        const auto Subtitle = Story->Subtitle();
        return TestTrue(TEXT("The first portrait line belongs to the selected protagonist"),
            Subtitle.Line == 1 && Subtitle.Character == Tower.State.Player() && Subtitle.Text == Script.Lines[0].Text) &&
            FinishConversation() && TestTrue(TEXT("Finishing the deduction restores movement"), Mode->CanExplore());
    };
    const auto ReadRecord = [&](FName Marker, EDuelNorthClue Clue)
    {
        return Touch(Marker) && FinishInvestigation(Marker) && TestTrue(TEXT("F records the discovered evidence"), Tower.State.HasClue(Clue));
    };
    const auto SolvePuzzle = [&](FName Marker)
    {
        const int32 Index = DuelNorthFinds::IndexOf(Marker);
        if (!TestTrue(TEXT("Required puzzle has a physical fixture"), DuelNorthFinds::All().IsValidIndex(Index))) return false;
        const auto& Puzzle = DuelNorthFinds::All()[Index];
        Place(Puzzle.Position + Puzzle.Front() * (Puzzle.Size.Y * .5f + 78.f));
        if (!TestEqual(TEXT("Approaching the prop selects the required puzzle"), Tower.Nearby, Marker)) return false;
        TapConfirm();
        if (!TestTrue(TEXT("F opens the puzzle at the actual object after its clues"),
            Tower.ActivePuzzle == Index && !Mode->CanExplore())) return false;
        for (int32 Round = 0; Round < 2; ++Round)
        {
            if (!TestEqual(TEXT("The previous answer reaches the next puzzle round"), Tower.PuzzleRound, Round)) return false;
            for (int32 Choice = 0; Choice < 3 && !DuelNorthFinds::Correct(Index, Round, Tower.PuzzleChoice); ++Choice)
            {
                KeyFrame(EKeys::D, IE_Pressed);
                KeyFrame(EKeys::D, IE_Released);
            }
            if (!TestTrue(TEXT("D can select the answer supported by the evidence"),
                DuelNorthFinds::Correct(Index, Round, Tower.PuzzleChoice))) return false;
            TapConfirm();
        }
        return FinishInvestigation(Marker) && TestTrue(TEXT("F commits both answers and restores movement"),
            Tower.ActivePuzzle == INDEX_NONE && (Tower.State.CacheBits & (1u << Index)) != 0 && Mode->CanExplore());
    };
    const auto TouchDoor = [&](FName Marker)
    {
        const auto* Portal = Portals().FindByPredicate([&](const FPortal& P) { return P.Id == Marker; });
        if (!TestNotNull(TEXT("Locked door has an authored portal"), Portal)) return false;
        const FRoom* Approach = FindRoom(Portal->RoomA);
        if (!TestNotNull(TEXT("Door approach room exists"), Approach)) return false;
        const float Sign = FVector::DotProduct(Approach->Bounds.GetCenter() - Portal->Center, Portal->Normal) >= 0 ? 1.f : -1.f;
        // Stand on the approach side. Never teleport through the locked panel to make its F test pass.
        Place(Portal->Center + Portal->Normal * (150.f * Sign));
        if (!TestEqual(TEXT("Door can be selected from its genuine approach side"), Tower.Nearby, Marker)) return false;
        Mode->Confirm();
        return true;
    };

    int32 SavedTransitions = 0;
    const auto CheckSnapshot = [&](const TCHAR* Stage)
    {
        const FDuelNorthProgress Expected = Tower.State;
        const auto Character = Player.SelectedCharacter();
        const auto Weapons = TArray<uint8>{uint8(Player.Equipped), uint8(Player.Offhand), uint8(Player.RightReserve), uint8(Player.LeftReserve)};
        const auto Spells = TArray<uint8>{uint8(Player.PreparedSpells[0]), uint8(Player.PreparedSpells[1]), uint8(Player.PreparedSpells[2])};
        const uint8 Art = uint8(Player.ArtSelection), Active = uint8(Player.ActiveSpellSlot);
        const int32 Flasks = Player.HealthFlaskAllocation;
        UDuelNorthSave* Saved = nullptr;
        if (bDisk)
        {
            Tower.Save();
            Saved = Cast<UDuelNorthSave>(UGameplayStatics::LoadGameFromSlot(TestSlot, 0));
            if (!TestNotNull(*FString::Printf(TEXT("%s is saved in the QA-only slot"), Stage), Saved)) return false;
        }
        else
        {
            // NoSave runs cover the actual reflected save format without touching any disk slot.
            auto* Source = NewObject<UDuelNorthSave>();
            Source->State = Expected; Source->Rooms = Tower.Explored.Array();
            Source->Weapons = Weapons; Source->Art = Art; Source->Spells = Spells;
            Source->ActiveSpell = Active; Source->HealthFlasks = Flasks; Source->bHasLoadout = true;
            TArray<uint8> Bytes;
            if (!TestTrue(TEXT("Checkpoint UObject serializes to memory"), UGameplayStatics::SaveGameToMemory(Source, Bytes))) return false;
            Saved = Cast<UDuelNorthSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
            if (!TestNotNull(TEXT("Checkpoint UObject deserializes from memory"), Saved)) return false;
        }
        FString Reason;
        TestTrue(TEXT("Round-tripped progress validates"), Saved->State.Validate(Reason));
        TestTrue(TEXT("Snapshot preserves the required opener and the first actual prison rest"),
            Saved->State.bOpeningCellRequired == Expected.bOpeningCellRequired &&
            Saved->State.bOpeningCellUnlocked == Expected.bOpeningCellUnlocked &&
            Saved->State.bPrisonRestActivated == Expected.bPrisonRestActivated);
        TestTrue(TEXT("Snapshot preserves the owning loot set"),
            Saved->State.CacheBits == Expected.CacheBits && Saved->State.bExpeditionLoot == Expected.bExpeditionLoot);
        TestEqual(TEXT("All recorded prison mail evidence survives the checkpoint"), Saved->State.BeaconBits, Expected.BeaconBits);
        TestTrue(TEXT("Snapshot preserves separate wins, pickups, doors and dialogue"),
            Saved->State.PlayerCharacter == Expected.PlayerCharacter && Saved->State.Wins == Expected.Wins &&
            Saved->State.Relics == Expected.Relics && Saved->State.Doors == Expected.Doors &&
            Saved->State.Dialogues == Expected.Dialogues && Saved->State.Checkpoint == Expected.Checkpoint);
        for (uint8 Index = 0; Index < static_cast<uint8>(EDuelNorthClue::Count); ++Index)
        {
            const auto Clue = static_cast<EDuelNorthClue>(Index);
            TestEqual(TEXT("Optional and main clue discoveries survive the checkpoint"),
                Saved->State.HasClue(Clue), Expected.HasClue(Clue));
        }
        for (uint8 Index = 0; Index < static_cast<uint8>(EDuelNorthDialogue::Count); ++Index)
        {
            const auto Dialogue = static_cast<EDuelNorthDialogue>(Index);
            TestEqual(TEXT("Every conversation completion survives the checkpoint"),
                Saved->State.HasDialogue(Dialogue), Expected.HasDialogue(Dialogue));
        }
        TestTrue(TEXT("Snapshot retains both hands, reserves, art, prepared spells and flask allocation"),
            Saved->bHasLoadout && Saved->Weapons == Weapons && Saved->Art == Art && Saved->Spells == Spells &&
            Saved->ActiveSpell == Active && Saved->HealthFlasks == Flasks);
        if (bDisk)
        {
            // Deliberately replace live selections so Continue must restore them, not merely retain them.
            Player.SelectCharacter(Character == EDuelCharacter::Ema ? EDuelCharacter::Hiro : EDuelCharacter::Ema);
            Player.SetWeapon(EDuelWeapon::Rapier); Player.SetWeapon(EDuelWeapon::Buckler, EDuelHand::Left);
            Player.RightReserve = EDuelWeapon::Hammer; Player.LeftReserve = EDuelWeapon::Empty;
            Player.ArtSelection = EDuelArt::Default; Player.ActiveSpellSlot = 0; Player.HealthFlaskAllocation = 1;
            Mode->Result = EDuelOutcome::Title; Mode->MenuSelection = 0;
            TestTrue(TEXT("Actual title offers Continue after QA save"), Mode->CanContinue());
            Mode->Confirm();
            TestTrue(TEXT("Continue loads the route through actual Mode confirmation"),
                Mode->Outcome() == EDuelOutcome::Ready && Player.SelectedCharacter() == Character &&
                Tower.State.Wins == Expected.Wins && Tower.State.Relics == Expected.Relics && Tower.State.Doors == Expected.Doors);
            TestTrue(TEXT("Continue restores weapons instead of retaining modified live gear"),
                uint8(Player.Equipped) == Weapons[0] && uint8(Player.Offhand) == Weapons[1] &&
                uint8(Player.RightReserve) == Weapons[2] && uint8(Player.LeftReserve) == Weapons[3] &&
                uint8(Player.ArtSelection) == Art && Player.HealthFlaskAllocation == Flasks);
            TestTrue(TEXT("Continue restores spell order and selected slot"), uint8(Player.PreparedSpells[0]) == Spells[0] &&
                uint8(Player.PreparedSpells[1]) == Spells[1] && uint8(Player.PreparedSpells[2]) == Spells[2] && Player.ActiveSpellSlot == Active);
        }
        ++SavedTransitions;
        return true;
    };

    for (EDuelCharacter Character : {EDuelCharacter::Hiro, EDuelCharacter::Ema})
    {
        Player.EndCombat();
        Mode->Result = EDuelOutcome::Title; Mode->MenuSelection = 1;
        const FKey SelectKey = Character == EDuelCharacter::Ema ? EKeys::A : EKeys::D;
        Player.SelectCharacter(Character == EDuelCharacter::Ema ? EDuelCharacter::Hiro : EDuelCharacter::Ema);
        KeyFrame(SelectKey, IE_Pressed); KeyFrame(SelectKey, IE_Released);
        TestEqual(TEXT("A/D chooses the protagonist on the new game row"), uint8(Player.SelectedCharacter()), uint8(Character));
        Mode->Confirm();
        TestTrue(TEXT("Title New Game resets progress and starts inside the locked prison cell"),
            Mode->Outcome() == EDuelOutcome::Ready && Tower.State.Wins == 0 && Tower.State.Relics == 0 &&
            Tower.State.Doors == 0 && Tower.State.Player() == Character && Tower.State.UsesStartingCellCheckpoint() &&
            !Tower.State.HasClue(EDuelNorthClue::OpeningDocket) && !Tower.State.IsOpeningCellOpen() &&
            !Tower.IsGateOpen(TEXT("CellDoor")) && FVector::DistSquared2D(Player.GetActorLocation(), SpawnSurface()) < 1.f);
        TestTrue(TEXT("New Game equips the character's own weapon and both initial spells"),
            Tower.State.bExpeditionLoot && Tower.State.CacheBits == 0 && Player.Equipped == DuelCourt::PersonalWeapon(Character) &&
            Player.Offhand == EDuelWeapon::Buckler && Player.RightReserve == EDuelWeapon::Staff &&
            Player.LeftReserve == EDuelWeapon::Empty && Player.SpellAt(0) == EDuelArt::IceLance &&
            Player.SpellAt(1) == EDuelArt::WitchRain && Player.SpellAt(2) == EDuelArt::Default && Player.ActiveSpellSlot == 0);
        Player.RequestWeapon(EDuelWeapon::Greatblade);
        TestEqual(TEXT("A direct equipment request cannot bypass weapon ownership"), uint8(Player.Equipped), uint8(DuelCourt::PersonalWeapon(Character)));
        Player.AdjustWeaponSlot(EDuelHand::Right, false, 1);
        TestEqual(TEXT("Keyboard configuration walks only owned, legal main hand choices"), uint8(Player.Equipped), uint8(EDuelWeapon::Buckler));
        Player.SetWeapon(DuelCourt::PersonalWeapon(Character));
        Player.SetSpellSlot(2, EDuelArt::PrismSeal);
        TestEqual(TEXT("Locked spell cannot be assigned into an empty slot"), uint8(Player.SpellAt(2)), uint8(EDuelArt::Default));
        Player.CycleSpell();
        TestEqual(TEXT("First cycle selects the second starting spell"), uint8(Player.SelectedSpell()), uint8(EDuelArt::WitchRain));
        Player.CycleSpell();
        TestEqual(TEXT("Second cycle skips the empty slot"), uint8(Player.SelectedSpell()), uint8(EDuelArt::IceLance));
        Player.SetSpellSlot(0, EDuelArt::Default);
        Player.SetSpellSlot(1, EDuelArt::Default);
        Player.SetWeapon(EDuelWeapon::Staff);
        const float Focus = Player.Focus;
        TestFalse(TEXT("A staff with no prepared spells cannot perform a hidden Ice Lance cast"), Player.StartMove(EDuelMove::Crimson));
        TestTrue(TEXT("Empty spell does not change focus or enter a cast"),
            Player.Equipped == EDuelWeapon::Staff && Player.SelectedSpell() == EDuelArt::Default && Player.Focus == Focus &&
            Player.Move == EDuelMove::Idle);
        Player.ResetNorthLoadout(Tower.State.bExpeditionLoot);
        TestFalse(TEXT("New game never starts inside combat"), Mode->GateOpen());
        if (!bDisk) TestFalse(TEXT("NoSave does not expose or load the user's checkpoint"), Mode->CanContinue());

        // The actual pause choice changes both the player and the story route before the first victory.
        Mode->TogglePause();
        int32 CharacterOption = INDEX_NONE;
        for (int32 I = 0; I < Mode->PauseOptionCount(); ++I)
            if (Mode->PauseOption(I) == EDuelPause::Player) CharacterOption = I;
        if (!TestTrue(TEXT("Pre-battle pause exposes protagonist selection"), CharacterOption != INDEX_NONE)) return false;
        Mode->MenuSelection = CharacterOption; Mode->Confirm();
        TestTrue(TEXT("Pause character choice updates player and storyline together"), Player.SelectedCharacter() != Character &&
            Tower.State.Player() == Player.SelectedCharacter() &&
            Player.Equipped == DuelCourt::PersonalWeapon(Player.SelectedCharacter()) && Player.RightReserve == EDuelWeapon::Staff);
        Mode->Confirm(); Mode->TogglePause();
        TestEqual(TEXT("Selecting again returns to the requested route"), uint8(Player.SelectedCharacter()), uint8(Character));

        const FName CellDoorId(TEXT("CellDoor")), DocketId(TEXT("OpeningDocket"));
        const FPortal* CellDoor = Portals().FindByPredicate([&](const FPortal& P) { return P.Id == CellDoorId; });
        const FLandmark* DoorHandle = FindLandmark(CellDoorId);
        const FLandmark* Docket = FindLandmark(DocketId);
        const UBoxComponent* CellBlocker = Tower.Gates.FindRef(CellDoorId).Get();
        if (!TestTrue(TEXT("Cell, door handle, docket and gate collider are present"), CellDoor && DoorHandle && Docket && CellBlocker))
            return false;
        Place(SpawnSurface());
        const auto* WayLight = Tower.WayLight.Get();
        if (!TestNotNull(TEXT("A warm way light is attached for exploration"), WayLight)) return false;
        TestTrue(TEXT("The way light uses tower-only lighting and retains mapped shadows"),
            !WayLight->LightingChannels.bChannel0 && !WayLight->LightingChannels.bChannel1 &&
            WayLight->LightingChannels.bChannel2 && WayLight->CastShadows &&
            WayLight->GetCastRaytracedShadow() == ECastRayTracedShadow::Disabled);
        const TInlineComponentArray<UMeshComponent*> PlayerMeshes(&Player);
        TestTrue(TEXT("Body and both equipped hands keep their scene lighting and are excluded from the way light"),
            PlayerMeshes.Num() > 3 && !PlayerMeshes.ContainsByPredicate([](const UMeshComponent* Mesh)
            {
                return !Mesh->LightingChannels.bChannel0 || Mesh->LightingChannels.bChannel2;
            }));
        const TInlineComponentArray<UMeshComponent*> TowerMeshes(&Tower);
        TestTrue(TEXT("The tower shell still receives scene lighting and occludes the way light"),
            TowerMeshes.ContainsByPredicate([](const UMeshComponent* Mesh)
            {
                return Mesh->ComponentHasTag(TEXT("NorthTowerShell")) && Mesh->CastShadow &&
                    Mesh->LightingChannels.bChannel0 && Mesh->LightingChannels.bChannel2;
            }));
        const bool bWayLightPreference = Tower.bWayLightEnabled;
        TapWayLight();
        TestTrue(TEXT("G toggles the actual exploration beam"), WayLight->IsVisible() == !bWayLightPreference);
        TapWayLight();
        TestTrue(TEXT("A second G restores the previous beam setting"), WayLight->IsVisible() == bWayLightPreference);
        TapMap();
        TestTrue(TEXT("M opens the actual map and blocks exploration actions"), Tower.MapVisible() && !Mode->CanExplore() && !Player.CanUseEquipment());
        FVector MapTarget;
        TestTrue(TEXT("The first map objective points at the docket inside the locked room"),
            Tower.MapObjectivePosition(MapTarget) && MapTarget.Equals(Tower.LandmarkPosition(*Docket), .1));
        Player.Cut();
        TestEqual(TEXT("Attack cannot leak through an open map"), uint8(Player.Move), uint8(EDuelMove::Idle));
        TapEscape();
        TestTrue(TEXT("First Escape closes the map without opening Pause"), !Tower.MapVisible() && Mode->CanExplore() &&
            !UGameplayStatics::IsGamePaused(Mode));
        TapEscape();
        TestTrue(TEXT("The next Escape opens Pause with the map closed"), !Tower.MapVisible() && UGameplayStatics::IsGamePaused(Mode));
        TapEscape();
        TapMap();
        TapMap();
        TestTrue(TEXT("Closing M restores exploration and equipment input"), !Tower.MapVisible() && Mode->CanExplore() && Player.CanUseEquipment());
        Player.Cut();
        TestTrue(TEXT("Attack works again after closing map"), Player.Move != EDuelMove::Idle);
        Player.EndCombat();
        const FVector BeforeWalk = Player.GetActorLocation();
        for (int32 Frame = 0; Frame < 12; ++Frame)
        {
            Player.AddMovementInput(FVector(0, -1, 0), 1.f, true);
            Player.GetCharacterMovement()->TickComponent(Step, LEVELTICK_All, nullptr);
        }
        TestTrue(TEXT("Movement resumes after closing map"), FVector::DistSquared2D(BeforeWalk, Player.GetActorLocation()) > FMath::Square(20.f));

        const UCapsuleComponent* Capsule = Player.GetCapsuleComponent();
        const FVector SweepZ(0, 0, Capsule->GetScaledCapsuleHalfHeight() + 2.f);
        const FVector DoorApproach = Tower.LandmarkPosition(*DoorHandle);
        const FVector Outward = (CellDoor->Center - DoorApproach).GetSafeNormal2D();
        const FVector Outside = CellDoor->Center + Outward * 140.f;
        const auto SweepDoor = [&](FHitResult& Hit)
        {
            FCollisionQueryParams Query(SCENE_QUERY_STAT(NorthOpeningCell), false, &Player);
            Query.AddIgnoredActor(Mode->BossFighter);
            return Mode->GetWorld()->SweepSingleByChannel(Hit, DoorApproach + SweepZ, Outside + SweepZ,
                FQuat::Identity, Capsule->GetCollisionObjectType(),
                FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Query);
        };
        Place(DoorApproach);
        TapConfirm();
        FHitResult Hit;
        if (!TestTrue(TEXT("F before the docket leaves the cell blocker across the player's path"),
            !Tower.State.IsOpeningCellOpen() && !Tower.IsGateOpen(CellDoorId) && SweepDoor(Hit) && Hit.GetComponent() == CellBlocker))
            return false;

        Place(Tower.LandmarkPosition(*Docket));
        if (!TestEqual(TEXT("Player proximity selects the in-cell docket"), Tower.Nearby, DocketId)) return false;
        const FVector AtDocket = Player.GetActorLocation();
        TapConfirm(EKeys::Enter);
        if (!TestTrue(TEXT("The live opening first presents the companion's original note"),
            Tower.Narrative()->IsReadingDocument() && Tower.Narrative()->DocumentText() ==
                Tower.State.ClueText(EDuelNorthClue::OpeningDocket) && Tower.Narrative()->Subtitle().Text.IsEmpty())) return false;
        if (!FinishInvestigation(DocketId)) return false;
        if (!TestTrue(TEXT("Bound Enter reads the docket in place and leaves the door shut"),
            Tower.State.HasClue(EDuelNorthClue::OpeningDocket) && !Tower.State.IsOpeningCellOpen() && !Tower.IsGateOpen(CellDoorId) &&
            Player.GetActorLocation().Equals(AtDocket, .01))) return false;
        TestTrue(TEXT("The second objective directs the player to the inside door handle"),
            Tower.MapObjectivePosition(MapTarget) && MapTarget.Equals(DoorApproach, .1));

        Place(DoorApproach);
        if (!TestEqual(TEXT("Player proximity selects the door handle after reading"), Tower.Nearby, CellDoorId)) return false;
        const FVector BeforeUnlock = Player.GetActorLocation();
        TapConfirm();
        if (!TestTrue(TEXT("Separate bound F unlocks the door in place and clears its physical collision"),
            Tower.State.IsOpeningCellOpen() && Tower.IsGateOpen(CellDoorId) && !SweepDoor(Hit) &&
            Player.GetActorLocation().Equals(BeforeUnlock, .01))) return false;
        auto& Movement = *Player.GetCharacterMovement();
        bool bExited = false;
        for (int32 Frame = 0; Frame < 60; ++Frame)
        {
            Player.AddMovementInput(Outward, 1.f, true);
            Movement.TickComponent(Step, LEVELTICK_All, nullptr);
            Mode->Tick(Step);
            if (Locate(Player.GetActorLocation(), Capsule->GetScaledCapsuleHalfHeight()).Room == CellDoor->RoomB)
            {
                bExited = true;
                break;
            }
        }
        Movement.StopMovementImmediately();
        if (!TestTrue(TEXT("Normal movement exits into the prison passage after unlocking"), bExited && Mode->CanExplore())) return false;

        if (!TestFalse(TEXT("The first fog requires the ration investigation"), Tower.State.CanEnterBattle(0)) ||
            !ReadRecord(TEXT("RefugeLedger"), EDuelNorthClue::RefugeLedger) || !SolvePuzzle(TEXT("FindRations")) ||
            !TestTrue(TEXT("The completed ration evidence enables the first fog"), Tower.State.CanEnterBattle(0))) return false;

        const FName FogIds[] = {TEXT("DungeonGate"), TEXT("MirrorGate"), TEXT("FinalGate")};
        const FName RelicIds[] = {TEXT("RustKey"), TEXT("MirrorKey"), TEXT("NameTag")};
        const FName DoorIds[] = {TEXT("ChapelDoor"), TEXT("FinalStairDoor"), TEXT("TowerExit")};
        const EDuelNorthDialogue Reunions[] = {EDuelNorthDialogue::RescuedCompanion,
            EDuelNorthDialogue::MirrorExit, EDuelNorthDialogue::FinalExit};
        const FName CombatExitIds[] = {TEXT("DungeonAfterDoor"), TEXT("FinalStairDoor"), TEXT("FinalExitInner")};
        const auto ExitBlocksCapsule = [&](FName Id)
        {
            const FPortal* Exit = Portals().FindByPredicate([&](const FPortal& P) { return P.Id == Id; });
            if (!Exit) { AddError(TEXT("A combat exit is missing from the authored rooms")); return false; }
            const UCapsuleComponent* Body = Player.GetCapsuleComponent();
            const FVector Centre = Exit->Center + FVector(0, 0, Body->GetScaledCapsuleHalfHeight() + 2.);
            FCollisionQueryParams Trace(SCENE_QUERY_STAT(NorthCombatExit), false, &Player);
            Trace.AddIgnoredActor(Mode->BossFighter.Get());
            FHitResult Hit;
            return Mode->GetWorld()->SweepSingleByChannel(Hit, Centre - Exit->Normal * 150., Centre + Exit->Normal * 150.,
                FQuat::Identity, Body->GetCollisionObjectType(),
                FCollisionShape::MakeCapsule(Body->GetScaledCapsuleRadius(), Body->GetScaledCapsuleHalfHeight()), Trace) &&
                Hit.GetActor() == &Tower;
        };
        const auto ReachPhysicalWalls = [&](ADuelFighter& Fighter, const FVector& Origin)
        {
            const auto* Body = Fighter.GetCapsuleComponent();
            const float Radius = Body->GetScaledCapsuleRadius(), HalfHeight = Body->GetScaledCapsuleHalfHeight();
            const FBox2D Walls = Mode->Arena->CourtBounds(), Centres = Mode->Arena->CourtBounds(Radius);
            const FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
            FCollisionQueryParams Trace(SCENE_QUERY_STAT(NorthCombatWall), false, &Fighter);
            Trace.AddIgnoredActor(Fighter.Target.Get());
            auto& Walk = *Fighter.GetCharacterMovement();
            const FTransform Restore = Fighter.GetActorTransform();
            ON_SCOPE_EXIT
            {
                Walk.StopMovementImmediately();
                Fighter.ConsumeMovementInputVector();
                Fighter.SetActorTransform(Restore, false, nullptr, ETeleportType::TeleportPhysics);
            };
            for (const FVector Direction : {FVector::ForwardVector, -FVector::ForwardVector,
                FVector::RightVector, -FVector::RightVector})
            {
                const int32 Axis = FMath::Abs(Direction.X) > .5 ? 0 : 1;
                const double Sign = Direction[Axis];
                const double Face = (Sign > 0 ? Walls.Max[Axis] : Walls.Min[Axis]) + Origin[Axis];
                const double Expected = (Sign > 0 ? Centres.Max[Axis] : Centres.Min[Axis]) + Origin[Axis];
                // Find an open stretch of the masonry between piers, furnishings and door jambs.
                FVector Start, Across;
                FHitResult Contact;
                bool bReachedWall = false;
                for (double Offset : {0., -240., 240., -420., 420., -600., 600.})
                {
                    Start = Origin + FVector(1050., -820., HalfHeight + 2.);
                    Start[Axis] = Face - Sign * (Radius + 185.);
                    Start[1 - Axis] += Offset;
                    Across = Start + Direction * (Radius + 285.);
                    const bool bBlocked = Mode->GetWorld()->SweepSingleByChannel(Contact, Start, Across, FQuat::Identity,
                        Body->GetCollisionObjectType(), CapsuleShape, Trace);
                    bReachedWall = bBlocked && Contact.GetActor() == &Tower && FMath::Abs(Contact.Location[Axis] - Expected) < 3.;
                    if (bReachedWall) break;
                }
                if (!TestTrue(FString::Printf(TEXT("A full-size combat capsule reaches the actual masonry face [origin=%s, start=%s, axis=%d, sign=%.0f, actor=%s, mesh=%s, position=%.2f, expected=%.2f]"),
                    *Origin.ToCompactString(), *Start.ToCompactString(), Axis, Sign, *GetNameSafe(Contact.GetActor()),
                    *GetNameSafe(Contact.GetComponent()), Contact.Location[Axis], Expected), bReachedWall))
                    return false;
                TestTrue(TEXT("Scripted combat movement stops at that same wall"),
                    Fighter.SweepNorthCourtTravel(Start, Across).Equals(Contact.Location, 3.));
                Fighter.SetActorLocation(Start, false, nullptr, ETeleportType::TeleportPhysics);
                Walk.StopMovementImmediately();
                Fighter.ConsumeMovementInputVector();
                Walk.SetMovementMode(MOVE_Walking);
                for (int32 Frame = 0; Frame < 55; ++Frame)
                {
                    Fighter.AddMovementInput(Direction, 1.f, true);
                    Walk.TickComponent(Step, LEVELTICK_All, nullptr);
                    Fighter.ClampToCourt();
                }
                const FVector At = Fighter.GetActorLocation();
                if (!TestTrue(TEXT("Normal combat movement uses the full court without escaping or falling"),
                    FMath::Abs(At[Axis] - Contact.Location[Axis]) < 6. &&
                    FMath::Abs(At.Z - Origin.Z - HalfHeight) < 6. && Walk.IsMovingOnGround())) return false;
            }
            return true;
        };
        for (int32 Battle = 0; Battle < FDuelNorthProgress::BattleCount; ++Battle)
        {
            if (!Touch(FogIds[Battle])) return false;
            if (!FinishConversation()) return false;
            if (!Mode->GateOpen()) Mode->Confirm();
            const FVector BeforeAdmission = Player.GetActorLocation();
            TestTrue(TEXT("F opens the fog but does not start combat or teleport the player"), Mode->GateOpen() &&
                Mode->Outcome() != EDuelOutcome::Fighting && Tower.IsGateOpen(FogIds[Battle]));
            TestEqual(TEXT("Opponent follows the protagonist's exact three-battle sequence"),
                uint8(Mode->BossFighter->SelectedCharacter()), uint8(Tower.State.BossForBattle(Battle)));
            ADuelFighter& WaitingBoss = *Mode->BossFighter;
            TestTrue(TEXT("The visible waiting boss has floor collision and uses its own capsule height"),
                WaitingBoss.GetActorEnableCollision() && WaitingBoss.GetActorLocation().Z -
                WaitingBoss.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() >= CourtOrigin(Battle).Z);
            for (int32 Frame = 0; Frame < 12; ++Frame)
            {
                WaitingBoss.Tick(Step);
                WaitingBoss.GetCharacterMovement()->TickComponent(Step, LEVELTICK_All, nullptr);
            }
            TestTrue(TEXT("Waiting at an opened fog does not make the boss fall through the floor"),
                FMath::Abs(WaitingBoss.GetActorLocation().Z - WaitingBoss.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - CourtOrigin(Battle).Z) < 8.f);
            int32 WalkingFrames = 0;
            while (Mode->Outcome() != EDuelOutcome::Fighting && WalkingFrames++ < 130)
            {
                Player.AddMovementInput(FVector::ForwardVector, 1.f, true);
                Player.GetCharacterMovement()->TickComponent(Step, LEVELTICK_All, nullptr);
                Mode->Tick(Step);
            }
            if (!TestTrue(TEXT("Actual capsule movement through each fog starts combat"), Mode->Outcome() == EDuelOutcome::Fighting)) return false;
            TestTrue(TEXT("Admission preserves physical movement at the threshold"),
                Player.GetActorLocation().X > BeforeAdmission.X + 350.f &&
                Player.GetActorLocation().X < CourtOrigin(Battle).X + DuelCourt::GateCrossX + 35.f);
            TestFalse(TEXT("Fog closes for the active fight"), Tower.IsGateOpen(FogIds[Battle]));
            TestTrue(TEXT("The opposite exit has a physical barrier during combat"),
                !Tower.IsGateOpen(CombatExitIds[Battle]) && ExitBlocksCapsule(CombatExitIds[Battle]));
            if (!ReachPhysicalWalls(Player, CourtOrigin(Battle)) ||
                !ReachPhysicalWalls(*Mode->BossFighter, CourtOrigin(Battle))) return false;
            TestFalse(TEXT("Crossing the combat threshold extinguishes the exploration beam"), WayLight->IsVisible());
            TapWayLight();
            TestTrue(TEXT("G in combat leaves the beam off and preserves its exploration setting"),
                !WayLight->IsVisible() && Tower.bWayLightEnabled == bWayLightPreference);
            TapMap();
            TestFalse(TEXT("M cannot interrupt an active boss fight"), Tower.MapVisible());

            // Controlled damage drives the authored phase transitions without relying on the combat AI.
            auto& Fallen = *Mode->BossFighter;
            for (int32 Phase = 1; Phase <= 3; ++Phase)
            {
                if (!TestEqual(TEXT("Boss reached its real combat phase before the test hit"), Fallen.Phase(), Phase)) return false;
                Fallen.Move = EDuelMove::Idle;
                Fallen.MoveAge = 0.f;
                if (!TestTrue(TEXT("Controlled hit reaches the actual boss damage handler"),
                    Fallen.ReceiveDuelDamage(DuelCourt::BossMaxHealth + 1.f, Player.GetActorLocation()))) return false;
                if (Phase < 3)
                {
                    const float Windup = DuelCourt::Timing(Phase == 1 ? EDuelMove::Awaken : EDuelMove::Eclipse, Phase == 2).Windup;
                    Fallen.Tick(Windup + .03f);
                }
            }
            const auto* Story = Tower.Narrative();
            const auto BossDialogue = FDuelNorthProgress::BossDefeatDialogue(Battle);
            Mode->Tick(0.f);
            if (!TestTrue(TEXT("The winning blow begins the visible fall and blocks exploration"),
                !Fallen.IsAlive() && !Fallen.IsHidden() && !Story->IsTalking() &&
                !Tower.State.HasWon(Battle) && !Mode->CanExplore())) return false;
            int32 FallFrames = 0;
            while (!Fallen.IsBossDefeatComplete() && FallFrames++ < 120)
            {
                Fallen.Tick(Step);
                Mode->Tick(Step);
                if (!Fallen.IsBossDefeatComplete() && Story->IsTalking())
                {
                    AddError(TEXT("Boss dialogue interrupted the incomplete defeat animation"));
                    return false;
                }
            }
            const auto FirstLine = Story->Subtitle();
            if (!TestTrue(TEXT("The finished animation opens the correct boss dialogue before exploration"),
                Fallen.IsBossDefeatComplete() && Story->IsTalking() && !Mode->CanExplore() &&
                Tower.State.HasDialogue(BossDialogue) && FirstLine.Line == 1 && FirstLine.Count == 3 &&
                FirstLine.Character == Tower.State.BossForBattle(Battle) &&
                FirstLine.bWitchPortrait == (Battle < 2) &&
                (Battle != 1 || FirstLine.Speaker.Contains(TEXT("镜中魔女"))))) return false;
            const auto Relic = static_cast<EDuelNorthRelic>(Battle);
            const auto Door = static_cast<EDuelNorthDoor>(Battle);
            TestTrue(TEXT("Mode records victory without granting its dropped relic or opening a lock"),
                Mode->Outcome() == EDuelOutcome::Victory && Tower.State.HasWon(Battle) &&
                !Tower.State.HasRelic(Relic) && !Tower.State.IsDoorOpen(Door));
            TestFalse(TEXT("Winning locks protagonist selection for this save route"), Tower.CanChoosePlayer());
            TestTrue(TEXT("After victory the arena fog is open and boss collision is disabled"),
                Tower.IsGateOpen(FogIds[Battle]) && !Mode->BossFighter->GetActorEnableCollision());
            if (Battle == 2)
                TestTrue(TEXT("The final room's temporary exit fog clears for exploration"),
                    Tower.IsGateOpen(CombatExitIds[Battle]) && !ExitBlocksCapsule(CombatExitIds[Battle]));
            const EDuelNorthDialogue Reunion = Reunions[Battle];
            if (!TestFalse(TEXT("Boss dialogue leaves the exit reunion pending"), Tower.State.HasDialogue(Reunion))) return false;
            Mode->Tick(0.f);
            Mode->Tick(0.f);
            TestEqual(TEXT("Repeated updates keep the same open boss line"), Story->Subtitle().Line, 1);
            Mode->Confirm();
            const auto PlayerLine = Story->Subtitle();
            if (!TestTrue(TEXT("F advances to the actual protagonist in the ordinary form"),
                PlayerLine.Character == Character && PlayerLine.Line == 2 && !PlayerLine.bWitchPortrait &&
                PlayerLine.Speaker != FirstLine.Speaker)) return false;
            if (Battle == 1)
            {
                if (!FinishConversation()) return false;
            }
            else if (bDisk && Battle == 2 && Character == EDuelCharacter::Hiro)
            {
                if (!CheckSnapshot(TEXT("Continue while boss dialogue is open"))) return false;
            }
            else TapEscape();
            Mode->Tick(0.f);
            if (!TestTrue(TEXT("Completion, skip or Continue resumes exploration without replaying the boss"),
                !Story->IsTalking() && Mode->CanExplore() && !UGameplayStatics::IsGamePaused(Mode) &&
                !Tower.State.HasDialogue(Reunion) && WayLight->IsVisible() == bWayLightPreference)) return false;
            if (!CheckSnapshot(TEXT("victory before pickup"))) return false;
            Mode->Tick(0.f);
            if (!TestTrue(TEXT("Repeated updates and checkpoint restoration never reopen the boss scene"),
                !Story->IsTalking() && Mode->CanExplore())) return false;
            if (!Touch(ADuelNorthNarrative::MarkerFor(Reunion)) ||
                !TestTrue(TEXT("The rescued companion can be addressed through F at the exit"), Tower.Narrative()->IsTalking())) return false;
            if (Battle == 2)
            {
                TapMap();
                TestFalse(TEXT("M cannot hide an unfinished reunion"), Tower.MapVisible());
                TapEscape();
                TestTrue(TEXT("Esc skips the final reunion and restores unpaused exploration"),
                    !UGameplayStatics::IsGamePaused(Mode) && Mode->CanExplore());
            }
            else if (!FinishConversation()) return false;
            if (!TestTrue(TEXT("Reunion is recorded after confirmation or skip"), Tower.State.HasDialogue(Reunion))) return false;
            if (!TouchDoor(DoorIds[Battle])) return false;
            TestFalse(TEXT("Victory alone does not unlock the token door"), Tower.State.IsDoorOpen(Door));
            if (!Touch(RelicIds[Battle])) return false;
            TestTrue(TEXT("F pickup grants only the relic"), Tower.State.HasRelic(Relic) && !Tower.State.IsDoorOpen(Door));
            TestEqual(TEXT("The side pickup notice names exactly the item just awarded"),
                FString(Tower.RecentRelicName()), FString(FDuelNorthProgress::RelicName(Relic)));
            const double RelicNoticeExpiry = Tower.RecentRelicUntil;
            Tower.Nearby = RelicIds[Battle];
            Mode->Confirm();
            TestEqual(TEXT("A duplicate pickup does not refresh the side notice"), Tower.RecentRelicUntil, RelicNoticeExpiry);
            if (!CheckSnapshot(TEXT("pickup before opening"))) return false;
            if (!TouchDoor(DoorIds[Battle])) return false;
            TestTrue(TEXT("Explicit F at the locked door persists and removes its barrier"),
                Tower.State.IsDoorOpen(Door) && Tower.IsGateOpen(DoorIds[Battle]));
            Mode->Tick(.016f);
            if (!TestTrue(TEXT("Returning to an open door suppresses its interaction and unlock prompt"),
                Tower.Nearby != DoorIds[Battle] && !Tower.Prompt().Contains(TEXT("开启门锁")))) return false;
            if (Battle == 0)
            {
                if (!TestFalse(TEXT("The mirror fog still requires the archive investigation"), Tower.State.CanEnterBattle(1)) ||
                    !ReadRecord(TEXT("SealPrototype"), EDuelNorthClue::SealPrototype) ||
                    !ReadRecord(TEXT("ArchiveTransferLedger"), EDuelNorthClue::ArchiveTransferLedger) ||
                    !ReadRecord(TEXT("ArchiveSignatureRegister"), EDuelNorthClue::ArchiveSignatureRegister) ||
                    !SolvePuzzle(TEXT("FindCounterseal")) ||
                    !TestTrue(TEXT("The completed archive evidence enables the mirror fog"), Tower.State.CanEnterBattle(1))) return false;

                // The two puzzle rewards were found above. Seed optional equipment for the independent loadout round trip.
                Tower.State.CacheBits = DuelNorthFinds::AllCacheBits();
                Player.SetWeapon(DuelCourt::PersonalWeapon(Character));
                Player.SetWeapon(EDuelWeapon::Greatshield, EDuelHand::Left);
                Player.RightReserve = EDuelWeapon::RitualSword;
                Player.LeftReserve = EDuelWeapon::Spear;
                Player.ArtSelection = Character == EDuelCharacter::Ema ? EDuelArt::EmaVotive : EDuelArt::HiroThreads;
                Player.PreparedSpells = {EDuelArt::SableBlade, EDuelArt::Default, EDuelArt::WitchRain};
                Player.ActiveSpellSlot = 2;
                Player.HealthFlaskAllocation = 3;
                if (!CheckSnapshot(TEXT("new route loadout"))) return false;
            }
            if (Battle == 1)
            {
                if (!TestTrue(TEXT("The mirror key opens the prison inspection rooms while the final fog stays locked"),
                    Tower.State.CanVisitFloor(EDuelNorthFloor::Beacon) && !Tower.State.CanEnterBattle(2) &&
                    Tower.State.NextObjective().Target == TEXT("BeaconChart"))) return false;
                if (!Touch(TEXT("FinalGate")) || !TestTrue(TEXT("F at the last fog cannot bypass the seal comparison"),
                    !Mode->GateOpen() && !Tower.IsGateOpen(TEXT("FinalGate")))) return false;
                for (FName Evidence : {FName(TEXT("BeaconChart")), FName(TEXT("LensWorkshop")), FName(TEXT("BeaconLetter"))})
                {
                    if (!Touch(Evidence) || !FinishInvestigation(Evidence) ||
                        !TestTrue(TEXT("F inspects the physical prison mail evidence"), Tower.State.HasBeacon(Evidence))) return false;
                }
                const FName SealDesk(TEXT("BeaconSwitch"));
                if (!Touch(SealDesk) || !TestTrue(TEXT("F opens the seal comparison at the evidence desk"),
                    Tower.GetPuzzle() == 2 && Tower.GetPuzzleRound() == 0 && Tower.GetPuzzleChoice() == 0 && !Mode->CanExplore())) return false;
                TapConfirm(EKeys::D); TapConfirm();
                if (!TestTrue(TEXT("The intercepted bag leads to the second round while the fog stays locked"),
                    Tower.GetPuzzle() == 2 && Tower.GetPuzzleRound() == 1 && Tower.GetPuzzleChoice() == 1 && !Tower.State.CanEnterBattle(2))) return false;
                TapConfirm(EKeys::A); TapConfirm();
                if (!FinishInvestigation(SealDesk) || !TestTrue(TEXT("Matching the damaged seal opens the route to the final fight"),
                    Tower.GetPuzzle() == INDEX_NONE && Tower.State.HasBeacon(SealDesk) && Tower.State.CanEnterBattle(2))) return false;
                if (!Touch(TEXT("FinalWarning")) || !TestTrue(TEXT("Yuki speaks at the final hall after the evidence is confirmed"),
                    Tower.Narrative()->IsTalking() && Tower.Narrative()->Subtitle().Character == EDuelCharacter::Yuki) ||
                    !FinishConversation()) return false;
            }
            if (Battle == 2 && !ReadRecord(TEXT("ReleaseOrder"), EDuelNorthClue::ReleaseOrder)) return false;
            if (!CheckSnapshot(TEXT("door opened"))) return false;
        }
        TestTrue(TEXT("The one court reunion suffices for the Garden ending"),
            Tower.State.HasFinalConversation() && !Tower.State.HasDialogue(EDuelNorthDialogue::Garden) &&
            Tower.State.bCompanionFollowing && !Tower.Narrative()->IsTalking() && Mode->CanExplore());
        if (!Touch(TEXT("GardenEdge"))) return false;
        TestTrue(TEXT("Garden edge opens the dedicated ending page"), Tower.Narrative()->EndingVisible());
        TapMap();
        TestFalse(TEXT("M cannot leave a hidden map trapping the ending's F input"), Tower.MapVisible());
        Mode->Confirm();
        TestTrue(TEXT("F closes ending and resumes the walk with the partner"),
            !Tower.Narrative()->EndingVisible() && Tower.State.bCompanionFollowing && Mode->CanExplore());
    }
    AddInfo(FString::Printf(TEXT("North integration covered both protagonists, all three puzzles on each route, six physically crossed fog gates, six defeat dialogues and %d %s snapshots."),
        SavedTransitions, bDisk ? TEXT("real disk Continue") : TEXT("memory-only")));
    return true;
}

#endif
