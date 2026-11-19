#include "DuelArena.h"
#include "DuelGeometry.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Algo/Find.h"
#include "Engine/Texture2D.h"
#include "Misc/App.h"

namespace
{
    constexpr const TCHAR* SigilSheetNames[] = {
        TEXT("T_NorthSigilCasts"),TEXT("T_NorthSigilJudgment"),TEXT("T_NorthSigilBloodfire"),TEXT("T_NorthSigilWinter")
    };
    UTexture2D* LoadSigilSheet(int32 Sheet)
    {
        const TCHAR* Name = SigilSheetNames[Sheet];
        return LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("/Game/DuelArt/Textures/%s.%s"), Name, Name));
    }

    UStaticMeshComponent* NewSigilArtworkPlane(AActor* Owner, USceneComponent* Root, UMaterialInterface* Material)
    {
        auto* Plane = NewObject<UStaticMeshComponent>(Owner);
        Plane->SetupAttachment(Root);
        Plane->SetMobility(EComponentMobility::Movable);
        Plane->SetStaticMesh(DuelGeometry::Mesh(DuelGeometry::EShape::Plane));
        Plane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Plane->SetCastShadow(false);
        Plane->SetAffectDynamicIndirectLighting(false);
        Plane->SetAffectDistanceFieldLighting(false);
        Plane->SetCanEverAffectNavigation(false);
        Plane->SetMaterial(0, Material);
        return Plane;
    }
}

void ADuelArena::PrecacheSigilArtwork()
{
    if (!FApp::CanEverRender()) return;

    // Both tints use the same unlit shader. Load it with the other battle assets before entering a court.
    SpellFacet(false);
    SpellFacet(true);
    SigilSheets.SetNum(UE_ARRAY_COUNT(SigilSheetNames));
    for (int32 Sheet = 0; Sheet < SigilSheets.Num(); ++Sheet)
        SigilSheets[Sheet] = LoadSigilSheet(Sheet);

    // Prepare the translucent plane's pipeline before the first timed warning.
    for (const TCHAR* Name : {TEXT("M_NorthFiligree"), TEXT("M_NorthHighSigil"), TEXT("MI_NorthStrikeSigil")})
        if (auto* Material = DuelGeometry::ArtMaterial(Name))
            NewSigilArtworkPlane(this, SceneRoot, Material)->PrecachePSOs();
}

