#include "DuelNorthNarrative.h"

#include "DuelGame.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
    const FName FollowFurnitureTag(TEXT("NorthStandingFurniture"));

    bool IsFollowFurniture(const UPrimitiveComponent* Component)
    {
        return Component && Component->ComponentHasTag(FollowFurnitureTag);
    }

    FCollisionQueryParams FollowQuery(const ADuelFighter* Companion, const ADuelFighter* Player)
    {
        FCollisionQueryParams Query(SCENE_QUERY_STAT(NorthCompanionFollow), false, Companion);
        Query.AddIgnoredActor(Player);
        return Query;
    }
}

void ADuelNorthNarrative::ResetFollow()
{
    FollowTrail.Reset();
    FollowDetour.Reset();
    FollowRepathDelay = 0.f;
}

bool ADuelNorthNarrative::FindFollowFloor(const FVector& Approximate, FVector& Standing) const
{
    if (!IsValid(RealCompanion) || !GetWorld()) return false;
    const auto* Capsule = RealCompanion->GetCapsuleComponent();
    const float Half = Capsule->GetScaledCapsuleHalfHeight();
    const auto Query = FollowQuery(RealCompanion, PlayerRef);
    FHitResult Ground;
    if (!GetWorld()->LineTraceSingleByChannel(Ground, Approximate + FVector(0, 0, 48),
        Approximate - FVector(0, 0, Half + 144.f), ECC_Pawn, Query) ||
        Ground.ImpactNormal.Z < .7f || IsFollowFurniture(Ground.GetComponent()))
        return false;
    Standing = FVector(Approximate.X, Approximate.Y, Ground.ImpactPoint.Z + Half + 2.f);
    if (FMath::Abs(Standing.Z - Approximate.Z) > 120.f) return false;
    return !GetWorld()->OverlapBlockingTestByChannel(Standing, FQuat::Identity, ECC_Pawn,
        FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius() + 2.f, Half), Query);
}

bool ADuelNorthNarrative::FindFollowPlacement(const FVector& Origin, FVector& Standing, bool bRecovery) const
{
    if (!IsValid(RealCompanion)) return false;
    auto Query = FollowQuery(RealCompanion, PlayerRef);
    const auto* Capsule = RealCompanion->GetCapsuleComponent();
    const auto Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight() - 1.f);
    if (bRecovery)
    {
        // A local correction may leave overlapping furniture, but cannot cross a wall or a closed gate.
        TArray<FOverlapResult> Nearby;
        GetWorld()->OverlapMultiByChannel(Nearby, Origin, FQuat::Identity, ECC_Pawn,
            FCollisionShape::MakeSphere(500.f), Query);
        for (const auto& Hit : Nearby)
            if (IsFollowFurniture(Hit.GetComponent())) Query.AddIgnoredComponent(Hit.GetComponent());
    }
    for (int32 Ring = 0; Ring <= 12; ++Ring)
    {
        const int32 Samples = Ring == 0 ? 1 : 16;
        for (int32 I = 0; I < Samples; ++I)
        {
            const double Angle = I * 2. * PI / Samples;
            const FVector Candidate = Origin + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * (Ring * 32.f);
            FVector Floor;
            if (!FindFollowFloor(Candidate, Floor)) continue;
            FHitResult Barrier;
            if (bRecovery && GetWorld()->SweepSingleByChannel(Barrier, Origin, Floor, FQuat::Identity, ECC_Pawn, Shape, Query))
                continue;
            Standing = Floor;
            return true;
        }
    }
    return false;
}

