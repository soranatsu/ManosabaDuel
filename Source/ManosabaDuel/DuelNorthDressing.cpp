#include "DuelNorthDressing.h"

#include "DuelGeometry.h"
#include "DuelNorthArchive.h"
#include "DuelNorthFinds.h"
#include "DuelNorthInteractables.h"
#include "DuelNorthLayout.h"
#include "Algo/Find.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/BoxComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "DuelGame.h"
#include "DuelNorthTower.h"
#include "Components/CapsuleComponent.h"
#include "Components/MeshComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#endif

namespace DuelNorthDressing
{
namespace
{
    using namespace DuelNorthLayout;
    using DuelGeometry::EShape;

    constexpr double WalkClearance = 114.;
    constexpr double PassageClearance = 95.;
    constexpr double CombatClearX = 2050.;
    constexpr double CombatClearY = 1450.;
    constexpr double WallFaceClearance = .35;
    const FName DressingTag(TEXT("NorthRoomDressing"));
    const FName WaterTag(TEXT("NorthStillWater"));
    const FName FurnitureTag(TEXT("NorthStandingFurniture"));
#if WITH_DEV_AUTOMATION_TESTS
    constexpr TCHAR ObjectCountPrefix[] = TEXT("NorthDressingObjects=");
#endif
    enum ESurface
    {
        Oak, DarkOak, Iron, Brass, Stone, Paper, Wax, Flame, Water, Clay, Leather,
        CourtSlate, Linen, MirrorGlass, WineCloth, BlueCloth, ChippedStone, DampStone, LiveFlame, Crust, OliveCloth, SurfaceCount
    };
    enum class EKit : uint8
    {
        Irons, Storage, Archive, Votive, Shelf, Garden, Bookcase, Reliquary, Cot, WallLedge,
        ReadingBay, Bindery, GardenBorder, CourtStation, MessTable, WashBench, LibraryStack,
        StandingMirror, MirrorDais, JudgmentDais, Tapestry, Drain, SortingCart, DampMasonry, BrokenMasonry,
        ToolBench, MortarTrough, NicheCache, LinenCare, ProvisionTable, FolioDesk, SealedCabinet,
        KitchenHearth, PantryRack, WorkBunk, PassageHooks, SealDesk, Conservation, CatalogCabinet,
        PumpGear, CellSeat, DutyBoard, WayPlate, DocumentRail, GlassRack, MourningHanging, TribunalScale,
        GardenSeat, BerthChain, HoldingCellSeat, MirrorVeil
    };

    struct FSite
    {
        FName Room;
        FVector2D At;
        float Yaw; // Local X follows the wall and local +Y points into the room.
        EKit Kit;
        double Height = 0.;
        const DuelNorthArchive::FVolume* Volume = nullptr;
    };
    const FSite Sites[] = {
        // The dungeon has two collection points and a separate restraint bay.
        {TEXT("DungeonCourt"), {-2225, -1300}, -82, EKit::ToolBench},
        {TEXT("DungeonCourt"), {230, 1575}, 173, EKit::NicheCache},
        {TEXT("DungeonCourt"), {2225, -1300}, 83, EKit::MortarTrough},
        {TEXT("DungeonCourt"), {-1640, -1665}, 0, EKit::Drain},
        {TEXT("DungeonCourt"), {-1345, 1655}, 176, EKit::Storage},
        {TEXT("DungeonCourt"), {-1010, -1697}, 0, EKit::Irons},
        {TEXT("DungeonCourt"), {-830, -1695}, 0, EKit::DutyBoard},
        {TEXT("DungeonCourt"), {-1900, 1695}, 180, EKit::Irons},
        {TEXT("DungeonCourt"), {1435, 1655}, 180, EKit::Drain},
        {TEXT("DungeonCourt"), {1680, 1660}, 176, EKit::Storage},
        {TEXT("DungeonAnte"), {-3105, -499}, 0, EKit::WallLedge},
        {TEXT("DungeonWest"), {-3698, -1730}, -90, EKit::NicheCache},
        {TEXT("DungeonEast"), {3698, 380}, 90, EKit::WallLedge},
        {TEXT("CompanionCell"), {-3100, -1001}, 180, EKit::Irons},
        {TEXT("CompanionCell"), {-3145, -1797}, 0, EKit::Cot},
        {TEXT("DungeonAfter"), {2905, 1299}, 180, EKit::Shelf},

        // Single cells use one berth; shared cells use separated bunks and their own wash/storage corner.
        {TEXT("DungeonCell01"), {-2520, 2718}, 180, EKit::Cot},
        {TEXT("DungeonCell01"), {-2052, 2460}, 90, EKit::CellSeat},
        {TEXT("DungeonCell02"), {-1870, 2718}, 180, EKit::Cot},
        {TEXT("DungeonCell02"), {-1402, 2460}, 90, EKit::Irons},
        {TEXT("DungeonSharedCell"), {-1210, 2718}, 180, EKit::WorkBunk},
        {TEXT("DungeonSharedCell"), {-740, 2718}, 180, EKit::WorkBunk},
        {TEXT("DungeonSharedCell"), {-265, 2718}, 180, EKit::WorkBunk},
        {TEXT("DungeonSharedCell"), {-1398, 2420}, -90, EKit::WashBench},
        {TEXT("DungeonSharedCell"), {-2, 2390}, 90, EKit::PantryRack},
        {TEXT("DungeonSideCell04"), {170, 2718}, 180, EKit::Cot},
        {TEXT("DungeonSideCell04"), {648, 2460}, 90, EKit::HoldingCellSeat},
        {TEXT("DungeonSideCell05"), {820, 2718}, 180, EKit::Cot},
        {TEXT("DungeonSideCell05"), {1298, 2460}, 90, EKit::HoldingCellSeat},
        {TEXT("DungeonSharedCell06"), {1480, 2718}, 180, EKit::WorkBunk},
        {TEXT("DungeonSharedCell06"), {1950, 2718}, 180, EKit::WorkBunk},
        {TEXT("DungeonSharedCell06"), {2398, 2420}, 90, EKit::WashBench},
        {TEXT("DungeonCustodyTurn"), {-3298, 1080}, -90, EKit::DutyBoard},
        {TEXT("DungeonCustodyGallery"), {-2750, 1702}, 0, EKit::WayPlate},
        {TEXT("DungeonCustodyGallery"), {590, 1702}, 0, EKit::DutyBoard},

        // Food comes in from the east passage, is stored by the kitchen, and crosses to the common tables.
        {TEXT("PrisonHall"), {2135, -1215}, 90, EKit::PantryRack},
        {TEXT("PrisonHall"), {1640, -1260}, -7, EKit::WashBench},
        {TEXT("PrisonHall"), {1185, -1435}, 0, EKit::KitchenHearth},
        {TEXT("PrisonHall"), {735, -1290}, -8, EKit::ProvisionTable},
        {TEXT("PrisonHall"), {185, -1380}, 5, EKit::PantryRack},
        {TEXT("PrisonHall"), {-335, -1100}, -14, EKit::ProvisionTable},
        {TEXT("PrisonHall"), {-870, -1105}, -6, EKit::MessTable},
        {TEXT("PrisonHall"), {-1350, -1035}, 8, EKit::MessTable},
        {TEXT("PrisonHall"), {-1900, -1140}, -90, EKit::PantryRack},
        {TEXT("PrisonHall"), {-1550, -1455}, 0, EKit::Storage},
        {TEXT("PrisonHall"), {-815, -1485}, 0, EKit::PassageHooks},
        {TEXT("PrisonHall"), {-50, -555}, 8, EKit::MessTable},
        {TEXT("PrisonHall"), {475, -655}, -7, EKit::MessTable},
        {TEXT("PrisonHall"), {1035, -740}, 17, EKit::MessTable},
        {TEXT("PrisonHall"), {1510, -560}, 12, EKit::LinenCare},
        {TEXT("PrisonHall"), {2300, -635}, 90, EKit::PantryRack},

        // Screens of furniture along the other side mark sleeping, mending, and refuge record keeping.
        {TEXT("PrisonHall"), {-1950, 910}, -90, EKit::LinenCare},
        {TEXT("PrisonHall"), {-1530, 1250}, 177, EKit::WorkBunk},
        {TEXT("PrisonHall"), {-1060, 1240}, 171, EKit::Cot},
        {TEXT("PrisonHall"), {-600, 1245}, 184, EKit::WorkBunk},
        {TEXT("PrisonHall"), {-1030, 650}, -11, EKit::LinenCare},
        {TEXT("PrisonHall"), {-1425, 665}, 22, EKit::WashBench},
        {TEXT("PrisonHall"), {485, 1260}, 180, EKit::FolioDesk},
        {TEXT("PrisonHall"), {845, 730}, 11, EKit::FolioDesk},
        {TEXT("PrisonHall"), {1120, 1250}, 175, EKit::ToolBench},
        {TEXT("PrisonHall"), {1560, 1210}, 189, EKit::LinenCare},
        {TEXT("PrisonHall"), {2030, 1160}, 184, EKit::WorkBunk},
        {TEXT("PrisonHall"), {2310, 646}, 90, EKit::SealedCabinet},
        {TEXT("PrisonHall"), {1650, 555}, 40, EKit::SortingCart},

        {TEXT("PrisonCell"), {-3165, 903}, 0, EKit::Cot},
        {TEXT("PrisonCell"), {-3298, 1335}, -90, EKit::PassageHooks},
        {TEXT("PrisonCellPassage"), {-2304, 930}, 90, EKit::PassageHooks},
        {TEXT("PrisonCrossing"), {-2950, 393}, 180, EKit::PassageHooks},
        {TEXT("PrisonCrossing"), {-2150, -393}, 0, EKit::WallLedge},
        {TEXT("PrisonWest"), {-3697, -1900}, -90, EKit::PassageHooks},
        {TEXT("PrisonWest"), {-3304, -1535}, 90, EKit::DutyBoard},
        {TEXT("PrisonWest"), {-3698, -1030}, -90, EKit::WallLedge},
        {TEXT("PrisonWest"), {-3304, -530}, 90, EKit::PassageHooks},
        {TEXT("PrisonWest"), {-3698, 535}, -90, EKit::PantryRack},
        {TEXT("PrisonWest"), {-3304, 988}, 90, EKit::WallLedge},
        {TEXT("PrisonWest"), {-3698, 1430}, -90, EKit::PassageHooks},
        {TEXT("PrisonEast"), {3696, -1910}, 90, EKit::CellSeat},
        {TEXT("PrisonEast"), {3304, -1435}, -90, EKit::DutyBoard},
        {TEXT("PrisonEast"), {3698, -965}, 90, EKit::PassageHooks},
        {TEXT("PrisonEast"), {3304, -530}, -90, EKit::WallLedge},
        {TEXT("PrisonEast"), {3698, 400}, 90, EKit::PassageHooks},
        {TEXT("PrisonEast"), {3304, 865}, -90, EKit::BerthChain},
        {TEXT("PrisonEast"), {3698, 1320}, 90, EKit::WallLedge},
        {TEXT("PrisonEast"), {3304, 2265}, -90, EKit::PassageHooks},
        {TEXT("PrisonNorth"), {-2860, 2095}, 180, EKit::PantryRack},
        {TEXT("PrisonNorth"), {-1980, 1704}, 0, EKit::NicheCache},
        {TEXT("PrisonNorth"), {-1060, 2097}, 180, EKit::WallLedge},
        {TEXT("PrisonNorth"), {-650, 1704}, 0, EKit::PassageHooks},
        {TEXT("PrisonNorth"), {720, 1704}, 0, EKit::NicheCache},
        {TEXT("PrisonNorth"), {1360, 2097}, 180, EKit::PassageHooks},
        {TEXT("PrisonNorth"), {2020, 1704}, 0, EKit::PantryRack},
        {TEXT("PrisonNorth"), {2730, 2096}, 180, EKit::WallLedge},
        {TEXT("PrisonEastLink"), {2860, -395}, 0, EKit::PassageHooks},
        {TEXT("PrisonEastLink"), {2860, 395}, 180, EKit::WallLedge},

        // The center remains a reading room, with archive workstations against the outer walls.
        {TEXT("ChapelHall"), {-2225, 1265}, -90, EKit::Conservation},
        {TEXT("ChapelHall"), {-2250, 470}, -90, EKit::SealDesk},
        {TEXT("ChapelHall"), {-2240, 870}, -90, EKit::FolioDesk},
        {TEXT("ChapelHall"), {-435, 495}, -9, EKit::ReadingBay},
        {TEXT("ChapelHall"), {-440, 1365}, 7, EKit::Bindery},
        {TEXT("ChapelHall"), {-1540, 1696}, 180, EKit::Tapestry, 198},
        {TEXT("ChapelHall"), {1540, 1696}, 180, EKit::Tapestry, 198},
        {TEXT("ChapelHall"), {0, 1693}, 180, EKit::Reliquary},
        {TEXT("ChapelHall"), {300, 1420}, 171, EKit::Votive},
        {TEXT("ChapelHall"), {575, 1020}, 187, EKit::ReadingBay},
        {TEXT("ChapelHall"), {2240, 465}, 90, EKit::SealDesk},
        {TEXT("ChapelHall"), {2215, 1255}, 90, EKit::Bindery},
        {TEXT("ChapelHall"), {2250, 1580}, 90, EKit::Conservation},
        {TEXT("ChapelHall"), {2240, 865}, 90, EKit::FolioDesk},
        {TEXT("ChapelHall"), {2294, 1090}, 90, EKit::CatalogCabinet},
        {TEXT("ChapelHall"), {-2294, 1580}, -90, EKit::CatalogCabinet},

        // Periodical racks bracket the two dense reserve stacks and stay clear of the cross aisle.
        {TEXT("ChapelHall"), {-2388, -1260}, -90, EKit::Bookcase},
        {TEXT("ChapelHall"), {-2388, -920}, -90, EKit::Bookcase},
        {TEXT("ChapelHall"), {-2388, -570}, -90, EKit::Bookcase},
        {TEXT("ChapelHall"), {2388, -1260}, 90, EKit::Bookcase},
        {TEXT("ChapelHall"), {2388, -915}, 90, EKit::Bookcase},
        {TEXT("ChapelHall"), {2388, -570}, 90, EKit::Bookcase},
        {TEXT("ChapelHall"), {-2070, -1510}, 12, EKit::SortingCart},
        {TEXT("ChapelHall"), {2110, -1520}, 15, EKit::SortingCart},
        {TEXT("ChapelHall"), {-1205, -1699}, 0, EKit::Bookcase},
        {TEXT("ChapelHall"), {-755, -1699}, 0, EKit::Bookcase},
        {TEXT("ChapelHall"), {745, -1699}, 0, EKit::Bookcase},
        {TEXT("ChapelHall"), {1205, -1699}, 0, EKit::Bookcase},
        {TEXT("ChapelHall"), {-370, -1330}, 92, EKit::SealedCabinet},
        {TEXT("ChapelHall"), {385, -1000}, -88, EKit::SortingCart},
        {TEXT("ChapelHall"), {-2020, -585}, 84, EKit::CatalogCabinet},
        {TEXT("ChapelHall"), {2060, -585}, -90, EKit::CatalogCabinet},

        // Returns, transit crates, and small resting pockets follow the archive's perimeter corridor.
        {TEXT("ChapelWest"), {-3698, -1755}, -90, EKit::DocumentRail},
        {TEXT("ChapelWest"), {-3304, -1260}, 90, EKit::PassageHooks},
        {TEXT("ChapelWest"), {-3698, -805}, -90, EKit::WallLedge},
        {TEXT("ChapelWest"), {-3304, 620}, 90, EKit::NicheCache},
        {TEXT("ChapelWest"), {-3698, 1100}, -90, EKit::PassageHooks},
        {TEXT("ChapelWest"), {-3304, 1590}, 90, EKit::WallLedge},
        {TEXT("ChapelWest"), {-3698, 2080}, -90, EKit::PassageHooks},
        {TEXT("ChapelEast"), {3698, -1660}, 90, EKit::PassageHooks},
        {TEXT("ChapelEast"), {3304, -1080}, -90, EKit::DocumentRail},
        {TEXT("ChapelEast"), {3698, -535}, 90, EKit::WallLedge},
        {TEXT("ChapelEast"), {3304, 495}, -90, EKit::PassageHooks},
        {TEXT("ChapelEast"), {3698, 976}, 90, EKit::NicheCache},
        {TEXT("ChapelEast"), {3304, 1420}, -90, EKit::WallLedge},
        {TEXT("ChapelEast"), {3698, 1890}, 90, EKit::DocumentRail},
        {TEXT("ChapelEastLink"), {2850, 397}, 180, EKit::PassageHooks},
        {TEXT("ChapelEastLink"), {2850, -397}, 0, EKit::WallLedge},
        {TEXT("ChapelWestLink"), {-2870, -397}, 0, EKit::PassageHooks},
        {TEXT("SeaGallery"), {-1910, -2346}, 0, EKit::NicheCache},
        {TEXT("SeaGallery"), {-1030, -1904}, 180, EKit::WallLedge},
        {TEXT("SeaGallery"), {-540, -2346}, 0, EKit::PassageHooks},
        {TEXT("SeaGallery"), {610, -1904}, 180, EKit::WallLedge},
        {TEXT("SeaGallery"), {1070, -2346}, 0, EKit::DocumentRail},
        {TEXT("SeaGallery"), {1785, -1904}, 180, EKit::PassageHooks},
        {TEXT("SeaWestLink"), {-2810, -2346}, 0, EKit::WallLedge},

        // Mirrors and stone stands sit on the walls, outside the combat envelope.
        {TEXT("MirrorAnte"), {-2850, -332}, 17, EKit::StandingMirror},
        {TEXT("MirrorAfter"), {2720, 1298}, 180, EKit::BrokenMasonry},
        {TEXT("MirrorCourt"), {-1410, -1635}, 0, EKit::StandingMirror},
        {TEXT("MirrorCourt"), {1450, 1640}, 180, EKit::MirrorDais},
        {TEXT("MirrorCourt"), {-2220, 815}, -80, EKit::FolioDesk},
        {TEXT("MirrorCourt"), {-415, 1565}, 176, EKit::GlassRack},
        {TEXT("MirrorCourt"), {2240, -1280}, 77, EKit::SealedCabinet},
        {TEXT("MirrorCourt"), {-1140, -1699}, 0, EKit::Votive},
        {TEXT("MirrorAnte"), {-3120, -499}, 0, EKit::Shelf},
        {TEXT("MirrorAfter"), {2930, 1299}, 180, EKit::WallLedge},
        {TEXT("MirrorWest"), {-3698, 1150}, -90, EKit::WallLedge},
        {TEXT("MirrorEast"), {3698, -905}, 90, EKit::NicheCache},

        // The final court's side offerings sit below the stone and glass.
        {TEXT("FinalCourt"), {-2090, -1658}, 0, EKit::JudgmentDais},
        {TEXT("FinalCourt"), {-2240, 845}, -84, EKit::LinenCare},
        {TEXT("FinalCourt"), {2225, 965}, 84, EKit::ProvisionTable},
        {TEXT("FinalCourt"), {575, -1555}, -3, EKit::NicheCache},
        {TEXT("FinalCourt"), {1140, -1695}, 0, EKit::CourtStation},
        {TEXT("FinalCourt"), {-1140, 1695}, 180, EKit::BrokenMasonry},
        {TEXT("FinalCourt"), {1130, 1695}, 180, EKit::Reliquary},
        {TEXT("FinalCourt"), {-1635, 1698}, 180, EKit::Tapestry, 255},
        {TEXT("FinalCourt"), {1635, -1698}, 0, EKit::Tapestry, 255},
        {TEXT("FinalAnte"), {-3100, 496}, 180, EKit::Archive},
        {TEXT("FinalExit"), {2935, 499}, 180, EKit::Shelf},
        {TEXT("FinalWest"), {-3698, -1250}, -90, EKit::WallLedge},
        {TEXT("FinalArrival"), {-860, -2748}, 0, EKit::WallLedge},

        // A few working surfaces sit outside the garden path.
        {TEXT("DawnGarden"), {5470, 1080}, 180, EKit::Garden},
        {TEXT("DawnGarden"), {6320, 1140}, 180, EKit::GardenBorder},
        {TEXT("DawnGarden"), {7380, 1045}, 180, EKit::GardenSeat},
        {TEXT("DawnGarden"), {5300, -950}, 0, EKit::Garden},
        {TEXT("DawnGarden"), {5880, -1080}, 0, EKit::Votive},
        {TEXT("DawnGarden"), {6420, -1150}, 0, EKit::GardenBorder},
        {TEXT("DawnGarden"), {7100, 1080}, 180, EKit::Votive},
        {TEXT("DawnGarden"), {7200, -1162}, 0, EKit::BrokenMasonry},
    };

