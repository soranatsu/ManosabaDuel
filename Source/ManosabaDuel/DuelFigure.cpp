#include "DuelGame.h"
#include "DuelBossDefense.h"

#include "DuelArena.h"
#include "DuelGeometry.h"
#include "DuelFrostMirror.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "KismetProceduralMeshLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

using DuelGeometry::EShape;

namespace
{
    struct FFootCycle
    {
        float Travel;
        float Lift;
        float Plant;
    };

    FFootCycle FootCycle(float Phase)
    {
        // A longer stance and a shorter return make the foot read as a planted step.
        // Phase is distance driven by PoseFigure, rather than advancing while stationary.
        const float Cycle = FMath::Frac(Phase / (2.f * PI) + 2.f);
        constexpr float Stance = .58f;
        if (Cycle < Stance)
        {
            const float T = Cycle / Stance;
            return {1.f - 2.f * T, 0.f, FMath::Sin(PI * T)};
        }
        const float T = (Cycle - Stance) / (1.f - Stance);
        return {FMath::Lerp(-1.f, 1.f, FMath::SmoothStep(0.f, 1.f, T)),
            FMath::Square(FMath::Sin(PI * T)), 0.f};
    }

    float PreparationAccent(float Wind, float Strike)
    {
        // Zero throughout the damage-active interval: presentation does not extend the blade sweep.
        return Strike <= 0.f && Wind < 1.f ? FMath::Square(FMath::Sin(PI * Wind)) : 0.f;
    }

    struct FWitchCycle
    {
        float Gather = 0.f;
        float Open = 0.f;
        float Release = 0.f;
        float Crest = 0.f;
        float Hover = 0.f;
    };

    FWitchCycle WitchCycle(float Age, const DuelCourt::FMoveTiming& Timing)
    {
        const auto Progress = [Age](float Start, float Length)
        {
            return Length > 0.f ? FMath::SmoothStep(0.f, 1.f, (Age - Start) / Length) : 0.f;
        };
        const float Strike = Progress(Timing.Windup, Timing.Active);
        const float Recover = Progress(Timing.Windup + Timing.Active, Timing.Recovery);
        FWitchCycle Cycle;
        Cycle.Gather = Progress(0.f, Timing.Windup * .40f) * (1.f - Progress(Timing.Windup * .44f, Timing.Windup * .56f));
        Cycle.Open = Progress(Timing.Windup * .36f, Timing.Windup * .64f) * (1.f - Recover);
        Cycle.Release = Strike * (1.f - Recover);
        Cycle.Crest = Strike > 0.f && Strike < 1.f ? FMath::Sin(PI * Strike) : 0.f;
        Cycle.Hover = Progress(Timing.Windup * .27f, Timing.Windup * .73f + Timing.Active * .65f) * (1.f - Recover);
        return Cycle;
    }

    constexpr float NorthDefeatDuration = 1.45f;
    struct FNorthDefeatPose
    {
        FVector Offset = FVector::ZeroVector;
        FRotator Body = FRotator::ZeroRotator;
        FRotator Head = FRotator::ZeroRotator;
        FRotator Tail = FRotator::ZeroRotator;
        TStaticArray<FRotator,4> Limbs{FRotator::ZeroRotator,FRotator::ZeroRotator,FRotator::ZeroRotator,FRotator::ZeroRotator};
        TStaticArray<FRotator,4> Hinges{FRotator::ZeroRotator,FRotator::ZeroRotator,FRotator::ZeroRotator,FRotator::ZeroRotator};
        float Drop = 0.f;
    };

    FNorthDefeatPose NorthDefeatPose(EDuelCharacter Character, float Age)
    {
        const auto Phase=[Age](float A,float B){return FMath::SmoothStep(0.f,1.f,(Age-A)/(B-A));};
        const float Lose=Phase(0.f,.24f), Kneel=Phase(.14f,.64f), Brace=Phase(.35f,.80f);
        const float Drop=Phase(.70f,1.24f), Settle=Phase(1.15f,NorthDefeatDuration);
        const float Recoil=Lose*(1.f-Kneel), Contact=FMath::Sin(PI*Phase(.99f,1.31f))*(1.f-Settle);
        FNorthDefeatPose P;P.Drop=Drop;
        if(Character==EDuelCharacter::Ema)
        {
            // Ema's right knee gives way; the right arm tries to brace before she rolls onto her side.
            P.Body=FRotator(-8*Lose-11*Kneel+7*Drop,-4*Recoil+8*Drop,4*Lose+59*Drop+2*Contact);
            P.Head=FRotator(-11*Lose-10*Brace+12*Drop,9*Brace-18*Drop,-5*Lose-11*Drop);
            P.Offset=FVector(7*Kneel+13*Drop,6*Kneel+23*Drop,-18*Kneel-2*Settle);
            P.Limbs={FRotator(8*Kneel+12*Drop,8*Drop,-4*Kneel),FRotator(28*Kneel-4*Drop,-7*Drop,9*Kneel),
                FRotator(15*Brace+12*Drop,-17*Brace,12*Brace+19*Drop),FRotator(28*Brace+15*Drop,14*Brace,-12*Brace-8*Drop)};
            P.Hinges={FRotator(-92*Kneel+12*Drop,0,0),FRotator(-112*Kneel+18*Drop,0,0),
                FRotator(-12*Brace-16*Drop,0,0),FRotator(-9*Brace-15*Drop,0,0)};
            P.Tail=FRotator(-22*Lose-16*Drop,13*Brace-10*Drop,-8*Drop);
        }
        else
        {
            // Hiro reaches with both arms, sinks onto folded knees and slumps forward to her left.
            P.Body=FRotator(-10*Lose-12*Kneel-37*Drop-2*Contact,5*Recoil-13*Drop,-5*Lose-18*Drop);
            P.Head=FRotator(-12*Lose-11*Brace+20*Drop,-7*Brace+13*Drop,4*Lose+9*Drop);
            P.Offset=FVector(8*Kneel+21*Drop,-4*Kneel-13*Drop,-20*Kneel-2*Settle);
            P.Limbs={FRotator(18*Kneel-4*Drop,7*Drop,-5*Kneel),FRotator(12*Kneel+6*Drop,-9*Drop,8*Kneel),
                FRotator(35*Brace+30*Drop,-19*Brace,13*Brace),FRotator(20*Brace+36*Drop,15*Brace,-18*Brace)};
            P.Hinges={FRotator(-105*Kneel+15*Drop,0,0),FRotator(-98*Kneel+12*Drop,0,0),
                FRotator(-8*Brace-6*Drop,0,0),FRotator(-12*Brace-8*Drop,0,0)};
            P.Tail=FRotator(-24*Lose-27*Drop,-10*Brace+9*Drop,5*Drop);
        }
        // Rotate around the low hips, then support the existing Q mesh at its skinned ground envelope.
        // Baked at 50 ms from the supplied bound GLBs (210607 / 214984 vertices); no CPU skinning in game.
        const FVector Hip(0,0,43);P.Offset+=Hip-P.Body.RotateVector(Hip);
        static constexpr float EmaGround[] = {0.000000f,0.169121f,0.722890f,1.311902f,2.827288f,5.195178f,7.909137f,10.337873f,12.020080f,12.826480f,13.096901f,13.119085f,13.030727f,12.994797f,12.994797f,13.226841f,13.747751f,14.224013f,14.286319f,13.670235f,12.415120f,10.708671f,9.510460f,11.503450f,12.502738f,12.854606f,13.211315f,13.690063f,14.060433f,14.208581f};
        static constexpr float HiroGround[] = {0.000000f,0.225042f,0.738357f,1.240970f,2.605260f,4.888473f,7.566457f,10.001882f,11.729313f,12.800965f,13.517906f,13.878054f,14.014430f,14.040457f,14.040457f,13.841966f,13.249424f,12.216924f,10.687556f,8.746761f,7.072325f,5.337311f,4.960781f,4.517530f,4.474003f,4.875826f,5.420201f,5.903034f,6.273404f,6.421553f};
        const float* Ground=Character==EDuelCharacter::Ema?EmaGround:HiroGround;
        const float Sample=FMath::Clamp(Age/.05f,0.f,29.f);
        const int32 Index=FMath::Min(FMath::FloorToInt(Sample),28);
        P.Offset.Z+=FMath::Lerp(Ground[Index],Ground[Index+1],Sample-Index)+.25f*Phase(0.f,.05f);
        return P;
    }

    FNorthDefeatPose NorthBossDefeatPose(EDuelCharacter Character, float Age)
    {
        const auto Ease = [Age](float Start, float End)
        {
            return FMath::SmoothStep(0.f, 1.f, (Age - Start) / (End - Start));
        };
        const float Flinch = Ease(0.f, .22f);
        FNorthDefeatPose P;
        if (Character == EDuelCharacter::Ema)
        {
            const float Brace = Ease(.24f, .70f), FallSide = Ease(.70f, 1.32f);
            P.Drop = FallSide;
            P.Body = FRotator(8.f * Flinch + 15.f * Brace - 15.f * FallSide, -19.f * Brace + 24.f * FallSide,
                -13.f * Flinch + 83.f * FallSide);
            P.Head = FRotator(-18.f * Flinch + 27.f * FallSide, -14.f * Brace, -19.f * FallSide);
            P.Offset = FVector(-12.f * Flinch + 20.f * FallSide, -13.f * Brace + 47.f * FallSide, -28.f * Brace + 11.f * FallSide);
            P.Limbs = {FRotator(26.f * Brace, 0, 13.f * FallSide), FRotator(32.f * Brace, -12.f * FallSide, 0),
                FRotator(-37.f * Brace + 54.f * FallSide, 0, 22.f * FallSide), FRotator(30.f * Brace, -18.f * Brace, -27.f * FallSide)};
            P.Hinges[0].Pitch = -55.f * Brace;
            P.Hinges[1].Pitch = -65.f * Brace + 15.f * FallSide;
            P.Hinges[3].Pitch = -37.f * Brace;
            P.Tail = FRotator(-27.f * Brace, 40.f * FallSide, -16.f * FallSide);
        }
        else if (Character == EDuelCharacter::Hiro)
        {
            const float Brace = Ease(.16f, .54f), Sink = Ease(.50f, 1.18f);
            P.Drop = Sink;
            P.Body = FRotator(14.f * Flinch - 12.f * Brace - 27.f * Sink, 14.f * Brace - 16.f * Sink, -18.f * Sink);
            P.Head = FRotator(8.f * Brace - 34.f * Sink, 20.f * (Brace - Sink), -14.f * Sink);
            P.Offset = FVector(22.f * Brace + 21.f * Sink, -15.f * Sink, -16.f * Brace - 9.f * Sink);
            P.Limbs = {FRotator(42.f * Brace, -18.f * Sink, 0), FRotator(37.f * Brace, 13.f * Sink, 0),
                FRotator(29.f * Brace + 29.f * Sink, 12.f * Sink, 0), FRotator(31.f * Brace + 23.f * Sink, -19.f * Sink, 0)};
            P.Hinges[0].Pitch = -78.f * Brace;
            P.Hinges[1].Pitch = -73.f * Brace;
            P.Hinges[2].Pitch = -47.f * Sink;
            P.Hinges[3].Pitch = -45.f * Sink;
            P.Tail = FRotator(28.f * (Brace - Sink) - 38.f * Sink, -58.f * Sink, -29.f * Sink);
        }
        else
        {
            const float Fold = Ease(.27f, .77f), FallBack = Ease(.72f, 1.38f);
            P.Drop = FallBack;
            P.Body = FRotator(-13.f * Flinch + 45.f * FallBack, 14.f * Fold, 8.f * Flinch - 60.f * FallBack);
            P.Head = FRotator(-17.f * Fold + 24.f * FallBack, -20.f * FallBack, 14.f * FallBack);
            P.Offset = FVector(-10.f * Fold - 34.f * FallBack, -28.f * FallBack, -31.f * Fold + 9.f * FallBack);
            P.Limbs = {FRotator(38.f * Fold, -8.f * FallBack, -10.f * FallBack), FRotator(31.f * Fold, 12.f * FallBack, 0),
                FRotator(-50.f * Flinch + 69.f * Fold, -22.f * FallBack, 17.f * FallBack),
                FRotator(-72.f * Flinch + 95.f * FallBack, 27.f * FallBack, -9.f * FallBack)};
            P.Hinges[0].Pitch = -66.f * Fold + 10.f * FallBack;
            P.Hinges[1].Pitch = -61.f * Fold;
            P.Hinges[2].Pitch = -34.f * Fold;
            P.Tail = FRotator(-33.f * FallBack, 24.f * FallBack, 14.f * FallBack);
        }
        // Keep the roll centred low in the body while the visual rig folds.
        const FVector Hips(0, 0, 49);
        P.Offset += Hips - P.Body.RotateVector(Hips);
        return P;
    }

    FRotator ReadyGripAngle(EDuelWeapon Weapon, float Side)
    {
        switch (Weapon)
        {
        case EDuelWeapon::Buckler:
        case EDuelWeapon::Greatshield:
            return FRotator(0.f, 20.f * Side, 0.f);
        case EDuelWeapon::Staff:
            return FRotator(68.f, 18.f * Side, 0.f);
        case EDuelWeapon::Spear:
            return FRotator(43.f, 18.f * Side, 43.f * Side);
        case EDuelWeapon::Greatblade:
        case EDuelWeapon::Houndfang:
        case EDuelWeapon::RitualSword:
            return FRotator(53.f, 28.f * Side, 63.f * Side);
        default:
            return FRotator(58.f, 22.f * Side, 64.f * Side);
        }
    }

    // Layers of simple custom geometry keep the video characters' readable outlines.
    void Ear(AActor* Owner, USceneComponent* Parent, const FVector& Origin, float Scale,
        float Side, UMaterialInterface* Outer, UMaterialInterface* Inner)
    {
        const auto Triangle = [&](const FVector& Offset, float Size, UMaterialInterface* Material)
        {
            const FVector A = (Origin + Offset + FVector(0, -25 * Side * Size, 0)) * Scale;
            const FVector B = (Origin + Offset + FVector(-5, 26 * Side * Size, 0)) * Scale;
            const FVector C = (Origin + Offset + FVector(-11, 22 * Side * Size, 60 * Size)) * Scale;
            const FVector Rear = FVector(-18 * Scale, 0, 0);
            const TArray<FVector> V{A, B, C, A + Rear, B + Rear, C + Rear};
            const TArray<int32> I{0, 1, 2, 0, 2, 1, 3, 4, 5, 3, 5, 4,
                0, 3, 4, 0, 4, 1, 0, 4, 3, 0, 1, 4,
                1, 4, 5, 1, 5, 2, 1, 5, 4, 1, 2, 5,
                2, 5, 3, 2, 3, 0, 2, 3, 5, 2, 0, 3};
            UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Owner);
            Mesh->SetupAttachment(Parent);
            Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Mesh->CreateMeshSection_LinearColor(0, V, I, TArray<FVector>(), TArray<FVector2D>(),
                TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false, false);
            Mesh->SetMaterial(0, Material);
            Owner->AddInstanceComponent(Mesh);
            Mesh->RegisterComponent();
        };
        Triangle(FVector::ZeroVector, 1.f, Outer);
        Triangle(FVector(1.6, 3.f * Side, 8), .70f, Inner);
    }

    void Blossom(AActor* Owner, USceneComponent* Parent, const FVector& Center, float Size,
        UMaterialInterface* Petal, UMaterialInterface* Stamen)
    {
        for (int32 Index = 0; Index < 5; ++Index)
        {
            const float Angle = Index * 72.f;
            const float Radians = FMath::DegreesToRadians(Angle);
            const FVector PetalCenter = Center + FVector(0, FMath::Sin(Radians) * Size * .42f,
                FMath::Cos(Radians) * Size * .42f);
            DuelGeometry::Shape(Owner, Parent, EShape::Ball, PetalCenter,
                FVector(Size * .32f, Size * .48f, Size * .78f), Petal, FRotator(0, 0, Angle));
        }
        DuelGeometry::Shape(Owner, Parent, EShape::Ball, Center + FVector(Size * .12f, 0, 0),
            FVector(Size * .32f, Size * .37f, Size * .37f), Stamen);
    }

    void SwordBlade(AActor* Owner, USceneComponent* Grip, UMaterialInterface* Steel,
        const FVector& Proportions = FVector::OneVector, bool bRitual = false)
    {
        const FVector Section[] = {{0, -6.8, 0}, {0, 0, 3.1}, {0, 6.8, 0}, {0, 0, -3.1}};
        const float Stations[] = {12.f, 23.f, bRitual ? 116.f : 104.f, 132.f};
        const float Widths[] = {.62f, 1.f, bRitual ? .69f : .79f, .012f};
        TArray<FVector> Positions;
        TArray<FVector> Normals;
        TArray<int32> Triangles;
        TArray<FVector2D> UVs;
        for (int32 Length = 0; Length < 3; ++Length)
        {
            for (int32 Side = 0; Side < 4; ++Side)
            {
                const int32 Next = (Side + 1) % 4;
                const FVector A = (FVector(Stations[Length], 0, 0) + Section[Side] * Widths[Length]) * Proportions;
                const FVector B = (FVector(Stations[Length + 1], 0, 0) + Section[Side] * Widths[Length + 1]) * Proportions;
                const FVector C = (FVector(Stations[Length + 1], 0, 0) + Section[Next] * Widths[Length + 1]) * Proportions;
                const FVector D = (FVector(Stations[Length], 0, 0) + Section[Next] * Widths[Length]) * Proportions;
                const FVector Normal = FVector::CrossProduct(B - A, D - A).GetSafeNormal();
                const int32 I = Positions.Num();
                Positions.Append({A, B, C, D});
                Normals.Append({Normal, Normal, Normal, Normal});
                Triangles.Append({I, I + 2, I + 1, I, I + 3, I + 2});
                UVs.Append({FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1)});
            }
        }
        UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Owner);
        Mesh->SetupAttachment(Grip);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->CreateMeshSection_LinearColor(0, Positions, Triangles, Normals, UVs,
            TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false, false);
        Mesh->SetMaterial(0, Steel);
        Owner->AddInstanceComponent(Mesh);
        Mesh->RegisterComponent();
    }

    void PenBarrel(AActor* Owner, USceneComponent* Grip, TConstArrayView<FVector2D> Profile, UMaterialInterface* Material)
    {
        constexpr int32 Sides = 20;
        TArray<FVector> Positions, Normals;
        TArray<FVector2D> UVs;
        TArray<int32> Triangles;
        for (int32 Ring = 0; Ring < Profile.Num(); ++Ring)
        {
            const FVector2D Prev = Profile[FMath::Max(0, Ring - 1)];
            const FVector2D Next = Profile[FMath::Min(Ring + 1, Profile.Num() - 1)];
            for (int32 Side = 0; Side < Sides; ++Side)
            {
                const float Angle = 2.f * PI * Side / Sides;
                const FVector Radial(0, FMath::Cos(Angle), FMath::Sin(Angle));
                Positions.Add(FVector(Profile[Ring].X, 0, 0) + Radial * Profile[Ring].Y);
                Normals.Add((Radial * (Next.X - Prev.X) + FVector(Prev.Y - Next.Y, 0, 0)).GetSafeNormal());
                UVs.Emplace(Ring / static_cast<float>(Profile.Num() - 1), Side / static_cast<float>(Sides));
                if (Ring > 0)
                {
                    const int32 A = (Ring - 1) * Sides + Side;
                    const int32 B = (Ring - 1) * Sides + (Side + 1) % Sides;
                    Triangles.Append({A, B, A + Sides, B, B + Sides, A + Sides});
                }
            }
        }
        UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Owner);
        Mesh->SetupAttachment(Grip);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->CreateMeshSection_LinearColor(0, Positions, Triangles, Normals, UVs, {}, {}, false, false);
        Mesh->SetMaterial(0, Material);
        Owner->AddInstanceComponent(Mesh);
        Mesh->RegisterComponent();
    }

    void AxeHead(AActor* Owner, USceneComponent* Grip, UMaterialInterface* Steel)
    {
        const FVector Profile[] = {{95, -12, 0}, {121, -18, 0}, {129, -38, 0}, {153, -43, 0},
            {166, -22, 0}, {166, 22, 0}, {153, 43, 0}, {129, 38, 0}, {121, 18, 0}, {95, 12, 0}};
        TArray<FVector> Positions, Normals;
        TArray<FVector2D> UVs;
        TArray<int32> Triangles;
        const auto Face = [&](const FVector& A, const FVector& B, const FVector& C)
        {
            const int32 I = Positions.Num();
            const FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
            for (const FVector& P : {A, B, C})
            {
                Positions.Add(P);
                Normals.Add(Normal);
                UVs.Emplace((P.X - 95.) / 71., (P.Y + 43.) / 86.);
            }
            Triangles.Append({I, I + 2, I + 1});
        };
        const FVector Center(132, 0, 0);
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Profile); ++Index)
        {
            const FVector A = Profile[Index];
            const FVector B = Profile[(Index + 1) % UE_ARRAY_COUNT(Profile)];
            for (const double Side : {-1., 1.})
            {
                const FVector Flat = Center + FVector(0, 0, 6.5 * Side);
                const FVector Near = FMath::Lerp(A, Center, .14) + FVector(0, 0, 6.5 * Side);
                const FVector Far = FMath::Lerp(B, Center, .14) + FVector(0, 0, 6.5 * Side);
                if (Side > 0.)
                {
                    Face(Flat, Near, Far);
                    Face(Near, A, B);
                    Face(Near, B, Far);
                }
                else
                {
                    Face(Flat, Far, Near);
                    Face(Near, B, A);
                    Face(Near, Far, B);
                }
            }
        }
        UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Owner);
        Mesh->SetupAttachment(Grip);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->CreateMeshSection_LinearColor(0, Positions, Triangles, Normals, UVs,
            TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false, false);
        Mesh->SetMaterial(0, Steel);
        Owner->AddInstanceComponent(Mesh);
        Mesh->RegisterComponent();
    }
}

USceneComponent* ADuelFighter::Joint(USceneComponent* Parent, const FVector& Position)
{
    return DuelGeometry::Joint(this, Parent, Position);
}

void ADuelFighter::BuildFigure()
{
    if (!ShowCharacterForm(false))
    {
        if (Character == EDuelCharacter::Yuki)
        {
            BuildYuki();
        }
        else if (bBoss)
        {
            BuildEma();
        }
        else
        {
            BuildHiro();
        }
    }
    if (Character == EDuelCharacter::Yuki && bBoss)
    {
        BuildYukiPrisms();
    }
    PoseFigure(0.f);
}

