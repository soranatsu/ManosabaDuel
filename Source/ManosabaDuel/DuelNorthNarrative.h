#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DuelNorthProgress.h"
#include "DuelNorthNarrative.generated.h"

class ADuelArena;
class ADuelFighter;
class ADuelNorthExtraction;
class UProceduralMeshComponent;
class UPoseableMeshComponent;
class USceneCaptureComponent2D;
class UTexture;
class UTexture2D;
class UTextureRenderTarget2D;

struct FDuelNorthNarrativeResult
{
    bool bHandled = false;
    bool bStateChanged = false;
    bool bEndingOpened = false;
};

struct FDuelNorthSubtitle
{
    FString Speaker;
    FString Text;
    int32 Line = 0;
    int32 Count = 0;
    EDuelCharacter Character = static_cast<EDuelCharacter>(255);
    EDuelNorthPresence Presence = EDuelNorthPresence::Person;
    EDuelNorthExpression Expression = EDuelNorthExpression::Smile;
    bool bWitchPortrait = false;
};

// Transient playback cursor. Closing or skipping the scene writes story state.
struct MANOSABADUEL_API FDuelNorthDialoguePlayback
{
    bool BeginEvidence(FName Id, const FDuelNorthProgress& State);
    bool Begin(EDuelNorthDialogue Dialogue, const FDuelNorthProgress& State);
    FDuelNorthNarrativeResult Advance(FDuelNorthProgress& State, bool bSkip = false);
    void Cancel();
    bool Active() const { return Current != EDuelNorthDialogue::Count || !Evidence.IsNone(); }
    FName EvidenceId() const { return Evidence; }
    EDuelNorthDialogue Dialogue() const { return Current; }
    FDuelNorthSubtitle Subtitle() const;
    bool IsSpeaking(EDuelCharacter Character) const;
    FString VoiceKey() const;
private:
    FName Evidence;
    EDuelNorthDialogue Current = EDuelNorthDialogue::Count;
    EDuelCharacter PlayerAtStart = static_cast<EDuelCharacter>(0);
    FDuelNorthDialogueScript Script;
    int32 Line = 0;
};

/** Visual narrative adapter. Persistent state remains owned and saved by the tower.
 * Restore after load/respawn; Update before interaction selection; Confirm before gate/pickup handlers.
 * The adapter uses public fighter APIs and never takes ownership of the combat boss.
 */
