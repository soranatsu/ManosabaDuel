#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "DuelGraphicsSettings.h"
#include "DuelGame.generated.h"

class ADuelArena;
class ADuelNorthTower;
class ACameraActor;
class UCameraComponent;
class UPoseableMeshComponent;
class UTexture;
class USpringArmComponent;
enum class EDuelVoice : uint8;
namespace DuelNorthLayout { enum class EFloor : uint8; }

enum class EDuelMove : uint8
{
    Idle, Cut, Backcut, Heavy, Crimson, Parry, Equip, Roll, Backstep, Flask, Hurt, Stagger,
    Paw, Leap, Pillar, Storm, WingSweep, Seals, Cross, Awaken, Eclipse, AtlasRain, TalonRush, NightLanes, InwardReap,
    FlankLeap, Pursuit, ShadowSweep, WingVerdict, Tribunal, Reversal, ShadowSnare, RakingClaws, HaloBreak,
    Wingfall, ShadowVolley, GuardBreak, BoneProcession, FeintCounter, WrittenVerdict, CrossAmbush,
    GlassShelter, BrokenRecord, SilentBell, VerdictTether, MirrorEdict, GuardReel, PrismPursuit, ArchiveCollapse,
    OssuaryChain, HoundChain, FrostStaffChain, IcicleRow, OssuaryShears, HoundShade, MirrorRicochet, MirrorRiposte,
    FinalSentence
};

enum class EDuelWeapon : uint8 { Rapier, Greatblade, Spear, Axe, Hammer, Houndfang, Staff, YukiPen, FirePoker, Buckler, Greatshield, Empty, RitualSword };
enum class EDuelArt : uint8
{
    Default, Orb, Burst, Thrust, Whirl, HiroStep, HiroBind, EmaLight, EmaWard, HiroThreads, EmaVotive, HiroReprise, EmaAbsolution,
    HoundFinesse, HoundChase, PenScript, PokerRake, IceLance, WitchRain, SableBlade,
    HiroCrossThread, EmaAegis, RitualOffering,
    LanternMote, Blackwell, SaltArc, WhiteMantle, WitnessStep, StarTrack, ReturningFrost, PrismSeal, EmberRecoil, GildedCross, MoonCrescent
};
enum class EDuelHand : uint8 { Right, Left };
enum class EDuelCharacter : uint8 { Hiro, Ema, Yuki };
enum class EDuelItem : uint8 { Flask, FocusFlask };
enum class EDuelOutcome : uint8 { Ready, Fighting, Victory, Defeat, Archive, Passage, Title, Epilogue };
enum class EDuelEnding : uint8 { Undecided, SubmitReceipt, CallWitness };
enum class EDuelPause : uint8 { Resume, Player, Boss, Loadout, Controls, Checkpoint, Quit, Subtitles, Title, Graphics, Inventory };
enum class EDuelMenuPage : uint8 { None, Title, Pause, Controls, Equipment, EquipmentChoices, Graphics, Map, Inventory };

namespace DuelCourt
{
    inline constexpr EDuelArt StaffSpells[] = {
        EDuelArt::IceLance, EDuelArt::WitchRain, EDuelArt::SableBlade, EDuelArt::LanternMote,
        EDuelArt::Blackwell, EDuelArt::SaltArc, EDuelArt::WhiteMantle, EDuelArt::WitnessStep,
        EDuelArt::StarTrack, EDuelArt::ReturningFrost, EDuelArt::PrismSeal, EDuelArt::EmberRecoil, EDuelArt::GildedCross, EDuelArt::MoonCrescent
    };
    inline constexpr float PlayerSprintSpeed = 680.f;
    constexpr int32 SpellSlotCount = 3;
    constexpr EDuelWeapon PersonalWeapon(EDuelCharacter Character)
    {
        return Character == EDuelCharacter::Ema ? EDuelWeapon::YukiPen : EDuelWeapon::FirePoker;
    }
    constexpr float ReturningFrostInner = 110.f;
    constexpr float ReturningFrostRecall = .25f;
    constexpr float PrismSealApothem = .8660254f;
    constexpr float EmberRecoilInner = .24f;
    constexpr float EmberRecoilHalfArc = 72.f;
    constexpr float HalfLength = 1680.f;
    constexpr float HalfWidth = 1050.f;
    constexpr float StormRadius[] = {725.f, 910.f, 1080.f};
    constexpr float CameraLimitX = 1990.f;
    constexpr float CameraLimitY = 1345.f;
    constexpr float GateX = -2160.f;
    constexpr float GateCrossX = -2010.f;
    constexpr float OuterGalleryHalfWidth = 1540.f;
    constexpr float ApproachMinX = -10120.f;
    constexpr float LowCourtZ = -300.f;
    constexpr float StairBaseX = -8380.f;
    constexpr float StairTopX = -7430.f;
    constexpr float PrisonGateX = -6240.f;
    inline const FVector Warrant(-6880, 772, 0);
    constexpr float RollMotionDelay = .012f;
    constexpr float RollGuardDelay = .048f;
    constexpr float RollActiveTime = .285f;
    constexpr float RollRecoveryTime = .14f;
    constexpr float RollMotionTime = .438f;
    constexpr float RollChainAt = .405f;
    constexpr float RollDistance = 345.f;
    constexpr float BossDefeatDuration = 1.55f;
    constexpr float HoundStepDistance = 86.f;
    constexpr float PrismSpeed = 1860.f;
    constexpr float ArchiveOuter = 1140.f;
    constexpr float ArchiveInner = 136.f;
    constexpr float ArchiveSweepTime = .83f;
    constexpr float ArchiveReturnAt = ArchiveSweepTime + RollChainAt + .04f;
    constexpr float ArchiveReturnTime = .68f;
    constexpr bool IsYukiCast(EDuelMove Move)
    {
        return (Move >= EDuelMove::GlassShelter && Move <= EDuelMove::MirrorEdict) ||
            Move == EDuelMove::PrismPursuit || Move == EDuelMove::ArchiveCollapse || Move == EDuelMove::IcicleRow ||
            Move == EDuelMove::FinalSentence;
    }
    constexpr float ArchiveGateX = 2280.f;
    constexpr float ArchiveEntryX = 2740.f;
    constexpr float ArchiveEndX = 4820.f;
    constexpr float ArchiveDeskX = 4280.f;
    constexpr float AppealRampStartX = -2860.f;
    constexpr float AppealRampEndX = -2280.f;
    constexpr float AppealLowerZ = -165.f;
    constexpr float VerdictDeskX = 3270.f;
    constexpr float VerdictRise = 96.f;
    constexpr float VerdictEndX = 3650.f;
    inline const FVector AppealOrigin(10800, 0, 370);
    constexpr float YukiStairRise = 460.f;
    constexpr float YukiStairStartX = -5390.f;
    constexpr float YukiStairEndX = -3060.f;
    inline const FVector YukiOrigin = AppealOrigin + FVector(10060, 0, VerdictRise + YukiStairRise);
    constexpr float YukiRestX = -2680.f;
    constexpr float YukiArchiveExitX = 3640.f;
    constexpr float YukiExitX = 2160.f;
    constexpr float YukiGardenX = 2470.f;
    inline const FVector AppealPassage(4240, -595, 0);
    inline const FVector PassageTurn(7110, -1140, 205);
    inline const FVector RitualMarks[] = {{3220, -475, 96}, {3220, 475, 96}, {3520, 0, 96}};
    constexpr float BossStageHealth[] = {540.f, 560.f, 520.f};
    constexpr float BossMaxHealth = BossStageHealth[0] + BossStageHealth[1] + BossStageHealth[2];
    constexpr float PlayerMaxHealth = 160.f;
    constexpr float BossDamageScale = (PlayerMaxHealth / 145.f) * 1.12f;
    constexpr float MaxStamina = 130.f;
    constexpr float StaminaCostScale = .85f;
    constexpr float ParryStaminaCost = 8.f * StaminaCostScale;
    constexpr float ChargeReadyFraction = .82f;