bool ADuelFighter::ShowCharacterForm(bool bAlternate)
{
    if (bAlternate && !CharacterMesh)
    {
        return false;
    }
    const bool bStanding = bBoss == bAlternate;
    if (Character == EDuelCharacter::Yuki && (bStanding || !bBoss))
    {
        return false;
    }
    const TCHAR* Name = Character == EDuelCharacter::Yuki ? TEXT("SK_YukiQuad") : Character == EDuelCharacter::Ema
        ? (bStanding ? TEXT("SK_EmaStand") : TEXT("SK_EmaQuad"))
        : (bStanding ? TEXT("SK_HiroStand") : TEXT("SK_HiroQuad"));
    if (CharacterMesh && CharacterMesh->GetSkinnedAsset()->GetFName() == FName(Name))
    {
        return true;
    }
    const FString Path = FString::Printf(TEXT("/Game/DuelArt/Characters/%s/%s.%s"), Name, Name, Name);
    USkeletalMesh* Asset = LoadObject<USkeletalMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (!Asset)
    {
        return false;
    }
    const FReferenceSkeleton& Skeleton = Asset->GetRefSkeleton();
    const FName Driven[] = {TEXT("DuelHead"), TEXT("DuelTail"), TEXT("DuelLimb0"),
        TEXT("DuelLimb1"), TEXT("DuelLimb2"), TEXT("DuelLimb3"),
        TEXT("DuelHinge0"), TEXT("DuelHinge1"), TEXT("DuelHinge2"), TEXT("DuelHinge3")};
    TStaticArray<int32, UE_ARRAY_COUNT(Driven)> Indices;
    for (int32 I = 0; I < UE_ARRAY_COUNT(Driven); ++I)
    {
        Indices[I] = Skeleton.FindBoneIndex(Driven[I]);
        if (Indices[I] == INDEX_NONE)
        {
            return false;
        }
    }
    const int32 MouthIndex = Skeleton.FindBoneIndex(TEXT("DuelMouth"));
    const int32 GripIndex = Skeleton.FindBoneIndex(TEXT("DuelGrip"));
    const int32 FlaskIndex = Skeleton.FindBoneIndex(TEXT("DuelFlask"));
    const int32 LeftWingIndex = Skeleton.FindBoneIndex(TEXT("DuelWing0"));
    const int32 RightWingIndex = Skeleton.FindBoneIndex(TEXT("DuelWing1"));
    if (MouthIndex == INDEX_NONE ||
        (!bAlternate && (bBoss ? Character != EDuelCharacter::Yuki && (LeftWingIndex == INDEX_NONE || RightWingIndex == INDEX_NONE)
                            : GripIndex == INDEX_NONE || FlaskIndex == INDEX_NONE)))
    {
        return false;
    }
    const TArray<FTransform>& Rest = Skeleton.GetRefBonePose();
    const FVector GripOffset = GripIndex == INDEX_NONE ? FVector::ZeroVector :
        Rest[GripIndex].GetTranslation() - Rest[Indices[9]].GetTranslation();
    const FVector FlaskOffset = FlaskIndex == INDEX_NONE ? FVector::ZeroVector :
        Rest[FlaskIndex].GetTranslation() - Rest[Indices[8]].GetTranslation();
    const bool bFirst = !CharacterMesh;
    if (bFirst)
    {
        CharacterMesh = NewObject<UPoseableMeshComponent>(this, TEXT("DuelCharacterMesh"));
        CharacterMesh->SetupAttachment(Figure);
        CharacterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        CharacterMesh->SetBoundsScale(bBoss ? 2.75f : 1.25f);
        CharacterMesh->SetSkinnedAssetAndUpdate(Asset);
        AddInstanceComponent(CharacterMesh);
        CharacterMesh->RegisterComponent();
        Head = Joint(Figure, Rest[Indices[0]].GetTranslation());
        Tail = Joint(Figure, Rest[Indices[1]].GetTranslation());
        Mouth = Joint(Head, Rest[MouthIndex].GetTranslation());
        for (int32 I = 0; I < 4; ++I)
        {
            Limbs.Add(Joint(Figure, Rest[Indices[I + 2]].GetTranslation()));
            Hinges.Add(Joint(Limbs[I], Rest[Indices[I + 6]].GetTranslation()));
        }
        BuildBreath();
        if (bBoss && Character != EDuelCharacter::Yuki)
        {
            BuildWings(Rest[LeftWingIndex].GetTranslation(), Rest[RightWingIndex].GetTranslation());
            BuildHiroTransformation();
        }
        else if (!bBoss)
        {
            BuildProps(GripOffset, FlaskOffset);
        }
    }
    else
    {
        // Poseable meshes reuse their local transforms when two skeletons have the same bone count.
        CharacterMesh->BoneSpaceTransforms.Reset();
        CharacterMesh->SetSkinnedAssetAndUpdate(Asset);
        Head->SetRelativeLocation(Rest[Indices[0]].GetTranslation());
        Tail->SetRelativeLocation(Rest[Indices[1]].GetTranslation());
        Mouth->SetRelativeLocation(Rest[MouthIndex].GetTranslation());
        for (int32 I = 0; I < 4; ++I)
        {
            Limbs[I]->SetRelativeLocation(Rest[Indices[I + 2]].GetTranslation());
            Hinges[I]->SetRelativeLocation(Rest[Indices[I + 6]].GetTranslation());
        }
        if (!bAlternate)
        {
            if (bBoss && Character != EDuelCharacter::Yuki)
            {
                Wings[0]->SetRelativeLocation(Rest[LeftWingIndex].GetTranslation());
                Wings[1]->SetRelativeLocation(Rest[RightWingIndex].GetTranslation());
            }
            else if (!bBoss)
            {
                Weapon->SetRelativeLocation(GripOffset);
                FlaskProp->SetRelativeLocation(FlaskOffset);
                LeftWeapon->SetRelativeLocation(FlaskOffset);
            }
        }
    }
    PoseBones = Indices;
    bPoseReady = false;
    bAlternateForm = bAlternate;
    return true;
}

bool ADuelFighter::SelectCharacter(EDuelCharacter Selection)
{
    if (Selection == Character)
    {
        return true;
    }
    if (Selection == EDuelCharacter::Yuki || Character == EDuelCharacter::Yuki)
    {
        return false;
    }
    const EDuelCharacter Previous = Character;
    Character = Selection;
    if (ShowCharacterForm(bAlternateForm))
    {
        if (!DuelCourt::SupportsArt(ArtSelection, Selection))
        {
            ArtSelection = EDuelArt::Default;
        }
        if (!bBoss)
        {
            const auto* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
            const bool bExpedition = Mode && Mode->UsesExpeditionLoot();
            for (EDuelHand Hand : {EDuelHand::Right, EDuelHand::Left})
            {
                EDuelWeapon& Reserve = Hand == EDuelHand::Right ? RightReserve : LeftReserve;
                if (!DuelCourt::CanEquip(EquippedWeapon(Hand), Selection, Hand))
                {
                    SetWeapon(bExpedition ? DuelCourt::PersonalWeapon(Selection) :
                        DuelCourt::NextWeapon(EquippedWeapon(Hand), Hand, Selection, 1, Reserve), Hand);
                }
                if (!DuelCourt::CanEquip(Reserve, Selection, Hand))
                {
                    Reserve = bExpedition ? DuelCourt::PersonalWeapon(Selection) :
                        DuelCourt::NextWeapon(Reserve, Hand, Selection, 1, EquippedWeapon(Hand));
                }
            }
        }
        return true;
    }
    Character = Previous;
    return false;
}

void ADuelFighter::SetCharacterPreview(bool bPreview)
{
    bCharacterPreview = bPreview;
    if (Weapon)
    {
        Weapon->SetHiddenInGame(bPreview, true);
    }
    if (LeftWeapon)
    {
        LeftWeapon->SetHiddenInGame(bPreview, true);
    }
}

void ADuelFighter::PoseCharacterMesh()
{
    if (!CharacterMesh)
    {
        return;
    }
    const TArray<FTransform>& Rest = CharacterMesh->GetSkinnedAsset()->GetRefSkeleton().GetRefBonePose();
    TArray<FTransform>& Pose = CharacterMesh->BoneSpaceTransforms;
    if (Pose.Num() != Rest.Num())
    {
        return;
    }
    const USceneComponent* Joints[] = {Head, Tail, Limbs[0], Limbs[1], Limbs[2], Limbs[3],
        Hinges[0], Hinges[1], Hinges[2], Hinges[3]};
    for (int32 I = 0; I < UE_ARRAY_COUNT(Joints); ++I)
    {
        const int32 Index = PoseBones[I];
        Pose[Index].SetTranslation(Joints[I]->GetRelativeLocation());
        Pose[Index].SetRotation(Joints[I]->GetRelativeRotation().Quaternion() * Rest[Index].GetRotation());
        Pose[Index].SetScale3D(Joints[I]->GetRelativeScale3D() * Rest[Index].GetScale3D());
    }
    // Keep the existing skeleton's marker hierarchy while the equipment follows the elbow.
    const FName Hands[] = {TEXT("DuelFlask"), TEXT("DuelGrip")};
    for (int32 I = 0; I < UE_ARRAY_COUNT(Hands); ++I)
    {
        const int32 Index = CharacterMesh->GetBoneIndex(Hands[I]);
        if (Index != INDEX_NONE && Weapon && LeftWeapon)
        {
            const USceneComponent* Grip = I ? Weapon.Get() : LeftWeapon.Get();
            Pose[Index].SetTranslation(Hinges[I + 2]->GetRelativeTransform().TransformPosition(Grip->GetRelativeLocation()));
        }
    }
    CharacterMesh->MarkRefreshTransformDirty();
}

void ADuelFighter::SetNorthExtractionPose(float RungCycle, float Weight, bool bSeated)
{
    const bool bWasBoarding = NorthExtractionWeight > 0.f;
    NorthExtractionCycle = RungCycle;
    NorthExtractionWeight = Weight;
    bNorthExtractionSeated = bSeated;
    if (Weapon && bWasBoarding != (Weight > 0.f)) Weapon->SetHiddenInGame(bCharacterPreview || Weight > 0.f, true);
}

void ADuelFighter::BuildHead(float Size)
{
    UMaterialInterface* Face = DuelGeometry::ColorMaterial(this, FLinearColor(.93f, .73f, .71f));
    UMaterialInterface* Hair = DuelGeometry::ColorMaterial(this,
        bBoss ? FLinearColor(.88f, .91f, .96f) : FLinearColor(.012f, .014f, .020f));
    UMaterialInterface* HairShade = DuelGeometry::ColorMaterial(this,
        bBoss ? FLinearColor(.72f, .71f, .81f) : FLinearColor(.030f, .026f, .036f));
    UMaterialInterface* Tips = DuelGeometry::ColorMaterial(this,
        bBoss ? FLinearColor(.78f, .32f, .45f) : FLinearColor(.024f, .018f, .027f));
    UMaterialInterface* Coal = DuelGeometry::ColorMaterial(this, FLinearColor(.013f, .012f, .018f));
    UMaterialInterface* Pink = DuelGeometry::ColorMaterial(this,
        bBoss ? FLinearColor(.89f, .31f, .48f) : FLinearColor(.54f, .022f, .047f));
    UMaterialInterface* Iris = DuelGeometry::ColorMaterial(this,
        bBoss ? FLinearColor(.69f, .055f, .20f) : FLinearColor(.71f, .024f, .051f));
    UMaterialInterface* Gleam = DuelGeometry::ColorMaterial(this, FLinearColor(.96f, .91f, .89f));
    UMaterialInterface* Petal = DuelGeometry::ColorMaterial(this,
        bBoss ? FLinearColor(.96f, .72f, .77f) : FLinearColor(.66f, .021f, .045f));
    UMaterialInterface* Tongue = DuelGeometry::ColorMaterial(this, FLinearColor(.49f, .12f, .17f));
    const auto Ball = [&](const FVector& At, const FVector& Dimensions, UMaterialInterface* Surface,
        const FRotator& Rotation = FRotator::ZeroRotator)
    {
        return DuelGeometry::Shape(this, Head, EShape::Ball, At * Size, Dimensions * Size, Surface, Rotation);
    };

    Ball(FVector(-19, 0, 15), FVector(128, 163, 144), Hair);
    Ball(FVector(10, 0, -8), FVector(123, 150, 124), Face);
    for (float Side : {-1.f, 1.f})
    {
        Ball(FVector(-21, 56 * Side, bBoss ? -15 : -49), FVector(85, 49, bBoss ? 108.0 : 163.0),
            HairShade, FRotator(-11, 0, Side * 12));
        Ball(FVector(-28, 62 * Side, bBoss ? -38 : -76), FVector(67, 31, bBoss ? 56.0 : 118.0),
            Tips, FRotator(-12, Side * 3, Side * 15));
        Ball(FVector(17, 67 * Side, -11), FVector(53, 30, 97), Hair, FRotator(3, Side * 8, Side * -16));
        if (bBoss)
        {
            Ball(FVector(21, 71 * Side, -41), FVector(32, 20, 40), Tips, FRotator(18, 0, Side * 17));
        }

        const FRotator EyeAngle(0, 0, Side * 12.f);
        Ball(FVector(64, 29 * Side, -8), FVector(11, 42, 30), Coal, EyeAngle);
        Ball(FVector(67.6, 29 * Side, -9.5), FVector(7.5, 37, 24), Gleam, EyeAngle);
        Ball(FVector(71.5, 29 * Side, -9.4), FVector(7, 25, 24), Iris, EyeAngle);
        Ball(FVector(75, 28 * Side, -8.4), FVector(3.8, 12, 18), Pink);
        Ball(FVector(77.5, 27 * Side - 3.5, -4.7), FVector(2.2, 6.1, 6.1), Gleam);
        Ball(FVector(63, 47 * Side, -30), FVector(5, 23, 8), Pink, FRotator(0, 0, Side * 10.f));
        DuelGeometry::Rod(this, Head, FVector(69, 16 * Side, 6) * Size,
            FVector(64, 47 * Side, 14) * Size, 1.7f * Size, Coal);

        Ear(this, Head, FVector(-20, 55 * Side, 57), Size, Side, Hair, Pink);
    }

    for (int32 Strand = 0; Strand < 6; ++Strand)
    {
        const float Side = -52.f + Strand * 20.f;
        const float Low = bBoss ? FMath::Abs(Side - 10.f) * .13f + (Strand % 3) * 3.4f
            : FMath::Abs(Side) * .06f;
        Ball(FVector(52.2 - FMath::Abs(Side) * .25, Side, 32 + Low),
            FVector(47, Strand % 2 ? 34.0 : 30.0, 66 - Low),
            Strand % 3 ? Hair : HairShade, FRotator(6.0, 0, bBoss ? -17 + Strand * 4.f : 1.f));
    }

    Mouth = Joint(Head, FVector(72, 0, -35) * Size);
    DuelGeometry::Shape(this, Mouth, EShape::Ball, FVector::ZeroVector,
        FVector(7, 35, 20) * Size, Coal);
    DuelGeometry::Shape(this, Mouth, EShape::Ball, FVector(4, 0, -1) * Size,
        FVector(3, 29, 15) * Size, Tongue);
    BuildBreath();

    if (bBoss)
    {
        Ball(FVector(-13, 1, 79), FVector(102, 66, 21), Coal, FRotator(9, 9, 8));
        Ball(FVector(-22, 1, 87), FVector(85, 58, 18), Coal, FRotator(9, 9, 8));
        Blossom(this, Head, FVector(43, 57, 52) * Size, 28 * Size, Petal, Pink);
        Blossom(this, Head, FVector(32, 72, 40) * Size, 18 * Size, Petal, Pink);
        DuelGeometry::Rod(this, Head, FVector(62, 41, 31) * Size, FVector(72, 44, 7) * Size, 1.9f * Size, Coal);
        DuelGeometry::Rod(this, Head, FVector(68, 51, 21) * Size, FVector(72, 34, 15) * Size, 1.9f * Size, Coal);
        DuelGeometry::Rod(this, Head, FVector(66, 51, 29) * Size, FVector(69, 34, 23) * Size, 1.9f * Size, Coal);
        Ball(FVector(26, 74, 22), FVector(11, 14, 45), Coal, FRotator(0, 0, -18));
    }
    else
    {
        for (int32 Lock = 0; Lock < 5; ++Lock)
        {
            const float Side = -54.f + Lock * 27.f;
            const float Height = 107.f - FMath::Abs(Side) * .62f + (Lock % 2) * 13.f;
            Ball(FVector(-66 + FMath::Abs(Side) * .19f, Side, -27),
                FVector(33, 33, Height), Lock % 2 ? Hair : HairShade,
                FRotator(-18.f + Lock * 2.7f, 4.f, Side * .24f));
        }
        Blossom(this, Head, FVector(49, -61, 43) * Size, 38 * Size, Petal, Coal);
        Blossom(this, Head, FVector(29, -73, 62) * Size, 16 * Size, Petal, Pink);
        Ball(FVector(25, -73, 9), FVector(18, 16, 64), Coal, FRotator(0, 0, 14));
        Ball(FVector(22, -77, -5), FVector(17, 11, 45), Pink, FRotator(0, 0, 14));
    }
}

void ADuelFighter::BuildBreath()
{
    if (UMaterialInterface* Vapor = DuelGeometry::ArtMaterial(TEXT("M_Breath")))
    {
        BreathMaterial = UMaterialInstanceDynamic::Create(Vapor, this);
        Breath = DuelGeometry::Shape(this, Head, EShape::Plane, FVector::ZeroVector,
            FVector(1), BreathMaterial);
        Breath->SetCastShadow(false);
        Breath->SetVisibility(false);
    }
}

void ADuelFighter::BuildYuki()
{
    UMaterialInterface* Ivory = DuelGeometry::ColorMaterial(this, FLinearColor(.82f, .86f, .96f), false, .76f, 0, true);
    UMaterialInterface* Silver = DuelGeometry::ColorMaterial(this, FLinearColor(.63f, .68f, .80f));
    UMaterialInterface* Navy = DuelGeometry::ColorMaterial(this, FLinearColor(.065f, .059f, .106f), false, .72f, 0, true);
    UMaterialInterface* Skin = DuelGeometry::ColorMaterial(this, FLinearColor(.94f, .77f, .74f));
    UMaterialInterface* Ice = DuelGeometry::ColorMaterial(this, FLinearColor(.48f, .67f, .88f), false, .33f);
    UMaterialInterface* Ink = DuelGeometry::ColorMaterial(this, FLinearColor(.033f, .027f, .053f));
    UMaterialInterface* Pink = DuelGeometry::ColorMaterial(this, FLinearColor(.78f, .27f, .35f));
    const auto Ball = [this](USceneComponent* Parent, const FVector& At, const FVector& Size,
        UMaterialInterface* Mat, const FRotator& Rot = FRotator::ZeroRotator)
    {
        return DuelGeometry::Shape(this, Parent, EShape::Ball, At, Size, Mat, Rot);
    };
    Head = Joint(Figure, FVector(30, 0, 153));
    Ball(Head, FVector(-18, 0, 18), FVector(133, 164, 143), Ivory);
    Ball(Head, FVector(17, 0, -15), FVector(121, 154, 117), Skin);
    Ball(Head, FVector(-17, 0, 78), FVector(151, 178, 44), Ivory);
    Ball(Head, FVector(-19, 0, 64), FVector(188, 226, 23), Navy);
    for (int32 Petal = 0; Petal < 18; ++Petal)
    {
        const FRotator Angle(0, Petal * 20.f, 0);
        const FVector Axis = Angle.Vector();
        const FVector At(Axis.X * 82.f - 19.f, Axis.Y * 103.f, 65.f);
        Ball(Head, At, FVector(39, 45, 18), Navy, Angle);
        Ball(Head, At - Axis * 7.f + FVector(0, 0, 10), FVector(38, 43, 15), Ivory, Angle);
    }
    for (float Side : {-1.f, 1.f})
    {
        Ear(this, Head, FVector(-26, 60 * Side, 78), .72f, Side, Ivory, Silver);
        Blossom(this, Head, FVector(40, Side * 67, 80), 20, Ivory, Ice);
        Ball(Head, FVector(60, Side * 31, -15), FVector(12, 41, 27), Ink, FRotator(0, 0, Side * 8));
        Ball(Head, FVector(65, Side * 31, -16), FVector(9, 32, 23), Ivory);
        Ball(Head, FVector(70, Side * 29, -16), FVector(7, 22, 21), Ice);
        Ball(Head, FVector(73, Side * 29 + 2, -12), FVector(3, 5, 5), Ivory);
        DuelGeometry::Rod(this, Head, FVector(64, Side * 17, -3), FVector(57, Side * 50, 3), 2.f, Ink);
        Ball(Head, FVector(-22, Side * 66, -33), FVector(52, 32, 137), Ivory, FRotator(-12, 0, Side * 10));
        Ball(Head, FVector(-54, Side * 54, -40), FVector(40, 38, 143), Silver, FRotator(-22, 0, Side * 12));
        Ball(Head, FVector(-36, Side * 75, 11), FVector(24, 17, 83), Navy, FRotator(-10, 0, Side * 5));
    }
    for (int32 I = -2; I <= 2; ++I)
    {
        Ball(Head, FVector(46 - FMath::Abs(I) * 5, I * 23, 28), FVector(53, 33, 59), Ivory, FRotator(6, 0, I * -9));
    }
    Mouth = Joint(Head, FVector(71, 0, -47));
    Ball(Mouth, FVector::ZeroVector, FVector(9, 39, 24), Ink);
    Ball(Mouth, FVector(3, 0, -.5), FVector(7, 34, 20), Pink);
    BuildBreath();

    Ball(Figure, FVector(-14, 0, 69), FVector(127, 106, 75), Ivory);
    for (int32 Layer = 0; Layer < 3; ++Layer)
    {
        const float Width = 108.f - 12.f * Layer;
        const FVector At(-63.f + 6.f * Layer, 0, 46.f + 13.f * Layer);
        Ball(Figure, At, FVector(85.f - Layer * 8.f, Width, 31), Layer == 0 ? Navy : Ivory);
        for (int32 I = 0; I < 12; ++I)
        {
            const FVector Dir = FRotator(0, I * 30.f, 0).Vector();
            Ball(Figure, At + FVector(Dir.X * 33, Dir.Y * (Width * .44f), 0), FVector(23, 23, 14),
                Layer == 0 ? Navy : Ivory);
        }
    }
    Ball(Figure, FVector(43, 0, 91), FVector(15, 32, 39), Ivory);
    Ball(Figure, FVector(52, 0, 97), FVector(10, 13, 20), Ice);
    for (float X : {29.f, -68.f})
    {
        for (float Side : {-1.f, 1.f})
        {
            USceneComponent* Limb = Joint(Figure, FVector(X, Side * 35, 59));
            Limbs.Add(Limb);
            Ball(Limb, FVector(0, 0, -20), FVector(38, 39, 47), Ivory);
            Ball(Limb, FVector(0, 0, -42), FVector(39, 39, 18), Navy);
            Ball(Limb, FVector(3, 0, -53), FVector(38, 32, 16), Navy);
        }
    }
    Tail = Joint(Figure, FVector(-99, 0, 71));
    const FVector TailPoints[] = {FVector::ZeroVector, FVector(-25, 0, 16), FVector(-51, 0, 32),
        FVector(-71, 0, 52), FVector(-81, 0, 77), FVector(-78, 0, 103)};
    for (int32 I = 1; I < UE_ARRAY_COUNT(TailPoints); ++I)
    {
        DuelGeometry::Rod(this, Tail, TailPoints[I - 1], TailPoints[I], 11.5f - .65f * I, Ivory);
        Ball(Tail, TailPoints[I], FVector(22 - I, 22 - I, 24 - I), Ivory);
    }
}