bool ADuelNorthNarrative::BuildFollowDetour(const FVector& From, const FVector& Target, const AActor* FurnitureOwner)
{
    FollowDetour.Reset();
    if (!FurnitureOwner || !IsValid(RealCompanion)) return false;
    const auto* Capsule = RealCompanion->GetCapsuleComponent();
    const float Radius = Capsule->GetScaledCapsuleRadius();
    const float Half = Capsule->GetScaledCapsuleHalfHeight();
    const auto Query = FollowQuery(RealCompanion, PlayerRef);
    const auto Shape = FCollisionShape::MakeCapsule(Radius + 2.f, Half - 1.f);
    FVector Goal;
    const FVector NearTarget = From + (Target - From).GetClampedToMaxSize(650.f);
    if (!FindFollowFloor(NearTarget, Goal) || FMath::Abs(Goal.Z - From.Z) > 8.f) return false;

    struct FNode { FVector At; double Cost = TNumericLimits<double>::Max(); int32 Parent = INDEX_NONE; bool bClosed = false; };
    TArray<FNode> Nodes;
    Nodes.Add({From, 0.});
    Nodes.Add({Goal});
    TArray<UBoxComponent*> Boxes;
    for (auto* Box : TInlineComponentArray<UBoxComponent*>(FurnitureOwner))
    {
        if (!IsFollowFurniture(Box) || !Box->IsQueryCollisionEnabled()) continue;
        const FVector Center = Box->GetComponentLocation();
        const auto Extent = Box->GetScaledBoxExtent();
        if (Center.Z + Extent.Z < From.Z - Half + 4.f || Center.Z - Extent.Z > From.Z + Half) continue;
        if (FVector::DistSquared2D(Center, FMath::ClosestPointOnSegment(Center, From, Goal)) < FMath::Square(380.f))
            Boxes.Add(Box);
    }
    Boxes.Sort([&](const UBoxComponent& A, const UBoxComponent& B)
    {
        return FVector::DistSquared2D(A.GetComponentLocation(), From) < FVector::DistSquared2D(B.GetComponentLocation(), From);
    });
    // Only nearby furniture needs corner nodes; every connection still sweeps against all real collision.
    if (Boxes.Num() > 24) Boxes.SetNum(24, EAllowShrinking::No);
    for (const auto* Box : Boxes)
    {
        const FVector Extent = Box->GetScaledBoxExtent() + FVector(Radius + 18.f, Radius + 18.f, 0);
        for (int32 X : {-1, 1}) for (int32 Y : {-1, 1})
        {
            FVector Corner = Box->GetComponentLocation() + Box->GetComponentQuat().RotateVector(FVector(X * Extent.X, Y * Extent.Y, 0));
            Corner.Z = From.Z;
            FVector Floor;
            if (FindFollowFloor(Corner, Floor) && FMath::Abs(Floor.Z - From.Z) < 8.f)
                Nodes.Add({Floor});
        }
    }
    const auto ClearEdge = [&](const FVector& A, const FVector& B)
    {
        FHitResult Hit;
        if (GetWorld()->SweepSingleByChannel(Hit, A, B, FQuat::Identity, ECC_Pawn, Shape, Query)) return false;
        const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector::Dist2D(A, B) / 80.));
        for (int32 I = 1; I <= Steps; ++I)
        {
            const FVector Along = FMath::Lerp(A, B, double(I) / Steps);
            FVector Floor;
            if (!FindFollowFloor(Along, Floor) || FMath::Abs(Floor.Z - Along.Z) > 8.f) return false;
        }
        return true;
    };
    for (int32 Search = 0; Search < Nodes.Num(); ++Search)
    {
        int32 Best = INDEX_NONE;
        double BestScore = TNumericLimits<double>::Max();
        for (int32 I = 0; I < Nodes.Num(); ++I)
        {
            const double Score = Nodes[I].Cost + FVector::Dist2D(Nodes[I].At, Goal);
            if (!Nodes[I].bClosed && Score < BestScore) { Best = I; BestScore = Score; }
        }
        if (Best == INDEX_NONE) break;
        if (Best == 1)
        {
            for (int32 I = 1; I != 0; I = Nodes[I].Parent) FollowDetour.Insert(Nodes[I].At, 0);
            FollowDetourTarget = Target;
            return true;
        }
        Nodes[Best].bClosed = true;
        for (int32 I = 1; I < Nodes.Num(); ++I)
        {
            if (Nodes[I].bClosed) continue;
            const double Cost = Nodes[Best].Cost + FVector::Dist2D(Nodes[Best].At, Nodes[I].At);
            if (Cost < Nodes[I].Cost && ClearEdge(Nodes[Best].At, Nodes[I].At))
            {
                Nodes[I].Cost = Cost;
                Nodes[I].Parent = Best;
            }
        }
    }
    return false;
}

