#include "DuelGame.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DuelNorthTower.h"
#include "DuelNorthInvestigationQA.h"
#include "DuelNorthNarrative.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "HAL/FileManager.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Templates/UnrealTemplate.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthPointerMenuTest, "ManosabaDuel.NorthTower.PointerMenus",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthPointerMenuTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    const TCHAR* Command = FCommandLine::Get();
    if (!FParse::Param(Command, TEXT("DuelPointerMenuQA")))
    {
        AddInfo(TEXT("Run explicitly with -DuelPointerMenuQA -RenderOffscreen in rendered standalone."));
        return true;
    }
    FString UserDir;
    if (!TestTrue(TEXT("Offscreen-rendered North Tower title in an isolated profile"),
        FApp::CanEverRender() && !FParse::Param(Command, TEXT("NullRHI")) &&
        FParse::Param(Command, TEXT("RenderOffscreen")) &&
        FParse::Param(Command, TEXT("DuelNorthTower")) && FParse::Param(Command, TEXT("DuelNoSave")) &&
        !FParse::Param(Command, TEXT("DuelNoTitle")) && FParse::Value(Command, TEXT("userdir="), UserDir) &&
        !UserDir.IsEmpty() && !FPaths::IsRelative(UserDir))) return false;
    FString CurrentUserDir = FPaths::ProjectUserDir();
    FString ProjectDir = FPaths::ProjectDir();
    FPaths::NormalizeDirectoryName(UserDir);
    FPaths::NormalizeDirectoryName(CurrentUserDir);
    FPaths::NormalizeDirectoryName(ProjectDir);
    if (!TestTrue(TEXT("The requested profile is active"), FPaths::IsSamePath(UserDir, CurrentUserDir) &&
        !FPaths::IsSamePath(CurrentUserDir, ProjectDir))) return false;
    FString CaptureDir;
    if (FParse::Value(Command, TEXT("DuelUICaptures="), CaptureDir) &&
        !TestTrue(TEXT("Screenshots use an absolute output directory"), !FPaths::IsRelative(CaptureDir) &&
            IFileManager::Get().MakeDirectory(*CaptureDir, true))) return false;
    ADuelMode* Mode = nullptr;
    if (GEngine)
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
            if (Context.WorldType == EWorldType::Game && Context.World() &&
                (Mode = Context.World()->GetAuthGameMode<ADuelMode>())) break;
    if (!TestTrue(TEXT("Use the actual title and HUD"), Mode && Mode->MenuPage() == EDuelMenuPage::Title &&
        Mode->NorthTowerActor && Mode->PlayerFighter)) return false;

    const TWeakObjectPtr<ADuelMode> ActiveMode(Mode);
    const bool bTravelQA = FParse::Param(Command, TEXT("DuelFastTravelUIQA"));
    const double Deadline = FPlatformTime::Seconds() + (bTravelQA ? 100.0 : 60.0);
    enum class EStep { TitleHero, TitleStart, PauseOpen, EquipmentOpen, WeaponSlot, WeaponChoice, SpellSlot, SpellChoice,
        EquipmentBack, InventoryOpen, InventorySwitch, InventoryBack, PauseResume, MapOpen, MapBack,
        TravelFixture, TravelMouseSelect, TravelEnter, TravelArrival, TravelKeyboardSelect, TravelF,
        TravelReturn, TravelCancelSelect, TravelCancel, LookAfterMap };
    auto Advance = [this, ActiveMode, Deadline, CaptureDir, bTravelQA, LastRequested = FString(), Stage = EStep::TitleHero]() mutable -> bool
    {
        ADuelMode* Game = ActiveMode.Get();
        if (!Game || FPlatformTime::Seconds() > Deadline)
        {
            AddError(FString::Printf(TEXT("Pointer menus stopped at step %d before receiving the expected HUD draw."),
                static_cast<int32>(Stage)));
            return true;
        }
        ADuelFighter& Player = *Game->PlayerFighter;
        ADuelNorthTower& Tower = *Game->NorthTowerActor;
        auto* Controller = Cast<APlayerController>(Player.GetController());
        auto* HUD = Controller ? Controller->GetHUD<ADuelHUD>() : nullptr;
        if (!Controller || !HUD || !Controller->PlayerInput || !Player.InputComponent) return false;
        const EDuelMenuPage Page = Game->MenuPage();
        const auto Drawn = [&](EDuelMenuPage Expected) { return Page == Expected && HUD->DrawnMenuPage == Expected; };
        const auto Capture = [&](const TCHAR* Name)
        {
            if (CaptureDir.IsEmpty()) return true;
            const FString File = CaptureDir / (FString(Name) + TEXT(".png"));
            if (LastRequested != File)
            {
                if (FScreenshotRequest::IsScreenshotRequested()) return false;
                if (IFileManager::Get().FileExists(*File))
                {
                    AddError(FString::Printf(TEXT("Refusing to overwrite UI capture: %s"), *File));
                    return true;
                }
                FScreenshotRequest::RequestScreenshot(File, false, false);
                LastRequested = File;
                return false;
            }
            return !FScreenshotRequest::IsScreenshotRequested() && IFileManager::Get().FileExists(*File);
        };
        TArray<UInputComponent*> InputStack{Player.InputComponent.Get()};
        const auto ProcessInput = [&]
        {
            Controller->PlayerInput->ProcessInputStack(InputStack, 1.f / 60.f, UGameplayStatics::IsGamePaused(Game));
        };
        const auto Key = [&](const FKey& InKey, EInputEvent Event)
        {
            Controller->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, InKey, Event, Event == IE_Pressed ? 1.f : 0.f, false, 0));
            ProcessInput();
        };
        const auto Tap = [&](const FKey& InKey)
        {
            Key(InKey, IE_Pressed);
            Key(InKey, IE_Released);
        };
        const auto QueueLook = [&]
        {
            Controller->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, EKeys::MouseX, 31.f, 1.f / 60.f, 1, 0));
            Controller->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, EKeys::MouseY, 19.f, 1.f / 60.f, 1, 0));
        };
        const auto TestLook = [&](bool bAllowLook)
        {
            const float Yaw = Player.ViewYaw, Pitch = Player.ViewPitch;
            QueueLook();
            ProcessInput();
            const float DeltaYaw = FMath::FindDeltaAngleDegrees(Yaw, Player.ViewYaw);
            const float DeltaPitch = Player.ViewPitch - Pitch;
            AddInfo(FString::Printf(TEXT("NorthPointerMenu page=%d cursor=%d mouseYaw=%.5f mousePitch=%.5f"),
                static_cast<int32>(Page), Controller->bShowMouseCursor ? 1 : 0, DeltaYaw, DeltaPitch));
            return TestTrue(TEXT("Mouse look follows the current input owner"), bAllowLook ?
                !FMath::IsNearlyZero(DeltaYaw) && !FMath::IsNearlyZero(DeltaPitch) : DeltaYaw == 0.f && DeltaPitch == 0.f);
        };
        const auto ClickAt = [&](const FVector2D& Pixel, const FBox2D* Target, bool bWithLook)
        {
            TGuardValue<TOptional<FVector2D>> Sample(HUD->TestPointerPixel, TOptional<FVector2D>(Pixel));
            FVector2D Pointer;
            if (!TestTrue(TEXT("Viewport sample converts to HUD coordinates"), HUD->ReadMenuPointer(Pointer))) return false;
            const bool bMatches = Target ? Target->IsInside(Pointer) : !HUD->MenuButtons.ContainsByPredicate(
                [&](const auto& Region) { return Region.Value.IsInside(Pointer); });
            if (!TestTrue(TEXT("Rescaled pointer hits the expected control or the designated empty region"), bMatches)) return false;
            if (bWithLook) QueueLook();
            Tap(EKeys::LeftMouseButton);
            return true;
        };
        const auto Click = [&](int32 Choice, bool bWithLook = false)
        {
            const auto* Button = HUD->MenuButtons.FindByPredicate([&](const auto& Region) { return Region.Key == Choice; });
            if (!TestNotNull(TEXT("The requested control was drawn with a hit target"), Button)) return false;
            int32 ViewW = 0, ViewH = 0;
            Controller->GetViewportSize(ViewW, ViewH);
            if (ViewW <= 0 || ViewH <= 0 || HUD->MenuCanvasSize.X <= 0 || HUD->MenuCanvasSize.Y <= 0)
            {
                AddError(TEXT("An offscreen game viewport must supply its rendered dimensions."));
                return false;
            }
            const FVector2D Center = Button->Value.GetCenter();
            const FVector2D Pixel(FMath::RoundToInt(Center.X * ViewW / HUD->MenuCanvasSize.X),
                FMath::RoundToInt(Center.Y * ViewH / HUD->MenuCanvasSize.Y));
            AddInfo(FString::Printf(TEXT("NorthPointerMenu click page=%d choice=%d viewport=%dx%d pixel=(%.0f,%.0f)"),
                static_cast<int32>(Page), Choice, ViewW, ViewH, Pixel.X, Pixel.Y));
            return ClickAt(Pixel, &Button->Value, bWithLook);
        };
        const auto NoAttack = [&]
        {
            return Player.Move == EDuelMove::Idle && Player.HandPresses[static_cast<uint8>(EDuelHand::Right)].Since < 0.0;
        };

        if (!bTravelQA && Stage >= EStep::TravelFixture && Stage < EStep::LookAfterMap) Stage = EStep::LookAfterMap;
        switch (Stage)
        {
        case EStep::TitleHero:
            if (!Drawn(EDuelMenuPage::Title)) return false;
            if (!Capture(TEXT("Title"))) return false;
            if (!TestTrue(TEXT("Title exposes the pointer"), Controller->bShowMouseCursor) || !TestLook(false) || !Click(4)) return true;
            if (!TestTrue(TEXT("Title click selects Hiro"), Player.SelectedCharacter() == EDuelCharacter::Hiro)) return true;
            break;
        case EStep::TitleStart:
            if (!Drawn(EDuelMenuPage::Title)) return false;
            if (!Click(1) || !TestTrue(TEXT("New game click starts exploration without attacking"), Game->Outcome() == EDuelOutcome::Ready &&
                !Controller->bShowMouseCursor && NoAttack())) return true;
            break;
        case EStep::PauseOpen:
            Tap(EKeys::Escape);
            if (!TestTrue(TEXT("Pause exposes the pointer"), Game->MenuPage() == EDuelMenuPage::Pause &&
                Controller->bShowMouseCursor) || !TestLook(false)) return true;
            break;
        case EStep::EquipmentOpen:
        {
            if (!Drawn(EDuelMenuPage::Pause)) return false;
            if (!Capture(TEXT("Pause"))) return false;
            int32 GearOption = 0;
            while (GearOption < Game->PauseOptionCount() && Game->PauseOption(GearOption) != EDuelPause::Loadout) ++GearOption;
            if (!Click(GearOption) || !TestTrue(TEXT("Pause click opens the equipment slots"),
                Game->MenuPage() == EDuelMenuPage::Equipment)) return true;
            break;
        }
        case EStep::WeaponSlot:
            if (!Drawn(EDuelMenuPage::Equipment)) return false;
            if (!Capture(TEXT("Equipment"))) return false;
            if (!Click(0) || !TestTrue(TEXT("Right-hand slot click opens legal weapon candidates"),
                Game->MenuPage() == EDuelMenuPage::EquipmentChoices && Game->LoadoutChoice() == 0 &&
                !Game->LoadoutChoices().Contains(static_cast<int32>(Player.ReserveWeapon(EDuelHand::Right))) &&
                !Game->LoadoutChoices().Contains(static_cast<int32>(EDuelWeapon::Greatblade)) &&
                !Game->LoadoutChoices().Contains(static_cast<int32>(EDuelWeapon::Empty)))) return true;
            break;
        case EStep::WeaponChoice:
        {
            if (!Drawn(EDuelMenuPage::EquipmentChoices)) return false;
            if (!Capture(TEXT("WeaponDetails"))) return false;
            const int32 Current = static_cast<int32>(Player.EquippedWeapon());
            const auto Values = Game->LoadoutChoices();
            const int32* Replacement = Values.FindByPredicate([&](int32 Choice) { return Choice != Current; });
            if (!TestNotNull(TEXT("A different valid weapon is available"), Replacement) || !Click(*Replacement) ||
                !TestTrue(TEXT("Weapon grid click equips the exact selected item"),
                    static_cast<int32>(Player.EquippedWeapon()) == *Replacement &&
                    Game->MenuPage() == EDuelMenuPage::Equipment && NoAttack())) return true;
            break;
        }
        case EStep::SpellSlot:
            if (!Drawn(EDuelMenuPage::Equipment)) return false;
            if (!Click(ADuelMode::FirstSpellRow) || !TestTrue(TEXT("Spell slot shows learned options"),
                Game->MenuPage() == EDuelMenuPage::EquipmentChoices && Game->LoadoutChoices().Num() == 3 &&
                Game->LoadoutChoices().Contains(static_cast<int32>(EDuelArt::Default)) &&
                !Game->LoadoutChoices().Contains(static_cast<int32>(EDuelArt::WhiteMantle)))) return true;
            break;
        case EStep::SpellChoice:
        {
            if (!Drawn(EDuelMenuPage::EquipmentChoices)) return false;
            const EDuelArt A = Player.SpellAt(0), B = Player.SpellAt(1);
            if (!Click(static_cast<int32>(B)) || !TestTrue(TEXT("Choosing a prepared spell swaps slots"),
                A != B && Player.SpellAt(0) == B && Player.SpellAt(1) == A &&
                Game->MenuPage() == EDuelMenuPage::Equipment)) return true;
            break;
        }
        case EStep::EquipmentBack:
            if (!Drawn(EDuelMenuPage::Equipment)) return false;
            if (!Click(INDEX_NONE) || !TestTrue(TEXT("Back retains pause pointer ownership"),
                Game->MenuPage() == EDuelMenuPage::Pause && Controller->bShowMouseCursor)) return true;
            break;
        case EStep::InventoryOpen:
        {
            if (!Drawn(EDuelMenuPage::Pause)) return false;
            int32 Entry = 0;
            while (Entry < Game->PauseOptionCount() && Game->PauseOption(Entry) != EDuelPause::Inventory) ++Entry;
            if (!Click(Entry) || !TestTrue(TEXT("Pause click opens the inventory"), Game->MenuPage() == EDuelMenuPage::Inventory))
                return true;
            break;
        }
        case EStep::InventorySwitch:
        {
            if (!Drawn(EDuelMenuPage::Inventory)) return false;
            const EDuelItem First = Player.EquippedItem();
            const auto Other = First == EDuelItem::Flask ? EDuelItem::FocusFlask : EDuelItem::Flask;
            if (Player.Flasks > 0) --Player.Flasks;
            else --Player.FocusFlasks;
            const int32 Doses = Player.Flasks + Player.FocusFlasks;
            Tap(EKeys::S);
            Tap(EKeys::F);
            if (!TestEqual(TEXT("Keyboard commits selected quick item"), int32(Player.EquippedItem()), int32(Other))) return true;
            const int32 Allocation = Player.FlaskAllocation();
            Tap(EKeys::D);
            if (!TestTrue(TEXT("Flask reallocation preserves expended doses"),
                Player.FlaskAllocation() != Allocation && Player.Flasks + Player.FocusFlasks == Doses)) return true;
            if (!Click(int32(First)) || !TestEqual(TEXT("Mouse selects the same quick slot"), int32(Player.EquippedItem()), int32(First)))
                return true;
            break;
        }
        case EStep::InventoryBack:
            if (!Drawn(EDuelMenuPage::Inventory) || !Capture(TEXT("Inventory"))) return false;
            if (!Click(INDEX_NONE) || !TestTrue(TEXT("Inventory back returns to pause with pointer ownership"),
                Game->MenuPage() == EDuelMenuPage::Pause && Controller->bShowMouseCursor)) return true;
            break;
        case EStep::PauseResume:
            if (!Drawn(EDuelMenuPage::Pause)) return false;
            if (!ClickAt(FVector2D(4, 4), nullptr, false) || !TestTrue(TEXT("Click-away leaves the menu open and does not attack"),
                Game->MenuPage() == EDuelMenuPage::Pause && NoAttack()) || !Click(0) ||
                !TestTrue(TEXT("Resume click and its release restore gameplay without attacking"),
                Game->MenuPage() == EDuelMenuPage::None && !Controller->bShowMouseCursor && NoAttack())) return true;
            break;
        case EStep::MapOpen:
        {
            using namespace DuelNorthLayout;
            const FVector Start = Player.GetActorLocation();
            Player.SetActorLocation(CourtOrigin(0) + FVector(0, 0, 63), false, nullptr, ETeleportType::TeleportPhysics);
            Tower.Update(*Game, 0.f);
            Player.SetActorLocation(Start, false, nullptr, ETeleportType::TeleportPhysics);
            Key(EKeys::M, IE_Pressed);
            Tower.Update(*Game, 0.f);
            Key(EKeys::M, IE_Released);
            if (!TestTrue(TEXT("M opens the map with an explored alternate floor and locked player input"),
                Game->MenuPage() == EDuelMenuPage::Map && Controller->bShowMouseCursor && !Game->CanExplore() &&
                !Player.CanUseEquipment() && Tower.CurrentFloor() == EFloor::Prison && Tower.HasExploredFloor(EFloor::Dungeon)) ||
                !TestLook(false)) return true;
            break;
        }
        case EStep::MapBack:
        {
            using namespace DuelNorthLayout;
            if (!Drawn(EDuelMenuPage::Map)) return false;
            if (!Capture(TEXT("Map"))) return false;
            if (!TestTrue(TEXT("Unexplored floors have no click target"),
                !HUD->MenuButtons.ContainsByPredicate([](const auto& Btn) { return Btn.Key == static_cast<int32>(EFloor::Final); }))) return true;
            if (!Click(static_cast<int32>(EFloor::Dungeon)) || !TestTrue(TEXT("Floor-tab click changes the view and keeps the real floor"),
                Game->ViewedMapFloor() == EFloor::Dungeon && Tower.CurrentFloor() == EFloor::Prison)) return true;
            Tap(EKeys::D);
            const float Yaw = Player.ViewYaw, Pitch = Player.ViewPitch;
            if (!TestTrue(TEXT("Keyboard can browse back to the current floor"), Game->ViewedMapFloor() == EFloor::Prison) ||
                !Click(INDEX_NONE, true) || !TestTrue(TEXT("Map close click restores exploration without attacking"),
                Game->MenuPage() == EDuelMenuPage::None && !Controller->bShowMouseCursor && Game->CanExplore() && NoAttack())) return true;
            const float CloseYaw = FMath::FindDeltaAngleDegrees(Yaw, Player.ViewYaw);
            const float ClosePitch = Player.ViewPitch - Pitch;
            AddInfo(FString::Printf(TEXT("NorthPointerMenu map-close queued mouseYaw=%.5f mousePitch=%.5f"), CloseYaw, ClosePitch));
            if (!TestTrue(TEXT("The map close frame drops mouse axes queued while the map was open"),
                CloseYaw == 0.f && ClosePitch == 0.f)) return true;
            break;
        }
        case EStep::TravelFixture:
        {
            using namespace DuelNorthLayout;
            // Only the fixture moves the party: activation still uses the real nearby-F interaction.
            Tower.State.ReadClue(EDuelNorthClue::OpeningDocket);
            if (!TestTrue(TEXT("The travel UI fixture opens the starting latch through its real state rule"),
                Tower.State.OpenStartingCell() || Tower.State.IsOpeningCellOpen())) return true;
            if (!TestTrue(TEXT("The fixture reaches a known archive floor through the real first-boss progression"),
                DuelNorthInvestigationQA::Prepare(Tower.State, 0) && Tower.State.WinBattle(0) &&
                Tower.State.PickupRelic(EDuelNorthRelic::RustKey) && Tower.State.OpenDoor(EDuelNorthDoor::Chapel))) return true;
            Tower.State.CompleteDialogue(EDuelNorthDialogue::RescuedCompanion);
            Tower.State.ExploreFloor(EDuelNorthFloor::Chapel);
            Tower.RefreshGates();
            if (Tower.NarrativeActor) Tower.NarrativeActor->Restore(Tower.State, Player, *Game->Arena);
            for (const EFloor RestFloor : {EFloor::Prison, EFloor::Dungeon})
            {
                const auto* Rest = Checkpoints().FindByPredicate([&](const auto& Point) { return Point.Floor == RestFloor; });
                if (!TestNotNull(TEXT("A real authored rest point is available"), Rest)) return true;
                Player.SetActorLocation(Rest->Position + FVector(0, 0, 63), false, nullptr, ETeleportType::TeleportPhysics);
                Tower.Update(*Game, 0.f);
                if (!TestTrue(TEXT("The rest is physically within the actual interaction prompt"), Tower.Nearby == Rest->Id)) return true;
                Tap(EKeys::F);
                if (!TestTrue(TEXT("A real rest interaction supplies the required activation evidence"), Tower.IsTravelPointActivated(RestFloor))) return true;
            }
            const auto* Start = Checkpoints().FindByPredicate([](const auto& Point) { return Point.Floor == EFloor::Prison; });
            Player.SetActorLocation(Start->Position + FVector(0, 0, 63), false, nullptr, ETeleportType::TeleportPhysics);
            Tower.Update(*Game, 0.f);
            Key(EKeys::M, IE_Pressed); Tower.Update(*Game, 0.f); Key(EKeys::M, IE_Released);
            if (!TestTrue(TEXT("The map opens from the first floor without activating the other rest points"),
                Tower.MapVisible() && Tower.CurrentFloor() == EFloor::Prison &&
                !Tower.IsTravelPointActivated(EFloor::Chapel) && !Tower.IsTravelPointActivated(EFloor::Mirror) &&
                !Tower.IsTravelPointActivated(EFloor::Final))) return true;
            break;
        }
        case EStep::TravelMouseSelect:
        {
            using namespace DuelNorthLayout;
            if (!Drawn(EDuelMenuPage::Map)) return false;
            if (!TestTrue(TEXT("An unlit point has no travel click target"),
                !HUD->MenuButtons.ContainsByPredicate([](const auto& Button) {
                    return Button.Key == ADuelNorthTower::TravelMenuBase + int32(EFloor::Chapel); }) &&
                Tower.HasExploredFloor(EFloor::Chapel) && !Tower.IsTravelPointActivated(EFloor::Chapel))) return true;
            if (!Click(ADuelNorthTower::TravelMenuBase + int32(EFloor::Dungeon)) ||
                !TestTrue(TEXT("A mouse row selects the activated destination without teleporting"),
                    Tower.SelectedTravelPoint() == EFloor::Dungeon && Game->ViewedMapFloor() == EFloor::Dungeon &&
                    Tower.CurrentFloor() == EFloor::Prison && Tower.MapVisible() && NoAttack())) return true;
            break;
        }
        case EStep::TravelEnter:
            if (!Drawn(EDuelMenuPage::Map) || !Capture(TEXT("FastTravel-SelectedDungeon"))) return false;
            Tap(EKeys::Enter);
            if (!TestTrue(TEXT("Enter confirms the selected point and releases map input ownership"),
                !Tower.MapVisible() && Tower.CurrentFloor() == DuelNorthLayout::EFloor::Dungeon &&
                Game->CanExplore() && !Controller->bShowMouseCursor && NoAttack() &&
                Tower.SelectedTravelPoint() == DuelNorthLayout::EFloor::Unknown)) return true;
            break;
        case EStep::TravelArrival:
            if (!Drawn(EDuelMenuPage::None) || !Capture(TEXT("FastTravel-DungeonArrival"))) return false;
            Key(EKeys::M, IE_Pressed); Tower.Update(*Game, 0.f); Key(EKeys::M, IE_Released);
            break;
        case EStep::TravelKeyboardSelect:
            if (!Drawn(EDuelMenuPage::Map)) return false;
            Tap(EKeys::Down); Tap(EKeys::Down);
            if (!TestTrue(TEXT("Keyboard selection cycles only activated points and keeps the player still"),
                Tower.SelectedTravelPoint() == DuelNorthLayout::EFloor::Prison &&
                Tower.CurrentFloor() == DuelNorthLayout::EFloor::Dungeon && Tower.MapVisible())) return true;
            break;
        case EStep::TravelF:
            if (!Drawn(EDuelMenuPage::Map) || !Capture(TEXT("FastTravel-KeyboardPrison"))) return false;
            Tap(EKeys::F);
            if (!TestTrue(TEXT("F confirms keyboard travel without leaking an attack or leaving the cursor captured"),
                !Tower.MapVisible() && Tower.CurrentFloor() == DuelNorthLayout::EFloor::Prison &&
                Game->CanExplore() && !Controller->bShowMouseCursor && NoAttack())) return true;
            break;
        case EStep::TravelReturn:
            if (!Drawn(EDuelMenuPage::None) || !Capture(TEXT("FastTravel-PrisonArrival"))) return false;
            Key(EKeys::M, IE_Pressed); Tower.Update(*Game, 0.f); Key(EKeys::M, IE_Released);
            break;
        case EStep::TravelCancelSelect:
            if (!Drawn(EDuelMenuPage::Map)) return false;
            if (!Click(int32(DuelNorthLayout::EFloor::Dungeon))) return true;
            break;
        case EStep::TravelCancel:
            if (!Drawn(EDuelMenuPage::Map)) return false;
            if (!Click(ADuelNorthTower::TravelIconMenuBase + int32(DuelNorthLayout::EFloor::Dungeon))) return true;
            Tap(EKeys::Escape);
            if (!TestTrue(TEXT("Esc cancels a map-icon selection without moving or lighting another point"),
                !Tower.MapVisible() && Tower.CurrentFloor() == DuelNorthLayout::EFloor::Prison &&
                Tower.SelectedTravelPoint() == DuelNorthLayout::EFloor::Unknown &&
                !Tower.IsTravelPointActivated(DuelNorthLayout::EFloor::Chapel) &&
                Game->CanExplore() && NoAttack())) return true;
            break;
        case EStep::LookAfterMap:
            if (GFrameCounter == Game->PointerClosedFrame) return false;
            TestLook(true);
            return true;
        }
        Stage = static_cast<EStep>(static_cast<int32>(Stage) + 1);
        return false;
    };
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(MoveTemp(Advance)));
    return true;
}

#endif