void ADuelFighter::BuildEma()
{
    UMaterialInterface* Snow = DuelGeometry::ColorMaterial(this, FLinearColor(.78f, .81f, .86f), false, .78f, 0.f, true);
    UMaterialInterface* Trim = DuelGeometry::ColorMaterial(this, FLinearColor(.028f, .032f, .041f), false, .74f, 0.f, true);
    UMaterialInterface* Pink = DuelGeometry::ColorMaterial(this, FLinearColor(.83f, .36f, .53f));
    UMaterialInterface* Bloom = DuelGeometry::ColorMaterial(this, FLinearColor(.95f, .74f, .79f));
    Head = Joint(Figure, FVector(60, 0, 163));
    BuildHead(1.f);
    DuelGeometry::Shape(this, Figure, EShape::Ball, FVector(-31, 0, 93), FVector(161, 116, 101), Snow);
    DuelGeometry::Shape(this, Figure, EShape::Ball, FVector(-79, 0, 80), FVector(91, 126, 71), Snow);
    DuelGeometry::Shape(this, Figure, EShape::Ball, FVector(35, 0, 102), FVector(85, 97, 97), Trim);
    DuelGeometry::Shape(this, Figure, EShape::Ball, FVector(75, 0, 95), FVector(22, 40, 74), Snow);
    DuelGeometry::Shape(this, Figure, EShape::Ball, FVector(85, 0, 115), FVector(19, 18, 17), Pink);
    for (float Side : {-1.f, 1.f})
    {
        DuelGeometry::Rod(this, Figure, FVector(77, Side * 5, 112), FVector(65, Side * 25, 88), 10.f, Snow);
        DuelGeometry::Rod(this, Figure, FVector(-6, Side * 44, 125), FVector(-48, Side * 56, 82), 7.f, Trim);
        DuelGeometry::Shape(this, Figure, EShape::Ball, FVector(-49, Side * 57, 91),
            FVector(18, 7, 15), Snow);
    }

    for (float Front : {1.f, -1.f})
    {
        for (float Side : {-1.f, 1.f})
        {
            USceneComponent* Leg = Joint(Figure, FVector(Front > 0 ? 39 : -76, Side * 43, 74));
            Limbs.Add(Leg);
            DuelGeometry::Shape(this, Leg, EShape::Ball, FVector(0, 0, -26), FVector(36, 36, 66),
                Front > 0 ? Snow : Trim, FRotator(Front > 0 ? -8 : 11, 0, 0));
            DuelGeometry::Shape(this, Leg, EShape::Tube, FVector(2, 0, -56), FVector(34, 34, 11), Trim);
            DuelGeometry::Shape(this, Leg, EShape::Ball, FVector(7, 0, -64), FVector(40, 32, 19), Snow);
            if (Front > 0.f)
            {
                Blossom(this, Leg, FVector(19, Side * 7, -45), 11, Bloom, Pink);
            }
        }
    }

    Tail = Joint(Figure, FVector(-98, 0, 91));
    DuelGeometry::Rod(this, Tail, FVector(0, 0, 0), FVector(-37, 3, 29), 14.f, Snow);
    DuelGeometry::Shape(this, Tail, EShape::Ball, FVector(-46, 4, 38), FVector(77, 33, 35), Snow,
        FRotator(37, 3, 0));
    DuelGeometry::Shape(this, Tail, EShape::Ball, FVector(-74, 8, 60), FVector(52, 32, 32), Pink,
        FRotator(37, 4, 0));
    BuildWings(FVector(-47, -42, 121), FVector(-47, 42, 121));
}

void ADuelFighter::BuildYukiPrisms()
{
    for (int32 Index = 0; Index < 6; ++Index)
    {
        USceneComponent* Prism = Joint(Figure, FVector::ZeroVector);
        YukiPrisms.Add(Prism);
        const float Length = Index % 3 == 0 ? 105.f : 78.f;
        DuelFrostMirror::Create(this, Prism, Index % 3, Length);
        Prism->SetVisibility(false, true);
    }
}

void ADuelFighter::BuildWings(const FVector& Left, const FVector& Right)
{
    UMaterialInterface* Ivory = DuelGeometry::ColorMaterial(this, FLinearColor(.81f, .78f, .72f));
    UMaterialInterface* DarkBone = DuelGeometry::ColorMaterial(this, FLinearColor(.53f, .49f, .50f));
    UStaticMesh* Tube = DuelGeometry::Mesh(EShape::Tube);
    UStaticMesh* Ball = DuelGeometry::Mesh(EShape::Ball);
    const bool bInstance = Character == EDuelCharacter::Ema && Tube && Ball && Ivory && DarkBone
        && Ivory->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes) && DarkBone->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes);
    for (float Side : {-1.f, 1.f})
    {
        USceneComponent* Wing = Joint(Figure, Side < 0.f ? Left : Right);
        Wings.Add(Wing);
        TArray<FTransform> BoneTransforms, JointTransforms;
        const auto Bone = [&](const FVector& A, const FVector& B, float Radius)
        {
            if (!bInstance)
            {
                DuelGeometry::Rod(this, Wing, A, B, Radius, Ivory);
                return;
            }
            const FVector Axis = B - A;
            BoneTransforms.Emplace(FRotationMatrix::MakeFromZ(Axis).Rotator(), (A + B) * .5,
                FVector(Radius * 2, Radius * 2, Axis.Size() + 1) / 100.);
        };
        const FVector Knuckles[] = {
            {0, 0, 0}, {-26, Side * 47, 42}, {-56, Side * 98, 77}, {-106, Side * 151, 112},
            {-154, Side * 190, 93}
        };
        for (int32 Index = 0; Index < 4; ++Index)
        {
            const float Radius = 9.f - Index * 1.35f;
            Bone(Knuckles[Index], Knuckles[Index + 1], Radius);
            const FVector JointSize(Radius * 2.3f);
            if (bInstance)
                JointTransforms.Emplace(FQuat::Identity, Knuckles[Index + 1], JointSize / 100.);
            else
                DuelGeometry::Shape(this, Wing, EShape::Ball, Knuckles[Index + 1], JointSize, DarkBone);
        }
        for (int32 Rib = 0; Rib < 6; ++Rib)
        {
            const FVector Root = FMath::Lerp(Knuckles[1], Knuckles[4], (Rib + .65f) / 6.4f);
            const FVector Bend = Root + FVector(-32, Side * 21, -29 - Rib * 2);
            const FVector Middle = Bend + FVector(-17, Side * 5, -34 + Rib * 1.8f);
            const FVector Tip = Middle + FVector(-10, Side * -17, -21 + Rib * 3);
            Bone(Root, Bend, 4.2f);
            Bone(Bend, Middle, 3.2f);
            Bone(Middle, Tip, 2.1f);
        }
        if (bInstance)
        {
            // Animation only drives the wing root. These transforms stay fixed in its local space.
            const auto Install = [&](UStaticMesh* SourceMesh, UMaterialInterface* Material, const TArray<FTransform>& Poses)
            {
                auto* Batch = NewObject<UInstancedStaticMeshComponent>(this);
                Batch->SetMobility(EComponentMobility::Movable);
                Batch->SetupAttachment(Wing);
                Batch->SetStaticMesh(SourceMesh);
                Batch->SetMaterial(0, Material);
                Batch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                Batch->SetCanEverAffectNavigation(false);
                Batch->SetCastShadow(true);
                Batch->ComponentTags.Add(TEXT("DuelBoneWingBatch"));
                Batch->AddInstances(Poses, false, false, false);
                AddInstanceComponent(Batch);
                Batch->RegisterComponent();
            };
            Install(Tube, Ivory, BoneTransforms);
            Install(Ball, DarkBone, JointTransforms);
        }
        Wing->SetVisibility(false, true);
    }
}

void ADuelFighter::BuildHiroTransformation()
{
    UMaterialInterface* Obsidian = DuelGeometry::ColorMaterial(this, FLinearColor(.052f, .010f, .018f), false, .31f, .74f);
    UMaterialInterface* Ember = DuelGeometry::ColorMaterial(this, FLinearColor(3.8f, .028f, .14f), true);
    for (int32 PawIndex = 0; PawIndex < 2; ++PawIndex)
    {
        USceneComponent* Claws = Joint(Limbs[PawIndex], FVector(16, 0, -60));
        ShadowClaws.Add(Claws);
        for (int32 Talon = -1; Talon <= 1; ++Talon)
        {
            const FVector Start(0, Talon * 10.f, 0);
            const FVector Tip(43.f - FMath::Abs(Talon) * 8.f, Talon * 13.f, -8.f);
            const FRotator Direction = FRotationMatrix::MakeFromZ(Tip - Start).Rotator();
            DuelGeometry::Shape(this, Claws, EShape::Cone, (Start + Tip) * .5,
                FVector(11, 9, FVector::Distance(Start, Tip)), Obsidian, Direction);
            DuelGeometry::Rod(this, Claws, Start + FVector(1, 0, 4), Tip, 1.7f, Ember)->SetCastShadow(false);
        }
        Claws->SetVisibility(false, true);
    }

    UMaterialInterface* Light = DuelGeometry::ArtMaterial(TEXT("M_SacredFx"));
    if (!Light)
    {
        return;
    }
    UMaterialInstanceDynamic* Ash = UMaterialInstanceDynamic::Create(Light, this);
    Ash->SetVectorParameterValue(TEXT("Tint"), FLinearColor(1.f, .026f, .115f));
    Ash->SetVectorParameterValue(TEXT("TileOrigin"), FLinearColor(0.f, .25f, 0.f));
    Ash->SetScalarParameterValue(TEXT("TileSize"), .25f);
    Ash->SetScalarParameterValue(TEXT("Gain"), 2.8f);
    Ash->SetScalarParameterValue(TEXT("Opacity"), .78f);
    Ash->SetScalarParameterValue(TEXT("SoftDepth"), 13.f);
    ShadowTail = Joint(Tail, FVector(-16, 0, 17));
    TArray<FVector> Positions;
    TArray<FVector2D> UVs;
    TArray<int32> Triangles;
    for (int32 Strand = -1; Strand <= 1; ++Strand)
    {
        for (int32 I = 0; I <= 18; ++I)
        {
            const float T = I / 18.f;
            const FVector At(-T * (219.f + Strand * 20.f), Strand * (5.f + T * 20.f), 106.f * FMath::Sin(T * 2.1f) - T * 17.f);
            const float Width = 16.f * FMath::Sin(T * PI) + .25f;
            const FVector Across(0, Width, Width * Strand);
            const int32 Edge = Positions.Num();
            Positions.Append({At - Across, At + Across});
            UVs.Append({FVector2D(T, 0), FVector2D(T, 1)});
            if (I > 0)
            {
                Triangles.Append({Edge - 2, Edge, Edge - 1, Edge - 1, Edge, Edge + 1});
            }
        }
    }
    UProceduralMeshComponent* Ribbon = NewObject<UProceduralMeshComponent>(this);
    Ribbon->SetupAttachment(ShadowTail);
    Ribbon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Ribbon->SetCastShadow(false);
    Ribbon->CreateMeshSection_LinearColor(0, Positions, Triangles, {}, UVs, {}, {}, false, false);
    Ribbon->SetMaterial(0, Ash);
    AddInstanceComponent(Ribbon);
    Ribbon->RegisterComponent();
    ShadowTail->SetVisibility(false, true);
}

void ADuelFighter::BuildHiro()
{
    UMaterialInterface* Cloth = DuelGeometry::ColorMaterial(this, FLinearColor(.025f, .020f, .028f), false, .81f, 0.f, true);
    UMaterialInterface* Red = DuelGeometry::ColorMaterial(this, FLinearColor(.48f, .019f, .038f), false, .75f, 0.f, true);
    UMaterialInterface* DarkRed = DuelGeometry::ColorMaterial(this, FLinearColor(.22f, .013f, .026f), false, .75f, 0.f, true);
    UMaterialInterface* Pale = DuelGeometry::ColorMaterial(this, FLinearColor(.74f, .70f, .66f));

    Head = Joint(Figure, FVector(5, 0, 102));
    BuildHead(.61f);
    DuelGeometry::Shape(this, Figure, EShape::Ball, FVector(0, 0, 58), FVector(45, 48, 66), Cloth);
    DuelGeometry::Shape(this, Figure, EShape::Ball, FVector(-12, 0, 38), FVector(46, 58, 33), Red);
    DuelGeometry::Shape(this, Figure, EShape::Ball, FVector(-20, 0, 59), FVector(26, 63, 63), DarkRed,
        FRotator(-9, 0, 0));
    DuelGeometry::Shape(this, Figure, EShape::Box, FVector(19, 0, 58), FVector(7, 9, 41), Red,
        FRotator(-10, 0, 0));
    DuelGeometry::Shape(this, Figure, EShape::Ball, FVector(22, 0, 77), FVector(14, 20, 9), Pale);
    for (float Side : {-1.f, 1.f})
    {
        DuelGeometry::Rod(this, Figure, FVector(23, 4 * Side, 66), FVector(15, 20 * Side, 83), 2.8f, Red);
    }

    for (float Side : {-1.f, 1.f})
    {
        USceneComponent* Leg = Joint(Figure, FVector(-3, Side * 14, 32));
        Limbs.Add(Leg);
        DuelGeometry::Shape(this, Leg, EShape::Ball, FVector(0, 0, -11), FVector(17, 18, 30), Cloth);
        DuelGeometry::Shape(this, Leg, EShape::Ball, FVector(6, 0, -24), FVector(28, 19, 17), Cloth);
    }
    for (float Side : {-1.f, 1.f})
    {
        USceneComponent* Arm = Joint(Figure, FVector(3, Side * 23, 70));
        Limbs.Add(Arm);
        DuelGeometry::Shape(this, Arm, EShape::Ball, FVector(3, Side * 4, -13), FVector(20, 19, 40), Cloth,
            FRotator(-6, 0, Side * 11));
        DuelGeometry::Shape(this, Arm, EShape::Tube, FVector(5, Side * 7, -29), FVector(18, 17, 8), Red);
        DuelGeometry::Shape(this, Arm, EShape::Ball, FVector(6, Side * 7, -34), FVector(16, 15, 16), Pale);
    }

    Tail = Joint(Figure, FVector(-20, 4, 45));
    DuelGeometry::Shape(this, Tail, EShape::Ball, FVector(-17, 3, 16), FVector(64, 18, 19), Cloth,
        FRotator(40, 4, 0));
    BuildProps(FVector(6, 7, -32), FVector(9, -8, -34));
}

void ADuelFighter::BuildProps(const FVector& Grip, const FVector& FlaskGrip)
{
    USceneComponent* RightHand = Hinges.Num() == 4 ? Hinges[3].Get() : Limbs[3].Get();
    USceneComponent* LeftHand = Hinges.Num() == 4 ? Hinges[2].Get() : Limbs[2].Get();
    Weapon = Joint(RightHand, Grip);
    LeftWeapon = Joint(LeftHand, FlaskGrip);
    BuildWeapons(Weapon, WeaponModels);
    BuildWeapons(LeftWeapon, LeftWeaponModels);
    SetWeapon(Equipped);
    SetWeapon(Offhand, EDuelHand::Left);

    FlaskProp = Joint(LeftHand, FlaskGrip);
    UMaterialInterface* Edge = DuelGeometry::ColorMaterial(this, FLinearColor(.53f, .53f, .59f), false, .29f, .78f);
    UMaterialInterface* Amber = DuelGeometry::ColorMaterial(this, FLinearColor(.55f, .23f, .055f), false, .28f, .24f);
    DuelGeometry::Shape(this, FlaskProp, EShape::Ball, FVector(0, 0, 2), FVector(15, 15, 20), Amber);
    DuelGeometry::Shape(this, FlaskProp, EShape::Tube, FVector(0, 0, 14), FVector(6, 6, 8), Edge);
    FlaskProp->SetVisibility(false, true);
}

