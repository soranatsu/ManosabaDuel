#include "DuelGame.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "DuelArena.h"
#include "DuelNorthLayout.h"
#include "DuelNorthFinds.h"
#include "DuelNorthTower.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "InputKeyEventArgs.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelCircularSpellsTest, "ManosabaDuel.NorthTower.CircularSpells",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelCircularSpellsTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    const TCHAR* Command = FCommandLine::Get();
    if (!FParse::Param(Command, TEXT("DuelNorthCircularSpells")))
    {
        AddInfo(TEXT("Use -DuelNorthCircularSpells to exercise staff casts in an isolated North Tower game."));
        return true;
    }
    if (!TestTrue(TEXT("North Tower automation uses a no-save game"),
        FParse::Param(Command, TEXT("DuelNorthTower")) && FParse::Param(Command, TEXT("DuelNoTitle")) &&
        FParse::Param(Command, TEXT("DuelNoSave")))) return false;

    ADuelMode* Mode = nullptr;
    if (GEngine)
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
            if (Context.World() && (Mode = Context.World()->GetAuthGameMode<ADuelMode>())) break;
    if (!TestTrue(TEXT("Ready North Tower game with two fighters"), Mode && Mode->NorthTowerActor && Mode->Arena &&
        Mode->Result == EDuelOutcome::Ready && Mode->PlayerFighter && Mode->BossFighter)) return false;

    auto& Player = *Mode->PlayerFighter;
    auto& Boss = *Mode->BossFighter;
    FDuelNorthProgress& Loot = Mode->NorthTowerActor->State;
    const uint32 OriginalCaches = Loot.CacheBits;
    auto* Controller = Cast<APlayerController>(Player.GetController());
    if (!TestTrue(TEXT("Local player input is bound to a live encounter target"),
        Controller && Controller->PlayerInput && Player.InputComponent && Player.Target == &Boss)) return false;

    const auto OriginalSpells = Player.PreparedSpells;
    const int32 OriginalSlot = Player.ActiveSpellSlot;
    const EDuelWeapon OriginalWeapon = Player.Equipped;
    const EDuelWeapon OriginalOffhand = Player.Offhand;
    const EDuelArt OriginalArt = Player.ArtSelection;
    const FTransform PlayerTransform = Player.GetActorTransform(), BossTransform = Boss.GetActorTransform();
    const FVector OriginalOrigin = Mode->Arena->CourtOrigin();
    ON_SCOPE_EXIT
    {
        Controller->FlushPressedKeys();
        Loot.CacheBits = OriginalCaches;
        Player.SetWeapon(OriginalWeapon);
        Player.SetWeapon(OriginalOffhand, EDuelHand::Left);
        Player.ArtSelection = OriginalArt;
        Player.PreparedSpells = OriginalSpells;
        Player.ActiveSpellSlot = OriginalSlot;
        Mode->NorthTowerActor->Respawn(*Mode);
        Mode->Arena->SetNorthOrigin(OriginalOrigin);
        Player.SetActorTransform(PlayerTransform, false, nullptr, ETeleportType::TeleportPhysics);
        Boss.SetActorTransform(BossTransform, false, nullptr, ETeleportType::TeleportPhysics);
    };

    TestTrue(TEXT("Advanced circles require exploration first"), Loot.bExpeditionLoot &&
        !Loot.KnowsSpell(EDuelArt::StarTrack) && !Loot.KnowsSpell(EDuelArt::ReturningFrost));
    Loot.CacheBits = DuelNorthFinds::AllCacheBits();
    Player.SetWeapon(EDuelWeapon::Staff);
    Player.SetSpellSlot(0, EDuelArt::StarTrack);
    Player.SetSpellSlot(1, EDuelArt::ReturningFrost);
    if (!TestTrue(TEXT("Both circular spells can be equipped on the staff"),
        Player.SpellAt(0) == EDuelArt::StarTrack && Player.SpellAt(1) == EDuelArt::ReturningFrost)) return false;

    const FVector Origin = DuelNorthLayout::CourtOrigin(0);
    const auto Place = [&](ADuelFighter& Fighter, double X, double FeetHeight, double Y = 0.)
    {
        const double HalfHeight = Fighter.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        Fighter.SetActorLocation(Origin + FVector(X, Y, HalfHeight + FeetHeight),
            false, nullptr, ETeleportType::TeleportPhysics);
    };
    const TArray<UInputComponent*> InputStack{Player.InputComponent};
    const auto KeyEvent = [&](FKey Key, EInputEvent Event)
    {
        Controller->InputKey(FInputKeyParams(Key, Event, Event == IE_Pressed ? 1. : 0.));
        Controller->PlayerInput->ProcessInputStack(InputStack, 1.f / 60.f, false);
    };
    const auto Tap = [&](FKey Key)
    {
        KeyEvent(Key,IE_Pressed);
        KeyEvent(Key,IE_Released);
    };
    const auto CastAt = [&](const TCHAR* Case, EDuelArt Spell, double InitialX, double ImpactX,
        double ImpactFeetHeight, bool bHit, double ImpactY = 0.)
    {
        Mode->Arena->SetNorthOrigin(Origin);
        Mode->ResetEncounter();
        Place(Player, 0., 2.);
        Place(Boss, InitialX, 2.);
        Player.ActiveSpellSlot = 0;
        const bool bFrost = Spell == EDuelArt::ReturningFrost;
        if (bFrost) Tap(EKeys::Four);
        else Player.SetSpellSlot(0, Spell);
        const float Focus = Player.Focus, Health = Boss.Health;
        Tap(EKeys::E);
        if (!TestTrue(FString::Printf(TEXT("%s: E starts the selected staff art"), Case),
            Player.Move == EDuelMove::Crimson && Player.MoveWeapon == EDuelWeapon::Staff &&
            Player.MoveArt == Spell)) return;
        TestEqual(FString::Printf(TEXT("%s: cast spends focus once"), Case), Player.Focus,
            Focus - DuelCourt::FocusCost(Spell));
        const bool bAtPlayer = bFrost || Spell == EDuelArt::EmberRecoil || Spell == EDuelArt::MoonCrescent;
        const FVector Mark = Origin + FVector(bAtPlayer ? 0. : InitialX, 0., 8.);
        TestTrue(FString::Printf(TEXT("%s: preview is on the intended ground center"), Case),
            Player.CastOrigin.Equals(Mark, .01));
        const auto Timing = DuelCourt::Timing(EDuelMove::Crimson, false, EDuelWeapon::Staff, Spell);
        Player.TickMove(Timing.Windup - .01f);
        TestEqual(FString::Printf(TEXT("%s: windup leaves target health intact"), Case), Boss.Health, Health);
        Place(Boss, ImpactX, ImpactFeetHeight, ImpactY);
        Player.TickMove(.02f);
        const auto& Strike = DuelCourt::WeaponArt(Spell).Strike;
        TestEqual(FString::Printf(TEXT("%s: health at impact"), Case), Health - Boss.Health, bHit ? Strike.Damage : 0.f);
        TestEqual(FString::Printf(TEXT("%s: posture at impact"), Case), Boss.Posture, bHit ? Strike.Posture : 0.f);
        if (bHit && Spell == EDuelArt::StarTrack)
            TestTrue(TEXT("Star Track prolongs boss posture recovery"), Boss.PostureDelay >= 3.2f);
    };

    const double StarRadius = DuelCourt::WeaponArt(EDuelArt::StarTrack).Strike.Reach;
    const double FrostRadius = DuelCourt::WeaponArt(EDuelArt::ReturningFrost).Strike.Reach;
    const double FrostInner = DuelCourt::ReturningFrostInner;
    CastAt(TEXT("Star Track reaches the locked ring edge"), EDuelArt::StarTrack, 800., 800. + StarRadius, 2., true);
    CastAt(TEXT("Star Track misses a target that steps off the ring"), EDuelArt::StarTrack,
        800., 801. + StarRadius, 2., false);
    CastAt(TEXT("Returning Frost protects the empty center"), EDuelArt::ReturningFrost, 250., FrostInner - 1., 2., false);
    CastAt(TEXT("Returning Frost reaches the inner edge"), EDuelArt::ReturningFrost, 250., FrostInner, 2., true);
    CastAt(TEXT("Returning Frost reaches the outer edge"), EDuelArt::ReturningFrost, 250., FrostRadius, 2., true);
    CastAt(TEXT("Returning Frost stops beyond the outer edge"), EDuelArt::ReturningFrost, 250., FrostRadius + 1., 2., false);
    CastAt(TEXT("Returning Frost stays close to the floor"), EDuelArt::ReturningFrost, 250., 250., 90., false);
    const auto TestRecall = [&](double AtRelease, double AtRecall, bool bReleaseHit, bool bRecallHit)
    {
        Mode->Arena->SetNorthOrigin(Origin);
        Mode->ResetEncounter();
        Place(Player, 0., 2.);
        Place(Boss, AtRelease, 2.);
        Player.SetSpellSlot(0, EDuelArt::ReturningFrost);
        Player.ActiveSpellSlot = 0;
        const auto Timing = DuelCourt::Timing(EDuelMove::Crimson, false, EDuelWeapon::Staff, EDuelArt::ReturningFrost);
        const auto& Strike = DuelCourt::WeaponArt(EDuelArt::ReturningFrost).Strike;
        const float Health = Boss.Health;
        Tap(EKeys::E);
        Player.TickMove(Timing.Windup + .01f);
        TestEqual(TEXT("Frost release uses the marked band"), Health - Boss.Health, bReleaseHit ? Strike.Damage : 0.f);
        Place(Boss, AtRecall, 2.);
        Player.TickMove(DuelCourt::ReturningFrostRecall - .02f);
        const float BeforeRecall = Boss.Health;
        Player.TickMove(.02f);
        TestEqual(TEXT("Frost recall has its own timed band check with a one-hit cap"),
            BeforeRecall - Boss.Health, bRecallHit ? Strike.Damage : 0.f);
    };
    TestRecall(80., 250., false, true);
    TestRecall(250., 250., true, false);
    TestRecall(80., 80., false, false);
    TestTrue(TEXT("Collected loot permits both advanced arts"),
        Loot.KnowsSpell(EDuelArt::PrismSeal) && Loot.KnowsSpell(EDuelArt::EmberRecoil));
    const double PrismRadius = DuelCourt::WeaponArt(EDuelArt::PrismSeal).Strike.Reach;
    CastAt(TEXT("Prism seal reaches the eastern vertex"), EDuelArt::PrismSeal, 720., 720. + PrismRadius - 1., 2., true);
    CastAt(TEXT("Prism seal does not become a circle at a flat edge"), EDuelArt::PrismSeal,
        720., 720., 2., false, PrismRadius * .90);
    CastAt(TEXT("Ember recoil hits the forward fire band"), EDuelArt::EmberRecoil, 350., 510., 2., true);
    CastAt(TEXT("Ember recoil leaves the centre empty"), EDuelArt::EmberRecoil, 350., 120., 2., false);
    CastAt(TEXT("Ember recoil excludes targets behind the caster"), EDuelArt::EmberRecoil, 350., -490., 2., false);
    TestTrue(TEXT("Both high-tier shapes are selectable after collection"),
        Loot.KnowsSpell(EDuelArt::GildedCross) && Loot.KnowsSpell(EDuelArt::MoonCrescent));
    CastAt(TEXT("Gilded cross hits the long arm"), EDuelArt::GildedCross, 650., 650. + 670., 2., true, 98.);
    CastAt(TEXT("Gilded cross hits the perpendicular arm"), EDuelArt::GildedCross, 650., 650., 2., true, 555.);
    CastAt(TEXT("Gilded cross leaves diagonal corners open"), EDuelArt::GildedCross, 650., 650. + 295., 2., false, 295.);
    CastAt(TEXT("Moon crescent hits the bright face"), EDuelArt::MoonCrescent, 440., 545., 2., true);
    CastAt(TEXT("Moon crescent leaves its cutout open"), EDuelArt::MoonCrescent, 440., 245., 2., false);
    CastAt(TEXT("Moon crescent leaves the rear clear"), EDuelArt::MoonCrescent, 440., -625., 2., false);
    for (const auto Spell : {EDuelArt::GildedCross, EDuelArt::MoonCrescent})
    {
        const bool bCross = Spell == EDuelArt::GildedCross;
        Mode->Arena->SetNorthOrigin(Origin);
        Mode->ResetEncounter();
        Place(Player,0.,2.);
        Place(Boss,545.,2.);
        Player.SetSpellSlot(0,Spell);
        Player.ActiveSpellSlot=0;
        Player.Health = DuelCourt::PlayerMaxHealth - 100.f;
        const float StartingHealth = Player.Health, BossHealth = Boss.Health, Focus = Player.Focus;
        const auto Spec = DuelCourt::Timing(EDuelMove::Crimson,false,EDuelWeapon::Staff,Spell);
        KeyEvent(EKeys::E,IE_Pressed);
        Player.TickMove(Spec.Windup * .9f);
        TestTrue(TEXT("Holding E builds charge before the spell commits"), Player.bChargedArt && Boss.Health == BossHealth);
        Player.TickMove(Spec.Windup * .27f);
        TestTrue(TEXT("A held spell preserves its cast mark and deals no damage before release"),
            Player.MoveAge < Spec.Windup && Boss.Health == BossHealth && Player.ChargeSeal.IsValid());
        KeyEvent(EKeys::E,IE_Released);
        Player.TickMove(.02f);
        const auto& Strike = DuelCourt::WeaponArt(Spell).Strike;
        TestTrue(TEXT("Fully charged hit is stronger, with the same focus charge"),
            FMath::IsNearlyEqual(BossHealth - Boss.Health,Strike.Damage * 1.30f,.01f) &&
            Player.Focus == Focus - DuelCourt::FocusCost(Spell));
        TestTrue(TEXT("Charged cross breaks posture, charged crescent restores health"),
            bCross ? Boss.Move == EDuelMove::Stagger && Boss.Posture == 0.f && Player.Health == StartingHealth
            : Player.Health > StartingHealth && Player.Health <= StartingHealth + 62.f);
        TestFalse(TEXT("Release removes the previous warning artwork"), Player.ChargeSeal.IsValid());
    }
    for (bool bTap : {true, false})
    {
        Mode->Arena->SetNorthOrigin(Origin);
        Mode->ResetEncounter();
        Place(Player, 0., 2.);
        Place(Boss, 400., 2.);
        Player.SetSpellSlot(0, EDuelArt::IceLance);
        Player.ActiveSpellSlot = 0;
        const float Health = Boss.Health;
        const auto Timing = DuelCourt::Timing(EDuelMove::Crimson, false, EDuelWeapon::Staff, EDuelArt::IceLance);
        const float Cap = DuelCourt::ChargeExtension(EDuelWeapon::Staff, EDuelArt::IceLance);
        KeyEvent(EKeys::E, IE_Pressed);
        if (bTap) KeyEvent(EKeys::E, IE_Released);
        Player.TickMove(Timing.Windup + .02f);
        if (!bTap)
        {
            TestEqual(TEXT("Holding Ice Lance keeps the projectile at the caster"), Boss.Health, Health);
            Player.TickMove(Cap);
            KeyEvent(EKeys::E, IE_Released);
        }
        Mode->Arena->Tick(.24f);
        const float Base = DuelCourt::WeaponArt(EDuelArt::IceLance).Strike.Damage;
        TestTrue(TEXT("Ice Lance has a normal tap and a stronger automatic release at the hold cap"),
            FMath::IsNearlyEqual(Health - Boss.Health, Base * (bTap ? 1.f : 1.38f), .01f));
    }
    for (bool bTap : {true, false})
    {
        Mode->Arena->SetNorthOrigin(Origin);
        Mode->ResetEncounter();
        Player.SetWeapon(EDuelWeapon::Axe);
        Player.ArtSelection = EDuelArt::Default;
        Place(Player, 0., 2.);
        Place(Boss, 190., 2.);
        const float Health = Boss.Health;
        const auto Timing = DuelCourt::Timing(EDuelMove::Crimson, false, EDuelWeapon::Axe, EDuelArt::Whirl);
        KeyEvent(EKeys::E, IE_Pressed);
        if (!bTap)
        {
            Player.TickMove(Timing.Windup + .12f);
            TestTrue(TEXT("Axe does not move or strike out of a fully charged hold"), Player.bChargedArt &&
                Boss.Health == Health && Player.GetActorLocation().Equals(Origin + FVector(0.,0.,
                    Player.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.), .01));
        }
        KeyEvent(EKeys::E, IE_Released);
        Player.TickMove(Timing.Windup + Timing.Active + .02f);
        const auto& Strike = DuelCourt::WeaponArt(EDuelArt::Whirl).Strike;
        TestTrue(TEXT("Axe charge raises the damage and posture of a physical contact exactly once"),
            FMath::IsNearlyEqual(Health - Boss.Health, Strike.Damage * (bTap ? 1.f : 1.18f), .01f) &&
            FMath::IsNearlyEqual(Boss.Posture, Strike.Posture * (bTap ? 1.f : 1.35f), .01f));
    }
    Player.SetWeapon(EDuelWeapon::YukiPen);
    Player.ArtSelection = EDuelArt::Default;
    const auto TestPenFinish = [&](double Lateral, double NextX, bool bFirstHit, bool bFinishHit)
    {
        Mode->Arena->SetNorthOrigin(Origin);
        Mode->ResetEncounter();
        Place(Player, 0., 2.);
        Place(Boss, 505., 2.);
        Tap(EKeys::E);
        const auto Spec = DuelCourt::Timing(EDuelMove::Crimson, false, EDuelWeapon::YukiPen, EDuelArt::PenScript);
        Place(Boss, 505., 2., Lateral);
        const float Health = Boss.Health;
        const auto& Strike = DuelCourt::WeaponArt(EDuelArt::PenScript).Strike;
        Player.TickMove(Spec.Windup + .01f);
        TestEqual(TEXT("Pen opens with a hit only in the first stroke"), Health - Boss.Health,
            bFirstHit ? Strike.Damage : 0.f);
        const FVector OldMark = Player.CastOrigin;
        Place(Boss, NextX, 2.);
        const float BeforeFinish = Boss.Health;
        Player.TickMove(.16f);
        if (bFirstHit && NextX != 505.)
        {
            const auto* Preview = Player.MapWaves.Num() == 1 ? Player.MapWaves[0].Preview.Get() : nullptr;
            TestTrue(TEXT("Pen finish warning follows its marked target before the second hit"),
                Preview && !Player.CastOrigin.Equals(OldMark, .01) &&
                FVector::DistSquared2D(Preview->GetComponentLocation(), Boss.GetActorLocation()) < .01);
        }
        Player.TickMove(.16f);
        TestTrue(TEXT("Pen finish follows the struck target and requires a real first contact"),
            FMath::IsNearlyEqual(BeforeFinish - Boss.Health, bFinishHit ? Strike.Damage * 1.50f : 0.f, .01f));
    };
    TestPenFinish(90., 505., false, false);
    TestPenFinish(0., 795., true, true);
    TestPenFinish(0., 505., true, true);
    const auto HeavyAt = [&](EDuelWeapon Weapon, EDuelHand Hand, double Distance, EDuelMove BossMove, float SetupPosture)
    {
        Mode->Arena->SetNorthOrigin(Origin);
        Mode->ResetEncounter();
        Player.SetWeapon(Weapon, Hand);
        Place(Player, 0., 2.);
        Place(Boss, Distance, 2.);
        Boss.Move = BossMove;
        const auto EnemyTiming = DuelCourt::Timing(BossMove, false);
        Boss.MoveAge = EnemyTiming.Windup + EnemyTiming.Active + .02f;
        Boss.Posture = SetupPosture;
        Player.StartMove(EDuelMove::Heavy, Hand);
        Player.TickMove(DuelCourt::Timing(EDuelMove::Heavy, false, Weapon).Total());
        return DuelCourt::BossMaxHealth - Boss.Health;
    };
    const auto HasPointHit = [&](EDuelWeapon Weapon, float Factor)
    {
        const float Damage = DuelCourt::Armament(Weapon).Heavy.Damage;
        for (double Range = 154.; Range <= 442.; Range += 12.)
            if (FMath::IsNearlyEqual(HeavyAt(Weapon, EDuelHand::Right, Range, EDuelMove::Idle, 0.f), Damage * Factor, .02f))
                return true;
        return false;
    };
    TestTrue(TEXT("The rapier point reward is reachable with the actual heavy blade sweep"), HasPointHit(EDuelWeapon::Rapier, 1.22f));
    TestTrue(TEXT("The spear point reward is reachable with the actual heavy blade sweep"), HasPointHit(EDuelWeapon::Spear, 1.25f));
    TestTrue(TEXT("A recovering boss takes the greatblade heavy punish"),
        FMath::IsNearlyEqual(HeavyAt(EDuelWeapon::Greatblade, EDuelHand::Right, 255., EDuelMove::Paw, 0.f),
            DuelCourt::Armament(EDuelWeapon::Greatblade).Heavy.Damage * 1.18f, .02f));
    HeavyAt(EDuelWeapon::Hammer, EDuelHand::Left, 182., EDuelMove::Idle, 0.f);
    TestFalse(TEXT("An isolated hammer heavy leaves a fresh posture bar below the break threshold"), Boss.IsStaggered());
    HeavyAt(EDuelWeapon::Hammer, EDuelHand::Left, 182., EDuelMove::Idle, 15.f);
    TestTrue(TEXT("Preloading 15 posture makes the same hammer attack break stance"), Boss.IsStaggered());
    for (const EDuelWeapon Shield : {EDuelWeapon::Buckler, EDuelWeapon::Greatshield})
    {
        Mode->Arena->SetNorthOrigin(Origin);
        Mode->ResetEncounter();
        Place(Player, 0., 2.);
        Place(Boss, 190., 2.);
        Player.SetWeapon(Shield, EDuelHand::Left);
        Player.NextParryPressAt = 0.0;
        Player.TickPlayer(.01f);
        const float WalkSpeed = Player.GetCharacterMovement()->MaxWalkSpeed;
        Player.PressLeftHand();
        Player.TickPlayer(.01f);
        TestEqual(TEXT("Both shields allow ordinary walking speed when raised"),
            Player.GetCharacterMovement()->MaxWalkSpeed, WalkSpeed);
        Player.HandPresses[static_cast<uint8>(EDuelHand::Left)].Since -= .15;
        const bool bBlocked = Player.ReceiveDuelDamage(40.f, Boss.GetActorLocation(), true);
        const bool bGreat = Shield == EDuelWeapon::Greatshield;
        const float Chip = 40.f * DuelCourt::BossDamageScale * (bGreat ? .06f : 0.f);
        TestTrue(TEXT("The Buckler can parry an age of .15 seconds that the Greatshield can only block"),
            bBlocked == bGreat && FMath::IsNearlyEqual(Player.Health, DuelCourt::PlayerMaxHealth - Chip, .01f));
        Player.ReleaseLeftHand();

        Mode->ResetEncounter();
        Place(Player, 0., 2.);
        Place(Boss, 190., 2.);
        Player.NextParryPressAt = 0.0;
        Player.PressLeftHand();
        Player.HandPresses[static_cast<uint8>(EDuelHand::Left)].Since -= .076;
        Player.ReceiveDuelDamage(40.f, Boss.GetActorLocation(), true);
        TestTrue(TEXT("An early Greatshield counter punishes posture more heavily"),
            FMath::IsNearlyEqual(Boss.Posture, bGreat ? 49.f : 36.f, .01f));
        Player.ReleaseLeftHand();
    }
    return !HasAnyErrors();
}

#endif