    constexpr float ChargeExtension(EDuelWeapon Weapon, EDuelArt Art)
    {
        if (Weapon == EDuelWeapon::Axe && Art == EDuelArt::Whirl) return .46f;
        if (Weapon != EDuelWeapon::Staff) return 0.f;
        if (Art == EDuelArt::IceLance) return .38f;
        return Art == EDuelArt::GildedCross || Art == EDuelArt::MoonCrescent ? .52f : 0.f;
    }

    struct FShieldResponse
    {
        float Chip, CostBase, CostPerDamage, BlockDot, ParryWindow, ParryPosture;
    };
    constexpr FShieldResponse ShieldResponse(EDuelWeapon Shield)
    {
        return Shield == EDuelWeapon::Greatshield ? FShieldResponse{.06f, 7.f, .26f, .05f, .12f, 49.f} :
            FShieldResponse{.17f, 10.f, .35f, .20f, .18f, 36.f};
    }
    constexpr bool AllowsFreeLocomotion(EDuelMove Move)
    {
        return Move == EDuelMove::Idle || Move == EDuelMove::Equip || Move == EDuelMove::Flask;
    }
    constexpr float MaxFocus = 110.f;
    constexpr float StaffSpellCostScale = MaxFocus / 175.f;
    constexpr float WeaponArtCostScale = 1.25f;
    constexpr int32 TotalFlasks = 6;
    constexpr int32 DefaultHealthFlasks = 3;
    constexpr int32 DefaultFocusFlasks = TotalFlasks - DefaultHealthFlasks;
    constexpr float FlaskRestoreFraction = .5f;
    constexpr float MaxPosture = 100.f;
    constexpr float RitualOfferingDuration = 15.f;
    constexpr float RitualOfferingHealthCost = .70f;
    constexpr float RitualOfferingDamage = 2.f;
    constexpr float RitualOfferingPosture = 1.5f;
    constexpr float WingThreshold = (BossStageHealth[1] + BossStageHealth[2]) / BossMaxHealth;
    constexpr float EclipseThreshold = BossStageHealth[2] / BossMaxHealth;
    constexpr float ReapOuter = 1000.f;
    constexpr float ReapInner = 225.f;
    constexpr float ReversalStepTime = .36f;
    constexpr float ReversalCueTime = .40f;
    constexpr float ReversalHalfWidth(int32 Phase) { return 70.f + 14.f * Phase; }
    constexpr float ReversalReach(int32 Phase) { return 430.f + 64.f * Phase; }
    constexpr float TailSweepInner = 78.f;
    constexpr float TailSweepArc(int32 Phase) { return 132.f + 6.f * Phase; }
    constexpr float TailSweepReach(int32 Phase) { return 190.f + 112.f * Phase; }
    constexpr float SnareWidth = 25.f;
    constexpr float SnareLength(int32 Phase) { return 960.f + Phase * 110.f; }
    constexpr float SnareRadius(int32 Phase) { return 158.f + 25.f * Phase; }

    constexpr float BossHealthFloor(int32 Phase)
    {
        return Phase == 1 ? BossStageHealth[1] + BossStageHealth[2] : (Phase == 2 ? BossStageHealth[2] : 0.f);
    }

    struct FMoveTiming
    {
        float Windup;
        float Active;
        float Recovery;
        float Cost = 0.f;
        float Distance = 0.f;
        float Total() const { return Windup + Active + Recovery; }
    };

    struct FStrike
    {
        FMoveTiming Timing;
        float Reach;
        float Damage;
        float Posture;
    };

    struct FArmament
    {
        const TCHAR* Name;
        EDuelArt NativeArt;
        FLinearColor Glow;
        FStrike Cut;
        FStrike Follow;
        FStrike Heavy;
    };

    struct FWeaponArt
    {
        const TCHAR* Name;
        const TCHAR* Description;
        FLinearColor Glow;
        FStrike Strike;
        float FocusCost;
    };

    const FArmament& Armament(EDuelWeapon Weapon);
    EDuelArt ResolveArt(EDuelWeapon Weapon, EDuelArt Selection);
    EDuelArt NextArt(EDuelArt Current, EDuelCharacter Character, int32 Step);
    bool SupportsArt(EDuelArt Art, EDuelCharacter Character);
    bool IsBladeArt(EDuelArt Art);
    const FWeaponArt& WeaponArt(EDuelArt Art);
    int32 FocusCost(EDuelArt Art);
    bool CanEquip(EDuelWeapon Weapon, EDuelCharacter Character, EDuelHand Hand);
    EDuelWeapon NextWeapon(EDuelWeapon Current, EDuelHand Hand, EDuelCharacter Character, int32 Step, EDuelWeapon Other);
    const TCHAR* EquipmentName(EDuelWeapon Weapon);
    const TCHAR* EquipmentEffect(EDuelWeapon Weapon);
    const FStrike* Strike(EDuelWeapon Weapon, EDuelMove Move, EDuelArt Art = EDuelArt::Default);
    FMoveTiming Timing(EDuelMove Move, bool bAwakened, EDuelWeapon Weapon = EDuelWeapon::Rapier,
        EDuelArt Art = EDuelArt::Default);
    bool IsBlade(EDuelWeapon Weapon);
    bool IsShield(EDuelWeapon Weapon);
    FVector2D BladeSpan(EDuelWeapon Weapon);

