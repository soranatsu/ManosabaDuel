#include "DuelNorthRose.h"

#include "DuelGeometry.h"
#include "DuelNorthLayout.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "DuelNorthTower.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#endif

namespace DuelNorthRose
{
namespace
{
    using namespace DuelNorthLayout;
    constexpr int32 Sectors = 48;
    constexpr double GlassRadius = ClearRadius + .45;
    constexpr double ArtRadius = 209.;
    constexpr double FaceDepth = 11.65;
    constexpr double ThroatDepth = 4.5;
    constexpr double FaceRadius = ClearRadius + 16.;

    struct FRoseSection
    {
        TArray<FVector> Vertices, Normals;
        TArray<int32> Indices;
        TArray<FVector2D> UVs;
        TArray<FProcMeshTangent> Tangents;

        void Triangle(const FVector& A, const FVector& B, const FVector& C, const FVector& Out,
            FVector2D UVa = FVector2D::ZeroVector, FVector2D UVb = FVector2D::ZeroVector, FVector2D UVc = FVector2D::ZeroVector)
        {
            const FVector N = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
            if (N.IsNearlyZero()) return;
            const bool bFlip = FVector::DotProduct(N, Out) < 0;
            const FVector Normal = bFlip ? -N : N;
            const FVector U = (B - A).GetSafeNormal();
            const int32 First = Vertices.Num();
            Vertices.Append({A, B, C});
            Normals.Append({Normal, Normal, Normal});
            UVs.Append({UVa, UVb, UVc});
            for (int32 K = 0; K < 3; ++K) Tangents.Emplace(U, false);
            Indices.Append({First, First + (bFlip ? 1 : 2), First + (bFlip ? 2 : 1)});
        }

        void Quad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Out)
        {
            Triangle(A, B, C, Out);
            Triangle(A, C, D, Out);
        }
    };

    struct FRoseWindow
    {
        FVector Centre, Normal, Across;
        double Scale;
        explicit FRoseWindow(const FPortal& P)
            : Centre(P.Center + FVector(0, 0, P.Height * .5)), Normal(P.Normal),
              Across(FVector::CrossProduct(FVector::UpVector, P.Normal)), Scale(P.Width / RoughDiameter) {}

        FVector Radial(double Angle) const
        {
            return Across * FMath::Cos(Angle) + FVector::UpVector * FMath::Sin(Angle);
        }

        FVector Point(double Angle, double Radius, double Depth) const
        {
            return Centre + Radial(Angle) * (Radius * Scale) + Normal * Depth;
        }
    };

    double AngleAt(int32 Step) { return 2. * PI * (Step % Sectors) / Sectors; }

    // This ring is a solid piece of the existing wall. Its square perimeter overlaps
    // the surrounding panels by one centimetre while both faces sit within the wall.
    void Spandrel(FRoseSection& Section, const FRoseWindow& W, double Half)
    {
        for (int32 I = 0; I < Sectors; ++I)
        {
            const double A = AngleAt(I), B = AngleAt(I+1);
            const auto Outer = [Half](double T)
            { return Half / FMath::Max(FMath::Abs(FMath::Cos(T)), FMath::Abs(FMath::Sin(T))); };
            for (double Side : {-1., 1.})
                Section.Quad(W.Point(A, Outer(A), Side * FaceDepth), W.Point(B, Outer(B), Side * FaceDepth),
                    W.Point(B, FaceRadius, Side * FaceDepth), W.Point(A, FaceRadius, Side * FaceDepth), W.Normal * Side);
            const FVector Radial = (W.Radial(A) + W.Radial(B)).GetSafeNormal();
            Section.Quad(W.Point(A, Outer(A), -FaceDepth), W.Point(B, Outer(B), -FaceDepth),
                W.Point(B, Outer(B), FaceDepth), W.Point(A, Outer(A), FaceDepth), Radial);
            const double Depth[] = {-FaceDepth, -ThroatDepth, ThroatDepth, FaceDepth};
            const double Radius[] = {FaceRadius, ClearRadius, ClearRadius, FaceRadius};
            for (int32 K = 0; K < 3; ++K)
                Section.Quad(W.Point(A, Radius[K], Depth[K]), W.Point(B, Radius[K], Depth[K]),
                    W.Point(B, Radius[K+1], Depth[K+1]), W.Point(A, Radius[K+1], Depth[K+1]), -Radial);
        }
    }