void ADuelFighter::BuildWeapons(USceneComponent* Grip, TArray<TObjectPtr<USceneComponent>>& Models)
{
    UMaterialInterface* Steel = DuelGeometry::ColorMaterial(this, FLinearColor(.35f, .34f, .39f), false, .29f, .88f);
    UMaterialInterface* Gold = DuelGeometry::ColorMaterial(this, FLinearColor(.57f, .36f, .15f), false, .38f, .80f);
    UMaterialInterface* Wood = DuelGeometry::ColorMaterial(this, FLinearColor(.079f, .036f, .029f));
    UMaterialInterface* Forged = DuelGeometry::ArtMaterial(TEXT("M_ForgedSteel"));
    for (int32 Index = 0; Index <= static_cast<int32>(EDuelWeapon::RitualSword); ++Index)
    {
        const EDuelWeapon Type = static_cast<EDuelWeapon>(Index);
        if (Type == EDuelWeapon::Empty)
        {
            Models.Add(nullptr);
            continue;
        }
        USceneComponent* Model = Joint(Grip, FVector::ZeroVector);
        Models.Add(Model);
        if (DuelCourt::IsShield(Type))
        {
            const bool bGreatshield = Type == EDuelWeapon::Greatshield;
            const FVector Size = bGreatshield ? FVector(76, 103, 6.7) : FVector(49, 49, 4.5);
            DuelGeometry::Shape(this, Model, EShape::Tube, FVector(2, 0, 9), Size, Gold, FRotator(90, 0, 0));
            DuelGeometry::Shape(this, Model, EShape::Ball, FVector(6, 0, 9),
                FVector(bGreatshield ? 18 : 12, Size.Y - 7.f, Size.X - 7.f), Steel);
            DuelGeometry::Shape(this, Model, EShape::Ball, FVector(12, 0, 9), FVector(bGreatshield ? 15 : 10), Gold);
            for (int32 I = 0; I < 8; ++I)
            {
                const float Angle = FMath::DegreesToRadians(I * 45.f);
                const FVector Rim(0, FMath::Sin(Angle) * Size.Y * .40, FMath::Cos(Angle) * Size.X * .40);
                DuelGeometry::Rod(this, Model, FVector(11, 0, 9) + Rim * .33,
                    FVector(7, 0, 9) + Rim, bGreatshield ? 1.5f : .85f, Gold);
            }
            continue;
        }
        const FLinearColor Glow = DuelCourt::Armament(Type).Glow * .27f;
        UMaterialInterface* Inlay = DuelGeometry::ColorMaterial(this, Glow, true);
        if (Type == EDuelWeapon::Houndfang)
        {
            UMaterialInterface* BladeSteel = DuelGeometry::ColorMaterial(this,
                FLinearColor(.29f, .27f, .24f), false, .53f, .82f);
            UMaterialInterface* GroundEdge = DuelGeometry::ColorMaterial(this,
                FLinearColor(.49f, .46f, .42f), false, .38f, .83f);
            UMaterialInterface* Fittings = DuelGeometry::ColorMaterial(this,
                FLinearColor(.35f, .30f, .24f), false, .50f, .76f);
            UMaterialInterface* Leather = DuelGeometry::ColorMaterial(this,
                FLinearColor(.031f, .029f, .033f), false, .84f);
            UMaterialInterface* Wrap = DuelGeometry::ColorMaterial(this,
                FLinearColor(.045f, .041f, .043f), false, .73f);
            DuelGeometry::Rod(this, Model, FVector(-39, 0, 0), FVector(12, 0, 0), 4.1f, Leather);
            for (int32 Band = 0; Band < 9; ++Band)
            {
                const float X = -34.f + Band * 4.5f;
                DuelGeometry::Rod(this, Model, FVector(X, .2f, -.28f), FVector(X + 2.1f, -.2f, .28f), 4.65f, Wrap);
            }
            DuelGeometry::Shape(this, Model, EShape::Ball, FVector(-40, 0, 0), FVector(12, 10, 9), Fittings);
            DuelGeometry::Rod(this, Model, FVector(9, 0, 0), FVector(22, 0, 0), 5.3f, Fittings);
            for (float Side : {-1.f, 1.f})
            {
                const FVector Shoulder(14, Side * 6, 0);
                const FVector Bend(15 + Side * 2, Side * 11, 0);
                const FVector Hook(8 + Side * 2, Side * 15, 0);
                DuelGeometry::Rod(this, Model, FVector(16, 0, 0), Shoulder, 3.5f, Fittings);
                DuelGeometry::Rod(this, Model, Shoulder, Bend, 2.8f, Fittings);
                DuelGeometry::Shape(this, Model, EShape::Ball, Bend, FVector(5.6f), Fittings);
                DuelGeometry::Rod(this, Model, Bend, Hook, 1.9f, Fittings);
                DuelGeometry::Shape(this, Model, EShape::Ball, Hook, FVector(3.7f, 3.7f, 3.1f), Fittings);
            }

            struct FBladeSection
            {
                float X;
                float Spine;
                float Edge;
                float Thickness;
            };
            // Pin the root and tip to the swing's blade span. The recurve sits between them.
            constexpr FBladeSection Profile[] = {
                {19, -6, 6, 3.2f}, {27, -8, 5, 3.4f}, {36, -10, 2, 3.6f}, {46, -11, 1, 3.6f},
                {58, -10, 1, 3.5f}, {69, -11, 1, 3.4f}, {80, -15, -1, 3.3f}, {91, -20, -4, 3.2f},
                {103, -25, -7, 3.0f}, {114, -25.5f, -7, 2.8f}, {125, -22.5f, -5, 2.6f},
                {136, -17, -2, 2.3f}, {147, -10, 2, 1.9f}, {157, -3, 7, 1.5f},
                {167, 7, 14, 1.1f}, {175, 16, 20, .65f}, {182, 24.95f, 25.05f, .07f}
            };
            constexpr int32 Sides = 8;
            UProceduralMeshComponent* Curved = NewObject<UProceduralMeshComponent>(this);
            Curved->SetupAttachment(Model);
            Curved->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            TArray<FVector> Vertices;
            TArray<FVector2D> UV;
            TArray<int32> FaceTriangles;
            TArray<int32> BevelTriangles;
            for (int32 I = 0; I < UE_ARRAY_COUNT(Profile); ++I)
            {
                const FBladeSection& Section = Profile[I];
                const float T = Section.Thickness;
                const float Ridge = FMath::Lerp(Section.Spine, Section.Edge, .16f);
                const float Bevel = FMath::Lerp(Section.Spine, Section.Edge, .76f);
                const FVector2D CrossSection[Sides] = {{Section.Spine, -.22f * T}, {Section.Spine, .22f * T},
                    {Ridge, T}, {Bevel, .64f * T}, {Section.Edge, 0}, {Bevel, -.64f * T}, {Ridge, -T},
                    {FMath::Lerp(Section.Spine, Section.Edge, .06f), -.58f * T}};
                for (const FVector2D& Point : CrossSection)
                {
                    Vertices.Emplace(Section.X, Point.X, Point.Y);
                    UV.Emplace((Section.X - 19.f) / 87.f, (Point.X - Section.Spine) / 26.f);
                }
                for (int32 Face = 0; I > 0 && Face < Sides; ++Face)
                {
                    const int32 A = (I - 1) * Sides + Face;
                    const int32 B = (I - 1) * Sides + (Face + 1) % Sides;
                    TArray<int32>& Triangles = Face == 3 || Face == 4 ? BevelTriangles : FaceTriangles;
                    Triangles.Append({A, B, A + Sides, B, B + Sides, A + Sides});
                }
            }
            TArray<int32> Triangles = FaceTriangles;
            Triangles.Append(BevelTriangles);
            TArray<FVector> Normals;
            TArray<FProcMeshTangent> Tangents;
            UKismetProceduralMeshLibrary::CalculateTangentsForMesh(Vertices, Triangles, UV, Normals, Tangents);
            Curved->CreateMeshSection_LinearColor(0, Vertices, FaceTriangles, Normals, UV, {}, Tangents, false, false);
            Curved->CreateMeshSection_LinearColor(1, Vertices, BevelTriangles, Normals, UV, {}, Tangents, false, false);
            Curved->SetMaterial(0, BladeSteel);
            Curved->SetMaterial(1, GroundEdge);
            AddInstanceComponent(Curved);
            Curved->RegisterComponent();
            continue;
        }
        if (Type == EDuelWeapon::YukiPen)
        {
            auto* Lacquer = DuelGeometry::ColorMaterial(this, FLinearColor(.39f, .023f, .043f), false, .21f, .28f);
            auto* Ivory = DuelGeometry::ColorMaterial(this, FLinearColor(.86f, .76f, .57f), false, .28f, .09f);
            auto* Nib = DuelGeometry::ColorMaterial(this, FLinearColor(.76f, .77f, .81f), false, .32f, .40f);
            auto* Ink = DuelGeometry::ColorMaterial(this, FLinearColor(.022f, .014f, .026f));
            const FVector2D TailProfile[] = {{-37, 0}, {-35, 4.9}, {-30, 6.3}, {-23, 7.6}, {-18, 8.2}};
            const FVector2D BarrelProfile[] = {{-20, 8.1}, {-9, 9.1}, {17, 9.5}, {39, 9.2}, {62, 8.7}};
            const FVector2D GripProfile[] = {{65, 8.7}, {77, 8.3}, {89, 6.8}, {99, 6.6}};
            PenBarrel(this, Model, TailProfile, Ivory);
            PenBarrel(this, Model, BarrelProfile, Lacquer);
            PenBarrel(this, Model, GripProfile, Ivory);
            for (const FVector2D& Band : {FVector2D(-20, 8.8), FVector2D(61, 9.1), FVector2D(98, 7.1)})
            {
                DuelGeometry::Rod(this, Model, FVector(Band.X, 0, 0), FVector(Band.X + 6, 0, 0), Band.Y, Gold);
                DuelGeometry::Rod(this, Model, FVector(Band.X + 4, 0, 0), FVector(Band.X + 5, 0, 0), Band.Y + .35, Nib);
            }
            DuelGeometry::Rod(this, Model, FVector(-15, 0, 8), FVector(-12, 0, 11.3), 1.8f, Gold);
            DuelGeometry::Rod(this, Model, FVector(-12, 0, 11.3), FVector(32, 0, 11.7), 1.5f, Gold);
            DuelGeometry::Shape(this, Model, EShape::Ball, FVector(33, 0, 11.2), FVector(9, 4.4, 3.5), Gold);
            for (float S : {-1.f, 1.f})
            {
                DuelGeometry::Rod(this, Model, FVector(-13, S * 4, 8), FVector(55, S * 4, 8), .35f, Gold);
                DuelGeometry::Rod(this, Model, FVector(49, 0, 9.4), FVector(43, S * 3.2, 9.1), .45f, Gold);
                DuelGeometry::Rod(this, Model, FVector(49, 0, 9.4), FVector(54, S * 3.8, 8.8), .45f, Gold);
            }
            USceneComponent* Tip = Joint(Model, FVector(99, 0, 0));
            SwordBlade(this, Tip, Nib, FVector(30.0 / 132.0, .88, .59));
            DuelGeometry::Rod(this, Tip, FVector(11, 0, 1.9), FVector(26, 0, .95), .28f, Ink);
            DuelGeometry::Shape(this, Tip, EShape::Ball, FVector(14, 0, 1.9), FVector(3.4, 2.8, .9), Ink);
            continue;
        }
        if (Type == EDuelWeapon::RitualSword)
        {
            auto* RoseGold = DuelGeometry::ColorMaterial(this, FLinearColor(.67f, .23f, .19f), false, .35f, .26f);
            auto* PaleRose = DuelGeometry::ColorMaterial(this, FLinearColor(.92f, .54f, .45f), false, .38f, .20f);
            auto* RedBronze = DuelGeometry::ColorMaterial(this, FLinearColor(.28f, .069f, .062f), false, .42f, .65f);
            DuelGeometry::Rod(this, Model, FVector(-29, 0, 0), FVector(24, 0, 0), 4.6f, RedBronze);
            for (float X = -23; X < 19; X += 7)
            {
                DuelGeometry::Rod(this, Model, FVector(X, -2, 4.2), FVector(X + 4, 2, 4.2), 1.18f, RoseGold);
            }
            DuelGeometry::Shape(this, Model, EShape::Box, FVector(-29, 0, 0), FVector(8, 9, 9), PaleRose, FRotator(0, 0, 45));
            DuelGeometry::Shape(this, Model, EShape::Box, FVector(21, 0, 0), FVector(7, 61, 5.5), RoseGold);
            SwordBlade(this, Model, RoseGold, FVector(169.0 / 132.0, 1.82, 1.10), true);
            for (const float S : {-1.f, 1.f})
            {
                const FVector Eye(22, 0, S * 3.f);
                for (const float Side : {-1.f, 1.f})
                {
                    const FVector Loop[] = {Eye, Eye + FVector(-8, 7 * Side, 1 * S),
                        Eye + FVector(-16, 16 * Side, 1 * S), Eye + FVector(-5, 15 * Side, 1 * S),
                        Eye + FVector(6, 8 * Side, 1 * S), Eye};
                    for (int32 I = 1; I < UE_ARRAY_COUNT(Loop); ++I)
                    {
                        DuelGeometry::Rod(this, Model, Loop[I - 1], Loop[I], 1.15f, PaleRose);
                    }
                }
                DuelGeometry::Shape(this, Model, EShape::Ball, Eye, FVector(11, 11, 5), PaleRose);
                DuelGeometry::Shape(this, Model, EShape::Ball, Eye + FVector(0, 0, S * 2.2), FVector(6, 6, 2), RedBronze);
                for (int32 I = 0; I < 5; ++I)
                {
                    const float X = 34.f + I * 23.f;
                    const float Width = 11.f - I * .6f;
                    DuelGeometry::Rod(this, Model, FVector(X + 17, -Width, 1.7 * S), FVector(X, 0, 3.75 * S), 1.2f, PaleRose);
                    DuelGeometry::Rod(this, Model, FVector(X, 0, 3.75 * S), FVector(X - 11, Width, 1.7 * S), 1.2f, PaleRose);
                }
            }
            continue;
        }
        if (Type == EDuelWeapon::Staff || Type == EDuelWeapon::FirePoker)
        {
            const bool bStaff = Type == EDuelWeapon::Staff;
            const float End = bStaff ? 133.f : 126.f;
            DuelGeometry::Rod(this, Model, FVector(bStaff ? -73 : -28, 0, 0), FVector(End, 0, 0),
                bStaff ? 4.8f : 3.9f, bStaff ? Wood : Steel);
            for (float X : {-23.f, -13.f, 9.f, End - 13.f, End - 4.f})
            {
                DuelGeometry::Rod(this, Model, FVector(X, 0, 0), FVector(X + 3, 0, 0), 5.4f, Gold);
            }
            if (bStaff)
            {
                for (int32 I = 0; I < 5; ++I)
                {
                    const FVector Rib(0, 16 * FMath::Cos(2 * PI * I / 5), 16 * FMath::Sin(2 * PI * I / 5));
                    DuelGeometry::Rod(this, Model, FVector(117, 0, 0), FVector(140, 0, 0) + Rib, 2.7f, Gold);
                    DuelGeometry::Rod(this, Model, FVector(140, 0, 0) + Rib, FVector(158, 0, 0) + Rib * .43, 2.7f, Gold);
                }
                DuelGeometry::Shape(this, Model, EShape::Ball, FVector(141, 0, 0), FVector(26, 16, 16), Inlay);
            }
            else
            {
                DuelGeometry::Rod(this, Model, FVector(126, 0, 0), FVector(143, 12, 0), 4.4f, Forged ? Forged : Steel);
                DuelGeometry::Rod(this, Model, FVector(143, 12, 0), FVector(141, 23, 0), 3.5f, Steel);
                for (int32 I = 0; I < 4; ++I)
                {
                    DuelGeometry::Shape(this, Model, EShape::Ball, FVector(111 + I * 7, I * 2, 3), FVector(6, 2, 2), Inlay);
                }
            }
            continue;
        }
        if (Type == EDuelWeapon::Axe || Type == EDuelWeapon::Hammer)
        {
            const bool bAxe = Type == EDuelWeapon::Axe;
            DuelGeometry::Rod(this, Model, FVector(-41, 0, 0), FVector(bAxe ? 141 : 121, 0, 0), 5.4f, Wood);
            for (float X : {-39.f, -24.f, -8.f, 12.f, 87.f, 99.f})
            {
                DuelGeometry::Rod(this, Model, FVector(X, 0, 0), FVector(X + 4, 0, 0), 6.1f, Gold);
            }
            if (bAxe)
            {
                AxeHead(this, Model, Forged ? Forged : Steel);
                for (const float Side : {-1.f, 1.f})
                {
                    DuelGeometry::Rod(this, Model, FVector(105, 0, Side * 7.5),
                        FVector(148, Side * 16, Side * 7), 2.0f, Gold);
                    DuelGeometry::Shape(this, Model, EShape::Ball, FVector(129, 0, Side * 8),
                        FVector(8, 9, 3), Inlay);
                }
            }
            else
            {
                DuelGeometry::Shape(this, Model, EShape::Box, FVector(116, 0, 0), FVector(49, 59, 33), Forged ? Forged : Steel);
                for (float Side : {-1.f, 1.f})
                {
                    DuelGeometry::Shape(this, Model, EShape::Tube, FVector(116, Side * 27, 0),
                        FVector(47, 40, 10), Gold, FRotator(0, 0, 90));
                    DuelGeometry::Shape(this, Model, EShape::Box, FVector(116, Side * 31, 0),
                        FVector(39, 7, 27), Forged ? Forged : Steel);
                    DuelGeometry::Shape(this, Model, EShape::Ball, FVector(116, 0, Side * 17),
                        FVector(20, 18, 4), Inlay);
                }
            }
            continue;
        }
        const bool bGreat = Type == EDuelWeapon::Greatblade;
        const bool bSpear = Type == EDuelWeapon::Spear;
        const float Hilt = bSpear ? 75.f : (bGreat ? 37.f : 24.f);
        DuelGeometry::Rod(this, Model, FVector(-Hilt, 0, 0), FVector(bSpear ? 155.f : 16.f, 0, 0),
            bGreat ? 5.9f : 4.1f, Wood);
        for (float X = -Hilt + 7.f; X < 10.f; X += bGreat ? 7.f : 5.f)
        {
            DuelGeometry::Rod(this, Model, FVector(X, 0, 0), FVector(X + 1.7f, 0, 0),
                bGreat ? 6.5f : 4.65f, Gold);
        }
        DuelGeometry::Shape(this, Model, EShape::Ball, FVector(-Hilt, 0, 0),
            FVector(bGreat ? 16.f : 11.f), Gold);
        if (bSpear)
        {
            USceneComponent* Tip = Joint(Model, FVector(150.f, 0, 0));
            SwordBlade(this, Tip, Steel, FVector(.64, 1.28, 1.05));
            DuelGeometry::Rod(this, Model, FVector(142, 0, 0), FVector(176, 0, 0), 6.f, Gold);
            for (float Side : {-1.f, 1.f})
            {
                DuelGeometry::Rod(this, Model, FVector(148, 0, 0), FVector(179, Side * 12.f, 0), 3.1f, Gold);
                DuelGeometry::Shape(this, Tip, EShape::Box, FVector(39, 0, Side * 3.2), FVector(36, 1.6, .85), Inlay);
            }
            continue;
        }
        const float Span = bGreat ? 28.f : 17.f;
        for (float Side : {-1.f, 1.f})
        {
            DuelGeometry::Rod(this, Model, FVector(11, 0, 0), FVector(2, Span * Side, 0),
                bGreat ? 4.7f : 2.8f, Gold);
            DuelGeometry::Shape(this, Model, EShape::Ball, FVector(2, Span * Side, 0), FVector(bGreat ? 9.f : 5.f), Gold);
        }
        SwordBlade(this, Model, Steel, bGreat ? FVector(1.40, 2.9, 1.60) : FVector::OneVector);
        for (float Side : {-1.f, 1.f})
        {
            for (int32 I = 0; I < (bGreat ? 6 : 3); ++I)
            {
                DuelGeometry::Shape(this, Model, EShape::Box, FVector(30.f + I * 15.f, 0, Side * (bGreat ? 4.4f : 2.8f)),
                    FVector(8, bGreat ? 3.6 : 1.6, .7), Inlay, FRotator(0, I % 2 ? 33.f : -33.f, 0));
            }
        }
    }
}

