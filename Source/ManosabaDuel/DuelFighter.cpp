#include "DuelGame.h"
#include "DuelBossDefense.h"

#include "DuelArena.h"
#include "Camera/CameraComponent.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"

using DuelCourt::FMoveTiming;
using DuelCourt::Timing;

FMoveTiming DuelCourt::Timing(EDuelMove Move, bool bAwakened, EDuelWeapon Weapon, EDuelArt Art)
{
    if (const FStrike* Attack = Strike(Weapon, Move, Art))
    {
        FMoveTiming PlayerAction = Attack->Timing;
        PlayerAction.Windup -= FMath::Min(.055f, PlayerAction.Windup * .12f);
        PlayerAction.Cost *= StaminaCostScale;
        return PlayerAction;
    }
    switch (Move)
    {
    case EDuelMove::Parry:   return { 0.f, .08f, .12f };
    case EDuelMove::Equip:   return { .176f, .02f, .18f };
    case EDuelMove::Roll:    return { RollGuardDelay, RollActiveTime, RollRecoveryTime, 18.f * StaminaCostScale, RollDistance };
    case EDuelMove::Backstep:return { .016f, .166f, .204f, 14.f * StaminaCostScale, 168.f };
    case EDuelMove::Flask:   return { .425f, .02f, .52f };
    case EDuelMove::Hurt:    return { 0.f, .12f, .27f };
    case EDuelMove::GuardBreak: return { 0.f, .25f, .29f };
    case EDuelMove::Stagger: return { 0.f, 1.10f, .53f };
    case EDuelMove::GuardReel: return { 0.f, .37f, .29f };
    case EDuelMove::Paw:     return bAwakened
        ? FMoveTiming{ .48f, .26f, .62f, 0.f, 231.f }
        : FMoveTiming{ .61f, .24f, .72f, 0.f, 170.f };
    case EDuelMove::Leap:    return bAwakened
        ? FMoveTiming{ .77f, .43f, .85f }
        : FMoveTiming{ .87f, .37f, .83f };
    case EDuelMove::Pillar:  return { 1.23f, .24f, 1.40f };
    case EDuelMove::Storm:   return { 1.46f, .84f, 1.35f };
    case EDuelMove::WingSweep: return { .79f, .30f, .77f, 0.f, 273.f };
    case EDuelMove::ShadowSweep: return { .74f, .44f, .69f };
    case EDuelMove::Seals:     return { .92f, DuelBossDefense::SealActive, .80f };
    case EDuelMove::Cross:     return { .99f, DuelBossDefense::CrossActive, .86f };
    case EDuelMove::Awaken:  return { .91f, .23f, 1.10f };
    case EDuelMove::Eclipse: return { 1.18f, .24f, 1.05f };
    case EDuelMove::AtlasRain: return { 1.39f, 1.48f, .96f };
    case EDuelMove::TalonRush: return { .56f, .43f, .56f };
    case EDuelMove::NightLanes: return { 1.23f, 1.48f, .89f };
    case EDuelMove::InwardReap: return { 1.08f, .90f, 1.06f };
    case EDuelMove::FlankLeap: return { .82f, .56f, .74f };
    case EDuelMove::Pursuit: return { .76f, DuelBossDefense::PursuitActive, .65f };
    case EDuelMove::WingVerdict: return { .90f, 1.25f, .57f };
    case EDuelMove::Tribunal: return { 1.08f, 1.66f, .72f };
    case EDuelMove::Reversal: return bAwakened
        ? FMoveTiming{ .51f, .87f, .61f }
        : FMoveTiming{ .63f, .88f, .70f };
    case EDuelMove::ShadowSnare: return { .83f, 2.00f, .55f };
    case EDuelMove::RakingClaws: return bAwakened
        ? FMoveTiming{ .66f, DuelBossDefense::RakingActive, .52f, 0.f, 349.f }
        : FMoveTiming{ .75f, DuelBossDefense::RakingActive, .63f, 0.f, 292.f };
    case EDuelMove::HaloBreak: return bAwakened
        ? FMoveTiming{ .92f, .97f, .70f } : FMoveTiming{ 1.02f, .97f, .80f };
    case EDuelMove::Wingfall: return { .73f, .66f, .72f };
    case EDuelMove::ShadowVolley: return { .93f, 1.52f, .83f };
    case EDuelMove::BoneProcession: return { .91f, DuelBossDefense::BoneActive, .70f };
    case EDuelMove::FeintCounter: return { .78f, .34f, .70f, 0.f, 318.f };
    case EDuelMove::WrittenVerdict: return { 1.03f, 1.02f, .65f };
    case EDuelMove::CrossAmbush: return { .78f, DuelBossDefense::AmbushActive, .69f };
    case EDuelMove::GlassShelter: return { 1.45f, .31f, 1.04f };
    case EDuelMove::BrokenRecord: return { DuelBossDefense::RecordWindup, DuelBossDefense::RecordActive, .71f };
    case EDuelMove::SilentBell: return { 1.03f, .92f, .78f };
    case EDuelMove::VerdictTether: return { .64f, 1.02f, .57f };
    case EDuelMove::MirrorEdict: return { .72f, DuelBossDefense::MirrorActive, .63f };
    case EDuelMove::PrismPursuit: return { .78f, 1.53f, .67f, 0.f, 240.f };
    case EDuelMove::ArchiveCollapse: return { 1.03f, ArchiveReturnAt + ArchiveReturnTime + .08f, .76f };
    case EDuelMove::OssuaryChain:
    case EDuelMove::HoundChain:
    case EDuelMove::FrostStaffChain:
    {
        using namespace DuelBossDefense;
        const auto Hits = CloseHits(Move);
        return {Hits[0].At, Hits.Last().At - Hits[0].At + CloseLastHitHold, CloseRecovery};
    }
    case EDuelMove::IcicleRow:
        return {DuelBossDefense::IcicleFirstRelease, (DuelBossDefense::MaxIcicles - 1) * DuelBossDefense::IcicleBeat + .18f, .72f};
    case EDuelMove::OssuaryShears: return { .86f, DuelBossDefense::WingFlight, .75f };
    case EDuelMove::HoundShade: return { .78f, DuelBossDefense::ShadeBodyStart + DuelBossDefense::ShadeBodyFlight, .71f };
    case EDuelMove::MirrorRicochet:
        return { .88f, DuelBossDefense::MirrorReturnStart + DuelBossDefense::MirrorReturnFlight, .80f };
    case EDuelMove::MirrorRiposte: return { 1.18f, DuelBossDefense::RiposteFlight, .77f };
    case EDuelMove::FinalSentence: return { 1.05f, DuelBossDefense::SentenceBeat + .16f, .86f };
    default:                return { 0.f, 0.f, 0.f };
    }
}

namespace
{
    const FLinearColor Sakura(4.7f, .15f, 1.45f);
    const FLinearColor CrimsonLight(5.f, .025f, .09f);
    const FLinearColor Gold(2.7f, .94f, .17f);
    const FLinearColor Moonlight(.22f, 2.9f, 5.3f);
    const FLinearColor EclipseLight(2.5f, .35f, 6.7f);
    struct FBossArea { float Reach; float Damage; };
    constexpr FBossArea Paw[] = {{276.f, 42.f}, {306.f, 52.f}, {349.f, 62.f}};
    constexpr FBossArea Leap[] = {{232.f, 54.f}, {264.f, 72.f}, {301.f, 82.f}};
    constexpr FBossArea Pillar[] = {{455.f, 67.f}, {511.f, 73.f}, {568.f, 81.f}};
    constexpr float StormDamage[] = {66.f, 74.f, 82.f};
    constexpr FBossArea Wing[] = {{365.f, 49.f}, {410.f, 57.f}, {462.f, 69.f}};
    constexpr float WingFacing = -.06f;
    constexpr FBossArea Seal[] = {{135.f, 39.f}, {152.f, 44.f}, {177.f, 54.f}};
    constexpr FBossArea Flank[] = {{266.f, 49.f}, {291.f, 64.f}, {318.f, 73.f}};
    constexpr FBossArea Wingfall[] = {{0.f, 0.f}, {344.f, 70.f}, {403.f, 86.f}};
    constexpr FBossArea Takeoff[] = {{0.f, 0.f}, {321.f, 48.f}, {398.f, 61.f}};
    constexpr float FlankTravel[] = {1080.f, 1260.f, 1420.f};
    constexpr FBossArea Rush[] = {{121.f, 49.f}, {138.f, 57.f}, {158.f, 66.f}};
    constexpr float RushTravel[] = {830.f, 985.f, 1135.f};
    constexpr float LeapTravel[] = {770.f, 895.f, 1010.f};
    constexpr float SealSpacing = DuelBossDefense::SealBeat;
    constexpr float LaneHalfLength = 1190.f;
    constexpr float LaneHalfWidth = 92.f;
    constexpr float LaneDelay = DuelBossDefense::CrossBeat;
    constexpr float RushLead = 140.f;
    constexpr float RushTail = 56.f;
    constexpr double SprintHoldSeconds = .22;
    constexpr float PlayerWalkSpeed = 455.f;
    constexpr float FlaskWalkSpeed = 250.f;

    bool CanSpendStamina(EDuelMove Move, float Stamina, float Cost)
    {
        return Move == EDuelMove::Roll ? Stamina > Cost * .5f : Stamina >= Cost;
    }

    bool IsDodge(EDuelMove Move)
    {
        return Move == EDuelMove::Roll || Move == EDuelMove::Backstep;
    }

    bool IsLightAttack(EDuelMove Move)
    {
        return Move == EDuelMove::Cut || Move == EDuelMove::Backcut;
    }

    float DodgeCancelAt(EDuelMove Move, const FMoveTiming& Timing)
    {
        return FMath::Max(Timing.Windup + Timing.Active, Timing.Total() - (IsLightAttack(Move) ? .22f : .14f));
    }

    float LimitTravel(float Distance, const FVector& At, const FVector& Direction, const FBox2D& Bounds)
    {
        for (int32 Axis = 0; Axis < 2; ++Axis)
        {
            const double Heading = Direction[Axis];
            if (FMath::Abs(Heading) <= UE_KINDA_SMALL_NUMBER) continue;
            const double Wall = Heading > 0. ? Bounds.Max[Axis] : Bounds.Min[Axis];
            Distance = FMath::Min(Distance, static_cast<float>((Wall - At[Axis]) / Heading));
        }
        return FMath::Max(0.f, Distance);
    }
}

ADuelFighter::ADuelFighter()
{
    PrimaryActorTick.bCanEverTick = true;
    AutoPossessAI = EAutoPossessAI::Disabled;
    GetCapsuleComponent()->InitCapsuleSize(34.f, 60.f);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

    UCharacterMovementComponent* Motion = GetCharacterMovement();
    Motion->MaxWalkSpeed = PlayerWalkSpeed;
    Motion->MaxAcceleration = 2200.f;
    Motion->BrakingDecelerationWalking = 2400.f;
    Motion->JumpZVelocity = 392.f;
    Motion->GravityScale = 1.18f;
    Motion->AirControl = .42f;
    Motion->bOrientRotationToMovement = false;
    bUseControllerRotationYaw = false;

    Figure = CreateDefaultSubobject<USceneComponent>(TEXT("Figure"));
    Figure->SetupAttachment(GetCapsuleComponent());

    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("LowFollowCamera"));
    CameraBoom->SetupAttachment(GetCapsuleComponent());
    CameraBoom->SetAbsolute(false, true, false);
    CameraBoom->SetRelativeRotation(FRotator(-15, 0, 0));
    CameraBoom->TargetArmLength = 664.f;
    CameraBoom->TargetOffset = FVector(0, 0, 115);
    CameraBoom->SocketOffset = FVector(0, 76, 9);
    CameraBoom->bDoCollisionTest = true;
    CameraBoom->ProbeSize = 19.f;
    CameraBoom->bEnableCameraLag = true;
    CameraBoom->bUseCameraLagSubstepping = true;
    CameraBoom->CameraLagMaxTimeStep = 1.f / 120.f;
    CameraBoom->CameraLagSpeed = 13.f;
    CameraBoom->CameraLagMaxDistance = 48.f;

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
    Camera->SetFieldOfView(68.f);
}

void ADuelFighter::ConfigureBoss(EDuelCharacter Selection)
{
    bBoss = true;
    Character = Selection;
    Health = DuelCourt::BossMaxHealth;
    GetCapsuleComponent()->SetCapsuleSize(93.f, 101.f);
    GetCharacterMovement()->MaxWalkSpeed = 320.f;
    GetCharacterMovement()->bRunPhysicsWithNoController = true;
}

void ADuelFighter::BeginPlay()
{
    Super::BeginPlay();
    if (bBoss)
    {
        Camera->Deactivate();
    }
    ResetFighter();
    BuildFigure();
}

void ADuelFighter::SetEncounter(ADuelFighter* Opponent, ADuelArena* InArena)
{
    Target = Opponent;
    Arena = InArena;
}

void ADuelFighter::RestAtCheckpoint()
{
    ClearJudgmentPenalty();
    RitualOfferingUntil = 0.0;
    Health = DuelCourt::PlayerMaxHealth;
    Stamina = DuelCourt::MaxStamina;
    Focus = FocusMax();
    Flasks = HealthFlaskAllocation;
    FocusFlasks = DuelCourt::TotalFlasks - HealthFlaskAllocation;
}

void ADuelFighter::ResetFighter(bool bKeepPosition)
{
    ClearJudgmentPenalty();
    StopJumping();
    ShiftPressedAt = -1.0;
    HoundFollowupUntil = 0.0;
    AegisUntil = 0.0;
    bMantleCounter = false;
    RitualOfferingUntil = 0.0;
    DamageBurst = 0.f;
    DamageBurstUntil = 0.0;
    ShiftDirection = DodgeDirection = FVector::ZeroVector;
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    GetCharacterMovement()->MaxWalkSpeed = bBoss ? (Character == EDuelCharacter::Ema ? 372.f : 320.f) : PlayerWalkSpeed;
    const FTransform Start = DuelCourt::Start(bBoss, Arena ? Arena->CourtOrigin() : FVector::ZeroVector);
    if (!bKeepPosition)
    {
        SetActorLocationAndRotation(Start.GetLocation(), Start.Rotator(), false, nullptr, ETeleportType::TeleportPhysics);
    }
    Move = QueuedMove = EDuelMove::Idle;
    MoveAge = RecoveryDelay = Gait = GaitWeight = Fall = FormProgress = Travel = PreviousWave = 0.f;
    GaitDirection = FVector::ForwardVector;
    GuardPose = {0.f, 0.f};
    bPoseReady = false;
    CameraImpulse = HitRecoil = Hitstop = LandingPose = AppealIntro = 0.f;
    bArtHeld = bChargedArt = false;
    ArtHeldTime = 0.f;
    ClearChargeSeal();
    HazardFraming = 0.f;
    HitDirection = FVector::ZeroVector;
    Posture = PostureDelay = 0.f;
    BossPause = 1.35f;
    BossPhase = 1;
    BossComboDepth = 0;
    PreviousBossAttack = LastBossAttack = BossFollowup = EDuelMove::Idle;
    ClearAttackPreviews();
    MapWaves.Reset();
    ShadowSnares.Reset();
    if (bBoss)
    {
        BossRandom.Initialize(FMath::Rand());
        OrbitSide = BossRandom.RandRange(0, 1) == 0 ? -1.f : 1.f;
    }
    if (!bKeepPosition)
    {
        RestAtCheckpoint();
    }
    if (bBoss)
    {
        Health = DuelCourt::BossMaxHealth;
    }
    ViewYaw = 0.f;
    ViewPitch = -15.f;
    ForwardAxis = RightAxis = 0.f;
    ActionDirection = GetActorForwardVector();
    bCombatOver = bImpactPlayed = bHitDelivered = bTrackingBlade = false;
    bSprintExhausted = bShiftCommittedToSprint = false;
    ClearHandPresses();
    NextParryPressAt = 0.0;
    PendingWeapon = MoveWeapon = Equipped;
    MoveArt = EDuelArt::Default;
    PendingHand = MoveHand = QueuedHand = EDuelHand::Right;
    bLockOn = true;
    bCameraFocusReady = false;
    SetActorEnableCollision(true);
    if (Figure)
    {
        Figure->SetRelativeLocation(FVector(0, 0, -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()));
        Figure->SetRelativeRotation(FRotator::ZeroRotator);
    }
}

