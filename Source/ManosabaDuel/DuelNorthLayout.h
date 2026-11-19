#pragma once

#include "CoreMinimal.h"

// Authored in centimetres. Enclosing wall panels are centred on the XY bounds.
namespace DuelNorthLayout
{
    enum class EFloor : uint8 { Dungeon, Prison, Chapel, Mirror, Final, Garden, Beacon, Unknown };
    inline constexpr EFloor MapOrder[] = {EFloor::Dungeon, EFloor::Prison, EFloor::Chapel, EFloor::Mirror, EFloor::Beacon, EFloor::Final, EFloor::Garden};
    enum class ERoomKind : uint8 { Hall, Combat, Cell, Corridor, Antechamber, Stairwell, Landing, Gallery, Garden };
    enum class EGate : uint8 { Open, DungeonFog, MirrorFog, FinalFog, DungeonWon, RustKey, MirrorKey, AllTokens,
        DungeonReturnShortcut, ChapelLoopShortcut, OpeningCell, FinalBattleExit };

    inline constexpr float WallThickness = 24.f;
    inline constexpr float FloorThickness = 40.f;
    inline constexpr float RoofThickness = 32.f;
    inline constexpr double ExteriorSightlineSlope = 1. / 8.;

    struct FRoom
    {
        FName Id;
        EFloor Floor = EFloor::Unknown;
        FBox Bounds;
        ERoomKind Kind = ERoomKind::Hall;
        bool bFloor = true;
        bool bRoof = true;
        bool bEnclose = true;
    };

    struct FCustodyLabel
    {
        FName Room;
        FName Door;
        const TCHAR* Number;
        const TCHAR* Name;
    };
    TConstArrayView<FCustodyLabel> CustodyLabels();

    struct FStairFlight
    {
        FName Id;
        FName CoreRoom;
        FName FromLanding;
        FName ToLanding;
        EFloor FromFloor = EFloor::Unknown;
        EFloor ToFloor = EFloor::Unknown;
        FVector Start;
        FVector End;
        float Width = 300.f;
        int32 StepCount = 0;
        float Headroom = 400.f;
        float Riser() const { return (End.Z - Start.Z) / StepCount; }
        float Tread() const { return FVector::Dist2D(Start, End) / StepCount; }
    };

    struct FPortal
    {
        FName Id;
        FName RoomA;
        FName RoomB;
        FVector Center; // Threshold / sill centre. Opening extends from Center.Z upwards.
        FVector Normal;
        float Width = 300.f;
        float Height = 340.f;
        EGate Gate = EGate::Open;
        bool bWindow = false;
    };

    struct FCheckpoint
    {
        FName Id;
        EFloor Floor = EFloor::Unknown;
        FVector Position; // Walking surface, add the player capsule half height when spawning.
    };

    struct FRoute
    {
        FName Id;
        TArray<FVector> Points; // Walking surface centreline; includes turns and every stair landing.
    };

    struct FLandmark
    {
        FName Id;
        EFloor Floor = EFloor::Unknown;
        FVector Position;
    };

    struct FShortcutLever
    {
        FName Id;
        FName Portal;
        FName ApproachRoom;
        EFloor Floor;
        FVector Position;
        FVector ApproachDirection; // Points from the closed panel into the usable side.
        uint8 ProgressIndex; // EDuelNorthShortcut order; layout stays independent of saved-state headers.
    };

    struct FLocation
    {
        EFloor Floor = EFloor::Unknown;
        FName Room;
        FName Stair;
        float SurfaceZ = 0.f;
        bool bOnStair = false;
    };

    const TArray<FRoom>& Rooms();
    const TArray<FStairFlight>& Stairs();
    const TArray<FPortal>& Portals();
    const TArray<FCheckpoint>& Checkpoints();
    const TArray<FRoute>& Routes();
    const TArray<FLandmark>& Landmarks();
    const TArray<FShortcutLever>& ShortcutLevers();
    const FShortcutLever* FindShortcutLever(FName Id);
    bool CanReachShortcutLever(const FShortcutLever& Lever, const FVector& ActorPosition, float CapsuleHalfHeight = 62.f);
    const FLandmark* FindLandmark(FName Id);
    const FRoom* FindRoom(FName Id);
    const FRoute* FindRoute(FName Id);
    FVector WindowOutward(const FPortal& Window, const FRoom& Room);
    bool IsExteriorWindow(const FPortal& Window);
    FLocation Locate(const FVector& ActorPosition, float CapsuleHalfHeight = 62.f);
    FVector CourtOrigin(int32 FightIndex);
    FVector SpawnSurface();
    FBox TowerBounds();
    const TCHAR* FloorName(EFloor Floor);
    // Data-level checks. Physical character traversal and camera checks remain runtime tests.
    bool Validate(TArray<FString>& Errors);
}
