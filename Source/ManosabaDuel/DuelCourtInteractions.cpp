#include "DuelArena.h"
#include "DuelGame.h"
#include "DuelNorthTower.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
    const FLinearColor Gold(1.65f, 1.04f, .26f), Cold(.26f, .65f, 1.1f), Red(1.6f, .13f, .21f);
    const TCHAR* SealNames[] = {TEXT("见证之眼"), TEXT("誓约之秤"), TEXT("断罪之冠")};
    void RemoveObject(USceneComponent* Root)
    {
        if (!Root) return;
        TArray<USceneComponent*> Children;
        Root->GetChildrenComponents(true, Children);
        for (int32 I = Children.Num() - 1; I >= 0; --I) Children[I]->DestroyComponent();
        Root->DestroyComponent();
    }
}

void ADuelArena::ClearCourtObjects()
{
    for (USceneComponent* Root : CourtObjects) RemoveObject(Root);
    CourtObjects.Reset();
}

void ADuelArena::ClearCourtInteractions()
{
    if (auto* World = GetWorld())
        if (const auto* Mode = World->GetAuthGameMode<ADuelMode>())
            if (auto* Player = const_cast<ADuelFighter*>(Mode->GetPlayerFighter())) Player->ClearJudgmentPenalty();
    ClearDungeonChains();
    ClearCourtObjects();
    RemoveObject(JudgmentBarrier.Get());
    JudgmentBarrier.Reset();
    bJudgmentWard = false;
    CourtBoss.Reset();
    CourtEncounter = INDEX_NONE;
    CourtStage = ECourtStage::Waiting;
    CourtClock = CourtDeadline = CourtActiveDeadline = CourtPulseClock = 0.f;
    NextCourtCycle = 5.f;
    CourtProgress = CourtCycle = 0;
    JudgmentSeed = 0;
    CourtFeedback.Reset();
    CourtFeedbackUntil = 0.f;
    CourtStrikes.Reset();
    LastCourtMelee.Reset();
    LastCourtMeleeSerial = MAX_uint32;
}

bool ADuelArena::JudgmentProtects(const ADuelFighter& Fighter) const
{
    const auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ADuelMode>() : nullptr;
    return Mode && Mode->Outcome() == EDuelOutcome::Fighting && bJudgmentWard && CourtEncounter == 2 &&
        CourtBoss.Get() == &Fighter && Fighter.SelectedCharacter() == EDuelCharacter::Yuki && Fighter.IsAlive() && !Fighter.bCombatOver;
}

void ADuelArena::BuildCourtObjects()
{
    ClearCourtObjects();
    if (CourtEncounter == 2) BuildJudgmentSeals();
}

void ADuelArena::CompleteCourtCounter()
{
    auto* Boss = CourtBoss.Get();
    if (!Boss) return;
    ClearCourtObjects();
    bJudgmentWard = false;
    RemoveObject(JudgmentBarrier.Get());
    JudgmentBarrier.Reset();
    CourtStage = ECourtStage::Feedback; // Remove immunity before awarding the real punish window.
    Boss->DamagePosture(DuelCourt::MaxPosture);
    CourtFeedback = TEXT("三枚判印已破 · 现在反击！");
    CourtFeedbackUntil = CourtClock + 2.5f;
    NextCourtCycle = CourtClock + 11.f;
    Burst(Boss->GetActorLocation() + FVector(0, 0, 55), Gold, 35, 450.f, true);
    Sound(EDuelSound::ParryChime, Boss->GetActorLocation());
}

void ADuelArena::BeginCourtRetaliation(bool bTimeout)
{
    const auto* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    auto* Player = Mode ? const_cast<ADuelFighter*>(Mode->GetPlayerFighter()) : nullptr;
    if (!Player || !CourtBoss.IsValid()) return;
    bCourtTimeout = bTimeout;
    CourtStage = ECourtStage::Retaliation;
    CourtDeadline = CourtClock + 1.f;
    CourtActiveDeadline = 0.f;
    NextCourtCycle = CourtClock + 5.05f;
    CourtProgress = 0;
    Player->ApplyJudgmentPenalty();
    CourtFeedback = bTimeout ? TEXT("判印超时：移动封锁1秒，武器、战技与施法封锁5秒") :
        TEXT("判印有误：移动封锁1秒，武器、战技与施法封锁5秒");
    CourtFeedbackUntil = CourtClock + 5.f;
    // Failure binds the player; it does not add an unavoidable HP hit during the movement lock.
    BuildCourtObjects();
    const FVector At = GroundMark(Player->GetActorLocation());
    Halo(At, 64.f, 165.f, Red, 1.f, false, EDuelMark::Scroll);
    Burst(At + FVector(0, 0, 45), Cold, 24, 190.f, true);
    Sound(EDuelSound::VerdictBell, At);
}