void ADuelFighter::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindAxis(TEXT("Forward"), this, &ADuelFighter::Forward);
    Input->BindAxis(TEXT("Right"), this, &ADuelFighter::Right);
    Input->BindAxis(TEXT("LookYaw"), this, &ADuelFighter::LookYaw);
    Input->BindAxis(TEXT("LookPitch"), this, &ADuelFighter::LookPitch);
    Input->BindAction(TEXT("UseRightHand"), IE_Pressed, this, &ADuelFighter::PressRightHand).bExecuteWhenPaused = true;
    Input->BindAction(TEXT("UseRightHand"), IE_Released, this, &ADuelFighter::ReleaseRightHand).bExecuteWhenPaused = true;
    Input->BindAction(TEXT("UseLeftHand"), IE_Pressed, this, &ADuelFighter::PressLeftHand);
    Input->BindAction(TEXT("UseLeftHand"), IE_Released, this, &ADuelFighter::ReleaseLeftHand).bExecuteWhenPaused = true;
    Input->BindAction(TEXT("WeaponArt"), IE_Pressed, this, &ADuelFighter::Crimson);
    Input->BindAction(TEXT("WeaponArt"), IE_Released, this, &ADuelFighter::ReleaseCrimson).bExecuteWhenPaused = true;
    Input->BindAction(TEXT("CycleItem"), IE_Pressed, this, &ADuelFighter::CycleItem);
    Input->BindAction(TEXT("CycleSpell"), IE_Pressed, this, &ADuelFighter::CycleSpell);
    Input->BindAction(TEXT("RightHand"), IE_Pressed, this, &ADuelFighter::CycleRightHand).bExecuteWhenPaused = true;
    Input->BindAction(TEXT("LeftHand"), IE_Pressed, this, &ADuelFighter::CycleLeftHand).bExecuteWhenPaused = true;
    const auto BindShift = [this, Input](EInputEvent Event, void (ADuelFighter::*Handler)())
    {
        FInputActionBinding Binding(TEXT("Roll"), Event);
        Binding.bExecuteWhenPaused = Event == IE_Released;
        Binding.ActionDelegate.GetDelegateForManualSet().BindWeakLambda(this, [this, Input, Handler]()
        {
            // The action delegates run before the axis callbacks. Read this frame's values for a quick tap.
            Forward(Input->GetAxisValue(TEXT("Forward")));
            Right(Input->GetAxisValue(TEXT("Right")));
            (this->*Handler)();
        });
        Input->AddActionBinding(MoveTemp(Binding));
    };
    BindShift(IE_Pressed, &ADuelFighter::PressShift);
    BindShift(IE_Released, &ADuelFighter::ReleaseShift);
    Input->BindAction(TEXT("Jump"), IE_Pressed, this, &ADuelFighter::StartJump);
    Input->BindAction(TEXT("Jump"), IE_Released, this, &ADuelFighter::ReleaseJump);
    Input->BindAction(TEXT("Flask"), IE_Pressed, this, &ADuelFighter::Flask);
    Input->BindAction(TEXT("Lock"), IE_Pressed, this, &ADuelFighter::ToggleLock);
    Input->BindAction(TEXT("CharacterView"), IE_Pressed, this, &ADuelFighter::CycleCharacterView);
    Input->BindAction(TEXT("Confirm"), IE_Pressed, this, &ADuelFighter::ConfirmDuel).bExecuteWhenPaused = true;
    Input->BindAction(TEXT("MenuUp"), IE_Pressed, this, &ADuelFighter::MenuUp).bExecuteWhenPaused = true;
    Input->BindAction(TEXT("MenuDown"), IE_Pressed, this, &ADuelFighter::MenuDown).bExecuteWhenPaused = true;
    for (const auto& Binding : {TPair<FName, void (ADuelFighter::*)()>(TEXT("MenuBack"), &ADuelFighter::MenuBack),
        TPair<FName, void (ADuelFighter::*)()>(TEXT("MenuForward"), &ADuelFighter::MenuForward)})
    {
        FInputActionBinding& MenuBinding = Input->BindAction(Binding.Key, IE_Pressed, this, Binding.Value);
        MenuBinding.bExecuteWhenPaused = true;
        MenuBinding.bConsumeInput = false;
    }
    for (const FKey& Key : {EKeys::A, EKeys::D})
    {
        FInputKeyBinding& SlotKey = Input->BindKey(Key, IE_Pressed, this,
            Key == EKeys::A ? &ADuelFighter::LoadoutLeft : &ADuelFighter::LoadoutRight);
        SlotKey.bExecuteWhenPaused = true;
        SlotKey.bConsumeInput = false;
    }
    Input->BindAction(TEXT("PauseDuel"), IE_Pressed, this, &ADuelFighter::PauseDuel).bExecuteWhenPaused = true;
}

void ADuelFighter::LoadoutLeft()
{
    if (ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>())
    {
        Mode->AdjustLoadout(-1);
    }
}

void ADuelFighter::LoadoutRight()
{
    if (ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>())
    {
        Mode->AdjustLoadout(1);
    }
}

void ADuelFighter::LookYaw(float Axis)
{
    const ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    if (!bLockOn && (!Mode || !Mode->BlocksMouseLook()))
    {
        ViewYaw = FRotator::NormalizeAxis(ViewYaw + Axis * 1.65f);
    }
}

void ADuelFighter::LookPitch(float Axis)
{
    const ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    if (!bLockOn && (!Mode || !Mode->BlocksMouseLook()))
    {
        if (!FMath::IsNearlyZero(Axis) && GardenCameraBlend > .0001f) bGardenCameraManual = true;
        ViewPitch = FMath::Clamp(ViewPitch + Axis * 1.2f, -37.f, 12.f);
    }
}

void ADuelFighter::PressRightHand()
{
    ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    if (!Mode || !Mode->HandlePrimaryMenuPress())
    {
        PressHand(EDuelHand::Right);
    }
}

void ADuelFighter::Cut() { RequestMove(EDuelMove::Cut); }
void ADuelFighter::Crimson()
{
    if (!CanUseEquipment() || UGameplayStatics::IsGamePaused(this) || JudgmentBlocksAction(EDuelMove::Crimson)) return;
    bArtHeld = true;
    const EDuelHand Hand = ArtHand();
    if (DuelCourt::IsBlade(EquippedWeapon(Hand)))
    {
        RequestMove(EDuelMove::Crimson, Hand);
    }
}
void ADuelFighter::ReleaseCrimson() { bArtHeld = false; }

void ADuelFighter::PressShift()
{
    if (ShiftPressedAt >= 0.0 || !CanUseEquipment() || UGameplayStatics::IsGamePaused(this) || JudgmentMoveLockSeconds() > 0.f)
    {
        return;
    }
    ShiftPressedAt = GetWorld()->GetTimeSeconds();
    ShiftDirection = MoveInput();
}

void ADuelFighter::ReleaseShift()
{
    if (ShiftPressedAt < 0.0)
    {
        return;
    }
    const bool bSprinted = bShiftCommittedToSprint;
    ShiftPressedAt = -1.0;
    bSprintExhausted = bShiftCommittedToSprint = false;
    if (bSprinted)
    {
        GetCharacterMovement()->MaxWalkSpeed = Move == EDuelMove::Flask ? FlaskWalkSpeed : PlayerWalkSpeed;
        return;
    }
    if (!UGameplayStatics::IsGamePaused(this))
    {
        const FVector Input = MoveInput();
        DodgeDirection = (Input.IsNearlyZero() ? ShiftDirection : Input).GetSafeNormal2D();
        RequestMove(DodgeDirection.IsNearlyZero() ? EDuelMove::Backstep : EDuelMove::Roll);
    }
}

bool ADuelFighter::Sprinting(const FVector& Input) const
{
    return ShiftPressedAt >= 0.0 && GetWorld()->GetTimeSeconds() - ShiftPressedAt >= SprintHoldSeconds &&
        !Input.IsNearlyZero() && GetCharacterMovement()->IsMovingOnGround() && Stamina > 0.f && !bSprintExhausted;
}

void ADuelFighter::StartJump()
{
    const ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    const bool bApproaching = Mode && Mode->CanExplore();
    if (!bBoss && (!bCombatOver || bApproaching) && IsAlive() && JudgmentMoveLockSeconds() <= 0.f && Move == EDuelMove::Idle &&
        GetCharacterMovement()->IsMovingOnGround())
    {
        Jump();
    }
}

void ADuelFighter::Landed(const FHitResult& Hit)
{
    Super::Landed(Hit);
    if (!bBoss && IsAlive())
    {
        LandingPose = .20f;
        if (Arena && !bCharacterPreview)
        {
            Arena->PlayFootfall(Hit.ImpactPoint, 1.f, IsPlayerControlled());
        }
    }
}

void ADuelFighter::Flask()
{
    const bool bCanRestore = Item == EDuelItem::Flask ? Health < DuelCourt::PlayerMaxHealth : Focus < FocusMax();
    if (ItemCount() > 0 && bCanRestore)
    {
        RequestMove(EDuelMove::Flask);
    }
}

void ADuelFighter::CycleItem()
{
    if (CanUseEquipment() && Move != EDuelMove::Flask && !UGameplayStatics::IsGamePaused(this))
    {
        Item = Item == EDuelItem::Flask ? EDuelItem::FocusFlask : EDuelItem::Flask;
    }
}

void ADuelFighter::ConfirmDuel()
{
    if (ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>())
    {
        if (Arena && Arena->TryCourtInteraction(*this)) return;
        Mode->Confirm();
    }
}

void ADuelFighter::CycleCharacterView()
{
    if (ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>())
    {
        Mode->CycleCharacterView();
    }
}

void ADuelFighter::PauseDuel()
{
    if (ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>())
    {
        Mode->TogglePause();
    }
}

void ADuelFighter::MenuUp()
{
    if (ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>())
    {
        Mode->Navigate(-1);
    }
}

void ADuelFighter::MenuDown()
{
    if (ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>())
    {
        if (Mode->MenuPage() == EDuelMenuPage::None && Move == EDuelMove::Idle &&
            (Mode->Outcome() == EDuelOutcome::Fighting || Mode->Outcome() == EDuelOutcome::Ready))
        {
            CycleItem();
        }
        else
        {
            Mode->Navigate(1);
        }
    }
}

void ADuelFighter::MoveMenu(int32 Step)
{
    if (ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>())
    {
        Mode->Navigate(Step);
    }
}

FVector ADuelFighter::MoveInput() const
{
    const FRotationMatrix View(FRotator(0, ViewYaw, 0));
    return (View.GetUnitAxis(EAxis::X) * ForwardAxis + View.GetUnitAxis(EAxis::Y) * RightAxis)
        .GetClampedToMaxSize(1.f);
}

void ADuelFighter::FaceDirection(const FVector& Direction, float DeltaSeconds)
{
    if (!Direction.IsNearlyZero())
    {
        const FRotator Desired(0, Direction.Rotation().Yaw, 0);
        SetActorRotation(FMath::RInterpTo(GetActorRotation(), Desired, DeltaSeconds, bBoss ? 7.f : 13.f));
    }
}

