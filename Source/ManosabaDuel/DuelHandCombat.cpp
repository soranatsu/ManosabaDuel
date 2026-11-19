#include "DuelGame.h"

#include "DuelArena.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "ProfilingDebugging/CsvProfiler.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

CSV_DECLARE_CATEGORY_EXTERN(DuelCombat);

namespace
{
    constexpr double HeavyHold = .29;
    constexpr double ShieldRetry = .32;

    struct FBladeHit
    {
        double EdgeFraction = 0., Distance = 0., AimDot = 0.;
        float ExistingPosture = 0.f;
        bool bRecovering = false, bMoving = false, bCharged = false;
    };
    struct FBladeBonus
    {
        float Damage = 1.f, Posture = 1.f, ExtraPosture = 0.f;
        bool bPoint = false;
    };
    FBladeBonus BladeBonus(EDuelWeapon Weapon, EDuelMove Move, EDuelArt Art, const FBladeHit& Hit)
    {
        const bool bPoint = Hit.EdgeFraction >= .68 && Hit.AimDot >= .94;
        switch (Weapon)
        {
        case EDuelWeapon::Rapier:
            if (Move == EDuelMove::Heavy && bPoint && Hit.Distance >= 154.) return {1.22f, 1.05f, 0.f, true};
            break;
        case EDuelWeapon::Spear:
            if (Move != EDuelMove::Crimson && bPoint && Hit.Distance >= 202.) return {1.25f, 1.18f, 0.f, true};
            if (Move != EDuelMove::Crimson && Hit.Distance < 114.) return {.85f, .85f};
            break;
        case EDuelWeapon::Greatblade:
            if (Move == EDuelMove::Heavy && Hit.bRecovering) return {1.18f, 1.10f};
            break;
        case EDuelWeapon::Hammer:
            if (Move == EDuelMove::Heavy && Hit.ExistingPosture >= 15.f) return {1.f, 1.20f};
            break;
        case EDuelWeapon::Axe:
            if (Art == EDuelArt::Whirl && Hit.bCharged) return {1.18f, 1.35f};
            break;
        case EDuelWeapon::FirePoker:
            if (Art == EDuelArt::PokerRake && Hit.bMoving) return {1.f, 1.f, 25.f};
            break;
        default:
            break;
        }
        return {};
    }
}

EDuelHand ADuelFighter::ArtHand() const
{
    return DuelCourt::IsBlade(Equipped) || !DuelCourt::IsBlade(Offhand) ? EDuelHand::Right : EDuelHand::Left;
}

EDuelArt ADuelFighter::ActiveArt() const
{
    const EDuelWeapon Gear = EquippedWeapon(ArtHand());
    if (Gear == EDuelWeapon::Staff)
    {
        return SelectedSpell();
    }
    if (Gear == EDuelWeapon::Houndfang && HoundFollowupUntil > GetWorld()->GetTimeSeconds())
    {
        return EDuelArt::HoundChase;
    }
    return DuelCourt::ResolveArt(Gear, ArtSelection);
}

void ADuelFighter::CycleSpell()
{
    if (CanUseEquipment() && !UGameplayStatics::IsGamePaused(this))
    {
        ChooseActivePreparedSpell(1);
    }
}

void ADuelFighter::ChooseActivePreparedSpell(int32 Offset)
{
    for (int32 Step = 0; Step < DuelCourt::SpellSlotCount; ++Step)
    {
        const int32 Slot = (ActiveSpellSlot + Offset + Step) % DuelCourt::SpellSlotCount;
        if (SpellAt(Slot) != EDuelArt::Default)
        {
            ActiveSpellSlot = Slot;
            return;
        }
    }
}

