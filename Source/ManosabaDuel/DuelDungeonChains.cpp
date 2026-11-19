#include "DuelDungeonChains.h"
#include "DuelArena.h"
#include "DuelGame.h"
#include "DuelGeometry.h"
#include "DuelNorthLayout.h"
#include "DuelNorthTower.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

namespace
{
    const FLinearColor ChainGold(1.65f,1.03f,.29f),Bone(1.40f,.87f,.52f),Shade(1.54f,.11f,.25f);
    void RemoveChainObject(USceneComponent* Root)
    {
        if(!Root)return;
        TArray<USceneComponent*> Children;Root->GetChildrenComponents(true,Children);
        for(int32 I=Children.Num()-1;I>=0;--I)Children[I]->DestroyComponent();
        Root->DestroyComponent();
    }
}

void ADuelArena::ClearDungeonChains()
{
    for(const auto& Root:DungeonChains.Chains)RemoveChainObject(Root.Get());
    RemoveChainObject(DungeonChains.Emitter.Get());RemoveChainObject(DungeonChains.Guide.Get());
    Orbs.RemoveAll([](const FCrimsonOrb& Shot)
    {
        auto* Visual=Shot.Visual.Get();
        if(!Visual || !Visual->ComponentHasTag(TEXT("DungeonCurseBolt347")))return false;
        RemoveChainObject(Visual);return true;
    });
    DungeonChains=FDuelDungeonChainsState();
}

void ADuelArena::BuildDungeonChains()
{
    using namespace DuelGeometry;
    auto& S=DungeonChains;
    auto* Iron=ColorMaterial(this,FLinearColor(.16f,.19f,.24f),false,.48f,.75f);
    auto* Glow=ColorMaterial(this,ChainGold,true);
    const auto* Room=DuelNorthLayout::FindRoom(TEXT("DungeonCourt"));
    const double Ceiling=Room?Room->Bounds.Max.Z-CourtOrigin().Z:1400.;
    for(int32 I=0;I<2;++I)
    {
        RemoveChainObject(S.Chains[I].Get());S.Chains[I].Reset();
        if(S.Broken&(1<<I))continue;
        auto* Root=Joint(this,SceneRoot,S.Points[I]);
        Root->ComponentTags.Append({TEXT("DungeonCurseChain347"),FName(I?TEXT("DungeonChainRight"):TEXT("DungeonChainLeft"))});
        S.Chains[I]=Root;
        for(int32 Link=0;Link<16;++Link)
        {
            const double Z=FMath::Lerp(55.,Ceiling-135.,(Link+.5)/16.);
            const double H=(Ceiling-190.)/32.;
            const FVector Side=Link%2?FVector(0,17,0):FVector(17,0,0);
            const FVector P(0,0,Z);
            Rod(this,Root,P+FVector(0,0,H),P+Side,3.6f,Iron);
            Rod(this,Root,P+Side,P-FVector(0,0,H),3.6f,Iron);
            Rod(this,Root,P-FVector(0,0,H),P-Side,3.6f,Iron);
            Rod(this,Root,P-Side,P+FVector(0,0,H),3.6f,Iron);
        }
        Shape(this,Root,EShape::Box,FVector(0,0,Ceiling-135.),FVector(105,105,20),Iron);
        Shape(this,Root,EShape::Box,FVector::ZeroVector,FVector(78,48,78),Glow,FRotator(0,0,45));
        for(int32 Side:{-1,1})Rod(this,Root,FVector(Side*55,-16,-42),FVector(Side*55,-16,42),4.f,Glow);
        Ring(S.Points[I]-FVector(0,0,125),100.f,ChainGold,.8f,1.f,4.f);
    }
    if(!S.Emitter.IsValid())
    {
        S.Emitter=Joint(this,SceneRoot,S.Source);
        S.Emitter->ComponentTags.Add(TEXT("DungeonCurseEmitter347"));
        for(int32 I=0;I<8;++I)
        {
            const auto P=[](int32 N){const double A=N*PI/4.;return FVector(0,FMath::Cos(A)*63,FMath::Sin(A)*63);};
            Rod(this,S.Emitter.Get(),P(I),P(I+1),4.8f,Glow);
        }
    }
    for(auto* Mesh:TInlineComponentArray<UStaticMeshComponent*>(this))
        if((S.Emitter.IsValid() && Mesh->IsAttachedTo(S.Emitter.Get())) ||
            (S.Chains[0].IsValid() && Mesh->IsAttachedTo(S.Chains[0].Get())) ||
            (S.Chains[1].IsValid() && Mesh->IsAttachedTo(S.Chains[1].Get())))
        {Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetCastShadow(false);}
}