UCLASS()
class MANOSABADUEL_API ADuelNorthNarrative final : public AActor
{
    GENERATED_BODY()
public:
    ADuelNorthNarrative();
    void Restore(const FDuelNorthProgress& State, ADuelFighter& Player, ADuelArena& Arena);
    void DiscussEvidence(FName Id, const FDuelNorthProgress& State, const FString& Document, const FString& Title);
    void PlayBossDefeat(int32 Battle, const FDuelNorthProgress& State);
    bool Update(FDuelNorthProgress& State, ADuelFighter& Player, float DeltaSeconds, bool bExploring);
    FDuelNorthNarrativeResult Confirm(FName Marker, FDuelNorthProgress& State);
    FDuelNorthNarrativeResult SkipDialogue(FDuelNorthProgress& State);
    FDuelNorthSubtitle Subtitle() const;
    FString Prompt(FName Marker, const FDuelNorthProgress& State) const;
    bool IsReadingEvidence() const { return !Playback.EvidenceId().IsNone(); }
    bool IsReadingDocument() const { return !EvidenceDocument.IsEmpty(); }
    const FString& DocumentText() const { return EvidenceDocument; }
    const FString& DocumentTitle() const { return EvidenceDocumentTitle; }
    int32 DocumentVisibleCharacters() const;
    bool IsTalking() const { return Playback.Active() || IsReadingDocument(); }
    UTexture* DialoguePortrait() const;
    void PreloadWitchPortrait(EDuelCharacter Character);
    UTexture* WitchPortrait(EDuelCharacter Character) const;
    void PreloadThoughtPortrait(EDuelCharacter Character);
    UTexture* ThoughtPortrait(EDuelCharacter Character) const;
    double DialogueLineStarted() const { return DialogueLineStartedAt; }
    bool UsesLocalPortrait() const { return LocalPortrait != nullptr; }
    bool EndingVisible() const { return bEndingVisible || ExtractionEndingVisible(); }
    bool ExtractionEndingVisible() const;
    bool ExtractionInProgress() const;
    bool WantsEndingMusic() const;
    bool BlocksInput() const { return IsTalking() || bEndingVisible || ExtractionInProgress(); }
    FVector TowerSamplePosition(const FVector& PlayerAt) const;
    const ADuelFighter* Companion() const { return RealCompanion; }
    // Following changes the partner's position; use this when selecting a nearby interaction.
    bool InteractionPosition(FName Marker, const FDuelNorthProgress& State, FVector& Position) const;
    static EDuelNorthDialogue DialogueAt(FName Marker);
    static FName MarkerFor(EDuelNorthDialogue Dialogue);
    // Pure dialogue markers retire after playback; MirrorGate remains a usable fog gate.
    static bool IsRetiredEcho(FName Marker, const FDuelNorthProgress& State);
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    friend class FDuelNorthNarrativeRuntimeTest;
    friend class FDuelNorthFollowFurnitureTest;
    UPROPERTY() TObjectPtr<ADuelFighter> RealCompanion;
    UPROPERTY() TMap<FName, TObjectPtr<UProceduralMeshComponent>> Inscriptions;
    UPROPERTY() TObjectPtr<ADuelArena> ArenaRef;
    UPROPERTY() TObjectPtr<ADuelFighter> PlayerRef;
    UPROPERTY() TObjectPtr<ADuelNorthExtraction> Extraction;
    FDuelNorthDialoguePlayback Playback;
    UPROPERTY() TObjectPtr<UTexture2D> LocalPortrait;
    UPROPERTY() TMap<uint8, TObjectPtr<UTexture2D>> WitchPortraits;
    UPROPERTY() TMap<uint8, TObjectPtr<UTexture2D>> ThoughtPortraits;
    UPROPERTY() TObjectPtr<UPoseableMeshComponent> PortraitMesh;
    UPROPERTY() TObjectPtr<USceneCaptureComponent2D> PortraitCamera;
    UPROPERTY() TObjectPtr<UTextureRenderTarget2D> PortraitTarget;
    double DialogueLineStartedAt = -1.0;
    FString EvidenceDocument;
    FString EvidenceDocumentTitle;
    double DocumentStartedAt = -1.0;
    bool bDocumentRevealed = false;
    void ClearEvidenceDocument();
    double NextPortraitCaptureAt = 0.0;
    void PresentDialogueLine();
    void ClearDialogueMedia();
    void UpdateDialoguePortrait(double Now);
    TMap<FName, float> EchoFadeElapsed;
    TArray<FVector> FollowTrail;
    TArray<FVector> FollowDetour;
    FVector FollowDetourTarget = FVector::ZeroVector;
    float FollowRepathDelay = 0.f;
    bool bEndingVisible = false;
    bool bWasFollowing = false;
    bool bExplorationEnabled = false;
    enum class ECompanionStage : uint8 { Absent, Rescue, MirrorExit, FinalExit, Following };
    ECompanionStage RealStage = ECompanionStage::Absent;
    EDuelCharacter CompanionCharacter = static_cast<EDuelCharacter>(255);
    ADuelFighter* SpawnFigure(EDuelCharacter Character, const FVector& Surface);
    void RebuildFigures(const FDuelNorthProgress& State);
    void SyncFigures(const FDuelNorthProgress& State, ADuelFighter& Player, bool bRestore);
    void Follow(ADuelFighter& Player, float DeltaSeconds);
    void ResetFollow();
    bool FindFollowFloor(const FVector& Approximate, FVector& Standing) const;
    bool FindFollowPlacement(const FVector& Origin, FVector& Standing, bool bRecovery) const;
    bool BuildFollowDetour(const FVector& From, const FVector& Target, const AActor* FurnitureOwner);
    void SetFigureVisible(ADuelFighter* Figure, bool bVisible);
    void UpdateEchoFades(float DeltaSeconds);
    FDuelNorthNarrativeResult AdvanceDialogue(FDuelNorthProgress& State, bool bSkip);
    void UpdateEndingMusic(bool bPreviouslyWanted);
};