void ADuelFighter::TickCamera(float DeltaSeconds)
{
    float ShoulderAngle = 0.f;
    float ArmGoal = 664.f;
    const bool bTracking = bLockOn && !bCombatOver && IsValid(Target) && Target->IsAlive();
    const FVector PreviousFocus = CameraFocus;
    const bool bHadFocus = bCameraFocusReady;
    const float Hazard = !bCombatOver && IsValid(Target) && Target->IsGroundHazard() ? 1.f : 0.f;
    HazardFraming = FMath::FInterpTo(HazardFraming, Hazard, DeltaSeconds, Hazard > 0.f ? 5.6f : 2.8f);
    if (bTracking)
    {
        const FVector Anchor = Target->LockPoint();
        const double Retain = FMath::Exp(-9.5 * DeltaSeconds);
        const bool bRecoveringSight = bCameraFocusReady && FVector::DistSquared(CameraFocus, Anchor) > FMath::Square(180.0);
        CameraFocus = bRecoveringSight ? FMath::VInterpTo(CameraFocus, Anchor, DeltaSeconds, 7.f) :
            bCameraFocusReady ? Anchor + ((CameraFocus - Anchor) * Retain).GetClampedToMaxSize(110.0) : Anchor;
        const FVector Offset = Target->GetActorLocation() - GetActorLocation();
        const float DesiredYaw = Offset.Rotation().Yaw;
        ViewYaw = FRotator::NormalizeAxis(ViewYaw + FMath::FindDeltaAngleDegrees(ViewYaw, DesiredYaw) *
            (1.f - FMath::Exp(-6.4f * DeltaSeconds)));
        const float Distance = static_cast<float>(Offset.Size2D());
        const float PitchGoal = FMath::Lerp(-6.1f, -8.2f, FMath::Clamp((Distance - 100.f) / 900.f, 0.f, 1.f));
        ViewPitch = FMath::FInterpTo(ViewPitch, PitchGoal, DeltaSeconds, 6.f);
        ArmGoal = FMath::Lerp(683.f, 803.f, FMath::Clamp((Distance - 110.f) / 890.f, 0.f, 1.f));
        ShoulderAngle = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(CameraBoom->SocketOffset.Y * .67),
            Distance + ArmGoal));
    }
    bCameraFocusReady = bTracking;
    const ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    const bool bFighting = Mode && Mode->Outcome() == EDuelOutcome::Fighting;
    const FVector At = GetActorLocation();
    // Raise only the final covered bridge/garden exploration view; manual pitch cancels the guidance.
    const bool bGardenView = Mode && Mode->UsesNorthTower() &&
        (Mode->Outcome() == EDuelOutcome::Ready || Mode->Outcome() == EDuelOutcome::Victory) &&
        At.X > 4300. && At.X < 7800. && FMath::Abs(At.Y) < 1300. && FMath::Abs(At.Z - 3462.) < 260.;
    const float GardenGoal = bGardenView ? FMath::SmoothStep(4500.f, 5500.f, static_cast<float>(At.X)) : 0.f;
    if (!bFighting && Mode && Mode->UsesNorthTower())
    {
        if (GardenGoal > 0.f && GardenCameraBlend <= .0001f && FMath::Abs(ViewPitch + 15.f) > .5f)
            bGardenCameraManual = true;
        if ((GardenGoal > 0.f || GardenCameraBlend > .0001f) && !bGardenCameraManual)
            ViewPitch = FMath::FInterpTo(ViewPitch, -15.f + 17.f * GardenGoal, DeltaSeconds, 2.3f);
        GardenCameraBlend = FMath::FInterpTo(GardenCameraBlend, GardenGoal, DeltaSeconds, 3.f);
        if (GardenGoal == 0.f && GardenCameraBlend <= .0001f)
        { GardenCameraBlend = 0.f; bGardenCameraManual = false; }
    }
    else { GardenCameraBlend = 0.f; bGardenCameraManual = false; }
    if (Mode && (Mode->UsesCastle() || Mode->UsesNorthTower()) && !bFighting)
    {
        ArmGoal = 532.f;
    }
    ArmGoal += HazardFraming * 95.f;
    const float Yaw = ViewYaw - ShoulderAngle;
    const float BasePitch = ViewPitch - HazardFraming * 3.8f;
    const double BaseHeight = (bTracking ? 205.0 : 115.0) + HazardFraming * 88.0;
    const FVector Forward = FRotator(0, Yaw, 0).Vector();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(DuelCombatCamera), false, this);
    if (IsValid(Target)) Query.AddIgnoredActor(Target.Get());
    // Extra skin keeps the near plane/lagged socket away from masonry relief.
    // Include North's real walls/ceilings and legacy camera-only boundaries.
    const float ProbeRadius = CameraBoom->ProbeSize + 9.f;
    const auto SweepClear = [&](const FVector& From, const FVector& To, FVector& Safe)
    {
        FHitResult Hit;
        if (!GetWorld()->SweepSingleByChannel(Hit, From, To, FQuat::Identity,
            CameraBoom->ProbeChannel, FCollisionShape::MakeSphere(ProbeRadius), Query))
        {
            Safe = To;
            return 1.f;
        }
        const double Length = FVector::Distance(From, To);
        const float Fraction = Hit.bStartPenetrating ? 0.f :
            FMath::Clamp(Hit.Time - 8.f / FMath::Max(1.f, static_cast<float>(Length)), 0.f, 1.f);
        Safe = FMath::Lerp(From, To, Fraction);
        return Fraction;
    };
    const float PreviousPressure = FMath::Clamp((76.f - static_cast<float>(CameraBoom->SocketOffset.Y)) / 58.f, 0.f, 1.f);
    float PressureGoal = 0.f;
    if (bFighting)
    {
        const FVector BasePivot = At + FVector(0, 0, BaseHeight);
        const FVector Desired = BasePivot + FRotator(BasePitch, Yaw, 0).RotateVector(
            FVector(-ArmGoal, 76, CameraBoom->SocketOffset.Z));
        FVector Safe;
        const float Clear = SweepClear(BasePivot, Desired, Safe);
        PressureGoal = FMath::SmoothStep(.08f, .64f, 1.f - Clear);
    }
    // Socket shoulder doubles as persistent pressure state; no static world map
    // or combat state is involved. Recover slowly to avoid corner pumping.
    const float Pressure = FMath::FInterpTo(PreviousPressure, PressureGoal, DeltaSeconds,
        PressureGoal > PreviousPressure ? 12.f : 3.8f);
    const float Pitch = BasePitch - Pressure * 5.f;
    const FRotator Rotation(Pitch, Yaw, 0);
    const FVector OffsetGoal = Forward * (Pressure * 120.f) + FVector(0, 0, BaseHeight + Pressure * 48.f);
    FVector Pivot = At + FMath::VInterpTo(CameraBoom->TargetOffset, OffsetGoal, DeltaSeconds, 15.f);
    if (bFighting)
    {
        // Sweep the anchor itself from inside the player's capsule. A shifted
        // pivot must not start on the far side of a column or a low ceiling.
        FVector SafePivot;
        SweepClear(At + FVector(0, 0, 32), Pivot, SafePivot);
        Pivot = SafePivot;
    }
    CameraBoom->TargetOffset = Pivot - At;
    CameraBoom->SocketOffset.Y = 76.f - Pressure * 58.f;
    CameraBoom->CameraLagMaxDistance = FMath::Lerp(48.f, 6.f, FMath::Max(Pressure, PressureGoal));
    CameraBoom->CameraLagSpeed = FMath::Lerp(13.f, 24.f, FMath::Max(Pressure, PressureGoal));
    const float ArmRate = Pressure > .02f || PressureGoal > .02f ?
        (ArmGoal < CameraBoom->TargetArmLength ? 12.f : 4.5f) : 9.f;
    float Arm = FMath::FInterpTo(CameraBoom->TargetArmLength, ArmGoal, DeltaSeconds, ArmRate);
    const FVector Socket = Rotation.RotateVector(CameraBoom->SocketOffset);
    FVector Resolved = Pivot - Rotation.Vector() * Arm + Socket;
    if (bFighting)
    {
        FVector Safe;
        const float Clear = SweepClear(Pivot, Resolved, Safe);
        if (Clear < 1.f)
        {
            // Immediate inward clearance is safety-critical; outward recovery
            // uses ArmRate above. The spring arm still performs its final sweep.
            Arm = FMath::Clamp(static_cast<float>(FVector::DotProduct(Pivot + Socket - Safe, Rotation.Vector())), 0.f, Arm);
            Resolved = Pivot - Rotation.Vector() * Arm + Socket;
        }
    }
    // Keep the built-in spring-arm sweep enabled even at an extreme corner.
    // UE skips its collision test for an exactly zero arm.
    Arm = FMath::Max(1.f, Arm);
    CameraBoom->TargetArmLength = Arm;
    CameraBoom->SetWorldRotation(Rotation);
    if (bTracking && bFighting)
    {
        const auto Visible = [&](const FVector& Aim)
        {
            FHitResult Hit;
            return !GetWorld()->LineTraceSingleByChannel(Hit, Resolved, Aim, CameraBoom->ProbeChannel, Query);
        };
        if (!Visible(CameraFocus))
        {
            const FVector Chest = Target->GetActorLocation() + FVector(0, 0, 45);
            FVector SafeFocus = Chest;
            if (!Visible(Chest))
            {
                // A genuinely hidden boss cannot be shown through a wall.
                // Keep the player's escape ground in view until line of sight
                // returns instead of staring into the intervening stone.
                const FVector NearGround = At + FVector(0, 0, 32);
                SweepClear(NearGround, NearGround + Forward * 160.f, SafeFocus);
            }
            CameraFocus = bHadFocus ? FMath::VInterpTo(PreviousFocus, SafeFocus, DeltaSeconds, 7.f) : SafeFocus;
        }
    }
    CameraImpulse = FMath::Max(0.f, CameraImpulse - DeltaSeconds * 4.1f);
    const float Time = GetWorld()->GetTimeSeconds();
    Camera->SetRelativeRotation(bTracking ? FRotator::ZeroRotator :
        FRotator(FMath::Sin(Time * 28.f) * CameraImpulse * .30f,
            FMath::Cos(Time * 24.f) * CameraImpulse * .16f, 0.f));
}

void ADuelFighter::CalcCamera(float DeltaSeconds, FMinimalViewInfo& View)
{
    Super::CalcCamera(DeltaSeconds, View);
    if (bLockOn && !bCombatOver && IsValid(Target) && Target->IsAlive())
    {
        const FVector AimAt = bCameraFocusReady ? CameraFocus : Target->LockPoint();
        View.Rotation = (AimAt - View.Location).Rotation();
        const float Cramped = FMath::Clamp((560.f - FVector::Dist2D(View.Location, GetActorLocation())) / 380.f, 0.f, 1.f);
        View.FOV += 4.f + HazardFraming * 10.f + Cramped * 8.f;
        const ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
        const float Pressure = FMath::Clamp((76.f - static_cast<float>(CameraBoom->SocketOffset.Y)) / 58.f, 0.f, 1.f);
        if (Mode && Mode->UsesNorthTower() && Pressure > .08f)
        {
            // Once a wall shortens the arm, centering a distant boss can put the
            // player's feet entirely below the screen. Fit boss + near ground,
            // allowing the boss to rise in frame only while physically cramped.
            float Aspect = Camera->AspectRatio;
            if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
            {
                int32 Width = 0, Height = 0; PC->GetViewportSize(Width, Height);
                if (Width > 0 && Height > 0) Aspect = static_cast<float>(Width) / Height;
            }
            Aspect = FMath::Max(1.f, Aspect);
            const FVector Heading = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
            const FVector Feet = GetActorLocation() - FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 14.f);
            FVector Ground = Feet - Heading * 55.f;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(DuelCameraGroundFrame), false, this);
            Query.AddIgnoredActor(Target.Get());
            FHitResult Hit;
            if (GetWorld()->LineTraceSingleByChannel(Hit, Feet, Ground, CameraBoom->ProbeChannel, Query))
                Ground = FMath::Lerp(Feet, Ground, FMath::Max(0.f, Hit.Time - .12f));
            const FVector ToGround = Ground - View.Location;
            const float GroundPitch = FMath::RadiansToDegrees(FMath::Atan2(ToGround.Z, ToGround.Size2D()));
            const float Top = View.Rotation.Pitch + 4.f;
            const float Bottom = FMath::Min(GroundPitch - 9.f, Top - 1.f);
            const float HalfNeeded = FMath::DegreesToRadians(FMath::Min(65.f, (Top - Bottom) * .5f));
            const float NeededFOV = FMath::RadiansToDegrees(2.f * FMath::Atan(FMath::Tan(HalfNeeded) * Aspect));
            // Same bounded vertical coverage on 16:10/16:9/ultrawide displays;
            // the emergency maximum corresponds to 100 horizontal degrees at 16:9.
            const float MaxFOV = FMath::RadiansToDegrees(2.f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(50.f)) * Aspect / (16.f / 9.f)));
            const float FramedFOV = FMath::Clamp(NeededFOV, View.FOV, FMath::Max(View.FOV, MaxFOV));
            const float HalfVertical = FMath::RadiansToDegrees(FMath::Atan(FMath::Tan(FMath::DegreesToRadians(FramedFOV * .5f)) / Aspect));
            const float LowPitch = Top - HalfVertical;
            const float HighPitch = Bottom + HalfVertical;
            const float FramedPitch = LowPitch <= HighPitch ? FMath::Clamp(static_cast<float>(View.Rotation.Pitch), LowPitch, HighPitch) : HighPitch;
            const float Blend = FMath::SmoothStep(.08f, .40f, Pressure);
            View.FOV = FMath::Lerp(View.FOV, FramedFOV, Blend);
            View.Rotation.Pitch = FMath::Lerp(static_cast<float>(View.Rotation.Pitch), FramedPitch, Blend);
        }
    }
}

void ADuelFighter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const float CameraDelta = DeltaSeconds;
    if (Hitstop > 0.f)
    {
        const float Frozen = FMath::Min(Hitstop, DeltaSeconds);
        Hitstop -= Frozen;
        DeltaSeconds -= Frozen;
    }
    if (!bCombatOver && IsAlive() && IsValid(Target) && IsValid(Arena))
    {
        if (bBoss)
        {
            Arena->TickMirrorCourt(this, DeltaSeconds);
            TickBoss(DeltaSeconds);
        }
        else
        {
            TickHands();
            TickPlayer(DeltaSeconds);
        }
    }
    else if (!bBoss && !bCharacterPreview && bCombatOver && IsAlive())
    {
        const ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
        if (Mode && Mode->CanExplore())
        {
            TickHands();
            TickPlayer(DeltaSeconds);
        }
    }
    ClampToCourt();
    PoseFigure(DeltaSeconds);
    if (!bBoss)
    {
        if (bCombatOver && !FMath::IsNearlyZero(RightAxis))
        {
            if (ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>())
            {
                Mode->TurnCharacterView(RightAxis * DeltaSeconds * 72.f);
            }
        }
        TickCamera(CameraDelta);
    }
}

void ADuelFighter::TickPlayer(float DeltaSeconds)
{
    if (JudgmentWeaponLockSeconds() > 0.f) CancelJudgmentActions();
    if (JudgmentMoveLockSeconds() > 0.f)
    {
        auto* Motion = GetCharacterMovement();
        Motion->Velocity.X = Motion->Velocity.Y = 0.;
        Motion->MaxWalkSpeed = 0.f;
        ConsumeMovementInputVector();
        if (Move != EDuelMove::Idle) TickMove(DeltaSeconds);
        return;
    }
    constexpr float StaminaRecoveryPerSecond = 28.f;
    const float RecoveryTime = FMath::Max(0.f, DeltaSeconds - RecoveryDelay);
    RecoveryDelay = FMath::Max(0.f, RecoveryDelay - DeltaSeconds);
    if (!DuelCourt::AllowsFreeLocomotion(Move))
    {
        TickMove(DeltaSeconds);
        return;
    }

    const FVector Input = MoveInput();
    if (ShiftPressedAt >= 0.0 && !Input.IsNearlyZero())
    {
        ShiftDirection = Input;
    }
    const bool bDrinking = Move == EDuelMove::Flask;
    const bool bGuarding = IsGuarding();
    const bool bSprint = !bDrinking && !bGuarding && Sprinting(Input);
    // Only a sprint that was actually entered consumes this Shift press.
    bShiftCommittedToSprint |= bSprint;
    if (bSprint && !bCombatOver)
    {
        Stamina = FMath::Max(0.f, Stamina - 19.f * DuelCourt::StaminaCostScale * DeltaSeconds);
        bSprintExhausted = Stamina <= 0.f;
        RecoveryDelay = .45f;
    }
    else if (RecoveryTime > 0.f)
    {
        Stamina = FMath::Min(DuelCourt::MaxStamina, Stamina + StaminaRecoveryPerSecond * RecoveryTime);
    }
    GetCharacterMovement()->MaxWalkSpeed = bDrinking ? FlaskWalkSpeed : (bSprint ? DuelCourt::PlayerSprintSpeed : PlayerWalkSpeed);
    AddMovementInput(Input.GetSafeNormal2D(), Input.Size2D());
    const bool bTrackGuard = bGuarding && bLockOn && !bCombatOver && IsValid(Target) && Target->IsAlive();
    FaceDirection(bTrackGuard ? Target->GetActorLocation() - GetActorLocation() : Input, DeltaSeconds);
    if (Move != EDuelMove::Idle)
    {
        TickMove(DeltaSeconds);
    }
}