bool ADuelArena::HitDungeonChains(ADuelFighter& Caster,const FVector& From,const FVector& To,float Radius)
{
    auto& S=DungeonChains;const auto* Mode=GetWorld()->GetAuthGameMode<ADuelMode>();
    if(S.Stage!=FDuelDungeonChainsState::EStage::Active || S.Boss.Get()!=&Caster || !Mode ||
        Mode->Outcome()!=EDuelOutcome::Fighting || Mode->GetBossFighter()!=&Caster || !Mode->UsesNorthTower() || UGameplayStatics::IsGamePaused(this) ||
        Caster.BossEncounter!=0 || !Caster.IsAlive() || Radius<0.f)return false;
    int32 Hit=INDEX_NONE;double Along=TNumericLimits<double>::Max();
    for(int32 I=0;I<2;++I)
    {
        if(S.Broken&(1<<I))continue;
        const FVector Near=FMath::ClosestPointOnSegment(S.Points[I],From,To);
        if(FVector::DistSquared(Near,S.Points[I])>FMath::Square(67.f+Radius))continue;
        const double Distance=FVector::DistSquared(From,Near);
        if(Distance<Along){Along=Distance;Hit=I;}
    }
    if(Hit==INDEX_NONE)return false;
    FCollisionQueryParams Sight(SCENE_QUERY_STAT(DungeonChainSight),false,&Caster);
    Sight.AddIgnoredActor(Mode->GetPlayerFighter());FHitResult Wall;
    if(GetWorld()->LineTraceSingleByChannel(Wall,From,S.Points[Hit],ECC_Visibility,Sight))return false;
    S.Broken|=1<<Hit;
    SpellContact(S.Points[Hit],(To-From).GetSafeNormal(),ChainGold,true);
    Burst(S.Points[Hit],ChainGold,26,390.f,true);Sound(EDuelSound::HeavyImpact,S.Points[Hit]);
    RemoveChainObject(S.Chains[Hit].Get());S.Chains[Hit].Reset();
    S.Feedback=S.Broken==3?TEXT("两条悬链尽断，咒力反噬对手！趁失衡近身反击！"):TEXT("悬链断了一条！再把攻击引向另一条，锁定亮线后横闪。");
    S.FeedbackUntil=S.Clock+2.6f;
    // Stagger on the arena tick, outside the signature/orb owner's current iteration.
    S.bPendingCounter=S.Broken==3;
    return true;
}

