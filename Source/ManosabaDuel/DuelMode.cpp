#include "DuelGame.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "CanvasItem.h"
#include "DuelArena.h"
#include "DuelNorthTower.h"
#include "DuelNorthNarrative.h"
#include "DuelCastle.h"
#include "DuelProgress.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineFontServices.h"
#include "Fonts/FontMeasure.h"
#include "GameFramework/PlayerController.h"
#include "Internationalization/BreakIterator.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

namespace
{
    constexpr const TCHAR* CheckpointSlot = TEXT("MissingTestimonyCheckpoint");

    const FLinearColor Ink(.012f, .014f, .020f, .91f);
    const FLinearColor Ivory(.89f, .83f, .71f);
    const FLinearColor Ash(.64f, .68f, .72f);
    const FLinearColor Bronze(.68f, .42f, .20f);
    const FLinearColor Ember(.94f, .66f, .34f);
    const FLinearColor Oxblood(.59f, .071f, .114f);
    const FLinearColor Moss(.26f, .51f, .35f);
    const FLinearColor Cerulean(.16f, .34f, .64f);
    const FLinearColor Sakura(.70f, .21f, .44f);

    FSlateFontInfo DuelFontInfo(const UFont* Font, float PixelSize)
    {
        // Slate sizes are points at 96 DPI; the HUD places labels in pixels.
        return FSlateFontInfo(Font, FMath::Max(1.f, FMath::RoundToFloat(PixelSize * .75f)));
    }

    int32 WeaponArtTile(EDuelWeapon Gear)
    {
        if (Gear == EDuelWeapon::Empty) return INDEX_NONE;
        return Gear == EDuelWeapon::RitualSword ? 11 : static_cast<int32>(Gear);
    }
}

ADuelMode::ADuelMode()
{
    PrimaryActorTick.bCanEverTick = true;
    DefaultPawnClass = ADuelFighter::StaticClass();
    HUDClass = ADuelHUD::StaticClass();
}

void ADuelMode::StartPlay()
{
    const TCHAR* CommandLine = FCommandLine::Get();
    const bool bForceLinear = FParse::Param(CommandLine, TEXT("DuelLinearRoute"));
    const bool bForceCastle = FParse::Param(CommandLine, TEXT("DuelCastleRoute"));
    bool bNorthTowerByDefault = false;
    GConfig->GetBool(TEXT("ManosabaDuel.Gameplay"), TEXT("NorthTowerByDefault"), bNorthTowerByDefault, GGameIni);
    bNorthTowerRoute = FParse::Param(CommandLine, TEXT("DuelNorthTower")) ||
        (FApp::CanEverRender() && bNorthTowerByDefault && !bForceLinear && !bForceCastle);
    bCastleRoute = !bNorthTowerRoute && !bForceLinear && (FApp::CanEverRender() || bForceCastle);
    GraphicsSettings.Restore();
    Arena = GetWorld()->SpawnActor<ADuelArena>();
    BossFighter = SpawnBoss(FirstBoss);
    if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
    {
        if (!Controller->GetPawn())
        {
            RestartPlayer(Controller);
        }
        PlayerFighter = Cast<ADuelFighter>(Controller->GetPawn());
    }
    LinkFighters();
    Super::StartPlay();
    if (bCastleRoute)
    {
        Arena->BuildCastle();
    }
    if (bNorthTowerRoute)
    {
        NorthTowerActor = GetWorld()->SpawnActor<ADuelNorthTower>();
        NorthTowerActor->Initialize(*this);
    }
    else ReturnToCheckpoint();
    if (FApp::CanEverRender() && !FParse::Param(FCommandLine::Get(), TEXT("DuelNoTitle")))
    {
        Result = EDuelOutcome::Title;
        MenuSelection = CanContinue() ? 0 : 1;
    }
    RefreshMenuPointer();
}

void ADuelMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    RefreshMenuPointer();
    if (!PlayerFighter || UGameplayStatics::IsGamePaused(this))
    {
        return;
    }
    if (NorthTowerActor) { NorthTowerActor->Update(*this, DeltaSeconds); return; }
    const FVector At = PlayerFighter->GetActorLocation();
    const FVector Local = At - Arena->CourtOrigin();
    if (bCastleRoute)
    {
        if (FigureView == 0)
        {
            TickCastle(At);
        }
    }
    else if (Result == EDuelOutcome::Ready && bGateOpen && Local.X >= DuelCourt::GateCrossX && FMath::Abs(Local.Y) < 244.f)
    {
        ResetEncounter(true);
    }
    else if (Result == EDuelOutcome::Victory && !bYukiCourt &&
        Local.X >= DuelCourt::ArchiveEntryX - 74.f && FMath::Abs(Local.Y) < 230.f)
    {
        EnterArchive();
    }
    else if (Result == EDuelOutcome::Passage && At.X >= DuelCourt::PassageTurn.X - 175.f && At.Y >= -164.f)
    {
        EnterAppeal();
    }
    else if (Result == EDuelOutcome::Archive && bAppeal && RitualComplete() && !bReadingArchive &&
        Local.X >= DuelCourt::YukiArchiveExitX && FMath::Abs(Local.Y) < 275.f)
    {
        EnterYukiCourt();
    }
    else if (Result == EDuelOutcome::Victory && bYukiCourt && Local.X >= DuelCourt::YukiGardenX &&
        FMath::Abs(Local.Y) < 250.f)
    {
        FinishTrial();
    }
    if (Result == EDuelOutcome::Epilogue && bCompanionFollowing && RescuedFighter && !bReadingArchive)
    {
        const FVector Forward = PlayerFighter->GetActorForwardVector();
        const FVector Side = FVector::CrossProduct(FVector::UpVector, Forward);
        const FVector Goal = At - Forward * 155.f + Side * 99.f;
        const FVector Origin = DuelCourt::YukiOrigin;
        const FVector SafeGoal(FMath::Clamp(Goal.X, Origin.X + DuelCourt::YukiGardenX - 35.f, Origin.X + 4330.f),
            FMath::Clamp(Goal.Y, -830., 830.), Goal.Z);
        const FVector Offset = SafeGoal - RescuedFighter->GetActorLocation();
        if (Offset.SizeSquared2D() > FMath::Square(58.f))
        {
            const FVector Direction = Offset.GetSafeNormal2D();
            RescuedFighter->FaceDirection(Direction, DeltaSeconds);
            RescuedFighter->AddMovementInput(Direction, FMath::Clamp((Offset.Size2D() - 53.) / 142., .23, 1.), true);
        }
    }
}

void ADuelMode::RestartPlayer(AController* NewPlayer)
{
    if (!IsValid(NewPlayer))
    {
        return;
    }
    RestartPlayerAtTransform(NewPlayer, DuelCourt::Start(false));
    PlayerFighter = Cast<ADuelFighter>(NewPlayer->GetPawn());
    LinkFighters();
}

void ADuelMode::LinkFighters()
{
    if (IsValid(PlayerFighter) && IsValid(BossFighter) && IsValid(Arena))
    {
        PlayerFighter->SetEncounter(BossFighter, Arena);
        BossFighter->SetEncounter(PlayerFighter, Arena);
    }
}