void ADuelFighter::TickBoss(float DeltaSeconds)
{
    PostureDelay = FMath::Max(0.f, PostureDelay - DeltaSeconds);
    if (PostureDelay <= 0.f && Posture < DuelCourt::MaxPosture)
    {
        Posture = FMath::Max(0.f, Posture - 16.f * DeltaSeconds);
    }
    if (Move != EDuelMove::Idle)
    {
        TickMove(DeltaSeconds);
        return;
    }
    if (TryChangePhase())
    {
        return;
    }
    if (Posture >= DuelCourt::MaxPosture)
    {
        StartMove(EDuelMove::Stagger);
        return;
    }

    BossPause = FMath::Max(0.f, BossPause - DeltaSeconds);
    const FVector Offset = Target->GetActorLocation() - GetActorLocation();
    const float Distance = static_cast<float>(Offset.Size2D());
    FaceDirection(Offset, DeltaSeconds);

    const bool bEma = Character == EDuelCharacter::Ema;
    const bool bHiro = Character == EDuelCharacter::Hiro;
    const float AttackDistance = bEma ? (BossPhase >= 2 ? 1765.f : 1545.f) :
        bHiro ? (BossPhase >= 2 ? 1710.f : 1460.f) : 1770.f;
    const ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    const bool bAppeal = Mode && Mode->InAppeal();
    if (bAppeal && AppealIntro > 0.f)
    {
        AppealIntro -= DeltaSeconds;
        return;
    }
    const int32 Difficulty = BossDifficulty();
    if (BossPause <= 0.f && Distance < AttackDistance + Difficulty * 65.f)
    {
        const bool bChain = BossFollowup != EDuelMove::Idle && CanReachWith(BossFollowup, Distance);
        const EDuelMove Next = bChain ? BossFollowup : PickBossAttack(Distance);
        BossFollowup = EDuelMove::Idle;
        BossComboDepth = bChain ? BossComboDepth + 1 : 0;
        if (Next != EDuelMove::Idle && StartMove(Next))
        {
            PreviousBossAttack = LastBossAttack;
            LastBossAttack = Next;
            return;
        }
    }

    const FVector Ahead = Offset.GetSafeNormal2D();
    const FVector Across = FVector::CrossProduct(FVector::UpVector, Ahead) * OrbitSide;
    const float Walk = (bEma ? 390.f : bHiro ? 402.f : 359.f) +
        (BossPhase - 1) * (bEma ? 58.f : bHiro ? 60.f : 51.f) + Difficulty * 44.f;
    const float PursuitSpeed = Walk + 16.f + 22.f * Difficulty + 5.f * Difficulty * (BossPhase - 1);
    GetCharacterMovement()->MaxWalkSpeed = Distance > 420.f ? PursuitSpeed * (Distance > 870.f ? 1.21f : 1.f) : Walk;
    const bool bMirrorRanged = BossEncounter == 1 && BossPhase >= 2 && Mode && Mode->UsesNorthTower();
    if (bMirrorRanged && Distance < 860.f)
    {
        const float Keep = bEma ? 640.f : 710.f;
        const FVector Desired = Across + Ahead * FMath::Clamp((Distance - Keep) / 170.f, -.85f, .65f);
        const FVector Probe = GetActorLocation() - Arena->CourtOrigin() + Desired.GetSafeNormal2D() * 150.f;
        if (Arena->CourtBounds(GetCapsuleComponent()->GetScaledCapsuleRadius()).IsInside(FVector2D(Probe)))
            AddMovementInput(Desired.GetSafeNormal2D());
        else AddMovementInput((-Across + Ahead * .15f).GetSafeNormal2D());
    }
    else if (Distance > 420.f)
    {
        const float Strafe = (bEma ? .38f : bHiro ? .83f : .68f) * (1.f - .09f * Difficulty);
        AddMovementInput((Ahead + Across * Strafe).GetSafeNormal2D());
    }
    else if (BossPause <= .83f && Distance > 100.f)
    {
        const FVector At = GetActorLocation() - Arena->CourtOrigin();
        const FVector Probe = At + Across * 180.f;
        const bool bRoom = Arena->CourtBounds(GetCapsuleComponent()->GetScaledCapsuleRadius()).IsInside(FVector2D(Probe));
        AddMovementInput(((bRoom ? Across : -Across) + Ahead * (Distance > 265.f ? .56f : -.26f)).GetSafeNormal2D());
    }
}

bool ADuelFighter::CanReachWith(EDuelMove Attack, float Distance) const
{
    const int32 Stage = BossPhase - 1;
    const auto AdvancingReach = [this, Attack](float Reach)
    {
        return Reach + Timing(Attack, IsAwakened()).Distance * FMath::SmoothStep(0.f, 1.f, .61f);
    };
    switch (Attack)
    {
    case EDuelMove::Paw:         return Distance <= AdvancingReach(Paw[Stage].Reach);
    case EDuelMove::WingSweep:   return Distance <= AdvancingReach(Wing[Stage].Reach);
    case EDuelMove::ShadowSweep: return Distance <= DuelCourt::TailSweepReach(BossPhase);
    case EDuelMove::Pillar:      return Distance <= Pillar[Stage].Reach;
    case EDuelMove::Storm:       return Distance <= DuelCourt::StormRadius[Stage];
    case EDuelMove::HaloBreak:   return Distance <= (BossPhase == 3 ? 1200.f : 1030.f);
    case EDuelMove::Leap:        return Distance <= LeapTravel[Stage] + Leap[Stage].Reach;
    case EDuelMove::FlankLeap:   return Distance <= FlankTravel[Stage] + Flank[Stage].Reach * .78f;
    case EDuelMove::Wingfall:    return Distance <= FlankTravel[Stage] + 195.f + Wingfall[Stage].Reach * .78f;
    case EDuelMove::Reversal:    return Distance <= 685.f + Stage * 126.f + DuelCourt::ReversalReach(BossPhase);
    case EDuelMove::TalonRush:   return Distance <= RushTravel[Stage] + RushLead;
    case EDuelMove::RakingClaws: return Distance <= 325.f + Stage * 51.f +
        (292.f + Stage * 56.f) * FMath::SmoothStep(0.f, 1.f, DuelBossDefense::RakingBeat / DuelBossDefense::RakingActive);
    case EDuelMove::FeintCounter: return Distance <= 292.f + Stage * 46.f + (290.f + Stage * 58.f) * .62f;
    default:                     return true;
    }
}

bool ADuelFighter::TryChangePhase()
{
    if (!bBoss || Move != EDuelMove::Idle)
    {
        return false;
    }
    const bool bWings = BossPhase == 1 && Health <= DuelCourt::BossHealthFloor(1);
    const bool bEclipse = BossPhase == 2 && Health <= DuelCourt::BossHealthFloor(2);
    if ((!bWings && !bEclipse) || !StartMove(bWings ? EDuelMove::Awaken : EDuelMove::Eclipse))
    {
        return false;
    }
    Posture = PostureDelay = 0.f;
    BossComboDepth = 0;
    PreviousBossAttack = LastBossAttack = BossFollowup = EDuelMove::Idle;
    MapWaves.Reset();
    return true;
}

void ADuelFighter::RequestMove(EDuelMove Next, EDuelHand Hand)
{
    if (JudgmentBlocksAction(Next)) return;
    if (StartMove(Next, Hand) || Move == EDuelMove::Idle || (!bBoss && !CanUseEquipment()) || !IsAlive())
    {
        return;
    }
    const FMoveTiming Current = Timing(Move, IsAwakened(), MoveWeapon, MoveArt);
    if (Next == EDuelMove::Roll && Move == EDuelMove::Roll)
    {
        if (MoveAge >= DuelCourt::RollChainAt && TryDodgeCancel(Next))
        {
            return;
        }
        if (MoveAge >= DuelCourt::RollChainAt - .22f)
        {
            QueuedMove = Next;
            QueuedHand = Hand;
        }
        return;
    }
    if (IsDodge(Next) && !IsDodge(Move))
    {
        const float CancelAt = DodgeCancelAt(Move, Current);
        if (MoveAge >= CancelAt && TryDodgeCancel(Next))
        {
            return;
        }
        if (MoveAge >= CancelAt - .20f)
        {
            QueuedMove = Next;
            QueuedHand = Hand;
        }
        return;
    }
    if (Next == EDuelMove::Cut && Move == EDuelMove::Cut && MoveAge >= Current.Windup)
    {
        QueuedMove = Hand == MoveHand ? EDuelMove::Backcut : EDuelMove::Cut;
        QueuedHand = Hand;
    }
    else if (MoveAge >= Current.Total() - .20f)
    {
        QueuedMove = Next;
        QueuedHand = Hand;
    }
}

bool ADuelFighter::TryDodgeCancel(EDuelMove Next)
{
    if (!GetCharacterMovement()->IsMovingOnGround())
    {
        return false;
    }
    const EDuelMove Interrupted = Move;
    Move = EDuelMove::Idle;
    if (StartMove(Next))
    {
        return true;
    }
    Move = Interrupted;
    return false;
}

