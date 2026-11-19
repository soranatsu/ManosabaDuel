#include "DuelGame.h"
#include "DuelBossDefense.h"

#include "DuelArena.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

namespace
{
    const FLinearColor BoneTint(1.40f, .87f, .52f, .9f);
    const FLinearColor ShadeTint(1.54f, .11f, .25f, .94f);
    const FLinearColor IceTint(.39f, .90f, 1.65f, .94f);
    const FLinearColor ReflectTint(.78f, .65f, 1.60f, .93f);
    constexpr float FlightVisualLife = 20.f;

    FVector FitInCourt(const ADuelArena& Arena, const FVector& At, double Height, double Clearance = 76.)
    {
        const FVector Court = Arena.CourtOrigin();
        const FVector2D Local = Arena.CourtBounds(Clearance).GetClosestPointTo(FVector2D(At - Court));
        return Court + FVector(Local, Height);
    }

    void RemovePreview(TWeakObjectPtr<USceneComponent>& Effect)
    {
        if (USceneComponent* Visual = Effect.Get()) Visual->DestroyComponent();
        Effect.Reset();
    }
}

void ADuelFighter::PrepareBossSignature(const DuelCourt::FMoveTiming& Spec)
{
    using namespace DuelBossDefense;
    const FVector At = GetActorLocation();
    const FVector Aim = Target->GetActorLocation();
    const FVector Across = FVector::CrossProduct(FVector::UpVector, CastAxis);
    const auto Stage = [&](FMapWave& Wave, const FVector& From, const FVector& To,
        float Time, float Radius, float Lift, const FLinearColor& Tint)
    {
        Wave.Marks = {From, To};
        Wave.Axis = (To - From).GetSafeNormal(UE_SMALL_NUMBER, CastAxis);
        Wave.ImpactTime = Spec.Windup;
        Wave.FlightTime = Time;
        Wave.FlightLift = Lift;
        Wave.Reach = Radius;
        Wave.Preview = Move == EDuelMove::MirrorRicochet
            ? Arena->StageIcicle(From, Wave.Axis, Tint, FlightVisualLife)
            : Arena->StageBossSignature(From, Wave.Axis, Move, Tint, FlightVisualLife);
        Wave.Guide = Arena->TraceBossSignature(From, To, Tint, Spec.Windup);
    };

    switch (Move)
    {
    case EDuelMove::OssuaryShears:
    {
        MapWaves.SetNum(2);
        for (int32 Index = 0; Index < 2; ++Index)
        {
            const double Side = Index == 0 ? -1. : 1.;
            const FVector From = FitInCourt(*Arena, At + Across * Side * 330. - CastAxis * 22., 145.);
            const FVector To = FitInCourt(*Arena, Aim - Across * Side * 166. + CastAxis * 250., 93.);
            Stage(MapWaves[Index], From, To, WingFlight, 72.f + BossPhase * 4.f, 21.f, BoneTint);
        }
        break;
    }
    case EDuelMove::HoundShade:
    {
        const FVector Left = FitInCourt(*Arena, At + Across * OrbitSide * 337. - CastAxis * 16., 78.);
        const FVector Right = FitInCourt(*Arena, At - Across * OrbitSide * 337. - CastAxis * 16., 78.);
        const FVector From = FVector::DistSquared2D(Left, At) >= FVector::DistSquared2D(Right, At) ? Left : Right;
        const FVector AimAxis = (Aim - From).GetSafeNormal2D(UE_SMALL_NUMBER, CastAxis);
        const FVector To = FitInCourt(*Arena, Aim + AimAxis * 300., 84.);
        MapWaves.SetNum(2);
        Stage(MapWaves[0], From, To, ShadeFlight, 72.f, 41.f, ShadeTint);
        break;
    }
    case EDuelMove::MirrorRicochet:
    {
        const FVector From = FitInCourt(*Arena, At + CastAxis * 94., 117.);
        FVector Panel = FitInCourt(*Arena, Aim + CastAxis * 530., 117.);
        if (FVector::DistSquared2D(Panel, Aim) < FMath::Square(290.))
        {
            const FVector Left = FitInCourt(*Arena, Aim + Across * 620., 117.);
            const FVector Right = FitInCourt(*Arena, Aim - Across * 620., 117.);
            Panel = FVector::DistSquared2D(Left, Aim) >= FVector::DistSquared2D(Right, Aim) ? Left : Right;
        }
        MapWaves.SetNum(BossPhase == 3 ? 5 : 3);
        Stage(MapWaves[0], From, Panel, MirrorFirstFlight, 61.f, 23.f, IceTint);
        MapWaves[1].Marks.Add(Panel);
        MapWaves[1].Preview = Arena->StageBossSignature(Panel, -(Panel - From).GetSafeNormal2D(), Move,
            IceTint * .80f, FlightVisualLife);
        break;
    }
    case EDuelMove::MirrorRiposte:
        MapWaves.SetNum(1);
        CastOrigin = At + CastAxis * 116. + FVector(0., 0., 38.);
        break;
    default:
        return;
    }
    Arena->Sound(EDuelSound::Charge, At);
}

