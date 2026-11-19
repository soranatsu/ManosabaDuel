#include "DuelArena.h"
#include "Audio.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "DuelGraphicsSettings.h"
#include "DuelBurstFx.h"
#include "DuelCombatMechanics.h"

#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "DuelGame.h"
#include "DuelGeometry.h"
#include "EngineUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "LocalVertexFactory.h"
#include "MaterialShared.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "ProceduralMeshComponent.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWaveProcedural.h"
#include "TimerManager.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

using DuelGeometry::EShape;
CSV_DEFINE_CATEGORY(DuelCombat, true);

namespace
{
    const FLinearColor HotPink(2.8f, .55f, 1.68f, .64f);
    const FLinearColor Red(5.6f, .043f, .12f, .90f);

    struct FNorthSoundStyle { float Duration = 0; float Gain = 0; };
    constexpr int32 NorthSoundRate = 24000;

    bool NorthSoundStyle(EDuelSound Cue, FNorthSoundStyle& Out)
    {
        switch (Cue)
        {
        case EDuelSound::RelicPickup: Out = {.48f, .25f}; return true;
        case EDuelSound::DoorLatch: Out = {.44f, .25f}; return true;
        case EDuelSound::Checkpoint: Out = {.92f, .17f}; return true;
        case EDuelSound::FogAdmission: Out = {.70f, .20f}; return true;
        default: return false;
        }
    }

    void MakeNorthInteractionPCM(EDuelSound Cue, const FNorthSoundStyle& Style, TArray<int16>& PCM)
    {
        PCM.SetNumUninitialized(FMath::RoundToInt(NorthSoundRate * Style.Duration));
        // Independent seed: interaction audio must not consume combat/VFX RNG.
        FRandomStream Noise(0x4E540000 + static_cast<int32>(Cue));
        float Low = 0.f, Air = 0.f;
        const auto Decay = [](float T, float Attack, float Fall)
        {
            return T < 0.f ? 0.f : (1.f - FMath::Exp(-T / Attack)) * FMath::Exp(-T / Fall);
        };
        const auto Tone = [](float T, float Frequency) { return FMath::Sin(2.f * PI * T * Frequency); };
        for (int32 I = 0; I < PCM.Num(); ++I)
        {
            const float T = static_cast<float>(I) / NorthSoundRate;
            const float White = Noise.FRandRange(-1.f, 1.f);
            Low = .965f * Low + .035f * White;
            Air = .73f * Air + .27f * White;
            float Signal = 0.f;
            switch (Cue)
            {
            case EDuelSound::RelicPickup:
            {
                // Two close crystalline touches, with a small upper glint rather
                // than the battle bell's low, long inharmonic ringing.
                const float Second = T - .078f;
                Signal = .46f * Decay(T, .0022f, .092f) *
                    (Tone(T, 1174.66f) + .22f * Tone(T, 2358.f)) +
                    .42f * Decay(Second, .0028f, .12f) *
                    (Tone(Second, 1567.98f) + .16f * Tone(Second, 3139.f)) +
                    .12f * (White - Air) * Decay(T, .001f, .012f);
                break;
            }
            case EDuelSound::DoorLatch:
            {
                // Dry key/bolt release, then a lower wooden seat. The short gap
                // and filtered friction distinguish it from a combat impact.
                const float Seat = T - .105f;
                Signal = .64f * (White - Air) * Decay(T, .0015f, .024f) +
                    .30f * Tone(T, 1840.f) * Decay(T, .001f, .017f) +
                    .56f * (Air + .40f * Tone(Seat, 173.f)) * Decay(Seat, .003f, .058f) +
                    .34f * Low * Decay(T - .025f, .015f, .18f);
                break;
            }
            case EDuelSound::Checkpoint:
            {
                // Gentle resonant settling with a soft cloth/air bed; no voice,
                // fanfare, repeating pulse, or long tail over resumed movement.
                const float Bloom = Decay(T, .052f, .27f);
                Signal = Bloom * (.48f * Tone(T, 293.66f) + .28f * Tone(T, 440.f) + .17f * Tone(T, 587.33f)) +
                    .12f * Tone(T - .14f, 880.f) * Decay(T - .14f, .028f, .18f) +
                    .48f * Low * Decay(T, .045f, .19f);
                break;
            }
            case EDuelSound::FogAdmission:
            {
                // A single rounded pressure swell and descending air resonance.
                // The phase integral gives a smooth 215 -> 100 Hz glide.
                const float U = T / Style.Duration;
                const float Swell = FMath::Pow(FMath::Sin(PI * U), 1.7f);
                const float Phase = 2.f * PI * (215.f * T - (115.f / (2.f * Style.Duration)) * T * T);
                Signal = Swell * (.70f * Air + .75f * Low + .27f * FMath::Sin(Phase) + .10f * FMath::Sin(Phase * 1.498f));
                break;
            }
            default: break;
            }
            const float FadeIn = FMath::Clamp(T / .002f, 0.f, 1.f);
            const float FadeOut = FMath::Clamp((Style.Duration - T) / .035f, 0.f, 1.f);
            const float Sample = Signal * FadeIn * FadeOut * Style.Gain;
            PCM[I] = static_cast<int16>(FMath::Clamp(Sample, -.96f, .96f) * 32767.f);
        }
        if (!PCM.IsEmpty()) { PCM[0] = 0; PCM.Last() = 0; }
    }

    // Sample a tapered elliptical stroke into the existing batched particle mesh.
    // All segments are cosmetic: gameplay continues to own every hit and warning.
    template<typename FEmitStroke>
    void ArcStroke(const FVector& Center, const FVector& PlaneX, const FVector& PlaneY,
        const FVector2D& Radius, float StartDegrees, float EndDegrees, int32 Segments, FEmitStroke&& EmitStroke)
    {
        const float Start = FMath::DegreesToRadians(StartDegrees);
        const float Step = FMath::DegreesToRadians(EndDegrees - StartDegrees) / Segments;
        for (int32 I = 0; I < Segments; ++I)
        {
            const float T = (I + .5f) / Segments;
            const float A = Start + Step * (I + .5f);
            const FVector Radial = PlaneX * (Radius.X * FMath::Cos(A)) + PlaneY * (Radius.Y * FMath::Sin(A));
            const FVector Derivative = -PlaneX * (Radius.X * FMath::Sin(A)) + PlaneY * (Radius.Y * FMath::Cos(A));
            EmitStroke(Center + Radial, Derivative.GetSafeNormal(),
                static_cast<float>(Derivative.Size()) * FMath::Abs(Step) * 1.12f, .22f + .78f * FMath::Sin(T * PI));
        }
    }

    struct FSpellHalo
    {
        TArray<FVector> Vertices;
        TArray<int32> Indices;
        TArray<FVector2D> UVs;
        TArray<FLinearColor> Colors;

        void Quad(const FVector& Start, const FVector& End, const FVector& Across)
        {
            const int32 V = Vertices.Num();
            const FVector Center = (Start + End) * .5;
            const FVector Corners[] = {Start - Across, Start + Across, End + Across, End - Across};
            constexpr double UVInset = .055;
            const FVector2D Samples[] = {{UVInset, UVInset}, {UVInset, 1. - UVInset},
                {1. - UVInset, 1. - UVInset}, {1. - UVInset, UVInset}};
            for (int32 Corner = 0; Corner < 4; ++Corner)
            {
                Vertices.Add(Corners[Corner]);
                UVs.Add(Samples[Corner]);
                Colors.Add(FLinearColor::Transparent);
            }
            for (int32 Corner = 0; Corner < 4; ++Corner)
            {
                Vertices.Add(FMath::Lerp(Center, Corners[Corner], .76));
                UVs.Add(FMath::Lerp(FVector2D(.5), Samples[Corner], .76));
                Colors.Add(FLinearColor::White);
                const int32 Next = (Corner + 1) % 4;
                Indices.Append({V + Corner, V + Next, V + Next + 4, V + Corner, V + Next + 4, V + Corner + 4});
            }
            Indices.Append({V + 4, V + 5, V + 6, V + 4, V + 6, V + 7});
        }

        void Append(const FVector& Heel, const FVector& Tip, const FVector& Right, const FVector& Up, double Width)
        {
            // Cross two soft atlas cards along the crystal. A third shows the glow head-on.
            const FVector Extension = (Tip - Heel) * .22;
            const double Spread = Width * 2.15 + 5.;
            Quad(Heel - Extension, Tip + Extension, Right * Spread);
            Quad(Heel - Extension, Tip + Extension, Up * Spread);
            const FVector Middle = (Heel + Tip) * .5;
            Quad(Middle - Up * Spread, Middle + Up * Spread, Right * Spread);
        }

        void Install(UProceduralMeshComponent& Mesh, UMaterialInterface* Material) const
        {
            if (!Material || Vertices.IsEmpty()) return;
            Mesh.SetMaterial(1, Material);
            Mesh.CreateMeshSection_LinearColor(1, Vertices, Indices, {}, UVs, Colors, {}, false, false);
        }
    };

    void AppendCrystalFace(const FVector& A, const FVector& B, const FVector& C, float Shade,
        TArray<FVector>& Vertices, TArray<int32>& Indices, TArray<FVector2D>& UVs, TArray<FLinearColor>& Colors)
    {
        const FVector Center = (A + B + C) / 3.;
        const FVector Corners[] = {A, B, C};
        const int32 V = Vertices.Num();
        Vertices.Append({A, B, C});
        for (const FVector& Corner : Corners) Vertices.Add(FMath::Lerp(Corner, Center, .16));
        Vertices.Add(Center);

        // Light concentrates into the ridges. Differences across the inner facet give the core depth.
        const float Face = .11f + .33f * Shade;
        const float Edge = .66f + .28f * Shade;
        Colors.Append({
            FLinearColor(.84f, .95f, 1.f, Edge * .79f),
            FLinearColor(.96f, .99f, 1.f, Edge),
            FLinearColor(.87f, .95f, 1.f, Edge * .93f),
            FLinearColor(.38f, .72f, .96f, Face * .57f),
            FLinearColor(.63f, .87f, 1.f, Face),
            FLinearColor(.55f, .80f, 1.f, Face * .87f),
            FLinearColor(.42f, .72f, .96f, Face * .72f)
        });
        UVs.AddZeroed(7);
        for (int32 Side = 0; Side < 3; ++Side)
        {
            const int32 Next = (Side + 1) % 3;
            Indices.Append({V + Side, V + Next, V + Next + 3, V + Side, V + Next + 3, V + Side + 3,
                V + Side + 3, V + Next + 3, V + 6});
        }
    }

    void AppendIceShard(const FVector& At, const FVector& Axis, float Length, float Width,
        TArray<FVector>& Vertices, TArray<int32>& Indices, TArray<FVector2D>& UVs, TArray<FLinearColor>& Colors,
        FSpellHalo* Halo = nullptr)
    {
        const FVector Support = FMath::Abs(Axis.Z) > .89f ? FVector::ForwardVector : FVector::UpVector;
        const FVector Right = FVector::CrossProduct(Support, Axis).GetSafeNormal();
        const FVector Up = FVector::CrossProduct(Axis, Right).GetSafeNormal();
        const FVector Base = At - Axis * (Length * .13f);
        const FVector Tip = At + Axis * (Length * .62f) + Right * (Width * .12f);
        const FVector Heel = At - Axis * (Length * .38f) + Up * (Width * .13f);
        const FVector Rim[] = {
            Base + Right * Width,
            Base + Up * (Width * .63f) - Axis * (Length * .04f),
            Base - Right * (Width * .71f) + Up * (Width * .12f),
            Base - Up * (Width * .75f) + Axis * (Length * .059f),
        };
        constexpr float Shades[] = {.92f, .49f, .72f, .34f};
        if (Halo) Halo->Append(Heel, Tip, Right, Up, Width);
        for (int32 Side = 0; Side < 4; ++Side)
        {
            const FVector Next = Rim[(Side + 1) % 4];
            if (Halo)
            {
                AppendCrystalFace(Heel, Rim[Side], Next, Shades[Side] * .66f, Vertices, Indices, UVs, Colors);
                AppendCrystalFace(Rim[Side], Tip, Next, Shades[Side], Vertices, Indices, UVs, Colors);
                continue;
            }
            const int32 V = Vertices.Num();
            Vertices.Append({Heel, Rim[Side], Tip, Next});
            Indices.Append({V, V + 1, V + 3, V + 1, V + 2, V + 3});
            UVs.Append({FVector2D(.10, .64), FVector2D(.44, .21), FVector2D(.92, .55), FVector2D(.51, .77)});
            const FLinearColor Color(.82f, .93f, 1.f, Shades[Side]);
            Colors.Append({Color, Color, Color, Color});
        }
    }

    void AppendVerdictPage(const FVector& At, const FVector& Out, float Height, float Width,
        TArray<FVector>& Vertices, TArray<int32>& Indices, TArray<FVector2D>& UVs, TArray<FLinearColor>& Colors)
    {
        const FVector Side = FVector::CrossProduct(FVector::UpVector, Out);
        const FVector Spine = (FVector::UpVector + Out * .44f + Side * .22f).GetSafeNormal() * Height;
        const FVector Turn = Side * Width;
        const int32 V = Vertices.Num();
        Vertices.Append({At - Spine * .45f - Turn * .62f, At - Spine * .53f + Turn * .58f,
            At + Spine * .24f + Turn * .60f, At + Spine * .47f + Turn * .19f,
            At + Spine * .53f - Turn * .53f, At - Out * (Width * .29f)});
        Indices.Append({V + 5, V, V + 1, V + 5, V + 1, V + 2, V + 5, V + 2, V + 3,
            V + 5, V + 3, V + 4, V + 5, V + 4, V});
        UVs.Append({{.10, .81}, {.64, .90}, {.96, .31}, {.77, .08}, {.13, .16}, {.45, .50}});
        const FLinearColor Ink(.66f, .55f, .46f, .56f);
        const FLinearColor Paper(1.f, .87f, .68f, .81f);
        Colors.Append({Ink, Ink, Paper, Paper, Ink, Paper});
    }

    FVector BurstCameraDirection(const UObject* Context, const FVector& At)
    {
        const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(Context, 0);
        return Camera ? (Camera->GetCameraLocation() - At).GetSafeNormal2D() : FVector::ForwardVector;
    }

    template<typename TParticleArray, typename FEmit>
    void QueueBurstPlan(const DuelBurstFx::FPlan& Plan, const TParticleArray& Existing, FEmit&& EmitParticle)
    {
        // Count once per impact, not once per new particle or on every frame.
        // This supplemental layer cannot push soft cloud count above 96, or the
        // existing total pool beyond 1536. Existing warning effects are untouched.
        int32 Clouds = 0;
        for (const auto& P : Existing) Clouds += P.Shape == EDuelLight::Mist ? 1 : 0;
        int32 CloudBudget = FMath::Max(0, 96 - Clouds);
        int32 Budget = FMath::Min(DuelBurstFx::MaxEmissions, FMath::Max(0, 1536 - Existing.Num()));
        for (const auto& E : Plan.Emissions)
        {
            if (Budget <= 0) break;
            if (E.Shape == EDuelLight::Mist)
            {
                if (CloudBudget <= 0) continue;
                --CloudBudget;
            }
            EmitParticle(E);
            --Budget;
        }
    }

    void DestroyGroup(USceneComponent* Root)
    {
        if (!IsValid(Root))
        {
            return;
        }
        TArray<USceneComponent*> Children;
        Root->GetChildrenComponents(true, Children);
        for (int32 Index = Children.Num() - 1; Index >= 0; --Index)
        {
            Children[Index]->DestroyComponent();
        }
        Root->DestroyComponent();
    }
}

void ADuelArena::Tick(float DeltaSeconds)
{
    CSV_SCOPED_TIMING_STAT(DuelCombat, Arena);
    Super::Tick(DeltaSeconds);
    if (!bSpellMaterialsWarmed && FApp::CanEverRender())
    {
        bSpellMaterialsWarmed = true;
        SpellFacet(false);
        SpellFacet(true);
        SpellHalo(false);
        SpellHalo(true);
    }
    TickEffects(DeltaSeconds);
    if (!MirrorCameras.IsEmpty()) UpdateMirrorImages(DeltaSeconds);
    TickOrbs(DeltaSeconds);
    const ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    TickCourtInteractions(Mode ? const_cast<ADuelFighter*>(Mode->GetBossFighter()) : nullptr, DeltaSeconds);
    TickRitualBlade(Mode ? Mode->GetPlayerFighter() : nullptr, DeltaSeconds);
    TickParticles(DeltaSeconds);
    TickBlade(DeltaSeconds);
    TickVoice();
    if (bPrisonGateOpen && PrisonGateLift < 480.f)
    {
        PrisonGateLift = FMath::Min(480.f, PrisonGateLift + DeltaSeconds * 430.f);
        if (PrisonGate)
        {
            PrisonGate->SetRelativeLocation(PrisonGateBase + FVector(0, 0, PrisonGateLift));
            PrisonGate->SetVisibility(PrisonGateLift < 480.f);
        }
        if (PrisonGateBlocker && PrisonGateLift >= 180.f)
        {
            PrisonGateBlocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
    }
    UStaticMeshComponent* Grille = bAppeal ? AppealGrille.Get() : ArchiveGate.Get();
    if (Grille && bArchiveGateOpen && ArchiveGateLift < 570.f)
    {
        ArchiveGateLift = FMath::Min(570.f, ArchiveGateLift + DeltaSeconds * 530.f);
        Grille->SetRelativeLocation((bAppeal ? AppealGrilleBase : ArchiveGateBase) + FVector(0, 0, ArchiveGateLift));
        if (ArchiveGateLift >= 570.f)
        {
            Grille->SetVisibility(false);
        }
    }
    if (Rain && Rain->IsVisible())
    {
        TickRain(DeltaSeconds);
    }
}

void ADuelArena::FEffectMeshBuffer::Reset()
{
    Positions.Reset();
    Indices.Reset();
    UVs.Reset();
    Colors.Reset();
}

void ADuelArena::FEffectMeshBuffer::Upload(UProceduralMeshComponent& Mesh)
{
    const FProcMeshSection* Section = Mesh.GetProcMeshSection(0);
    if (Section && Section->ProcVertexBuffer.Num() == Positions.Num() &&
        Section->ProcIndexBuffer.Num() == Indices.Num() &&
        FMemory::Memcmp(Section->ProcIndexBuffer.GetData(), Indices.GetData(), Indices.Num() * sizeof(int32)) == 0)
    {
        Mesh.UpdateMeshSection_LinearColor(0, Positions, {}, UVs, Colors, {}, false);
    }
    else
    {
        Mesh.CreateMeshSection_LinearColor(0, Positions, Indices, {}, UVs, Colors, {}, false, false);
    }
}

UProceduralMeshComponent* ADuelArena::Ribbon(const FVector& At, float Radius, float Width,
    float StartAngle, float EndAngle, float Rise, UMaterialInterface* Material)
{
    const int32 Sections = FMath::Clamp(FMath::CeilToInt(FMath::Abs(EndAngle - StartAngle) / 7.5f), 5, 48);
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    for (int32 Step = 0; Step <= Sections; ++Step)
    {
        const float Progress = static_cast<float>(Step) / Sections;
        const float Angle = FMath::DegreesToRadians(FMath::Lerp(StartAngle, EndAngle, Progress));
        const float Height = Rise * FMath::Sin(Progress * PI);
        const float Taper = Rise == 0.f ? 1.f : .12f + .88f * FMath::Sin(Progress * PI);
        const FVector Axis(FMath::Cos(Angle), FMath::Sin(Angle), 0);
        Vertices.Add(Axis * (Radius - Width * Taper) + FVector(0, 0, Height * .54f));
        Vertices.Add(Axis * Radius + FVector(0, 0, Height));
        Normals.Append({FVector::UpVector, FVector::UpVector});
        UVs.Append({FVector2D(Progress, 0), FVector2D(Progress, 1)});
        if (Step)
        {
            const int32 Index = Step * 2;
            Triangles.Append({Index - 2, Index, Index - 1, Index - 1, Index, Index + 1});
        }
    }

    UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
    Mesh->SetupAttachment(SceneRoot);
    Mesh->SetRelativeLocation(At);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCastShadow(false);
    Mesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs,
        TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false, false);
    Mesh->SetMaterial(0, Material);
    AddInstanceComponent(Mesh);
    Mesh->RegisterComponent();
    return Mesh;
}

UMaterialInstanceDynamic* ADuelArena::LightMaterial(EDuelLight Shape, const FLinearColor& Tint,
    float Gain, float SoftDepth, float Repeat)
{
    UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(DuelGeometry::ArtMaterial(TEXT("M_SacredFx")), this);
    if (!Material)
    {
        return nullptr;
    }
    const int32 Tile = static_cast<int32>(Shape);
    const float Peak = FMath::Max(1.f, FMath::Max3(Tint.R, Tint.G, Tint.B));
    Material->SetVectorParameterValue(TEXT("Tint"), FLinearColor(Tint.R / Peak, Tint.G / Peak, Tint.B / Peak, 1));
    Material->SetVectorParameterValue(TEXT("TileOrigin"), FLinearColor((Tile % 4) * .25f, (Tile / 4) * .25f, 0, 0));
    Material->SetScalarParameterValue(TEXT("TileSize"), .25f);
    Material->SetScalarParameterValue(TEXT("RepeatX"), Repeat);
    Material->SetScalarParameterValue(TEXT("SoftDepth"), SoftDepth);
    Material->SetScalarParameterValue(TEXT("Gain"), Gain * Tint.A);
    return Material;
}

UStaticMeshComponent* ADuelArena::LightPlane(const FVector& At, const FVector2D& Size, const FRotator& Rotation,
    EDuelLight Shape, const FLinearColor& Tint, float Gain, float SoftDepth, float Repeat)
{
    UStaticMeshComponent* Surface = DuelGeometry::Shape(this, SceneRoot, EShape::Plane, At,
        FVector(Size.X, Size.Y, 100), LightMaterial(Shape, Tint, Gain, SoftDepth, Repeat), Rotation);
    Surface->SetCastShadow(false);
    return Surface;
}

