#include "DuelNorthTower.h"
#include "DuelNorthNarrative.h"
#include "DuelGame.h"
#include "DuelArena.h"
#include "Components/MeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

namespace
{
    const TCHAR* NorthCheckpointSlot() { return FParse::Param(FCommandLine::Get(),TEXT("DuelNorthSaveTest")) ? TEXT("NorthTower200IntegrationQA") : TEXT("NorthTower200Checkpoint"); }
    bool NoSave() { return FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")); }

    USpotLightComponent* AttachWayLight(ADuelFighter& Player)
    {
        auto* Light = NewObject<USpotLightComponent>(&Player, TEXT("NorthWayLight"));
        Light->SetupAttachment(Player.GetRootComponent());
        Light->SetMobility(EComponentMobility::Movable);
        // Light the next few steps and both sides of the path at once.
        Light->SetRelativeLocation(FVector(34, 0, 35));
        Light->SetRelativeRotation(FRotator(-38.f, 0, 0));
        Light->SetIntensityUnits(ELightUnits::Candelas);
        Light->SetIntensity(115.f);
        Light->SetUseTemperature(true);
        Light->SetTemperature(3200.f);
        Light->SetAttenuationRadius(430.f);
        Light->SetInnerConeAngle(35.f);
        Light->SetOuterConeAngle(46.f);
        Light->SetCastShadows(true);
        // The beam lights and is occluded by the tower, without picking up the runner's animated silhouette.
        Light->SetLightingChannels(false, false, true);
        Light->SetCastRaytracedShadows(ECastRayTracedShadow::Disabled);
        Light->SetVolumetricScatteringIntensity(0);
        Light->SetIndirectLightingIntensity(0);
        Light->SetVisibility(false);
        Player.AddInstanceComponent(Light);
        Light->RegisterComponent();
        return Light;
    }
}

void ADuelNorthTower::Save()
{
    if (NoSave()) return;
    auto* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    if (!Mode || !Mode->PlayerFighter) return;
    FString Reason;
    if (!State.Validate(Reason))
    {
        UE_LOG(LogTemp, Error, TEXT("North Tower refused inconsistent checkpoint: %s"), *Reason);
        Notice = TEXT("保存失败，请再次休整。");
        NoticeUntil = GetWorld()->GetTimeSeconds() + 9;
        return;
    }
    auto* Saved = NewObject<UDuelNorthSave>(this);
    Saved->State = State;
    Saved->Rooms = Explored.Array();
    const auto& Player = *Mode->PlayerFighter;
    Saved->Weapons = {uint8(Player.Equipped), uint8(Player.Offhand), uint8(Player.RightReserve), uint8(Player.LeftReserve)};
    Saved->Art = uint8(Player.ArtSelection);
    for (auto Spell : Player.PreparedSpells) Saved->Spells.Add(uint8(Spell));
    Saved->ActiveSpell = uint8(Player.ActiveSpellSlot);
    Saved->HealthFlasks = Player.HealthFlaskAllocation;
    Saved->bHasLoadout = true;
    if (UGameplayStatics::SaveGameToSlot(Saved, NorthCheckpointSlot(), 0)) bCheckpointAvailable = true;
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Could not save North Tower checkpoint"));
        Notice = TEXT("保存失败，请检查磁盘空间后再次休整。");
        NoticeUntil = GetWorld()->GetTimeSeconds() + 9;
    }
}