void ADuelFighter::AdjustSpellSlot(int32 Slot, int32 Step)
{
    if (Slot < 0 || Slot >= DuelCourt::SpellSlotCount || Step == 0)
    {
        return;
    }
    const ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    const auto Spells = MakeArrayView(DuelCourt::StaffSpells);
    const int32 Count = Spells.Num() + 1;
    int32 Index = Spells.IndexOfByKey(SpellAt(Slot)) + 1;
    for (int32 I = 0; I < Count; ++I)
    {
        Index = (Index + (Step > 0 ? 1 : -1) + Count) % Count;
        const EDuelArt Next = Index == 0 ? EDuelArt::Default : Spells[Index - 1];
        if (Index == 0 ? Mode && Mode->UsesExpeditionLoot() : !Mode || Mode->KnowsSpell(Next))
        {
            SetSpellSlot(Slot, Next);
            return;
        }
    }
}

void ADuelFighter::SetSpellSlot(int32 Slot, EDuelArt Spell)
{
    const ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    const bool bAllowed = Spell == EDuelArt::Default ? Mode && Mode->UsesExpeditionLoot() :
        MakeArrayView(DuelCourt::StaffSpells).Contains(Spell) && (!Mode || Mode->KnowsSpell(Spell));
    if (Slot < 0 || Slot >= DuelCourt::SpellSlotCount || !bAllowed)
    {
        return;
    }
    for (int32 Other = 0; Other < DuelCourt::SpellSlotCount && Spell != EDuelArt::Default; ++Other)
    {
        if (SpellAt(Other) == Spell)
        {
            Swap(PreparedSpells[Slot], PreparedSpells[Other]);
            ChooseActivePreparedSpell();
            return;
        }
    }
    PreparedSpells[Slot] = Spell;
    ChooseActivePreparedSpell();
}

void ADuelFighter::ResetNorthLoadout(bool bExpeditionLoot)
{
    SetWeapon(bExpeditionLoot ? DuelCourt::PersonalWeapon(Character) : EDuelWeapon::Rapier);
    SetWeapon(EDuelWeapon::Buckler, EDuelHand::Left);
    RightReserve = bExpeditionLoot ? EDuelWeapon::Staff : EDuelWeapon::Greatblade;
    LeftReserve = bExpeditionLoot ? EDuelWeapon::Empty : EDuelWeapon::Spear;
    ArtSelection = EDuelArt::Default;
    PreparedSpells = {EDuelArt::IceLance, EDuelArt::WitchRain,
        bExpeditionLoot ? EDuelArt::Default : EDuelArt::SableBlade};
    ActiveSpellSlot = 0;
    HealthFlaskAllocation = DuelCourt::DefaultHealthFlasks;
    Item = EDuelItem::Flask;
}

bool ADuelFighter::IsGuarding(EDuelHand Hand) const
{
    const FHandPress& Press = HandPresses[static_cast<uint8>(Hand)];
    return JudgmentWeaponLockSeconds() <= 0.f && Move == EDuelMove::Idle && CanUseEquipment() && Press.Since >= 0 &&
        DuelCourt::IsShield(Press.Item) && EquippedWeapon(Hand) == Press.Item;
}

bool ADuelFighter::IsGuarding() const
{
    return GuardingShield() != EDuelWeapon::Empty;
}

EDuelWeapon ADuelFighter::GuardingShield() const
{
    const bool bRight = IsGuarding(EDuelHand::Right);
    const bool bLeft = IsGuarding(EDuelHand::Left);
    if ((bRight && Equipped == EDuelWeapon::Greatshield) || (bLeft && Offhand == EDuelWeapon::Greatshield))
        return EDuelWeapon::Greatshield;
    return bRight || bLeft ? EDuelWeapon::Buckler : EDuelWeapon::Empty;
}

bool ADuelFighter::CanUseEquipment() const
{
    if (bBoss || bCharacterPreview || !IsAlive())
    {
        return false;
    }
    const ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    return Mode && (Mode->Outcome() == EDuelOutcome::Fighting || Mode->CanExplore());
}

bool ADuelFighter::HasFocus(EDuelArt Art) const
{
    return Focus >= DuelCourt::FocusCost(Art);
}