bool ADuelFighter::StartMove(EDuelMove Next, EDuelHand Hand)
{
    if (JudgmentBlocksAction(Next)) return false;
    if (Move != EDuelMove::Idle || !IsAlive() || !IsValid(Arena) || !IsValid(Target) ||
        (bBoss ? bCombatOver : !CanUseEquipment()))
    {
        return false;
    }
    if (Next == EDuelMove::Crimson)
    {
        Hand = ArtHand();
    }
    const EDuelWeapon Gear = EquippedWeapon(Hand);
    const EDuelArt Art = Next == EDuelMove::Crimson ? ActiveArt() : EDuelArt::Default;
    if (Next == EDuelMove::Crimson && Gear == EDuelWeapon::Staff && Art == EDuelArt::Default)
    {
        return false;
    }
    const FMoveTiming Spec = Timing(Next, IsAwakened(), Gear, Art);
    if (Spec.Total() <= 0.f)
    {
        return false;
    }
    UCharacterMovementComponent* Motion = GetCharacterMovement();
    const bool bEnoughStamina = CanSpendStamina(Next, Stamina, Spec.Cost);
    if (!bBoss && (!bEnoughStamina || !HasFocus(Art) ||
        (Motion->IsFalling() && Next != EDuelMove::Hurt && Next != EDuelMove::GuardBreak && Next != EDuelMove::Equip) ||
        (Next == EDuelMove::Flask && ItemCount() <= 0)))
    {
        return false;
    }

    if (bPoseReady)
    {
        PreviousLimbs = DisplayedLimbs;
        PreviousHinges = DisplayedHinges;
        PreviousBody = DisplayedBody;
        PreviousPivot = DisplayedPivot;
        const USceneComponent* Grip = Hand == EDuelHand::Right ? Weapon.Get() : LeftWeapon.Get();
        PreviousBlade = Grip && Figure ?
            (Figure->GetComponentQuat().Inverse() * Grip->GetComponentQuat()).Rotator() : DisplayedBlade;
    }
    Move = Next;
    ++MoveSerial;
    if (!bBoss && Next == EDuelMove::Flask)
    {
        Motion->MaxWalkSpeed = FlaskWalkSpeed;
    }
    MoveHand = Hand;
    MoveWeapon = Gear;
    MoveArt = Art;
    const bool bPreserveLocomotion = !bBoss && DuelCourt::AllowsFreeLocomotion(Next);
    if (!bBoss && !bPreserveLocomotion)
    {
        StopJumping();
    }
    QueuedMove = EDuelMove::Idle;
    MoveAge = PreviousWave = 0.f;
    bImpactPlayed = bHitDelivered = bTrackingBlade = false;
    ArtHeldTime = 0.f;
    bChargedArt = false;
    ClearChargeSeal();
    bFeintTriggered = false;
    Travel = Spec.Distance;
    if (!bBoss && Art == EDuelArt::Whirl && MoveWeapon == EDuelWeapon::Axe)
    {
        Travel += 66.f;
    }
    ActionDirection = GetActorForwardVector();
    if (!bPreserveLocomotion)
    {
        const double VerticalSpeed = Motion->Velocity.Z;
        Motion->StopMovementImmediately();
        if (Motion->IsFalling())
        {
            Motion->Velocity.Z = VerticalSpeed;
        }
    }

    if (Next == EDuelMove::Roll)
    {
        ActionDirection = DodgeDirection.IsNearlyZero() ? GetActorForwardVector() : DodgeDirection;
        SetActorRotation(ActionDirection.Rotation());
    }
    else if (Next == EDuelMove::Backstep)
    {
        ActionDirection = -GetActorForwardVector();
        Arena->Sound(EDuelSound::Roll, GetActorLocation());
    }
    else if (Next != EDuelMove::Flask && Next != EDuelMove::Hurt && Next != EDuelMove::GuardBreak && Next != EDuelMove::Stagger &&
        Next != EDuelMove::Equip && Next != EDuelMove::Parry && (bBoss || bLockOn))
    {
        FVector Point = Target->GetActorLocation();
        if (!bBoss && bLockOn) Arena->CourtSealAimPoint(Point);
        const FVector Aim = (Point - GetActorLocation()).GetSafeNormal2D();
        if (!Aim.IsNearlyZero())
        {
            ActionDirection = Aim;
            SetActorRotation(FRotator(0, Aim.Rotation().Yaw, 0));
        }
    }

    if (!bBoss && (IsLightAttack(Next) || Next == EDuelMove::Heavy))
    {
        const FVector Input = MoveInput();
        if (!bLockOn && !Input.IsNearlyZero())
        {
            ActionDirection = Input.GetSafeNormal2D();
            SetActorRotation(ActionDirection.Rotation());
        }
        const float ForwardInput = FMath::Max(0.f, static_cast<float>(FVector::DotProduct(Input, ActionDirection)));
        StrikeStepDirection = (ActionDirection + Input * .70f).GetSafeNormal2D();
        Travel += (Next == EDuelMove::Heavy ? 26.f : 44.f) + 30.f * ForwardInput;
    }

    Stamina = FMath::Max(0.f, Stamina - Spec.Cost);
    Focus -= DuelCourt::FocusCost(Art);
    if (Art == EDuelArt::HoundChase || Next == EDuelMove::Equip)
    {
        HoundFollowupUntil = 0.0;
    }
    if (Spec.Cost > 0.f)
    {
        RecoveryDelay = Next == EDuelMove::Roll || Next == EDuelMove::Backstep ? .45f : .70f;
    }

    const FVector Floor = Arena->GroundMark(GetActorLocation());
    const FVector LocalFloor = Floor - Arena->CourtOrigin();
    const FBox2D TravelBounds = Arena->CourtBounds(GetCapsuleComponent()->GetScaledCapsuleRadius());
    const auto* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    const bool bNorthFight = Mode && Mode->UsesNorthTower() && Mode->Outcome() == EDuelOutcome::Fighting;
    const auto FitLanding = [&](const FVector& From, const FVector& To)
    {
        const double Height = GetActorLocation().Z - Floor.Z;
        const FVector Lift(0, 0, Height);
        return SweepNorthCourtTravel(From + Lift, To + Lift) - Lift;
    };
    const int32 Stage = BossPhase - 1;
    CastAxis = ActionDirection;
    ClearAttackPreviews();
    MapWaves.Reset();
    ShadowSnares.Reset();
    if (DuelBossDefense::IsSignature(Next))
    {
        PrepareBossSignature(Spec);
    }
    else if (DuelBossDefense::IsCloseChain(Next))
    {
        PrepareCloseChain();
    }
    else if (Next == EDuelMove::FlankLeap || Next == EDuelMove::Wingfall)
    {
        const bool bWingfall = Next == EDuelMove::Wingfall;
        const FVector Across = FVector::CrossProduct(FVector::UpVector, ActionDirection);
        const FVector Mark = Target->GetActorLocation() + ActionDirection * (bWingfall ? 94.f : -91.f);
        const float Side = BossRandom.RandRange(0, 1) ? 1.f : -1.f;
        const FVector First = Arena->GroundMark(Mark + Across * (Side * (218.f + Stage * 28.f)));
        const FVector Other = Arena->GroundMark(Mark - Across * (Side * (218.f + Stage * 28.f)));
        const FVector Goal = FVector::DistSquared2D(First, Mark) >= FMath::Square(175.f) ? First : Other;
        const FVector Route = Goal - Floor;
        Travel = FMath::Min(static_cast<float>(Route.Size2D()), FlankTravel[Stage] + (bWingfall ? 195.f : 0.f));
        if (Travel > 1.f)
        {
            ActionDirection = Route.GetSafeNormal2D();
            SetActorRotation(FRotator(0, ActionDirection.Rotation().Yaw, 0));
        }
        CastOrigin = Floor + ActionDirection * Travel;
        if (bNorthFight)
        {
            CastOrigin = FitLanding(Floor, CastOrigin);
            Travel = static_cast<float>(FVector::Dist2D(Floor, CastOrigin));
        }
        if (bWingfall)
        {
            SealCenters[0] = Floor;
            Arena->Sigil(Floor, Takeoff[Stage].Reach, EclipseLight, Spec.Windup, EDuelMark::Takeoff, ActionDirection);
        }
        Arena->Sigil(CastOrigin, bWingfall ? Wingfall[Stage].Reach : Flank[Stage].Reach,
            Gold, Spec.Windup + Spec.Active * .94f, bWingfall ? EDuelMark::Wingfall : EDuelMark::Flank, ActionDirection);
        Arena->Sound(EDuelSound::Charge, Floor);
    }
    else if (Next == EDuelMove::Reversal)
    {
        const FVector Across = FVector::CrossProduct(FVector::UpVector, ActionDirection);
        const FVector Mark = Target->GetActorLocation() - ActionDirection * (178.f - Stage * 19.f);
        const FVector Left = Arena->GroundMark(Mark + Across * OrbitSide * (183.f + Stage * 25.f));
        const FVector Right = Arena->GroundMark(Mark - Across * OrbitSide * (183.f + Stage * 25.f));
        const FVector Goal = FVector::DistSquared2D(Mark, Left) > FMath::Square(160.f) ? Left : Right;
        const FVector Route = Goal - Floor;
        const float Distance = FMath::Min(static_cast<float>(Route.Size2D()), 685.f + Stage * 126.f);
        SealCenters[0] = Floor;
        CastOrigin = Floor + Route.GetSafeNormal2D() * Distance;
        if (bNorthFight) CastOrigin = FitLanding(Floor, CastOrigin);
        Arena->Sigil(CastOrigin, 79.f, CrimsonLight * .56f, Spec.Windup + DuelCourt::ReversalStepTime,
            EDuelMark::Pursuit, Route);
        Arena->Flash(Floor + FVector(0, 0, 50), CrimsonLight, 7800.f, Spec.Windup, 510.f);
        Arena->Sound(EDuelSound::Charge, Floor);
    }
    else if (Next == EDuelMove::ShadowSweep)
    {
        CastOrigin = Floor;
        Arena->TailSweepWarning(CastOrigin, CastAxis.Rotation().Yaw, DuelCourt::TailSweepReach(BossPhase),
            DuelCourt::TailSweepArc(BossPhase), Spec.Windup);
        Arena->Sound(EDuelSound::Charge, Floor);
    }
    else if (Next == EDuelMove::WingSweep)
    {
        const float HalfArc = FMath::RadiansToDegrees(FMath::Acos(WingFacing));
        const float ContactAt = Spec.Windup + Spec.Active * .61f;
        if (USceneComponent* Mark = Arena->WingSweep(Floor, ActionDirection, Wing[Stage].Reach, HalfArc,
            EclipseLight, ContactAt, false))
        {
            Mark->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
        }
    }
    else if (Next == EDuelMove::Leap)
    {
        const float TargetDistance = static_cast<float>(FVector::Dist2D(GetActorLocation(), Target->GetActorLocation()));
        Travel = FMath::Clamp(TargetDistance - 126.f, 132.f, LeapTravel[Stage]);
        CastOrigin = Arena->GroundMark(Floor + ActionDirection * Travel);
        if (bNorthFight)
        {
            CastOrigin = FitLanding(Floor, Floor + ActionDirection * Travel);
            Travel = static_cast<float>(FVector::Dist2D(Floor, CastOrigin));
        }
        Arena->Sigil(CastOrigin, Leap[Stage].Reach, FLinearColor(.88f, .28f, .29f, .78f),
            Spec.Windup + Spec.Active, EDuelMark::Leap, ActionDirection);
        Arena->Ring(CastOrigin, 31.f, FLinearColor(.52f, .055f, .11f, .48f),
            Spec.Windup + Spec.Active, Leap[Stage].Reach / 31.f, 3.f);
    }
    else if (Next == EDuelMove::Pillar || Next == EDuelMove::Storm)
    {
        Arena->Sigil(Floor, Next == EDuelMove::Pillar ? Pillar[Stage].Reach : DuelCourt::StormRadius[Stage],
            FLinearColor(.95f, .61f, .37f, .84f), Spec.Windup, Next == EDuelMove::Pillar ? EDuelMark::Pillar : EDuelMark::Storm);
        Arena->Ring(Floor, 102.f, Sakura, Spec.Windup, 1.8f, 3.f);
        Arena->Flash(Floor + FVector(0, 0, 125), Sakura, 2400.f, Spec.Windup, 525.f);
        Arena->Sound(EDuelSound::Charge, GetActorLocation());
    }
    else if (Next == EDuelMove::Awaken || Next == EDuelMove::Eclipse)
    {
        Arena->WitchTransformation(Floor, Character, Next == EDuelMove::Awaken ? 2 : 3, Spec.Windup, false);
        Arena->Sound(EDuelSound::Charge, GetActorLocation());
    }
    else if (Next == EDuelMove::Seals)
    {
        const FVector Aim = Arena->GroundMark(Target->GetActorLocation());
        const FVector Across = FVector::CrossProduct(FVector::UpVector, ActionDirection);
        SealCenters[0] = Aim;
        SealCenters[1] = Arena->GroundMark(Aim + ActionDirection * 130.f + Across * 255.f);
        SealCenters[2] = Arena->GroundMark(Aim + ActionDirection * 130.f - Across * 255.f);
        for (int32 I = 0; I < SealCenters.Num(); ++I)
        {
            Arena->Sigil(SealCenters[I], Seal[Stage].Reach, EclipseLight, Spec.Windup + I * SealSpacing,
                EDuelMark::Scroll, ActionDirection);
        }
        Arena->Sound(EDuelSound::Charge, Aim);
    }
    else if (Next == EDuelMove::Cross)
    {
        CastOrigin = Arena->GroundMark(Target->GetActorLocation());
        Arena->Lane(CastOrigin, ActionDirection, LaneHalfLength, LaneHalfWidth, EclipseLight, Spec.Windup,
            false, false, false, EDuelMark::CrossVerdict);
        Arena->Lane(CastOrigin, FVector::CrossProduct(FVector::UpVector, ActionDirection),
            LaneHalfLength, LaneHalfWidth, Sakura, Spec.Windup + LaneDelay, false, false, false, EDuelMark::CrossVerdict);
        Arena->Sound(EDuelSound::Charge, CastOrigin);
    }
    else if (Next == EDuelMove::AtlasRain || Next == EDuelMove::NightLanes || Next == EDuelMove::Pursuit ||
        Next == EDuelMove::WingVerdict || Next == EDuelMove::Tribunal ||
        Next == EDuelMove::HaloBreak || Next == EDuelMove::ShadowVolley)
    {
        if (Next == EDuelMove::Pursuit)
        {
            const FVector Across = FVector::CrossProduct(FVector::UpVector, ActionDirection);
            const float Side = BossRandom.RandRange(0, 1) ? 1.f : -1.f;
            const float Max = BossPhase == 3 ? 770.f : 565.f;
            const FVector First = (ActionDirection * .28f + Across * Side).GetSafeNormal2D();
            const FVector Other = (ActionDirection * .28f - Across * Side).GetSafeNormal2D();
            Travel = LimitTravel(Max, LocalFloor, First, TravelBounds);
            if (Travel >= Max * .55f)
            {
                ActionDirection = First;
            }
            else
            {
                ActionDirection = Other;
                Travel = LimitTravel(Max, LocalFloor, Other, TravelBounds);
            }
        }
        BeginMapAttack(Spec);
    }
    else if (Next == EDuelMove::RakingClaws)
    {
        const float MaxTravel = 292.f + 56.f * Stage;
        Travel = LimitTravel(MaxTravel, LocalFloor, ActionDirection, TravelBounds);
        Arena->Flash(Floor + FVector(0, 0, 115), CrimsonLight, 7300.f, Spec.Windup, 490.f);
        Arena->Sound(EDuelSound::Charge, Floor);
    }
    else if (Next == EDuelMove::BoneProcession)
    {
        CastOrigin = Floor;
        BoneWaveHits[0] = BoneWaveHits[1] = false;
        Arena->Sound(EDuelSound::Charge, Floor);
        Arena->BoneWave(Floor, 194.f, 1090.f + Stage * 224.f, Spec.Windup, true, Stage == 2);
    }
    else if (Next == EDuelMove::ShadowSnare)
    {
        PrepareShadowSnare(Spec);
    }
    else if (DuelCourt::IsYukiCast(Next))
    {
        if (Next == EDuelMove::PrismPursuit)
        {
            const FVector Side = FVector::CrossProduct(FVector::UpVector, ActionDirection) * OrbitSide;
            ActionDirection = (Side + ActionDirection * .43f).GetSafeNormal2D();
            Travel = LimitTravel(Travel, LocalFloor, ActionDirection, TravelBounds);
        }
        PrepareYukiAttack(Spec);
    }
    else if (Next == EDuelMove::FeintCounter)
    {
        Travel = 290.f + Stage * 58.f;
        Arena->Flash(Floor + FVector(0, 0, 95), CrimsonLight, 4600.f, .43f, 465.f);
    }
    else if (Next == EDuelMove::WrittenVerdict)
    {
        PrepareWrittenVerdict(Spec);
    }
    else if (Next == EDuelMove::CrossAmbush)
    {
        const FVector Across = FVector::CrossProduct(FVector::UpVector, ActionDirection);
        const FVector Aim = Arena->GroundMark(Target->GetActorLocation());
        const FVector Side = Across * OrbitSide;
        SealCenters[0] = Floor;
        SealCenters[1] = Arena->GroundMark(Aim + ActionDirection * 221.f + Side * (346.f + Stage * 42.f));
        SealCenters[2] = Arena->GroundMark(Aim - ActionDirection * 264.f - Side * (301.f + Stage * 34.f));
        if (bNorthFight)
        {
            SealCenters[1] = FitLanding(SealCenters[0], SealCenters[1]);
            SealCenters[2] = FitLanding(SealCenters[1], SealCenters[2]);
        }
        for (int32 I = 0; I < 2; ++I)
        {
            const FVector A = SealCenters[I];
            const FVector B = SealCenters[I + 1];
            const FVector Axis = (B - A).GetSafeNormal2D();
            Arena->Lane((A + B + Axis * 118.f) * .5, Axis, (FVector::Dist2D(A, B) + 118.f) * .5f,
                Stage == 2 ? 139.f : 109.f, I == 0 ? CrimsonLight * .53f : Sakura * .64f,
                Spec.Windup + I * DuelBossDefense::AmbushSecondStart, false, false, false, EDuelMark::CrossAmbush);
        }
        Arena->Sound(EDuelSound::Charge, Floor);
    }
    else if (Next == EDuelMove::TalonRush)
    {
        const double Distance = FVector::Dist2D(GetActorLocation(), Target->GetActorLocation());
        Travel = LimitTravel(FMath::Clamp(static_cast<float>(Distance) - 106.f, 160.f, RushTravel[Stage]),
            LocalFloor, ActionDirection, TravelBounds);
        RushHalfLength = (Travel + RushLead + RushTail) * .5f;
        CastOrigin = Floor + ActionDirection * (RushHalfLength - RushTail);
        Arena->Lane(CastOrigin, ActionDirection, RushHalfLength, Rush[Stage].Reach, CrimsonLight, Spec.Windup,
            false, false, false, EDuelMark::TalonRush);
        Arena->Flash(Floor + FVector(0, 0, 103), CrimsonLight, 11200.f, Spec.Windup, 720.f);
        Arena->Sound(EDuelSound::Charge, Floor);
    }
    else if (Next == EDuelMove::InwardReap)
    {
        CastOrigin = Floor;
        Arena->Halo(CastOrigin, DuelCourt::ReapInner, DuelCourt::ReapOuter, CrimsonLight,
            Spec.Windup, false, EDuelMark::Reap);
        Arena->Sound(EDuelSound::Charge, Floor);
    }
    else if (Next == EDuelMove::Stagger || Next == EDuelMove::GuardReel)
    {
        BossFollowup = EDuelMove::Idle;
        BossComboDepth = 0;
        if (Next == EDuelMove::Stagger)
        {
            Posture = 0.f;
            Arena->Detonate(Floor, 160.f, Gold, .42f);
        }
    }
    else if (Next == EDuelMove::Crimson)
    {
        PrepareArt(Spec);
    }
    return true;
}

float ADuelFighter::TravelProgress(float Age, const FMoveTiming& Spec) const
{
    if (Move == EDuelMove::Crimson && MoveArt == EDuelArt::HoundFinesse)
    {
        const float Step = FMath::SmoothStep(0.f, 1.f, (Age - .04f) / (Spec.Windup - .04f));
        const float Retreat = FMath::SmoothStep(0.f, 1.f, (Age - Spec.Windup - .14f) / .21f);
        const float Forward = DuelCourt::HoundStepDistance / Spec.Distance;
        return Forward * Step - (1.f + Forward) * Retreat;
    }
    if (Move == EDuelMove::Backstep)
    {
        return 1.f - FMath::Square(1.f - FMath::Clamp(Age / .205f, 0.f, 1.f));
    }
    if (Move == EDuelMove::Roll)
    {
        return FMath::SmoothStep(0.f, 1.f, (Age - DuelCourt::RollMotionDelay) / DuelCourt::RollMotionTime);
    }
    if (bBoss && Character == EDuelCharacter::Ema && Move == EDuelMove::Pursuit)
    {
        return FMath::SmoothStep(0.f, 1.f, (Age - Spec.Windup) / DuelBossDefense::EmaDashTime);
    }
    const float Lead = !bBoss && IsLightAttack(Move) ? .075f : !bBoss && Move == EDuelMove::Heavy ? .055f : 0.f;
    return FMath::SmoothStep(0.f, 1.f, (Age - Spec.Windup + Lead) / (Spec.Active + Lead));
}

float ADuelFighter::MoveRecoveryAt(const DuelCourt::FMoveTiming& Spec) const
{
    const float ActiveEnd = Spec.Windup + Spec.Active;
    if (!DuelBossDefense::HasProjectileCadence(Move) || MapWaves.IsEmpty())
    {
        return ActiveEnd;
    }
    const float Retired = MapWaves.Last().ProjectileRetiredAt;
    return Retired < 0.f ? TNumericLimits<float>::Max() : FMath::Max(ActiveEnd, Retired);
}