    inline FTransform Start(bool bBoss, const FVector& Origin = FVector::ZeroVector)
    {
        return bBoss
            ? FTransform(FRotator(0, 180, 0), Origin + FVector(480, 0, 103))
            : FTransform(FRotator::ZeroRotator, Origin + FVector(-1360, 0, 62));
    }

    inline FTransform Checkpoint()
    {
        return FTransform(FRotator::ZeroRotator, FVector(-4110, 0, 62));
    }

    inline FTransform LowerCheckpoint()
    {
        return FTransform(FRotator::ZeroRotator, FVector(-9560, -175, LowCourtZ + 62.f));
    }

    inline FTransform AppealCheckpoint()
    {
        return FTransform(FRotator::ZeroRotator, AppealOrigin + FVector(-3120, 0, AppealLowerZ + 62.f));
    }

    inline FTransform YukiCheckpoint()
    {
        return FTransform(FRotator::ZeroRotator, YukiOrigin + FVector(YukiRestX, 0, 62));
    }
}

UCLASS()
class MANOSABADUEL_API ADuelFighter final : public ACharacter
{
    GENERATED_BODY()

public:
    ADuelFighter();
    virtual void Tick(float DeltaSeconds) override;
    virtual void CalcCamera(float DeltaSeconds, FMinimalViewInfo& View) override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;

    void ConfigureBoss(EDuelCharacter Selection = EDuelCharacter::Ema);
    void SetEncounter(ADuelFighter* Opponent, ADuelArena* InArena);
    void ResetFighter(bool bKeepPosition = false);
    void RestAtCheckpoint();
    bool ReceiveDuelDamage(float Damage, const FVector& Source, bool bParryable = false,
        const FVector* Contact = nullptr, bool bProjectile = false, bool bRecoil = true, bool bBlockable = false);
    void DamagePosture(float Amount);
    void EndCombat();
    void PrepareExploration();
    bool ShowCharacterForm(bool bAlternate);
    bool SelectCharacter(EDuelCharacter Selection);
    EDuelCharacter SelectedCharacter() const { return Character; }
    const TCHAR* CharacterName() const
    {
        return Character == EDuelCharacter::Yuki ? TEXT("月代雪") : Character == EDuelCharacter::Ema ? TEXT("艾玛") : TEXT("希罗");
    }
    void SetCharacterPreview(bool bPreview);
    void SetNorthExtractionPose(float RungCycle, float Weight, bool bSeated);
    void SetDialogueSpeaking(bool bSpeaking) { bDialogueSpeaking = bSpeaking; }

    float HealthRatio() const { return Health / (bBoss ? DuelCourt::BossMaxHealth : DuelCourt::PlayerMaxHealth); }
    float StaminaRatio() const { return Stamina / DuelCourt::MaxStamina; }
    float FocusMax() const { return DuelCourt::MaxFocus; }
    float FocusRatio() const { return Focus / FocusMax(); }
    bool HasFocus(EDuelArt Art) const;
    float PostureRatio() const { return Posture / DuelCourt::MaxPosture; }
    int32 FlaskCount() const { return Flasks; }
    int32 ItemCount() const { return Item == EDuelItem::Flask ? Flasks : FocusFlasks; }
    int32 ItemCount(EDuelItem Slot) const { return Slot == EDuelItem::Flask ? Flasks : FocusFlasks; }
    EDuelItem EquippedItem() const { return Item; }
    bool IsGuarding() const;
    bool IsGuarding(EDuelHand Hand) const;
    bool CanUseEquipment() const;
    float JudgmentMoveLockSeconds() const;
    float JudgmentWeaponLockSeconds() const;
    bool JudgmentImmune() const;
    int32 Phase() const { return BossPhase; }
    bool IsAwakened() const { return BossPhase >= 2; }
    bool IsStaggered() const { return Move == EDuelMove::Stagger || Move == EDuelMove::GuardReel; }
    bool IsGuardReeling() const { return Move == EDuelMove::GuardReel; }
    int32 RecentDamage() const;
    bool IsGuardBroken() const { return Move == EDuelMove::GuardBreak; }
    bool HasAegis() const;
    float RitualOfferingSeconds() const;
    EDuelWeapon EquippedWeapon(EDuelHand Hand = EDuelHand::Right) const { return Hand == EDuelHand::Right ? Equipped : Offhand; }
    EDuelHand ArtHand() const;
    EDuelArt SelectedArt() const { return ArtSelection; }
    EDuelArt ActiveArt() const;
    int32 SpellSlot() const { return ActiveSpellSlot; }
    EDuelArt SpellAt(int32 Slot) const { return PreparedSpells[Slot]; }
    EDuelArt SelectedSpell() const { return SpellAt(SpellSlot()); }
    EDuelWeapon ReserveWeapon(EDuelHand Hand) const { return Hand == EDuelHand::Right ? RightReserve : LeftReserve; }
    int32 FlaskAllocation() const { return HealthFlaskAllocation; }
    bool IsAlive() const { return Health > 0.f; }
    bool IsBossDefeatComplete() const { return bBoss && !IsAlive() && Fall >= 1.f; }
    bool IsLocked() const { return bLockOn; }
    FVector LockPoint() const;

protected:
    virtual void BeginPlay() override;
    virtual void Landed(const FHitResult& Hit) override;

private:
    friend class FDuelMatchSmoke;
    friend class FDuelBossDefenseQA;
    friend class FDuelCombatMechanicsQA;
    friend class FDuelShieldGroundMagicQA;
    friend class FDuelFlyingBodyQA;
    friend class FDuelCourtInteractionsQA;
    friend class FDuelJudgment347QA;
    friend struct FDuelJudgmentVisualFixture;
    friend class FDuelDungeonChainsQA;
    friend struct FDuelDungeonChainsVisualFixture;
    friend class FDuelNorthMechanicHintsTest;
    friend class FDuelNorthFastTravelTest;
    friend class FDuelCircularSpellsTest;
    friend class FDuelNorthIntegrationTest;
    friend class FDuelNorthWalkTest;
    friend class FDuelNorthStairStressTest;
    friend class FDuelNorthPerformanceProbe;
    friend class FDuelNorthJourneyTest;
    friend class ADuelNorthTower;
    friend struct FDuelVisualQA;
    friend struct FDuelNorthVisualFixture;
    friend class ADuelMode;
    friend class ADuelArena;
    friend class FDuelAppealSmoke;
    friend class FDuelLiveMatch;
    friend struct FDuelLiveCombat;
    friend class FDuelNorthCombatCameraTest;
    friend class FDuelNorthGardenCameraTest;
    friend class FDuelNorthPointerMenuTest;
    friend class FDuelFinaleSmoke;
    friend class FDuelCheckpointSmoke;
    friend class FDuelCastleSmoke;
    friend class FDuelCastleProgress;

