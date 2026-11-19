#include "DuelDungeonChains.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "DuelArena.h"
#include "DuelGame.h"
#include "DuelNorthLayout.h"
#include "DuelNorthProgress.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelDungeonChainsQA,"ManosabaDuel.NorthTower.DungeonChains347",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDuelDungeonChainsQA::RunTest(const FString&)
{
    if(!TestTrue(TEXT("Dungeon chains run in a save-free actual North Tower"),
        FParse::Param(FCommandLine::Get(),TEXT("DuelNorthTower")) && FParse::Param(FCommandLine::Get(),TEXT("DuelNoSave"))))return false;
    ADuelMode* Mode=nullptr;
    for(const auto& C:GEngine->GetWorldContexts())if(C.World() && (Mode=C.World()->GetAuthGameMode<ADuelMode>()))break;
    if(!TestTrue(TEXT("Actual arena and two fighters exist"),Mode && Mode->Arena && Mode->PlayerFighter && Mode->BossFighter))return false;
    auto* Arena=Mode->Arena.Get();auto* Player=Mode->PlayerFighter.Get();auto* Boss=Mode->BossFighter.Get();
    const auto SavedPlayer=Player->Character,SavedBoss=Boss->Character;
    ON_SCOPE_EXIT
    {
        UGameplayStatics::SetGamePaused(Mode,false);Arena->ClearCombatEffects();
        Player->Character=SavedPlayer;Boss->Character=SavedBoss;Player->BuildFigure();Boss->BuildFigure();Mode->ReturnToCheckpoint();
    };
    using Stage=FDuelDungeonChainsState::EStage;
    const auto Reset=[&](EDuelCharacter Hero)
    {
        Arena->ClearCombatEffects();Mode->Result=EDuelOutcome::Fighting;
        FDuelNorthProgress Route;Route.Reset(Hero);
        Player->Character=Hero;Player->ResetFighter(true);Player->RestAtCheckpoint();Player->bLockOn=false;
        Boss->Character=Route.BossForBattle(0);Boss->BossEncounter=0;Boss->ResetFighter(true);Boss->BossPhase=2;
        Player->Target=Boss;Boss->Target=Player;Player->Arena=Boss->Arena=Arena;
        Arena->SetNorthOrigin(DuelNorthLayout::CourtOrigin(0));
        const auto Origin=Arena->CourtOrigin();
        Player->SetActorLocationAndRotation(Origin+FVector(-460,0,62),FRotator::ZeroRotator);
        Boss->SetActorLocationAndRotation(Origin+FVector(650,0,103),FRotator(0,180,0));
        Player->ForwardAxis=Player->RightAxis=0;Player->ViewYaw=0;
    };
    const auto Open=[&]()
    {
        Arena->TickCourtInteractions(Boss,5.01f);
        if(!TestTrue(TEXT("Real phase-two encounter raises two visible hanging chains before activation"),
            Arena->DungeonChains.Stage==Stage::Raising && Arena->DungeonChains.Chains[0].IsValid() && Arena->DungeonChains.Chains[1].IsValid()))return false;
        Arena->TickCourtInteractions(Boss,1.26f);
        return TestTrue(TEXT("An announced chain cycle becomes active for twenty-four seconds"),
            Arena->DungeonChains.Stage==Stage::Active && FMath::IsNearlyEqual(Arena->DungeonChains.ActiveUntil-Arena->DungeonChains.Clock,24.f,.01f));
    };
    const auto Advance=[&](float Seconds,bool bTickBoss=false)
    {
        const int32 Steps=FMath::CeilToInt(Seconds/.01f);const float Dt=Seconds/Steps;
        for(int32 I=0;I<Steps;++I)
        {
            if(Player->Move!=EDuelMove::Idle)Player->TickMove(Dt);
            if((bTickBoss || Boss->Move==EDuelMove::Stagger) && Boss->Move!=EDuelMove::Idle)Boss->TickMove(Dt);
            Arena->TickOrbs(Dt);Arena->TickCourtInteractions(Boss,Dt);
        }
    };
    for(const auto Hero:{EDuelCharacter::Hiro,EDuelCharacter::Ema})
    {
        Reset(Hero);if(!Open())return false;
        const auto Enemy=Hero==EDuelCharacter::Hiro?EDuelCharacter::Ema:EDuelCharacter::Hiro;
        TestTrue(TEXT("Dungeon caster is the actual companion opponent on both story routes"),
            Boss->SelectedCharacter()==Enemy && Player->SelectedCharacter()==Hero && Arena->DungeonChains.Boss.Get()==Boss);
        auto& S=Arena->DungeonChains;
        Player->SetActorLocation(S.Points[0]+FVector(-120,0,-73));Player->ConfirmDuel();
        TestTrue(TEXT("F never removes a chain or leaks into a world interaction during combat"),
            S.Broken==0 && Mode->Outcome()==EDuelOutcome::Fighting && !UGameplayStatics::IsGamePaused(Mode));
        S.NextVolley=S.Clock+100.f;
        TestFalse(TEXT("Player-owned attacks cannot claim an enemy chain strike"),Arena->HitDungeonChains(*Player,S.Points[0]+FVector(400,0,0),S.Points[0]-FVector(400,0,0),48.f));
        Arena->LaunchThread(Boss,S.Points[0]+FVector(450,0,0),-FVector::ForwardVector,10,0,FLinearColor::Blue,1000,40,.9f);
        Advance(.95f);
        TestEqual(TEXT("A real player projectile crossing the chain does not break it"),S.Broken,uint8(0));
        UGameplayStatics::SetGamePaused(Mode,true);const float PausedClock=S.Clock;
        Arena->TickDungeonChains(Boss,10.f);
        TestFalse(TEXT("Paused menus cannot advance chain contacts"),Arena->HitDungeonChains(*Boss,S.Points[0]+FVector(400,0,0),S.Points[0]-FVector(400,0,0),48));
        TestEqual(TEXT("Pause freezes the actual mechanism clock"),S.Clock,PausedClock);UGameplayStatics::SetGamePaused(Mode,false);

        Reset(Hero);if(!Open())return false;
        Arena->DungeonChains.NextVolley=Arena->DungeonChains.Clock+100.f;
        Player->SetActorLocation(Arena->CourtOrigin()+FVector(650,0,62));
        const float BeforeHit=Player->Health;
        Arena->LaunchThread(Player,Arena->DungeonChains.Source,-FVector::ForwardVector,34,16,FLinearColor::Red,1180,48,1.4f,true);
        Advance(1.3f);
        TestTrue(TEXT("An unavoided actual hostile projectile deals HP damage without granting chain progress"),Player->Health<BeforeHit && Arena->DungeonChains.Broken==0);

        Reset(Hero);if(!Open())return false;
        Arena->DungeonChains.NextVolley=Arena->DungeonChains.Clock+100.f;
        const FVector PastChain=(Arena->DungeonChains.Points[0]-Arena->DungeonChains.Source).GetSafeNormal2D();
        Player->SetActorLocation(Arena->DungeonChains.Points[0]+PastChain*350.-FVector(0,0,73));
        const float BeforeChainHit=Player->Health;
        Arena->LaunchThread(Player,Arena->DungeonChains.Source,PastChain,34,16,FLinearColor::Red,1180,48,3.8f,true);
        Advance(2.25f);
        TestTrue(TEXT("A real hostile bolt continues beyond the first severed chain and hits a player who does not sidestep"),
            Arena->DungeonChains.Broken==1 && Player->Health<BeforeChainHit);

        Reset(Hero);if(!Open())return false;
        for(int32 Chain=0;Chain<2;++Chain)
        {
            auto& C=Arena->DungeonChains;
            const int32 TargetChain=Hero==EDuelCharacter::Ema?1-Chain:Chain;
            const FVector Behind=(C.Points[TargetChain]-C.Source).GetSafeNormal2D();
            Player->Move=EDuelMove::Idle;Player->SetActorLocation(C.Points[TargetChain]+Behind*350.f-FVector(0,0,73));
            Player->SetActorRotation(FRotator::ZeroRotator);Player->ViewYaw=0;Player->bLockOn=false;
            Player->ForwardAxis=0;Player->RightAxis=TargetChain?1.f:-1.f;
            C.NextVolley=C.Clock;const float Health=Player->Health;const FVector BeforeRoll=Player->GetActorLocation();
            Advance(.02f);const FVector LockedAim=C.Aim;
            if(!TestTrue(TEXT("A visible curse volley locks the real player's position for a complete warning"),C.bAiming && C.FireAt-C.Clock>1.15f))return false;
            Advance(1.02f);
            Player->PressShift();Player->ReleaseShift();
            if(!TestTrue(TEXT("Real Shift press/release starts a lateral roll after the line locks"),Player->Move==EDuelMove::Roll &&
                FMath::Abs(FVector::DotProduct(Player->DodgeDirection,(LockedAim-C.Source).GetSafeNormal2D()))<.45))return false;
            Advance(3.f);
            TestTrue(TEXT("The aimed line stays fixed while a real lateral dodge moves the player clear"),
                C.Aim.Equals(LockedAim,.01) && FVector::Dist2D(Player->GetActorLocation(),BeforeRoll)>200.f && Player->Health==Health &&
                FMath::Abs(FVector::DotProduct((Player->GetActorLocation()-BeforeRoll).GetSafeNormal2D(),(LockedAim-C.Source).GetSafeNormal2D()))<.45);
            TestEqual(TEXT("A real emitted projectile breaks the aligned chain, without F"),C.Progress(),Chain+1);
            Player->RightAxis=0;
        }
        TestTrue(TEXT("Two broken chains trigger a real stagger and a readable counter window"),
            Arena->DungeonChains.Stage==Stage::Counter && Boss->Move==EDuelMove::Stagger &&
            Arena->DungeonChains.Deadline-Arena->DungeonChains.Clock>1.f && Arena->DungeonChainsPrompt().Contains(TEXT("近身反击")));
        const float BossHealth=Boss->Health;
        Player->Move=EDuelMove::Idle;Player->bLockOn=false;Player->SetWeapon(EDuelWeapon::Rapier);
        Player->SetActorLocationAndRotation(Boss->GetActorLocation()+FVector(-165,0,-41),FRotator::ZeroRotator);
        if(!TestTrue(TEXT("A real melee counterattack starts in the earned window"),Player->StartMove(EDuelMove::Cut)))return false;
        Advance(.72f);
        TestTrue(TEXT("The earned opening permits real weapon HP damage"),Boss->Health<BossHealth);

        Reset(Hero);if(!Open())return false;
        Arena->DungeonChains.NextVolley=Arena->DungeonChains.Clock+100.f;
        Player->SetActorLocationAndRotation(Arena->CourtOrigin()+FVector(-280,-600,62),FRotator::ZeroRotator);
        Boss->SetActorLocationAndRotation(Arena->CourtOrigin()+FVector(1400,-600,103),FRotator(0,180,0));
        const EDuelMove Signature=Enemy==EDuelCharacter::Ema?EDuelMove::OssuaryShears:EDuelMove::HoundShade;
        if(!TestTrue(TEXT("The actual route boss signature starts"),Boss->StartMove(Signature)))return false;
        Advance(2.8f,true);
        TestTrue(TEXT("Actual moving bone wings or hound signature, not only the scheduled field bolt, break a chain"),Arena->DungeonChains.Broken&1);
        Arena->TickCourtInteractions(Boss,25.f);
        TestTrue(TEXT("An unfinished cycle recovers after timeout without instant printed-seal punishment"),Arena->DungeonChains.Stage==Stage::Cooldown);
        Arena->TickCourtInteractions(Boss,6.01f);
        TestTrue(TEXT("Retry rebuilds both unbroken chains with fresh progress"),Arena->DungeonChains.Stage==Stage::Raising &&
            Arena->DungeonChains.Broken==0 && Arena->DungeonChains.Chains[0].IsValid() && Arena->DungeonChains.Chains[1].IsValid());
        Arena->ClearCombatEffects();
        TestTrue(TEXT("Defeat/retry cleanup releases chain state and its field projectiles"),!Arena->DungeonChains.Boss.IsValid() &&
            !Arena->Orbs.ContainsByPredicate([](const auto& Shot){return Shot.Visual.IsValid() && Shot.Visual->ComponentHasTag(TEXT("DungeonCurseBolt347"));}));
        AddInfo(FString::Printf(TEXT("DungeonChains347 hero=%s boss=%s: F rejected, pause frozen, friendly projectile ignored, live HP damage, two real baited volleys with lateral dodge, melee counter, native signature contact and retry cleanup checked"),Player->CharacterName(),Boss->CharacterName()));
    }
    return !HasAnyErrors();
}
#endif