void ADuelFighter::TickMove(float DeltaSeconds)
{
    if (JudgmentBlocksAction(Move)) { CancelJudgmentActions(); return; }
    const EDuelMove Current = Move;
    const bool bCloseChain = DuelBossDefense::IsCloseChain(Current);
    const bool bSignature = DuelBossDefense::IsSignature(Current);
    if (bCloseChain || bSignature)
    {
        // Preserve the authored reaction windows when a frame hitches.
        DeltaSeconds = FMath::Min(DeltaSeconds, DuelBossDefense::CloseMaxStep);
    }
    const bool bProjectileSequence = DuelBossDefense::HasProjectileCadence(Current);
    const FMoveTiming Spec = Timing(Current, IsAwakened(), MoveWeapon, MoveArt);
    const float LastAge = MoveAge;
    const FVector BeforeTravel = GetActorLocation();
    FVector BladeSweepFrom = BeforeTravel;
    float BladeSweepAge = LastAge;
    MoveAge = bProjectileSequence ? MoveAge + DeltaSeconds : FMath::Min(MoveAge + DeltaSeconds, Spec.Total());
    const float HoldExtension = !bBoss && Current == EDuelMove::Crimson ? DuelCourt::ChargeExtension(MoveWeapon, MoveArt) : 0.f;
    if (HoldExtension > 0.f && bArtHeld && LastAge < Spec.Windup)
    {
        const float PreviousHold = ArtHeldTime;
        ArtHeldTime += DeltaSeconds;
        const float Approach = FMath::Max(0.f, Spec.Windup - .001f - LastAge);
        const float AfterCap = FMath::Clamp(ArtHeldTime - Spec.Windup - HoldExtension, 0.f, DeltaSeconds);
        MoveAge = FMath::Min(MoveAge, LastAge + Approach + AfterCap);
        const bool bFull = !bChargedArt && ArtHeldTime >= Spec.Windup * DuelCourt::ChargeReadyFraction;
        const bool bMid = PreviousHold < Spec.Windup * .42f && ArtHeldTime >= Spec.Windup * .42f;
        if (bFull || bMid)
        {
            bChargedArt |= bFull;
            FTransform Grip = FTransform::Identity;
            PoseFigure(0.f, &Grip);
            const FVector At = Grip.TransformPosition(FVector(DuelCourt::BladeSpan(MoveWeapon).Y, 0, 0));
            Arena->CastAccent(MoveArt == EDuelArt::IceLance ? EDuelMark::IceLance : EDuelMark::EmberRecoil,
                At, ActionDirection, DuelCourt::WeaponArt(MoveArt).Glow, bFull);
        }
    }
    const bool bChainRoll = Current == EDuelMove::Roll && QueuedMove == EDuelMove::Roll;
    if (bChainRoll && LastAge < DuelCourt::RollChainAt && GetCharacterMovement()->IsMovingOnGround() &&
        CanSpendStamina(Current, Stamina, Spec.Cost))
    {
        MoveAge = FMath::Min(MoveAge, DuelCourt::RollChainAt);
    }

    const bool bLeap = Current == EDuelMove::Leap || Current == EDuelMove::FlankLeap || Current == EDuelMove::Wingfall;
    if (Travel > 0.f)
    {
        const FVector Direction = !bBoss && (IsLightAttack(Current) || Current == EDuelMove::Heavy) ? StrikeStepDirection : ActionDirection;
        const auto Advance = [&](float From, float To)
        {
            const float Distance = Travel * (TravelProgress(To, Spec) - TravelProgress(From, Spec));
            // An airborne leap can cross the player on its way to the marked landing point.
            if (bLeap)
            {
                const FVector Start = GetActorLocation();
                SetActorLocation(SweepNorthCourtTravel(Start, Start + Direction * Distance));
            }
            else
            {
                AddActorWorldOffset(Direction * Distance, true);
            }
            ClampToCourt();
        };
        if (Current == EDuelMove::Crimson && MoveArt == EDuelArt::HoundFinesse && LastAge < Spec.Windup && MoveAge >= Spec.Windup)
        {
            // A long frame may cross both the step and retreat. Sweep each leg through collision.
            Advance(LastAge, Spec.Windup);
            BladeSweepAge = Spec.Windup;
            BladeSweepFrom = GetActorLocation();
        }
        Advance(BladeSweepAge, MoveAge);
    }
    if (Current == EDuelMove::Roll && LastAge < DuelCourt::RollMotionDelay && MoveAge >= DuelCourt::RollMotionDelay)
    {
        Arena->Sound(EDuelSound::Roll, BeforeTravel);
    }
    const float PlantAt = Spec.Windup * .62f;
    if (!bBoss && LastAge < PlantAt && MoveAge >= PlantAt &&
        (Current == EDuelMove::Heavy || (Current == EDuelMove::Crimson && DuelCourt::IsBladeArt(MoveArt))))
    {
        const FVector Axis = GetActorForwardVector();
        const FVector Toe = BeforeTravel + Axis * 30.f + GetActorRightVector() * (MoveHand == EDuelHand::Right ? 15.f : -15.f);
        Arena->BladeStep(Arena->GroundMark(Toe), Axis, Current == EDuelMove::Heavy);
    }
    if (Current == EDuelMove::FlankLeap && LastAge < Spec.Windup && MoveAge >= Spec.Windup)
    {
        Arena->Burst(Arena->GroundMark(BeforeTravel) + FVector(0, 0, 35), Gold, 38, 536.f, true);
    }
    if (Current == EDuelMove::Pursuit && LastAge < Spec.Windup + .19f && MoveAge >= Spec.Windup + .19f)
    {
        const FVector Foot = Arena->GroundMark(GetActorLocation() + ActionDirection * 42.f);
        Arena->BladeStep(Foot, ActionDirection, true);
        if (Character == EDuelCharacter::Ema)
            Arena->WingSweep(Foot + FVector(0, 0, 64), ActionDirection, 160.f, 66.f, Gold, .30f, true);
        else
            Arena->ClawWake(Foot + FVector(0, 0, 34), ActionDirection, 68.f, BossPhase);
    }
    if (bBoss && Character == EDuelCharacter::Ema && Current == EDuelMove::Pursuit &&
        MoveAge >= Spec.Windup && LastAge < Spec.Windup + DuelBossDefense::EmaDashTime &&
        !BeforeTravel.Equals(GetActorLocation(), .5f))
    {
        const FVector Lift(0, 0, 67.f);
        Arena->TraceBossSignature(BeforeTravel - ActionDirection * 34.f + Lift, GetActorLocation() + Lift, Gold, .30f);
        if (LastAge < Spec.Windup || FMath::FloorToInt(LastAge / .09f) != FMath::FloorToInt(MoveAge / .09f))
            Arena->WingSweep(Arena->GroundMark(GetActorLocation()) + FVector(0, 0, 64),
                ActionDirection, 132.f, 58.f, Gold, .26f, true);
    }
    if (Current == EDuelMove::Wingfall && LastAge < Spec.Windup && MoveAge >= Spec.Windup)
    {
        const FVector& At = SealCenters[0];
        const FBossArea& Area = Takeoff[BossPhase - 1];
        Arena->Detonate(At, Area.Reach, EclipseLight, .53f, EDuelMark::Takeoff, CastAxis);
        if (FVector::DistSquared2D(Target->GetActorLocation(), At) <= FMath::Square(Area.Reach))
        {
            HitGroundSpell(Area.Damage, At);
        }
        if (bCombatOver)
        {
            return;
        }
    }
    if (Current == EDuelMove::FeintCounter && LastAge < Spec.Windup && MoveAge >= Spec.Windup)
    {
        const FVector Aim = (Target->GetActorLocation() - BeforeTravel).GetSafeNormal2D(UE_SMALL_NUMBER, ActionDirection);
        ActionDirection = Aim;
        SetActorRotation(Aim.Rotation());
        Arena->Sound(EDuelSound::Cut, GetActorLocation());
    }

    const bool bStrike = Current == EDuelMove::Cut || Current == EDuelMove::Backcut ||
        Current == EDuelMove::Heavy || Current == EDuelMove::Paw || Current == EDuelMove::WingSweep ||
        Current == EDuelMove::FeintCounter ||
        (Current == EDuelMove::Crimson && (DuelCourt::IsBladeArt(MoveArt) || MoveArt == EDuelArt::Burst));
    float ImpactFraction = bLeap ? .94f : bStrike ? .61f : 0.f;
    if (Current == EDuelMove::Crimson && MoveArt == EDuelArt::HoundFinesse)
    {
        ImpactFraction = .28f;
    }
    else if (Current == EDuelMove::Crimson && MoveArt == EDuelArt::SableBlade)
    {
        ImpactFraction = .70f;
    }
    const float ImpactAt = Spec.Windup + Spec.Active * ImpactFraction;
    if (!bImpactPlayed && MoveAge >= ImpactAt)
    {
        ClearChargeSeal();
        bImpactPlayed = true;
        Impact();
    }

    if ((bCombatOver && (bBoss || !CanUseEquipment())) || Move != Current)
    {
        return;
    }
    CheckBladeContact(BladeSweepAge, Spec, BladeSweepFrom);
    if ((bCombatOver && (bBoss || !CanUseEquipment())) || Move != Current)
    {
        return;
    }
    if (Current == EDuelMove::Storm && bImpactPlayed)
    {
        TickStorm(FMath::Clamp((MoveAge - Spec.Windup) / Spec.Active, 0.f, 1.f));
    }
    else if (Current == EDuelMove::Seals)
    {
        TickSeals(LastAge);
    }
    else if (Current == EDuelMove::Cross && LastAge < Spec.Windup + LaneDelay && MoveAge >= Spec.Windup + LaneDelay)
    {
        StrikeLine(CastOrigin, FVector::CrossProduct(FVector::UpVector, ActionDirection));
    }
    else if (bCloseChain)
    {
        TickCloseChain(LastAge);
    }
    else if (bSignature)
    {
        TickBossSignature(LastAge, Spec);
    }
    else if (bProjectileSequence)
    {
        TickBossProjectiles(Spec);
    }
    else if (Current == EDuelMove::AtlasRain || Current == EDuelMove::NightLanes || Current == EDuelMove::Pursuit ||
        Current == EDuelMove::WingVerdict || Current == EDuelMove::Tribunal)
    {
        TickMapAttack(LastAge);
    }
    else if (Current == EDuelMove::HaloBreak)
    {
        TickHaloBreak(LastAge, Spec);
    }
    else if (Current == EDuelMove::BoneProcession)
    {
        TickBoneProcession(LastAge, Spec);
    }
    else if (Current == EDuelMove::WrittenVerdict)
    {
        TickWrittenVerdict(LastAge, Spec);
    }
    else if (Current == EDuelMove::CrossAmbush)
    {
        TickCrossAmbush(LastAge, Spec);
    }
    else if (Current == EDuelMove::ShadowSnare)
    {
        TickShadowSnare(LastAge, Spec);
    }
    else if (Current == EDuelMove::ShadowSweep)
    {
        TickShadowSweep(LastAge, Spec);
    }
    else if (DuelCourt::IsYukiCast(Current))
    {
        TickYukiAttack(LastAge, Spec);
    }
    else if (Current == EDuelMove::Reversal)
    {
        TickReversal(LastAge, Spec);
    }
    else if (Current == EDuelMove::InwardReap)
    {
        TickInwardReap(LastAge, Spec);
    }
    else if (Current == EDuelMove::TalonRush)
    {
        TickTalonRush(LastAge, Spec, BeforeTravel);
    }
    else if (Current == EDuelMove::RakingClaws && LastAge < Spec.Windup + DuelBossDefense::RakingBeat && MoveAge >= Spec.Windup + DuelBossDefense::RakingBeat)
    {
        const FVector Ground = Arena->GroundMark(GetActorLocation());
        const float Reach = 325.f + (BossPhase - 1) * 51.f;
        Arena->Slash(Ground + FVector(0, 0, 53), ActionDirection.Rotation().Yaw - 12.f, Reach, true, Sakura);
        Arena->Sound(EDuelSound::Cut, Ground);
        TryMelee(Reach, -.08f, 36.f + (BossPhase - 1) * 7.f, 0.f, true);
    }
    else if (Current == EDuelMove::Crimson && MoveArt == EDuelArt::Blackwell &&
        LastAge < Spec.Windup + .52f && MoveAge >= Spec.Windup + .52f)
    {
        const auto& Art = DuelCourt::WeaponArt(MoveArt);
        Arena->Detonate(CastOrigin, Art.Strike.Reach, Art.Glow, .67f, EDuelMark::InkRupture);
        if (!bCombatOver && IsValid(Target) &&
            FVector::DistSquared2D(Target->GetActorLocation(), CastOrigin) <= FMath::Square(Art.Strike.Reach) &&
            Target->ReceiveDuelDamage(Art.Strike.Damage * .72f, CastOrigin))
        {
            Target->DamagePosture(Art.Strike.Posture * .65f);
        }
    }
    else if (Current == EDuelMove::Crimson && MoveArt == EDuelArt::HiroBind &&
        LastAge < Spec.Windup + .39f && MoveAge >= Spec.Windup + .39f)
    {
        BindStrike(DuelCourt::WeaponArt(MoveArt).Strike, true);
    }
    else if (Current == EDuelMove::Crimson && MoveArt == EDuelArt::ReturningFrost &&
        LastAge < Spec.Windup + DuelCourt::ReturningFrostRecall && MoveAge >= Spec.Windup + DuelCourt::ReturningFrostRecall)
    {
        const auto& Art = DuelCourt::WeaponArt(MoveArt);
        Arena->FrostReturn(CastOrigin, DuelCourt::ReturningFrostInner, Art.Strike.Reach, Art.Glow);
        StrikeReturningFrost(Art.Strike);
    }
    else if (Current == EDuelMove::Crimson && MoveArt == EDuelArt::PenScript && bHitDelivered &&
        LastAge < Spec.Windup + .32f && IsValid(Target) && Target->IsAlive())
    {
        const FVector Mark = ArtGround(Target->GetActorLocation());
        if (!Mark.Equals(CastOrigin, .01) && !MapWaves.IsEmpty())
            if (auto* Preview = MapWaves[0].Preview.Get()) Preview->AddWorldOffset(Mark - CastOrigin);
        CastOrigin = Mark;
        if (MoveAge >= Spec.Windup + .32f)
        {
            ClearAttackPreviews();
            const DuelCourt::FStrike& Attack = DuelCourt::WeaponArt(MoveArt).Strike;
            StrikeLine(CastOrigin, FVector::CrossProduct(FVector::UpVector, CastAxis),
                Attack.Reach * .61f, 66.f, Attack.Damage * 1.50f, Moonlight, true);
        }
    }
    else if (Current == EDuelMove::Crimson && MoveArt == EDuelArt::EmaVotive)
    {
        for (int32 I = 1; I < SealCenters.Num() && CanUseEquipment(); ++I)
        {
            const float At = Spec.Windup + I * .29f;
            if (LastAge < At && MoveAge >= At)
            {
                StrikeVotive(DuelCourt::WeaponArt(MoveArt).Strike, I);
            }
        }
    }
    if ((bCombatOver && (bBoss || !CanUseEquipment())) || Move != Current)
    {
        return;
    }
    const float CancelAt = bChainRoll ? DuelCourt::RollChainAt : DodgeCancelAt(Current, Spec);
    if ((bChainRoll || (!IsDodge(Current) && IsDodge(QueuedMove))) && MoveAge >= CancelAt && TryDodgeCancel(QueuedMove))
    {
        // Preserve the portion of the frame after the cancel so the defense window is independent of frame rate.
        const float Remainder = LastAge + DeltaSeconds - FMath::Max(LastAge, CancelAt);
        if (Remainder > 0.f)
        {
            TickMove(Remainder);
        }
        return;
    }
    if (MoveAge < (bProjectileSequence ? MoveRecoveryAt(Spec) + Spec.Recovery : Spec.Total()))
    {
        return;
    }

    if (bSignature)
    {
        ClearAttackPreviews();
    }
    const EDuelMove Buffered = QueuedMove;
    const EDuelHand BufferedHand = QueuedHand;
    Move = EDuelMove::Idle;
    MoveAge = 0.f;
    QueuedMove = EDuelMove::Idle;
    if (!bBoss && Current == EDuelMove::Flask)
    {
        GetCharacterMovement()->MaxWalkSpeed = PlayerWalkSpeed;
    }
    if (Buffered != EDuelMove::Idle)
    {
        StartMove(Buffered, BufferedHand);
    }
    if (bBoss)
    {
        const float Distance = static_cast<float>(FVector::Dist2D(GetActorLocation(), Target->GetActorLocation()));
        BossFollowup = PickBossFollowup(Current, Distance);
        const int32 Difficulty = BossDifficulty();
        if (BossFollowup != EDuelMove::Idle)
        {
            constexpr float FollowupPause[] = {.12f, .075f, .06f};
            BossPause = FollowupPause[Difficulty];
        }
        else
        {
            constexpr float IdlePressure[] = {.025f, .055f, .088f};
            constexpr float ChainRest[] = {.40f, .32f, .23f};
            const float Pressure = (BossPhase - 1) * .083f + Difficulty * .077f + IdlePressure[Difficulty];
            const float PhasePressure = Character == EDuelCharacter::Ema ? 0.f : .035f * (BossPhase - 1);
            BossPause = FMath::Max(.075f, BossRandom.FRandRange(.29f, .55f) -
                Pressure - PhasePressure + (BossComboDepth > 0 ? ChainRest[Difficulty] : 0.f));
            if (Current == EDuelMove::Stagger || Current == EDuelMove::GuardReel)
            {
                BossPause = Current == EDuelMove::Stagger ? .38f : .18f;
            }
            OrbitSide = BossRandom.RandRange(0, 1) == 0 ? -1.f : 1.f;
        }
    }
}