void ADuelArena::TickCourtInteractions(ADuelFighter* Boss, float DeltaSeconds)
{
    const auto* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    auto* Player = Mode ? const_cast<ADuelFighter*>(Mode->GetPlayerFighter()) : nullptr;
    if (!Mode || !Mode->UsesNorthTower() || Mode->Outcome() != EDuelOutcome::Fighting || !Boss || !Player ||
        !Boss->IsAlive() || !Player->IsAlive())
    {
        ClearCourtInteractions();
        return;
    }
    if (Boss->BossEncounter == 0)
    {
        if (CourtEncounter != INDEX_NONE) ClearCourtInteractions();
        TickDungeonChains(Boss, DeltaSeconds);
        return;
    }
    ClearDungeonChains();
    if (Boss->BossEncounter != 2 || Boss->SelectedCharacter() != EDuelCharacter::Yuki || Boss->BossPhase < 2)
    {
        if (CourtEncounter != INDEX_NONE) ClearCourtInteractions();
        return;
    }
    if (CourtBoss.Get() != Boss || CourtEncounter != 2)
    {
        ClearCourtInteractions();
        CourtBoss = Boss;
        CourtEncounter = 2;
        int32 Seed = Boss->BossRandom.GetInitialSeed();
        FParse::Value(FCommandLine::Get(), TEXT("DuelJudgmentSeed="), Seed);
        JudgmentSeed = uint32(Seed);
    }
    if (Boss->Move == EDuelMove::Awaken || Boss->Move == EDuelMove::Eclipse) return;
    CourtClock += FMath::Max(0.f, DeltaSeconds);
    if (JudgmentBarrier.IsValid()) JudgmentBarrier->SetWorldLocation(Boss->GetActorLocation() + FVector(0, 0, 28));
    if ((CourtStage == ECourtStage::Waiting || CourtStage == ECourtStage::Feedback) &&
        CourtClock >= NextCourtCycle && Player->JudgmentWeaponLockSeconds() <= 0.f)
    {
        CourtProgress = 0;
        ++CourtCycle;
        if (!PlaceJudgmentSeals())
        {
            NextCourtCycle = CourtClock + 1.f;
            return;
        }
        FRandomStream OrderRandom(int32(JudgmentSeed ^ (uint32(CourtCycle) * 7919u)));
        CourtOrder = {0, 1, 2};
        for (int32 I = 2; I > 0; --I) Swap(CourtOrder[I], CourtOrder[OrderRandom.RandRange(0, I)]);
        CourtStage = ECourtStage::Warning;
        CourtDeadline = CourtClock + 1.2f;
        CourtActiveDeadline = CourtDeadline + 12.5f;
        bJudgmentWard = true;
        BuildCourtObjects();
        for (int32 I = 0; I < 3; ++I)
            Sigil(CourtPoints[I] - FVector(0, 0, 117), 150.f, Cold, 1.2f, EDuelMark::Scroll);
        Sound(EDuelSound::Charge, CourtPoints[0]);
        UE_LOG(LogTemp, Display, TEXT("North judgment: seed=%u cycle=%d order=%d,%d,%d points=%s | %s | %s"),
            JudgmentSeed, CourtCycle, CourtOrder[0], CourtOrder[1], CourtOrder[2],
            *CourtPoints[0].ToString(), *CourtPoints[1].ToString(), *CourtPoints[2].ToString());
        if (auto* Tower = const_cast<ADuelNorthTower*>(Mode->NorthTower())) Tower->ShowMechanicHint(2);
    }
    else if (CourtStage == ECourtStage::Warning && CourtClock >= CourtDeadline)
    {
        CourtStage = ECourtStage::Open;
        Sound(EDuelSound::SpellReady, CourtPoints[CourtOrder[0]]);
    }
    else if (CourtStage == ECourtStage::Open && CourtClock >= CourtActiveDeadline)
        BeginCourtRetaliation(true);
    else if (CourtStage == ECourtStage::Retaliation && CourtClock >= CourtDeadline)
    {
        ClearCourtObjects();
        CourtStage = ECourtStage::Feedback;
        // The ward stays latched. A failed judgment is not an unearned boss-damage window.
    }
    if (CourtStage == ECourtStage::Warning || CourtStage == ECourtStage::Open)
    {
        const int32 Active = CourtOrder[CourtProgress];
        if (CourtObjects.IsValidIndex(Active) && CourtObjects[Active])
            for (USceneComponent* Child : CourtObjects[Active]->GetAttachChildren())
                if (Child->ComponentHasTag(TEXT("JudgmentActiveOrbit347")))
                    Child->AddLocalRotation(FRotator(0, 0, DeltaSeconds * 45.f));
        CourtPulseClock -= DeltaSeconds;
        if (CourtPulseClock <= 0.f)
        {
            CourtPulseClock = .16f;
            Emit(EDuelLight::Star, CourtPoints[Active] + FVector(0, 0, 172), FVector(0, 0, 40), FVector2D(32), Gold, .25f);
        }
    }
}