UMaterialInstanceDynamic* ADuelArena::SpellFacet(bool bShadow)
{
    auto& Cached = bShadow ? ShadeFacet : FrostFacet;
    if (Cached) return Cached;
    UMaterialInterface* Parent = DuelGeometry::ArtMaterial(TEXT("M_NorthSpellFacet"));
    Cached = UMaterialInstanceDynamic::Create(Parent, this);
    if (Cached)
    {
        Cached->SetVectorParameterValue(TEXT("Tint"), bShadow ?
            FLinearColor(.095f, .006f, .13f) : FLinearColor(.016f, .095f, .31f));
        Cached->SetVectorParameterValue(TEXT("HotFacet"), bShadow ?
            FLinearColor(3.75f, .22f, 2.48f) : FLinearColor(.81f, 3.20f, 5.75f));
    }
    return Cached;
}

UMaterialInstanceDynamic* ADuelArena::SpellHalo(bool bShadow)
{
    auto& Cached = bShadow ? ShadeHalo : FrostHalo;
    if (!Cached)
    {
        const FLinearColor Tint = bShadow ? FLinearColor(1.f, .095f, .56f) : FLinearColor(.16f, .65f, 1.f);
        Cached = LightMaterial(EDuelLight::Bloom, Tint, bShadow ? .81f : .98f, .12f);
    }
    return Cached.Get();
}

UProceduralMeshComponent* ADuelArena::LightMesh(UMaterialInterface* Material)
{
    UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
    Mesh->SetupAttachment(SceneRoot);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCastShadow(false);
    Mesh->SetMaterial(0, Material);
    AddInstanceComponent(Mesh);
    Mesh->RegisterComponent();
    return Mesh;
}

void ADuelArena::Animate(USceneComponent* Component, float Life, const FVector& EndScale,
    const FVector& Velocity, float Gravity, float Spin, bool bWarning)
{
    FEffect& Effect = Effects.AddDefaulted_GetRef();
    Effect.Component = Component;
    if (UPrimitiveComponent* Surface = Cast<UPrimitiveComponent>(Component))
    {
        Effect.Material = Cast<UMaterialInstanceDynamic>(Surface->GetMaterial(0));
    }
    Effect.StartScale = Component->GetRelativeScale3D();
    Effect.Velocity = Velocity;
    Effect.ScaleMultiplier = EndScale;
    Effect.Life = Life;
    Effect.Gravity = Gravity;
    Effect.Spin = Spin;
    Effect.bWarning = bWarning;
}

void ADuelArena::CombatAtmosphere(const FVector& At, const FVector& Axis, const FVector2D& Extent,
    const FLinearColor& Tint, float Life, bool bStrike, bool bLane, float Inner)
{
    const float Space = static_cast<float>(bLane ? Extent.GetMin() : Extent.X - Inner);
    if (Life < .09f || Space < 26.f || Particles.Num() >= 820) return;

    const FVector Forward = bLane ? Axis.GetSafeNormal2D(UE_SMALL_NUMBER, FVector::ForwardVector) :
        BurstCameraDirection(this, At).GetSafeNormal2D(UE_SMALL_NUMBER, FVector::ForwardVector);
    const FVector Side = FVector::CrossProduct(FVector::UpVector, Forward);
    const float Peak = FMath::Max(.01f, FMath::Max3(Tint.R, Tint.G, Tint.B));
    const FLinearColor Color(Tint.R / Peak, Tint.G / Peak, Tint.B / Peak, 1);
    const float Seconds = bStrike ? FMath::Clamp(Life * 1.18f, .20f, .57f) : FMath::Min(Life, .48f);
    const float Height = FMath::Clamp(Space * .36f, 29.f, 325.f);
    const int32 Knots = bLane ? FMath::Clamp(FMath::RoundToInt32(Extent.X / (bStrike ? 145. : 220.)), 3, 8) :
        (bStrike ? 7 : 4);
    DuelBurstFx::FPlan Plan;
    for (int32 I = 0; I < Knots; ++I)
    {
        const float T = (I + .5f) / Knots;
        const float Sign = I % 2 ? -1.f : 1.f;
        const float Angle = FMath::DegreesToRadians(57.f + T * 246.f);
        const FVector Out = Forward * FMath::Cos(Angle) + Side * FMath::Sin(Angle);
        const float Radius = Inner + Space * (.47f + .029f * FMath::Sin(I * 1.91f));
        const FVector Offset = bLane ? Forward * (Extent.X * .70 * (2.f * T - 1.f)) +
            Side * (Space * .22f * Sign) : Out * Radius;
        const FVector Fan = bLane ? -Forward * .72f + Side * (.28f * Sign) : Out * Sign;
        const float Drift = FMath::Min(55.f, Space * .045f / Seconds);
        const FVector Base = At + Offset + FVector(0, 0, 15.f + Height * (.18f + (I % 3) * .12f));
        // Reserve room for billboard spread and drift, including around a safe hole.
        const float Clearance = bLane ?
            static_cast<float>(FMath::Min(Extent.X * (1. - .70 * FMath::Abs(2.f * T - 1.f)), Extent.Y - Space * .22f)) :
            static_cast<float>(FMath::Min(Extent.X - Radius, double(Radius - Inner)));
        const float Width = FMath::Min3(280.f, Space * (bLane ? .46f : .31f), (Clearance - Drift * Seconds) / 1.28f);
        const FVector Plume = Fan * Drift + FVector(0, 0, 28.f + Height * (bStrike ? .38f : .15f));
        const FLinearColor Smoke = FMath::Lerp(FLinearColor(.10f, .12f, .17f), Color, I % 3 ? .42f : .68f)
            .CopyWithNewOpacity(bStrike ? .34f : .18f);
        Plan.Emissions.Add({EDuelLight::Mist, Base, Plume, Fan,
            FVector2D(Width, Width * (.60f + .06f * (I % 3))), Smoke, Seconds, -12.f, Sign * (26.f + (I % 3) * 14.f)});

        const FVector Lift = Fan * Drift * .64f + FVector(0, 0, 104.f + Height * (bStrike ? .66f : .27f));
        const float Length = FMath::Clamp(Space * (.19f + .045f * (I % 3)), 11.f, 123.f);
        const FLinearColor Spark = FMath::Lerp(Color, FLinearColor(.93f, .89f, 1.f), I % 3 == 0 ? .37f : .12f)
            .CopyWithNewOpacity(bStrike ? .78f : .38f);
        Plan.Emissions.Add({bStrike ? EDuelLight::Spark : EDuelLight::Mote, Base, Lift, Lift,
            bStrike ? FVector2D(Length, FMath::Min(15.f, Length * .16f)) : FVector2D(FMath::Min(18.f, Length * .53f)),
            Spark, Seconds * .81f, bStrike ? 184.f : 49.f, 0.f});
    }
    QueueBurstPlan(Plan, Particles, [this](const DuelBurstFx::FEmission& E)
    {
        Emit(E.Shape, E.At, E.Velocity, E.Size, E.Tint, E.Life, E.Gravity, E.Axis, E.Spin);
    });
}

void ADuelArena::Ring(const FVector& At, float Radius, const FLinearColor& Color,
    float Life, float EndScale, float Width)
{
    if (bNorthOrigin && Radius >= 86.f)
    {
        GroundWarning(At, FVector::ForwardVector, FVector2D(Radius), Color, Life, EndScale, false, true);
        return;
    }
    const double Size = 2 * Radius / .88;
    UStaticMeshComponent* Glow = LightPlane(At, FVector2D(Size), FRotator::ZeroRotator,
        EDuelLight::Ring, Color, FMath::Clamp(Width * .61f, 1.75f, 5.1f), .08f);
    Animate(Glow, Life, FVector(EndScale, EndScale, 1), FVector::ZeroVector, 0.f, 0.f, EndScale == 1.f);
}

void ADuelArena::Slash(const FVector& At, float Yaw, float Radius, bool bBoss, const FLinearColor& Tint, EDuelMark Mark)
{
    const FLinearColor Tone = Tint == FLinearColor::Transparent ? (bBoss ? HotPink : Red) : Tint;
    const FVector Forward = FRotator(0, Yaw, 0).Vector();
    CombatAtmosphere(At + Forward * (Radius * .31f), Forward,
        FVector2D(Radius * .65f, Radius * .58f), Tone, bBoss ? .43f : .31f, true, true);
    if (Mark != EDuelMark::Default)
    {
        const bool bBone = Mark == EDuelMark::BoneRake || Mark == EDuelMark::BoneSlam;
        const bool bGlass = Mark == EDuelMark::FrostHook || Mark == EDuelMark::FrostReap;
        const bool bFinish = Mark == EDuelMark::BoneSlam || Mark == EDuelMark::HoundTurn || Mark == EDuelMark::FrostReap;
        UMaterialInstanceDynamic* Body = UMaterialInstanceDynamic::Create(DuelGeometry::ArtMaterial(TEXT("M_BladeWake")), this);
        if (Body)
        {
            Body->SetVectorParameterValue(TEXT("Tint"), Tone);
        }
        const FVector Axis = FRotator(0, Yaw, 0).Vector();
        const int32 RibbonCount = bBone || bGlass ? 2 : 3;
        for (int32 I = 0; I < RibbonCount; ++I)
        {
            const float Offset = (I - (RibbonCount - 1) * .5f);
            UProceduralMeshComponent* Cut = Ribbon(At + FVector(0, 0, bBone ? 23.f + I * 31.f : I * 15.f),
                Radius * (1.f - I * .08f), Radius * (bBone ? .25f : bGlass ? .115f : .085f),
                bFinish ? -116.f : -83.f, bFinish ? 106.f : 78.f,
                bBone ? 130.f : bGlass ? 72.f : 31.f + I * 7.f, Body);
            Cut->SetRelativeRotation(FRotator(bBone ? -18.f : bGlass ? 13.f * Offset : 3.f,
                Yaw + Offset * (bGlass ? 25.f : 10.f), bBone ? Offset * 31.f : bGlass ? Offset * 18.f : 11.f));
            Animate(Cut, bFinish ? .35f : .27f, FVector(1.f, 1.f, .72f),
                Axis * (bGlass ? 48.f : 72.f) + FVector(0, 0, bFinish ? -32.f : 27.f), 0, Offset * 28.f);
        }
        for (int32 I = 0; I < (bFinish ? 23 : 12); ++I)
        {
            const FVector Out = FRotator(0, Yaw - 90.f + I * (bFinish ? 8.f : 15.f), 0).Vector();
            const FVector Flow = Out * (180.f + (I % 4) * 64.f) + FVector(0, 0, bBone || bGlass ? 156.f : 53.f);
            Emit(bBone || bGlass ? EDuelLight::Feather : EDuelLight::Spark,
                At + Out * Radius * (.41f + .12f * (I % 4)), Flow,
                FVector2D((bBone ? 91 : 51) + I % 4 * 19, bBone ? 29 : bGlass ? 14 : 8),
                Tone, bFinish ? .49f : .32f, 245.f, bBone ? Out + FVector::UpVector : Flow, bGlass ? 98.f : 0.f);
        }
        return;
    }
    const FRotator Heading(0, Yaw, bBoss ? 29.f : -21.f);
    const float Peak = FMath::Max(1.f, FMath::Max3(Tone.R, Tone.G, Tone.B));
    auto* Edge = UMaterialInstanceDynamic::Create(DuelGeometry::ArtMaterial(TEXT("M_BladeWake")), this);
    if (Edge)
    {
        Edge->SetVectorParameterValue(TEXT("Tint"), FLinearColor(Tone.R / Peak, Tone.G / Peak, Tone.B / Peak));
    }
    UProceduralMeshComponent* Cut = Ribbon(At, Radius, Radius * .38f, -116.f, 91.f, bBoss ? 116.f : 76.f, Edge);
    Cut->SetRelativeRotation(Heading);
    Animate(Cut, .27f, FVector(1.04, 1.04, .74), Heading.Vector() * 43.f, 0.f, 138.f);
    UProceduralMeshComponent* Echo = Ribbon(At + FVector(0, 0, 17), Radius * .91f, Radius * .23f,
        -88.f, 61.f, 55.f, LightMaterial(EDuelLight::Filament, Tone, 1.46f, 34.f));
    Echo->SetRelativeRotation(Heading);
    Animate(Echo, .38f, FVector(1.09, 1.09, .61), Heading.Vector() * 47.f, 0.f, 81.f);
    for (int32 Index = 0; Index < (bBoss ? 34 : 18); ++Index)
    {
        const float Angle = Yaw + Random.FRandRange(-93.f, 78.f);
        const FVector Dir = FRotator(0, Angle, 0).Vector();
        const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Dir);
        const FVector Position = At + Dir * (Radius * Random.FRandRange(.77f, 1.f)) + FVector(0, 0, Random.FRandRange(1, 40));
        const FVector Velocity = Tangent * Random.FRandRange(180.f, bBoss ? 560.f : 420.f) +
            Dir * Random.FRandRange(68.f, 158.f) + FVector(0, 0, Random.FRandRange(20.f, 145.f));
        Emit(Index % 5 == 0 ? EDuelLight::Feather : EDuelLight::Spark, Position, Velocity,
            FVector2D(Random.FRandRange(22.f, bBoss ? 76.f : 54.f), Random.FRandRange(3.f, 8.f)),
            Tone, Random.FRandRange(.24f, .59f), 210.f, Velocity);
    }
}

void ADuelArena::Thrust(const FVector& At, const FVector& Axis, float Reach, const FLinearColor& Tint, bool bSkill,
    EDuelMark Mark)
{
    const FVector Across = FVector::CrossProduct(FVector::UpVector, Axis);
    if (Mark == EDuelMark::Tether)
    {
        for (int32 I = 0; I < 7; ++I)
        {
            const FVector Link = At + Axis * (Reach * (I + .5f) / 7.f);
            const FVector Rise = FVector::UpVector * 15.f;
            const FVector Offset = Across * 17.f * (I % 2 ? 1.f : -1.f);
            Emit(EDuelLight::Filament, Link + Offset + Rise, FVector::ZeroVector,
                FVector2D(Reach / 7.f + 4.f, 9), Tint, .15f, 0, Axis);
            Emit(EDuelLight::Star, Link - Offset - Rise, FVector::ZeroVector, FVector2D(18), Tint, .15f);
        }
        return;
    }
    CombatAtmosphere(At + Axis * (Reach * .47f), Axis, FVector2D(Reach * .46f, Reach * .28f),
        Tint, bSkill ? .42f : .28f, true, true);
    if (Mark == EDuelMark::Prism || Mark == EDuelMark::IceLance)
    {
        for (int32 I = -1; I <= 1; ++I)
        {
            const FVector Tip = (Axis + Across * (.23f * I)).GetSafeNormal();
            Emit(EDuelLight::Feather, At + Axis * (Reach * .46f) + Across * (I * 37.f), Tip * 190.f,
                FVector2D(Reach * (I == 0 ? 1.09f : .57f), I == 0 ? 50.f : 27.f),
                Tint, I == 0 ? .29f : .20f, 0, Tip);
        }
        Flash(At + Axis * 130.f, Tint, 8600.f, .28f, 490.f);
        return;
    }
    if (Mark == EDuelMark::Reversal || Mark == EDuelMark::CrossThread)
    {
        for (int32 I = 0; I < 4; ++I)
        {
            const FVector Claw = At + Axis * (Reach * (.15f + I * .22f));
            const FVector Aim = (Axis + Across * (I % 2 ? .23f : -.23f)).GetSafeNormal();
            Emit(EDuelLight::Filament, Claw, Axis * 152.f, FVector2D(Reach * .38f, 22.f - I * 3.f),
                Tint, .25f + I * .023f, 0, Aim);
        }
        Burst(At + Axis * (Reach * .83f), Tint, 11, 430.f, true);
        Flash(At + Axis * 130.f, Tint, 8000.f, .27f, 510.f);
        return;
    }
    for (int32 Layer = -1; Layer <= 1; ++Layer)
    {
        const float Length = Reach * (Layer == 0 ? 1.14f : .76f);
        Emit(EDuelLight::Shaft, At + Axis * (Length * .48f) + Across * (Layer * 24.f),
            Axis * (bSkill ? 154.f : 74.f), FVector2D(Length, Layer == 0 ? 66.f : 25.f),
            Tint, bSkill ? .42f : .27f, 0, Axis);
    }
    if (bSkill)
    {
        Burst(At + Axis * (Reach - 26.f), Tint, 55, 550.f, true);
        Flash(At + Axis * 130.f, Tint, 14500.f, .46f, 650.f);
    }
}

USceneComponent* ADuelArena::WingSweep(const FVector& At, const FVector& Axis, float Radius, float HalfArc,
    const FLinearColor& Tint, float Life, bool bStrike)
{
    USceneComponent* Mark = DrawSpellMark(EDuelMark::WingLane, At, Axis, FVector2D(Radius), Tint, Life,
        bStrike, false, 0.f, 1.f, 0.f, HalfArc);
    if (!bStrike) return Mark;

    const float Yaw = Axis.Rotation().Yaw;
    auto* Material = UMaterialInstanceDynamic::Create(DuelGeometry::ArtMaterial(TEXT("M_BladeWake")), this);
    if (Material) Material->SetVectorParameterValue(TEXT("Tint"), Tint);
    auto* Sweep = Ribbon(At + FVector(0, 0, 41), Radius, Radius * .17f, Yaw - HalfArc, Yaw + HalfArc, 63.f, Material);
    Animate(Sweep, .24f, FVector(1, 1, .69), FVector::ZeroVector, 0.f, 0.f);
    for (int32 I = 0; I < 14; ++I)
    {
        const float T = (I + .5f) / 14.f;
        const FVector Out = Axis.RotateAngleAxis((2.f * T - 1.f) * HalfArc, FVector::UpVector);
        const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Out);
        const FVector Point = At + Out * (Radius * (.57f + .09f * (I % 4))) + FVector(0, 0, 33 + 13 * (I % 3));
        const FVector Flight = Tangent * (203.f + (I % 4) * 36.f) + Out * 65.f + FVector(0, 0, 100);
        Emit(EDuelLight::Feather, Point, Flight, FVector2D(91 + 15 * (I % 4), 19),
            Tint, .36f, 219.f, Tangent, 57.f, false, T * .11f);
        if (I % 3 == 0)
            Emit(EDuelLight::Mist, Point - FVector(0, 0, 25), Flight * .32, FVector2D(94, 35),
                Tint.CopyWithNewOpacity(.24f), .36f, 20.f, Tangent, 23.f, false, T * .11f);
    }
    return Mark;
}

void ADuelArena::ClawWake(const FVector& At, const FVector& Axis, float HalfWidth, int32 Phase)
{
    const FVector Across = FVector::CrossProduct(FVector::UpVector, Axis);
    const FLinearColor Blood(1.f, .13f, .27f, .85f);
    const FLinearColor Ember(1.f, .57f, .54f, .89f);
    CombatAtmosphere(At - Axis * 42.f, Axis, FVector2D(142.f + Phase * 16.f, HalfWidth),
        Blood, .36f + Phase * .04f, true, true);
    for (int32 I = -1; I <= 1; ++I)
    {
        const FVector Trail = At + Across * (I * HalfWidth * .62f);
        ArcStroke(Trail + FVector(0, 0, 25), Axis, FVector::UpVector,
            FVector2D(83.f + 14.f * Phase, 48.f + 8.f * Phase), -58.f, 112.f, 5,
            [&](const FVector& Point, const FVector& Tangent, float Length, float Taper)
            {
                Emit(EDuelLight::Filament, Point, -Axis * 135.f + FVector(0, 0, 28),
                    FVector2D(Length, (15.f + 3.f * Phase) * Taper), I == 0 ? Ember : Blood,
                    .25f + .07f * Taper, 80.f, Tangent);
            });
        const FVector Drift = -Axis * 227.f + Across * (I * 75.f) + FVector(0, 0, 126.f);
        Emit(EDuelLight::Feather, Trail, Drift, FVector2D(36, 11), Blood, .59f, 170.f, Drift, I * 99.f);
    }
    Burst(At, Blood, 8 + 2 * Phase, 430.f + 40.f * Phase, true);
}

void ADuelArena::Sigil(const FVector& At, float Radius, const FLinearColor& Tint, float Windup,
    EDuelMark Mark, const FVector& Axis)
{
    if (Mark != EDuelMark::Default || bNorthOrigin)
    {
        DrawSpellMark(Mark, At, Axis, FVector2D(Radius), Tint, Windup, false);
        return;
    }
    Ring(At, Radius, Tint, Windup, 1.f, 4.3f);
    UStaticMeshComponent* Plate = LightPlane(At + FVector(0, 0, .9f), FVector2D(Radius * 2.08),
        FRotator::ZeroRotator, EDuelLight::Seal, Tint, 2.7f, .07f);
    Animate(Plate, Windup, FVector::OneVector, FVector::ZeroVector, 0.f, 13.f, true);
    UStaticMeshComponent* Center = LightPlane(At + FVector(0, 0, 1.6f), FVector2D(Radius * 1.24),
        FRotator::ZeroRotator, EDuelLight::Bloom, Tint, .68f, .07f);
    Animate(Center, Windup, FVector(1.55, 1.55, 1), FVector::ZeroVector, 0.f, 0.f, true);
    for (int32 I = 0; I < 24; ++I)
    {
        const FVector Radial = FRotator(0, I * 15.f, 0).Vector();
        Emit(I % 4 ? EDuelLight::Mote : EDuelLight::Star, At + Radial * (Radius * .75f) + FVector(0, 0, 10),
            -Radial * Random.FRandRange(9, 36) + FVector(0, 0, Random.FRandRange(16, 58)),
            FVector2D(Random.FRandRange(5, 13)), Tint * .78f, Windup);
    }
}

