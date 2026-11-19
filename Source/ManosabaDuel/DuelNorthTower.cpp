#include "DuelNorthTower.h"
#include "DuelNorthLampworks.h"
#include "DuelNorthFinds.h"
#include "DuelNorthNarrative.h"
#include "DuelNorthArchitecture.h"
#include "DuelNorthInteractables.h"
#include "DuelNorthLighting.h"
#include "DuelGame.h"
#include "DuelArena.h"
#include "DuelGeometry.h"
#include "Algo/Reverse.h"
#include "Engine/StaticMesh.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

using namespace DuelNorthLayout;
namespace
{
    const FName OptionalReadingCues[] = {TEXT("RefugeLedger"), TEXT("SealPrototype"), TEXT("ReleaseOrder")};
    const FName TaskActionCues[] = {TEXT("CellDoor"), TEXT("DungeonGate"), TEXT("ReturnLever"),
        TEXT("ChapelDoor"), TEXT("MirrorGate"), TEXT("FinalStairDoor"), TEXT("FinalGate"), TEXT("TowerExit")};

    bool WithinFindReach(const ADuelMode& Mode, const DuelNorthFinds::FFind& Find)
    {
        const ADuelFighter* Player = Mode.GetPlayerFighter();
        const FVector At = Player->GetActorLocation();
        const FVector Item = Find.ItemPosition();
        if (FVector::DistSquared2D(At, Item) >= FMath::Square(DuelNorthFinds::Reach) ||
            FMath::Abs(At.Z - Find.Position.Z - 62) >= 100. ||
            FVector::DotProduct(At - Find.Position, Find.Front()) <= Find.Size.Y * .5) return false;
        FCollisionQueryParams Sight(SCENE_QUERY_STAT(NorthFindSight), false, Player);
        Sight.AddIgnoredActor(Mode.GetBossFighter());
        return !Mode.GetWorld()->LineTraceTestByChannel(At + FVector(0,0,12), Item, ECC_Visibility, Sight);
    }

    FVector ShortcutLeverBase(const FShortcutLever& Lever)
    {
        const FVector Side = FVector::CrossProduct(Lever.ApproachDirection, FVector::UpVector);
        return Lever.Position + Side * 112.f - Lever.ApproachDirection * 65.f;
    }

    FVector ShortcutLeverDirection(const FShortcutLever& Lever)
    {
        return (Lever.ApproachDirection * .55f + FVector(0, 0, .84f)).GetSafeNormal();
    }

    FVector TaskCueOrigin(FName Id, const FVector& At)
    {
        if (const auto* Lever = FindShortcutLever(Id))
            return ShortcutLeverBase(*Lever) + FVector(0, 0, 80) + ShortcutLeverDirection(*Lever) * 39.f;
        if (const auto* Lock = DuelNorthInteractables::Placements().FindByPredicate([Id](const auto& Prop)
            { return Prop.Id == Id && Prop.Kind == DuelNorthInteractables::EKind::Lock; }))
        {
            return Lock->Position + Lock->Rotation.RotateVector(FVector(0, -8.f, 119.f));
        }
        if (const auto* Gate = Portals().FindByPredicate([Id](const FPortal& Portal) { return Portal.Id == Id; }))
        {
            const auto* Approach = FindRoom(Gate->RoomA);
            if (Approach)
            {
                const double Side = FMath::Sign(FVector::DotProduct(Approach->Bounds.GetCenter() - Gate->Center, Gate->Normal));
                return Gate->Center + Gate->Normal * (58. * Side) + FVector(0, 0, 1.2f);
            }
        }
        return At + FVector(0, 0, 1.2f);
    }