    const FSite RoomLife[] = {
        // B1. The sluice sits in the south lamp pool, with restraint, tool and salt-water storage on separate sides.
        {TEXT("DungeonCourt"), {-1280, -1584}, 0, EKit::PumpGear},
        {TEXT("DungeonCourt"), {-490, 1677}, 180, EKit::ToolBench},
        {TEXT("DungeonCourt"), {775, -1697}, 0, EKit::BerthChain},
        {TEXT("DungeonCourt"), {775, -1665}, 0, EKit::MortarTrough},
        {TEXT("DungeonCourt"), {2397, 450}, 90, EKit::DutyBoard},
        {TEXT("DungeonCourt"), {-2397, 760}, -90, EKit::Irons},
        {TEXT("DungeonCourt"), {-410, -1697}, 0, EKit::DutyBoard},
        {TEXT("DungeonAnte"), {-3060, 498}, 180, EKit::DutyBoard, 25},
        {TEXT("DungeonWest"), {-3698, -2340}, -90, EKit::BerthChain},
        {TEXT("DungeonWest"), {-3302, -890}, 90, EKit::DutyBoard},
        {TEXT("CompanionCell"), {-2703, -1630}, 90, EKit::HoldingCellSeat},
        {TEXT("DungeonDownLink"), {-2915, -2748}, 0, EKit::BerthChain},
        {TEXT("DungeonDownLink"), {-2250, -2452}, 180, EKit::WayPlate},
        {TEXT("DungeonAfter"), {2600, 1298}, 180, EKit::DutyBoard, 35},
        {TEXT("DungeonEast"), {3697, -2070}, 90, EKit::BerthChain},
        {TEXT("DungeonEast"), {3302, -1360}, -90, EKit::DutyBoard},
        {TEXT("DungeonReturnLink"), {2780, -2748}, 0, EKit::DutyBoard},
        {TEXT("DungeonReturnLink"), {2490, -2452}, 180, EKit::WayPlate},

        // 1F. Kitchen tallies and a washing stool sit near the things they account for.
        {TEXT("PrisonHall"), {-1320, -1497}, 0, EKit::DutyBoard, 58},
        {TEXT("PrisonHall"), {-999, 1498}, 180, EKit::DutyBoard, 40},
        {TEXT("PrisonCell"), {-3297, 1086}, -90, EKit::CellSeat},
        {TEXT("PrisonCellPassage"), {-2698, 688}, -90, EKit::DutyBoard},
        {TEXT("PrisonDownLink"), {-2850, -2408}, 0, EKit::DutyBoard},
        {TEXT("PrisonReturnLink"), {2695, -2112}, 180, EKit::BerthChain},
        {TEXT("PrisonNorthLink"), {-298, 1600}, -90, EKit::WayPlate},
        {TEXT("PrisonChapelLink"), {2690, 2748}, 180, EKit::DocumentRail},

        // 2F. Incoming folios and sea post hang above the perimeter's traffic.
        {TEXT("ChapelHall"), {1110, 1698}, 180, EKit::DocumentRail, 100},
        {TEXT("ChapelArrival"), {2295, 2408}, 180, EKit::DocumentRail},
        {TEXT("SeaGallery"), {-1825, -1902}, 180, EKit::DocumentRail},
        {TEXT("SeaWestLink"), {-2750, -1902}, 180, EKit::DocumentRail},
        {TEXT("SeaEastLink"), {2805, -2348}, 0, EKit::DocumentRail},
        {TEXT("ChapelSeaLink"), {-298, -1800}, -90, EKit::WayPlate},
        {TEXT("ChapelMirrorLink"), {-2640, 2748}, 180, EKit::DocumentRail},

        // 3F. Polishing tables are distinct from the full mirrors prepared for testimony.
        {TEXT("MirrorCourt"), {2397, -760}, 90, EKit::GlassRack},
        {TEXT("MirrorCourt"), {1012, 1668}, 180, EKit::GlassRack},
        {TEXT("MirrorCourt"), {-1710, 1698}, 180, EKit::MirrorVeil, 80},
        {TEXT("MirrorCourt"), {495, 1698}, 180, EKit::DocumentRail, 36},
        {TEXT("MirrorCourt"), {1770, -1697}, 0, EKit::MirrorVeil, 60},
        {TEXT("MirrorAnte"), {-3180, 498}, 180, EKit::DocumentRail, 60},
        {TEXT("MirrorAnte"), {-2600, -498}, 0, EKit::MirrorVeil, 30},
        {TEXT("MirrorWest"), {-3697, 1910}, -90, EKit::MirrorVeil, 25},
        {TEXT("MirrorWest"), {-3302, 710}, 90, EKit::DocumentRail},
        {TEXT("MirrorArrival"), {-2645, 2408}, 180, EKit::MirrorVeil},
        {TEXT("MirrorAfter"), {2560, 1298}, 180, EKit::DocumentRail},
        {TEXT("MirrorEast"), {3698, -2065}, 90, EKit::DocumentRail},
        {TEXT("MirrorEast"), {3302, -1500}, -90, EKit::WayPlate},
        {TEXT("MirrorFinalLink"), {2760, -2748}, 0, EKit::MirrorVeil},

        // 4F. A scale and hanging standards flank the space reserved for judgment.
        {TEXT("FinalCourt"), {-765, 1698}, 180, EKit::TribunalScale},
        {TEXT("FinalCourt"), {35, -1698}, 0, EKit::MourningHanging, 95},
        {TEXT("FinalCourt"), {-470, -1655}, 0, EKit::SealedCabinet},
        {TEXT("FinalCourt"), {2398, -1210}, 90, EKit::Reliquary},
        {TEXT("FinalCourt"), {-2398, -750}, -90, EKit::MourningHanging, 90},
        {TEXT("FinalAnte"), {-3105, 498}, 180, EKit::DocumentRail, 155},
        {TEXT("FinalWest"), {-3697, -1985}, -90, EKit::MourningHanging, 25},
        {TEXT("FinalWest"), {-3302, -633}, 90, EKit::DutyBoard, 40},
        {TEXT("FinalArrival"), {-2640, -2748}, 0, EKit::MourningHanging},
        {TEXT("FinalArrival"), {-1975, -2452}, 180, EKit::DocumentRail, 30},
        {TEXT("FinalExit"), {2625, 498}, 180, EKit::MourningHanging, 30},
        {TEXT("FinalExit"), {3020, -498}, 0, EKit::DocumentRail, 38},

        // Outside, the watch chain gives way to seats and pitchers facing the garden.
        {TEXT("DawnBridge"), {3970, 248}, 180, EKit::BerthChain},
        {TEXT("DawnBridge"), {4660, -248}, 0, EKit::WayPlate},
        {TEXT("DawnGarden"), {6815, -1080}, 0, EKit::GardenSeat},
    };

    struct FPropMeshes
    {
        UStaticMesh* Candle;
        UStaticMesh* Vessel;
        UStaticMesh* Handcuff;
        UStaticMesh* Table;
        UStaticMesh* Altar;
        UStaticMesh* Bed;
        UStaticMesh* Candlestick;
        UStaticMesh* LowVessel;
        UStaticMesh* ArchiveShelf;
        UStaticMesh* Guardian;
        UStaticMesh* OpenCask;
        UStaticMesh* ClosedCask;
        UStaticMesh* WashBowl;
    };

    struct FPart
    {
        EShape Shape;
        ESurface Surface;
        FTransform Transform; // Unit-cube bounds, before normalizing the source mesh.
        UStaticMesh* AuthoredMesh;
    };

    struct FArrangement
    {
        FArrangement(const FVector& InOrigin, const FQuat& InOrientation, const FPropMeshes& InMeshes)
            : Origin(InOrigin), Orientation(InOrientation), Meshes(InMeshes) {}

        FVector Origin;
        FQuat Orientation;
        const FPropMeshes& Meshes;
        TArray<FPart> Parts;
        TArray<FTransform> SolidPieces;
        TArray<FTransform> LegacySolidPieces;
        FBox Bounds = FBox(ForceInit);
        int32 Objects = 0;
        int32 ShelfVolumes = 0;

        void Translate(const FVector& Offset)
        {
            Origin += Offset;
            Bounds = Bounds.ShiftBy(Offset);
            for (FPart& Part : Parts) Part.Transform.AddToTranslation(Offset);
            for (FTransform& Solid : SolidPieces) Solid.AddToTranslation(Offset);
            for (FTransform& Solid : LegacySolidPieces) Solid.AddToTranslation(Offset);
        }

        void Solid(const FVector& Centre, const FVector& Size)
        {
            SolidPieces.Emplace(Orientation, Origin + Orientation.RotateVector(Centre), Size);
        }

        void LegacySolid(const FVector& Centre,const FVector& Size,const FQuat& Rotation=FQuat::Identity)
        {
            LegacySolidPieces.Emplace(Orientation*Rotation,Origin+Orientation.RotateVector(Centre),Size);
        }

        void Part(EShape Shape, ESurface Surface, const FVector& Centre, const FVector& Size,
            const FQuat& Rotation = FQuat::Identity, UStaticMesh* AuthoredMesh = nullptr)
        {
            const FTransform Transform(Orientation * Rotation, Origin + Orientation.RotateVector(Centre), Size);
            Bounds += FBox(FVector(-.5), FVector(.5)).TransformBy(Transform);
            Parts.Add({Shape, Surface, Transform, AuthoredMesh});
            if(AuthoredMesh && Size.X>=35. && Size.Y>=30. && Size.Z>=40. && Centre.Z-Size.Z*.5<20.)
                LegacySolid(Centre,Size-FVector(2,2,1),Rotation);
        }

        void Box(ESurface Surface, const FVector& Centre, const FVector& Size, double Yaw = 0.)
        {
            Part(EShape::Box, Surface, Centre, Size, FRotator(0, Yaw, 0).Quaternion());
        }

        void Disc(ESurface Surface, const FVector& Centre, double Diameter, double Height)
        {
            Part(EShape::Tube, Surface, Centre, FVector(Diameter, Diameter, Height));
        }

        void Candle(const FVector& Bottom, double Height, bool bLit, bool bAnimated = false)
        {
            ++Objects;
            Disc(Brass, Bottom + FVector(0, 0, 1.5), 11, 3);
            Part(EShape::Tube, Wax, Bottom + FVector(0, 0, 3 + Height * .5), FVector(5.4, 5.4, Height),
                FQuat::Identity, Meshes.Candle);
            Box(Iron, Bottom + FVector(0, 0, 4 + Height), FVector(.8, .8, 2));
            if (bLit && bAnimated)
            {
                for (const FVector& Across : {FVector::ForwardVector, FVector::RightVector})
                    Part(EShape::Plane, LiveFlame, Bottom + FVector(0, 0, Height + 9.5), FVector(8, 12, .5),
                        FRotationMatrix::MakeFromXY(Across, -FVector::UpVector).ToQuat());
            }
            else if (bLit)
                Part(EShape::Box, Flame, Bottom + FVector(0, 0, 7 + Height), FVector(2.2, 2.2, 4),
                    FRotator(0, 45, 8).Quaternion());
        }

        void Book(const FVector& Bottom, const FVector2D& Size, double Yaw, bool bRed)
        {
            ++Objects;
            Box(bRed ? Leather : DarkOak, Bottom + FVector(0, 0, 1), FVector(Size.X, Size.Y, 2), Yaw);
            Box(Paper, Bottom + FVector(0, 0, 3), FVector(Size.X - 1.6, Size.Y - 1.5, 2), Yaw);
            Box(bRed ? Leather : DarkOak, Bottom + FVector(0, 0, 4.8), FVector(Size.X, Size.Y, 1.6), Yaw);
            const FQuat Rotation = FRotator(0, Yaw, 0).Quaternion();
            const FVector Spine = Rotation.RotateVector(FVector(0, Size.Y * -.5, 3));
            Box(bRed ? Leather : DarkOak, Bottom + Spine, FVector(Size.X, 2, 5.2), Yaw);
        }

        void Beam(ESurface Surface, const FVector& A, const FVector& B, double Width, double Depth)
        {
            const FVector Delta = B - A;
            Part(EShape::Box, Surface, (A + B) * .5, FVector(Width, Depth, Delta.Size()),
                FRotationMatrix::MakeFromZ(Delta.GetSafeNormal()).ToQuat());
        }

        void Loaf(const FVector& Bottom, double Size, double Yaw)
        {
            const FQuat Rotation = FRotator(0, Yaw, 0).Quaternion();
            Part(EShape::Ball, Crust, Bottom + FVector(0, 0, Size * .20), FVector(Size, Size * .61, Size * .39), Rotation);
            for (double X : {-.25, 0., .25})
                Part(EShape::Ball, Paper, Bottom + Rotation.RotateVector(FVector(X * Size, 0, Size * .378)),
                    FVector(Size * .055, Size * .35, Size * .032), Rotation * FRotator(0, 15, 0).Quaternion());
            ++Objects;
        }