void ADuelFighter::PressHand(EDuelHand Hand)
{
    if (!CanUseEquipment() || UGameplayStatics::IsGamePaused(this) || JudgmentWeaponLockSeconds() > 0.f)
    {
        return;
    }
    FHandPress& Press = HandPresses[static_cast<uint8>(Hand)];
    if (Press.Since < 0)
    {
        const double Now = GetWorld()->GetTimeSeconds();
        const EDuelWeapon Gear = EquippedWeapon(Hand);
        const bool bRaiseShield = DuelCourt::IsShield(Gear) && Move == EDuelMove::Idle && !IsGuarding();
        const bool bTimedGuard = bRaiseShield && !bCombatOver && Now >= NextParryPressAt;
        Press = {Now, Gear, false, bTimedGuard};
        if (bTimedGuard)
        {
            NextParryPressAt = Now + ShieldRetry;
        }
    }
}

bool ADuelFighter::TryShieldParry(const FVector& Toward, bool bProjectile)
{
    if (FVector::DotProduct(GetActorForwardVector(), Toward) < .32f || Stamina < DuelCourt::ParryStaminaCost)
    {
        return false;
    }
    const double Now = GetWorld()->GetTimeSeconds();
    for (EDuelHand Hand : {EDuelHand::Left, EDuelHand::Right})
    {
        FHandPress& Press = HandPresses[static_cast<uint8>(Hand)];
        const auto Guard = DuelCourt::ShieldResponse(Press.Item);
        if (!IsGuarding(Hand) || !Press.bParryReady || Now - Press.Since > Guard.ParryWindow)
        {
            continue;
        }
        Press.bParryReady = false;
        Stamina -= DuelCourt::ParryStaminaCost;
        RecoveryDelay = .43f;
        const FVector Contact = GetActorLocation() + Toward * 64.f + FVector(0, 0, 27);
        Arena->ShieldContact(Contact, Toward, true);
        if (StartMove(EDuelMove::Parry, Hand))
        {
            Hitstop = .036f;
        }
        if (!bProjectile && IsValid(Target) && !Target->JudgmentImmune())
        {
            Target->Move = EDuelMove::Idle;
            if (!Target->TryChangePhase())
            {
                Target->DamagePosture(Guard.ParryPosture);
                if (!Target->IsStaggered())
                {
                    Target->StartMove(EDuelMove::GuardReel);
                }
            }
        }
        return true;
    }
    return false;
}

void ADuelFighter::ReleaseHand(EDuelHand Hand)
{
    FHandPress& Press = HandPresses[static_cast<uint8>(Hand)];
    const FHandPress Previous = Press;
    Press = {};
    if (Previous.Since < 0 || Previous.bHeavy || !DuelCourt::IsBlade(Previous.Item) ||
        Previous.Item != EquippedWeapon(Hand) || UGameplayStatics::IsGamePaused(this))
    {
        return;
    }
    RequestMove(GetWorld()->GetTimeSeconds() - Previous.Since >= HeavyHold ? EDuelMove::Heavy : EDuelMove::Cut, Hand);
}

void ADuelFighter::TickHands()
{
    for (EDuelHand Hand : {EDuelHand::Right, EDuelHand::Left})
    {
        FHandPress& Press = HandPresses[static_cast<uint8>(Hand)];
        if (Press.Since >= 0 && !Press.bHeavy && Press.Item == EquippedWeapon(Hand) &&
            DuelCourt::IsBlade(Press.Item) && GetWorld()->GetTimeSeconds() - Press.Since >= HeavyHold)
        {
            Press.bHeavy = true;
            RequestMove(EDuelMove::Heavy, Hand);
        }
    }
}

void ADuelFighter::ClearHandPresses()
{
    bArtHeld = false;
    for (FHandPress& Press : HandPresses)
    {
        Press = {};
    }
}

bool ADuelFighter::CanEquipOwnedWeapon(EDuelWeapon Gear, EDuelHand Hand) const
{
    const ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    return DuelCourt::CanEquip(Gear, Character, Hand) && (bBoss || !Mode || Mode->OwnsWeapon(Gear));
}