    void BuildShortcutLevers(AActor* Owner)
    {
        UStaticMesh* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
        UStaticMesh* Cylinder=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
        if(!Cube||!Cylinder)return;
        auto* Iron=DuelGeometry::ColorMaterial(Owner,FLinearColor(.12f,.10f,.075f),false,.72f);
        auto* Grip=DuelGeometry::ColorMaterial(Owner,FLinearColor(.26f,.20f,.10f),false,.67f);
        for(const auto& Lever:ShortcutLevers())
        {
            const FVector Base=ShortcutLeverBase(Lever);
            const auto Part=[&](UStaticMesh* Mesh,FVector At,FVector Scale,FRotator Rotation,UMaterialInterface* Material)
            {
                auto* Piece=NewObject<UStaticMeshComponent>(Owner);Piece->SetupAttachment(Owner->GetRootComponent());
                Piece->SetStaticMesh(Mesh);Piece->SetRelativeLocation(At);Piece->SetRelativeRotation(Rotation);Piece->SetRelativeScale3D(Scale/100.f);
                Piece->SetMaterial(0,Material);Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);Piece->SetCanEverAffectNavigation(false);
                Piece->SetMobility(EComponentMobility::Static);Piece->SetCastShadow(false);
                Piece->ComponentTags.Add(TEXT("NorthShortcutLever"));Piece->ComponentTags.Add(Lever.Id);
                Owner->AddInstanceComponent(Piece);Piece->RegisterComponent();
            };
            // Narrow iron pedestal and tilted handle remain beside the walking centreline.
            // These cosmetic pieces have no collision, light, tick or hidden shadow.
            Part(Cube,Base+FVector(0,0,44),FVector(8,16,88),FRotator::ZeroRotator,Iron);
            const FVector Direction=ShortcutLeverDirection(Lever);
            const FRotator RodRotation=FRotationMatrix::MakeFromZ(Direction).Rotator();
            Part(Cylinder,Base+FVector(0,0,80)+Direction*16,FVector(3.5f,3.5f,32),RodRotation,Iron);
            Part(Cylinder,Base+FVector(0,0,80)+Direction*33,FVector(6.5f,6.5f,12),RodRotation,Grip);
        }
    }
    FDuelNorthObjective CurrentNorthObjective(const FDuelNorthProgress& State, EFloor Floor)
    {
        // A loaded checkpoint upstairs must never be sent back down for an optional lever.
        if (Floor == EFloor::Dungeon && State.HasRelic(EDuelNorthRelic::RustKey) &&
            !State.IsShortcutOpen(EDuelNorthShortcut::DungeonReturn) && !State.IsDoorOpen(EDuelNorthDoor::Chapel))
            return {EDuelNorthFloor::Dungeon, TEXT("ReturnLever"), TEXT("B1 回升通道 · 开启回升梯捷径，再上行前往档案室")};
        return State.NextObjective();
    }
}
ADuelNorthTower::ADuelNorthTower()
{
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("NorthTower"));
    RootComponent->SetMobility(EComponentMobility::Static);
}
void ADuelNorthTower::BeginPlay() { Super::BeginPlay();Build();BuildShortcutLevers(this); }
void ADuelNorthTower::SetGateOpen(FName Id,bool bOpen)
{
    if(auto* B=Gates.Find(Id)) { (*B)->SetCollisionEnabled(bOpen?ECollisionEnabled::NoCollision:ECollisionEnabled::QueryAndPhysics);(*B)->SetHiddenInGame(bOpen,true); }
    if(bOpen) OpenGates.Add(Id);else OpenGates.Remove(Id);
}
bool ADuelNorthTower::IsGateOpen(FName Id) const { return OpenGates.Contains(Id); }
void ADuelNorthTower::RefreshGates()
{
    const auto* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    const bool bFinalFight = ActiveFight == 2 && Mode && Mode->Outcome() == EDuelOutcome::Fighting;
    for(const auto& P:Portals())
    {
        bool Open=P.Gate==EGate::Open;
        if(P.Gate==EGate::FinalBattleExit) Open=!bFinalFight;
        if(P.Gate==EGate::DungeonWon) Open=State.HasWon(0);
        if(P.Gate==EGate::RustKey) Open=State.IsDoorOpen(EDuelNorthDoor::Chapel);
        if(P.Gate==EGate::MirrorKey) Open=State.IsDoorOpen(EDuelNorthDoor::FinalStair);
        if(P.Gate==EGate::AllTokens) Open=State.IsDoorOpen(EDuelNorthDoor::TowerExit);
        if(P.Gate==EGate::OpeningCell) Open=State.IsOpeningCellOpen();
        if(P.Gate==EGate::DungeonReturnShortcut) Open=State.IsShortcutOpen(EDuelNorthShortcut::DungeonReturn);
        if(P.Gate==EGate::ChapelLoopShortcut) Open=State.IsShortcutOpen(EDuelNorthShortcut::ChapelLoop);
        if(P.Gate==EGate::DungeonFog||P.Gate==EGate::MirrorFog||P.Gate==EGate::FinalFog)
        { const int I=P.Gate==EGate::DungeonFog?0:P.Gate==EGate::MirrorFog?1:2;Open=State.HasWon(I); }
        SetGateOpen(P.Id,Open);
    }
}
void ADuelNorthTower::Respawn(ADuelMode& M, const FVector* SafeLanding)
{
    bBossDefeatPending = false;
    RecentRelicUntil = 0;
    using DuelNorthLampworks::ETableView;
    DuelNorthLampworks::ShowEvidenceTable(EvidenceTableParts,
        State.HasBeacon(TEXT("BeaconSwitch")) ? ETableView::Open : ETableView::Idle);
    DuelNorthFinds::ShowRationBowl(RationBowl, (State.CacheBits & 1u) != 0);
    M.NorthWitchStartedAt = -1.0;
    TreasureUntil = 0;
    ActivePuzzle = INDEX_NONE;
    PuzzleFeedback.Reset();
    M.CloseCharacterView();M.bControls=false;M.bLoadout=false;M.bGraphics=false;M.bReadingArchive=false;M.MenuSelection=0;
    UGameplayStatics::SetGamePaused(this,false);M.Arena->ClearCombatEffects();
    M.Arena->StopScore(0.f);M.Arena->StopVoice(0.f);
    if(auto* PC=GetWorld()->GetFirstPlayerController())
    {PC->SetInputMode(FInputModeGameOnly());PC->SetShowMouseCursor(false);PC->SetViewTarget(M.PlayerFighter);}
    ActiveFight=INDEX_NONE;M.bGateOpen=false;M.Result=EDuelOutcome::Ready;
    M.Arena->SetCombatCamera(false);bMap=false;ClearTravelSelection();
    const auto* CP=Checkpoints().FindByPredicate([&](const FCheckpoint& C){return uint8(C.Floor)==State.Checkpoint;});
    const FVector At=SafeLanding ? *SafeLanding :
        (CP&&!State.UsesStartingCellCheckpoint()?CP->Position:SpawnSurface())+FVector(0,0,62);
    M.PlayerFighter->ResetFighter(true);M.PlayerFighter->RestAtCheckpoint();M.PlayerFighter->EndCombat();M.PlayerFighter->PrepareExploration();
    M.PlayerFighter->SetActorLocation(At,false,nullptr,ETeleportType::TeleportPhysics);
    if (State.UsesStartingCellCheckpoint())
    {
        M.PlayerFighter->ViewYaw = 35.f;
        M.PlayerFighter->SetActorRotation(FRotator(0, 35.f, 0));
    }
    else if (CP && (CP->Floor == EFloor::Dungeon || CP->Floor == EFloor::Mirror || CP->Floor == EFloor::Final))
    {
        // The west doorway would put the camera inside the entrance arch after arrival.
        M.PlayerFighter->ViewYaw = -45.f;
        M.PlayerFighter->SetActorRotation(FRotator(0, -45.f, 0));
    }
    M.BossFighter->EndCombat();M.BossFighter->SetActorHiddenInGame(true);M.BossFighter->SetActorEnableCollision(false);
    M.Arena->SetNorthOrigin(FVector(0,0,At.Z-62));RefreshGates();
    if(NarrativeActor) NarrativeActor->Restore(State,*M.PlayerFighter,*M.Arena);
    DuelNorthInteractables::Update(this,State,NAME_None,0);
}
void ADuelNorthTower::Update(ADuelMode& M,float Delta)
{
    if (bBossDefeatPending && M.BossFighter->IsBossDefeatComplete() && GetWorld()->GetTimeSeconds() >= BossDefeatVoiceEndsAt)
    {
        bBossDefeatPending = false;
        FinishBattle(M, true);
    }
    const FVector At=M.PlayerFighter->GetActorLocation();
    const FVector Sample=NarrativeActor?NarrativeActor->TowerSamplePosition(At):At;
    Location=Locate(Sample);
    if (M.Arena)
    {
        const bool bExploring = M.Result == EDuelOutcome::Ready || M.Result == EDuelOutcome::Victory;
        if (M.Result == EDuelOutcome::Title) M.Arena->SetNorthExplorationScore(TEXT("UnderStone"));
        else if (!bExploring || (NarrativeActor && NarrativeActor->WantsEndingMusic())) M.Arena->SetNorthExplorationScore(NAME_None);
        else if (Location.Floor != EFloor::Unknown)
        {
            const bool bLowerTower = Location.Floor == EFloor::Dungeon || Location.Floor == EFloor::Prison;
            M.Arena->SetNorthExplorationScore(Location.Floor == EFloor::Garden ? TEXT("MoonlitReturn") :
                bLowerTower ? TEXT("UnderStone") : TEXT("MarginNotes"));
        }
    }
    DuelNorthLighting::Update(this,Sample,Delta,&Location);
    DuelNorthInteractables::Update(this,State,Nearby,Delta,M.CanExplore());
    if(!Location.Room.IsNone()) Explored.Add(Location.Room);
    if(Location.Floor!=EFloor::Unknown) State.ExploreFloor(static_cast<EDuelNorthFloor>(Location.Floor));
    if(NarrativeActor && NarrativeActor->Update(State,*M.PlayerFighter,Delta,M.Result==EDuelOutcome::Ready||M.Result==EDuelOutcome::Victory)) Save();
    if (NarrativeActor && NarrativeActor->ExtractionEndingVisible())
    {
        FlightEpilogueSeconds += Delta;
        if (FlightEpilogueSeconds >= 16.f) FinishFlightEpilogue(M);
        return;
    }
    FlightEpilogueSeconds = 0.f;
    UpdateStoryCues(M);
    if (ActivePuzzle != INDEX_NONE)
    {
        if (auto* PC = GetWorld()->GetFirstPlayerController())
        {
            const bool Previous = PC->WasInputKeyJustPressed(EKeys::A) || PC->WasInputKeyJustPressed(EKeys::Left);
            const bool Next = PC->WasInputKeyJustPressed(EKeys::D) || PC->WasInputKeyJustPressed(EKeys::Right);
            const int32 Count = (ActivePuzzle == BeaconPuzzle && PuzzleRound == 1) ? 2 : 3;
            const int32 OldChoice = PuzzleChoice;
            PuzzleChoice = (PuzzleChoice + Count + int32(Next) - int32(Previous)) % Count;
            if (PuzzleChoice != OldChoice)
            {
                PuzzleFeedback.Reset();
                if (ActivePuzzle == BeaconPuzzle)
                {
                    using DuelNorthLampworks::ETableView;
                    DuelNorthLampworks::ShowEvidenceTable(EvidenceTableParts,
                        PuzzleRound == 0 ? ETableView::SelectBag : ETableView::SelectRing, PuzzleChoice);
                }
            }
        }
        return;
    }
    if (auto* PC = GetWorld()->GetFirstPlayerController(); PC && PC->WasInputKeyJustPressed(EKeys::M) &&
        (bMap || M.CanExplore()) && !(NarrativeActor && NarrativeActor->BlocksInput()))
    {
        bMap = !bMap;
        ClearTravelSelection();
        M.RefreshMenuPointer();
    }
    UpdateWayLight(M);
    if(At.Z < -2350) {Respawn(M);return;}
    Nearby=NAME_None;
    if (M.Result == EDuelOutcome::Fighting) UpdateMechanicHint(M);
    if(M.Result==EDuelOutcome::Fighting || (NarrativeActor && NarrativeActor->ExtractionInProgress())) return;
    if(ActiveFight!=INDEX_NONE && M.bGateOpen)
    {
        const FVector Origin=DuelNorthLayout::CourtOrigin(ActiveFight);
        const FVector Local=At-Origin;
        if(FMath::Abs(Local.Z-62)<110 && Local.X>=DuelCourt::GateCrossX && Local.X<0 && FMath::Abs(Local.Y)<280)
        {
            M.Arena->SetNorthOrigin(Origin);M.ResetEncounter(true);UpdateWayLight(M,false);
            M.Arena->Sound(EDuelSound::FogAdmission,M.PlayerFighter->GetActorLocation());RefreshGates();return;
        }
    }
    M.Arena->SetNorthOrigin(FVector(0,0,Location.SurfaceZ));
    const double InteractionRange=FMath::Square(220.);
    double Closest=InteractionRange;
    FCollisionQueryParams Sight(SCENE_QUERY_STAT(NorthInteraction),false,M.PlayerFighter);
    Sight.AddIgnoredActor(M.BossFighter);
    const auto Visible=[&](const FVector& Ground)
    {
        FHitResult Hit;
        return !GetWorld()->LineTraceSingleByChannel(Hit,At+FVector(0,0,12),Ground+FVector(0,0,70),ECC_Visibility,Sight) || Hit.GetActor()!=this;
    };
    for(const auto& P:Landmarks())
    {
        if (IsGateOpen(P.Id)) continue;
        if(ADuelNorthNarrative::IsRetiredEcho(P.Id,State))continue;
        if(P.Id==TEXT("CellDoor") && State.IsOpeningCellOpen())continue;
        if(P.Id==TEXT("OpeningDocket") && !State.CanReadClue(EDuelNorthClue::OpeningDocket))continue;
        if (const auto* Ledger = DuelNorthInteractables::Ledger(P.Id))
        {
            const auto Clue = static_cast<EDuelNorthClue>(Ledger->Index);
            const FVector Point = Ledger->ReadingPoint();
            const double Distance = FVector::DistSquared2D(At, Point);
            if (Location.Floor == P.Floor && State.CanReadClue(Clue) &&
                FMath::Abs(At.Z - Ledger->Position.Z - 62.) < 100. &&
                Distance < FMath::Min(Closest, FMath::Square(DuelNorthInteractables::LedgerReach)) &&
                FVector::DotProduct(At - Ledger->Position, Ledger->Front()) > Ledger->Size.Y &&
                !GetWorld()->LineTraceTestByChannel(At + FVector(0, 0, 12), Point, ECC_Visibility, Sight))
            {
                Closest = Distance;
                Nearby = P.Id;
            }
            continue;
        }
        if(const auto* Lever=FindShortcutLever(P.Id))
        {
            if(State.IsShortcutOpen(static_cast<EDuelNorthShortcut>(Lever->ProgressIndex)) ||
                !CanReachShortcutLever(*Lever,At,M.PlayerFighter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()))continue;
        }
        if(P.Id==TEXT("RustKey") && !State.RelicAvailable(EDuelNorthRelic::RustKey))continue;
        if(P.Id==TEXT("MirrorKey") && !State.RelicAvailable(EDuelNorthRelic::MirrorKey))continue;
        if(P.Id==TEXT("NameTag") && !State.RelicAvailable(EDuelNorthRelic::NameTag))continue;
        FVector Position;
        if (P.Id == TEXT("GardenCompanion"))
        {
            if (!NarrativeActor || !NarrativeActor->InteractionPosition(P.Id, State, Position)) continue;
        }
        else Position=LandmarkPosition(P);
        const double D=FVector::DistSquared2D(At,Position);
        const bool bEndingInteraction = P.Id == TEXT("GardenEdge") && Nearby == TEXT("GardenCompanion");
        if(FMath::Abs(At.Z-Position.Z-62)<100 && D<(bEndingInteraction ? InteractionRange : Closest) && Visible(Position))
        {Closest=D;Nearby=P.Id;}
    }
    for(const auto& C:Checkpoints())
    { const double D=FVector::DistSquared2D(At,C.Position);if(FMath::Abs(At.Z-C.Position.Z-62)<100 && D<Closest && Visible(C.Position)){Closest=D;Nearby=C.Id;} }
    const auto Finds = DuelNorthFinds::All();
    for (int32 Index = 0; Index < Finds.Num(); ++Index)
    {
        const auto& Find = Finds[Index];
        if (Find.Floor != static_cast<EDuelNorthFloor>(Location.Floor) || !State.CanVisitFloor(Find.Floor) ||
            (State.CacheBits & (1u << Index))) continue;
        const double D = FVector::DistSquared2D(At, Find.ItemPosition());
        if (D < Closest && WithinFindReach(M, Find))
        {
            Closest = D;
            Nearby = Find.Id;
        }
    }
}