UStaticMeshComponent* ADuelArena::DrawSigilArtwork(EDuelMark Mark, const FVector& At, const FVector& Axis,
    const FVector2D& Extent, const FLinearColor& Tint, float Life, bool bStrike, bool bLane,
    float Inner, float EndScale, float HalfArc)
{
    const bool bCross = Mark == EDuelMark::GildedCross;
    const bool bCrescent = Mark == EDuelMark::MoonCrescent;
    const bool bSector = !bLane && HalfArc < 180.f;
    const bool bCircle = !bLane && !bCross && !bCrescent && !bSector;
    const FVector Along = Axis.GetSafeNormal2D(UE_SMALL_NUMBER, FVector::ForwardVector);
    const float Yaw = Along.Rotation().Yaw + (bCrescent ? 180.f : 0.f);
    const FLinearColor Display = FMath::Lerp(Tint.GetClamped(0.f,1.f), FLinearColor::White, .58f);
    const auto Layer = [&](UMaterialInstanceDynamic* Material, float Height, float Size, float Spin, float Opacity)
    {
        auto* Art = NewSigilArtworkPlane(this, SceneRoot, Material);
        Art->SetRelativeLocationAndRotation(At + FVector(0,0,Height), FRotator(0,Yaw,0));
        Art->SetRelativeScale3D(FVector(2. * Extent.X * Size / 100.,2. * Extent.Y * Size / 100.,1.));
        Art->ComponentTags.Add(TEXT("NorthSigilArtwork"));
        AddInstanceComponent(Art);
        Art->RegisterComponent();
        Animate(Art, Life, FVector(EndScale,EndScale,1), FVector::ZeroVector,0,Spin,!bStrike);
        Effects.Last().OpacityBase = Opacity;
        Effects.Last().Breath = bStrike ? 0.f : 8.8f;
        Material->SetScalarParameterValue(TEXT("Opacity"), Opacity);
        return Art;
    };

    struct FArtwork { EDuelMark Mark; int32 Sheet; int32 Tile; };
    static constexpr FArtwork Designs[] = {
        {EDuelMark::GildedCross,0,0}, {EDuelMark::MoonCrescent,0,1},
        {EDuelMark::Wingfall,1,0}, {EDuelMark::LightRosette,1,1}, {EDuelMark::Tribunal,1,2}, {EDuelMark::Scroll,1,3},
        {EDuelMark::TalonRush,2,0}, {EDuelMark::CrossAmbush,2,1}, {EDuelMark::EmberRecoil,2,2}, {EDuelMark::SnareTrap,2,3},
        {EDuelMark::ReturningFrost,3,0}, {EDuelMark::MirrorPanel,3,1}, {EDuelMark::IceLance,3,2}, {EDuelMark::Archive,3,3},
    };
    const auto* Design = Algo::FindByPredicate(Designs, [Mark](const FArtwork& Art){ return Art.Mark == Mark; });
    const auto Generated = [&](int32 Sheet, int32 Tile, float Height, float Size, float Spin, float Opacity)
        -> UStaticMeshComponent*
    {
        auto* Parent = DuelGeometry::ArtMaterial(TEXT("M_NorthFiligree"));
        if (!Parent) return nullptr;
        if (SigilSheets.IsEmpty()) SigilSheets.SetNum(UE_ARRAY_COUNT(SigilSheetNames));
        if (!SigilSheets[Sheet]) SigilSheets[Sheet] = LoadSigilSheet(Sheet);
        auto* Texture = SigilSheets[Sheet].Get();
        if (!Texture) return nullptr;
        auto* Material = UMaterialInstanceDynamic::Create(Parent,this);
        Material->SetTextureParameterValue(TEXT("Artwork"), Texture);
        Material->SetVectorParameterValue(TEXT("Tint"), Display);
        // Crop the cross tile's transparent margin into its gameplay footprint.
        const float TileSize = bCross ? .45f : .50f;
        const float Inset = (.50f - TileSize) * .50f;
        Material->SetVectorParameterValue(TEXT("TileOrigin"), FLinearColor((Tile % 2) * .50f + Inset,(Tile / 2) * .50f + Inset,0,0));
        Material->SetScalarParameterValue(TEXT("TileSize"),TileSize);
        Material->SetScalarParameterValue(TEXT("Gain"),bStrike ? 5.2f : 4.1f);
        const float Rule = bCross ? 1.f : bCrescent ? 2.f : bLane ? 0.f : 3.f;
        Material->SetVectorParameterValue(TEXT("Footprint"),
            FLinearColor(Rule, bCross ? DuelSpellMarks::CrossHalfWidth / Extent.X : DuelSpellMarks::MoonCutOffset, 0, 0));
        Material->SetScalarParameterValue(TEXT("Inner"),bCrescent ? DuelSpellMarks::MoonCutRadius : Inner / Extent.X);
        return Layer(Material,Height,Size,Spin,Opacity);
    };
    if (Design && !bSector && (!bLane || (Mark != EDuelMark::TalonRush && Mark != EDuelMark::IceLance)))
    {
        const float SpinRate = bCircle ? (Mark == EDuelMark::ReturningFrost ? -46.f : 34.f) : 0.f;
        if (auto* Artwork = Generated(Design->Sheet, Design->Tile, 7.f, 1.f, SpinRate, .94f))
        {
            if (bCircle && Inner <= 1.f &&
                (Mark == EDuelMark::LightRosette || Mark == EDuelMark::Tribunal || Mark == EDuelMark::Wingfall))
                Generated(0, 3, 7.6f, .49f, -54.f, .68f);
            return Artwork;
        }
    }

    // The original atlases use four half-width tiles. Sample their artwork in
    // the stationary hit shape, with counter-rotating ink in full circles.
    const bool bDirectionalArt = bLane || bSector;
    auto* Parent = DuelGeometry::ArtMaterial(bDirectionalArt ? TEXT("MI_NorthStrikeSigil") : TEXT("M_NorthHighSigil"));
    if (!Parent) return nullptr;
    const bool bFrost = Tint.B > Tint.R * 1.13f && Tint.G > Tint.R * .72f;
    const bool bCrimson = Tint.R > Tint.G * 1.65f;
    const bool bViolet = Tint.B > Tint.R * 1.08f;
    int32 Tile = bFrost ? 2 : bViolet ? 3 : bCrimson ? 1 : 0;
    if (bSector)
    {
        Tile = Mark == EDuelMark::TailSweep ? 1 : bFrost ? 2 : 0;
    }
    else if (bCircle)
    {
        if (Mark == EDuelMark::StarTrack || Mark == EDuelMark::Ward) Tile = 3;
        if (Mark == EDuelMark::Votive) Tile = 0;
        if (Mark == EDuelMark::Reap) Tile = 1;
    }
    auto* Material = UMaterialInstanceDynamic::Create(Parent, this);
    Material->SetVectorParameterValue(TEXT("TileOrigin"), FLinearColor((Tile % 2) * .50f, (Tile / 2) * .50f, 0, 0));
    Material->SetVectorParameterValue(TEXT("TileFit"), FLinearColor(.89f, .89f, 0, 0));
    Material->SetVectorParameterValue(TEXT("Tint"), Display);
    Material->SetScalarParameterValue(TEXT("Layered"), bCircle ? (Inner > 1.f ? 2.f : 1.f) : 0.f);
    Material->SetScalarParameterValue(TEXT("Spin"), .67f);
    Material->SetScalarParameterValue(TEXT("InnerFraction"), Inner / Extent.X);
    Material->SetScalarParameterValue(TEXT("Gain"), bStrike ? 4.2f : 3.85f);
    Material->SetScalarParameterValue(TEXT("Arc"), FMath::DegreesToRadians(HalfArc));
    Material->SetScalarParameterValue(TEXT("Fan"), bSector && Tile == 0 ? 1.f : 0.f);
    Material->SetScalarParameterValue(TEXT("Lane"), bLane ? 1.f : 0.f);
    if (bLane)
    {
        const int32 Repeat = FMath::Clamp(FMath::RoundToInt(Extent.X / (2.15 * Extent.Y)), 1, 12);
        Material->SetScalarParameterValue(TEXT("Repeat"), Repeat);
    }
    return Layer(Material, 5.3f, 1.f, 0.f, .94f);
}