void ADuelFighter::PoseFigure(float DeltaSeconds, FTransform* BladeGrip)
{
    if (!Head || Limbs.Num() != 4)
    {
        return;
    }
    const ADuelMode* PoseMode = GetWorld()->GetAuthGameMode<ADuelMode>();
    const bool bNorthDefeat = !IsAlive() && PoseMode && PoseMode->UsesNorthTower();
    const bool bNorthBossDefeat = bNorthDefeat && bBoss;
    FNorthDefeatPose DefeatPose;
    const float Speed = static_cast<float>(GetVelocity().Size2D());
    const float DesiredJog = FMath::Clamp(Speed / 395.f, 0.f, 1.f);
    const float Sprint = FMath::Clamp((Speed - 440.f) / 220.f, 0.f, 1.f);
    const bool bContactSample = BladeGrip != nullptr;
    if (!bBoss && !bContactSample)
    {
        for (const EDuelHand Hand : {EDuelHand::Right, EDuelHand::Left})
        {
            float& Raised = GuardPose[static_cast<uint8>(Hand)];
            const float Goal = IsGuarding(Hand) ? 1.f : 0.f;
            Raised = FMath::FInterpConstantTo(Raised, Goal, DeltaSeconds, 1.f / .18f);
        }
    }
    const bool bLocomotion = DuelCourt::AllowsFreeLocomotion(Move);
    const float GaitChange = bLocomotion ? DesiredJog - GaitWeight : 0.f;
    if (!bContactSample)
    {
        const float Blend = 1.f - FMath::Exp(-13.f * DeltaSeconds);
        GaitWeight = FMath::Lerp(GaitWeight, bLocomotion ? DesiredJog : 0.f, Blend);
        if (Speed > 18.f)
        {
            const FVector Direction = GetVelocity().GetSafeNormal2D();
            const double Alignment = FVector::DotProduct(GaitDirection, Direction);
            GaitDirection = Alignment < -.87 ? Direction :
                FMath::Lerp(GaitDirection, Direction, Blend).GetSafeNormal2D();
        }
        const float PreviousGait = Gait;
        Gait += DeltaSeconds * Speed * (bBoss ? .030f : FMath::Lerp(.022f, .0165f, Sprint));
        const float FootfallOffset = bBoss ? PI * .30f : 0.f;
        const int32 Plant = FMath::FloorToInt((Gait + FootfallOffset) / PI);
        if (Plant != FMath::FloorToInt((PreviousGait + FootfallOffset) / PI) && Arena && !bCharacterPreview &&
            IsAlive() && bLocomotion && Speed > 140.f && GetCharacterMovement()->IsMovingOnGround())
        {
            const FVector Foot = GetActorLocation() - FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) +
                GaitDirection * 24.f + GetActorRightVector() * (Plant % 2 ? 21.f : -21.f);
            Arena->PlayFootfall(Foot, bBoss ? .92f : .16f + .55f * Sprint, IsPlayerControlled());
            if (Sprint > .34f)
            {
                Arena->Burst(Foot, FLinearColor(.42f, .56f, .66f, .40f), 3, 132.f);
            }
        }
        if (Gait >= 2.f * PI)
        {
            Gait = FMath::Fmod(Gait, 2.f * PI);
        }
    }
    const float Jog = GaitWeight;
    const FVector LocalMotion = GetActorTransform().InverseTransformVectorNoScale(GaitDirection);
    const float Stride = FMath::Sin(Gait) * Jog;
    FVector Pivot(0, 0, -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() + FMath::Abs(Stride) * (bBoss ? 4.f : 7.f + 3.f * Sprint));
    FRotator Pose(-Sprint * 16.f + 2.2f * Stride + LocalMotion.X * Jog * -6.f, bBoss ? 0.f : -Stride * (5.f + 5.f * Sprint),
        Stride * 4.4f + LocalMotion.Y * Jog * 7.f);
    FRotator HeadPose(-Stride * 3, 0, Stride * -4.5f);
    const bool bPlayerStride = !bBoss && bLocomotion && GetCharacterMovement()->IsMovingOnGround();
    if (bPlayerStride)
    {
        const float Settle = FMath::Square(FMath::Sin(Gait * 2.f)) * Jog;
        const float Bank = LocalMotion.Y * Jog;
        Pivot.Z = -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() +
            Settle * (3.f + 2.f * Sprint) - FMath::Abs(GaitChange) * 3.f;
        Pivot.Y = Stride * (1.5f + Sprint);
        Pose = FRotator(-Jog * (5.f + 11.f * Sprint) - GaitChange * 11.f,
            -Stride * (5.f + 3.f * Sprint), Stride * 2.8f + Bank * 8.f);
        // The head settles against the shoulders; it no longer repeats the full body sway.
        HeadPose = FRotator(-Pose.Pitch * .48f, -Pose.Yaw * .68f, -Pose.Roll * .72f);
    }
    const float Breathe = bNorthDefeat ? 0.f : FMath::Sin(GetWorld()->GetTimeSeconds() * 2.15f);
    const DuelCourt::FMoveTiming Spec = DuelCourt::Timing(Move, IsAwakened(), MoveWeapon, MoveArt);
    const auto Progress = [&](float Start, float Length)
    {
        return Length > 0.f ? FMath::SmoothStep(0.f, 1.f, (MoveAge - Start) / Length) : 0.f;
    };
    const auto Beat = [&](float At, float Lead, float Tail)
    {
        return Progress(At - Lead, Lead) * (1.f - Progress(At, Tail));
    };
    const float Wind = Progress(0.f, Spec.Windup);
    const float Strike = Progress(Spec.Windup, Spec.Active);
    const float Recover = Progress(MoveRecoveryAt(Spec), Spec.Recovery);
    const float Commitment = Wind * (1.f - Recover);
    float CloseTail = 0.f, CloseWing = 0.f;
    const bool bHiroShadow = bBoss && Character == EDuelCharacter::Hiro && !bAlternateForm;
    const bool bWitching = bBoss && (Move == EDuelMove::Awaken || Move == EDuelMove::Eclipse);
    const FWitchCycle Witch = bWitching ? WitchCycle(MoveAge, Spec) : FWitchCycle{};
    const float WitchGather = Witch.Gather;
    const float WitchOpen = Witch.Open;
    const float WitchRelease = Witch.Release;
    const float WitchCrest = Witch.Crest;
    HitRecoil = FMath::Max(0.f, HitRecoil - DeltaSeconds * 4.8f);
    LandingPose = FMath::Max(0.f, LandingPose - DeltaSeconds);
    float MouthOpen = .72f;
    TStaticArray<FVector, 4> JointLocation;
    TStaticArray<FRotator, 4> JointRotation{FRotator::ZeroRotator, FRotator::ZeroRotator,
        FRotator::ZeroRotator, FRotator::ZeroRotator};
    TStaticArray<FRotator, 4> HingeRotation{FRotator::ZeroRotator, FRotator::ZeroRotator,
        FRotator::ZeroRotator, FRotator::ZeroRotator};
    const USkeletalMesh* PosedMesh = CharacterMesh ? Cast<USkeletalMesh>(CharacterMesh->GetSkinnedAsset()) : nullptr;
    for (int32 Index = 0; Index < Limbs.Num(); ++Index)
    {
        const float LegSign = Index == 0 || Index == 3 ? 1.f : -1.f;
        const bool bArm = !bBoss && Index >= 2;
        const float Swing = bBoss ? 24.f + 15.f * Sprint : bArm ? 32.f + 30.f * Sprint : 38.f + 27.f * Sprint;
        const float Foot = FMath::Sin(Gait + (bBoss && Index >= 2 ? .37f : 0.f)) * Jog;
        const float SwingLeg = FMath::Max(0.f, FMath::Cos(Gait + (Index >= 2 && bBoss ? .37f : 0.f)) * LegSign) * Jog;
        float Arc = Foot * LegSign * Swing;
        JointLocation[Index] = Limbs[Index]->GetRelativeLocation();
        if (PosedMesh)
        {
            FVector JointAt = PosedMesh->GetRefSkeleton().GetRefBonePose()[PoseBones[Index + 2]].GetTranslation();
            if (bPlayerStride && Index < 2)
            {
                const FFootCycle Step = FootCycle(Gait + (Index == 0 ? 0.f : PI));
                const float Lift = Step.Lift * Jog;
                const float Press = Step.Plant * Jog;
                JointAt += LocalMotion * (Step.Travel * Jog * (3.f + 3.f * Sprint)) +
                    FVector(0, 0, Lift * (3.f + 4.f * Sprint) - Press * (1.8f + Sprint));
                Arc = Step.Travel * Jog * (34.f + 22.f * Sprint);
            }
            JointLocation[Index] = JointAt;
        }
        const float Carry = bArm ? 9.f * Jog + 19.f * Sprint : 0.f;
        JointRotation[Index] = FRotator(Arc * LocalMotion.X - Carry,
            bArm ? LegSign * 11.f * Jog * LocalMotion.Y : 0.f, Arc * -LocalMotion.Y);
        const float Bend = bArm ? -10.f * Jog - SwingLeg * (14.f + 22.f * Sprint) :
            SwingLeg * (bBoss ? 28.f : 34.f + 18.f * Sprint);
        HingeRotation[Index] = FRotator(Bend, 0.f, !bArm ? -LocalMotion.Y * Bend * .45f : 0.f);
        if (bPlayerStride)
        {
            if (bArm)
            {
                const float Shoulder = FMath::Cos(Gait - .24f) * LegSign * Jog;
                JointRotation[Index] = FRotator(Shoulder * (35.f + 23.f * Sprint) - Jog * (10.f + 15.f * Sprint),
                    -Pose.Yaw * .72f, LegSign * Jog * (6.f + 4.f * Sprint));
                HingeRotation[Index].Pitch = -Jog * (18.f + 21.f * Sprint) -
                    FMath::Max(0.f, Shoulder) * (14.f + 12.f * Sprint);
            }
            else
            {
                const FFootCycle Step = FootCycle(Gait + (Index == 0 ? 0.f : PI));
                HingeRotation[Index].Pitch = Jog * (Step.Lift * (48.f + 20.f * Sprint) + Step.Plant * 7.f);
                JointRotation[Index].Yaw = -Pose.Yaw * .6f;
                JointRotation[Index].Roll -= LocalMotion.Y * Step.Travel * Jog * 7.f;
            }
        }
    }
    if (AppealIntro > 0.f && Move == EDuelMove::Idle)
    {
        const float Brace = FMath::Sin(PI * FMath::Clamp(AppealIntro / .82f, 0.f, 1.f));
        Pivot.Z -= 20.f * Brace;
        Pose.Pitch -= 15.f * Brace;
        HeadPose.Pitch -= 9.f * Brace;
    }

    if (!IsAlive())
    {
        if(bNorthDefeat)
        {
            if(Fall<=0.f && bPoseReady && !bContactSample)
            {
                // EndCombat resets MoveAge. Capture the actual displayed pose before using Fall as a separate visual clock.
                PreviousPivot=DisplayedPivot;PreviousBody=DisplayedBody;
                PreviousLimbs=DisplayedLimbs;PreviousHinges=DisplayedHinges;
            }
            const float Duration = bNorthBossDefeat ? DuelCourt::BossDefeatDuration : NorthDefeatDuration;
            if (!bContactSample) Fall = FMath::Min(1.f, Fall + DeltaSeconds / Duration);
            const float Age = Fall * Duration;
            DefeatPose = bNorthBossDefeat ? NorthBossDefeatPose(Character, Age) : NorthDefeatPose(Character, Age);
            Pivot=FVector(0,0,-GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight())+DefeatPose.Offset;
            Pose=DefeatPose.Body;HeadPose=DefeatPose.Head;
            JointRotation=DefeatPose.Limbs;HingeRotation=DefeatPose.Hinges;
            const float Entry=FMath::SmoothStep(0.f,1.f,Age/.14f);
            if(bPoseReady && Entry<1.f)
            {
                Pivot=FMath::Lerp(PreviousPivot,Pivot,Entry);
                Pose=FQuat::Slerp(PreviousBody.Quaternion(),Pose.Quaternion(),Entry).Rotator();
                HeadPose=FQuat::Slerp(Head->GetRelativeRotation().Quaternion(),HeadPose.Quaternion(),Entry).Rotator();
                for(int32 I=0;I<4;++I)
                {
                    JointRotation[I]=FQuat::Slerp(PreviousLimbs[I].Quaternion(),JointRotation[I].Quaternion(),Entry).Rotator();
                    HingeRotation[I]=FQuat::Slerp(PreviousHinges[I].Quaternion(),HingeRotation[I].Quaternion(),Entry).Rotator();
                }
            }
            MouthOpen=FMath::Lerp(.72f,.37f,FMath::SmoothStep(0.f,1.f,Age/.65f));
        }
        else
        {
            Fall = FMath::FInterpConstantTo(Fall, 1.f, DeltaSeconds, 2.2f);
            Pose = FRotator(0, 0, Fall * 78.f);
            Pivot += FVector(0, Fall * 37.f, Fall * 34.f);
            HeadPose.Pitch = Fall * 16.f;
        }
    }
    else if (bWitching)
    {
        // The same windup/active/recovery clock drives the existing phase-change burst.
        // These poses change only the figure, never phase damage, movement or invulnerability.
        const float Tremor = FMath::Sin(MoveAge * 31.f) * WitchGather;
        if (Character == EDuelCharacter::Ema)
        {
            Pivot.Z += -26.f * WitchGather + 43.f * WitchOpen + 16.f * WitchCrest;
            Pose = FRotator(24.f * WitchGather - 32.f * WitchOpen + 13.f * WitchRelease,
                2.f * Tremor, 1.6f * Tremor);
            HeadPose = FRotator(-15.f * WitchGather + 17.f * WitchOpen - 21.f * WitchRelease, 0.f, 0.f);
            for (int32 Index = 0; Index < 4; ++Index)
            {
                const float Side = Index % 2 ? 1.f : -1.f;
                const bool bFore = Index < 2;
                JointRotation[Index] = FRotator(bFore ? 39.f * WitchGather - 89.f * WitchOpen + 27.f * WitchRelease
                    : -31.f * WitchGather + 24.f * WitchOpen,
                    Side * (bFore ? 17.f : 8.f) * WitchOpen, Side * (bFore ? 29.f : 15.f) * WitchOpen);
                HingeRotation[Index].Pitch = (bFore ? 47.f : 38.f) * WitchGather +
                    (bFore ? 25.f : 11.f) * WitchOpen;
            }
        }
        else if (Character == EDuelCharacter::Hiro)
        {
            // Coil through the haunches while the forearms support the chest.
            // Large positive shoulder + elbow angles had curled both paws into a seated pose.
            Pivot.Z += -10.f * WitchGather - 5.f * WitchOpen + 6.f * WitchCrest;
            Pose = FRotator(8.f * WitchGather + 4.f * WitchOpen - 6.f * WitchRelease,
                2.5f * Tremor, 2.f * Tremor);
            HeadPose = FRotator(-10.f * WitchGather - 6.f * WitchOpen + 5.f * WitchRelease, 0.f, 0.f);
            for (int32 Index = 0; Index < 4; ++Index)
            {
                const float Side = Index % 2 ? 1.f : -1.f;
                const bool bFore = Index < 2;
                JointRotation[Index] = FRotator(bFore ? -20.f * WitchGather - 8.f * WitchOpen - 12.f * WitchRelease
                    : -42.f * WitchGather + 14.f * WitchOpen,
                    Side * (bFore ? 4.f : 9.f) * WitchOpen,
                    Side * (bFore ? 7.f : 12.f) * (WitchGather + WitchOpen * .5f));
                HingeRotation[Index].Pitch = (bFore ? 32.f : 58.f) * WitchGather + (bFore ? 10.f : 16.f) * WitchOpen;
            }
        }
        else
        {
            // Yuki rises as a suspended, composed figure rather than borrowing Ema's wing reveal.
            const float Float = Witch.Hover;
            Pivot.Z += -13.f * WitchGather + 108.f * Float + 8.f * WitchCrest;
            Pose = FRotator(13.f * WitchGather - 15.f * WitchOpen + 5.f * WitchRelease,
                -9.f * WitchOpen + 9.f * WitchRelease, 0.f);
            // Counter the torso's lean so the brim does not turn into the camera.
            // Keep a small upward gaze through suspension, release and the landing blend.
            HeadPose = FRotator(-Pose.Pitch + 4.f * WitchGather + 6.f * WitchOpen - 3.f * WitchRelease,
                7.f * WitchOpen - 7.f * WitchRelease, 0.f);
            for (int32 Index = 0; Index < 4; ++Index)
            {
                const float Side = Index % 2 ? 1.f : -1.f;
                const bool bFore = Index < 2;
                JointRotation[Index] = FRotator(bFore ? 31.f * WitchGather - 61.f * WitchOpen + 9.f * WitchRelease
                    : 21.f * Float, Side * 11.f * WitchOpen, Side * (bFore ? 23.f : 8.f) * WitchOpen);
                HingeRotation[Index].Pitch = (bFore ? 29.f : 32.f) * WitchGather +
                    (bFore ? 19.f : 26.f) * Float;
            }
        }
        MouthOpen += .45f * WitchGather + .98f * WitchOpen + .24f * WitchCrest;
    }
    else if (Move == EDuelMove::Roll)
    {
        const float Tuck = Progress(0.f, DuelCourt::RollMotionDelay + .025f);
        const float Somersault = Progress(DuelCourt::RollMotionDelay, DuelCourt::RollMotionTime);
        const float Plant = Tuck * (1.f - Progress(.315f, Spec.Total() - .323f));
        const float RecoverFoot = FMath::Sin(PI * Progress(.30f, Spec.Total() - .30f));
        Pose = FQuat(FVector::RightVector, 2.f * PI * Somersault).Rotator();
        const FVector Center(0, 0, CharacterMesh ? 65.f : 52.f);
        Pivot += Center - Pose.RotateVector(Center) + FVector(0, 0,
            18.f * FMath::Sin(PI * Somersault) - 12.f * Plant - 5.f * RecoverFoot);
        HeadPose.Pitch += 17.f * Plant;
        JointRotation[0] = FRotator(52.f * Plant - 31.f * RecoverFoot, 0, -7.f * Plant);
        JointRotation[1] = FRotator(40.f * Plant + 12.f * RecoverFoot, 0, 9.f * Plant);
        JointRotation[2] = FRotator(-39.f * Plant, 0, 13.f * Plant);
        JointRotation[3] = FRotator(-53.f * Plant, 0, -11.f * Plant);
        HingeRotation[0].Pitch = 58.f * Plant + 25.f * RecoverFoot;
        HingeRotation[1].Pitch = 68.f * Plant + 13.f * RecoverFoot;
        HingeRotation[2].Pitch = -34.f * Plant;
        HingeRotation[3].Pitch = -46.f * Plant;
    }
    else if (Move == EDuelMove::Backstep)
    {
        const float Hop = FMath::Sin(PI * FMath::Clamp(MoveAge / .29f, 0.f, 1.f));
        Pivot.Z += 22.f * Hop;
        Pose.Pitch = 15.f * Hop;
        JointRotation[0] = FRotator(-21.f * Hop, 0, 0);
        JointRotation[1] = FRotator(25.f * Hop, 0, 0);
        JointRotation[2] = FRotator(18.f * Hop, 0, -13.f * Hop);
        JointRotation[3] = FRotator(-16.f * Hop, 0, 11.f * Hop);
        HingeRotation[0].Pitch = 23.f * Hop;
        HingeRotation[1].Pitch = 41.f * Hop;
    }
    else if (Move == EDuelMove::Leap || Move == EDuelMove::FlankLeap || Move == EDuelMove::Wingfall)
    {
        const bool bFlank = Move == EDuelMove::FlankLeap;
        const bool bWingfall = Move == EDuelMove::Wingfall;
        const float Crouch = Wind * (1.f - Strike);
        const float Landing = Progress(Spec.Windup + Spec.Active * .53f, Spec.Active * .42f) * (1.f - Recover);
        const float Air = FMath::Sin(PI * Strike);
        Pivot.Z += Air * (bWingfall ? 475.f : IsAwakened() ? 289.f : 221.f) - Crouch * 46.f - Landing * 33.f + (bFlank ? 78.f * Air : 0.f);
        Pose.Pitch = FMath::Sin(2.f * PI * Strike) * 44.f - Crouch * 19.f + Landing * 32.f;
        Pose.Roll = bFlank ? FMath::Sin(2.f * PI * Strike) * 28.f : Pose.Roll;
        if (bWingfall)
        {
            Pose.Pitch += -21.f * Crouch - 37.f * Air + 48.f * Landing;
            HeadPose.Pitch += 19.f * Air - 12.f * Landing;
        }
        for (int32 Index = 0; Index < 4; ++Index)
        {
            const bool bFront = Index < 2;
            JointRotation[Index] = FRotator(
                (bFront ? -66.f : 35.f) * Air - Crouch * 29.f + (bFront ? 41.f : 12.f) * Landing,
                0, (Index % 2 ? 1.f : -1.f) * (17.f * Air + 7.f * Landing));
            HingeRotation[Index].Pitch = (bFront ? 38.f : 60.f) * Air - Landing * 12.f;
        }
        MouthOpen = .72f + Commitment * .64f;
    }
    else if (Move == EDuelMove::Reversal)
    {
        const float StepEnd = Spec.Windup + DuelCourt::ReversalStepTime;
        const float Sidestep = FMath::Sin(PI * Progress(Spec.Windup, DuelCourt::ReversalStepTime));
        const float JabAt = StepEnd + DuelCourt::ReversalCueTime;
        const float Brace = Progress(StepEnd, .18f) * (1.f - Progress(JabAt - .10f, .13f));
        const float Jab = Progress(JabAt - .13f, .16f) * (1.f - Recover);
        Pivot.Z += 43.f * Sidestep - 23.f * Wind - 13.f * Brace;
        Pose.Roll = OrbitSide * (-13.f * Wind + 31.f * Sidestep - 10.f * Jab);
        Pose.Pitch = 14.f * Wind - 29.f * Brace + 43.f * Jab;
        JointRotation[0] = FRotator(36.f * Wind - 51.f * Sidestep - 102.f * Jab, 17.f * Jab, -19.f * Sidestep);
        JointRotation[1] = FRotator(42.f * Wind - 62.f * Sidestep + 22.f * Jab, 0, 24.f * Sidestep);
        JointRotation[2] = FRotator(65.f * Sidestep + 38.f * Jab, 0, 17.f * Sidestep);
        JointRotation[3] = FRotator(-59.f * Sidestep + 22.f * Jab, 0, -16.f * Sidestep);
        HeadPose.Yaw = OrbitSide * 26.f * Sidestep;
        MouthOpen += .86f * Jab;
    }
    else if (bBoss && DuelBossDefense::IsCloseChain(Move))
    {
        using namespace DuelBossDefense;
        for (const FCloseHit& Hit : CloseHits(Move))
        {
            if (MoveAge < Hit.At - CloseWarningTime || MoveAge > Hit.At + CloseLastHitHold) continue;
            const float Draw = Progress(Hit.At - CloseWarningTime, .23f) * (1.f - Progress(Hit.At - .10f, .11f));
            const float StrikeBeat = Progress(Hit.At - .10f, .12f) * (1.f - Progress(Hit.At + .02f, CloseLastHitHold - .02f));
            const float Side = (Hit.AimBias < 0.f ? -1.f : 1.f) * OrbitSide;
            const int32 Lead = Side < 0.f ? 2 : 3;
            switch (Hit.Style)
            {
            case ECloseStyle::BoneRake:
                Pose.Yaw += Side * (-32.f * Draw + 55.f * StrikeBeat);
                Pose.Roll += Side * (16.f * Draw - 20.f * StrikeBeat);
                Pivot.Z += 25.f * Draw - 9.f * StrikeBeat;
                JointRotation[Lead] = FRotator(-82.f * Draw + 58.f * StrikeBeat, Side * 62.f * StrikeBeat, Side * 21.f * Draw);
                HingeRotation[Lead].Pitch = -36.f * Draw + 23.f * StrikeBeat;
                CloseWing = Side * (-45.f * Draw + 78.f * StrikeBeat);
                break;
            case ECloseStyle::BoneSlam:
                Pose.Pitch += -37.f * Draw + 50.f * StrikeBeat;
                Pivot.Z += 46.f * Draw - 21.f * StrikeBeat;
                for (int32 Arm : {2, 3})
                {
                    JointRotation[Arm].Pitch = -102.f * Draw + 82.f * StrikeBeat;
                    HingeRotation[Arm].Pitch = -34.f * Draw;
                }
                CloseWing = 68.f * Draw - 79.f * StrikeBeat;
                break;
            case ECloseStyle::HoundRake:
                Pivot.Z -= 25.f * Draw + 12.f * StrikeBeat;
                Pose.Roll += Side * (-18.f * Draw + 30.f * StrikeBeat);
                Pose.Yaw += Side * (-33.f * Draw + 72.f * StrikeBeat);
                JointRotation[Lead] = FRotator(-54.f * Draw + 44.f * StrikeBeat, Side * 83.f * StrikeBeat, 0);
                HingeRotation[Lead].Pitch -= 27.f * Draw;
                break;
            case ECloseStyle::HoundLunge:
                Pivot.Z += 34.f * StrikeBeat - 24.f * Draw;
                Pose.Pitch += 23.f * Draw - 40.f * StrikeBeat;
                for (int32 Arm : {2, 3}) JointRotation[Arm].Pitch -= 76.f * StrikeBeat;
                HingeRotation[0].Pitch -= 49.f * Draw;
                HingeRotation[1].Pitch -= 49.f * Draw;
                break;
            case ECloseStyle::HoundTail:
                Pivot.Z -= 22.f * (Draw + StrikeBeat);
                Pose.Yaw += Side * (-44.f * Draw + 104.f * StrikeBeat);
                Pose.Roll += 19.f * Side * StrikeBeat;
                CloseTail = Side * (-90.f * Draw + 128.f * StrikeBeat);
                break;
            case ECloseStyle::IceThrust:
                Pose.Pitch += -20.f * Draw + 39.f * StrikeBeat;
                HeadPose.Pitch -= 14.f * StrikeBeat;
                JointRotation[3] = FRotator(-37.f * Draw - 91.f * StrikeBeat, 0, 0);
                HingeRotation[3].Pitch = -43.f * Draw;
                break;
            case ECloseStyle::IceHook:
                Pose.Yaw += Side * (-46.f * Draw + 58.f * StrikeBeat);
                Pose.Roll += Side * 27.f * Draw;
                JointRotation[3] = FRotator(-74.f * Draw + 41.f * StrikeBeat, 67.f * Side * StrikeBeat, 0);
                break;
            case ECloseStyle::IceReap:
                Pose.Yaw += Side * (-48.f * Draw + 85.f * StrikeBeat);
                Pose.Roll -= Side * 33.f * StrikeBeat;
                Pivot.Z += 17.f * Draw;
                JointRotation[2] = FRotator(-80.f * Draw + 42.f * StrikeBeat, -70.f * Side * Draw, 0);
                JointRotation[3] = FRotator(-89.f * Draw + 47.f * StrikeBeat, 81.f * Side * StrikeBeat, 0);
                break;
            }
            MouthOpen += .58f * StrikeBeat;
            break;
        }
    }
    else if (Move == EDuelMove::Paw || Move == EDuelMove::RakingClaws)
    {
        const float Raised = Wind * (1.f - Strike);
        const float Follow = Strike * (1.f - Recover);
        Pose.Roll += -11.f * Raised + 15.f * Follow;
        Pose.Pitch += -8.f * Raised + 19.f * Follow;
        Pivot.Z -= 11.f * Follow;
        JointRotation[1] = FRotator((CharacterMesh ? -101.f : -108.f) * Raised + 61.f * Follow,
            -39.f * Raised + 38.f * Follow, -19.f * Raised);
        HingeRotation[1].Pitch = 36.f * Raised - 21.f * Follow;
        HingeRotation[2].Pitch = 14.f * Raised + 19.f * Follow;
        HingeRotation[3].Pitch = 18.f * Raised + 23.f * Follow;
        HeadPose.Roll -= 12.f * Commitment;
        MouthOpen = .72f + .88f * Commitment;
        if (Move == EDuelMove::RakingClaws)
        {
            const float Second = Progress(Spec.Windup + DuelBossDefense::RakingBeat - .22f, .29f) * (1.f - Recover);
            Pose.Yaw = -28.f * Raised + 48.f * Follow - 68.f * Second;
            Pose.Roll -= 24.f * Second;
            JointRotation[0] = FRotator(-107.f * Follow + 142.f * Second, 26.f * Second, -27.f * Follow);
            HingeRotation[0].Pitch = 47.f * Follow - 38.f * Second;
            Pivot.Z += 21.f * FMath::Sin(PI * Strike) - 17.f * Second;
        }
    }
    else if (Move == EDuelMove::FeintCounter)
    {
        const float Bait = Progress(.06f, .23f) * (1.f - Strike);
        const float Snap = Strike * (1.f - Recover);
        Pivot.Z += 36.f * Bait - 27.f * Snap;
        Pose.Roll = -26.f * Bait + 22.f * Snap;
        Pose.Pitch = -15.f * Bait + 41.f * Snap;
        Pose.Yaw = OrbitSide * (26.f * Bait - 42.f * Snap);
        JointRotation[0] = FRotator(-115.f * Bait + 81.f * Snap, -28.f * Bait, -18.f * Bait);
        JointRotation[1] = FRotator(35.f * Bait - 28.f * Snap, 26.f * Snap, 21.f * Snap);
        HeadPose.Pitch -= 17.f * Bait;
        MouthOpen += .98f * Snap;
    }
    else if (Move == EDuelMove::WingSweep)
    {
        const float ContactAt = Spec.Windup + Spec.Active * .61f;
        const float Load = Progress(.07f, Spec.Windup * .72f) * (1.f - Progress(Spec.Windup, Spec.Active * .65f));
        const float Follow = Progress(Spec.Windup, Spec.Active * .61f) * (1.f - Recover);
        const float Land = Beat(ContactAt, .09f, .24f);
        Pivot += FVector(18.f * Follow, -22.f * Load + 29.f * Follow, -14.f * Load - 23.f * Land);
        Pose.Yaw += -63.f * Load + 68.f * Follow;
        Pose.Roll += 31.f * Load - 36.f * Follow;
        HeadPose.Yaw = -11.f * Load - 19.f * Follow;
        JointRotation[0] = FRotator(-56.f * Load + 21.f * Land, -19.f * Load, -14.f * Load);
        JointRotation[1] = FRotator(39.f * Follow, 24.f * Follow, 15.f * Follow);
        HingeRotation[1].Pitch += 29.f * Land;
        CloseWing = -17.f * Load + 22.f * Beat(ContactAt + .055f, .12f, .29f);
        MouthOpen += Commitment;
    }
    else if (Move == EDuelMove::ShadowSweep)
    {
        const float Coil = Wind * (1.f - Strike);
        const float Unwind = Strike * (1.f - Recover);
        Pivot.Z -= 33.f * Coil + 19.f * Unwind;
        Pose.Yaw = OrbitSide * (-49.f * Coil + 59.f * Unwind);
        Pose.Roll = -19.f * Coil + 32.f * Unwind;
        Pose.Pitch += 21.f * Coil - 13.f * Unwind;
        JointRotation[0] = FRotator(49.f * Coil - 71.f * Unwind, -28.f * Unwind, -19.f * Coil);
        JointRotation[1] = FRotator(28.f * Coil - 33.f * Unwind, 0, 13.f);
        MouthOpen += .74f * Commitment;
    }
    else if (Move == EDuelMove::CrossAmbush)
    {
        const float Slip = FMath::Sin(PI * Progress(Spec.Windup, DuelBossDefense::AmbushLunge)) +
            FMath::Sin(PI * Progress(Spec.Windup + DuelBossDefense::AmbushSecondStart, DuelBossDefense::AmbushLunge));
        const float Load = Wind * (1.f - Strike);
        Pivot.Z += 83.f * Slip - 44.f * Load;
        Pose.Pitch = 37.f * Slip - 18.f * Load;
        Pose.Roll = OrbitSide * (36.f * Slip - 22.f * Load);
        JointRotation[0] = FRotator(-121.f * Slip + 42.f * Load, 13.f * Slip, -32.f * Slip);
        JointRotation[1] = FRotator(-92.f * Slip + 38.f * Load, -17.f * Slip, 29.f * Slip);
        JointRotation[2] = FRotator(68.f * Slip - 47.f * Load, 0, 0);
        JointRotation[3] = FRotator(52.f * Slip - 43.f * Load, 0, 0);
        MouthOpen += .95f * Slip;
    }
    else if (Move == EDuelMove::TalonRush)
    {
        const float Coil = Wind * (1.f - Strike);
        const float Pounce = FMath::Sin(PI * Strike);
        const float Brake = Strike * (1.f - Recover);
        Pivot.Z += 59.f * Pounce - 37.f * Coil - 21.f * Brake;
        Pose.Pitch += -14.f * Coil + 32.f * Pounce + 17.f * Brake;
        Pose.Roll = 16.f * Pounce - 9.f * Brake;
        HeadPose.Pitch = -17.f * Pounce;
        JointRotation[0] = FRotator(49.f * Coil - 112.f * Pounce, 0, -25.f * Pounce);
        JointRotation[1] = FRotator(51.f * Coil - 88.f * Pounce, 0, 29.f * Pounce);
        JointRotation[2] = FRotator(-51.f * Coil + 56.f * Pounce, 0, -13.f * Brake);
        JointRotation[3] = FRotator(-49.f * Coil + 48.f * Pounce, 0, 9.f * Brake);
        HingeRotation[0].Pitch = 18.f * Coil - 33.f * Pounce + 39.f * Brake;
        HingeRotation[1].Pitch = 23.f * Coil - 29.f * Pounce + 44.f * Brake;
        HingeRotation[2].Pitch = 39.f * Coil + 49.f * Pounce;
        HingeRotation[3].Pitch = 47.f * Coil + 39.f * Pounce;
        MouthOpen += .85f * Pounce;
    }
    else if (Move == EDuelMove::ShadowSnare)
    {
        const float Lay = FMath::Sin(PI * Progress(0.f, Spec.Windup));
        const float Draw = FMath::Sin(PI * Progress(Spec.Windup, .59f));
        Pivot.Z -= 36.f * Lay + 18.f * Draw;
        Pose.Pitch = 34.f * Lay - 18.f * Draw;
        Pose.Roll = OrbitSide * (18.f * Lay - 31.f * Draw);
        HeadPose.Pitch = -21.f * Lay;
        JointRotation[0] = FRotator(-96.f * Lay + 34.f * Draw, -33.f * Lay, -23.f * Lay);
        JointRotation[1] = FRotator(-67.f * Draw, 28.f * Draw, 25.f * Draw);
        MouthOpen += .64f * Draw;
    }
    else if (DuelBossDefense::IsSignature(Move))
    {
        using namespace DuelBossDefense;
        const float Ready = Wind * (1.f - Progress(Spec.Windup, .14f));
        const float First = Beat(Spec.Windup, .13f, .24f);
        const float Settle = 1.f - Recover;
        if (Move == EDuelMove::OssuaryShears)
        {
            Pivot.Z += 48.f * Ready - 24.f * First;
            Pose.Pitch = -27.f * Ready + 40.f * First;
            JointRotation[2] = FRotator(-99.f * Ready + 83.f * First, -43.f * Ready + 60.f * First, -20.f * Ready);
            JointRotation[3] = FRotator(-99.f * Ready + 83.f * First, 43.f * Ready - 60.f * First, 20.f * Ready);
            HingeRotation[2].Pitch -= 47.f * Ready;
            HingeRotation[3].Pitch -= 47.f * Ready;
        }
        else if (Move == EDuelMove::HoundShade)
        {
            const float JumpAt = Spec.Windup + ShadeBodyStart;
            const float Crouch = Beat(JumpAt - .065f, ShadeBodyAimLead - .065f, .16f);
            const float Pounce = FMath::Sin(PI * Progress(JumpAt, ShadeBodyFlight));
            Pivot.Z += 76.f * Pounce - 34.f * Crouch;
            Pose.Pitch = 29.f * Crouch - 43.f * Pounce;
            Pose.Roll = OrbitSide * (32.f * First - 18.f * Pounce);
            JointRotation[0] = FRotator(-105.f * Pounce + 36.f * Crouch, 18.f * First, -32.f * Pounce);
            JointRotation[1] = FRotator(-103.f * Pounce + 35.f * Crouch, -20.f * First, 32.f * Pounce);
            JointRotation[2].Pitch = 64.f * Pounce - 42.f * Crouch;
            JointRotation[3].Pitch = 54.f * Pounce - 47.f * Crouch;
            CloseTail = OrbitSide * (-38.f * Ready + 74.f * First - 42.f * Pounce);
            MouthOpen += .97f * Pounce;
        }
        else if (Move == EDuelMove::MirrorRicochet)
        {
            const float ReturnAt = Spec.Windup + MirrorReturnStart;
            const float Turn = Beat(ReturnAt, MirrorAimLead, .38f);
            Pivot.Z += 65.f * (Ready + Turn) - 28.f * First;
            Pose.Yaw = OrbitSide * (-34.f * Ready + 62.f * Turn);
            Pose.Roll = OrbitSide * (-20.f * First + 26.f * Turn);
            Pose.Pitch = -25.f * Ready + 36.f * First - 24.f * Turn;
            JointRotation[0] = FRotator(-108.f * Ready + 60.f * First, -25.f * Ready, -27.f * First);
            JointRotation[1] = FRotator(-103.f * Turn, 44.f * OrbitSide * Turn, 21.f * Turn);
        }
        else
        {
            const float Cover = Progress(.07f, RiposteGuardOpens - .07f) * (1.f - Progress(RiposteGuardCloses, .17f));
            const float Step = Progress(Spec.Windup - .08f, .24f) * Settle;
            Pivot.Z -= 23.f * Cover + 17.f * Step;
            Pose.Pitch = -16.f * Cover + 41.f * Step;
            Pose.Roll = OrbitSide * (21.f * Cover - 20.f * Step);
            JointRotation[0] = FRotator(-74.f * Cover - 57.f * Step, -38.f * Cover, -21.f * Step);
            JointRotation[1] = FRotator(-71.f * Cover + 31.f * Step, 39.f * Cover, 28.f * Cover);
            HingeRotation[0].Pitch -= 57.f * Cover - 38.f * Step;
            HingeRotation[1].Pitch -= 46.f * Cover;
        }
    }
    else if (DuelCourt::IsYukiCast(Move))
    {
        const float Raise = Wind * (1.f - Strike);
        const float Cast = FMath::Sin(PI * Strike);
        const float Side = (Move == EDuelMove::SilentBell ? -1.f : 1.f) * OrbitSide;
        Pivot.Z += 57.f * Raise + 18.f * Cast;
        Pose.Roll = Side * (24.f * Raise - 35.f * Cast);
        Pose.Pitch = -31.f * Raise + 35.f * Cast;
        HeadPose.Yaw = Side * 18.f * Cast;
        JointRotation[0] = FRotator(-104.f * Raise + 63.f * Cast, -27.f * Raise, -22.f * Cast);
        JointRotation[1] = FRotator(-64.f * Raise + 41.f * Cast, 24.f * Cast, 29.f * Raise);
        MouthOpen += .75f * Cast;
        if (Move == EDuelMove::GlassShelter)
        {
            const float Open = Progress(Spec.Windup * .30f, Spec.Windup * .51f) * (1.f - Recover);
            const float Crack = Beat(Spec.Windup, .115f, .30f);
            Pivot.Z += -38.f * Raise - 15.f * Crack;
            Pose.Pitch = -10.f * Open + 24.f * Crack;
            Pose.Roll = 8.f * Side * Raise;
            HeadPose.Pitch = 11.f * Raise - 15.f * Crack;
            JointRotation[0] = FRotator(-71.f * Raise + 24.f * Crack, -46.f * Open, -21.f * Open);
            JointRotation[1] = FRotator(-58.f * Raise + 20.f * Crack, 46.f * Open, 21.f * Open);
            HingeRotation[0].Pitch = -34.f * Raise;
            HingeRotation[1].Pitch = -34.f * Raise;
        }
        else if (Move == EDuelMove::VerdictTether)
        {
            Pivot.Z -= 34.f * Cast;
            Pose.Pitch += 23.f * Cast;
            JointRotation[1] = FRotator(42.f * Raise - 97.f * Cast, 0, Side * 21.f);
        }
        else if (Move == EDuelMove::MirrorEdict)
        {
            const float Close = FMath::Sin(PI * Strike);
            Pose.Yaw += Side * 54.f * Close;
            JointRotation[0] = FRotator(-44.f * Raise - 83.f * Close, Side * 48.f * Close, -25.f * Close);
        }
        else if (Move == EDuelMove::IcicleRow)
        {
            float Flick = 0.f;
            for (const FMapWave& Wave : MapWaves)
                Flick = FMath::Max(Flick, FMath::Sin(PI * Progress(Wave.ImpactTime - .06f, .17f)));
            Pose.Roll += OrbitSide * (19.f * Raise - 16.f * Flick);
            JointRotation[0].Yaw -= OrbitSide * 35.f * Raise;
            JointRotation[1].Pitch -= 40.f * Flick;
            HingeRotation[1].Pitch += 22.f * Flick;
        }
        else if (Move == EDuelMove::PrismPursuit)
        {
            Pivot.Z += 43.f * Commitment;
            Pose.Roll += OrbitSide * (30.f * Cast - 19.f * Raise);
            Pose.Yaw += OrbitSide * 42.f * Cast;
            JointRotation[1].Pitch -= 41.f * Cast;
            HingeRotation[0].Pitch = 43.f * Cast;
        }
        else if (Move == EDuelMove::ArchiveCollapse)
        {
            Pose.Pitch += 39.f * Cast;
            Pivot.Z += 67.f * Raise - 24.f * Cast;
            JointRotation[0].Pitch -= 58.f * Cast;
            JointRotation[1].Pitch -= 58.f * Cast;
        }
    }
    else if (Move == EDuelMove::AtlasRain || Move == EDuelMove::NightLanes || Move == EDuelMove::Pursuit ||
        Move == EDuelMove::WingVerdict || Move == EDuelMove::Tribunal || Move == EDuelMove::ShadowVolley)
    {
        const bool bAtlas = Move == EDuelMove::AtlasRain || Move == EDuelMove::Tribunal;
        float Rebound = 0.f;
        for (const FMapWave& Wave : MapWaves)
        {
            if (Move != EDuelMove::ShadowVolley || !Wave.Marks.IsEmpty())
            {
                Rebound = FMath::Max(Rebound, 1.f - FMath::Abs(MoveAge - Wave.ImpactTime) / .23f);
            }
        }
        const float Poise = Wind * (1.f - Recover);
        Pivot.Z += (bAtlas ? 60.f : 32.f) * Poise - 40.f * Rebound;
        Pose.Pitch = (bAtlas ? -38.f : -24.f) * Poise + 45.f * Rebound;
        Pose.Yaw = bAtlas ? 6.f * Poise : FMath::Sin(MoveAge * 4.8f) * 25.f * Poise;
        if (Move == EDuelMove::Pursuit)
        {
            const float Side = FVector::DotProduct(GetActorRightVector(), ActionDirection) > 0 ? 1.f : -1.f;
            const float Coil = Progress(.04f, Spec.Windup * .71f) * (1.f - Progress(Spec.Windup, .19f));
            const float Bound = Beat(Spec.Windup + .19f, .17f, .31f);
            Pose.Roll = Side * (-26.f * Coil + 28.f * Bound - 14.f * Rebound);
            Pose.Yaw = Side * (-28.f * Coil + 48.f * Bound - 17.f * Rebound);
            Pose.Pitch = 26.f * Coil - 38.f * Bound + 20.f * Rebound;
            Pivot.Z += -58.f * Coil + 43.f * Bound;
            CloseTail = Side * (-52.f * Coil + 90.f * Bound - 34.f * Rebound);
            JointRotation[2] = FRotator(-41.f * Coil + 28.f * Bound, 0.f, -Side * 19.f * Bound);
            JointRotation[3] = FRotator(45.f * Coil - 48.f * Bound, 0.f, Side * 18.f * Bound);
            HingeRotation[2].Pitch = 38.f * Coil + 23.f * Rebound;
            HingeRotation[3].Pitch = 52.f * Coil + 18.f * Bound;
            if (Character == EDuelCharacter::Ema)
            {
                const float Dash = Beat(Spec.Windup + .23f, .23f, .28f);
                Pivot.Z = -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() + 24.f * Dash - 14.f * Coil;
                Pose.Pitch = -19.f * Dash + 13.f * Coil;
                Pose.Roll = Side * (12.f * Dash - 10.f * Coil);
                CloseWing = Side * (42.f * Dash - 18.f * Coil);
            }
        }
        if (Move == EDuelMove::WingVerdict)
        {
            const float Turn = OrbitSide * FMath::Sin(MoveAge * 5.2f) * Poise;
            Pose.Roll += Turn * 23.f;
            Pose.Yaw += Turn * 37.f;
        }
        else if (Move == EDuelMove::ShadowVolley)
        {
            const float Turn = FMath::Sin((MoveAge - Spec.Windup) * 6.83f) * Poise;
            Pivot.Z -= 42.f * Poise;
            Pose.Yaw += Turn * 44.f;
            Pose.Pitch += 30.f * Poise - 14.f * Rebound;
            Pose.Roll += Turn * 23.f;
        }
        HeadPose.Pitch = 11.f * Poise - 22.f * Rebound;
        JointRotation[0] = FRotator(-78.f * Poise + 41.f * Rebound, -17.f * Poise, -15.f);
        JointRotation[1] = FRotator(-89.f * Poise + 53.f * Rebound, 17.f * Poise, 15.f);
        if (Move == EDuelMove::Pursuit)
        {
            const float Reach = Beat(Spec.Windup + .20f, .18f, .26f);
            JointRotation[0] = FRotator(-59.f * Poise - 45.f * Reach, -23.f * Reach, -16.f * Reach);
            JointRotation[1] = FRotator(-17.f * Poise + 21.f * Rebound, 22.f * Poise, 16.f * Poise);
        }
        MouthOpen += .63f * Poise + .56f * Rebound;
    }
    else if (Move == EDuelMove::InwardReap)
    {
        const float Open = Wind * (1.f - Strike);
        const float DrawIn = Strike * (1.f - Recover);
        Pivot.Z += 90.f * Open - 20.f * DrawIn;
        Pose.Pitch = -50.f * Open + 39.f * DrawIn;
        Pose.Roll = 14.f * Open - 22.f * DrawIn;
        JointRotation[0] = FRotator(-107.f * Open + 20.f * DrawIn, 0, -31.f * Open);
        JointRotation[1] = FRotator(-83.f * Open + 37.f * DrawIn, 0, 26.f * Open);
        MouthOpen += .82f * Commitment;
    }
    else if (Move == EDuelMove::Pillar || Move == EDuelMove::Storm || Move == EDuelMove::HaloBreak ||
        Move == EDuelMove::BoneProcession || Move == EDuelMove::WrittenVerdict ||
        Move == EDuelMove::Seals || Move == EDuelMove::Cross)
    {
        const float Raise = Wind * (1.f - Strike);
        const float Slam = Strike * (1.f - Recover);
        Pose.Pitch = -42.f * Raise + 23.f * Slam;
        Pivot.Z += 46.f * Raise - 18.f * Slam;
        HeadPose.Pitch = 14.f * Raise - 19.f * Slam;
        JointRotation[0] = FRotator(-77.f * Raise + 25.f * Slam, 0, -15.f * Raise);
        JointRotation[1] = FRotator(-77.f * Raise + 25.f * Slam, 0, 15.f * Raise);
        MouthOpen = .72f + 1.18f * Commitment;
        if (Move == EDuelMove::HaloBreak)
        {
            const float Second = Progress(Spec.Windup + .48f, .20f) * (1.f - Recover);
            Pivot.Z += 59.f * Raise - 34.f * Second;
            Pose.Pitch += 28.f * Second;
            HeadPose.Pitch -= 17.f * Second;
        }
        if (Move == EDuelMove::BoneProcession)
        {
            const float Plant = FMath::Sin(PI * Progress(Spec.Windup - .18f, .37f));
            Pivot.Z -= 34.f * Plant;
            Pose.Pitch += 33.f * Plant;
            JointRotation[0] = FRotator(-74.f * Raise + 60.f * Plant, -16.f * Raise, -24.f * Raise);
            JointRotation[1] = FRotator(-96.f * Raise + 57.f * Plant, 21.f * Raise, 24.f * Raise);
        }
        if (Move == EDuelMove::WrittenVerdict)
        {
            const float FirstInk = Beat(Spec.Windup, .24f, .26f);
            const float SecondInk = Beat(Spec.Windup + .76f, .26f, .29f);
            const float Reload = Beat(Spec.Windup + .49f, .24f, .19f);
            Pose.Yaw = OrbitSide * (-46.f * Raise + 38.f * FirstInk - 42.f * SecondInk);
            Pose.Roll = OrbitSide * (19.f * Raise - 14.f * FirstInk + 34.f * SecondInk);
            Pose.Pitch += 22.f * FirstInk + 30.f * SecondInk;
            Pivot.Z -= 18.f * FirstInk + 21.f * SecondInk;
            HeadPose.Yaw -= 24.f * OrbitSide * Raise;
            HeadPose.Pitch += 20.f * Raise;
            JointRotation[0] = FRotator(-35.f * Raise - 24.f * Reload + 19.f * SecondInk, 0, -24.f * Raise);
            JointRotation[1] = FRotator(-127.f * Raise + 68.f * FirstInk - 52.f * Reload + 37.f * SecondInk,
                -55.f * Raise + 92.f * FirstInk - 74.f * SecondInk, 42.f * SecondInk);
            HingeRotation[1].Pitch = -36.f * Raise + 29.f * FirstInk - 18.f * Reload;
            CloseWing = OrbitSide * (-28.f * Raise + 49.f * FirstInk - 55.f * SecondInk);
        }
        if (Move == EDuelMove::Seals || Move == EDuelMove::Cross)
        {
            const float Casting = FMath::Sin(PI * 3.f * Strike) * (1.f - Recover);
            JointRotation[0] = FRotator(-67.f * Raise + 35.f * Casting, -12.f, -10.f);
            JointRotation[1] = FRotator(-67.f * Raise - 35.f * Casting, 12.f, 10.f);
            HeadPose.Roll = Casting * 13.f;
        }
    }
    else if (Move == EDuelMove::GuardReel)
    {
        const float Reel = FMath::SmoothStep(0.f, 1.f, MoveAge / .09f) * (1.f - Recover);
        Pivot.Z -= 10.f * Reel;
        Pose.Pitch = -21.f * Reel;
        Pose.Roll = 12.f * Reel;
        HeadPose.Pitch = -22.f * Reel;
        JointRotation[0] = FRotator(-36.f * Reel, -19.f * Reel, 0.f);
        JointRotation[1] = FRotator(-22.f * Reel, 21.f * Reel, 0.f);
    }
    else if (Move == EDuelMove::Stagger)
    {
        const float Drop = FMath::SmoothStep(0.f, 1.f, MoveAge / .22f) * (1.f - Recover);
        Pivot.Z -= 32.f * Drop;
        Pose.Pitch = 20.f * Drop;
        Pose.Roll = 14.f * Drop;
        HeadPose.Pitch = 18.f * Drop;
        JointRotation[0] = FRotator(-35.f * Drop, 0, -12.f);
        JointRotation[1] = FRotator(-24.f * Drop, 0, 10.f);
    }
    else if (Move == EDuelMove::Hurt || Move == EDuelMove::GuardBreak)
    {
        const float Stumble = FMath::Sin(PI * FMath::Clamp(MoveAge / Spec.Total(), 0.f, 1.f));
        const FVector LocalHit = GetActorTransform().InverseTransformVectorNoScale(HitDirection);
        Pose.Pitch = LocalHit.X * Stumble * 29.f;
        Pose.Roll = LocalHit.Y * Stumble * 31.f;
        Pivot.Z -= Stumble * 14.f;
        HeadPose.Pitch -= 19.f * Stumble;
    }
    else if (!bBoss && Move == EDuelMove::Idle && GetCharacterMovement()->IsFalling())
    {
        const float Rise = FMath::Clamp(static_cast<float>(GetVelocity().Z / 392.f), -1.f, 1.f);
        Pose.Pitch = -11.f + 12.f * Rise;
        Pose.Roll = -6.f;
        HeadPose.Pitch = 9.f * Rise;
        JointRotation[0] = FRotator(-27.f - 28.f * Rise, 0, -5.f);
        JointRotation[1] = FRotator(21.f - 24.f * Rise, 0, 7.f);
        JointRotation[2] = FRotator(-29.f, -15.f, -19.f);
        JointRotation[3] = FRotator(-17.f, 9.f, 16.f);
        HingeRotation[0].Pitch = 28.f + 21.f * Rise;
        HingeRotation[1].Pitch = 37.f - 18.f * Rise;
    }
    else if (!bBoss && Move == EDuelMove::Idle && LandingPose > 0.f)
    {
        const float Brace = FMath::Sin(PI * LandingPose / .20f);
        Pivot.Z -= Brace * 13.f;
        Pose.Pitch += Brace * 17.f;
        JointRotation[0] = FRotator(-Brace * 23.f, 0, 0);
        JointRotation[1] = FRotator(Brace * 12.f, 0, 0);
    }

    USceneComponent* ActiveGrip = nullptr;
    FRotator AttackAngle = FRotator::ZeroRotator;
    if (Weapon && LeftWeapon)
    {
        const bool bSpear = MoveWeapon == EDuelWeapon::Spear;
        const bool bHoundFirst = Move == EDuelMove::Crimson && MoveArt == EDuelArt::HoundFinesse;
        const bool bGreat = MoveWeapon == EDuelWeapon::Greatblade || MoveWeapon == EDuelWeapon::Hammer || MoveWeapon == EDuelWeapon::Houndfang;
        const bool bShadowStep = Move == EDuelMove::Crimson && MoveArt == EDuelArt::HiroStep;
        const bool bRapierThrust = MoveWeapon == EDuelWeapon::Rapier && Move == EDuelMove::Heavy;
        const bool bThrust = bRapierThrust ||
            (Move == EDuelMove::Crimson && (MoveArt == EDuelArt::Thrust || MoveArt == EDuelArt::HoundChase || bShadowStep));
        const bool bWhirl = Move == EDuelMove::Crimson && MoveArt == EDuelArt::Whirl;
        const float Load = Wind * (1.f - Strike);
        const float Follow = Strike * (1.f - Recover);
        constexpr float GuardShoulderPitch = 63.f;
        constexpr float GuardElbowPitch = 66.f;
        FRotator Sword(bSpear ? 2.f : (bGreat ? -11.f : -12.f), bSpear ? 15.f : 23.f, 0);
        if (bThrust || (bSpear && (Move == EDuelMove::Cut || Move == EDuelMove::Heavy)))
        {
            if (bThrust)
            {
                Sword = FRotator(3, 11, 0);
            }
            Sword.Pitch += 13.f * Load - 6.f * Follow;
            Sword.Yaw -= 15.f * Commitment;
            Pose.Pitch = -11.f * Load + 19.f * Follow;
            Pivot.Z -= 10.f * Commitment;
            JointRotation[3] = FRotator(25.f * Load - 82.f * Follow, -12.f * Load, -14.f * Commitment);
            JointRotation[2] = FRotator(-45.f * Load + 27.f * Follow, 33.f * Commitment, 13.f * Follow);
            JointRotation[0] = FRotator(34.f * Follow - 19.f * Load, 0, -8.f * Follow);
            JointRotation[1] = FRotator(-49.f * Follow + 17.f * Load, 0, 9.f * Follow);
            HingeRotation[3].Pitch = -24.f * Load + 15.f * Follow;
            HingeRotation[2].Pitch = -34.f * Commitment;
            HingeRotation[1].Pitch = 29.f * Follow;
            if (bRapierThrust)
            {
                Sword.Yaw -= 19.f * Load;
                Pose.Yaw -= 17.f * Load;
                JointRotation[3].Yaw += 21.f * Follow;
            }
            if (bShadowStep)
            {
                Pivot.Z -= 21.f * Commitment;
                Pose.Pitch += 14.f * Commitment;
                Pose.Roll -= 11.f * Commitment;
            }
            if (MoveArt == EDuelArt::HoundChase)
            {
                const float Brace = PreparationAccent(Wind, Strike);
                const float Upcut = FMath::Sin(PI * Recover);
                Pose.Pitch += 22.f * Brace - 25.f * Upcut;
                Pivot.Z += -14.f * Brace + 15.f * Upcut;
                Sword.Pitch += 42.f * Upcut - 23.f * Brace;
                JointRotation[3].Pitch -= 38.f * Upcut;
                HingeRotation[3].Pitch += 23.f * Upcut;
            }
        }
        else if (Move == EDuelMove::Crimson && MoveArt == EDuelArt::PokerRake)
        {
            Sword = FRotator(65.f * Load + 4.f * Follow, 15.f * Load - 6.f * Follow, 0);
            Pose.Pitch = -13.f * Load + 11.f * Follow;
            Pivot.Z -= 7.f * Follow;
            JointRotation[3] = FRotator(-77.f * Load + 62.f * Follow,
                -12.f * Load - 16.f * Follow, -24.f * Load + 9.f * Follow);
            JointRotation[2] = FRotator(-24.f * Load - 31.f * Follow, 21.f * Follow, 13.f * Follow);
            JointRotation[0] = FRotator(-24.f * Load + 31.f * Follow, -12.f * Follow, -9.f * Follow);
            JointRotation[1] = FRotator(21.f * Load - 50.f * Follow, 8.f * Follow, 12.f * Follow);
            HingeRotation[3].Pitch = -25.f * Load + 18.f * Follow;
            HingeRotation[2].Pitch = -28.f * Load - 19.f * Follow;
            HingeRotation[1].Pitch = 35.f * Follow;
        }
        else if (Move == EDuelMove::Heavy || (Move == EDuelMove::Crimson && MoveArt == EDuelArt::Burst))
        {
            Sword.Pitch += (bGreat ? 110.f : 69.f) * Load - 39.f * Follow;
            Sword.Yaw -= 27.f * Load;
            Pose.Pitch = -17.f * Load + 33.f * Follow;
            Pivot.Z -= 17.f * Follow;
            JointRotation[3] = FRotator(-85.f * Load - 32.f * Follow, -14.f * Load + 13.f * Follow, -26.f * Load);
            JointRotation[0] = FRotator(-24.f * Load + 31.f * Follow, -12.f * Follow, -9.f * Follow);
            JointRotation[1] = FRotator(21.f * Load - 50.f * Follow, 8.f * Follow, 12.f * Follow);
            HingeRotation[3].Pitch = -43.f * Load + 21.f * Follow;
            HingeRotation[0].Pitch = 35.f * Load;
            HingeRotation[1].Pitch = 30.f * Follow;
            if (bGreat)
            {
                JointRotation[2] = FRotator(-72.f * Load - 39.f * Follow, 26.f * Commitment, 16.f);
            }
        }
        else if (bHoundFirst)
        {
            const float Sweep = Progress(Spec.Windup, .215f);
            const float Hop = FMath::Sin(PI * Progress(Spec.Windup + .14f, .21f));
            const float Reach = FMath::Sin(PI * Sweep);
            Sword = FRotator(56.f * (1.f - Wind) + 20.f * Load - 10.f * Reach + 12.f * Follow,
                FMath::Lerp(FMath::Lerp(28.f, -68.f, Wind), 79.f, Sweep), 47.f - 26.f * Reach);
            Pose.Yaw += -16.f * Load + 13.f * Follow;
            Pose.Pitch += 10.f * Reach - 24.f * Hop;
            Pivot += FVector(12.f * Reach, 0, 34.f * Hop - 10.f * Load);
            JointRotation[3] = FRotator(30.f * Reach + 32.f * Follow - 20.f * Load, -23.f * Load + 42.f * Follow, 13.f * Reach);
            JointRotation[2] = FRotator(35.f * Hop - 25.f * Reach, -14.f * Follow, 18.f * Hop);
            JointRotation[0] = FRotator(28.f * Reach - 34.f * Hop, 0, -10.f * Reach);
            JointRotation[1] = FRotator(-24.f * Reach + 46.f * Hop, 0, 10.f * Hop);
            HingeRotation[0].Pitch = 37.f * Hop;
            HingeRotation[1].Pitch = 46.f * Hop;
            HingeRotation[3].Pitch = 28.f * Reach - 21.f * Follow;
        }
        else if (Move == EDuelMove::Cut || Move == EDuelMove::Backcut || bWhirl)
        {
            const bool bBackcut = Move == EDuelMove::Backcut;
            const float Side = bBackcut ? -1.f : 1.f;
            const float Sweep = bWhirl ? 147.f : bBackcut ? 119.f : 107.f;
            const float Snap = FMath::SmoothStep(0.f, 1.f, Strike * 1.15f);
            const float Finish = FMath::Sin(PI * Strike);
            const float Yaw = FMath::Lerp(23.f, -(Sweep + 6.f) * Side, Wind);
            Sword.Yaw = FMath::Lerp(FMath::Lerp(Yaw, Sweep * Side, Snap), 23.f, Recover);
            const float Lift = Load;
            Sword.Pitch += (bBackcut ? 24.f : 53.f) * Lift + (bBackcut ? 29.f : -13.f) * Follow + (bBackcut ? -16.f : 6.f) * Finish;
            Pose.Yaw += Side * (-31.f * Lift + (bWhirl ? 62.f : 38.f) * Follow);
            Pose.Roll += Side * ((bWhirl ? 15.f : 9.f) * Lift - 16.f * Follow);
            Pose.Pitch += 12.f * Follow - (bBackcut ? 9.f : 4.f) * Lift;
            Pivot += FVector(7.f * Follow, -6.f * Side * Lift + 7.f * Side * Follow, -9.f * Commitment);
            JointRotation[3] = FRotator(-70.f * Lift - 57.f * Follow,
                Side * (-39.f * Lift + 64.f * Follow), Side * (-25.f * Lift - 32.f * Follow));
            JointRotation[2] = FRotator(-29.f * Lift + 21.f * Follow, Side * -29.f * Follow, 18.f * Lift);
            JointRotation[0] = FRotator(-24.f * Lift + 26.f * Follow, Side * 13.f * Follow, -13.f * Follow);
            JointRotation[1] = FRotator(17.f * Lift - 40.f * Follow, Side * -11.f * Lift, 12.f * Follow);
            HingeRotation[3].Pitch = -39.f * Lift + 24.f * Follow;
            HingeRotation[2].Pitch = -22.f * Commitment;
            HingeRotation[0].Pitch = 23.f * Lift;
            HingeRotation[1].Pitch = 27.f * Follow;
            HeadPose.Yaw -= Side * (11.f * Lift + 18.f * Follow);
        }
        else if (Move == EDuelMove::Crimson)
        {
            if (MoveWeapon == EDuelWeapon::Staff)
            {
                Sword = FRotator(65.f + 15.f * Load - 39.f * Follow, 18.f + 14.f * Load - 12.f * Follow, 0.f);
                JointRotation[3] = FRotator(31.f * Load + 49.f * Follow, -18.f * Commitment, 11.f * Commitment);
                HingeRotation[3].Pitch = 27.f * Load + 31.f * Follow;
                Pose.Pitch += 9.f * Follow - 6.f * Load;
                const float Release = Beat(Spec.Windup, .065f, .22f);
                switch (MoveArt)
                {
                case EDuelArt::IceLance:
                    Sword = FRotator(61.f + 13.f * Load - 62.f * Release, 16.f - 28.f * Load + 14.f * Release, 0);
                    Pivot += FVector(7.f * Release, -8.f * Load, -11.f * Load);
                    Pose.Yaw += -24.f * Load + 29.f * Release;
                    Pose.Pitch += 17.f * Load + 22.f * Release;
                    JointRotation[3] = FRotator(54.f * Load - 82.f * Release, -28.f * Load + 16.f * Release, 17.f * Load);
                    HingeRotation[3].Pitch += 31.f * Load - 23.f * Release;
                    HeadPose.Pitch -= 14.f * Load;
                    break;
                case EDuelArt::StarTrack:
                case EDuelArt::WitchRain:
                {
                    const float Trace = 2.f * PI * Progress(0.f, Spec.Windup);
                    Sword = FRotator(64.f + 25.f * Load - 33.f * Release, 15.f + 58.f * FMath::Sin(Trace) * Load, 22.f * Load);
                    Pose.Pitch += -22.f * Load + 19.f * Release;
                    JointRotation[3] = FRotator(-74.f * Load + 25.f * Release, 32.f * FMath::Cos(Trace) * Load, -16.f * Load);
                    JointRotation[2] = FRotator(-44.f * Load, 35.f * Load, 28.f * Load);
                    HingeRotation[2].Pitch -= 24.f * Load;
                    HeadPose.Pitch -= 24.f * Load;
                    break;
                }
                case EDuelArt::ReturningFrost:
                {
                    const float Catch = Beat(Spec.Windup + DuelCourt::ReturningFrostRecall, .10f, .23f);
                    Sword = FRotator(54.f + 22.f * Load - 49.f * Release + 26.f * Catch,
                        16.f - 74.f * Load + 99.f * Release - 49.f * Catch, -17.f * Release);
                    Pose.Yaw += -29.f * Load + 46.f * Release - 29.f * Catch;
                    Pose.Roll += 17.f * Load - 18.f * Release;
                    Pivot.Z -= 12.f * Catch;
                    JointRotation[3] = FRotator(39.f * Load - 55.f * Release + 43.f * Catch,
                        -39.f * Load + 52.f * Release - 30.f * Catch, -24.f * Release);
                    HingeRotation[3].Pitch += 31.f * Catch;
                    break;
                }
                case EDuelArt::GildedCross:
                case EDuelArt::MoonCrescent:
                {
                    const float Side = MoveArt == EDuelArt::GildedCross ? -1.f : 1.f;
                    const float Charged = bChargedArt ? Commitment : 0.f;
                    Sword = FRotator(53.f + (Side < 0.f ? 35.f : 14.f) * Load - 34.f * Release,
                        17.f + Side * (-59.f * Load + (92.f + 18.f * Charged) * Release), Side * 29.f * Release);
                    Pose.Yaw += Side * (-24.f * Load + 32.f * Release);
                    Pose.Pitch += 16.f * Charged + 21.f * Release;
                    Pivot.Z -= 12.f * Charged + 9.f * Release;
                    JointRotation[3] = FRotator(-52.f * Load + 38.f * Release,
                        Side * (-41.f * Load + 54.f * Release), Side * 26.f * Release);
                    JointRotation[2] = FRotator(-37.f * Load, -Side * 31.f * Charged, Side * 21.f * Release);
                    break;
                }
                default:
                    break;
                }
            }
            else if (MoveArt == EDuelArt::PenScript)
            {
                const float Write = Beat(Spec.Windup, .105f, .24f);
                const float Finish = bHitDelivered ? Beat(Spec.Windup + .32f, .09f, .21f) : 0.f;
                Sword = FRotator(31.f + 34.f * Load - 64.f * Write - 19.f * Finish,
                    17.f - 41.f * Load + 48.f * Write + 67.f * Finish, 12.f * Write);
                Pivot.Z -= 9.f * Load;
                Pose.Yaw += -15.f * Load + 23.f * Write - 21.f * Finish;
                HeadPose.Pitch += 16.f * Load - 12.f * Finish;
                JointRotation[3] = FRotator(-36.f * Load - 62.f * Write - 36.f * Finish,
                    -21.f * Load + 38.f * Write + 42.f * Finish, 24.f * Finish);
                HingeRotation[3].Pitch -= 29.f * Load + 25.f * Finish;
            }
            else if (MoveArt == EDuelArt::EmaWard)
            {
                const float Counter = bHitDelivered ? (1.f - Recover) : 0.f;
                JointRotation[3] = FRotator(-59.f * Commitment + 40.f * Counter, 29.f * Commitment, 0);
                JointRotation[2] = FRotator(-43.f * Commitment, -23.f * Commitment, 16.f * Counter);
                Sword.Yaw += 59.f * Commitment - 104.f * Counter;
                Pivot.Z -= 18.f * Commitment;
                Pose.Yaw -= 28.f * Commitment - 55.f * Counter;
            }
            else
            {
                const float Lift = MoveArt == EDuelArt::EmaLight ? 55.f * Load :
                    MoveArt == EDuelArt::EmaAbsolution ? 74.f * Load : MoveArt == EDuelArt::EmaVotive ? 42.f * Load : 0.f;
                JointRotation[3] = FRotator((-55.f - 37.f * Strike) * Commitment - Lift, -21.f * Commitment, 0);
                Pose.Pitch += 16.f * Follow - .30f * Lift;
                Sword.Pitch -= 33.f * Commitment + .40f * Lift;
                HeadPose.Pitch -= .31f * Lift;
                if (MoveArt == EDuelArt::HiroThreads)
                {
                    Pose.Yaw -= 28.f * Load - 24.f * Follow;
                    Sword.Yaw += -80.f * Load + 108.f * Follow;
                    JointRotation[2] = FRotator(20.f * Load, 28.f * Follow, 16.f * Commitment);
                }
                else if (MoveArt == EDuelArt::HiroReprise)
                {
                    const float Retreat = FMath::Sin(PI * Strike);
                    Pivot.Z -= 22.f * Load;
                    Pivot.Z += 25.f * Retreat;
                    Pose.Pitch += 24.f * Retreat;
                    JointRotation[0] = FRotator(37.f * Retreat, 0, 0);
                    JointRotation[1] = FRotator(-29.f * Retreat, 0, 0);
                }
                else if (MoveArt == EDuelArt::HiroCrossThread)
                {
                    Pose.Yaw += 34.f * Load - 51.f * Follow;
                    Sword.Yaw += 57.f * Load - 86.f * Follow;
                    JointRotation[2] = FRotator(-92.f * Follow, -31.f * Load, 23.f * Follow);
                }
                else if (MoveArt == EDuelArt::EmaAegis)
                {
                    JointRotation[2] = FRotator(-74.f * Commitment, -30.f * Load, 33.f * Follow);
                    Sword.Pitch += 71.f * Load;
                    Pose.Pitch -= 16.f * Commitment;
                }
            }
        }
        else if (Move == EDuelMove::Parry)
        {
            const float Deflect = 1.f - Recover;
            const bool bStillHeld = HandPresses[static_cast<uint8>(MoveHand)].Since >= 0.0;
            const float Raised = bStillHeld ? 1.f : Deflect;
            const float Hit = Strike * Deflect;
            Sword = FRotator(3.f + 13.f * Hit, FMath::Lerp(20.f, -9.f, Raised) + 27.f * Hit, -9.f * Hit);
            Pose.Roll += 5.f * Raised - 14.f * Hit;
            JointRotation[3] = FRotator(GuardShoulderPitch * Raised + 14.f * Hit, -30.f * Raised + 23.f * Hit, 5.f * Raised);
            JointRotation[0] = FRotator(28.f * Deflect, 0, 0);
            HingeRotation[3].Pitch = GuardElbowPitch * Raised - 10.f * Hit;
        }
        else if (Move == EDuelMove::Equip)
        {
            Sword.Pitch -= 57.f * Commitment;
            const int32 Arm = MoveHand == EDuelHand::Right ? 3 : 2;
            const float Side = MoveHand == EDuelHand::Right ? 1.f : -1.f;
            JointRotation[Arm] = FQuat::Slerp(JointRotation[Arm].Quaternion(),
                FRotator(12.f, Side * 44.f, -Side * 23.f).Quaternion(), Commitment).Rotator();
        }
        else if (Move == EDuelMove::Flask)
        {
            JointRotation[2] = FQuat::Slerp(JointRotation[2].Quaternion(),
                FRotator(CharacterMesh ? -115.f : -139.f, -15.f, 18.f).Quaternion(), Commitment).Rotator();
            HingeRotation[2].Pitch = FMath::Lerp(HingeRotation[2].Pitch, -24.0, static_cast<double>(Commitment));
            HeadPose.Pitch -= 22.f * Commitment;
            MouthOpen += .53f * Commitment;
        }
        if (!bBoss && (Move == EDuelMove::Cut || Move == EDuelMove::Backcut || Move == EDuelMove::Heavy || bThrust))
        {
            const float Side = Move == EDuelMove::Backcut ? -1.f : 1.f;
            const float Prepare = PreparationAccent(Wind, Strike);
            const float Settle = FMath::Square(FMath::Sin(PI * Recover));
            const float Step = FMath::Sin(PI * Progress(Spec.Windup * .45f,
                Spec.Windup * .55f + Spec.Active * .78f)) * (1.f - Recover);
            const float Weight = Move == EDuelMove::Heavy ? 1.25f : 1.f;
            // Coil shoulders against hips, then release through the already-established blade arc.
            // Preparation/settle accents are exactly zero during weapon contact sampling.
            Pose.Yaw += Side * (-8.f * Prepare + 4.f * Settle) * Weight;
            Pose.Pitch += (-4.f * Prepare + 3.f * Settle) * Weight;
            Pivot.Z -= (3.f * Prepare + 2.f * Settle) * Weight;
            HeadPose.Yaw += Side * (7.f * Prepare - 4.f * Settle);
            HeadPose.Pitch -= 4.f * Settle;
            JointRotation[3].Yaw += Side * (13.f * Prepare - 7.f * Settle);
            JointRotation[2].Roll += Side * (9.f * Prepare - 6.f * Settle);
            HingeRotation[3].Pitch -= 10.f * Prepare;
            const int32 LeadLeg = Move == EDuelMove::Backcut ? 1 : 0;
            const int32 TrailLeg = 1 - LeadLeg;
            JointLocation[LeadLeg] += FVector(4.f * Step, 0.f, 5.f * Step);
            JointRotation[LeadLeg].Pitch += 12.f * Step;
            HingeRotation[LeadLeg].Pitch += 22.f * Step;
            JointRotation[TrailLeg].Pitch -= 8.f * Step;
            HingeRotation[TrailLeg].Pitch += 9.f * (Prepare + Settle);
            if (Move == EDuelMove::Heavy)
            {
                const float Sink = Prepare + .68f * Settle;
                Pivot.Z -= (bGreat ? 20.f : 10.f) * Sink;
                Pose.Roll += Side * (17.f * Prepare - 13.f * Settle);
                Pose.Yaw -= Side * (bGreat ? 15.f : 7.f) * Prepare;
                Sword.Roll += Side * (29.f * Prepare - 19.f * Settle);
                HingeRotation[TrailLeg].Pitch += 31.f * Prepare + 15.f * Settle;
                HeadPose.Yaw += 14.f * Side * Prepare;
            }
            else if (MoveWeapon == EDuelWeapon::Rapier)
            {
                Sword.Pitch += 25.f * Prepare - 12.f * Settle;
                HingeRotation[3].Pitch -= 18.f * Prepare;
                JointRotation[2].Yaw += 19.f * Side * Prepare;
            }
        }
        if (DuelCourt::Strike(MoveWeapon, Move, MoveArt) || Move == EDuelMove::Parry || Move == EDuelMove::Equip)
        {
            if (MoveHand == EDuelHand::Left)
            {
                const auto Mirror = [](const FRotator& Rotation)
                {
                    return FRotator(Rotation.Pitch, -Rotation.Yaw, -Rotation.Roll);
                };
                const FRotator Right = JointRotation[3];
                JointRotation[3] = Mirror(JointRotation[2]);
                JointRotation[2] = Mirror(Right);
                const FRotator Elbow = HingeRotation[3];
                HingeRotation[3] = Mirror(HingeRotation[2]);
                HingeRotation[2] = Mirror(Elbow);
                Sword = Mirror(Sword);
                Pose.Yaw *= -1.f;
                Pose.Roll *= -1.f;
            }
            ActiveGrip = MoveHand == EDuelHand::Right ? Weapon.Get() : LeftWeapon.Get();
            const FRotator Ready = ReadyGripAngle(EquippedWeapon(MoveHand), MoveHand == EDuelHand::Right ? 1.f : -1.f);
            AttackAngle = Move == EDuelMove::Parry ? Sword : FQuat::Slerp(Sword.Quaternion(), Ready.Quaternion(), Recover).Rotator();
        }
        for (const EDuelHand Hand : {EDuelHand::Right, EDuelHand::Left})
        {
            const float Raised = GuardPose[static_cast<uint8>(Hand)];
            if (Move == EDuelMove::Idle && Raised > 0.f)
            {
                const bool bRight = Hand == EDuelHand::Right;
                const int32 Arm = bRight ? 3 : 2;
                const float Side = bRight ? 1.f : -1.f;
                const float Absorb = HitRecoil * HitRecoil;
                const float Blend = FMath::SmoothStep(0.f, 1.f, Raised);
                const FRotator Shoulder(GuardShoulderPitch - 20.f * Absorb, -Side * 30.f, Side * 5.f);
                const FRotator Elbow(GuardElbowPitch + 26.f * Absorb, 0, 0);
                JointRotation[Arm] = FQuat::Slerp(JointRotation[Arm].Quaternion(), Shoulder.Quaternion(), Blend).Rotator();
                HingeRotation[Arm] = FQuat::Slerp(HingeRotation[Arm].Quaternion(), Elbow.Quaternion(), Blend).Rotator();
                // Keep the raised rim clear of the chest and hair throughout the arc.
                if (PosedMesh) JointLocation[Arm].X += 12.f * Blend;
                Pose.Roll += Side * (5.f + 12.f * Absorb) * Blend;
                Pose.Yaw += Side * (5.f * Jog + 10.f * Absorb) * Blend;
            }
        }
    }

    if (bBoss && IsAlive())
    {
        Pivot += GetActorTransform().InverseTransformVectorNoScale(HitDirection) * (18.f * HitRecoil * HitRecoil);
        HeadPose.Pitch -= 11.f * HitRecoil;
        Pivot.Z -= bHiroShadow ? FormProgress * 11.f : 0.f;
    }
    const float Scale = bBoss && !bCharacterPreview && !bAlternateForm ? 1.154f + .059f * FormProgress : 1.f;
    if (bBoss)
    {
        Pivot.Z += GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * (Scale - 1.f);
    }
    if (NorthExtractionWeight > 0.f && IsAlive())
    {
        const float Blend = NorthExtractionWeight;
        const float Step = FMath::Sin(NorthExtractionCycle);
        Pivot.Z -= bNorthExtractionSeated ? 38.f * Blend : 0.f;
        Pose = FQuat::Slerp(Pose.Quaternion(), FRotator(bNorthExtractionSeated ? 3.f : -8.f, 0.f,
            bNorthExtractionSeated ? 0.f : 3.f * Step).Quaternion(), Blend).Rotator();
        HeadPose.Pitch = FMath::Lerp(HeadPose.Pitch, bNorthExtractionSeated ? 0.f : 13.f, Blend);
        for (int32 Index = 0; Index < 4; ++Index)
        {
            const bool bArm = Index >= 2;
            const float RungStride = (Index % 2 == 0 ? 1.f : -1.f) * Step;
            const float Lift = FMath::Max(0.f, RungStride);
            const float Reach = bNorthExtractionSeated ? (bArm ? 29.f : 86.f) :
                (bArm ? 123.f + 26.f * RungStride : 24.f + 29.f * Lift);
            const float Bend = bNorthExtractionSeated ? (bArm ? -27.f : -85.f) :
                (bArm ? -16.f - 24.f * Lift : -18.f - 36.f * Lift);
            JointRotation[Index] = FQuat::Slerp(JointRotation[Index].Quaternion(),
                FRotator(Reach, 0.f, 0.f).Quaternion(), Blend).Rotator();
            HingeRotation[Index] = FQuat::Slerp(HingeRotation[Index].Quaternion(),
                FRotator(Bend, 0.f, 0.f).Quaternion(), Blend).Rotator();
        }
    }
    if (IsAlive() && bPoseReady && Move != EDuelMove::Idle)
    {
        // The lead-in fits inside the earliest windup, so collision and animation share the same blade.
        const float BlendTime = Move == EDuelMove::Roll ? DuelCourt::RollMotionDelay : .095f;
        const float Blend = FMath::SmoothStep(0.f, 1.f, MoveAge / BlendTime);
        if (Blend < 1.f)
        {
            Pivot = FMath::Lerp(PreviousPivot, Pivot, Blend);
            Pose = FQuat::Slerp(PreviousBody.Quaternion(), Pose.Quaternion(), Blend).Rotator();
            AttackAngle = FQuat::Slerp(PreviousBlade.Quaternion(), AttackAngle.Quaternion(), Blend).Rotator();
            for (int32 Index = 0; Index < 4; ++Index)
            {
                JointRotation[Index] = FQuat::Slerp(PreviousLimbs[Index].Quaternion(),
                    JointRotation[Index].Quaternion(), Blend).Rotator();
                HingeRotation[Index] = FQuat::Slerp(PreviousHinges[Index].Quaternion(),
                    HingeRotation[Index].Quaternion(), Blend).Rotator();
            }
        }
    }
    else if (IsAlive() && bPoseReady && Move == EDuelMove::Idle && !bContactSample)
    {
        // TickMove resets Idle's age to zero. Blend from the last displayed pose, not the
        // attack's old start pose; this also joins a completed roll to the first running step.
        const float Settle = 1.f - FMath::Exp(-28.f * DeltaSeconds);
        Pivot = FMath::Lerp(DisplayedPivot, Pivot, Settle);
        Pose = FQuat::Slerp(DisplayedBody.Quaternion(), Pose.Quaternion(), Settle).Rotator();
        for (int32 Index = 0; Index < 4; ++Index)
        {
            const bool bHeldGuard = !bBoss && Index >= 2 &&
                GuardPose[static_cast<uint8>(Index == 2 ? EDuelHand::Left : EDuelHand::Right)] > 0.f;
            if (!bHeldGuard)
            {
                JointRotation[Index] = FQuat::Slerp(DisplayedLimbs[Index].Quaternion(), JointRotation[Index].Quaternion(), Settle).Rotator();
                HingeRotation[Index] = FQuat::Slerp(DisplayedHinges[Index].Quaternion(), HingeRotation[Index].Quaternion(), Settle).Rotator();
                JointLocation[Index] = FMath::Lerp(Limbs[Index]->GetRelativeLocation(), JointLocation[Index], Settle);
            }
        }
    }
    const FTransform FigureTransform(Pose, Pivot, FVector(Scale));
    if (bContactSample)
    {
        if (ActiveGrip)
        {
            const int32 Index = MoveHand == EDuelHand::Right ? 3 : 2;
            const FTransform Limb(JointRotation[Index], JointLocation[Index]);
            FVector Wrist = ActiveGrip->GetRelativeLocation();
            if (Hinges.Num() == 4)
            {
                const FTransform Hinge(HingeRotation[Index], Hinges[Index]->GetRelativeLocation());
                Wrist = Hinge.TransformPosition(Wrist);
            }
            const FTransform Body = FigureTransform * GetActorTransform();
            *BladeGrip = FTransform(Body.GetRotation() * AttackAngle.Quaternion(),
                Body.TransformPosition(Limb.TransformPosition(Wrist)), Body.GetScale3D());
        }
        return;
    }
    if (FlaskProp)
    {
        FlaskProp->SetVisibility(Move == EDuelMove::Flask && Wind > .08f && Recover < .92f, true);
    }
    if (LeftWeapon)
    {
        LeftWeapon->SetHiddenInGame(bCharacterPreview || Move == EDuelMove::Flask || NorthExtractionWeight > 0.f, true);
    }
    const bool bGesture = bDialogueSpeaking && IsAlive() && Move == EDuelMove::Idle &&
        Jog < .35f && NorthExtractionWeight <= 0.f;
    DialogueGestureWeight = FMath::FInterpTo(DialogueGestureWeight, bGesture ? 1.f : 0.f,
        DeltaSeconds, bGesture ? 7.f : 9.f);
    if (Move == EDuelMove::Idle && DialogueGestureWeight > .001f && NorthExtractionWeight <= 0.f)
    {
        const double Phrase = GetWorld()->GetTimeSeconds() * 3.45;
        const float Weight = DialogueGestureWeight;
        const float Accent = FMath::Square(FMath::Max(0.f, float(FMath::Sin(Phrase))));
        const float Turn = FMath::Sin(Phrase * .56);
        HeadPose.Pitch += Weight * (-1.5f + 5.2f * FMath::Sin(Phrase * 1.24));
        HeadPose.Yaw += Weight * Turn * 4.8f;
        HeadPose.Roll += Weight * FMath::Sin(Phrase * .7 + .9) * 2.3f;
        JointRotation[3].Pitch -= Weight * (7.f + 18.f * Accent);
        JointRotation[3].Roll -= Weight * (4.f + 6.f * Accent);
        HingeRotation[3].Pitch -= Weight * (8.f + 17.f * Accent);
        JointRotation[2].Pitch -= Weight * (3.f + 5.f * FMath::Square(Turn));
    }
    Figure->SetRelativeTransform(FigureTransform);
    DisplayedPivot = Pivot;
    DisplayedBody = Pose;
    DisplayedBlade = AttackAngle;
    DisplayedLimbs = JointRotation;
    DisplayedHinges = HingeRotation;
    // StartPlay may assign a move before the first frame.
    if (!bPoseReady)
    {
        PreviousPivot = Pivot;
        PreviousBody = Pose;
        PreviousBlade = AttackAngle;
        PreviousLimbs = JointRotation;
        PreviousHinges = HingeRotation;
        bPoseReady = true;
    }
    for (int32 Index = 0; Index < 4; ++Index)
    {
        Limbs[Index]->SetRelativeLocationAndRotation(JointLocation[Index], JointRotation[Index]);
        if (Hinges.Num() == 4)
        {
            Hinges[Index]->SetRelativeRotation(HingeRotation[Index]);
        }
    }
    for (const EDuelHand Hand : {EDuelHand::Right, EDuelHand::Left})
    {
        const bool bRight = Hand == EDuelHand::Right;
        const float Side = bRight ? 1.f : -1.f;
        USceneComponent* Grip = bRight ? Weapon.Get() : LeftWeapon.Get();
        if (!Grip)
        {
            continue;
        }
        if(bNorthDefeat)
        {
            const EDuelWeapon Held=EquippedWeapon(Hand);
            const bool bShield=Held==EDuelWeapon::Buckler||Held==EDuelWeapon::Greatshield;
            const FRotator Ready=ReadyGripAngle(Held,Side);
            // Relax the blade along the ground; shields turn flat instead of cutting through the floor.
            const FRotator Rest(FMath::Lerp(Ready.Pitch,bShield?82.f:8.f,DefeatPose.Drop),
                Ready.Yaw+Side*25.f*DefeatPose.Drop,Ready.Roll*(1.f-.7f*DefeatPose.Drop));
            const FQuat Goal=GetActorQuat()*Rest.Quaternion();
            Grip->SetWorldRotation(FQuat::Slerp(Grip->GetComponentQuat(),Goal,1.f-FMath::Exp(-18.f*DeltaSeconds)));
        }
        else if (Move == EDuelMove::Idle && GuardPose[static_cast<uint8>(Hand)] > 0.f)
        {
            const float Raised = FMath::SmoothStep(0.f, 1.f, GuardPose[static_cast<uint8>(Hand)]);
            const FQuat Ready = Figure->GetComponentQuat() * ReadyGripAngle(EquippedWeapon(Hand), Side).Quaternion();
            const FQuat Guard = GetActorQuat() * FRotator(3.f, -Side * 9.f, 0.f).Quaternion();
            Grip->SetWorldRotation(FQuat::Slerp(Ready, Guard, Raised));
        }
        else
        {
            const FRotator Angle = Grip == ActiveGrip ? AttackAngle : ReadyGripAngle(EquippedWeapon(Hand), Side);
            Grip->SetWorldRotation(Figure->GetComponentQuat() * Angle.Quaternion());
        }
    }
    Head->SetRelativeRotation(HeadPose + FRotator(Breathe * 1.5f, 0, 0));
    const float HissAge = FMath::Fmod(GetWorld()->GetTimeSeconds() + (bBoss ? .12f : 1.85f), 4.8f) / .48f;
    const bool bHiss = IsAlive() && Move == EDuelMove::Idle && Jog < .3f && HissAge < 1.f;
    const float Hiss = bHiss ? FMath::Sin(PI * HissAge) : 0.f;
    Mouth->SetRelativeScale3D(FVector(1, 1, MouthOpen + .88f * Hiss));
    if (Breath)
    {
        const APlayerCameraManager* View = UGameplayStatics::GetPlayerCameraManager(this, 0);
        Breath->SetVisibility(View && Hiss > .035f);
        if (View && Hiss > .035f)
        {
            const FVector Forward = Mouth->GetForwardVector();
            const FVector Position = Mouth->GetComponentLocation() + Forward * (8.f + HissAge * 39.f)
                + FVector(0, 0, HissAge * 10.f);
            const FRotator Facing = FRotationMatrix::MakeFromZX(View->GetCameraLocation() - Position, Forward).Rotator();
            Breath->SetWorldLocationAndRotation(Position, Facing);
            Breath->SetWorldScale3D(FVector(.18f + HissAge * .43f, .12f + HissAge * .19f, 1));
            BreathMaterial->SetScalarParameterValue(TEXT("Opacity"), Hiss * .30f);
        }
    }
    const float Spread = IsAwakened() ? 1.f : bWitching ?
        Progress(Spec.Windup * .36f, Spec.Windup * .64f) : 0.f;
    FormProgress = FMath::FInterpConstantTo(FormProgress, Spread, DeltaSeconds, 1.45f);
    for (int32 Index = 0; Index < YukiPrisms.Num(); ++Index)
    {
        USceneComponent* Prism = YukiPrisms[Index];
        const bool bVisible = IsAlive() && !bCombatOver && FormProgress > .015f && (BossPhase == 3 || Index % 2 == 0);
        Prism->SetVisibility(bVisible, true);
        if (bVisible)
        {
            const float Theta = Index * PI / 3.f + GetWorld()->GetTimeSeconds() * (BossPhase == 3 ? .94f : .58f);
            const float Radius = (BossPhase == 3 ? 152.f : 114.f) * (.65f + .35f * FormProgress) *
                (1.f - .24f * WitchGather + .28f * WitchCrest);
            Prism->SetRelativeLocationAndRotation(
                FVector(FMath::Cos(Theta) * Radius - 24.f, FMath::Sin(Theta) * Radius,
                    208.f + 38.f * FMath::Sin(2.f * Theta) + 42.f * WitchOpen),
                FRotator(19.f * FMath::Sin(Theta), FMath::RadiansToDegrees(Theta), 21.f * FMath::Cos(Theta)));
            Prism->SetRelativeScale3D(FVector(FormProgress * (BossPhase == 3 ? 1.2f : .91f)));
        }
    }
    const float Lash = Move == EDuelMove::ShadowSweep ? OrbitSide * (-111.f * Wind + 227.f * Strike) * (1.f - Recover) : 0.f;
    Tail->SetRelativeRotation(FRotator(Breathe * 4.f - (bHiroShadow ? FormProgress * 16.f + 29.f * WitchGather - 18.f * WitchRelease : 0.f),
        FMath::Sin(Gait) * (bHiroShadow ? 24.f : 13.f) + Lash + CloseTail +
        (bHiroShadow ? 25.f * WitchOpen * FMath::Sin(MoveAge * 4.5f) : 0.f), 0));
    if(bNorthDefeat)Tail->SetRelativeRotation(DefeatPose.Tail);
    Tail->SetRelativeScale3D(FVector(1.f + (bHiroShadow ? FormProgress * (BossPhase == 3 ? .43f : .29f) : 0.f)));
    PoseCharacterMesh();

    const bool bShowShadow = bHiroShadow && FormProgress > .015f;
    for (USceneComponent* Claws : ShadowClaws)
    {
        Claws->SetVisibility(bShowShadow, true);
        Claws->SetRelativeScale3D(FVector(FMath::Max(.01f, FormProgress) * (BossPhase == 3 ? 1.23f : 1.f)));
    }
    if (ShadowTail)
    {
        ShadowTail->SetVisibility(bShowShadow, true);
        ShadowTail->SetRelativeScale3D(FVector(FMath::Max(.01f, FormProgress)));
        ShadowTail->SetRelativeRotation(FRotator((Move == EDuelMove::ShadowSweep ? -26.f * Commitment : 0.f) - 21.f * WitchGather,
            Breathe * 13.f + 24.f * WitchRelease, 14.f * Breathe - 19.f * WitchOpen));
    }
    for (int32 Index = 0; Index < Wings.Num(); ++Index)
    {
        const float Side = Index ? 1.f : -1.f;
        Wings[Index]->SetVisibility(Character == EDuelCharacter::Ema && !bAlternateForm && FormProgress > .015f, true);
        Wings[Index]->SetRelativeScale3D(FVector(FMath::Max(.01f, FormProgress) * (BossPhase == 3 ? 1.16f : 1.f)));
        const float Sweep = Move == EDuelMove::WingSweep ? -60.f * Wind * (1.f - Strike) + 95.f * Strike * (1.f - Recover) :
            (Move == EDuelMove::WingVerdict ? 42.f * FMath::Sin(MoveAge * 6.f) * Commitment :
                (Move == EDuelMove::HaloBreak ? 33.f * Wind - 68.f * Strike * (1.f - Recover) :
                    Move == EDuelMove::Wingfall ? Side * (-54.f * Wind + 83.f * Strike) * (1.f - Recover) : 0.f));
        const float Droop = Move == EDuelMove::Stagger ? 28.f * (1.f - Recover) : 0.f;
        const float Fold = bNorthBossDefeat ? DefeatPose.Drop : 0.f;
        const float Shear = Move == EDuelMove::OssuaryShears
            ? Side * (-66.f * Wind + 144.f * Progress(Spec.Windup - .05f, .20f)) * (1.f - Recover) : 0.f;
        Wings[Index]->SetRelativeRotation(FRotator(Droop - 15.f * WitchOpen + 11.f * WitchRelease - 31.f * Fold,
            Side * ((1.f - FormProgress) * 39.f + 29.f * WitchGather - 22.f * WitchOpen + 66.f * Fold) + Sweep + CloseWing + Shear,
            Side * (Breathe * 6.f + (1.f - FormProgress) * -60.f - 19.f * WitchGather + 24.f * WitchOpen + 14.f * WitchCrest - 56.f * Fold)));
    }
}