void ADuelNorthTower::BuildStoryCues()
{
    const auto* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    const auto* Player = Mode ? Mode->GetPlayerFighter() : nullptr;
    const TConstArrayView<TObjectPtr<USceneComponent>> Weapons = Player ? MakeArrayView(Player->WeaponModels) :
        TConstArrayView<TObjectPtr<USceneComponent>>();
    DuelNorthFinds::BuildProps(this, GetRootComponent(), Weapons, StoryCues, RationBowl);
    DuelNorthLampworks::Build(this, GetRootComponent(), EvidenceTableParts);
    for (int32 I=0; I<4; ++I)
    {
        const FName Id(DuelNorthLampworks::Stations[I]);
        StoryCues.Add(Id,DuelNorthInteractables::BuildStoryCue(this,GetRootComponent(),Id,
            DuelNorthLampworks::Surface(I) + FVector(0, 0, I == 3 ? 18. : 7.)));
    }
    for (FName Id : OptionalReadingCues)
        if (!DuelNorthInteractables::Ledger(Id))
            StoryCues.Add(Id, DuelNorthInteractables::BuildStoryCue(this, GetRootComponent(), Id));
    for (uint8 Index = 0; Index < static_cast<uint8>(EDuelNorthDialogue::DungeonBossDefeat); ++Index)
    {
        const auto Dialogue = static_cast<EDuelNorthDialogue>(Index);
        const FName Id = ADuelNorthNarrative::MarkerFor(Dialogue);
        StoryCues.Add(Id, DuelNorthInteractables::BuildStoryCue(this, GetRootComponent(), Id));
    }
    for (FName Id : TaskActionCues)
        if (!StoryCues.Contains(Id)) StoryCues.Add(Id, DuelNorthInteractables::BuildStoryCue(this, GetRootComponent(), Id));
}

