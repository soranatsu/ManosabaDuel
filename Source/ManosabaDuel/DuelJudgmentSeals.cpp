#include "DuelArena.h"
#include "DuelGame.h"
#include "DuelGeometry.h"
#include "DuelJudgmentRelief.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include <cfloat>

bool ADuelArena::PlaceJudgmentSeals()
{
    const auto* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    const auto* Player = Mode ? Mode->GetPlayerFighter() : nullptr;
    if (!Player || !CourtBoss.IsValid()) return false;
    const FVector Origin = CourtOrigin();
    const FBox2D Bounds = CourtBounds(250.);
    const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(JudgmentPlacement347), false, this);
    Query.AddIgnoredActor(Player);
    Query.AddIgnoredActor(CourtBoss.Get());
    const float Height = Capsule->GetScaledCapsuleHalfHeight();
    const auto PathClear = [&](const FVector& A, const FVector& B)
    {
        FHitResult Hit;
        return !GetWorld()->SweepSingleByChannel(Hit, A, B, FQuat::Identity, Capsule->GetCollisionObjectType(),
            FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Height), Query);
    };
    const auto GroundPoint = [&](const FVector2D& Local, FVector& Out)
    {
        if (!Bounds.IsInside(Local)) return false;
        FVector At = Origin + FVector(Local, 0.);
        if (FVector::DistSquared2D(At, Player->GetActorLocation()) < FMath::Square(230.) ||
            FVector::DistSquared2D(At, CourtBoss->GetActorLocation()) < FMath::Square(250.)) return false;
        FHitResult Floor;
        if (!GetWorld()->LineTraceSingleByChannel(Floor, At + FVector(0, 0, 420), At - FVector(0, 0, 80), ECC_Visibility, Query) ||
            Floor.ImpactNormal.Z < .95 || FMath::Abs(Floor.ImpactPoint.Z - Origin.Z) > 12.) return false;
        Out = FVector(At.X, At.Y, Floor.ImpactPoint.Z + 125.);
        const FVector WalkAt(At.X, At.Y, Floor.ImpactPoint.Z + Height + 2.);
        return PathClear(Player->GetActorLocation(), WalkAt);
    };
    const auto TripleClear = [&](const TStaticArray<FVector, 3>& Points)
    {
        double MaxPlayer = 0., Sum = 0., Min = DBL_MAX;
        for (int32 I = 0; I < 3; ++I)
        {
            MaxPlayer = FMath::Max(MaxPlayer, FVector::Dist2D(Player->GetActorLocation(), Points[I]));
            for (int32 J = I + 1; J < 3; ++J)
            {
                const double D = FVector::Dist2D(Points[I], Points[J]);
                if (D < 380. || D > 1300.) return false;
                Sum += D; Min = FMath::Min(Min, D);
                const FVector A = Points[I] + FVector(0, 0, Height + 2. - 125.);
                const FVector B = Points[J] + FVector(0, 0, Height + 2. - 125.);
                if (!PathClear(A, B)) return false;
            }
        }
        // Even the worst randomly ordered tour fits the complete 12.5 s window at ordinary walking speed.
        return MaxPlayer + Sum - Min <= 3300.;
    };
    FRandomStream Random347(int32(JudgmentSeed ^ (uint32(CourtCycle) * 0x9e3779b9u)));
    const FVector2D PlayerAt(Player->GetActorLocation() - Origin);
    const FVector2D Middle = Bounds.GetCenter();
    for (int32 Trial = 0; Trial < 128; ++Trial)
    {
        const FVector2D Center = PlayerAt * .60 + Middle * .40 + FVector2D(Random347.FRandRange(-95, 95), Random347.FRandRange(-95, 95));
        const float Angle = Random347.FRandRange(-PI, PI);
        TStaticArray<FVector, 3> Candidate;
        bool bValid = true;
        for (int32 I = 0; I < 3 && bValid; ++I)
        {
            const float Theta = Angle + I * 2.f * PI / 3.f + Random347.FRandRange(-.17f, .17f);
            const float Radius = Random347.FRandRange(425.f, 570.f);
            bValid = GroundPoint(Center + FVector2D(FMath::Cos(Theta), FMath::Sin(Theta)) * Radius, Candidate[I]);
        }
        if (bValid && TripleClear(Candidate)) { CourtPoints = Candidate; return true; }
    }
    // Static furnishings can invalidate a random triangle. Search actual reachable floor cells, never clamp targets together.
    TArray<FVector> Grid;
    for (int32 X = 0; X < 7; ++X)
        for (int32 Y = 0; Y < 5; ++Y)
        {
            FVector Point;
            const FVector2D Local(FMath::Lerp(Bounds.Min.X + 25., Bounds.Max.X - 25., X / 6.),
                FMath::Lerp(Bounds.Min.Y + 25., Bounds.Max.Y - 25., Y / 4.));
            if (GroundPoint(Local, Point)) Grid.Add(Point);
        }
    Grid.Sort([&](const FVector& A, const FVector& B)
        { return FVector::DistSquared2D(A, Player->GetActorLocation()) < FVector::DistSquared2D(B, Player->GetActorLocation()); });
    for (int32 A = 0; A < Grid.Num(); ++A)
        for (int32 B = A + 1; B < Grid.Num(); ++B)
            for (int32 C = B + 1; C < Grid.Num(); ++C)
            {
                const TStaticArray<FVector, 3> Candidate{Grid[A], Grid[B], Grid[C]};
                if (TripleClear(Candidate)) { CourtPoints = Candidate; return true; }
            }
    UE_LOG(LogTemp, Warning, TEXT("North judgment placement postponed: no safe reachable nonoverlapping triple, seed=%u cycle=%d"), JudgmentSeed, CourtCycle);
    return false;
}

