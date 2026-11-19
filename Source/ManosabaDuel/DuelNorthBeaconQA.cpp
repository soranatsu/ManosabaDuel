#include "DuelNorthTower.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DuelGame.h"
#include "DuelNorthFinds.h"
#include "DuelNorthInvestigationQA.h"
#include "DuelNorthLampworks.h"
#include "DuelNorthLighting.h"
#include "DuelNorthNarrative.h"
#include "CollisionShape.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthBeaconTest, "ManosabaDuel.NorthTower.Beacon",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthBeaconTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace DuelNorthLayout;
    constexpr float Frame = 1.f / 60.f;
    const FName Chart(TEXT("BeaconChart")), Sketch(TEXT("LensWorkshop")), Letter(TEXT("BeaconLetter"));
    const FName Switch(TEXT("BeaconSwitch")), Gate(TEXT("FinalGate"));
    const TCHAR* Command = FCommandLine::Get();
    constexpr const TCHAR* Slot = TEXT("NorthTower200IntegrationQA");
    const bool bDisk = FParse::Param(Command, TEXT("DuelNorthSaveTest"));
    if (!TestTrue(TEXT("Run the prison mail investigation in an isolated North Tower"), FParse::Param(Command, TEXT("DuelNorthTower")) &&
        bDisk != FParse::Param(Command, TEXT("DuelNoSave")))) return false;
    if (bDisk)
    {
        FString UserDirectory;
        const FString Saved = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
        const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Saved"));
        if (!TestTrue(TEXT("A fresh test slot in an explicit project-local user directory is required"),
            FParse::Value(Command, TEXT("UserDir="), UserDirectory) && !UserDirectory.IsEmpty() &&
            FPaths::IsUnderDirectory(Saved, Root) && !FPaths::IsSamePath(Saved, Root) &&
            !UGameplayStatics::DoesSaveGameExist(Slot, 0))) return false;
    }

    ADuelMode* Mode = nullptr;
    for (const auto& Context : GEngine->GetWorldContexts())
        if (Context.World() && (Mode = Context.World()->GetAuthGameMode<ADuelMode>())) break;
    if (!TestTrue(TEXT("Live tower, protagonist and input are active"), Mode && Mode->NorthTower() &&
        Mode->GetPlayerFighter() && Mode->GetWorld()->GetFirstPlayerController())) return false;
    auto& Tower = *const_cast<ADuelNorthTower*>(Mode->NorthTower());
    auto& Player = *const_cast<ADuelFighter*>(Mode->GetPlayerFighter());
    auto& Motion = *Player.GetCharacterMovement();
    UWorld& World = *Mode->GetWorld();
    auto& PC = *World.GetFirstPlayerController();
    if (!TestTrue(TEXT("Gameplay action bindings are installed"), PC.PlayerInput && Player.InputComponent)) return false;
    const TArray<UInputComponent*> InputStack{Player.InputComponent.Get()};
    const auto* Capsule = Player.GetCapsuleComponent();
    const FVector Lift(0, 0, Capsule->GetScaledCapsuleHalfHeight() + 2.);
    const auto Initial = Tower.State;
    const auto InitialRooms = Tower.Explored;
    const auto InitialPosition = Player.GetActorTransform();
    ON_SCOPE_EXIT
    {
        PC.FlushPressedKeys();
        Motion.StopMovementImmediately();
        Tower.State = Initial;
        Tower.Explored = InitialRooms;
        Tower.Respawn(*Mode);
        Player.SetActorTransform(InitialPosition, false, nullptr, ETeleportType::TeleportPhysics);
        Tower.Update(*Mode, 0.f);
    };

    Tower.NewGame(*Mode);
    if (!TestTrue(TEXT("Seed the prior opening without any prison mail evidence"),
        Tower.State.ReadClue(EDuelNorthClue::OpeningDocket) && Tower.State.OpenStartingCell())) return false;
    for (int32 Battle = 0; Battle < 2; ++Battle)
        if (!TestTrue(TEXT("Seed the prior encounter, relic and route only"), DuelNorthInvestigationQA::Prepare(Tower.State, Battle) &&
            Tower.State.WinBattle(Battle) && Tower.State.PickupRelic(static_cast<EDuelNorthRelic>(Battle)) &&
            Tower.State.OpenDoor(static_cast<EDuelNorthDoor>(Battle)))) return false;
    if (!TestTrue(TEXT("Use the authored Mirror rest for restart"), Tower.State.ActivateCheckpoint(EDuelNorthFloor::Mirror))) return false;
    Tower.Respawn(*Mode);
    Tower.RefreshGates();
    if (bDisk)
    {
        Tower.Save();
        if (!TestTrue(TEXT("The checkpoint reloads before any evidence has been examined"), Tower.LoadCheckpoint(*Mode) &&
            Tower.State.BeaconBits == 0 && !Tower.State.CanEnterBattle(2))) return false;
    }

    const auto* Stair = FindRoute(TEXT("FinalStair"));
    const auto* Landing = FindRoom(TEXT("FinalStairLanding1"));
    const auto FindDoor = [&](FName Id) { return Portals().FindByPredicate([&](const FPortal& P) { return P.Id == Id; }); };
    const FPortal* LandingDoor = FindDoor(TEXT("BeaconLanding"));
    const FPortal* LowerDoor = FindDoor(TEXT("FinalLower"));
    const FPortal* UpperDoor = FindDoor(TEXT("FinalUpper"));
    if (!TestTrue(TEXT("The prison office entrance joins the actual switchback turn"), Stair && Landing && LandingDoor && LowerDoor && UpperDoor &&
        FMath::IsNearlyEqual(Landing->Bounds.Min.Z, LandingDoor->Center.Z))) return false;
    const double Z = LandingDoor->Center.Z;
    const FVector Turn(Landing->Bounds.GetCenter().X, LandingDoor->Center.Y, Z);
    const int32 BottomCorner = Stair->Points.IndexOfByPredicate([&](const FVector& At)
    { return FMath::IsNearlyEqual(At.X, Turn.X) && FMath::IsNearlyEqual(At.Z, Z) && At.Y < Turn.Y; });
    if (!TestTrue(TEXT("The shared landing sits on both authored stair directions"), BottomCorner != INDEX_NONE &&
        Stair->Points.IsValidIndex(BottomCorner + 1) && Stair->Points[BottomCorner + 1].Y > Turn.Y)) return false;

    const auto Tap = [&](const FKey& Key)
    {
        for (const EInputEvent Event : {IE_Pressed, IE_Released})
        {
            PC.InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, Key, Event,
                Event == IE_Pressed ? 1.f : 0.f, false, 0));
            PC.PlayerInput->ProcessInputStack(InputStack, Frame, UGameplayStatics::IsGamePaused(&World));
            Tower.Update(*Mode, Frame);
        }
    };
    int32 Frames = 0;
    double Distance = 0.;
    const auto Walk = [&](const FVector& Goal)
    {
        const double InitialDistance = FVector::Dist2D(Player.GetActorLocation(), Goal);
        const int32 Budget = FMath::CeilToInt(InitialDistance / (140. * Frame)) + 240;
        for (int32 Tick = 0; Tick < Budget; ++Tick)
        {
            const FVector Before = Player.GetActorLocation();
            const FVector Offset = Goal + Lift - Before;
            if (Offset.Size2D() < 9. && FMath::Abs(Offset.Z) < 24. && !Motion.IsFalling())
            {
                Motion.StopMovementImmediately();
                Tower.Update(*Mode, Frame);
                return true;
            }
            Motion.AddInputVector(Offset.GetSafeNormal2D() * FMath::Clamp(Offset.Size2D() / 52., .14, 1.));
            Motion.TickComponent(Frame, LEVELTICK_All, nullptr);
            const FVector After = Player.GetActorLocation();
            if (After.ContainsNaN() || FVector::Dist(Before, After) > 70.) break;
            ++Frames;
            Distance += FVector::Dist(Before, After);
            Tower.Update(*Mode, Frame);
        }
        AddError(FString::Printf(TEXT("Walk to %s stopped at %s, room %s"), *Goal.ToCompactString(),
            *Player.GetActorLocation().ToCompactString(), *Tower.Location.Room.ToString()));
        return false;
    };
    const auto WalkPath = [&](std::initializer_list<FVector> Points)
    {
        for (const FVector& At : Points) if (!Walk(At)) return false;
        return true;
    };
    const auto WalkStair = [&](int32 Start, int32 End)
    {
        const int32 Direction = Start <= End ? 1 : -1;
        for (int32 I = Start; I != End + Direction; I += Direction) if (!Walk(Stair->Points[I])) return false;
        return true;
    };
    const auto Navigation = [&](const FPortal& Immediate, FName Target)
    {
        ADuelNorthTower::FObjectiveRoute Route;
        const auto* Goal = FindLandmark(Target);
        if (!TestTrue(TEXT("Map route reaches the named station through the expected doorway"), Goal &&
            Tower.BuildObjectiveRoute(Tower.Location, Tower.CurrentFloor(), Route) && !Route.Doors.IsEmpty() &&
            Route.Doors[0] == &Immediate && Route.Goal.Equals(Tower.LandmarkPosition(*Goal), .25))) return false;
        Tap(EKeys::M);
        FVector Marker;
        const bool bMapped = TestTrue(TEXT("M opens the map and marks this floor's next threshold"), Tower.MapVisible() &&
            Tower.MapObjectivePosition(Marker) && Marker.Equals(Immediate.Center, .25));
        Tap(EKeys::M);
        if (!bMapped || !TestFalse(TEXT("M returns to the world"), Tower.MapVisible())) return false;
        for (int32 I = 0; I < 63; ++I) Tower.Update(*Mode, Frame);
        const auto Light = DuelNorthLighting::Stats(&Tower);
        return TestTrue(FString::Printf(TEXT("The live guide plots a collision-clear path from %s"), *Immediate.Id.ToString()),
            Light.bGuidance && !Light.GuidanceTarget.IsNearlyZero());
    };
    const auto From = [&](FName Marker) { return Tower.LandmarkPosition(*FindLandmark(Marker)); };
    const auto Focus = [&](FName Marker)
    {
        return TestTrue(FString::Printf(TEXT("F focuses the installed %s"), *Marker.ToString()),
            Tower.Nearby == Marker && Tower.Prompt().StartsWith(TEXT("F")));
    };
    const auto Read = [&](FName Marker, bool bSkip)
    {
        if (!Focus(Marker) || !TestFalse(TEXT("The record arrives from the station interaction"), Tower.State.HasBeacon(Marker))) return false;
        if (Marker == Sketch && !TestTrue(TEXT("The physical observation station uses its current player-visible record name"),
            Tower.Prompt().Contains(TEXT("观察记录")))) return false;
        Tap(EKeys::F);
        const auto* Story = Tower.Narrative();
        if (!TestTrue(TEXT("Taking the record first displays its original text on a separate paper page"),
            Story && Story->IsReadingDocument() && Story->Subtitle().Text.IsEmpty() && !Mode->CanExplore())) return false;
        Tap(EKeys::F);
        if (!TestTrue(TEXT("F reveals the complete page without starting the analysis"), Story->IsReadingDocument() &&
            Story->DocumentVisibleCharacters() == Story->DocumentText().Len() && Story->Subtitle().Text.IsEmpty())) return false;
        Tap(EKeys::F);
        if (!TestTrue(TEXT("A real evidence interaction starts a multi-line portrait scene"),
            Story && Story->IsTalking() && Story->Subtitle().Count > 1 && !Story->Subtitle().Text.IsEmpty() &&
            !Mode->CanExplore())) return false;
        if (bSkip) Tap(EKeys::Escape);
        else
        {
            const int32 Lines = Story->Subtitle().Count;
            for (int32 I = 0; I < Lines && Story->IsTalking(); ++I) Tap(EKeys::F);
        }
        return TestTrue(TEXT("Finishing or skipping preserves the discovered evidence and restores walking"),
            Tower.State.HasBeacon(Marker) && !Story->IsTalking() && Mode->CanExplore());
    };

    const FVector Below = LowerDoor->Center + FVector(112, 0, 0);
    const FVector Above = UpperDoor->Center - FVector(110, 0, 0);
    Player.PrepareExploration();
    Motion.SetMovementMode(MOVE_Walking);
    Player.SetActorLocation(Below + Lift, false, nullptr, ETeleportType::TeleportPhysics);
    Tower.Update(*Mode, Frame);
    const int32 Top = Stair->Points.Num() - 1;
    const FVector UpperWest(-3500, Above.Y, Above.Z), WestAnte(-3500, 0, Above.Z);
    const FVector Trial = From(Gate);
    if (!Navigation(*LowerDoor, Chart) || !WalkStair(0, Top) || !Walk(Above) || !Navigation(*UpperDoor, Chart) ||
        !WalkPath({UpperWest, WestAnte, Trial}) || !Focus(Gate)) return false;
    Tap(EKeys::F);
    if (!TestTrue(TEXT("Walking past the optional branch cannot open the final fog"),
        !Tower.IsGateOpen(Gate) && !Tower.State.CanEnterBattle(2))) return false;
    if (!WalkPath({WestAnte, UpperWest, Above}) || !WalkStair(Top, BottomCorner + 1) || !Walk(Turn) ||
        !Navigation(*LandingDoor, Chart)) return false;

    const FVector DoorInside = LandingDoor->Center - FVector(110, 0, 0);
    const FVector ChartExit(-300, -2490, Z), GalleryHead(-545, -2490, Z), WorkshopFork(-910, -2490, Z);
    const FVector WorkshopStep(-910, -2200, Z), ServiceDoor(-1390, -2010, Z), ServiceStep(-1460, -2010, Z);
    const FVector ServiceWest(-2050, -2010, Z), OpticsViaService(-2280, -2000, Z);
    const FVector CabinFork(-1660, -2490, Z), CabinStep(-1660, -2820, Z);
    const FVector OpticsDoor(-2160, -2520, Z), OpticsStep(-2280, -2520, Z);
    if (!WalkPath({DoorInside, From(Chart)}) || !Read(Chart, false) ||
        !WalkPath({ChartExit, GalleryHead, WorkshopFork, WorkshopStep, From(Sketch)}) || !Read(Sketch, true)) return false;
    if (!WalkPath({FVector(-1130, -2010, Z), ServiceDoor, ServiceStep, ServiceWest, OpticsViaService, From(Switch)}) ||
        !Focus(Switch)) return false;
    Tap(EKeys::F);
    if (!TestTrue(TEXT("Arriving via the service loop cannot skip the order in the warden's office"),
        Tower.GetPuzzle() == INDEX_NONE && !Tower.State.HasBeacon(Switch) && !Tower.State.CanEnterBattle(2))) return false;
    if (!WalkPath({OpticsStep, OpticsDoor, FVector(-2010, -2520, Z), CabinFork, CabinStep, From(Letter)}) ||
        !Read(Letter, false)) return false;
    if (!WalkPath({CabinStep, CabinFork, FVector(-2010, -2520, Z), OpticsDoor, OpticsStep, From(Switch)}) ||
        !Focus(Switch)) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(NorthBeaconFurniture), false, &Player);
    Query.AddIgnoredActor(Mode->GetBossFighter());
    const FCollisionShape Body = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
    const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
    const FVector Surface = DuelNorthLampworks::Surface(DuelNorthLampworks::Index(Switch));
    FHitResult Collision;
    const bool bHit = World.SweepSingleByChannel(Collision, Player.GetActorLocation(), FVector(Surface.X, Surface.Y, Z) + Lift,
        FQuat::Identity, ECC_Pawn, Body, Query, Responses);
    if (!TestTrue(TEXT("The seal comparison table physically blocks the player's swept capsule"), bHit &&
        Collision.GetActor() == &Tower && Collision.GetComponent() && Collision.GetComponent()->ComponentHasTag(TEXT("NorthEvidenceTableBody")) &&
        Collision.GetComponent()->ComponentHasTag(Switch)))
        return false;

    const auto TrialAt = [&](int32 Round, int32 Choice)
    { return Tower.GetPuzzle() == 2 && Tower.GetPuzzleRound() == Round && Tower.GetPuzzleChoice() == Choice; };
    const TInlineComponentArray<USceneComponent*> Components(&Tower);
    const auto GetPart = [&](FName Tag) -> const USceneComponent*
    {
        const auto* Found = Components.FindByPredicate([&](const USceneComponent* Part)
            { return Part->IsRegistered() && Part->ComponentHasTag(Tag); });
        return Found ? *Found : nullptr;
    };
    const USceneComponent* Bags[] = {GetPart(TEXT("EvidenceBag0")), GetPart(TEXT("EvidenceBag1")), GetPart(TEXT("EvidenceBag2"))};
    const USceneComponent* Rings[] = {GetPart(TEXT("EvidenceRing0")), GetPart(TEXT("EvidenceRing1"))};
    const auto* Flap = GetPart(TEXT("NorthEvidenceBagFlap"));
    const auto* Letters = GetPart(TEXT("NorthSeizedLetters"));
    const auto* Intact = GetPart(TEXT("NorthSeizedMailIntactSeal"));
    const auto* Broken = GetPart(TEXT("NorthSeizedMailBrokenSeal"));
    const auto* Wrong = GetPart(TEXT("NorthEvidenceWrongImpression"));
    const auto* Matched = GetPart(TEXT("NorthEvidenceMatchedImpression"));
    if (!TestTrue(TEXT("The desk contains three physical bags, two rings, a seal and the concealed letters"),
        Bags[0] && Bags[1] && Bags[2] && Rings[0] && Rings[1] && Flap && Letters && Intact && Broken && Wrong && Matched)) return false;
    const FVector BagRest[] = {Bags[0]->GetRelativeLocation(), Bags[1]->GetRelativeLocation(), Bags[2]->GetRelativeLocation()};
    const FVector RingRest[] = {Rings[0]->GetRelativeLocation(), Rings[1]->GetRelativeLocation()};
    const auto Shown = [](const USceneComponent* Part) { return Part->IsVisible() && !Part->bHiddenInGame; };
    const auto BagsAt = [&](int32 Raised)
    {
        for (int32 I = 0; I < UE_ARRAY_COUNT(Bags); ++I)
            if (!Shown(Bags[I]) || !Bags[I]->GetRelativeLocation().Equals(BagRest[I] + FVector(0, 0, I == Raised ? 7. : 0.), .25))
                return false;
        return true;
    };
    const auto RingAt = [&](int32 Index)
    {
        return Shown(Rings[Index]) && Rings[1 - Index]->GetRelativeLocation().Equals(RingRest[1 - Index], .25) &&
            Rings[Index]->GetRelativeLocation().Z > RingRest[Index].Z + 6. &&
            FVector::DistSquared2D(Rings[Index]->GetRelativeLocation(), Matched->GetRelativeLocation()) < 1.;
    };
    const auto IntactBag = [&]
    {
        return FMath::IsNearlyZero(Flap->GetRelativeRotation().Roll, .25) &&
            Shown(Intact) && !Shown(Broken) && !Shown(Letters) && !Shown(Matched);
    };
    const auto OpenBag = [&]
    {
        return FMath::Abs(Flap->GetRelativeRotation().Roll) > 90. &&
            !Shown(Intact) && Shown(Broken) && Shown(Letters) && Shown(Matched) && !Shown(Wrong);
    };
    if (!TestTrue(TEXT("The accepted choices are the red wax bag and the damaged right petal"),
        DuelNorthFinds::Correct(2, 0, 1) && DuelNorthFinds::Correct(2, 1, 0) &&
        FString(DuelNorthFinds::Answer(2, 0, 1)).Contains(TEXT("红蜡封袋")) &&
        FString(DuelNorthFinds::Answer(2, 1, 0)).Contains(TEXT("右边一瓣缺角")))) return false;
    Tap(EKeys::F);
    if (!TestTrue(TEXT("F opens the two-stage seal comparison and physically raises the first bag"),
        TrialAt(0, 0) && BagsAt(0) && IntactBag() && !Mode->CanExplore())) return false;
    Tap(EKeys::F);
    if (!TestTrue(TEXT("A wrong bag remains raised, keeps its seal closed and gives feedback"),
        TrialAt(0, 0) && BagsAt(0) && IntactBag() && !Tower.PuzzleHint().IsEmpty())) return false;
    Tap(EKeys::D);
    if (!TestTrue(TEXT("D lowers the first bag and raises the red wax bag"), TrialAt(0, 1) && BagsAt(1))) return false;
    Tap(EKeys::A);
    if (!TestTrue(TEXT("A returns to the first bag and lowers the red wax bag"), TrialAt(0, 0) && BagsAt(0))) return false;
    Tap(EKeys::D); Tap(EKeys::F);
    if (!TestTrue(TEXT("The red wax bag moves to the tray with the first trial ring poised above the wax"),
        TrialAt(1, 1) && Bags[1]->GetRelativeLocation().Equals(FVector(0, -18, 2), .25) &&
        RingAt(1) && IntactBag() && !Tower.State.CanEnterBattle(2))) return false;
    Tap(EKeys::F);
    if (!TestTrue(TEXT("The intact ring leaves a visible wrong impression and keeps the bag and final fog sealed"),
        TrialAt(1, 1) && Shown(Wrong) && IntactBag() && !Tower.PuzzleHint().IsEmpty() &&
        !Tower.State.HasBeacon(Switch) && !Tower.IsGateOpen(Gate))) return false;
    Tap(EKeys::A);
    if (!TestTrue(TEXT("A lifts the damaged ring over the test wax and clears the wrong impression"),
        TrialAt(1, 0) && RingAt(0) && !Shown(Wrong) && IntactBag())) return false;
    Tap(EKeys::Escape);
    if (!TestTrue(TEXT("Esc restores movement and the untouched arrangement of bags, rings and wax"),
        Tower.GetPuzzle() == INDEX_NONE && Mode->CanExplore() && !Tower.State.CanEnterBattle(2) && BagsAt(INDEX_NONE) &&
        Rings[0]->GetRelativeLocation().Equals(RingRest[0], .25) &&
        Rings[1]->GetRelativeLocation().Equals(RingRest[1], .25) && IntactBag() && !Shown(Wrong))) return false;
    if (bDisk)
    {
        const auto* Saved = Cast<UDuelNorthSave>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
        if (!TestTrue(TEXT("Unfinished seal comparisons cannot turn into a completed disk checkpoint"), Saved &&
            Saved->State.HasBeacon(Chart) && Saved->State.HasBeacon(Sketch) && Saved->State.HasBeacon(Letter) &&
            !Saved->State.HasBeacon(Switch) && !Saved->State.CanEnterBattle(2))) return false;
    }
    Tap(EKeys::F);
    if (!TestTrue(TEXT("Reopening the seal comparison starts both choices from the beginning"), TrialAt(0, 0) && BagsAt(0))) return false;
    Tap(EKeys::D); Tap(EKeys::F); Tap(EKeys::A); Tap(EKeys::F);
    if (!TestTrue(TEXT("The damaged ring matches the seal, opens the physical bag and reveals the withheld letters"),
        Tower.GetPuzzle() == INDEX_NONE && Tower.State.HasBeacon(Switch) && OpenBag() && Tower.State.CanEnterBattle(2))) return false;
    if (const auto* Story = Tower.Narrative(); Story && Story->IsTalking()) Tap(EKeys::Escape);
    if (bDisk)
    {
        const auto* Saved = Cast<UDuelNorthSave>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
        if (!TestTrue(TEXT("The completed seal comparison is committed to the actual checkpoint"), Saved &&
            Saved->State.HasBeacon(Switch) && Saved->State.CanEnterBattle(2))) return false;
    }

    if (!WalkPath({OpticsStep, OpticsDoor, FVector(-2010, -2520, Z), CabinFork, WorkshopFork, GalleryHead,
        ChartExit, DoorInside, Turn}) || !WalkStair(BottomCorner + 1, Top) || !WalkPath({Above, UpperWest, WestAnte, Trial}) ||
        !Focus(Gate)) return false;
    const FVector BeforeAdmission = Player.GetActorLocation();
    Tap(EKeys::F);
    if (!TestTrue(TEXT("A completed investigation lets F open the physical fog without moving the player"),
        Tower.IsGateOpen(Gate) && Player.GetActorLocation().Equals(BeforeAdmission, .25))) return false;
    for (int32 Tick = 0; Tick < 180 && Mode->Outcome() != EDuelOutcome::Fighting; ++Tick)
    {
        Motion.AddInputVector(FVector::ForwardVector);
        Motion.TickComponent(Frame, LEVELTICK_All, nullptr);
        Tower.Update(*Mode, Frame);
    }
    if (!TestTrue(TEXT("Walking across the open fog plays the final encounter"),
        Mode->Outcome() == EDuelOutcome::Fighting && Tower.ActiveFight == 2)) return false;
    if (bDisk && !TestTrue(TEXT("Loading the finished checkpoint restores the open bag with the final encounter unstarted"),
        Tower.LoadCheckpoint(*Mode) && Tower.State.HasBeacon(Switch) && Tower.State.CanEnterBattle(2) && OpenBag() &&
        !Tower.IsGateOpen(Gate))) return false;
    for (const TCHAR* Room : {TEXT("BeaconChartRoom"),TEXT("BeaconGallery"),TEXT("BeaconWorkshop"),TEXT("BeaconCabin"),
        TEXT("BeaconCrossway"),TEXT("BeaconOptics")})
        TestTrue(FString::Printf(TEXT("Real walking registered room %s on the map"), Room), Tower.IsRoomExplored(Room));
    AddInfo(FString::Printf(TEXT("Prison mail route: %.2f m in %d movement frames, six rooms, both approaches to the seal desk, pre/post fog and %s checkpoint"),
        Distance / 100., Frames, bDisk ? TEXT("disk") : TEXT("isolated no-save")));
    return !HasAnyErrors();
}

#endif