void ADuelNorthTower::UpdateStoryCues(const ADuelMode& Mode)
{
    const bool bCanShow = NarrativeActor && Mode.CanExplore();
    const FVector PlayerPosition = Mode.PlayerFighter->GetActorLocation();
    const FName Objective = bCanShow ? CurrentNorthObjective(State, Location.Floor).Target : NAME_None;
    const float Seconds = static_cast<float>(GetWorld()->GetTimeSeconds());
    for (const auto& Entry : StoryCues)
    {
        if (!Entry.Value) continue;
        const int32 Station = DuelNorthLampworks::Index(Entry.Key);
        if (Station != INDEX_NONE)
        {
            const bool Visible = bCanShow && Location.Floor == EFloor::Beacon && !State.HasBeacon(Entry.Key) &&
                FVector::DistSquared2D(PlayerPosition,DuelNorthLampworks::Surface(Station)) < FMath::Square(1200.);
            DuelNorthInteractables::UpdateStoryCue(Entry.Value,Visible,DuelNorthInteractables::ECueTone::Reading,.87f,Seconds);
            continue;
        }
        const int32 FindIndex = DuelNorthFinds::IndexOf(Entry.Key);
        if (FindIndex != INDEX_NONE)
        {
            const auto& Find = DuelNorthFinds::All()[FindIndex];
            const bool bAcquired = (State.CacheBits & (1u << FindIndex)) != 0;
            if (auto* Pickup = Entry.Value->GetAttachParent(); Pickup && Pickup->IsVisible() == bAcquired)
                Pickup->SetVisibility(!bAcquired, true);
            const bool bVisible = bCanShow && !bAcquired &&
                static_cast<EDuelNorthFloor>(Location.Floor) == Find.Floor && State.CanVisitFloor(Find.Floor) &&
                FVector::DistSquared2D(PlayerPosition, Find.ItemPosition()) < FMath::Square(1400.);
            DuelNorthInteractables::UpdateStoryCue(Entry.Value, bVisible,
                DuelNorthInteractables::ECueTone::Treasure, .86f, Seconds);
            continue;
        }
        const auto Dialogue = ADuelNorthNarrative::DialogueAt(Entry.Key);
        const bool bDialogue = Dialogue != EDuelNorthDialogue::Count;
        int32 Optional = INDEX_NONE;
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(OptionalReadingCues); ++Index)
            if (OptionalReadingCues[Index] == Entry.Key) { Optional = Index; break; }
        const bool bReading = bDialogue ?
            State.DialogueAvailable(Dialogue) && !State.HasDialogue(Dialogue) : Optional != INDEX_NONE &&
            State.CanReadClue(static_cast<EDuelNorthClue>(static_cast<int32>(EDuelNorthClue::RefugeLedger) + Optional));
        const bool bAction = !bReading && Entry.Key == Objective && MakeArrayView(TaskActionCues).Contains(Entry.Key) &&
            !IsGateOpen(Entry.Key);
        const auto* Landmark = FindLandmark(Entry.Key);
        bool bVisible = bCanShow && Landmark && (bReading || bAction);
        FVector Position;
        if (bVisible && Dialogue == EDuelNorthDialogue::Garden)
            bVisible = NarrativeActor->InteractionPosition(Entry.Key, State, Position);
        else if (bVisible) Position = LandmarkPosition(*Landmark);
        float Strength = .68f;
        if (bVisible)
        {
            const double DistanceSquared = FVector::DistSquared2D(Position, PlayerPosition);
            bVisible = DistanceSquared < FMath::Square(1500.f) &&
                FMath::Abs(Position.Z + 62.f - PlayerPosition.Z) < 190.f;
            if (bVisible)
            {
                const bool bAtFigure = bDialogue && Dialogue != EDuelNorthDialogue::DungeonDoor &&
                    Dialogue != EDuelNorthDialogue::MirrorDoor;
                const FVector At = bAction ? TaskCueOrigin(Entry.Key, Position) :
                    Optional == 1 ? Position + FVector(-8, -3, 91.) :
                    Optional == 2 ? Position + FVector(0, -16.5, 118.75) + FVector(0, 30, 62).GetSafeNormal() * 6.8 :
                    Position + FVector(bAtFigure ? -46.f : 0.f, 0, 1.2f);
                if (!Entry.Value->GetComponentLocation().Equals(At, .25f)) Entry.Value->SetWorldLocation(At);
                Strength = DistanceSquared < FMath::Square(245.) ? .86f : .68f;
            }
        }
        using DuelNorthInteractables::ECueTone;
        DuelNorthInteractables::UpdateStoryCue(Entry.Value, bVisible,
            bAction ? ECueTone::Pickup : ECueTone::Reading, Strength, Seconds);
    }
}

bool ADuelNorthTower::SkipPendingDialogue()
{
    if (ActivePuzzle != INDEX_NONE)
    {
        if (ActivePuzzle == BeaconPuzzle)
            DuelNorthLampworks::ShowEvidenceTable(EvidenceTableParts, DuelNorthLampworks::ETableView::Idle);
        if (ActivePuzzle == 0)
            DuelNorthFinds::ShowRationBowl(RationBowl, false);
        ActivePuzzle = INDEX_NONE;
        PuzzleFeedback.Reset();
        return true;
    }
    if (!NarrativeActor) return false;
    const auto Result = NarrativeActor->SkipDialogue(State);
    if (Result.bStateChanged) Save();
    return Result.bHandled;
}
const TCHAR* ADuelNorthTower::RescueObjective() const
{
    if (!State.HasWon(0))
    {
        const bool Known = State.HasInvestigation(0) || State.HasClue(EDuelNorthClue::CellVerdict);
        if (State.Companion() == EDuelCharacter::Ema) return Known ? TEXT("救回魔女化的艾玛") : TEXT("找到失踪的艾玛");
        return Known ? TEXT("救回魔女化的希罗") : TEXT("找到失踪的希罗");
    }
    if (!State.HasWon(1)) return State.HasInvestigation(1) ? TEXT("救回镜中魔女化的自己") : TEXT("查清同伴为什么会魔女化");
    if (!State.HasWon(2))
        return State.HasBeacon(TEXT("BeaconLetter")) ? TEXT("阻止小雪，救出牢里的女孩") : TEXT("找出是谁让女孩们魔女化");
    return TEXT("和同伴一起离开监狱");
}

FString ADuelNorthTower::Objective(bool bIncludeNotice) const
{
    const FStringView LiveNotice = bIncludeNotice ? VisibleNotice() : FStringView();
    FString Text = !LiveNotice.IsEmpty() ? FString(LiveNotice) : CurrentNorthObjective(State, Location.Floor).Text;
    if (Location.Room == TEXT("DawnGarden") && State.DialogueAvailable(EDuelNorthDialogue::Garden))
        Text += TEXT("\n也可以靠近同伴，按 F 交谈");
    return Text;
}

