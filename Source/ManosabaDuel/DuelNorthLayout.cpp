#include "DuelNorthLayout.h"
#include "DuelNorthArchive.h"
#include "DuelNorthRose.h"
#include <initializer_list>

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace DuelNorthLayout
{
namespace
{
    struct FLayout
    {
        TArray<FRoom> RoomData;
        TArray<FStairFlight> StairData;
        TArray<FPortal> PortalData;
        TArray<FCheckpoint> CheckpointData;
        TArray<FRoute> RouteData;
        TArray<FLandmark> LandmarkData;

        void Room(const TCHAR* Id, EFloor Floor, float X0, float X1, float Y0, float Y1,
            float Z, float Height, ERoomKind Kind = ERoomKind::Corridor,
            bool bRoof = true, bool bEnclose = true, bool bFloor = true)
        {
            RoomData.Add({FName(Id), Floor, FBox(FVector(X0, Y0, Z), FVector(X1, Y1, Z + Height)),
                Kind, bFloor, bRoof, bEnclose});
        }

        void Door(const TCHAR* Id, const TCHAR* A, const TCHAR* B, FVector At, FVector Normal,
            float Width = 300, float Height = 360, EGate Gate = EGate::Open)
        {
            PortalData.Add({FName(Id), FName(A), FName(B), At, Normal, Width, Height, Gate, false});
        }

        void Window(const TCHAR* Id, const TCHAR* RoomId, FVector Sill, FVector Normal, float Width, float Height)
        {
            PortalData.Add({FName(Id), FName(RoomId), NAME_None, Sill, Normal, Width, Height, EGate::Open, true});
        }

        // Each switchback has its own hollow enclosure. Landings are individual slabs,
        // never a full intermediate storey slab through the stair flights.
        void Stair(const TCHAR* Id, EFloor Low, EFloor High, float X, float Y, float Direction,
            float LaneDirection, float Bottom, float Top, float Run, TArray<int32> Steps)
        {
            const FString Core = FString(Id) + TEXT("Core");
            const float FarX = X + Direction * Run;
            const float OtherY = Y + LaneDirection * 340.f;
            Room(*Core, Low, FMath::Min(X, FarX) - 300, FMath::Max(X, FarX) + 300,
                FMath::Min(Y, OtherY) - 150, FMath::Max(Y, OtherY) + 150,
                Bottom, Top - Bottom + 420, ERoomKind::Stairwell, true, true, false);
            int32 TotalSteps = 0;
            for (int32 Count : Steps) TotalSteps += Count;
            const float Rise = (Top - Bottom) / TotalSteps;
            float Z = Bottom;
            FRoute Route{FName(Id), {}};
            for (int32 Index = 0; Index <= Steps.Num(); ++Index)
            {
                const bool bFar = Index % 2 != 0;
                const float EdgeX = bFar ? FarX : X;
                const float Outside = (bFar ? Direction : -Direction) * 300;
                const FString Landing = FString::Printf(TEXT("%sLanding%d"), Id, Index);
                Room(*Landing, (Z - Bottom) < (Top - Bottom) * .5f ? Low : High,
                    FMath::Min(EdgeX, EdgeX + Outside), FMath::Max(EdgeX, EdgeX + Outside),
                    FMath::Min(Y, OtherY) - 150, FMath::Max(Y, OtherY) + 150,
                    Z, 400, ERoomKind::Landing, false, false, true);
                const float CurrentY = Index % 2 == 0 ? Y : OtherY;
                if (Index == 0)
                {
                    Route.Points.Add(FVector(EdgeX + Outside * .5f, Y, Z));
                }
                else
                {
                    const float PreviousY = (Index - 1) % 2 == 0 ? Y : OtherY;
                    Route.Points.Add(FVector(EdgeX + Outside * .5f, PreviousY, Z));
                    if (Index < Steps.Num()) Route.Points.Add(FVector(EdgeX + Outside * .5f, CurrentY, Z));
                }
                if (Index == Steps.Num()) break;
                const FVector Start(EdgeX, CurrentY, Z);
                Z += Steps[Index] * Rise;
                if (Index == Steps.Num() - 1) Z = Top;
                const FVector End(bFar ? X : FarX, CurrentY, Z);
                StairData.Add({FName(*FString::Printf(TEXT("%sFlight%d"), Id, Index)), FName(*Core),
                    FName(*Landing), FName(*FString::Printf(TEXT("%sLanding%d"), Id, Index + 1)),
                    Low, High, Start, End, 300, Steps[Index], 400});
                Route.Points.Add(Start);
                Route.Points.Add(End);
            }
            RouteData.Add(MoveTemp(Route));
        }

        FLayout()
        {
            const FVector X(1, 0, 0), Y(0, 1, 0);
            Room(TEXT("DungeonCourt"), EFloor::Dungeon, -2400, 2400, -1700, 1700, -1700, 1550, ERoomKind::Combat);
            Room(TEXT("DungeonAnte"), EFloor::Dungeon, -3300, -2400, -500, 500, -1700, 650, ERoomKind::Antechamber);
            Room(TEXT("DungeonWest"), EFloor::Dungeon, -3700, -3300, -2750, 500, -1700, 420);
            Room(TEXT("CompanionCell"), EFloor::Dungeon, -3300, -2700, -1800, -1000, -1700, 360, ERoomKind::Cell);
            Room(TEXT("DungeonDownLink"), EFloor::Dungeon, -3300, -1900, -2750, -2450, -1700, 400);
            Room(TEXT("DungeonAfter"), EFloor::Dungeon, 2400, 3300, 700, 1300, -1700, 420);
            Room(TEXT("DungeonEast"), EFloor::Dungeon, 3300, 3700, -2750, 1300, -1700, 420);
            Room(TEXT("DungeonReturnLink"), EFloor::Dungeon, 1900, 3300, -2750, -2450, -1700, 400);

            // The custody branch ends north of 03; it never opens into the combat court or its post-fight exit.
            Room(TEXT("DungeonCustodyTurn"), EFloor::Dungeon, -3300, -2700, 500, 1700, -1700, 420);
            Room(TEXT("DungeonCustodyGallery"), EFloor::Dungeon, -3300, 2400, 1700, 2150, -1700, 420, ERoomKind::Gallery);
            Room(TEXT("DungeonCell01"), EFloor::Dungeon, -2700, -2050, 2150, 2720, -1700, 380, ERoomKind::Cell);
            Room(TEXT("DungeonCell02"), EFloor::Dungeon, -2050, -1400, 2150, 2720, -1700, 380, ERoomKind::Cell);
            Room(TEXT("DungeonSharedCell"), EFloor::Dungeon, -1400, 0, 2150, 2720, -1700, 420, ERoomKind::Cell);
            Room(TEXT("DungeonSideCell04"), EFloor::Dungeon, 0, 650, 2150, 2720, -1700, 360, ERoomKind::Cell);
            Room(TEXT("DungeonSideCell05"), EFloor::Dungeon, 650, 1300, 2150, 2720, -1700, 360, ERoomKind::Cell);
            Room(TEXT("DungeonSharedCell06"), EFloor::Dungeon, 1300, 2400, 2150, 2720, -1700, 420, ERoomKind::Cell);

            Room(TEXT("PrisonCell"), EFloor::Prison, -3300, -2700, 900, 1500, 0, 360, ERoomKind::Cell);
            Room(TEXT("PrisonCellPassage"), EFloor::Prison, -2700, -2300, 400, 1700, 0, 400);
            Room(TEXT("PrisonCrossing"), EFloor::Prison, -3300, -2000, -400, 400, 0, 420);
            Room(TEXT("PrisonHall"), EFloor::Prison, -2000, 2400, -1500, 1500, 0, 650, ERoomKind::Hall);
            Room(TEXT("PrisonWest"), EFloor::Prison, -3700, -3300, -2410, 2100, 0, 420);
            Room(TEXT("PrisonDownLink"), EFloor::Prison, -3300, -1900, -2410, -2110, 0, 400);
            Room(TEXT("PrisonEast"), EFloor::Prison, 3300, 3700, -2410, 2750, 0, 420);
            Room(TEXT("PrisonReturnLink"), EFloor::Prison, 1900, 3300, -2410, -2110, 0, 400);
            Room(TEXT("PrisonEastLink"), EFloor::Prison, 2400, 3300, -400, 400, 0, 420);
            Room(TEXT("PrisonNorth"), EFloor::Prison, -3300, 3300, 1700, 2100, 0, 400);
            Room(TEXT("PrisonNorthLink"), EFloor::Prison, -300, 300, 1500, 1700, 0, 400);
            Room(TEXT("PrisonChapelLink"), EFloor::Prison, 1900, 3300, 2450, 2750, 0, 400);

            Room(TEXT("ChapelHall"), EFloor::Chapel, -2400, 2400, -1700, 1700, DuelNorthArchive::FloorZ, 950, ERoomKind::Hall);
            Room(TEXT("ChapelEast"), EFloor::Chapel, 3300, 3700, -2350, 2410, 700, 420);
            Room(TEXT("ChapelArrival"), EFloor::Chapel, 1900, 3300, 2110, 2410, 700, 400);
            Room(TEXT("ChapelEastLink"), EFloor::Chapel, 2400, 3300, -400, 400, 700, 420);
            Room(TEXT("ChapelWest"), EFloor::Chapel, -3700, -3300, -2350, 2750, 700, 420);
            Room(TEXT("ChapelWestLink"), EFloor::Chapel, -3300, -2400, -400, 400, 700, 420);
            Room(TEXT("SeaGallery"), EFloor::Chapel, -2400, 2400, -2350, -1900, 700, 420, ERoomKind::Gallery);
            Room(TEXT("SeaWestLink"), EFloor::Chapel, -3300, -2400, -2350, -1900, 700, 420);
            Room(TEXT("SeaEastLink"), EFloor::Chapel, 2400, 3300, -2350, -1900, 700, 420);
            Room(TEXT("ChapelSeaLink"), EFloor::Chapel, -300, 300, -1900, -1700, 700, 420);
            Room(TEXT("ChapelMirrorLink"), EFloor::Chapel, -3300, -1900, 2450, 2750, 700, 400);

            Room(TEXT("MirrorCourt"), EFloor::Mirror, -2400, 2400, -1700, 1700, 1700, 1550, ERoomKind::Combat);
            Room(TEXT("MirrorAnte"), EFloor::Mirror, -3300, -2400, -500, 500, 1700, 650, ERoomKind::Antechamber);
            Room(TEXT("MirrorWest"), EFloor::Mirror, -3700, -3300, -500, 2410, 1700, 420);
            Room(TEXT("MirrorArrival"), EFloor::Mirror, -3300, -1900, 2110, 2410, 1700, 400);
            Room(TEXT("MirrorAfter"), EFloor::Mirror, 2400, 3300, 700, 1300, 1700, 420);
            Room(TEXT("MirrorEast"), EFloor::Mirror, 3300, 3700, -2750, 1300, 1700, 420);
            Room(TEXT("MirrorFinalLink"), EFloor::Mirror, 1900, 3300, -2750, -2450, 1700, 400);

            Room(TEXT("FinalCourt"), EFloor::Final, -2400, 2400, -1700, 1700, 3400, 1800, ERoomKind::Combat);
            Room(TEXT("FinalAnte"), EFloor::Final, -3300, -2400, -500, 500, 3400, 900, ERoomKind::Antechamber);
            Room(TEXT("FinalWest"), EFloor::Final, -3700, -3300, -2750, 500, 3400, 600);
            Room(TEXT("FinalArrival"), EFloor::Final, -3300, 280, -2750, -2450, 3400, 420);
            Room(TEXT("FinalExit"), EFloor::Final, 2400, 3300, -500, 500, 3400, 650, ERoomKind::Antechamber);
            Room(TEXT("DawnBridge"), EFloor::Garden, 3300, 5100, -250, 250, 3400, 500, ERoomKind::Gallery);
            Room(TEXT("DawnGarden"), EFloor::Garden, 5100, 7600, -1200, 1200, 3400, 1000, ERoomKind::Garden, false, false);

            Stair(TEXT("Descent"), EFloor::Dungeon, EFloor::Prison, -1600, -2600, 1, 1,
                -1700, 0, 510, {17, 17, 17, 17, 16, 16});
            Stair(TEXT("Return"), EFloor::Dungeon, EFloor::Prison, 1600, -2600, -1, 1,
                -1700, 0, 510, {17, 17, 17, 17, 16, 16});
            Stair(TEXT("ChapelStair"), EFloor::Prison, EFloor::Chapel, 1600, 2600, -1, -1,
                0, 700, 600, {20, 20});
            Stair(TEXT("MirrorStair"), EFloor::Chapel, EFloor::Mirror, -1600, 2600, 1, -1,
                700, 1700, 420, {15, 15, 14, 14});
            Stair(TEXT("FinalStair"), EFloor::Mirror, EFloor::Final, 1600, -2600, -1, 1,
                1700, 3400, 1020, {34, 33, 33});

            // The first upper-stair turn is a real landing at 1700 + 34 * 17 cm.
            constexpr float L = 2278.f;
            Room(TEXT("BeaconChartRoom"), EFloor::Beacon,-440,280,-2750,-2110,L,410,ERoomKind::Antechamber);
            Room(TEXT("BeaconGallery"), EFloor::Beacon,-2160,-440,-2720,-2290,L,410);
            Room(TEXT("BeaconWorkshop"), EFloor::Beacon,-1390,-440,-2290,-1735,L,435,ERoomKind::Cell);
            Room(TEXT("BeaconCabin"), EFloor::Beacon,-2160,-1210,-3410,-2720,L,405,ERoomKind::Cell);
            Room(TEXT("BeaconCrossway"), EFloor::Beacon,-2160,-1390,-2290,-1760,L,400,ERoomKind::Gallery);
            Room(TEXT("BeaconOptics"), EFloor::Beacon,-3200,-2160,-3010,-1750,L,470,ERoomKind::Antechamber);
            Door(TEXT("BeaconLanding"),TEXT("FinalStairCore"),TEXT("BeaconChartRoom"),{280,-2420,L},X,240,325);
            Door(TEXT("BeaconEntry"),TEXT("BeaconChartRoom"),TEXT("BeaconGallery"),{-440,-2490,L},X,245,325);
            Door(TEXT("BeaconWorkshopDoor"),TEXT("BeaconGallery"),TEXT("BeaconWorkshop"),{-910,-2290,L},Y,250,325);
            Door(TEXT("BeaconCabinDoor"),TEXT("BeaconGallery"),TEXT("BeaconCabin"),{-1660,-2720,L},Y,235,320);
            Door(TEXT("BeaconOpticsDoor"),TEXT("BeaconGallery"),TEXT("BeaconOptics"),{-2160,-2520,L},X,240,330);
            Door(TEXT("BeaconServiceDoor"),TEXT("BeaconWorkshop"),TEXT("BeaconCrossway"),{-1390,-2010,L},X,210,315);
            Door(TEXT("BeaconServiceReturn"),TEXT("BeaconCrossway"),TEXT("BeaconOptics"),{-2160,-2000,L},X,235,325);

            Door(TEXT("CellDoor"), TEXT("PrisonCell"), TEXT("PrisonCellPassage"), FVector(-2700, 1200, 0), X, 240, 320, EGate::OpeningCell);
            Door(TEXT("NameWallTurn"), TEXT("PrisonCellPassage"), TEXT("PrisonCrossing"), FVector(-2500, 400, 0), Y);
            Door(TEXT("CellShortLoop"), TEXT("PrisonCellPassage"), TEXT("PrisonNorth"), FVector(-2500, 1700, 0), Y);
            Door(TEXT("PrisonHallEntry"), TEXT("PrisonCrossing"), TEXT("PrisonHall"), FVector(-2000, 0, 0), X, 500, 400);
            Door(TEXT("PrisonWestTurn"), TEXT("PrisonCrossing"), TEXT("PrisonWest"), FVector(-3300, 0, 0), X);
            Door(TEXT("PrisonDownTurn"), TEXT("PrisonWest"), TEXT("PrisonDownLink"), FVector(-3300, -2260, 0), X);
            Door(TEXT("DescentUpper"), TEXT("PrisonDownLink"), TEXT("DescentCore"), FVector(-1900, -2260, 0), X);
            Door(TEXT("DescentLower"), TEXT("DescentCore"), TEXT("DungeonDownLink"), FVector(-1900, -2600, -1700), X);
            Door(TEXT("DungeonDownTurn"), TEXT("DungeonDownLink"), TEXT("DungeonWest"), FVector(-3300, -2600, -1700), X);
            Door(TEXT("CompanionCellDoor"), TEXT("DungeonWest"), TEXT("CompanionCell"), FVector(-3300, -1400, -1700), X, 240, 320);
            Door(TEXT("DungeonCustodyEntry"), TEXT("DungeonAnte"), TEXT("DungeonCustodyTurn"), FVector(-3000, 500, -1700), Y, 240, 330);
            Door(TEXT("DungeonCustodyCorner"), TEXT("DungeonCustodyTurn"), TEXT("DungeonCustodyGallery"), FVector(-3000, 1700, -1700), Y, 300, 360);
            Door(TEXT("DungeonCell01Door"), TEXT("DungeonCustodyGallery"), TEXT("DungeonCell01"), FVector(-2375, 2150, -1700), Y, 190, 300);
            Door(TEXT("DungeonCell02Door"), TEXT("DungeonCustodyGallery"), TEXT("DungeonCell02"), FVector(-1725, 2150, -1700), Y, 190, 300);
            Door(TEXT("DungeonSharedDoor"), TEXT("DungeonCustodyGallery"), TEXT("DungeonSharedCell"), FVector(-700, 2150, -1700), Y, 280, 320);
            Door(TEXT("DungeonSideCell04Door"), TEXT("DungeonCustodyGallery"), TEXT("DungeonSideCell04"), FVector(325, 2150, -1700), Y, 180, 290);
            Door(TEXT("DungeonSideCell05Door"), TEXT("DungeonCustodyGallery"), TEXT("DungeonSideCell05"), FVector(975, 2150, -1700), Y, 180, 290);
            Door(TEXT("DungeonShared06Door"), TEXT("DungeonCustodyGallery"), TEXT("DungeonSharedCell06"), FVector(1850, 2150, -1700), Y, 260, 320);
            Door(TEXT("DungeonAnteEntry"), TEXT("DungeonWest"), TEXT("DungeonAnte"), FVector(-3300, 0, -1700), X);
            Door(TEXT("DungeonGate"), TEXT("DungeonAnte"), TEXT("DungeonCourt"), FVector(-2400, 0, -1700), X, 600, 620, EGate::DungeonFog);
            Door(TEXT("DungeonAfterDoor"), TEXT("DungeonCourt"), TEXT("DungeonAfter"), FVector(2400, 1000, -1700), X, 400, 400, EGate::DungeonWon);
            Door(TEXT("DungeonEastTurn"), TEXT("DungeonAfter"), TEXT("DungeonEast"), FVector(3300, 1000, -1700), X);
            Door(TEXT("DungeonReturnTurn"), TEXT("DungeonEast"), TEXT("DungeonReturnLink"), FVector(3300, -2600, -1700), X);
            Door(TEXT("ReturnLower"), TEXT("DungeonReturnLink"), TEXT("ReturnCore"), FVector(1900, -2600, -1700), X, 300, 360, EGate::DungeonReturnShortcut);
            Door(TEXT("ReturnUpper"), TEXT("ReturnCore"), TEXT("PrisonReturnLink"), FVector(1900, -2260, 0), X);
            Door(TEXT("PrisonReturnTurn"), TEXT("PrisonReturnLink"), TEXT("PrisonEast"), FVector(3300, -2260, 0), X);
            Door(TEXT("PrisonEastInner"), TEXT("PrisonHall"), TEXT("PrisonEastLink"), FVector(2400, 0, 0), X, 500, 400);
            Door(TEXT("PrisonEastOuter"), TEXT("PrisonEastLink"), TEXT("PrisonEast"), FVector(3300, 0, 0), X);
            Door(TEXT("PrisonNorthWest"), TEXT("PrisonNorth"), TEXT("PrisonWest"), FVector(-3300, 1900, 0), X);
            Door(TEXT("PrisonNorthEast"), TEXT("PrisonNorth"), TEXT("PrisonEast"), FVector(3300, 1900, 0), X);
            Door(TEXT("PrisonNorthInner"), TEXT("PrisonHall"), TEXT("PrisonNorthLink"), FVector(0, 1500, 0), Y, 500, 360);
            Door(TEXT("PrisonNorthOuter"), TEXT("PrisonNorthLink"), TEXT("PrisonNorth"), FVector(0, 1700, 0), Y, 500, 360);
            Door(TEXT("PrisonChapelTurn"), TEXT("PrisonEast"), TEXT("PrisonChapelLink"), FVector(3300, 2600, 0), X);
            Door(TEXT("ChapelDoor"), TEXT("PrisonChapelLink"), TEXT("ChapelStairCore"), FVector(1900, 2600, 0), X, 300, 360, EGate::RustKey);
            Door(TEXT("ChapelUpper"), TEXT("ChapelStairCore"), TEXT("ChapelArrival"), FVector(1900, 2260, 700), X);
            Door(TEXT("ChapelArrivalTurn"), TEXT("ChapelArrival"), TEXT("ChapelEast"), FVector(3300, 2260, 700), X);
            Door(TEXT("ChapelEastOuter"), TEXT("ChapelEast"), TEXT("ChapelEastLink"), FVector(3300, 0, 700), X);
            Door(TEXT("ChapelEastInner"), TEXT("ChapelEastLink"), TEXT("ChapelHall"), FVector(2400, 0, 700), X, 500, 400);
            Door(TEXT("ChapelWestInner"), TEXT("ChapelHall"), TEXT("ChapelWestLink"), FVector(-2400, 0, 700), X, 500, 400);
            Door(TEXT("ChapelWestOuter"), TEXT("ChapelWestLink"), TEXT("ChapelWest"), FVector(-3300, 0, 700), X);
            Door(TEXT("ChapelSeaInner"), TEXT("ChapelHall"), TEXT("ChapelSeaLink"), FVector(0, -1700, 700), Y, 500, 400);
            Door(TEXT("ChapelSeaOuter"), TEXT("ChapelSeaLink"), TEXT("SeaGallery"), FVector(0, -1900, 700), Y, 500, 400);
            Door(TEXT("SeaWestInner"), TEXT("SeaGallery"), TEXT("SeaWestLink"), FVector(-2400, -2125, 700), X, 340, 400);
            Door(TEXT("SeaWestOuter"), TEXT("SeaWestLink"), TEXT("ChapelWest"), FVector(-3300, -2125, 700), X, 340, 400);
            Door(TEXT("SeaEastInner"), TEXT("SeaGallery"), TEXT("SeaEastLink"), FVector(2400, -2125, 700), X, 340, 400, EGate::ChapelLoopShortcut);
            Door(TEXT("SeaEastOuter"), TEXT("SeaEastLink"), TEXT("ChapelEast"), FVector(3300, -2125, 700), X, 340, 400);
            Window(TEXT("SeaWindowWest"), TEXT("SeaGallery"), FVector(-1400, -2350, 830), Y, 380, 240);
            Window(TEXT("SeaWindowCentre"), TEXT("SeaGallery"), FVector(0, -2350, 830), Y, 380, 240);
            Window(TEXT("SeaWindowEast"), TEXT("SeaGallery"), FVector(1400, -2350, 830), Y, 380, 240);
            // Clear the stair roofs at the lower sill and the chapel vault at the upper heads.
            Window(TEXT("PrisonSouthWindowWest"), TEXT("PrisonHall"), FVector(-915, -1500, 465), Y, 194, 132);
            Window(TEXT("PrisonSouthWindowEast"), TEXT("PrisonHall"), FVector(915, -1500, 465), Y, 194, 132);
            Window(TEXT("ChapelNorthWindowWest"), TEXT("ChapelHall"), FVector(-320, 1700, 845), Y, DuelNorthRose::ChapelRoughDiameter, DuelNorthRose::ChapelRoughDiameter);
            Window(TEXT("ChapelNorthWindowEast"), TEXT("ChapelHall"), FVector(320, 1700, 845), Y, DuelNorthRose::ChapelRoughDiameter, DuelNorthRose::ChapelRoughDiameter);
            Door(TEXT("ChapelMirrorTurn"), TEXT("ChapelWest"), TEXT("ChapelMirrorLink"), FVector(-3300, 2600, 700), X);
            Door(TEXT("MirrorLower"), TEXT("ChapelMirrorLink"), TEXT("MirrorStairCore"), FVector(-1900, 2600, 700), X);
            Door(TEXT("MirrorUpper"), TEXT("MirrorStairCore"), TEXT("MirrorArrival"), FVector(-1900, 2260, 1700), X);
            Door(TEXT("MirrorArrivalTurn"), TEXT("MirrorArrival"), TEXT("MirrorWest"), FVector(-3300, 2260, 1700), X);
            Door(TEXT("MirrorAnteEntry"), TEXT("MirrorWest"), TEXT("MirrorAnte"), FVector(-3300, 0, 1700), X);
            Door(TEXT("MirrorGate"), TEXT("MirrorAnte"), TEXT("MirrorCourt"), FVector(-2400, 0, 1700), X, 600, 620, EGate::MirrorFog);
            Door(TEXT("FinalStairDoor"), TEXT("MirrorCourt"), TEXT("MirrorAfter"), FVector(2400, 1000, 1700), X, 400, 400, EGate::MirrorKey);
            Door(TEXT("MirrorEastTurn"), TEXT("MirrorAfter"), TEXT("MirrorEast"), FVector(3300, 1000, 1700), X);
            Door(TEXT("MirrorFinalTurn"), TEXT("MirrorEast"), TEXT("MirrorFinalLink"), FVector(3300, -2600, 1700), X);
            Door(TEXT("FinalLower"), TEXT("MirrorFinalLink"), TEXT("FinalStairCore"), FVector(1900, -2600, 1700), X);
            Door(TEXT("FinalUpper"), TEXT("FinalStairCore"), TEXT("FinalArrival"), FVector(280, -2600, 3400), X);
            Door(TEXT("FinalArrivalTurn"), TEXT("FinalArrival"), TEXT("FinalWest"), FVector(-3300, -2600, 3400), X);
            Door(TEXT("FinalAnteEntry"), TEXT("FinalWest"), TEXT("FinalAnte"), FVector(-3300, 0, 3400), X, 300, 560);
            Door(TEXT("FinalGate"), TEXT("FinalAnte"), TEXT("FinalCourt"), FVector(-2400, 0, 3400), X, 600, 800, EGate::FinalFog);
            Door(TEXT("FinalExitInner"), TEXT("FinalCourt"), TEXT("FinalExit"), FVector(2400, 0, 3400), X, 500, 600, EGate::FinalBattleExit);
            for(int Side:{-1,1}) for(int Bay:{-1,1})
                Window(*FString::Printf(TEXT("FinalHighWindow%d_%d"),Side,Bay),TEXT("FinalCourt"),
                    FVector(Side*2400,Bay*450,DuelNorthRose::SillZ),X,DuelNorthRose::RoughDiameter,DuelNorthRose::RoughDiameter);
            // The four side slots sit between transverse ribs and below their spring.
            constexpr float SideWindowSpan = 340.f;
            for (int Side : {-1, 1}) for (int Bay : {-1, 1})
                Window(*FString::Printf(TEXT("FinalSideWindow%d_%d"), Side, Bay), TEXT("FinalCourt"),
                    FVector(Bay * 1260, Side * 1700, 3670), Y, SideWindowSpan, SideWindowSpan);
            Door(TEXT("TowerExit"), TEXT("FinalExit"), TEXT("DawnBridge"), FVector(3300, 0, 3400), X, 360, 440, EGate::AllTokens);
            Door(TEXT("GardenEntry"), TEXT("DawnBridge"), TEXT("DawnGarden"), FVector(5100, 0, 3400), X, 420, 440);

            CheckpointData = {
                {TEXT("PrisonRest"), EFloor::Prison, FVector(-1000, -600, 0)},
                {TEXT("DungeonRest"), EFloor::Dungeon, FVector(-2850, -100, -1700)},
                {TEXT("ChapelRest"), EFloor::Chapel, FVector(435, 560, 700)},
                {TEXT("MirrorRest"), EFloor::Mirror, FVector(-2850, -100, 1700)},
                {TEXT("FinalRest"), EFloor::Final, FVector(-2850, -100, 3400)}
            };
            LandmarkData = {
                {TEXT("OpeningDocket"), EFloor::Prison, FVector(-2910, 1405, 0)},
                {TEXT("CellDoor"), EFloor::Prison, FVector(-2820, 1200, 0)},
                {TEXT("CellEcho"), EFloor::Prison, FVector(-3180, 1230, 0)},
                {TEXT("NameWall"), EFloor::Prison, FVector(-2500, 1550, 0)},
                {TEXT("LongTable"), EFloor::Prison, FVector(300, 450, 0)},
                {TEXT("RefugeLedger"), EFloor::Prison, FVector(-1420, 2050, 0)},
                {TEXT("DungeonGate"), EFloor::Dungeon, FVector(-2550, 0, -1700)},
                {TEXT("CellVerdict"), EFloor::Dungeon, FVector(-3450, -1400, -1700)},
                {TEXT("WaterGateLog"), EFloor::Dungeon, FVector(-3000, -1400, -1700)},
                {TEXT("WaterGateEcho"), EFloor::Dungeon, FVector(-3215, -1180, -1700)},
                {TEXT("RescuedCompanion"), EFloor::Dungeon, FVector(2070, 1035, -1700)},
                {TEXT("RustKey"), EFloor::Dungeon, FVector(1550, 450, -1700)},
                {TEXT("ReturnLever"), EFloor::Dungeon, FVector(2070, -2600, -1700)},
                {TEXT("ChapelDoor"), EFloor::Prison, FVector(2050, 2600, 0)},
                {TEXT("ChapelJudgments"), EFloor::Chapel, FVector(0, 650, 700)},
                {TEXT("ArchiveConversation"), EFloor::Chapel, FVector(360, 1040, 700)},
                {TEXT("SealPrototype"), EFloor::Chapel, FVector(-2810, 340, 700)},
                {TEXT("SeaBench"), EFloor::Chapel, FVector(1400, -2125, 700)},
                {TEXT("SeaEastLever"), EFloor::Chapel, FVector(2230, -2125, 700)},
                {TEXT("SeaMark"), EFloor::Chapel, FVector(-1400, -2125, 700)},
                {TEXT("OwnVerdict"), EFloor::Mirror, FVector(-2800, 250, 1700)},
                {TEXT("MirrorGate"), EFloor::Mirror, FVector(-2550, 0, 1700)},
                {TEXT("Signature"), EFloor::Mirror, FVector(1500, 400, 1700)},
                {TEXT("MirrorKey"), EFloor::Mirror, FVector(1500, -150, 1700)},
                {TEXT("MirrorExitCompanion"), EFloor::Mirror, FVector(2040, 1050, 1700)},
                {TEXT("FinalStairDoor"), EFloor::Mirror, FVector(2250, 1000, 1700)},
                {TEXT("FinalGate"), EFloor::Final, FVector(-2550, 0, 3400)},
                {TEXT("BeaconChart"), EFloor::Beacon, FVector(-130,-2220,2278)},
                {TEXT("LensWorkshop"), EFloor::Beacon, FVector(-830,-1860,2278)},
                {TEXT("BeaconLetter"), EFloor::Beacon, FVector(-1750,-3270,2278)},
                {TEXT("BeaconSwitch"), EFloor::Beacon, FVector(-2890,-2330,2278)},
                {TEXT("FinalWarning"), EFloor::Final, FVector(-2870, 295, 3400)},
                {TEXT("NameTag"), EFloor::Final, FVector(1450, 0, 3400)},
                {TEXT("FinalSeal"), EFloor::Final, FVector(1800, 700, 3400)},
                {TEXT("FinalExitCompanion"), EFloor::Final, FVector(2080, 325, 3400)},
                {TEXT("ReleaseOrder"), EFloor::Final, FVector(2800, -420, 3400)},
                {TEXT("TowerExit"), EFloor::Final, FVector(3150, 0, 3400)},
                {TEXT("GardenCompanion"), EFloor::Garden, FVector(5500, 350, 3400)},
                {TEXT("GardenEdge"), EFloor::Garden, FVector(7150, 0, 3400)}
            };

            for (const auto& Volume : DuelNorthArchive::Volumes)
                LandmarkData.Add({Volume.Id, EFloor::Chapel, Volume.Position()});

            FRoute Main{TEXT("MainWalk"), {
                FVector(-3000,1200,0), FVector(-2500,1200,0), FVector(-2500,1550,0), FVector(-2500,0,0),
                FVector(-1000,0,0), FVector(300,450,0), FVector(-1000,0,0), FVector(-1000,-600,0), FVector(-2500,0,0),
                FVector(-3500,0,0), FVector(-3500,-2260,0), FVector(-1750,-2260,0)}};
            auto AppendStair = [&](const TCHAR* Id, bool bReverse)
            {
                const FRoute* Route = RouteData.FindByPredicate([&](const FRoute& Value) { return Value.Id == FName(Id); });
                check(Route);
                for (int32 I = 0; I < Route->Points.Num(); ++I)
                {
                    const FVector Point = Route->Points[bReverse ? Route->Points.Num() - 1 - I : I];
                    if (!Main.Points.Last().Equals(Point, .1f)) Main.Points.Add(Point);
                }
            };
            auto Add = [&](std::initializer_list<FVector> Points) { for (const FVector& Point : Points) Main.Points.Add(Point); };
            AppendStair(TEXT("Descent"), true);
            Add({FVector(-3500,-2600,-1700), FVector(-3500,-1400,-1700), FVector(-3000,-1400,-1700),
                FVector(-3500,-1400,-1700), FVector(-3500,0,-1700), FVector(-2850,0,-1700),
                FVector(-2300,0,-1700), FVector(0,0,-1700), FVector(850,-500,-1700),
                FVector(850,-100,-1700), FVector(1550,450,-1700),
                FVector(2100,1000,-1700), FVector(2700,1000,-1700), FVector(3500,1000,-1700),
                FVector(3500,-2600,-1700), FVector(1750,-2600,-1700)});
            AppendStair(TEXT("Return"), false);
            Add({FVector(3500,-2260,0), FVector(3500,2600,0), FVector(1750,2600,0)});
            AppendStair(TEXT("ChapelStair"), false);
            Add({FVector(3500,2260,700), FVector(3500,0,700), FVector(2200,0,700), FVector(0,0,700),
                FVector(0,650,700), FVector(0,0,700), FVector(0,-2125,700), FVector(1400,-2125,700),
                FVector(-1400,-2125,700),
                FVector(-3500,-2125,700), FVector(-3500,2600,700), FVector(-1750,2600,700)});
            AppendStair(TEXT("MirrorStair"), false);
            Add({FVector(-3500,2260,1700), FVector(-3500,0,1700), FVector(-2800,0,1700),
                FVector(-2800,250,1700), FVector(-2800,0,1700),
                FVector(-2300,0,1700), FVector(0,0,1700), FVector(1500,400,1700),
                FVector(1500,-150,1700), FVector(2100,1000,1700), FVector(2700,1000,1700),
                FVector(3500,1000,1700), FVector(3500,-2600,1700), FVector(1750,-2600,1700)});
            AppendStair(TEXT("FinalStair"), false);
            Add({FVector(-3500,-2600,3400), FVector(-3500,0,3400), FVector(-2850,0,3400),
                FVector(-2300,0,3400), FVector(0,0,3400), FVector(1450,0,3400), FVector(1800,700,3400),
                FVector(2100,0,3400), FVector(2700,0,3400),
                FVector(4000,0,3400), FVector(5000,0,3400), FVector(5500,180,3400), FVector(7150,0,3400)});
            // A physical inspection route exercises both corridor turns and every cell entrance.
            RouteData.Add({TEXT("CustodyWalk"), {
                FVector(-3000,0,-1700), FVector(-3000,1900,-1700),
                FVector(-2375,1900,-1700), FVector(-2375,2400,-1700), FVector(-2375,1900,-1700),
                FVector(-1725,1900,-1700), FVector(-1725,2400,-1700), FVector(-1725,1900,-1700),
                FVector(-700,1900,-1700), FVector(-700,2400,-1700), FVector(-700,1900,-1700),
                FVector(325,1900,-1700), FVector(325,2400,-1700), FVector(325,1900,-1700),
                FVector(975,1900,-1700), FVector(975,2400,-1700), FVector(975,1900,-1700),
                FVector(1850,1900,-1700), FVector(1850,2400,-1700)}});
            RouteData.Add(MoveTemp(Main));
        }
    };

    const FLayout& Data()
    {
        static const FLayout Value;
        return Value;
    }
}

const TArray<FRoom>& Rooms() { return Data().RoomData; }
TConstArrayView<FCustodyLabel> CustodyLabels()
{
    // Keep saved room IDs stable; visible numbers follow the route from the antechamber.
    static const FCustodyLabel Labels[] = {
        {TEXT("DungeonCell01"), TEXT("DungeonCell01Door"), TEXT("04"), TEXT("04号普通牢房")},
        {TEXT("DungeonCell02"), TEXT("DungeonCell02Door"), TEXT("05"), TEXT("05号普通牢房")},
        {TEXT("DungeonSharedCell"), TEXT("DungeonSharedDoor"), TEXT("06"), TEXT("06号多人羁押室")},
        {TEXT("DungeonSideCell04"), TEXT("DungeonSideCell04Door"), TEXT("07"), TEXT("07号副牢房")},
        {TEXT("DungeonSideCell05"), TEXT("DungeonSideCell05Door"), TEXT("08"), TEXT("08号副牢房")},
        {TEXT("DungeonSharedCell06"), TEXT("DungeonShared06Door"), TEXT("09"), TEXT("09号集体牢房")}
    };
    return MakeArrayView(Labels);
}
const TArray<FStairFlight>& Stairs() { return Data().StairData; }
const TArray<FPortal>& Portals() { return Data().PortalData; }
const TArray<FCheckpoint>& Checkpoints() { return Data().CheckpointData; }
const TArray<FRoute>& Routes() { return Data().RouteData; }
const TArray<FLandmark>& Landmarks() { return Data().LandmarkData; }
const FRoom* FindRoom(FName Id) { return Rooms().FindByPredicate([&](const FRoom& Room) { return Room.Id == Id; }); }

FVector WindowOutward(const FPortal& Window, const FRoom& Room)
{
    return Window.Normal * FMath::Sign(FVector::DotProduct(Window.Center - Room.Bounds.GetCenter(), Window.Normal));
}

bool IsExteriorWindow(const FPortal& Window)
{
    if (!Window.bWindow || !Window.RoomB.IsNone() || Window.Gate != EGate::Open ||
        Window.Width <= 0 || Window.Height <= 0) return false;
    const FRoom* Source = FindRoom(Window.RoomA);
    if (!Source || !Source->bEnclose) return false;

    const int32 Axis = FMath::Abs(Window.Normal.X) > .5 ? 0 : 1;
    const int32 Along = 1 - Axis;
    if (!FMath::IsNearlyEqual(FMath::Abs(Window.Normal[Axis]), 1.) ||
        !FMath::IsNearlyZero(Window.Normal[Along]) || !FMath::IsNearlyZero(Window.Normal.Z)) return false;
    const double Out = WindowOutward(Window, *Source)[Axis];
    const double Face = Out > 0 ? Source->Bounds.Max[Axis] : Source->Bounds.Min[Axis];
    const double MinU = Window.Center[Along] - Window.Width * .5;
    const double MaxU = Window.Center[Along] + Window.Width * .5;
    const double Sill = Window.Center.Z, Head = Sill + Window.Height;
    if (FMath::Abs(Window.Center[Axis] - Face) > 1. || MinU < Source->Bounds.Min[Along] ||
        MaxU > Source->Bounds.Max[Along] || Sill < Source->Bounds.Min.Z || Head > Source->Bounds.Max.Z) return false;

    const double CentreZ = (Sill + Head) * .5;
    const double FaceHalfHeight = DuelNorthRose::IsRound(Window) ? DuelNorthRose::ApertureRadius(Window) : Window.Height * .5;
    // A connected room must clear the whole opening. Beyond a setback, test for outside sky above the lower roofs.
    const auto BlocksOpening = [&](const FBox& Bounds)
    {
        const double Near = ((Out > 0 ? Bounds.Min[Axis] : Bounds.Max[Axis]) - Face) * Out;
        const double Far = ((Out > 0 ? Bounds.Max[Axis] : Bounds.Min[Axis]) - Face) * Out;
        if (Far <= 1. || Bounds.Min[Along] >= MaxU || Bounds.Max[Along] <= MinU) return false;
        if (Near <= WallThickness)
            return Bounds.Min.Z < CentreZ + FaceHalfHeight && Bounds.Max.Z > CentreZ - FaceHalfHeight;
        return Bounds.Min.Z < CentreZ + Far * ExteriorSightlineSlope && Bounds.Max.Z > CentreZ + Near * ExteriorSightlineSlope;
    };
    const FVector WallExtent(WallThickness * .5, WallThickness * .5, 0);
    for (const FRoom& Other : Rooms())
    {
        if (&Other == Source) continue;
        const FBox& B = Other.Bounds;
        if (Other.bEnclose && BlocksOpening(B.ExpandBy(WallExtent))) return false;
        if (Other.bFloor && BlocksOpening(FBox(B.Min - FVector(0, 0, FloorThickness), FVector(B.Max.X, B.Max.Y, B.Min.Z)))) return false;
        if (Other.bRoof && BlocksOpening(FBox(FVector(B.Min.X, B.Min.Y, B.Max.Z), B.Max + FVector(0, 0, RoofThickness)))) return false;
    }
    return true;
}
const FRoute* FindRoute(FName Id) { return Routes().FindByPredicate([&](const FRoute& Route) { return Route.Id == Id; }); }
const FLandmark* FindLandmark(FName Id) { return Landmarks().FindByPredicate([&](const FLandmark& Mark) { return Mark.Id == Id; }); }
FVector CourtOrigin(int32 FightIndex) { return FVector(0, 0, FightIndex == 0 ? -1700 : FightIndex == 1 ? 1700 : 3400); }
FVector SpawnSurface() { return FVector(-3000, 1200, 0); }
FBox TowerBounds() { return FBox(FVector(-3780,-3500,-1760), FVector(3780,3500,5300)); }

const TCHAR* FloorName(EFloor Floor)
{
    switch (Floor)
    {
    case EFloor::Dungeon: return TEXT("B1 地下禁闭区");
    case EFloor::Prison: return TEXT("1F 牢房与生活区");
    case EFloor::Chapel: return TEXT("2F 档案室");
    case EFloor::Mirror: return TEXT("3F 镜厅");
    case EFloor::Final: return TEXT("4F 终审厅");
    case EFloor::Garden: return TEXT("海崖月庭");
    case EFloor::Beacon: return TEXT("监狱办公区");
    default: return TEXT("北塔楼梯");
    }
}

FLocation Locate(const FVector& ActorPosition, float CapsuleHalfHeight)
{
    const FVector Foot = ActorPosition - FVector(0,0,CapsuleHalfHeight);
    FLocation Result;
    double Best = TNumericLimits<double>::Max();
    for (const FStairFlight& Flight : Stairs())
    {
        const FVector Axis = (Flight.End - Flight.Start).GetSafeNormal2D();
        const FVector From = Foot - Flight.Start;
        const double Along = FVector::DotProduct(From, Axis);
        const double Run = FVector::Dist2D(Flight.Start, Flight.End);
        const FVector Side(-Axis.Y, Axis.X, 0);
        if (Along < -5 || Along > Run + 5 || FMath::Abs(FVector::DotProduct(From, Side)) > Flight.Width * .5f + 5) continue;
        const double Surface = FMath::Lerp(Flight.Start.Z, Flight.End.Z, FMath::Clamp(Along / Run, 0., 1.));
        const double Error = Foot.Z - Surface;
        if (Error < -22 || Error > 220 || FMath::Abs(Error) >= Best) continue;
        const FRoom* Core = FindRoom(Flight.CoreRoom);
        const double Mid = Core ? (Core->Bounds.Min.Z + Core->Bounds.Max.Z - 420) * .5 : Surface;
        Result = {Surface < Mid ? Flight.FromFloor : Flight.ToFloor, Flight.CoreRoom, Flight.Id, static_cast<float>(Surface), true};
        Best = FMath::Abs(Error);
    }
    for (const FRoom& Room : Rooms())
    {
        if (!Room.bFloor || Foot.X < Room.Bounds.Min.X - 1 || Foot.X > Room.Bounds.Max.X + 1 ||
            Foot.Y < Room.Bounds.Min.Y - 1 || Foot.Y > Room.Bounds.Max.Y + 1) continue;
        const double Error = Foot.Z - Room.Bounds.Min.Z;
        if (Error < -22 || Error > 220 || FMath::Abs(Error) >= Best) continue;
        Result = {Room.Floor, Room.Id, NAME_None, static_cast<float>(Room.Bounds.Min.Z), false};
        Best = FMath::Abs(Error);
    }
    return Result;
}


const TArray<FShortcutLever>& ShortcutLevers()
{
    static const TArray<FShortcutLever> Levers{
        {TEXT("ReturnLever"), TEXT("ReturnLower"), TEXT("DungeonReturnLink"), EFloor::Dungeon,
            FVector(2070, -2600, -1700), FVector(1,0,0), 0},
        {TEXT("SeaEastLever"), TEXT("SeaEastInner"), TEXT("SeaGallery"), EFloor::Chapel,
            FVector(2230, -2125, 700), FVector(-1,0,0), 1}
    };
    return Levers;
}
const FShortcutLever* FindShortcutLever(FName Id)
{
    return ShortcutLevers().FindByPredicate([&](const FShortcutLever& Lever) { return Lever.Id == Id; });
}
bool CanReachShortcutLever(const FShortcutLever& Lever, const FVector& ActorPosition, float CapsuleHalfHeight)
{
    const auto* Portal = Portals().FindByPredicate([&](const FPortal& P) { return P.Id == Lever.Portal; });
    return Portal && FVector::DistSquared2D(ActorPosition, Lever.Position) < FMath::Square(220.f) &&
        FMath::Abs(ActorPosition.Z - CapsuleHalfHeight - Lever.Position.Z) < 100.f &&
        FVector::DotProduct(ActorPosition - Portal->Center, Lever.ApproachDirection) >= 50.f &&
        Locate(ActorPosition, CapsuleHalfHeight).Room == Lever.ApproachRoom;
}

bool Validate(TArray<FString>& Errors)
{
    auto Fail = [&](const FString& Message) { Errors.Add(Message); };
    TSet<FName> Names;
    for (const FRoom& Room : Rooms())
    {
        if (Names.Contains(Room.Id)) Fail(FString::Printf(TEXT("Duplicate room %s"), *Room.Id.ToString()));
        Names.Add(Room.Id);
        const FVector Size = Room.Bounds.GetSize();
        if (Size.X <= 0 || Size.Y <= 0 || Size.Z < 320) Fail(FString::Printf(TEXT("Invalid room dimensions %s"), *Room.Id.ToString()));
        if (Room.Kind == ERoomKind::Stairwell && Room.bFloor) Fail(TEXT("A stair core must not have a full floor slab"));
    }
    for (const FStairFlight& Flight : Stairs())
    {
        if (Flight.StepCount <= 0 || Flight.Riser() <= 0 || Flight.Riser() > 18.01f || Flight.Tread() < 27.99f || Flight.Tread() > 32.01f || Flight.Width < 280)
            Fail(FString::Printf(TEXT("Unsafe stair proportions %s"), *Flight.Id.ToString()));
        if (!FindRoom(Flight.CoreRoom) || !FindRoom(Flight.FromLanding) || !FindRoom(Flight.ToLanding)) Fail(TEXT("Stair references a missing core or landing"));
        if (FMath::Abs(Flight.Start.X-Flight.End.X) < 1 || FMath::Abs(Flight.Start.Y-Flight.End.Y) > 1) Fail(TEXT("Flight should be axis aligned along X"));
    }
    for (const FPortal& Portal : Portals())
    {
        if (Portal.bWindow)
        {
            if (!IsExteriorWindow(Portal)) Fail(FString::Printf(TEXT("Window %s must face unobstructed exterior space"), *Portal.Id.ToString()));
            continue;
        }
        for (FName Id : {Portal.RoomA, Portal.RoomB})
        {
            const FRoom* Room = FindRoom(Id);
            if (!Room) { Fail(FString::Printf(TEXT("Portal %s missing room %s"), *Portal.Id.ToString(), *Id.ToString())); continue; }
            const FBox& B = Room->Bounds;
            const bool bX = FMath::Abs(Portal.Normal.X) > .5;
            const float V = bX ? Portal.Center.X : Portal.Center.Y;
            const float VMin = bX ? B.Min.X : B.Min.Y, VMax = bX ? B.Max.X : B.Max.Y;
            const float U = bX ? Portal.Center.Y : Portal.Center.X;
            const float UMin = bX ? B.Min.Y : B.Min.X, UMax = bX ? B.Max.Y : B.Max.X;
            if ((FMath::Abs(V - VMin) > 1 && FMath::Abs(V - VMax) > 1) || U - Portal.Width*.5 < UMin - 1 ||
                U + Portal.Width*.5 > UMax + 1 || Portal.Center.Z < B.Min.Z - 1 || Portal.Center.Z + Portal.Height > B.Max.Z + 1)
                Fail(FString::Printf(TEXT("Portal %s does not fit shared boundary of %s"), *Portal.Id.ToString(), *Id.ToString()));
        }
    }
    for (const FRoute& Route : Routes())
    {
        for (int32 I=1; I<Route.Points.Num(); ++I)
        {
            const FVector A=Route.Points[I-1], B=Route.Points[I];
            const int32 Samples=FMath::Max(1,FMath::CeilToInt(FVector::Dist(A,B)/50.f));
            for (int32 J=0; J<=Samples; ++J)
            {
                const FVector Point=FMath::Lerp(A,B,static_cast<float>(J)/Samples);
                if (Locate(Point,0).Floor==EFloor::Unknown)
                {
                    Fail(FString::Printf(TEXT("Route %s leaves a walking surface at segment %d: %s"),*Route.Id.ToString(),I,*Point.ToString()));
                    break;
                }
            }
            // A connected set of floor rectangles is not enough: every wall crossing
            // must have an actual door wide and tall enough for the current capsule.
            for (const FRoom& Room : Rooms())
            {
                if (!Room.bEnclose) continue;
                for (int32 Axis=0; Axis<2; ++Axis)
                {
                    const double Delta=B[Axis]-A[Axis];
                    if (FMath::Abs(Delta)<.01) continue;
                    for (double Face : {Room.Bounds.Min[Axis],Room.Bounds.Max[Axis]})
                    {
                        const double T=(Face-A[Axis])/Delta;
                        if (T<0 || T>1) continue;
                        const FVector At=FMath::Lerp(A,B,T);
                        const int32 Other=1-Axis;
                        if (At[Other]<Room.Bounds.Min[Other]-.1 || At[Other]>Room.Bounds.Max[Other]+.1 ||
                            At.Z<Room.Bounds.Min.Z-1 || At.Z>Room.Bounds.Max.Z-1) continue;
                        const bool bDoor=Portals().ContainsByPredicate([&](const FPortal& Door)
                        {
                            return !Door.bWindow && (Door.RoomA==Room.Id || Door.RoomB==Room.Id) &&
                                FMath::Abs(Door.Normal[Axis])>.5 && FMath::Abs(Door.Center[Axis]-Face)<1 &&
                                FMath::Abs(Door.Center[Other]-At[Other])+42 <= Door.Width*.5+1 &&
                                At.Z>=Door.Center.Z-22 && At.Z+124<=Door.Center.Z+Door.Height+1;
                        });
                        if (!bDoor) Fail(FString::Printf(TEXT("Route %s crosses an uncut wall of %s at %s"),
                            *Route.Id.ToString(),*Room.Id.ToString(),*At.ToString()));
                    }
                }
            }
        }
    }
    for (const FCheckpoint& Checkpoint : Checkpoints())
        if (Locate(Checkpoint.Position,0).Floor!=Checkpoint.Floor) Fail(FString::Printf(TEXT("Checkpoint outside its floor: %s"),*Checkpoint.Id.ToString()));
    for (const FLandmark& Mark : Landmarks())
        if (Locate(Mark.Position,0).Floor!=Mark.Floor) Fail(FString::Printf(TEXT("Landmark outside its floor: %s"),*Mark.Id.ToString()));
    for (const FShortcutLever& Lever : ShortcutLevers())
    {
        const FPortal* Portal = Portals().FindByPredicate([&](const FPortal& P) { return P.Id == Lever.Portal; });
        const FLandmark* Mark = FindLandmark(Lever.Id);
        if (!Portal || !Mark || !Mark->Position.Equals(Lever.Position, .1f) || Mark->Floor != Lever.Floor ||
            Portal->Gate != (Lever.ProgressIndex == 0 ? EGate::DungeonReturnShortcut : EGate::ChapelLoopShortcut) ||
            !CanReachShortcutLever(Lever, Lever.Position + FVector(0,0,62)))
            Fail(FString::Printf(TEXT("Invalid one-sided shortcut %s"), *Lever.Id.ToString()));
    }
    return Errors.IsEmpty();
}
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthLayoutTest, "ManosabaDuel.NorthTower.Layout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDuelNorthLayoutTest::RunTest(const FString& Parameters)
{
    using namespace DuelNorthLayout;
    TArray<FString> Errors;
    const bool bValid=Validate(Errors);
    for (const FString& Error : Errors) AddError(Error);
    TestTrue(TEXT("Layout, stair dimensions, doors and walk route"),bValid);
    TestTrue(TEXT("Dungeon at shared XY"),Locate(FVector(0,0,-1638)).Floor==EFloor::Dungeon);
    TestTrue(TEXT("Prison at shared XY"),Locate(FVector(0,0,62)).Floor==EFloor::Prison);
    TestTrue(TEXT("Chapel at shared XY"),Locate(FVector(0,0,762)).Floor==EFloor::Chapel);
    TestTrue(TEXT("Mirror at shared XY"),Locate(FVector(0,0,1762)).Floor==EFloor::Mirror);
    TestTrue(TEXT("Final at shared XY"),Locate(FVector(0,0,3462)).Floor==EFloor::Final);
    TestTrue(TEXT("Void must not masquerade as a floor"),Locate(FVector(9000,9000,62)).Floor==EFloor::Unknown);
    TestEqual(TEXT("One checkpoint on every tower floor"),Checkpoints().Num(),5);
    TestEqual(TEXT("Independent descent/return plus three ascents"),Stairs().Num(),21);
    TestEqual(TEXT("Two independently persistent shortcut levers"), ShortcutLevers().Num(), 2);
    const FPortal* PrisonSlot = Portals().FindByPredicate([](const FPortal& P) { return P.Id == TEXT("PrisonSouthWindowEast"); });
    if (TestNotNull(TEXT("The prison hall has an exposed south slot"), PrisonSlot))
    {
        TestTrue(TEXT("The south slot clears the stair roofs"), IsExteriorWindow(*PrisonSlot));
        FPortal Masked = *PrisonSlot;
        Masked.Center = FVector(0, 1500, 395);
        TestFalse(TEXT("Even an adjoining corridor roof masking only the sill disqualifies the slot"), IsExteriorWindow(Masked));
        Masked.Center = FVector(1200, 1500, 470);
        Masked.Width = 180;
        TestFalse(TEXT("The former elevated north slot faces a setback stair room"), IsExteriorWindow(Masked));
        Masked.Center = FVector(0, 1500, 100);
        TestFalse(TEXT("A room immediately across the face disqualifies an opening"), IsExteriorWindow(Masked));
    }
    const FName Cell(TEXT("PrisonCell"));
    const auto* Docket = FindLandmark(TEXT("OpeningDocket"));
    const auto* DoorHandle = FindLandmark(TEXT("CellDoor"));
    const auto* Door = Portals().FindByPredicate([](const FPortal& P) { return P.Id == TEXT("CellDoor"); });
    TestTrue(TEXT("The spawn, opening docket and cell-door interaction all lie inside the same cell"),
        Docket && DoorHandle && Locate(SpawnSurface() + FVector(0,0,62)).Room == Cell &&
        Locate(Docket->Position + FVector(0,0,62)).Room == Cell && Locate(DoorHandle->Position + FVector(0,0,62)).Room == Cell);
    TestTrue(TEXT("The cell's sole passage uses the starting-cell lock"),
        Door && Door->Gate == EGate::OpeningCell && Door->RoomA == Cell &&
        Portals().FilterByPredicate([&](const FPortal& P)
        { return !P.bWindow && (P.RoomA == Cell || P.RoomB == Cell); }).Num() == 1);
    for (const FShortcutLever& Lever : ShortcutLevers())
    {
        const auto* Portal = Portals().FindByPredicate([&](const FPortal& P) { return P.Id == Lever.Portal; });
        if (!TestNotNull(TEXT("Shortcut has a real portal"), Portal)) return false;
        const FVector At = Lever.Position + FVector(0,0,62);
        TestTrue(TEXT("Approach-side capsule can operate lever"), CanReachShortcutLever(Lever, At));
        TestTrue(TEXT("Current taller capsule is measured at its actual feet"), CanReachShortcutLever(Lever, Lever.Position + FVector(0,0,101), 101));
        TestFalse(TEXT("Opposite side of closed gate cannot operate lever"), CanReachShortcutLever(Lever,
            Portal->Center - Lever.ApproachDirection * 60 + FVector(0,0,62)));
        TestFalse(TEXT("Too far from the handle cannot operate lever"), CanReachShortcutLever(Lever, At + Lever.ApproachDirection * 230));
        TestFalse(TEXT("Another storey cannot operate lever"), CanReachShortcutLever(Lever, At + FVector(0,0,700)));
    }
    return bValid;
}
#endif