        void Sack(const FVector& Bottom, double Width, double Height, bool bGreen)
        {
            const ESurface Fabric = bGreen ? OliveCloth : Linen;
            Part(EShape::Ball, Fabric, Bottom + FVector(0, 0, Height * .47), FVector(Width, Width * .78, Height * .94));
            Part(EShape::Ball, Fabric, Bottom + FVector(0, 0, Height * .94), FVector(Width * .42, Width * .30, Height * .21));
            Disc(Paper, Bottom + FVector(0, 0, Height * .92), Width * .32, 1.4);
            ++Objects;
        }

        void Scroll(const FVector& Bottom, double Length, double Yaw)
        {
            const FQuat Axis = FRotator(0, Yaw, 0).Quaternion() * FQuat(FVector::RightVector, UE_HALF_PI);
            const FVector Centre = Bottom + FVector(0, 0, 3.4);
            Part(EShape::Tube, Paper, Centre, FVector(6.8, 6.8, Length), Axis);
            Part(EShape::Tube, Leather, Centre, FVector(7.1, 7.1, 1.6), Axis);
            ++Objects;
        }

        void ShelfVolume(const FVector& Foot, double Thickness, double Depth, double Height, int32 Seed, double Facing)
        {
            const ESurface Binders[] = {Leather, WineCloth, DarkOak, OliveCloth, BlueCloth, Crust};
            const ESurface Cover = Binders[Seed % UE_ARRAY_COUNT(Binders)];
            const double Lean = Seed % 6 == 1 ? ((Seed % 4) - 1.5) * 3. : 0.;
            const FQuat Rotation(FVector::RightVector, FMath::DegreesToRadians(Lean));
            const auto Place = [&](EShape Shape, ESurface Material, const FVector& At, const FVector& Size)
            {
                Part(Shape, Material, Foot + Rotation.RotateVector(At), Size, Rotation);
            };
            // The binding curves around the rounded spine. A recessed page block remains visible at the head and fore-edge.
            Place(EShape::Box, Paper, FVector(0, 0, Height * .51), FVector(Thickness * .81, Depth - 1.2, Height - 1.5));
            Place(EShape::Tube, Cover, FVector(0, Facing * (Depth * .5 - .9), Height * .5),
                FVector(Thickness, 4.4, Height));
            if (Seed % 8 == 0)
                Place(EShape::Box, Cover, FVector((Seed % 2 == 0 ? 1. : -1.) * Thickness * .435, 0, Height * .5),
                    FVector(Thickness * .13, Depth + .4, Height));
            if (Seed % 11 == 0)
                Place(EShape::Box, Paper, FVector(0, Facing * (Depth * .5 + 1.32), Height * .74),
                    FVector(Thickness * .57, .45, 2.1));
            ++Objects;
            ++ShelfVolumes;
        }

        void Vessel(const FVector& Bottom, double Diameter, double Height)
        {
            ++Objects;
            if(Bottom.Z<20. && Diameter>=28. && Height>=35.)
                LegacySolid(Bottom+FVector(0,0,Height*.46),FVector(Diameter*.80,Diameter*.80,Height*.90));
            if (Meshes.Vessel)
            {
                Part(EShape::Tube, Clay, Bottom + FVector(0, 0, Height * .5), FVector(Diameter, Diameter, Height),
                    FQuat::Identity, Meshes.Vessel);
                return;
            }
            // Three profiles give the clay jug a foot, shoulder and narrow neck.
            Disc(Clay, Bottom + FVector(0, 0, 2), Diameter * .72, 4);
            Disc(Clay, Bottom + FVector(0, 0, Height * .39), Diameter, Height * .69);
            Disc(Clay, Bottom + FVector(0, 0, Height * .74), Diameter * .83, Height * .21);
            Disc(Clay, Bottom + FVector(0, 0, Height * .9), Diameter * .44, Height * .22);
            Disc(DarkOak, Bottom + FVector(0, 0, Height * 1.02), Diameter * .25, .8);
        }

        void Cask(const FVector& Bottom, double Radius, double Height, bool bOpen)
        {
            ++Objects;
            if(Bottom.Z<20.)LegacySolid(Bottom+FVector(0,0,Height*.5),FVector(Radius*1.70,Radius*1.70,Height));
            if (UStaticMesh* Mesh = bOpen ? Meshes.OpenCask : Meshes.ClosedCask)
            {
                const double Diameter = Radius * 2. + 3.8;
                Part(EShape::Tube, Oak, Bottom + FVector(0, 0, Height * .5), FVector(Diameter, Diameter, Height),
                    FQuat::Identity, Mesh);
                if (bOpen)
                    Disc(Water, Bottom + FVector(0, 0, Height * .79), Diameter * .88 + .24, .7);
                return;
            }
            constexpr int32 Staves = 12;
            const double End = Radius * .88, Belly = Radius;
            const double Width = 2. * Belly * FMath::Tan(PI / Staves) - .35;
            for (int32 Stave = 0; Stave < Staves; ++Stave)
            {
                const double Angle = Stave * (2. * PI / Staves);
                const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0);
                const FVector Foot = Bottom + End * Out;
                const FVector Middle = Bottom + Belly * Out + FVector(0, 0, Height * .5);
                const FVector Top = Foot + FVector(0, 0, Height);
                for (const TPair<FVector, FVector>& Span : {TPair<FVector, FVector>(Foot, Middle), {Middle, Top}})
                {
                    const FVector Delta = Span.Value - Span.Key;
                    Part(EShape::Box, Stave % 4 == 0 ? DarkOak : Oak, (Span.Key + Span.Value) * .5,
                        FVector(3.8, Width, Delta.Size() + .35), FRotationMatrix::MakeFromZX(Delta.GetSafeNormal(), Out).ToQuat());
                }
            }
            Disc(Iron, Bottom + FVector(0, 0, Height * .19), (Radius * .94 + 2.3) * 2., 4.0);
            Disc(Iron, Bottom + FVector(0, 0, Height * .68), (Radius * .97 + 2.4) * 2., 4.0);
            if (bOpen)
                Disc(Water, Bottom + FVector(0, 0, Height * .79), (Radius * .925 - 1.4) * 2., .7);
            else
                Disc(DarkOak, Bottom + FVector(0, 0, Height - 2), End * 2., 5.);
        }

        void Crate(const FVector& Bottom, double Width, double Depth, double Height, double Yaw)
        {
            ++Objects;
            const FQuat Rotation = FRotator(0, Yaw, 0).Quaternion();
            if(Bottom.Z<20.)LegacySolid(Bottom+FVector(0,0,Height*.5),FVector(Width-2,Depth-2,Height),Rotation);
            Box(DarkOak, Bottom + FVector(0, 0, Height * .5), FVector(Width, Depth - 2, Height), Yaw);
            for (double Side : {-1., 1.})
            {
                for (double Level : {.20, .49, .78})
                    Box(Oak, Bottom + Rotation.RotateVector(FVector(0, Side * Depth * .5, Height * Level)),
                        FVector(Width, 2.2, Height * .27), Yaw);
                Box(Iron, Bottom + Rotation.RotateVector(FVector(Side * Width * .35, 0, Height + 1)),
                    FVector(4, Depth + 2, 2), Yaw);
            }
        }

        void Table(double Height, double Width, double Depth, bool bStone, bool bRoundWood = false)
        {
            ++Objects;
            const double Back = 2., Middle = Back + Depth * .5;
            LegacySolid(FVector(0,Middle,Height*.5),FVector(Width-4,Depth-4,Height-1));
            if (bRoundWood && Meshes.Table)
            {
                Part(EShape::Box, Oak, FVector(0, Middle, Height * .5), FVector(Width, Depth, Height),
                    FQuat::Identity, Meshes.Table);
                return;
            }
            Box(bStone ? Stone : Oak, FVector(0, Middle, Height - 4), FVector(Width, Depth, 8));
            Box(DarkOak, FVector(0, Middle, Height - 12), FVector(Width - 12, Depth - 7, 7));
            for (double X : {-1., 1.}) for (double Y : {-1., 1.})
                Box(bStone ? Stone : DarkOak,
                    FVector(X * (Width * .5 - 11), Middle + Y * (Depth * .5 - 10), (Height - 12) * .5),
                    FVector(11, 11, Height - 12));
        }

        void Link(const FVector& Centre, bool bCrosswise, double Size)
        {
            const FQuat Q = bCrosswise ? FRotator(0, 90, 0).Quaternion() : FQuat::Identity;
            for (double Sign : {-1., 1.})
            {
                const FQuat R = FRotator(0, 0, Sign * 13).Quaternion();
                Part(EShape::Box, Iron, Centre + Q.RotateVector(FVector(Sign * Size * .28, 0, 0)),
                    FVector(1.7, 1.8, Size * .9), Q * R);
                Part(EShape::Box, Iron, Centre + Q.RotateVector(FVector(0, 0, Sign * Size * .39)),
                    FVector(Size * .50, 1.8, 1.7), Q);
            }
        }

        void IronsSet(int32 Variant)
        {
            for (double X : {-38., 42.})
            {
                ++Objects;
                const double Height = X < 0 ? 116. : 101. + 5. * (Variant % 4);
                Box(Iron, FVector(X, 4, Height + 21), FVector(19, 7, 26));
                for (int32 L = 0; L < 4; ++L)
                    Link(FVector(X, 12, Height + (3 - L) * 6), L % 2 == 0, 10);
                if (Meshes.Handcuff)
                    Part(EShape::Tube, Iron, FVector(X + 1, 13, Height - 10), FVector(16, 20, 6.3),
                        FRotator(0, X < 0 ? -12 : 12, 90).Quaternion(), Meshes.Handcuff);
                else
                    Link(FVector(X + 1, 13, Height - 10), X < 0, 20);
            }
            for (int32 I = 0; I < 4; ++I)
            {
                ++Objects;
                Box(Stone, FVector(-60 + 38 * I, 20 + 4 * (I % 3), 5 + I % 3),
                    FVector(22 + I % 2 * 8, 14, 9 + I % 3), I * 17 + Variant * 3);
            }
        }