void ADuelNorthNarrative::Follow(ADuelFighter& Player, float DeltaSeconds)
{
    if (!IsValid(RealCompanion)) return;
    auto* Motion = RealCompanion->GetCharacterMovement();
    const auto* Capsule = RealCompanion->GetCapsuleComponent();
    const float Radius = Capsule->GetScaledCapsuleRadius(), Half = Capsule->GetScaledCapsuleHalfHeight();
    const auto Query = FollowQuery(RealCompanion, &Player);
    const auto Stop = [&]()
    {
        RealCompanion->ConsumeMovementInputVector();
        Motion->StopMovementImmediately();
    };
    FVector Here = RealCompanion->GetActorLocation();
    FHitResult Support;
    const bool bFurnitureSupport = GetWorld()->LineTraceSingleByChannel(Support, Here,
        Here - FVector(0, 0, Half + 10.f), ECC_Pawn, Query) && IsFollowFurniture(Support.GetComponent());
    TArray<FOverlapResult> Overlaps;
    GetWorld()->OverlapMultiByChannel(Overlaps, Here, FQuat::Identity, ECC_Pawn,
        FCollisionShape::MakeCapsule(Radius - 1.f, Half - 2.f), Query);
    const bool bInsideFurniture = Overlaps.ContainsByPredicate([](const FOverlapResult& Hit)
    {
        return Hit.bBlockingHit && IsFollowFurniture(Hit.GetComponent());
    });
    if (bFurnitureSupport || bInsideFurniture)
    {
        FVector Recovered;
        if (!FindFollowPlacement(Here, Recovered, true)) { Stop(); return; }
        Stop();
        RealCompanion->SetActorLocation(Recovered, false, nullptr, ETeleportType::TeleportPhysics);
        Motion->SetMovementMode(MOVE_Walking);
        Motion->bForceNextFloorCheck = true;
        FollowDetour.Reset();
        FollowRepathDelay = 0.f;
        Here = Recovered;
    }

    const FVector PlayerAt = Player.GetActorLocation();
    FVector PlayerFloor;
    if (Player.GetCharacterMovement()->IsMovingOnGround() && FindFollowFloor(PlayerAt, PlayerFloor) &&
        FMath::Abs(PlayerFloor.Z - PlayerAt.Z) < 30.f && FollowTrail.Num() < 4096 &&
        (FollowTrail.IsEmpty() || FVector::DistSquared2D(FollowTrail.Last(), PlayerFloor) > FMath::Square(55.f) ||
            FMath::Abs(FollowTrail.Last().Z - PlayerFloor.Z) > 25.f))
        FollowTrail.Add(PlayerFloor);
    while (FollowTrail.Num() > 1 && FVector::DistSquared2D(Here, FollowTrail[0]) < FMath::Square(45.f) &&
        FMath::Abs(Here.Z - FollowTrail[0].Z) < 70.f)
    {
        FollowTrail.RemoveAt(0, 1, EAllowShrinking::No);
        FollowDetour.Reset();
    }
    if (FollowTrail.IsEmpty() || (FollowTrail.Num() <= 1 && FollowDetour.IsEmpty() &&
        FVector::DistSquared2D(Here, PlayerAt) < FMath::Square(155.f)))
    {
        Stop();
        return;
    }
    const FVector Target = FollowTrail[0];
    if (!FollowDetour.IsEmpty() && !Target.Equals(FollowDetourTarget, 10.f)) FollowDetour.Reset();
    while (!FollowDetour.IsEmpty() && FVector::DistSquared2D(Here, FollowDetour[0]) < FMath::Square(12.f))
        FollowDetour.RemoveAt(0, 1, EAllowShrinking::No);
    FollowRepathDelay = FMath::Max(0.f, FollowRepathDelay - DeltaSeconds);
    if (FollowDetour.IsEmpty())
    {
        FHitResult Obstacle;
        if (GetWorld()->SweepSingleByChannel(Obstacle, Here, Target, FQuat::Identity, ECC_Pawn,
            FCollisionShape::MakeCapsule(Radius + 2.f, Half - 1.f), Query) && IsFollowFurniture(Obstacle.GetComponent()))
        {
            if (FollowRepathDelay <= 0.f)
            {
                BuildFollowDetour(Here, Target, Obstacle.GetActor());
                FollowRepathDelay = .5f;
            }
            if (FollowDetour.IsEmpty()) { Stop(); return; }
        }
    }
    const FVector Aim = FollowDetour.IsEmpty() ? Target : FollowDetour[0];
    const FVector Direction = (Aim - Here).GetSafeNormal2D();
    RealCompanion->SetActorRotation(FMath::RInterpTo(RealCompanion->GetActorRotation(), Direction.Rotation(), DeltaSeconds, 9.f));
    Motion->MaxWalkSpeed = FMath::GetMappedRangeValueClamped(FVector2D(180.f, 650.f), FVector2D(455.f, 720.f),
        static_cast<float>(FVector::Dist2D(Here, PlayerAt)));
    if (!FollowDetour.IsEmpty())
        Motion->MaxWalkSpeed = FMath::Min(Motion->MaxWalkSpeed, FMath::Max(90.f, float(FVector::Dist2D(Here, Aim)) * 3.f));
    RealCompanion->AddMovementInput(Direction, 1.f, true);
}
