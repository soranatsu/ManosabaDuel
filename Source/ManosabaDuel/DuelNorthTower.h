#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/SaveGame.h"
#include "DuelNorthLayout.h"
#include "DuelNorthProgress.h"
#include "DuelNorthTower.generated.h"
class ADuelMode;
class ADuelNorthNarrative;
class UTexture;
class UBoxComponent;
class USpotLightComponent;
class UProceduralMeshComponent;

UCLASS()
class UDuelNorthSave final : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY() FDuelNorthProgress State;
    UPROPERTY() TArray<FName> Rooms;
    UPROPERTY() TArray<uint8> Weapons;
    UPROPERTY() uint8 Art = 0;
    UPROPERTY() TArray<uint8> Spells;
    UPROPERTY() uint8 ActiveSpell = 0;
    UPROPERTY() int32 HealthFlasks = 3;
    UPROPERTY() bool bHasLoadout = false;
};

UCLASS()
class MANOSABADUEL_API ADuelNorthTower final : public AActor
{
    GENERATED_BODY()
public:
    ADuelNorthTower();
    void Initialize(ADuelMode& Mode);
    void Update(ADuelMode& Mode, float Delta);
    void Confirm(ADuelMode& Mode);
    bool SkipPendingDialogue();
    void Respawn(ADuelMode& Mode, const FVector* SafeLanding = nullptr);
    void FinishBattle(ADuelMode& Mode, bool bWon);
    void ShowMechanicHint(int32 Battle);
    FStringView MechanicHintText(float& Age, float& Opacity) const;
    UTexture* MechanicHintPortrait() const;
    void NewGame(ADuelMode& Mode);
    bool LoadCheckpoint(ADuelMode& Mode);
    bool HasCheckpoint() const { return bCheckpointAvailable; }
    bool CanChoosePlayer() const { return State.Wins == 0 && ActiveFight == INDEX_NONE; }
    void ChangePlayer(ADuelMode& Mode);
    void Save();
    const ADuelNorthNarrative* Narrative() const;
    bool BlocksPlayerInput() const;
    FString Objective(bool bIncludeNotice = true) const;
    const TCHAR* RescueObjective() const;
    FStringView VisibleNotice() const;
    const FString& EvidenceNote() const { return Notice; }
    struct FObjectiveRoute
    {
        FVector Goal = FVector::ZeroVector;
        DuelNorthLayout::EFloor GoalFloor = DuelNorthLayout::EFloor::Unknown;
        FName StartRoom;
        TArray<const DuelNorthLayout::FPortal*> Doors;
    };
    bool BuildObjectiveRoute(const DuelNorthLayout::FLocation& From, DuelNorthLayout::EFloor PlayerFloor,
        FObjectiveRoute& Route) const;
    bool MapObjectivePosition(FVector& Position) const;
    FString Prompt() const;
    const TCHAR* RecentRelicName() const;
    int32 GetPuzzle() const { return ActivePuzzle; }
    int32 GetPuzzleRound() const { return PuzzleRound; }
    int32 GetPuzzleChoice() const { return PuzzleChoice; }
    const FString& PuzzleHint() const { return PuzzleFeedback; }
    FString RecentTreasure() const;
    bool MapVisible() const { return bMap; }
    static constexpr int32 TravelMenuBase = 100;
    static constexpr int32 TravelIconMenuBase = 110;
    static constexpr int32 TravelMenuConfirm = 200;
    bool CanFastTravel() const;
    bool CanTravelTo(DuelNorthLayout::EFloor Floor) const;
    bool IsTravelPointActivated(DuelNorthLayout::EFloor Floor) const
    { return State.IsCheckpointActivated(static_cast<EDuelNorthFloor>(Floor)); }
    bool SelectTravelPoint(DuelNorthLayout::EFloor Floor);
    void NavigateTravelPoints(ADuelMode& Mode, int32 Step);
    bool ConfirmTravel(ADuelMode& Mode);
    DuelNorthLayout::EFloor SelectedTravelPoint() const { return TravelSelection; }
    const FString& TravelMessage() const { return TravelFeedback; }
    void ClearTravelSelection() { TravelSelection = DuelNorthLayout::EFloor::Unknown; TravelFeedback.Reset(); }
    static const TCHAR* TravelPointName(DuelNorthLayout::EFloor Floor);
    bool CloseMap()
    {
        if (!bMap) return false;
        bMap = false;
        ClearTravelSelection();
        return true;
    }
    bool IsRoomExplored(FName Room) const { return Explored.Contains(Room); }
    DuelNorthLayout::EFloor CurrentFloor() const { return Location.Floor; }
    bool HasExploredFloor(DuelNorthLayout::EFloor Floor) const
    {
        return Floor < DuelNorthLayout::EFloor::Unknown && State.HasExplored(static_cast<EDuelNorthFloor>(Floor));
    }
    bool KnowsSpell(EDuelArt Art) const { return State.KnowsSpell(Art); }
    bool OwnsWeapon(EDuelWeapon Weapon) const { return State.OwnsWeapon(Weapon); }
    bool UsesExpeditionLoot() const { return State.bExpeditionLoot; }
    bool HasRelic(EDuelNorthRelic Relic) const { return State.HasRelic(Relic); }
    void SetGateOpen(FName Id, bool bOpen);
    bool IsGateOpen(FName Id) const;
protected:
    virtual void BeginPlay() override;
private:
    friend class FDuelNorthFastTravelTest;
    friend class FDuelNorthPointerMenuTest;
    friend class FDuelNorthMechanicHintsTest;
    friend class FDuelNorthFindsTest;
    friend class FDuelNorthBeaconTest;
    UPROPERTY() TArray<TObjectPtr<USceneComponent>> EvidenceTableParts;
    UPROPERTY() TObjectPtr<USceneComponent> RationBowl;
    bool InteractBeacon(ADuelMode& Mode);
    static constexpr int32 BeaconPuzzle = 2;
    friend struct FDuelNorthVisualFixture;
    friend struct FDuelDungeonChainsVisualFixture;
    friend struct FDuelJudgmentVisualFixture;
    friend class FDuelNorthIntegrationTest;
    friend class FDuelCircularSpellsTest;
    friend class FDuelNorthWalkTest;
    friend class FDuelNorthStairStressTest;
    friend class FDuelNorthPerformanceProbe;
    friend class FDuelNorthJourneyTest;
    friend class FDuelLiveMatch;
    UPROPERTY() FDuelNorthProgress State;
    UPROPERTY() TObjectPtr<ADuelNorthNarrative> NarrativeActor;
    UPROPERTY() TMap<FName,TObjectPtr<UBoxComponent>> Gates;
    UPROPERTY() TMap<FName,TObjectPtr<UProceduralMeshComponent>> StoryCues;
    TWeakObjectPtr<USpotLightComponent> WayLight;
    bool bWayLightEnabled = true;
    TSet<FName> Explored;
    TSet<FName> OpenGates;
    DuelNorthLayout::FLocation Location;
    bool bMap = false;
    DuelNorthLayout::EFloor TravelSelection = DuelNorthLayout::EFloor::Unknown;
    FString TravelFeedback;
    bool bBossDefeatPending = false;
    double BossDefeatVoiceEndsAt = 0.0;
    bool bCheckpointAvailable = false;
    float FlightEpilogueSeconds = 0.f;
    void FinishFlightEpilogue(ADuelMode& Mode);
    int32 ActiveFight = INDEX_NONE;
    int32 MechanicHintBattle = INDEX_NONE;
    double MechanicHintEncounter = -1.;
    double MechanicHintStartedAt = -1.;
    bool bMechanicHintShown = false;
    FString MechanicHintLine;
    void UpdateMechanicHint(const ADuelMode& Mode);
    FName Nearby;
    int32 ActivePuzzle = INDEX_NONE;
    int32 PuzzleRound = 0;
    int32 PuzzleChoice = 0;
    FString PuzzleFeedback;
    int32 RecentFindIndex = INDEX_NONE;
    double TreasureUntil = 0;
    bool InteractFind(ADuelMode& Mode);
    FString Notice;
    double NoticeUntil = 0;
    EDuelNorthRelic RecentRelic = EDuelNorthRelic::Count;
    double RecentRelicUntil = 0;
    void Build();
    void UpdateWayLight(const ADuelMode& Mode, bool bAllowToggle = true);
    void BuildStoryCues();
    void UpdateStoryCues(const ADuelMode& Mode);
    void RefreshGates();
    FVector LandmarkPosition(const DuelNorthLayout::FLandmark& Landmark) const;
};