    void CircumferentialStone(FRoseSection& Section, const FRoseWindow& W)
    {
        for (int32 I = 0; I < Sectors; ++I)
        {
            const double A = AngleAt(I), B = AngleAt(I+1);
            const FVector Radial = (W.Radial(A) + W.Radial(B)).GetSafeNormal();
            for (double Side : {-1., 1.})
            {
                const FVector Out = W.Normal * Side;
                const double OuterDepth = Side * 21., CrownDepth = Side * 26., RootDepth = Side * 11.4;
                // Asymmetric moulded cross section, buried into the spandrel.
                Section.Quad(W.Point(A, 294, RootDepth), W.Point(B, 294, RootDepth),
                    W.Point(B, 290, OuterDepth), W.Point(A, 290, OuterDepth), Radial + Out);
                Section.Quad(W.Point(A, 290, OuterDepth), W.Point(B, 290, OuterDepth),
                    W.Point(B, 278, CrownDepth), W.Point(A, 278, CrownDepth), Out);
                Section.Quad(W.Point(A, 278, CrownDepth), W.Point(B, 278, CrownDepth),
                    W.Point(B, FaceRadius, RootDepth), W.Point(A, FaceRadius, RootDepth), Out - Radial);
            }
        }
    }

    void RosetteTracery(FRoseSection& Section, const FRoseWindow& W)
    {
        const auto Band = [&](double A, double B, double Inner, double Outer, double Depth)
        {
            const FVector Inward = -(W.Radial(A) + W.Radial(B)).GetSafeNormal();
            for (double Side : {-1., 1.})
                Section.Quad(W.Point(A, Inner, Depth * Side), W.Point(B, Inner, Depth * Side),
                    W.Point(B, Outer, Depth * Side), W.Point(A, Outer, Depth * Side), W.Normal * Side);
            Section.Quad(W.Point(A, Inner, -Depth), W.Point(B, Inner, -Depth),
                W.Point(B, Inner, Depth), W.Point(A, Inner, Depth), Inward);
            Section.Quad(W.Point(A, Outer, -Depth), W.Point(B, Outer, -Depth),
                W.Point(B, Outer, Depth), W.Point(A, Outer, Depth), -Inward);
        };
        for (int32 I = 0; I < Sectors; ++I)
            Band(AngleAt(I), AngleAt(I+1), ArtRadius - 3.5, ArtRadius + 3.5, 4.);
        for (int32 Arm = 0; Arm < 8; ++Arm)
        {
            const double T = PI * Arm / 4.;
            const double A = T - .013, B = T + .013;
            Band(A, B, ArtRadius, ClearRadius + .8, 4.);
            for (double Side : {-1., 1.})
            {
                const double Angle = T + Side * .013;
                Section.Quad(W.Point(Angle, ArtRadius, -4), W.Point(Angle, ClearRadius + .8, -4),
                    W.Point(Angle, ClearRadius + .8, 4), W.Point(Angle, ArtRadius, 4),
                    (W.Radial(T + .12) - W.Radial(T)) * Side);
            }
        }
    }

    void WindowDisc(FRoseSection& Section, const FRoseWindow& W, double Radius)
    {
        const auto UV = [](double T) { return FVector2D(.5 + FMath::Cos(T) * .5, .5 - FMath::Sin(T) * .5); };
        for (int32 I = 0; I < Sectors; ++I)
        {
            const double A = AngleAt(I), B = AngleAt(I+1);
            Section.Triangle(W.Centre, W.Point(A, Radius, 0), W.Point(B, Radius, 0), W.Normal,
                FVector2D(.5, .5), UV(A), UV(B));
        }
    }