void ADuelNorthTower::Initialize(ADuelMode& Mode)
{
    State.Reset(Mode.PlayerFighter->SelectedCharacter());
    State.bOpeningCellRequired=true;
    if (!NoSave())
    {
        if (const auto* Saved = Cast<UDuelNorthSave>(UGameplayStatics::LoadGameFromSlot(NorthCheckpointSlot(), 0)))
        {
            FString Reason;
            bCheckpointAvailable = Saved->State.Validate(Reason);
        }
    }
    NarrativeActor = GetWorld()->SpawnActor<ADuelNorthNarrative>();
    BuildStoryCues();
    TInlineComponentArray<UMeshComponent*> TowerMeshes(this);
    for (auto* Mesh : TowerMeshes)
        Mesh->SetLightingChannels(Mesh->LightingChannels.bChannel0, Mesh->LightingChannels.bChannel1, true);
    WayLight = AttachWayLight(*Mode.PlayerFighter);
    Mode.PlayerFighter->ResetNorthLoadout(State.bExpeditionLoot);
    Respawn(Mode);
    Mode.PlayerFighter->SetActorLocation(DuelNorthLayout::SpawnSurface()+FVector(0,0,62),false,nullptr,ETeleportType::TeleportPhysics);
}

void ADuelNorthTower::UpdateWayLight(const ADuelMode& Mode, bool bAllowToggle)
{
    auto* Light = WayLight.Get();
    if (!Light) return;
    const bool bExploring = (Mode.Outcome() == EDuelOutcome::Ready || Mode.Outcome() == EDuelOutcome::Victory) &&
        !(NarrativeActor && NarrativeActor->ExtractionInProgress());
    if (bAllowToggle && bExploring && Mode.CanExplore() && !bMap)
    {
        const auto* Controller = GetWorld()->GetFirstPlayerController();
        if (Controller && Controller->WasInputKeyJustPressed(EKeys::G)) bWayLightEnabled = !bWayLightEnabled;
    }
    const bool bVisible = bExploring && bWayLightEnabled;
    if (Light->IsVisible() != bVisible) Light->SetVisibility(bVisible);
}

void ADuelNorthTower::NewGame(ADuelMode& Mode)
{
    State.Reset(Mode.PlayerFighter->SelectedCharacter());
    State.bOpeningCellRequired=true;
    Explored.Reset();
    Notice.Reset(); NoticeUntil=0;
    Mode.PlayerFighter->ResetNorthLoadout(State.bExpeditionLoot);
    Respawn(Mode);
    if (NarrativeActor) NarrativeActor->Restore(State,*Mode.PlayerFighter,*Mode.Arena);
    Save();
}