        void Create(EKit Kit, int32 Variant, const DuelNorthArchive::FVolume* Volume)
        {
            switch (Kit)
            {
            case EKit::Irons:
                IronsSet(Variant);
                break;
            case EKit::Storage:
                Cask(FVector(-67, 4, 1), 29 + Variant % 6, 80 + Variant % 13, false);
                Cask(FVector(-2, 13, 1), 21 + Variant % 5, 57 + Variant % 11, true);
                Crate(FVector(65, 22, 1), 51, 41, 36, Variant % 2 ? 11 : -8);
                Vessel(FVector(59, 21, 39), 16, 23 + Variant % 7);
                Book(FVector(80, 28, 39), FVector2D(15, 11), -8, false);
                break;
            case EKit::Archive:
                if (Meshes.Table)
                {
                    ++Objects;
                    Part(EShape::Box, Oak, FVector(0, 29, 43.5), FVector(165, 58, 87),
                        FQuat::Identity, Meshes.Table);
                }
                else Table(87, 155, 45, false);
                Crate(FVector(-20, 26, 1), 34, 30, 28, 0);
                Book(FVector(-45, 23, 87), FVector2D(32, 23), -7, true);
                Book(FVector(-38, 26, 92.5), FVector2D(27, 22), 13 + Variant % 7, false);
                Book(FVector(5, 24, 87), FVector2D(34, 25), 8, false);
                Vessel(FVector(43, 25, 87), 19, 26);
                Candle(FVector(64, 17, 87), 10, true);
                break;
            case EKit::Votive:
                Table(66, 146, 48, true);
                for (int32 I = 0; I < 5; ++I)
                    Candle(FVector(-58 + 27 * I, 9 + ((I + Variant) % 3) * 2, 66),
                        7 + ((I * 5 + Variant) % 16), (I + Variant) % 4 != 0);
                Book(FVector(46, 33, 66), FVector2D(29, 20), 9, true);
                break;
            case EKit::Shelf:
                ++Objects;
                Box(Stone, FVector(0, 20, 79), FVector(136, 36, 8));
                for (double X : {-47., 47.})
                    Box(Stone, FVector(X, 11, 69), FVector(12, 22, 20));
                Candle(FVector(-45, 18, 83), 16 + Variant % 5, true);
                Candle(FVector(-22, 26, 83), 8 + Variant % 3, Variant % 3 == 0);
                Vessel(FVector(16, 20, 83), 18 + Variant % 3, 21);
                Book(FVector(48, 21, 83), FVector2D(27, 20), Variant % 7 - 3, Variant % 2 == 0);
                break;
            case EKit::MessTable:
            {
                const double Width = 264. + 24. * (Variant % 2);
                Table(79, Width, 86, false);
                Solid(FVector(0, 45, 40), FVector(Width - 12, 82, 78));
                for (double Side : {-1., 1.})
                {
                    const double Y = Side < 0. ? -22. : 112.;
                    const double X = Side < 0. ? -18. : 16.;
                    const double Length = (Side < 0. ? .71 : .86) * Width;
                    Box(Oak, FVector(X, Y, 44), FVector(Length, 27, 9));
                    for (double Foot : {-1., 1.})
                        Box(DarkOak, FVector(X + Foot * Length * .38, Y, 18), FVector(12, 22, 36));
                    Solid(FVector(X, Y, 24), FVector(Length - 7, 25, 46));
                    ++Objects;
                }
                for (int32 I = 0; I < (Variant % 2 ? 3 : 5); ++I)
                {
                    const double X = -104. + I * 47.;
                    const double Y = 28. + (I * 23 + Variant * 7) % 32;
                    Disc(Clay, FVector(X, Y, 80), 22. + I % 3 * 3., 2.2);
                    Disc(Paper, FVector(X, Y, 81.2), 16. + I % 3 * 2., .45);
                    if (I % 2 == 0) Vessel(FVector(X + 16., Y + 9., 79.), 13., 16. + I);
                    if ((I + Variant) % 3 == 0) Loaf(FVector(X, Y, 81.4), 15. + I, I * 15.);
                    ++Objects;
                }
                Candle(FVector(-108, 66, 79), 13, true);
                Candle(FVector(91, 24, 79), 22, true);
                Vessel(FVector(58, 58, 79), 31, 39);
                Box(Linen, FVector(6, 65, 80.2), FVector(45, 25, 1.8), Variant % 11 - 4);
                Loaf(FVector(14, 61, 81.1), 30, -18);
                break;
            }
            case EKit::WashBench:
                Table(69, 168, 52, false, true);
                Solid(FVector(0, 28, 34), FVector(160, 49, 68));
                Cask(FVector(-109, 38, 1), 28, 62, true);
                Solid(FVector(-109, 38, 31), FVector(51, 51, 60));
                Cask(FVector(105, 87, 1), 21, 43, false);
                Solid(FVector(105, 87, 22), FVector(39, 39, 42));
                Disc(Clay, FVector(-31, 31, 74), 41, 11);
                Disc(Water, FVector(-31, 31, 79.7), 33, .6);
                Box(Linen, FVector(32, 14, 71), FVector(47, 27, 3.4), Variant % 2 ? 9 : -13);
                Box(Linen, FVector(29, 18, 74.4), FVector(29, 20, 3.2), -5);
                Box(Linen, FVector(59, 7, 63), FVector(19, 7, 14), 2);
                Vessel(FVector(58, 44, 69), 18, 23);
                Candle(FVector(-61, 42, 69), 11, true);
                Objects += 4;
                break;
            case EKit::ReadingBay:
            case EKit::Bindery:
            {
                const bool bBindery = Kit == EKit::Bindery;
                if (Meshes.Table)
                {
                    ++Objects;
                    Part(EShape::Box, Oak, FVector(0, 0, 42), FVector(205, 110, 84),
                        FQuat::Identity, Meshes.Table);
                }
                else
                {
                    ++Objects;
                    Box(Oak, FVector(0, 0, 81), FVector(205, 110, 6));
                    for (double X : {-87., 87.}) for (double Y : {-42., 42.})
                        Box(DarkOak, FVector(X, Y, 39), FVector(10, 10, 78));
                }
                Solid(FVector(0, 0, 42), FVector(202, 105, 84));
                const ESurface Runner = bBindery || Variant % 2 == 0 ? BlueCloth : WineCloth;
                Box(Runner, FVector(-20, 0, 84.75), FVector(104, 113, 1.5));
                Box(Runner, FVector(-20, -56.5, 73), FVector(104, 1.6, 23));
                Box(Brass, FVector(-20, -57.4, 64), FVector(96, .8, 1.1));
                for (double Y : {-92., 92.})
                {
                    if (bBindery && Y > 0.) continue;
                    ++Objects;
                    Box(Oak, FVector(0, Y, 41), FVector(170, 29, 10));
                    for (double X : {-63., 63.}) Box(DarkOak, FVector(X, Y, 18), FVector(11, 22, 36));
                    Solid(FVector(0, Y, 22), FVector(167, 27, 44));
                }
                Candle(FVector(-72, 31, 84), 19, true);
                if (bBindery)
                {
                    // The press anchors the binding tools beside the folded paper.
                    Box(DarkOak, FVector(49, 17, 89), FVector(66, 49, 10));
                    for (double X : {23., 75.}) Box(Oak, FVector(X, 17, 106), FVector(12, 13, 35));
                    Box(Oak, FVector(49, 17, 123), FVector(66, 15, 8));
                    Disc(Iron, FVector(49, 17, 118), 7, 41);
                    Box(Iron, FVector(49, 17, 138), FVector(36, 4, 4), -14);
                    Box(Paper, FVector(49, 17, 96), FVector(36, 30, 4));
                    Book(FVector(-28, -12, 86), FVector2D(44, 33), -8, true);
                    Objects += 2;
                }
                else
                {
                    // An open docket, wax weight, and spare folios live on the sorting table.
                    Box(Leather, FVector(-22, -5, 87), FVector(70, 45, 3));
                    for (double Side : {-1., 1.})
                    {
                        Part(EShape::Box, Paper, FVector(-22 + Side * 17., -5, 90), FVector(34, 40, 3),
                            FRotator(Side * 4., 0, 0).Quaternion());
                        for (int32 I = 0; I < 4; ++I)
                            Box(DarkOak, FVector(-22 + Side * 17., -17 + I * 7., 92),
                                FVector(20 - I, .7, .20));
                    }
                    Disc(Wax, FVector(-3, 7, 94), 9, 4);
                    Book(FVector(47, 18, 84), FVector2D(44, 29), 7, false);
                    Book(FVector(51, 19, 89.5), FVector2D(31, 25), -5, true);
                    Vessel(FVector(75, -20, 84), 13, 16);
                    ++Objects;
                }
                break;
            }
            case EKit::Bookcase:
            case EKit::LibraryStack:
            {
                const bool bFree = Kit == EKit::LibraryStack;
                using namespace DuelNorthArchive;
                const double Width = bFree ? ShelfWidth : 203.;
                const double Height = bFree ? ShelfHeight : 265.;
                const double Depth = bFree ? ShelfDepth : 54.;
                const double Middle = bFree ? 0. : 29.;
                const double Facing = bFree ? -1. : 1.;
                const bool bImported = bFree && Meshes.ArchiveShelf;
                if (bImported)
                {
                    const FVector SourceSize = Meshes.ArchiveShelf->GetBoundingBox().GetSize();
                    const bool bWidthOnY = SourceSize.Y > SourceSize.X;
                    Part(EShape::Box, Oak, FVector(0, Middle, Height * .5),
                        bWidthOnY ? FVector(Depth, Width, Height) : FVector(Width, Depth, Height),
                        FRotator(0, bWidthOnY ? 90. : 0., 0.).Quaternion(), Meshes.ArchiveShelf);
                }
                else
                {
                    Box(DarkOak, FVector(0, Middle - Facing * (Depth * .5 - 3), Height * .5), FVector(Width, 6, Height));
                    Box(Oak, FVector(0, Middle, Height - 4.5), FVector(Width + 9, Depth + 9, 9));
                    Box(DarkOak, FVector(0, Middle, 9), FVector(Width + 6, Depth + 5, 17));
                    Box(Brass, FVector(0, Middle + Facing * (Depth * .5 + 5), Height - 5.5), FVector(Width - 20, 1.7, 2.1));
                    for (double Side : {-1., 1.})
                    {
                        const double X = Side * (Width * .5 - 6.);
                        Box(Oak, FVector(X, Middle, Height * .5), FVector(12, Depth, Height));
                        Box(DarkOak, FVector(X, Middle + Facing * Depth * .5, Height * .52), FVector(9, 8, Height * .81));
                        Box(Brass, FVector(X, Middle + Facing * (Depth * .5 + 4.5), Height - 23), FVector(10, 2, 21));
                    }
                }
                Solid(FVector(0, Middle, Height * .5), FVector(Width - 3, Depth, Height));
                ++Objects;

                // Keep returned folios lying flat in irregular breaks between the upright volumes.
                const int32 Tiers = bFree ? UE_ARRAY_COUNT(TierTops) : 5;
                const auto ShelfTop = [&](int32 Tier)
                {
                    if (Tier >= Tiers) return Height - 9.;
                    return bFree ? TierTops[Tier] * (Height / ImportedHeight) : 25. + 46. * Tier;
                };
                for (int32 Tier = 0; Tier < Tiers; ++Tier)
                {
                    const double Z = ShelfTop(Tier);
                    if (!bImported)
                        Box(Oak, FVector(0, Middle, Z - 2.), FVector(Width - 15, Depth - 8, 4));
                    const int32 Group = (Variant + 3 * Tier) % 7;
                    const double ShelfY = Middle + Facing * (bFree ? 11. : 5.);
                    const double MaxHeight = ShelfTop(Tier + 1) - Z - 5.;
                    double X = -Width * .5 + 15. + Group * 1.5;
                    const double End = Width * .5 - (Group < 3 ? 43. : 16.);
                    const int32 VolumeCount = 8 + (Variant + Tier * 2) % 3;
                    for (int32 BookIndex = 0; BookIndex < VolumeCount; ++BookIndex)
                    {
                        const int32 Seed = Variant * 59 + Tier * 131 + BookIndex * 83;
                        const double Thickness = 9. + Seed % 10;
                        if (Volume && Tier == VolumeTier && X < Volume->Slot + VolumeSize.X + 3. &&
                            X + Thickness > Volume->Slot - VolumeSize.X - 3.)
                            X = Volume->Slot + VolumeSize.X + 3.;
                        if (X + Thickness >= End) break;
                        const FVector Foot(X + Thickness * .5, ShelfY + (Seed % 7 - 3) * .83, Z + .25);
                        ShelfVolume(Foot, Thickness, 22. + Seed % 10, FMath::Min(MaxHeight, 19. + Seed % 18), Seed, Facing);
                        X += Thickness + (BookIndex == (Group % 4) + 2 ? 7. : Seed % 6 == 1 ? 3.2 : 1.5);
                    }
                    if (Group < 3)
                    {
                        const double Pile = Width * .5 - 27.;
                        const ESurface Cover = Group == 0 ? BlueCloth : Leather;
                        Box(Cover, FVector(Pile, ShelfY - 1., Z + 2.8), FVector(30, 27, 5.6), Group * 8. - 13.);
                        Box(Paper, FVector(Pile, ShelfY - 1., Z + 2.8), FVector(29, 27.4, 3.1), Group * 8. - 13.);
                        Box(DarkOak, FVector(Pile + 1.5, ShelfY + 1., Z + 8.2), FVector(26, 24, 5.2), 9.);
                        Box(Paper, FVector(Pile + 1.5, ShelfY + 1., Z + 8.2), FVector(25, 24.4, 2.9), 9.);
                        Objects += 2;
                        ShelfVolumes += 2;
                    }
                }
                break;
            }
            case EKit::StandingMirror:
            case EKit::MirrorDais:
            {
                const bool bDais = Kit == EKit::MirrorDais;
                const double HalfWidth = bDais ? 61. : Variant % 2 ? 43. : 51.;
                const double Head = bDais ? 238. : Variant % 2 ? 193. : 212.;
                Box(Stone, FVector(0, 0, 7), FVector(2. * HalfWidth + 34, 55, 14));
                Box(DarkOak, FVector(0, 0, 16), FVector(2. * HalfWidth + 27, 44, 5));
                Box(MirrorGlass, FVector(0, 3, 33. + (Head - 33.) * .5), FVector(2. * HalfWidth, 2.5, Head - 33.));
                for (double Side : {-1., 1.})
                {
                    const double X = Side * (HalfWidth + 5.);
                    Box(DarkOak, FVector(X, 0, (Head + 21.) * .5), FVector(12, 18, Head - 17.));
                    Box(Brass, FVector(X, 10, (Head + 24.) * .5), FVector(3.4, 3, Head - 22.));
                    for (double Z : {35., Head - 4.})
                        Part(EShape::Ball, Brass, FVector(X, 12, Z), FVector(9, 6, 9));
                }
                Box(DarkOak, FVector(0, 1, 28), FVector(2. * HalfWidth + 16, 18, 12));
                Box(Brass, FVector(0, 9, 33), FVector(2. * HalfWidth, 4, 4));
                Box(DarkOak, FVector(0, 1, Head), FVector(2. * HalfWidth + 16, 18, 12));
                for (int32 I = -5; I <= 5; ++I)
                {
                    const double T = I / 5.;
                    Part(EShape::Box, Oak, FVector(T * HalfWidth, 1., Head + 17. * (1. - FMath::Square(T))),
                        FVector(HalfWidth * .22, 15, 12), FRotator(31. * T, 0, 0).Quaternion());
                }
                Part(EShape::Ball, Brass, FVector(0, 10, Head + 22), FVector(13, 7, 17));
                Solid(FVector(0, 0, Head * .5), FVector(2. * HalfWidth + 22, 35, Head));
                ++Objects;
                if (bDais)
                {
                    Box(ChippedStone, FVector(18, 29, 6), FVector(485, 116, 12));
                    Box(DarkOak, FVector(168, 31, 77), FVector(172, 64, 9));
                    if (Meshes.Altar)
                        Part(EShape::Box, Stone, FVector(168, 31, 43), FVector(64, 168, 67),
                            FRotator(0, 90, 0).Quaternion(), Meshes.Altar);
                    if (Meshes.Guardian)
                        Part(EShape::Box, Stone, FVector(-148, 23, 88), FVector(86, 69, 153),
                            FRotator(0, 156, 0).Quaternion(), Meshes.Guardian);
                    Box(BlueCloth, FVector(161, 32, 82), FVector(91, 65, 2));
                    Book(FVector(172, 32, 83), FVector2D(51, 36), 11, true);
                    Book(FVector(177, 32, 88.5), FVector2D(42, 28), -9, false);
                    Vessel(FVector(213, 36, 83), 22, 26);
                    Candle(FVector(114, 27, 83), 24, true);
                    Part(EShape::Box, MirrorGlass, FVector(155, 44, 85), FVector(29, 17, 2.5),
                        FRotator(0, 28, 0).Quaternion());
                    Solid(FVector(168, 31, 45), FVector(166, 60, 72));
                    Objects += 4;
                }
                break;
            }
            case EKit::JudgmentDais:
            {
                Box(ChippedStone, FVector(0, 53, 7), FVector(372, 113, 14));
                Box(CourtSlate, FVector(0, 52, 14), FVector(353, 101, 3));
                if (Meshes.Altar)
                    Part(EShape::Box, Stone, FVector(0, 53, 65), FVector(74, 218, 100),
                        FRotator(0, 90, 0).Quaternion(), Meshes.Altar);
                else Table(115, 218, 74, true);
                Box(WineCloth, FVector(0, 53, 116), FVector(78, 75, 2));
                Box(WineCloth, FVector(0, 91, 92), FVector(78, 2.3, 47));
                Box(Brass, FVector(0, 92.4, 77), FVector(70, .8, 2.2));
                Box(DarkOak, FVector(0, 55, 122), FVector(61, 43, 10), 4);
                Box(Paper, FVector(0, 55, 127.8), FVector(51, 36, 2), 4);
                Disc(Wax, FVector(17, 47, 130), 12, 2.5);
                for (double X : {-148., 148.})
                {
                    if (Meshes.Candlestick)
                        Part(EShape::Tube, Iron, FVector(X, 52, 67), FVector(37, 37, 104),
                            FQuat::Identity, Meshes.Candlestick);
                    else Disc(Iron, FVector(X, 52, 67), 24, 104);
                    Candle(FVector(X, 52, 119), 25, true);
                }
                Vessel(FVector(69, 66, 115), 31, 32);
                Solid(FVector(0, 53, 62), FVector(212, 71, 104));
                Objects += 6;
                break;
            }
            case EKit::Reliquary:
                if (Meshes.Altar)
                {
                    ++Objects;
                    Part(EShape::Box, Stone, FVector(0, 36, 39), FVector(67, 182, 78),
                        FRotator(0, 90, 0).Quaternion(), Meshes.Altar);
                }
                else Table(78, 182, 64, true);
                for (const double X : {-66., 66.})
                {
                    if (Meshes.Candlestick)
                    {
                        ++Objects;
                        Part(EShape::Tube, Iron, FVector(X, 35, 91), FVector(12, 12, 26),
                            FQuat::Identity, Meshes.Candlestick);
                        Candle(FVector(X, 35, 104), 9 + Variant % 6, true);
                    }
                    else Candle(FVector(X, 35, 78), 19, true);
                }
                Book(FVector(0, 34, 78), FVector2D(44, 33), Variant % 11 - 5, true);
                Vessel(FVector(29, 34, 78), 18, 22);
                break;
            case EKit::Cot:
                if (Meshes.Bed)
                {
                    Solid(FVector(-12, 48, 33), FVector(186, 78, 64));
                    ++Objects;
                    Part(EShape::Box, Oak, FVector(-12, 48, 37), FVector(83, 194, 74),
                        FRotator(0, 90, 0).Quaternion(), Meshes.Bed);
                }
                Crate(FVector(114, 29, 1), 36, 34, 30, -4);
                Vessel(FVector(114, 31, 31), 16, 21);
                break;
            case EKit::WallLedge:
            {
                const double Height = 85. + Variant % 4 * 26.;
                ++Objects;
                Box(Stone, FVector(0, 14, Height - 4), FVector(108, 29, 8));
                for (const double X : {-36., 36.})
                    Box(Stone, FVector(X, 9, Height - 15), FVector(15, 18, 15));
                Candle(FVector(-35, 15, Height), 10 + Variant % 8, true);
                if (Meshes.LowVessel)
                {
                    ++Objects;
                    Part(EShape::Tube, Clay, FVector(8, 15, Height + 10), FVector(22, 22, 20),
                        FQuat::Identity, Meshes.LowVessel);
                }
                Book(FVector(35, 15, Height), FVector2D(21, 15), Variant % 13 - 6, Variant % 2 == 0);
                break;
            }
            case EKit::ToolBench:
                Table(74, 168, 58, false, true);
                Box(Oak, FVector(0, 9, 116), FVector(165, 8, 75));
                for (double X : {-68., 68.}) Box(Iron, FVector(X, 15, 100), FVector(8, 11, 83));
                Box(Iron, FVector(0, 17, 130), FVector(148, 7, 6));
                for (double X : {-54., -16., 29.})
                    Box(DarkOak, FVector(X, 20, 109), FVector(5, 9, 36), X * .34);
                Box(Iron, FVector(-53, 35, 78), FVector(31, 7, 6), -13);
                Box(ChippedStone, FVector(20, 35, 83), FVector(36, 26, 17), 14);
                Cask(FVector(107, 35, 1), 24, 58, true);
                Candle(FVector(62, 27, 74), 13, true, true);
                Solid(FVector(0, 29, 39), FVector(160, 56, 74));
                ++Objects;
                break;
            case EKit::MortarTrough:
                Box(ChippedStone, FVector(0, 48, 15), FVector(161, 91, 30));
                Box(DampStone, FVector(0, 48, 31), FVector(151, 82, 2.5));
                for (double X : {-74., 74.}) Box(ChippedStone, FVector(X, 47, 38), FVector(15, 79, 15));
                for (double Y : {11., 85.}) Box(ChippedStone, FVector(0, Y, 38), FVector(140, 13, 15));
                Box(Water, FVector(0, 48, 33), FVector(129, 65, 1.1));
                Crate(FVector(-113, 26, 1), 51, 43, 41, 9);
                Vessel(FVector(-110, 23, 43), 24, 32);
                Box(DarkOak, FVector(24, 51, 44), FVector(120, 6, 5), -18);
                Solid(FVector(0, 48, 19), FVector(158, 87, 38));
                ++Objects;
                break;
            case EKit::NicheCache:
                Crate(FVector(-26, 25, 1), 65, 39, 47, -6);
                Crate(FVector(-21, 24, 48), 47, 31, 26, 8);
                Vessel(FVector(35, 27, 1), 36, 53);
                Vessel(FVector(61, 23, 1), 23, 23);
                Box(Linen, FVector(-24, 26, 75), FVector(28, 17, 3.5), 16);
                break;
            case EKit::LinenCare:
            {
                Table(58, 167, 63, false);
                const auto ClothFold = [&](double X, double Y, double Z, double Width)
                {
                    Box(Linen, FVector(X, Y, Z), FVector(Width, 36, 5), 7.);
                    Box(BlueCloth, FVector(X + 2, Y + 1, Z + 4.2), FVector(Width * .78, 30, 4), -7.);
                };
                ClothFold(-23, 36, 61, 86);
                ClothFold(50, 30, 60, 46);
                Box(BlueCloth, FVector(-19, 67, 50), FVector(68, 2.5, 23), -1);
                Crate(FVector(-113, 38, 1), 37, 35, 30, -13);
                Box(Oak, FVector(109, 53, 33), FVector(48, 34, 8), 6);
                for (double X : {93., 124.}) Box(DarkOak, FVector(X, 53, 15), FVector(9, 29, 29), 6);
                Solid(FVector(0, 34, 28), FVector(165, 61, 56));
                Objects += 5;
                break;
            }
            case EKit::ProvisionTable:
                Table(88, 199, 76, false);
                Box(Linen, FVector(-27, 40, 88.7), FVector(112, 77, 1.6), 2);
                for (int32 I = 0; I < 3; ++I)
                {
                    Disc(Clay, FVector(-57, 32, 90. + I * 3.), 24. - I, 3.2);
                    Vessel(FVector(-18 + I * 29., 56 - I * 10., 89), 17. + I * 4, 17. + I * 5);
                }
                Crate(FVector(94, 23, 1), 49, 44, 40, 17);
                Crate(FVector(43, 39, 1), 66, 49, 54, -8);
                Candle(FVector(59, 62, 88), 20, true, true);
                Solid(FVector(0, 39, 41), FVector(195, 72, 84));
                Objects += 3;
                break;
            case EKit::FolioDesk:
                if (Meshes.Table)
                    Part(EShape::Box, Oak, FVector(0, 21, 44), FVector(184, 72, 88),
                        FQuat::Identity, Meshes.Table);
                else Table(88, 184, 72, false);
                Box(BlueCloth, FVector(12, 23, 89), FVector(86, 69, 1.7), 1);
                Book(FVector(4, 21, 90), FVector2D(58, 44), 9, true);
                Book(FVector(-6, 19, 96), FVector2D(49, 37), -8, false);
                Box(Paper, FVector(63, 17, 89.4), FVector(42, 36, 1.4), 14);
                Disc(Wax, FVector(69, 4, 92), 11, 3);
                Candle(FVector(-63, 41, 88), 22, true, true);
                Box(Oak, FVector(-15, 103, 40), FVector(116, 27, 9));
                for (double X : {-56., 25.}) Box(DarkOak, FVector(X, 103, 18), FVector(10, 23, 35));
                Solid(FVector(0, 21, 42), FVector(180, 70, 84));
                Solid(FVector(-15, 103, 20), FVector(113, 26, 43));
                Objects += 4;
                break;
            case EKit::SealedCabinet:
                Box(DarkOak, FVector(0, 3, 58), FVector(132, 61, 110));
                Box(Oak, FVector(0, 3, 117), FVector(144, 72, 9));
                for (double X : {-31., 31.})
                {
                    Box(Oak, FVector(X, 35, 59), FVector(59, 6, 93), X < 0 ? 3 : -4);
                    for (double Z : {38., 79.}) Box(Iron, FVector(X, 39, Z), FVector(56, 4, 5));
                    Box(Brass, FVector(X * .30, 42, 60), FVector(10, 6, 19));
                }
                Vessel(FVector(49, 4, 121), 29, 29);
                Box(Linen, FVector(-29, 5, 123), FVector(55, 42, 4), -9);
                Book(FVector(-17, 10, 126), FVector2D(36, 29), 7, false);
                Solid(FVector(0, 4, 58), FVector(133, 62, 113));
                Objects += 4;
                break;
            case EKit::SortingCart:
            {
                Box(DarkOak, FVector(0, 0, 43), FVector(127, 65, 12));
                Box(Oak, FVector(0, 0, 51), FVector(129, 66, 6));
                Box(Oak, FVector(0, 33, 53), FVector(133, 6, 12));
                Box(Iron, FVector(0, 0, 18), FVector(94, 66, 7));
                for (double X : {-44., 44.}) for (double Y : {-25., 25.})
                    Part(EShape::Tube, Iron, FVector(X, Y, 17), FVector(31, 31, 7),
                        FRotator(0, 0, 90).Quaternion());
                Crate(FVector(-29, 2, 54), 53, 45, 32, -8);
                Book(FVector(42, 2, 54), FVector2D(36, 29), 13, false);
                Book(FVector(39, 4, 60), FVector2D(30, 22), -9, true);
                Solid(FVector(0, 0, 27), FVector(123, 64, 54));
                ++Objects;
                break;
            }
            case EKit::DampMasonry:
            case EKit::BrokenMasonry:
            {
                const bool bDamp = Kit == EKit::DampMasonry;
                const int32 Missing = 2 + Variant % 3;
                for (int32 I = 0; I < 7; ++I)
                {
                    const double X = -95. + 31. * I;
                    const int32 Irregular = (Variant * 11 + I * 17) % 13;
                    const double Height = 14. + Irregular * 1.7;
                    if (I == Missing)
                    {
                        // The missing skirting block leaves a shallow, shifted spall on the paving.
                        Box(ChippedStone, FVector(X - 7, 53, 5), FVector(26, 22, 7), -23);
                        Box(ChippedStone, FVector(X + 18, 60, 3.6), FVector(14, 17, 5.5), 31);
                        continue;
                    }
                    const double Y = 18. + (I % 3) * 3. + (I < Missing ? 0. : 5.);
                    Box(bDamp ? DampStone : ChippedStone, FVector(X, Y, Height * .5),
                        FVector(29. + I % 2 * 3., 23. + Irregular * .65, Height), (I % 3 - 1) * 4.5);
                    if (bDamp && I < Missing + 2)
                    {
                        // Uneven wetted stone follows the joints up out of the lowest course.
                        const double Rise = 11. + (I == 1 ? 16. : Irregular);
                        Box(DampStone, FVector(X + 6, 10, Height + Rise * .5 - 1.),
                            FVector(17. + I % 2 * 9., 14., Rise), -4.5 + I);
                    }
                }
                ++Objects;
                break;
            }
            case EKit::KitchenHearth:
            {
                Box(Stone, FVector(0, 36, 23), FVector(232, 82, 46));
                Box(Stone, FVector(0, 39, 49), FVector(243, 92, 7));
                Box(ChippedStone, FVector(0, 71, 55), FVector(165, 39, 7));
                Box(DampStone, FVector(0, 21, 108), FVector(207, 26, 116));
                for (double X : {-97., 97.})
                {
                    Box(Stone, FVector(X, 35, 109), FVector(30, 58, 112));
                    Box(Stone, FVector(X, 45, 58), FVector(42, 75, 17));
                }
                Box(Stone, FVector(0, 36, 159), FVector(231, 63, 20));
                Box(Stone, FVector(0, 43, 174), FVector(246, 80, 12));
                Box(Iron, FVector(0, 54, 95), FVector(163, 4, 4));
                Vessel(FVector(-22, 54, 56), 53, 37);
                Vessel(FVector(103, 31, 174), 25, 28);
                for (int32 I = 0; I < 8; ++I)
                {
                    const double X = -66. + I * 16., Y = 64. + (I * 7) % 10;
                    Box(DarkOak, FVector(X, Y, 56), FVector(22, 9, 10), I * 27.);
                    if (I % 3 == 0) Box(Flame, FVector(X, Y - 1., 61), FVector(9, 4, 1.8), I * 27.);
                }
                Sack(FVector(-151, 46, 0), 46, 59, false);
                Sack(FVector(145, 67, 0), 49, 48, true);
                Beam(Oak, FVector(-101, 72, 57), FVector(-125, 74, 116), 3.8, 3.6);
                Solid(FVector(0, 40, 88), FVector(229, 78, 176));
                ++Objects;
                break;
            }
            case EKit::PantryRack:
            {
                Box(DarkOak, FVector(0, 8, 81), FVector(153, 8, 153));
                for (double X : {-72., 72.})
                {
                    Box(Oak, FVector(X, 22, 81), FVector(9, 38, 162));
                    Box(Iron, FVector(X, 42, 17), FVector(12, 4, 27));
                }
                for (double Z : {12., 60., 111., 158.})
                    Box(Oak, FVector(0, 23, Z), FVector(151, 39, 6));
                Crate(FVector(-41, 25, 15), 44, 33, 32, -4);
                Sack(FVector(31, 21, 15), 34, 36, true);
                Vessel(FVector(-40, 21, 63), 26, 37);
                Vessel(FVector(-9, 27, 63), 14, 25);
                Vessel(FVector(24, 21, 63), 27, 33);
                Vessel(FVector(53, 27, 63), 16, 23);
                Loaf(FVector(-41, 25, 114), 37, 17);
                Loaf(FVector(0, 21, 114), 33, -10);
                Sack(FVector(39, 23, 114), 31, 32, Variant % 2 == 0);
                Solid(FVector(0, 22, 81), FVector(151, 36, 161));
                ++Objects;
                break;
            }
            case EKit::WorkBunk:
            {
                for (double X : {-101., 101.})
                    for (double Y : {4., 89.})
                        Box(DarkOak, FVector(X, Y, 79), FVector(9, 11, 157));
                for (double Z : {28., 112.})
                {
                    Box(Oak, FVector(0, 47, Z), FVector(208, 93, 13));
                    Box(Linen, FVector(0, 47, Z + 11), FVector(193, 79, 9));
                    Box(Linen, FVector(-69, 45, Z + 21), FVector(45, 70, 12), 6);
                    const ESurface Quilt = (Variant + int32(Z)) % 2 ? OliveCloth : WineCloth;
                    Box(Quilt, FVector(30, 50, Z + 17), FVector(136, 81, 4), -1);
                    Box(Quilt, FVector(38, 89, Z + 7), FVector(130, 3.5, 22), -1);
                    Box(Linen, FVector(79, 90.8, Z + 10), FVector(6, .9, 19), -1);
                    ++Objects;
                }
                for (int32 Step = 0; Step < 4; ++Step)
                    Box(Oak, FVector(72, 104 - 3.8 * Step, 21. + 25. * Step), FVector(30, 7, 5));
                for (double X : {55., 90.})
                    Beam(DarkOak, FVector(X, 107, 0), FVector(X, 87, 119), 5.1, 5.1);
                Crate(FVector(-129, 50, 1), 38, 38, 31, -4);
                Solid(FVector(0, 46, 70), FVector(205, 94, 139));
                ++Objects;
                break;
            }
            case EKit::PassageHooks:
            {
                const double Height = 165. + 6. * (Variant % 5);
                Box(Oak, FVector(0, 6, Height), FVector(138, 10, 12));
                for (double X : {-50., -13., 28., 55.})
                {
                    Beam(Iron, FVector(X, 9, Height), FVector(X, 25, Height + 2), 4, 4);
                    Disc(Iron, FVector(X, 24, Height + 5), 4.4, 8);
                }
                const ESurface Cloth = Variant % 2 ? WineCloth : OliveCloth;
                for (int32 Fold = -1; Fold <= 1; ++Fold)
                {
                    const double Drop = 67. + 8. * (2 - FMath::Abs(Fold));
                    Box(Cloth, FVector(-48. + 12. * Fold, 21. + 3. * (Fold + 1), Height - Drop * .5),
                        FVector(13.2, 3.5, Drop), Fold * 6.5);
                }
                Box(Linen, FVector(-47, 24, Height - 63), FVector(28, 1, 3));
                const double X = Variant % 3 == 0 ? -12. : 45.;
                Beam(Oak, FVector(X - 7, 18, 6), FVector(X + 6, 22, Height - 5), 3.7, 4.5);
                Part(EShape::Cone, Linen, FVector(X - 7, 19, 12), FVector(28, 20, 24));
                ++Objects;
                break;
            }
            case EKit::SealDesk:
            {
                Table(86, 201, 81, false);
                Box(WineCloth, FVector(32, 45, 86.9), FVector(110, 82, 1.8), -3);
                Box(Leather, FVector(-46, 37, 88), FVector(63, 49, 4), 3);
                for (double X : {-62., -30.})
                    Box(Paper, FVector(X, 37, 91.5), FVector(31, 45, 2.5), 3);
                for (int32 I = 0; I < 6; ++I)
                    Box(DarkOak, FVector(-62, 21. + 5. * I, 93), FVector(24. - I, .7, .3), 3);
                Vessel(FVector(60, 60, 86), 18, 16);
                Scroll(FVector(23, 30, 88), 41, 18);
                Scroll(FVector(40, 48, 88), 34, -26);
                for (double X : {45., 61., 76.})
                    Disc(Leather, FVector(X, 35, 88.5), 8., 2.2);
                Disc(Brass, FVector(27, 56, 91), 10, 9);
                Beam(Iron, FVector(50, 26, 88), FVector(65, 21, 100), 2.2, 2.2);
                Part(EShape::Ball, Wax, FVector(68, 20, 102), FVector(10, 3.2, 2.1), FRotator(9, -22, 0).Quaternion());
                Candle(FVector(-79, 59, 86), 17, true);
                Solid(FVector(0, 42, 42), FVector(196, 80, 84));
                Objects += 4;
                break;
            }
            case EKit::Conservation:
            {
                Table(89, 235, 87, false);
                Box(BlueCloth, FVector(-42, 43, 90), FVector(132, 88, 2), 2);
                Box(Leather, FVector(-40, 38, 93), FVector(71, 49, 5), -9);
                Box(Paper, FVector(-44, 38, 96), FVector(64, 42, 2.5), -9);
                for (int32 Line = 0; Line < 5; ++Line)
                    Box(DarkOak, FVector(-48, 25. + Line * 6., 97.5), FVector(36. - Line * 3., 1.1, .25), -9);
                for (int32 I = 0; I < 4; ++I)
                    Box(Paper, FVector(35 + 7 * I, 61 - 11 * I, 90.7 + .35 * I),
                        FVector(22. + I * 4., 32, .7), -10 + I * 11);
                Vessel(FVector(82, 62, 89), 23, 22);
                Beam(Wax, FVector(69, 51, 96), FVector(104, 30, 91), 2.7, 2.7);
                Beam(Oak, FVector(58, 74, 90), FVector(102, 75, 91), 4.8, 5.7);
                Candle(FVector(-92, 64, 90), 15, true);
                Crate(FVector(83, 30, 1), 55, 48, 39, 7);
                Book(FVector(78, 32, 41), FVector2D(39, 30), -10, true);
                Solid(FVector(0, 46, 42), FVector(231, 86, 85));
                Objects += 5;
                break;
            }
            case EKit::CatalogCabinet:
            {
                Box(DarkOak, FVector(0, 28, 60), FVector(151, 56, 117));
                Box(Oak, FVector(0, 28, 120), FVector(161, 65, 7));
                Box(Oak, FVector(0, 29, 8), FVector(160, 64, 13));
                for (int32 Row = 0; Row < 4; ++Row)
                    for (double X : {-46., 0., 46.})
                    {
                        const double Z = 27. + Row * 22.;
                        Box(Oak, FVector(X, 59. + ((Variant + Row) % 3 == 0 && X < 0. ? 4. : 0.), Z),
                            FVector(43, 6.5, 19.5));
                        Box(Brass, FVector(X, 65, Z + 2), FVector(9, 5, 4));
                        if (Row >= 2) Box(Paper, FVector(X + 12, 62.9, Z + 3.2), FVector(9, .6, 4.7));
                    }
                Scroll(FVector(-43, 32, 124), 36, -16);
                Book(FVector(20, 30, 123.5), FVector2D(60, 36), 9, true);
                Book(FVector(24, 27, 129), FVector2D(44, 29), -12, false);
                Candle(FVector(58, 40, 123.5), 18, true);
                Solid(FVector(0, 30, 60), FVector(153, 59, 120));
                ++Objects;
                break;
            }
            case EKit::Tapestry:
            {
                const ESurface Fabric = Variant % 2 ? BlueCloth : WineCloth;
                Box(Iron, FVector(0, 12, 363), FVector(204, 7, 7));
                for (double X : {-89., 89.})
                {
                    Box(Iron, FVector(X, 8, 364), FVector(8, 16, 18));
                    Box(Brass, FVector(X, 17, 358), FVector(5, 2, 10));
                }
                for (int32 I = -2; I <= 2; ++I)
                {
                    const double Drop = 324. - 12. * FMath::Abs(I);
                    const double Y = I % 2 == 0 ? 15. : 17.;
                    Box(Fabric, FVector(I * 34, Y, 356. - Drop * .5), FVector(34.5, 6, Drop));
                    Box(Brass, FVector(I * 34, Y + 3.2, 362. - Drop), FVector(34, 1, 3));
                }
                for (double X : {-75., 75.})
                    Box(Brass, FVector(X, 18.6, 197), FVector(2.4, 1, 304));
                Box(Brass, FVector(0, 20.6, 340), FVector(150, 1, 3));
                ++Objects;
                break;
            }
            case EKit::Drain:
                Box(DampStone, FVector(0, 72, .8), FVector(136, 114, 1.6));
                Box(DampStone, FVector(-38, 26, 22), FVector(43, 16, 42), -8);
                Box(DampStone, FVector(1, 23, 12.5), FVector(32, 15, 24), 5);
                Box(ChippedStone, FVector(62, 111, 5), FVector(32, 24, 9), 13);
                Box(Water, FVector(0, 44, 1.65), FVector(120, 49, .65));
                for (double X : {-64., 64.})
                    Box(Stone, FVector(X, 55, 8), FVector(16, 96, 16));
                for (double Y : {40., 57., 74., 91.})
                    Box(Iron, FVector(0, Y, 5), FVector(120, 4, 5));
                for (double X : {-33., 25.})
                    Box(CourtSlate, FVector(X, 129, .55), FVector(9, 52, 1.1), X * .43);
                ++Objects;
                break;
            case EKit::CourtStation:
                ++Objects;
                Box(Stone,FVector(0,37,18),FVector(165,74,36));
                Box(Stone,FVector(0,38,39),FVector(174,77,7));
                for (int32 Side : {-1,1})
                {
                    const FVector At(Side * 56,40,43);
                    Disc(Stone,At+FVector(0,0,5),25,10);
                    Candle(At+FVector(0,0,10),15 + Variant % 6,true);
                }
                Vessel(FVector(0,40,43),32,41);
                Book(FVector(36,61,43),FVector2D(20,15),-11,true);
                for (int32 I = 0; I < 4; ++I)
                {
                    ++Objects;
                    Box(Stone,FVector(-69+I*46,10,61),FVector(24,20,46),I % 2 ? 2 : -3);
                }
                break;
            case EKit::GardenBorder:
                ++Objects;
                Box(Stone, FVector(0, 24, 11), FVector(104, 40, 22));
                Box(Stone, FVector(0, 24, 24), FVector(110, 46, 5));
                for (int32 I = 0; I < 3; ++I)
                    Vessel(FVector(-34 + I * 34, 23, 27), 19 + I * 3, 20 + (I + Variant) % 4 * 7);
                Candle(FVector(-47, 30, 27), 7, true);
                Candle(FVector(45, 17, 27), 10, false);
                for(int32 Side : {-1,1})
                    for(int32 I = 0; I < 4; ++I)
                    {
                        const double X = Side * (12. + I * 24.);
                        ++Objects;
                        Part(EShape::Box, Stone, FVector(X, 28., 1.9 + I % 3),
                            FVector(13. + I % 3 * 4., 11., 4. + I % 3), FRotator(0,Variant*17 + I*11,0).Quaternion());
                    }
                break;
            case EKit::Garden:
                Table(60, 151, 52, false, true);
                Cask(FVector(-105, 31, 1), 19, 40 + Variant % 6, true);
                Vessel(FVector(-53, 29, 60), 26, 27);
                Vessel(FVector(-13, 35, 60), 22, 22);
                Vessel(FVector(24, 20, 60), 19, 19);
                Crate(FVector(22, 26, 1), 37, 26, 32, 0);
                Book(FVector(52, 31, 60), FVector2D(25, 18), -11, false);
                break;
            case EKit::PumpGear:
            {
                Box(ChippedStone, FVector(0, 27, 26), FVector(157, 53, 52));
                Box(DampStone, FVector(0, 9, 103), FVector(123, 15, 109));
                const FVector Hub(0, 20, 126);
                const FQuat Axle(FVector::ForwardVector, UE_HALF_PI);
                Part(EShape::Tube, Iron, Hub, FVector(23, 23, 32), Axle);
                for (int32 Tooth = 0; Tooth < 10; ++Tooth)
                {
                    const double Angle = Tooth * (2. * PI / 10.);
                    const auto Rim = [&](double Radians)
                    {
                        return Hub + FVector(FMath::Cos(Radians) * 43., 0, FMath::Sin(Radians) * 43.);
                    };
                    Beam(Iron, Rim(Angle), Rim(Angle + 2. * PI / 10.), 6, 7);
                    if (Tooth % 2 == 0) Beam(Iron, Hub, Rim(Angle), 3, 4);
                }
                Beam(Iron, Hub + FVector(40, 0, -4), Hub + FVector(40, 22, -4), 7, 7);
                Disc(Brass, FVector(49, 6, 79), 9, 67);
                for (double Z : {59., 79., 101.})
                    Box(Iron, FVector(49, 11, Z), FVector(16, 13, 4));
                Cask(FVector(-93, 27, 1), 18, 44, true);
                Solid(FVector(0, 27, 26), FVector(155, 51, 51));
                Objects += 3;
                break;
            }
            case EKit::HoldingCellSeat:
            case EKit::CellSeat:
            {
                const bool bHoldingCell = Kit == EKit::HoldingCellSeat;
                if (bHoldingCell)
                {
                    Box(DampStone, FVector(0, 29, 16), FVector(70, 42, 32));
                    Box(ChippedStone, FVector(0, 30, 34), FVector(84, 49, 6));
                    Box(Leather, FVector(26, 31, 38), FVector(24, 14, 3), -19);
                    Box(Iron, FVector(15, 33, 38), FVector(15, 6, 2), -19);
                }
                else
                {
                    Box(DarkOak, FVector(0, 30, 33), FVector(84, 49, 7));
                    for (double X : {-29., 29.})
                        Box(Oak, FVector(X, 30, 15), FVector(9, 30, 30));
                    Box(Linen, FVector(14, 32, 38), FVector(46, 37, 2.4), -8);
                    Box(Linen, FVector(26, 56, 26), FVector(24, 2, 24));
                }
                if (Meshes.WashBowl)
                {
                    Part(EShape::Tube, Clay, FVector(-22, 35, 43), FVector(28, 28, 13),
                        FQuat::Identity, Meshes.WashBowl);
                    ++Objects;
                }
                Vessel(FVector(35, 14, 1), 23, 32);
                if (!bHoldingCell) Candle(FVector(35, 18, 37), 6, false);
                Solid(FVector(0, 30, 19), FVector(82, 47, 37));
                Objects += 2;
                break;
            }
            case EKit::DutyBoard:
            {
                const double Height = 156. + 7. * (Variant % 3);
                const ESurface Metal = Variant % 3 == 0 ? Brass : Iron;
                Box(DarkOak, FVector(0, 4, Height), FVector(141, 8, 38));
                Box(Oak, FVector(0, 7, Height + 16), FVector(141, 4, 6));
                for (int32 Hook = 0; Hook < 4; ++Hook)
                {
                    const double X = -49. + Hook * 32.;
                    Box(Metal, FVector(X, 12, Height + 2), FVector(6, 12, 5));
                    if ((Hook + Variant) % 3 == 0) continue;
                    const double Drop = 13. + 4. * ((Hook + Variant) % 4);
                    Box(Hook % 2 == 0 ? Metal : Leather, FVector(X, 16, Height - Drop * .5 + 1),
                        FVector(4 + Hook * 2, 2, Drop), (Hook - 2) * 6.);
                    Disc(Metal, FVector(X + 2, 16, Height - Drop), 7 + Hook, 2);
                }
                Box(Paper, FVector(53, 10, Height - 11), FVector(19, 1, 27));
                Objects += 3;
                break;
            }
            case EKit::WayPlate:
            {
                // The two short cross-walls only have a narrow strip outside the doors.
                const double Height = 145. + (Variant % 4) * 14.;
                const double Width = 64. + 7. * (Variant % 3);
                Box(ChippedStone, FVector(0, 3.4, Height), FVector(Width, 6.8, 32));
                Box(Brass, FVector(0, 7.3, Height + 10), FVector(Width - 14, 1.2, 2.4));
                for (int32 Mark = 0; Mark < 3 + Variant % 2; ++Mark)
                    Box(Variant % 2 == 0 ? Iron : Brass, FVector(-22. + 13. * Mark, 7.4, Height - 4.),
                        FVector(2.5, 1.2, 7. + (Mark + Variant) % 5), 0);
                ++Objects;
                break;
            }
            case EKit::DocumentRail:
            {
                const double Height = 163. + 9. * (Variant % 4);
                Box(DarkOak, FVector(0, 5, Height + 23), FVector(158, 10, 10));
                Box(Oak, FVector(-4, 12, Height - 13), FVector(146, 23, 5));
                for (int32 Bundle = 0; Bundle < 3; ++Bundle)
                {
                    const double X = -53. + Bundle * 38.;
                    const double H = 27. + 4. * ((Variant + Bundle) % 4);
                    Box(Bundle % 2 ? BlueCloth : Leather, FVector(X, 13, Height + H * .5 - 10),
                        FVector(33, 9, H), Bundle * 6. - 7.);
                    Box(Paper, FVector(X, 18, Height + H * .5 - 9), FVector(24, 1.3, H - 8), Bundle * 6. - 7.);
                    Box(Brass, FVector(X + 1, 19, Height + 3), FVector(27, 1.2, 2));
                }
                Objects += 4;
                break;
            }
            case EKit::GlassRack:
            {
                Table(67, 176, 60, false, true);
                Box(Linen, FVector(18, 31, 67.8), FVector(121, 54, 1.6), -6);
                Box(Oak, FVector(0, 14, 77), FVector(165, 13, 18));
                for (int32 Glass = 0; Glass < 3; ++Glass)
                {
                    const double X = -51. + Glass * 54.;
                    const double W = 37. + 5. * (Glass % 2);
                    const double H = 77. - 15. * (Variant % 2 == 0 ? Glass : 2 - Glass);
                    const FQuat Lean(FVector::ForwardVector, FMath::DegreesToRadians(Glass == 1 ? 8. : -11.));
                    const FVector At(X, 21, 74. + H * .5);
                    Part(EShape::Box, DarkOak, At, FVector(W + 7, 5, H + 6), Lean);
                    Part(EShape::Box, MirrorGlass, At + Lean.RotateVector(FVector(0, 3.3, 0)), FVector(W, 1.4, H), Lean);
                    Part(EShape::Box, Brass, At + Lean.RotateVector(FVector(0, 4, H * -.5)),
                        FVector(W, 1.8, 2.5), Lean);
                }
                const double WorkSide = Variant % 2 == 0 ? 1. : -1.;
                Vessel(FVector(-WorkSide * 64, 44, 67), 18, 20);
                Box(BlueCloth, FVector(WorkSide * 52, 47, 70), FVector(38, 29, 3.5), WorkSide * 12);
                Beam(Iron, FVector(8, 44, 70), FVector(32, 54, 73), 3, 3);
                Solid(FVector(0, 32, 33), FVector(171, 57, 66));
                Objects += 6;
                break;
            }
            case EKit::MirrorVeil:
            case EKit::MourningHanging:
            {
                const bool bVeil = Kit == EKit::MirrorVeil;
                const double Drop = (bVeil ? 96. : 127.) + 18. * (Variant % 3);
                const ESurface Fabric = bVeil ? BlueCloth : WineCloth;
                const ESurface Thread = bVeil ? Iron : Brass;
                Box(Iron, FVector(0, 9, 263), FVector(158, 6, 6));
                for (double X : {-66., 66.})
                    Box(Brass, FVector(X, 8, 263), FVector(5, 16, 11));
                for (int32 Fold = -1; Fold <= 1; ++Fold)
                {
                    const double H = Drop - FMath::Abs(Fold) * 23.;
                    const double Y = Fold == 0 ? 12. : 9.;
                    Box(Fabric, FVector(Fold * 43., Y, 258. - H * .5), FVector(43.5, 4.4, H));
                    Box(Thread, FVector(Fold * 43., Y + 2.4, 262. - H), FVector(40, .7, 1.8));
                }
                for (double Tilt : {-45., 45.})
                    Part(EShape::Box, Thread, FVector(0, 14.5, 206), FVector(22, .8, 2),
                        FQuat(FVector::RightVector, FMath::DegreesToRadians(Tilt)));
                ++Objects;
                break;
            }
            case EKit::TribunalScale:
            {
                Table(83, 155, 63, true);
                Box(WineCloth, FVector(4, 35, 84), FVector(96, 59, 2));
                const FVector Crossbar(5, 34, 143);
                Disc(Stone, FVector(5, 34, 90), 28, 12);
                Disc(Brass, FVector(5, 34, 118), 8, 46);
                Beam(Brass, Crossbar + FVector(-47, 0, -4), Crossbar + FVector(48, 0, 5), 4, 4);
                for (const double X : {-40., 49.})
                {
                    const double Z = X > 0. ? 114. : 102.;
                    for (const double Y : {-10., 10.})
                        Beam(Iron, Crossbar + FVector(X, 0, X > 0. ? 5. : -4.),
                            FVector(X, 34 + Y, Z + 2), 1.5, 1.5);
                    Disc(Brass, FVector(X, 34, Z), 30, 4);
                }
                Book(FVector(-54, 20, 85), FVector2D(27, 18), -9, false);
                Candle(FVector(62, 49, 83), 12, true);
                Solid(FVector(0, 33, 41), FVector(151, 60, 82));
                Objects += 2;
                break;
            }
            case EKit::GardenSeat:
            {
                Box(ChippedStone, FVector(0, 32, 44), FVector(181, 53, 8));
                for (double X : {-63., 63.})
                    Box(Stone, FVector(X, 32, 20), FVector(27, 38, 40));
                Box(Linen, FVector(-37, 30, 49), FVector(78, 34, 2.2), 4);
                Part(EShape::Tube, OliveCloth, FVector(-53, 28, 55), FVector(14, 14, 31),
                    FQuat(FVector::ForwardVector, UE_HALF_PI));
                Vessel(FVector(104, 31, 1), 34, 41);
                Vessel(FVector(125, 54, 1), 17, 19);
                Solid(FVector(0, 32, 22), FVector(177, 50, 46));
                Objects += 3;
                break;
            }
            case EKit::BerthChain:
            {
                const double Height = 94. + 12. * (Variant % 4);
                Box(ChippedStone, FVector(0, 4, Height), FVector(177, 8, 26));
                for (double X : {-72., 72.})
                    Box(Iron, FVector(X, 10, Height), FVector(8, 9, 17));
                for (int32 LinkIndex = 0; LinkIndex < 8; ++LinkIndex)
                {
                    const double X = -61. + LinkIndex * 17.4;
                    const double Z = Height - 17. * (1. - FMath::Square(X / 72.));
                    if (LinkIndex % 2 == 0)
                        Part(EShape::Tube, Iron, FVector(X, 11, Z), FVector(12, 12, 3),
                            FQuat(FVector::ForwardVector, UE_HALF_PI));
                    else Box(Iron, FVector(X, 11, Z), FVector(13, 4, 2));
                }
                ++Objects;
                break;
            }
            }
        }
    };

