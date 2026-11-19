#include "DuelNorthNarrative.h"

#include "DuelArena.h"
#include "DuelGame.h"
#include "DuelGeometry.h"
#include "DuelNorthExtraction.h"
#include "DuelNorthLayout.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "Components/MeshComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "DuelNorthInvestigationQA.h"
#include "Misc/ScopeExit.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#endif

namespace
{
    const TCHAR* SpeakerName(EDuelCharacter Character)
    {
        if (Character == EDuelCharacter::Yuki) return TEXT("月代雪");
        return Character == EDuelCharacter::Ema ? TEXT("艾玛") : TEXT("希罗");
    }

    const TCHAR* SpeakerKey(EDuelCharacter Character)
    {
        if (Character == EDuelCharacter::Yuki) return TEXT("Yuki");
        return Character == EDuelCharacter::Ema ? TEXT("Ema") : TEXT("Hiro");
    }

    const EDuelNorthDialogue EchoDialogues[] = {EDuelNorthDialogue::NameWall,
        EDuelNorthDialogue::LongTable, EDuelNorthDialogue::ChapelBench, EDuelNorthDialogue::MirrorDoor};

    constexpr float EchoFadeDuration = 1.4f;

    bool IsEchoDialogue(EDuelNorthDialogue Dialogue)
    {
        for (const auto Echo : EchoDialogues) if (Echo == Dialogue) return true;
        return false;
    }

    #include "DuelNorthInscriptionGlyphs.inl"

    FString InscriptionText(FName Marker, EDuelCharacter Speaker)
    {
        const FString Prefix = Speaker == EDuelCharacter::Ema ? TEXT("希罗酱，") : TEXT("");
        if(Marker==TEXT("NameWall"))return Prefix + TEXT("你终于醒了。");
        if(Marker==TEXT("LongTable"))return Prefix + TEXT("楼下那扇门又响了。");
        if(Marker==TEXT("SeaBench"))return Prefix + TEXT("坐这里能看见海。");
        if(Marker==TEXT("MirrorGate"))return Prefix + TEXT("看看镜子，再看看我。");
        return TEXT("");
    }

    const FNorthInscriptionGlyph* InscriptionGlyph(TCHAR Character)
    {
        for(const auto& Glyph:NorthInscriptionGlyphs)if(Glyph.Codepoint==static_cast<uint32>(Character))return &Glyph;
        return nullptr;
    }

    UProceduralMeshComponent* BuildInscription(AActor* Owner,FName Marker,const FVector& Surface,EDuelCharacter Speaker)
    {
        const FString Text=InscriptionText(Marker, Speaker);
        if(Text.IsEmpty())return nullptr;
        constexpr float Scale=20.f/48.f;
        constexpr int32 Wrap=8;
        const int32 Rows=FMath::DivideAndRoundUp(Text.Len(),Wrap);
        const bool bNorthFacing=Marker==TEXT("NameWall")||Marker==TEXT("LongTable");
        const FRotator Facing(0,bNorthFacing?90.f:0.f,0);
        const FVector Right=Facing.RotateVector(FVector::RightVector),Forward=Facing.RotateVector(FVector::ForwardVector);
        TArray<FVector> V,N;TArray<int32> Indices;TArray<FVector2D> UV;TArray<FProcMeshTangent> Tangents;
        for(int32 Letter=0;Letter<Text.Len();++Letter)
        {
            const auto* Glyph=InscriptionGlyph(Text[Letter]);
            if(!Glyph){UE_LOG(LogTemp,Error,TEXT("North inscription %s lacks glyph U+%04X"),*Marker.ToString(),uint32(Text[Letter]));return nullptr;}
            const int32 Row=Letter/Wrap,Column=Letter%Wrap;
            const int32 Count=FMath::Min(Wrap,Text.Len()-Row*Wrap);
            const float Left=(Column-Count*.5f)*20.f;
            const float Top=(Rows*.5f-Row)*26.f;
            for(int32 R=0;R<Glyph->Count;++R)
            {
                const auto& Stroke=NorthInscriptionRects[Glyph->Start+R];
                const float X0=Left+(Stroke.X-12)*Scale,X1=X0+Stroke.W*Scale;
                const float Y0=Top-(Stroke.Y-12)*Scale,Y1=Y0-Stroke.H*Scale;
                const int32 Start=V.Num();
                V.Append({Right*X0+Forward*Y0,Right*X1+Forward*Y0,Right*X1+Forward*Y1,Right*X0+Forward*Y1});
                Indices.Append({Start,Start+1,Start+2,Start,Start+2,Start+3});
                for(int32 K=0;K<4;++K){N.Add(FVector::UpVector);UV.Add(FVector2D::ZeroVector);Tangents.Emplace(Right,false);}
            }
        }
        auto* Mesh=NewObject<UProceduralMeshComponent>(Owner);
        Mesh->SetupAttachment(Owner->GetRootComponent());
        Mesh->SetRelativeLocation(Surface+FVector(0,0,.9f));
        Mesh->SetMobility(EComponentMobility::Movable);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetCanEverAffectNavigation(false);
        Mesh->SetCastShadow(false);Mesh->bCastHiddenShadow=false;
        Mesh->ComponentTags.Add(TEXT("NorthGroundInscription"));Mesh->ComponentTags.Add(Marker);
        Mesh->CreateMeshSection_LinearColor(0,V,Indices,N,UV,TArray<FLinearColor>(),Tangents,false);
        Mesh->SetMaterial(0,DuelGeometry::ColorMaterial(Owner,FLinearColor(2.2f,1.1f,.30f,1.f),true));
        Owner->AddInstanceComponent(Mesh);Mesh->RegisterComponent();Mesh->SetVisibility(false);
        return Mesh;
    }

    void SetInscriptionOpacity(UProceduralMeshComponent* Mesh,float Opacity)
    {
        if(!Mesh)return;
        if(auto* Material=Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)))
        {
            const FLinearColor Color(2.2f,1.1f,.30f,FMath::Clamp(Opacity,0.f,1.f));
            if(!Material->K2_GetVectorParameterValue(TEXT("Color")).Equals(Color,.00001f))Material->SetVectorParameterValue(TEXT("Color"),Color);
        }
    }

    void StopFigure(ADuelFighter* Figure)
    {
        if (!IsValid(Figure)) return;
        Figure->ConsumeMovementInputVector();
        Figure->GetCharacterMovement()->StopMovementImmediately();
    }
}

bool FDuelNorthDialoguePlayback::BeginEvidence(FName Id, const FDuelNorthProgress& State)
{
    if (Active() || Id.IsNone()) return false;
    Script = State.InvestigationScript(Id);
    if (Script.Lines.IsEmpty()) return false;
    Evidence = Id;
    PlayerAtStart = State.Player();
    Line = 0;
    return true;
}

bool FDuelNorthDialoguePlayback::Begin(EDuelNorthDialogue Dialogue, const FDuelNorthProgress& State)
{
    if (Active() || !State.DialogueAvailable(Dialogue)) return false;
    Script = State.DialogueScript(Dialogue);
    if (Script.Lines.IsEmpty()) return false;
    Current = Dialogue;
    PlayerAtStart = State.Player();
    Line = 0;
    return true;
}

FDuelNorthNarrativeResult FDuelNorthDialoguePlayback::Advance(FDuelNorthProgress& State, bool bSkip)
{
    if (!Active()) return {};
    FDuelNorthNarrativeResult Result;
    Result.bHandled = true;
    if (PlayerAtStart != State.Player() || (Evidence.IsNone() && !State.DialogueAvailable(Current)))
    {
        Cancel();
        return Result;
    }
    if (!bSkip && Line + 1 < Script.Lines.Num())
    {
        ++Line;
        return Result;
    }
    Result.bStateChanged = Evidence.IsNone() && State.CompleteDialogue(Current);
    Cancel();
    return Result;
}

void FDuelNorthDialoguePlayback::Cancel()
{
    Current = EDuelNorthDialogue::Count;
    Evidence = NAME_None;
    Script.Lines.Reset();
    Line = 0;
}

bool FDuelNorthDialoguePlayback::IsSpeaking(EDuelCharacter Character) const
{
    return Active() && Script.Lines.IsValidIndex(Line) && !Script.Lines[Line].bDefeatedBoss &&
        Script.Lines[Line].Speaker == Character;
}

FDuelNorthSubtitle FDuelNorthDialoguePlayback::Subtitle() const
{
    if (!Active() || !Script.Lines.IsValidIndex(Line)) return {};
    const auto& CurrentLine = Script.Lines[Line];
    FString Speaker = SpeakerName(CurrentLine.Speaker);
    const bool bWitch = CurrentLine.bDefeatedBoss && CurrentLine.Speaker != EDuelCharacter::Yuki;
    if (bWitch)
    {
        Speaker += Current == EDuelNorthDialogue::MirrorBossDefeat ? TEXT("（镜中魔女）") : TEXT("（魔女化）");
    }
    return {Speaker, CurrentLine.Text, Line + 1, Script.Lines.Num(),
        CurrentLine.Speaker, Script.Presence, CurrentLine.Expression, bWitch};
}