ADuelFighter* ADuelMode::SpawnBoss(EDuelCharacter Character)
{
    const FTransform Start = DuelCourt::Start(true, Arena->CourtOrigin());
    ADuelFighter* Fighter = GetWorld()->SpawnActorDeferred<ADuelFighter>(ADuelFighter::StaticClass(), Start,
        nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (Fighter)
    {
        Fighter->ConfigureBoss(Character);
        Fighter->FinishSpawning(Start);
        Fighter->SetActorTransform(Start, false, nullptr, ETeleportType::TeleportPhysics);
    }
    return Fighter;
}

void ADuelMode::SetBoss(EDuelCharacter Character)
{
    if (BossFighter && BossFighter->SelectCharacter(Character))
    {
        return;
    }
    ADuelFighter* Next = SpawnBoss(Character);
    if (Next)
    {
        if (BossFighter)
        {
            BossFighter->Destroy();
        }
        BossFighter = Next;
        LinkFighters();
    }
}

void ADuelMode::RescueBoss()
{
    BossFighter->BossPhase = 1;
    BossFighter->FormProgress = 0.f;
    BossFighter->Health = DuelCourt::BossMaxHealth;
    BossFighter->ShowCharacterForm(true);
    Arena->RitualTrace(BossFighter->GetActorLocation(), false);
    Arena->SetBattlePhase(1);
}

void ADuelMode::FighterFell(const ADuelFighter* Fighter)
{
    if (Result != EDuelOutcome::Fighting || !PlayerFighter || !BossFighter || (Fighter != PlayerFighter && Fighter != BossFighter))
    {
        return;
    }
    if (NorthTowerActor) { NorthTowerActor->FinishBattle(*this, Fighter == BossFighter); return; }
    Result = Fighter == BossFighter ? EDuelOutcome::Victory : EDuelOutcome::Defeat;
    CompletedDuelSeconds = FMath::Max(1, FMath::RoundToInt(GetWorld()->GetTimeSeconds() - DuelStartedAt));
    MenuSelection = 0;
    bCompanionFollowing = false;
    PlayerFighter->EndCombat();
    BossFighter->EndCombat();
    if (Result == EDuelOutcome::Victory)
    {
        BossFighter->SetActorEnableCollision(false);
    }
    if (Arena)
    {
        Arena->SetCombatCamera(false);
        Arena->ClearCombatEffects();
        Arena->Sound(Result == EDuelOutcome::Victory ? EDuelSound::Victory : EDuelSound::Defeat,
            Fighter->GetActorLocation());
        if (Result == EDuelOutcome::Defeat)
        {
            if (bCastleRoute)
            {
                VerdictScars = FMath::Min<int32>(VerdictScars + 1, 99);
                WriteCheckpoint();
            }
            const bool bSpoken = Arena->TryBossLine(BossFighter->SelectedCharacter(), EDuelVoice::Defeat, BossFighter->GetActorLocation());
            SubtitleUntil = !bSpoken ? GetWorld()->GetTimeSeconds() + 6.1 : 0.0;
        }
        if (Result == EDuelOutcome::Victory)
        {
            if (bYukiCourt)
            {
                if (!bCastleRoute)
                {
                    Arena->SetDawn(true);
                }
            }
            else
            {
                RescueBoss();
                if (bCastleRoute && CastleDuel >= 0)
                {
                    if (CastleWins == 0)
                    {
                        FirstDuelSeconds = CompletedDuelSeconds;
                    }
                    else
                    {
                        SecondDuelSeconds += CompletedDuelSeconds;
                    }
                    CastleWins |= DuelCastle::WingBit(CastleDuel);
                    Arena->SetCastleFog(CastleDuel, false);
                    WriteCheckpoint();
                }
                else
                {
                    Arena->SetArchiveGateOpen(true);
                }
            }
            PlayerFighter->PrepareExploration();
        }
    }
}

void ADuelMode::BeginEncounter()
{
    if (Result == EDuelOutcome::Ready && NearGate())
    {
        bGateOpen = true;
        Arena->SetGateClosed(false);
        Arena->Sound(EDuelSound::VerdictBell, Arena->CourtOrigin() + FVector(1790, 0, 532));
    }
}

bool ADuelMode::NearGate() const
{
    if (!PlayerFighter || FigureView != 0 || bGateOpen || Result != EDuelOutcome::Ready ||
        (!bAppeal && !bPrisonGateOpen))
    {
        return false;
    }
    const FVector At = PlayerFighter->GetActorLocation() - Arena->CourtOrigin();
    return At.X >= (bAppeal && !bYukiCourt ? DuelCourt::AppealRampEndX - 28.f : DuelCourt::GateX - 235.f) &&
        At.X < DuelCourt::GateX && FMath::Abs(At.Y) < 208.f && At.Z < 146.f;
}

bool ADuelMode::NearPrisonGate() const
{
    if (Result != EDuelOutcome::Ready || FigureView != 0 || bAppeal || bPrisonGateOpen || !PlayerFighter)
    {
        return false;
    }
    const FVector At = PlayerFighter->GetActorLocation();
    return At.X >= DuelCourt::PrisonGateX - 232.f && At.X <= DuelCourt::PrisonGateX &&
        FMath::Abs(At.Y) < 200.f && At.Z < 146.f;
}

bool ADuelMode::NearWarrant() const
{
    return Result == EDuelOutcome::Ready && FigureView == 0 && !bAppeal && !bWarrantFound && PlayerFighter &&
        FVector::DistSquared2D(PlayerFighter->GetActorLocation(), DuelCourt::Warrant) < FMath::Square(178.f);
}

bool ADuelMode::NearArchiveDesk() const
{
    return Result == EDuelOutcome::Archive && PlayerFighter &&
        (!bAppeal || PlayerFighter->GetActorLocation().Z >= Arena->CourtOrigin().Z + DuelCourt::VerdictRise + 38.f) &&
        FVector::DistSquared2D(PlayerFighter->GetActorLocation() - Arena->CourtOrigin(),
            FVector(bAppeal ? DuelCourt::VerdictDeskX : DuelCourt::ArchiveDeskX, 0, 0)) <= FMath::Square(247.f);
}

bool ADuelMode::NearAppealPassage() const
{
    return Result == EDuelOutcome::Archive && !bAppeal && !bReadingArchive && Decision != EDuelEnding::Undecided &&
        PlayerFighter && FVector::DistSquared2D(PlayerFighter->GetActorLocation(), DuelCourt::AppealPassage) < FMath::Square(208.f);
}

const TCHAR* ADuelMode::AreaName() const
{
    if (NorthTowerActor) return DuelNorthLayout::FloorName(NorthTowerActor->CurrentFloor());
    if (bCastleRoute && PlayerFighter)
    {
        return CastleArea();
    }
    if (bYukiCourt)
    {
        if (Result == EDuelOutcome::Epilogue)
        {
            return TEXT("月庭");
        }
        return Result == EDuelOutcome::Ready ? TEXT("裁决回廊") : TEXT("终审庭");
    }
    if (Result == EDuelOutcome::Passage)
    {
        return TEXT("证人回廊");
    }
    if (Result == EDuelOutcome::Archive)
    {
        return bAppeal ? TEXT("复审席") : TEXT("封存证物库");
    }
    if (Result == EDuelOutcome::Fighting || Result == EDuelOutcome::Victory)
    {
        return bAppeal ? TEXT("复审礼堂") : TEXT("审讯庭");
    }
    if (bAppeal)
    {
        return TEXT("证人通道");
    }
    const double X = PlayerFighter->GetActorLocation().X;
    if (X < DuelCourt::StairBaseX)
    {
        return TEXT("沉水门房");
    }
    if (X < DuelCourt::StairTopX)
    {
        return TEXT("押送阶梯");
    }
    return X < DuelCourt::PrisonGateX ? TEXT("禁闭回廊") : TEXT("监牢外廊");
}

bool ADuelMode::CanExplore() const
{
    if(NorthTowerActor && NorthTowerActor->BlocksPlayerInput())return false;
    return (Result == EDuelOutcome::Ready && FigureView == 0) ||
        Result == EDuelOutcome::Victory || Result == EDuelOutcome::Passage ||
        (Result == EDuelOutcome::Epilogue && !bReadingArchive) ||
        (Result == EDuelOutcome::Archive && !bReadingArchive);
}

void ADuelMode::EnterArchive()
{
    const FVector Origin = Arena->CourtOrigin();
    Result = EDuelOutcome::Archive;
    Arena->Sound(EDuelSound::RainAtlas, Origin + FVector(bAppeal ? DuelCourt::VerdictDeskX : DuelCourt::ArchiveDeskX, 0,
        170.f + (bAppeal ? DuelCourt::VerdictRise : 0.f)));
}

void ADuelMode::EnterAppeal()
{
    const EDuelCharacter Next = BossFighter->SelectedCharacter() == EDuelCharacter::Ema ? EDuelCharacter::Hiro : EDuelCharacter::Ema;
    if (!BossFighter->SelectCharacter(Next))
    {
        return;
    }
    FirstDuelSeconds = CompletedDuelSeconds;
    FirstBoss = BossFighter->SelectedCharacter() == EDuelCharacter::Ema ? EDuelCharacter::Hiro : EDuelCharacter::Ema;
    bAppeal = true;
    bAppealRested = false;
    Arena->SetAppeal(true);
    BossFighter->ResetFighter();
    BossFighter->EndCombat();
    Result = EDuelOutcome::Ready;
    bGateOpen = false;
    Arena->SetGateClosed(true);
    Arena->SetArchiveGateOpen(false);
    PlayerFighter->PrepareExploration();
    Arena->Sound(EDuelSound::VerdictBell, DuelCourt::AppealOrigin + FVector(-2600, 0, 300));
}

void ADuelMode::EnterYukiCourt()
{
    SecondDuelSeconds = CompletedDuelSeconds;
    bYukiCourt = true;
    bGateOpen = false;
    Result = EDuelOutcome::Ready;
    Arena->SetYukiCourt(true);
    Arena->SetGateClosed(true);
    SetBoss(EDuelCharacter::Yuki);
    BossFighter->EndCombat();
    Arena->SetCombatCamera(false);
    Arena->Sound(EDuelSound::VerdictBell, DuelCourt::YukiCheckpoint().GetLocation());
    PlayerFighter->RestAtCheckpoint();
    WriteCheckpoint();
}

void ADuelMode::FinishTrial(bool bFollow)
{
    Result = EDuelOutcome::Epilogue;
    bReadingArchive = false;
    MenuSelection = 0;
    bCompanionFollowing = bFollow;
    PlayerFighter->EndCombat();
    PlayerFighter->RestAtCheckpoint();
    Arena->SetDawn(true);
    if (!RescuedFighter)
    {
        const EDuelCharacter Partner = PlayerFighter->SelectedCharacter() == EDuelCharacter::Hiro ? EDuelCharacter::Ema : EDuelCharacter::Hiro;
        RescuedFighter = SpawnBoss(Partner);
    }
    if (RescuedFighter)
    {
        RescuedFighter->ShowCharacterForm(true);
        RescuedFighter->EndCombat();
        RescuedFighter->GetCharacterMovement()->bRunPhysicsWithNoController = true;
        RescuedFighter->GetCharacterMovement()->MaxWalkSpeed = 550.f;
        RescuedFighter->SetActorLocationAndRotation(DuelCourt::YukiOrigin + FVector(3275, 190, 103), FRotator(0, 202, 0));
    }
    Arena->Sound(EDuelSound::Victory, DuelCourt::YukiOrigin + FVector(3300, 0, 150));
    WriteCheckpoint();
}

bool ADuelMode::NearGardenExit() const
{
    return Result == EDuelOutcome::Epilogue && PlayerFighter &&
        RescuedFighter && FVector::DistSquared2D(PlayerFighter->GetActorLocation(), RescuedFighter->GetActorLocation())
            <= FMath::Square(310.f);
}

bool ADuelMode::SilentBellActive() const
{
    return bYukiCourt && Result == EDuelOutcome::Fighting && BossFighter->Move == EDuelMove::SilentBell &&
        BossFighter->MoveAge <= 1.95f;
}

bool ADuelMode::CanContinue() const
{
    if(NorthTowerActor)return NorthTowerActor->HasCheckpoint();
    return UGameplayStatics::DoesSaveGameExist(CheckpointSlot, 0);
}

bool ADuelMode::NearRestPoint() const
{
    if (Result != EDuelOutcome::Ready || !PlayerFighter || FigureView != 0 || (!bAppeal && !bPrisonGateOpen))
    {
        return false;
    }
    const FVector At = bYukiCourt ? DuelCourt::YukiCheckpoint().GetLocation() :
        bAppeal ? DuelCourt::AppealCheckpoint().GetLocation() : DuelCourt::Checkpoint().GetLocation();
    return FVector::DistSquared2D(PlayerFighter->GetActorLocation(), At) <= FMath::Square(167.f);
}

int32 ADuelMode::RitualMark() const
{
    if (!bAppeal || Result != EDuelOutcome::Archive || bReadingArchive || RitualComplete())
    {
        return INDEX_NONE;
    }
    const FVector At = Arena->CourtOrigin() + DuelCourt::RitualMarks[RitualSeal];
    return FVector::DistSquared2D(PlayerFighter->GetActorLocation(), At) <= FMath::Square(131.f) ? RitualSeal : INDEX_NONE;
}

float ADuelMode::RitualCaptionOpacity() const
{
    return FMath::SmoothStep(0.f, .72f, static_cast<float>(RitualAt + 5.2 - GetWorld()->GetTimeSeconds()));
}

void ADuelMode::WriteCheckpoint()
{
    if(NorthTowerActor){NorthTowerActor->Save();return;}
    if (!FApp::CanEverRender() || FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")))
    {
        return;
    }
    UDuelProgress* Save = NewObject<UDuelProgress>(this);
    Save->bAppeal = bAppeal;
    Save->bYukiCourt = bYukiCourt;
    Save->bFinished = Result == EDuelOutcome::Epilogue;
    Save->bCompanionFollowing = bCompanionFollowing;
    Save->Character = static_cast<uint8>(PlayerFighter->SelectedCharacter());
    const EDuelCharacter Boss = BossFighter->SelectedCharacter();
    Save->FirstBoss = static_cast<uint8>(bAppeal ? FirstBoss : Boss);
    Save->Evidence = static_cast<uint8>(Decision);
    Save->Right = static_cast<uint8>(PlayerFighter->Equipped);
    Save->Left = static_cast<uint8>(PlayerFighter->Offhand);
    Save->RightReserve = static_cast<uint8>(PlayerFighter->RightReserve);
    Save->LeftReserve = static_cast<uint8>(PlayerFighter->LeftReserve);
    Save->Art = static_cast<uint8>(PlayerFighter->ArtSelection);
    Save->Spell = static_cast<uint8>(PlayerFighter->SelectedSpell());
    for (const EDuelArt Spell : PlayerFighter->PreparedSpells)
    {
        Save->StaffSpells.Add(static_cast<uint8>(Spell));
    }
    Save->ActiveSpellSlot = static_cast<uint8>(PlayerFighter->SpellSlot());
    Save->BloodFlasks = PlayerFighter->HealthFlaskAllocation;
    Save->FlaskBudget = DuelCourt::TotalFlasks;
    Save->FirstFight = FirstDuelSeconds;
    Save->SecondFight = SecondDuelSeconds;
    Save->FinalFight = Save->bFinished ? CompletedDuelSeconds : 0;
    if (bCastleRoute)
    {
        Save->CastleFormat = 1;
        Save->CastleKeys = CastleKeys;
        Save->CastleClues = CastleClues;
        Save->CastleSeals = CastleSeals;
        Save->CastleShortcuts = CastleShortcuts;
        Save->CastleWins = CastleWins;
        Save->VerdictScars = VerdictScars;
        Save->bEastOpen = bEastOpen;
        Save->bFinalRecord = bFinalRecord;
        Save->FinalFight = bFinalRecord ? CompletedDuelSeconds : 0;
    }
    if (!UGameplayStatics::SaveGameToSlot(Save, CheckpointSlot, 0))
    {
        UE_LOG(LogTemp, Error, TEXT("Could not save the witch trial checkpoint"));
    }
}

void ADuelMode::LoadCheckpoint()
{
    if(NorthTowerActor){NorthTowerActor->LoadCheckpoint(*this);return;}
    const UDuelProgress* Save = Cast<UDuelProgress>(UGameplayStatics::LoadGameFromSlot(CheckpointSlot, 0));
    if (!Save)
    {
        ReturnToCheckpoint();
        return;
    }
    const EDuelCharacter PlayerCharacter = Save->Character == 1 ? EDuelCharacter::Ema : EDuelCharacter::Hiro;
    PlayerFighter->SelectCharacter(PlayerCharacter);
    const auto Equip = [PlayerCharacter](uint8 Value, EDuelHand Hand, EDuelWeapon Fallback)
    {
        const EDuelWeapon Gear = static_cast<EDuelWeapon>(Value);
        return DuelCourt::CanEquip(Gear, PlayerCharacter, Hand) ? Gear : Fallback;
    };
    PlayerFighter->SetWeapon(Equip(Save->Right, EDuelHand::Right, EDuelWeapon::Rapier));
    PlayerFighter->SetWeapon(Equip(Save->Left, EDuelHand::Left, EDuelWeapon::Buckler), EDuelHand::Left);
    PlayerFighter->RightReserve = Equip(Save->RightReserve, EDuelHand::Right, EDuelWeapon::Greatblade);
    PlayerFighter->LeftReserve = Equip(Save->LeftReserve, EDuelHand::Left, EDuelWeapon::Spear);
    const EDuelArt Art = static_cast<EDuelArt>(Save->Art);
    PlayerFighter->ArtSelection = DuelCourt::SupportsArt(Art, PlayerCharacter) ? Art : EDuelArt::Default;
    TArray<EDuelArt, TInlineAllocator<DuelCourt::SpellSlotCount>> ValidSpells;
    const auto StaffSpells = MakeArrayView(DuelCourt::StaffSpells);
    for (uint8 Value : Save->StaffSpells)
    {
        const EDuelArt Spell = static_cast<EDuelArt>(Value);
        if (StaffSpells.Contains(Spell))
        {
            ValidSpells.AddUnique(Spell);
        }
    }
    const bool bHasStaffSlots = Save->StaffSpells.Num() == DuelCourt::SpellSlotCount &&
        ValidSpells.Num() == DuelCourt::SpellSlotCount;
    if (!bHasStaffSlots)
    {
        ValidSpells.Reset();
        const EDuelArt LegacySpell = static_cast<EDuelArt>(Save->Spell);
        if (StaffSpells.Contains(LegacySpell))
        {
            ValidSpells.Add(LegacySpell);
        }
        for (const EDuelArt Spell : StaffSpells.Left(DuelCourt::SpellSlotCount))
        {
            ValidSpells.AddUnique(Spell);
        }
    }
    for (int32 Slot = 0; Slot < DuelCourt::SpellSlotCount; ++Slot)
    {
        PlayerFighter->PreparedSpells[Slot] = ValidSpells[Slot];
    }
    PlayerFighter->ActiveSpellSlot = bHasStaffSlots && Save->ActiveSpellSlot < DuelCourt::SpellSlotCount ? Save->ActiveSpellSlot : 0;
    constexpr int32 LegacyDefaultHealthFlasks = 3;
    const bool bLegacyDefault = (Save->FlaskBudget == 0 && Save->BloodFlasks == LegacyDefaultHealthFlasks) ||
        (Save->FlaskBudget == 7 && Save->BloodFlasks == 4);
    PlayerFighter->HealthFlaskAllocation = bLegacyDefault ? DuelCourt::DefaultHealthFlasks :
        FMath::Clamp(Save->BloodFlasks, 0, DuelCourt::TotalFlasks);
    if (bCastleRoute)
    {
        CastleKeys = Save->CastleKeys & DuelCastle::Whole;
        CastleClues = Save->CastleClues & 63;
        CastleSeals = Save->CastleSeals & DuelCastle::Whole;
        CastleShortcuts = Save->CastleShortcuts & DuelCastle::Whole;
        VerdictScars = Save->VerdictScars;
        if (!Save->CastleFormat && Save->bYukiCourt)
        {
            CastleKeys = CastleSeals = CastleShortcuts = DuelCastle::Whole;
            CastleClues = 63;
        }
        else if (!Save->CastleFormat && Save->bAppeal)
        {
            CastleKeys |= DuelCastle::WingBit(1);
            CastleSeals |= DuelCastle::WingBit(0);
            CastleClues |= DuelCastle::ClueMask(0);
        }
        CastleWins = (Save->CastleWins & DuelCastle::Whole) | CastleSeals;
        bEastOpen = CastleSeals == DuelCastle::Whole && (Save->bEastOpen || Save->bYukiCourt);
        bFinalRecord = bEastOpen && (Save->bFinalRecord || Save->bFinished);
        for (int32 Index = 0; Index < DuelCourt::SpellSlotCount; ++Index)
        {
            if (!KnowsSpell(PlayerFighter->PreparedSpells[Index]))
            {
                for (const EDuelArt Spell : StaffSpells.Left(DuelCourt::SpellSlotCount))
                {
                    if (!MakeArrayView(PlayerFighter->PreparedSpells.GetData(), DuelCourt::SpellSlotCount).Contains(Spell))
                    {
                        PlayerFighter->PreparedSpells[Index] = Spell;
                        break;
                    }
                }
            }
        }
        FirstDuelSeconds = FMath::Max(0, Save->FirstFight);
        SecondDuelSeconds = FMath::Max(0, Save->SecondFight);
        ReturnToCastleHub();
        if (bFinalRecord)
        {
            SetCastleCourt(DuelCastle::WingCount);
            PlayerFighter->SetActorLocation(DuelCourt::YukiOrigin + FVector(2065, 0, 62));
            CompletedDuelSeconds = FMath::Max(0, Save->FinalFight);
            Result = EDuelOutcome::Victory;
            Arena->SetDawn(true);
            if (Save->bFinished)
            {
                PlayerFighter->SetActorLocation(DuelCourt::YukiOrigin + FVector(2590, 0, 62));
                FinishTrial(Save->bCompanionFollowing);
            }
        }
        return;
    }
    bWarrantFound = bPrisonGateOpen = bAntechamberReached = true;
    Arena->ShowWarrant(false);
    Arena->OpenPrisonGate();
    bAppeal = bAppealRested = Save->bAppeal || Save->bYukiCourt;
    bYukiCourt = Save->bYukiCourt;
    Decision = bAppeal ? (Save->Evidence == static_cast<uint8>(EDuelEnding::CallWitness) ? EDuelEnding::CallWitness : EDuelEnding::SubmitReceipt)
        : EDuelEnding::Undecided;
    FirstDuelSeconds = FMath::Max(0, Save->FirstFight);
    SecondDuelSeconds = FMath::Max(0, Save->SecondFight);
    FirstBoss = Save->FirstBoss == 1 ? EDuelCharacter::Ema : EDuelCharacter::Hiro;
    Arena->SetAppeal(bAppeal);
    Arena->SetYukiCourt(bYukiCourt);
    Arena->SetYukiRouteOpen(bYukiCourt);
    const EDuelCharacter Other = FirstBoss == EDuelCharacter::Ema ? EDuelCharacter::Hiro : EDuelCharacter::Ema;
    SetBoss(bYukiCourt ? EDuelCharacter::Yuki : bAppeal ? Other : FirstBoss);
    ReturnToCheckpoint();
    if (bYukiCourt && Save->bFinished)
    {
        CompletedDuelSeconds = FMath::Max(0, Save->FinalFight);
        BossFighter->SetActorHiddenInGame(true);
        BossFighter->SetActorEnableCollision(false);
        PlayerFighter->SetActorLocation(DuelCourt::YukiOrigin + FVector(2590, 0, 62));
        FinishTrial(Save->bCompanionFollowing);
    }
}

void ADuelMode::TogglePause()
{
    if (Result == EDuelOutcome::Title)
    {
        return;
    }
    if (NorthTowerActor && NorthTowerActor->CloseMap())
    {
        RefreshMenuPointer();
        return;
    }
    const bool bPaused = UGameplayStatics::IsGamePaused(this);
    if (!bPaused && NorthTowerActor && NorthTowerActor->SkipPendingDialogue()) return;
    if (bPaused && (bControls || bLoadout || bGraphics || bInventory))
    {
        if (bLoadout && LoadoutCandidate != INDEX_NONE)
        {
            LoadoutCandidate = INDEX_NONE;
            return;
        }
        bControls = false;
        bLoadout = false;
        bGraphics = false;
        bInventory = false;
        return;
    }
    if (!bPaused)
    {
        MenuSelection = 0;
    }
    UGameplayStatics::SetGamePaused(this, !bPaused);
    if (Arena)
    {
        Arena->PauseVoice(!bPaused);
    }
    RefreshMenuPointer();
}

EDuelMenuPage ADuelMode::MenuPage() const
{
    if (Result == EDuelOutcome::Title) return EDuelMenuPage::Title;
    if (UGameplayStatics::IsGamePaused(this))
    {
        if (bControls) return EDuelMenuPage::Controls;
        if (bLoadout) return LoadoutCandidate == INDEX_NONE ? EDuelMenuPage::Equipment : EDuelMenuPage::EquipmentChoices;
        if (bGraphics) return EDuelMenuPage::Graphics;
        if (bInventory) return EDuelMenuPage::Inventory;
        return EDuelMenuPage::Pause;
    }
    return NorthTowerActor && NorthTowerActor->MapVisible() ? EDuelMenuPage::Map : EDuelMenuPage::None;
}

bool ADuelMode::BlocksMouseLook() const
{
    return MenuPage() != EDuelMenuPage::None || PointerClosedFrame == GFrameCounter;
}

void ADuelMode::RefreshMenuPointer()
{
    const EDuelMenuPage Page = MenuPage();
    if (Page != EDuelMenuPage::Map) BrowsedMapFloor = INDEX_NONE;

    const bool bShow = Page != EDuelMenuPage::None;
    // The action that closes a menu can be dispatched before its queued mouse axes.
    if (bMenuPointerOwned && !bShow) PointerClosedFrame = GFrameCounter;
    bMenuPointerOwned = bShow;
    APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    if (!Controller || Controller->bShowMouseCursor == bShow) return;

    if (bShow)
    {
        if (PlayerFighter)
        {
            PlayerFighter->ForwardAxis = PlayerFighter->RightAxis = 0.f;
            PlayerFighter->ShiftPressedAt = -1.0;
            PlayerFighter->bShiftCommittedToSprint = PlayerFighter->bSprintExhausted = false;
            PlayerFighter->ClearHandPresses();
            if (Page == EDuelMenuPage::Map)
            {
                PlayerFighter->ConsumeMovementInputVector();
                PlayerFighter->GetCharacterMovement()->StopMovementImmediately();
            }
        }
        FInputModeGameAndUI Input;
        Input.SetHideCursorDuringCapture(false);
        Input.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        Controller->SetInputMode(Input);
    }
    else
    {
        Controller->SetInputMode(FInputModeGameOnly());
    }
    Controller->SetShowMouseCursor(bShow);
}

bool ADuelMode::HandlePrimaryMenuPress()
{
    if (MenuPage() == EDuelMenuPage::None) return false;
    if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
    {
        if (ADuelHUD* HUD = Cast<ADuelHUD>(Controller->GetHUD())) HUD->ClickMenu(*this);
    }
    return true;
}

void ADuelMode::ActivateMenuChoice(int32 Choice)
{
    const EDuelMenuPage Page = MenuPage();
    if (Choice == INDEX_NONE)
    {
        TogglePause();
        return;
    }
    switch (Page)
    {
    case EDuelMenuPage::Title:
        if (UsesNorthTower() && (Choice == 3 || Choice == 4))
        {
            MenuSelection = 1;
            AdjustLoadout(Choice == 3 ? -1 : 1);
        }
        else if (Choice >= 0 && Choice < 3)
        {
            MenuSelection = Choice;
            Confirm();
        }
        break;
    case EDuelMenuPage::Pause:
        if (Choice >= 0 && Choice < PauseOptionCount())
        {
            MenuSelection = Choice;
            Confirm();
        }
        break;
    case EDuelMenuPage::Graphics:
        if (Choice >= 0 && Choice < 2 * int32(EDuelGraphicsRow::Count))
        {
            GraphicsRow = Choice % int32(EDuelGraphicsRow::Count);
            AdjustLoadout(Choice < int32(EDuelGraphicsRow::Count) ? 1 : -1);
        }
        break;
    case EDuelMenuPage::Equipment:
        if (Choice >= 0 && Choice < LoadoutRows)
        {
            SelectLoadoutRow(Choice);
            Confirm();
        }
        break;
    case EDuelMenuPage::EquipmentChoices:
        ChooseLoadoutValue(Choice);
        break;
    case EDuelMenuPage::Inventory:
        if (Choice == -2 || Choice == -3)
        {
            AdjustLoadout(Choice == -2 ? -1 : 1);
        }
        else if (InventoryChoices().Contains(Choice))
        {
            InventorySelection = Choice;
            Confirm();
        }
        break;
    case EDuelMenuPage::Map:
        if (Choice == ADuelNorthTower::TravelMenuConfirm) NorthTowerActor->ConfirmTravel(*this);
        else if ((Choice >= ADuelNorthTower::TravelMenuBase && Choice < ADuelNorthTower::TravelMenuBase + 5) ||
            (Choice >= ADuelNorthTower::TravelIconMenuBase && Choice < ADuelNorthTower::TravelIconMenuBase + 5))
        {
            const int32 Base = Choice >= ADuelNorthTower::TravelIconMenuBase ? ADuelNorthTower::TravelIconMenuBase : ADuelNorthTower::TravelMenuBase;
            const auto Floor = static_cast<DuelNorthLayout::EFloor>(Choice - Base);
            if (NorthTowerActor->SelectTravelPoint(Floor)) BrowsedMapFloor = static_cast<int32>(Floor);
        }
        else if (Choice >= 0 && Choice < static_cast<int32>(DuelNorthLayout::EFloor::Unknown) &&
            NorthTowerActor->HasExploredFloor(static_cast<DuelNorthLayout::EFloor>(Choice)))
        {
            BrowsedMapFloor = Choice;
            NorthTowerActor->ClearTravelSelection();
        }
        break;
    default:
        break;
    }
}

void ADuelMode::SelectLoadoutRow(int32 Row)
{
    if (bLoadout && Row >= 0 && Row < LoadoutRows)
    {
        LoadoutCandidate = INDEX_NONE;
        LoadoutRow = Row;
    }
}

int32 ADuelMode::EquippedLoadoutValue(int32 Row) const
{
    if (!PlayerFighter) return INDEX_NONE;
    if (Row < ArtLoadoutRow)
    {
        const EDuelHand Hand = Row % 2 == 0 ? EDuelHand::Right : EDuelHand::Left;
        return static_cast<int32>(Row >= 2 ? PlayerFighter->ReserveWeapon(Hand) : PlayerFighter->EquippedWeapon(Hand));
    }
    if (Row == ArtLoadoutRow) return static_cast<int32>(PlayerFighter->SelectedArt());
    if (Row == FlaskLoadoutRow) return PlayerFighter->FlaskAllocation();
    return static_cast<int32>(PlayerFighter->SpellAt(Row - FirstSpellRow));
}

TArray<int32> ADuelMode::LoadoutChoices() const
{
    TArray<int32> Values;
    if (!PlayerFighter) return Values;
    const EDuelCharacter Character = PlayerFighter->SelectedCharacter();
    if (LoadoutRow < ArtLoadoutRow)
    {
        const EDuelHand Hand = LoadoutRow % 2 == 0 ? EDuelHand::Right : EDuelHand::Left;
        const int32 OtherSlot = EquippedLoadoutValue(LoadoutRow < 2 ? LoadoutRow + 2 : LoadoutRow - 2);
        for (int32 I = 0; I <= static_cast<int32>(EDuelWeapon::RitualSword); ++I)
        {
            const auto Gear = static_cast<EDuelWeapon>(I);
            if ((I != OtherSlot || (Gear == EDuelWeapon::Empty && UsesExpeditionLoot())) &&
                PlayerFighter->CanEquipOwnedWeapon(Gear, Hand)) Values.Add(I);
        }
    }
    else if (LoadoutRow == ArtLoadoutRow)
    {
        for (int32 I = 0; I <= static_cast<int32>(EDuelArt::WitnessStep); ++I)
        {
            if (DuelCourt::SupportsArt(static_cast<EDuelArt>(I), Character)) Values.Add(I);
        }
    }
    else if (LoadoutRow == FlaskLoadoutRow)
    {
        for (int32 I = 0; I <= DuelCourt::TotalFlasks; ++I) Values.Add(I);
    }
    else
    {
        if (UsesExpeditionLoot()) Values.Add(static_cast<int32>(EDuelArt::Default));
        for (EDuelArt Spell : DuelCourt::StaffSpells)
        {
            if (KnowsSpell(Spell)) Values.Add(static_cast<int32>(Spell));
        }
    }
    return Values;
}

void ADuelMode::ChooseLoadoutValue(int32 Value)
{
    if (MenuPage() != EDuelMenuPage::EquipmentChoices || !LoadoutChoices().Contains(Value)) return;
    if (LoadoutRow < ArtLoadoutRow)
    {
        const EDuelHand Hand = LoadoutRow % 2 == 0 ? EDuelHand::Right : EDuelHand::Left;
        const auto Gear = static_cast<EDuelWeapon>(Value);
        if (LoadoutRow < 2) PlayerFighter->SetWeapon(Gear, Hand);
        else if (Hand == EDuelHand::Right) PlayerFighter->RightReserve = Gear;
        else PlayerFighter->LeftReserve = Gear;
    }
    else if (LoadoutRow == ArtLoadoutRow)
    {
        PlayerFighter->ArtSelection = static_cast<EDuelArt>(Value);
    }
    else if (LoadoutRow == FlaskLoadoutRow)
    {
        SetFlaskAllocation(Value);
    }
    else
    {
        PlayerFighter->SetSpellSlot(LoadoutRow - FirstSpellRow, static_cast<EDuelArt>(Value));
    }
    LoadoutCandidate = INDEX_NONE;
}

TArray<int32> ADuelMode::InventoryChoices() const
{
    TArray<int32> Choices{0, 1};
    if (NorthTowerActor)
    {
        for (int32 I = 0; I < static_cast<int32>(EDuelNorthRelic::Count); ++I)
        {
            if (NorthTowerActor->HasRelic(static_cast<EDuelNorthRelic>(I))) Choices.Add(I + 2);
        }
    }
    return Choices;
}

void ADuelMode::SetFlaskAllocation(int32 Value)
{
    ADuelFighter& Fighter = *PlayerFighter;
    const int32 Old = Fighter.HealthFlaskAllocation;
    if (Value == Old) return;
    const int32 Remaining = FMath::Clamp(Fighter.Flasks + Fighter.FocusFlasks, 0, DuelCourt::TotalFlasks);
    const int32 BlueCapacity = DuelCourt::TotalFlasks - Value;
    // Reallocate the remaining doses and preserve the number already consumed.
    Fighter.Flasks = FMath::Clamp(Fighter.Flasks + Value - Old,
        FMath::Max(0, Remaining - BlueCapacity), FMath::Min(Value, Remaining));
    Fighter.FocusFlasks = Remaining - Fighter.Flasks;
    Fighter.HealthFlaskAllocation = Value;
}

void ADuelMode::NavigateLoadoutChoices(int32 Step, bool bVertical)
{
    const auto Values = LoadoutChoices();
    int32 Index = Values.IndexOfByKey(LoadoutCandidate);
    if (Index == INDEX_NONE) return;
    if (bVertical)
    {
        const int32 Rows = FMath::DivideAndRoundUp(Values.Num(), LoadoutColumns);
        const int32 NextRow = (Index / LoadoutColumns + Step + Rows) % Rows;
        Index = FMath::Min(Values.Num() - 1, NextRow * LoadoutColumns + Index % LoadoutColumns);
    }
    else
    {
        Index = (Index + Step + Values.Num()) % Values.Num();
    }
    LoadoutCandidate = Values[Index];
}

DuelNorthLayout::EFloor ADuelMode::ViewedMapFloor() const
{
    return BrowsedMapFloor != INDEX_NONE ? static_cast<DuelNorthLayout::EFloor>(BrowsedMapFloor) : NorthTowerActor->CurrentFloor();
}

void ADuelMode::NavigateMapFloors(int32 Step)
{
    constexpr int32 Count = static_cast<int32>(DuelNorthLayout::EFloor::Unknown);
    int32 Floor = MakeArrayView(DuelNorthLayout::MapOrder).Find(ViewedMapFloor());
    for (int32 I = 0; I < Count; ++I)
    {
        Floor = (Floor + Step + Count) % Count;
        if (NorthTowerActor->HasExploredFloor(DuelNorthLayout::MapOrder[Floor]))
        {
            BrowsedMapFloor = static_cast<int32>(DuelNorthLayout::MapOrder[Floor]);
            NorthTowerActor->ClearTravelSelection();
            return;
        }
    }
}

int32 ADuelMode::PauseOptionCount() const
{
    if (NorthTowerActor)
    {
        return NorthTowerActor->CanChoosePlayer() && Result == EDuelOutcome::Ready ? 10 : 9;
    }
    if (Result == EDuelOutcome::Epilogue)
    {
        return 7;
    }
    return Result == EDuelOutcome::Ready ? (bCastleRoute || bAppeal ? 10 : 11) : 8;
}

EDuelPause ADuelMode::PauseOption(int32 Index) const
{
    if (Index == 1) return EDuelPause::Inventory;
    if (Index > 1) --Index;
    if(NorthTowerActor)
    {
        const EDuelPause NorthOptions[] = {EDuelPause::Resume, EDuelPause::Player, EDuelPause::Loadout, EDuelPause::Controls,
            EDuelPause::Graphics, EDuelPause::Checkpoint, EDuelPause::Subtitles, EDuelPause::Quit, EDuelPause::Title};
        const bool Choose=NorthTowerActor->CanChoosePlayer() && Result==EDuelOutcome::Ready;
        return NorthOptions[!Choose && Index>=1?Index+1:Index];
    }
    const int32 Count = PauseOptionCount() - 1;
    if (Index == Count - 1)
    {
        return EDuelPause::Title;
    }
    if (Index == Count - 2)
    {
        return EDuelPause::Graphics;
    }
    const EDuelPause Options[] = {EDuelPause::Resume, EDuelPause::Player, EDuelPause::Boss, EDuelPause::Loadout, EDuelPause::Controls,
        EDuelPause::Checkpoint, EDuelPause::Quit, EDuelPause::Subtitles};
    if (Result != EDuelOutcome::Ready && Index > 0)
    {
        return Options[Index + (Result == EDuelOutcome::Epilogue && Index >= 2 ? 4 : 3)];
    }
    return Options[(bCastleRoute || bAppeal) && Index >= 2 ? Index + 1 : Index];
}

void ADuelMode::Navigate(int32 Step)
{
    if (Result == EDuelOutcome::Title)
    {
        do
        {
            MenuSelection = (MenuSelection + 3 + Step) % 3;
        } while (MenuSelection == 0 && !CanContinue());
        return;
    }
    if (MenuPage() == EDuelMenuPage::Map)
    {
        NorthTowerActor->NavigateTravelPoints(*this, Step);
        return;
    }
    if (bControls)
    {
        return;
    }
    if (bGraphics)
    {
        constexpr int32 Count = static_cast<int32>(EDuelGraphicsRow::Count);
        GraphicsRow = (GraphicsRow + Step + Count) % Count;
        return;
    }
    if (bInventory)
    {
        const TArray<int32> Choices = InventoryChoices();
        const int32 Index = FMath::Max(0, Choices.IndexOfByKey(InventorySelection));
        InventorySelection = Choices[(Index + Step + Choices.Num()) % Choices.Num()];
        return;
    }
    if (bLoadout)
    {
        if (LoadoutCandidate == INDEX_NONE) SelectLoadoutRow((LoadoutRow + Step + LoadoutRows) % LoadoutRows);
        else NavigateLoadoutChoices(Step, true);
        return;
    }
    const int32 Count = UGameplayStatics::IsGamePaused(this) ? PauseOptionCount() :
        (Result == EDuelOutcome::Archive && !bAppeal && bReadingArchive && Decision == EDuelEnding::Undecided ? 2 : 0);
    if (Count > 0)
    {
        MenuSelection = (MenuSelection + Count + Step) % Count;
    }
}

void ADuelMode::AdjustLoadout(int32 Step)
{
    if (Result == EDuelOutcome::Title && UsesNorthTower() && MenuSelection == 1 && PlayerFighter)
    {
        PlayerFighter->SelectCharacter(Step < 0 ? EDuelCharacter::Ema : EDuelCharacter::Hiro);
        return;
    }
    if (MenuPage() == EDuelMenuPage::Map)
    {
        NavigateMapFloors(Step);
        return;
    }
    if (bGraphics && UGameplayStatics::IsGamePaused(this))
    {
        GraphicsSettings.Adjust(static_cast<EDuelGraphicsRow>(GraphicsRow), Step);
        if (Arena) Arena->RefreshAudioLevels();
        return;
    }
    if (bInventory && UGameplayStatics::IsGamePaused(this))
    {
        if (PlayerFighter && InventorySelection < 2)
        {
            const int32 Delta = InventorySelection == 0 ? Step : -Step;
            SetFlaskAllocation(FMath::Clamp(PlayerFighter->FlaskAllocation() + Delta, 0, DuelCourt::TotalFlasks));
        }
        return;
    }
    if (!bLoadout || !PlayerFighter || !UGameplayStatics::IsGamePaused(this))
    {
        return;
    }
    if (LoadoutCandidate != INDEX_NONE)
    {
        NavigateLoadoutChoices(Step, false);
    }
    else if (LoadoutRow == ArtLoadoutRow)
    {
        PlayerFighter->ArtSelection = DuelCourt::NextArt(PlayerFighter->ArtSelection, PlayerFighter->SelectedCharacter(), Step);
    }
    else if (LoadoutRow >= FirstSpellRow && LoadoutRow < FlaskLoadoutRow)
    {
        PlayerFighter->AdjustSpellSlot(LoadoutRow - FirstSpellRow, Step);
    }
    else if (LoadoutRow == FlaskLoadoutRow)
    {
        constexpr int32 Options = DuelCourt::TotalFlasks + 1;
        SetFlaskAllocation((PlayerFighter->HealthFlaskAllocation + Step + Options) % Options);
    }
    else
    {
        const EDuelHand Hand = LoadoutRow % 2 == 0 ? EDuelHand::Right : EDuelHand::Left;
        PlayerFighter->AdjustWeaponSlot(Hand, LoadoutRow >= 2, Step);
    }
}

void ADuelMode::Confirm()
{
    if (Result == EDuelOutcome::Title)
    {
        if (MenuSelection == 0 && CanContinue())
        {
            LoadCheckpoint();
        }
        else if (MenuSelection == 1)
        {
            if(NorthTowerActor)NorthTowerActor->NewGame(*this);else ReturnToCheckpoint();
        }
        else if (MenuSelection == 2)
        {
            UKismetSystemLibrary::QuitGame(this, GetWorld()->GetFirstPlayerController(), EQuitPreference::Quit, false);
        }
        RefreshMenuPointer();
        return;
    }
    if (UGameplayStatics::IsGamePaused(this))
    {
        if (bControls)
        {
            bControls = false;
            return;
        }
        if (bInventory)
        {
            if (PlayerFighter && InventorySelection < 2)
            {
                PlayerFighter->Item = static_cast<EDuelItem>(InventorySelection);
            }
            return;
        }
        if (bLoadout)
        {
            if (LoadoutCandidate != INDEX_NONE)
            {
                ChooseLoadoutValue(LoadoutCandidate);
            }
            else
            {
                const auto Values = LoadoutChoices();
                const int32 Equipped = EquippedLoadoutValue(LoadoutRow);
                if (!Values.IsEmpty()) LoadoutCandidate = Values.Contains(Equipped) ? Equipped : Values[0];
            }
            return;
        }
        if (bGraphics)
        {
            AdjustLoadout(1);
            return;
        }
        switch (PauseOption(MenuSelection))
        {
        case EDuelPause::Resume:
            TogglePause();
            break;
        case EDuelPause::Player:
        case EDuelPause::Boss:
        {
            if(NorthTowerActor){NorthTowerActor->ChangePlayer(*this);break;}
            ADuelFighter* Fighter = PauseOption(MenuSelection) == EDuelPause::Player ? PlayerFighter.Get() : BossFighter.Get();
            if (Fighter)
            {
                Fighter->SelectCharacter(Fighter->SelectedCharacter() == EDuelCharacter::Hiro ? EDuelCharacter::Ema : EDuelCharacter::Hiro);
            }
            break;
        }
        case EDuelPause::Checkpoint:
            ReturnToCheckpoint();
            break;
        case EDuelPause::Controls:
            bControls = true;
            break;
        case EDuelPause::Graphics:
            bGraphics = true;
            GraphicsRow = 0;
            break;
        case EDuelPause::Subtitles:
            bEffortSubtitles = !bEffortSubtitles;
            break;
        case EDuelPause::Loadout:
            bLoadout = true;
            LoadoutRow = 0;
            LoadoutCandidate = INDEX_NONE;
            break;
        case EDuelPause::Inventory:
            bInventory = true;
            InventorySelection = static_cast<int32>(PlayerFighter->EquippedItem());
            break;
        case EDuelPause::Title:
            if(NorthTowerActor)NorthTowerActor->Save();
            UGameplayStatics::SetGamePaused(this, false);
            UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/Maps/RainCourt")));
            break;
        case EDuelPause::Quit:
            if(NorthTowerActor)NorthTowerActor->Save();
            UKismetSystemLibrary::QuitGame(this, GetWorld()->GetFirstPlayerController(), EQuitPreference::Quit, false);
            break;
        }
        RefreshMenuPointer();
    }
    else if (NorthTowerActor) { NorthTowerActor->Confirm(*this); }
    else if (bCastleRoute && (Result == EDuelOutcome::Ready || Result == EDuelOutcome::Victory) && FigureView == 0)
    {
        ConfirmCastle();
    }
    else if (Result == EDuelOutcome::Ready)
    {
        if (FigureView != 0)
        {
            CloseCharacterView();
        }
        else if (NearWarrant())
        {
            bWarrantFound = true;
            Arena->ShowWarrant(false);
            Arena->Sound(EDuelSound::Flask, DuelCourt::Warrant);
        }
        else if (NearPrisonGate() && bWarrantFound)
        {
            bPrisonGateOpen = true;
            Arena->OpenPrisonGate();
            Arena->Sound(EDuelSound::RainAtlas, FVector(DuelCourt::PrisonGateX, 0, 120));
        }
        else if (NearRestPoint())
        {
            bAntechamberReached |= !bAppeal;
            bAppealRested |= bAppeal;
            PlayerFighter->RestAtCheckpoint();
            WriteCheckpoint();
            Arena->Sound(EDuelSound::Flask, PlayerFighter->GetActorLocation());
        }
        else
        {
            BeginEncounter();
        }
    }
    else if (Result == EDuelOutcome::Archive)
    {
        if (NearAppealPassage())
        {
            Result = EDuelOutcome::Passage;
            Arena->Sound(EDuelSound::RainAtlas, DuelCourt::AppealPassage);
        }
        else if (bReadingArchive)
        {
            if (!bAppeal && Decision == EDuelEnding::Undecided)
            {
                ChooseEnding(MenuSelection == 0 ? EDuelEnding::SubmitReceipt : EDuelEnding::CallWitness);
            }
            else
            {
                bReadingArchive = false;
            }
        }
        else if (RitualMark() != INDEX_NONE)
        {
            const FVector Mark = Arena->CourtOrigin() + DuelCourt::RitualMarks[RitualSeal];
            ++RitualSeal;
            RitualAt = GetWorld()->GetTimeSeconds();
            Arena->RitualTrace(Mark, RitualComplete());
            if (RitualComplete())
            {
                Arena->SetYukiRouteOpen(true);
            }
        }
        else if (NearArchiveDesk() && (!bAppeal || RitualComplete()))
        {
            bReadingArchive = true;
            MenuSelection = 0;
            PlayerFighter->EndCombat();
        }
    }
    else if (Result == EDuelOutcome::Epilogue)
    {
        if (bReadingArchive)
        {
            if (++MenuSelection > 2)
            {
                bReadingArchive = false;
                bCompanionFollowing = true;
                WriteCheckpoint();
            }
        }
        else if (NearGardenExit())
        {
            MenuSelection = 0;
            bReadingArchive = true;
        }
    }
    else if (Result == EDuelOutcome::Defeat)
    {
        ReturnToCheckpoint();
    }
}

void ADuelMode::ReturnToCheckpoint()
{
    if (NorthTowerActor) { NorthTowerActor->Respawn(*this); return; }
    if (!PlayerFighter || !BossFighter || !Arena)
    {
        return;
    }
    if (Result == EDuelOutcome::Epilogue)
    {
        return;
    }
    if (bCastleRoute)
    {
        ReturnToCastleHub();
        return;
    }
    if (bAppeal && !bAppealRested)
    {
        bAppealRested = true;
    }
    ResetEncounter();
    Result = EDuelOutcome::Ready;
    Arena->SetGateClosed(true);
    Arena->SetCombatCamera(false);
    const FTransform Spawn = bYukiCourt ? DuelCourt::YukiCheckpoint() : bAppeal ? DuelCourt::AppealCheckpoint() :
        (bAntechamberReached ? DuelCourt::Checkpoint() : DuelCourt::LowerCheckpoint());
    PlayerFighter->SetActorLocationAndRotation(Spawn.GetLocation(), Spawn.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
    PlayerFighter->EndCombat();
    PlayerFighter->PrepareExploration();
    BossFighter->EndCombat();
}

void ADuelMode::ResetEncounter(bool bKeepPosition)
{
    if (PlayerFighter && BossFighter && Arena)
    {
        CloseCharacterView();
        BossFighter->ShowCharacterForm(false);
        BossFighter->SetActorHiddenInGame(false);
        BossFighter->SetActorEnableCollision(true);
        UGameplayStatics::SetGamePaused(this, false);
        Arena->PauseVoice(false);
        Arena->ClearCombatEffects();
        Arena->SetBattlePhase(1);
        Arena->SetGateClosed(false);
        Arena->SetCombatCamera(true);
        Arena->SetArchiveGateOpen(false);
        PlayerFighter->ResetFighter(bKeepPosition);
        BossFighter->ResetFighter();
        BossFighter->AppealIntro = bAppeal ? .82f : 0.f;
        if (!bAppeal)
        {
            Decision = EDuelEnding::Undecided;
        }
        SpokenPhase = 0;
        NorthWitchStartedAt = -1.0;
        SubtitleUntil = 0.f;
        MenuSelection = 0;
        bControls = false;
        bLoadout = false;
        bGraphics = false;
        bInventory = false;
        bReadingArchive = false;
        bGateOpen = false;
        RitualSeal = 0;
        RitualAt = 0.0;
        DuelStartedAt = GetWorld()->GetTimeSeconds();
        CompletedDuelSeconds = 0;
        Result = EDuelOutcome::Fighting;
        if (bKeepPosition)
        {
            if (bCastleRoute)
            {
                Arena->SetCastleFog(CastleDuel, true);
            }
            Arena->StartScore(BossFighter->SelectedCharacter());
            Arena->TryBossLine(BossFighter->SelectedCharacter(), EDuelVoice::Intro, BossFighter->GetActorLocation());
        }
    }
}

void ADuelMode::CycleCharacterView()
{
    if (NorthTowerActor) return;
    APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    if (Result != EDuelOutcome::Ready || bYukiCourt || !PlayerFighter || !BossFighter || !Controller)
    {
        return;
    }
    if (FigureView == 2)
    {
        CloseCharacterView();
        return;
    }
    const FVector Origin = Arena->CourtOrigin();
    const FVector Position = Origin + FVector(-695, 0, 180);
    if (!FigureCamera)
    {
        const FRotator Aim = (Origin + FVector(0, 0, 119) - Position).Rotation();
        FigureCamera = GetWorld()->SpawnActor<ACameraActor>(Position, Aim);
        if (!FigureCamera)
        {
            return;
        }
        FigureCamera->GetCameraComponent()->SetFieldOfView(54.f);
    }
    FigureCamera->SetActorLocation(Position);
    const bool bAlternate = FigureView == 1;
    if (!PlayerFighter->ShowCharacterForm(bAlternate) || !BossFighter->ShowCharacterForm(bAlternate))
    {
        PlayerFighter->ShowCharacterForm(false);
        BossFighter->ShowCharacterForm(false);
        return;
    }

    if (FigureView == 0)
    {
        ApproachBeforePreview = PlayerFighter->GetActorTransform();
    }
    PlayerFighter->EndCombat();
    BossFighter->EndCombat();
    PlayerFighter->SetActorLocationAndRotation(Origin + FVector(0, -155, 62), FRotator(0, 145, 0));
    BossFighter->SetActorLocationAndRotation(Origin + FVector(20, 159, 103), FRotator(0, -145, 0));
    PlayerFighter->SetCharacterPreview(true);
    BossFighter->SetCharacterPreview(true);
    Controller->SetViewTarget(FigureCamera);
    FigureView = bAlternate ? 2 : 1;
}

void ADuelMode::TurnCharacterView(float Degrees)
{
    if (FigureView != 0 && PlayerFighter && BossFighter)
    {
        const FRotator Turn(0, Degrees, 0);
        PlayerFighter->AddActorWorldRotation(Turn);
        BossFighter->AddActorWorldRotation(Turn);
    }
}

void ADuelMode::CloseCharacterView()
{
    if (FigureView == 0)
    {
        return;
    }
    FigureView = 0;
    for (ADuelFighter* Fighter : {PlayerFighter.Get(), BossFighter.Get()})
    {
        Fighter->ShowCharacterForm(false);
        Fighter->SetCharacterPreview(false);
        const FTransform Start = Fighter == BossFighter ? DuelCourt::Start(true, Arena->CourtOrigin()) : ApproachBeforePreview;
        Fighter->SetActorLocationAndRotation(Start.GetLocation(), Start.GetRotation());
    }
    if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
    {
        Controller->SetViewTarget(PlayerFighter);
    }
}

void ADuelMode::AnnouncePhase(int32 Phase)
{
    Arena->IntensifyScore(Phase);
    if (Phase == 2 && NorthTowerActor && NorthWitchStartedAt < 0.0)
    {
        const auto* Narrative = NorthTowerActor->Narrative();
        if (Narrative && Narrative->WitchPortrait(BossFighter->SelectedCharacter()))
            NorthWitchStartedAt = GetWorld()->GetTimeSeconds();
    }
    SpokenPhase = UsesNorthTower() ? 0 : Phase;
    SubtitleUntil = UsesNorthTower() ? 0.f : GetWorld()->GetTimeSeconds() + 6.1f;
    if (Phase == 2 && Arena->TryBossLine(BossFighter->SelectedCharacter(), EDuelVoice::Witch, BossFighter->GetActorLocation()))
    {
        SubtitleUntil = 0.f;
    }
    else if (Phase == 3)
    {
        Arena->StopVoice();
    }
    if (Phase == 3 && Arena)
    {
        Arena->Sound(EDuelSound::VerdictBell, Arena->CourtOrigin() + FVector(2050, 0, 540));
    }
}

UTexture* ADuelMode::NorthWitchPortrait(float& Opacity) const
{
    Opacity = 0.f;
    if (NorthWitchStartedAt < 0.0 || !NorthTowerActor || Result != EDuelOutcome::Fighting ||
        !BossFighter || !BossFighter->IsAlive() || BossFighter->Phase() != 2) return nullptr;
    const float Age = static_cast<float>(GetWorld()->GetTimeSeconds() - NorthWitchStartedAt);
    if (Age < 0.f || Age >= 2.10f) return nullptr;
    const auto* Narrative = NorthTowerActor->Narrative();
    if (UTexture* Portrait = Narrative ? Narrative->WitchPortrait(BossFighter->SelectedCharacter()) : nullptr)
    {
        Opacity = FMath::SmoothStep(0.f, .16f, Age) * (1.f - FMath::SmoothStep(1.66f, 2.10f, Age));
        return Portrait;
    }
    return nullptr;
}

bool ADuelMode::DialogueVisible() const
{
    return SpokenPhase > 0 && Result == EDuelOutcome::Fighting && GetWorld()->GetTimeSeconds() < SubtitleUntil;
}

const TCHAR* ADuelMode::CombatCaption() const
{
    if (UsesNorthTower())
    {
        const bool bCombat = Result == EDuelOutcome::Fighting || Result == EDuelOutcome::Defeat;
        return Arena && bCombat && (bEffortSubtitles || Arena->VoiceIsStory()) ? Arena->VoiceCaption() : nullptr;
    }
    if (Result == EDuelOutcome::Defeat)
    {
        if (const TCHAR* Line = Arena->VoiceCaption())
        {
            return Line;
        }
        if (GetWorld()->GetTimeSeconds() >= SubtitleUntil)
        {
            return nullptr;
        }
        switch (BossFighter->SelectedCharacter())
        {
        case EDuelCharacter::Hiro: return TEXT("我讨厌你的事实不会改变。永远不会。");
        case EDuelCharacter::Ema: return TEXT("怎、怎么会……！我怎么可能会撒谎……！");
        default: return TEXT("这一页还有空白。等你们自己来写吧。");
        }
    }
    if (DialogueVisible())
    {
        if (bYukiCourt)
        {
            return SpokenPhase == 2 ? TEXT("没有我的判决，一切都不算完。") :
                TEXT("让我们自己说，也让她把话说完。");
        }
        const ADuelFighter* Speaker = SpokenPhase == 2 ? BossFighter.Get() : PlayerFighter.Get();
        const bool bEma = Speaker->SelectedCharacter() == EDuelCharacter::Ema;
        if (SpokenPhase == 2)
        {
            if (bAppeal)
            {
                return Decision == EDuelEnding::CallWitness
                    ? TEXT("传唤名册上有她。让她自己说。") : TEXT("复核印还在。调出前一份记录。");
            }
            return bEma ? TEXT("这份证词上，怎么会是我的字？") :
                TEXT("如果那是我，你还是会拉住我吗？");
        }
        if (bAppeal)
        {
            return bEma ? TEXT("她的证词，必须完整地留下。") : TEXT("落印之前，把原件送上席。");
        }
        return bEma ? TEXT("把手给我。我们一起离开这里。") : TEXT("我认得那个眼神。让我去叫醒她。");
    }
    if (UsesCastle() && InYukiCourt() && Result == EDuelOutcome::Victory && Arena->VoiceIsStory())
    {
        return Arena->VoiceCaption();
    }
    return Result == EDuelOutcome::Fighting && (bEffortSubtitles || Arena->VoiceIsStory()) ? Arena->VoiceCaption() : nullptr;
}

FString ADuelMode::CaptionSpeaker() const
{
    const bool bBoss = DialogueVisible() ? SpokenPhase == 2 : Arena->VoiceIsBoss();
    const ADuelFighter* Speaker = bBoss ? BossFighter.Get() : PlayerFighter.Get();
    return FString::Printf(TEXT("%s  ·  %s"), Speaker->CharacterName(), bBoss ? TEXT("首领") : TEXT("主角"));
}

float ADuelMode::CaptionOpacity() const
{
    const bool bTimedFallback = Result == EDuelOutcome::Defeat && !Arena->VoiceCaption();
    return DialogueVisible() || bTimedFallback ? FMath::SmoothStep(0.f, .46f,
        static_cast<float>(SubtitleUntil - GetWorld()->GetTimeSeconds())) : Arena->VoiceCaptionOpacity();
}

void ADuelMode::ChooseEnding(EDuelEnding Choice)
{
    if (Result == EDuelOutcome::Archive && !bAppeal && bReadingArchive && Decision == EDuelEnding::Undecided && Choice != EDuelEnding::Undecided)
    {
        Decision = Choice;
        Arena->Sound(EDuelSound::RainAtlas, FVector(DuelCourt::ArchiveDeskX, 0, 170));
    }
}

void ADuelHUD::BeginPlay()
{
    Super::BeginPlay();
    if (FApp::CanEverRender())
    {
        const FString Path = FPaths::ConvertRelativePathToFull(
            FPaths::ProjectContentDir() / TEXT("DuelFonts/NotoSansCJKsc-Regular.otf"));
        if (FPaths::FileExists(Path))
        {
            HudFont = NewObject<UFont>(this);
            HudFont->FontCacheType = EFontCacheType::Runtime;
            HudFont->GetMutableInternalCompositeFont().DefaultTypeface.AppendFont(
                NAME_None, Path, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("Missing bundled HUD font: %s"), *Path);
        }
    }
    ChromeAtlas = LoadObject<UTexture2D>(nullptr, TEXT("/Game/DuelArt/UI/T_HudChrome.T_HudChrome"));
    InventoryAtlas = LoadObject<UTexture2D>(nullptr, TEXT("/Game/DuelArt/UI/T_NorthInventory.T_NorthInventory"));
}

void ADuelHUD::InventoryIcon(int32 Tile, float X, float Y, float Size, const FLinearColor& Tint)
{
    if (Tile == INDEX_NONE)
    {
        Chrome(13, X, Y, Size, Size, Tint);
        return;
    }
    const FTexture* Texture = InventoryAtlas ? InventoryAtlas->GetResource() : nullptr;
    if (!Texture || Tile < 0 || Tile >= 16) return;
    const FVector2D Origin((Tile % 4) * .25, (Tile / 4) * .25);
    const FVector2D Border(.001, .001);
    FCanvasTileItem Icon(FVector2D(X,Y), Texture, FVector2D(Size,Size), Origin+Border,
        Origin+FVector2D(.25,.25)-Border, Tint);
    Icon.BlendMode = SE_BLEND_Translucent;
    Canvas->DrawItem(Icon);
}

void ADuelHUD::Chrome(int32 Tile, float X, float Y, float W, float H, const FLinearColor& Tint)
{
    const FTexture* Texture = ChromeAtlas ? ChromeAtlas->GetResource() : nullptr;
    if (!Texture)
    {
        return;
    }
    const FVector2D UV((Tile % 4) * .25, (Tile / 4) * .125);
    FCanvasTileItem Item(FVector2D(X, Y), Texture, FVector2D(W, H), UV + FVector2D(.001, .0015),
        UV + FVector2D(.249, .1235), Tint);
    Item.BlendMode = SE_BLEND_Translucent;
    Canvas->DrawItem(Item);
}

void ADuelHUD::Panel(float X, float Y, float W, float H, float S)
{
    DrawRect(Ink, X, Y, W, H);
    Chrome(0, X, Y, W, H, FLinearColor(1.f, 1.f, 1.f, .28f));
    const float Rule = FMath::Max(1.f, S);
    FadeBand(X, Y, W, Rule, Bronze.CopyWithNewOpacity(.76f), Bronze.CopyWithNewOpacity(.13f));
    FadeBand(X, Y + H - Rule, W, Rule, Bronze.CopyWithNewOpacity(.13f), Bronze.CopyWithNewOpacity(.64f));
    const float Corner = FMath::Min(29.f * S, H * .40f);
    Chrome(1, X + 1.f, Y + 1.f, Corner, Corner, Bronze);
    DrawRect(Bronze.CopyWithNewOpacity(.24f), X + W - Rule, Y + H - Corner, Rule, Corner);
}

bool ADuelHUD::ReadMenuPointer(FVector2D& Position) const
{
    if (!PlayerOwner) return false;
    int32 ViewW = 0, ViewH = 0;
    PlayerOwner->GetViewportSize(ViewW, ViewH);
    if (ViewW <= 0 || ViewH <= 0) return false;
    FVector2D Pixel;
#if WITH_DEV_AUTOMATION_TESTS
    if (TestPointerPixel.IsSet()) Pixel = TestPointerPixel.GetValue();
    else
#endif
    if (!PlayerOwner->GetMousePosition(Pixel.X, Pixel.Y)) return false;
    if (Pixel.X < 0.0 || Pixel.Y < 0.0 || Pixel.X >= ViewW || Pixel.Y >= ViewH) return false;
    Position = Pixel * MenuCanvasSize / FVector2D(ViewW, ViewH);
    return true;
}

bool ADuelHUD::MenuButton(int32 Choice, float X, float Y, float W, float H)
{
    const FBox2D Bounds(FVector2D(X, Y), FVector2D(X + W, Y + H));
    MenuButtons.Emplace(Choice, Bounds);
    return bMenuPointerKnown && Bounds.IsInside(MenuPointer);
}

void ADuelHUD::ClickMenu(ADuelMode& Mode)
{
    const EDuelMenuPage Page = Mode.MenuPage();
    if (Page == EDuelMenuPage::None || Page != DrawnMenuPage ||
        (Page == EDuelMenuPage::EquipmentChoices && Mode.LoadoutChoice() != DrawnLoadoutRow)) return;
    FVector2D Pointer;
    if (!ReadMenuPointer(Pointer)) return;
    for (const auto& Button : MenuButtons)
    {
        if (Button.Value.IsInside(Pointer))
        {
            Mode.ActivateMenuChoice(Button.Key);
            return;
        }
    }
}

void ADuelHUD::DrawMenuBack(float X, float Y, float W, float H, float S, const TCHAR* Text)
{
    const bool bHover = MenuButton(INDEX_NONE, X, Y, W, H);
    DrawRect(FLinearColor(.057f, .043f, .039f, .88f), X, Y, W, H);
    if (bHover) FocusBand(X, Y, W, H, S);
    else DrawRect(Bronze.CopyWithNewOpacity(.43f), X, Y + H - FMath::Max(1.f, S), W, FMath::Max(1.f, S));
    Label(Text, X + W * .5f, Y + (H - 15.f * S) * .5f, 14.f * S, bHover ? Ember : Ivory, true);
}

void ADuelHUD::DrawMeter(float X, float Y, float W, float H, float Ratio, const FLinearColor& Color, float Trail)
{
    DrawRect(Ink, X - 2.f, Y - 2.f, W + 4.f, H + 4.f);
    DrawRect(FLinearColor(.41f, .31f, .19f, .76f), X - 1.f, Y - 1.f, W + 2.f, H + 2.f);
    DrawRect(FLinearColor(.040f, .029f, .026f, .96f), X, Y, W, H);
    const float Wound = W * FMath::Clamp(Trail, 0.f, 1.f);
    const float Fill = W * FMath::Clamp(Ratio, 0.f, 1.f);
    if (Wound > Fill)
    {
        DrawRect(FLinearColor(.67f, .40f, .18f, .83f), X + Fill, Y, Wound - Fill, H);
    }
    if (Fill > 0.f)
    {
        FadeBand(X, Y, Fill, H, FMath::Lerp(Color, Ink, .22f), Color);
        DrawRect(FMath::Lerp(Color, Ivory, .43f).CopyWithNewOpacity(.64f), X, Y, Fill, 1.f);
    }
}

void ADuelHUD::DrawEquipment(const ADuelFighter& Fighter, float H, float S)
{
    const float X = 32.f * S;
    const float Y = H - 137.f * S;
    const float Card = 55.f * S;
    const float Gap = 4.f * S;
    const float Middle = X + Card + Gap;
    const auto DrawSlot = [&](float SlotX, float SlotY, const FLinearColor& Tint)
    {
        Chrome(0, SlotX, SlotY, Card, Card);
        Chrome(14, SlotX, SlotY, Card, Card, Tint);
    };
    for (EDuelHand Hand : {EDuelHand::Left, EDuelHand::Right})
    {
        const bool bRight = Hand == EDuelHand::Right;
        const float CardX = bRight ? Middle + Card + Gap : X;
        const EDuelWeapon Item = Fighter.EquippedWeapon(Hand);
        const FLinearColor HandTint = Fighter.IsGuarding(Hand) ? Ember : FLinearColor::White;
        DrawSlot(CardX, Y, HandTint);
        InventoryIcon(WeaponArtTile(Item), CardX+7.f*S, Y+3.f*S, 40.f*S, HandTint);
        Label(bRight ? TEXT("3") : TEXT("1"), CardX + 6.f * S, Y + 3.f * S, 11.f * S, Ash);
        Label(DuelCourt::EquipmentName(Item), CardX + Card * .5f, Y + 43.f * S, 10.f * S, Ivory, true);
    }
    const float Bottom = Y + Card + Gap;
    const bool bHeal = Fighter.EquippedItem() == EDuelItem::Flask;
    DrawSlot(Middle, Bottom, bHeal ? Ivory : FLinearColor(.61f, .81f, .99f));
    InventoryIcon(bHeal ? 12 : 13, Middle+8.f*S, Bottom+3.f*S, 38.f*S);
    Label(FString::FromInt(Fighter.ItemCount()), Middle + 45.f * S, Bottom + 5.f * S, 16.f * S,
        Fighter.ItemCount() > 0 ? Ember : Ash, true);
    Label(bHeal ? TEXT("血瓶") : TEXT("蓝瓶"), Middle + Card * .5f, Bottom + 42.f * S, 11.f * S, Ivory, true);
    const EDuelArt Art = Fighter.ActiveArt();
    const bool bStaff = Fighter.EquippedWeapon(Fighter.ArtHand()) == EDuelWeapon::Staff;
    if (Art != EDuelArt::Default || bStaff)
    {
        const bool bReady = Art != EDuelArt::Default && Fighter.HasFocus(Art) &&
            Fighter.StaminaRatio() * DuelCourt::MaxStamina >= DuelCourt::WeaponArt(Art).Strike.Timing.Cost;
        const FLinearColor Tint = bReady ? Ivory : Ash;
        DrawSlot(Middle, Y - Card - Gap, Tint);
        if (Art == EDuelArt::Default)
            Label(TEXT("空"), Middle + Card * .5f, Y - Card - Gap + 16.f * S, 16.f * S, Tint, true);
        else
            DrawLoadoutArt(Art, Middle+9.f*S, Y-Card-Gap+5.f*S, 37.f*S, Tint);
        Label(bStaff ? FString::Printf(TEXT("E   4 · %d"), Fighter.SpellSlot() + 1) : TEXT("E"),
            Middle + 6.f * S, Y - Card + 1.f * S, 11.f * S, Ash);
        Label(Art == EDuelArt::Default ? TEXT("空法术槽") : DuelCourt::WeaponArt(Art).Name,
            Middle + Card * .5f, Y - Gap - 12.f * S, 11.f * S, Tint, true);
    }
    Label(TEXT("2 切换    R 使用"), Middle + Card * .5f, H - 16.f * S, 10.f * S, Ash, true);
}

void ADuelHUD::DrawLoadoutArt(EDuelArt Art, float X, float Y, float Size, const FLinearColor& Tint)
{
    constexpr int32 ArtCount = 34;
    constexpr int32 Columns = 8;
    constexpr double TileSize = 1. / Columns;
    constexpr double HalfTexel = .5 / 1024.;
    static_assert(static_cast<int32>(EDuelArt::MoonCrescent) + 1 == ArtCount);

    if (!ArtIconAtlas)
        ArtIconAtlas = LoadObject<UTexture2D>(nullptr, TEXT("/Game/DuelArt/UI/T_NorthArtCards.T_NorthArtCards"));
    const FTexture* Texture = ArtIconAtlas ? ArtIconAtlas->GetResource() : nullptr;
    if (!Texture)
    {
        Chrome(12, X, Y, Size, Size, Tint);
        return;
    }

    const int32 Slot = static_cast<int32>(Art) < ArtCount ? static_cast<int32>(Art) : 0;
    const FVector2D UV((Slot % Columns) * TileSize, (Slot / Columns) * TileSize);
    const FLinearColor Color = FMath::Lerp(FLinearColor::White, Tint.GetClamped(0.f, 1.f), .19f)
        .CopyWithNewOpacity(Tint.A);
    FCanvasTileItem Icon(FVector2D(X, Y), Texture, FVector2D(Size), UV + FVector2D(HalfTexel),
        UV + FVector2D(TileSize - HalfTexel), Color);
    Icon.BlendMode = SE_BLEND_Translucent;
    Canvas->DrawItem(Icon);
}

void ADuelHUD::DrawLoadout(const ADuelMode& Mode, float W, float H, float S)
{
    const ADuelFighter& Fighter = *Mode.GetPlayerFighter();
    S = FMath::Min(S, FMath::Min(W / 1140.f, H / 588.f));
    const float Width = 1110.f * S, Height = 554.f * S;
    const float X = (W - Width) * .5f, Y = (H - Height) * .5f;
    const bool bChoosing = Mode.MenuPage() == EDuelMenuPage::EquipmentChoices;
    const int32 SelectedRow = Mode.LoadoutChoice();
    const TCHAR* Slots[] = {TEXT("右手主槽"), TEXT("左手主槽"), TEXT("右手备用"), TEXT("左手备用"),
        TEXT("通用战技"), TEXT("法术槽 1"), TEXT("法术槽 2"), TEXT("法术槽 3"), TEXT("药瓶分配")};
    static_assert(UE_ARRAY_COUNT(Slots) == ADuelMode::LoadoutRows);

    Panel(X, Y, Width, Height, S);
    Label(bChoosing ? TEXT("更换装备") : TEXT("武器与法术"), X + 29.f * S, Y + 18.f * S, 31.f * S, Ivory);
    DrawMenuBack(X + 985.f * S, Y + 16.f * S, 98.f * S, 31.f * S, S, TEXT("Esc 返回"));
    const FLinearColor Rule = Bronze.CopyWithNewOpacity(.48f);
    DrawRect(Rule, X + 26.f * S, Y + 63.f * S, Width - 52.f * S, 1.f);
    DrawRect(Rule, X + 422.f * S, Y + 77.f * S, 1.f, 418.f * S);
    DrawRect(Rule, X + 783.f * S, Y + 77.f * S, 1.f, 418.f * S);
    DrawRect(Rule, X + 26.f * S, Y + 503.f * S, Width - 52.f * S, 1.f);

    const auto ItemName = [&](int32 Row, int32 Value) -> FString
    {
        if (Row < ADuelMode::ArtLoadoutRow) return DuelCourt::EquipmentName(static_cast<EDuelWeapon>(Value));
        if (Row == ADuelMode::FlaskLoadoutRow)
            return FString::Printf(TEXT("血瓶 %d   蓝瓶 %d"), Value, DuelCourt::TotalFlasks - Value);
        if (Value == static_cast<int32>(EDuelArt::Default))
            return Row >= ADuelMode::FirstSpellRow ? TEXT("空法术槽") : TEXT("随武器");
        return DuelCourt::WeaponArt(static_cast<EDuelArt>(Value)).Name;
    };
    const auto ItemIcon = [&](int32 Row, int32 Value, float Left, float Top, float Size)
    {
        if (Row == ADuelMode::FlaskLoadoutRow)
        {
            InventoryIcon(12, Left, Top + .19f * Size, .60f * Size);
            InventoryIcon(13, Left + .43f * Size, Top + .14f * Size, .59f * Size);
            return;
        }
        if (Row >= ADuelMode::ArtLoadoutRow)
        {
            const auto Art = static_cast<EDuelArt>(Value);
            if (Row >= ADuelMode::FirstSpellRow && Art == EDuelArt::Default)
            {
                Label(TEXT("空"), Left + .5f * Size, Top + .34f * Size, .22f * Size, Ash, true);
                return;
            }
            const FLinearColor Tint = Art == EDuelArt::Default ? Ash :
                DuelCourt::WeaponArt(Art).Glow.GetClamped(.38f, 1.f);
            DrawLoadoutArt(Art, Left, Top, Size, Tint);
            return;
        }
        const auto Gear = static_cast<EDuelWeapon>(Value);
        const FLinearColor Tint = Gear == EDuelWeapon::Empty ? Ash : DuelCourt::IsBlade(Gear) ?
            FMath::Lerp(Ivory, DuelCourt::Armament(Gear).Glow.GetClamped(.38f, 1.f), .54f) : Ivory;
        InventoryIcon(WeaponArtTile(Gear), Left, Top, Size, Tint);
    };
    int32 ShownRow = SelectedRow;
    int32 ShownValue = bChoosing ? Mode.LoadoutCandidate : Mode.EquippedLoadoutValue(SelectedRow);
    const auto Tile = [&](int32 Row, int32 Value, float TX, float TY, const FString& Name, bool bSelected, bool bEquipped)
    {
        const float Size = 80.f * S;
        const bool bHover = MenuButton(bChoosing ? Value : Row, TX - 2.f * S, TY - 2.f * S, Size + 4.f * S, Size + 23.f * S);
        const bool bFocused = bHover || bSelected;
        DrawRect(FLinearColor(.047f, .041f, .038f, .95f), TX, TY, Size, Size);
        if (bFocused) FocusBand(TX, TY, Size, Size + 19.f * S, S);
        Chrome(14, TX, TY, Size, Size, bFocused ? Ember : FLinearColor(.75f, .68f, .56f, .62f));
        ItemIcon(Row, Value, TX + 11.f * S, TY + 10.f * S, 58.f * S);
        if (bEquipped) Chrome(13, TX + 61.f * S, TY + 5.f * S, 13.f * S, 13.f * S, Ember);
        const float Font = 12.f * S;
        const float TextSize = FMath::Clamp(Font * (Size / FMath::Max(Size, LabelWidth(Name, Font))), 9.f * S, Font);
        Label(Name, TX + Size * .5f, TY + 83.f * S, TextSize, bFocused ? Ember : Ivory, true);
        if (bHover)
        {
            ShownRow = Row;
            ShownValue = Value;
        }
    };

    if (bChoosing)
    {
        Label(Slots[SelectedRow], X + 29.f * S, Y + 84.f * S, 19.f * S, Ember);
        const auto Choices = Mode.LoadoutChoices();
        const int32 Equipped = Mode.EquippedLoadoutValue(SelectedRow);
        for (int32 I = 0; I < Choices.Num(); ++I)
        {
            const int32 Value = Choices[I];
            const float TX = X + (29.f + I % ADuelMode::LoadoutColumns * 94.f) * S;
            const float TY = Y + (123.f + I / ADuelMode::LoadoutColumns * 110.f) * S;
            Tile(SelectedRow, Value, TX, TY, ItemName(SelectedRow, Value), Mode.LoadoutCandidate == Value, Equipped == Value);
        }
    }
    else
    {
        Label(TEXT("武器"), X + 29.f * S, Y + 81.f * S, 18.f * S, Ash);
        Label(TEXT("法术"), X + 29.f * S, Y + 220.f * S, 18.f * S, Ash);
        Label(TEXT("战技与道具"), X + 29.f * S, Y + 358.f * S, 18.f * S, Ash);
        for (int32 Row = 0; Row < ADuelMode::LoadoutRows; ++Row)
        {
            const bool bWeapon = Row < ADuelMode::ArtLoadoutRow;
            const bool bSpell = Row >= ADuelMode::FirstSpellRow && Row < ADuelMode::FlaskLoadoutRow;
            const int32 Column = bWeapon ? Row : bSpell ? Row - ADuelMode::FirstSpellRow : Row == ADuelMode::ArtLoadoutRow ? 0 : 1;
            const float TY = Y + (bWeapon ? 110.f : bSpell ? 249.f : 387.f) * S;
            Tile(Row, Mode.EquippedLoadoutValue(Row), X + (29.f + Column * 94.f) * S, TY, Slots[Row],
                Row == SelectedRow, bSpell && Fighter.SelectedSpell() != EDuelArt::Default &&
                Row - ADuelMode::FirstSpellRow == Fighter.SpellSlot());
        }
    }

    const bool bWeapon = ShownRow < ADuelMode::ArtLoadoutRow;
    const bool bFlasks = ShownRow == ADuelMode::FlaskLoadoutRow;
    const bool bSpell = ShownRow >= ADuelMode::FirstSpellRow && !bFlasks;
    const auto Gear = bWeapon ? static_cast<EDuelWeapon>(ShownValue) : EDuelWeapon::Empty;
    const auto Art = bWeapon ? (Gear == EDuelWeapon::Staff ? Fighter.SelectedSpell() : DuelCourt::ResolveArt(Gear, Fighter.SelectedArt())) :
        bFlasks ? EDuelArt::Default : static_cast<EDuelArt>(ShownValue);
    const float CenterX = X + 603.f * S;
    const float ArtX = X + 444.f * S;
    FadeBand(ArtX, Y + 119.f * S, 318.f * S, 201.f * S,
        FLinearColor(.15f, .115f, .075f, .49f), FLinearColor(.042f, .046f, .064f, .45f));
    DrawRect(Rule, ArtX, Y + 319.f * S, 318.f * S, FMath::Max(1.f, S));
    Chrome(1, ArtX + 1.f * S, Y + 120.f * S, 25.f * S, 25.f * S, Bronze);
    Label(Slots[ShownRow], CenterX, Y + 86.f * S, 16.f * S, Ash, true);
    ItemIcon(ShownRow, ShownValue, CenterX - 81.f * S, Y + 137.f * S, 162.f * S);
    const bool bCurrent = Mode.EquippedLoadoutValue(ShownRow) == ShownValue;
    if (bChoosing && bCurrent) Label(TEXT("已选用"), CenterX, Y + 297.f * S, 14.f * S, Ember, true);
    Label(ItemName(ShownRow, ShownValue), CenterX, Y + 339.f * S, bFlasks ? 23.f * S : 28.f * S, Ivory, true);
    float DetailY = Y + 385.f * S;
    if (bWeapon && Art != EDuelArt::Default)
    {
        Label(FString::Printf(TEXT("%s  %s"), Gear == EDuelWeapon::Staff ? TEXT("当前法术") : TEXT("战技"), DuelCourt::WeaponArt(Art).Name),
            X + 451.f * S, DetailY, 15.f * S, Ember);
        DetailY += 25.f * S;
    }
    const TCHAR* Effect = bWeapon ? DuelCourt::EquipmentEffect(Gear) : nullptr;
    const TCHAR* Description = Effect ? Effect : Art != EDuelArt::Default ? DuelCourt::WeaponArt(Art).Description :
        bSpell ? TEXT("此槽未装配法术。游戏内按 4 切换时会跳过空槽。") :
        DuelCourt::IsShield(Gear) ? TEXT("按住武器键防御") : nullptr;
    if (Description)
    {
        for (const FString& Line : WrapLabel(Description, 15.f * S, 307.f * S))
        {
            Label(Line, X + 451.f * S, DetailY, 15.f * S, Ash);
            DetailY += 23.f * S;
        }
    }

    float StatY = Y + 122.f * S;
    const auto Stat = [&](const TCHAR* Name, const FString& Value)
    {
        DrawRect(FLinearColor(.078f, .077f, .087f, .68f), X + 803.f * S, StatY - 3.f * S, 276.f * S, 29.f * S);
        Label(Name, X + 813.f * S, StatY, 15.f * S, Ash);
        Label(Value, X + 1067.f * S - LabelWidth(Value, 15.f * S), StatY, 15.f * S, Ivory);
        StatY += 34.f * S;
    };
    Label(Fighter.CharacterName(), X + 803.f * S, Y + 83.f * S, 22.f * S, Ember);
    Stat(TEXT("生命"), FString::Printf(TEXT("%d / %d"), FMath::RoundToInt(Fighter.HealthRatio() * DuelCourt::PlayerMaxHealth),
        FMath::RoundToInt(DuelCourt::PlayerMaxHealth)));
    Stat(TEXT("MP"), FString::Printf(TEXT("%d / %d"), FMath::RoundToInt(Fighter.FocusRatio() * Fighter.FocusMax()), FMath::RoundToInt(Fighter.FocusMax())));
    Stat(TEXT("体力"), FString::Printf(TEXT("%d / %d"), FMath::RoundToInt(Fighter.StaminaRatio() * DuelCourt::MaxStamina), FMath::RoundToInt(DuelCourt::MaxStamina)));
    DrawRect(Rule, X + 803.f * S, StatY + 5.f * S, 276.f * S, 1.f);
    StatY += 20.f * S;
    if (bWeapon && DuelCourt::IsBlade(Gear))
    {
        const auto& Weapon = DuelCourt::Armament(Gear);
        Stat(TEXT("轻击伤害"), FString::FromInt(FMath::RoundToInt(Weapon.Cut.Damage)));
        Stat(TEXT("重击伤害"), FString::FromInt(FMath::RoundToInt(Weapon.Heavy.Damage)));
        Stat(TEXT("重击架势伤害"), FString::FromInt(FMath::RoundToInt(Weapon.Heavy.Posture)));
    }
    else if (Art != EDuelArt::Default)
    {
        const auto& Data = DuelCourt::WeaponArt(Art);
        Stat(TEXT("MP 消耗"), FString::FromInt(DuelCourt::FocusCost(Art)));
        if (Data.Strike.Reach > 0.f) Stat(TEXT("范围"), FString::Printf(TEXT("%.1f 米"), Data.Strike.Reach / 100.f));
        if (bSpell && Data.Strike.Damage > 0.f) Stat(TEXT("法术伤害"), FString::FromInt(FMath::RoundToInt(Data.Strike.Damage)));
    }
    else if (bFlasks)
    {
        Stat(TEXT("血瓶"), FString::FromInt(ShownValue));
        Stat(TEXT("蓝瓶"), FString::FromInt(DuelCourt::TotalFlasks - ShownValue));
    }

    const float KeyY = Y + 515.f * S;
    float KeyX = X + 29.f * S;
    KeyX += KeyHint(KeyX, KeyY, S, bChoosing ? TEXT("WASD") : TEXT("W / S"), TEXT("选择"));
    if (!bChoosing) KeyX += KeyHint(KeyX, KeyY, S, TEXT("A / D"), TEXT("快速更换"));
    KeyX += KeyHint(KeyX, KeyY, S, TEXT("F / Enter"), bChoosing ? TEXT("确认") : TEXT("查看候选"));
    KeyHint(KeyX, KeyY, S, TEXT("Esc"), TEXT("返回"));
}

void ADuelHUD::Label(FStringView Text, float X, float Y, float Size, const FLinearColor& Color, bool bCenter)
{
    if (!HudFont || !FEngineFontServices::IsInitialized() || Text.IsEmpty() || Color.A <= 0.f)
    {
        return;
    }
    // Place the visible CJK text at the existing HUD line anchors.
    FCanvasTextStringViewItem Item(FVector2D(X, Y - Size * .25f), Text, DuelFontInfo(HudFont, Size), Color);
    Item.bCentreX = bCenter;
    Item.bDontCorrectStereoscopic = true;
    Item.EnableShadow(FLinearColor(.004f, .006f, .009f), FVector2D(1.f, 1.5f));
    Canvas->DrawItem(Item);
}

float ADuelHUD::LabelWidth(FStringView Text, float Size) const
{
    if (!HudFont || !Canvas || !Canvas->Canvas || !FEngineFontServices::IsInitialized()) return 0.f;
    const TSharedPtr<FSlateFontMeasure> Measure = FEngineFontServices::Get().GetFontMeasure();
    if (!Measure) return 0.f;
    const float DPI = Canvas->Canvas->GetDPIScale();
    const FVector2D Extent = Measure->Measure(Text, DuelFontInfo(HudFont, Size), DPI);
    return Extent.X / DPI;
}

TArray<FString> ADuelHUD::WrapLabel(const FString& Text, float Size, float Width) const
{
    TArray<FString> Lines;
    if (!HudFont || !Canvas || !Canvas->Canvas || !FEngineFontServices::IsInitialized()) return Lines;
    const TSharedPtr<FSlateFontMeasure> Measure = FEngineFontServices::Get().GetFontMeasure();
    if (!Measure) return Lines;
    const float DPI = Canvas->Canvas->GetDPIScale();
    const FSlateFontInfo Font = DuelFontInfo(HudFont, Size);
    const int32 PixelWidth = FMath::Max(1, FMath::FloorToInt(Width * DPI));
    TArray<FString> Paragraphs;
    Text.ParseIntoArrayLines(Paragraphs);
    for (const FString& Paragraph : Paragraphs)
    {
        FStringView Remaining(Paragraph);
        while (!Remaining.IsEmpty())
        {
            int32 Count = FMath::Max(1,
                Measure->FindLastWholeCharacterIndexBeforeOffset(Remaining, Font, PixelWidth, DPI) + 1);
            if (Count < Remaining.Len())
            {
                const auto Break = FBreakIterator::CreateLineBreakIterator();
                Break->SetStringRef(Remaining);
                const int32 Boundary = Break->MoveToCandidateBefore(Count + 1);
                if (Boundary > 0) Count = Boundary;
            }
            Lines.Emplace(Remaining.Left(Count).TrimStartAndEnd());
            Remaining = Remaining.RightChop(Count).TrimStart();
        }
    }
    return Lines;
}

void ADuelHUD::DrawPrompt(const ADuelMode& Mode, bool bPaused, float W, float H, float S)
{
    const EDuelOutcome Outcome = Mode.Outcome();
    if (Outcome == EDuelOutcome::Title)
    {
        DrawTitle(Mode, W, H, S);
        return;
    }
    if (bPaused)
    {
        DrawRect(FLinearColor(.007f, .010f, .018f, .58f), 0, 0, W, H);
        switch (Mode.MenuPage())
        {
        case EDuelMenuPage::Equipment:
        case EDuelMenuPage::EquipmentChoices: DrawLoadout(Mode, W, H, S); break;
        case EDuelMenuPage::Inventory: DrawInventory(Mode, W, H, S); break;
        case EDuelMenuPage::Graphics: DrawSettings(Mode, W, H, S); break;
        case EDuelMenuPage::Controls: DrawControls(Mode, W, H, S); break;
        default: DrawPause(Mode, W, H, S); break;
        }
        return;
    }
    if (Outcome == EDuelOutcome::Ready)
    {
        DrawCompass(Mode, W, S);
        const FVector At = Mode.GetPlayerFighter()->GetActorLocation();
        const bool bRest = Mode.NearRestPoint();
        const TCHAR* Prompt = nullptr;
        if (Mode.UsesCastle())
        {
            const TCHAR* Investigation = Mode.ActiveInvestigation();
            Prompt = Mode.CastlePrompt();
            if (Investigation)
            {
                Panel(W * .5f - 380.f * S, H * .765f, 760.f * S, 44.f * S, S);
                Label(Investigation, W * .5f, H * .765f + 9.f * S, 17.f * S, Ivory, true);
            }
            if (!Prompt)
            {
                Label(FString::Printf(TEXT("判词铭牌   %d / 3"), Mode.CastleMarks()), W * .5f, H * .858f, 15.f * S, Ash, true);
                return;
            }
        }
        else if (Mode.NearGate())
        {
            Prompt = TEXT("F   打开雾门");
        }
        else if (Mode.NearWarrant())
        {
            Prompt = TEXT("F   取走通行文书");
        }
        else if (Mode.NearPrisonGate())
        {
            Prompt = Mode.HasWarrant() ? TEXT("F   用文书打开铁闸") : TEXT("铁闸锁着。去右侧禁闭室找通行文书。");
        }
        else if (bRest)
        {
            Prompt = TEXT("F   休息并记录进度");
        }
        else if (Mode.GateOpen())
        {
            Prompt = TEXT("向前走入雾门");
        }
        if (Prompt)
        {
            const float Width = (Mode.UsesCastle() ? 650.f : 440.f) * S;
            Panel((W - Width) * .5f, H * .835f, Width, 43.f * S, S);
            Label(Prompt, W * .5f, H * .835f + 9.f * S, 18.f * S, Ember, true);
            return;
        }
        if (Mode.InAppeal())
        {
            Label(Mode.InYukiCourt() ? TEXT("前往终审庭，与月代雪对质") : TEXT("沿证人通道登上复审礼堂"),
                W * .5f, H * .858f, 16.f * S, Ivory, true);
            return;
        }
        if (!At.Equals(DuelCourt::LowerCheckpoint().GetLocation(), 190.0))
        {
            const TCHAR* Objective = Mode.PrisonGateOpen() ? TEXT("前往审讯庭雾门") : Mode.HasWarrant() ? TEXT("打开押送铁闸") :
                (At.X < DuelCourt::StairTopX ? TEXT("登上押送阶梯") : TEXT("在右侧禁闭室找通行文书"));
            Label(Objective, W * .5f, H * .858f, 16.f * S, Ivory, true);
            return;
        }
        const float X = W * .055f;
        const float Y = H * .19f;
        Panel(X - 19.f * S, Y - 17.f * S, 418.f * S, 284.f * S, S);
        Label(TEXT("魔女审判"), X, Y, 46.f * S, Ivory);
        Label(TEXT("缺页证言  ·  开庭前夜"), X, Y + 53.f * S, 20.f * S, Ember);
        Label(TEXT("案卷少了一页目击证词。证人还在禁闭室。"), X, Y + 89.f * S, 16.f * S, Ivory);
        Label(TEXT("审判把证人写成四足幻形，重复那份指控。"), X, Y + 111.f * S, 16.f * S, Ivory);
        Label(TEXT("解除幻形，才能听见她原本的话。"), X, Y + 133.f * S, 16.f * S, Ivory);
        DrawRect(Bronze, X, Y + 162.f * S, 354.f * S, 1.f);
        Label(TEXT("WASD  前往押送阶梯"), X, Y + 174.f * S, 18.f * S, Ember);
        Label(TEXT("长按 Shift 跑步   Space 跳跃"), X, Y + 205.f * S, 13.f * S, Ash);
        Label(TEXT("Esc 配置角色与首领    G 查看模型"), X, Y + 225.f * S, 13.f * S, Ash);
        return;
    }

    if (Outcome == EDuelOutcome::Victory)
    {
        if (Mode.UsesCastle())
        {
            if (const TCHAR* Prompt = Mode.CastlePrompt())
            {
                Panel(W * .5f - 298.f * S, H * .828f, 596.f * S, 46.f * S, S);
                Label(Prompt, W * .5f, H * .828f + 10.f * S, 17.f * S, Ember, true);
            }
            return;
        }
        const float Width = 510.f * S;
        Panel((W - Width) * .5f, H * .84f, Width, 44.f * S, S);
        const TCHAR* Prompt = Mode.InYukiCourt() ? TEXT("魔女之源已断  ·  穿过拱门前往月庭") :
            Mode.InAppeal() ? TEXT("魔女化已解除  ·  登上复审席传唤月代雪") :
            TEXT("魔女化已解除  ·  穿过拱门去证物库");
        Label(Prompt,
            W * .5f, H * .84f + 9.f * S, 19.f * S, Ember, true);
        return;
    }
    if (Outcome == EDuelOutcome::Passage)
    {
        Label(TEXT("沿证人回廊前往下一处复活点"), W * .5f, H * .862f, 16.f * S, Ivory, true);
        return;
    }
    const float X = (W - 525.f * S) * .5f;
    const float Y = H * .28f;
    if (Outcome == EDuelOutcome::Epilogue)
    {
        if (Mode.ReadingArchive())
        {
            if (Mode.Choice() < 2)
            {
                const bool bHiro = Mode.GetPlayerFighter()->SelectedCharacter() == EDuelCharacter::Hiro;
                const bool bPartner = Mode.Choice() == 0;
                Panel(W * .5f - 330.f * S, H * .71f, 660.f * S, 123.f * S, S);
                Label(bHiro == bPartner ? TEXT("艾玛") : TEXT("希罗"), W * .5f, H * .71f + 16.f * S, 20.f * S, Ember, true);
                const TCHAR* Line = bPartner ? (bHiro ? TEXT("希罗酱，海上有月光。我们一起回去吧。") :
                    TEXT("艾玛，门开着。这一次，我们一起走。")) :
                    (bHiro ? TEXT("好。以后下雨，我们就在家等天晴。") : TEXT("嗯。从今以后，下雨也不用害怕了。"));
                Label(Line, W * .5f, H * .71f + 47.f * S, 17.f * S, Ivory, true);
                Label(TEXT("F   继续"), W * .5f, H * .71f + 88.f * S, 14.f * S, Ash, true);
                return;
            }
            const int32 Total = Mode.HearingsTime();
            Panel(W * .5f - 304.f * S, Y, 608.f * S, 269.f * S, S);
            Label(TEXT("在一个不用等判决的早晨"), W * .5f, Y + 26.f * S, 30.f * S, Ivory, true);
            Label(Mode.UsesCastle() ? TEXT("证词归卷了。世上再没有魔女。") : TEXT("审判终止了。命案重新调查。"),
                W * .5f, Y + 78.f * S, 18.f * S, Ember, true);
            Label(TEXT("艾玛和希罗搬到了离海很近的地方。"), W * .5f, Y + 113.f * S, 17.f * S, Ivory, true);
            Label(TEXT("晴天一起去买苹果，下雨就留在家里。"), W * .5f, Y + 143.f * S, 17.f * S, Ivory, true);
            const TCHAR* DuelCount = Mode.UsesCastle() ? TEXT("四场战斗") : TEXT("三场战斗");
            Label(FString::Printf(TEXT("%s  ·  用时 %02d:%02d"), DuelCount, Total / 60, Total % 60),
                W * .5f, Y + 181.f * S, 14.f * S, Ash, true);
            Label(TEXT("F   回到月庭"), W * .5f, Y + 229.f * S, 16.f * S, Ember, true);
        }
        else
        {
            Label(Mode.NearGardenExit() ? TEXT("F   和她说话") : TEXT("往前走，去她身边"),
                W * .5f, H * .855f, 19.f * S, Ivory, true);
        }
        return;
    }
    if (Outcome == EDuelOutcome::Archive)
    {
        const int32 Time = Mode.DuelSeconds();
        const FString ResultLine = FString::Printf(TEXT("击败 %s    ·    用时 %02d:%02d"),
            Mode.GetBossFighter()->CharacterName(), Time / 60, Time % 60);
        if (Mode.ReadingArchive())
        {
            if (Mode.InAppeal())
            {
                const bool bReceipt = Mode.Ending() == EDuelEnding::SubmitReceipt;
                const int32 Total = Time + Mode.FirstHearingSeconds();
                Panel(X, Y, 525.f * S, 249.f * S, S);
                Label(TEXT("月代雪的传唤"), W * .5f, Y + 21.f * S, 30.f * S, Ember, true);
                Label(FString::Printf(TEXT("两人已解除魔女化  ·  用时 %02d:%02d"), Total / 60, Total % 60),
                    W * .5f, Y + 69.f * S, 15.f * S, Ash, true);
                Label(bReceipt ? TEXT("核查：那一节没有她的签认。") :
                    TEXT("她当庭说：那一节不是她写的。"), W * .5f, Y + 106.f * S, 16.f * S, Ivory, true);
                Label(TEXT("月代雪：缺页是我带走的，那一节也是我写的。"), W * .5f, Y + 136.f * S, 16.f * S, Ivory, true);
                Label(TEXT("F   合上笔录，穿过石案后方的门"), W * .5f, Y + 199.f * S, 16.f * S, Ember, true);
            }
            else if (Mode.Ending() == EDuelEnding::Undecided)
            {
                Panel(X, Y, 525.f * S, 259.f * S, S);
                Label(TEXT("证物 07  ·  签收存根"), W * .5f, Y + 20.f * S, 30.f * S, Ember, true);
                Label(ResultLine, W * .5f, Y + 66.f * S, 15.f * S, Ash, true);
                Label(TEXT("22:41 签收，命案 23:16 才发生。"),
                    W * .5f, Y + 100.f * S, 17.f * S, Ivory, true);
                Label(TEXT("原件的名字还在，只是多了一节目击。"),
                    W * .5f, Y + 129.f * S, 17.f * S, Ivory, true);
                const float Selected = W * .5f + (Mode.Choice() == 0 ? -107.f : 107.f) * S;
                DrawRect(FLinearColor(.48f, .31f, .15f, .28f), Selected - 94.f * S, Y + 168.f * S, 188.f * S, 34.f * S);
                Label(TEXT("呈交签收存根"), W * .5f - 107.f * S, Y + 173.f * S, 18.f * S, Mode.Choice() == 0 ? Ember : Ash, true);
                Label(TEXT("申请传唤证人"), W * .5f + 107.f * S, Y + 173.f * S, 18.f * S, Mode.Choice() == 1 ? Ember : Ash, true);
                Label(TEXT("W / S 或 ↑ / ↓ 选择    F 确认"), W * .5f, Y + 220.f * S, 14.f * S, Ash, true);
            }
            else
            {
                const bool bReceipt = Mode.Ending() == EDuelEnding::SubmitReceipt;
                Panel(X, Y, 525.f * S, 228.f * S, S);
                Label(bReceipt ? TEXT("审判笔录  ·  暂缓表决") : TEXT("审判笔录  ·  传唤许可"),
                    W * .5f, Y + 20.f * S, 30.f * S, Ember, true);
                Label(ResultLine, W * .5f, Y + 67.f * S, 15.f * S, Ash, true);
                Label(bReceipt ? TEXT("表决暂缓。先核查原件与这一份。") :
                    TEXT("证人准许出席，从石案左侧进入通道。"), W * .5f, Y + 96.f * S, 17.f * S, Ivory, true);
                Label(bReceipt ? TEXT("从石案左侧进去，看谁写出了那一节。") :
                    TEXT("请她自己说，那一节是不是她写的。"), W * .5f, Y + 125.f * S, 17.f * S, Ivory, true);
                Label(TEXT("F   合上笔录"), W * .5f, Y + 183.f * S, 16.f * S, Ember, true);
            }
        }
        else
        {
            const float Height = H * .807f;
            Panel(W * .5f - 196.f * S, Height, 392.f * S, 77.f * S, S);
            Label(ResultLine, W * .5f, Height + 12.f * S, 16.f * S, Ivory, true);
            const TCHAR* Prompt = Mode.InAppeal()
                ? (Mode.RitualMark() != INDEX_NONE ? TEXT("F   点亮证言烛台") :
                    !Mode.RitualComplete() ? TEXT("依次点亮左席、右席、中央的烛台") :
                    Mode.NearArchiveDesk() ? TEXT("F   听月代雪的传唤") : TEXT("穿过石案后方的门，前往终审庭"))
                : Mode.NearAppealPassage() ? TEXT("F   前往证人通道")
                : Mode.Ending() != EDuelEnding::Undecided ? TEXT("从石案左侧前往复审礼堂")
                : Mode.NearArchiveDesk() ? TEXT("F   查阅证物 07 与胜利结算") : TEXT("到石案前查阅证物 07");
            if (!Mode.InAppeal() && Mode.Ending() != EDuelEnding::Undecided && !Mode.NearArchiveDesk())
            {
                DrawCompass(Mode, W, S);
            }
            Label(Prompt,
                W * .5f, Height + 39.f * S, 18.f * S, Ember, true);
        }
        return;
    }
    DrawRect(FLinearColor(.015f, .018f, .026f, .42f), 0, 0, W, H);
    Panel(X, Y, 525.f * S, 204.f * S, S);
    Label(TEXT("调查中断"), W * .5f, Y + 20.f * S, 35.f * S, Sakura, true);
    Label(TEXT("F   回到复活点"), W * .5f, Y + 157.f * S, 18.f * S, Ember, true);
}

void ADuelHUD::DrawCompass(const ADuelMode& Mode, float W, float S)
{
    const ADuelFighter& Fighter = *Mode.GetPlayerFighter();
    const FVector At = Fighter.GetActorLocation();
    FVector Goal = (Mode.InYukiCourt() ? DuelCourt::YukiOrigin : Mode.InAppeal() ? DuelCourt::AppealOrigin : FVector::ZeroVector)
        + FVector(DuelCourt::GateX, 0, 0);
    if (Mode.UsesCastle())
    {
        Goal = Mode.InvestigationTarget();
    }
    else if (Mode.Outcome() == EDuelOutcome::Archive)
    {
        Goal = DuelCourt::AppealPassage;
    }
    else if (!Mode.InAppeal() && !Mode.PrisonGateOpen())
    {
        Goal = Mode.HasWarrant() ? FVector(DuelCourt::PrisonGateX, 0, 0) :
            At.X < DuelCourt::StairTopX ? FVector(DuelCourt::StairTopX + 100.f, 0, 0) : DuelCourt::Warrant;
    }
    const float Center = W * .5f;
    const float Y = 32.f * S;
    const double Yaw = PlayerOwner->PlayerCameraManager->GetCameraRotation().Yaw;
    Chrome(0, Center - 148.f * S, Y - 8.f * S, 296.f * S, 20.f * S, FLinearColor(1, 1, 1, .80f));
    DrawRect(FLinearColor(.56f, .46f, .31f, .72f), Center - 124.f * S, Y, 248.f * S, 1.f);
    const TCHAR* Cardinal[] = {TEXT("东"), TEXT("北"), TEXT("西"), TEXT("南")};
    for (int32 I = 0; I < 24; ++I)
    {
        const float Delta = FMath::FindDeltaAngleDegrees(Yaw, static_cast<double>(I * 15));
        if (FMath::Abs(Delta) < 72.f)
        {
            const float X = Center + Delta * 1.65f * S;
            DrawRect(I % 6 == 0 ? Bronze : Ash, X, Y, 1.f, (I % 6 == 0 ? 6.f : 3.f) * S);
            if (I % 6 == 0)
            {
                Label(Cardinal[I / 6], X, Y - 21.f * S, 12.f * S, Ivory, true);
            }
        }
    }
    const float Offset = FMath::Clamp(static_cast<float>(FMath::FindDeltaAngleDegrees(Yaw, (Goal - At).Rotation().Yaw)), -72.f, 72.f);
    Chrome(13, Center + (Offset * 1.65f - 8.f) * S, Y - 7.f * S, 16.f * S, 16.f * S, Ember);
    if (PreviousZone == Mode.AreaName() && GetWorld()->GetTimeSeconds() - ZoneChangedAt >= 3.35)
    {
        Label(Mode.AreaName(), Center, Y + 15.f * S, 15.f * S, Ivory, true);
    }
}

void ADuelHUD::DrawArea(const ADuelMode& Mode, float W, float H, float S)
{
    const FString Area = Mode.AreaName();
    const double Now = GetWorld()->GetTimeSeconds();
    if (PreviousZone != Area)
    {
        PreviousZone = Area;
        ZoneChangedAt = Now;
    }
    const float Age = static_cast<float>(Now - ZoneChangedAt);
    const bool bFighting = Mode.Outcome() == EDuelOutcome::Fighting;
    const float Fade = bFighting ? FMath::SmoothStep(.91f, 1.8f, Age) : FMath::SmoothStep(2.25f, 3.35f, Age);
    const float Alpha = FMath::SmoothStep(0.f, .42f, Age) * (1.f - Fade);
    if (Alpha <= .01f || Mode.DialogueVisible())
    {
        return;
    }
    const float Y = H * (bFighting ? .125f : .20f);
    Chrome(0, W * .5f - 177.f * S, Y - 8.f * S, 354.f * S, 60.f * S, FLinearColor(1, 1, 1, Alpha * .68f));
    DrawRect(FLinearColor(.65f, .50f, .27f, Alpha * .69f), W * .5f - 137.f * S, Y + 37.f * S, 274.f * S, 1.f);
    Label(Area, W * .5f, Y + 2.f * S, 24.f * S, FLinearColor(Ivory.R, Ivory.G, Ivory.B, Alpha), true);
}

void ADuelHUD::DrawCaption(const FString& Speaker, const FString& Line, float W, float H, float S, float Alpha)
{
    const float Center = W * .5f;
    const float Width = FMath::Min(W * .68f, 630.f * S);
    const float Size = 19.f * S;
    const TArray<FString> Lines = WrapLabel(Line, Size, Width - 36.f * S);
    const float Height = (29.f + 25.f * Lines.Num()) * S;
    const float Y = H - 74.f * S - Height;
    DrawRect(Ink.CopyWithNewOpacity(.89f * Alpha), Center - Width * .5f, Y, Width, Height);
    DrawRect(FLinearColor(Bronze.R, Bronze.G, Bronze.B, .48f * Alpha),
        Center - Width * .42f, Y, Width * .84f, 1.f);
    Label(Speaker, Center, Y + 6.f * S, 13.f * S, FLinearColor(Ember.R, Ember.G, Ember.B, Alpha), true);
    for (int32 Row = 0; Row < Lines.Num(); ++Row)
    {
        Label(Lines[Row], Center, Y + (26.f + Row * 25.f) * S, Size, FLinearColor(Ivory.R, Ivory.G, Ivory.B, Alpha), true);
    }
}

void ADuelHUD::DrawHUD()
{
    Super::DrawHUD();
    MenuButtons.Reset();
    DrawnMenuPage = EDuelMenuPage::None;
    const ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    const ADuelFighter* Player = PlayerOwner ? Cast<ADuelFighter>(PlayerOwner->GetPawn()) : nullptr;
    const ADuelFighter* Boss = Mode ? Mode->GetBossFighter() : nullptr;
    if (!Canvas || !Player || !Boss)
    {
        return;
    }
    const float W = static_cast<float>(Canvas->SizeX);
    const float H = static_cast<float>(Canvas->SizeY);
    const float S = FMath::Clamp(FMath::Min(H / 720.f, W / 1060.f), .58f, 1.8f);
    DrawnMenuPage = Mode->MenuPage();
    DrawnLoadoutRow = Mode->LoadoutChoice();
    MenuCanvasSize = FVector2D(W, H);
    bMenuPointerKnown = DrawnMenuPage != EDuelMenuPage::None && ReadMenuPointer(MenuPointer);
    const EDuelOutcome Outcome = Mode->Outcome();
    const bool bPaused = UGameplayStatics::IsGamePaused(this);
    if (bPaused || Outcome == EDuelOutcome::Title)
    {
        DrawPrompt(*Mode, bPaused, W, H, S);
        return;
    }
    if (DrawnMenuPage == EDuelMenuPage::Map)
    {
        DrawNorthHUD(*Mode, W, H, S);
        return;
    }
    if (Outcome == EDuelOutcome::Ready && Mode->CharacterView())
    {
        const bool bAlternate = Mode->CharacterView() == 2;
        Panel(W * .13f, H - 82.f * S, W * .74f, 69.f * S, S);
        Label(FString::Printf(TEXT("%s · %s"), Player->CharacterName(), bAlternate ? TEXT("四足") : TEXT("站立")),
            W * .29f, H - 69.f * S, 21.f * S, Ivory, true);
        Label(FString::Printf(TEXT("%s · %s"), Boss->CharacterName(), bAlternate ? TEXT("站立") : TEXT("四足")),
            W * .73f, H - 69.f * S, 21.f * S, Ivory, true);
        Label(TEXT("A / D 旋转     G 切换姿态     F 返回"), W * .5f, H - 37.f * S, 15.f * S, Ember, true);
        return;
    }
    const float Delta = GetWorld()->GetDeltaSeconds();
    const auto Trail = [Delta](float Health, float& Previous, float& Delayed)
    {
        Delayed = Health > Previous ? Health :
            FMath::FInterpConstantTo(FMath::Max(Previous, Delayed), Health, Delta, .26f);
        Previous = Health;
        return Delayed;
    };
    const float Pad = 32.f * S;
    if (Player->CanUseEquipment() && Outcome != EDuelOutcome::Epilogue)
    {
        FadeBand(14.f * S, 14.f * S, 396.f * S, 86.f * S, Ink.CopyWithNewOpacity(.82f), Ink.CopyWithNewOpacity(0.f));
        Chrome(12, Pad, 32.f * S, 37.f * S, 37.f * S, Ember);
        Label(Player->CharacterName(), Pad + 52.f * S, 21.f * S, 19.f * S, Ivory);
        DrawMeter(Pad + 52.f * S, 47.f * S, 289.f * S, 9.f * S, Player->HealthRatio(), Oxblood,
            Trail(Player->HealthRatio(), PreviousHealth, HealthTrail));
        DrawMeter(Pad + 52.f * S, 63.f * S, 234.f * S, 5.f * S, Player->FocusRatio(), Cerulean);
        DrawMeter(Pad + 52.f * S, 76.f * S, 265.f * S, 5.f * S, Player->StaminaRatio(), Moss);
        if (Player->IsGuardBroken() || Player->HasAegis())
        {
            Label(Player->IsGuardBroken() ? TEXT("盾防崩解") : TEXT("守证辉幕"), Pad + 52.f * S, 92.f * S, 15.f * S, Ember);
        }
        if (const float Offering = Player->RitualOfferingSeconds(); Offering > 0.f)
        {
            Label(FString::Printf(TEXT("献血仪式  %d 秒"), FMath::CeilToInt(Offering)),
                Pad + 52.f * S, 114.f * S, 15.f * S, Ember);
        }
        DrawEquipment(*Player, H, S);
    }
    if (Mode->UsesNorthTower())
    {
        DrawNorthHUD(*Mode,W,H,S);
        if (Outcome == EDuelOutcome::Defeat)
        {
            if (const TCHAR* Line = Mode->CombatCaption())
            {
                DrawCaption(Mode->CaptionSpeaker(), Line, W, H * .91f, S, Mode->CaptionOpacity());
            }
        }
        if (Outcome != EDuelOutcome::Fighting) return;
    }
    if (Outcome != EDuelOutcome::Fighting)
    {
        PreviousBossHealth = BossHealthTrail = 1.f;
        DrawPrompt(*Mode, false, W, H, S);
        if (Outcome == EDuelOutcome::Defeat)
        {
            if (const TCHAR* Line = Mode->CombatCaption())
            {
                DrawCaption(FString::Printf(TEXT("%s  ·  首领"), Boss->CharacterName()), Line, W, H, S, Mode->CaptionOpacity());
            }
        }
        if (Outcome == EDuelOutcome::Archive && Mode->RitualComplete() && !Mode->ReadingArchive())
        {
            const float Alpha = Mode->RitualCaptionOpacity();
            if (Alpha > 0.f)
            {
                DrawCaption(TEXT("大魔女 · 月代雪"), TEXT("这一页我判了。你们还要再判一次吗？"), W, H * .91f, S, Alpha);
            }
        }
        if (Mode->CanExplore())
        {
            DrawArea(*Mode, W, H, S);
        }
        return;
    }
    KeyHint(W - Pad - 75.f * S, 25.f * S, S, TEXT("Esc"), TEXT("暂停"));
    if (Mode->SilentBellActive())
    {
        Label(TEXT("止步  ·  钟声只追逐脚步"), W * .5f, H * .153f, 18.f * S, Ember, true);
    }

    const float BossW = FMath::Min(W * .60f, 815.f * S);
    const float BossX = (W - BossW) * .5f;
    const float BossY = H - 50.f * S;
    const TCHAR* EmaPhases[] = {TEXT("幻形"), TEXT("魔女化 · 骨翼"), TEXT("魔女化 · 白烬")};
    const TCHAR* HiroPhases[] = {TEXT("幻形"), TEXT("魔女化 · 绯影"), TEXT("魔女化 · 裂影")};
    const TCHAR* YukiPhases[] = {TEXT("大魔女"), TEXT("魔女化 · 裂镜"), TEXT("魔女化 · 终审")};
    const TCHAR* const* Phases = Boss->SelectedCharacter() == EDuelCharacter::Yuki ? YukiPhases :
        Boss->SelectedCharacter() == EDuelCharacter::Ema ? EmaPhases : HiroPhases;
    const TCHAR* Phase = Phases[FMath::Clamp(Boss->Phase(), 1, 3) - 1];
    FadeBand(BossX - 27.f * S, BossY - 35.f * S, BossW * .60f, 70.f * S,
        Ink.CopyWithNewOpacity(.87f), Ink.CopyWithNewOpacity(0.f));
    FadeBand(BossX + BossW * .43f, BossY - 35.f * S, BossW * .57f + 27.f * S, 70.f * S,
        Ink.CopyWithNewOpacity(0.f), Ink.CopyWithNewOpacity(.87f));
    Label(Boss->CharacterName(), BossX, BossY - 31.f * S, 22.f * S, Ivory);
    Label(Phase, BossX + LabelWidth(Boss->CharacterName(), 22.f * S) + 17.f * S,
        BossY - 29.f * S, 15.f * S, Ember);
    DrawMeter(BossX, BossY, BossW, 10.f * S, Boss->HealthRatio(), Boss->IsAwakened() ? Sakura : Oxblood,
        Trail(Boss->HealthRatio(), PreviousBossHealth, BossHealthTrail));
    Chrome(15, BossX - 20.f * S, BossY - 6.f * S, 19.f * S, 24.f * S);
    Chrome(15, BossX + BossW + S, BossY - 6.f * S, 19.f * S, 24.f * S);
    for (float Threshold : {DuelCourt::WingThreshold, DuelCourt::EclipseThreshold})
    {
        const float X = BossX + BossW * Threshold;
        DrawRect(Ink, X - 1.f, BossY - 2.f * S, 3.f, 15.f * S);
        Chrome(13, X - 5.f * S, BossY - 9.f * S, 10.f * S, 10.f * S, Ivory);
    }
    DrawMeter(BossX, BossY + 17.f * S, BossW - 52.f * S, 3.f * S, Boss->PostureRatio(), Bronze);
    Label(TEXT("架势"), BossX + BossW - 42.f * S, BossY + 13.f * S, 12.f * S, Ash);
    const int32 RecentDamage = Boss->RecentDamage();
    if (RecentDamage > 0)
    {
        const FString Value = FString::FromInt(RecentDamage);
        Label(Value, BossX + BossW - LabelWidth(Value, 18.f * S), BossY - 28.f * S, 18.f * S, Ivory);
    }
    if (Boss->IsStaggered())
    {
        Label(Boss->IsGuardReeling() ? TEXT("精准格挡 · 可反击") : TEXT("架势破绽"),
            W * .5f, BossY - 52.f * S, 17.f * S, Ember, true);
    }

    if (const TCHAR* Line = Mode->CombatCaption())
    {
        DrawCaption(Mode->CaptionSpeaker(), Line, W, H, S, Mode->CaptionOpacity());
    }
    DrawArea(*Mode, W, H, S);
    if (Player->IsLocked() && Boss->IsAlive())
    {
        const float Size = 26.f * S;
        Chrome(2, (W - Size) * .5f, (H - Size) * .5f, Size, Size, FLinearColor(1, 1, 1, .76f));
    }
}