    FBox InteractionBounds(const DuelNorthInteractables::FPlacement& Item)
    {
        const FVector Radius(Item.Size.X + 12, Item.Size.Y + 12, Item.Size.Z * .5 + 12);
        return FBox(-Radius, Radius).TransformBy(FTransform(Item.Rotation,
            Item.Position + FVector(0, 0, Item.Size.Z * .5)));
    }

    bool AlignWithWall(const FRoom& Room, FArrangement& Art)
    {
        if (!Room.bEnclose) return false;
        // Construction centres the wall panels on the layout boundary.
        // Set the backs of wall furnishings on the interior face of those panels.
        const double Inset = WallThickness * .5 + WallFaceClearance;
        const FBox Clear = Room.Bounds.ExpandBy(FVector(-Inset, -Inset, 0));
        FVector Offset = FVector::ZeroVector;
        for (int32 Axis = 0; Axis < 2; ++Axis)
        {
            Offset[Axis] = FMath::Max(Clear.Min[Axis] - Art.Bounds.Min[Axis], 0.) -
                FMath::Max(Art.Bounds.Max[Axis] - Clear.Max[Axis], 0.);
        }
        if (Offset.IsNearlyZero()) return false;
        Art.Translate(Offset);
        return true;
    }

    const UPrimitiveComponent* FindBlockingPart(UWorld& World, const FArrangement& Art, const FTransform& ToWorld,
        const FCollisionQueryParams& Query)
    {
        const FBox WorldBounds = Art.Bounds.TransformBy(ToWorld);
        if (!World.OverlapBlockingTestByChannel(WorldBounds.GetCenter(), FQuat::Identity, ECC_Pawn,
            FCollisionShape::MakeBox(WorldBounds.GetExtent()), Query)) return nullptr;

        // The broad bounds enclose the air under tables and between separate props.
        // Check the rendered pieces individually before rejecting the arrangement.
        TArray<FOverlapResult> Overlaps;
        for (const FPart& Part : Art.Parts)
        {
            const FVector Centre = ToWorld.TransformPosition(Part.Transform.GetLocation());
            const FQuat Rotation = ToWorld.GetRotation() * Part.Transform.GetRotation();
            const FVector Extent = Part.Transform.GetScale3D() * ToWorld.GetScale3D().GetAbs() * .5;
            Overlaps.Reset();
            if (World.OverlapMultiByChannel(Overlaps, Centre, Rotation, ECC_Pawn, FCollisionShape::MakeBox(Extent), Query))
                for (const FOverlapResult& Hit : Overlaps)
                    if (Hit.bBlockingHit && Hit.GetComponent()) return Hit.GetComponent();
        }
        return nullptr;
    }