FString FDuelNorthDialoguePlayback::VoiceKey() const
{
    if (!Active() || !Script.Lines.IsValidIndex(Line)) return {};
    const bool bFloorRevision = Evidence == TEXT("FindCounterseal");
    const bool bRevised = Current == EDuelNorthDialogue::FinalWarning || Current == EDuelNorthDialogue::FinalBossDefeat ||
        Evidence == TEXT("FindCounterseal") || Evidence == TEXT("OwnVerdict") || Evidence == TEXT("LensWorkshop") ||
        Evidence == TEXT("BeaconLetter") || Evidence == TEXT("BeaconSwitch");
    return FString(bFloorRevision ? TEXT("Revision346/") : bRevised ? TEXT("Revision344/") : TEXT("")) +
        FString::Printf(TEXT("%s/%s/%02d_%s.wav"), SpeakerKey(PlayerAtStart),
        *(Evidence.IsNone() ? ADuelNorthNarrative::MarkerFor(Current) : Evidence).ToString(), Line + 1, SpeakerKey(Script.Lines[Line].Speaker));
}

ADuelNorthNarrative::ADuelNorthNarrative()
{
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("NorthNarrative"));
    PrimaryActorTick.bCanEverTick = false;
}

FName ADuelNorthNarrative::MarkerFor(EDuelNorthDialogue Dialogue)
{
    static const FName Names[] = {TEXT("NameWall"), TEXT("LongTable"), TEXT("DungeonGate"),
        TEXT("RescuedCompanion"), TEXT("SeaBench"), TEXT("MirrorGate"), TEXT("GardenCompanion"),
        TEXT("MirrorExitCompanion"), TEXT("FinalExitCompanion"), TEXT("CellEcho"),
        TEXT("WaterGateEcho"), TEXT("ArchiveConversation"), TEXT("FinalWarning"),
        TEXT("DungeonBossDefeat"), TEXT("MirrorBossDefeat"), TEXT("FinalBossDefeat")};
    static_assert(UE_ARRAY_COUNT(Names) == static_cast<int32>(EDuelNorthDialogue::Count));
    const int32 Index = static_cast<int32>(Dialogue);
    return Index >= 0 && Index < UE_ARRAY_COUNT(Names) ? Names[Index] : NAME_None;
}

EDuelNorthDialogue ADuelNorthNarrative::DialogueAt(FName Marker)
{
    for (int32 Index = 0; Index < static_cast<int32>(EDuelNorthDialogue::Count); ++Index)
    {
        const auto Dialogue = static_cast<EDuelNorthDialogue>(Index);
        if (Marker == MarkerFor(Dialogue)) return Dialogue;
    }
    return EDuelNorthDialogue::Count;
}

bool ADuelNorthNarrative::IsRetiredEcho(FName Marker, const FDuelNorthProgress& State)
{
    const auto Dialogue = DialogueAt(Marker);
    if (Dialogue == EDuelNorthDialogue::Garden ||
        (Dialogue >= EDuelNorthDialogue::CellEcho && Dialogue <= EDuelNorthDialogue::FinalWarning))
        return !State.DialogueAvailable(Dialogue);
    const bool bCompanionMeeting = Dialogue == EDuelNorthDialogue::RescuedCompanion || Dialogue == EDuelNorthDialogue::MirrorExit ||
        Dialogue == EDuelNorthDialogue::FinalExit;
    if (bCompanionMeeting) return !State.DialogueAvailable(Dialogue) || State.HasDialogue(Dialogue);
    return Dialogue != EDuelNorthDialogue::MirrorDoor && IsEchoDialogue(Dialogue) && State.HasDialogue(Dialogue);
}

