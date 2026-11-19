#pragma once

#include "DuelGame.h"

namespace DuelCastle
{
    constexpr int32 WingCount = 3;
    constexpr uint8 Whole = (1 << WingCount) - 1;
    constexpr uint8 ClueMask(int32 Wing) { return static_cast<uint8>(3 << (Wing * 2)); }
    constexpr uint8 WingBit(int32 Wing) { return static_cast<uint8>(1 << Wing); }
    constexpr float DropLimit = -2100.f;
    inline const FVector Hub(0, -4950, 184);
    inline const FTransform Spawn(FRotator(0, 90, 0), Hub + FVector(-245, -345, 62));
    inline const FVector FurnaceOrigin(4190, -11290, -320);
    inline const FVector EastGate(15140, -1540, 466);
    inline const FVector IndexStone = Hub + FVector(945, -135, 0);

    struct FWing
    {
        FVector Court;
        FVector Key;
        FVector Clue[2];
        FVector Shortcut;
    };

    inline const FWing Wings[WingCount] = {
        {FVector::ZeroVector, FVector(-11160, -2330, -300),
            {FVector(-9850, -830, -300), FVector(-6890, 749, 0)}, FVector(-3160, -1140, 0)},
        {DuelCourt::AppealOrigin, FVector(4440, -4240, 205),
            {FVector(6200, -4660, 205), FVector(7470, -350, 205)}, FVector(9085, -650, 370)},
        {FurnaceOrigin, FVector(-330, -8890, -320),
            {FVector(-2850, -9340, -320), FVector(710, -11040, -320)}, FVector(1460, -10630, -320)}
    };

    inline FVector Gate(int32 Wing)
    {
        return Wings[Wing].Court + FVector(DuelCourt::GateX - 127.f, 0, 0);
    }

    inline FVector Record(int32 Wing)
    {
        return Wings[Wing].Court + FVector(1310, 385, 0);
    }

    inline FVector FinalRecord()
    {
        return DuelCourt::YukiOrigin + FVector(1920, -358, 0);
    }

    inline bool Close(const FVector& At, const FVector& Point, float Reach = 204.f)
    {
        return FMath::Abs(At.Z - Point.Z - 62.f) < 106.f && FVector::DistSquared2D(At, Point) <= FMath::Square(Reach);
    }
}