    UPROPERTY() TObjectPtr<USceneComponent> Figure;
    UPROPERTY() TObjectPtr<UPoseableMeshComponent> CharacterMesh;
    UPROPERTY() TObjectPtr<USceneComponent> Head;
    UPROPERTY() TObjectPtr<USceneComponent> Mouth;
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> Breath;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> BreathMaterial;
    UPROPERTY() TObjectPtr<USceneComponent> Tail;
    UPROPERTY() TObjectPtr<USceneComponent> Weapon;
    UPROPERTY() TObjectPtr<USceneComponent> LeftWeapon;
    UPROPERTY() TArray<TObjectPtr<USceneComponent>> WeaponModels;
    UPROPERTY() TArray<TObjectPtr<USceneComponent>> LeftWeaponModels;
    UPROPERTY() TObjectPtr<USceneComponent> FlaskProp;
    UPROPERTY() TArray<TObjectPtr<USceneComponent>> Limbs;
    UPROPERTY() TArray<TObjectPtr<USceneComponent>> Hinges;
    UPROPERTY() TArray<TObjectPtr<USceneComponent>> Wings;
    UPROPERTY() TArray<TObjectPtr<USceneComponent>> ShadowClaws;
    UPROPERTY() TArray<TObjectPtr<USceneComponent>> YukiPrisms;
    UPROPERTY() TObjectPtr<USceneComponent> ShadowTail;
    UPROPERTY() TObjectPtr<USpringArmComponent> CameraBoom;
    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    UPROPERTY() TObjectPtr<ADuelFighter> Target;
    UPROPERTY() TObjectPtr<ADuelArena> Arena;

    EDuelMove Move = EDuelMove::Idle;
    uint32 MoveSerial = 0;
    double JudgmentMoveUntil = 0., JudgmentWeaponUntil = 0.;
    EDuelMove QueuedMove = EDuelMove::Idle;
    EDuelWeapon Equipped = EDuelWeapon::Rapier;
    EDuelWeapon Offhand = EDuelWeapon::Buckler;
    EDuelWeapon MoveWeapon = EDuelWeapon::Rapier;
    EDuelWeapon PendingWeapon = EDuelWeapon::Rapier;
    EDuelWeapon RightReserve = EDuelWeapon::Greatblade;
    EDuelWeapon LeftReserve = EDuelWeapon::Spear;
    EDuelArt ArtSelection = EDuelArt::Default;
    EDuelArt MoveArt = EDuelArt::Default;
    TStaticArray<EDuelArt, DuelCourt::SpellSlotCount> PreparedSpells{
        DuelCourt::StaffSpells[0], DuelCourt::StaffSpells[1], DuelCourt::StaffSpells[2]};
    int32 ActiveSpellSlot = 0;
    EDuelHand MoveHand = EDuelHand::Right;
    EDuelHand QueuedHand = EDuelHand::Right;
    EDuelHand PendingHand = EDuelHand::Right;
    EDuelCharacter Character = EDuelCharacter::Hiro;
    EDuelItem Item = EDuelItem::Flask;
    float MoveAge = 0.f;
    float StaggerDurationScale = 1.f;
    float Health = DuelCourt::PlayerMaxHealth;
    float DamageBurst = 0.f;
    double DamageBurstUntil = 0.0;
    float Stamina = DuelCourt::MaxStamina;
    float Focus = DuelCourt::MaxFocus;
    float Posture = 0.f;
    float PostureDelay = 0.f;
    float RecoveryDelay = 0.f;
    float BossPause = 1.2f;
    float Gait = 0.f;
    float Fall = 0.f;
    float FormProgress = 0.f;
    float ViewYaw = 0.f;
    float ViewPitch = -15.f;
    float GardenCameraBlend = 0.f;
    bool bGardenCameraManual = false;
    float ForwardAxis = 0.f;
    float RightAxis = 0.f;
    float Travel = 0.f;
    float PreviousWave = 0.f;
    float CameraImpulse = 0.f;
    float HazardFraming = 0.f;
    float HitRecoil = 0.f;
    float Hitstop = 0.f;
    float LandingPose = 0.f;
    float GaitWeight = 0.f;
    TStaticArray<float, 2> GuardPose{0.f, 0.f};
    FVector GaitDirection = FVector::ForwardVector;
    TStaticArray<FRotator, 4> DisplayedLimbs;
    TStaticArray<FRotator, 4> DisplayedHinges;
    TStaticArray<FRotator, 4> PreviousLimbs;
    TStaticArray<FRotator, 4> PreviousHinges;
    FRotator DisplayedBody = FRotator::ZeroRotator;
    FRotator PreviousBody = FRotator::ZeroRotator;
    FRotator DisplayedBlade = FRotator::ZeroRotator;
    FRotator PreviousBlade = FRotator::ZeroRotator;
    FVector DisplayedPivot = FVector::ZeroVector;
    FVector PreviousPivot = FVector::ZeroVector;
    bool bPoseReady = false;
    float AppealIntro = 0.f;
    double ShiftPressedAt = -1.0;
    double HoundFollowupUntil = 0.0;
    double AegisUntil = 0.0;
    bool bMantleCounter = false;
    double RitualOfferingUntil = 0.0;
    int32 BossPhase = 1;
    int32 BossComboDepth = 0;
    int32 BossEncounter = 0;
    float OrbitSide = 1.f;
    EDuelMove LastBossAttack = EDuelMove::Idle;
    EDuelMove PreviousBossAttack = EDuelMove::Idle;
    EDuelMove BossFollowup = EDuelMove::Idle;
    FRandomStream BossRandom;
    int32 Flasks = DuelCourt::DefaultHealthFlasks;
    int32 FocusFlasks = DuelCourt::DefaultFocusFlasks;
    int32 HealthFlaskAllocation = DuelCourt::DefaultHealthFlasks;
    FVector ActionDirection = FVector::ForwardVector;
    FVector StrikeStepDirection = FVector::ForwardVector;
    FVector ShiftDirection = FVector::ZeroVector;
    FVector DodgeDirection = FVector::ZeroVector;
    FVector CameraFocus = FVector::ZeroVector;
    FVector CastOrigin = FVector::ZeroVector;
    FVector CastAxis = FVector::ForwardVector;
    FVector HitDirection = FVector::ZeroVector;
    TStaticArray<FVector, 3> SealCenters;
    struct FMapWave
    {
        TArray<FVector> Marks;
        FVector Axis = FVector::ForwardVector;
        float HalfLength = DuelCourt::HalfLength;
        float Reach = 0.f;
        float WarnTime = 0.f;
        float ImpactTime = 0.f;
        bool bHitDelivered = false;
        TWeakObjectPtr<USceneComponent> Projectile;
        TWeakObjectPtr<USceneComponent> Preview;
        TWeakObjectPtr<USceneComponent> Guide;
        float ProjectileRetiredAt = -1.f;
        float FlightTime = 0.f;
        float FlightLift = 0.f;
    };
    TArray<FMapWave> MapWaves;
    struct FShadowSnare
    {
        FVector Center = FVector::ZeroVector;
        FVector Axis = FVector::ForwardVector;
        FVector Strike = FVector::ZeroVector;
        TWeakObjectPtr<USceneComponent> Thread;
        float StrikeAt = 0.f;
        bool bTriggered = false;
    };
    TArray<FShadowSnare, TInlineAllocator<3>> ShadowSnares;
    TStaticArray<bool, 2> BoneWaveHits{false, false};
    float RushHalfLength = 0.f;
    TStaticArray<int32, 10> PoseBones;
    bool bBoss = false;
    bool bLockOn = true;
    bool bCameraFocusReady = false;
    bool bCombatOver = false;
    bool bArtHeld = false;
    bool bChargedArt = false;
    float ArtHeldTime = 0.f;
    TWeakObjectPtr<USceneComponent> ChargeSeal;
    bool bImpactPlayed = false;
    bool bHitDelivered = false;
    bool bFeintTriggered = false;
    bool bVerdictTetherHeld = false;
    bool bAlternateForm = false;
    bool bTrackingBlade = false;
    bool bSprintExhausted = false;
    bool bShiftCommittedToSprint = false;
    bool bCharacterPreview = false;
    bool bNorthExtractionSeated = false;
    float NorthExtractionCycle = 0.f;
    float NorthExtractionWeight = 0.f;
    bool bDialogueSpeaking = false;
    float DialogueGestureWeight = 0.f;