    FBox ApproachBounds(const FVector& Position, const FVector& Front, const FVector& HalfFootprint, double PropHeight)
    {
        constexpr double SideMargin = 13.;
        constexpr double FrontClearance = 132.;
        const FVector Across(-Front.Y, Front.X, 0.);
        const double Height = FMath::Max(160., PropHeight + 50.);
        const FVector Extent = Across.GetAbs() * (HalfFootprint.X + SideMargin) +
            Front.GetAbs() * (HalfFootprint.Y + (FrontClearance + SideMargin) * .5) +
            FVector(0, 0, Height * .5);
        const FVector Mid = Position + Front * ((FrontClearance - SideMargin) * .5) + FVector(0, 0, Height * .5);
        return FBox(Mid - Extent, Mid + Extent);
    }

    bool ClearsPassage(const FRoom& Room, const FBox& Bounds)
    {
        if (Room.Kind != ERoomKind::Corridor && Room.Kind != ERoomKind::Gallery) return true;
        const FVector Size = Room.Bounds.GetSize();
        const int32 Across = Size.X < Size.Y ? 0 : 1;
        if (Size[Across] < 300.) return true;
        const double Center = Room.Bounds.GetCenter()[Across];
        return Bounds.Max[Across] <= Center - PassageClearance || Bounds.Min[Across] >= Center + PassageClearance;
    }