FVector ADuelFighter::LockPoint() const
{
    const FVector Chest = GetActorLocation() + FVector(0, 0, bBoss ? 45.f : 43.f);
    if (!bBoss || !Head)
    {
        return Chest;
    }
    const double Rise = Figure->GetRelativeLocation().Z;
    const double Scale = Figure->GetRelativeScale3D().X;
    const FVector Face = GetActorLocation() +
        GetActorQuat().RotateVector((Head->GetRelativeLocation() + FVector(33, 0, 35)) * Scale) +
        FVector(0, 0, Move == EDuelMove::Wingfall ? Rise * .66 : Rise);
    return FMath::Lerp(Chest, Face, .51);
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthAnimationCurvesTest, "ManosabaDuel.NorthTower.AnimationCurves",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthAnimationCurvesTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    for (int32 Sample = 0; Sample <= 720; ++Sample)
    {
        const float Phase = Sample * PI / 180.f;
        const FFootCycle Foot = FootCycle(Phase);
        const FFootCycle NextCycle = FootCycle(Phase + 2.f * PI);
        TestTrue(TEXT("Foot trajectory remains bounded"), FMath::Abs(Foot.Travel) <= 1.00001f &&
            Foot.Lift >= 0.f && Foot.Lift <= 1.00001f && Foot.Plant >= 0.f && Foot.Plant <= 1.00001f);
        TestTrue(TEXT("Planted feet do not simultaneously lift"), Foot.Lift * Foot.Plant == 0.f);
        TestTrue(TEXT("Stride loops without accumulating a positional offset"),
            FMath::IsNearlyEqual(Foot.Travel, NextCycle.Travel, .00002f) &&
            FMath::IsNearlyEqual(Foot.Lift, NextCycle.Lift, .00002f));
    }
    for (const float Boundary : {0.f, .58f * 2.f * PI, 2.f * PI})
    {
        const FFootCycle Before = FootCycle(Boundary - .0001f);
        const FFootCycle After = FootCycle(Boundary + .0001f);
        TestTrue(TEXT("Contact and toe-off positions are continuous"),
            FMath::Abs(Before.Travel - After.Travel) < .001f && FMath::Abs(Before.Lift - After.Lift) < .001f);
    }
    TestTrue(TEXT("Preparation visibly coils before contact"), PreparationAccent(.5f, 0.f) > .99f);
    for (int32 Sample = 0; Sample <= 100; ++Sample)
    {
        TestEqual(TEXT("New torso accents cannot extend the damage-active blade arc"),
            PreparationAccent(1.f, Sample / 100.f), 0.f);
    }
    for(const EDuelCharacter Character : {EDuelCharacter::Ema,EDuelCharacter::Hiro})
    {
        const FNorthDefeatPose Start=NorthDefeatPose(Character,0.f);
        const FNorthDefeatPose End=NorthDefeatPose(Character,NorthDefeatDuration);
        const FNorthDefeatPose Held=NorthDefeatPose(Character,NorthDefeatDuration+4.f);
        TestTrue(TEXT("Defeat starts from the standing reference without a figure offset"),Start.Offset.IsNearlyZero(.001f)&&Start.Body.IsNearlyZero(.001f));
        TestTrue(TEXT("Defeat remains collapsed instead of recovering to standing"),
            End.Body.Equals(Held.Body,.001f)&&End.Offset.Equals(Held.Offset,.001f)&&FMath::Abs(End.Body.Pitch)+FMath::Abs(End.Body.Roll)>60.f);
        for(int32 I=0;I<4;++I)TestTrue(TEXT("Limbs remain in the final collapsed pose"),End.Limbs[I].Equals(Held.Limbs[I],.001f)&&End.Hinges[I].Equals(Held.Hinges[I],.001f));
        FNorthDefeatPose Previous=Start;
        for(int32 Sample=1;Sample<=435;++Sample)
        {
            const FNorthDefeatPose P=NorthDefeatPose(Character,Sample/300.f);
            TestTrue(TEXT("Defeat body remains finite and bounded"),!P.Body.ContainsNaN()&&!P.Head.ContainsNaN()&&!P.Offset.ContainsNaN()&&P.Offset.Size()<100.f);
            TestTrue(TEXT("Defeat curves have no frame discontinuity"),FVector::Dist(P.Offset,Previous.Offset)<3.f&&FMath::Abs(FRotator::NormalizeAxis(P.Body.Roll-Previous.Body.Roll))<3.f&&FMath::Abs(FRotator::NormalizeAxis(P.Body.Pitch-Previous.Body.Pitch))<3.f);
            Previous=P;
        }
    }
    TestTrue(TEXT("Each protagonist has a distinct collapse silhouette"),
        NorthDefeatPose(EDuelCharacter::Ema,NorthDefeatDuration).Body.Roll>55.f&&
        NorthDefeatPose(EDuelCharacter::Hiro,NorthDefeatDuration).Body.Pitch<-50.f);
    const float BossEnd = DuelCourt::BossDefeatDuration;
    for (EDuelCharacter Character : {EDuelCharacter::Ema, EDuelCharacter::Hiro, EDuelCharacter::Yuki})
    {
        FNorthDefeatPose Previous = NorthBossDefeatPose(Character, 0.f);
        for (int32 Frame = 1; Frame <= 186; ++Frame)
        {
            const FNorthDefeatPose Pose = NorthBossDefeatPose(Character, BossEnd * Frame / 186.f);
            TestTrue(TEXT("Boss defeat pose advances continuously across a finite body arc"),
                !Pose.Offset.ContainsNaN() && !Pose.Body.ContainsNaN() && Pose.Offset.Size() < 130.f &&
                FVector::Dist(Pose.Offset, Previous.Offset) < 5.f &&
                FQuat::ErrorAutoNormalize(Pose.Body.Quaternion(), Previous.Body.Quaternion()) < .06f);
            Previous = Pose;
        }
        const FNorthDefeatPose Rest = NorthBossDefeatPose(Character, BossEnd + 3.f);
        TestTrue(TEXT("The defeated boss keeps her final body position"), Previous.Drop > .999f &&
            Previous.Body.Equals(Rest.Body, .001) && Previous.Offset.Equals(Rest.Offset, .001));
    }
    TestTrue(TEXT("The bosses finish in distinct side, forward and backward silhouettes"),
        NorthBossDefeatPose(EDuelCharacter::Ema, BossEnd).Body.Roll > 65.f &&
        NorthBossDefeatPose(EDuelCharacter::Hiro, BossEnd).Body.Pitch < -20.f &&
        NorthBossDefeatPose(EDuelCharacter::Yuki, BossEnd).Body.Pitch > 25.f);
    for (const EDuelMove Move : {EDuelMove::Awaken, EDuelMove::Eclipse})
    {
        const DuelCourt::FMoveTiming Timing = DuelCourt::Timing(Move, true);
        for (const float Age : {0.f, Timing.Total(), Timing.Total() + 1.f})
        {
            const FWitchCycle Pose = WitchCycle(Age, Timing);
            TestTrue(TEXT("Phase-change pose begins and ends at rest"),
                FMath::Abs(Pose.Gather) + FMath::Abs(Pose.Open) + FMath::Abs(Pose.Release) +
                FMath::Abs(Pose.Crest) + FMath::Abs(Pose.Hover) < .00001f);
        }
        const FWitchCycle BeforeBurst = WitchCycle(Timing.Windup - .0001f, Timing);
        const FWitchCycle AtBurst = WitchCycle(Timing.Windup, Timing);
        const FWitchCycle AfterBurst = WitchCycle(Timing.Windup + .0001f, Timing);
        TestTrue(TEXT("Reveal is ready when the existing burst fires"), AtBurst.Open > .999f);
        TestTrue(TEXT("Burst onset does not snap the pose"),
            FMath::Abs(BeforeBurst.Open - AfterBurst.Open) < .001f && AfterBurst.Release < .001f);
        TestTrue(TEXT("Phase-change burst accent peaks inside the existing active interval"),
            WitchCycle(Timing.Windup + Timing.Active * .5f, Timing).Crest > .99f);
        for (int32 Sample = 0; Sample <= 500; ++Sample)
        {
            const FWitchCycle Pose = WitchCycle(Timing.Total() * Sample / 500.f, Timing);
            for (const float Value : {Pose.Gather, Pose.Open, Pose.Release, Pose.Crest, Pose.Hover})
            {
                TestTrue(TEXT("Transformation layers remain bounded and finite"), FMath::IsFinite(Value) && Value >= 0.f && Value <= 1.00001f);
            }
        }
    }
    return true;
}
#endif