FStringView ADuelNorthTower::VisibleNotice() const
{
    return GetWorld()->GetTimeSeconds() < NoticeUntil ? FStringView(Notice) : FStringView();
}

FVector ADuelNorthTower::LandmarkPosition(const FLandmark& Landmark) const
{
    FVector Position = Landmark.Position;
    if (const auto* Prop = DuelNorthInteractables::Placements().FindByPredicate([&](const auto& Object)
        { return Object.Id == Landmark.Id && Object.Kind != DuelNorthInteractables::EKind::Lock; }))
    {
        Position = Prop->Approach();
    }
    if (NarrativeActor) NarrativeActor->InteractionPosition(Landmark.Id, State, Position);
    return Position;
}

bool ADuelNorthTower::BuildObjectiveRoute(const FLocation& From, EFloor PlayerFloor, FObjectiveRoute& Route) const
{
    Route = {};
    const FDuelNorthObjective Objective = CurrentNorthObjective(State,PlayerFloor);
    const FLandmark* Landmark = FindLandmark(Objective.Target);
    const int32 FindIndex = DuelNorthFinds::IndexOf(Objective.Target);
    if ((!Landmark && FindIndex == INDEX_NONE) || From.Room.IsNone()) return false;
    FVector Goal;
    if (Landmark) Goal = LandmarkPosition(*Landmark);
    else
    {
        const auto& Evidence = DuelNorthFinds::All()[FindIndex];
        Goal = Evidence.Position + Evidence.Front() * (Evidence.Size.Y * .5 + 72.);
    }
    const auto CoreForLanding = [](FName Room)
    {
        for (const FStairFlight& Flight : Stairs())
            if (Flight.FromLanding == Room || Flight.ToLanding == Room) return Flight.CoreRoom;
        return Room;
    };
    const FName Start = CoreForLanding(From.Room);
    const FName End = CoreForLanding(Locate(Goal + FVector(0, 0, 62)).Room);
    if (End.IsNone()) return false;
    Route.Goal = Goal;
    Route.GoalFloor = static_cast<EFloor>(Objective.Floor);
    Route.StartRoom = Start;
    if (Start == End) return true;

    // Each connected switchback has one core. Remember the actual door used
    // to enter each room so the guide and map share the unlocked route.
    TArray<FName> Queue{Start};
    TMap<FName, const FPortal*> Entrance;
    Entrance.Add(Start, nullptr);
    for (int32 Index = 0; Index < Queue.Num(); ++Index)
    {
        const FName Room = Queue[Index];
        for (const FPortal& Portal : Portals())
        {
            if (Portal.bWindow || (Portal.Gate != EGate::Open && !IsGateOpen(Portal.Id))) continue;
            const FName A = Portal.RoomA, B = Portal.RoomB;
            if (Room != A && Room != B) continue;
            const FName Next = Room == A ? B : A;
            if (Entrance.Contains(Next)) continue;
            Entrance.Add(Next, &Portal);
            if (Next == End)
            {
                for (FName At = End; At != Start;)
                {
                    const FPortal* Entered = Entrance.FindChecked(At);
                    Route.Doors.Add(Entered);
                    At = Entered->RoomA == At ? Entered->RoomB : Entered->RoomA;
                }
                Algo::Reverse(Route.Doors);
                return true;
            }
            Queue.Add(Next);
        }
    }
    return false;
}

bool ADuelNorthTower::MapObjectivePosition(FVector& Position) const
{
    FObjectiveRoute Route;
    if (!BuildObjectiveRoute(Location, Location.Floor, Route)) return false;
    if (Route.Doors.IsEmpty())
    {
        if (Location.Floor != Route.GoalFloor) return false;
        Position = Route.Goal;
        return true;
    }
    // The staircase is already marked on the map. Keep the threshold on the
    // current storey while the world-space guide shows the connecting flights.
    if (FMath::Abs(Route.Doors[0]->Center.Z - Location.SurfaceZ) > 200) return false;
    Position = Route.Doors[0]->Center;
    return true;
}
FString ADuelNorthTower::Prompt() const
{
    if (const auto* Mode = GetWorld()->GetAuthGameMode<ADuelMode>(); Mode && Mode->Outcome() == EDuelOutcome::Fighting && Mode->Arena)
        return Mode->Arena->CourtInteractionPrompt();
    if(const auto* Mode=GetWorld()->GetAuthGameMode<ADuelMode>();Mode && Mode->Outcome()==EDuelOutcome::Defeat)return TEXT("F  回到休整点");
    if (NarrativeActor && NarrativeActor->ExtractionEndingVisible()) return TEXT("F  返回标题画面");
    if(bMap || IsGateOpen(Nearby))return FString();
    const int32 Station = DuelNorthLampworks::Index(Nearby);
    if (Station != INDEX_NONE)
    {
        const TCHAR* Actions[] = {TEXT("F  查阅值班记录"), TEXT("F  查阅观察记录"),
            TEXT("F  翻开小雪的日记"), TEXT("F  查看物证桌")};
        if (Station == 3 && State.HasBeacon(Nearby)) return TEXT("F  重读拆出的求救信");
        return Actions[Station];
    }
    if (Nearby == TEXT("FinalGate") && !State.HasInvestigation(2)) return TEXT("F  检查锁住的终审雾门");
    const int32 FindIndex = DuelNorthFinds::IndexOf(Nearby);
    if (FindIndex != INDEX_NONE && !(State.CacheBits & (1u << FindIndex)))
    {
        const auto& Find = DuelNorthFinds::All()[FindIndex];
        return FString(TEXT("F  ")) + Find.Action + Find.Title;
    }
    if (const auto* Ledger = DuelNorthInteractables::Ledger(Nearby))
        return FString(Ledger->IsArchiveVolume() ? (Ledger->bOptional ? TEXT("F  阅读") : TEXT("F  从书架抽出")) : TEXT("F  查阅")) +
            Ledger->LedgerTitle;
    if(NarrativeActor){const FString Talk=NarrativeActor->Prompt(Nearby,State);if(!Talk.IsEmpty())return Talk;}
    if(Nearby.IsNone() || Nearby==TEXT("GardenCompanion"))return FString();
    if(Checkpoints().ContainsByPredicate([&](const FCheckpoint& C){return C.Id==Nearby;}))return TEXT("F  休整并保存");
    if(Nearby==TEXT("ReturnLever"))return TEXT("F  开启回升梯捷径");
    if(Nearby==TEXT("SeaEastLever"))return TEXT("F  开启海廊东侧捷径");
    if(Nearby==TEXT("CellDoor")) return State.HasClue(EDuelNorthClue::OpeningDocket) ? TEXT("F  抬起铁门插销") : TEXT("F  检查铁门");
    if(Nearby==TEXT("OpeningDocket")) return TEXT("F  阅读折过三次的纸条");
    if(Nearby==TEXT("SealPrototype")) return TEXT("F  查阅医务室观察单");
    if(Nearby==TEXT("ReleaseOrder")) return TEXT("F  查阅放行名单");
    if (Nearby == TEXT("NameTag")) return TEXT("F  拾取放行木牌");
    if (Nearby == TEXT("FinalSeal")) return TEXT("F  查看撕开的判决书");
    if(Nearby==TEXT("RustKey")||Nearby==TEXT("MirrorKey"))return TEXT("F  拾取钥匙");
    if (Nearby == TEXT("DungeonGate") && !State.HasInvestigation(0)) return TEXT("F  检查地下禁闭室的门闩");
    if (Nearby == TEXT("MirrorGate") && !State.HasInvestigation(1)) return TEXT("F  检查镜厅门锁");
    if(Nearby==TEXT("DungeonGate")||Nearby==TEXT("MirrorGate")||Nearby==TEXT("FinalGate"))return TEXT("F  推开雾门，步行进入战厅");
    if(Nearby==TEXT("ChapelDoor")||Nearby==TEXT("FinalStairDoor")||Nearby==TEXT("TowerExit"))return TEXT("F  开启门锁");
    return TEXT("F  阅读线索     M  地图");
}
FString ADuelNorthTower::RecentTreasure() const
{
    return TreasureUntil > GetWorld()->GetTimeSeconds() && RecentFindIndex != INDEX_NONE ?
        DuelNorthFinds::Rewards(DuelNorthFinds::All()[RecentFindIndex]) : FString();
}