void ADuelFighter::RequestWeapon(EDuelWeapon Next, EDuelHand Hand)
{
    const ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    if (Next == EquippedWeapon(Hand) || !CanEquipOwnedWeapon(Next, Hand) || Move != EDuelMove::Idle ||
        !Mode || !CanUseEquipment() || UGameplayStatics::IsGamePaused(this))
    {
        return;
    }
    if (Mode->Outcome() == EDuelOutcome::Ready && !bCharacterPreview)
    {
        EDuelWeapon& Reserve = Hand == EDuelHand::Right ? RightReserve : LeftReserve;
        Reserve = EquippedWeapon(Hand);
        SetWeapon(Next, Hand);
    }
    else if (StartMove(EDuelMove::Equip, Hand))
    {
        PendingWeapon = Next;
        PendingHand = Hand;
    }
}

void ADuelFighter::AdjustWeaponSlot(EDuelHand Hand, bool bReserve, int32 Step)
{
    if (Step == 0) return;
    const ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    const bool bAllowEmptyPair = Mode && Mode->UsesExpeditionLoot();
    EDuelWeapon& Reserve = Hand == EDuelHand::Right ? RightReserve : LeftReserve;
    const EDuelWeapon Other = bReserve ? EquippedWeapon(Hand) : Reserve;
    int32 Index = static_cast<int32>(bReserve ? Reserve : EquippedWeapon(Hand));
    constexpr int32 Count = static_cast<int32>(EDuelWeapon::RitualSword) + 1;
    for (int32 I = 0; I < Count; ++I)
    {
        Index = (Index + (Step > 0 ? 1 : -1) + Count) % Count;
        const EDuelWeapon Next = static_cast<EDuelWeapon>(Index);
        if ((Next != Other || (bAllowEmptyPair && Next == EDuelWeapon::Empty)) && CanEquipOwnedWeapon(Next, Hand))
        {
            if (bReserve) Reserve = Next;
            else SetWeapon(Next, Hand);
            return;
        }
    }
}

void ADuelFighter::SetWeapon(EDuelWeapon Next, EDuelHand Hand)
{
    EDuelWeapon& Gear = Hand == EDuelHand::Right ? Equipped : Offhand;
    Gear = Next;
    HandPresses[static_cast<uint8>(Hand)] = {};
    GuardPose[static_cast<uint8>(Hand)] = 0.f;
    const TArray<TObjectPtr<USceneComponent>>& Models = Hand == EDuelHand::Right ? WeaponModels : LeftWeaponModels;
    for (int32 I = 0; I < Models.Num(); ++I)
    {
        if (USceneComponent* Model = Models[I])
        {
            Model->SetVisibility(I == static_cast<int32>(Next), true);
        }
    }
}

void ADuelFighter::CycleRightHand()
{
    if (UGameplayStatics::IsGamePaused(this))
    {
        if (ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>())
        {
            Mode->AdjustLoadout(1);
        }
        return;
    }
    RequestWeapon(RightReserve, EDuelHand::Right);
}

void ADuelFighter::CycleLeftHand()
{
    if (UGameplayStatics::IsGamePaused(this))
    {
        if (ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>())
        {
            Mode->AdjustLoadout(-1);
        }
        return;
    }
    RequestWeapon(LeftReserve, EDuelHand::Left);
}

