#pragma once

#include "CoreMinimal.h"

enum class EDuelLight : uint8;
enum class EDuelCharacter : uint8;

// Cosmetic emission plans only. No hit tests, attack timing, gameplay RNG, lights,
// components or per-frame emitters. ADuelArena consumes these in its two FX batches.
namespace DuelBurstFx
{
    enum class EStyle : uint8 { Ember, Carmine, Frost };
    enum class EKind : uint8 { Impact, Detonation, BossRelease, Front };
    constexpr int32 MaxEmissions = 64;
    constexpr int32 MaxClouds = 16;

    struct FEmission
    {
        EDuelLight Shape;
        FVector At, Velocity, Axis;
        FVector2D Size;
        FLinearColor Tint;
        float Life = .5f, Gravity = 0.f, Spin = 0.f;
    };
    struct FPlan
    {
        TArray<FEmission, TInlineAllocator<MaxEmissions>> Emissions;
        FLinearColor Light;
        int32 Clouds = 0;
    };
    EStyle CharacterStyle(EDuelCharacter Character);
    EStyle TintStyle(const FLinearColor& Tint);
    FPlan Make(EStyle Style, EKind Kind, const FVector& At, float Radius,
        float Life, const FVector& TowardCamera, uint32 Seed = 0);
}