bool ADuelNorthTower::InteractFind(ADuelMode& M)
{
    const int32 Index = DuelNorthFinds::IndexOf(Nearby);
    if (Index == INDEX_NONE) return false;
    const auto& Find = DuelNorthFinds::All()[Index];
    const uint32 Bit = 1u << Index;
    if ((State.CacheBits & Bit) || !State.CanVisitFloor(Find.Floor) ||
        static_cast<EDuelNorthFloor>(Location.Floor) != Find.Floor || !WithinFindReach(M, Find))
    {
        if (ActivePuzzle == 0) DuelNorthFinds::ShowRationBowl(RationBowl, (State.CacheBits & 1u) != 0);
        ActivePuzzle = INDEX_NONE;
        return true;
    }
    if (Find.bPuzzle && ActivePuzzle == INDEX_NONE)
    {
        const bool Ready = (Index == 0) ? State.HasClue(EDuelNorthClue::RefugeLedger) :
            State.HasClue(EDuelNorthClue::ArchiveTransferLedger) && State.HasClue(EDuelNorthClue::ArchiveSignatureRegister);
        if (!Ready)
        {
            Notice = Index == 0 ? TEXT("先去一楼北廊查配给底账，看看额外那份饭用什么碗。") :
                TEXT("先从两侧书架抽出《退信登记》和《送信记录》，再来核对桌上的信。");
            NoticeUntil = GetWorld()->GetTimeSeconds() + 7;
            return true;
        }
        M.PlayerFighter->GetCharacterMovement()->StopMovementImmediately();
        ActivePuzzle = Index;
        PuzzleRound = PuzzleChoice = 0;
        PuzzleFeedback.Reset();
        return true;
    }
    if (Find.bPuzzle)
    {
        if (!DuelNorthFinds::Correct(ActivePuzzle, PuzzleRound, PuzzleChoice))
        {
            PuzzleFeedback = Index == 0
                ? (PuzzleRound == 0 ? TEXT("底账记了单独送饭用的碗。留意碗沿。") : TEXT("把碗底的房号与底账里的房号对一遍。"))
                : (PuzzleRound == 0 ? TEXT("再看一次封存夹签。两封亲笔原件都在那里。") : TEXT("对照两封送达信的红蜡，看右边花瓣。"));
            return true;
        }
        if (PuzzleRound == 0)
        {
            ++PuzzleRound;
            PuzzleChoice = 0;
            PuzzleFeedback.Reset();
            if (Index == 0) DuelNorthFinds::ShowRationBowl(RationBowl, true);
            return true;
        }
        ActivePuzzle = INDEX_NONE;
    }
    State.CacheBits |= Bit;
    RecentFindIndex = Index;
    TreasureUntil = GetWorld()->GetTimeSeconds() + 6;
    Notice = Find.Discovery;
    NoticeUntil = TreasureUntil + 2;
    M.Arena->Sound(EDuelSound::RelicPickup, Find.ItemPosition());
    Save();
    if (NarrativeActor && Find.bPuzzle) NarrativeActor->DiscussEvidence(Find.Id, State, Notice, Find.Title);
    return true;
}

bool ADuelNorthTower::InteractBeacon(ADuelMode& M)
{
    const int32 I = DuelNorthLampworks::Index(Nearby);
    if (I == INDEX_NONE) return false;
    const auto* Site = FindLandmark(Nearby);
    const FVector Player = M.PlayerFighter->GetActorLocation();
    if (!Site || Location.Floor != EFloor::Beacon || !State.IsDoorOpen(EDuelNorthDoor::FinalStair) ||
        FVector::DistSquared2D(Player,Site->Position) >= FMath::Square(220.) ||
        FMath::Abs(Player.Z-Site->Position.Z-62.) >= 100.) return true;
    using DuelNorthLampworks::ETableView;
    if (I != 0 && !State.HasBeacon(TEXT("BeaconChart")))
    {
        Notice = TEXT("先到值班室查值班记录，找出哪些信件被扣了。");
    }
    else if (I == 3 && (State.BeaconBits & 7) != 7)
    {
        Notice = State.BeaconEvidenceText(Nearby);
    }
    else if (I == 3 && !State.HasBeacon(Nearby))
    {
        if (ActivePuzzle == INDEX_NONE)
        {
            M.PlayerFighter->GetCharacterMovement()->StopMovementImmediately();
            ActivePuzzle = BeaconPuzzle;
            PuzzleRound = PuzzleChoice = 0;
            PuzzleFeedback.Reset();
            DuelNorthLampworks::ShowEvidenceTable(EvidenceTableParts, ETableView::SelectBag, PuzzleChoice);
            return true;
        }
        if (PuzzleRound == 0)
        {
            if (!DuelNorthFinds::Correct(BeaconPuzzle, PuzzleRound, PuzzleChoice))
            {
                PuzzleFeedback = PuzzleChoice == 0 ? TEXT("这是发回原处的普通退件。日记写了封口用的东西。") :
                    TEXT("这是公文袋。找日记里那个有红蜡封口的袋子。");
                return true;
            }
            ++PuzzleRound;
            PuzzleChoice = 1;
            PuzzleFeedback = TEXT("红蜡封袋移到了手边。先在旁边的空白红蜡上试压。");
            DuelNorthLampworks::ShowEvidenceTable(EvidenceTableParts, ETableView::SelectRing, PuzzleChoice);
            return true;
        }
        if (!DuelNorthFinds::Correct(BeaconPuzzle, PuzzleRound, PuzzleChoice))
        {
            PuzzleFeedback = TEXT("压出来的三瓣都完整。假信上的右瓣有一角缺口。");
            DuelNorthLampworks::ShowEvidenceTable(EvidenceTableParts, ETableView::WrongRing);
            return true;
        }
        ActivePuzzle = INDEX_NONE;
        State.RecordBeacon(Nearby);
        DuelNorthLampworks::ShowEvidenceTable(EvidenceTableParts, ETableView::Open);
        Notice = State.BeaconEvidenceText(Nearby);
        M.Arena->Sound(EDuelSound::DoorLatch, Site->Position);
        RefreshGates();
        Save();
        if (NarrativeActor) NarrativeActor->DiscussEvidence(Nearby, State, Notice, TEXT("物证室里的求救信"));
    }
    else
    {
        const bool First = State.RecordBeacon(Nearby);
        Notice = State.BeaconEvidenceText(Nearby);
        if (First)
        {
            Save();
            if (NarrativeActor) NarrativeActor->DiscussEvidence(Nearby, State, Notice,
                I == 2 ? TEXT("小雪的日记") : I == 1 ? TEXT("看守的观察记录") : TEXT("值班记录"));
        }
    }
    NoticeUntil=GetWorld()->GetTimeSeconds()+9.;
    return true;
}