    bool ClearsPlacement(const FRoom& Room, const FBox& Bounds)
    {
        if (!Room.Bounds.ExpandBy(1.).IsInside(Bounds.Min) || !Room.Bounds.ExpandBy(1.).IsInside(Bounds.Max))
            return false;
        if (Room.Kind == ERoomKind::Combat && Bounds.Min.X < CombatClearX && Bounds.Max.X > -CombatClearX &&
            Bounds.Min.Y < CombatClearY && Bounds.Max.Y > -CombatClearY)
            return false;

        for (const FPortal& Portal : Portals())
        {
            if (Portal.RoomA != Room.Id && Portal.RoomB != Room.Id) continue;
            const FVector Side = FVector(-Portal.Normal.Y, Portal.Normal.X, 0).GetAbs() * (Portal.Width * .5 + 23.);
            const FVector Extent = FVector(0, 0, Portal.Height * .5) + Portal.Normal.GetAbs() * 112. + Side;
            const FVector Mid = Portal.Center + FVector(0, 0, Portal.Height * .5);
            if (Bounds.Intersect(FBox(Mid - Extent, Mid + Extent))) return false;
        }
        if (!ClearsPassage(Room, Bounds)) return false;
        for (const DuelNorthInteractables::FPlacement& Item : DuelNorthInteractables::Placements())
        {
            const FBox Reserved = Item.IsArchiveVolume() ?
                ApproachBounds(Item.Position + Item.Front() * (Item.Size.Y + 13.), Item.Front(),
                    FVector(Item.Size.X, 0, 0), Item.MountHeight + Item.Size.Z) : Item.LedgerTitle ?
                ApproachBounds(Item.Position, Item.Front(), Item.Size, Item.Size.Z) :
                InteractionBounds(Item);
            if (Bounds.Intersect(Reserved)) return false;
        }
        for (const DuelNorthFinds::FFind& Find : DuelNorthFinds::All())
            if (Bounds.Intersect(ApproachBounds(Find.Position, Find.Front(), Find.Size * .5, Find.Size.Z))) return false;
        for (const FLandmark& Landmark : Landmarks())
        {
            if (Landmark.Floor != Room.Floor) continue;
            const auto* Ledger = DuelNorthInteractables::Ledger(Landmark.Id);
            if (Ledger && Ledger->IsArchiveVolume()) continue;
            const FVector Extent(92, 92, 160);
            if (Bounds.Intersect(FBox(Landmark.Position - Extent, Landmark.Position + Extent))) return false;
        }
        if (const FRoute* Route = FindRoute(TEXT("MainWalk")))
        {
            const FBox Sweep = Bounds.ExpandBy(FVector(WalkClearance, WalkClearance, 0));
            for (int32 I = 1; I < Route->Points.Num(); ++I)
            {
                const FVector& A = Route->Points[I - 1];
                const FVector& B = Route->Points[I];
                if (FMath::Abs(A.Z - Room.Bounds.Min.Z) > 1. || FMath::Abs(B.Z - A.Z) > 1.) continue;
                // Vertical strip rejects even the player/companion's path under a shelf.
                const FVector Mid = Sweep.GetCenter();
                const FVector Start(A.X, A.Y, Mid.Z), End(B.X, B.Y, Mid.Z);
                if (FMath::LineBoxIntersection(Sweep, Start, End, End - Start)) return false;
            }
        }
        return true;
    }
}

void Build(AActor* Owner, USceneComponent* Parent)
{
    if (!Owner || !Parent || !Owner->GetWorld()) return;
    const auto Colour = [Owner](float R, float G, float B, float Roughness, float Metallic = 0.f)
    {
        return DuelGeometry::ColorMaterial(Owner, FLinearColor(R, G, B), false, Roughness, Metallic);
    };
    auto* Damp = UMaterialInstanceDynamic::Create(DuelGeometry::ArtMaterial(TEXT("M_NorthWall")), Owner);
    if (Damp)
    {
        Damp->SetVectorParameterValue(TEXT("StoneTint"), FLinearColor(.33f,.42f,.40f));
        Damp->SetScalarParameterValue(TEXT("WorldTileCm"), 110.f);
    }
    UMaterialInterface* Materials[SurfaceCount] = {
        DuelGeometry::ArtMaterial(TEXT("M_NorthOak")), DuelGeometry::ArtMaterial(TEXT("MI_NorthDarkOak")),
        Colour(.085f, .088f, .090f, .38f, .81f), Colour(.32f, .185f, .063f, .35f, .70f),
        DuelGeometry::ArtMaterial(TEXT("M_NorthDressedStone")), Colour(.47f, .34f, .20f, .86f),
        Colour(.76f, .57f, .30f, .52f), DuelGeometry::ArtMaterial(TEXT("M_NorthCandle")),
        Colour(.024f, .061f, .056f, .048f), Colour(.33f, .105f, .044f, .84f),
        Colour(.20f, .040f, .028f, .66f), Colour(.036f, .045f, .050f, .37f),
        DuelGeometry::ColorMaterial(Owner, FLinearColor(.29f, .26f, .20f), false, .94f, 0.f, true),
        Colour(.26f, .32f, .37f, .091f, .92f),
        DuelGeometry::ColorMaterial(Owner, FLinearColor(.25f, .054f, .057f), false, .93f, 0.f, true),
        DuelGeometry::ColorMaterial(Owner, FLinearColor(.052f, .123f, .151f), false, .92f, 0.f, true),
        DuelGeometry::ArtMaterial(TEXT("M_RuinedWall")),
        Damp ? static_cast<UMaterialInterface*>(Damp) : DuelGeometry::ArtMaterial(TEXT("M_NorthDressedStone")),
        DuelGeometry::ArtMaterial(TEXT("M_NorthDungeonFlame")),
        Colour(.26f, .105f, .036f, .91f),
        DuelGeometry::ColorMaterial(Owner, FLinearColor(.151f, .172f, .084f), false, .94f, 0.f, true)
    };

    const auto LoadProp = [](const TCHAR* Name)
    {
        return LoadObject<UStaticMesh>(nullptr,
            *FString::Printf(TEXT("/Game/MedievalDungeon/Meshes/Props/%s.%s"), Name, Name));
    };
    const FPropMeshes PropMeshes{LoadProp(TEXT("SM_Candle")), LoadProp(TEXT("SM_Pot_C_Complete")),
        LoadProp(TEXT("SM_Handcuff")), LoadProp(TEXT("SM_Table")), LoadProp(TEXT("SM_Alter")),
        LoadProp(TEXT("SM_Bed")), LoadProp(TEXT("SM_CandleStick_Iron")), LoadProp(TEXT("SM_Pot_B_Complete")),
        LoadObject<UStaticMesh>(nullptr, TEXT("/Game/DuelArt/NorthTower/Archive/Shelves/wooden_bookshelf_worn/StaticMeshes/wooden_bookshelf_worn.wooden_bookshelf_worn")),
        LoadProp(TEXT("SM_Gargoyle_Statue_On_Stand")),
        LoadObject<UStaticMesh>(nullptr, TEXT("/Game/DuelArt/NorthTower/Meshes/SM_NorthCaskOpen.SM_NorthCaskOpen")),
        LoadObject<UStaticMesh>(nullptr, TEXT("/Game/DuelArt/NorthTower/Meshes/SM_NorthCaskClosed.SM_NorthCaskClosed")),
        LoadProp(TEXT("SM_Pot_A_Complete"))};
    if (!PropMeshes.OpenCask || !PropMeshes.ClosedCask)
        UE_LOG(LogTemp, Error, TEXT("North dressing is missing an authored cask mesh"));

    struct FBatch
    {
        UHierarchicalInstancedStaticMeshComponent* Component = nullptr;
        TArray<FTransform> Instances;
    };
    TMap<int32, FBatch> Batches;
    TMap<UStaticMesh*, FBatch> AuthoredBatches;
    TArray<FBox> Occupied;
    struct FRoomUsage { int32 Sites = 0; int32 Objects = 0; };
    TMap<FName, FRoomUsage> RoomUsage;
    int32 StandingFurniture = 0;
    int32 ShelfVolumes = 0;
    int32 HallSites[2][2] = {};
    int32 PassageSites[2] = {};
    int32 ObjectCount = 0, ExpectedParts = 0, InstalledParts = 0, WetCount = 0, Blocked = 0, WallAligned = 0;
    TArray<FSite> FurnishingSites;
    using namespace DuelNorthArchive;
    for (int32 Side : {-1, 1})
        for (int32 Row = 0; Row < UE_ARRAY_COUNT(Rows); ++Row)
            for (int32 Bay = 0; Bay < Rows[Row].Bays; ++Bay)
            {
                const auto* Volume = Algo::FindByPredicate(Volumes, [&](const FVolume& Item)
                { return Item.Side == Side && Item.Row == Row && Item.Bay == Bay; });
                FurnishingSites.Add({TEXT("ChapelHall"), BayPosition(Side, Row, Bay), Rows[Row].Yaw, EKit::LibraryStack, 0., Volume});
            }
    const int32 StackSites = FurnishingSites.Num();
    FurnishingSites.Append(Sites, UE_ARRAY_COUNT(Sites));
    FurnishingSites.Append(RoomLife, UE_ARRAY_COUNT(RoomLife));
    const int32 SiteCount = FurnishingSites.Num();
    int32 FloorObjects[int32(EFloor::Unknown)] = {};
    const FTransform ToWorld = Parent->GetComponentTransform();
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(NorthDressing), false);
    for (int32 I = 0; I < SiteCount; ++I)
    {
        const FSite& Site = FurnishingSites[I];
        const FRoom* Room = FindRoom(Site.Room);
        if (!Room) continue;
        FArrangement Art{FVector(Site.At, Room->Bounds.Min.Z + ShelfFoot + Site.Height), FRotator(0, Site.Yaw, 0).Quaternion(),
            PropMeshes};
        Art.Create(Site.Kit, I < StackSites ? I : I - StackSites / 2, Site.Volume);
        WallAligned += AlignWithWall(*Room, Art) ? 1 : 0;
        if (!ClearsPlacement(*Room, Art.Bounds) || Occupied.ContainsByPredicate([&](const FBox& Existing)
            { return Art.Bounds.ExpandBy(4.).Intersect(Existing); }))
        {
#if WITH_DEV_AUTOMATION_TESTS
            UE_LOG(LogTemp, Display, TEXT("North dressing clearance skipped %s site %d near %s"),
                *Site.Room.ToString(), int32(Site.Kit), *Art.Bounds.GetCenter().ToString());
#endif
            continue;
        }
        if (const UPrimitiveComponent* Blocker = FindBlockingPart(*Owner->GetWorld(), Art, ToWorld, Query))
        {
            ++Blocked;
            UE_LOG(LogTemp, Display, TEXT("North dressing skipped %s at %s because %s overlaps %s"),
                *Site.Room.ToString(), *Art.Bounds.GetCenter().ToString(), *GetNameSafe(Blocker), *Blocker->Bounds.GetBox().ToString());
            continue;
        }

        Occupied.Add(Art.Bounds);
        if (Room->Id == TEXT("ChapelHall"))
        {
            // Use the installed wick locations after wall alignment and clearance checks.
            // Close-set wicks on a single stand share one small source.
            TArray<FVector> LitWicks;
            for (const FPart& Part : Art.Parts)
            {
                if (Part.Surface != Flame && Part.Surface != LiveFlame) continue;
                const FVector Wick = Part.Transform.GetLocation();
                if (LitWicks.ContainsByPredicate([&](const FVector& Other)
                    { return FVector::DistSquared(Wick, Other) < FMath::Square(55.); })) continue;
                LitWicks.Add(Wick);
                const FBox& Shell = Room->Bounds;
                const double ToWall = FMath::Min(FMath::Min(Wick.X - Shell.Min.X, Shell.Max.X - Wick.X),
                    FMath::Min(Wick.Y - Shell.Min.Y, Shell.Max.Y - Wick.Y));
                auto* Glow = NewObject<UPointLightComponent>(Owner);
                Glow->SetupAttachment(Parent);
                Glow->SetMobility(EComponentMobility::Movable);
                Glow->SetRelativeLocation(Wick + FVector(0, 0, 4));
                Glow->SetIntensityUnits(ELightUnits::Candelas);
                Glow->SetIntensity(10.f);
                Glow->SetUseTemperature(true);
                Glow->SetTemperature(2850.f);
                Glow->SetAttenuationRadius(FMath::Clamp(ToWall, 12., 235.));
                Glow->SetSourceRadius(4.f);
                Glow->SetSpecularScale(.22f);
                Glow->SetCastShadows(false);
                Glow->SetVolumetricScatteringIntensity(0.f);
                Glow->SetIndirectLightingIntensity(0.f);
                Glow->SetMaxDrawDistance(1900.f);
                Glow->SetMaxDistanceFadeRange(450.f);
                Glow->ComponentTags.Add(TEXT("NorthArchiveCandle"));
                Owner->AddInstanceComponent(Glow);
                Glow->RegisterComponent();
            }
        }
        FRoomUsage& Usage = RoomUsage.FindOrAdd(Room->Id);
        ++Usage.Sites;
        Usage.Objects += Art.Objects;
        if (Room->Floor == EFloor::Prison || Room->Floor == EFloor::Chapel)
        {
            const int32 FloorIndex = Room->Floor == EFloor::Prison ? 0 : 1;
            if (Room->Kind == ERoomKind::Hall)
                ++HallSites[FloorIndex][Site.At.Y < 0. ? 0 : 1];
            else if (Room->Kind == ERoomKind::Corridor || Room->Kind == ERoomKind::Gallery)
                ++PassageSites[FloorIndex];
        }
        ShelfVolumes += Art.ShelfVolumes;
        if (!Art.SolidPieces.IsEmpty() || !Art.LegacySolidPieces.IsEmpty()) ++StandingFurniture;
        for(int32 Kind=0;Kind<2;++Kind)
        for (const FTransform& Solid : (Kind?Art.LegacySolidPieces:Art.SolidPieces))
        {
            const FBox NewBounds=FBox(FVector(-.5),FVector(.5)).TransformBy(Solid);
            if(Kind && Art.SolidPieces.ContainsByPredicate([&](const FTransform& Existing)
                {const FBox B=FBox(FVector(-.5),FVector(.5)).TransformBy(Existing);return B.IsInsideOrOn(NewBounds.Min) && B.IsInsideOrOn(NewBounds.Max);}))continue;
            auto* Block = NewObject<UBoxComponent>(Owner);
            Block->SetupAttachment(Parent);
            Block->SetMobility(EComponentMobility::Static);
            Block->SetRelativeLocationAndRotation(Solid.GetLocation(), Solid.GetRotation());
            Block->SetBoxExtent(Solid.GetScale3D() * .5);
            Block->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Block->SetCollisionObjectType(ECC_WorldStatic);
            Block->SetCollisionResponseToAllChannels(ECR_Ignore);
            Block->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
            Block->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
            if (Site.Kit == EKit::LibraryStack)
            {
                Block->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
                Block->ComponentTags.Add(TEXT("NorthArchiveRack"));
            }
            Block->SetCanEverAffectNavigation(false);
            Block->ComponentTags.Add(FurnitureTag);
            Block->ComponentTags.Add(Site.Room);
            if(Kind){Block->ComponentTags.Add(TEXT("NorthLegacyCollisionShell"));Block->ComponentTags.Add(TEXT("NorthLegacyFurniture"));}
            Owner->AddInstanceComponent(Block);
            Block->RegisterComponent();
        }
        ObjectCount += Art.Objects;
        FloorObjects[int32(Room->Floor)] += Art.Objects;
        ExpectedParts += Art.Parts.Num();
        for (const FPart& Part : Art.Parts)
        {
            const FVector Size = Part.Transform.GetScale3D();
            const double FaceArea = FMath::Max3(Size.X * Size.Y, Size.X * Size.Z, Size.Y * Size.Z);
            const bool bLarge = Part.AuthoredMesh ? Part.Surface == Oak || Part.Surface == Stone : FaceArea >= 1650.;
            const int32 Key = (int32(Part.Shape) * SurfaceCount + Part.Surface) * 2 + int32(bLarge);
            FBatch& Group = Part.AuthoredMesh ? AuthoredBatches.FindOrAdd(Part.AuthoredMesh) : Batches.FindOrAdd(Key);
            auto*& Batch = Group.Component;
            if (!Batch)
            {
                UStaticMesh* Mesh = Part.AuthoredMesh ? Part.AuthoredMesh : DuelGeometry::Mesh(Part.Shape);
                if (!Mesh || (!Part.AuthoredMesh && !Materials[Part.Surface]))
                {
                    UE_LOG(LogTemp, Error, TEXT("North dressing is missing a mesh or material for batch %d"), Key);
                    continue;
                }
                Batch = NewObject<UHierarchicalInstancedStaticMeshComponent>(Owner);
                Batch->bAutoRebuildTreeOnInstanceChanges = false;
                Batch->SetupAttachment(Parent);
                Batch->SetMobility(EComponentMobility::Static);
                Batch->SetStaticMesh(Mesh);
                if (!Part.AuthoredMesh) Batch->SetMaterial(0, Materials[Part.Surface]);
                else Batch->ComponentTags.Add(TEXT("NorthMedievalDungeonProp"));
                Batch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                Batch->bDisallowNanite = Part.Surface == LiveFlame;
                Batch->SetCanEverAffectNavigation(false);
                // Retain furniture silhouettes in shadows and reflections. Small book
                // bands, wax and trim only need their raster surface near the viewer.
                const bool bSurfaceDetail = Part.Surface == DampStone || Part.Surface == ChippedStone;
                Batch->SetCastShadow(bLarge && Part.Surface != Water && !bSurfaceDetail);
                Batch->SetVisibleInRayTracing((bLarge && !bSurfaceDetail) || Part.Surface == Water);
                Batch->SetAffectDynamicIndirectLighting(false);
                Batch->SetAffectDistanceFieldLighting(false);
                Batch->SetCullDistances(0, bLarge ? 5300 : Part.Surface == Water ? 4400 : Part.Surface == LiveFlame ? 2300 : 3400);
                Batch->ComponentTags.Add(DressingTag);
                if (Part.Surface == Water)
                {
                    // Real horizontal low-roughness dielectric, visible to the existing
                    // reflections and ray-traced view just like the nearby opaque props.
                    Batch->ComponentTags.Add(WaterTag);
                }
                Owner->AddInstanceComponent(Batch);
            }
            const FBox Source = Batch->GetStaticMesh()->GetBoundingBox();
            const FVector Scale = Part.Transform.GetScale3D() / Source.GetSize().ComponentMax(FVector(1.));
            const FQuat Rotation = Part.Transform.GetRotation();
            Group.Instances.Emplace(Rotation,
                Part.Transform.GetLocation() - Rotation.RotateVector(Source.GetCenter() * Scale), Scale);
        }
    }
    int32 AuthoredParts = 0;
    const auto RegisterBatch = [&](const FBatch& Group)
    {
        auto* Batch = Group.Component;
        if (!Batch) return;
        Batch->AddInstances(Group.Instances, false, false, false);
        Batch->BuildTreeIfOutdated(false, false);
        Batch->RegisterComponent();
        if (!Batch->IsRegistered()) return;
        InstalledParts += Batch->GetInstanceCount();
        if (Batch->ComponentHasTag(WaterTag)) WetCount += Batch->GetInstanceCount();
        if (Batch->ComponentHasTag(TEXT("NorthMedievalDungeonProp"))) AuthoredParts += Batch->GetInstanceCount();
    };
    for (const auto& Pair : Batches) RegisterBatch(Pair.Value);
    for (const auto& Pair : AuthoredBatches) RegisterBatch(Pair.Value);
    UE_LOG(LogTemp, Display, TEXT("North dressing by floor: B1=%d 1F=%d 2F=%d 3F=%d 4F=%d Garden=%d; native props=%d"),
        FloorObjects[0], FloorObjects[1], FloorObjects[2], FloorObjects[3], FloorObjects[4], FloorObjects[5], AuthoredParts);
    UE_LOG(LogTemp, Display, TEXT("North hall furnishing sites south/north: 1F %d/%d, 2F %d/%d. Passage sites: 1F %d, 2F %d. Shelved volumes: %d"),
        HallSites[0][0], HallSites[0][1], HallSites[1][0], HallSites[1][1], PassageSites[0], PassageSites[1], ShelfVolumes);