bool ADuelArena::TryCourtInteraction(ADuelFighter& Player)
{
    const auto* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    // F never dismantles either arena mechanic and cannot fall through to a door or menu during combat.
    return Mode && Mode->UsesNorthTower() && Mode->Outcome() == EDuelOutcome::Fighting &&
        Mode->GetPlayerFighter() == &Player && !UGameplayStatics::IsGamePaused(this);
}

bool ADuelArena::HitCourtSeal(ADuelFighter& Attacker, const FVector& From, const FVector& To, float Radius, bool bProjectile)
{
    const auto* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    if (!Mode || Mode->GetPlayerFighter() != &Attacker || !Attacker.IsAlive() || Attacker.JudgmentWeaponLockSeconds() > 0.f ||
        CourtEncounter != 2 || CourtStage != ECourtStage::Open) return false;
    if (!bProjectile && LastCourtMelee.Get() == &Attacker && LastCourtMeleeSerial == Attacker.MoveSerial) return false;
    int32 Nearest = INDEX_NONE;
    double Distance = TNumericLimits<double>::Max();
    for (int32 I = 0; I < 3; ++I)
    {
        if (!CourtObjects.IsValidIndex(I) || !CourtObjects[I]) continue;
        const FVector Point = FMath::ClosestPointOnSegment(CourtPoints[I], From, To);
        if (FVector::DistSquared(Point, CourtPoints[I]) > FMath::Square(98.f + Radius)) continue;
        const double Along = FVector::DistSquared(From, Point);
        if (Along < Distance) { Distance = Along; Nearest = I; }
    }
    if (Nearest == INDEX_NONE) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(JudgmentSealSight), false, &Attacker);
    Query.AddIgnoredActor(CourtBoss.Get());
    FHitResult Obstruction;
    if (GetWorld()->LineTraceSingleByChannel(Obstruction, From, CourtPoints[Nearest], ECC_Visibility, Query)) return false;
    if (!bProjectile)
    {
        LastCourtMelee = &Attacker;
        LastCourtMeleeSerial = Attacker.MoveSerial;
    }
    if (Nearest != CourtOrder[CourtProgress])
    {
        Burst(CourtPoints[Nearest], Red, 27, 360.f, true);
        BeginCourtRetaliation(false);
    }
    else
    {
        SpellContact(CourtPoints[Nearest], (To - From).GetSafeNormal(), Gold, true);
        Sound(EDuelSound::ParryChime, CourtPoints[Nearest]);
        ++CourtProgress;
        if (CourtProgress == 3) CompleteCourtCounter();
        else BuildCourtObjects();
    }
    return true;
}

FString ADuelArena::CourtInteractionPrompt() const
{
    const FString Dungeon = DungeonChainsPrompt();
    if (!Dungeon.IsEmpty()) return Dungeon;
    if (CourtClock < CourtFeedbackUntil) return CourtFeedback;
    if (CourtStage == ECourtStage::Warning) return TEXT("判印将显现：找金光双环目标");
    if (CourtStage == ECourtStage::Open)
        return FString::Printf(TEXT("攻击金光「%s」 %d/3 · %.1f秒"), SealNames[CourtOrder[CourtProgress]], CourtProgress,
            FMath::Max(0.f, CourtActiveDeadline - CourtClock));
    if (bJudgmentWard) return TEXT("封锁结束后再试 · 护罩尚未解除");
    return {};
}

bool ADuelArena::CourtSealAimPoint(FVector& Point) const
{
    if (CourtEncounter != 2 || CourtStage != ECourtStage::Open || CourtProgress >= 3) return false;
    Point = CourtPoints[CourtOrder[CourtProgress]];
    return true;
}
