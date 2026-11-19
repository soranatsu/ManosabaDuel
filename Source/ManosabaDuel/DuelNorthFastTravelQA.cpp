#include "DuelNorthTower.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DuelArena.h"
#include "DuelGame.h"
#include "DuelNorthInvestigationQA.h"
#include "DuelNorthLighting.h"
#include "DuelNorthNarrative.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Sound/SoundWaveProcedural.h"
#include "UObject/UnrealType.h"

namespace
{
    class FLegacyRestWriter : public FMemoryWriter
    {
    public:
        explicit FLegacyRestWriter(TArray<uint8>& Bytes) : FMemoryWriter(Bytes, true) {}
        virtual bool ShouldSkipProperty(const FProperty* Property) const override
        { return Property && Property->GetFName() == TEXT("ActivatedCheckpoints"); }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthFastTravelTest, "ManosabaDuel.NorthTower.FastTravel",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthFastTravelTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace DuelNorthLayout;
    if (!TestTrue(TEXT("Fast travel QA runs only in an isolated save-free tower"),
        FParse::Param(FCommandLine::Get(), TEXT("DuelNorthTower")) && FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("DuelNorthSaveTest")))) return false;
    ADuelMode* Mode = nullptr;
    for (const auto& Context : GEngine->GetWorldContexts())
        if (Context.World() && (Mode = Context.World()->GetAuthGameMode<ADuelMode>())) break;
    if (!TestTrue(TEXT("Actual tower is idle and unpaused"), Mode && Mode->NorthTowerActor && Mode->Arena &&
        Mode->Outcome() != EDuelOutcome::Fighting && !UGameplayStatics::IsGamePaused(Mode))) return false;
    auto* World = Mode->GetWorld();
    if (FParse::Param(FCommandLine::Get(), TEXT("DuelCheckAudioComponents")) &&
        !TestTrue(TEXT("The requested audio component check has an enabled audio device"), World->AllowAudioPlayback())) return false;
    auto& Tower = *Mode->NorthTowerActor;
    const FDuelNorthProgress OriginalState = Tower.State;
    const auto OriginalRooms = Tower.Explored, OriginalOpenGates = Tower.OpenGates;
    const auto OriginalLocation = Tower.Location;
    const auto OriginalNarrative = Tower.NarrativeActor;
    const auto OriginalPlayer = Mode->PlayerFighter, OriginalBoss = Mode->BossFighter;
    const auto OriginalOutcome = Mode->Result;
    const auto OriginalFight = Tower.ActiveFight, OriginalPuzzle = Tower.ActivePuzzle;
    const bool OriginalMap = Tower.bMap, OriginalDefeat = Tower.bBossDefeatPending;
    const auto OriginalTravel = Tower.TravelSelection;
    const auto OriginalTravelFeedback = Tower.TravelFeedback;
    const auto OriginalNotice = Tower.Notice;
    const auto OriginalNoticeUntil = Tower.NoticeUntil;
    const auto OriginalNearby = Tower.Nearby;
    const auto OriginalViewed = Mode->BrowsedMapFloor;
    const auto OriginalScore = Mode->Arena->ExplorationMusicCue;
    const auto OriginalOrigin = Mode->Arena->CourtOrigin();
    const bool OriginalGateOpen = Mode->bGateOpen;
    auto* Controller = World->GetFirstPlayerController();
    TWeakObjectPtr<AActor> OriginalView = Controller ? Controller->GetViewTarget() : nullptr;
    auto* Player = World->SpawnActor<ADuelFighter>();
    auto* Boss = World->SpawnActor<ADuelFighter>();
    auto* Story = World->SpawnActor<ADuelNorthNarrative>();
    AActor* Obstruction = nullptr;
    ON_SCOPE_EXIT
    {
        if (IsValid(Obstruction)) Obstruction->Destroy();
        Tower.State = OriginalState; Tower.Explored = OriginalRooms; Tower.OpenGates = OriginalOpenGates;
        Tower.Location = OriginalLocation; Tower.NarrativeActor = OriginalNarrative;
        Tower.ActiveFight = OriginalFight; Tower.ActivePuzzle = OriginalPuzzle;
        Tower.bMap = OriginalMap; Tower.bBossDefeatPending = OriginalDefeat;
        Tower.TravelSelection = OriginalTravel; Tower.TravelFeedback = OriginalTravelFeedback;
        Tower.Notice = OriginalNotice; Tower.NoticeUntil = OriginalNoticeUntil; Tower.Nearby = OriginalNearby;
        Mode->PlayerFighter = OriginalPlayer; Mode->BossFighter = OriginalBoss; Mode->Result = OriginalOutcome;
        Mode->bGateOpen = OriginalGateOpen; Mode->BrowsedMapFloor = OriginalViewed;
        Mode->Arena->SetNorthOrigin(OriginalOrigin); Mode->Arena->SetNorthExplorationScore(OriginalScore);
        Tower.RefreshGates(); Mode->RefreshMenuPointer();
        if (Controller && OriginalView.IsValid()) Controller->SetViewTarget(OriginalView.Get());
        if (OriginalPlayer) DuelNorthLighting::Update(&Tower, OriginalPlayer->GetActorLocation(), 1.f, &OriginalLocation);
        if (IsValid(Story)) Story->Destroy();
        if (IsValid(Player)) Player->Destroy();
        if (IsValid(Boss)) Boss->Destroy();
    };
    if (!TestTrue(TEXT("Independent party fixtures exist"), Player && Boss && Story)) return false;
    Mode->PlayerFighter = Player; Mode->BossFighter = Boss; Tower.NarrativeActor = Story;
    Player->SetEncounter(Boss, Mode->Arena); Boss->SetEncounter(Player, Mode->Arena);
    Tower.ActiveFight = Tower.ActivePuzzle = INDEX_NONE; Tower.bBossDefeatPending = false;

