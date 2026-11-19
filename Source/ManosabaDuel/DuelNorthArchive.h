#pragma once

#include "CoreMinimal.h"

namespace DuelNorthArchive
{
    inline constexpr double FloorZ = 700.;
    inline constexpr double ShelfFoot = .8;
    inline constexpr double ShelfWidth = 208.;
    inline constexpr double ShelfDepth = 71.;
    inline constexpr double ShelfHeight = 243.;
    inline constexpr double ImportedHeight = 206.344;
    inline constexpr double TierTops[] = {6.8, 39.0, 66.8, 96.7, 128.5, 167.4};
    inline constexpr int32 VolumeTier = 3;
    inline constexpr double VolumeMount = ShelfFoot + TierTops[VolumeTier] * ShelfHeight / ImportedHeight + .25;
    inline const FVector VolumeSize(9.2, 14.5, 28.);

    struct FRow { double X; float Yaw; int32 Bays; };
    inline constexpr FRow Rows[] = {
        {-1690, -90, 4}, {-1230, 90, 5}, {-765, 90, 4},
        {790, -90, 4}, {1255, -90, 5}, {1710, 90, 4},
    };
    inline constexpr double BayDistance[] = {1395., 1150., 905., 485., 243.};
    inline constexpr double AisleX[] = {-1875., -1460., -997.5, 0., 1022.5, 1482.5, 1895.};
    inline constexpr double CrossAisle = 690.;
    inline constexpr double AisleEnd = 1492.;

    inline FVector2D BayPosition(int32 Side, int32 Row, int32 Bay)
    {
        return FVector2D(Rows[Row].X, Side * BayDistance[Bay]);
    }

    struct FVolume
    {
        const TCHAR* Id;
        const TCHAR* Title;
        int32 Side, Row, Bay;
        double Slot;
        bool bOptional = false;

        FRotator Rotation() const { return FRotator(0, Rows[Row].Yaw + 180., 0); }
        FVector Position() const
        {
            const FRotator Rack(0, Rows[Row].Yaw, 0);
            return FVector(BayPosition(Side, Row, Bay), FloorZ) + Rack.RotateVector(FVector(Slot, -22.5, 0));
        }
    };

    inline constexpr FVolume Volumes[] = {
        {TEXT("ArchiveTransferLedger"), TEXT("退信登记"), 1, 2, 2, 13.},
        {TEXT("ArchiveSignatureRegister"), TEXT("送信记录"), -1, 4, 2, -15.},
        {TEXT("ArchiveBindingNote"), TEXT("夹在书脊里的留言"), -1, 1, 1, 23., true},
        {TEXT("ArchiveNightIndex"), TEXT("夜班看守的便签"), 1, 4, 1, 17., true},
    };
}