FVector ADuelFighter::SignaturePosition(const FMapWave& Wave, float Age)
{
    const float T = FMath::Clamp((Age - Wave.ImpactTime) / Wave.FlightTime, 0.f, 1.f);
    return FMath::Lerp(Wave.Marks[0], Wave.Marks[1], T) + FVector(0., 0., Wave.FlightLift * FMath::Sin(PI * T));
}

void ADuelFighter::SignatureContact(const FVector& From, const FVector& To, float Radius, float Damage,
    bool bProjectile, const FLinearColor& Tint, bool& bContacted)
{
    if (bContacted) return;
    const FVector Center = Target->GetActorLocation();
    const UCapsuleComponent* Capsule = Target->GetCapsuleComponent();
    const FVector Up(0., 0., Capsule->GetScaledCapsuleHalfHeight_WithoutHemisphere());
    const double Limit = Radius + Capsule->GetScaledCapsuleRadius();
    FVector Contact, BodyPoint;
    FMath::SegmentDistToSegmentSafe(From, To, Center - Up, Center + Up, Contact, BodyPoint);
    if (FVector::DistSquared(Contact, BodyPoint) > FMath::Square(Limit)) return;

    FCollisionQueryParams Sight(SCENE_QUERY_STAT(BossSignatureSight), false, this);
    Sight.AddIgnoredActor(Target.Get());
    FHitResult Obstruction;
    if (GetWorld()->LineTraceSingleByChannel(Obstruction, From, BodyPoint, ECC_Visibility, Sight)) return;

    const FVector Incoming = (To - From).GetSafeNormal2D(UE_SMALL_NUMBER, ActionDirection);
    const FVector Source = bProjectile ? From - Incoming * Limit : GetActorLocation();
    bContacted = true;
    if (Target->ReceiveDuelDamage(Damage, Source, true, &Contact, bProjectile, true, true))
    {
        Arena->SpellContact(Contact, Incoming, Tint, Damage >= 43.f);
    }
}

