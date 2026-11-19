#pragma once

#include "CoreMinimal.h"

namespace DuelCombatMechanics
{
    inline constexpr float MirrorFormTime = 1.65f;
    inline constexpr float MirrorHeight = 570.f;

    inline bool ReflectMirrorSegment(const FVector& Center, float HalfWidth, float HalfLength,
        const FVector& From, FVector& To, FVector& Velocity, FVector& Bounce)
    {
        const FVector Step = To - From;
        const double Half[] = {HalfLength, HalfWidth};
        double First = 2.;
        FVector Normal = FVector::ZeroVector;
        for (int32 Axis = 0; Axis < 2; ++Axis)
        {
            if (Half[Axis] <= 0. || FMath::Abs(Step[Axis]) < UE_SMALL_NUMBER) continue;
            const double Side = FMath::Sign(Step[Axis]);
            const double Edge = Center[Axis] + Side * Half[Axis];
            const double T = (Edge - From[Axis]) / Step[Axis];
            if (T < 0. || T > 1. || T > First + UE_SMALL_NUMBER) continue;
            const FVector Hit = From + Step * T;
            if (FMath::Abs(Hit[1-Axis] - Center[1-Axis]) > Half[1-Axis] + .001 ||
                Hit.Z < Center.Z || Hit.Z > Center.Z + MirrorHeight) continue;
            if (T < First - UE_SMALL_NUMBER) Normal = FVector::ZeroVector;
            First = T;
            Normal[Axis] = 1.;
        }
        if (First > 1.) return false;
        Bounce = From + Step * First;
        const FVector Sign = FVector::OneVector - 2. * Normal;
        To = Bounce + (To - Bounce) * Sign;
        Velocity *= Sign;
        return true;
    }
}