void ADuelFighter::TryMelee(float Reach, float FacingDot, float Damage, float PoiseDamage, bool bParryable, bool bBlockable)
{
    const FVector Offset = Target->GetActorLocation() - GetActorLocation();
    if (Offset.SizeSquared2D() <= FMath::Square(Reach) &&
        (Offset.IsNearlyZero() || FVector::DotProduct(ActionDirection, Offset.GetSafeNormal2D()) >= FacingDot) &&
        Target->ReceiveDuelDamage(Damage, GetActorLocation(), bParryable, nullptr, false, true, bBlockable))
    {
        Target->DamagePosture(PoiseDamage);
    }
}

void ADuelFighter::Impact()
{
    if (JudgmentBlocksAction(Move)) return;
    const FVector Floor = Arena->GroundMark(GetActorLocation());
    const int32 Stage = BossPhase - 1;
    if ((Move == EDuelMove::Crimson && MoveArt != EDuelArt::EmaWard) || (bBoss && BossComboDepth == 0 &&
        (Move == EDuelMove::Tribunal || Move == EDuelMove::ShadowSnare || Move == EDuelMove::InwardReap)))
    {
        Speak(EDuelVoice::Art);
    }
    else if (Move == EDuelMove::Heavy || (bBoss && BossComboDepth == 0 &&
        (Move == EDuelMove::FlankLeap || Move == EDuelMove::TalonRush || Move == EDuelMove::Wingfall)))
    {
        Speak(EDuelVoice::Attack);
    }
    if (const DuelCourt::FStrike* Attack = DuelCourt::Strike(MoveWeapon, Move, MoveArt))
    {
        if (Move == EDuelMove::Crimson)
        {
            CommitArt(*Attack);
        }
        return;
    }

    switch (Move)
    {
    case EDuelMove::Parry:
        Arena->Slash(Floor + FVector(0, 0, 62), ActionDirection.Rotation().Yaw, 103.f, false, Moonlight);
        break;
    case EDuelMove::Equip:
        (PendingHand == EDuelHand::Right ? RightReserve : LeftReserve) = EquippedWeapon(PendingHand);
        SetWeapon(PendingWeapon, PendingHand);
        Arena->Burst(Floor + FVector(0, 0, 65), Gold, 9, 130.f);
        break;
    case EDuelMove::Flask:
    {
        if (Item == EDuelItem::Flask)
        {
            --Flasks;
            Health = FMath::Min(DuelCourt::PlayerMaxHealth,
                Health + DuelCourt::PlayerMaxHealth * DuelCourt::FlaskRestoreFraction);
        }
        else
        {
            --FocusFlasks;
            const float Capacity = FocusMax();
            Focus = FMath::Min(Capacity, Focus + Capacity * DuelCourt::FlaskRestoreFraction);
        }
        const FLinearColor& Tint = Item == EDuelItem::Flask ? Gold : Moonlight;
        Arena->Burst(GetActorLocation() + FVector(0, 0, 38), Tint, 24, 165.f);
        Arena->Flash(GetActorLocation(), Tint, 4500.f, .47f, 490.f);
        Arena->Sound(EDuelSound::Flask, GetActorLocation());
        break;
    }
    case EDuelMove::Paw:
        Arena->Slash(Floor + FVector(0, 0, 62), ActionDirection.Rotation().Yaw, Paw[Stage].Reach, true);
        Arena->Sound(EDuelSound::Cut, GetActorLocation());
        TryMelee(Paw[Stage].Reach, .02f, Paw[Stage].Damage, 0.f, true);
        break;
    case EDuelMove::WingSweep:
        Arena->WingSweep(Floor, ActionDirection, Wing[Stage].Reach, FMath::RadiansToDegrees(FMath::Acos(WingFacing)),
            EclipseLight, .35f, true);
        Arena->Sound(EDuelSound::Cut, Floor);
        TryMelee(Wing[Stage].Reach, WingFacing, Wing[Stage].Damage, 0.f, true);
        break;
    case EDuelMove::RakingClaws:
    {
        const float Reach = 290.f + Stage * 45.f;
        Arena->Slash(Floor + FVector(0, 0, 91), ActionDirection.Rotation().Yaw + 14.f, Reach, true, CrimsonLight);
        Arena->Sound(EDuelSound::Cut, Floor);
        TryMelee(Reach, .03f, 32.f + Stage * 6.f, 0.f, true);
        break;
    }
    case EDuelMove::FeintCounter:
    {
        const float Reach = 292.f + Stage * 46.f;
        Arena->Slash(Floor + FVector(0, 0, 79), ActionDirection.Rotation().Yaw + 16.f, Reach, true, CrimsonLight);
        Arena->Burst(Floor + ActionDirection * 122.f + FVector(0, 0, 51), CrimsonLight, 24, 520.f, true);
        TryMelee(Reach, .22f, (bFeintTriggered ? 64.f : 49.f) + Stage * 7.f, 0.f, true);
        break;
    }
    case EDuelMove::Leap:
    case EDuelMove::FlankLeap:
    case EDuelMove::Wingfall:
    {
        const bool bFlank = Move == EDuelMove::FlankLeap;
        const bool bWingfall = Move == EDuelMove::Wingfall;
        const FBossArea& Area = bWingfall ? Wingfall[Stage] : bFlank ? Flank[Stage] : Leap[Stage];
        const FLinearColor Tint = bFlank || bWingfall ? Gold :
            (IsAwakened() ? EclipseLight : FLinearColor(.62f, .75f, .92f, .57f));
        if (bWingfall)
        {
            Arena->Rainfall({CastOrigin}, Area.Reach, Gold * .81f, true, EDuelMark::Wingfall, CastAxis);
        }
        else
        {
            Arena->Detonate(CastOrigin, Area.Reach, Tint, .46f, bFlank ? EDuelMark::Flank : EDuelMark::Leap, CastAxis);
        }
        if (FVector::DistSquared2D(Target->GetActorLocation(), CastOrigin) <= FMath::Square(Area.Reach))
        {
            HitGroundSpell(Area.Damage, CastOrigin);
        }
        break;
    }
    case EDuelMove::Pillar:
        Arena->Pillar(Floor, Pillar[Stage].Reach, false, EDuelMark::Pillar);
        if (!bCombatOver && FVector::DistSquared2D(GetActorLocation(), Target->GetActorLocation()) <= FMath::Square(Pillar[Stage].Reach))
            HitGroundSpell(Pillar[Stage].Damage, Floor, nullptr, false);
        break;
    case EDuelMove::Storm:
    {
        const float Radius = DuelCourt::StormRadius[Stage];
        Arena->BossBurst(Floor, Radius, FLinearColor(1.f, .11f, .35f, .92f), 1.f);
        Arena->StormFront(Floor, Radius, FLinearColor(1.f, .34f, .53f, .86f),
            Timing(EDuelMove::Storm, true).Active, EDuelMark::Storm);
        break;
    }
    case EDuelMove::Cross:
        StrikeLine(CastOrigin, ActionDirection);
        break;
    case EDuelMove::TalonRush:
        Arena->Flash(Floor + FVector(0, 0, 61), CrimsonLight, 19000.f, .30f, 760.f);
        Arena->Sound(EDuelSound::Cut, Floor);
        break;
    case EDuelMove::InwardReap:
        Arena->InwardWave(CastOrigin, DuelCourt::ReapOuter, DuelCourt::ReapInner,
            Timing(EDuelMove::InwardReap, true).Active, CrimsonLight, EDuelMark::Reap);
        break;
    case EDuelMove::Awaken:
    case EDuelMove::Eclipse:
    {
        BossPhase = Move == EDuelMove::Awaken ? 2 : 3;
        const bool bHiro = Character == EDuelCharacter::Hiro;
        GetCharacterMovement()->MaxWalkSpeed = (BossPhase == 2 ? 402.f : 449.f) + (bHiro ? 0.f : 39.f);
        Arena->WitchTransformation(Floor, Character, BossPhase, 1.05f, true);
        Arena->SetBattlePhase(BossPhase, bHiro);
        if (ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>())
        {
            Mode->AnnouncePhase(BossPhase);
        }
        if (Target)
        {
            Target->CameraImpulse = FMath::Max(Target->CameraImpulse, 1.22f);
        }
        break;
    }
    default:
        break;
    }
}

void ADuelFighter::TickTalonRush(float LastAge, const FMoveTiming& Spec, const FVector& BeforeTravel)
{
    if (MoveAge < Spec.Windup || LastAge >= Spec.Windup + Spec.Active)
    {
        return;
    }
    const FVector At = GetActorLocation();
    if (LastAge < Spec.Windup || FMath::FloorToInt((LastAge - Spec.Windup) / .074f) !=
        FMath::FloorToInt((MoveAge - Spec.Windup) / .074f))
    {
        Arena->ClawWake(Arena->GroundMark(At) + ActionDirection * 58.f + FVector(0, 0, 33),
            ActionDirection, Rush[BossPhase - 1].Reach, BossPhase);
    }
    const FVector Offset = Target->GetActorLocation() - BeforeTravel;
    const float Along = FVector::DotProduct(Offset, ActionDirection);
    const float Swept = FMath::Max(0.0, FVector::DotProduct(At - BeforeTravel, ActionDirection));
    const FVector Across = FVector::CrossProduct(FVector::UpVector, ActionDirection);
    const FBossArea& Area = Rush[BossPhase - 1];
    const float Feet = Target->GetActorLocation().Z - Arena->CourtOrigin().Z - Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    if (!bHitDelivered && Along >= -RushTail && Along <= Swept + RushLead &&
        FMath::Abs(FVector::DotProduct(Offset, Across)) <= Area.Reach && Feet <= 125.f)
    {
        bHitDelivered = true;
        Target->ReceiveDuelDamage(Area.Damage, At, true);
    }
}

void ADuelFighter::TickSeals(float LastAge)
{
    const FBossArea& Area = Seal[BossPhase - 1];
    const float Windup = Timing(EDuelMove::Seals, IsAwakened()).Windup;
    for (int32 I = 0; I < SealCenters.Num() && !bCombatOver; ++I)
    {
        const float At = Windup + I * SealSpacing;
        if (LastAge < At && MoveAge >= At)
        {
            const FVector& Center = SealCenters[I];
            Arena->Detonate(Center, Area.Reach, I == 0 ? EclipseLight : Sakura, .46f, EDuelMark::Scroll, CastAxis);
            if (FVector::DistSquared2D(Target->GetActorLocation(), Center) <= FMath::Square(Area.Reach))
            {
                HitGroundSpell(Area.Damage, Center);
            }
        }
    }
}

void ADuelFighter::StrikeLine(const FVector& Origin, const FVector& Axis)
{
    StrikeLine(Origin, Axis, LaneHalfLength, LaneHalfWidth, 63.f, EclipseLight);
}

bool ADuelFighter::HitGroundSpell(float Damage, const FVector& Impact, const FVector* Emitter, bool bPrecise)
{
    if (!IsValid(Target)) return false;
    FVector Source = Emitter ? *Emitter : GetActorLocation();
    if (FVector::DistSquared2D(Source, Target->GetActorLocation()) < 1.)
        Source = Target->GetActorLocation() + Target->GetActorForwardVector() * 180.f;
    // A centred ground mark has no facing vector; guard faces its caster, while feedback stays at impact.
    // Treat a precise spell guard as ranged so it cannot remotely cancel the caster's whole attack.
    return Target->ReceiveDuelDamage(Damage, Source, bPrecise, &Impact, true, true, true);
}

bool ADuelFighter::StrikeLine(const FVector& Origin, const FVector& Axis, float HalfLength, float HalfWidth, float Damage,
    const FLinearColor& Tint, bool bQuiet)
{
    const EDuelMark Mark = Move == EDuelMove::Cross ? EDuelMark::CrossVerdict :
        Move == EDuelMove::CrossAmbush ? EDuelMark::CrossAmbush :
        Move == EDuelMove::Crimson && MoveArt == EDuelArt::PenScript ? EDuelMark::PenNib : EDuelMark::Default;
    Arena->Lane(Origin, Axis, HalfLength, HalfWidth, Tint, .38f, true, bQuiet, false, Mark);
    const FVector Offset = Target->GetActorLocation() - Origin;
    const FVector Perpendicular = FVector::CrossProduct(FVector::UpVector, Axis);
    if (!bCombatOver && FMath::Abs(FVector::DotProduct(Offset, Axis)) <= HalfLength &&
        FMath::Abs(FVector::DotProduct(Offset, Perpendicular)) <= HalfWidth)
    {
        const FVector Emitter = Origin - Axis * (HalfLength + 10.f);
        return HitGroundSpell(Damage, Origin, &Emitter);
    }
    return false;
}

void ADuelFighter::TickStorm(float ActiveProgress)
{
    const float Radius = DuelCourt::StormRadius[BossPhase - 1];
    const float Wave = FMath::Lerp(70.f, Radius, ActiveProgress);
    const float Distance = static_cast<float>(FVector::Dist2D(GetActorLocation(), Target->GetActorLocation()));
    if (!bHitDelivered && Distance <= FMath::Min(Radius, Wave + 32.f) && Distance >= PreviousWave - 38.f)
    {
        bHitDelivered = true;
        const double Height = Target->GetActorLocation().Z - Arena->CourtOrigin().Z - Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        if (Height < 40.0)
        {
            HitGroundSpell(StormDamage[BossPhase - 1], GetActorLocation());
        }
    }
    PreviousWave = Wave;
}

bool ADuelFighter::InRollWindow() const
{
    if (Move == EDuelMove::Crimson && MoveArt == EDuelArt::HoundChase)
    {
        return MoveAge >= .20f && MoveAge < .31f;
    }
    const FMoveTiming Spec = Timing(Move, false, MoveWeapon);
    return (Move == EDuelMove::Roll || Move == EDuelMove::Backstep) &&
        MoveAge >= Spec.Windup && MoveAge < Spec.Windup + Spec.Active;
}

bool ADuelFighter::HasAegis() const
{
    return AegisUntil > GetWorld()->GetTimeSeconds();
}

float ADuelFighter::RitualOfferingSeconds() const
{
    return static_cast<float>(FMath::Max(0.0, RitualOfferingUntil - GetWorld()->GetTimeSeconds()));
}

