#pragma once

#include "CoreMinimal.h"
#include "DuelNorthProgress.generated.h"

enum class EDuelCharacter : uint8;
enum class EDuelArt : uint8;
enum class EDuelWeapon : uint8;

// Values are save-format identifiers; keep their order stable.
enum class EDuelNorthFloor : uint8 { Dungeon, Prison, Chapel, Mirror, Final, Garden, Beacon, Count, Unknown = 255 };
enum class EDuelNorthRelic : uint8 { RustKey, MirrorKey, NameTag, Count };
enum class EDuelNorthDoor : uint8 { Chapel, FinalStair, TowerExit, Count };
enum class EDuelNorthClue : uint8 { CellVerdict, WaterGateLog, ChapelJudgments, SeaMark, OwnVerdict, Signature, FinalSeal, RefugeLedger, SealPrototype, ReleaseOrder, OpeningDocket, ArchiveTransferLedger, ArchiveSignatureRegister, ArchiveBindingNote, ArchiveNightIndex, Count };
enum class EDuelNorthDialogue : uint8 { NameWall, LongTable, DungeonDoor, RescuedCompanion, ChapelBench, MirrorDoor, Garden, MirrorExit, FinalExit, CellEcho, WaterGateEcho, ArchiveConversation, FinalWarning,
    DungeonBossDefeat, MirrorBossDefeat, FinalBossDefeat, Count };
enum class EDuelNorthShortcut : uint8 { DungeonReturn, ChapelLoop, Count };
enum class EDuelNorthPresence : uint8 { Echo, Voice, Person };
enum class EDuelNorthExpression : uint8 { Neutral, Concern, Resolve, Soft, Startled, Angry, Urgent, Smile };

struct FDuelNorthDialogueLine
{
    EDuelCharacter Speaker;
    FString Text;
    EDuelNorthExpression Expression = EDuelNorthExpression::Smile;
    bool bDefeatedBoss = false;
};

struct FDuelNorthDialogueScript
{
    EDuelNorthPresence Presence = EDuelNorthPresence::Echo;
    TArray<FDuelNorthDialogueLine> Lines;
};

struct FDuelNorthObjective
{
    EDuelNorthFloor Floor = EDuelNorthFloor::Prison;
    // Stable semantic IDs. The map/interaction layer resolves these to world positions.
    FName Target;
    FString Text;
};

/** Pure story state. Embed as a UPROPERTY in the separate North Tower save slot.
 * A successful transition returns true only when persistent state changed.
 * World proximity, facing and combat activity are checked by the caller.
 */
USTRUCT()
struct MANOSABADUEL_API FDuelNorthProgress
{
    GENERATED_BODY()

    static constexpr uint8 CurrentFormat = 1;
    static constexpr int32 BattleCount = 3;

    UPROPERTY() uint8 Format = CurrentFormat;
    UPROPERTY() uint8 PlayerCharacter = 0;
    UPROPERTY() uint8 Wins = 0;
    UPROPERTY() uint8 Relics = 0;
    UPROPERTY() uint8 Doors = 0;
    UPROPERTY() uint8 Clues = 0;
    UPROPERTY() uint8 Dialogues = 0;
    UPROPERTY() uint8 Shortcuts = 0;
    UPROPERTY() uint8 SideClues = 0;
    // Bits 0–1 hold the reunions from 3.3; later bits hold additional conversations.
    UPROPERTY() uint8 Reunions = 0;
    // Absent in older saves, which already had access to the main corridor.
    UPROPERTY() bool bOpeningCellRequired = false;
    UPROPERTY() bool bOpeningCellUnlocked = false;
    UPROPERTY() bool bPrisonRestActivated = false;
    UPROPERTY() uint8 ExploredFloors = 1 << static_cast<uint8>(EDuelNorthFloor::Prison);
    UPROPERTY() uint8 VisitedCheckpoints = 1 << static_cast<uint8>(EDuelNorthFloor::Prison);
    // Actual rest interactions. Older saves' prefilled Prison bit is not proof of activation.
    UPROPERTY() uint8 ActivatedCheckpoints = 0;
    UPROPERTY() uint8 Checkpoint = static_cast<uint8>(EDuelNorthFloor::Prison);
    UPROPERTY() bool bCompanionFollowing = false;
    UPROPERTY() bool bEndingSeen = false;
    UPROPERTY() bool bExtractionSeen = false;
    UPROPERTY() uint8 BeaconBits = 0;
    bool HasBeacon(FName Id) const;
    bool RecordBeacon(FName Id);
    FString BeaconEvidenceText(FName Id) const;
    FDuelNorthDialogueScript InvestigationScript(FName EvidenceId) const;
    UPROPERTY() uint32 CacheBits = 0;
    // Older checkpoints preserve their original loadout and unlock rules.
    UPROPERTY() bool bExpeditionLoot = false;