USceneComponent* ADuelArena::DrawSpellMark(EDuelMark Mark, const FVector& At, const FVector& Axis,
    const FVector2D& Extent, const FLinearColor& Tint, float Life, bool bStrike, bool bLane,
    float Inner, float EndScale, float Spin, float HalfArc)
{
    if (Life <= 0.f || Extent.GetMin() <= 0.)
    {
        return nullptr;
    }
    TraceSigil(Mark, At, Axis, Extent, Tint, Life, bStrike);
    if (HalfArc >= 180.f && Mark != EDuelMark::Default && Mark != EDuelMark::GildedCross &&
        Mark != EDuelMark::MoonCrescent && Mark != EDuelMark::StarTrack && Mark != EDuelMark::Pursuit)
        CombatAtmosphere(At, Axis, Extent, Tint, Life, bStrike, bLane, Inner);
    if (auto* Artwork = DrawSigilArtwork(Mark, At, Axis, Extent, Tint, Life, bStrike, bLane, Inner, EndScale, HalfArc))
        return Artwork;

    TArray<FVector> Points;
    TArray<int32> Triangles;
    DuelSpellMarks::Build(Mark, Axis, Extent, bLane, Inner, Points, Triangles, HalfArc, EndScale);
    if (Triangles.IsEmpty())
    {
        GroundWarning(At, Axis, Extent, Tint, Life, EndScale, bLane);
        return nullptr;
    }
    const float Peak = FMath::Max(.01f, FMath::Max3(Tint.R, Tint.G, Tint.B));
    const float Strength = bStrike ? 2.8f : 3.45f;
    const FLinearColor Color(FMath::Pow(FMath::Max(0.f, Tint.R / Peak), 1.18f) * Strength,
        FMath::Pow(FMath::Max(0.f, Tint.G / Peak), 1.18f) * Strength,
        FMath::Pow(FMath::Max(0.f, Tint.B / Peak), 1.18f) * Strength, .87f);
    const auto AddLayer = [&](const TArray<FVector>& Vertices, float Height, const FLinearColor& LayerColor)
    {
        auto* Layer = LightMesh(DuelGeometry::ColorMaterial(this, LayerColor, true));
        Layer->SetRelativeLocation(At + FVector(0, 0, Height));
        Layer->SetCanEverAffectNavigation(false);
        Layer->SetAffectDistanceFieldLighting(false);
        Layer->SetAffectDynamicIndirectLighting(false);
        Layer->CreateMeshSection_LinearColor(0, Vertices, Triangles, {}, {}, {}, {}, false, false);
        Animate(Layer, Life, FVector(EndScale, EndScale, 1), FVector::ZeroVector, 0, Spin, !bStrike);
        Effects.Last().MaterialColor = LayerColor;
        return Layer;
    };
    const float Height = bStrike ? 6.5f : 5.f;
    // The shadow snare is hidden as soon as it triggers. Its warning must be one component.
    const bool bSnare = Mark == EDuelMark::Snare;
    auto* Outline = AddLayer(Points, Height, Color.CopyWithNewOpacity(bSnare ? Color.A : .48f));
    Outline->ComponentTags.Add(TEXT("NorthUniqueSigil"));
    if (bSnare) return Outline;

    for (int32 Index = 0; Index < Points.Num(); Index += 4)
    {
        const FVector Start = (Points[Index] + Points[Index + 1]) * .5;
        const FVector End = (Points[Index + 2] + Points[Index + 3]) * .5;
        Points[Index] = FMath::Lerp(Start, Points[Index], .48);
        Points[Index + 1] = FMath::Lerp(Start, Points[Index + 1], .48);
        Points[Index + 2] = FMath::Lerp(End, Points[Index + 2], .48);
        Points[Index + 3] = FMath::Lerp(End, Points[Index + 3], .48);
    }
    const FLinearColor Core = FMath::Lerp(Color, FLinearColor(4.2f, 4.2f, 4.2f, .87f), .52f);
    AddLayer(Points, Height + .6f, Core);
    return Outline;
}