    struct FHandPress
    {
        double Since = -1.0;
        EDuelWeapon Item = EDuelWeapon::Empty;
        bool bHeavy = false;
        bool bParryReady = false;
    };
    TStaticArray<FHandPress, 2> HandPresses;
    double NextParryPressAt = 0.0;

    void Forward(float Axis) { ForwardAxis = Axis; }
    void Right(float Axis) { RightAxis = Axis; }
    void LookYaw(float Axis);
    void LookPitch(float Axis);
    void Cut();
    void Crimson();
    void ReleaseCrimson();
    bool TryShieldParry(const FVector& Toward, bool bProjectile);
    void EquipRapier() { RequestWeapon(EDuelWeapon::Rapier); }
    void EquipGreatblade() { RequestWeapon(EDuelWeapon::Greatblade); }
    void EquipSpear() { RequestWeapon(EDuelWeapon::Spear); }
    void CycleRightHand();
    void CycleLeftHand();
    void LoadoutLeft();
    void LoadoutRight();
    void PressRightHand();
    void ReleaseRightHand() { ReleaseHand(EDuelHand::Right); }
    void PressLeftHand() { PressHand(EDuelHand::Left); }
    void ReleaseLeftHand() { ReleaseHand(EDuelHand::Left); }
    void PressHand(EDuelHand Hand);
    void ReleaseHand(EDuelHand Hand);
    void TickHands();
    void ClearHandPresses();
    EDuelWeapon GuardingShield() const;
    void PressShift();
    void ReleaseShift();
    bool Sprinting(const FVector& Input) const;
    void StartJump();
    void ReleaseJump() { StopJumping(); }
    void Flask();
    void CycleItem();
    void CycleSpell();
    void AdjustSpellSlot(int32 Slot, int32 Step);
    void SetSpellSlot(int32 Slot, EDuelArt Spell);
    void ChooseActivePreparedSpell(int32 Offset = 0);
    void ResetNorthLoadout(bool bExpeditionLoot);
    void ToggleLock() { if (!bCombatOver) { bLockOn = !bLockOn; bCameraFocusReady = false; } }
    void ConfirmDuel();
    void MenuUp();
    void MenuDown();
    void MenuBack() { MoveMenu(-1); }
    void MenuForward() { MoveMenu(1); }
    void MoveMenu(int32 Step);
    void CycleCharacterView();
    void PauseDuel();