void ADuelFighter::DamagePosture(float Amount)
{
    if (JudgmentImmune() || (bBoss && Target && Target->JudgmentWeaponLockSeconds() > 0.f)) return;
    if (!bBoss || Amount <= 0.f || !IsAlive() || bCombatOver || Move == EDuelMove::Stagger ||
        Move == EDuelMove::Awaken || Move == EDuelMove::Eclipse)
    {
        return;
    }
    Posture = FMath::Min(DuelCourt::MaxPosture, Posture + Amount);
    PostureDelay = 2.3f;
    if (Posture >= DuelCourt::MaxPosture)
    {
        Move = EDuelMove::Idle;
        StartMove(EDuelMove::Stagger);
    }
}

bool ADuelFighter::ReceiveDuelDamage(float Damage, const FVector& Source, bool bParryable, const FVector* Contact,
    bool bProjectile, bool bRecoil, bool bBlockable)
{
    if (JudgmentImmune() || (bBoss && Target && Target->JudgmentWeaponLockSeconds() > 0.f)) return false;
    if (Damage <= 0.f || !IsValid(Arena) || !IsAlive() || bCombatOver ||
        Move == EDuelMove::Awaken || Move == EDuelMove::Eclipse || InRollWindow())
    {
        return false;
    }

    if (!bBoss && bParryable && TryWardCounter(Source))
    {
        return false;
    }
    const FVector Toward = (Source - GetActorLocation()).GetSafeNormal2D();
    if (bBoss && Move == EDuelMove::MirrorRiposte &&
        TryMirrorRiposte(Toward, Contact ? *Contact : GetActorLocation() + Toward * 85.f))
    {
        return false;
    }
    if (bBoss && Move == EDuelMove::FeintCounter && !bProjectile && Contact && !bFeintTriggered &&
        MoveAge >= .15f && MoveAge <= .51f && FVector::DotProduct(ActionDirection, Toward) > .48f)
    {
        bFeintTriggered = true;
        MoveAge = .62f;
        Arena->Sound(EDuelSound::Block, *Contact);
        Arena->Burst(*Contact, CrimsonLight, 29, 474.f);
        return false;
    }
    if (!bBoss && bParryable && TryShieldParry(Toward, bProjectile))
    {
        return false;
    }
    const EDuelWeapon Shield = bBoss ? EDuelWeapon::Empty : GuardingShield();
    const auto Guard = DuelCourt::ShieldResponse(Shield);
    const float GuardCost = (Guard.CostBase + Damage * Guard.CostPerDamage) * DuelCourt::StaminaCostScale;
    // Sustained blocking and the brief parry window are separate defenses.
    const bool bGuardHit = Shield != EDuelWeapon::Empty && (bParryable || bBlockable) &&
        FVector::DotProduct(GetActorForwardVector(), Toward) > Guard.BlockDot;
    const bool bGuardBreak = bGuardHit && Stamina < GuardCost;
    Damage *= bBoss ? 1.f : DuelCourt::BossDamageScale;
    if (bGuardBreak)
    {
        Stamina = 0.f;
        RecoveryDelay = 1.f;
        Arena->ShieldContact(GetActorLocation() + Toward * 44.f + FVector(0, 0, 26), Toward, false);
    }
    else if (bGuardHit)
    {
        Stamina -= GuardCost;
        RecoveryDelay = .64f;
        Damage *= Guard.Chip;
        if (Health > Damage)
        {
            Health -= Damage;
            HitRecoil = FMath::Max(HitRecoil, .52f);
            HitDirection = -Toward;
            Arena->ShieldContact(GetActorLocation() + Toward * 57.f + FVector(0, 0, 27), Toward, false);
            return true;
        }
    }
    bool bReturnMantle = false;
    if (!bBoss && HasAegis())
    {
        AegisUntil = 0.0;
        Damage *= bMantleCounter ? .53f : .40f;
        const FVector At = GetActorLocation() + FVector(0, 0, 30);
        Arena->Burst(At, Gold, 36, 335.f);
        Arena->Sound(EDuelSound::Block, At);
        bReturnMantle = bMantleCounter;
        bMantleCounter = false;
    }
    const float Floor = bBoss ? DuelCourt::BossHealthFloor(BossPhase) : 0.f;
    if (Health <= Floor)
    {
        return false;
    }
    const float Previous = Health;
    Health = FMath::Max(Floor, Health - Damage * (Move == EDuelMove::Stagger ? 1.30f : 1.f));
    if (bBoss)
    {
        const double Now = GetWorld()->GetTimeSeconds();
        DamageBurst = (Now < DamageBurstUntil ? DamageBurst : 0.f) + (Previous - Health);
        DamageBurstUntil = Now + 2.2;
    }
    const bool bStaggerHit = bRecoil || bGuardBreak;
    HitRecoil = FMath::Max(HitRecoil, bStaggerHit ? 1.f : .34f);
    HitDirection = (GetActorLocation() - Source).GetSafeNormal2D();
    const bool bHeavy = bGuardBreak || (Contact && bBoss ? Damage >= 103.f : Damage >= 55.f);
    ADuelFighter* Player = bBoss ? Target.Get() : this;
    if (IsValid(Player))
    {
        const float Impulse = bBoss ? (bHeavy ? .62f : .38f) : bStaggerHit ? .88f : .30f;
        Player->CameraImpulse = FMath::Max(Player->CameraImpulse, Impulse);
    }
    const FVector HitPoint = Contact ? *Contact : GetActorLocation() + FVector(0, 0, 34);
    const FLinearColor& Tint = bBoss ? CrimsonLight : Gold;
    Arena->Burst(HitPoint, Tint, bHeavy ? 31 : 15, bHeavy ? 455.f : 325.f);
    Arena->Flash(HitPoint, Tint, bHeavy ? 4900.f : 2350.f, .16f, bHeavy ? 450.f : 315.f);
    Arena->Sound(bHeavy ? EDuelSound::HeavyImpact : EDuelSound::Impact, HitPoint);
    if (bBoss && IsAlive() && Contact)
    {
        Hitstop = FMath::Max(Hitstop, bHeavy ? .047f : .024f);
    }
    if (IsAlive() && bHeavy)
    {
        Speak(EDuelVoice::Hurt);
    }

    if (!IsAlive())
    {
        SetActorEnableCollision(false);
        if (ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>())
        {
            Mode->FighterFell(this);
        }
    }
    else if (bBoss)
    {
        TryChangePhase();
    }
    else if (bStaggerHit)
    {
        Move = EDuelMove::Idle;
        Hitstop = 0.f;
        StartMove(bGuardBreak ? EDuelMove::GuardBreak : EDuelMove::Hurt);
        AddActorWorldOffset(HitDirection * (bHeavy ? 44.f : 26.f), true);
    }
    if (bReturnMantle && IsAlive() && IsValid(Target) && Target->IsAlive())
    {
        const FVector At = GetActorLocation();
        const auto& Art = DuelCourt::WeaponArt(EDuelArt::WhiteMantle);
        Arena->Detonate(Arena->GroundMark(At), Art.Strike.Reach, Art.Glow, .38f, EDuelMark::Mantle);
        if (FVector::DistSquared2D(At, Target->GetActorLocation()) <= FMath::Square(Art.Strike.Reach) &&
            Target->ReceiveDuelDamage(Art.Strike.Damage, At, false, nullptr, false, false))
        {
            Target->DamagePosture(Art.Strike.Posture);
        }
    }
    return true;
}

int32 ADuelFighter::RecentDamage() const
{
    return GetWorld()->GetTimeSeconds() < DamageBurstUntil ? FMath::RoundToInt(DamageBurst) : 0;
}

void ADuelFighter::EndCombat()
{
    ClearJudgmentPenalty();
    if (bBoss && Arena) Arena->ClearMirrorCourt();
    RitualOfferingUntil = 0.0;
    bCombatOver = true;
    ClearAttackPreviews();
    ClearChargeSeal();
    AegisUntil = 0.0;
    Hitstop = 0.f;
    ClearHandPresses();
    Move = QueuedMove = EDuelMove::Idle;
    MoveAge = 0.f;
    GetCharacterMovement()->StopMovementImmediately();
    if (!IsAlive())
    {
        GetCharacterMovement()->DisableMovement();
    }
}

void ADuelFighter::PrepareExploration()
{
    bLockOn = false;
    ViewYaw = 0.f;
    ViewPitch = -15.f;
    GardenCameraBlend = 0.f;
    bGardenCameraManual = false;
    SetActorRotation(FRotator::ZeroRotator);
    GetCharacterMovement()->StopMovementImmediately();
}

FVector ADuelFighter::SweepNorthCourtTravel(const FVector& From, const FVector& To) const
{
    const auto* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    if (!Mode || !Mode->UsesNorthTower() || Mode->Outcome() != EDuelOutcome::Fighting || From.Equals(To, .01))
        return To;
    const UCapsuleComponent* Body = GetCapsuleComponent();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(NorthScriptedCombatTravel), false, this);
    Query.AddIgnoredActor(Target.Get());
    FHitResult Hit;
    // The airborne/phase move can pass its opponent but follows the tower's
    // physical walls, piers and closed gates with the real combat capsule.
    if (GetWorld()->SweepSingleByChannel(Hit, From, To, FQuat::Identity, Body->GetCollisionObjectType(),
        FCollisionShape::MakeCapsule(Body->GetScaledCapsuleRadius(), Body->GetScaledCapsuleHalfHeight()), Query))
        return Hit.bStartPenetrating ? From : Hit.Location;
    return To;
}

void ADuelFighter::ClampToCourt()
{
    if (bBoss && bCombatOver)
    {
        return;
    }
    const FVector Origin = Arena ? Arena->CourtOrigin() : FVector::ZeroVector;
    const FVector At = GetActorLocation() - Origin;
    const ADuelMode* WorldMode = GetWorld()->GetAuthGameMode<ADuelMode>();
    if (WorldMode && WorldMode->UsesNorthTower())
    {
        if (Arena && WorldMode->Outcome() == EDuelOutcome::Fighting)
        {
            const FBox2D Bounds = Arena->MirrorCourtBounds(Arena->CourtBounds(GetCapsuleComponent()->GetScaledCapsuleRadius()),
                GetCapsuleComponent()->GetScaledCapsuleRadius());
            const FVector Bound(Bounds.GetClosestPointTo(FVector2D(At)), At.Z);
            if (!At.Equals(Bound)) SetActorLocation(Origin + Bound, false);
        }
        return;
    }
    const ADuelMode* Mode = !bBoss ? WorldMode : nullptr;
    if (Mode && Mode->UsesCastle() && Mode->Outcome() != EDuelOutcome::Fighting)
    {
        return;
    }
    float MinX = bBoss ? -DuelCourt::HalfLength : DuelCourt::GateCrossX;
    float MaxX = DuelCourt::HalfLength;
    float HalfWidth = DuelCourt::HalfWidth;
    if (Mode && Mode->Outcome() == EDuelOutcome::Title)
    {
        return;
    }
    if (Mode && Mode->InYukiCourt())
    {
        if (Mode->Outcome() == EDuelOutcome::Ready)
        {
            MinX = DuelCourt::YukiArchiveExitX - (DuelCourt::YukiOrigin.X - DuelCourt::AppealOrigin.X);
            MaxX = Mode->GateOpen() ? DuelCourt::GateCrossX + 42.f : DuelCourt::GateX - 62.f;
            HalfWidth = 272.f;
        }
        else if (Mode->Outcome() == EDuelOutcome::Victory)
        {
            MaxX = FMath::Abs(At.Y) <= 272.f ? DuelCourt::YukiGardenX + 70.f : DuelCourt::YukiExitX - 120.f;
            if (At.X > DuelCourt::YukiExitX - 120.f)
            {
                HalfWidth = 272.f;
            }
        }
        else if (Mode->Outcome() == EDuelOutcome::Epilogue)
        {
            MinX = DuelCourt::YukiGardenX - 110.f;
            MaxX = 4380.f;
            HalfWidth = 890.f;
        }
        const FVector Bound(FMath::Clamp(At.X, MinX, MaxX), FMath::Clamp(At.Y, -HalfWidth, HalfWidth), At.Z);
        if (!At.Equals(Bound))
        {
            SetActorLocation(Bound + Origin, false);
        }
        return;
    }
    if (Mode && Mode->Outcome() == EDuelOutcome::Passage)
    {
        const FVector WorldAt = GetActorLocation();
        const double Blend = FMath::Clamp((WorldAt.X - 4290.0) / 380.0, 0.0, 1.0);
        const double Center = FMath::Lerp(-595.0, -1140.0, Blend);
        const double X = FMath::Clamp(WorldAt.X, 4180.0, 7360.0);
        const bool bTurn = X >= 6870.0;
        const double Y = FMath::Clamp(WorldAt.Y, Center - 240.0, bTurn ? 228.0 : Center + 240.0);
        if (X != WorldAt.X || Y != WorldAt.Y)
        {
            SetActorLocation(FVector(X, Y, WorldAt.Z));
        }
        return;
    }
    if (Mode && Mode->Outcome() == EDuelOutcome::Ready && Mode->InAppeal() && Mode->CharacterView() == 0)
    {
        MinX = -3760.f;
        MaxX = Mode->GateOpen() ? DuelCourt::GateCrossX + 42.f : DuelCourt::GateX - 62.f;
        HalfWidth = 488.f;
    }
    else if (Mode && Mode->Outcome() == EDuelOutcome::Ready && Mode->CharacterView() == 0)
    {
        MinX = DuelCourt::ApproachMinX;
        MaxX = Mode->GateOpen() ? DuelCourt::GateCrossX + 42.f : DuelCourt::GateX - 62.f;
        if (At.X < DuelCourt::StairBaseX + 10.f)
        {
            HalfWidth = FMath::GetMappedRangeValueClamped(
                FVector2D(DuelCourt::StairBaseX - 640.f, DuelCourt::StairBaseX + 10.f), FVector2D(1000, 445), At.X);
        }
        else if (At.X < DuelCourt::StairTopX)
        {
            HalfWidth = 445.f;
        }
        else if (At.X < DuelCourt::PrisonGateX + 180.f)
        {
            HalfWidth = FMath::GetMappedRangeValueClamped(
                FVector2D(DuelCourt::PrisonGateX - 430.f, DuelCourt::PrisonGateX - 118.f), FVector2D(1020, 222), At.X);
        }
        else
        {
            const float Open = FMath::GetMappedRangeValueClamped(
                FVector2D(DuelCourt::PrisonGateX + 240, DuelCourt::PrisonGateX + 720),
                FVector2D(1020, DuelCourt::OuterGalleryHalfWidth), At.X);
            HalfWidth = FMath::GetMappedRangeValueClamped(FVector2D(-3200, -2160), FVector2D(Open, 365), At.X);
        }
    }
    else if (Mode && Mode->Outcome() == EDuelOutcome::Victory && Mode->CanExplore())
    {
        const float ApproachEnd = DuelCourt::ArchiveGateX - 170.f;
        MaxX = FMath::Abs(At.Y) <= 230.f ? DuelCourt::ArchiveEntryX + 43.f : ApproachEnd;
        if (At.X > ApproachEnd)
        {
            HalfWidth = 230.f;
        }
    }
    else if (Mode && Mode->Outcome() == EDuelOutcome::Archive)
    {
        MinX = 2590.f;
        MaxX = Mode->InAppeal() ? (Mode->RitualComplete() ? 3710.f : DuelCourt::VerdictEndX) : DuelCourt::ArchiveEndX;
        HalfWidth = 722.f;
    }
    const FVector Bound(FMath::Clamp(At.X, MinX, MaxX), FMath::Clamp(At.Y, -HalfWidth, HalfWidth), At.Z);
    if (!At.Equals(Bound))
    {
        SetActorLocation(Bound + Origin, false);
    }
}
