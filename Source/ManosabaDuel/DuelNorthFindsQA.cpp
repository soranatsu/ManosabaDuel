#include "DuelNorthFinds.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Count.h"
#include "DuelGame.h"
#include "DuelNorthInteractables.h"
#include "DuelNorthLayout.h"
#include "DuelNorthNarrative.h"
#include "DuelNorthTower.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/MeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "ProceduralMeshComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthFindsTest, "ManosabaDuel.NorthTower.Finds",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthFindsTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace DuelNorthLayout;
    using namespace DuelNorthFinds;
    const TCHAR* Command = FCommandLine::Get();
    if (!TestTrue(TEXT("Finds QA uses a no-save North Tower game"),
        FParse::Param(Command, TEXT("DuelNorthTower")) && FParse::Param(Command, TEXT("DuelNoSave")))) return false;

    ADuelMode* Mode = nullptr;
    if (GEngine)
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
            if (Context.World() && (Mode = Context.World()->GetAuthGameMode<ADuelMode>())) break;
    if (!TestTrue(TEXT("The tower and player are in exploration"),
        Mode && Mode->Outcome() == EDuelOutcome::Ready && Mode->NorthTower() && Mode->GetPlayerFighter())) return false;

    auto& Tower = *const_cast<ADuelNorthTower*>(Mode->NorthTower());
    auto& Player = *const_cast<ADuelFighter*>(Mode->GetPlayerFighter());
    auto* PC = Cast<APlayerController>(Player.GetController());
    if (!TestTrue(TEXT("Actual F and navigation bindings are installed"),
        PC && PC->PlayerInput && Player.InputComponent)) return false;
    UWorld& World = *Mode->GetWorld();
    const FTransform SavedTransform = Player.GetActorTransform();
    const FDuelNorthProgress InitialState = Tower.State;
    const TSet<FName> InitialRooms = Tower.Explored;
    ON_SCOPE_EXIT
    {
        PC->FlushPressedKeys();
        Tower.State = InitialState;
        Tower.Explored = InitialRooms;
        Tower.Respawn(*Mode);
        Player.SetActorTransform(SavedTransform, false, nullptr, ETeleportType::TeleportPhysics);
        Tower.Update(*Mode, 0.f);
    };

    Tower.State.Reset(Player.SelectedCharacter());
    Tower.Respawn(*Mode);
    const TArray<UInputComponent*> InputStack{Player.InputComponent.Get()};
    constexpr float Frame = 1.f / 60.f;
    const auto Tap = [&](const FKey& Key)
    {
        for (const EInputEvent Event : {IE_Pressed, IE_Released})
        {
            PC->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, Key, Event,
                Event == IE_Pressed ? 1.f : 0.f, false, 0));
            const bool bPaused = UGameplayStatics::IsGamePaused(Mode);
            PC->PlayerInput->ProcessInputStack(InputStack, Frame, bPaused);
            if (!bPaused) Tower.Update(*Mode, Frame);
        }
    };
    const auto Place = [&](const FVector& At)
    {
        Player.GetCharacterMovement()->StopMovementImmediately();
        Player.GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        Player.SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
        Tower.Update(*Mode, 0.f);
    };
    const UCapsuleComponent* Capsule = Player.GetCapsuleComponent();
    const float Radius = Capsule->GetScaledCapsuleRadius();
    const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
    const FVector Lift(0., 0., HalfHeight + 2.);
    FCollisionQueryParams BodyQuery(SCENE_QUERY_STAT(FindsCapsuleClearance), false, &Player);
    BodyQuery.AddIgnoredActor(Mode->GetBossFighter());
    const FCollisionResponseParams BodyResponses(Capsule->GetCollisionResponseToChannels());
    const FCollisionShape Body = FCollisionShape::MakeCapsule(Radius, HalfHeight);
    const auto Clear = [&](const FVector& At)
    {
        if (World.OverlapBlockingTestByChannel(At, FQuat::Identity, ECC_Pawn, Body, BodyQuery, BodyResponses)) return false;
        const FVector Foot = At - Lift;
        FHitResult Floor;
        return World.LineTraceSingleByChannel(Floor, Foot + FVector(0,0,11), Foot - FVector(0,0,13), ECC_Pawn, BodyQuery) &&
            Floor.ImpactNormal.Z >= .70;
    };
    const auto OpenStep = [&](const FVector& From, const FVector& To)
    {
        return !World.SweepTestByChannel(From, To, FQuat::Identity, ECC_Pawn, Body, BodyQuery, BodyResponses);
    };
    const auto Visible = [&](const FVector& At, const FVector& Item)
    {
        FHitResult Wall;
        return !World.LineTraceSingleByChannel(Wall, At + FVector(0, 0, 12), Item, ECC_Visibility, BodyQuery);
    };
    const auto WalkTo = [&](const FVector& Goal)
    {
        auto& Motion = *Player.GetCharacterMovement();
        for (int32 FrameIndex = 0; FrameIndex < 120; ++FrameIndex)
        {
            const FVector Offset = Goal - Player.GetActorLocation();
            if (Offset.SizeSquared2D() < FMath::Square(6.) && FMath::Abs(Offset.Z) < 5.)
            {
                Motion.StopMovementImmediately();
                return true;
            }
            Motion.AddInputVector(Offset.GetSafeNormal2D() * FMath::Clamp(Offset.Size2D() / 50., .12, 1.));
            Motion.TickComponent(Frame, LEVELTICK_All, nullptr);
        }
        return false;
    };

    struct FTarget
    {
        FName Id;
        FVector Base, Item, Front;
        double HalfDepth, Range;
    };
    // Use the swept capsule to find a route, then walk it with the production movement component.
    const auto Approach = [&](const FTarget& Target, bool bMustFocus, FName& UsedDoor)
    {
        constexpr double Grid = 58.;
        const FRoom* Room = FindRoom(Locate(Target.Base + Lift, HalfHeight).Room);
        if (!Room) return false;
        const FBox2D Interior(FVector2D(Room->Bounds.Min) + FVector2D(Radius + 12.),
            FVector2D(Room->Bounds.Max) - FVector2D(Radius + 12.));
        const FIntPoint Directions[] = {{1,0}, {-1,0}, {0,1}, {0,-1}};
        struct FStep { FIntPoint GridPos; FVector WorldPos; int32 Parent; };
        for (const FPortal& Entry : Portals())
        {
            if (Entry.bWindow || (Entry.RoomA != Room->Id && Entry.RoomB != Room->Id) ||
                !Tower.IsGateOpen(Entry.Id) || !FMath::IsNearlyEqual(Entry.Center.Z, Target.Base.Z, 2.))
                continue;
            const double Dot = FVector::DotProduct(Room->Bounds.GetCenter() - Entry.Center, Entry.Normal);
            const FVector TowardRoom = Entry.Normal * FMath::Sign(Dot);
            const FVector Threshold = Entry.Center + Lift;
            const FVector Outside = Threshold - TowardRoom * (Radius + 38.);
            const FVector Inside = Threshold + TowardRoom * (Radius + 38.);
            if (!Clear(Outside) || !Clear(Inside) || !OpenStep(Outside, Inside)) continue;
            TSet<FIntPoint> Visited{FIntPoint::ZeroValue};
            TArray<FStep> Steps{{FIntPoint::ZeroValue, Inside, INDEX_NONE}};
            for (int32 Index = 0; Index < Steps.Num(); ++Index)
            {
                const FStep Current = Steps[Index];
                if (FVector::DistSquared2D(Current.WorldPos, Target.Item) < FMath::Square(Target.Range - 9.) &&
                    FVector::DotProduct(Current.WorldPos - Target.Base, Target.Front) > Target.HalfDepth + 8. &&
                    Visible(Current.WorldPos, Target.Item))
                {
                    Place(Current.WorldPos);
                    if (!bMustFocus || Tower.Nearby == Target.Id)
                    {
                        TArray<FVector> Path;
                        for (int32 At = Index; At != INDEX_NONE; At = Steps[At].Parent) Path.Add(Steps[At].WorldPos);
                        Place(Outside);
                        for (int32 Step = Path.Num() - 1; Step >= 0; --Step)
                        {
                            if (WalkTo(Path[Step])) continue;
                            AddError(FString::Printf(TEXT("%s could not walk from %s to %s"),
                                *Target.Id.ToString(), *Player.GetActorLocation().ToString(), *Path[Step].ToString()));
                            return false;
                        }
                        Tower.Update(*Mode, Frame);
                        UsedDoor = Entry.Id;
                        return !bMustFocus || Tower.Nearby == Target.Id;
                    }
                }
                for (const FIntPoint& Direction : Directions)
                {
                    const FIntPoint Next = Current.GridPos + Direction;
                    if (Visited.Contains(Next)) continue;
                    const FVector At = Inside + FVector(Next.X * Grid, Next.Y * Grid, 0.);
                    if (!Interior.IsInside(FVector2D(At)) || !Clear(At) || !OpenStep(Current.WorldPos, At)) continue;
                    Visited.Add(Next);
                    Steps.Add({Next, At, Index});
                }
            }
        }
        return false;
    };
    const auto FindTarget = [](const FFind& Find)
    {
        return FTarget{Find.Id, Find.Position, Find.ItemPosition(), Find.Front(), Find.Size.Y * .5, Reach};
    };

    const auto Finds = All();
    const auto PuzzleCount = Algo::CountIf(Finds, [](const FFind& Find) { return Find.bPuzzle; });
    if (!TestTrue(TEXT("The tower has ten ordinary finds and two puzzles"), Finds.Num() == 12 && PuzzleCount == 2)) return false;
    for (const FFind& Find : Finds)
    {
        const FLocation Location = Locate(Find.Position + Lift, HalfHeight);
        TestTrue(FString::Printf(TEXT("%s lies on its authored floor in a room"), *Find.Id.ToString()),
            !Location.Room.IsNone() && !Location.bOnStair && static_cast<EDuelNorthFloor>(Location.Floor) == Find.Floor);
    }

    const int32 VeilIndex = IndexOf(TEXT("FindVeil"));
    if (!TestEqual(TEXT("Relocating the sleeve preserves its 3.4.4 checkpoint bit"), VeilIndex, 7)) return false;
    const auto& Veil = Finds[VeilIndex];
    const uint32 VeilBit = 1u << VeilIndex;
    FName VeilDoor;
    if (!TestTrue(TEXT("White Mantle has one physical source in the open 06 room, not at the former chapel cabinet"),
        Veil.Spell == EDuelArt::WhiteMantle && Veil.Floor == EDuelNorthFloor::Dungeon &&
        Locate(Veil.Position + Lift, HalfHeight).Room == TEXT("DungeonSharedCell") &&
        Algo::CountIf(Finds, [](const auto& F) { return F.Spell == EDuelArt::WhiteMantle; }) == 1 &&
        !Finds.ContainsByPredicate([](const auto& F) { return F.Position.Equals(FVector(2620,2340,700)); }) &&
        !Tower.State.KnowsSpell(EDuelArt::WhiteMantle) && !Tower.State.CanEnterBattle(0) &&
        Approach(FindTarget(Veil), true, VeilDoor) && VeilDoor == TEXT("DungeonSharedDoor"))) return false;
    TestTrue(TEXT("Walking from the 06 doorway discovers its existing stable map ID and the numbered source prompt"),
        Tower.IsRoomExplored(TEXT("DungeonSharedCell")) && Tower.Prompt().Contains(TEXT("06号牢房")));
    Tap(EKeys::M);
    TestTrue(TEXT("The map opens with the actually visited 06 room recorded"),
        Tower.MapVisible() && Tower.IsRoomExplored(TEXT("DungeonSharedCell")));
    Tap(EKeys::M);
    Tap(EKeys::F);
    if (!TestTrue(TEXT("Reading the stitched sleeve before 03 grants White Mantle once without opening the main battle"),
        Tower.State.CacheBits == VeilBit && Tower.State.KnowsSpell(EDuelArt::WhiteMantle) &&
        !Tower.State.CanEnterBattle(0) && Tower.RecentTreasure() == Rewards(Veil) &&
        Tower.Notice.Contains(TEXT("白幕覆肩")) && Tower.Notice.Contains(TEXT("习得魔法：白幕护体")))) return false;
    auto* SavedVeil = NewObject<UDuelNorthSave>();
    SavedVeil->State = Tower.State;
    SavedVeil->Rooms = Tower.Explored.Array();
    TArray<uint8> SavedVeilBytes;
    if (!TestTrue(TEXT("The unchanged production checkpoint object serializes the relocated reward and explored room"),
        UGameplayStatics::SaveGameToMemory(SavedVeil, SavedVeilBytes))) return false;
    const auto* LoadedVeil = Cast<UDuelNorthSave>(UGameplayStatics::LoadGameFromMemory(SavedVeilBytes));
    if (!TestTrue(TEXT("A checkpoint round trip retains both the old reward bit and 06 exploration ID"), LoadedVeil &&
        LoadedVeil->State.CacheBits == VeilBit && LoadedVeil->State.KnowsSpell(EDuelArt::WhiteMantle) &&
        LoadedVeil->Rooms.Contains(TEXT("DungeonSharedCell")))) return false;
    Tower.State = LoadedVeil->State;
    Tower.Respawn(*Mode);
    auto* VeilCue = Tower.StoryCues.FindRef(Veil.Id).Get();
    TestTrue(TEXT("An already-collected 3.4.4 sleeve stays learned and absent at the new location"),
        Tower.State.KnowsSpell(EDuelArt::WhiteMantle) && VeilCue && VeilCue->bHiddenInGame);
    Tower.State.Reset(Player.SelectedCharacter());
    Tower.Explored.Reset();
    Tower.Respawn(*Mode);
    AddInfo(TEXT("Relocation: WhiteMantle FindVeil cache_bit=7 room=DungeonSharedCell visible_number=06; real movement/F/map and checkpoint memory round trip checked"));

    // Leave the earlier battles locked until their evidence is collected through F.
    const FFind* Locked = Finds.FindByPredicate([](const FFind& Find) { return Find.Floor == EDuelNorthFloor::Final; });
    if (!TestNotNull(TEXT("A final-floor cache exists for the floor-lock test"), Locked)) return false;
    FName LockedDoor;
    if (TestTrue(TEXT("A final-floor cache can be physically approached in the isolated locked fixture"),
        Approach(FindTarget(*Locked), false, LockedDoor)))
    {
        Tap(EKeys::F);
        TestTrue(TEXT("A locked floor cannot be used to acquire a cache even at its position"),
            !Tower.State.CanVisitFloor(Locked->Floor) && Tower.Nearby != Locked->Id && Tower.State.CacheBits == 0);
    }
    const auto CycleTo = [&](int32 Choice)
    {
        for (int32 Step = 0; Step < 3 && Tower.PuzzleChoice != Choice; ++Step) Tap(EKeys::D);
        return TestEqual(TEXT("D advances the real puzzle choice"), Tower.PuzzleChoice, Choice);
    };
    const auto FinishInvestigation = [&](FName Id, bool bExpected)
    {
        const auto* Story = Tower.Narrative();
        if (!TestNotNull(TEXT("The investigation has the normal narrative actor"), Story)) return false;
        if (Story->IsReadingDocument())
        {
            if (!TestTrue(TEXT("The original record is shown before any deduction or portrait speech"),
                Story->IsTalking() && !Story->DocumentText().IsEmpty() && Story->Subtitle().Text.IsEmpty() && !Mode->CanExplore())) return false;
            Tap(EKeys::F);
            if (Story->IsReadingDocument()) Tap(EKeys::F);
        }
        if (!TestEqual(Id.ToString() + TEXT(" opens a portrait scene when there is a new deduction"), Story->IsTalking(), bExpected))
            return false;
        if (bExpected)
        {
            const auto Subtitle = Story->Subtitle();
            if (!TestTrue(TEXT("The portrait scene begins with the player thinking and holds movement"),
                Subtitle.Line == 1 && Subtitle.Count > 1 && Subtitle.Character == Tower.State.Player() && !Mode->CanExplore())) return false;
            for (int32 Line = 0; Line < Subtitle.Count && Story->IsTalking(); ++Line) Tap(EKeys::F);
        }
        return TestTrue(TEXT("The completed deduction releases world input"), !Story->IsTalking() && Mode->CanExplore());
    };
    const auto ReadLedger = [&](FName Id)
    {
        using namespace DuelNorthInteractables;
        const FPlacement* Shelf = Ledger(Id);
        if (!TestNotNull(TEXT("The record is placed on a physical shelf"), Shelf)) return false;
        FName Door;
        if (!TestTrue(Id.ToString() + TEXT(" can be approached from a real doorway"),
            Approach({Id, Shelf->Position, Shelf->ReadingPoint(), Shelf->Front(), Shelf->Size.Y, LedgerReach}, true, Door)))
            return false;
        const bool bArchive = Shelf->IsArchiveVolume();
        const TCHAR* Action = bArchive ? (Shelf->bOptional ? TEXT("F  阅读") : TEXT("F  从书架抽出")) : TEXT("F  查阅");
        TestTrue(Id.ToString() + TEXT(" focuses the ledger's reading action"), Tower.Prompt() == FString(Action) + Shelf->LedgerTitle);
        bool bShelfVisible = false, bPageCue = false;
        const UProceduralMeshComponent* Volume = nullptr;
        for (const auto* Piece : TInlineComponentArray<UProceduralMeshComponent*>(&Tower))
        {
            if (!Piece->ComponentHasTag(Id) || !Piece->IsRegistered() || Piece->bHiddenInGame) continue;
            bShelfVisible |= Piece->ComponentHasTag(TEXT("NorthLedgerShelf"));
            if (Piece->ComponentHasTag(TEXT("NorthArchiveVolume"))) Volume = Piece;
            bPageCue |= Piece->ComponentHasTag(TEXT("NorthStoryCue")) &&
                Piece->GetComponentLocation().Equals(Shelf->ReadingPoint(), .25) &&
                (bArchive ? Piece->ComponentHasTag(TEXT("NorthArchiveReadingCue")) && Piece->Bounds.BoxExtent.Z <= 9. :
                    Piece->Bounds.GetBox().Max.Z < Shelf->Position.Z + Shelf->Size.Z - 39.);
        }
        TestTrue(Id.ToString() + TEXT(" has a visible volume with its attached reading cue"),
            (bArchive ? Volume && !bShelfVisible : bShelfVisible) && bPageCue);
        const FVector Seated = Volume ? Volume->GetComponentLocation() : FVector::ZeroVector;
        const uint32 Before = Tower.State.CacheBits;
        Tap(EKeys::F);
        if (Volume)
        {
            for (int32 Tick = 0; Tick < 14; ++Tick) Tower.Update(*Mode, Frame);
            TestTrue(Id.ToString() + TEXT(" pulls out from the shelf along the spine normal"),
                FVector::DotProduct(Volume->GetComponentLocation() - Seated, Shelf->Front()) > 30.);
            for (int32 Tick = 0; Tick < 60; ++Tick) Tower.Update(*Mode, Frame);
            TestTrue(Id.ToString() + TEXT(" settles into its own slot after reading"), Volume->GetComponentLocation().Equals(Seated, .25));
        }
        if (!FinishInvestigation(Id, !Shelf->bOptional)) return false;
        AddInfo(FString::Printf(TEXT("Finds ledger: %s door=%s capsule=%s"),
            *Id.ToString(), *Door.ToString(), *Player.GetActorLocation().ToCompactString()));
        return TestTrue(Id.ToString() + TEXT(" is recorded by F without granting the puzzle reward"),
            Tower.State.HasClue(static_cast<EDuelNorthClue>(Shelf->Index)) && Tower.State.CacheBits == Before);
    };
    int32 Acquired = 0;
    for (int32 Index = 0; Index < Finds.Num(); ++Index)
    {
        const FFind& Find = Finds[Index];
        const FString Label = Find.Id.ToString();
        const FRoom* Room = FindRoom(Locate(Find.Position + Lift, HalfHeight).Room);
        FName Door;
        if (!TestTrue(Label + TEXT(" has a wall- and furniture-clear route from a real doorway"),
            Room && Approach(FindTarget(Find), true, Door)))
        {
            Tower.Respawn(*Mode);
            continue;
        }
        const uint32 Before = Tower.State.CacheBits;
        const uint32 Bit = 1u << Index;
        UProceduralMeshComponent* Cue = Tower.StoryCues.FindRef(Find.Id);
        const USceneComponent* Pickup = Cue ? Cue->GetAttachParent() : nullptr;
        const USceneComponent* Fixture = Pickup ? Pickup->GetAttachParent() : nullptr;
        TArray<USceneComponent*> ItemPieces;
        if (Pickup) Pickup->GetChildrenComponents(false, ItemPieces);
        const bool bHasVisibleItem = ItemPieces.ContainsByPredicate([&](const USceneComponent* Piece)
        {
            const auto* Mesh = Cast<UMeshComponent>(Piece);
            return Mesh && Mesh != Cue && Mesh->IsRegistered() && Mesh->IsVisible() &&
                Mesh->Bounds.GetBox().ComputeSquaredDistanceToPoint(Find.ItemPosition()) < FMath::Square(3.);
        });
        const bool bHasWeapon = ItemPieces.ContainsByPredicate([](const USceneComponent* Piece)
        {
            return Piece->IsVisible() && Piece->ComponentHasTag(TEXT("NorthFindWeapon"));
        });
        const auto* FixtureBody = Fixture ? Fixture->GetAttachChildren().FindByPredicate([](const auto& Child)
            { return Child->ComponentHasTag(TEXT("NorthFindBody")); }) : nullptr;
        const auto* BodyBox = FixtureBody ? Cast<UBoxComponent>(FixtureBody->Get()) : nullptr;
        TestTrue(Label + TEXT(" has registered colliding furniture beneath a physical pickup"), Fixture && Fixture->IsVisible() &&
            Fixture->ComponentHasTag(TEXT("NorthFindFixture")) && Pickup->ComponentHasTag(TEXT("NorthFindPickup")) &&
            BodyBox && BodyBox->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block &&
            BodyBox->GetUnscaledBoxExtent().Equals(Find.Size * .5) && bHasVisibleItem);
        TestTrue(Label + TEXT(" shows its weapon model when the reward includes equipment"),
            bHasWeapon == (Find.Weapon != EDuelWeapon::Empty));
        auto* Material = Cue ? Cast<UMaterialInstanceDynamic>(Cue->GetMaterial(0)) : nullptr;
        const FLinearColor Tint = Material ? Material->K2_GetVectorParameterValue(TEXT("Tint")) : FLinearColor::Black;
        TestTrue(Label + TEXT(" attaches its visible gold cue to the loot"), Cue && Cue->IsVisible() && !Cue->bHiddenInGame &&
            Cue->GetCollisionEnabled() == ECollisionEnabled::NoCollision &&
            Cue->GetComponentLocation().Equals(Find.ItemPosition(), .25) &&
            Tint.R > Tint.G && Tint.G > Tint.B);
        TestTrue(Label + TEXT(" shows the item's specific action and name"),
            Tower.Prompt() == FString(TEXT("F  ")) + Find.Action + Find.Title && Tower.Nearby == Find.Id);

        if (Find.bPuzzle)
        {
            const auto* Bowl = Index == 0 ? Tower.RationBowl.Get() : nullptr;
            if (Index == 0)
            {
                if (!TestTrue(TEXT("The chipped bowl has a registered physical 03 plate attached to its underside"),
                    Bowl && Bowl->IsRegistered() && Bowl->ComponentHasTag(TEXT("NorthChippedRationBowl")) &&
                    Bowl->GetAttachChildren().ContainsByPredicate([](const auto& Child)
                    {
                        return Child->IsRegistered() && Child->ComponentHasTag(TEXT("NorthRationRoomTag03")) &&
                            Child->GetRelativeLocation().Z < 0 && !Child->GetAttachChildren().IsEmpty();
                    }))) return false;
            }
            else if (!TestTrue(TEXT("The archive desk physically displays the returned originals and two false letters"),
                Fixture && Algo::CountIf(Fixture->GetAttachChildren(), [](const auto& Child)
                    { return Child->IsRegistered() && Child->ComponentHasTag(TEXT("NorthDeliveredFalseLetter")); }) == 2 &&
                Fixture->GetAttachChildren().ContainsByPredicate([](const auto& Child)
                    { return Child->IsRegistered() && Child->ComponentHasTag(TEXT("NorthReturnedOriginals")); }))) return false;

            TestFalse(Label + TEXT(" keeps the next battle locked until its evidence is solved"), Tower.State.CanEnterBattle(Index));
            Tap(EKeys::F);
            TestTrue(Label + TEXT(" requires the earlier record"), Tower.ActivePuzzle == INDEX_NONE && Tower.State.CacheBits == Before);
            if (Index == 0)
            {
                if (!ReadLedger(TEXT("RefugeLedger")) || !Approach(FindTarget(Find), true, Door)) return false;
            }
            else
            {
                if (!ReadLedger(TEXT("ArchiveBindingNote")) || !ReadLedger(TEXT("ArchiveNightIndex")) ||
                    !Approach(FindTarget(Find), true, Door)) return false;
                Tap(EKeys::F);
                TestTrue(TEXT("The optional shelf notes leave the sealed comparison locked"),
                    Tower.ActivePuzzle == INDEX_NONE && Tower.State.CacheBits == Before);
                if (!ReadLedger(TEXT("ArchiveTransferLedger")) || !Approach(FindTarget(Find), true, Door)) return false;
                Tap(EKeys::F);
                TestTrue(TEXT("One archive record is insufficient"), Tower.ActivePuzzle == INDEX_NONE && Tower.State.CacheBits == Before);
                if (!ReadLedger(TEXT("ArchiveSignatureRegister")) || !Approach(FindTarget(Find), true, Door)) return false;
            }
            const int32 First = Index == 0 ? 1 : 0;
            const int32 Second = Index == 0 ? 2 : 1;
            const TCHAR* FirstEvidence = Index == 0 ? TEXT("碗沿缺一角") : TEXT("退信袋");
            const TCHAR* SecondEvidence = Index == 0 ? TEXT("地下禁闭室") : TEXT("缺了一角的红蜡印");
            if (!TestTrue(TEXT("Both accepted answers match the evidence displayed to the player"),
                Correct(Index, 0, First) && Correct(Index, 1, Second) &&
                FString(Answer(Index, 0, First)).Contains(FirstEvidence) &&
                FString(Answer(Index, 1, Second)).Contains(SecondEvidence) &&
                (Index != 0 || (FString(Answer(Index, 0, First)).Contains(TEXT("旧碗")) &&
                    Tower.State.ClueText(EDuelNorthClue::RefugeLedger).Contains(FirstEvidence) &&
                    Tower.State.ClueText(EDuelNorthClue::RefugeLedger).Contains(TEXT("03 是地下禁闭室")))))) return false;
            Tap(EKeys::F);
            if (!TestTrue(Label + TEXT(" opens round one on F and owns world input"), Tower.ActivePuzzle == Index &&
                Tower.PuzzleRound == 0 && !Mode->CanExplore())) return false;
            if (!CycleTo((First + 1) % 3)) return false;
            Tap(EKeys::F);
            TestTrue(Label + TEXT(" gives feedback for a wrong answer without giving equipment"),
                Tower.ActivePuzzle == Index && Tower.PuzzleRound == 0 && !Tower.PuzzleHint().IsEmpty() &&
                Tower.State.CacheBits == Before);
            Tap(EKeys::Escape);
            if (!TestTrue(Label + TEXT(" exits without granting a reward"), Tower.ActivePuzzle == INDEX_NONE &&
                Mode->CanExplore() && Mode->MenuPage() == EDuelMenuPage::None && Tower.State.CacheBits == Before)) return false;
            Tap(EKeys::F);
            if (!TestTrue(TEXT("Re-entry resets the first question"), Tower.ActivePuzzle == Index && Tower.PuzzleRound == 0 &&
                Tower.PuzzleChoice == 0) || !CycleTo(First)) return false;
            Tap(EKeys::F);
            if (!TestTrue(TEXT("The first correct answer advances to round two without awarding gear"),
                Tower.ActivePuzzle == Index && Tower.PuzzleRound == 1 && Tower.State.CacheBits == Before) || !CycleTo(Second))
                return false;
            if (!CycleTo((Second + 1) % 3)) return false;
            Tap(EKeys::F);
            if (!TestTrue(TEXT("A wrong second answer keeps the reward and next battle locked"),
                Tower.ActivePuzzle == Index && Tower.PuzzleRound == 1 && !Tower.PuzzleHint().IsEmpty() &&
                Tower.State.CacheBits == Before && !Tower.State.CanEnterBattle(Index)) || !CycleTo(Second)) return false;
            if (Bowl)
            {
                if (!TestTrue(TEXT("The correct bowl is turned to expose its physical number during the second round"),
                    FMath::Abs(Bowl->GetRelativeRotation().Roll) > 90.)) return false;
                Tap(EKeys::Escape);
                if (!TestTrue(TEXT("Leaving the ration check sets the bowl back on its base"),
                    Tower.ActivePuzzle == INDEX_NONE && FMath::IsNearlyZero(Bowl->GetRelativeRotation().Roll, .25))) return false;
                Tap(EKeys::F);
                if (!CycleTo(First)) return false;
                Tap(EKeys::F);
                if (!CycleTo(Second)) return false;
            }
        }
        Tap(EKeys::F);
        TestTrue(Label + TEXT(" grants exactly its own reward and closes the puzzle"), Tower.State.CacheBits == (Before | Bit) &&
            Tower.ActivePuzzle == INDEX_NONE && Tower.State.KnowsSpell(Find.Spell) &&
            (Find.Weapon == EDuelWeapon::Empty || Tower.State.OwnsWeapon(Find.Weapon)) &&
            Tower.RecentTreasure() == Rewards(Find) && Tower.Notice == Find.Discovery);
        if (Find.bPuzzle && !FinishInvestigation(Find.Id, true)) return false;
        const uint32 Collected = Tower.State.CacheBits;
        TestTrue(Label + TEXT(" extinguishes the cue and takes the item while the fixture remains"),
            Cue && Cue->bHiddenInGame && Tower.Nearby != Find.Id && Fixture && Fixture->IsVisible() &&
            !ItemPieces.ContainsByPredicate([](const USceneComponent* Piece) { return Piece->IsVisible(); }));
        Tap(EKeys::F);
        TestEqual(Label + TEXT(" cannot be awarded again"), Tower.State.CacheBits, Collected);
        if (Collected & Bit) ++Acquired;
        AddInfo(FString::Printf(TEXT("Finds: %s room=%s door=%s capsule=%s"),
            *Find.Id.ToString(), *Room->Id.ToString(), *Door.ToString(), *Player.GetActorLocation().ToCompactString()));
        if (Find.bPuzzle)
        {
            if (!TestTrue(Label + TEXT(" enables the next battle and its floor doors"),
                Tower.State.WinBattle(Index) && Tower.State.PickupRelic(static_cast<EDuelNorthRelic>(Index)) &&
                Tower.State.OpenDoor(static_cast<EDuelNorthDoor>(Index)))) return false;
        }
        Tower.Respawn(*Mode);
    }
    TestTrue(TEXT("All twelve caches are obtainable through F with the recorded rewards"),
        Acquired == Finds.Num() && Tower.State.CacheBits == AllCacheBits());
    FString Reason;
    TestTrue(TEXT("The fully collected checkpoint remains valid"), Tower.State.Validate(Reason));
    return !HasAnyErrors();
}
#endif