    FVector MoveInput() const;
    void FaceDirection(const FVector& Direction, float DeltaSeconds);
    void TickCamera(float DeltaSeconds);
    void TickPlayer(float DeltaSeconds);
    void TickBoss(float DeltaSeconds);
    int32 BossDifficulty() const { return Character == EDuelCharacter::Yuki ? 2 : FMath::Clamp(BossEncounter, 0, 1); }
    EDuelMove PickBossAttack(float Distance);
    EDuelMove PickBossFollowup(EDuelMove Last, float Distance);
    bool CanReachWith(EDuelMove Attack, float Distance) const;
    void BeginMapAttack(const DuelCourt::FMoveTiming& Spec);
    void ShowMapWarning(int32 Index);
    void TickMapAttack(float LastAge);
    void TickInwardReap(float LastAge, const DuelCourt::FMoveTiming& Spec);
    void TickHaloBreak(float LastAge, const DuelCourt::FMoveTiming& Spec);
    void TickBoneProcession(float LastAge, const DuelCourt::FMoveTiming& Spec);
    void TickWrittenVerdict(float LastAge, const DuelCourt::FMoveTiming& Spec);
    void TickCrossAmbush(float LastAge, const DuelCourt::FMoveTiming& Spec);
    void PrepareYukiAttack(const DuelCourt::FMoveTiming& Spec);
    void TickYukiAttack(float LastAge, const DuelCourt::FMoveTiming& Spec);
    void TickBossProjectiles(const DuelCourt::FMoveTiming& Spec);
    void PrepareBossSignature(const DuelCourt::FMoveTiming& Spec);
    void TickBossSignature(float LastAge, const DuelCourt::FMoveTiming& Spec);
    bool TryMirrorRiposte(const FVector& Toward, const FVector& Contact);
    static FVector SignaturePosition(const FMapWave& Wave, float Age);
    void SignatureContact(const FVector& From, const FVector& To, float Radius, float Damage,
        bool bProjectile, const FLinearColor& Tint, bool& bContacted);
    void PrepareCloseChain();
    void TickCloseChain(float LastAge);
    void PrepareIcicleRow(const DuelCourt::FMoveTiming& Spec);
    void TickIcicleRow();
    void ClearAttackPreviews();
    float MoveRecoveryAt(const DuelCourt::FMoveTiming& Spec) const;
    void TickArchiveCollapse(float LastAge, const DuelCourt::FMoveTiming& Spec);
    void PrepareShadowSnare(const DuelCourt::FMoveTiming& Spec);
    void TickShadowSnare(float LastAge, const DuelCourt::FMoveTiming& Spec);
    void TickShadowSweep(float LastAge, const DuelCourt::FMoveTiming& Spec);
    void TickReversal(float LastAge, const DuelCourt::FMoveTiming& Spec);
    void PrepareWrittenVerdict(const DuelCourt::FMoveTiming& Spec);
    bool IsGroundHazard() const;
    void TickTalonRush(float LastAge, const DuelCourt::FMoveTiming& Spec, const FVector& BeforeTravel);
    void Speak(EDuelVoice Cue);
    void RequestMove(EDuelMove Next, EDuelHand Hand = EDuelHand::Right);
    bool JudgmentBlocksAction(EDuelMove Next) const;
    void ApplyJudgmentPenalty();
    void ClearJudgmentPenalty();
    void CancelJudgmentActions();
    float TravelProgress(float Age, const DuelCourt::FMoveTiming& Spec) const;
    bool TryDodgeCancel(EDuelMove Next);
    bool StartMove(EDuelMove Next, EDuelHand Hand = EDuelHand::Right);
    bool TryChangePhase();
    bool CanEquipOwnedWeapon(EDuelWeapon Gear, EDuelHand Hand) const;
    void RequestWeapon(EDuelWeapon Next, EDuelHand Hand = EDuelHand::Right);
    void SetWeapon(EDuelWeapon Next, EDuelHand Hand = EDuelHand::Right);
    void AdjustWeaponSlot(EDuelHand Hand, bool bReserve, int32 Step);
    void TickMove(float DeltaSeconds);
    void Impact();
    void PrepareArt(const DuelCourt::FMoveTiming& Spec);
    void ClearChargeSeal();
    void StrikeReturningFrost(const DuelCourt::FStrike& Attack);
    void CommitArt(const DuelCourt::FStrike& Attack);
    void StrikeVotive(const DuelCourt::FStrike& Attack, int32 Index);
    void StrikeAbsolution(const DuelCourt::FStrike& Attack);
    void BindStrike(const DuelCourt::FStrike& Attack, bool bEcho);
    bool TryWardCounter(const FVector& Source);
    void TickStorm(float ActiveProgress);
    void TickSeals(float LastAge);
    void TryMelee(float Reach, float FacingDot, float Damage, float PoiseDamage = 0.f, bool bParryable = false, bool bBlockable = false);
    void CheckBladeContact(float LastAge, const DuelCourt::FMoveTiming& Spec, const FVector& BeforeTravel);
    void StrikeLine(const FVector& Origin, const FVector& Axis);
    bool HitGroundSpell(float Damage, const FVector& Impact, const FVector* Emitter = nullptr, bool bPrecise = true);
    bool StrikeLine(const FVector& Origin, const FVector& Axis, float HalfLength, float HalfWidth, float Damage,
        const FLinearColor& Tint, bool bQuiet = false);
    bool InRollWindow() const;
    FVector ArtGround(const FVector& At) const;
    FVector SweepNorthCourtTravel(const FVector& From, const FVector& To) const;
    void ClampToCourt();

    USceneComponent* Joint(USceneComponent* Parent, const FVector& Position);
    void BuildFigure();
    void BuildHead(float Size);
    void BuildBreath();
    void BuildHiro();
    void BuildProps(const FVector& Grip, const FVector& FlaskGrip);
    void BuildWeapons(USceneComponent* Grip, TArray<TObjectPtr<USceneComponent>>& Models);
    void BuildEma();
    void BuildYuki();
    void BuildYukiPrisms();
    void BuildWings(const FVector& Left, const FVector& Right);
    void BuildHiroTransformation();
    void PoseCharacterMesh();
    void PoseFigure(float DeltaSeconds, FTransform* BladeGrip = nullptr);
};