    void Reset(EDuelCharacter Player);
    EDuelCharacter Player() const;
    EDuelCharacter Companion() const;
    EDuelCharacter BossForBattle(int32 Battle) const;
    static EDuelNorthDialogue BossDefeatDialogue(int32 Battle);

    bool HasWon(int32 Battle) const;
    bool HasInvestigation(int32 Battle) const;
    bool CanEnterBattle(int32 Battle) const;
    bool WinBattle(int32 Battle);
    bool HasRelic(EDuelNorthRelic Relic) const;
    bool RelicAvailable(EDuelNorthRelic Relic) const;
    bool PickupRelic(EDuelNorthRelic Relic);
    bool IsDoorOpen(EDuelNorthDoor Door) const;
    bool CanOpenDoor(EDuelNorthDoor Door) const;
    bool OpenDoor(EDuelNorthDoor Door);
    bool HasClue(EDuelNorthClue Clue) const;
    bool CanReadClue(EDuelNorthClue Clue) const;
    bool ReadClue(EDuelNorthClue Clue);
    bool HasDialogue(EDuelNorthDialogue Dialogue) const;
    bool DialogueAvailable(EDuelNorthDialogue Dialogue) const;
    bool HasFinalConversation() const;
    // Closing or deliberately skipping playback commits the same progress.
    bool CompleteDialogue(EDuelNorthDialogue Dialogue);
    FDuelNorthDialogueScript DialogueScript(EDuelNorthDialogue Dialogue) const;
    FString ClueText(EDuelNorthClue Clue) const;
    bool KnowsSpell(EDuelArt Spell) const;
    bool OwnsWeapon(EDuelWeapon Weapon) const;

    bool CanVisitFloor(EDuelNorthFloor Floor) const;
    bool HasExplored(EDuelNorthFloor Floor) const;
    bool ExploreFloor(EDuelNorthFloor Floor);
    bool CanActivateCheckpoint(EDuelNorthFloor Floor) const;
    bool ActivateCheckpoint(EDuelNorthFloor Floor);
    bool IsCheckpointActivated(EDuelNorthFloor Floor) const;
    bool IsOpeningCellOpen() const { return !bOpeningCellRequired || bOpeningCellUnlocked; }
    bool OpenStartingCell();
    bool UsesStartingCellCheckpoint() const;
    bool IsShortcutOpen(EDuelNorthShortcut Shortcut) const;
    bool OpenShortcut(EDuelNorthShortcut Shortcut);
    bool CanViewEnding() const;
    bool ViewEnding();
    bool CompleteExtraction();

    FDuelNorthObjective NextObjective() const;
    // Reject unknown/corrupt saves; never silently grant keys or victories.
    bool Validate(FString& Reason) const;

    static const TCHAR* FloorName(EDuelNorthFloor Floor);
    static const TCHAR* RelicName(EDuelNorthRelic Relic);
    static const TCHAR* ClueTitle(EDuelNorthClue Clue);
    static const TCHAR* EndingText();
    static const TCHAR* ExtractionEndingText();
};