void ADuelArena::BuildJudgmentSeals()
{
    using namespace DuelGeometry;
    const auto* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    const auto* Player = Mode ? Mode->GetPlayerFighter() : nullptr;
    if (!Player || !CourtBoss.IsValid()) return;
    auto* Metal = ColorMaterial(this, FLinearColor(.24f, .31f, .39f), false, .24f, .82f);
    auto* GoldMetal = ColorMaterial(this, FLinearColor(.85f, .56f, .18f), false, .23f, .83f);
    auto* ActiveGlow = ColorMaterial(this, FLinearColor(2.2f, 1.30f, .32f), true);
    auto* DormantGlow = ColorMaterial(this, FLinearColor(.055f, .22f, .39f), true);
    const bool bSelect = CourtStage == ECourtStage::Warning || CourtStage == ECourtStage::Open;
    const int32 Active = CourtOrder[CourtProgress];
    const TCHAR* ShapeTags[] = {TEXT("JudgmentEye347"), TEXT("JudgmentScales347"), TEXT("JudgmentCrown347")};
    const TCHAR* Numbers[] = {TEXT("I"), TEXT("II"), TEXT("III")};
    for (int32 I = 0; I < 3; ++I)
    {
        bool bCompleted = false;
        for (int32 J = 0; J < CourtProgress; ++J) bCompleted |= CourtOrder[J] == I;
        if (bCompleted) { CourtObjects.Add(nullptr); continue; }
        auto* Root = Joint(this, SceneRoot, FVector::ZeroVector);
        Root->SetWorldLocationAndRotation(CourtPoints[I], (Player->GetActorLocation() - CourtPoints[I]).GetSafeNormal2D().Rotation());
        Root->ComponentTags.Add(ShapeTags[I]);
        Root->ComponentTags.Add(TEXT("JudgmentSeal347"));
        CourtObjects.Add(Root);
        const bool bActive = bSelect && I == Active;
        auto* Frame = bActive ? GoldMetal : Metal;
        auto* Glow = bActive ? ActiveGlow : DormantGlow;
        if (I == 0)
        {
            // Almond-shaped eye frame. Its centre is deliberately open for the double-sided relief.
            for (float Side : {-1.f, 1.f})
                for (int32 E = 0; E < 24; ++E)
                {
                    const auto Point = [&](int32 K) { const float T = K * PI / 24.f; return FVector(0, FMath::Cos(T) * 99, Side * FMath::Sin(T) * 69); };
                    Rod(this, Root, Point(E), Point(E + 1), 4.f, Frame);
                }
            for (float Side : {-1.f, 1.f})
                Shape(this, Root, EShape::Cone, FVector(0, Side * 108, 0), FVector(17, 17, 33), Glow, FRotator(0, 0, 90));
        }
        else if (I == 1)
        {
            // Two hanging pans and an arched balance beam have a wide, unmistakable silhouette.
            Rod(this, Root, FVector(0, -112, 85), FVector(0, 112, 85), 5.f, Frame);
            for (float Side : {-1.f, 1.f})
            {
                Rod(this, Root, FVector(0, Side * 62, -79), FVector(0, Side * 62, 85), 4.f, Frame);
                Rod(this, Root, FVector(0, Side * 100, 81), FVector(0, Side * 100, -40), 2.5f, Frame);
                Shape(this, Root, EShape::Tube, FVector(0, Side * 100, -45), FVector(51, 51, 9), Frame);
                Shape(this, Root, EShape::Ball, FVector(0, Side * 100, -39), FVector(13), Glow);
            }
            Shape(this, Root, EShape::Box, FVector(0, 0, -82), FVector(32, 155, 12), Frame);
        }
        else
        {
            // A five-point coronet is tall rather than eye-shaped or balance-shaped.
            Rod(this, Root, FVector(0, -94, -72), FVector(0, 94, -72), 6.f, Frame);
            for (float Side : {-1.f, 1.f}) Rod(this, Root, FVector(0, Side * 94, -72), FVector(0, Side * 92, 70), 4.5f, Frame);
            for (int32 Point = 0; Point < 5; ++Point)
            {
                const float Y = -80.f + Point * 40.f;
                const float Top = 89.f + (2 - FMath::Abs(Point - 2)) * 19.f;
                Rod(this, Root, FVector(0, Y - 17, 69), FVector(0, Y, Top), 4.f, Frame);
                Rod(this, Root, FVector(0, Y, Top), FVector(0, Y + 17, 69), 4.f, Frame);
                Shape(this, Root, EShape::Ball, FVector(0, Y, Top), FVector(12), Glow);
            }
        }
        DuelJudgmentRelief::Attach(this, Root, I);
        if (bActive)
        {
            Root->ComponentTags.Add(TEXT("JudgmentActiveTarget347"));
            auto* Orbit = Joint(this, Root, FVector::ZeroVector);
            Orbit->ComponentTags.Add(TEXT("JudgmentActiveOrbit347"));
            for (float X : {-18.f, 18.f})
                for (int32 E = 0; E < 32; ++E)
                {
                    const auto Point = [&](int32 K) { const float A = K * 2.f * PI / 32.f; return FVector(X, FMath::Cos(A) * 135, FMath::Sin(A) * 135); };
                    Rod(this, Orbit, Point(E), Point(E + 1), 2.f, ActiveGlow);
                }
            Shape(this, Root, EShape::Box, FVector(0, 0, 176), FVector(25, 8, 25), ActiveGlow, FRotator(0, 0, 45));
        }
        auto* Ground = Joint(this, Root, FVector(0, 0, -119));
        Ground->ComponentTags.Add(TEXT("JudgmentGroundCue347"));
        for (int32 Ring = 0; Ring < (bActive ? 2 : 1); ++Ring)
            for (int32 E = 0; E < 32; ++E)
            {
                const float R = (bActive ? 150.f : 127.f) - Ring * 14.f;
                const auto Point = [&](int32 K) { const float A = K * 2.f * PI / 32.f; return FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 0); };
                Rod(this, Ground, Point(E), Point(E + 1), bActive ? 3.f : 1.8f, Glow);
            }
        auto* Number = NewObject<UTextRenderComponent>(this);
        Number->SetupAttachment(Ground);
        Number->SetRelativeLocationAndRotation(FVector(0, 0, 2), FRotator(90, 0, 0));
        Number->SetText(FText::FromString(Numbers[I]));
        Number->SetHorizontalAlignment(EHTA_Center);
        Number->SetWorldSize(47.f);
        Number->SetTextRenderColor(bActive ? FColor(255, 218, 97) : FColor(91, 167, 202));
        AddInstanceComponent(Number); Number->RegisterComponent();
        TArray<USceneComponent*> SealComponents;
        Root->GetChildrenComponents(true, SealComponents);
        for (USceneComponent* Child : SealComponents)
            if (auto* Primitive = Cast<UPrimitiveComponent>(Child))
            {
                Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                Primitive->SetCastShadow(false);
            }
    }
    if (bJudgmentWard && !JudgmentBarrier.IsValid())
    {
        auto* Ward = Joint(this, SceneRoot, FVector::ZeroVector);
        Ward->SetWorldLocation(CourtBoss->GetActorLocation() + FVector(0, 0, 28));
        Ward->ComponentTags.Add(TEXT("JudgmentWard347"));
        JudgmentBarrier = Ward;
        for (int32 Axis = 0; Axis < 2; ++Axis)
            for (int32 E = 0; E < 24; ++E)
            {
                const auto Point = [&](int32 K)
                {
                    const float A = K * 2.f * PI / 24.f;
                    return FVector(Axis ? 0 : FMath::Cos(A) * 164, Axis ? FMath::Cos(A) * 164 : 0, FMath::Sin(A) * 210);
                };
                auto* Rail = Rod(this, Ward, Point(E), Point(E + 1), 1.5f, DormantGlow);
                Rail->SetCollisionEnabled(ECollisionEnabled::NoCollision); Rail->SetCastShadow(false);
            }
    }
}