UCLASS()
class MANOSABADUEL_API ADuelMode final : public AGameModeBase
{
    GENERATED_BODY()

public:
    ADuelMode();
    virtual void StartPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void RestartPlayer(AController* NewPlayer) override;
    void FighterFell(const ADuelFighter* Fighter);
    void BeginEncounter();
    void ReturnToCheckpoint();
    void TogglePause();
    EDuelMenuPage MenuPage() const;
    bool BlocksMouseLook() const;
    bool HandlePrimaryMenuPress();
    void Confirm();
    void Navigate(int32 Step);
    void AdjustLoadout(int32 Step);
    void SelectLoadoutRow(int32 Row);
    void ResetEncounter(bool bKeepPosition = false);
    void AnnouncePhase(int32 Phase);
    UTexture* NorthWitchPortrait(float& Opacity) const;
    void ChooseEnding(EDuelEnding Choice);
    void CycleCharacterView();
    void TurnCharacterView(float Degrees);
    int32 CharacterView() const { return FigureView; }
    int32 Choice() const { return MenuSelection; }
    int32 LoadoutChoice() const { return LoadoutRow; }
    static constexpr int32 ArtLoadoutRow = 4;
    static constexpr int32 FirstSpellRow = ArtLoadoutRow + 1;
    static constexpr int32 FlaskLoadoutRow = FirstSpellRow + DuelCourt::SpellSlotCount;
    static constexpr int32 LoadoutRows = FlaskLoadoutRow + 1;
    static constexpr int32 LoadoutColumns = 4;
    bool ShowingControls() const { return bControls; }
    bool ShowingLoadout() const { return bLoadout; }
    bool ShowingGraphics() const { return bGraphics; }
    int32 GraphicsChoice() const { return GraphicsRow; }
    const FDuelGraphicsSettings& Graphics() const { return GraphicsSettings; }
    bool UsesCastle() const { return bCastleRoute; }
    bool UsesNorthTower() const { return bNorthTowerRoute; }
    const ADuelNorthTower* NorthTower() const { return NorthTowerActor; }
    bool KnowsSpell(EDuelArt Spell) const;
    bool OwnsWeapon(EDuelWeapon Weapon) const;
    bool UsesExpeditionLoot() const;
    const TCHAR* CastlePrompt() const;
    const TCHAR* CastleArea() const;
    FVector InvestigationTarget() const;
    const TCHAR* ActiveInvestigation() const;
    int32 CastleMarks() const;
    bool EffortSubtitles() const { return bEffortSubtitles; }
    int32 PauseOptionCount() const;
    EDuelPause PauseOption(int32 Index) const;
    bool NearGate() const;
    bool NearPrisonGate() const;
    bool NearWarrant() const;
    bool NearArchiveDesk() const;
    bool NearAppealPassage() const;
    bool InAppeal() const { return bAppeal; }
    bool InYukiCourt() const { return bYukiCourt; }
    int32 HearingsTime() const { return FirstDuelSeconds + SecondDuelSeconds + CompletedDuelSeconds; }
    bool NearRestPoint() const;
    bool NearGardenExit() const;
    bool SilentBellActive() const;
    int32 FirstHearingSeconds() const { return FirstDuelSeconds; }
    bool HasWarrant() const { return bWarrantFound; }
    bool PrisonGateOpen() const { return bPrisonGateOpen; }
    bool AntechamberReached() const { return bAntechamberReached; }
    bool GateOpen() const { return bGateOpen; }
    bool CanContinue() const;
    bool RitualComplete() const { return RitualSeal == 3; }
    float RitualCaptionOpacity() const;
    int32 RitualMark() const;
    const TCHAR* CombatCaption() const;
    FString CaptionSpeaker() const;
    float CaptionOpacity() const;
    const TCHAR* AreaName() const;
    bool CanExplore() const;
    int32 DuelSeconds() const { return CompletedDuelSeconds; }
    bool ReadingArchive() const { return bReadingArchive; }
    int32 DialoguePhase() const { return SpokenPhase; }
    bool DialogueVisible() const;
    EDuelEnding Ending() const { return Decision; }
    const ADuelFighter* GetBossFighter() const { return BossFighter; }
    const ADuelFighter* GetPlayerFighter() const { return PlayerFighter; }
    EDuelOutcome Outcome() const { return Result; }

private:
    friend class ADuelHUD;
    friend class FDuelCombatMechanicsQA;
    friend class FDuelShieldGroundMagicQA;
    friend class FDuelFlyingBodyQA;
    friend class FDuelCourtInteractionsQA;
    friend class FDuelJudgment347QA;
    friend class FDuelDungeonChainsQA;
    friend struct FDuelDungeonChainsVisualFixture;
    friend class FDuelNorthFollowFurnitureTest;
    friend class FDuelNorthMechanicHintsTest;
    friend class FDuelNorthFastTravelTest;
    friend class FDuelNorthPointerMenuTest;
    friend class FDuelMatchSmoke;
    friend class FDuelBossDefenseQA;
    friend class FDuelCircularSpellsTest;
    friend class FDuelNorthIntegrationTest;
    friend class FDuelNorthWalkTest;
    friend class FDuelNorthStairStressTest;
    friend class FDuelNorthPerformanceProbe;
    friend class FDuelNorthJourneyTest;
    friend struct FDuelVisualQA;
    friend class FDuelAppealSmoke;
    friend class FDuelFinaleSmoke;
    friend class FDuelCheckpointSmoke;
    friend class FDuelLiveMatch;
    friend struct FDuelLiveCombat;
    friend class FDuelNorthCombatCameraTest;
    friend class FDuelCastleSmoke;
    friend class FDuelCastleFinalWalk;
    friend class FDuelCastleProgress;