void ADuelArena::TraceSigil(EDuelMark Mark, const FVector& At, const FVector& Axis, const FVector2D& Extent,
    const FLinearColor& Tint, float Life, bool bStrike)
{
    if (Life < .13f) return;
    const FVector Forward = Axis.GetSafeNormal2D(UE_SMALL_NUMBER, FVector::ForwardVector);
    const double Reach = Extent.X;
    const FLinearColor Edge = FMath::Lerp(Tint.GetClamped(0.f, 1.f), FLinearColor(.95f, .85f, 1.f), .36f);
    if (Mark == EDuelMark::TalonRush || Mark == EDuelMark::CrossAmbush || Mark == EDuelMark::Pursuit)
    {
        const FVector Across = FVector::CrossProduct(FVector::UpVector, Forward);
        const double Width = Extent.Y;
        const int32 Count = FMath::Clamp(FMath::RoundToInt(2. * Reach / 170.), 3, 10);
        for (int32 I = 0; I < Count; ++I)
        {
            const double T = (I + .5) / Count;
            const FVector Start = At + Forward * (Reach * (-.86 + T * 1.44)) + FVector(0, 0, 9 + (I % 2) * 4);
            const float Delay = Life * (.06f + .39f * I / Count);
            const float Tail = Life - Delay;
            const double Advance = FMath::Min(Reach * .21, 146.);
            for (float Side : {-1.f, 1.f})
            {
                const FVector Slope = (Forward + Across * Side * .61).GetSafeNormal();
                Emit(EDuelLight::Filament, Start - Slope * FMath::Min(Width * .22, 23.), Forward * (Advance / Tail),
                    FVector2D(FMath::Min3(Width * .75, Reach * .22, 67.), bStrike ? 8. : 5.),
                    Edge.CopyWithNewOpacity(bStrike ? .84f : .63f), Tail, 0.f, Slope, 0.f, false, Delay);
            }
        }
        return;
    }
    if (Mark == EDuelMark::StarTrack)
    {
        for (int32 I = 0; I < 6; ++I)
        {
            const int32 Beat = I / 2;
            const float Delay = Life * (bStrike ? .16f : .22f) * Beat;
            const float Duration = Life * (bStrike ? .68f : .80f) - Delay;
            const FVector Out = Forward.RotateAngleAxis(I * 137.5f, FVector::UpVector);
            const double Height = 147. + (I % 2) * 59. + Beat * 27.;
            const FVector Start = At + Out * (Reach * (.31 + .12 * Beat)) + FVector(0, 0, Height);
            const FVector Drift = (-Out * (Reach * .10) - FVector(0, 0, Height * .77)) / Duration;
            Emit(bStrike ? EDuelLight::Star : EDuelLight::Filament, Start, Drift,
                bStrike ? FVector2D(48, 36) : FVector2D(39, 9), Edge.CopyWithNewOpacity(bStrike ? .91f : .68f),
                Duration, 238.f, Drift, 0.f, false, Delay);
        }
        return;
    }
    if (Mark == EDuelMark::GildedCross)
    {
        for (int32 I = 0; I < 4; ++I)
        {
            const FVector Out = Forward.RotateAngleAxis(90.f * I, FVector::UpVector);
            const float Delay = Life * .09f * I;
            const float Duration = Life - Delay;
            const FVector Start = At + Out * (Reach * .19) + FVector(0, 0, 13);
            Emit(EDuelLight::Filament, Start, Out * (Reach * .63 / Duration),
                FVector2D(FMath::Min(132., Reach * .29), bStrike ? 17. : 8.), Edge,
                Duration, 0.f, Out, 0.f, false, Delay);
        }
        return;
    }
    if (Mark == EDuelMark::MoonCrescent)
    {
        for (int32 I = -3; I <= 3; ++I)
        {
            const FVector Out = Forward.RotateAngleAxis(I * 31.f, FVector::UpVector);
            const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Out);
            const float Delay = Life * .08f * FMath::Abs(I);
            const float Duration = Life - Delay;
            const FVector Start = At + Out * (Reach * .81) + FVector(0, 0, 13);
            const FVector Drift = Out * (Reach * .10 / Duration) + (bStrike ? FVector(0, 0, 98) : FVector::ZeroVector);
            Emit(EDuelLight::Filament, Start, Drift, FVector2D(FMath::Min(117., Reach * .25), bStrike ? 19. : 8.),
                Edge, Duration, 0.f, Tangent, 0.f, false, Delay);
        }
    }
}