void ADuelArena::Lane(const FVector& At, const FVector& Axis, float HalfLength, float HalfWidth,
    const FLinearColor& Tint, float Life, bool bStrike, bool bQuiet, bool bFeathered, EDuelMark Mark)
{
    const FVector Across = FVector::CrossProduct(FVector::UpVector, Axis);
    if (Mark != EDuelMark::Default || bNorthOrigin)
    {
        DrawSpellMark(Mark, At, Axis, FVector2D(HalfLength, HalfWidth), Tint, Life,
            bStrike && Mark != EDuelMark::SilentBell, true);
    }
    else
    {
        UStaticMeshComponent* Guide = LightPlane(At + FVector(0, 0, .8f),
            FVector2D(2 * HalfLength, 2 * HalfWidth / .954), FRotator(0, Axis.Rotation().Yaw, 0),
            EDuelLight::Lane, Tint, bStrike ? 4.6f : 1.8f, .08f, 12);
        Animate(Guide, Life, FVector::OneVector, FVector::ZeroVector, 0.f, 0.f, !bStrike);
    }
    if (Mark == EDuelMark::MirrorPanel)
    {
        for (int32 I = 0; I < 8; ++I)
        {
            const FVector Base = At + Axis * (HalfLength * (-.94f + I * .268f + Random.FRandRange(-.022f, .022f)));
            const float Side = I % 2 ? 1.f : -1.f;
            const FVector Fall = (FVector::UpVector + Axis * (.28f * Side) + Across * .12f).GetSafeNormal();
            const float Height = 245.f + (I % 4) * 136.f;
            Emit(EDuelLight::Feather, Base + Fall * (Height * .52f), Axis * (Side * 145.f) - FVector(0, 0, 191.f),
                FVector2D(Height, 37.f - I % 4 * 4.f), Tint * .72f, .22f, 0, Fall, Side * 62.f);
        }
        return;
    }
    if (!bStrike)
    {
        return;
    }

    if (Mark == EDuelMark::NightLane || Mark == EDuelMark::WingLane || Mark == EDuelMark::CrossVerdict ||
        Mark == EDuelMark::CrossAmbush || Mark == EDuelMark::PenNib || Mark == EDuelMark::SilentBell)
    {
        const bool bBell = Mark == EDuelMark::SilentBell;
        const bool bWings = Mark == EDuelMark::WingLane || Mark == EDuelMark::CrossVerdict;
        const int32 Count = bBell ? 10 : 12;
        for (int32 I = 0; I < Count; ++I)
        {
            const float T = 2.f * (I + .5f) / Count - 1.f;
            const float Side = I % 2 ? 1.f : -1.f;
            const FVector Base = At + Axis * (HalfLength * T) + Across * (HalfWidth * .38f * Side);
            if (Mark == EDuelMark::NightLane || bBell)
            {
                const FVector Pulse = Axis * (bBell ? 90.f * FMath::Sign(T) : 276.f);
                ArcStroke(Base + FVector(0, 0, bBell ? 31.f : 42.f), Across, FVector::UpVector,
                    FVector2D(HalfWidth * (bBell ? .44f : .56f), bBell ? 68.f : 104.f),
                    13.f, 167.f, 5, [&](const FVector& Point, const FVector& Tangent, float Length, float Taper)
                    {
                        Emit(EDuelLight::Filament, Point, Pulse, FVector2D(Length, 13.f * Taper),
                            Tint * (bBell ? .72f : 1.f), FMath::Min(.44f, Life), 0, Tangent);
                    });
            }
            else
            {
                const FVector Angle = (Axis * (bWings ? .37f : .85f) - Across * Side * (bWings ? .25f : .1f) +
                    FVector::UpVector * (bWings ? .88f : .23f)).GetSafeNormal();
                const float Length = bWings ? 186.f + (I % 4) * 57.f : 185.f + (I % 3) * 33.f;
                Emit(bWings ? EDuelLight::Feather : EDuelLight::Filament, Base + Angle * (Length * .46f),
                    Angle * (bWings ? 141.f : 248.f), FVector2D(Length, bWings ? 34.f : 22.f),
                    Tint, Life + .09f, 0, Angle, bWings ? Side * 32.f : 0.f);
                const FVector Trail = Axis * 74.f + FVector(0, 0, 86.f + I % 4 * 28.f);
                Emit(EDuelLight::Spark, Base + FVector(0, 0, 15.f), Trail, FVector2D(52, 10), Tint, Life, 125.f, Trail);
            }
        }
        if (!bQuiet)
        {
            Flash(At + FVector(0, 0, 89), Tint, 24500.f, Life + .20f, 1250.f);
            Sound(EDuelSound::Pulse, At);
        }
        return;
    }

    const int32 Pillars = bFeathered ? 12 : (bQuiet ? 17 : 29);
    for (int32 I = 0; I < Pillars; ++I)
    {
        const float Along = HalfLength * FMath::Lerp(-.94f, .94f, I / static_cast<float>(Pillars - 1));
        const FVector Base = At + Axis * Along + Across * Random.FRandRange(-HalfWidth * .69f, HalfWidth * .69f);
        const FVector Rise = bFeathered ? (Across * (I % 2 ? .51 : -.51) + FVector::UpVector).GetSafeNormal() :
            FVector(.24f, .07f, 1.f).GetSafeNormal();
        const float Height = bFeathered ? Random.FRandRange(160.f, 460.f) : Random.FRandRange(126.f, 290.f);
        const EDuelLight Blade = bFeathered && I % 3 == 1 ? EDuelLight::Feather : EDuelLight::Shaft;
        Emit(Blade, Base + Rise * (Height * .46f), Rise * 118.f,
            FVector2D(Height, Random.FRandRange(19, bFeathered ? 45 : 56)), Tint, Life + Random.FRandRange(.04f, .18f), 0, Rise);
        Emit(EDuelLight::Filament, Base + FVector(0, 0, Random.FRandRange(15, 62)), Axis * 251.f,
            FVector2D(Random.FRandRange(240, 395), bFeathered ? 36.f : 34.f), Tint * .46f, Life + .11f, 0, Axis);
        if (I % 4 == 1)
        {
            Burst(Base + FVector(0, 0, 26), Tint, 15, 365.f, true);
        }
    }
    if (!bQuiet)
    {
        Flash(At + FVector(0, 0, 89), Tint, 24500.f, Life + .20f, 1250.f);
        Sound(EDuelSound::Pulse, At);
    }
}

void ADuelArena::Rainfall(const TArray<FVector>& Marks, float Radius, const FLinearColor& Tint, bool bTribunal,
    EDuelMark Mark, const FVector& Axis)
{
    FVector Average = FVector::ZeroVector;
    TArray<FVector> Pages;
    TArray<int32> PageFaces;
    TArray<FVector2D> PageUVs;
    TArray<FLinearColor> PageColors;
    for (const FVector& At : Marks)
    {
        Average += At;
        if (Mark != EDuelMark::Default)
        {
            DrawSpellMark(Mark, At, Axis, FVector2D(Radius), Tint, .57f, true);
            const bool bWax = Mark == EDuelMark::Votive;
            const bool bGlass = Mark == EDuelMark::BrokenRecord;
            const bool bStars = Mark == EDuelMark::WitchRain;
            const bool bWingfall = Mark == EDuelMark::Wingfall;
            const bool bPage = Mark == EDuelMark::Tribunal;
            const bool bAtlas = Mark == EDuelMark::Atlas;
            const int32 Rays = bWax ? 4 : bGlass ? 9 : bStars ? 5 : bWingfall ? 10 : bAtlas ? 6 : 7;
            for (int32 I = 0; I < Rays; ++I)
            {
                const FVector Radial = Axis.RotateAngleAxis(I * 360.f / Rays, FVector::UpVector);
                const float Side = I % 2 ? 1.f : -1.f;
                const FVector Across = FVector::CrossProduct(FVector::UpVector, Axis);
                const FVector Base = At + (bWingfall ? Across * (Side * (.26f + .11f * (I / 2))) :
                    Radial * (bWax ? .19f : bAtlas ? .64f : I == 0 ? 0.f : .55f)) * Radius;
                const FVector Tilt = (FVector::UpVector + (bWingfall ? Across * (Side * .69f) :
                    Radial * (bGlass ? .94f : bPage ? .64f : .10f))).GetSafeNormal();
                const float Length = bWax ? 145.f + I * 54.f : bGlass ? 180.f + (I % 3) * 54.f :
                    bWingfall ? 262.f + (I % 3) * 98.f : bPage ? 364.f + I * 41.f : bAtlas ? 540.f : I == 0 ? 990.f : 500.f;
                const EDuelLight Shape = bGlass || bPage ? EDuelLight::Filament :
                    bWingfall || bWax ? EDuelLight::Feather : EDuelLight::Shaft;
                Emit(Shape, Base + Tilt * (Length * .47f), Tilt * (bWax ? 118.f : -146.f),
                    FVector2D(Length, bGlass ? 28.f : bWax ? 21.f : bWingfall ? 64.f : 39.f),
                    Tint, bWax ? .68f : .49f + (I % 3) * .04f, 0, Tilt, bGlass ? Side * 55.f : 0.f);
                const FVector Drift = (bGlass ? Across * (Side * 98.f) : Radial * 65.f) + FVector(0, 0, bWax ? 134.f : 83.f);
                Emit(bStars ? EDuelLight::Star : bGlass || bPage ? EDuelLight::Feather : EDuelLight::Spark,
                    Base + FVector(0, 0, bStars ? 210.f : 26.f), bStars ? -Tilt * 196.f : Drift,
                    bStars ? FVector2D(45) : FVector2D(48, bGlass || bPage ? 13.f : 9.f),
                    Tint, bWax ? .66f : .53f, 90.f, bStars ? Tilt : Drift, bGlass || bPage ? Side * 91.f : 0.f);
                if (bPage && I % 2 == 0)
                {
                    AppendVerdictPage(Base - Marks[0] + FVector(0, 0, 154.f + I * 108.f), Radial,
                        139.f + I * 22.f, 93.f + I * 11.f, Pages, PageFaces, PageUVs, PageColors);
                }
            }
            continue;
        }
        Ring(At, Radius * .27f, Tint, .59f, 1.f / .27f, 6.f);
        UStaticMeshComponent* Seal = LightPlane(At + FVector(0, 0, 1.6), FVector2D(Radius * 1.91f),
            FRotator::ZeroRotator, EDuelLight::Seal, Tint, 2.8f, .07f);
        Animate(Seal, .70f, FVector(1.12, 1.12, 1), FVector::ZeroVector, 0.f, 18.f);
        const int32 Rays = bTribunal ? 5 : 8;
        for (int32 I = 0; I < Rays; ++I)
        {
            const FVector Radial = FRotator(0, I * 137.5f, 0).Vector() * (I ? Random.FRandRange(28, Radius * .65f) : 0.f);
            const FVector BeamAxis = (FVector::UpVector + Radial * .00078).GetSafeNormal();
            const float Length = I == 0 ? (bTribunal ? 1440.f : 1160.f) : Random.FRandRange(390, 890);
            const float BeamWidth = I == 0 ? (bTribunal ? 67.f : 82.f) : Random.FRandRange(18, 41);
            Emit(EDuelLight::Shaft, At + Radial + BeamAxis * (Length * .50f), BeamAxis * -138.f,
                FVector2D(Length, BeamWidth), Tint * (I == 0 ? .89f : .52f), Random.FRandRange(.37f, .67f), 0, BeamAxis);
            if (bTribunal && I > 0)
            {
                const FVector Drift = Radial.GetSafeNormal() * Random.FRandRange(39.f, 75.f) - FVector(0, 0, 95.f);
                Emit(EDuelLight::Feather, At + Radial + FVector(0, 0, 195.f + 65.f * I), Drift,
                    FVector2D(37.f + I * 8.f, 18.f), Tint * .73f, .91f + I * .11f, 85.f, Drift, I % 2 ? 64.f : -71.f);
                Emit(EDuelLight::Star, At + Radial + FVector(0, 0, 100.f + I * 39.f),
                    Drift * .59f + FVector(0, 0, 143.f), FVector2D(25.f + I * 3.f),
                    FLinearColor(1.f, .89f, .66f, .77f), .63f + I * .07f, 75.f);
            }
        }
        Burst(At + FVector(0, 0, 27), Tint, 33, 437.f, true);
    }
    if (!Marks.IsEmpty())
    {
        Average /= Marks.Num();
        if (!Pages.IsEmpty())
        {
            UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(
                DuelGeometry::ArtMaterial(TEXT("M_BladeWake")), this);
            if (Material)
            {
                Material->SetVectorParameterValue(TEXT("Tint"), FLinearColor(.97f, .78f, .53f, .82f));
                auto* PageFlight = LightMesh(Material);
                PageFlight->SetRelativeLocation(Marks[0]);
                PageFlight->CreateMeshSection_LinearColor(0, Pages, PageFaces, {}, PageUVs, PageColors, {}, false, false);
                Animate(PageFlight, .87f, FVector::OneVector, FVector(0, 0, -251.f));
            }
        }
        Flash(Average + FVector(0, 0, 187), Tint, bTribunal ? 21800.f : 28000.f, .49f, 1650.f);
        Sound(EDuelSound::Pulse, Average);
    }
}

void ADuelArena::InwardWave(const FVector& At, float Outer, float Inner, float Life, const FLinearColor& Tint,
    EDuelMark Mark)
{
    if (Mark != EDuelMark::Default)
    {
        const bool bArchive = Mark == EDuelMark::Archive;
        DrawSpellMark(Mark, At, FVector::ForwardVector, FVector2D(Outer), Tint, Life, true, false,
            Outer - (bArchive ? 36.f : 28.f), Inner / Outer);
        for (int32 I = 0; I < 48; ++I)
        {
            const FVector Radial = FRotator(0, I * 7.5f, 0).Vector();
            const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Radial);
            const FVector Rim = At + Radial * Outer;
            const FVector Speed = -Radial * ((Outer - Inner) / Life);
            const FVector Slope = (Tangent * .40f + FVector::UpVector).GetSafeNormal();
            const float Height = bArchive ? 124.f + (I % 6) * 102.f : 32.f + (I % 5) * 27.f;
            Emit(bArchive ? EDuelLight::Feather : EDuelLight::Filament, Rim + FVector(0, 0, Height),
                Speed, FVector2D(bArchive ? 170.f + (I % 4) * 57.f : 249.f, bArchive ? 31.f : 38.f),
                Tint, Life, 0.f, bArchive ? Slope : Tangent + FVector(0, 0, .27),
                (I % 2 ? 1.f : -1.f) * (bArchive ? 88.f : 5.f));
            const FVector Wake = Speed * .78f + Tangent * (I % 2 ? 109.f : -109.f) + FVector(0, 0, 118.f);
            Emit(bArchive ? EDuelLight::Mote : EDuelLight::Spark, Rim + FVector(0, 0, Height * .67f),
                Wake, bArchive ? FVector2D(14, 10) : FVector2D(56, 9), Tint * .73f, Life * 1.14f, 100.f, Wake);
        }
        Flash(At + FVector(0, 0, 91), Tint, 29400.f, Life, Outer + 620.f);
        Sound(EDuelSound::Pulse, At);
        return;
    }
    Ring(At, Outer, Tint, Life, Inner / Outer, 8.f);
    for (int32 I = 0; I < 38; ++I)
    {
        const FVector Radial = FRotator(0, I * 360.f / 38.f, 0).Vector();
        const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Radial);
        const FVector Edge = At + Radial * Outer + FVector(0, 0, 43);
        const FVector Speed = -Radial * ((Outer - Inner) / Life);
        Emit(EDuelLight::Filament, Edge, Speed, FVector2D(205, 82), Tint, Life, 0, Tangent);
        Emit(EDuelLight::Feather, Edge + FVector(0, 0, 29), Speed * .82 + FVector(0, 0, 199),
            FVector2D(93, 51), FLinearColor(1.f, .49f, .62f, .78f), Life * .86f, 340.f, -Radial, 172.f);
    }
    Flash(At + FVector(0, 0, 91), Tint, 29400.f, Life, Outer + 620.f);
    Sound(EDuelSound::Pulse, At);
}

void ADuelArena::BoneWave(const FVector& At, float Inner, float Outer, float Life, bool bWarning, bool bSecond)
{
    const FLinearColor Tint = bSecond ? FLinearColor(.67f, .65f, 1.45f) : FLinearColor(1.48f, .96f, .49f);
    if (bWarning)
    {
        DrawSpellMark(EDuelMark::Procession, At, FVector::ForwardVector, FVector2D(Outer), Tint * .61f,
            Life, false, false, Inner);
        Ring(At, Inner, Tint, Life, Outer / Inner, 7.f);
        return;
    }
    constexpr int32 Count = 40;
    for (int32 I = 0; I < Count; ++I)
    {
        const FVector Axis = FRotator(0, I * 360.f / Count + (bSecond ? 4.5f : 0.f), 0).Vector();
        const FVector AtRim = At + Axis * Inner + FVector(0, 0, 26);
        const FVector Velocity = Axis * ((Outer - Inner) / Life);
        const FVector Lift = (FVector::UpVector + Axis * .52f).GetSafeNormal();
        Emit(EDuelLight::Feather, AtRim + FVector(0, 0, 38), Velocity + FVector(0, 0, 80),
            FVector2D(134 + I % 4 * 18, 35), Tint, Life, 200.f, Lift, I % 2 ? 19.f : -23.f);
        Emit(EDuelLight::Filament, AtRim, Velocity, FVector2D(147, 28), Tint * .69f, Life,
            0.f, FVector::CrossProduct(FVector::UpVector, Axis));
        if (I % 3 == 0)
        {
            Emit(EDuelLight::Spark, AtRim, Velocity + FVector(0, 0, 177), FVector2D(64, 11), Tint, Life, 340.f);
        }
    }
    Ring(At, Inner, Tint, Life, Outer / Inner, 12.f);
    Flash(At + FVector(0, 0, 89), Tint, bSecond ? 19300.f : 12900.f, .53f, Outer + 120.f);
    Sound(EDuelSound::Pulse, At);
}

void ADuelArena::TailSweepWarning(const FVector& At, float Yaw, float Reach, float Arc, float Life)
{
    const FLinearColor Tint(.88f, .07f, .25f, .73f);
    DrawSpellMark(EDuelMark::TailSweep, At, FRotator(0, Yaw, 0).Vector(), FVector2D(Reach), Tint, Life,
        false, false, DuelCourt::TailSweepInner, 1.f, 0.f, Arc);
}

void ADuelArena::TailSweepTrail(const FVector& At, const FVector& Axis, float Reach)
{
    const FLinearColor Tint(2.1f, .17f, .55f, .89f);
    const FVector Across = FVector::CrossProduct(FVector::UpVector, Axis);
    Emit(EDuelLight::Filament, At + Axis * (Reach * .55f) + FVector(0, 0, 25),
        Across * 146.f, FVector2D(Reach * .88f, 31), Tint, .18f, 0.f, Axis);
    ArcStroke(At + Axis * (Reach * .72f) + FVector(0, 0, 28), Axis, Across,
        FVector2D(Reach * .23f, Reach * .18f), -85.f, 74.f, 5,
        [&](const FVector& Point, const FVector& Tangent, float Length, float Taper)
        {
            Emit(EDuelLight::Filament, Point, Across * 114.f + FVector(0, 0, 36.f),
                FVector2D(Length, 26.f * Taper), Tint, .23f, 74.f, Tangent);
        });
    for (int32 I = 0; I < 5; ++I)
    {
        const FVector Tip = At + Axis * (Reach * Random.FRandRange(.75f, .98f));
        Emit(I % 2 ? EDuelLight::Filament : EDuelLight::Spark, Tip + FVector(0, 0, 26.f),
            Across * Random.FRandRange(130.f, 317.f) + FVector(0, 0, 78), FVector2D(43, 7), Tint, .31f, 145.f);
    }
}