    for (const auto Route : {EDuelCharacter::Hiro, EDuelCharacter::Ema})
    {
        FDuelNorthProgress State;
        State.Reset(Route); State.bOpeningCellRequired = true;
        State.ReadClue(EDuelNorthClue::OpeningDocket); State.OpenStartingCell();
        TestFalse(TEXT("A prefilled first-floor visited bit is not a lit rest point"), State.IsCheckpointActivated(EDuelNorthFloor::Prison));
        TestTrue(TEXT("An actual first-floor rest interaction lights the point"), State.ActivateCheckpoint(EDuelNorthFloor::Prison) &&
            State.IsCheckpointActivated(EDuelNorthFloor::Prison));
        State.ActivateCheckpoint(EDuelNorthFloor::Dungeon);
        for (int32 Battle = 0; Battle < FDuelNorthProgress::BattleCount; ++Battle)
        {
            if (!DuelNorthInvestigationQA::Prepare(State, Battle) || !State.WinBattle(Battle)) return false;
            State.PickupRelic(static_cast<EDuelNorthRelic>(Battle));
            if (Battle < 2) State.OpenDoor(static_cast<EDuelNorthDoor>(Battle));
            if (Battle == 0) { State.CompleteDialogue(EDuelNorthDialogue::RescuedCompanion); State.ActivateCheckpoint(EDuelNorthFloor::Chapel); }
            if (Battle == 1) { State.CompleteDialogue(EDuelNorthDialogue::MirrorExit); State.ActivateCheckpoint(EDuelNorthFloor::Mirror); }
        }
        State.CompleteDialogue(EDuelNorthDialogue::FinalExit);
        State.ActivateCheckpoint(EDuelNorthFloor::Final);
        FString Reason;
        if (!TestTrue(TEXT("The completed fixture is a valid real progression"), State.Validate(Reason))) return false;

        FDuelNorthProgress Old = State;
        Old.ActivatedCheckpoints = 0; Old.bPrisonRestActivated = false;
        Old.VisitedCheckpoints = 1 << uint8(EDuelNorthFloor::Prison); Old.Checkpoint = uint8(EDuelNorthFloor::Prison);
        for (const auto& Point : Checkpoints())
            TestFalse(TEXT("Finished story or explored floors never fabricate a missing rest activation"), Old.IsCheckpointActivated(static_cast<EDuelNorthFloor>(Point.Floor)));
        Old.VisitedCheckpoints |= 1 << uint8(EDuelNorthFloor::Dungeon);
        TArray<uint8> LegacyBytes;
        FLegacyRestWriter Writer(LegacyBytes);
        FDuelNorthProgress::StaticStruct()->SerializeItem(Writer, &Old, nullptr);
        FDuelNorthProgress Loaded;
        FMemoryReader Reader(LegacyBytes, true);
        FDuelNorthProgress::StaticStruct()->SerializeItem(Reader, &Loaded, nullptr);
        TestTrue(TEXT("An archive without the new field keeps only old demonstrable nonseeded activations"),
            Loaded.ActivatedCheckpoints == 0 && Loaded.IsCheckpointActivated(EDuelNorthFloor::Dungeon) &&
            !Loaded.IsCheckpointActivated(EDuelNorthFloor::Prison) && !Loaded.IsCheckpointActivated(EDuelNorthFloor::Chapel));
        TestTrue(TEXT("Resting again safely adds missing proof in an old checkpoint"), Loaded.ActivateCheckpoint(EDuelNorthFloor::Prison) &&
            Loaded.IsCheckpointActivated(EDuelNorthFloor::Prison));

        Tower.State = State;
        Player->SelectCharacter(Route); Boss->ConfigureBoss(State.Companion());
        Mode->Result = EDuelOutcome::Ready;
        Tower.Respawn(*Mode);
        const auto Wins = State.Wins, Relics = State.Relics, Doors = State.Doors, Clues = State.Clues;
        const auto Proof = State.ActivatedCheckpoints, Visited = State.VisitedCheckpoints;
        for (const auto& Point : Checkpoints())
        {
            Tower.bMap = true; Mode->RefreshMenuPointer();
            Tower.ClearTravelSelection();
            Mode->ActivateMenuChoice(ADuelNorthTower::TravelMenuBase + int32(Point.Floor));
            TestTrue(TEXT("Every proven point can be selected through the actual mouse-menu route"),
                Tower.SelectedTravelPoint() == Point.Floor);
            Mode->Navigate(1);
            TestTrue(TEXT("Keyboard rest selection visits only an activated point"), Tower.CanTravelTo(Tower.SelectedTravelPoint()));
            Tower.SelectTravelPoint(Point.Floor);
            const FVector Before = Player->GetActorLocation();
            Mode->Result = EDuelOutcome::Fighting;
            TestFalse(TEXT("Combat prevents both selection and confirmation"), Tower.SelectTravelPoint(Point.Floor) || Tower.ConfirmTravel(*Mode));
            TestTrue(TEXT("Rejected travel leaves the party in place"), Player->GetActorLocation().Equals(Before, .001));
            Mode->Result = EDuelOutcome::Ready;
            Mode->Confirm();
            TestTrue(TEXT("F/Enter confirmation closes the map and restores ordinary exploration"), !Tower.MapVisible() &&
                Mode->CanExplore() && (!Controller || !Controller->bShowMouseCursor));
            TestEqual(TEXT("Only mouse axes queued on the close frame are discarded"), Mode->BlocksMouseLook(),
                Mode->PointerClosedFrame == GFrameCounter);
            TestTrue(TEXT("Travel reaches the requested real rest floor with nearby capsule clearance"),
                Tower.CurrentFloor() == Point.Floor && FVector::Dist2D(Player->GetActorLocation(), Point.Position) < 150.);
            const auto* Capsule = Player->GetCapsuleComponent();
            FCollisionQueryParams Query(SCENE_QUERY_STAT(NorthFastTravelQA), false, Player); Query.AddIgnoredActor(Boss);
            Query.AddIgnoredActor(Story->Companion());
            TestFalse(TEXT("The restored player capsule is not in furniture or a wall"),
                World->OverlapBlockingTestByChannel(Player->GetActorLocation(), FQuat::Identity, ECC_Pawn,
                    FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius()-1, Capsule->GetScaledCapsuleHalfHeight()-2), Query));
            FMinimalViewInfo View; Player->CalcCamera(1.f/60.f, View);
            TestTrue(TEXT("The real camera follows the destination without a cross-map lag or old view target"),
                !View.Location.ContainsNaN() && FVector::Dist(View.Location, Player->GetActorLocation()) < 750. &&
                (!Controller || Controller->GetViewTarget() == Player));
            if (Point.Floor == EFloor::Dungeon || Point.Floor == EFloor::Mirror || Point.Floor == EFloor::Final)
            {
                const auto* Room = FindRoom(Locate(Player->GetActorLocation()).Room);
                TestTrue(TEXT("Antechamber travel keeps the real camera inside the arrival room, clear of the rear doorway"),
                    Room && Room->Bounds.IsInside(View.Location));
            }
            const auto* Partner = Story->Companion();
            TestTrue(TEXT("The selected route's following partner reconstructs on the destination floor"),
                Partner && !Partner->IsHidden() && Partner->SelectedCharacter() == State.Companion() &&
                FVector::Dist2D(Partner->GetActorLocation(), Player->GetActorLocation()) < 550. &&
                FMath::Abs(Partner->GetActorLocation().Z-Player->GetActorLocation().Z) < 10.);
            if (World->AllowAudioPlayback())
            {
                TestEqual(TEXT("Exploration BGM switches immediately to the arrival floor"), Mode->Arena->ExplorationMusicCue,
                    FName(Point.Floor == EFloor::Dungeon || Point.Floor == EFloor::Prison ? TEXT("UnderStone") : TEXT("MarginNotes")));
                const auto* Audio = Mode->Arena->ExplorationMusic.Get();
                auto* Wave = Audio ? Cast<USoundWaveProcedural>(Audio->Sound) : nullptr;
                TestTrue(TEXT("Arrival music has an actual playing decoded PCM component, not only a cue name"),
                    Audio && Audio->IsPlaying() && !Audio->bIsPaused && Wave && Wave->GetAvailableAudioByteCount() > 0);
                AddInfo(FString::Printf(TEXT("FastTravelAudio route=%d floor=%d cue=%s playing=%d queued_pcm_bytes=%d"),
                    int32(Route), int32(Point.Floor), *Mode->Arena->ExplorationMusicCue.ToString(),
                    Audio && Audio->IsPlaying(), Wave ? Wave->GetAvailableAudioByteCount() : 0));
            }
            TestTrue(TEXT("Travel never awards a boss, opens a gate, collects a clue, or lights another rest"),
                Tower.State.Wins == Wins && Tower.State.Relics == Relics && Tower.State.Doors == Doors &&
                Tower.State.Clues == Clues && Tower.State.ActivatedCheckpoints == Proof && Tower.State.VisitedCheckpoints == Visited);
        }
        Tower.bMap = true; Tower.SelectTravelPoint(EFloor::Dungeon);
        TestTrue(TEXT("Esc/map cancellation clears the selection without moving"), Tower.CloseMap() &&
            Tower.SelectedTravelPoint() == EFloor::Unknown);
        Tower.bMap = true; Tower.State.ActivatedCheckpoints = 0; Tower.State.VisitedCheckpoints = 1 << uint8(EDuelNorthFloor::Prison);
        Tower.State.bPrisonRestActivated = false;
        TestFalse(TEXT("A known-looking map icon without actual proof cannot be selected"), Tower.SelectTravelPoint(EFloor::Dungeon));
        Tower.State = State;
        const auto* Blocked = Checkpoints().FindByPredicate([](const FCheckpoint& Point) { return Point.Floor == EFloor::Dungeon; });
        Obstruction = World->SpawnActor<AActor>();
        auto* Block = NewObject<UBoxComponent>(Obstruction);
        Obstruction->SetRootComponent(Block); Obstruction->AddInstanceComponent(Block);
        Block->SetBoxExtent(FVector(300,300,200)); Block->SetCollisionProfileName(TEXT("BlockAll")); Block->RegisterComponent();
        Obstruction->SetActorLocation(Blocked->Position + FVector(0,0,100));
        Tower.bMap = true; Tower.SelectTravelPoint(EFloor::Dungeon);
        const FVector BeforeBlocked = Player->GetActorLocation();
        TestFalse(TEXT("Blocked destination rejects travel rather than embedding the player"), Tower.ConfirmTravel(*Mode));
        TestTrue(TEXT("Rejected landing preserves the player's position"), Player->GetActorLocation().Equals(BeforeBlocked, .001));
        Obstruction->Destroy(); Obstruction = nullptr;
        Tower.bMap = false;
    }
    return !HasAnyErrors();
}

#endif
