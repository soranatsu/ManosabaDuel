#include "DuelGame.h"
#include "DuelArena.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

float ADuelFighter::JudgmentMoveLockSeconds() const
{
    return bBoss || !GetWorld() ? 0.f : float(FMath::Max(0., JudgmentMoveUntil - GetWorld()->GetTimeSeconds()));
}

float ADuelFighter::JudgmentWeaponLockSeconds() const
{
    return bBoss || !GetWorld() ? 0.f : float(FMath::Max(0., JudgmentWeaponUntil - GetWorld()->GetTimeSeconds()));
}

bool ADuelFighter::JudgmentImmune() const
{
    return bBoss && Arena && Arena->JudgmentProtects(*this);
}

bool ADuelFighter::JudgmentBlocksAction(EDuelMove Next) const
{
    if (bBoss) return false;
    if (JudgmentMoveLockSeconds() > 0.f && (Next == EDuelMove::Roll || Next == EDuelMove::Backstep)) return true;
    if (JudgmentWeaponLockSeconds() <= 0.f) return false;
    // All weapon and spell routes, including a future unarmed cast, use the same action gate.
    return Next != EDuelMove::Idle && Next != EDuelMove::Flask && Next != EDuelMove::Roll &&
        Next != EDuelMove::Backstep && Next != EDuelMove::Hurt && Next != EDuelMove::GuardBreak && Next != EDuelMove::Stagger;
}

void ADuelFighter::CancelJudgmentActions()
{
    ClearHandPresses();
    GuardPose = {0.f, 0.f};
    if (JudgmentBlocksAction(QueuedMove)) QueuedMove = EDuelMove::Idle;
    bArtHeld = bChargedArt = false;
    ArtHeldTime = 0.f;
    HoundFollowupUntil = AegisUntil = RitualOfferingUntil = 0.;
    bMantleCounter = false;
    ClearChargeSeal();
    if (JudgmentBlocksAction(Move))
    {
        ClearAttackPreviews();
        Move = EDuelMove::Idle;
        MoveAge = Travel = 0.f;
        bImpactPlayed = bHitDelivered = true;
        bTrackingBlade = false;
    }
}

void ADuelFighter::ApplyJudgmentPenalty()
{
    if (bBoss || !GetWorld() || !IsAlive()) return;
    const double Now = GetWorld()->GetTimeSeconds();
    JudgmentMoveUntil = FMath::Max(JudgmentMoveUntil, Now + 1.);
    JudgmentWeaponUntil = FMath::Max(JudgmentWeaponUntil, Now + 5.);
    CancelJudgmentActions();
    ShiftPressedAt = -1.;
    bShiftCommittedToSprint = bSprintExhausted = false;
    StopJumping();
    GetCharacterMovement()->StopMovementImmediately();
    ConsumeMovementInputVector();
}

void ADuelFighter::ClearJudgmentPenalty()
{
    JudgmentMoveUntil = JudgmentWeaponUntil = 0.;
    if (!bBoss && GetCharacterMovement() && GetCharacterMovement()->MaxWalkSpeed == 0.f)
        GetCharacterMovement()->MaxWalkSpeed = GetDefault<ADuelFighter>()->GetCharacterMovement()->MaxWalkSpeed;
}