void ADuelArena::GlassVerdict(const FVector& Safe, float Radius, float Life, bool bStrike)
{
    const FLinearColor Ice(.44f, .81f, 1.35f, .86f);
    const FLinearColor Glass(.70f, .67f, 1.30f, .81f);
    const FVector Origin = CourtOrigin();
    // The verdict hits the entire room outside the marked opening. Its warning
    // and strike mesh must cover the new walkable edges as well.
    const FBox2D Field = bNorthOrigin ? CourtBounds().ExpandBy(-FVector2D(30., 20.)) :
        FBox2D(FVector2D(DuelCourt::GateCrossX + 30., 20. - DuelCourt::HalfWidth),
            FVector2D(DuelCourt::HalfLength - 30., DuelCourt::HalfWidth - 20.));
    const FVector Floor = Origin + FVector(Field.GetCenter(), (Safe.Z - Origin.Z) + (bStrike ? 6.5 : 5.5));
    const FVector2D Extent = Field.GetExtent();
    TArray<FVector> Points;
    TArray<int32> Triangles;
    DuelSpellMarks::BuildGlassField(FVector2D(Safe - Floor), Extent, Radius, Points, Triangles);
    const FLinearColor Cracks = bStrike ? FLinearColor(.75f, 1.28f, 2.88f, .67f) : FLinearColor(.46f, 1.03f, 2.24f, .76f);
    auto* Fracture = LightMesh(DuelGeometry::ColorMaterial(this, Cracks, true));
    Fracture->SetRelativeLocation(Floor);
    Fracture->CreateMeshSection_LinearColor(0, Points, Triangles, {}, {}, {}, {}, false, false);
    Animate(Fracture, Life, FVector::OneVector, FVector::ZeroVector, 0, 0, !bStrike);
    Effects.Last().MaterialColor = Cracks;
    if (!bStrike)
    {
        for (int32 I = 0; I < 14; ++I)
        {
            const FVector Radial = FRotator(0, I * 360.f / 14.f, 0).Vector();
            const FVector Edge = Safe + Radial * (Radius + 28.f);
            if (!Field.IsInside(FVector2D(Edge + Radial * 64.f - Origin))) continue;
            const float Delay = Life * .044f * ((I * 3) % 10);
            const FVector Growth = Radial * 76.f + FVector(0, 0, 18);
            Emit(EDuelLight::Filament, Edge + FVector(0, 0, 15), Growth, FVector2D(51, 8),
                Ice, Life - Delay, 0.f, Radial, 0.f, false, Delay);
        }
        return;
    }
    const APlayerCameraManager* CameraView = UGameplayStatics::GetPlayerCameraManager(this, 0);
    TArray<FVector> ShardVertices;
    TArray<int32> ShardIndices;
    TArray<FVector2D> ShardUVs;
    TArray<FLinearColor> ShardColors;
    FSpellHalo FallingHalo;
    for (int32 I = 0; I < 62; ++I)
    {
        const FVector Point = Origin + FVector(Random.FRandRange(Field.Min.X, Field.Max.X), Random.FRandRange(Field.Min.Y, Field.Max.Y), 9);
        // Keep the entire broken-glass crown outside the safe opening, not just its center.
        if (FVector::DistSquared2D(Point, Safe) <= FMath::Square(Radius + 110.f))
        {
            continue;
        }
        const FVector Out = (Point - Safe).GetSafeNormal2D();
        const FVector Fall = (FVector::UpVector + Out * .16f).GetSafeNormal();
        const float Clear = static_cast<float>(FVector::Dist2D(Point, Safe)) - Radius;
        const float Height = Random.FRandRange(143.f, FMath::Min(I % 4 ? 505.f : 825.f, Clear * 2.5f));
        const FVector Center = Point + Fall * Height * .52f;
        const float Near = CameraView ? FMath::GetMappedRangeValueClamped(FVector2D(190, 570), FVector2D(.14, 1),
            FVector::Dist(CameraView->GetCameraLocation(), Center)) : 1.f;
        FLinearColor Tint = I % 3 ? Glass : Ice;
        Tint.A *= Near;
        Emit(I % 4 ? EDuelLight::Filament : EDuelLight::Feather, Center, FVector(0, 0, -290) + Out * 47.f,
            FVector2D(Height, Random.FRandRange(16.f, 44.f)), Tint, Life, 0.f, Fall, I % 2 ? 31.f : -37.f);
        if (I % 4 == 3 && Clear > 225.f && Near > .50f)
        {
            const FVector Slope = (-FVector::UpVector + Out * .29f).GetSafeNormal();
            AppendIceShard(Point - Origin + FVector(0, 0, 320.f + (I % 5) * 218.f), Slope,
                281.f + I % 5 * 87.f, 34.f + I % 4 * 11.f, ShardVertices, ShardIndices, ShardUVs, ShardColors, &FallingHalo);
        }
        if (I % 7 == 0)
        {
            ArcStroke(Point + FVector(0, 0, 52), Out, FVector::UpVector, FVector2D(53, 72), 12.f, 164.f, 6,
                [&](const FVector& Position, const FVector& Tangent, float Length, float Taper)
                {
                    Emit(EDuelLight::Filament, Position, Out * 48.f + FVector(0, 0, 70),
                        FVector2D(Length, 9.f * Taper), Tint * .76f, Life * .72f, 115.f, Tangent);
                });
            for (int32 Shard = 0; Shard < 4; ++Shard)
            {
                const FVector Drift = Out * (65.f + Shard * 27.f) + FVector(0, 0, 118.f + Shard * 59.f);
                Emit(EDuelLight::Feather, Point + FVector(0, 0, 75.f + Shard * 66.f), Drift,
                    FVector2D(39.f + Shard * 16.f, 11), Tint * .74f, Life + .17f, 210.f, Drift, 79.f);
            }
        }
    }
    if (!ShardVertices.IsEmpty())
    {
        UMaterialInstanceDynamic* Material = SpellFacet(false);
        if (Material)
        {
            auto* Shards = LightMesh(Material);
            Shards->SetRelativeLocation(Origin);
            Shards->CreateMeshSection_LinearColor(0, ShardVertices, ShardIndices, {}, ShardUVs, ShardColors, {}, false, false);
            FallingHalo.Install(*Shards, SpellHalo(false));
            Animate(Shards, Life, FVector::OneVector, FVector(0, 0, -568.f));
        }
    }
    Flash(Safe + FVector(0, 0, 108), Ice, 13700.f, Life * .67f, 640.f);
    Sound(EDuelSound::Pulse, Origin);
}

USceneComponent* ADuelArena::SnareThread(const FVector& At, const FVector& Axis, float Length, float Width, float Life)
{
    const FLinearColor Tint(1.42f, .11f, .44f, .93f);
    return DrawSpellMark(EDuelMark::Snare, At, Axis, FVector2D(Length, Width), Tint, Life, false, true);
}

void ADuelArena::PenLattice(const FVector& At, const FVector& Axis, float HalfLength, float Gap, float Width, float Life, bool bStrike)
{
    const FVector Across = FVector::CrossProduct(FVector::UpVector, Axis);
    const FLinearColor Nib(1.00f, .71f, .38f, .91f);
    const FLinearColor Ink(.70f, .20f, .32f, .86f);
    const int32 Lines = Gap > 0.f ? 2 : 1;
    for (int32 Side = 0; Side < Lines; ++Side)
    {
        const FVector Center = At + Across * Gap * (Side ? 1.f : -1.f);
        const FVector Write = Axis * (Side ? -1.f : 1.f);
        DrawSpellMark(EDuelMark::WrittenVerdict, Center, Write, FVector2D(HalfLength, Width), Nib, Life, bStrike, true);
        for (int32 I = 0; I < 6; ++I)
        {
            const float Delay = Life * (.07f + .088f * I);
            const FVector Start = Center + Write * (HalfLength * (-.83f + .28f * I)) +
                Across * (Width * .19f * FMath::Sin(I * 2.f)) + FVector(0, 0, 15);
            const FVector Drift = Write * 134.f + FVector(0, 0, bStrike ? 47.f : 4.f);
            Emit(EDuelLight::Filament, Start, Drift, FVector2D(FMath::Min(132.f, HalfLength * .16f), bStrike ? 17.f : 9.f),
                Nib, Life - Delay, 0.f, Write, 0.f, false, Delay);
        }
        if (!bStrike)
        {
            continue;
        }
        // Curved pen strokes stay within the marked lane; the gap is never filled with glow.
        for (int32 Arch = 0; Arch < 3; ++Arch)
        {
            const FVector Crest = Center + Write * (HalfLength * (-.63f + .58f * Arch)) + FVector(0, 0, 28);
            ArcStroke(Crest, Write, FVector::UpVector, FVector2D(HalfLength * .19f, 145.f + 39.f * (Arch % 2)),
                8.f, 172.f, 5, [&](const FVector& Point, const FVector& Tangent, float Length, float Taper)
                {
                    Emit(EDuelLight::Filament, Point, Write * 74.f + FVector(0, 0, 56),
                        FVector2D(Length, FMath::Min(13.f * Taper, Width * .14f)), Nib, Life * .63f,
                        45.f, Tangent, 0.f, false, Life * .095f * Arch);
                });
        }
        for (int32 I = 0; I < 7; ++I)
        {
            const FVector Base = Center + Write * (HalfLength * FMath::Lerp(-.80f, .75f, I / 6.f));
            const FVector Drift = Write * (55.f + I * 9.f) + FVector(0, 0, 157.f + 19.f * (I % 3));
            Emit(EDuelLight::Feather, Base + FVector(0, 0, 65), Drift,
                FVector2D(68, 17), I % 3 ? Ink : Nib, Life + .08f, 265.f, Drift, I % 2 ? 83.f : -83.f);
            if (I % 3 == 1)
            {
                const double Cloud = FMath::Min(Width * .59, 83.);
                Emit(EDuelLight::Mist, Base + FVector(0, 0, 36), Write * 53.f + FVector(0, 0, 28),
                    FVector2D(Cloud, Cloud * .44), Ink.CopyWithNewOpacity(.26f), Life * .85f, 0.f, Write, 18.f);
            }
        }
    }
    if (bStrike)
    {
        Flash(At + FVector(0, 0, 125), Nib, 12500.f, .26f, 580.f);
        Sound(EDuelSound::Pulse, At);
    }
}

void ADuelArena::Detonate(const FVector& At, float Radius, const FLinearColor& Tint, float Life,
    EDuelMark Mark, const FVector& Axis)
{
    if (Mark != EDuelMark::Default)
    {
        DrawSpellMark(Mark, At, Axis, FVector2D(Radius), Tint, Life, true);
        if (Mark == EDuelMark::StarTrack)
        {
            for (int32 I = 0; I < 6; ++I)
            {
                const int32 Beat = I / 2;
                const FVector Out = Axis.RotateAngleAxis(I * 137.5f, FVector::UpVector);
                const FVector Base = At + Out * (Radius * (.21f + .12f * Beat)) + FVector(0, 0, 15);
                const FVector Lift = Out * 109.f + FVector(0, 0, 150);
                Emit(EDuelLight::Spark, Base, Lift, FVector2D(59.f, 7.f), Tint, .23f, 430.f, Lift,
                    0.f, false, Life * (.40f + .17f * Beat));
            }
            Flash(At + FVector(0, 0, 76), Tint, 11500.f, .24f, Radius + 140.f);
            Sound(EDuelSound::Pulse, At);
            return;
        }
        const bool bCross = Mark == EDuelMark::GildedCross;
        const bool bMoon = Mark == EDuelMark::MoonCrescent;
        if (bCross || bMoon)
        {
            for (int32 I = 0; I < 32; ++I)
            {
                const double Angle = bCross ? (I % 4) * 90. : -117. + 234. * I / 31.;
                const FVector Out = Axis.RotateAngleAxis(Angle, FVector::UpVector);
                const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Out);
                const double Band = bCross ? .16 + .10 * (I / 4) : .87 + .034 * (I % 3);
                const FVector Base = At + Out * (Radius * Band);
                const FVector Rise = FVector::UpVector * (145. + 38. * (I % 5));
                const FVector Flow = bCross ? Rise + Out * 85. : Rise + Tangent * 175.;
                Emit(bCross ? EDuelLight::Filament : EDuelLight::Spark, Base, Flow,
                    FVector2D(bCross ? 104. : 179., bCross ? 18. : 10.), Tint, Life, 75.f, Flow, 0.f);
                if (I % 3 == 0)
                    Emit(EDuelLight::Mist, Base + FVector(0, 0, 36), Flow * .39,
                        FVector2D(143., 67.), Tint * .26f, Life + .23f, 0.f, Tangent, 21.f);
            }
            Flash(At + FVector(0, 0, 115), Tint, 21500.f, Life, Radius + 500.f);
            Sound(EDuelSound::Pulse, At);
            return;
        }
        const bool bInk = Mark == EDuelMark::InkWell || Mark == EDuelMark::InkRupture;
        const bool bInward = Mark == EDuelMark::Bind || Mark == EDuelMark::InkWell;
        const bool bPrism = Mark == EDuelMark::PrismSeal;
        const bool bFold = Mark == EDuelMark::Scroll || Mark == EDuelMark::HaloCore || bPrism;
        const bool bWing = Mark == EDuelMark::Leap || Mark == EDuelMark::Flank || Mark == EDuelMark::Takeoff;
        const bool bCrater = Mark == EDuelMark::EmberCrater;
        const bool bPursuit = Mark == EDuelMark::Pursuit;
        const int32 Count = bPrism ? 30 : bInk || bWing ? 24 : bPursuit ? 12 : 18;
        for (int32 I = 0; I < Count; ++I)
        {
            const float Angle = bWing ? (I % 2 ? 1.f : -1.f) * (35.f + 8.f * (I / 2)) :
                bPursuit ? -57.f + I * 10.f : I * 360.f / Count;
            const FVector Out = Axis.RotateAngleAxis(Angle, FVector::UpVector);
            const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Out);
            const FVector Base = At + Out * (Radius * (bInward ? .76f : .20f + (I % 4) * .14f));
            const float Curl = bInk ? 168.f : bFold ? 99.f : 41.f;
            const float Height = bWing || bFold ? 210.f + I % 6 * 50.f : bCrater ? 172.f + I % 4 * 29.f : 40.f + I % 4 * 38.f;
            const FVector Flow = Out * (bInward ? -Radius * .59f / Life : Radius * .58f / Life) +
                Tangent * ((bInward ? -1.f : 1.f) * Curl) + FVector(0, 0, Height);
            const FVector Aim = bFold ? (Tangent + FVector::UpVector * .65f).GetSafeNormal() :
                bWing ? (Out + FVector::UpVector * .56f).GetSafeNormal() : Flow;
            Emit(bWing ? EDuelLight::Feather : bCrater ? EDuelLight::Spark : EDuelLight::Filament,
                Base + FVector(0, 0, bFold ? 65.f : bCrater ? 12.f : 35.f), Flow,
                FVector2D((bWing ? 104.f : 67.f) + I % 4 * 21.f, bWing ? 37.f : bCrater ? 12.f : 16.f),
                Tint, Life * (1.05f + .09f * (I % 3)), bInward ? 105.f : 270.f, Aim, bFold ? 184.f : bInk ? 64.f : 0.f);
            if (I % 2 == 0)
            {
                Emit(bInk || bPrism ? EDuelLight::Mist : EDuelLight::Mote, Base + FVector(0, 0, 28), Flow * .44f,
                    bInk || bPrism ? FVector2D(126, 47) : FVector2D(10), Tint * (bInk || bPrism ? .23f : .64f),
                    Life * 1.35f, 24.f, Tangent, bInk ? 91.f : 0.f);
            }
        }
        Flash(At + FVector(0, 0, 92), Tint, 9800.f + Radius * 23.f, Life, 420.f + Radius * 1.7f);
        Sound(Radius > 180.f ? EDuelSound::Pulse : EDuelSound::Impact, At);
        return;
    }
    const FLinearColor Gold(1.f, .71f, .32f, .76f);
    const FLinearColor Halo = FMath::Lerp(Tint, Gold, .39f);
    Ring(At, Radius, Halo, Life * .47f, 1.03f, 7.f);
    Ring(At + FVector(0, 0, 1.9f), Radius * .24f, Halo, Life, 4.14f, 6.f);
    UStaticMeshComponent* Crest = LightPlane(At + FVector(0, 0, 1.6f), FVector2D(Radius * 1.78f),
        FRotator::ZeroRotator, EDuelLight::Seal, Halo, 1.9f, .07f);
    Animate(Crest, Life, FVector(1.07, 1.07, 1), FVector::ZeroVector, 0.f, 23.f);
    const auto Plan = DuelBurstFx::Make(DuelBurstFx::TintStyle(Tint), DuelBurstFx::EKind::Detonation,
        At, Radius, Life, BurstCameraDirection(this, At), GetTypeHash(At));
    QueueBurstPlan(Plan, Particles, [this](const DuelBurstFx::FEmission& E)
    {
        Emit(E.Shape, E.At, E.Velocity, E.Size, E.Tint, E.Life, E.Gravity, E.Axis, E.Spin);
    });
    Flash(At + FVector(0, 0, 92), Halo, 9800.f + Radius * 23.f, Life, 420.f + Radius * 1.7f);
    Sound(Radius > 180.f ? EDuelSound::Pulse : EDuelSound::Impact, At);
}

void ADuelArena::BossBurst(const FVector& At, float Radius, const FLinearColor& Tint, float Strength)
{
    const ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    const ADuelFighter* Boss = Mode ? Mode->GetBossFighter() : nullptr;
    const auto Style = Boss ? DuelBurstFx::CharacterStyle(Boss->SelectedCharacter()) : DuelBurstFx::TintStyle(Tint);
    const auto Plan = DuelBurstFx::Make(Style, DuelBurstFx::EKind::BossRelease,
        At, Radius, .78f, BurstCameraDirection(this, At), GetTypeHash(At));
    QueueBurstPlan(Plan, Particles, [this](const DuelBurstFx::FEmission& E)
    {
        Emit(E.Shape, E.At, E.Velocity, E.Size, E.Tint, E.Life, E.Gravity, E.Axis, E.Spin);
    });
    // Same impact-time event and existing light budget. Residual smoke is cosmetic.
    Flash(At + FVector(0, 0, 174), Plan.Light, (43000.f + Radius * 33.f) * Strength, .64f, Radius + 990.f);
    Sound(EDuelSound::Pulse, At);
}