    UProceduralMeshComponent* NewPart(AActor* Owner, USceneComponent* Parent,
        const TCHAR* Tag, bool bShadows, bool bCollision)
    {
        auto* C = NewObject<UProceduralMeshComponent>(Owner);
        C->SetupAttachment(Parent);
        C->SetMobility(EComponentMobility::Static);
        C->SetCollisionEnabled(bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
        if (bCollision) C->SetCollisionProfileName(TEXT("BlockAll"));
        C->bUseComplexAsSimpleCollision = bCollision;
        C->SetGenerateOverlapEvents(false);
        C->SetCanEverAffectNavigation(false);
        C->SetCastShadow(bShadows);
        C->SetAffectDistanceFieldLighting(false);
        C->SetAffectDynamicIndirectLighting(false);
        C->ComponentTags.Add(Tag);
        return C;
    }

    void InstallSection(UProceduralMeshComponent* Part, int32 Index, const FRoseSection& Section, UMaterialInterface* Mat,
        bool bCollision = false)
    {
        Part->CreateMeshSection(Index, Section.Vertices, Section.Indices, Section.Normals, Section.UVs,
            TArray<FColor>(), Section.Tangents, bCollision);
        Part->SetMaterial(Index, Mat);
    }

    void RegisterPart(AActor* Owner, UProceduralMeshComponent* Part)
    {
        Owner->AddInstanceComponent(Part);
        Part->RegisterComponent();
    }
}

bool IsRound(const DuelNorthLayout::FPortal& P)
{
    return P.bWindow && ((P.RoomA == TEXT("FinalCourt") && FMath::Abs(P.Normal.X) > .9) ||
        (P.RoomA == TEXT("ChapelHall") && FMath::Abs(P.Normal.Y) > .9));
}

float ApertureRadius(const DuelNorthLayout::FPortal& P)
{
    return ClearRadius * P.Width / RoughDiameter;
}

void Build(AActor* Owner, USceneComponent* Parent)
{
    if (!Owner || !Parent) return;
    auto* Wall = DuelGeometry::ArtMaterial(TEXT("M_NorthDungeonWall"));
    auto* Dressed = DuelGeometry::ArtMaterial(TEXT("M_NorthDressedStone"));
    auto* Iron = DuelGeometry::ColorMaterial(Owner, FLinearColor(.075f, .081f, .088f), false, .65f, .78f);
    auto* Glazing = DuelGeometry::ArtMaterial(TEXT("M_NorthRoseGlazing"));
    auto* Clear = Glazing ? Glazing : DuelGeometry::ArtMaterial(TEXT("M_NorthWindowGlass"));
    auto* LegacyArt = Glazing ? nullptr : DuelGeometry::ArtMaterial(TEXT("M_WitnessGlass"));
    if (!Wall || !Dressed || !Clear) UE_LOG(LogTemp, Error, TEXT("North round-window material missing"));

    FRoseSection Spandrels, Mouldings, Tracery;
    int32 Installed = 0;
    for (const FPortal& P : Portals())
    {
        if (!IsRound(P) || !IsExteriorWindow(P)) continue;
        const FRoseWindow Window(P);
        Spandrel(Spandrels, Window, RoughDiameter * .5 + 1. / Window.Scale);
        CircumferentialStone(Mouldings, Window);
        RosetteTracery(Tracery, Window);
        FRoseSection Glass;
        WindowDisc(Glass, Window, GlassRadius);
        auto* Part = NewPart(Owner, Parent, TEXT("NorthRoseGlass"), false, false);
        Part->ComponentTags.Add(P.Id);
        InstallSection(Part, 0, Glass, Clear);
        if (LegacyArt)
        {
            // Existing assets keep the rose visible even before the unified
            // translucent coloured-glass asset has been cooked.
            FRoseSection CentreArt;
            WindowDisc(CentreArt, Window, ArtRadius + 1);
            InstallSection(Part, 1, CentreArt, LegacyArt);
        }
        RegisterPart(Owner, Part);
        ++Installed;
    }
    if (!Installed) return;
    auto* WallPart = NewPart(Owner, Parent, TEXT("NorthRoseSpandrel"), true, true);
    InstallSection(WallPart, 0, Spandrels, Wall, true);
    RegisterPart(Owner, WallPart);
    auto* Border = NewPart(Owner, Parent, TEXT("NorthRoseStone"), true, false);
    InstallSection(Border, 0, Mouldings, Dressed);
    InstallSection(Border, 1, Tracery, Iron);
    RegisterPart(Owner, Border);
    UE_LOG(LogTemp, Display, TEXT("North Tower: %d circular rose apertures in the chapel and final court"), Installed);
}
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthRoseWindowsTest, "ManosabaDuel.NorthTower.RoundRoseWindows",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDuelNorthRoseWindowsTest::RunTest(const FString& Parameters)
{
    using namespace DuelNorthLayout;
    using namespace DuelNorthRose;
    ADuelNorthTower* Tower = nullptr;
    if (GEngine) for (const auto& Context : GEngine->GetWorldContexts()) if (Context.World() && Context.World()->IsGameWorld())
        for (TActorIterator<ADuelNorthTower> It(Context.World()); It; ++It) { Tower = *It; break; }
    if (!TestTrue(TEXT("An isolated North Tower is running"), Tower && FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave"))))
        return false;
    TInlineComponentArray<UProceduralMeshComponent*> Geometry(Tower);
    int32 GlassCount = 0, StoneGroups = 0, Triangles = 0;
    for (auto* C : Geometry)
    {
        const bool Glass = C->ComponentHasTag(TEXT("NorthRoseGlass"));
        const bool Spandrel = C->ComponentHasTag(TEXT("NorthRoseSpandrel"));
        if (!Glass && !Spandrel && !C->ComponentHasTag(TEXT("NorthRoseStone"))) continue;
        GlassCount += Glass ? 1 : 0;
        StoneGroups += Glass ? 0 : 1;
        TestEqual(TEXT("Only the actual stone wall is a geometric occluder"), C->GetCollisionEnabled() != ECollisionEnabled::NoCollision, Spandrel);
        TestEqual(TEXT("Stone casts real shadows; permeable glazing is shadow-free"), bool(C->CastShadow), !Glass);
        for (int32 I = 0; I < C->GetNumSections(); ++I)
        {
            if (auto* S = C->GetProcMeshSection(I)) Triangles += S->ProcIndexBuffer.Num() / 3;
            TestNotNull(TEXT("Rose structure or glazing material installed"), C->GetMaterial(I));
        }
    }
    TestEqual(TEXT("Six mesh discs and two stone batches"), GlassCount + StoneGroups, 8);
    TestEqual(TEXT("Both chapel and all four final portals have fitted circular glazing"), GlassCount, 6);
    TestTrue(TEXT("Bounded high-window geometry cost"), Triangles > 0 && Triangles < 11000);
    FCollisionQueryParams LightRay(SCENE_QUERY_STAT(NorthRoundWindow), false);
    TInlineComponentArray<UBoxComponent*> Bodies(Tower);
    for (auto* B : Bodies) if (B->ComponentHasTag(TEXT("NorthWindowPane"))) LightRay.AddIgnoredComponent(B);
    const FCollisionQueryParams BlockingRay(SCENE_QUERY_STAT(NorthRoundWindowBody), false);
    int32 Windows = 0;
    for (const FPortal& P : Portals())
    {
        if (!IsRound(P)) continue;
        ++Windows;
        const FRoseWindow W(P);
        const auto Trace = [&](const FVector& Mid, const FVector& Direction, const FCollisionQueryParams& Query, const TCHAR* Tag)
        {
            FHitResult Hit;
            return Tower->GetWorld()->LineTraceSingleByChannel(Hit, Mid - Direction, Mid + Direction, ECC_Visibility, Query) &&
                (!Tag || (Hit.GetComponent() && Hit.GetComponent()->ComponentHasTag(Tag)));
        };
        TestTrue(TEXT("The original physical pane still blocks traversal"), Trace(W.Centre, W.Normal * 46, BlockingRay, TEXT("NorthWindowPane")));
        TestFalse(TEXT("The equivalent ray passes through real open space after ignoring the clear pane"),
            Trace(W.Centre, W.Normal * 46 + W.Across * 14 + FVector(0, 0, 19), LightRay, nullptr));
        for (double U : {-1., 1.}) for (double Z : {-1., 1.})
        {
            const FVector Corner = W.Centre + W.Across * (U * 223 * W.Scale) + FVector(0, 0, Z * 223 * W.Scale);
            TestTrue(TEXT("Every former rectangle corner is now opaque across oblique rays"),
                Trace(Corner, W.Normal * 42 + W.Across * 10 + FVector(0, 0, 15), LightRay, TEXT("NorthRoseSpandrel")));
        }
    }
    TestEqual(TEXT("All six round aperture meshes traced"), Windows, 6);
    AddInfo(FString::Printf(TEXT("Traced open circles, blocking panes and 24 sealed spandrel corners; %d triangles."), Triangles));
    return !HasAnyErrors();
}
#endif