void ADuelArena::TickDungeonChains(ADuelFighter* Boss,float DeltaSeconds)
{
    const auto* Mode=GetWorld()->GetAuthGameMode<ADuelMode>();
    auto* Player=Mode?const_cast<ADuelFighter*>(Mode->GetPlayerFighter()):nullptr;
    if(!Mode || !Mode->UsesNorthTower() || Mode->Outcome()!=EDuelOutcome::Fighting || !Boss || !Player ||
        Boss->BossEncounter!=0 || Boss->BossPhase<2 || !Boss->IsAlive() || !Player->IsAlive())
    {if(DungeonChains.Boss.IsValid())ClearDungeonChains();return;}
    if(UGameplayStatics::IsGamePaused(this))return;
    auto& S=DungeonChains;
    if(S.Boss.Get()!=Boss){ClearDungeonChains();S.Boss=Boss;}
    if(Boss->Move==EDuelMove::Awaken || Boss->Move==EDuelMove::Eclipse)return;
    S.Clock+=FMath::Max(0.f,DeltaSeconds);
    using Stage=FDuelDungeonChainsState::EStage;
    if(S.bPendingCounter)
    {
        S.bPendingCounter=false;S.Stage=Stage::Counter;S.Deadline=S.Clock+3.f;S.bAiming=false;
        RemoveChainObject(S.Guide.Get());S.Guide.Reset();
        Orbs.RemoveAll([](const FCrimsonOrb& Shot)
        {auto* V=Shot.Visual.Get();if(V && V->ComponentHasTag(TEXT("DungeonCurseBolt347"))){RemoveChainObject(V);return true;}return false;});
        Boss->DamagePosture(DuelCourt::MaxPosture);Boss->BossFollowup=EDuelMove::Idle;
        Sound(EDuelSound::ParryChime,Boss->GetActorLocation());
    }
    if(S.Stage==Stage::Counter)
    {
        if(S.Clock<S.Deadline)
        {if(Boss->Move==EDuelMove::Stagger)Boss->MoveAge=FMath::Min(Boss->MoveAge,.78f);Boss->BossPause=FMath::Max(Boss->BossPause,.15f);return;}
        if(Boss->Move==EDuelMove::Stagger)
        {const auto T=DuelCourt::Timing(EDuelMove::Stagger,true);Boss->MoveAge=T.Windup+T.Active+T.Recovery-.02f;}
        S.Stage=Stage::Cooldown;S.NextCycle=S.Clock+11.f;RemoveChainObject(S.Emitter.Get());S.Emitter.Reset();return;
    }
    if((S.Stage==Stage::Waiting || S.Stage==Stage::Cooldown) && S.Clock>=S.NextCycle)
    {
        const auto Bounds=CourtBounds(300.);const auto Center=Bounds.GetCenter();
        for(int32 I=0;I<2;++I)S.Points[I]=CourtOrigin()+FVector(Bounds.GetClosestPointTo(Center+FVector2D(0,I?600.:-600.)),135.);
        S.Source=CourtOrigin()+FVector(Bounds.GetClosestPointTo(Center+FVector2D(1500,0)),135.);
        S.Broken=0;S.Stage=Stage::Raising;S.Deadline=S.Clock+1.25f;++S.Cycle;
        S.Feedback=TEXT("悬链正支撑咒力：站到链后，让锁定亮线穿过链心，再横向闪避。");S.FeedbackUntil=S.Deadline;
        BuildDungeonChains();Sound(EDuelSound::Charge,S.Source);
        if(auto* Tower=const_cast<ADuelNorthTower*>(Mode->NorthTower()))Tower->ShowMechanicHint(0);
    }
    else if(S.Stage==Stage::Raising && S.Clock>=S.Deadline)
    {S.Stage=Stage::Active;S.ActiveUntil=S.Clock+24.f;S.NextVolley=S.Clock+.65f;}
    else if(S.Stage==Stage::Active)
    {
        if(S.Clock>=S.ActiveUntil)
        {
            for(auto& C:S.Chains){RemoveChainObject(C.Get());C.Reset();}
            RemoveChainObject(S.Emitter.Get());S.Emitter.Reset();RemoveChainObject(S.Guide.Get());S.Guide.Reset();
            S.Stage=Stage::Cooldown;S.NextCycle=S.Clock+6.f;S.bAiming=false;
            S.Feedback=TEXT("悬链重新绷紧。先摆好位置，下一次再把咒力引到链上。");S.FeedbackUntil=S.Clock+3.f;return;
        }
        const FLinearColor Tint=Boss->SelectedCharacter()==EDuelCharacter::Ema?Bone:Shade;
        if(!S.bAiming && S.Clock>=S.NextVolley)
        {
            S.Aim=Player->GetActorLocation();S.Aim.Z=S.Source.Z;
            const FVector Axis=(S.Aim-S.Source).GetSafeNormal2D(UE_SMALL_NUMBER,FVector::ForwardVector);
            RemoveChainObject(S.Guide.Get());S.Guide=TraceBossSignature(S.Source,S.Aim+Axis*380.f,Tint,1.2f);
            Lane(GroundMark((S.Source+S.Aim)*.5)+FVector(0,0,6),Axis,FVector::Dist2D(S.Source,S.Aim)*.5,55,Tint,1.2f,false,true);
            S.FireAt=S.Clock+1.2f;S.bAiming=true;Sound(EDuelSound::Charge,S.Source);
        }
        else if(S.bAiming && S.Clock>=S.FireAt)
        {
            const FVector Axis=(S.Aim-S.Source).GetSafeNormal2D(UE_SMALL_NUMBER,FVector::ForwardVector);
            auto* Bolt=LaunchThread(Player,S.Source,Axis,34.f,16.f,Tint,1180.f,48.f,3.8f,true,0.f,true,0.f,EDuelMark::Default);
            if(Bolt)
            {
                Orbs.Last().Caster=Boss;
                S.Bolts.RemoveAll([](const auto& V){return !V.IsValid();});
                Bolt->SetWorldRotation(Axis.Rotation());Bolt->ComponentTags.Add(TEXT("DungeonCurseBolt347"));S.Bolts.Add(Bolt);
                auto* Form=StageBossSignature(S.Source,Axis,Boss->SelectedCharacter()==EDuelCharacter::Ema?EDuelMove::OssuaryShears:EDuelMove::HoundShade,Tint,3.8f);
                if(Form){Form->ComponentTags.Add(Boss->SelectedCharacter()==EDuelCharacter::Ema?TEXT("DungeonBoneWing347"):TEXT("DungeonShadowHound347"));Form->AttachToComponent(Bolt,FAttachmentTransformRules::KeepWorldTransform);}
            }
            ++S.Shots;S.bAiming=false;S.NextVolley=S.Clock+3.3f;Sound(EDuelSound::Cut,S.Source);
        }
    }
}

FString ADuelArena::DungeonChainsPrompt() const
{
    const auto& S=DungeonChains;if(!S.Boss.IsValid())return {};
    using Stage=FDuelDungeonChainsState::EStage;
    if(S.Stage==Stage::Counter)return FString::Printf(TEXT("咒力反噬，近身反击！ · %.1f秒"),FMath::Max(0.f,S.Deadline-S.Clock));
    if(S.Stage==Stage::Active)
        return (S.Clock<S.FeedbackUntil?S.Feedback+TEXT("\n"):FString())+FString::Printf(TEXT("引攻击撞断悬链 %d/2 · 剩余%.1f秒%s"),S.Progress(),FMath::Max(0.f,S.ActiveUntil-S.Clock),S.bAiming?TEXT(" · 亮线已锁定，横闪！"):TEXT(" · 站到链后诱导"));
    if(S.Clock<S.FeedbackUntil)return S.Feedback;
    return {};
}