void ADuelArena::WitchTransformation(const FVector& At, EDuelCharacter Character, int32 Phase,
    float Duration, bool bRelease)
{
    const bool bFinal = Phase >= 3;
    const float Scale = bFinal ? 1.19f : 1.f;
    const float Life = FMath::Clamp(Duration, .35f, bRelease ? 1.30f : 1.50f);
    FVector Forward = FVector::ForwardVector;
    double Nearest = TNumericLimits<double>::Max();
    for (TActorIterator<ADuelFighter> It(GetWorld()); It; ++It)
    {
        const double Distance = FVector::DistSquared(It->GetActorLocation(), At);
        if (It->bBoss && !It->IsHidden() && It->SelectedCharacter() == Character && Distance < Nearest)
        {
            Forward = It->GetActorForwardVector().GetSafeNormal2D();
            Nearest = Distance;
        }
    }
    const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
    const float FacingYaw = Forward.Rotation().Yaw;
    const FLinearColor Ivory(.91f, .87f, .77f, .47f);
    const FLinearColor Ink(.055f, .025f, .045f, .52f);
    const FLinearColor Blood(.64f, .015f, .041f, .56f);
    const FLinearColor Frost(.60f, .76f, .89f, .39f);

    // A few curved, translucent fragment batches supply dark body and lit edges.
    // They are cosmetic meshes with no collision; each batch has at most 12 fragments.
    const auto Fragments = [&](bool bMirror, int32 Count, float Side, float Radius, float Height,
        const FLinearColor& Tint, float Seconds, bool bGather)
    {
        UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(
            DuelGeometry::ArtMaterial(TEXT("M_BladeWake")), this);
        if (!Material) return;
        Material->SetVectorParameterValue(TEXT("Tint"), Tint);
        UProceduralMeshComponent* Mesh = LightMesh(Material);
        Mesh->SetRelativeLocation(At - Forward * 65.f);
        TArray<FVector> Positions;
        TArray<int32> Indices;
        TArray<FVector2D> UVs;
        TArray<FLinearColor> Colors;
        for (int32 I = 0; I < Count; ++I)
        {
            const float T = (I + .5f) / Count;
            const float Angle = (.16f + T * .94f) * PI;
            const FVector Out = Right * (Side * FMath::Sin(Angle)) - Forward * FMath::Abs(FMath::Cos(Angle));
            const FVector Along = (Out * .48f + FVector::UpVector * (bMirror ? .92f : .68f)).GetSafeNormal();
            const FVector Across = FVector::CrossProduct(Along, Forward + Right * Side * .4f).GetSafeNormal();
            const FVector Center = Out * Radius * (.64f + .36f * FMath::Sin(T * 7.1f)) +
                FVector(0, 0, Height + T * (bMirror ? 175.f : 100.f));
            const float Length = (bMirror ? 43.f : 38.f) + (I % 4) * 9.f;
            const float Width = (bMirror ? 15.f : 22.f) + (I % 3) * 4.f;
            const int32 Base = Positions.Num();
            // Uneven ridges and a folded centre avoid flat rectangular paper or diamond billboards.
            Positions.Append({Center - Along * Length, Center - Along * Length * .22f - Across * Width,
                Center + Along * Length * .67f - Across * Width * .56f,
                Center + Along * Length, Center + Along * Length * .24f + Across * Width * .77f,
                Center - Along * Length * .61f + Across * Width * .43f,
                Center + Forward * (bMirror ? 11.f : 19.f) + Along * Length * .08f});
            UVs.Append({FVector2D(.02, .96), FVector2D(.18, .20), FVector2D(.76, .32),
                FVector2D(.98, .96), FVector2D(.68, .94), FVector2D(.28, .93), FVector2D(.48, .52)});
            for (int32 Edge = 0; Edge < 6; ++Edge)
            {
                Indices.Append({Base + 6, Base + Edge, Base + (Edge + 1) % 6});
            }
            for (int32 Vertex = 0; Vertex < 7; ++Vertex)
            {
                Colors.Add(FLinearColor(1.f, 1.f, 1.f, bGather ? .36f : .72f));
            }
        }
        Mesh->CreateMeshSection_LinearColor(0, Positions, Indices, {}, UVs, Colors, {}, false, false);
        Animate(Mesh, Seconds, bGather ? FVector(.57f, .57f, 1.24f) :
            bMirror ? FVector(.75f, .75f, 1.35f) : FVector(1.72f, 1.72f, .96f),
            FVector(0, 0, bMirror ? (bGather ? 50.f : 124.f) : 62.f), 0.f, Side * (bMirror ? 23.f : 37.f));
    };

    const auto Mist = [&](int32 Count, float Radius, float Height, const FLinearColor& Tint,
        bool bGather, float Width, float Curl)
    {
        for (int32 I = 0; I < Count; ++I)
        {
            // Keep the near/front quarter open so the face, feet and floor remain readable.
            const float Angle = FMath::DegreesToRadians(56.f + I * 248.f / FMath::Max(1, Count - 1));
            const FVector Out = Forward * FMath::Cos(Angle) + Right * FMath::Sin(Angle);
            const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Out);
            const FVector Flow = Out * (bGather ? -Radius * .61f : Radius * .38f) +
                Tangent * (bGather ? 78.f : 126.f) + FVector(0, 0, bGather ? 22.f : 62.f);
            Emit(EDuelLight::Mist, At + Out * Radius + FVector(0, 0, Height + (I % 3) * 31.f), Flow,
                FVector2D(Width, Width * .33f), Tint, Life * (1.f + (I % 3) * .07f),
                0.f, Tangent, I % 2 ? Curl : -Curl);
        }
    };

    if (Character == EDuelCharacter::Ema)
    {
        Mist(bRelease ? 10 : 8, (bRelease ? 265.f : 330.f) * Scale, bRelease ? 93.f : 43.f,
            bRelease ? FLinearColor(.19f, .13f, .16f, .36f) : Ink, !bRelease, 230.f * Scale, 66.f);
        for (float Side : {-1.f, 1.f})
        {
            Fragments(false, bRelease ? 12 : 8, Side, (bRelease ? 210.f : 330.f) * Scale,
                bRelease ? 125.f : 75.f, bRelease ? FLinearColor(.78f, .59f, .47f) : FLinearColor(.30f, .22f, .25f),
                Life, !bRelease);
        }
        const int32 Count = bRelease ? 32 : 14;
        for (int32 I = 0; I < Count; ++I)
        {
            const float Side = I % 2 ? 1.f : -1.f;
            const float T = static_cast<float>(I / 2) / (Count / 2);
            const FVector Fan = (Right * Side * (.50f + .50f * T) - Forward * (.63f - T * .22f)).GetSafeNormal();
            const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Fan) * Side;
            const FVector Start = At + Fan * (bRelease ? 165.f : 350.f) * Scale +
                FVector(0, 0, 95.f + 155.f * T);
            const FVector Velocity = Fan * (bRelease ? 260.f + 150.f * T : -180.f) +
                Tangent * 130.f + FVector(0, 0, bRelease ? 235.f - T * 90.f : 64.f);
            Emit(EDuelLight::Feather, Start, Velocity, FVector2D((52.f + 55.f * T) * Scale, 14.f + 10.f * T),
                Ivory.CopyWithNewOpacity(bRelease ? .48f : .21f), Life * (bRelease ? 1.22f : .90f),
                bRelease ? 180.f : 0.f, Velocity, Side * (65.f + 30.f * T));
            if (I % (bRelease ? 4 : 2) == 0)
            {
                Emit(EDuelLight::Mote, Start, Velocity * .46f, FVector2D(7.f),
                    FLinearColor(.72f, .35f, .24f, .34f), Life * 1.15f, 25.f);
            }
        }
        if (bRelease) Flash(At - Forward * 100.f + FVector(0, 0, 190), Ivory, 14500.f * Scale, .24f, 540.f * Scale);
    }
    else if (Character == EDuelCharacter::Hiro)
    {
        Mist(bRelease ? 12 : 9, (bRelease ? 225.f : 360.f) * Scale, bRelease ? 54.f : 28.f,
            FLinearColor(.026f, .008f, .018f, bRelease ? .67f : .51f), !bRelease,
            (bRelease ? 310.f : 230.f) * Scale, 91.f);
        if (bRelease)
        {
            for (int32 I = 0; I < 3; ++I)
            {
                UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(
                    DuelGeometry::ArtMaterial(TEXT("M_BladeWake")), this);
                if (!Material) continue;
                Material->SetVectorParameterValue(TEXT("Tint"), FLinearColor(.74f, .012f, .039f));
                UProceduralMeshComponent* Claw = Ribbon(At - Forward * (130.f + I * 18.f) +
                    FVector(0, 0, 68.f + I * 43.f), (290.f + I * 57.f) * Scale,
                    (37.f + I * 7.f) * Scale, -115.f, 27.f, 130.f, Material);
                Claw->SetRelativeRotation(FRotator(24.f + I * 8.f, FacingYaw + 128.f + I * 7.f, -33.f));
                Animate(Claw, .56f + .06f * I, FVector(1.22f, 1.22f, .86f),
                    Forward * 180.f + Right * (35.f - I * 32.f) + FVector(0, 0, -27), 0.f, -43.f);
            }
        }
        const int32 Count = bRelease ? 18 : 12;
        for (int32 I = 0; I < Count; ++I)
        {
            const float Angle = I * 2.399963f;
            const FVector Out = (Right * FMath::Sin(Angle) - Forward * FMath::Abs(FMath::Cos(Angle))).GetSafeNormal();
            const FVector Start = At + Out * (bRelease ? 155.f : 300.f) * Scale + FVector(0, 0, 35.f + I % 4 * 20.f);
            const FVector Flow = bRelease ? Forward * (260.f + I * 7.f) + Out * 230.f + FVector(0, 0, 64.f) :
                -Out * 180.f + FVector(0, 0, 38.f);
            Emit(bRelease ? EDuelLight::Spark : EDuelLight::Mote, Start, Flow,
                bRelease ? FVector2D(23, 3.5) : FVector2D(8), Blood.CopyWithNewOpacity(bRelease ? .54f : .20f),
                Life * .80f, bRelease ? 95.f : 0.f, Flow);
            if (bRelease && I % 2 == 0)
            {
                Emit(EDuelLight::Mote, Start, Flow * .37f, FVector2D(8), Blood * .68f, Life * 1.14f, 20.f);
            }
        }
        if (bRelease) Flash(At - Forward * 100.f + FVector(0, 0, 110), Blood, 10800.f * Scale, .18f, 490.f * Scale);
    }
    else
    {
        Mist(bRelease ? 11 : 7, (bRelease ? 280.f : 350.f) * Scale, bRelease ? 150.f : 54.f,
            Frost.CopyWithNewOpacity(bRelease ? .26f : .18f), !bRelease, 280.f * Scale, 43.f);
        for (int32 Layer = 0; Layer < (bRelease ? 4 : 3); ++Layer)
        {
            const float Side = Layer % 2 ? -1.f : 1.f;
            Fragments(true, bRelease ? 12 : 6, Side, (320.f - Layer * 39.f) * Scale,
                70.f + Layer * 75.f, FLinearColor(.65f, .83f, 1.f), Life * (1.f + .07f * Layer), true);
        }
        const int32 Count = bRelease ? 32 : 16;
        for (int32 I = 0; I < Count; ++I)
        {
            const float Angle = I * 2.399963f;
            const FVector Out = (Right * FMath::Sin(Angle) - Forward * FMath::Abs(FMath::Cos(Angle))).GetSafeNormal();
            const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Out);
            const FVector Start = At + Out * (260.f + I % 5 * 23.f) * Scale + FVector(0, 0, 50.f + I % 7 * 38.f);
            const FVector Flow = -Out * (bRelease ? 125.f : 110.f) + Tangent * 74.f +
                FVector(0, 0, bRelease ? 220.f : 95.f);
            Emit(EDuelLight::Mote, Start, Flow, FVector2D(bRelease ? 10.f : 7.f),
                Frost.CopyWithNewOpacity(bRelease ? .38f : .20f), Life * 1.15f, -12.f);
            if (bRelease && I % 3 == 0)
            {
                Emit(EDuelLight::Spark, Start, Flow * .63f, FVector2D(19, 3), Frost,
                    Life * .72f, 0.f, Tangent + FVector::UpVector);
            }
        }
        if (bRelease) Flash(At - Forward * 135.f + FVector(0, 0, 235), Frost, 12500.f * Scale, .24f, 530.f * Scale);
    }
    if (bRelease) Sound(EDuelSound::Pulse, At);
}

void ADuelArena::StormFront(const FVector& At, float Radius, const FLinearColor& Tint, float Life, EDuelMark Mark)
{
    constexpr float Start = 70.f;
    const FVector Expansion(Radius / Start, Radius / Start, 1);
    if (Mark == EDuelMark::Archive)
    {
        DrawSpellMark(Mark, At, FVector::ForwardVector, FVector2D(Start), Tint, Life,
            true, false, Start * (1.f - 35.f / Radius), Expansion.X);
        constexpr int32 Sections = 32;
        for (int32 I = 0; I < Sections; ++I)
        {
            const FVector Out = FRotator(0, I * 360.f / Sections + 6.f, 0).Vector();
            const FVector Side = FVector::CrossProduct(FVector::UpVector, Out);
            const FVector Velocity = Out * ((Radius - Start) / Life);
            const FVector Rim = At + Out * Start;
            const FVector Lean = (FVector::UpVector - Out * .31f + Side * (I % 2 ? .25f : -.25f)).GetSafeNormal();
            Emit(EDuelLight::Feather, Rim + FVector(0, 0, 144.f + I % 6 * 76.f), Velocity,
                FVector2D(330.f + I % 4 * 86.f, 33.f + I % 4 * 7.f), Tint, Life, 0.f, Lean, I % 2 ? 76.f : -72.f);
            const FVector Drift = Velocity * .46f + FVector(0, 0, 188.f);
            Emit(EDuelLight::Spark, Rim + FVector(0, 0, 80.f + I % 7 * 47.f), Drift,
                FVector2D(62, 8), Tint * .83f, Life * 1.19f, 185.f, Drift);
            Emit(EDuelLight::Mote, Rim + FVector(0, 0, 48.f + I % 7 * 53.f), Velocity * .19f + Side * 42.f,
                FVector2D(16.f + I % 3 * 3.f), Tint * .51f, Life * 1.5f, 80.f);
        }
        return;
    }
    Ring(At + FVector(0, 0, 4), Start, Tint, Life, Expansion.X, 7.f);
    auto* Material = UMaterialInstanceDynamic::Create(DuelGeometry::ArtMaterial(TEXT("M_BladeWake")), this);
    if (Material)
    {
        Material->SetVectorParameterValue(TEXT("Tint"), Tint);
    }
    const float Spin = Random.FRandRange(0.f, 360.f);
    for (int32 Sector = 0; Sector < 5; ++Sector)
    {
        const float Angle = Spin + Sector * 72.f;
        UProceduralMeshComponent* Crest = Ribbon(At + FVector(0, 0, 8 + (Sector % 2) * 3), Start,
            25.f - (Sector % 3) * 3.f, Angle, Angle + 85.f, 36.f + (Sector % 3) * 15.f, Material);
        Animate(Crest, Life, Expansion, FVector::ZeroVector, 0.f, 25.f);
        if (Mark == EDuelMark::Storm)
        {
            for (int32 I = 0; I < 9; ++I)
            {
                const FVector Out = FRotator(0, Angle + I * 8.3f, 0).Vector();
                const FVector Across = FVector::CrossProduct(FVector::UpVector, Out);
                const FVector Flow = Out * ((Radius - Start) / Life) + Across * 76.f;
                const FVector Tilt = (Across * .65f + FVector::UpVector).GetSafeNormal();
                Emit(EDuelLight::Feather, At + Out * Start + FVector(0, 0, 115.f + 82.f * (I % 6)), Flow,
                    FVector2D(180.f + 97.f * (I % 4), 38.f + 6.f * (I % 3)), Tint * .76f, Life, 0.f, Tilt, 53.f);
            }
        }
    }
    const auto Plan = DuelBurstFx::Make(DuelBurstFx::TintStyle(Tint), DuelBurstFx::EKind::Front,
        At, Radius, Life, BurstCameraDirection(this, At), GetTypeHash(At));
    QueueBurstPlan(Plan, Particles, [this](const DuelBurstFx::FEmission& E)
    {
        Emit(E.Shape, E.At, E.Velocity, E.Size, E.Tint, E.Life, E.Gravity, E.Axis, E.Spin);
    });
}

void ADuelArena::EmberRecoil(const FVector& At, const FVector& Axis, float Radius, const FLinearColor& Tint,
    float Life, bool bStrike)
{
    const float HalfArc = DuelCourt::EmberRecoilHalfArc;
    DrawSpellMark(EDuelMark::EmberRecoil, At, Axis, FVector2D(Radius), Tint, Life,
        bStrike, false, Radius * DuelCourt::EmberRecoilInner, 1.f, 0.f, HalfArc);
    if (!bStrike) return;
    SableSweep(At, Axis, Radius, Tint, HalfArc, EDuelMark::SaltArc);
    for (int32 I = 0; I < 12; ++I)
    {
        const float Angle = FMath::Lerp(-HalfArc, HalfArc, (I + .5f) / 12.f);
        const FVector Out = Axis.RotateAngleAxis(Angle, FVector::UpVector);
        const FVector Flow = Out * (165.f + I % 4 * 32.f) + FVector(0, 0, 100.f + I % 3 * 46.f);
        const FVector Base = At + Out * (Radius * (.55f + (I % 3) * .13f));
        Emit(EDuelLight::Mist, Base + FVector(0, 0, 26.f), Flow,
            FVector2D(175.f, 78.f), Tint * .25f, .78f, 14.f, Flow);
    }
    Sound(EDuelSound::Pulse, At);
}

void ADuelArena::SableSweep(const FVector& At, const FVector& Axis, float Radius, const FLinearColor& Tint, float HalfArc,
    EDuelMark Mark)
{
    auto* Edge = UMaterialInstanceDynamic::Create(DuelGeometry::ArtMaterial(TEXT("M_BladeWake")), this);
    if (Edge)
    {
        Edge->SetVectorParameterValue(TEXT("Tint"), Tint);
    }
    const float Yaw = Axis.Rotation().Yaw;
    if (Mark == EDuelMark::SaltArc)
    {
        UProceduralMeshComponent* Fan = Ribbon(At + FVector(0, 0, 7.f), Radius, Radius * .24f,
            Yaw - HalfArc, Yaw + HalfArc, 34.f, Edge);
        Animate(Fan, .44f, FVector(1, 1, 1.22f));
        for (int32 I = 0; I < 18; ++I)
        {
            const FVector Out = Axis.RotateAngleAxis(-HalfArc + (I + .5f) * 2.f * HalfArc / 18.f, FVector::UpVector);
            const FVector Flow = Out * (215.f + I % 4 * 37.f) + FVector(0, 0, 123.f + I % 3 * 74.f);
            Emit(I % 3 ? EDuelLight::Spark : EDuelLight::Feather, At + Out * (Radius * .75f) + FVector(0, 0, 12.f),
                Flow, FVector2D(53.f + I % 4 * 14.f, I % 3 ? 8.f : 19.f), Tint, .47f, 570.f, Flow);
        }
        Flash(At + Axis * 145.f + FVector(0, 0, 75), Tint, 9700.f, .31f, Radius + 190.f);
        return;
    }
    for (const float Side : {-1.f, 1.f})
    {
        auto* Crescent = Ribbon(At + FVector(0, 0, 38.f), Radius, Radius * .29f,
            Yaw - HalfArc, Yaw + HalfArc, 75.f, Edge);
        Crescent->SetRelativeRotation(FRotator(0, 0, Side * 14.f));
        Animate(Crescent, .31f, FVector(1, 1, .69));
    }
    Burst(At + Axis * (Radius * .61f) + FVector(0, 0, 43), Tint, 31, 372.f, true);
    Flash(At + Axis * 145.f + FVector(0, 0, 75), Tint, 9500.f, .32f, Radius + 190.f);
}

void ADuelArena::RitualTrace(const FVector& At, bool bFinal)
{
    const FLinearColor Silver(.53f, .79f, 1.f, .84f);
    Pillar(At, bFinal ? 273.f : 86.f, false);
    Flash(At + FVector(0, 0, 215), Silver, bFinal ? 32000.f : 8500.f, bFinal ? 2.25f : .71f, 870.f);
    Burst(At + FVector(0, 0, 93), Silver, bFinal ? 105 : 31, bFinal ? 380.f : 205.f, true);
    if (bFinal)
    {
        const FVector Witness = CourtOrigin() + FVector(DuelCourt::VerdictDeskX + 285.f, 0, DuelCourt::VerdictRise + 287.f);
        Emit(EDuelLight::Mist, Witness, FVector(0, 0, 12), FVector2D(482, 346), Silver, 8.f);
        Emit(EDuelLight::Shaft, Witness, FVector(0, 0, 9), FVector2D(593, 135), Silver, 6.6f, 0, FVector::UpVector);
        Emit(EDuelLight::Star, Witness + FVector(0, 0, 132), FVector(0, 0, 3), FVector2D(185), Silver, 6.6f);
        Sound(EDuelSound::VerdictBell, Witness);
    }
}

void ADuelArena::Halo(const FVector& At, float Inner, float Outer, const FLinearColor& Tint, float Life, bool bStrike,
    EDuelMark Mark)
{
    if (Mark != EDuelMark::Default)
    {
        DrawSpellMark(Mark, At, FVector::ForwardVector, FVector2D(Outer), Tint, Life, bStrike, false, Inner);
        if (!bStrike)
        {
            return;
        }
        if (Mark == EDuelMark::ReturningFrost)
        {
            const float Band = Outer - Inner;
            for (int32 I = 0; I < 16; ++I)
            {
                const FVector Out = FRotator(0, I * 22.5f, 0).Vector();
                const FVector Tilt = (Out + FVector::UpVector * .37f).GetSafeNormal();
                const FVector Start = At + Out * (Inner + Band * (.20f + .06f * (I % 3))) + FVector(0, 0, 23.f);
                const FVector Flow = Out * (Band * .56f / Life) + FVector(0, 0, 80.f + (I % 4) * 18.f);
                Emit(EDuelLight::Feather, Start, Flow, FVector2D(54.f + I % 4 * 6.f, 15.f),
                    Tint, Life, 82.f, Tilt, I % 2 ? -38.f : 38.f);
            }
            Flash(At + FVector(0, 0, 84.f), Tint, 9500.f, Life * .76f, Outer + 140.f);
            Sound(EDuelSound::Cut, At);
            return;
        }
        const bool bOffering = Mark == EDuelMark::Offering;
        const bool bAbsolution = Mark == EDuelMark::Absolution;
        const int32 Count = bOffering ? 16 : bAbsolution ? 28 : 38;
        for (int32 I = 0; I < Count; ++I)
        {
            const FVector Out = FRotator(0, I * 360.f / Count, 0).Vector();
            const FVector Side = FVector::CrossProduct(FVector::UpVector, Out);
            const FVector Base = At + Out * FMath::Lerp(Inner, Outer, .28f + I % 4 * .14f);
            const FVector Lean = (FVector::UpVector + Side * (bAbsolution ? .55f : -.41f) + Out * .31f).GetSafeNormal();
            const float Height = bOffering ? 96.f + I % 5 * 24.f : 272.f + I % 5 * 98.f;
            const FVector Rise = Lean * (bOffering ? 104.f : 184.f);
            Emit(bOffering ? EDuelLight::Spark : bAbsolution ? EDuelLight::Shaft : EDuelLight::Feather,
                Base + Lean * (Height * .45f), Rise, FVector2D(Height, bOffering ? 8.f : 33.f + I % 3 * 14.f),
                Tint, Life * 1.11f, bOffering ? 215.f : 0.f, Lean, bOffering ? 0.f : 51.f);
            if (I % 2 == 0)
            {
                Emit(EDuelLight::Feather, Base + FVector(0, 0, bOffering ? 16.f : 78.f), Rise * .78f + Side * 68.f,
                    FVector2D(bOffering ? 32.f : 98.f, bOffering ? 10.f : 24.f), Tint * .57f, Life * 1.32f,
                    180.f, Side, I % 4 ? 168.f : -168.f);
            }
        }
        Flash(At + FVector(0, 0, bOffering ? 92.f : 157.f), Tint, bOffering ? 8700.f : 20700.f, Life + .10f, Outer + 270.f);
        Sound(EDuelSound::Pulse, At);
        return;
    }
    Ring(At, Inner, Tint, Life, 1.f, 4.1f);
    Ring(At + FVector(0, 0, .7), Outer, Tint, Life, 1.f, 5.8f);
    UProceduralMeshComponent* Band = Ribbon(At + FVector(0, 0, 1.7), Outer, Outer - Inner, 0.f, 360.f, 0.f,
        LightMaterial(EDuelLight::Filament, Tint, bStrike ? 3.8f : .85f, 14.f, 5.f));
    Animate(Band, Life, FVector::OneVector, FVector::ZeroVector, 0.f, 0.f, !bStrike);
    if (!bStrike)
    {
        return;
    }
    for (int32 I = 0; I < 32; ++I)
    {
        const FVector Axis = FRotator(0, I * 11.25f, 0).Vector();
        const FVector Point = At + Axis * FMath::Lerp(Inner, Outer, I % 2 ? .23f : .71f);
        const FVector Aim = (Axis * .68 + FVector::UpVector).GetSafeNormal();
        const float Length = Random.FRandRange(290.f, 540.f);
        Emit(EDuelLight::Shaft, Point + Aim * (Length * .42f), Aim * 146.f,
            FVector2D(Length, Random.FRandRange(16.f, 29.f)), Tint * .79f, Life + .17f, 0.f, Aim);
        Emit(EDuelLight::Feather, Point + FVector(0, 0, 105.f), Axis * 116.f + FVector(0, 0, 95.f),
            FVector2D(42, 17), Tint * .54f, .90f, 90.f, Axis, I % 2 ? 48.f : -56.f);
    }
    Flash(At + FVector(0, 0, 157), Tint, 20700.f, Life + .1f, Outer + 270.f);
    Sound(EDuelSound::Pulse, At);
}

void ADuelArena::FrostReturn(const FVector& At, float Inner, float Outer, const FLinearColor& Tint)
{
    const float Life = .31f;
    const float Band = Outer - Inner;
    DrawSpellMark(EDuelMark::ReturningFrost, At, FVector::ForwardVector, FVector2D(Outer), Tint, Life, true, false, Inner);
    for (int32 I = 0; I < 16; ++I)
    {
        const FVector Out = FRotator(0, (I + .5f) * 22.5f, 0).Vector();
        const FVector Tilt = (-Out + FVector::UpVector * .19).GetSafeNormal();
        const FVector Position = At + Out * (Inner + Band * .80f) + FVector(0, 0, 22.f + (I % 3) * 7.f);
        const FVector Velocity = -Out * (Band * .58f / Life) + FVector(0, 0, 46.f + (I % 4) * 11.f);
        Emit(EDuelLight::Filament, Position, Velocity, FVector2D(61.f, 12.f), Tint, Life, 59.f, Tilt);
    }
    Sound(EDuelSound::FrostRecall, At);
}

void ADuelArena::Emit(EDuelLight Shape, const FVector& At, const FVector& Velocity, const FVector2D& Size,
    const FLinearColor& Tint, float Life, float Gravity, const FVector& Axis, float Spin, bool bOffering, float Delay)
{
    constexpr int32 MaxParticles = 1536;
    const int32 Budget = Shape == EDuelLight::Mist ? 820 : Shape == EDuelLight::Mote ? 1180 : MaxParticles;
    if (Particles.Num() < Budget && Life > KINDA_SMALL_NUMBER && Size.GetMin() > 0.)
    {
        const float Peak = FMath::Max(1.f, FMath::Max3(Tint.R, Tint.G, Tint.B));
        const FLinearColor Tone(Tint.R / Peak, Tint.G / Peak, Tint.B / Peak, FMath::Clamp(Tint.A, .02f, 1.f));
        Particles.Add({At, Velocity, Axis, Size, Tone, Shape, -FMath::Max(0.f, Delay), Life, Gravity, Spin, bOffering});
    }
}