bool ADuelNorthTower::LoadCheckpoint(ADuelMode& Mode)
{
    if (NoSave()) return false;
    const auto* Saved = Cast<UDuelNorthSave>(UGameplayStatics::LoadGameFromSlot(NorthCheckpointSlot(),0));
    FString Reason;
    if (!Saved || !Saved->State.Validate(Reason))
    {
        bCheckpointAvailable=false;
        UE_LOG(LogTemp, Warning, TEXT("North Tower checkpoint unavailable: %s"), *Reason);
        return false;
    }
    State=Saved->State;
    State.bCompanionFollowing=State.HasFinalConversation();
    Explored.Reset();
    for (FName Room : Saved->Rooms) if (DuelNorthLayout::FindRoom(Room)) Explored.Add(Room);
    auto& Player=*Mode.PlayerFighter;
    Player.EndCombat();
    Player.SelectCharacter(State.Player());
    Player.ResetNorthLoadout(State.bExpeditionLoot);
    if (Saved->bHasLoadout)
    {
        const auto Gear = [&](int32 Index, EDuelHand Hand, EDuelWeapon Fallback)
        {
            if (!Saved->Weapons.IsValidIndex(Index)) return Fallback;
            const auto Weapon = static_cast<EDuelWeapon>(Saved->Weapons[Index]);
            return State.OwnsWeapon(Weapon) && DuelCourt::CanEquip(Weapon, State.Player(), Hand) ? Weapon : Fallback;
        };
        Player.SetWeapon(Gear(0, EDuelHand::Right, Player.Equipped));
        Player.SetWeapon(Gear(1, EDuelHand::Left, Player.Offhand), EDuelHand::Left);
        Player.RightReserve = Gear(2, EDuelHand::Right, Player.RightReserve);
        Player.LeftReserve = Gear(3, EDuelHand::Left, Player.LeftReserve);
        if (State.bExpeditionLoot && Player.RightReserve == Player.Equipped)
        {
            Player.RightReserve = Player.Equipped == EDuelWeapon::Staff ? EDuelWeapon::Rapier : EDuelWeapon::Staff;
        }
        if (State.bExpeditionLoot && Player.LeftReserve == Player.Offhand)
        {
            Player.LeftReserve = EDuelWeapon::Empty;
        }
        const auto Art=static_cast<EDuelArt>(Saved->Art);
        Player.ArtSelection=DuelCourt::SupportsArt(Art,State.Player())?Art:EDuelArt::Default;
        const auto Staff = MakeArrayView(DuelCourt::StaffSpells);
        TArray<EDuelArt, TInlineAllocator<DuelCourt::SpellSlotCount>> ValidSpells;
        if (State.bExpeditionLoot)
        {
            for (int32 Slot = 0; Slot < DuelCourt::SpellSlotCount; ++Slot)
            {
                const EDuelArt Spell = Saved->Spells.IsValidIndex(Slot) ? static_cast<EDuelArt>(Saved->Spells[Slot]) :
                    Player.PreparedSpells[Slot];
                const bool bLegal = Staff.Contains(Spell) && State.KnowsSpell(Spell) && !ValidSpells.Contains(Spell);
                Player.PreparedSpells[Slot] = bLegal ? Spell : EDuelArt::Default;
                if (bLegal) ValidSpells.Add(Spell);
            }
        }
        else
        {
            for (uint8 Value : Saved->Spells)
            {
                const auto Spell = static_cast<EDuelArt>(Value);
                if (Staff.Contains(Spell) && State.KnowsSpell(Spell)) ValidSpells.AddUnique(Spell);
            }
            for (const auto Spell : Staff) if (State.KnowsSpell(Spell)) ValidSpells.AddUnique(Spell);
            for (int32 Slot = 0; Slot < DuelCourt::SpellSlotCount; ++Slot) Player.PreparedSpells[Slot] = ValidSpells[Slot];
        }
        Player.ActiveSpellSlot = Saved->ActiveSpell < DuelCourt::SpellSlotCount ? Saved->ActiveSpell : 0;
        Player.ChooseActivePreparedSpell();
        Player.HealthFlaskAllocation=FMath::Clamp(Saved->HealthFlasks,0,DuelCourt::TotalFlasks);
    }
    Respawn(Mode);
    // Completed routes resume on the accessible garden instead of replaying the final approach.
    if(State.IsDoorOpen(EDuelNorthDoor::TowerExit))
    {
        const FName ResumeMarker = State.HasFinalConversation() ? TEXT("GardenCompanion") : TEXT("FinalExitCompanion");
        if(const auto* Place=DuelNorthLayout::FindLandmark(ResumeMarker))
            Player.SetActorLocation(Place->Position+FVector(-200,0,62),false,nullptr,ETeleportType::TeleportPhysics);
        if(NarrativeActor) NarrativeActor->Restore(State,Player,*Mode.Arena);
    }
    Notice.Reset(); NoticeUntil=0;
    return true;
}

void ADuelNorthTower::ChangePlayer(ADuelMode& Mode)
{
    if(!CanChoosePlayer()) return;
    const auto Next=State.Player()==EDuelCharacter::Ema?EDuelCharacter::Hiro:EDuelCharacter::Ema;
    if(Mode.PlayerFighter->SelectCharacter(Next))
    {
        State.PlayerCharacter=uint8(Next);
        if(NarrativeActor) NarrativeActor->Restore(State,*Mode.PlayerFighter,*Mode.Arena);
        Save();
    }
}

bool ADuelNorthTower::BlocksPlayerInput() const
{
    return ActivePuzzle != INDEX_NONE || bBossDefeatPending || bMap || (NarrativeActor && NarrativeActor->BlocksInput());
}

const ADuelNorthNarrative* ADuelNorthTower::Narrative() const { return NarrativeActor; }