void ADuelFighter::TickBossSignature(float LastAge, const DuelCourt::FMoveTiming& Spec)
{
    using namespace DuelBossDefense;
    const EDuelMove Current = Move;
    const auto Crossed = [&](float At) { return LastAge < At && MoveAge >= At; };
    const auto Fly = [&](FMapWave& Wave, const FLinearColor& Tint, float Damage, bool& bContacted)
    {
        const float End = Wave.ImpactTime + Wave.FlightTime;
        if (MoveAge < Wave.ImpactTime || LastAge >= End) return;
        const FVector From = SignaturePosition(Wave, LastAge);
        const FVector To = SignaturePosition(Wave, MoveAge);
        if (USceneComponent* Shape = Wave.Preview.Get())
        {
            Shape->SetWorldLocationAndRotation(To, (To - From).GetSafeNormal(UE_SMALL_NUMBER, Wave.Axis).Rotation());
        }
        if (MoveAge >= End) RemovePreview(Wave.Preview);
        Arena->HitDungeonChains(*this,From,To,Wave.Reach);
        SignatureContact(From, To, Wave.Reach, Damage, true, Tint, bContacted);
    };
    const auto LockBody = [&](FMapWave& Wave, float Starts, float Duration, float MaxStep, float HeldDistance,
        float Radius, float TipLead, const FLinearColor& Tint)
    {
        const FVector At = GetActorLocation();
        const FVector Aim = Target->GetActorLocation();
        const FVector Axis = (Aim - At).GetSafeNormal2D(UE_SMALL_NUMBER, CastAxis);
        const double Step = FMath::Clamp(FVector::Dist2D(At, Aim) - HeldDistance, 0., static_cast<double>(MaxStep));
        const FVector Goal = FitInCourt(*Arena, At + Axis * Step, At.Z - Arena->CourtOrigin().Z,
            GetCapsuleComponent()->GetScaledCapsuleRadius());
        SealCenters[0] = At;
        SealCenters[1] = SweepNorthCourtTravel(At, Goal);
        Wave.Marks = {At + Axis * 64., SealCenters[1] + Axis * TipLead};
        Wave.ImpactTime = Starts;
        Wave.FlightTime = Duration;
        Wave.Reach = Radius;
        Wave.Axis = Axis;
        Wave.Guide = Arena->TraceBossSignature(Wave.Marks[0], Wave.Marks[1], Tint,
            FMath::Max(.035f, Starts - MoveAge));
        ActionDirection = Axis;
        SetActorRotation(Axis.Rotation());
    };
    const auto Lunge = [&](FMapWave& Wave, const FLinearColor& Tint, float Damage, float FinishTip)
    {
        if (Wave.Marks.IsEmpty() || MoveAge < Wave.ImpactTime || LastAge >= Wave.ImpactTime + Wave.FlightTime) return;
        const auto Fraction = [&](float Age)
        {
            return FMath::Clamp((Age - Wave.ImpactTime) / Wave.FlightTime, 0.f, 1.f);
        };
        const float Before = Fraction(LastAge);
        const float After = Fraction(MoveAge);
        const FVector At = GetActorLocation();
        AddActorWorldOffset((SealCenters[1] - SealCenters[0]) * (After - Before), true);
        ClampToCourt();
        const FVector From = At + Wave.Axis * FMath::Lerp(64.f, FinishTip, Before);
        const FVector To = GetActorLocation() + Wave.Axis * FMath::Lerp(64.f, FinishTip, After);
        Arena->HitDungeonChains(*this,From,To,Wave.Reach);
        SignatureContact(From, To, Wave.Reach, Damage, false, Tint, Wave.bHitDelivered);
    };

    switch (Current)
    {
    case EDuelMove::OssuaryShears:
        if (Crossed(Spec.Windup))
        {
            Arena->Sound(EDuelSound::Cut, GetActorLocation());
            Arena->Flash(GetActorLocation() + FVector(0., 0., 67.), BoneTint, 8200.f, .21f, 475.f);
        }
        for (FMapWave& Wave : MapWaves)
        {
            Fly(Wave, BoneTint, 31.f + 5.f * BossPhase, bHitDelivered);
            if (bCombatOver || Move != Current) return;
        }
        break;
    case EDuelMove::HoundShade:
    {
        const float LungeAt = Spec.Windup + ShadeBodyStart;
        if (Crossed(Spec.Windup))
            Arena->Sound(EDuelSound::Cut, MapWaves[0].Marks[0]);
        Fly(MapWaves[0], ShadeTint, 23.f + 4.f * BossPhase, MapWaves[0].bHitDelivered);
        if (bCombatOver || Move != Current) return;
        if (Crossed(LungeAt - ShadeBodyAimLead))
        {
            LockBody(MapWaves[1], LungeAt, ShadeBodyFlight, 790.f + 72.f * BossPhase, 68.f,
                94.f + 4.f * BossPhase, 88.f, ShadeTint);
            Arena->Sound(EDuelSound::Charge, GetActorLocation());
            Arena->Flash(GetActorLocation(), ShadeTint, 6600.f, ShadeBodyAimLead, 490.f);
        }
        if (Crossed(LungeAt))
        {
            Arena->Thrust(GetActorLocation(), ActionDirection, 211.f, ShadeTint, true, EDuelMark::HoundPounce);
            Arena->Sound(EDuelSound::HeavyImpact, GetActorLocation());
        }
        Lunge(MapWaves[1], ShadeTint, 37.f + 5.f * BossPhase, 88.f);
        break;
    }
    case EDuelMove::MirrorRicochet:
    {
        const float ReturnAt = Spec.Windup + MirrorReturnStart;
        const FVector Panel = MapWaves[1].Marks[0];
        if (Crossed(Spec.Windup))
        {
            Arena->Sound(EDuelSound::Cut, GetActorLocation());
            Arena->Flash(MapWaves[0].Marks[0], IceTint, 5400.f, .18f, 375.f);
        }
        Fly(MapWaves[0], IceTint, 31.f + BossPhase * 3.f, MapWaves[0].bHitDelivered);
        if (bCombatOver || Move != Current) return;
        if (Crossed(Spec.Windup + MirrorFirstFlight))
        {
            Arena->Flash(Panel, IceTint, 9200.f, .25f, 510.f);
            Arena->Burst(Panel, IceTint, 25, 300.f, true);
            Arena->Sound(EDuelSound::Block, Panel);
        }
        if (Crossed(ReturnAt - MirrorAimLead))
        {
            const FVector Aim = Target->GetActorLocation();
            const FVector Axis = (Aim - Panel).GetSafeNormal2D(UE_SMALL_NUMBER, -CastAxis);
            if (USceneComponent* Mirror = MapWaves[1].Preview.Get()) Mirror->SetWorldRotation(Axis.Rotation());
            const FVector Across = FVector::CrossProduct(FVector::UpVector, Axis);
            for (int32 Index = 2; Index < MapWaves.Num(); ++Index)
            {
                const float Fork = Index == 3 ? -1.f : Index == 4 ? 1.f : 0.f;
                const FVector Ray = Axis.RotateAngleAxis(Fork * 8.5f, FVector::UpVector);
                const FVector From = Panel + Across * Fork * 43.f;
                const double RayDistance = FMath::Min(2600., FVector::Dist2D(Panel, Aim) + 385.);
                const FVector To = FitInCourt(*Arena, From + Ray * RayDistance, Panel.Z - Arena->CourtOrigin().Z);
                FMapWave& Wave = MapWaves[Index];
                Wave.Marks = {From, To};
                Wave.Axis = (To - From).GetSafeNormal(UE_SMALL_NUMBER, Ray);
                Wave.ImpactTime = ReturnAt;
                Wave.FlightTime = MirrorReturnFlight;
                Wave.Reach = Fork == 0.f ? 69.f : 47.f;
                if (Index == 2) Wave.Guide = Arena->TraceBossSignature(From, To, ReflectTint, ReturnAt - MoveAge);
                Wave.Preview = Arena->StageIcicle(From, Wave.Axis, ReflectTint, FlightVisualLife);
            }
            Arena->Sound(EDuelSound::FrostRecall, Panel);
        }
        if (Crossed(ReturnAt))
        {
            Arena->Flash(Panel, ReflectTint, 9800.f, .24f, 500.f);
            Arena->Sound(EDuelSound::Cut, Panel);
        }
        for (int32 Index = 2; Index < MapWaves.Num() && !MapWaves[Index].Marks.IsEmpty(); ++Index)
        {
            Fly(MapWaves[Index], ReflectTint, 38.f + 4.f * BossPhase, MapWaves[2].bHitDelivered);
            if (bCombatOver || Move != Current) return;
        }
        break;
    }
    case EDuelMove::MirrorRiposte:
    {
        FMapWave& Wave = MapWaves[0];
        if (Crossed(RiposteGuardOpens))
        {
            Wave.Preview = Arena->StageBossSignature(CastOrigin, CastAxis, Move, IceTint, FlightVisualLife);
            Arena->Sound(EDuelSound::ParryChime, CastOrigin);
        }
        if (Crossed(RiposteGuardCloses))
        {
            RemovePreview(Wave.Preview);
            if (bFeintTriggered)
            {
                const FVector At = GetActorLocation();
                const FVector Aim = Target->GetActorLocation();
                const FVector Axis = (Aim - At).GetSafeNormal2D(UE_SMALL_NUMBER, CastAxis);
                const FVector From = FitInCourt(*Arena, At + Axis * 91., 108.);
                const double Distance = FMath::Min(1230., FVector::Dist2D(At, Aim) + 295.);
                const FVector To = FitInCourt(*Arena, At + Axis * Distance, 108.);
                Wave.Marks = {From, To};
                Wave.Axis = (To - From).GetSafeNormal(UE_SMALL_NUMBER, Axis);
                Wave.Reach = 67.f;
                Wave.ImpactTime = Spec.Windup;
                Wave.FlightTime = RiposteFlight;
                Wave.Preview = Arena->StageIcicle(From, Wave.Axis, ReflectTint, FlightVisualLife);
                Wave.Guide = Arena->TraceBossSignature(From, To, ReflectTint, Spec.Windup - MoveAge);
                ActionDirection = Axis;
                SetActorRotation(Axis.Rotation());
            }
            else
            {
                Arena->Burst(CastOrigin, IceTint, 13, 205.f, true);
                LockBody(Wave, Spec.Windup, RiposteStep, 257.f, 155.f, 58.f, 249.f, IceTint);
            }
            Arena->Sound(bFeintTriggered ? EDuelSound::FrostRecall : EDuelSound::Charge, CastOrigin);
        }
        if (Crossed(Spec.Windup))
        {
            const FVector From = Wave.Marks[0];
            Arena->Thrust(From, Wave.Axis, 269.f, bFeintTriggered ? ReflectTint : IceTint, true, EDuelMark::FrostThrust);
            Arena->Sound(EDuelSound::Cut, From);
        }
        if (bFeintTriggered && !Wave.Marks.IsEmpty())
            Fly(Wave, ReflectTint, 36.f + 3.f * BossPhase, Wave.bHitDelivered);
        else
            Lunge(Wave, IceTint, 32.f + 4.f * BossPhase, 249.f);
        break;
    }
    default:
        break;
    }
}

bool ADuelFighter::TryMirrorRiposte(const FVector& Toward, const FVector& Contact)
{
    using namespace DuelBossDefense;
    if (bFeintTriggered || MoveAge < RiposteGuardOpens || MoveAge >= RiposteGuardCloses ||
        FVector::DotProduct(Toward, CastAxis) < .48f)
    {
        return false;
    }
    bFeintTriggered = true;
    if (!MapWaves.IsEmpty()) RemovePreview(MapWaves[0].Preview);
    Arena->Sound(EDuelSound::Block, Contact);
    Arena->Burst(CastOrigin, IceTint, 32, 465.f, true);
    Arena->Flash(CastOrigin, ReflectTint, 11200.f, .24f, 480.f);
    return true;
}