#if WITH_DEV_AUTOMATION_TESTS
    for (const FRoom& Room : Rooms())
    {
        if (Room.Kind == ERoomKind::Landing || Room.Kind == ERoomKind::Stairwell) continue;
        const FRoomUsage Usage = RoomUsage.FindRef(Room.Id);
        Owner->Tags.Add(FName(*FString::Printf(TEXT("NorthDressingRoom_%s=%d"), *Room.Id.ToString(), Usage.Sites)));
        UE_LOG(LogTemp, Display, TEXT("North room dressing %s: %d groups, %d objects"),
            *Room.Id.ToString(), Usage.Sites, Usage.Objects);
    }
    for (int32 Floor = 0; Floor < 2; ++Floor)
    {
        Owner->Tags.Add(FName(*FString::Printf(TEXT("NorthHallSouth%d=%d"), Floor, HallSites[Floor][0])));
        Owner->Tags.Add(FName(*FString::Printf(TEXT("NorthHallNorth%d=%d"), Floor, HallSites[Floor][1])));
        Owner->Tags.Add(FName(*FString::Printf(TEXT("NorthPassageSites%d=%d"), Floor, PassageSites[Floor])));
    }
    Owner->Tags.Add(FName(*FString::Printf(TEXT("NorthShelfVolumes=%d"), ShelfVolumes)));
    for (int32 Floor = 0; Floor < UE_ARRAY_COUNT(FloorObjects); ++Floor)
        Owner->Tags.Add(FName(*FString::Printf(TEXT("NorthFloorDressing%d=%d"), Floor, FloorObjects[Floor])));
#endif
    if (InstalledParts != ExpectedParts)
    {
        UE_LOG(LogTemp, Error, TEXT("North dressing registered %d of %d intended instances"), InstalledParts, ExpectedParts);
        ObjectCount = 0;
    }
#if WITH_DEV_AUTOMATION_TESTS
    Owner->Tags.Add(FName(*FString::Printf(TEXT("%s%d"), ObjectCountPrefix, ObjectCount)));
#endif
    UE_LOG(LogTemp, Display, TEXT("North dressing: %d objects at %d/%d sites, %d instances in %d batches, %d standing furniture groups, %d reflective water surfaces, %d wall alignments, %d physical conflicts skipped"),
        ObjectCount, Occupied.Num(), SiteCount, InstalledParts, Batches.Num() + AuthoredBatches.Num(), StandingFurniture, WetCount, WallAligned, Blocked);
}
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthDressingTest, "ManosabaDuel.NorthTower.RoomDressing",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDuelNorthDressingTest::RunTest(const FString& Parameters)
{
    using namespace DuelNorthDressing;
    ADuelNorthTower* Tower = nullptr;
    if (GEngine) for (const FWorldContext& Context : GEngine->GetWorldContexts())
        if (Context.World() && Context.World()->IsGameWorld())
            for (TActorIterator<ADuelNorthTower> It(Context.World()); It; ++It) { Tower = *It; break; }
    if (!TestTrue(TEXT("An isolated North Tower is running"), Tower && FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave"))))
        return false;

    int32 Parts = 0, Wet = 0, OpenCasks = 0, ClosedCasks = 0;
    TArray<FVector> ArchiveShelves;
    const auto CountTag = [&](FStringView Prefix)
    {
        for (FName Tag : Tower->Tags)
        {
            FString Count = Tag.ToString();
            if (Count.RemoveFromStart(Prefix)) return FCString::Atoi(*Count);
        }
        return 0;
    };
    int32 FurnishedRooms = 0;
    int32 CirculationRooms = 0;
    const TInlineComponentArray<UMeshComponent*> MeshComponents(Tower);
    for (const DuelNorthLayout::FRoom& Room : DuelNorthLayout::Rooms())
    {
        using DuelNorthLayout::ERoomKind;
        if (Room.Kind == ERoomKind::Stairwell || Room.Kind == ERoomKind::Landing)
        {
            ++CirculationRooms;
            continue;
        }
        if (Room.Floor == DuelNorthLayout::EFloor::Beacon)
        {
            const bool bInstalled = MeshComponents.ContainsByPredicate([&](const UMeshComponent* Mesh)
            {
                return Mesh->IsRegistered() && Mesh->ComponentHasTag(TEXT("NorthBeaconDressed")) && Mesh->ComponentHasTag(Room.Id);
            });
            TestTrue(FString::Printf(TEXT("%s has installed beacon-specific furnishings"), *Room.Id.ToString()), bInstalled);
            FurnishedRooms += bInstalled;
            continue;
        }
        const int32 RoomSites = CountTag(FString::Printf(TEXT("NorthDressingRoom_%s="), *Room.Id.ToString()));
        const int32 Minimum = Room.Kind == ERoomKind::Combat ? 10 : 1;
        TestTrue(*FString::Printf(TEXT("%s has at least %d installed furnishing groups"),
            *Room.Id.ToString(), Minimum), RoomSites >= Minimum);
        FurnishedRooms += RoomSites > 0;
    }
    AddInfo(FString::Printf(TEXT("Dressing covers %d rooms and passages. %d stairwells and turning landings retain full circulation width."),
        FurnishedRooms, CirculationRooms));
    const int32 Objects = CountTag(ObjectCountPrefix);
    const int32 ShelfVolumes = CountTag(TEXT("NorthShelfVolumes="));
    TestTrue(TEXT("Shelves carry at least seventeen hundred placed volumes"), ShelfVolumes >= 1700);
    for (int32 Floor : {0, 1})
    {
        TestTrue(TEXT("Both halves of the hall have installed furnishings"),
            CountTag(FString::Printf(TEXT("NorthHallSouth%d="), Floor)) >= 12 &&
            CountTag(FString::Printf(TEXT("NorthHallNorth%d="), Floor)) >= 10);
        TestTrue(TEXT("Each perimeter passage has installed work or storage stations"),
            CountTag(FString::Printf(TEXT("NorthPassageSites%d="), Floor)) >= 22);
    }
    TInlineComponentArray<UInstancedStaticMeshComponent*> Batches(Tower);
    for (const auto* Batch : Batches) if (Batch->ComponentHasTag(DressingTag))
    {
        TestTrue(TEXT("Dressing batches are registered and have no collision"),
            Batch->IsRegistered() && Batch->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
        Parts += Batch->GetInstanceCount();
        if (const UStaticMesh* Mesh = Batch->GetStaticMesh())
        {
            const FName Name = Mesh->GetFName();
            if (Name == TEXT("wooden_bookshelf_worn"))
                for (int32 Index = 0; Index < Batch->GetInstanceCount(); ++Index)
                {
                    FTransform Transform;
                    if (Batch->GetInstanceTransform(Index, Transform, true))
                        ArchiveShelves.Add(Transform.TransformPosition(Mesh->GetBoundingBox().GetCenter()));
                }
            if (Name == TEXT("SM_NorthCaskOpen")) OpenCasks += Batch->GetInstanceCount();
            if (Name == TEXT("SM_NorthCaskClosed")) ClosedCasks += Batch->GetInstanceCount();
        }
        if (Batch->ComponentHasTag(WaterTag))
        {
            TestTrue(TEXT("Barrel water participates in hardware ray tracing"), bool(Batch->bVisibleInRayTracing));
            Wet += Batch->GetInstanceCount();
        }
    }
    int32 DiningBlocks = 0, LibraryBlocks = 0, MirrorBlocks = 0;
    for (const auto* Block : TInlineComponentArray<UBoxComponent*>(Tower))
    {
        if (!Block->ComponentHasTag(FurnitureTag)) continue;
        TestTrue(TEXT("Standing furniture blocks pawn movement with a registered shape"),
            Block->IsRegistered() && Block->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block);
        const auto Floor = DuelNorthLayout::Locate(Block->GetComponentLocation(), Block->GetScaledBoxExtent().Z).Floor;
        DiningBlocks += Floor == DuelNorthLayout::EFloor::Prison;
        LibraryBlocks += Floor == DuelNorthLayout::EFloor::Chapel;
        MirrorBlocks += Floor == DuelNorthLayout::EFloor::Mirror;
    }
    TestTrue(TEXT("Living area and library both have accessible standing furniture"), DiningBlocks >= 6 && LibraryBlocks >= 6);
    TestTrue(TEXT("Mirror hall has a standing mirror"), MirrorBlocks >= 1);
    using namespace DuelNorthArchive;
    int32 BayCount = 0;
    for (int32 Side : {-1, 1})
        for (int32 Row = 0; Row < UE_ARRAY_COUNT(Rows); ++Row)
            for (int32 Bay = 0; Bay < Rows[Row].Bays; ++Bay)
            {
                ++BayCount;
                const FVector2D Expected = BayPosition(Side, Row, Bay);
                TestTrue(FString::Printf(TEXT("Imported archive bookcase on side %d, row %d, bay %d"), Side, Row, Bay),
                    ArchiveShelves.ContainsByPredicate([&](const FVector& Center) { return Expected.Equals(FVector2D(Center), .5); }));
            }
    TestEqual(TEXT("Both halves carry six full rows of the same imported bookshelves"), ArchiveShelves.Num(), BayCount);
    if (const auto* Mode = Tower->GetWorld()->GetAuthGameMode<ADuelMode>())
        if (const auto* Player = Mode->GetPlayerFighter())
        {
            const auto* Capsule = Player->GetCapsuleComponent();
            const float Radius = Capsule->GetScaledCapsuleRadius(), HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
            FCollisionQueryParams Query(SCENE_QUERY_STAT(NorthArchiveAisle), false, Player);
            Query.AddIgnoredActor(Mode->GetBossFighter());
            const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
            const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
            const auto IsClear = [&](const FVector2D& Start, const FVector2D& End)
            {
                const double Z = FloorZ + HalfHeight + 2.;
                FHitResult Hit;
                if (!Tower->GetWorld()->SweepSingleByChannel(Hit, FVector(Start, Z), FVector(End, Z),
                    FQuat::Identity, ECC_Pawn, Shape, Query, Responses)) return true;
                AddInfo(FString::Printf(TEXT("Archive aisle collision at %s with %s"),
                    *Hit.Location.ToCompactString(), *GetNameSafe(Hit.GetComponent())));
                return false;
            };
            TestTrue(TEXT("The player capsule crosses the main hall from west to east"), IsClear({-2260., 0.}, {2260., 0.}));
            for (int32 Side : {-1, 1})
            {
                for (double X : AisleX)
                {
                    if (X == 0.) continue;
                    TestTrue(FString::Printf(TEXT("The player capsule reaches the back of the aisle at X=%.1f on side %d"), X, Side),
                        IsClear({X, 0.}, {X, Side * AisleEnd}));
                }
                const double Cross = Side * CrossAisle;
                TestTrue(TEXT("The side cross aisle connects all three western rows"), IsClear({-1875., Cross}, {-700., Cross}));
                TestTrue(TEXT("The side cross aisle connects all three eastern rows"), IsClear({720., Cross}, {1895., Cross}));
            }
        }
    AddInfo(FString::Printf(TEXT("Standing furniture collision boxes: living=%d archive=%d mirror=%d. Imported archive shelves=%d."),
        DiningBlocks, LibraryBlocks, MirrorBlocks, ArchiveShelves.Num()));
    TestTrue(TEXT("Curated furniture fits the instance budget"), Objects > 0 && Parts >= Objects && Parts <= 12500);
    for (int32 Floor = 0; Floor < int32(DuelNorthLayout::EFloor::Unknown); ++Floor)
    {
        if (Floor == int32(DuelNorthLayout::EFloor::Beacon)) continue;
        const FString Prefix = FString::Printf(TEXT("NorthFloorDressing%d="), Floor);
        const int32 Count = CountTag(Prefix);
        TestTrue(*FString::Printf(TEXT("Storey %d has its authored room furnishings"), Floor), Count >= 12);
        AddInfo(FString::Printf(TEXT("Dressing storey %d: %d objects"), Floor, Count));
    }
    TestTrue(TEXT("Both authored cask meshes are installed"), OpenCasks >= 4 && ClosedCasks >= 4);
    TestTrue(TEXT("Open casks have registered water surfaces"), Wet >= OpenCasks);
    AddInfo(FString::Printf(TEXT("Authored casks: %d open and %d closed"), OpenCasks, ClosedCasks));
    AddInfo(FString::Printf(TEXT("North dressing installed %d objects in %d instances, with %d separate water surfaces"), Objects, Parts, Wet));
    return !HasAnyErrors();
}
#endif