void ADuelNorthTower::FinishFlightEpilogue(ADuelMode& M)
{
    NarrativeActor->Confirm(NAME_None, State);
    FlightEpilogueSeconds = 0.f;
    Save();
    Nearby = NAME_None;
    M.Result = EDuelOutcome::Title;
    M.MenuSelection = M.CanContinue() ? 0 : 1;
    M.RefreshMenuPointer();
}

void ADuelNorthTower::Confirm(ADuelMode& M)
{
    if (NarrativeActor && NarrativeActor->ExtractionEndingVisible()) { FinishFlightEpilogue(M); return; }
    if(M.Result==EDuelOutcome::Defeat){Respawn(M);return;}
    if (bMap) { ConfirmTravel(M); return; }
    if(M.Result==EDuelOutcome::Fighting)return;
    if ((!NarrativeActor || !NarrativeActor->BlocksInput()) && (InteractFind(M) || InteractBeacon(M))) return;
    if(NarrativeActor)
    {
        const auto Action=NarrativeActor->Confirm(Nearby,State);
        if(Action.bHandled)
        {
            M.PlayerFighter->GetCharacterMovement()->StopMovementImmediately();
            if(Action.bStateChanged)Save();return;
        }
    }
    if(Nearby.IsNone() || IsGateOpen(Nearby) || Nearby==TEXT("GardenCompanion"))return;
    bool Changed=false;Notice.Empty();
    if (Nearby == TEXT("CellDoor"))
    {
        const bool bOpened = State.OpenStartingCell();
        Notice = bOpened ? TEXT("插销松开了。去北廊查配给底账，看看地牢那份饭往哪送。") : TEXT("铁门锁着。先看看墙边桌上的纸条。");
        NoticeUntil = GetWorld()->GetTimeSeconds() + 6;
        if (bOpened)
        {
            RefreshGates();
            M.Arena->Sound(EDuelSound::DoorLatch, M.PlayerFighter->GetActorLocation());
            Save();
        }
        return;
    }
    if (const auto* CP = Checkpoints().FindByPredicate([&](const FCheckpoint& C) { return C.Id == Nearby; }))
    {
        const auto FloorId = static_cast<EDuelNorthFloor>(CP->Floor);
        if (State.CanActivateCheckpoint(FloorId))
        {
            State.ActivateCheckpoint(FloorId);
            M.PlayerFighter->RestAtCheckpoint();
            M.Arena->Sound(EDuelSound::Checkpoint,M.PlayerFighter->GetActorLocation());
            Notice = FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")) ? TEXT("已休整。") : TEXT("已休整，进度已保存。");
            NoticeUntil = GetWorld()->GetTimeSeconds() + 4;
            Save();
        }
        return;
    }
    if(const auto* Lever=FindShortcutLever(Nearby))
    {
        const FVector At=M.PlayerFighter->GetActorLocation();
        FCollisionQueryParams Sight(SCENE_QUERY_STAT(NorthShortcutF),false,M.PlayerFighter);
        Sight.AddIgnoredActor(M.BossFighter);
        FHitResult Hit;
        const bool Clear=!GetWorld()->LineTraceSingleByChannel(Hit,At+FVector(0,0,12),Lever->Position+FVector(0,0,70),ECC_Visibility,Sight) || Hit.GetActor()!=this;
        // Recheck the real side, distance and visibility at F time, not only the cached prompt.
        if(!CanReachShortcutLever(*Lever,At,M.PlayerFighter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) || !Clear)
        {Notice=TEXT("需要靠近门内一侧的开关。");NoticeUntil=GetWorld()->GetTimeSeconds()+4;return;}
        Changed=State.OpenShortcut(static_cast<EDuelNorthShortcut>(Lever->ProgressIndex));
        if(Changed)
        {
            RefreshGates();
            M.Arena->Sound(EDuelSound::DoorLatch,Lever->Position+FVector(0,0,90));
            Notice=Lever->ProgressIndex==0?TEXT("回升梯捷径已开启。"):TEXT("海廊东侧捷径已开启。");
            NoticeUntil=GetWorld()->GetTimeSeconds()+4;
            Save();
        }
        else {Notice=TEXT("当前还没有满足开启条件。");NoticeUntil=GetWorld()->GetTimeSeconds()+4;}
        return;
    }
    const FName GatesByFight[]={TEXT("DungeonGate"),TEXT("MirrorGate"),TEXT("FinalGate")};
    for(int I=0;I<3;++I) if(Nearby==GatesByFight[I])
    {
        if (!State.CanEnterBattle(I))
        {
            Notice = State.NextObjective().Text;
            NoticeUntil = GetWorld()->GetTimeSeconds() + 7;
            return;
        }
        ActiveFight=I;M.bAppeal=I!=1;M.bYukiCourt=I==2;M.Arena->SetAppeal(M.bAppeal);M.Arena->SetYukiCourt(M.bYukiCourt);
        M.Arena->SetNorthOrigin(DuelNorthLayout::CourtOrigin(I));M.SetBoss(State.BossForBattle(I));
        M.BossFighter->BossEncounter = I;
        if (NarrativeActor) NarrativeActor->PreloadWitchPortrait(M.BossFighter->SelectedCharacter());
        // Admission never teleports the player. F opens the fog, walking across starts combat.
        M.BossFighter->SetActorEnableCollision(true);M.BossFighter->SetActorHiddenInGame(false);
        M.BossFighter->SetActorLocation(DuelNorthLayout::CourtOrigin(I)+FVector(700,0,M.BossFighter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2));
        M.BossFighter->GetCharacterMovement()->SetMovementMode(MOVE_Walking);M.BossFighter->EndCombat();
        SetGateOpen(Nearby,true);M.bGateOpen=true;return;
    }
    const FName RelicIds[] = {TEXT("RustKey"), TEXT("MirrorKey"), TEXT("NameTag")};
    static_assert(UE_ARRAY_COUNT(RelicIds) == static_cast<int32>(EDuelNorthRelic::Count));
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(RelicIds); ++Index)
    {
        if (Nearby != RelicIds[Index]) continue;
        const auto Relic = static_cast<EDuelNorthRelic>(Index);
        Changed = State.PickupRelic(Relic);
        if (Changed)
        {
            RecentRelic = Relic;
            RecentRelicUntil = GetWorld()->GetTimeSeconds() + 4.5;
            M.Arena->Sound(EDuelSound::RelicPickup,M.PlayerFighter->GetActorLocation());
        }
        break;
    }
    if(Nearby==TEXT("ChapelDoor")) Changed=State.OpenDoor(EDuelNorthDoor::Chapel);
    if(Nearby==TEXT("FinalStairDoor")) Changed=State.OpenDoor(EDuelNorthDoor::FinalStair);
    if(Nearby==TEXT("TowerExit")) Changed=State.OpenDoor(EDuelNorthDoor::TowerExit);
    const FName ClueIds[] = {TEXT("CellVerdict"), TEXT("WaterGateLog"), TEXT("ChapelJudgments"), TEXT("SeaMark"),
        TEXT("OwnVerdict"), TEXT("Signature"), TEXT("FinalSeal"), TEXT("RefugeLedger"), TEXT("SealPrototype"), TEXT("ReleaseOrder"), TEXT("OpeningDocket"),
        TEXT("ArchiveTransferLedger"), TEXT("ArchiveSignatureRegister"), TEXT("ArchiveBindingNote"), TEXT("ArchiveNightIndex")};
    static_assert(UE_ARRAY_COUNT(ClueIds) == static_cast<int32>(EDuelNorthClue::Count));
    for (int32 I = 0; I < UE_ARRAY_COUNT(ClueIds); ++I)
    {
        const auto Clue = static_cast<EDuelNorthClue>(I);
        if (Nearby != ClueIds[I] || !State.CanReadClue(Clue)) continue;
        const EDuelArt Unlock = Clue == EDuelNorthClue::RefugeLedger ? EDuelArt::LanternMote :
            Clue == EDuelNorthClue::SealPrototype ? EDuelArt::WhiteMantle : EDuelArt::Default;
        const bool bAlreadyKnown = State.KnowsSpell(Unlock);
        Changed = State.ReadClue(Clue);
        DuelNorthInteractables::PullArchiveVolume(this, Nearby);
        Notice = State.ClueText(Clue);
        if (Changed && NarrativeActor) NarrativeActor->DiscussEvidence(Nearby, State, Notice, FDuelNorthProgress::ClueTitle(Clue));
        if (Changed && Unlock != EDuelArt::Default && !bAlreadyKnown && State.KnowsSpell(Unlock))
        {
            Notice += Clue == EDuelNorthClue::RefugeLedger ? TEXT("\n习得魔法：引路灯灵") : TEXT("\n习得魔法：白幕护体");
        }
        if (Changed && Clue == EDuelNorthClue::ReleaseOrder)
        {
            M.PlayerFighter->Health = DuelCourt::PlayerMaxHealth;
            M.PlayerFighter->Focus = M.PlayerFighter->FocusMax();
            Notice += TEXT("\n取用看守的备用药剂，回满生命和魔力。");
        }
        break;
    }
    if(Changed)
    {
        const bool Unlocked=Nearby==TEXT("ChapelDoor")||Nearby==TEXT("FinalStairDoor")||Nearby==TEXT("TowerExit");
        if(Unlocked)M.Arena->Sound(EDuelSound::DoorLatch,M.PlayerFighter->GetActorLocation());
    }
    if(Notice.IsEmpty())Notice=Changed?CurrentNorthObjective(State,Location.Floor).Text:TEXT("当前还没有满足开启条件。");
    NoticeUntil=GetWorld()->GetTimeSeconds()+9;RefreshGates();if(Changed)Save();
}
const TCHAR* ADuelNorthTower::RecentRelicName() const
{
    return RecentRelicUntil > GetWorld()->GetTimeSeconds() ? FDuelNorthProgress::RelicName(RecentRelic) : nullptr;
}
void ADuelNorthTower::FinishBattle(ADuelMode& M,bool bWon)
{
    if (bBossDefeatPending) return;
    if (bWon && !M.BossFighter->IsAlive() && !M.BossFighter->IsBossDefeatComplete())
    {
        bBossDefeatPending = true;
        BossDefeatVoiceEndsAt = M.Arena->TryBossLine(M.BossFighter->SelectedCharacter(), EDuelVoice::Loss, M.BossFighter->GetActorLocation())
            ? M.Arena->CombatVoiceEndTime() : 0.0;
        M.PlayerFighter->EndCombat();
        M.BossFighter->EndCombat();
        M.BossFighter->SetActorEnableCollision(false);
        M.Arena->ClearCombatEffects();
        M.Arena->StopScore(.55f);
        return;
    }
    M.Result=bWon?EDuelOutcome::Victory:EDuelOutcome::Defeat;
    M.Arena->SetCombatCamera(false);M.bGateOpen=false;bMap=false;
    M.CompletedDuelSeconds=FMath::Max(1,FMath::RoundToInt(GetWorld()->GetTimeSeconds()-M.DuelStartedAt));
    M.PlayerFighter->EndCombat();
    M.BossFighter->EndCombat();
    if (!bWon)
    {
        M.Arena->StopScore(.12f);
        M.Arena->StopVoice(0.f);
    }
    M.Arena->ClearCombatEffects();
    const FVector At = M.PlayerFighter->GetActorLocation();
    if (bWon)
    {
        M.Arena->Sound(EDuelSound::Victory, At);
    }
    else if (!M.Arena->PlayVoice(M.PlayerFighter->SelectedCharacter(), EDuelVoice::Defeat, false, At))
    {
        M.Arena->Sound(EDuelSound::Defeat, At);
    }
    if (bWon)
    {
        const int32 Battle = ActiveFight;
        const bool bNewVictory = State.WinBattle(Battle);
        ActiveFight = INDEX_NONE;
        M.BossFighter->SetActorEnableCollision(false);
        M.BossFighter->SetActorHiddenInGame(true);
        Save();
        RefreshGates();
        M.PlayerFighter->PrepareExploration();
        if (bNewVictory && NarrativeActor) NarrativeActor->PlayBossDefeat(Battle, State);
    }
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthShortcutPolicyTest, "ManosabaDuel.NorthTower.ShortcutPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDuelNorthShortcutPolicyTest::RunTest(const FString& Parameters)
{
    FDuelNorthProgress State; State.Reset(EDuelCharacter::Ema);
    TestFalse(TEXT("No return shortcut before rescuing first boss"),State.OpenShortcut(EDuelNorthShortcut::DungeonReturn));
    State.ReadClue(EDuelNorthClue::RefugeLedger);
    State.CacheBits |= 1u;
    TestTrue(TEXT("First battle won"),State.WinBattle(0));
    TestTrue(TEXT("Rust key is picked independently"),State.PickupRelic(EDuelNorthRelic::RustKey));
    TestEqual(TEXT("Rust key points downstairs player to return lever"),CurrentNorthObjective(State,EFloor::Dungeon).Target,FName(TEXT("ReturnLever")));
    TestTrue(TEXT("Existing upstairs checkpoint is not sent downstairs"),CurrentNorthObjective(State,EFloor::Prison).Target!=FName(TEXT("ReturnLever")));
    TestTrue(TEXT("First legitimate return unlock persists"),State.OpenShortcut(EDuelNorthShortcut::DungeonReturn));
    TestFalse(TEXT("Repeated return unlock has no state change or cue"),State.OpenShortcut(EDuelNorthShortcut::DungeonReturn));
    TestTrue(TEXT("Opened return shortcut resumes ordinary objective"),CurrentNorthObjective(State,EFloor::Dungeon).Target==State.NextObjective().Target);
    TestFalse(TEXT("Chapel loop cannot open before chapel access"),State.OpenShortcut(EDuelNorthShortcut::ChapelLoop));
    TestTrue(TEXT("Rust key opens chapel independently"),State.OpenDoor(EDuelNorthDoor::Chapel));
    TestTrue(TEXT("Chapel loop can open after chapel access"),State.OpenShortcut(EDuelNorthShortcut::ChapelLoop));
    TestFalse(TEXT("Repeated chapel unlock has no state change or cue"),State.OpenShortcut(EDuelNorthShortcut::ChapelLoop));
    TestEqual(TEXT("Both shortcut bits recorded"),State.Shortcuts,uint8(3));
    FString Error;TestTrue(TEXT("Shortcut state retains save invariants"),State.Validate(Error));
    return true;
}
#endif