ADuelFighter* ADuelNorthNarrative::SpawnFigure(EDuelCharacter Character, const FVector& Surface)
{
    if (!PlayerRef || !ArenaRef) return nullptr;
    FActorSpawnParameters Params;
    Params.Owner = this;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ADuelFighter* Figure = GetWorld()->SpawnActor<ADuelFighter>(ADuelFighter::StaticClass(),
        Surface + FVector(0, 0, 62), FRotator::ZeroRotator, Params);
    if (!Figure) return nullptr;
    Figure->SetActorHiddenInGame(true);
    Figure->SetEncounter(PlayerRef, ArenaRef);
    if (!Figure->SelectCharacter(Character) || !Figure->ShowCharacterForm(false))
    {
        UE_LOG(LogTemp, Warning, TEXT("North narrative standing model unavailable for %d"), static_cast<int32>(Character));
        Figure->Destroy();
        return nullptr;
    }
    Figure->SetCharacterPreview(true);
    Figure->EndCombat();
    Figure->PrepareExploration();
    const float HalfHeight = Figure->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    Figure->SetActorLocation(Surface + FVector(0, 0, HalfHeight + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
    Figure->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
    Figure->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    UCharacterMovementComponent* Motion = Figure->GetCharacterMovement();
    Motion->bRunPhysicsWithNoController = true;
    Motion->bCanWalkOffLedges = false;
    // Authored stair risers are at most 18 cm; a 44 cm bench is an obstacle.
    Motion->MaxStepHeight = 22.f;
    Motion->MaxWalkSpeed = 455.f;
    Motion->MaxAcceleration = 1600.f;
    Motion->BrakingDecelerationWalking = 1800.f;
    TInlineComponentArray<UCameraComponent*> Cameras;
    Figure->GetComponents(Cameras);
    for (UCameraComponent* Camera : Cameras) Camera->Deactivate();
    SetFigureVisible(Figure, false);
    return Figure;
}

void ADuelNorthNarrative::SetFigureVisible(ADuelFighter* Figure, bool bVisible)
{
    if (!IsValid(Figure)) return;
    Figure->SetActorHiddenInGame(!bVisible);
    Figure->SetActorEnableCollision(bVisible);
    Figure->SetActorTickEnabled(bVisible);
    Figure->GetCharacterMovement()->SetComponentTickEnabled(bVisible);
    if (!bVisible) StopFigure(Figure);
}

void ADuelNorthNarrative::RebuildFigures(const FDuelNorthProgress& State)
{
    ClearDialogueMedia();
    ClearEvidenceDocument();
    Playback.Cancel();
    EchoFadeElapsed.Reset();
    if (IsValid(RealCompanion)) RealCompanion->Destroy();
    RealCompanion = nullptr;
    for(auto& Entry:Inscriptions)if(IsValid(Entry.Value))Entry.Value->DestroyComponent();
    Inscriptions.Reset();
    for (const EDuelNorthDialogue Dialogue : EchoDialogues)
    {
        const FName Marker = MarkerFor(Dialogue);
        if (const auto* At = DuelNorthLayout::FindLandmark(Marker))
        {
            Inscriptions.Add(Marker, BuildInscription(this, Marker, At->Position, State.Companion()));
        }
    }
    if (const auto* At = DuelNorthLayout::FindLandmark(TEXT("RescuedCompanion")))
    {
        RealCompanion = SpawnFigure(State.Companion(), At->Position);
    }
    CompanionCharacter = State.Companion();
    RealStage = ECompanionStage::Absent;
    bWasFollowing = false;
    ResetFollow();
}

void ADuelNorthNarrative::Restore(const FDuelNorthProgress& State, ADuelFighter& Player, ADuelArena& Arena)
{
    if (Extraction) Extraction->Reset(false);
    PlayerRef = &Player;
    ArenaRef = &Arena;
    Arena.SetNorthEndingMusic(false);
    ClearDialogueMedia();
    ClearEvidenceDocument();
    Playback.Cancel();
    EchoFadeElapsed.Reset();
    bEndingVisible = false;
    bExplorationEnabled = true;
    PreloadThoughtPortrait(State.Player());
    if (CompanionCharacter != State.Companion() || !IsValid(RealCompanion)) RebuildFigures(State);
    ResetFollow();
    bWasFollowing = false;
    SyncFigures(State, Player, true);
}

void ADuelNorthNarrative::SyncFigures(const FDuelNorthProgress& State, ADuelFighter& Player, bool bRestore)
{
    if (CompanionCharacter != State.Companion()) RebuildFigures(State);
    const FVector PlayerAt = Player.GetActorLocation();
    for(auto& Entry:Inscriptions)
    {
        if(!IsValid(Entry.Value))continue;
        const auto Dialogue=DialogueAt(Entry.Key);
        const auto* At=DuelNorthLayout::FindLandmark(Entry.Key);
        const bool bNear=At&&FMath::Abs(PlayerAt.Z-At->Position.Z-62.f)<250.f&&
            FVector::DistSquared2D(PlayerAt,At->Position)<FMath::Square(2100.f);
        const float* Fade=EchoFadeElapsed.Find(Entry.Key);
        const float Opacity=Fade?1.f-FMath::SmoothStep(0.f,EchoFadeDuration,*Fade):State.HasDialogue(Dialogue)?0.f:1.f;
        SetInscriptionOpacity(Entry.Value,Opacity);
        Entry.Value->SetVisibility(bExplorationEnabled&&bNear&&State.DialogueAvailable(Dialogue)&&
            (!State.HasDialogue(Dialogue)||Fade!=nullptr));
    }
    if (!IsValid(RealCompanion)) return;
    ECompanionStage Stage = ECompanionStage::Absent;
    EDuelNorthDialogue Meeting = EDuelNorthDialogue::Count;
    if (State.HasWon(2))
    {
        Stage = ECompanionStage::FinalExit;
        Meeting = EDuelNorthDialogue::FinalExit;
    }
    else if (State.HasWon(1))
    {
        Stage = ECompanionStage::MirrorExit;
        Meeting = EDuelNorthDialogue::MirrorExit;
    }
    else if (State.HasWon(0))
    {
        Stage = ECompanionStage::Rescue;
        Meeting = EDuelNorthDialogue::RescuedCompanion;
    }
    if (State.HasDialogue(Meeting) || (Meeting == EDuelNorthDialogue::FinalExit && State.HasFinalConversation()))
        Stage = ECompanionStage::Following;
    if (Stage != ECompanionStage::Absent && (Stage != RealStage || bRestore))
    {
        if (const auto* At = DuelNorthLayout::FindLandmark(MarkerFor(Meeting)))
        {
            const float Half = RealCompanion->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            FVector Position = At->Position + FVector(0, 0, Half + 2.f);
            if (Stage == ECompanionStage::Following && bRestore)
            {
                // Reconstruct beside the player on clear floor, including saves made beside furniture.
                FindFollowPlacement(PlayerAt + Player.GetActorQuat().RotateVector(FVector(-140, 80, 0)), Position, false);
            }
            // Start walking from the meeting point; reconstruct position only on load or when entering a new waiting scene.
            if (Stage != ECompanionStage::Following || bRestore || RealStage == ECompanionStage::Absent)
            {
                RealCompanion->SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
                RealCompanion->SetActorRotation(FRotator(0.f, (PlayerAt - Position).Rotation().Yaw, 0.f));
            }
        }
        ResetFollow();
    }
    RealStage = Stage;
    const bool bNear = FMath::Abs(PlayerAt.Z - RealCompanion->GetActorLocation().Z) < 250.f &&
        FVector::DistSquared2D(PlayerAt, RealCompanion->GetActorLocation()) < FMath::Square(2600.f);
    SetFigureVisible(RealCompanion, bExplorationEnabled && Stage != ECompanionStage::Absent &&
        (Stage == ECompanionStage::Following || bNear));
}

bool ADuelNorthNarrative::Update(FDuelNorthProgress& State, ADuelFighter& Player, float DeltaSeconds, bool bExploring)
{
    if (!ArenaRef) return false;
    const bool bPreviouslyWantedMusic = WantsEndingMusic();
    PlayerRef = &Player;
    bExplorationEnabled = bExploring;
    if (!bExploring)
    {
        ClearDialogueMedia();
        ClearEvidenceDocument();
        Playback.Cancel();
        bEndingVisible = false;
        EchoFadeElapsed.Reset();
    }
    const bool bStoryChanged = Extraction && Extraction->Update(State, Player, DeltaSeconds, bExploring);
    UpdateEndingMusic(bPreviouslyWantedMusic);
    const bool bTalkOnFoot = bExploring && !ExtractionInProgress() && !bEndingVisible && !IsReadingDocument();
    if (bTalkOnFoot && Playback.Active()) UpdateDialoguePortrait(GetWorld()->GetTimeSeconds());
    Player.SetDialogueSpeaking(bTalkOnFoot && Playback.IsSpeaking(Player.SelectedCharacter()));
    if (IsValid(RealCompanion))
        RealCompanion->SetDialogueSpeaking(bTalkOnFoot && Playback.IsSpeaking(RealCompanion->SelectedCharacter()));
    if (ExtractionInProgress())
    {
        ResetFollow();
        bWasFollowing = false;
        return bStoryChanged;
    }
    UpdateEchoFades(DeltaSeconds);
    SyncFigures(State, Player, false);
    const bool bFollowing = RealStage == ECompanionStage::Following;
    if (bFollowing && bExploring && !IsTalking() && !bEndingVisible)
    {
        if (!bWasFollowing) ResetFollow();
        Follow(Player, DeltaSeconds);
    }
    else StopFigure(RealCompanion);
    bWasFollowing = bFollowing && bExploring;
    return bStoryChanged;
}

void ADuelNorthNarrative::UpdateEchoFades(float DeltaSeconds)
{
    const float Step = FMath::IsFinite(DeltaSeconds) ? FMath::Max(0.f, DeltaSeconds) : 0.f;
    for (auto It = EchoFadeElapsed.CreateIterator(); It; ++It)
    {
        It.Value() += Step;
        if (It.Value() >= EchoFadeDuration) It.RemoveCurrent();
    }
}

bool ADuelNorthNarrative::InteractionPosition(FName Marker, const FDuelNorthProgress& State, FVector& Position) const
{
    if (Marker == TEXT("GardenCompanion"))
    {
        if (!State.DialogueAvailable(EDuelNorthDialogue::Garden) || !bExplorationEnabled || RealStage != ECompanionStage::Following ||
            !IsValid(PlayerRef) || !PlayerRef->IsAlive() || !IsValid(RealCompanion) || !RealCompanion->IsAlive() || RealCompanion->IsHidden())
            return false;
        const FVector CompanionAt = RealCompanion->GetActorLocation();
        const auto Deck = DuelNorthLayout::Locate(CompanionAt, RealCompanion->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        if (Deck.Room != TEXT("DawnGarden") || DuelNorthLayout::Locate(PlayerRef->GetActorLocation(),
            PlayerRef->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()).Room != Deck.Room) return false;
        // The radio and ladder keep their own F interaction when the player is at them.
        if (Extraction ? !Extraction->Prompt(*PlayerRef, RealCompanion, State).IsEmpty() :
            ADuelNorthExtraction::CanCall(*PlayerRef, State)) return false;
        Position = FVector(CompanionAt.X, CompanionAt.Y, Deck.SurfaceZ);
        return true;
    }
    const bool bAtMeeting = (Marker == TEXT("RescuedCompanion") && RealStage == ECompanionStage::Rescue) ||
        (Marker == TEXT("MirrorExitCompanion") && RealStage == ECompanionStage::MirrorExit) ||
        (Marker == TEXT("FinalExitCompanion") && RealStage == ECompanionStage::FinalExit);
    if (bAtMeeting)
    {
        if (!IsValid(RealCompanion)) return false;
        Position = RealCompanion->GetActorLocation() - FVector(0, 0, RealCompanion->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f);
        return true;
    }
    if (DialogueAt(Marker) == EDuelNorthDialogue::Count && Marker != TEXT("GardenEdge")) return false;
    const auto* At = DuelNorthLayout::FindLandmark(Marker);
    if (!At) return false;
    Position = At->Position;
    return true;
}

FDuelNorthNarrativeResult ADuelNorthNarrative::Confirm(FName Marker, FDuelNorthProgress& State)
{
    if (!bExplorationEnabled) return {};
    if (bEndingVisible)
    {
        bEndingVisible = false;
        if (ArenaRef) ArenaRef->SetNorthEndingMusic(false);
        return {true, false, false};
    }
    if (IsTalking()) return AdvanceDialogue(State, false);
    if (PlayerRef)
    {
        if (!Extraction && ADuelNorthExtraction::CanCall(*PlayerRef, State))
        {
            FActorSpawnParameters Spawn;
            Spawn.Owner = this;
            Extraction = GetWorld()->SpawnActor<ADuelNorthExtraction>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
        }
        const bool bPreviouslyWantedMusic = WantsEndingMusic();
        if (Extraction && Extraction->Confirm(*PlayerRef, RealCompanion, State))
        {
            UpdateEndingMusic(bPreviouslyWantedMusic);
            return {true};
        }
    }
    FVector At;
    if (!PlayerRef || !InteractionPosition(Marker, State, At) ||
        FVector::DistSquared2D(PlayerRef->GetActorLocation(), At) > FMath::Square(320.f) ||
        FMath::Abs(PlayerRef->GetActorLocation().Z - At.Z - 62.f) > 120.f) return {};
    if (Marker == TEXT("GardenEdge"))
    {
        if (!State.CanViewEnding()) return {true, false, false};
        bEndingVisible = true;
        if (ArenaRef) ArenaRef->SetNorthEndingMusic(true);
        return {true, State.ViewEnding(), true};
    }
    const EDuelNorthDialogue Dialogue = DialogueAt(Marker);
    if (IsRetiredEcho(Marker, State)) return {true, false, false};
    // Gate dialogue is heard once; the next F reaches the tower's fog-admission handler.
    if ((Dialogue == EDuelNorthDialogue::DungeonDoor || Dialogue == EDuelNorthDialogue::MirrorDoor) && State.HasDialogue(Dialogue)) return {};
    if (Playback.Begin(Dialogue, State))
    {
        PresentDialogueLine();
        StopFigure(RealCompanion);
        if (Dialogue == EDuelNorthDialogue::Garden)
        {
            const FVector TowardPlayer = (PlayerRef->GetActorLocation() - RealCompanion->GetActorLocation()).GetSafeNormal2D();
            if (!TowardPlayer.IsNearlyZero()) RealCompanion->SetActorRotation(TowardPlayer.Rotation());
        }
        return {true, false, false};
    }
    return {};
}

void ADuelNorthNarrative::DiscussEvidence(FName Id, const FDuelNorthProgress& State,
    const FString& Document, const FString& Title)
{
    if (IsTalking()) return;
    const bool bAnalysis = Playback.BeginEvidence(Id, State);
    if (!bAnalysis && Document.IsEmpty()) return;
    StopFigure(RealCompanion);
    if (PlayerRef) PlayerRef->GetCharacterMovement()->StopMovementImmediately();
    if (Document.IsEmpty()) { PresentDialogueLine(); return; }
    ClearDialogueMedia();
    EvidenceDocument = Document;
    EvidenceDocumentTitle = Title;
    DocumentStartedAt = GetWorld()->GetTimeSeconds();
    bDocumentRevealed = false;
}

int32 ADuelNorthNarrative::DocumentVisibleCharacters() const
{
    if (!IsReadingDocument()) return 0;
    if (bDocumentRevealed) return EvidenceDocument.Len();
    const double Age = FMath::Max(0.0, GetWorld()->GetTimeSeconds() - DocumentStartedAt - .15);
    return FMath::Clamp(FMath::FloorToInt(Age * 32.0), 0, EvidenceDocument.Len());
}

void ADuelNorthNarrative::ClearEvidenceDocument()
{
    EvidenceDocument.Reset();
    EvidenceDocumentTitle.Reset();
    DocumentStartedAt = -1.0;
    bDocumentRevealed = false;
}

void ADuelNorthNarrative::PlayBossDefeat(int32 Battle, const FDuelNorthProgress& State)
{
    if (Playback.Begin(FDuelNorthProgress::BossDefeatDialogue(Battle), State))
    {
        StopFigure(RealCompanion);
        PresentDialogueLine();
    }
}

FDuelNorthNarrativeResult ADuelNorthNarrative::SkipDialogue(FDuelNorthProgress& State)
{
    return bExplorationEnabled ? AdvanceDialogue(State, true) : FDuelNorthNarrativeResult{};
}

FDuelNorthNarrativeResult ADuelNorthNarrative::AdvanceDialogue(FDuelNorthProgress& State, bool bSkip)
{
    if (IsReadingDocument())
    {
        if (!bSkip && DocumentVisibleCharacters() < EvidenceDocument.Len())
        {
            bDocumentRevealed = true;
            return {true};
        }
        ClearEvidenceDocument();
        if (!bSkip)
        {
            if (Playback.Active()) PresentDialogueLine();
            return {true};
        }
        if (!Playback.Active()) return {true};
    }
    const auto FinishedDialogue = Playback.Dialogue();
    const auto Result = Playback.Advance(State, bSkip);
    if (Result.bHandled)
    {
        if (Playback.Active()) PresentDialogueLine();
        else ClearDialogueMedia();
    }
    if (Result.bStateChanged && IsEchoDialogue(FinishedDialogue))
        EchoFadeElapsed.Add(MarkerFor(FinishedDialogue), 0.f);
    return Result;
}

bool ADuelNorthNarrative::WantsEndingMusic() const
{
    return bEndingVisible || (Extraction && Extraction->WantsEndingMusic());
}

void ADuelNorthNarrative::UpdateEndingMusic(bool bPreviouslyWanted)
{
    const bool bWanted = WantsEndingMusic();
    if (ArenaRef && bWanted != bPreviouslyWanted) ArenaRef->SetNorthEndingMusic(bWanted);
}

FString ADuelNorthNarrative::Prompt(FName Marker, const FDuelNorthProgress& State) const
{
    if (bEndingVisible) return TEXT("F  返回月庭");
    if (IsReadingDocument()) return DocumentVisibleCharacters() < EvidenceDocument.Len()
        ? TEXT("F  显示全文    Esc  放下") : Playback.Active()
        ? TEXT("F  开始分析    Esc  放下") : TEXT("F  收起纸页    Esc  放下");
    if (Playback.Active())
    {
        const auto Line = Playback.Subtitle();
        return Line.Line < Line.Count ? TEXT("F  下一句    Esc  跳过") : TEXT("F  结束交谈    Esc  跳过");
    }
    if (PlayerRef)
    {
        if (Extraction)
        {
            const FString Text = Extraction->Prompt(*PlayerRef, RealCompanion, State);
            if (!Text.IsEmpty() || ExtractionInProgress()) return Text;
        }
        else if (ADuelNorthExtraction::CanCall(*PlayerRef, State))
        {
            return ADuelNorthExtraction::CallPrompt(State);
        }
    }
    if (Marker == TEXT("GardenEdge")) return State.CanViewEnding() ? TEXT("F  查看结局") : TEXT("先回终审厅与同伴会合");
    const auto Dialogue = DialogueAt(Marker);
    if (IsRetiredEcho(Marker, State) || !State.DialogueAvailable(Dialogue) ||
        ((Dialogue == EDuelNorthDialogue::DungeonDoor || Dialogue == EDuelNorthDialogue::MirrorDoor) && State.HasDialogue(Dialogue))) return FString();
    FVector Position;
    if (Dialogue == EDuelNorthDialogue::Garden && !InteractionPosition(Marker, State, Position)) return {};
    if (Dialogue == EDuelNorthDialogue::CellEcho) return TEXT("F  听墙那边的敲击声");
    if (Dialogue == EDuelNorthDialogue::WaterGateEcho) return TEXT("F  听禁闭室里的动静");
    if (Dialogue == EDuelNorthDialogue::ArchiveConversation) return TEXT("F  和同伴核对信件");
    if (Dialogue == EDuelNorthDialogue::FinalWarning) return TEXT("F  听终审厅里的声音");
    if (Dialogue == EDuelNorthDialogue::DungeonDoor) return TEXT("F  隔着门叫她一声");
    if(State.Companion()==EDuelCharacter::Ema&&IsEchoDialogue(Dialogue))return TEXT("F  阅读地上的留言");
    if(IsEchoDialogue(Dialogue))return FString::Printf(TEXT("F  回想%s留下的话"), SpeakerName(State.Companion()));
    return FString::Printf(TEXT("F  与%s交谈"), SpeakerName(State.Companion()));
}

FDuelNorthSubtitle ADuelNorthNarrative::Subtitle() const
{
    if (IsReadingDocument()) return {};
    if (Playback.Active()) return Playback.Subtitle();
    return Extraction ? Extraction->Subtitle() : FDuelNorthSubtitle{};
}

bool ADuelNorthNarrative::ExtractionEndingVisible() const
{
    return Extraction && Extraction->EndingVisible();
}

bool ADuelNorthNarrative::ExtractionInProgress() const
{
    return Extraction && Extraction->BlocksInput();
}

FVector ADuelNorthNarrative::TowerSamplePosition(const FVector& PlayerAt) const
{
    // Keep the outdoor exposure and Garden floor context while the passenger is airborne.
    return ExtractionInProgress() ? DuelNorthExtractionVisual::PickupMark + FVector(0, 0, 62) : PlayerAt;
}

void ADuelNorthNarrative::EndPlay(const EEndPlayReason::Type Reason)
{
    ClearDialogueMedia();
    ClearEvidenceDocument();
    Playback.Cancel();
    if (IsValid(ArenaRef)) ArenaRef->SetNorthEndingMusic(false);
    EchoFadeElapsed.Reset();
    if (IsValid(Extraction))
    {
        Extraction->Reset(false);
        Extraction->Destroy();
    }
    if (IsValid(PlayerRef)) PlayerRef->SetDialogueSpeaking(false);
    if (IsValid(RealCompanion)) RealCompanion->Destroy();
    for(auto& Entry:Inscriptions)if(IsValid(Entry.Value))Entry.Value->DestroyComponent();
    Inscriptions.Reset();
    Super::EndPlay(Reason);
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthNarrativePlaybackTest, "ManosabaDuel.NorthTower.NarrativePlayback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthNarrativePlaybackTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    for (const EDuelCharacter Character : {EDuelCharacter::Hiro, EDuelCharacter::Ema})
    {
        FDuelNorthProgress State;
        State.Reset(Character);
        FDuelNorthDialoguePlayback Playback;
        const auto Play = [&](EDuelNorthDialogue Dialogue)
        {
            const uint8 Before = State.Dialogues;
            const uint8 BeforeExtended = State.Reunions;
            TestTrue(TEXT("Available story fragment starts"), Playback.Begin(Dialogue, State));
            TestTrue(TEXT("Opening a conversation does not mark it complete"),
                State.Dialogues == Before && State.Reunions == BeforeExtended);
            const auto Script = State.DialogueScript(Dialogue);
            for (int32 Index = 0; Index < Script.Lines.Num(); ++Index)
            {
                const auto Subtitle = Playback.Subtitle();
                TestEqual(TEXT("Subtitle follows the selected protagonist's script"), Subtitle.Text, Script.Lines[Index].Text);
                const auto& Line = Script.Lines[Index];
                TestEqual(TEXT("Subtitle identifies the actual speaker"), Subtitle.Speaker, FString(SpeakerName(Line.Speaker)));
                TestTrue(TEXT("The portrait uses the active speaker and expression"),
                    Subtitle.Character == Line.Speaker && Subtitle.Expression == Line.Expression && Subtitle.Presence == Script.Presence);
                TestEqual(TEXT("Recording key uses exact route, scene, line and speaker"), Playback.VoiceKey(),
                    FString(Dialogue == EDuelNorthDialogue::FinalWarning ? TEXT("Revision344/") : TEXT("")) +
                    FString::Printf(TEXT("%s/%s/%02d_%s.wav"), SpeakerKey(Character),
                        *ADuelNorthNarrative::MarkerFor(Dialogue).ToString(), Index + 1, SpeakerKey(Line.Speaker)));
                for (const EDuelCharacter Listener : {EDuelCharacter::Hiro, EDuelCharacter::Ema, EDuelCharacter::Yuki})
                    TestEqual(TEXT("Only this line's speaker gestures"), Playback.IsSpeaking(Listener), Script.Lines[Index].Speaker == Listener);
                TestEqual(TEXT("Subtitle index advances by one"), Subtitle.Line, Index + 1);
                TestFalse(TEXT("Displayed final line is not yet completion"), State.HasDialogue(Dialogue));
                const auto Result = Playback.Advance(State);
                TestTrue(TEXT("F is consumed by the active conversation"), Result.bHandled);
                TestEqual(TEXT("Only closing the final line changes persistent state"), Result.bStateChanged, Index + 1 == Script.Lines.Num());
            }
            TestFalse(TEXT("Last confirmation closes playback"), Playback.Active());
            TestTrue(TEXT("Completion is now persistent"), State.HasDialogue(Dialogue));
            TestFalse(TEXT("Repeated F after completion cannot mutate the story again"), Playback.Advance(State).bStateChanged);
        };
        Play(EDuelNorthDialogue::CellEcho);
        for (const EDuelNorthDialogue Dialogue : {EDuelNorthDialogue::NameWall, EDuelNorthDialogue::LongTable, EDuelNorthDialogue::DungeonDoor}) Play(Dialogue);
        TestFalse(TEXT("The water-gate conversation follows its discovered record"), Playback.Begin(EDuelNorthDialogue::WaterGateEcho, State));
        State.ReadClue(EDuelNorthClue::WaterGateLog);
        Play(EDuelNorthDialogue::WaterGateEcho);
        TestTrue(TEXT("The first battle wins after its investigation independently of dialogue"),
            DuelNorthInvestigationQA::Prepare(State, 0) && State.WinBattle(0));
        Play(EDuelNorthDialogue::RescuedCompanion);
        State.PickupRelic(EDuelNorthRelic::RustKey);
        State.OpenDoor(EDuelNorthDoor::Chapel);
        Play(EDuelNorthDialogue::ChapelBench);
        TestFalse(TEXT("The archive conversation follows the old trial drawing"), Playback.Begin(EDuelNorthDialogue::ArchiveConversation, State));
        State.ReadClue(EDuelNorthClue::ChapelJudgments);
        Play(EDuelNorthDialogue::ArchiveConversation);
        Play(EDuelNorthDialogue::MirrorDoor);
        TestTrue(TEXT("The mirror battle wins after its investigation"),
            DuelNorthInvestigationQA::Prepare(State, 1) && State.WinBattle(1));
        State.PickupRelic(EDuelNorthRelic::MirrorKey);
        State.OpenDoor(EDuelNorthDoor::FinalStair);
        TestFalse(TEXT("The final speaker waits for the lamp investigation"), Playback.Begin(EDuelNorthDialogue::FinalWarning, State));
        if (!DuelNorthInvestigationQA::Prepare(State, 2)) return false;
        const auto FinalWarning = State.DialogueScript(EDuelNorthDialogue::FinalWarning);
        TestTrue(TEXT("Yuki addresses the selected protagonist before the final fight"),
            !FinalWarning.Lines.IsEmpty() && FinalWarning.Lines[0].Speaker == EDuelCharacter::Yuki);
        Play(EDuelNorthDialogue::FinalWarning);
        State.WinBattle(2);
        State.PickupRelic(EDuelNorthRelic::NameTag);
        FDuelNorthProgress Skipped = State;
        TestTrue(TEXT("The final court conversation can start before the exit opens"), Playback.Begin(EDuelNorthDialogue::FinalExit, Skipped));
        TestTrue(TEXT("The first line still leaves several unplayed lines"), Playback.Subtitle().Count > 2 && Playback.Subtitle().Line == 1);
        TestFalse(TEXT("Starting the conversation does not open the exit"), Skipped.CanOpenDoor(EDuelNorthDoor::TowerExit));
        const auto Skip = Playback.Advance(Skipped, true);
        TestTrue(TEXT("Skipping remaining lines closes the subtitle and commits the reunion"),
            Skip.bHandled && Skip.bStateChanged && !Playback.Active() && Skipped.bCompanionFollowing);
        TestFalse(TEXT("Another skip cannot commit progress again"), Playback.Advance(Skipped, true).bStateChanged);
        Play(EDuelNorthDialogue::FinalExit);
        TestTrue(TEXT("Reading and skipping commit the same story state"),
            Skipped.Reunions == State.Reunions && Skipped.Dialogues == State.Dialogues && Skipped.bCompanionFollowing == State.bCompanionFollowing);
        TestFalse(TEXT("The rooftop script stays locked until the exit opens"), Playback.Begin(EDuelNorthDialogue::Garden, State));
        TestTrue(TEXT("The exit opens after finishing or skipping"), State.OpenDoor(EDuelNorthDoor::TowerExit) &&
            Skipped.OpenDoor(EDuelNorthDoor::TowerExit));
        FDuelNorthProgress WithoutConversation = State;
        TestTrue(TEXT("Both endings remain available without rooftop dialogue"), WithoutConversation.ViewEnding() &&
            WithoutConversation.CompleteExtraction() && !WithoutConversation.HasDialogue(EDuelNorthDialogue::Garden));
        TestTrue(TEXT("The rooftop conversation can be deliberately skipped"), Playback.Begin(EDuelNorthDialogue::Garden, Skipped));
        TestTrue(TEXT("Skipping saves the rooftop conversation"), Playback.Advance(Skipped, true).bStateChanged && !Playback.Active());
        Play(EDuelNorthDialogue::Garden);
        TestTrue(TEXT("Finishing and skipping the rooftop conversation preserve the same state"),
            Skipped.Dialogues == State.Dialogues && Skipped.Reunions == State.Reunions &&
            Skipped.bCompanionFollowing == State.bCompanionFollowing && !State.bEndingSeen && !State.bExtractionSeen &&
            !Skipped.bEndingSeen && !Skipped.bExtractionSeen);
        TestFalse(TEXT("Neither a second court conversation nor a second garden conversation can start"),
            Playback.Begin(EDuelNorthDialogue::FinalExit, State) || Playback.Begin(EDuelNorthDialogue::Garden, State));
        FString Reason;
        TestTrue(TEXT("Completed dialogue state is valid for saving"), State.Validate(Reason));
    }
    FDuelNorthProgress MessageState;MessageState.Reset(EDuelCharacter::Hiro);
    for(const auto Dialogue:EchoDialogues)
    {
        if(Dialogue==EDuelNorthDialogue::ChapelBench)
        {
            if (!DuelNorthInvestigationQA::Prepare(MessageState, 0) || !MessageState.WinBattle(0)) return false;
            MessageState.PickupRelic(EDuelNorthRelic::RustKey);
            MessageState.OpenDoor(EDuelNorthDoor::Chapel);
        }
        const auto Script=MessageState.DialogueScript(Dialogue);
        if(!TestEqual(TEXT("Each inscription retains one original Ema sentence"),Script.Lines.Num(),1))return false;
        const FName Marker=ADuelNorthNarrative::MarkerFor(Dialogue);
        const FString GroundText=InscriptionText(Marker,EDuelCharacter::Ema);
        TestEqual(TEXT("Ground words match the DS dialogue verbatim"),GroundText,Script.Lines[0].Text);
        for(const EDuelCharacter Speaker:{EDuelCharacter::Ema, EDuelCharacter::Hiro})
            for(TCHAR Character:InscriptionText(Marker,Speaker))
                TestNotNull(TEXT("Every ground character has an existing font glyph"),InscriptionGlyph(Character));
    }
    FDuelNorthProgress Interrupted;
    FDuelNorthDialoguePlayback Playback;
    TestTrue(TEXT("Fragment begins before interruption"), Playback.Begin(EDuelNorthDialogue::NameWall, Interrupted));
    Playback.Cancel();
    TestFalse(TEXT("Cancel/load never completes an unheard fragment"), Interrupted.HasDialogue(EDuelNorthDialogue::NameWall));
    TestTrue(TEXT("Interrupted fragment can restart"), Playback.Begin(EDuelNorthDialogue::NameWall, Interrupted));
    Interrupted.Reset(EDuelCharacter::Ema);
    TestFalse(TEXT("Changing protagonist cannot stamp the other route's dialogue"), Playback.Advance(Interrupted).bStateChanged);
    for (int32 Index = 0; Index < static_cast<int32>(EDuelNorthDialogue::Count); ++Index)
    {
        const auto Dialogue = static_cast<EDuelNorthDialogue>(Index);
        const FName Marker = ADuelNorthNarrative::MarkerFor(Dialogue);
        const bool bHasInteraction = Dialogue < EDuelNorthDialogue::DungeonBossDefeat;
        TestEqual(TEXT("Physical layout markers belong to prompted conversations"),
            DuelNorthLayout::FindLandmark(Marker) != nullptr, bHasInteraction);
        TestEqual(TEXT("Marker-to-dialogue mapping round-trips"), static_cast<int32>(ADuelNorthNarrative::DialogueAt(Marker)), Index);
    }
    TestEqual(TEXT("Unrelated interaction is not swallowed by narrative"),
        static_cast<int32>(ADuelNorthNarrative::DialogueAt(TEXT("RustKey"))), static_cast<int32>(EDuelNorthDialogue::Count));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthNarrativeRuntimeTest, "ManosabaDuel.NorthTower.NarrativeRuntime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthNarrativeRuntimeTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    if (!TestTrue(TEXT("Narrative actor test requires isolated North Tower without saves"),
        FParse::Param(FCommandLine::Get(), TEXT("DuelNorthTower")) && FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")))) return false;
    UWorld* World = nullptr;
    for (const auto& Context : GEngine->GetWorldContexts())
    {
        if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); break; }
    }
    if (!TestNotNull(TEXT("A game world is running"), World)) return false;
    ADuelFighter* Player = Cast<ADuelFighter>(UGameplayStatics::GetPlayerPawn(World, 0));
    ADuelArena* Arena = nullptr;
    for (TActorIterator<ADuelArena> It(World); It; ++It) { Arena = *It; break; }
    if (!TestNotNull(TEXT("Player exists"), Player) || !TestNotNull(TEXT("Arena exists"), Arena)) return false;
    const auto* Garden = DuelNorthLayout::FindLandmark(TEXT("GardenCompanion"));
    const auto* Final = DuelNorthLayout::FindLandmark(TEXT("FinalExitCompanion"));
    if (!TestNotNull(TEXT("Garden checkpoint placement exists"), Garden) ||
        !TestNotNull(TEXT("Final court meeting exists"), Final)) return false;
    const FTransform Original = Player->GetActorTransform();
    const FVector OriginalVelocity = Player->GetCharacterMovement()->Velocity;
    ADuelNorthNarrative* Narrative = World->SpawnActor<ADuelNorthNarrative>();
    AActor* Door = nullptr;
    ON_SCOPE_EXIT
    {
        if (IsValid(Door)) Door->Destroy();
        if (IsValid(Narrative)) Narrative->Destroy();
        Player->SetActorTransform(Original, false, nullptr, ETeleportType::TeleportPhysics);
        Player->GetCharacterMovement()->Velocity = OriginalVelocity;
    };
    if (!TestNotNull(TEXT("Narrative adapter spawns"), Narrative)) return false;
    for(const EDuelCharacter RouteCharacter:{EDuelCharacter::Hiro,EDuelCharacter::Ema})
    {
        FDuelNorthProgress Trial;Trial.Reset(RouteCharacter);Narrative->Restore(Trial,*Player,*Arena);
        Trial.bOpeningCellRequired = true;
        Trial.ReadClue(EDuelNorthClue::OpeningDocket);
        const FString OriginalNote = Trial.ClueText(EDuelNorthClue::OpeningDocket);
        Narrative->DiscussEvidence(TEXT("OpeningDocket"), Trial, OriginalNote, TEXT("牢房里的纸条"));
        TestTrue(TEXT("Acquired papers start with original text and no spoken analysis"),
            Narrative->IsReadingDocument() && Narrative->BlocksInput() && Narrative->DocumentText() == OriginalNote &&
            Narrative->Subtitle().Text.IsEmpty() && Narrative->DocumentVisibleCharacters() == 0);
        Narrative->DocumentStartedAt -= .8;
        TestTrue(TEXT("Paper ink appears progressively rather than all at once"),
            Narrative->DocumentVisibleCharacters() > 0 && Narrative->DocumentVisibleCharacters() < OriginalNote.Len());
        Narrative->Confirm(NAME_None, Trial);
        TestTrue(TEXT("First F reveals the full paper while keeping the analysis hidden"),
            Narrative->IsReadingDocument() && Narrative->DocumentVisibleCharacters() == OriginalNote.Len() &&
            Narrative->Subtitle().Text.IsEmpty());
        Narrative->Confirm(NAME_None, Trial);
        TestTrue(TEXT("The next F starts the selected protagonist's analysis at its first line"),
            !Narrative->IsReadingDocument() && Narrative->IsTalking() && Narrative->Subtitle().Line == 1 &&
            Narrative->Subtitle().Character == RouteCharacter);
        Narrative->SkipDialogue(Trial);
        TestFalse(TEXT("Skipping an analysis releases the paper and dialogue input lock"), Narrative->BlocksInput());
        Narrative->DiscussEvidence(TEXT("ArchiveBindingNote"), Trial, TEXT("今天我要自己写下名字。"), TEXT("一页旧日记"));
        Narrative->Confirm(NAME_None, Trial);
        Narrative->Confirm(NAME_None, Trial);
        TestFalse(TEXT("A paper with no analysis can be closed normally"), Narrative->IsTalking());
        TestEqual(TEXT("Each opening has four grounded dialogue inscriptions"), Narrative->Inscriptions.Num(), 4);
        int32 Figures = 0;
        for (TActorIterator<ADuelFighter> It(World); It; ++It)
            if (It->GetOwner() == Narrative) ++Figures;
        TestEqual(TEXT("Only the rescued companion can have a humanoid mesh"), Figures, 1);
        for(const auto& Entry:Narrative->Inscriptions)
        {
            const auto* At=DuelNorthLayout::FindLandmark(Entry.Key);
            auto* Mesh=Entry.Value.Get();
            if(!TestNotNull(TEXT("Every inscription has real glyph geometry"),Mesh))return false;
            const auto* Section=Mesh->GetProcMeshSection(0);
            TestTrue(TEXT("Glyph strokes are real triangles"),Section&&Section->ProcVertexBuffer.Num()>80&&Section->ProcIndexBuffer.Num()>120);
            TestTrue(TEXT("Inscription attaches to the narrative root at its authored floor position"),
                At && Mesh->GetAttachParent() == Narrative->GetRootComponent() &&
                Mesh->GetComponentLocation().Equals(At->Position + FVector(0, 0, .9f), .001f));
            TestEqual(TEXT("Ground words do not block the character"),Mesh->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
            if(Section)for(const auto& Vertex:Section->ProcVertexBuffer)TestTrue(TEXT("All letters lie flat, without a vertical ghost silhouette"),FMath::Abs(Vertex.Position.Z)<.001f);
        }
    }
    const auto CheckDiscovery = [&](FDuelNorthProgress& Trial, EDuelNorthDialogue Dialogue,
        EDuelCharacter FirstSpeaker, int32 LineCount)
    {
        const FName Marker = ADuelNorthNarrative::MarkerFor(Dialogue);
        const auto* At = DuelNorthLayout::FindLandmark(Marker);
        if (!TestNotNull(TEXT("New story point has a world position"), At)) return false;
        const auto* Capsule = Player->GetCapsuleComponent();
        const FVector Standing = At->Position + FVector(0, 0, Capsule->GetScaledCapsuleHalfHeight() + 2.f);
        FCollisionQueryParams Query(SCENE_QUERY_STAT(NorthDialogueReach), false, Player);
        if (Narrative->Companion()) Query.AddIgnoredActor(Narrative->Companion());
        if (!TestTrue(FString::Printf(TEXT("The player capsule fits at %s"), *Marker.ToString()),
            !World->OverlapBlockingTestByChannel(Standing, FQuat::Identity, Capsule->GetCollisionObjectType(),
                FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Query)))
            return false;
        Player->SetActorLocation(Standing, false, nullptr, ETeleportType::TeleportPhysics);
        Narrative->Update(Trial, *Player, 1.f / 60.f, true);
        if (!TestTrue(TEXT("Normal interaction opens the optional dialogue"),
            !Narrative->Prompt(Marker, Trial).IsEmpty() && Narrative->Confirm(Marker, Trial).bHandled && Narrative->IsTalking()))
            return false;
        const auto Subtitle = Narrative->Subtitle();
        TestTrue(TEXT("The new scene opens with the authored speaker and line count"),
            Subtitle.Character == FirstSpeaker && Subtitle.Line == 1 && Subtitle.Count == LineCount);
        const auto Skip = Narrative->SkipDialogue(Trial);
        return TestTrue(TEXT("Closing a discovery saves it and retires its point"),
            Skip.bStateChanged && Trial.HasDialogue(Dialogue) && !Narrative->IsTalking() &&
            ADuelNorthNarrative::IsRetiredEcho(Marker, Trial));
    };
    for (const EDuelCharacter Route : {EDuelCharacter::Hiro, EDuelCharacter::Ema})
    {
        FDuelNorthProgress Trial;
        Trial.Reset(Route);
        Narrative->Restore(Trial, *Player, *Arena);
        if (!CheckDiscovery(Trial, EDuelNorthDialogue::CellEcho, Trial.Companion(), 5)) return false;
        Trial.ReadClue(EDuelNorthClue::WaterGateLog);
        if (!CheckDiscovery(Trial, EDuelNorthDialogue::WaterGateEcho, Trial.Companion(), 6)) return false;
        if (!DuelNorthInvestigationQA::Prepare(Trial, 0) || !Trial.WinBattle(0)) return false;
        Trial.PickupRelic(EDuelNorthRelic::RustKey);
        Trial.OpenDoor(EDuelNorthDoor::Chapel);
        Trial.ReadClue(EDuelNorthClue::ChapelJudgments);
        if (!CheckDiscovery(Trial, EDuelNorthDialogue::ArchiveConversation, Route, 6)) return false;
        if (!DuelNorthInvestigationQA::Prepare(Trial, 1) || !Trial.WinBattle(1)) return false;
        Trial.PickupRelic(EDuelNorthRelic::MirrorKey);
        Trial.OpenDoor(EDuelNorthDoor::FinalStair);
        if (!DuelNorthInvestigationQA::Prepare(Trial, 2)) return false;
        if (!CheckDiscovery(Trial, EDuelNorthDialogue::FinalWarning, EDuelCharacter::Yuki, 5)) return false;
    }

    FDuelNorthProgress State;
    State.Reset(Player->SelectedCharacter());

    const auto* NameWall = DuelNorthLayout::FindLandmark(TEXT("NameWall"));
    const auto* LongTable = DuelNorthLayout::FindLandmark(TEXT("LongTable"));
    if (!TestNotNull(TEXT("Name wall exists for echo fade"), NameWall) ||
        !TestNotNull(TEXT("Long table exists for a second independent echo"), LongTable)) return false;
    Player->SetActorLocation(NameWall->Position + FVector(150, 0, 62), false, nullptr, ETeleportType::TeleportPhysics);
    Narrative->Restore(State, *Player, *Arena);
    Narrative->Update(State, *Player, 0.f, true);
    const auto VisualAt=[&](FName Marker)->UMeshComponent*
    {
        return Narrative->Inscriptions.FindRef(Marker).Get();
    };
    UMeshComponent* Echo=VisualAt(TEXT("NameWall"));
    const auto EchoMaterial=[](UMeshComponent* Mesh)->UMaterialInstanceDynamic*
    {return Mesh?Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)):nullptr;};
    const auto IsHidden=[](const UMeshComponent* Mesh)
    {return !Mesh||!Mesh->IsVisible()||Mesh->GetOwner()->IsHidden();};
    auto* Material = EchoMaterial(Echo);
    if (!TestNotNull(TEXT("A physical name-wall echo exists"), Echo) ||
        !TestNotNull(TEXT("Echo uses a dynamic opacity material"), Material)) return false;
    TestEqual(TEXT("Existing echo material supports actual transparency"), static_cast<int32>(Material->GetBlendMode()), static_cast<int32>(BLEND_Additive));
    const FTransform EchoTransform = Echo->GetComponentTransform();
    TestFalse(TEXT("Available echo is visible"), IsHidden(Echo));
    TestEqual(TEXT("Available echo starts at full authored alpha"), Material->K2_GetVectorParameterValue(TEXT("Color")).A, 1.f);
    TestTrue(TEXT("First F starts echo dialogue"), Narrative->Confirm(TEXT("NameWall"), State).bHandled);
    const auto ToLastLine = [Narrative](FDuelNorthProgress& Progress)
    {
        while (Narrative->IsTalking() && Narrative->Subtitle().Line < Narrative->Subtitle().Count)
            Narrative->Confirm(NAME_None, Progress);
    };
    ToLastLine(State);
    Narrative->Update(State, *Player, .7f, true);
    TestFalse(TEXT("Displaying the last sentence does not save completion"), State.HasDialogue(EDuelNorthDialogue::NameWall));
    TestEqual(TEXT("Echo does not fade before the final F"), Material->K2_GetVectorParameterValue(TEXT("Color")).A, 1.f);
    TestTrue(TEXT("Final F immediately marks echo completion for saving"), Narrative->Confirm(NAME_None, State).bStateChanged);
    TestTrue(TEXT("Completion is persistent while the figure is still visible"), State.HasDialogue(EDuelNorthDialogue::NameWall) && !IsHidden(Echo));
    TestTrue(TEXT("Pure dialogue point retires immediately"), ADuelNorthNarrative::IsRetiredEcho(TEXT("NameWall"), State));
    TestFalse(TEXT("Mirror marker remains usable as a fog gate"), ADuelNorthNarrative::IsRetiredEcho(TEXT("MirrorGate"), State));
    TestFalse(TEXT("Repeated F cannot restart a completed echo"), Narrative->Confirm(TEXT("NameWall"), State).bStateChanged || Narrative->IsTalking());
    float PreviousAlpha = 1.f;
    for (int32 Sample = 0; Sample < 3; ++Sample)
    {
        Narrative->Update(State, *Player, .35f, true);
        const float Alpha = Material->K2_GetVectorParameterValue(TEXT("Color")).A;
        TestTrue(TEXT("Actual material alpha decreases gradually before the end"), Alpha > 0.f && Alpha < PreviousAlpha);
        TestFalse(TEXT("Echo is not abruptly hidden during its fade"), IsHidden(Echo));
        TestTrue(TEXT("Fading keeps the same position, rotation and scale"), Echo->GetComponentTransform().Equals(EchoTransform, .001f));
        PreviousAlpha = Alpha;
    }
    Narrative->Update(State, *Player, .40f, true);
    TestEqual(TEXT("Material alpha reaches zero after the full fade"), Material->K2_GetVectorParameterValue(TEXT("Color")).A, 0.f);
    TestTrue(TEXT("Echo is hidden after fading and its timer is released"), IsHidden(Echo) && Narrative->EchoFadeElapsed.IsEmpty());

    Player->SetActorLocation(LongTable->Position + FVector(150, 0, 62), false, nullptr, ETeleportType::TeleportPhysics);
    Narrative->Update(State, *Player, 0.f, true);
    UMeshComponent* OtherEcho=VisualAt(TEXT("LongTable"));
    auto* OtherMaterial = EchoMaterial(OtherEcho);
    if (!TestNotNull(TEXT("Second echo remains independent"), OtherMaterial)) return false;
    TestEqual(TEXT("Completing one echo leaves the next echo fully visible"), OtherMaterial->K2_GetVectorParameterValue(TEXT("Color")).A, 1.f);
    TestTrue(TEXT("Second echo can begin its own dialogue"), Narrative->Confirm(TEXT("LongTable"), State).bHandled);
    ToLastLine(State);
    TestTrue(TEXT("Second echo saves on its own final confirmation"), Narrative->Confirm(NAME_None, State).bStateChanged);
    Narrative->Update(State, *Player, .25f, true);
    TestTrue(TEXT("Second echo is actually mid-fade before save/load"), OtherMaterial->K2_GetVectorParameterValue(TEXT("Color")).A > 0.f &&
        OtherMaterial->K2_GetVectorParameterValue(TEXT("Color")).A < 1.f && !Narrative->EchoFadeElapsed.IsEmpty());
    TArray<uint8> EchoBytes;
    FMemoryWriter EchoWriter(EchoBytes, true);
    FDuelNorthProgress::StaticStruct()->SerializeItem(EchoWriter, &State, nullptr);
    FDuelNorthProgress EchoLoaded;
    FMemoryReader EchoReader(EchoBytes, true);
    FDuelNorthProgress::StaticStruct()->SerializeItem(EchoReader, &EchoLoaded, nullptr);
    Narrative->Restore(EchoLoaded, *Player, *Arena);
    TestTrue(TEXT("Loading completed dialogue discards transient fade timers"), Narrative->EchoFadeElapsed.IsEmpty());
    TestTrue(TEXT("Loading mid-fade completion does not resurrect either echo"), IsHidden(Echo) && IsHidden(OtherEcho));
    TestEqual(TEXT("Reloaded completed echo has zero actual alpha"), OtherMaterial->K2_GetVectorParameterValue(TEXT("Color")).A, 0.f);

    const FName WaitingMarkers[] = {TEXT("RescuedCompanion"), TEXT("MirrorExitCompanion"), TEXT("FinalExitCompanion")};
    const FName WaitingRooms[] = {TEXT("DungeonCourt"), TEXT("MirrorCourt"), TEXT("FinalCourt")};
    for (int32 Battle = 0; Battle < FDuelNorthProgress::BattleCount; ++Battle)
    {
        if (!DuelNorthInvestigationQA::Prepare(State, Battle) || !State.WinBattle(Battle)) return false;
        State.PickupRelic(static_cast<EDuelNorthRelic>(Battle));
        if (Battle < 2) State.OpenDoor(static_cast<EDuelNorthDoor>(Battle));
        const auto* Waiting = DuelNorthLayout::FindLandmark(WaitingMarkers[Battle]);
        if (!TestNotNull(TEXT("A waiting point is set for this fight"), Waiting)) return false;
        Player->SetActorLocation(Waiting->Position + FVector(-150, 0, 62), false, nullptr, ETeleportType::TeleportPhysics);
        Narrative->Update(State, *Player, 0.f, true);
        FVector Position;
        TestTrue(TEXT("The visible companion waits on the battlefield side of each exit"),
            Narrative->Companion() && !Narrative->Companion()->IsHidden() && Narrative->InteractionPosition(Waiting->Id, State, Position) &&
            DuelNorthLayout::Locate(Position, 0).Room == WaitingRooms[Battle]);
    }
    TestTrue(TEXT("F starts the final conversation in the court"), Narrative->Confirm(Final->Id, State).bHandled && Narrative->IsTalking());
    TestFalse(TEXT("Showing the first line leaves following disabled"), State.bCompanionFollowing);
    const auto Skip = Narrative->SkipDialogue(State);
    TestTrue(TEXT("Esc completes the one conversation and requests a save"), Skip.bHandled && Skip.bStateChanged && !Narrative->IsTalking());
    TestTrue(TEXT("Completing the real companion conversation creates no echo fade"), Narrative->EchoFadeElapsed.IsEmpty());
    ADuelFighter* Companion = const_cast<ADuelFighter*>(Narrative->Companion());
    if (!TestNotNull(TEXT("A separate standing companion exists"), Companion)) return false;
    const FVector BeforeFollow = Companion->GetActorLocation();
    Narrative->Update(State, *Player, 1.f / 60.f, true);
    TestFalse(TEXT("Real companion remains visible after the conversation"), Companion->IsHidden());
    TestTrue(TEXT("Starting follow does not teleport the speaking companion"), Companion->GetActorLocation().Equals(BeforeFollow, .01f));
    TestTrue(TEXT("Finishing the conversation lets the player open the tower gate"), State.OpenDoor(EDuelNorthDoor::TowerExit));
    Narrative->Update(State, *Player, 1.f / 60.f, true);
    TestTrue(TEXT("Opening the tower gate keeps the same companion on the same path"),
        Narrative->Companion() == Companion && Companion->GetActorLocation().Equals(BeforeFollow, .01f));
    TestFalse(TEXT("The visible follower does not offer the rooftop conversation inside the court"),
        Narrative->Confirm(Garden->Id, State).bHandled || Narrative->IsTalking());
    TArray<uint8> Bytes;
    FMemoryWriter Writer(Bytes, true);
    FDuelNorthProgress::StaticStruct()->SerializeItem(Writer, &State, nullptr);
    FDuelNorthProgress Loaded;
    FMemoryReader Reader(Bytes, true);
    FDuelNorthProgress::StaticStruct()->SerializeItem(Reader, &Loaded, nullptr);
    Player->SetActorLocation(Garden->Position + FVector(150, 0, 62), false, nullptr, ETeleportType::TeleportPhysics);
    Narrative->Restore(Loaded, *Player, *Arena);
    Companion = const_cast<ADuelFighter*>(Narrative->Companion());
    TestFalse(TEXT("Reload starts with no unfinished transient subtitle"), Narrative->IsTalking());
    TestEqual(TEXT("Reload restores the selected route's companion"), static_cast<int32>(Companion->SelectedCharacter()), static_cast<int32>(Loaded.Companion()));
    TestTrue(TEXT("Reload reconstructs following partner near the player"),
        FVector::DistSquared2D(Companion->GetActorLocation(), Player->GetActorLocation()) < FMath::Square(300.f));
    const FVector PlayerOnDeck = Player->GetActorLocation(), CompanionOnDeck = Companion->GetActorLocation();
    Player->SetActorLocation(FVector(5180, 0, PlayerOnDeck.Z), false, nullptr, ETeleportType::TeleportPhysics);
    Companion->SetActorLocation(FVector(5030, 0, CompanionOnDeck.Z), false, nullptr, ETeleportType::TeleportPhysics);
    TestFalse(TEXT("A nearby companion still on the bridge cannot start the conversation"),
        Narrative->Confirm(Garden->Id, Loaded).bHandled || Narrative->IsTalking());
    Player->SetActorLocation(PlayerOnDeck, false, nullptr, ETeleportType::TeleportPhysics);
    Companion->SetActorLocation(CompanionOnDeck, false, nullptr, ETeleportType::TeleportPhysics);

    Narrative->Update(Loaded, *Player, 1.f / 60.f, false);
    TestTrue(TEXT("Combat hides the companion and removes its collision and actor tick"),
        Companion->IsHidden() && !Companion->GetActorEnableCollision() && !Companion->IsActorTickEnabled() &&
        !Companion->GetCharacterMovement()->IsComponentTickEnabled());
    Narrative->Update(Loaded, *Player, 1.f / 60.f, true);
    TestTrue(TEXT("Returning to exploration restores the physical companion"),
        !Companion->IsHidden() && Companion->GetActorEnableCollision() && Companion->IsActorTickEnabled() &&
        Companion->GetCharacterMovement()->IsComponentTickEnabled());

    const FVector Start = Companion->GetActorLocation();
    Door = World->SpawnActor<AActor>();
    if (!TestNotNull(TEXT("Collision-only test door spawns"), Door)) return false;
    UBoxComponent* Block = NewObject<UBoxComponent>(Door);
    Door->SetRootComponent(Block);
    Block->SetBoxExtent(FVector(20, 180, 200));
    Block->SetCollisionProfileName(TEXT("BlockAll"));
    Door->AddInstanceComponent(Block);
    Block->RegisterComponent();
    const FVector Barrier = Start + FVector(220, 0, 0);
    Door->SetActorLocation(Barrier);
    Player->SetActorLocation(Start + FVector(650, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
    auto* Motion = Companion->GetCharacterMovement();
    for (int32 Frame = 0; Frame < 150; ++Frame)
    {
        Narrative->Update(Loaded, *Player, 1.f / 60.f, true);
        Companion->Tick(1.f / 60.f);
        Motion->TickComponent(1.f / 60.f, LEVELTICK_All, nullptr);
    }
    TestTrue(TEXT("Following makes physical walking progress"), Companion->GetActorLocation().X > Start.X + 60.f);
    TestTrue(TEXT("Closed door stops the companion capsule"), Companion->GetActorLocation().X < Barrier.X - 20.f);
    Block->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    for (int32 Frame = 0; Frame < 180; ++Frame)
    {
        Narrative->Update(Loaded, *Player, 1.f / 60.f, true);
        Companion->Tick(1.f / 60.f);
        Motion->TickComponent(1.f / 60.f, LEVELTICK_All, nullptr);
    }
    TestTrue(TEXT("Opening the door lets the companion continue walking"), Companion->GetActorLocation().X > Barrier.X + 60.f);
    TestTrue(TEXT("Companion settles near the player without overtaking"),
        FVector::DistSquared2D(Companion->GetActorLocation(), Player->GetActorLocation()) < FMath::Square(220.f));
    TestTrue(TEXT("Full actor tick preserves the garden floor instead of clamping to an old court"),
        FMath::Abs(Companion->GetActorLocation().Z - Start.Z) < 8.f && Companion->GetActorLocation().X > 5000.f);
    const FVector BeforeGardenTalk = Companion->GetActorLocation();
    FVector TalkPosition;
    TestTrue(TEXT("The interaction follows the real companion's location across the deck"),
        Narrative->InteractionPosition(Garden->Id, Loaded, TalkPosition) &&
        FVector::DistSquared2D(TalkPosition, BeforeGardenTalk) < .01f);
    TestTrue(TEXT("F starts the rooftop dialogue with the living companion"),
        Narrative->Confirm(Garden->Id, Loaded).bHandled && Narrative->IsTalking() &&
        Narrative->Subtitle().Speaker == SpeakerName(Loaded.Companion()));
    ToLastLine(Loaded);
    TestFalse(TEXT("The open dialogue has yet to write the rooftop conversation"), Loaded.HasDialogue(EDuelNorthDialogue::Garden));
    TestTrue(TEXT("Finishing the rooftop dialogue writes completion without opening either ending"),
        Narrative->Confirm(NAME_None, Loaded).bStateChanged && Loaded.HasDialogue(EDuelNorthDialogue::Garden) &&
        !Narrative->IsTalking() && !Narrative->EndingVisible());
    Narrative->Update(Loaded, *Player, 1.f / 60.f, true);
    TestTrue(TEXT("Talking on the roof retains the same visible follower at the same location"),
        Narrative->Companion() == Companion && !Companion->IsHidden() && Companion->GetActorLocation().Equals(BeforeGardenTalk, .01f));
    TestTrue(TEXT("The completed rooftop dialogue cannot start again"),
        Narrative->Prompt(Garden->Id, Loaded).IsEmpty() && !Narrative->Confirm(Garden->Id, Loaded).bHandled);
    const auto* Edge = DuelNorthLayout::FindLandmark(TEXT("GardenEdge"));
    if (TestNotNull(TEXT("Ending marker exists"), Edge))
    {
        Player->SetActorLocation(Edge->Position + FVector(0, 0, 62), false, nullptr, ETeleportType::TeleportPhysics);
        const auto Result = Narrative->Confirm(TEXT("GardenEdge"), Loaded);
        TestTrue(TEXT("Ending opens only through its explicit interaction"), Result.bEndingOpened && Result.bStateChanged);
        TestTrue(TEXT("Ending page is visible"), Narrative->EndingVisible());
        TestTrue(TEXT("F closes the page even away from an interaction"), Narrative->Confirm(NAME_None, Loaded).bHandled);
        TestFalse(TEXT("Closing ending returns the view to the garden"), Narrative->EndingVisible());
        TestTrue(TEXT("Ending can be replayed without another progress write"),
            Narrative->Confirm(TEXT("GardenEdge"), Loaded).bEndingOpened && !Narrative->Confirm(NAME_None, Loaded).bStateChanged);
    }
    return true;
}
#endif
