#include "DuelGame.h"
#include "DuelBossDefense.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "DuelArena.h"
#include "DuelNorthLayout.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelBossDefenseQA, "ManosabaDuel.BossDefenseCadence",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelBossDefenseQA::RunTest(const FString& Parameters)
{
    using namespace DuelBossDefense;
    if (!FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")))
    {
        AddError(TEXT("BossDefenseCadence requires -DuelNoSave and an isolated user directory."));
        return false;
    }
    ADuelMode* Mode = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        if (UWorld* World = Context.World())
        {
            Mode = World->GetAuthGameMode<ADuelMode>();
            if (Mode) break;
        }
    }
    if (!TestNotNull(TEXT("Duel mode"), Mode)) return false;
    ADuelFighter* Player = Mode->PlayerFighter.Get();
    ADuelFighter* Boss = Mode->BossFighter.Get();
    if (!TestNotNull(TEXT("Player"), Player) || !TestNotNull(TEXT("Boss"), Boss) ||
        !TestNotNull(TEXT("Arena"), Mode->Arena.Get())) return false;

    const EDuelWeapon SavedRight = Player->EquippedWeapon(EDuelHand::Right);
    const EDuelWeapon SavedLeft = Player->EquippedWeapon(EDuelHand::Left);
    const EDuelCharacter SavedBoss = Boss->Character;
    const FVector Origin = Mode->Arena->CourtOrigin();
    const auto Near = [](float A, float B) { return FMath::IsNearlyEqual(A, B, .015f); };
    const auto Reset = [&]()
    {
        Mode->ResetEncounter();
        Player->SetActorLocationAndRotation(Origin + FVector(0, 0, 62), FRotator::ZeroRotator);
        Boss->SetActorLocationAndRotation(Origin + FVector(300, 0, 103), FRotator(0, 180, 0));
        Player->SetWeapon(EDuelWeapon::Buckler, EDuelHand::Left);
        Boss->Character = EDuelCharacter::Ema;
    };
    const auto Raise = [&]()
    {
        Player->PressHand(EDuelHand::Left);
        TestTrue(TEXT("Holding the left shield enters guard"), Player->IsGuarding(EDuelHand::Left));
    };
    const FVector Front = Origin + FVector(300, 0, 8);
    const FVector Rear = Origin + FVector(-300, 0, 8);
    constexpr float Raw = 40.f;
    const float Scaled = Raw * DuelCourt::BossDamageScale;

    Reset(); Raise();
    // Intentionally inside the precise-parry window: a blockable spell must
    // still spend normal guard stamina and chip HP instead of parrying for free.
    TestTrue(TEXT("Front energy impact resolves through held guard"),
        Player->ReceiveDuelDamage(Raw, Front, false, nullptr, false, true, true));
    TestTrue(TEXT("Blockable is not synonymous with parryable"),
        Near(Player->Health, DuelCourt::PlayerMaxHealth - Scaled * .17f) &&
        Near(Player->Stamina, DuelCourt::MaxStamina - (10.f + Raw * .35f) * DuelCourt::StaminaCostScale) &&
        Player->Move == EDuelMove::Idle && Boss->Move == EDuelMove::Idle);

    Reset(); Raise();
    Player->ReceiveDuelDamage(Raw, Rear, false, nullptr, false, true, true);
    TestTrue(TEXT("Rear impact bypasses the forward shield"),
        Near(Player->Health, DuelCourt::PlayerMaxHealth - Scaled) && Near(Player->Stamina, DuelCourt::MaxStamina));

    Reset(); Raise();
    Player->ReceiveDuelDamage(Raw, Front);
    TestTrue(TEXT("Ground and restraint damage retain their avoidance rule"),
        Near(Player->Health, DuelCourt::PlayerMaxHealth - Scaled) && Near(Player->Stamina, DuelCourt::MaxStamina));

    Reset(); Raise();
    Player->Stamina = 1.f;
    Player->ReceiveDuelDamage(Raw, Front, false, nullptr, false, true, true);
    TestTrue(TEXT("An exhausted shield breaks rather than granting immunity"),
        Player->IsGuardBroken() && Near(Player->Stamina, 0.f) && Near(Player->Health, DuelCourt::PlayerMaxHealth - Scaled));

    Reset();
    Player->SetWeapon(EDuelWeapon::Greatshield, EDuelHand::Right);
    Player->PressHand(EDuelHand::Right);
    Player->ReceiveDuelDamage(Raw, Front, false, nullptr, false, true, true);
    TestTrue(TEXT("Right greatshield uses its own chip and scaled stamina cost"),
        Near(Player->Health, DuelCourt::PlayerMaxHealth - Scaled * .06f) &&
        Near(Player->Stamina, DuelCourt::MaxStamina - (7.f + Raw * .26f) * DuelCourt::StaminaCostScale));

    Reset(); Raise();
    TestTrue(TEXT("Pillar begins"), Boss->StartMove(EDuelMove::Pillar));
    Boss->TickMove(DuelCourt::Timing(EDuelMove::Pillar, false).Windup + .001f);
    TestTrue(TEXT("Actual radial Pillar routes through held guard"),
        Near(Player->Health, DuelCourt::PlayerMaxHealth - 67.f * DuelCourt::BossDamageScale * .17f));

    Reset();
    Boss->BossPhase = 3;
    Boss->SetActorLocation(Origin + FVector(-600, 0, 103));
    Raise();
    Player->SetActorRotation(FRotator::ZeroRotator);
    TestTrue(TEXT("Wing verdict begins"), Boss->StartMove(EDuelMove::WingVerdict));
    if (!TestTrue(TEXT("Wing verdict owns a first wave"), !Boss->MapWaves.IsEmpty())) return false;
    auto& Lane = Boss->MapWaves[0];
    Lane.Marks = {Origin + FVector(0, 0, 8)};
    Lane.Axis = -FVector::ForwardVector;
    Lane.HalfLength = 600.f;
    Lane.Reach = 120.f;
    Boss->TickMove(Lane.ImpactTime + .001f);
    TestTrue(TEXT("Lane guard faces the real lane emitter, even with caster behind"),
        Near(Player->Health, DuelCourt::PlayerMaxHealth - 59.f * DuelCourt::BossDamageScale * .17f));

    TestTrue(TEXT("Ambush collision windows leave a whole new-roll interval"),
        AmbushSecondStart - AmbushLunge >= HitGap - .0001f);
    TestTrue(TEXT("Both moving floor-wave pairs leave a whole new-roll interval"),
        BoneSecondStart - BoneSweep >= HitGap - .0001f &&
        DuelCourt::ArchiveReturnAt - DuelCourt::ArchiveSweepTime >= HitGap - .0001f);
    TestTrue(TEXT("Mirror rounds and sequential marks keep their warnings"),
        MirrorSecondStart - MirrorSweep >= HitGap - .0001f &&
        RecordWindup - 3.f * RecordBeat >= .42f - .0001f);

    for (const EDuelMove Move : {EDuelMove::Seals, EDuelMove::Pursuit, EDuelMove::BrokenRecord,
        EDuelMove::MirrorEdict, EDuelMove::CrossAmbush, EDuelMove::BoneProcession, EDuelMove::ArchiveCollapse})
    {
        const auto Spec = DuelCourt::Timing(Move, true);
        float LastContact = 0.f;
        switch (Move)
        {
        case EDuelMove::Seals: LastContact = 2.f * SealBeat; break;
        case EDuelMove::Pursuit: LastContact = 3.f * PursuitBeat; break;
        case EDuelMove::BrokenRecord: LastContact = 3.f * RecordBeat; break;
        case EDuelMove::MirrorEdict: LastContact = MirrorSecondStart + MirrorSecondTime; break;
        case EDuelMove::CrossAmbush: LastContact = AmbushSecondStart + AmbushLunge; break;
        case EDuelMove::BoneProcession: LastContact = BoneSecondStart + BoneSweep; break;
        case EDuelMove::ArchiveCollapse: LastContact = DuelCourt::ArchiveReturnAt + DuelCourt::ArchiveReturnTime; break;
        default: break;
        }
        TestTrue(FString::Printf(TEXT("Move %d retains all scheduled damage within Active"), static_cast<int32>(Move)),
            Spec.Active + .0001f >= LastContact);
    }

    // Exercise real buffered roll chaining with 30/60/120 Hz ticks. Contacts are
    // direct defense probes, not an assertion about untested projectile paths.
    for (const float Frame : {1.f / 30.f, 1.f / 60.f, 1.f / 120.f})
    {
        Reset();
        Player->ForwardAxis = 1.f;
        TestTrue(TEXT("First roll starts"), Player->StartMove(EDuelMove::Roll));
        float Clock = 0.f;
        bool bQueued = false;
        const float FirstContact = DuelCourt::RollGuardDelay + .002f;
        const float SecondContact = FirstContact + HitGap;
        for (const float Contact : {FirstContact, SecondContact})
        {
            while (Clock + .00001f < Contact)
            {
                const float Step = FMath::Min(Frame, Contact - Clock);
                Player->TickMove(Step);
                Clock += Step;
                if (!bQueued && Clock >= .20f)
                {
                    Player->RequestMove(EDuelMove::Roll);
                    bQueued = true;
                }
            }
            TestTrue(FString::Printf(TEXT("Rolling protects contact %.3f at %.0f Hz"), Contact, 1.f / Frame),
                Player->InRollWindow() && !Player->ReceiveDuelDamage(20.f, Front, true) &&
                Near(Player->Health, DuelCourt::PlayerMaxHealth));
        }
    }

    // Put the fighter on the earliest contact point of each warning. Between these
    // placements, movement and every projectile collision use the normal roll and flight steps.
    ADuelArena& Arena = *Mode->Arena;
    for (const float Frame : {1.f / 30.f, 1.f / 60.f, 1.f / 120.f})
    {
        for (const EDuelMove Move : {EDuelMove::PrismPursuit, EDuelMove::ShadowVolley})
        {
            Reset();
            const bool bPrism = Move == EDuelMove::PrismPursuit;
            Boss->Character = bPrism ? EDuelCharacter::Yuki : EDuelCharacter::Hiro;
            Boss->BossPhase = 3;
            if (!TestTrue(TEXT("Boss starts its final-phase projectile chain"), Boss->StartMove(Move))) return false;
            const auto Tick = [&](float Step)
            {
                Player->TickMove(Step);
                Boss->TickMove(Step);
                Arena.TickOrbs(Step);
            };
            float PreviousContact = -HitGap;
            for (int32 Index = 0; Index < Boss->MapWaves.Num(); ++Index)
            {
                auto& Wave = Boss->MapWaves[Index];
                while (Wave.Marks.IsEmpty() && Boss->Move == Move && Boss->MoveAge < 8.f) Tick(Frame);
                if (!TestTrue(TEXT("Each projectile reports a new lane"), !Wave.Marks.IsEmpty())) return false;

                const float RollAt = Wave.ImpactTime - .085f;
                while (Boss->MoveAge + .00001f < RollAt) Tick(FMath::Min(Frame, RollAt - Boss->MoveAge));
                const FVector From = bPrism ? Wave.Marks[0] : Wave.Marks[0] - Wave.Axis * Wave.HalfLength;
                const FVector At = From + Wave.Axis * (Wave.Reach + 16.f);
                Player->SetActorLocation(FVector(At.X, At.Y, Origin.Z + 62.f));
                Player->DodgeDirection = -Wave.Axis;
                Player->RequestMove(EDuelMove::Roll);

                while (!Wave.bHitDelivered && Boss->Move == Move && Boss->MoveAge < 8.f) Tick(Frame);
                if (!TestTrue(FString::Printf(TEXT("Projectile %d of move %d contacts during a real roll at %.0f Hz"),
                    Index + 1, static_cast<int32>(Move), 1.f / Frame), Wave.bHitDelivered && !Wave.Projectile.IsValid() &&
                    Player->InRollWindow() && Near(Player->Health, DuelCourt::PlayerMaxHealth))) return false;

                TestTrue(TEXT("Even the earliest subsequent swept contact leaves another roll interval"),
                    Boss->MoveAge - PreviousContact >= HitGap - .0001f);
                PreviousContact = Boss->MoveAge;
            }
        }
    }

    struct FChainCase { EDuelCharacter Character; EDuelMove Move; int32 Count; };
    const FChainCase Chains[] = {
        {EDuelCharacter::Ema, EDuelMove::OssuaryChain, 3},
        {EDuelCharacter::Hiro, EDuelMove::HoundChain, 4},
        {EDuelCharacter::Yuki, EDuelMove::FrostStaffChain, 3},
    };
    const float FullRoll = DuelCourt::Timing(EDuelMove::Roll, false).Total();
    for (const FChainCase& Chain : Chains)
    {
        Reset();
        Boss->Character = Chain.Character;
        Boss->BossRandom.Initialize(4819);
        bool bSelected = false;
        for (int32 Draw = 0; Draw < 96; ++Draw)
            bSelected |= Boss->PickBossAttack(300.f) == Chain.Move;
        TestTrue(TEXT("Each boss can choose its own close-range chain in phase one"), bSelected);
        for (const FChainCase& Other : Chains)
        {
            if (Other.Character == Chain.Character) continue;
            Boss->Character = Other.Character;
            Boss->BossRandom.Initialize(4819);
            bool bShared = false;
            for (int32 Draw = 0; Draw < 96; ++Draw)
                bShared |= Boss->PickBossAttack(300.f) == Chain.Move;
            TestFalse(TEXT("The other boss attack pools cannot select this close chain"), bShared);
        }

        for (const float Frame : {1.f / 30.f, 1.f / 60.f, 1.f / 120.f})
        {
            for (const bool bRoll : {false, true})
            {
                Reset();
                Boss->Character = Chain.Character;
                if (!TestTrue(TEXT("The actual melee chain starts and schedules every authored contact"),
                    Boss->StartMove(Chain.Move) && Boss->MapWaves.Num() == Chain.Count)) return false;
                const auto AdvanceTo = [&](float To)
                {
                    while (Boss->Move == Chain.Move && Boss->MoveAge + .00001f < To)
                    {
                        const float Step = FMath::Min(Frame, To - Boss->MoveAge);
                        if (Player->Move != EDuelMove::Idle) Player->TickMove(Step);
                        Boss->TickMove(Step);
                    }
                    return Boss->Move == Chain.Move;
                };
                float LastContact = -1.f;
                for (int32 I = 0; I < Chain.Count; ++I)
                {
                    const auto& Wave = Boss->MapWaves[I];
                    const float Contact = Wave.ImpactTime;
                    if (!AdvanceTo(Contact - .075f)) return false;
                    Player->Health = DuelCourt::PlayerMaxHealth;
                    if (bRoll)
                    {
                        Player->DodgeDirection = -Wave.Axis;
                        if (!TestTrue(TEXT("A fresh normal roll is legal before the next melee contact"),
                            Player->Move == EDuelMove::Idle && Player->StartMove(EDuelMove::Roll))) return false;
                    }
                    if (!AdvanceTo(Contact - .002f)) return false;
                    const FVector Point = Boss->GetActorLocation() + Wave.Axis * 187.f;
                    // Sample the painted threat even if the preceding roll already left its reach.
                    Player->SetActorLocation(FVector(Point.X, Point.Y, Origin.Z + 62.f));
                    if (!AdvanceTo(Contact + .001f)) return false;
                    if (!TestTrue(FString::Printf(TEXT("Move %d hit %d at %.0f Hz resolves only when unprotected"),
                        int32(Chain.Move), I + 1, 1.f / Frame), Wave.bHitDelivered && (bRoll ?
                            Player->InRollWindow() && Near(Player->Health, DuelCourt::PlayerMaxHealth) :
                            Player->Health < DuelCourt::PlayerMaxHealth))) return false;
                    if (LastContact >= 0.f)
                        TestTrue(TEXT("Measured melee contacts leave a full roll, guard lead and 30Hz frame"),
                            Boss->MoveAge - LastContact >= FullRoll + DuelCourt::RollGuardDelay + 1.f / 30.f);
                    LastContact = Boss->MoveAge;
                }
            }
        }
        Reset();
        Boss->Character = Chain.Character;
        if (!TestTrue(TEXT("The melee hitch test starts"), Boss->StartMove(Chain.Move))) return false;
        Boss->TickMove(1.f);
        TestTrue(TEXT("A hitch cannot skip the normal attack's first anticipation"),
            Near(Boss->MoveAge, CloseMaxStep) && !Boss->MapWaves[0].bHitDelivered);
        AddInfo(FString::Printf(TEXT("Close chain %d: %d contacts tested with unprotected and full-roll controls at 30/60/120Hz"),
            int32(Chain.Move), Chain.Count));
    }

    for (const int32 Phase : {1, 3})
    {
        Reset();
        Boss->Character = EDuelCharacter::Yuki;
        Boss->BossPhase = Phase;
        Boss->BossRandom.Initialize(4819);
        bool bSelected = false;
        for (int32 Draw = 0; Draw < 96; ++Draw)
            bSelected |= Boss->PickBossAttack(850.f) == EDuelMove::IcicleRow;
        TestTrue(TEXT("Yuki can choose the icicle row from ranged spacing"), bSelected);
        Boss->SetActorLocation(Origin + FVector(410, 0, 103));
        Player->SetActorLocation(Origin + FVector(-1070, 0, 62));
        const int32 Count = Phase == 3 ? MaxIcicles : MaxIcicles - 1;
        if (!TestTrue(TEXT("Every ice shard visibly stages beside Yuki before any flight"),
            Boss->StartMove(EDuelMove::IcicleRow) && Boss->MapWaves.Num() == Count)) return false;
        const FVector Across = FVector::CrossProduct(FVector::UpVector, Boss->CastAxis);
        for (int32 I = 0; I < Count; ++I)
        {
            const auto& Wave = Boss->MapWaves[I];
            if (!TestTrue(TEXT("Each staged row slot has a stationary preview on the same side"),
                Wave.Marks.Num() == 1 && Wave.Preview.IsValid() && !Wave.Projectile.IsValid() &&
                FMath::Abs(FVector::DotProduct(Wave.Marks[0] - Boss->GetActorLocation(), Across)) >= 150.f)) return false;
            if (I > 0)
                TestTrue(TEXT("Staged crystals occupy distinct row positions"),
                    FVector::Dist2D(Wave.Marks[0], Boss->MapWaves[I - 1].Marks[0]) > 70.f);
        }
        for (int32 I = 0; I < Count; ++I)
        {
            auto& Wave = Boss->MapWaves[I];
            const FVector Aim = Origin + FVector(-1070, I % 2 ? 250 : -250, 62);
            Player->SetActorLocation(Aim);
            while (!Wave.bHitDelivered && Boss->Move == EDuelMove::IcicleRow)
            {
                constexpr float Frame = 1.f / 120.f;
                Boss->TickMove(Frame);
                Arena.TickOrbs(Frame);
            }
            if (!TestTrue(TEXT("Each shard replaces its staged mesh with actual flight toward independently sampled aim"),
                Wave.bHitDelivered && Wave.Projectile.IsValid() && !Wave.Preview.IsValid() && Wave.Marks.Num() == 2 &&
                Wave.Marks[1].Equals(Aim, .1) && Wave.Axis.Equals((Aim - Wave.Marks[0]).GetSafeNormal2D(), .01))) return false;
            if (I > 0)
            {
                const auto& Prior = Boss->MapWaves[I - 1];
                TestTrue(TEXT("Rapid ice releases overlap in flight at the authored interval"),
                    Near(Wave.ImpactTime - Prior.ImpactTime, IcicleBeat) && Prior.Projectile.IsValid() &&
                    !Prior.Axis.Equals(Wave.Axis, .05));
            }
        }
        AddInfo(FString::Printf(TEXT("Yuki ice row phase %d: %d aimed releases at %.0f ms, earlier shards still flying"),
            Phase, Count, 1000.f * IcicleBeat));
    }

    const int32 SavedEncounter = Boss->BossEncounter;
    const TPair<EDuelCharacter, EDuelMove> EncounterSignatures[] = {
        {EDuelCharacter::Ema, EDuelMove::OssuaryShears}, {EDuelCharacter::Hiro, EDuelMove::HoundShade}
    };
    for (const auto& Stage : EncounterSignatures)
    {
        Reset();
        Boss->Character = Stage.Key;
        for (const int32 Encounter : {0, 1})
        {
            Boss->BossEncounter = Encounter;
            Boss->BossRandom.Initialize(9346);
            bool bSelected = false;
            for (int32 Draw = 0; Draw < 96; ++Draw) bSelected |= Boss->PickBossAttack(660.f) == Stage.Value;
            TestEqual(TEXT("Either character may lead with their signature in battle two, independent of protagonist"),
                bSelected, Encounter == 1);
            TestEqual(TEXT("Difficulty follows the actual encounter for Ema and Hiro"), Boss->BossDifficulty(), Encounter);
        }
    }
    Boss->Character = EDuelCharacter::Yuki;
    TestEqual(TEXT("Yuki uses the final difficulty tier"), Boss->BossDifficulty(), 2);
    const auto DrawsLink = [&](EDuelMove From, EDuelMove To, float Range)
    {
        Boss->BossRandom.Initialize(4821);
        for (int32 Draw = 0; Draw < 128; ++Draw)
            if (Boss->PickBossFollowup(From, Range) == To) return true;
        return false;
    };
    Reset();
    Boss->BossEncounter = 0;
    TestTrue(TEXT("Ema can use the bone chain after her first-encounter leap"),
        DrawsLink(EDuelMove::Leap, EDuelMove::OssuaryChain, 315.f));
    Boss->Character = EDuelCharacter::Hiro;
    TestTrue(TEXT("Hiro can use the hound chain after her first-encounter paw strike"),
        DrawsLink(EDuelMove::Paw, EDuelMove::HoundChain, 315.f));
    struct FProfileLink
    {
        EDuelCharacter Character;
        EDuelMove From;
        EDuelMove To;
        float Range;
        const TCHAR* Label;
    };
    const FProfileLink MirrorLinks[] = {
        {EDuelCharacter::Ema, EDuelMove::OssuaryChain, EDuelMove::Wingfall, 660.f,
            TEXT("Ema extends the bone chain into a wingfall in mirror combat")},
        {EDuelCharacter::Hiro, EDuelMove::HoundChain, EDuelMove::CrossAmbush, 660.f,
            TEXT("Hiro extends the hound chain into an ambush in mirror combat")},
    };
    for (const FProfileLink& Link : MirrorLinks)
    {
        Reset();
        Boss->Character = Link.Character;
        Boss->BossEncounter = 0;
        Boss->BossPhase = 2;
        TestFalse(TEXT("The first encounter retains a break after the close chain"), DrawsLink(Link.From, Link.To, Link.Range));
        Boss->BossEncounter = 1;
        Boss->BossPhase = 1;
        TestFalse(TEXT("The mirror extension still waits for the phase change"), DrawsLink(Link.From, Link.To, Link.Range));
        Boss->BossPhase = 2;
        TestTrue(Link.Label, DrawsLink(Link.From, Link.To, Link.Range));
    }
    Reset();
    Boss->Character = EDuelCharacter::Yuki;
    TestTrue(TEXT("Yuki can follow a distant icicle row with a prism pursuit"),
        DrawsLink(EDuelMove::IcicleRow, EDuelMove::PrismPursuit, 720.f));
    Boss->BossPhase = 2;
    TestTrue(TEXT("Yuki can meet a committed approach with her staff chain"),
        DrawsLink(EDuelMove::PrismPursuit, EDuelMove::FrostStaffChain, 320.f));
    Boss->BossComboDepth = Boss->BossPhase + Boss->BossDifficulty();
    TestEqual(TEXT("A full final-tier combo still finishes"), Boss->PickBossFollowup(EDuelMove::PrismPursuit, 320.f), EDuelMove::Idle);
    Boss->BossComboDepth = 0;
    TestEqual(TEXT("A signature always leaves its punish window"), Boss->PickBossFollowup(EDuelMove::MirrorRiposte, 300.f), EDuelMove::Idle);
    TestTrue(TEXT("Airborne and physical pairs leave enough time to reset the roll"),
        ShadeBodyStart - ShadeFlight >= HitGap && MirrorReturnStart - MirrorFirstFlight >= HitGap);

    const auto StartSignature = [&](EDuelCharacter Character, EDuelMove Attack, float X)
    {
        const int32 Court = Character == EDuelCharacter::Yuki ? 2 : 1;
        const FVector Floor = Mode->UsesNorthTower() ? DuelNorthLayout::CourtOrigin(Court) : Origin;
        if (Mode->UsesNorthTower()) Arena.SetNorthOrigin(Floor);
        Reset();
        Boss->Character = Character;
        Boss->BossPhase = 3;
        Boss->BossEncounter = Court;
        Boss->SetActorLocationAndRotation(Floor + FVector(X, 0, 103), FRotator(0, 180, 0));
        Player->SetActorLocationAndRotation(Floor + FVector(0, 0, 62), FRotator::ZeroRotator);
        TestTrue(TEXT("Signature starts in the character's combat arena"), Boss->StartMove(Attack));
        return Floor;
    };
    const auto AdvanceSignature = [&](float Age, bool bAdvancePlayer = true)
    {
        for (int32 I = 0; I < 320 && IsSignature(Boss->Move) && Boss->MoveAge + .00001f < Age; ++I)
        {
            const float Step = FMath::Min(1.f / 60.f, Age - Boss->MoveAge);
            if (bAdvancePlayer && Player->Move != EDuelMove::Idle) Player->TickMove(Step);
            Boss->TickMove(Step);
        }
    };
    const auto HeldShield = [&]()
    {
        Player->PressHand(EDuelHand::Left);
        Player->HandPresses[static_cast<uint8>(EDuelHand::Left)].bParryReady = false;
    };

    StartSignature(EDuelCharacter::Ema, EDuelMove::OssuaryShears, 630.f);
    const auto Shears = DuelCourt::Timing(EDuelMove::OssuaryShears, true);
    if (!TestTrue(TEXT("Ema shows two raised bone blades and two independent paths"),
        Boss->MapWaves.Num() == 2 && Boss->MapWaves[0].Preview.IsValid() && Boss->MapWaves[1].Preview.IsValid() &&
        FVector::Dist2D(Boss->MapWaves[0].Marks[0], Boss->MapWaves[1].Marks[0]) > 400.)) return false;
    AdvanceSignature(Shears.Windup - .015f);
    TestEqual(TEXT("The hovering bone blades do not hit in anticipation"), Player->Health, DuelCourt::PlayerMaxHealth);
    AdvanceSignature(Shears.Windup + Shears.Active + .001f);
    TestTrue(TEXT("The airborne scissors contact the player at most once"),
        Boss->bHitDelivered && Near(DuelCourt::PlayerMaxHealth - Player->Health, 46.f * DuelCourt::BossDamageScale));

    const FVector EmaFloor = StartSignature(EDuelCharacter::Ema, EDuelMove::OssuaryShears, 630.f);
    AdvanceSignature(Shears.Windup - .06f);
    const auto& FirstBlade = Boss->MapWaves[0];
    const FVector DodgeStart = FirstBlade.Marks[0] + FirstBlade.Axis.GetSafeNormal2D() * 84.;
    Player->SetActorLocation(FVector(DodgeStart.X, DodgeStart.Y, EmaFloor.Z + 62.));
    Player->DodgeDirection = FirstBlade.Axis.GetSafeNormal2D();
    TestTrue(TEXT("Real roll starts before a low-flying blade"), Player->StartMove(EDuelMove::Roll));
    AdvanceSignature(Shears.Windup + .04f);
    TestTrue(TEXT("The swept airborne blade is dodgeable during the normal roll window"),
        Boss->bHitDelivered && Player->InRollWindow() && Near(Player->Health, DuelCourt::PlayerMaxHealth));

    const FVector HiroFloor = StartSignature(EDuelCharacter::Hiro, EDuelMove::HoundShade, 640.f);
    const auto Shade = DuelCourt::Timing(EDuelMove::HoundShade, true);
    const FVector LockedShadow = Boss->MapWaves[0].Marks[1];
    AdvanceSignature(Shade.Windup + ShadeFlight + .01f);
    TestTrue(TEXT("Hiro's red shade hits before the stationary original steps"),
        Boss->MapWaves[0].bHitDelivered && Near(DuelCourt::PlayerMaxHealth - Player->Health,
            35.f * DuelCourt::BossDamageScale) && FVector::Dist2D(Boss->GetActorLocation(), HiroFloor) >= 635.);
    Player->SetActorLocation(HiroFloor + FVector(0, 240, 62));
    AdvanceSignature(Shade.Windup + ShadeBodyStart - .10f);
    TestTrue(TEXT("The real pounce reads the player's changed position after the shade has committed"),
        Boss->MapWaves[0].Marks[1].Equals(LockedShadow, .01) && Boss->MapWaves[1].Marks.Num() == 2 &&
        Boss->MapWaves[1].Marks[1].Y > HiroFloor.Y + 160.);
    AdvanceSignature(Shade.Windup + ShadeBodyStart + ShadeBodyFlight + .001f);
    TestTrue(TEXT("Hiro's body travels and has its own second contact"),
        Boss->MapWaves[1].bHitDelivered && FVector::Dist2D(Boss->GetActorLocation(), HiroFloor + FVector(0, 240, 0)) < 200. &&
        Near(DuelCourt::PlayerMaxHealth - Player->Health, (35.f + 52.f) * DuelCourt::BossDamageScale));

    const FVector YukiFloor = StartSignature(EDuelCharacter::Yuki, EDuelMove::MirrorRicochet, 635.f);
    const auto Ricochet = DuelCourt::Timing(EDuelMove::MirrorRicochet, true);
    if (!TestTrue(TEXT("Yuki stages an upright mirror beyond the path of her first ice blade"),
        Boss->MapWaves.Num() == 5 && Boss->MapWaves[1].Preview.IsValid() && Boss->MapWaves[1].Marks[0].X < YukiFloor.X))
        return false;
    AdvanceSignature(Ricochet.Windup + MirrorFirstFlight + .01f);
    TestTrue(TEXT("The outgoing blade reaches the target before the reflector retargets"),
        Boss->MapWaves[0].bHitDelivered && Boss->MapWaves[2].Marks.IsEmpty());
    const float BeforeReturn = Player->Health;
    Player->SetActorLocation(YukiFloor + FVector(0, 365, 62));
    const float ReturnAt = Ricochet.Windup + MirrorReturnStart;
    AdvanceSignature(ReturnAt - .21f);
    TestTrue(TEXT("Final-phase reflection sends three new real paths toward the sampled position"),
        Boss->MapWaves[2].Preview.IsValid() && Boss->MapWaves[3].Preview.IsValid() && Boss->MapWaves[4].Preview.IsValid() &&
        FVector::DotProduct(Boss->MapWaves[0].Axis, Boss->MapWaves[2].Axis) < -.50 &&
        Boss->MapWaves[2].Axis.Y > .42 && Near(Player->Health, BeforeReturn));
    AdvanceSignature(ReturnAt + MirrorReturnFlight + .001f);
    TestTrue(TEXT("The reflected fan uses one damage beat and leaves a punish window"),
        Boss->MapWaves[2].bHitDelivered && Near(BeforeReturn - Player->Health, 50.f * DuelCourt::BossDamageScale) &&
        Boss->Move == EDuelMove::MirrorRicochet && Ricochet.Total() - Boss->MoveAge >= .77f);
    Boss->DamagePosture(DuelCourt::MaxPosture);
    TestTrue(TEXT("Stagger cancels the remaining mirror before the next attack"),
        Boss->Move == EDuelMove::Stagger && Boss->MapWaves.IsEmpty());

    const FVector RiposteFloor = StartSignature(EDuelCharacter::Yuki, EDuelMove::MirrorRiposte, 418.f);
    const auto Riposte = DuelCourt::Timing(EDuelMove::MirrorRiposte, true);
    AdvanceSignature(RiposteGuardOpens + .045f);
    const FVector ShardContact = Boss->CastOrigin;
    const FVector Side = Boss->GetActorLocation() + FVector(0, 250, 0);
    const float MirrorHealth = Boss->Health;
    TestTrue(TEXT("An off-axis hit bypasses the visible mirror guard"),
        Boss->MapWaves[0].Preview.IsValid() && Boss->ReceiveDuelDamage(14.f, Side, false, &ShardContact) &&
        Near(MirrorHealth - Boss->Health, 14.f));
    TestTrue(TEXT("The front mirror absorbs exactly one projectile"),
        !Boss->ReceiveDuelDamage(20.f, RiposteFloor + FVector(0, 0, 62), false, &ShardContact, true) &&
        Boss->bFeintTriggered && !Boss->MapWaves[0].Preview.IsValid() &&
        Boss->ReceiveDuelDamage(13.f, RiposteFloor + FVector(0, 0, 62), false, &ShardContact, true));
    AdvanceSignature(Riposte.Windup - .03f);
    HeldShield();
    Player->SetActorRotation(FRotator::ZeroRotator);
    AdvanceSignature(Riposte.Windup + Riposte.Active + .001f);
    TestTrue(TEXT("The returned shot is an actual blockable projectile with normal chip and guard cost"),
        Boss->MapWaves[0].bHitDelivered && Player->Stamina < DuelCourt::MaxStamina &&
        Near(DuelCourt::PlayerMaxHealth - Player->Health, 45.f * DuelCourt::BossDamageScale * .17f));

    StartSignature(EDuelCharacter::Yuki, EDuelMove::MirrorRiposte, 328.f);
    AdvanceSignature(Riposte.Windup - .045f);
    Player->PressHand(EDuelHand::Left);
    AdvanceSignature(Riposte.Windup + Riposte.Active + .001f);
    TestTrue(TEXT("The untriggered guard lowers into a physical thrust that a precise parry interrupts"),
        Player->Move == EDuelMove::Parry && Boss->Move == EDuelMove::GuardReel &&
        Near(Player->Health, DuelCourt::PlayerMaxHealth));
    if (Mode->UsesNorthTower()) Arena.SetNorthOrigin(Origin);
    Boss->BossEncounter = SavedEncounter;

    Player->SetWeapon(SavedRight, EDuelHand::Right);
    Player->SetWeapon(SavedLeft, EDuelHand::Left);
    Boss->Character = SavedBoss;
    Mode->ResetEncounter();
    return true;
}
#endif