    friend class ADuelNorthTower;
    UPROPERTY() TObjectPtr<ADuelNorthTower> NorthTowerActor;
    bool bNorthTowerRoute = false;
    UPROPERTY() TObjectPtr<ADuelArena> Arena;
    UPROPERTY() TObjectPtr<ADuelFighter> BossFighter;
    UPROPERTY() TObjectPtr<ADuelFighter> PlayerFighter;
    UPROPERTY() TObjectPtr<ADuelFighter> RescuedFighter;
    bool bCompanionFollowing = false;
    UPROPERTY() TObjectPtr<ACameraActor> FigureCamera;
    EDuelOutcome Result = EDuelOutcome::Ready;
    EDuelEnding Decision = EDuelEnding::Undecided;
    int32 SpokenPhase = 0;
    double NorthWitchStartedAt = -1.0;
    int32 FigureView = 0;
    int32 MenuSelection = 0;
    int32 LoadoutRow = 0;
    int32 LoadoutCandidate = INDEX_NONE;
    int32 InventorySelection = 0;
    int32 BrowsedMapFloor = INDEX_NONE;
    uint64 PointerClosedFrame = MAX_uint64;
    bool bMenuPointerOwned = false;
    int32 GraphicsRow = 0;
    FDuelGraphicsSettings GraphicsSettings;
    int32 CompletedDuelSeconds = 0;
    int32 FirstDuelSeconds = 0;
    int32 SecondDuelSeconds = 0;
    EDuelCharacter FirstBoss = EDuelCharacter::Ema;
    double DuelStartedAt = 0.0;
    bool bControls = false;
    bool bLoadout = false;
    bool bInventory = false;
    bool bGraphics = false;
    bool bReadingArchive = false;
    bool bWarrantFound = false;
    bool bPrisonGateOpen = false;
    bool bAntechamberReached = false;
    bool bGateOpen = false;
    bool bAppeal = false;
    bool bAppealRested = false;
    bool bYukiCourt = false;
    bool bCastleRoute = false;
    uint8 CastleKeys = 0;
    uint8 CastleClues = 0;
    uint8 CastleSeals = 0;
    uint8 CastleShortcuts = 0;
    uint8 CastleWins = 0;
    uint8 VerdictScars = 0;
    bool bEastOpen = false;
    bool bFinalRecord = false;
    int32 CastleDuel = INDEX_NONE;
    double InvestigationUntil = 0.0;
    const TCHAR* InvestigationLine = nullptr;
    int32 RitualSeal = 0;
    double RitualAt = 0.0;
    void WriteCheckpoint();
    void LoadCheckpoint();
    bool bEffortSubtitles = true;
    FTransform ApproachBeforePreview;
    float SubtitleUntil = 0.f;
    void LinkFighters();
    void RefreshMenuPointer();
    void ActivateMenuChoice(int32 Choice);
    TArray<int32> InventoryChoices() const;
    void SetFlaskAllocation(int32 Value);
    TArray<int32> LoadoutChoices() const;
    int32 EquippedLoadoutValue(int32 Row) const;
    void ChooseLoadoutValue(int32 Value);
    void NavigateLoadoutChoices(int32 Step, bool bVertical);
    DuelNorthLayout::EFloor ViewedMapFloor() const;
    void NavigateMapFloors(int32 Step);
    ADuelFighter* SpawnBoss(EDuelCharacter Character);
    void SetBoss(EDuelCharacter Character);
    void RescueBoss();
    void EnterYukiCourt();
    void FinishTrial(bool bFollow = false);
    void CloseCharacterView();
    void EnterArchive();
    void EnterAppeal();
    void TickCastle(const FVector& At);
    void ConfirmCastle();
    void ReturnToCastleHub();
    bool CanEnterCastleWing(int32 Wing) const;
    int32 NearbyFog() const;
    void SetCastleCourt(int32 Wing);
    void TellInvestigation(const TCHAR* Text);
    void RestoreCastleLayout();
};

UCLASS()
class MANOSABADUEL_API ADuelHUD final : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;
    void ClickMenu(ADuelMode& Mode);

protected:
    virtual void BeginPlay() override;

private:
    friend class FDuelNorthPointerMenuTest;
    TArray<TPair<int32, FBox2D>> MenuButtons;
    EDuelMenuPage DrawnMenuPage = EDuelMenuPage::None;
    int32 DrawnLoadoutRow = INDEX_NONE;
    FVector2D MenuCanvasSize = FVector2D::ZeroVector;
    FVector2D MenuPointer = FVector2D::ZeroVector;
    bool bMenuPointerKnown = false;
#if WITH_DEV_AUTOMATION_TESTS
    TOptional<FVector2D> TestPointerPixel;
#endif
    UPROPERTY() TObjectPtr<class UFont> HudFont;
    UPROPERTY() TObjectPtr<class UTexture2D> ChromeAtlas;
    UPROPERTY() TObjectPtr<class UTexture2D> ArtIconAtlas;
    UPROPERTY() TObjectPtr<class UTexture2D> InventoryAtlas;
    float PreviousHealth = 1.f;
    float HealthTrail = 1.f;
    float PreviousBossHealth = 1.f;
    float BossHealthTrail = 1.f;
    double ZoneChangedAt = 0.0;
    FString PreviousZone;
    double NorthObjectiveChangedAt = 0.0;
    FString PreviousNorthObjective;
    EDuelOutcome PreviousNorthOutcome = EDuelOutcome::Ready;
    double NorthOutcomeChangedAt = 0.0;

    void DrawMeter(float X, float Y, float W, float H, float Ratio, const FLinearColor& Color, float Trail = 0.f);
    void Chrome(int32 Tile, float X, float Y, float W, float H, const FLinearColor& Tint = FLinearColor::White);
    void InventoryIcon(int32 Tile, float X, float Y, float Size, const FLinearColor& Tint = FLinearColor::White);
    void Panel(float X, float Y, float W, float H, float S);
    void FadeBand(float X, float Y, float W, float H, const FLinearColor& Left, const FLinearColor& Right);
    void FocusBand(float X, float Y, float W, float H, float S);
    float KeyHint(float X, float Y, float S, FStringView Key, FStringView Action);
    void DrawTitle(const ADuelMode& Mode, float W, float H, float S);
    void DrawPause(const ADuelMode& Mode, float W, float H, float S);
    void DrawSettings(const ADuelMode& Mode, float W, float H, float S);
    void DrawControls(const ADuelMode& Mode, float W, float H, float S);
    void DrawInventory(const ADuelMode& Mode, float W, float H, float S);
    bool ReadMenuPointer(FVector2D& Position) const;
    bool MenuButton(int32 Choice, float X, float Y, float W, float H);
    void DrawMenuBack(float X, float Y, float W, float H, float S, const TCHAR* Text);
    void DrawEquipment(const ADuelFighter& Fighter, float H, float S);
    void DrawNorthHUD(const ADuelMode& Mode, float W, float H, float S);
    float DrawNorthJudgmentHUD(const ADuelMode& Mode, float W, float H, float S);
    void DrawLoadout(const ADuelMode& Mode, float W, float H, float S);
    void DrawLoadoutArt(EDuelArt Art, float X, float Y, float Size, const FLinearColor& Tint);
    void DrawCompass(const ADuelMode& Mode, float W, float S);
    void DrawArea(const ADuelMode& Mode, float W, float H, float S);
    void Label(FStringView Text, float X, float Y, float Size, const FLinearColor& Color, bool bCenter = false);
    float LabelWidth(FStringView Text, float Size) const;
    TArray<FString> WrapLabel(const FString& Text, float Size, float Width) const;
    void DrawPrompt(const ADuelMode& Mode, bool bPaused, float W, float H, float S);
    void DrawCaption(const FString& Speaker, const FString& Line, float W, float H, float S, float Alpha);
};