void ADuelFighter::CheckBladeContact(float LastAge, const DuelCourt::FMoveTiming& Spec, const FVector& BeforeTravel)
{
    const DuelCourt::FStrike* Strike = DuelCourt::Strike(MoveWeapon, Move, MoveArt);
    if (bBoss || !Weapon || !LeftWeapon || !Strike ||
        (Move == EDuelMove::Crimson && !DuelCourt::IsBladeArt(MoveArt)))
    {
        return;
    }

    const float First = FMath::Max(LastAge, Spec.Windup);
    const float Last = FMath::Min(MoveAge, Spec.Windup + Spec.Active);
    if (Last < First)
    {
        return;
    }
    CSV_SCOPED_TIMING_STAT(DuelCombat, BladeContact);
    const float FrameEnd = MoveAge;
    const FVector EndOfTravel = GetActorLocation();
    const bool bLiveTarget = !bCombatOver && IsValid(Target) && Target->IsAlive();
    const FVector Body = bLiveTarget ? Target->GetActorLocation() : FVector::ZeroVector;
    const float Radius = bLiveTarget ? Target->GetCapsuleComponent()->GetScaledCapsuleRadius() : 0.f;
    const float AxisHalfLength = bLiveTarget ? Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - Radius : 0.f;
    const double HeadScale = bLiveTarget && Target->Figure ? Target->Figure->GetRelativeScale3D().X / 1.075 : 1.0;
    const FVector HeadCenter = bLiveTarget && Target->Head ? Target->Head->GetComponentLocation() +
        (Target->GetActorForwardVector() * 36.f + FVector(0, 0, 50.f)) * HeadScale : Body;
    const int32 Samples = FMath::Max(1, FMath::CeilToInt((Last - First) * 120.f));
    const bool bHeavyTrail = Move == EDuelMove::Heavy || Move == EDuelMove::Crimson;
    const bool bOfferingAttack = RitualOfferingSeconds() > 0.f && Move != EDuelMove::Crimson;
    const bool bOfferingTrail = bOfferingAttack && MoveWeapon == EDuelWeapon::RitualSword;
    const FLinearColor Glow = bOfferingTrail ? FLinearColor(4.8f, .13f, .28f) :
        MoveArt == EDuelArt::HoundFinesse || MoveArt == EDuelArt::HoundChase
        ? (Character == EDuelCharacter::Hiro ? FLinearColor(4.4f, .12f, .25f) : FLinearColor(4.6f, 2.95f, 1.3f))
        : Move == EDuelMove::Crimson ? DuelCourt::WeaponArt(MoveArt).Glow : DuelCourt::Armament(MoveWeapon).Glow;
    const float TravelFirst = TravelProgress(LastAge, Spec);
    const float TravelLast = TravelProgress(FrameEnd, Spec);
    const FVector2D Span = DuelCourt::BladeSpan(MoveWeapon);
    const FVector LocalHilt(Span.X, 0.f, 0.f);
    const float WhirlExtension = MoveWeapon == EDuelWeapon::Axe && MoveArt == EDuelArt::Whirl ? 116.f : 0.f;
    const FVector LocalTip(Span.Y + WhirlExtension, MoveWeapon == EDuelWeapon::Houndfang ? 25.f : 0.f, 0.f);

    FVector Contact = FVector::ZeroVector;
    FVector ContactAxis = ActionDirection;
    double EdgeFraction = 0.;
    FVector ContactOrigin = GetActorLocation();
    FVector PreviousHilt = FVector::ZeroVector, PreviousTip = FVector::ZeroVector;
    bool bContact = bHitDelivered;
    bool bCourtContact = false;
    for (int32 I = 0; I <= Samples; ++I)
    {
        MoveAge = FMath::Lerp(First, Last, I / static_cast<float>(Samples));
        FTransform Grip;
        PoseFigure(0.f, &Grip);
        const float FrameAlpha = !FMath::IsNearlyEqual(TravelLast, TravelFirst)
            ? (TravelProgress(MoveAge, Spec) - TravelFirst) / (TravelLast - TravelFirst) : 1.f;
        const FVector Offset = FMath::Lerp(BeforeTravel, EndOfTravel, FrameAlpha) - EndOfTravel;
        const FVector A = Grip.TransformPosition(LocalHilt) + Offset;
        const FVector B = Grip.TransformPosition(LocalTip) + Offset;
        const FVector LastHilt = PreviousHilt, LastTip = PreviousTip;
        PreviousHilt = A;
        PreviousTip = B;
        if (!bHitDelivered && !bCourtContact && Arena->HitCourtSeal(*this, A, B, 11.f, false)) bCourtContact = true;
        Arena->TrackBlade(A, B, Glow, bHeavyTrail, !bTrackingBlade, bOfferingTrail);
        bTrackingBlade = true;
        if (!bLiveTarget || bContact || bCourtContact)
        {
            continue;
        }

        FVector NearBody;
        FMath::SegmentDistToSegmentSafe(A, B, Body - FVector(0, 0, AxisHalfLength),
            Body + FVector(0, 0, AxisHalfLength), Contact, NearBody);
        if (FVector::DistSquared(Contact, NearBody) <= FMath::Square(Radius + 11.f))
        {
            bContact = true;
        }
        else
        {
            Contact = FMath::ClosestPointOnSegment(HeadCenter, A, B);
            bContact = FVector::DistSquared(Contact, HeadCenter) <= FMath::Square(99.0 * HeadScale);
        }
        if (bContact)
        {
            EdgeFraction = FMath::Clamp(FVector::Dist(A, Contact) / FMath::Max(1., FVector::Dist(A, B)), 0., 1.);
            ContactOrigin = EndOfTravel + Offset;
            if (I > 0)
                ContactAxis = (Contact - FMath::Lerp(LastHilt, LastTip, EdgeFraction)).GetSafeNormal(UE_SMALL_NUMBER, ActionDirection);
        }
    }
    MoveAge = FrameEnd;
    if (bCourtContact) { bHitDelivered = true; return; }
    if (!bContact || bHitDelivered) return;
    const FVector ToTarget = Body - ContactOrigin;
    FBladeHit Hit{EdgeFraction, ToTarget.Size2D(), FVector::DotProduct(ActionDirection, ToTarget.GetSafeNormal2D()),
        Target->Posture, false, false, bChargedArt};
    if (MoveWeapon == EDuelWeapon::Greatblade || MoveWeapon == EDuelWeapon::FirePoker)
    {
        const auto TargetTiming = DuelCourt::Timing(Target->Move, Target->IsAwakened(), Target->MoveWeapon, Target->MoveArt);
        Hit.bRecovering = Target->Move >= EDuelMove::Paw && !Target->IsStaggered() &&
            Target->MoveAge >= Target->MoveRecoveryAt(TargetTiming) && TargetTiming.Total() > 0.f;
        Hit.bMoving = Target->GetVelocity().SizeSquared2D() > FMath::Square(95.) ||
            (Target->Travel > 0.f && Target->MoveAge >= TargetTiming.Windup &&
                Target->MoveAge < TargetTiming.Windup + TargetTiming.Active);
    }
    const FBladeBonus Bonus = BladeBonus(MoveWeapon, Move, MoveArt, Hit);
    const float Damage = Strike->Damage * Bonus.Damage * (bOfferingAttack ? DuelCourt::RitualOfferingDamage : 1.f);
    if (Target->ReceiveDuelDamage(Damage, ContactOrigin, false, &Contact))
    {
        bHitDelivered = true;
        Target->DamagePosture((Strike->Posture * Bonus.Posture + Bonus.ExtraPosture) *
            (bOfferingAttack ? DuelCourt::RitualOfferingPosture : 1.f));
        if (!bCombatOver)
        {
            Hitstop = bHeavyTrail ? .053f : Bonus.bPoint ? .043f : .030f;
        }
        const FLinearColor ImpactGlow = Bonus.bPoint ? FLinearColor(.64f, .88f, 1.f) :
            bOfferingTrail ? DuelCourt::Armament(MoveWeapon).Glow : Glow;
        Arena->BladeImpact(Contact, ContactAxis, ImpactGlow, bHeavyTrail);
    }
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelWeaponRoles, "ManosabaDuel.WeaponRoles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelWeaponRoles::RunTest(const FString& Parameters)
{
    (void)Parameters;
    FBladeHit Hit{.85, 215., .97};
    const auto Bonuses = [&Hit](EDuelWeapon Weapon, EDuelMove Move, EDuelArt Art = EDuelArt::Default)
    {
        return BladeBonus(Weapon, Move, Art, Hit);
    };
    TestTrue(TEXT("The aligned rapier heavy and spear light reward their real point contact"),
        Bonuses(EDuelWeapon::Rapier, EDuelMove::Heavy).Damage == 1.22f &&
        Bonuses(EDuelWeapon::Spear, EDuelMove::Cut).Damage == 1.25f &&
        Bonuses(EDuelWeapon::Spear, EDuelMove::Cut).Posture == 1.18f);
    TestEqual(TEXT("The dedicated advancing spear art retains its own single-hit baseline"),
        Bonuses(EDuelWeapon::Spear, EDuelMove::Crimson, EDuelArt::Thrust).Damage, 1.f);
    Hit.AimDot = .80;
    TestFalse(TEXT("Angular error removes the point reward"), Bonuses(EDuelWeapon::Rapier, EDuelMove::Heavy).bPoint);
    Hit.AimDot = .97;
    Hit.EdgeFraction = .45;
    TestFalse(TEXT("Contact in the middle of the shaft removes the point reward"),
        Bonuses(EDuelWeapon::Spear, EDuelMove::Cut).bPoint);
    Hit.Distance = 95.;
    TestEqual(TEXT("A spear hit crowded against the shaft loses force"), Bonuses(EDuelWeapon::Spear, EDuelMove::Cut).Damage, .85f);
    Hit.bRecovering = true;
    TestTrue(TEXT("Greatblade heavy rewards the enemy recovery window"),
        Bonuses(EDuelWeapon::Greatblade, EDuelMove::Heavy).Damage == 1.18f &&
        Bonuses(EDuelWeapon::Greatblade, EDuelMove::Cut).Damage == 1.f);
    Hit.ExistingPosture = 15.f;
    TestTrue(TEXT("Hammer follows prior posture pressure, preserving ordinary HP damage"),
        Bonuses(EDuelWeapon::Hammer, EDuelMove::Heavy).Posture == 1.20f &&
        Bonuses(EDuelWeapon::Hammer, EDuelMove::Heavy).Damage == 1.f);
    Hit.ExistingPosture = 14.99f;
    TestEqual(TEXT("Hammer opens at the normal posture value"),
        Bonuses(EDuelWeapon::Hammer, EDuelMove::Heavy).Posture, 1.f);
    Hit.bMoving = true;
    TestTrue(TEXT("Fire poker hooks moving enemies, leaving an ordinary jab unchanged"),
        Bonuses(EDuelWeapon::FirePoker, EDuelMove::Crimson, EDuelArt::PokerRake).ExtraPosture == 25.f &&
        Bonuses(EDuelWeapon::FirePoker, EDuelMove::Cut).ExtraPosture == 0.f);
    const auto Small = DuelCourt::ShieldResponse(EDuelWeapon::Buckler);
    const auto Large = DuelCourt::ShieldResponse(EDuelWeapon::Greatshield);
    TestTrue(TEXT("The longer Buckler parry window trades against Greatshield block coverage and stamina economy"),
        Small.ParryWindow > Large.ParryWindow &&
        Small.Chip > Large.Chip && Small.CostPerDamage > Large.CostPerDamage &&
        Small.BlockDot > Large.BlockDot && Small.ParryPosture < Large.ParryPosture);
    TestEqual(TEXT("Empty is a valid offhand option"), DuelCourt::CanEquip(EDuelWeapon::Empty,
        EDuelCharacter::Ema, EDuelHand::Left), true);
    TestFalse(TEXT("Empty cannot replace a required main-hand weapon"), DuelCourt::CanEquip(EDuelWeapon::Empty,
        EDuelCharacter::Ema, EDuelHand::Right));
    for (int32 Index = 0; Index <= static_cast<int32>(EDuelWeapon::RitualSword); ++Index)
        TestNotNull(TEXT("Each equipment selection exposes its play style in the inventory"),
            DuelCourt::EquipmentEffect(static_cast<EDuelWeapon>(Index)));
    return !HasAnyErrors();
}
#endif