void ADuelArena::TickRitualBlade(const ADuelFighter* Player, float DeltaSeconds)
{
    const bool bOffering = Player && Player->RitualOfferingSeconds() > 0.f;
    if (!bOffering && bRitualWasActive)
    {
        Particles.RemoveAllSwap([](const FParticle& Particle) { return Particle.bOffering; }, EAllowShrinking::No);
        for (int32 Index = BladeSamples.Num() - 1; Index >= 0; --Index)
        {
            if (BladeSamples[Index].bOffering)
            {
                BladeSamples.RemoveAt(Index, 1, EAllowShrinking::No);
                if (BladeSamples.IsValidIndex(Index))
                {
                    BladeSamples[Index].bStartsSwing = true;
                }
            }
        }
    }
    bRitualWasActive = bOffering;

    const USceneComponent* Grip = nullptr;
    if (bOffering)
    {
        if (Player->EquippedWeapon() == EDuelWeapon::RitualSword)
        {
            Grip = Player->Weapon.Get();
        }
        else if (Player->EquippedWeapon(EDuelHand::Left) == EDuelWeapon::RitualSword)
        {
            Grip = Player->LeftWeapon.Get();
        }
    }
    if (!Grip || (Grip == Player->LeftWeapon.Get() && Player->Move == EDuelMove::Flask))
    {
        if (RitualLight)
        {
            RitualLight->SetVisibility(false);
        }
        RitualParticleDelay = 0.f;
        return;
    }
    if (!RitualLight)
    {
        RitualLight = NewObject<UPointLightComponent>(this);
        RitualLight->SetupAttachment(SceneRoot);
        RitualLight->SetLightColor(FLinearColor(1.f, .023f, .064f));
        RitualLight->SetAttenuationRadius(250.f);
        RitualLight->SetCastShadows(false);
        AddInstanceComponent(RitualLight);
        RitualLight->RegisterComponent();
    }
    const FTransform Blade = Grip->GetComponentTransform();
    const FVector2D Span = DuelCourt::BladeSpan(EDuelWeapon::RitualSword);
    const FVector Axis = Blade.GetUnitAxis(EAxis::X);
    const FVector Center = Blade.TransformPosition(FVector((Span.X + Span.Y) * .5, 0, 0));
    const float Pulse = .5f + .5f * FMath::Sin(GetWorld()->GetTimeSeconds() * 9.f);
    RitualLight->SetWorldLocation(Center);
    RitualLight->SetIntensity(620.f + 330.f * Pulse);
    RitualLight->SetVisibility(true);

    RitualParticleDelay -= DeltaSeconds;
    if (RitualParticleDelay <= 0.f)
    {
        RitualParticleDelay = .045f;
        Emit(EDuelLight::Filament, Center, FVector::ZeroVector, FVector2D(150, 19 + Pulse * 7.f),
            FLinearColor(1.f, .07f, .19f, .40f), .09f, 0.f, Axis, 0.f, true);
        for (int32 I = 0; I < 3; ++I)
        {
            const FVector At = Blade.TransformPosition(FVector(Random.FRandRange(Span.X, Span.Y),
                Random.FRandRange(-9.f, 9.f), Random.FRandRange(-6.f, 6.f)));
            const FVector Velocity = Random.VRand() * 24.f + FVector(0, 0, 32.f);
            Emit(I == 0 ? EDuelLight::Spark : EDuelLight::Mote, At, Velocity,
                FVector2D(I == 0 ? 19.f : 9.f, I == 0 ? 2.5f : 6.f),
                FLinearColor(1.f, .045f, .12f, .8f), .28f + .15f * Pulse, 16.f, Velocity, 0.f, true);
        }
    }
}

void ADuelArena::Burst(const FVector& At, const FLinearColor& Color, int32 Count, float Speed, bool bPetals)
{
    for (int32 I = 0; I < Count; ++I)
    {
        const FVector Across = FRotator(0, Random.FRandRange(0, 360), 0).Vector();
        const FVector Origin = At + Across * Random.FRandRange(1.f, 34.f);
        const bool bFeather = bPetals && I % 3 == 0;
        const EDuelLight Type = bFeather ? EDuelLight::Feather : (I % 5 == 0 ? EDuelLight::Star : EDuelLight::Spark);
        const float Size = Random.FRandRange(5.f, bFeather ? 18.f : 12.f);
        const FVector Velocity = Across * Random.FRandRange(Speed * .18f, Speed) +
            FVector(0, 0, Random.FRandRange(Speed * .1f, Speed * .81f));
        Emit(Type, Origin, Velocity, FVector2D(Size * (Type == EDuelLight::Star ? 1.4f : 3.4f), Size),
            Color * Random.FRandRange(.62f, 1.09f), Random.FRandRange(.37f, bFeather ? 1.48f : .79f),
            bFeather ? 135.f : 346.f, FVector::ZeroVector, bFeather ? Random.FRandRange(-150.f, 150.f) : 0.f);
        if (bPetals && I % 2 == 0)
        {
            Emit(EDuelLight::Mote, Origin, Velocity * .30f, FVector2D(Size * 1.30f),
                Color * .47f, Random.FRandRange(.75f, 1.51f), 30.f);
        }
    }
}

void ADuelArena::Flash(const FVector& At, const FLinearColor& Color, float Intensity, float Life, float Radius)
{
    int32 ActiveLights = 0;
    int32 ActiveShadowLights = 0;
    for (const FEffect& Effect : Effects)
    {
        if (const UPointLightComponent* Existing = Cast<UPointLightComponent>(Effect.Component.Get()))
        {
            if (Effect.Age < Effect.Life)
            {
                ++ActiveLights;
                if (Existing->CastShadows) ++ActiveShadowLights;
            }
        }
    }
    const bool bCastFlashShadow = bCastleInterior && Intensity >= 14000.f;
    // Skip excess flashes instead of replacing shadowed indoor lights with leaking unshadowed ones.
    if (ActiveLights >= 6 || (bCastFlashShadow && ActiveShadowLights >= 2)) return;
    UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
    Light->SetupAttachment(SceneRoot);
    Light->SetMobility(EComponentMobility::Movable);
    Light->SetRelativeLocation(At);
    Light->SetAttenuationRadius(bCastleInterior && Intensity < 14000.f ? FMath::Min(Radius, 420.f) : Radius);
    const float Peak = FMath::Max(1.f, FMath::Max3(Color.R, Color.G, Color.B));
    Light->SetLightColor(FMath::Lerp(FLinearColor(Color.R / Peak, Color.G / Peak, Color.B / Peak),
        FLinearColor(1.f, .82f, .54f), .28f));
    Light->SetCastShadows(bCastFlashShadow);
    Light->SetCastVolumetricShadow(bCastFlashShadow);
    Light->SetVolumetricScatteringIntensity(FMath::GetMappedRangeValueClamped(FVector2D(20000, 85000),
        FVector2D(.55, 1.45), static_cast<double>(Intensity)));
    Light->SetIntensity(Intensity);
    AddInstanceComponent(Light);
    Light->RegisterComponent();
    Animate(Light, Life, FVector::OneVector);
    Effects.Last().LightIntensity = Intensity;
}

void ADuelArena::Pillar(const FVector& At, float Radius, bool bStorm, EDuelMark Mark)
{
    const float Time = bStorm ? .84f : .70f;
    const float Height = bStorm ? 1450.f : 1110.f;
    const FLinearColor Aura = bStorm ? FLinearColor(.89f, .66f, .42f, .94f) : FLinearColor(1.f, .61f, .31f, .88f);
    if (Mark == EDuelMark::Pillar)
    {
        DrawSpellMark(Mark, At, FVector::ForwardVector, FVector2D(Radius), Aura, .73f, true);
        for (int32 I = 0; I < 5; ++I)
        {
            const FVector Out = FRotator(0, I * 72.f, 0).Vector();
            const FVector Side = FVector::CrossProduct(FVector::UpVector, Out);
            const FVector Tilt = (FVector::UpVector + Out * .32f).GetSafeNormal();
            const FVector Lift = Tilt * (265.f + I % 3 * 57.f);
            const FVector Stem = At + Out * (Radius * .34f);
            const float Length = 860.f + I % 3 * 130.f;
            Emit(EDuelLight::Shaft, Stem + Tilt * (Length * .46f), Lift, FVector2D(Length, 70.f),
                Aura, Time, 0, Tilt);
            for (int32 Barb = 0; Barb < 7; ++Barb)
            {
                const FVector Feather = (Tilt + Side * (Barb % 2 ? .48f : -.48f)).GetSafeNormal();
                Emit(EDuelLight::Feather, Stem + Tilt * (90.f + Barb * 102.f), Lift * .67f,
                    FVector2D(140.f + Barb % 3 * 57.f, 34.f), Aura * .72f, Time + .15f, 220.f, Feather,
                    Barb % 2 ? 78.f : -78.f);
            }
        }
        Burst(At + FVector(0, 0, 52), Aura, 39, 456.f, true);
        Flash(At + FVector(0, 0, 135), Aura, 20700.f, Time + .20f, 1540.f);
        Sound(EDuelSound::Pulse, At);
        return;
    }
    for (int32 I = 0; I < (bStorm ? 21 : 14); ++I)
    {
        const FVector Offset = FRotator(0, I * 137.5f, 0).Vector() * (I == 0 ? 0.f : Random.FRandRange(27, 168));
        const FVector Axis = (FVector::UpVector + Offset * .0009).GetSafeNormal();
        const float Length = Height * (I == 0 ? 1.f : Random.FRandRange(.31f, .89f));
        Emit(EDuelLight::Shaft, At + Offset + Axis * (Length * .47f), Axis * 126.f,
            FVector2D(Length, I == 0 ? 151.f : Random.FRandRange(27.f, 88.f)),
            Aura * (I == 0 ? 1.f : .64f), Time * Random.FRandRange(.63f, 1.09f), 0, Axis);
    }
    Ring(At, bStorm ? 70.f : 125.f, Aura, Time, Radius / (bStorm ? 70.f : 125.f), 10.f);
    UStaticMeshComponent* Engraving = LightPlane(At + FVector(0, 0, 2), FVector2D(Radius * 1.66f),
        FRotator::ZeroRotator, EDuelLight::Seal, Aura, 3.5f, .10f);
    Animate(Engraving, Time + .22f, FVector(1.08, 1.08, 1), FVector::ZeroVector, 0.f, 10.f);
    Burst(At + FVector(0, 0, 54), Aura, bStorm ? 121 : 83, bStorm ? 792.f : 514.f, true);
    Flash(At + FVector(0, 0, 135), Aura, bStorm ? 28000.f : 20700.f, Time + .20f, 1540.f);
    Sound(EDuelSound::Pulse, At);
}

void ADuelArena::LaunchCrimson(ADuelFighter* From, ADuelFighter* Target, const FVector& Direction, float Damage, float Posture)
{
    const FVector At = From->GetActorLocation() + Direction * 63.f + FVector(0, 0, 42);
    LaunchThread(Target, At, Direction, Damage, Posture, Red, 1420.f, 106.f, 1.08f);
    Sound(EDuelSound::Cut, At);
}

USceneComponent* ADuelArena::StageIcicle(const FVector& At, const FVector& Axis, const FLinearColor& Tint, float Life)
{
    UMaterialInstanceDynamic* Material = SpellFacet(false);
    if (!Material)
    {
        return nullptr;
    }
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
    FSpellHalo Halo;
    AppendIceShard(FVector::ZeroVector, FVector::ForwardVector, 262.f, 32.f, Vertices, Triangles, UVs, Colors, &Halo);
    AppendIceShard(FVector(-63.f, 29.f, 26.f), FVector(1, -.18, -.22).GetSafeNormal(), 96.f, 14.f,
        Vertices, Triangles, UVs, Colors, &Halo);
    auto* Icicle = LightMesh(Material);
    Icicle->SetWorldLocationAndRotation(At, Axis.Rotation());
    Icicle->CreateMeshSection_LinearColor(0, Vertices, Triangles, {}, UVs, Colors, {}, false, false);
    Halo.Install(*Icicle, SpellHalo(false));
    Animate(Icicle, Life, FVector::OneVector, FVector::ZeroVector, 0.f, 0.f, true);
    return Icicle;
}

USceneComponent* ADuelArena::StageBossSignature(const FVector& At, const FVector& Axis,
    EDuelMove Move, const FLinearColor& Tint, float Life)
{
    const bool bShadow = Move == EDuelMove::HoundShade;
    const bool bBoneWing = Move == EDuelMove::OssuaryShears;
    // These are closed 3D meshes. The blade-wake atlas is a planar trail shader and makes their faces black.
    auto* Material = bBoneWing ? UMaterialInstanceDynamic::Create(DuelGeometry::ArtMaterial(TEXT("M_NorthSpellFacet")), this) : SpellFacet(bShadow);
    if (!Material) return nullptr;
    if (bBoneWing)
    {
        Material->SetVectorParameterValue(TEXT("Tint"), FLinearColor(.31f, .15f, .055f));
        Material->SetVectorParameterValue(TEXT("HotFacet"), FLinearColor(4.2f, 2.75f, 1.15f));
    }
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
    FSpellHalo Halo;
    const auto Shard = [&](const FVector& Origin, const FVector& Direction, float Length, float Width)
    {
        AppendIceShard(Origin, Direction.GetSafeNormal(), Length, Width, Vertices, Triangles, UVs, Colors, &Halo);
    };
    if (Move == EDuelMove::OssuaryShears)
    {
        Shard(FVector(0., 0., 22.), FVector(1., 0., -.21), 222.f, 23.f);
        Shard(FVector(-43., 0., 93.), FVector(.63, 0., -.78), 195.f, 18.f);
        for (int32 I = 0; I < 4; ++I)
        {
            const double Side = I % 2 ? 1. : -1.;
            Shard(FVector(-95. + I * 20., Side * (32. + 7. * I), 68. - I * 28.),
                FVector(.62, Side * .27, -.50), 116.f + 9.f * I, 13.f);
        }
    }
    else if (Move == EDuelMove::HoundShade)
    {
        Shard(FVector(-21., 0., 31.), FVector(1., 0., -.10), 174.f, 40.f);
        Shard(FVector(25., 0., -28.), FVector(1., 0., .28), 125.f, 26.f);
        for (double Side : {-1., 1.})
        {
            Shard(FVector(-50., Side * 42., 63.), FVector(-.69, Side * .20, .72), 91.f, 21.f);
            Shard(FVector(-91., Side * 55., 27.), FVector(-.83, Side * .35, .46), 165.f, 28.f);
            Shard(FVector(55., Side * 35., -4.), FVector(.21, 0., -.98), 58.f, 10.f);
        }
    }
    else
    {
        constexpr int32 Corners = 8;
        const FVector Rim[Corners] = {{0., 0., 132.}, {0., 65., 93.}, {0., 99., 30.}, {0., 85., -77.},
            {0., 0., -126.}, {0., -70., -98.}, {0., -100., -26.}, {0., -75., 64.}};
        for (int32 I = 0; I < Corners; ++I)
        {
            const FVector& A = Rim[I];
            const FVector& B = Rim[(I + 1) % Corners];
            const int32 V = Vertices.Num();
            Vertices.Append({FVector(15., 4., 7.), A, B, FVector(-15., -4., 7.), B, A});
            Triangles.Append({V, V + 1, V + 2, V + 3, V + 4, V + 5});
            UVs.Append({{.50, .48}, {.16, .26}, {.76, .77}, {.50, .48}, {.76, .77}, {.16, .26}});
            const FLinearColor Edge(.79f, .95f, 1.f, .71f);
            const FLinearColor Face(.26f + I * .041f, .61f, .97f, .49f);
            Colors.Append({Face, Edge, Edge, Face, Edge, Edge});
            Shard((A + B) * .5, B - A, static_cast<float>((B - A).Size()) + 6.f, 8.f);
        }
        Shard(FVector(23., 2., -3.), FVector(.02, .33, 1.), 139.f, 13.f);
        Shard(FVector(-23., 2., -3.), FVector(.02, -.33, 1.), 139.f, 13.f);
    }
    auto* Shape = LightMesh(Material);
    Shape->ComponentTags.Add(bBoneWing ? TEXT("EmaFlyingBoneWing346") :
        bShadow ? TEXT("HiroFlyingHound346") : TEXT("YukiFlyingMirror346"));
    Shape->SetWorldLocationAndRotation(At, Axis.Rotation());
    Shape->CreateMeshSection_LinearColor(0, Vertices, Triangles, {}, UVs, Colors, {}, false, false);
    Halo.Install(*Shape, bBoneWing ? LightMaterial(EDuelLight::Bloom, FLinearColor(1.f, .62f, .19f), .78f, .12f) : SpellHalo(bShadow));
    Animate(Shape, Life, FVector::OneVector, FVector::ZeroVector, 0.f, 0.f, true);
    return Shape;
}

USceneComponent* ADuelArena::TraceBossSignature(const FVector& From, const FVector& To, const FLinearColor& Tint, float Life)
{
    const FVector Path = To - From;
    if (Path.SizeSquared() < FMath::Square(30.) || Life <= .015f) return nullptr;
    FRotator Rotation = Path.Rotation();
    Rotation.Roll = 43.f;
    auto* Trace = LightPlane((From + To) * .5, FVector2D(Path.Size(), 25.), Rotation,
        EDuelLight::Shaft, Tint * .54f, 1.8f, .16f, 3.f);
    Animate(Trace, Life, FVector::OneVector, FVector::ZeroVector, 0.f, 0.f, true);
    return Trace;
}