USceneComponent* ADuelArena::DrawChargeSeal(const FVector& At, const FVector& Axis, float Radius,
    const FLinearColor& Tint, float Windup, float Hold, EDuelMark Mark)
{
    const FVector2D Extent(Radius);
    if (auto* Seal = DrawSigilArtwork(Mark, At, Axis, Extent, Tint, Windup + Hold + .06f,
        false, false, 0.f, 1.f, 180.f))
    {
        TraceSigil(Mark, At, Axis, Extent, Tint, Windup, false);
        return Seal;
    }
    return DrawSpellMark(Mark, At, Axis, Extent, Tint, Windup + Hold + .06f, false);
}

void ADuelArena::GroundWarning(const FVector& At, const FVector& Axis, const FVector2D& Extent,
    const FLinearColor& Tint, float Life, float EndScale, bool bLane, bool bEdgeOnly)
{
    if (Life <= 0.f || Extent.GetMin() <= 0.f) return;

    const float Inner = bEdgeOnly ? Extent.X * .67f : 0.f;
    if (DrawSigilArtwork(EDuelMark::Default, At, Axis, Extent, Tint, Life, false, bLane, Inner, EndScale, 180.f))
        return;

    const FVector Forward = Axis.GetSafeNormal2D(UE_SMALL_NUMBER, FVector::ForwardVector);
    const FVector Side = FVector::CrossProduct(FVector::UpVector, Forward);
    const double Width = FMath::Clamp(Extent.Y * .072, 6., 35.);
    const float MaxTint = FMath::Max(.01f, FMath::Max3(Tint.R, Tint.G, Tint.B));
    const bool bFrost = Tint.B > Tint.R * 1.13f;
    const bool bBlood = !bFrost && Tint.G < Tint.R * .40f;
    const float Strength = bLane ? 3.8f : 3.35f;
    const FLinearColor Color(FMath::Pow(FMath::Max(0.f, Tint.R / MaxTint), 1.28f) * Strength,
        FMath::Pow(FMath::Max(0.f, Tint.G / MaxTint), 1.28f) * Strength,
        FMath::Pow(FMath::Max(0.f, Tint.B / MaxTint), 1.28f) * Strength, .88f);

    TArray<FVector> Points;
    TArray<int32> Triangles;
    // Keep every stroke inside the real gameplay footprint. The warning mesh
    // can have broken edges and forks without implying a different hit shape.
    const auto Clip = [&](FVector P)
    {
        if (bLane)
        {
            const double X = FMath::Clamp(FVector::DotProduct(P, Forward), -Extent.X, Extent.X);
            const double Y = FMath::Clamp(FVector::DotProduct(P, Side), -Extent.Y, Extent.Y);
            return Forward * X + Side * Y;
        }
        return P.GetClampedToMaxSize2D(Extent.X);
    };
    const auto Stroke = [&](const FVector& A, const FVector& B, double W0, double W1)
    {
        const FVector Perp = FVector::CrossProduct(FVector::UpVector, (B - A).GetSafeNormal2D());
        const int32 V = Points.Num();
        Points.Append({Clip(A - Perp * W0), Clip(A + Perp * W0), Clip(B + Perp * W1), Clip(B - Perp * W1)});
        Triangles.Append({V, V+1, V+2, V, V+2, V+3});
    };
    if (bLane)
    {
        // Ragged banks with staggered inward fractures show the lane's actual width.
        for (double Sign : {-1., 1.})
        {
            FVector Previous = Forward * -Extent.X + Side * Sign * (Extent.Y - Width);
            for (int32 I = 1; I <= 18; ++I)
            {
                const double T = I / 18.;
                const double Jag = (1. + FMath::Sin(I * 2.37 + Sign)) * Width * .65;
                const FVector P = Forward * FMath::Lerp(-Extent.X, Extent.X, T) + Side * Sign * (Extent.Y - Width * .42 - Jag);
                Stroke(Previous, P, Width * .46, Width * (.25 + .14 * (I % 3)));
                if (I % 3 == (Sign > 0 ? 1 : 0))
                {
                    const FVector Joint = P - Side * Sign * Extent.Y * .37 + Forward * Extent.X * .035;
                    Stroke(P, Joint, Width * .62, Width * .35);
                    Stroke(Joint, Joint - Side * Sign * Extent.Y * .18 + Forward * Extent.X * .11, Width * .35, Width * .06);
                }
                Previous = P;
            }
        }
    }
    else
    {
        const double Radius = Extent.X;
        // Three offset, eroded bands. The crack patterns distinguish frost,
        // claw marks and ember impacts without painting an opaque floor disk.
        for (int32 Sector = 0; Sector < 6; ++Sector)
        {
            const double Start = Sector * 60. + 3. + (Sector % 3) * 2.;
            const double Span = 47. + (Sector % 3) * 2.5;
            FVector Previous;
            for (int32 I = 0; I <= 8; ++I)
            {
                const double Angle = Start + Span * I / 8.;
                const FVector Radial = FRotator(0, Angle, 0).Vector();
                const double Dist = Radius - Width * (.42 + .72 * (1. + FMath::Sin(I * 2.27 + Sector * 3.1)));
                const FVector P = Radial * Dist;
                if (I > 0) Stroke(Previous, P, Width * (.40 + .15 * (I % 3)), Width * (I == 8 ? .14 : .53));
                if (I == 3 || (!bEdgeOnly && I == 6))
                {
                    const FVector Across = FVector::CrossProduct(FVector::UpVector, Radial);
                    const double Reach = bEdgeOnly ? .13 : bFrost ? .55 : bBlood ? .31 : .42;
                    const FVector Joint = P - Radial * Radius * Reach * .55 + Across * Radius * (bBlood ? .15 : .07);
                    const FVector Tip = P - Radial * Radius * Reach + Across * Radius * (bFrost ? -.08 : .18);
                    Stroke(P, Joint, Width * .66, Width * .36);
                    Stroke(Joint, Tip, Width * .36, Width * .05);
                    if (bFrost && !bEdgeOnly)
                        Stroke(Joint, Joint - Radial * Radius * .15 + Across * Radius * .145, Width * .28, Width * .05);
                }
                Previous = P;
            }
        }
    }

    auto* Mesh = LightMesh(DuelGeometry::ColorMaterial(this, Color, true));
    Mesh->SetRelativeLocation(At + FVector(0, 0, 5));
    Mesh->CreateMeshSection_LinearColor(0, Points, Triangles, {}, {}, {}, {}, false, false);
    const bool bWarning = EndScale == 1.f;
    Animate(Mesh, Life, FVector(EndScale, EndScale, 1), FVector::ZeroVector, 0, 0, bWarning);
    Effects.Last().MaterialColor = Color;
}