USceneComponent* ADuelArena::LaunchThread(ADuelFighter* Target, const FVector& At, const FVector& Axis, float Damage,
    float Posture, const FLinearColor& Tint, float Speed, float Radius, float Life, bool bParryable,
    float TurnDegreesPerSecond, bool bRecoil, float HomingDegreesPerSecond, EDuelMark Mark)
{
    FVector FlightAxis = Axis;
    const auto* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    FVector SealAim;
    if (Target && Target->bBoss && Mode && Mode->GetPlayerFighter() && Mode->GetPlayerFighter()->IsLocked() && CourtSealAimPoint(SealAim))
    {
        FlightAxis = (SealAim - At).GetSafeNormal(UE_SMALL_NUMBER, Axis);
        TurnDegreesPerSecond = HomingDegreesPerSecond = 0.f;
    }
    FCrimsonOrb& Shot = Orbs.AddDefaulted_GetRef();
    Shot.Visual = DuelGeometry::Joint(this, SceneRoot, At);
    Shot.Target = Target;
    Shot.Caster = Mode && Target == Mode->GetPlayerFighter() ? const_cast<ADuelFighter*>(Mode->GetBossFighter()) :
        Mode && Target == Mode->GetBossFighter() ? const_cast<ADuelFighter*>(Mode->GetPlayerFighter()) : nullptr;
    Shot.Velocity = FlightAxis * Speed;
    Shot.Damage = Damage;
    Shot.Posture = Posture;
    Shot.Radius = Radius;
    Shot.Life = Life;
    Shot.TurnDegreesPerSecond = TurnDegreesPerSecond;
    Shot.HomingDegreesPerSecond = HomingDegreesPerSecond;
    Shot.Tint = Tint;
    Shot.Mark = Mark;
    if (Mark == EDuelMark::IceLance || Mark == EDuelMark::Prism || Mark == EDuelMark::ShadowVolley)
    {
        const bool bShadow = Mark == EDuelMark::ShadowVolley;
        TArray<FVector> Vertices;
        TArray<int32> Faces;
        TArray<FVector2D> UVs;
        TArray<FLinearColor> Facets;
        FSpellHalo Halo;
        const float Width = FMath::Clamp(Radius * .53f, 12.f, 39.f);
        AppendIceShard(FVector::ZeroVector, FVector::ForwardVector, Width * 4.6f, Width, Vertices, Faces, UVs, Facets, &Halo);
        if (bShadow)
        {
            for (float Side : {-1.f,1.f})
                AppendIceShard(FVector(-Width,Side*Width*.63f,Width*.31f),FVector(-.78f,Side*.48f,.39f).GetSafeNormal(),
                    Width * 2.5f, Width * .29f, Vertices, Faces, UVs, Facets, &Halo);
        }
        auto* Body = LightMesh(SpellFacet(bShadow));
        Body->AttachToComponent(Shot.Visual.Get(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
        Body->CreateMeshSection_LinearColor(0, Vertices, Faces, {}, UVs, Facets, {}, false, false);
        Halo.Install(*Body, SpellHalo(bShadow));
        Shot.Visual->SetWorldRotation(FlightAxis.Rotation());
    }
    Shot.bParryable = bParryable;
    Shot.bRecoil = bRecoil;
    Emit(EDuelLight::Star, At, FlightAxis * 130.f, FVector2D(66), Tint, .16f);
    Flash(At, Tint, 3600.f, .22f, 360.f);
    return Shot.Visual.Get();
}

void ADuelArena::TrackBlade(const FVector& Hilt, const FVector& Tip, const FLinearColor& Color,
    bool bHeavy, bool bNewSwing, bool bOffering)
{
    bNewSwing |= !BladeSamples.IsEmpty() && BladeSamples.Last().bOffering != bOffering;
    if (!bNewSwing && !BladeSamples.IsEmpty() && FVector::DistSquared(BladeSamples.Last().Tip, Tip) < 36.0)
    {
        return;
    }
    if (BladeSamples.Num() >= 74)
    {
        BladeSamples.RemoveAt(0, 1, EAllowShrinking::No);
    }
    const float Peak = FMath::Max(1.f, FMath::Max3(Color.R, Color.G, Color.B));
    BladeSamples.Add({Hilt, Tip, FLinearColor(Color.R / Peak, Color.G / Peak, Color.B / Peak, 1.f),
        0.f, bHeavy, bNewSwing, bOffering});
    if (bNewSwing)
    {
        Sound(EDuelSound::Cut, Hilt);
    }
    if (!bNewSwing && BladeSamples.Num() > 1 && BladeSamples.Num() % 3 == 1)
    {
        const FVector Motion = Tip - BladeSamples[BladeSamples.Num() - 2].Tip;
        const FVector Velocity = Motion.GetClampedToMaxSize(38.f) * 6.f;
        const FVector Axis = Motion.GetSafeNormal();
        Emit(EDuelLight::Filament, FMath::Lerp(Hilt, Tip, .93), Velocity,
            FVector2D(bHeavy ? 55.f : 28.f, bHeavy ? 7.f : 4.f), Color, .16f, 98.f, Axis, 0.f, bOffering);
        if (bHeavy && BladeSamples.Num() % 6 == 1)
            Emit(EDuelLight::Mist, FMath::Lerp(Hilt, Tip, .69), Velocity * .26 + FVector(0, 0, 30),
                FVector2D(92, 34), Color.GetClamped(0.f, 1.f).CopyWithNewOpacity(.23f),
                .34f, 14.f, Axis, 37.f, bOffering);
    }
}

void ADuelArena::BladeStep(const FVector& At, const FVector& Axis, bool bHeavy)
{
    const FVector Across = FVector::CrossProduct(FVector::UpVector, Axis);
    const FLinearColor Dust(.46f, .39f, .34f, .24f);
    for (float Side : {-1.f, 0.f, 1.f})
    {
        const FVector Drift = -Axis * 65.f + Across * Side * 31.f + FVector(0, 0, 27);
        Emit(EDuelLight::Mist, At + Across * Side * 18.f + FVector(0, 0, 8), Drift,
            bHeavy ? FVector2D(70, 27) : FVector2D(43, 18), Dust, bHeavy ? .29f : .22f, 30.f, Drift, Side * 36.f);
    }
    if (bHeavy) Sound(EDuelSound::Footstep, At);
}

void ADuelArena::BladeImpact(const FVector& At, const FVector& Axis, const FLinearColor& Tint, bool bHeavy)
{
    const int32 Streaks = bHeavy ? 12 : 6;
    const FLinearColor Core = FMath::Lerp(Tint, FLinearColor(1.f, .83f, .66f), .25f);
    const FVector DirectionAxis = Axis.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
    FVector Across = FVector::CrossProduct(FVector::UpVector, DirectionAxis).GetSafeNormal();
    if (Across.IsNearlyZero()) Across = FVector::RightVector;
    const FVector Lift = FVector::CrossProduct(DirectionAxis, Across).GetSafeNormal();
    for (int32 Cut = 0; Cut < (bHeavy ? 2 : 1); ++Cut)
    {
        const float Tilt = Cut == 0 ? -31.f : 43.f;
        ArcStroke(At, Across.RotateAngleAxis(Tilt, DirectionAxis), Lift.RotateAngleAxis(Tilt, DirectionAxis),
            FVector2D(bHeavy ? 103.f : 57.f, bHeavy ? 46.f : 26.f), 17.f, 163.f, 5,
            [&](const FVector& Point, const FVector& Tangent, float Length, float Taper)
            {
                Emit(EDuelLight::Filament, Point, DirectionAxis * 91.f + Tangent * 48.f,
                    FVector2D(Length, (bHeavy ? 12.f : 7.f) * Taper), Core,
                    bHeavy ? .23f : .15f, 130.f, Tangent);
            });
    }
    for (int32 I = 0; I < Streaks; ++I)
    {
        const FVector Direction = (DirectionAxis.RotateAngleAxis(Random.FRandRange(-48.f, 48.f), FVector::UpVector) +
            FVector(0, 0, Random.FRandRange(-.20f, .69f))).GetSafeNormal();
        const float Speed = Random.FRandRange(385.f, bHeavy ? 1160.f : 652.f);
        const float Size = Random.FRandRange(28.f, bHeavy ? 128.f : 68.f);
        Emit(EDuelLight::Spark, At + Direction * 10.f, Direction * Speed, FVector2D(Size, 2.6f + Size * .052f),
            I % 4 ? Tint : Core, Random.FRandRange(.16f, bHeavy ? .51f : .35f), 412.f, Direction);
    }
    Emit(EDuelLight::Bloom, At, FVector::ZeroVector, FVector2D(bHeavy ? 102.f : 68.f), Core, bHeavy ? .14f : .095f);
    if (bHeavy)
    {
        for (float Side : {-1.f, 1.f})
            Emit(EDuelLight::Mist, At + Across * Side * 35.f, DirectionAxis * 47.f + Across * Side * 82.f,
                FVector2D(127, 55), Tint.GetClamped(0.f, 1.f).CopyWithNewOpacity(.30f), .43f, 17.f, DirectionAxis, Side * 38.f);
        Flash(At, Tint, 10500.f, .16f, 385.f);
    }
}

void ADuelArena::SpellContact(const FVector& At, const FVector& Axis, const FLinearColor& Tint, bool bHeavy)
{
    const FVector Forward = Axis.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
    const FVector Across = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
    const FLinearColor Core = FMath::Lerp(Tint, FLinearColor(1.f, .96f, .90f), .63f);
    const int32 Count = bHeavy ? 11 : 7;
    for (int32 I = 0; I < Count; ++I)
    {
        const double Spread = (I - (Count - 1) * .5) / (Count - 1);
        const FVector Throw = Forward * (bHeavy ? 570. : 310.) + Across * Spread * 320. + FVector(0, 0, 30 + 37 * (I % 4));
        Emit(EDuelLight::Spark, At - Forward * (14.f + (I % 3) * 12.f), Throw,
            FVector2D((bHeavy ? 99.f : 62.f) - (I % 3) * 10.f, 3.f + I % 3),
            I % 3 == 0 ? Core : Tint, bHeavy ? .29f : .21f, 210.f, Throw);
    }
    Emit(EDuelLight::Bloom, At, Forward * 51.f, FVector2D(bHeavy ? 136.f : 84.f), Core, bHeavy ? .16f : .12f);
    Flash(At, Tint, bHeavy ? 12100.f : 7200.f, .16f, bHeavy ? 520.f : 325.f);
}

void ADuelArena::ShieldContact(const FVector& At, const FVector& Toward, bool bParried)
{
    const FVector Across = FVector::CrossProduct(FVector::UpVector, Toward.GetSafeNormal2D(UE_SMALL_NUMBER, FVector::ForwardVector));
    const FLinearColor Tint = bParried ? FLinearColor(.52f, .84f, 1.0f) : FLinearColor(1.f, .62f, .29f);
    for (int32 I = 0; I < (bParried ? 12 : 8); ++I)
    {
        const FVector Direction = Across * (I % 2 ? 1.f : -1.f) * (245.f + 42.f * (I % 5)) +
            Toward * 68.f + FVector(0, 0, 68.f + 69.f * (I % 4));
        Emit(EDuelLight::Spark, At + Across * ((I % 3) - 1) * 14.f, Direction,
            FVector2D(bParried ? 62.f : 37.f, 4.f), Tint, bParried ? .28f : .21f, 430.f, Direction);
    }
    Emit(EDuelLight::Bloom, At, FVector::ZeroVector, FVector2D(bParried ? 81.f : 59.f), Tint, .11f);
    Sound(bParried ? EDuelSound::ParryChime : EDuelSound::Block, At);
}

void ADuelArena::CastAccent(EDuelMark Mark, const FVector& At, const FVector& Axis, const FLinearColor& Tint, bool bReady)
{
    const FVector Across = FVector::CrossProduct(FVector::UpVector, Axis);
    const FLinearColor Core = FMath::Lerp(Tint.GetClamped(0.f, 1.f), FLinearColor::White, .55f);
    const bool bIce = Mark == EDuelMark::IceLance;
    Emit(EDuelLight::Star, At, Axis * 18.f, FVector2D(bReady ? 72.f : 36.f), Core, bReady ? .21f : .16f);
    for (int32 I = 0; I < (bReady ? 8 : 4); ++I)
    {
        const FVector Out = Axis * (bIce ? 1.f : .32f) + Across * (.12f + .24f * (I / 2)) * (I % 2 ? 1.f : -1.f) +
            FVector(0, 0, .16f * ((I % 3) - 1));
        const FVector Flow = Out * (bReady ? 224.f : -92.f);
        Emit(bIce ? EDuelLight::Filament : EDuelLight::Star, At + Out * (bReady ? 15.f : 44.f), Flow,
            FVector2D(bIce ? 53.f : 23.f, bIce ? 5.f : 15.f), bReady ? Core : Tint, bReady ? .23f : .16f, 0.f, Out);
    }
    if (bReady) Sound(EDuelSound::SpellReady, At);
}

void ADuelArena::TickParticles(float DeltaSeconds)
{
    CSV_SCOPED_TIMING_STAT(DuelCombat, Particles);
    const float MistDrag = FMath::Exp(-.58f * DeltaSeconds);
    const float SparkDrag = FMath::Exp(-1.40f * DeltaSeconds);
    for (FParticle& Particle : Particles)
    {
        Particle.Age += DeltaSeconds;
        if (Particle.Age <= 0.f || Particle.Age >= Particle.Life) continue;
        const bool bMist = Particle.Shape == EDuelLight::Mist;
        const float ActiveStep = FMath::Min(DeltaSeconds, Particle.Age);
        if (bMist && !FMath::IsNearlyZero(Particle.Spin))
        {
            // A bounded curl in the existing mist batch; no per-particle component or field simulation.
            Particle.Velocity = Particle.Velocity.RotateAngleAxis(Particle.Spin * .65f * ActiveStep, FVector::UpVector);
        }
        Particle.At += Particle.Velocity * ActiveStep;
        Particle.Velocity *= ActiveStep < DeltaSeconds
            ? FMath::Exp(-(bMist ? .58f : 1.40f) * ActiveStep) : (bMist ? MistDrag : SparkDrag);
        Particle.Velocity.Z -= Particle.Gravity * ActiveStep;
    }
    Particles.RemoveAllSwap([](const FParticle& Particle) { return Particle.Age >= Particle.Life; }, EAllowShrinking::No);
    CSV_CUSTOM_STAT(DuelCombat, ActiveParticles, Particles.Num(), ECsvCustomStatOp::Set);
    const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
    if (!Camera)
    {
        return;
    }
    ParticleBuffer.Reset();
    MistBuffer.Reset();
    ParticleBuffer.Positions.Reserve(Particles.Num() * 4);
    ParticleBuffer.UVs.Reserve(Particles.Num() * 4);
    ParticleBuffer.Colors.Reserve(Particles.Num() * 4);
    ParticleBuffer.Indices.Reserve(Particles.Num() * 6);
    const FVector Eye = Camera->GetCameraLocation();
    const FRotationMatrix View(Camera->GetCameraRotation());
    const FVector ViewRight = View.GetUnitAxis(EAxis::Y);
    const FVector ViewForward = View.GetUnitAxis(EAxis::X);
    for (const FParticle& Particle : Particles)
    {
        if (Particle.Age <= 0.f) continue;
        const FVector ToCamera = Eye - Particle.At;
        if (FVector::DotProduct(ViewForward, ToCamera) > FMath::Max(Particle.Size.X, Particle.Size.Y) * .68)
        {
            continue;
        }
        const FVector Facing = ToCamera.GetSafeNormal();
        const bool bMist = Particle.Shape == EDuelLight::Mist;
        FEffectMeshBuffer& Buffer = bMist ? MistBuffer : ParticleBuffer;
        const FVector Aim = Particle.Axis.IsNearlyZero() ? Particle.Velocity : Particle.Axis;
        FVector Long = FVector::VectorPlaneProject(Aim, Facing).GetSafeNormal();
        if (Long.IsNearlyZero())
        {
            Long = ViewRight;
        }
        if (Particle.Spin != 0.f)
            Long = Long.RotateAngleAxis(Particle.Spin * Particle.Age, Facing);
        const float Age = Particle.Age / Particle.Life;
        const float Spread = Particle.Shape == EDuelLight::Bloom ? 1.f + .85f * Age :
            1.f + (bMist ? 1.02f : .17f) * Age;
        const FVector Right = Long * (Particle.Size.X * Spread * .5);
        const FVector Up = FVector::CrossProduct(Facing, Long) * (Particle.Size.Y * Spread * .5);
        const int32 Start = Buffer.Positions.Num();
        Buffer.Positions.Append({Particle.At - Right + Up, Particle.At + Right + Up,
            Particle.At + Right - Up, Particle.At - Right - Up});
        Buffer.Indices.Append({Start, Start + 1, Start + 2, Start, Start + 2, Start + 3});
        const int32 Tile = static_cast<int32>(Particle.Shape);
        const FVector2D Corner((Tile % 4) * .25 + .0007, (Tile / 4) * .25 + .0007);
        constexpr double Scale = .2486;
        Buffer.UVs.Append({Corner, Corner + FVector2D(Scale, 0), Corner + FVector2D(Scale, Scale), Corner + FVector2D(0, Scale)});
        FLinearColor Tint = Particle.Tint;
        const float FadeIn = Particle.Shape == EDuelLight::Spark || Particle.Shape == EDuelLight::Bloom ? .025f : .085f;
        Tint.A *= FMath::SmoothStep(0.f, FadeIn, Age) * (1.f - FMath::SmoothStep(.18f, 1.f, Age));
        if (bMist || Particle.Shape == EDuelLight::Bloom || Particle.Shape == EDuelLight::Shaft)
        {
            // Preserve floor warnings when the camera sits inside a burst or backs into a wall.
            Tint.A *= FMath::SmoothStep(105.f, bMist ? 430.f : 285.f, static_cast<float>(ToCamera.Size()));
        }
        Buffer.Colors.Append({Tint, Tint, Tint, Tint});
    }
    const auto UploadParticles = [this](FEffectMeshBuffer& Buffer, TObjectPtr<UProceduralMeshComponent>& Mesh,
        const TCHAR* Material, int32 Priority)
    {
        if (Buffer.Positions.IsEmpty())
        {
            if (Mesh && Mesh->IsVisible())
            {
                Mesh->SetVisibility(false);
            }
            return;
        }
        if (!Mesh)
        {
            Mesh = LightMesh(DuelGeometry::ArtMaterial(Material));
            Mesh->TranslucencySortPriority = Priority;
            Mesh->MarkRenderStateDirty();
        }
        if (!Mesh->IsVisible()) Mesh->SetVisibility(true);
        Buffer.Upload(*Mesh);
    };
    UploadParticles(MistBuffer, MistMesh, TEXT("M_CombatMist"), 0);
    UploadParticles(ParticleBuffer, ParticleMesh, TEXT("M_SacredFx"), 2);
}

void ADuelArena::TickBlade(float DeltaSeconds)
{
    CSV_SCOPED_TIMING_STAT(DuelCombat, BladeTrails);
    const auto Lifetime = [](const FBladeSample& Sample) { return Sample.bOffering ? .23f : Sample.bHeavy ? .43f : .29f; };
    for (FBladeSample& Sample : BladeSamples)
    {
        Sample.Age += DeltaSeconds;
    }
    for (int32 I = BladeSamples.Num() - 1; I >= 0; --I)
    {
        if (BladeSamples[I].Age >= Lifetime(BladeSamples[I]))
        {
            if (I + 1 < BladeSamples.Num())
            {
                BladeSamples[I + 1].bStartsSwing = true;
            }
            BladeSamples.RemoveAt(I, 1, EAllowShrinking::No);
        }
    }
    if (BladeSamples.Num() < 2)
    {
        if (BladeMesh && BladeMesh->IsVisible())
        {
            BladeMesh->SetVisibility(false);
        }
        return;
    }
    if (!BladeMesh)
    {
        BladeMesh = LightMesh(DuelGeometry::ArtMaterial(TEXT("M_BladeWake")));
        BladeMesh->TranslucencySortPriority = 1;
        BladeMesh->MarkRenderStateDirty();
    }
    BladeBuffer.Reset();
    for (int32 Begin = 0; Begin < BladeSamples.Num();)
    {
        int32 End = Begin + 1;
        while (End < BladeSamples.Num() && !BladeSamples[End].bStartsSwing)
        {
            ++End;
        }
        for (int32 I = Begin; End - Begin > 1 && I < End; ++I)
        {
            const FBladeSample& Point = BladeSamples[I];
            const float Along = (I - Begin) / static_cast<float>(End - Begin - 1);
            const float Fade = FMath::Clamp(1.f - Point.Age / Lifetime(Point), 0.f, 1.f);
            const FVector Span = Point.Tip - Point.Hilt;
            const FVector Mid = Point.Hilt + Span * (Point.bHeavy ? .08f : .30f);
            const FVector Edge = Point.Tip + Span.GetSafeNormal() * (Point.bHeavy ? 40.f : 12.f);
            FLinearColor Tint = Point.Tint;
            Tint.A = FMath::Pow(Fade, 1.43f) * (Point.bHeavy ? .92f : .74f);
            if (Point.bOffering)
            {
                Tint.A *= .77f;
            }
            const int32 Start = BladeBuffer.Positions.Num();
            BladeBuffer.Positions.Append({Mid, Edge});
            BladeBuffer.UVs.Append({FVector2D(Along, 0.f), FVector2D(Along, 1.f)});
            BladeBuffer.Colors.Append({Tint, Tint});
            if (I > Begin)
            {
                BladeBuffer.Indices.Append({Start - 2, Start, Start - 1, Start - 1, Start, Start + 1});
            }
        }
        Begin = End;
    }
    BladeMesh->SetVisibility(!BladeBuffer.Positions.IsEmpty());
    if (!BladeBuffer.Positions.IsEmpty())
    {
        BladeBuffer.Upload(*BladeMesh);
    }
}

void ADuelArena::TickEffects(float DeltaSeconds)
{
    CSV_SCOPED_TIMING_STAT(DuelCombat, EffectTransforms);
    static const FName ColorName(TEXT("Color"));
    static const FName OpacityName(TEXT("Opacity"));
    for (int32 Index = Effects.Num() - 1; Index >= 0; --Index)
    {
        FEffect& Effect = Effects[Index];
        USceneComponent* Mesh = Effect.Component.Get();
        Effect.Age += DeltaSeconds;
        if (!Mesh || Effect.Age >= Effect.Life)
        {
            if (Mesh)
            {
                Mesh->DestroyComponent();
            }
            Effects.RemoveAtSwap(Index);
            continue;
        }

        const float Age = Effect.Age / Effect.Life;
        if (UPointLightComponent* Light = Cast<UPointLightComponent>(Mesh))
        {
            Light->SetIntensity(Effect.LightIntensity * FMath::Square(1.f - Age));
            continue;
        }
        if (UMaterialInstanceDynamic* Material = Effect.Material.Get())
        {
            const float Envelope = Effect.bWarning ? .70f + .30f * FMath::Sin(Age * 2.2f)
                : 1.f - FMath::SmoothStep(.10f, 1.f, Age);
            const float Breath = Effect.Breath > 0.f ? .88f + .12f * FMath::Sin(Effect.Age * Effect.Breath) : 1.f;
            const float Fade = Envelope * Breath * Effect.OpacityBase;
            if (Effect.MaterialColor.A > 0.f)
                Material->SetVectorParameterValue(ColorName, Effect.MaterialColor.CopyWithNewOpacity(Fade * Effect.MaterialColor.A));
            else
                Material->SetScalarParameterValue(OpacityName, Fade);
        }
        if (Effect.ScaleMultiplier != FVector::OneVector)
            Mesh->SetRelativeScale3D(Effect.StartScale * FMath::Lerp(FVector::OneVector, Effect.ScaleMultiplier, Age));
        if (!Effect.Velocity.IsZero())
            Mesh->AddRelativeLocation(Effect.Velocity * DeltaSeconds);
        if (Effect.Spin != 0.f)
            Mesh->AddLocalRotation(FRotator(0, Effect.Spin * DeltaSeconds, 0));
        if (Effect.Gravity != 0.f)
            Effect.Velocity.Z -= Effect.Gravity * DeltaSeconds;
    }
}

void ADuelArena::TickOrbs(float DeltaSeconds)
{
    for (int32 Index = Orbs.Num() - 1; Index >= 0; --Index)
    {
        FCrimsonOrb& Shot = Orbs[Index];
        USceneComponent* Orb = Shot.Visual.Get();
        ADuelFighter* Target = Shot.Target.Get();
        if (!Orb || Shot.Age >= Shot.Life)
        {
            DestroyGroup(Orb);
            Orbs.RemoveAtSwap(Index);
            continue;
        }

        const FVector From = Orb->GetComponentLocation();
        const float Step = FMath::Min(DeltaSeconds, Shot.Life - Shot.Age);
        Shot.Age += Step;
        float Turn = Shot.TurnDegreesPerSecond * Step;
        if (Shot.HomingDegreesPerSecond > 0.f && IsValid(Target) && Target->IsAlive() && !Target->bCombatOver)
        {
            const float Delta = FMath::FindDeltaAngleDegrees(static_cast<float>(Shot.Velocity.Rotation().Yaw),
                static_cast<float>((Target->GetActorLocation() - From).Rotation().Yaw));
            const float Limit = Shot.HomingDegreesPerSecond * Step;
            Turn = FMath::Clamp(Delta, -Limit, Limit);
        }
        FVector NextVelocity = Shot.Velocity.RotateAngleAxis(Turn, FVector::UpVector);
        FVector To = From + (Shot.Velocity + NextVelocity) * (.5f * Step);
        Shot.Velocity = NextVelocity;
        FVector Bounce = From;
        const bool bMirrorBounce = MirrorPhase > 0 && MirrorPressureAge >= DuelCombatMechanics::MirrorFormTime &&
            DuelCombatMechanics::ReflectMirrorSegment(MirrorCenter, MirrorHalfWidth, MirrorHalfLength,
                From, To, Shot.Velocity, Bounce);
        if (bMirrorBounce)
        {
            NextVelocity = Shot.Velocity;
            Shot.HomingDegreesPerSecond = 0.f;
            Shot.TurnDegreesPerSecond *= -1.f;
            if (Shot.MirrorBounces == 0) Shot.Life = FMath::Max(Shot.Life, Shot.Age + .85f);
            ++Shot.MirrorBounces;
            SpellContact(Bounce, Shot.Velocity.GetSafeNormal(), Shot.Tint, false);
            Sound(EDuelSound::ParryChime, Bounce);
        }
        const bool bLiveTarget = Target && Target->IsAlive() && !Target->bCombatOver;
        const auto* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
        auto* Player = Mode ? const_cast<ADuelFighter*>(Mode->GetPlayerFighter()) : nullptr;
        if(Shot.Caster.IsValid())HitDungeonChains(*Shot.Caster.Get(),From,To,Shot.Radius);
        if (Target && Target->bBoss && Player && HitCourtSeal(*Player, From, To, Shot.Radius, true))
        {
            DestroyGroup(Orb);
            Orbs.RemoveAtSwap(Index);
            continue;
        }
        FVector HitFrom = bMirrorBounce ? Bounce : From;
        FVector Near = bLiveTarget ? FMath::ClosestPointOnSegment(Target->GetActorLocation(), HitFrom, To) : To;
        if (bLiveTarget && bMirrorBounce)
        {
            const FVector Incoming = FMath::ClosestPointOnSegment(Target->GetActorLocation(), From, Bounce);
            if (FVector::DistSquared(Target->GetActorLocation(), Incoming) < FVector::DistSquared(Target->GetActorLocation(), Near))
            {
                Near = Incoming;
                HitFrom = From;
            }
        }
        if (bLiveTarget && FVector::DistSquared2D(Target->GetActorLocation(), Near) <= FMath::Square(Shot.Radius) &&
            (!Shot.bParryable || Target->GetActorLocation().Z - Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() <= Near.Z + 12.f))
        {
            const FLinearColor Tint = Shot.Tint;
            const float Damage = Shot.Damage;
            const float Posture = Shot.Posture;
            const bool bParryable = Shot.bParryable;
            const bool bRecoil = Shot.bRecoil;
            const FVector Source = HitFrom - (Near - HitFrom).GetSafeNormal(UE_SMALL_NUMBER, Shot.Velocity.GetSafeNormal()) * Shot.Radius;
            DestroyGroup(Orb);
            Orbs.RemoveAtSwap(Index);
            if (Target->ReceiveDuelDamage(Damage, Source, bParryable, &Near, true, bRecoil))
            {
                Target->DamagePosture(Posture);
                SpellContact(Near, (Near - Source).GetSafeNormal(), Tint, Damage >= 115.f);
            }
            if (Orbs.IsEmpty())
            {
                break;
            }
            continue;
        }

        if (Shot.Mark == EDuelMark::IceLance || Shot.Mark == EDuelMark::Prism || Shot.Mark == EDuelMark::ShadowVolley)
            Orb->SetWorldLocationAndRotation(To, NextVelocity.Rotation());
        else
            Orb->SetWorldLocation(To);
        Shot.EmissionDelay -= DeltaSeconds;
        if (Shot.EmissionDelay <= 0.f)
        {
            Shot.EmissionDelay = .023f;
            if (Shot.Mark != EDuelMark::Default)
            {
                const FVector Axis = Shot.Velocity.GetSafeNormal();
                const FVector Across = FVector::CrossProduct(FVector::UpVector, Axis);
                const bool bLance = Shot.Mark == EDuelMark::Prism || Shot.Mark == EDuelMark::IceLance;
                const bool bLantern = Shot.Mark == EDuelMark::Lantern;
                const bool bWitness = Shot.Mark == EDuelMark::Witness;
                const bool bReturnCut = Shot.Mark == EDuelMark::Reprise;
                const bool bTightWeave = Shot.Mark == EDuelMark::CrossThread;
                if (bLantern)
                {
                    const float Phase = Shot.Age * 16.f;
                    const FVector Halo = Across * FMath::Cos(Phase) + FVector::UpVector * FMath::Sin(Phase);
                    Emit(EDuelLight::Mote, To, Shot.Velocity * .10f, FVector2D(41, 36), Shot.Tint, .18f);
                    Emit(EDuelLight::Spark, To + Halo * 54.f, -Halo * 65.f + FVector(0, 0, 152.f),
                        FVector2D(28, 8), Shot.Tint * .72f, .46f, 105.f);
                    Emit(EDuelLight::Bloom, To, FVector(0, 0, 57.f), FVector2D(92, 88), Shot.Tint * .39f, .29f);
                }
                else if (bLance)
                {
                    const double Width = Shot.Mark == EDuelMark::Prism ? 52. : 35.;
                    const FVector Tip = To + Axis * 26.f;
                    Emit(EDuelLight::Shaft, Tip, Shot.Velocity * .07f, FVector2D(182, Width), Shot.Tint,
                        .136f, 0, Axis);
                    for (float Side : {-1.f, 1.f})
                    {
                        const FVector Drift = Across * (Side * 135.f) + FVector(0, 0, 81);
                        Emit(EDuelLight::Feather, To + Across * (Side * Width * .45), Drift,
                            FVector2D(92, 15), Shot.Tint * .67f, .29f, 200.f, (Axis + Across * Side * .51f), Side * 77.f);
                    }
                }
                else
                {
                    const float Side = FMath::Sin(Shot.Age * (bTightWeave ? 38.f : bWitness ? 10.5f : 24.f));
                    const FVector Knot = To + Across * Side * (bTightWeave ? 12.f : bReturnCut ? 49.f : 31.f);
                    const FVector Drift = Shot.Velocity * -.038f + FVector(0, 0, 89.f);
                    Emit(bWitness ? EDuelLight::Shaft : EDuelLight::Filament, To, Shot.Velocity * .07f,
                        FVector2D(bWitness ? 175.f : 154.f, bTightWeave ? 14.f : 31.f), Shot.Tint, .16f, 0, Axis);
                    Emit(bReturnCut ? EDuelLight::Feather : EDuelLight::Filament, Knot - Axis * 31.f, Shot.Velocity * .03f,
                        FVector2D(bReturnCut ? 148.f : bWitness ? 112.f : 74.f, bReturnCut ? 28.f : 16.f),
                        Shot.Tint * .67f, .21f, 0, Axis + Across * Side * (bReturnCut ? .69f : .23f));
                    Emit(bTightWeave ? EDuelLight::Star : EDuelLight::Spark, Knot, Drift,
                        bTightWeave ? FVector2D(23.f) : FVector2D(48, 8), Shot.Tint * .79f,
                        .32f, 210.f, Axis + Across * Side * .27f);
                }
                continue;
            }
            Emit(EDuelLight::Star, To, Shot.Velocity * .58f, FVector2D(52, 39), Shot.Tint, .12f);
            Emit(EDuelLight::Filament, To - Shot.Velocity.GetSafeNormal() * 37.f, Shot.Velocity * .28f,
                FVector2D(143, 35), Shot.Tint * .67f, .28f, 0, Shot.Velocity);
            Emit(EDuelLight::Spark, To, Shot.Velocity * -.10f + FVector(0, Random.FRandRange(-136, 136), 63),
                FVector2D(38, 8), Shot.Tint, .39f, 110.f);
        }
    }
}

void ADuelArena::ClearCombatEffects()
{
    ClearCourtInteractions();
    ClearMirrorCourt();
    StopVoice();
    StopScore();
    for (FEffect& Effect : Effects)
    {
        DestroyGroup(Effect.Component.Get());
    }
    for (FCrimsonOrb& Shot : Orbs)
    {
        DestroyGroup(Shot.Visual.Get());
    }
    Effects.Reset();
    Orbs.Reset();
    Particles.Reset();
    BladeSamples.Reset();
    if (ParticleMesh)
    {
        ParticleMesh->ClearAllMeshSections();
    }
    if (MistMesh)
    {
        MistMesh->ClearAllMeshSections();
    }
    if (BladeMesh)
    {
        BladeMesh->ClearAllMeshSections();
    }
}

void ADuelArena::LoadFootfalls()
{
    for (int32 Take = 1; Take <= 9; ++Take)
    {
        const FString File = FPaths::ProjectContentDir() / TEXT("DuelFootsteps") / FString::Printf(TEXT("S_StoneBoot_%02d.wav"), Take);
        TArray<uint8> Bytes;
        FWaveModInfo Info;
        if (FFileHelper::LoadFileToArray(Bytes,*File) && Info.ReadWaveInfo(Bytes.GetData(),Bytes.Num()) &&
            *Info.pFormatTag==FWaveModInfo::WAVE_INFO_FORMAT_PCM && *Info.pChannels==1 &&
            *Info.pBitsPerSample==16 && *Info.pSamplesPerSec==24000 && Info.SampleDataSize>0)
            StepTakes.AddDefaulted_GetRef().Append(Info.SampleDataStart,Info.SampleDataSize);
        else UE_LOG(LogTemp,Error,TEXT("Could not read recorded step %s"),*File);
    }
    StepAttenuation = NewObject<USoundAttenuation>(this);
    StepAttenuation->Attenuation.AttenuationShapeExtents = FVector(260.f);
    StepAttenuation->Attenuation.FalloffDistance = 1250.f;
}

void ADuelArena::PlayFootfall(const FVector& At, float Effort, bool bLocalPlayer)
{
    if (!GetWorld()->AllowAudioPlayback() || StepTakes.IsEmpty()) return;
    int32 Pick = 0;
    if (StepTakes.Num() > 1)
    {
        Pick = StepVariation.RandRange(0, StepTakes.Num() - (LastStepTake == INDEX_NONE ? 1 : 2));
        if (LastStepTake != INDEX_NONE && Pick >= LastStepTake) ++Pick;
    }
    LastStepTake = Pick;
    const float Load = FMath::Clamp(Effort, 0.f, 1.f);
    const float Gain = (.25f + .24f * Load) * StepVariation.FRandRange(.90f, 1.f)
        * FDuelGraphicsSettings::AudioGain(EDuelGraphicsRow::CombatAudio);
    if (Gain <= 0.f) return;
    const float Pitch=StepVariation.FRandRange(.92f, 1.025f) - Load * .035f;
    const auto& PCM=StepTakes[Pick];
    auto* Wave=NewObject<USoundWaveProcedural>(this);
    Wave->SetSampleRate(24000);
    Wave->NumChannels=1;
    Wave->Duration=PCM.Num()/48000.f;
    Wave->bLooping=false;
    Wave->QueueAudio(PCM.GetData(),PCM.Num());
    // A long lock-on camera boom must not attenuate the player's own footfalls.
    auto* Audio=bLocalPlayer ? UGameplayStatics::SpawnSound2D(this,Wave,Gain*.8f,Pitch) :
        UGameplayStatics::SpawnSoundAtLocation(this,Wave,At,FRotator::ZeroRotator,Gain,Pitch,0.f,StepAttenuation);
    if(Audio)
    {
        FTimerHandle Finish;
        GetWorldTimerManager().SetTimer(Finish,FTimerDelegate::CreateWeakLambda(Audio,[Audio]{Audio->Stop();}),
            Wave->Duration/Pitch+.13f,false);
    }
#if WITH_DEV_AUTOMATION_TESTS
    if(FParse::Param(FCommandLine::Get(),TEXT("DuelCaptureMix")))
        UE_LOG(LogTemp,Display,TEXT("Stone boot contact: take=%02d t=%.4f effort=%.3f pitch=%.4f local=%d"),
            Pick+1,GetWorld()->GetTimeSeconds(),Load,Pitch,bLocalPlayer);
#endif
}

void ADuelArena::Sound(EDuelSound Cue, const FVector& At)
{
    if (!GetWorld()->AllowAudioPlayback())
    {
        return;
    }
    struct FSound
    {
        float Duration;
        float Frequency;
        float EndFrequency;
        float Noise;
        float Level;
    };
    FSound Style{.23f, 124.f, 58.f, .56f, .29f};
    switch (Cue)
    {
    case EDuelSound::Cut:     Style = {.19f, 345.f, 102.f, .67f, .24f}; break;
    case EDuelSound::Impact:  Style = {.36f, 91.f, 39.f, .46f, .42f}; break;
    case EDuelSound::HeavyImpact: Style = {.41f, 117.f, 33.f, .24f, .64f}; break;
    case EDuelSound::Block:   Style = {.32f, 521.f, 236.f, .18f, .40f}; break;
    case EDuelSound::ParryChime: Style = {.21f, 1341.f, 744.f, .10f, .40f}; break;
    case EDuelSound::Roll:    Style = {.26f, 160.f, 75.f, .72f, .17f}; break;
    case EDuelSound::Flask:   Style = {.58f, 660.f, 780.f, .03f, .21f}; break;
    case EDuelSound::Charge:  Style = {.57f, 105.f, 281.f, .19f, .26f}; break;
    case EDuelSound::SpellReady: Style = {.18f, 836.f, 1240.f, .045f, .25f}; break;
    case EDuelSound::FrostRecall: Style = {.28f, 141.f, 68.f, .32f, .29f}; break;
    case EDuelSound::Pulse:   Style = {.95f, 74.f, 28.f, .39f, .52f}; break;
    case EDuelSound::Victory: Style = {1.23f, 392.f, 523.f, .01f, .30f}; break;
    case EDuelSound::Defeat:  Style = {1.02f, 147.f, 58.f, .13f, .27f}; break;
    case EDuelSound::RainAtlas:    Style = {2.75f, 104.f, 104.f, .035f, .39f}; break;
    case EDuelSound::Footstep: PlayFootfall(At, .85f); return;
    case EDuelSound::VerdictBell:  Style = {3.1f, 154.f, 152.f, .01f, .49f}; break;
    case EDuelSound::RelicPickup:
    case EDuelSound::DoorLatch:
    case EDuelSound::Checkpoint:
    case EDuelSound::FogAdmission: break; // Dedicated authored interaction waveforms below.
    }

    FNorthSoundStyle NorthStyle;
    const bool bNorthInteraction = NorthSoundStyle(Cue, NorthStyle);
    if (bNorthInteraction) { Style.Duration = NorthStyle.Duration; Style.Level = NorthStyle.Gain; }

    if (!EffectsAttenuation)
    {
        EffectsAttenuation = NewObject<USoundAttenuation>(this);
        EffectsAttenuation->Attenuation.AttenuationShapeExtents = FVector(690.f);
        EffectsAttenuation->Attenuation.FalloffDistance = 2420.f;
    }
    const bool bBell = Cue == EDuelSound::RainAtlas || Cue == EDuelSound::VerdictBell;
    const bool bSpatial = !bBell && Cue != EDuelSound::Victory && Cue != EDuelSound::Defeat;
    constexpr int32 Rate = 24000;
    FSoundPCM& Prepared = CombatSoundPCM.FindOrAdd(Cue);
    if (Prepared.Samples.IsEmpty())
    {
        if (bNorthInteraction)
        {
            MakeNorthInteractionPCM(Cue, NorthStyle, Prepared.Samples);
        }
        else
        {
            const int32 Samples = FMath::RoundToInt(Rate * Style.Duration);
            Prepared.Samples.SetNumUninitialized(Samples);
            FRandomStream Noise(0x57495400u + static_cast<uint8>(Cue));
            float Phase = 0.f, SmoothedNoise = 0.f;
            double Energy = 0.;
            int32 Peak = 1;
            for (int32 Index = 0; Index < Samples; ++Index)
            {
                const float Time = static_cast<float>(Index) / Rate;
                const float Alpha = Time / Style.Duration;
                Phase += 2.f * PI * FMath::Lerp(Style.Frequency, Style.EndFrequency, Alpha) / Rate;
                SmoothedNoise = .81f * SmoothedNoise + .19f * Noise.FRandRange(-1.f, 1.f);
                const float Envelope = FMath::Min(Time * 165.f, 1.f) * FMath::Square(1.f - Alpha);
                const float Pitch = bBell
                    ? FMath::Sin(Phase) * .57f + FMath::Sin(Phase * 2.76f) * .26f * FMath::Exp(-Time * 2.4f) +
                        FMath::Sin(Phase * 5.39f) * .16f * FMath::Exp(-Time * 5.8f) +
                        FMath::Sin(Phase * 1.5f) * .15f * FMath::Exp(-Time * 1.9f)
                    : FMath::Sin(Phase) * .67f + FMath::Sin(Phase * 1.503f) * .18f + FMath::Sin(Phase * 2.01f) * .11f;
                const bool bMetal = Cue == EDuelSound::Block || Cue == EDuelSound::ParryChime;
                const float Attack = Cue == EDuelSound::HeavyImpact || bMetal
                    ? .33f * FMath::Sin(Phase * (bMetal ? 4.27f : 12.31f)) * FMath::Exp(-Time * 58.f) : 0.f;
                const float Signal = FMath::Lerp(Pitch, SmoothedNoise, Style.Noise) + Attack;
                const int16 Value = static_cast<int16>(Signal * Envelope * Style.Level * 32767.f);
                Prepared.Samples[Index] = Value;
                Energy += double(Value) * Value;
                Peak = FMath::Max(Peak, FMath::Abs(int32(Value)));
            }
            if (bSpatial)
            {
                const double RMS = FMath::Sqrt(Energy / FMath::Max(1, Samples));
                const float Target = Cue == EDuelSound::HeavyImpact ? 4300.f : Cue == EDuelSound::Impact ? 3500.f :
                    Cue == EDuelSound::SpellReady ? 1750.f : Cue == EDuelSound::FrostRecall ? 2150.f : 2900.f;
                Prepared.Gain = FMath::Min3(2.55f, 21200.f / Peak, Target / float(FMath::Max(1., RMS)));
            }
        }
    }
    USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(this);
    Wave->SetSampleRate(Rate);
    Wave->NumChannels = 1;
    Wave->Duration = Style.Duration;
    Wave->bLooping = false;
    Wave->QueueAudio(reinterpret_cast<const uint8*>(Prepared.Samples.GetData()), Prepared.Samples.Num() * sizeof(int16));
    const bool bVoicePlaying = FDuelGraphicsSettings::AudioGain(EDuelGraphicsRow::VoiceAudio) > KINDA_SMALL_NUMBER && ActiveVoice.IsValid() && GetWorld()->GetTimeSeconds() < VoiceUntil;
    const float Volume = Prepared.Gain * (bVoicePlaying ? (Cue == EDuelSound::Charge ? .84f : .59f) : 1.f) *
        FDuelGraphicsSettings::AudioGain(EDuelGraphicsRow::CombatAudio);
    UAudioComponent* Audio = UGameplayStatics::SpawnSoundAtLocation(this, Wave, At, FRotator::ZeroRotator,
        Volume, 1.f, 0.f, bSpatial ? EffectsAttenuation.Get() : nullptr);
    if (Audio)
    {
        FTimerHandle Stop;
        GetWorldTimerManager().SetTimer(Stop, FTimerDelegate::CreateWeakLambda(Audio, [Audio] { Audio->Stop(); }),
            Wave->Duration + .13f, false);
    }
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthInteractionSoundTest, "ManosabaDuel.NorthTower.InteractionSoundPCM",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthInteractionSoundTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    const EDuelSound Cues[] = {EDuelSound::RelicPickup, EDuelSound::DoorLatch, EDuelSound::Checkpoint, EDuelSound::FogAdmission};
    TArray<TArray<int16>> Clips;
    for (const auto Cue : Cues)
    {
        FNorthSoundStyle Style;
        TestTrue(TEXT("Interaction has its own short style"), NorthSoundStyle(Cue, Style) && Style.Duration > .3f && Style.Duration < 1.f);
        TArray<int16>& PCM = Clips.AddDefaulted_GetRef();
        MakeNorthInteractionPCM(Cue, Style, PCM);
        TestEqual(TEXT("24 kHz mono duration matches the declared playback"), PCM.Num(), FMath::RoundToInt(NorthSoundRate * Style.Duration));
        TestTrue(TEXT("No discontinuity at first or final sample"), PCM[0] == 0 && PCM.Last() == 0);
        double Energy = 0, Mean = 0; int32 Peak = 0;
        for (int16 Sample : PCM) { Peak = FMath::Max(Peak, FMath::Abs(int32(Sample))); Energy += double(Sample) * Sample; Mean += Sample; }
        const double RMS = FMath::Sqrt(Energy / PCM.Num()) / 32767.;
        TestTrue(TEXT("Audible signal with headroom; no hard limiter contact"), RMS > .004 && Peak > 1000 && Peak < 18000);
        TestTrue(TEXT("No appreciable DC offset"), FMath::Abs(Mean / PCM.Num()) < 75.);
        TArray<int16> Again; MakeNorthInteractionPCM(Cue, Style, Again);
        TestTrue(TEXT("Deterministic cue does not depend on arena RNG"), Again == PCM);
        AddInfo(FString::Printf(TEXT("North interaction cue=%d duration=%.3f gain=%.3f peak=%.5f rms=%.5f samples=%d"),
            int32(Cue), Style.Duration, Style.Gain, Peak / 32767., RMS, PCM.Num()));
    }
    for (int32 I = 0; I < Clips.Num(); ++I)
        for (int32 J = I + 1; J < Clips.Num(); ++J)
        {
            double Dot = 0, AA = 0, BB = 0;
            for (int32 K = 0; K < FMath::Min(Clips[I].Num(), Clips[J].Num()); ++K)
            { const double A = Clips[I][K], B = Clips[J][K]; Dot += A * B; AA += A * A; BB += B * B; }
            TestTrue(TEXT("Interaction cues do not reuse the same waveform"), FMath::Abs(Dot) / FMath::Sqrt(AA * BB) < .8);
        }
    FNorthSoundStyle Other;
    TestFalse(TEXT("Combat cue stays outside the interaction generator"), NorthSoundStyle(EDuelSound::Impact, Other));
    return !HasAnyErrors();
}
#endif
