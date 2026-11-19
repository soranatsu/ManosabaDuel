#include "DuelArena.h"

#include "DuelCastle.h"
#include "DuelGame.h"
#include "DuelGeometry.h"
#include "DuelNorthLayout.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LocalFogVolumeComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"

using DuelGeometry::EShape;

namespace
{
    // The stone meshes share projected UVs at the scans' real-world scale.
    struct FStoneBatch
    {
        TArray<FVector> Vertices;
        TArray<int32> Triangles;
        TArray<FVector> Normals;
        TArray<FVector2D> UVs;
        TArray<FLinearColor> Colors;
        TArray<FProcMeshTangent> Tangents;
        float UVScale;

        explicit FStoneBatch(float InUVScale) : UVScale(InUVScale) {}

        void Face(const FVector* Points, int32 Count, const FVector& Outside, float Tone = 1.f)
        {
            FVector Normal = FVector::CrossProduct(Points[1] - Points[0], Points[2] - Points[0]).GetSafeNormal();
            const bool bFlipped = FVector::DotProduct(Normal, Outside) < 0.f;
            if (bFlipped)
            {
                Normal *= -1.f;
            }

            const bool bFloor = FMath::Abs(Normal.Z) > .60;
            const FVector U = bFloor ? FVector::ForwardVector
                : FVector::CrossProduct(FVector::UpVector, Normal).GetSafeNormal();
            const FVector V = bFloor ? FVector::RightVector : -FVector::UpVector;
            const FVector Tangent = (U - Normal * FVector::DotProduct(U, Normal)).GetSafeNormal();
            const int32 First = Vertices.Num();
            for (int32 Index = 0; Index < Count; ++Index)
            {
                const FVector& Point = Points[Index];
                Vertices.Add(Point);
                Normals.Add(Normal);
                UVs.Emplace(FVector::DotProduct(Point, U) / UVScale, FVector::DotProduct(Point, V) / UVScale);
                const float Damp = FMath::PerlinNoise2D(FVector2D(Point.X, Point.Y) * .0024) * .5f + .5f;
                const float Shade = Tone * FMath::Lerp(.72f, 1.f, Damp);
                Colors.Emplace(Shade, FMath::Min(1.f, Shade * 1.03f), Shade, 1.f);
                Tangents.Emplace(Tangent, false);
            }
            for (int32 Index = 1; Index + 1 < Count; ++Index)
            {
                Triangles.Append({First, First + (bFlipped ? Index : Index + 1),
                    First + (bFlipped ? Index + 1 : Index)});
            }
        }

        void Stone(const FVector& Center, const FVector& Dimensions, const FRotator& Rotation,
            float Bevel, float Chip = 0.f)
        {
            const FVector Half = Dimensions * .5;
            const float Tone = .92f + .12f * FMath::PerlinNoise3D(Center * .013);
            const double Cut = FMath::Clamp<double>(Bevel, .2, FMath::Min3(Half.X, Half.Y, Half.Z) * .75);
            FVector Corners[4][8];
            const FRotationMatrix Matrix(Rotation);
            for (int32 Row = 0; Row < 4; ++Row)
            {
                const double Inset = Row == 0 || Row == 3 ? Cut : 0.0;
                const double X = Half.X - Inset;
                const double Y = Half.Y - Inset;
                const double Z = Row < 2 ? -Half.Z + (Row == 1 ? Cut : 0.0)
                    : Half.Z - (Row == 2 ? Cut : 0.0);
                const double C = FMath::Min(Cut + Chip, .74 * FMath::Min(X, Y));
                const FVector Profile[] = {
                    {-X + Cut, -Y, Z}, {X - C, -Y, Z}, {X, -Y + C, Z}, {X, Y - Cut, Z},
                    {X - Cut, Y, Z}, {-X + Cut, Y, Z}, {-X, Y - Cut, Z}, {-X, -Y + Cut, Z}
                };
                for (int32 Side = 0; Side < 8; ++Side)
                {
                    Corners[Row][Side] = Center + Matrix.TransformVector(Profile[Side]);
                }
            }
            Face(Corners[0], 8, -Matrix.GetUnitAxis(EAxis::Z), Tone);
            Face(Corners[3], 8, Matrix.GetUnitAxis(EAxis::Z), Tone);
            for (int32 Row = 0; Row < 3; ++Row)
            {
                for (int32 Side = 0; Side < 8; ++Side)
                {
                    const int32 Next = (Side + 1) % 8;
                    const FVector Quad[] = {Corners[Row][Side], Corners[Row][Next],
                        Corners[Row + 1][Next], Corners[Row + 1][Side]};
                    Face(Quad, 4, (Quad[0] + Quad[2]) * .5 - Center, Tone);
                }
            }
        }

        void Install(AActor* Owner, USceneComponent* Parent, UMaterialInterface* Material) const
        {
            UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Owner);
            Mesh->SetupAttachment(Parent);
            Mesh->SetMobility(EComponentMobility::Movable);
            Mesh->bCastShadowAsTwoSided = true;
            Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Mesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs,
                Colors, Tangents, false, false);
            Mesh->SetMaterial(0, Material);
            Owner->AddInstanceComponent(Mesh);
            Mesh->RegisterComponent();
        }
    };

    UStaticMesh* CourtScan(const TCHAR* Name)
    {
        const FString Path = FString::Printf(TEXT("/Game/DuelArt/Scans/%s/%s.%s"), Name, Name, Name);
        return LoadObject<UStaticMesh>(nullptr, *Path);
    }

    void PointedArch(FStoneBatch& Wall, FStoneBatch& Trim, const FVector& Origin, const FVector& Across,
        float HalfSpan, float Spring, float Depth, float StoneSize, bool bRuined)
    {
        const FVector Thickness = FVector::CrossProduct(Across, FVector::UpVector);
        const FRotator Orientation = FRotationMatrix::MakeFromXY(Across, Thickness).Rotator();
        const auto Arc = [&](int32 Index)
        {
            const double T = Index / 18.0;
            const double Horizontal = FMath::Lerp(-static_cast<double>(HalfSpan), static_cast<double>(HalfSpan), T);
            const double Offset = FMath::Abs(Horizontal) + HalfSpan * .44;
            const double Rise = FMath::Sqrt(FMath::Max(0.0, FMath::Square(HalfSpan * 1.44) - FMath::Square(Offset)));
            return Origin + Across * Horizontal + FVector(0, 0, Spring + Rise);
        };

        for (int32 Side : {-1, 1})
        {
            const FVector Base = Origin + Across * (Side * HalfSpan);
            for (float Z = 20.f; Z + 8.f < Spring;)
            {
                const float Grain = FMath::PerlinNoise3D((Base + FVector(0, 0, Z)) * .024);
                const float Course = FMath::Min(Spring - Z, 53.f + Grain * 24.f);
                Wall.Stone(Base + Across * (Grain * 6.f) + FVector(0, 0, Z + Course * .5f),
                    FVector(StoneSize + 29.f + Grain * 15.f, Depth + Grain * 13.f, Course + 1.f),
                    Orientation + FRotator(0, Grain * 2.f, 0), 3.f, FMath::Abs(Grain) * 21.f);
                Z += Course;
            }
            Trim.Stone(Base + FVector(0, 0, 17), FVector(StoneSize + 65.f, Depth + 57.f, 36.f), Orientation, 4.f);
            Trim.Stone(Base + FVector(0, 0, Spring - 10), FVector(StoneSize + 51.f, Depth + 38.f, 22.f), Orientation, 4.f);
        }
        for (int32 Index = 0; Index < 18; ++Index)
        {
            if (bRuined && Index >= 7 && Index <= 12)
            {
                continue;
            }
            const FVector A = Arc(Index);
            const FVector B = Arc(Index + 1);
            const FVector Delta = B - A;
            const FRotator Turn = FRotationMatrix::MakeFromXY(Delta, Thickness).Rotator();
            Wall.Stone((A + B) * .5, FVector(Delta.Size() + 6.f, Depth, StoneSize), Turn, 5.f, Index % 4);
            Trim.Stone((A + B) * .5 + Thickness * (Depth * .51), FVector(Delta.Size() + 5.f, 19.f, StoneSize * .57f), Turn, 3.f);
        }
    }
}

ADuelArena::ADuelArena()
{
    PrimaryActorTick.bCanEverTick = true;
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Court"));
    RootComponent = SceneRoot;
}

void ADuelArena::BeginPlay()
{
    Super::BeginPlay();
    LoadFootfalls();
    const ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    if (Mode && Mode->UsesNorthTower())
    {
        bCastleInterior = true;
        PrecacheSigilArtwork();
        LoadVoices();
        return;
    }
    bCastleInterior = Mode && Mode->UsesCastle();
    BuildCourt();
    BuildLight();
    if (!bCastleInterior)
    {
        BuildRain();
    }
    LoadVoices();
    if (USoundBase* RainLoop = LoadObject<USoundBase>(nullptr,
        TEXT("/Game/DuelArt/Audio/S_RainCourt.S_RainCourt"), nullptr, LOAD_NoWarn | LOAD_Quiet))
    {
        RainBed = UGameplayStatics::SpawnSound2D(this, RainLoop);
        SetRainMuted(false, 0.f);
    }
}

void ADuelArena::BuildCourt()
{
    using namespace DuelCourt;
    UBoxComponent* Ground = NewObject<UBoxComponent>(this, TEXT("GroundCollision"));
    Ground->SetupAttachment(SceneRoot);
    Ground->SetBoxExtent(FVector((5320.f - StairTopX) * .5f, 1710, 38));
    Ground->SetRelativeLocation(FVector((5320.f + StairTopX) * .5f, 0, -38));
    Ground->SetCollisionProfileName(TEXT("BlockAll"));
    Ground->SetHiddenInGame(true);
    AddInstanceComponent(Ground);
    Ground->RegisterComponent();

    const auto BlockPath = [&](const FVector& Position, const FVector& HalfSize,
        const FRotator& Rotation = FRotator::ZeroRotator, bool bGround = false)
    {
        UBoxComponent* Obstacle = NewObject<UBoxComponent>(this);
        Obstacle->SetupAttachment(SceneRoot);
        Obstacle->SetRelativeLocation(Position);
        Obstacle->SetRelativeRotation(Rotation);
        Obstacle->SetBoxExtent(HalfSize);
        Obstacle->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Obstacle->SetCollisionResponseToAllChannels(ECR_Ignore);
        Obstacle->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
        if (bGround)
        {
            Obstacle->SetCollisionObjectType(ECC_WorldStatic);
        }
        Obstacle->SetHiddenInGame(true);
        AddInstanceComponent(Obstacle);
        Obstacle->RegisterComponent();
        return Obstacle;
    };

    const auto CameraBoundary = [&](const FVector& Position, const FVector& HalfSize)
    {
        UBoxComponent* WallProbe = NewObject<UBoxComponent>(this);
        WallProbe->SetupAttachment(SceneRoot);
        WallProbe->SetRelativeLocation(Position);
        WallProbe->SetBoxExtent(HalfSize);
        WallProbe->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        WallProbe->SetCollisionResponseToAllChannels(ECR_Ignore);
        WallProbe->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
        WallProbe->SetHiddenInGame(true);
        AddInstanceComponent(WallProbe);
        WallProbe->RegisterComponent();
        return WallProbe;
    };
    CameraBoundary(FVector(-10370, 0, 80), FVector(40, 1300, 500));
    CameraBoundary(FVector(5130, 0, 340), FVector(40, 880, 440));
    for (float Side : {-1.f, 1.f})
    {
        CombatCameraWalls.Add(CameraBoundary(FVector(Side * (DuelCourt::CameraLimitX + 60.f), 0, 379),
            FVector(60, 1620, 500)));
        CombatCameraWalls.Add(CameraBoundary(FVector(0, Side * (DuelCourt::CameraLimitY + 50.f), 379),
            FVector(2300, 50, 500)));
        CameraBoundary(FVector(3880, Side * 825.f, 330), FVector(1320, 34, 420));
    }
    for (const UBoxComponent* Wall : CombatCameraWalls)
    {
        CombatCameraWallCenters.Add(Wall->GetRelativeLocation());
    }
    SetCombatCamera(false);

    FStoneBatch Floor(240.f);
    FStoneBatch LowFloor(240.f);
    FStoneBatch Steps(182.f);
    FStoneBatch ArchiveFloor(258.f);
    FStoneBatch HallFloor(180.f);
    FStoneBatch Wall(235.f);
    FStoneBatch Trim(171.f);
    FStoneBatch Ruin(201.f);
    FStoneBatch Flood(240.f);
    FStoneBatch Water(240.f);
    const FVector PassagePoints[] = {
        {4290, -595, 0}, {4670, -1140, 0}, {6840, -1140, 205}, DuelCourt::PassageTurn, {7100, 0, 205}, {7490, 0, 205}};
    for (int32 I = 0; I + 1 < UE_ARRAY_COUNT(PassagePoints); ++I)
    {
        const FVector A = PassagePoints[I];
        const FVector B = PassagePoints[I + 1];
        const FVector Axis = B - A;
        const FRotator Slope = Axis.Rotation();
        const FVector Across = FVector::CrossProduct(FVector::UpVector, Axis.GetSafeNormal2D());
        BlockPath((A + B) * .5 - Slope.RotateVector(FVector(0, 0, 24)),
            FVector(Axis.Size() * .5 + 190, 245, 24), Slope, true);
        const int32 Courses = FMath::CeilToInt(Axis.Size() / 134.0);
        for (int32 Step = 0; Step < Courses; ++Step)
        {
            const FVector At = FMath::Lerp(A, B, (Step + .5) / Courses);
            Steps.Stone(At - Slope.RotateVector(FVector(0, 0, 19)),
                FVector(Axis.Size() / Courses + 7, 484, 39), Slope, 5.f, (Step * 9 + I) % 18);
            for (float Side : {-1.f, 1.f})
            {
                if ((Step < 2 || Step >= Courses - 2) && I != 1 && I != 3)
                {
                    continue;
                }
                const FVector Edge = At + Across * Side * 254.f;
                Ruin.Stone(Edge + FVector(0, 0, 46), FVector(Axis.Size() / Courses + 9, 62, 89),
                    FRotator(0, Slope.Yaw, 0), 5.f, (Step * 13 + I) % 18);
                if (Step % 4 == 1)
                {
                    Trim.Stone(Edge + FVector(0, 0, 123), FVector(86, 78, 97), FRotator(0, Slope.Yaw, 0), 6.f, 9.f);
                }
            }
        }
        if (I == 1 || I == 3)
        {
            for (float Position : {.23f, .60f, .87f})
            {
                PointedArch(Ruin, Trim, FMath::Lerp(A, B, Position), Across, 260.f, 278.f, 145.f, 61.f, Position == .60f);
            }
        }
    }
    Wall.Stone(FVector(-350, 0, -195), FVector(11800, 10200, 244), FRotator::ZeroRotator, 6);
    // Cobblestone relief comes from the scan. A continuous surface keeps the dodge lane clear.
    constexpr float Tile = 185.f;
    for (float Y = -1665.f; Y < 1665.f; Y += Tile)
    {
        for (float X = StairTopX; X < 2540.f; X += Tile)
        {
            const FVector Quad[] = {{X, Y, -.35}, {X + Tile, Y, -.35},
                {X + Tile, Y + Tile, -.35}, {X, Y + Tile, -.35}};
            Floor.Face(Quad, 4, FVector::UpVector);
        }
    }

    // The lower gatehouse and the broad stairs share a walkable slope beneath the dressed treads.
    constexpr float LowEnd = -10410.f;
    constexpr float StepWidth = 492.f;
    BlockPath(FVector((LowEnd + StairBaseX) * .5f, 0, LowCourtZ - 42.f),
        FVector((StairBaseX - LowEnd) * .5f, 1240, 42), FRotator::ZeroRotator, true);
    const FVector Run(StairTopX - StairBaseX, 0, -LowCourtZ);
    const FRotator Slope = Run.Rotation();
    const FVector SlopeMid((StairTopX + StairBaseX) * .5f, 0, LowCourtZ * .5f);
    BlockPath(SlopeMid - Slope.RotateVector(FVector(0, 0, 24)), FVector(Run.Size() * .5 + 1, StepWidth, 24), Slope, true);
    const FVector Appeal = AppealOrigin;
    constexpr float HallStart = AppealRampEndX;
    constexpr float HallEnd = 3800.f;
    BlockPath(Appeal + FVector((HallStart + HallEnd) * .5f, 0, -35),
        FVector((HallEnd - HallStart) * .5f, 1460, 35), FRotator::ZeroRotator, true);
    BlockPath(Appeal + FVector((-3810.f + AppealRampStartX) * .5f, 0, AppealLowerZ - 35.f),
        FVector((AppealRampStartX + 3810.f) * .5f, 602, 35), FRotator::ZeroRotator, true);
    const FVector AppealRun(AppealRampEndX - AppealRampStartX, 0, -AppealLowerZ);
    const FRotator AppealSlope = AppealRun.Rotation();
    const FVector AppealMid((AppealRampEndX + AppealRampStartX) * .5f, 0, AppealLowerZ * .5f);
    BlockPath(Appeal + AppealMid - AppealSlope.RotateVector(FVector(0, 0, 23)),
        FVector(AppealRun.Size() * .5f + 1.f, 546, 23), AppealSlope, true);
    CameraBoundary(Appeal + FVector(-3890, 0, 145), FVector(45, 735, 420));
    CameraBoundary(Appeal + FVector(3800, 0, 455), FVector(70, 935, 530));
    for (float X = LowEnd; X < StairBaseX; X += Tile)
    {
        const float End = FMath::Min(StairBaseX, X + Tile);
        for (float Y = -1250.f; Y < 1250.f; Y += Tile)
        {
            const FVector Quad[] = {{X, Y, LowCourtZ - .25}, {End, Y, LowCourtZ - .25},
                {End, Y + Tile, LowCourtZ - .25}, {X, Y + Tile, LowCourtZ - .25}};
            LowFloor.Face(Quad, 4, FVector::UpVector, .85f);
        }
    }
    for (float X = HallStart; X < HallEnd; X += 228.f)
    {
        const double End = FMath::Min(HallEnd, X + 228.f);
        for (float Y = -1450; Y < 1450; Y += 173.f)
        {
            const FVector Quad[] = {Appeal + FVector(X, Y, -.26f), Appeal + FVector(End, Y, -.26f),
                Appeal + FVector(End, Y + 173.f, -.26f), Appeal + FVector(X, Y + 173.f, -.26f)};
            HallFloor.Face(Quad, 4, FVector::UpVector, .94f);
        }
    }
    for (float X = -3890; X < AppealRampStartX; X += 190.f)
    {
        const double End = FMath::Min(AppealRampStartX, X + 190.f);
        const FVector Quad[] = {Appeal + FVector(X, -662, AppealLowerZ), Appeal + FVector(End, -662, AppealLowerZ),
            Appeal + FVector(End, 662, AppealLowerZ), Appeal + FVector(X, 662, AppealLowerZ)};
        LowFloor.Face(Quad, 4, FVector::UpVector);
    }
    for (int32 I = 0; I < 11; ++I)
    {
        const float X = AppealRampStartX + (I + .5f) * (AppealRampEndX - AppealRampStartX) / 11.f;
        const float Z = FMath::Lerp(AppealLowerZ, 0.f, (I + 1) / 11.f);
        for (int32 Block = -1; Block <= 1; ++Block)
        {
            Steps.Stone(Appeal + FVector(X, Block * 377.f, Z - 16.f), FVector(62, 375, 31),
                FRotator(0, Random.FRandRange(-.27f, .27f), 0), 4.f, 7.f + (I + Block) % 4 * 3.f);
        }
    }
    PointedArch(Ruin, Trim, Appeal + FVector(GateX, 0, 0), FVector::RightVector, 272.f, 420.f, 234.f, 76.f, false);
    if (UMaterialInterface* Mist = DuelGeometry::ArtMaterial(TEXT("M_FogGate")))
    {
        AppealFog = DuelGeometry::Shape(this, SceneRoot, EShape::Plane, Appeal + FVector(GateX - 3.f, 0, 280),
            FVector(514, 556, 100), Mist,
            FRotationMatrix::MakeFromXY(FVector::RightVector, -FVector::UpVector).Rotator());
        AppealFog->SetCastShadow(false);
    }
    const FVector AppealRest = AppealCheckpoint().GetLocation();
    const FLinearColor RestGold(.94f, .72f, .39f);
    LightPlane(AppealRest + FVector(0, 0, -60), FVector2D(245), FRotator::ZeroRotator,
        EDuelLight::Seal, RestGold, 3.1f, .11f);
    for (const FVector& Across : {FVector::ForwardVector, FVector::RightVector})
    {
        LightPlane(AppealRest + FVector(0, 0, 73), FVector2D(266, 59),
            FRotationMatrix::MakeFromXY(FVector::UpVector, Across).Rotator(),
            EDuelLight::Shaft, RestGold, 1.4f, .08f);
    }
    for (float Side : {-1.f, 1.f})
    {
        for (float X : {-3510.f, -2775.f})
        {
            PointedArch(Ruin, Trim, Appeal + FVector(X, Side * 668.f, AppealLowerZ), FVector::ForwardVector,
                366.f, 342.f, 148.f, 67.f, Side < 0.f);
        }
        for (int32 I = 0; I < 5; ++I)
        {
            const float X = -1635.f + I * 810.f;
            PointedArch(Ruin, Trim, Appeal + FVector(X, Side * 1455.f, 0), FVector::ForwardVector,
                345.f, 492.f + (I % 2) * 64.f, 166.f, 78.f, Side > 0.f && I >= 2);
            for (int32 Course = 0; Course < (Side < 0.f ? 8 : 4 + I % 2); ++Course)
            {
                const float H = 77.f;
                Wall.Stone(Appeal + FVector(X + (Course % 2) * 28.f, Side * 1575.f, H * (.5f + Course)),
                    FVector(813, 168, H - .7f), FRotator(0, Side * .24f, 0), 5.f, (Course * 9 + I * 2) % 18);
            }
            const FVector Bench(X + 108.f, Side * 1220.f, 0);
            Trim.Stone(Appeal + Bench + FVector(0, 0, 20), FVector(362, 169, 43),
                FRotator::ZeroRotator, 5.f, I % 2 * 17.f);
            Ruin.Stone(Appeal + Bench + FVector(0, Side * 59.f, 65), FVector(342, 40, 117),
                FRotator(0, I * .66f, Side * 6.f), 7.f, I % 3 * 13.f);
        }
        for (int32 I = 0; I < 8; ++I)
        {
            const float X = 2360.f + I * 171.f;
            Wall.Stone(Appeal + FVector(X, Side * 728.f, 188), FVector(171, 139, 380),
                FRotator(0, Side * .45f * (I % 3), 0), 7.f, 23.f);
        }
    }
    for (float X : {-1240.f, -397.f, 451.f, 1280.f})
    {
        const FVector Base = Appeal + FVector(X, 0, 0);
        PointedArch(Ruin, Trim, Base, FVector::RightVector, 1495.f, 690.f, 193.f, 86.f, X > 0);
        if (X <= 0.f)
        {
            for (float Side : {-1.f, 1.f})
            {
                for (int32 I = 0; I < 4; ++I)
                {
                    const FVector Foot = Base + FVector(425, Side * (1180.f - I * 280.f), 0);
                    Ruin.Stone(Foot + FVector(0, 0, 1260.f + I * 180.f), FVector(872, 320, 83),
                        FRotator(0, .3f * I, Side * 24.f), 7.f, 17.f);
                }
            }
        }
    }
    PointedArch(Wall, Trim, Appeal + FVector(ArchiveGateX - 82.f, 0, 0), FVector::RightVector, 291.f, 462.f, 232.f, 91.f, false);
    PointedArch(Ruin, Trim, Appeal + FVector(2480, 0, 0), FVector::RightVector, 904.f, 579.f, 213.f, 116.f, false);
    PointedArch(Ruin, Trim, Appeal + FVector(2830, 0, 0), FVector::RightVector, 950.f, 620.f, 233.f, 99.f, true);
    for (float Side : {-1.f, 1.f})
    {
        for (int32 Course = 0; Course < 12; ++Course)
        {
            for (int32 I = 0; I < 3; ++I)
            {
                Ruin.Stone(Appeal + FVector(2575.f + (Course % 2) * 13.f,
                    Side * (453.f + I * 304.f), 34.f + Course * 70.f),
                    FVector(241, 303, 69), FRotator(0, Side * .25f, 0), 6.f, (Course * 7 + I * 5) % 22);
            }
        }
    }
    constexpr float DaisStart = 2710.f;
    constexpr float DaisTop = 3050.f;
    constexpr float DaisEnd = 3710.f;
    const FVector DaisRun(DaisTop - DaisStart, 0, VerdictRise);
    const FVector DaisMid((DaisStart + DaisTop) * .5f, 0, VerdictRise * .5f);
    BlockPath(Appeal + DaisMid - DaisRun.Rotation().RotateVector(FVector(0, 0, 21)),
        FVector(DaisRun.Size() * .5 + 1, 610, 21), DaisRun.Rotation(), true);
    BlockPath(Appeal + FVector((DaisTop + DaisEnd) * .5f, 0, VerdictRise - 29.f),
        FVector((DaisEnd - DaisTop) * .5f, 649, 29), FRotator::ZeroRotator, true);
    constexpr int32 VerdictSteps = 6;
    for (int32 I = 0; I < VerdictSteps; ++I)
    {
        const float Tread = (DaisTop - DaisStart) / VerdictSteps;
        const float X = DaisStart + Tread * (I + .5f);
        const float Top = VerdictRise * (I + 1) / VerdictSteps;
        for (float Y : {-431.f, -216.f, 0.f, 216.f, 431.f})
        {
            Trim.Stone(Appeal + FVector(X, Y, Top * .5f - 4.f), FVector(Tread + 2, 217, Top + 8.f),
                FRotator::ZeroRotator, 3.f, (I + FMath::RoundToInt(FMath::Abs(Y))) % 11);
        }
    }
    for (int32 I = 0; I < 4; ++I)
    {
        Trim.Stone(Appeal + FVector((DaisTop + DaisEnd) * .5f, (I - 1.5f) * 324.f, VerdictRise - 18.f),
            FVector(DaisEnd - DaisTop, 326, 36), FRotator::ZeroRotator, 4.f, 12.f + I * 3.f);
    }
    const FVector VerdictDesk = Appeal + FVector(VerdictDeskX, 0, VerdictRise);
    Trim.Stone(VerdictDesk + FVector(0, 0, 14), FVector(255, 396, 30), FRotator::ZeroRotator, 7.f, 17.f);
    Ruin.Stone(VerdictDesk + FVector(0, 0, 71), FVector(162, 328, 100), FRotator::ZeroRotator, 7.f, 15.f);
    Trim.Stone(VerdictDesk + FVector(0, 0, 127), FVector(224, 373, 24), FRotator::ZeroRotator, 5.f, 17.f);
    BlockPath(VerdictDesk + FVector(0, 0, 60), FVector(118, 200, 65));
    LightPlane(VerdictDesk + FVector(-16, 0, 142), FVector2D(140, 235), FRotator::ZeroRotator, EDuelLight::Seal,
        FLinearColor(.86f, .54f, .29f), 2.9f, .18f)->SetCastShadow(false);
    for (const FVector& Mark : RitualMarks)
    {
        const FVector At = Appeal + Mark;
        Trim.Stone(At + FVector(0, 0, 28), FVector(97, 108, 60), FRotator(0, 45, 0), 7.f, 12.f);
        BlockPath(At + FVector(0, 0, 27), FVector(43, 43, 29));
        LightPlane(At + FVector(0, 0, 61), FVector2D(89), FRotator(0, 45, 0), EDuelLight::Seal,
            FLinearColor(.57f, .74f, 1.f), 2.1f, .10f);
    }
    if (!bCastleInterior)
    {
        YukiRoute = BlockPath(Appeal + FVector(YukiArchiveExitX + 81, 0, VerdictRise + 144), FVector(42, 722, 145));
    }
    PointedArch(Ruin, Trim, Appeal + FVector(3708, 0, VerdictRise), FVector::RightVector, 282.f, 390.f, 151.f, 64.f, false);
    for (int32 Row = 0; Row < 8; ++Row)
    {
        for (int32 I = -3; I <= 3; ++I)
        {
            if (FMath::Abs(I) <= 1 && Row < 5)
            {
                continue;
            }
            Ruin.Stone(Appeal + FVector(3725, I * 202.f + Row % 2 * 14.f, 45.f + Row * 87.f),
                FVector(153, 205, 87), FRotator::ZeroRotator, 4.f, (I + Row * 3 + 9) % 18);
        }
    }
    const FVector Window = Appeal + FVector(3680, 0, 1320);
    constexpr float WindowCut = 655.f;
    for (int32 Course = 0; Course < 14; ++Course)
    {
        const float Z = 736.f + Course * 78.f;
        const float Height = FMath::Max(0.f, FMath::Abs(Z - 1320.f) - 39.f);
        const float Inner = FMath::Sqrt(FMath::Max(0.f, FMath::Square(WindowCut) - FMath::Square(Height)));
        const float Outer = 859.f - FMath::Max(0.f, Z - 1050.f) * .19f;
        for (float Side : {-1.f, 1.f})
        {
            for (float Y = Inner; Y < Outer - 6.f; Y += 146.f)
            {
                const float End = FMath::Min(Y + 147.f, Outer);
                Ruin.Stone(Appeal + FVector(3727.f + (Course % 2) * 13.f, Side * ((Y + End) * .5f), Z),
                    FVector(148, End - Y, 79), FRotator::ZeroRotator, 4.f, (Course * 13 + FMath::RoundToInt(Y)) % 21);
            }
        }
    }
    constexpr int32 Segments = 40;
    for (int32 I = 0; I < Segments; ++I)
    {
        const float Angle = (I + .5f) * 2.f * PI / Segments;
        const FVector Radial(0, FMath::Cos(Angle), FMath::Sin(Angle));
        const FVector Tangent(0, -Radial.Z, Radial.Y);
        const FRotator Masonry = FRotationMatrix::MakeFromXY(FVector::ForwardVector, Tangent).Rotator();
        Ruin.Stone(Window + Radial * 625.f, FVector(155, 103, 117), Masonry, 5.f, (I * 7) % 16);
        Trim.Stone(Window - FVector(82, 0, 0) + Radial * 575.f, FVector(29, 99, 35), Masonry, 3.f, 3.f);
    }
    if (UMaterialInterface* Glass = DuelGeometry::ArtMaterial(TEXT("M_WitnessGlass")))
    {
        DuelGeometry::Shape(this, SceneRoot, EShape::Plane, Window - FVector(91, 0, 0), FVector(1160, 1160, 100),
            Glass, FRotationMatrix::MakeFromXY(-FVector::RightVector, -FVector::UpVector).Rotator())->SetCastShadow(false);
    }
    for (float Side : {-1.f, 1.f})
    {
        PointedArch(Ruin, Trim, Appeal + FVector(3690, Side * 954.f, 0), FVector::RightVector,
            273.f, 707.f, 152.f, 74.f, Side > 0);
        Trim.Stone(Appeal + FVector(3312, Side * 572.f, VerdictRise + 21.f), FVector(342, 112, 44),
            FRotator::ZeroRotator, 6.f, 9.f);
    }

    UMaterialInterface* Brass = DuelGeometry::ColorMaterial(this, FLinearColor(.22f, .119f, .046f), false, .39f, .76f);
    UMaterialInterface* Wax = DuelGeometry::ColorMaterial(this, FLinearColor(.41f, .32f, .18f), false, .62f);
    UMaterialInterface* Flame = DuelGeometry::ColorMaterial(this, FLinearColor(1.0f, .61f, .23f, .96f), true);
    for (float Side : {-1.f, 1.f})
    {
        for (int32 Bank = 0; Bank < 4; ++Bank)
        {
            const bool bDais = Bank == 3;
            const FVector Base = Appeal + FVector(bDais ? 3312.f : -1020.f + 1045.f * Bank,
                Side * (bDais ? 571.f : 1137.f), bDais ? VerdictRise + 43.f : 0.f);
            if (!bDais)
            {
                Trim.Stone(Base + FVector(0, 0, 18), FVector(186, 111, 36), FRotator::ZeroRotator, 5.f, 15.f);
            }
            for (int32 I = 0; I < 7; ++I)
            {
                const FVector Foot = Base + FVector((I - 3) * 24.f, (I % 2) * 26.f - 13.f, bDais ? 0.f : 36.f);
                const float Height = 18.f + (I * 11 + Bank * 13) % 28;
                DuelGeometry::Shape(this, SceneRoot, EShape::Tube, Foot + FVector(0, 0, 2), FVector(21, 21, 6), Brass);
                DuelGeometry::Shape(this, SceneRoot, EShape::Tube, Foot + FVector(0, 0, Height * .5f + 4.f), FVector(9, 9, Height), Wax);
                DuelGeometry::Shape(this, SceneRoot, EShape::Ball, Foot + FVector(0, 0, Height + 10.f),
                    FVector(5, 5, 13), Flame, FRotator(-5.f + I, 0, 3.f))->SetCastShadow(false);
            }
            UPointLightComponent* Candlelight = NewObject<UPointLightComponent>(this);
            Candlelight->SetupAttachment(SceneRoot);
            Candlelight->SetRelativeLocation(Base + FVector(0, 0, 95));
            Candlelight->SetIntensity(bDais ? 1500.f : 910.f);
            Candlelight->SetLightColor(FLinearColor(1.f, .44f, .13f));
            Candlelight->SetAttenuationRadius(bDais ? 430.f : 315.f);
            Candlelight->SetCastShadows(false);
            AddInstanceComponent(Candlelight);
            Candlelight->RegisterComponent();
        }
    }
    constexpr int32 Risers = 15;
    const float Tread = (StairTopX - StairBaseX) / Risers;
    for (int32 I = 0; I < Risers; ++I)
    {
        const float X = StairBaseX + Tread * (I + .5f);
        const float Z = FMath::Lerp(LowCourtZ, 0.f, (I + 1) / static_cast<float>(Risers));
        for (int32 Stone = -2; Stone <= 2; ++Stone)
        {
            const float Y = Stone * (StepWidth * .4f);
            Steps.Stone(FVector(X + Random.FRandRange(-1.6f, 1.6f), Y, Z - 23 + Random.FRandRange(-1.3f, 1.3f)),
                FVector(Tread + 1.5f, StepWidth * .4f - 1.f, 45),
                FRotator(0, Random.FRandRange(-.85f, .85f), 0), 3.4f, 4.f + (I + Stone + 2) % 8);
        }
        for (float Side : {-1.f, 1.f})
        {
            Wall.Stone(FVector(X, Side * 512.f, Z - 74.f), FVector(Tread + 2.f, 110.f, 248),
                FRotator::ZeroRotator, 5.2f, I % 5 * 4.f);
            Trim.Stone(FVector(X, Side * 512.f, Z + 52.f), FVector(Tread + 1.f, 132.f, 25),
                FRotator(0, Side * .36f, 0), 4.f, I % 4);
        }
    }
    for (float Side : {-1.f, 1.f})
    {
        for (float X : {-9980.f, -9275.f, -8620.f})
        {
            const bool bWest = X < -9700.f;
            PointedArch(Ruin, Trim, FVector(X, Side * 1220.f, LowCourtZ), FVector::ForwardVector,
                bWest ? 283.f : 321.f, bWest ? 536.f : 414.f, 238.f, 82.f, !bWest && Side < 0);
            const float Width = bWest ? 560.f : 425.f;
            const float Depth = Side > 0.f ? 280.f : 195.f;
            for (int32 Course = 0; Course < (Side > 0.f ? 11 : 6); ++Course)
            {
                Ruin.Stone(FVector(X + Course * (Side > 0 ? 4.f : -11.f), Side * 1440.f,
                    LowCourtZ + 42.f + Course * 82.f), FVector(Width + (Course % 2) * 43.f, Depth, 82),
                    FRotator(0, Side * (Course % 3), 0), 7.f, 9.f + (Course * 5) % 27);
            }
        }
        const float BankY = Side * 850.f;
        for (float X = LowEnd; X < StairBaseX - 146.f; X += 151.f)
        {
            const float Width = 193.f + 33.f * FMath::Sin(X * .006f);
            const FVector Quad[] = {{X, BankY - Width, LowCourtZ + 1.4}, {X + 151, BankY - Width, LowCourtZ + 1.4},
                {X + 151, BankY + Width, LowCourtZ + 1.4}, {X, BankY + Width, LowCourtZ + 1.4}};
            Flood.Face(Quad, 4, FVector::UpVector, .84f);
        }
        Trim.Stone(FVector(-8620, Side * 634.f, LowCourtZ + 55.f), FVector(422, 235, 116),
            FRotator(0, Side * 10.f, 0), 12.f, 27.f);
    }
    PointedArch(Ruin, Trim, FVector(-8525, 0, LowCourtZ), FVector::RightVector, 525.f, 532.f, 272.f, 101.f, false);
    PointedArch(Ruin, Trim, FVector(-7455, 0, 0), FVector::RightVector, 515.f, 528.f, 242.f, 79.f, true);

    PointedArch(Wall, Trim, FVector(PrisonGateX, 0, 0), FVector::RightVector, 300.f, 418.f, 250.f, 79.f, false);
    PrisonGateBlocker = BlockPath(FVector(PrisonGateX, 0, 338), FVector(60, 280, 345));
    for (float Side : {-1.f, 1.f})
    {
        BlockPath(FVector(PrisonGateX, Side * 664.f, 318), FVector(125, 411, 325));
        for (int32 Course = 0; Course < 10; ++Course)
        {
            const float Z = 32.f + Course * 64.f;
            for (int32 Pier = 0; Pier < 3; ++Pier)
            {
                const float Y = Side * (433.f + 280.f * Pier + (Course % 2) * 13.f);
                Wall.Stone(FVector(PrisonGateX + (Course % 3) * 6.f, Y, Z),
                    FVector(258 - Pier * 12.f, 282, 63), FRotator(0, Side * .42f, 0), 6.f, Course % 4 * 6.f);
            }
        }
    }
    PointedArch(Wall, Trim, FVector(-6865, 1060, 0), FVector::ForwardVector, 396.f, 462.f, 211.f, 71.f, false);
    PointedArch(Ruin, Trim, FVector(-6900, -1085, 0), FVector::ForwardVector, 390.f, 399.f, 213.f, 78.f, true);
    for (int32 I = 0; I < 15; ++I)
    {
        const float X = -7440.f + I * 73.f;
        const float Y = -658.f - 202.f * FMath::Cos(I * .29f);
        const float Length = Random.FRandRange(54.f, 131.f);
        Ruin.Stone(FVector(X, Y, 14), FVector(Length, Length * .72f, Random.FRandRange(21.f, 47.f)),
            FRotator(Random.FRandRange(-8.f, 6.f), Random.FRandRange(-21.f, 36.f), Random.FRandRange(-7.f, 9.f)),
            5.f, Random.FRandRange(8.f, 20.f));
    }
    Trim.Stone(Warrant + FVector(0, 0, 14), FVector(137, 137, 29), FRotator(0, -8, 0), 6.f, 9.f);
    Wall.Stone(Warrant + FVector(0, 0, 54), FVector(98, 107, 61), FRotator(0, -8, 0), 7.f, 11.f);
    Trim.Stone(Warrant + FVector(0, 0, 91), FVector(136, 142, 21), FRotator(0, -8, 0), 4.f, 4.f);
    BlockPath(Warrant + FVector(0, 0, 51), FVector(61, 67, 52));
    WarrantStand = DuelGeometry::Joint(this, SceneRoot, Warrant + FVector(0, 0, 105));
    UMaterialInterface* WarrantPaper = DuelGeometry::ColorMaterial(this, FLinearColor(.61f, .43f, .23f));
    UMaterialInterface* WarrantWax = DuelGeometry::ColorMaterial(this, FLinearColor(.40f, .042f, .061f), false, .38f);
    DuelGeometry::Shape(this, WarrantStand, EShape::Box, FVector::ZeroVector, FVector(66, 48, 4), WarrantPaper, FRotator(0, -12, 4));
    DuelGeometry::Shape(this, WarrantStand, EShape::Tube, FVector(-12, 9, 5), FVector(12, 12, 3), WarrantWax);
    LightPlane(Warrant + FVector(-99, -18, 2), FVector2D(86), FRotator::ZeroRotator, EDuelLight::Seal,
        FLinearColor(1, .71f, .31f), 1.85f, .12f)->AttachToComponent(WarrantStand, FAttachmentTransformRules::KeepWorldTransform);
    for (int32 Row = -5; Row < 5; ++Row)
    {
        const float Y = Row * 183.f;
        for (float X = 2540.f; X < 5070.f;)
        {
            const float Width = FMath::Min(5070.f - X, X == 2540.f && Row % 2 ? 128.f : 256.f);
            const float Tone = .87f + .13f * FMath::PerlinNoise2D(FVector2D(X, Y) * .006);
            const FVector Quad[] = {{X + 1.4, Y + 1.2, -.18}, {X + Width - 1.4, Y + 1.2, -.18},
                {X + Width - 1.4, Y + 181.8, -.18}, {X + 1.4, Y + 181.8, -.18}};
            ArchiveFloor.Face(Quad, 4, FVector::UpVector, Tone);
            X += Width;
        }
    }

    // Traversable side galleries and lower terraces frame the route to the first hearing.
    for (float Side : {-1.f, 1.f})
    {
        for (float X = -5710.f; X < -3220.f; X += 124.f)
        {
            for (int32 Row = 0; Row < 5; ++Row)
            {
                const float Y = Side * (1010.f + Row * 123.f);
                const FVector Paving[] = {{X, Y - 61.f, .7f}, {X + 123.f, Y - 61.f, .7f},
                    {X + 123.f, Y + 61.f, .7f}, {X, Y + 61.f, .7f}};
                Floor.Face(Paving, 4, FVector::UpVector, .69f + .18f * FMath::PerlinNoise2D(FVector2D(X, Y) * .009));
                if (Row == 3 && FMath::PerlinNoise2D(FVector2D(X, Y) * .012) > .23f)
                {
                    const FVector Wet[] = {{X + 9.f, Y - 50.f, 1.2f}, {X + 107.f, Y - 30.f, 1.2f},
                        {X + 112.f, Y + 49.f, 1.2f}, {X + 31.f, Y + 43.f, 1.2f}};
                    Water.Face(Wet, 4, FVector::UpVector, .62f);
                }
            }
        }
        for (int32 Bay = 0; Bay < 6; ++Bay)
        {
            const FVector At(-5620.f + Bay * 428.f, Side * (DuelCourt::OuterGalleryHalfWidth + 133.f), 0);
            for (float Pier : {-192.f, 192.f})
            {
                CameraBoundary(At + FVector(Pier, 0, 156.f), FVector(63.f, 79.f, 155.f));
            }
            PointedArch(Ruin, Trim, At, FVector::ForwardVector, 192.f, 255.f + (Bay % 3) * 34.f,
                104.f, 56.f, Bay == 2 || (Bay == 4 && Side < 0));
            if (Bay % 3 == 0)
            {
                Trim.Stone(At + FVector(-170, -Side * 81, 39), FVector(97, 122, 78), FRotator(0, Side * 13.f, 0), 6.f, 21.f);
            }
        }
        for (const FVector& Origin : {FVector::ZeroVector, AppealOrigin, YukiOrigin})
        {
            const bool bFinal = Origin == YukiOrigin;
            const float RidgeY = Side * (bFinal ? 3740.f : 3280.f);
            for (int32 Bay = 0; Bay < 7; ++Bay)
            {
                const float X = -3880.f + Bay * (bFinal ? 1210.f : 1040.f);
                const float Rise = 390.f + (Bay % 3) * 120.f + (bFinal ? 190.f : 0.f);
                const float Grade = -248.f - 137.f * (Bay % 3);
                const FVector Foot = Origin + FVector(X, RidgeY + Side * (Bay % 3) * 167.f, Grade);
                const FVector GroundPatch[] = {
                    Foot + FVector(-570, -900, -9), Foot + FVector(570, -900, -9),
                    Foot + FVector(570, 900, -9), Foot + FVector(-570, 900, -9)};
                LowFloor.Face(GroundPatch, 4, FVector::UpVector, .48f);
                if (Bay == 1 || Bay == 3 || Bay == 5)
                {
                    PointedArch(Ruin, Trim, Foot, FVector::ForwardVector, 423.f, Rise,
                        164.f, 89.f, Bay == 3 && Side < 0);
                }
                else
                {
                    const float Height = Rise + (Bay == 4 ? 535.f : 244.f);
                    for (int32 Tier = 0; Tier < 4; ++Tier)
                    {
                        const FVector At = Foot + FVector(Tier * 19.f, -Side * 33.f * Tier, (Tier + .5f) * Height * .25f);
                        Ruin.Stone(At, FVector(370.f - 48.f * Tier, 264.f - Tier * 17.f, Height * .25f),
                            FRotator(0, Side * (7.f + Tier * 2.f), 0), 16.f, 42.f);
                    }
                }
            }
        }
    }

    for (float Radius : {510.f, 555.f})
    {
        for (int32 Index = 0; Index < 86; ++Index)
        {
            if (Random.FRand() < .11f)
            {
                continue;
            }
            const float Angle = Index * 360.f / 86.f;
            const FVector Direction = FRotator(0, Angle, 0).Vector();
            const float Width = Random.FRandRange(6.f, 11.f);
            Trim.Stone(Direction * Radius + FVector(0, 0, -.3), FVector(Width, Radius * .071f, 1.4f),
                FRotator(0, Angle, 0), .45f, Random.FRandRange(1, 3));
        }
    }

    // Arcades leave a broad, unobstructed lane for the duel.
    for (int32 Side : {-1, 1})
    {
        const float Y = Side * 1510.f;
        for (int32 Bay = 0; Bay < 5; ++Bay)
        {
            const float X = -1400.f + Bay * 710.f;
            PointedArch(Wall, Trim, FVector(X, Y, 0), FVector::ForwardVector,
                265.f, 425.f + (Bay % 3) * 22.f, 155.f, 61.f, (Bay + Side) % 4 == 0);
            for (int32 Block = 0; Block < 5; ++Block)
            {
                const float Height = 88.f + (Block % 3) * 32.f;
                Wall.Stone(FVector(X + Block * 138.f - 271.f, Y + Side * 65.f, Height * .5f),
                    FVector(135.f, 176.f, Height), FRotator(0, Random.FRandRange(-3, 3), 0), 6, 11);
            }
            const FVector Foot(X + 354.f, Y + Side * 55.f, 0);
            Trim.Stone(Foot + FVector(0, 0, 29), FVector(216, 208, 58), FRotator::ZeroRotator, 8);
            const int32 Courses = Bay % 3 + 3;
            for (int32 Course = 0; Course < Courses; ++Course)
            {
                Wall.Stone(Foot + FVector(0, 0, 99.f + Course * 84.f), FVector(151, 167, 82),
                    FRotator(0, Course % 2 ? 4.f : -3.f, 0), 7, 13);
            }
            Trim.Stone(Foot + FVector(0, 0, 60.f + Courses * 84.f), FVector(177, 197, 23),
                FRotator(0, 0, Side * 3.f), 4);
        }
        for (int32 Index = 0; Index < 19; ++Index)
        {
            const float Size = Random.FRandRange(31.f, 93.f);
            const float EdgeY = Side * Random.FRandRange(1375.f, 1570.f);
            Wall.Stone(FVector(Random.FRandRange(-1860.f, 1870.f), EdgeY, Size * .31f),
                FVector(Size, Size * .65f, Size * .52f),
                FRotator(Random.FRandRange(-23, 36), Random.FRandRange(0, 360), Random.FRandRange(-22, 21)), 5, 8);
        }
    }

    PointedArch(Wall, Trim, FVector(DuelCourt::GateX, 0, 0), FVector::RightVector, 249.f, 368.f, 192.f, 69.f, false);
    for (int32 Side : {-1, 1})
    {
        for (int32 Row = 0; Row < 5; ++Row)
        {
            Wall.Stone(FVector(DuelCourt::GateX + Row * 2, Side * (452.f + Row * 3), 36.f + Row * 69.f),
                FVector(195, 305 - Row * 14, 68), FRotator(0, Side * Row * .91f, 0), 7.f, 19.f);
        }
        for (int32 Step = 0; Step < 7; ++Step)
        {
            const float X = -3210.f + Step * 165.f;
            Trim.Stone(FVector(X, Side * 431.f, 19), FVector(155, 55, 40),
                FRotator(0, Step % 2 ? -2.f : 3.f, 0), 5.f, 13.f);
            if (Step % 2 == 0)
            {
                Wall.Stone(FVector(X, Side * 486.f, 35), FVector(132, 90, 78),
                    FRotator(-6, Step * 11.f, 5), 11.f, 26.f);
            }
        }
    }
    if (UMaterialInterface* Mist = DuelGeometry::ArtMaterial(TEXT("M_FogGate")))
    {
        FogGate = DuelGeometry::Shape(this, SceneRoot, EShape::Plane, FVector(DuelCourt::GateX - 6, 0, 266),
            FVector(466, 525, 100), Mist,
            FRotationMatrix::MakeFromXY(FVector::RightVector, -FVector::UpVector).Rotator());
        FogGate->SetCastShadow(false);
    }
    LightPlane(DuelCourt::Checkpoint().GetLocation() + FVector(0, 0, -60), FVector2D(226),
        FRotator::ZeroRotator, EDuelLight::Seal, FLinearColor(1.f, .72f, .32f), 3.2f, .10f)->SetCastShadow(false);
    const float EastArchX = DuelCourt::ArchiveGateX - 115.f;
    PointedArch(Wall, Trim, FVector(EastArchX, 0, 0), FVector::RightVector, 288.f, 455.f, 240.f, 82.f, false);
    for (int32 Side : {-1, 1})
    {
        for (int32 Row = 0; Row < 8; ++Row)
        {
            const float Left = 337.f + (Row % 2) * 22.f;
            const float End = 1250.f - FMath::Square(Row) * 12.f + Random.FRandRange(-31, 36);
            for (float Y = Left; Y + 38.f < End;)
            {
                const float Span = FMath::Min(End - Y, Random.FRandRange(82, 220));
                const float Height = Row > 4 ? Random.FRandRange(51, 66) : 65.f;
                Wall.Stone(FVector(EastArchX + Random.FRandRange(-13, 11), Side * (Y + Span * .5f), 31.f + Row * 62.f),
                    FVector(Random.FRandRange(201, 253), Span + 1.f, Height),
                    FRotator(Random.FRandRange(-1, 1), Random.FRandRange(-2, 2), Random.FRandRange(-2, 2)),
                    2.f, Random.FRandRange(4, 23));
                Y += Span;
            }
        }
        for (int32 Step = 0; Step < 9; ++Step)
        {
            const float Height = 19.f * (Step + 1);
            Trim.Stone(FVector(DuelCourt::ArchiveGateX + 5.f + 45.f * Step, Side * 685.f, Height * .5f - 2.f),
                FVector(50, 360, Height), FRotator(0, 0, Side * .18f), 3.f, 8.f);
        }
    }
    for (int32 Bay : {-3, -2, -1, 1, 3})
    {
        const float X = 3730.f + Bay * 160.f;
        PointedArch(Wall, Trim, FVector(X, Bay * 730.f, -180), FVector::RightVector,
            278.f, 490.f + Bay * 23.f, 250.f, 82.f, Bay == -3 || Bay == 3);
        if (Bay == -2 || Bay == -1)
        {
            Wall.Stone(FVector(X, Bay * 730.f, 640.f), FVector(235, 652, 94),
                FRotator(Bay * 1.4f, 4.8f, -3.5f), 4.f, 29.f);
        }
    }
    // Silhouettes behind the eastern gate frame the courtroom without occupying its fighting floor.
    for (float Side : {-1.f, 1.f})
    {
        const FVector Tower(2770.f + Side * 180.f, Side * 1610.f, -64);
        const int32 Courses = Side > 0.f ? 19 : 15;
        for (int32 Course = 0; Course < Courses; ++Course)
        {
            const float H = 91.f + (Course % 4) * 3.f;
            const float Z = Course * 89.f + H * .47f;
            const float Radius = 378.f - Course * 3.8f;
            for (int32 Panel = 0; Panel < 7; ++Panel)
            {
                if (Course > 8 && (Panel == 0 || Panel == 6) && (Course + Panel) % 7 < 2)
                {
                    continue;
                }
                const float Yaw = Panel * (360.f / 7.f);
                const FVector Dir = FRotator(0, Yaw, 0).Vector();
                Ruin.Stone(Tower + Dir * Radius + FVector(0, 0, Z),
                    FVector(159, Radius * .89f + 23.f, H), FRotator(0, Yaw + .42f * (Course % 3), 0),
                    9.f, 6.f + (Course * Panel) % 20);
            }
        }
        PointedArch(Ruin, Trim, Tower + FVector(-395, 0, 65), FVector::RightVector, 177.f, 607.f, 114.f, 58.f, false);
        for (int32 I = 0; I < (Side > 0.f ? 4 : 3); ++I)
        {
            const float Yaw = 52.f + I * 71.f;
            const FVector Dir = FRotator(0, Yaw, 0).Vector();
            Ruin.Stone(Tower + Dir * 309.f + FVector(0, 0, Courses * 89.f + 53.f + (I % 3) * 47.f),
                FVector(112, 148, 119 + (I % 3) * 94), FRotator(0, Yaw + 4.f, Side * 2.f), 9.f, 26.f);
        }
    }

    // The ruined prison arcade frames the gate at the end of the approach.
    for (int32 Side : {-1, 1})
    {
        for (int32 Bay = 0; Bay < 6; ++Bay)
        {
            const float X = -5450.f + Bay * 583.f;
            const float Y = Side * (Bay < 5 ? 2010.f : 704.f);
            const float Z = Bay < 5 ? -170.f : -1.f;
            if (Bay < 5)
            {
                // The raised path stays clear. The older arcade sits on a lower terrace beyond the parapet.
                Ruin.Stone(FVector(X, Y, Z - 105.f), FVector(586, 550, 210), FRotator::ZeroRotator, 5.f, 23.f);
            }
            PointedArch(Wall, Trim, FVector(X, Y, Z), FVector::ForwardVector, 270.f,
                353.f + 23.f * (Bay % 3), 154.f, 68.f, (Bay + Side + 1) % 3 != 0);
            for (int32 Course = 0; Course < 3; ++Course)
            {
                Wall.Stone(FVector(X + Course * 7.f, Y + Side * 233.f, Z + 25.f + Course * 49.f),
                    FVector(560, 170, 50), FRotator(0, Random.FRandRange(-1.9f, 1.9f), 0), 4.f, 23.f);
            }
        }
        for (float X = -5830.f; X < -2500.f; X += 190.f)
        {
            const float Y = Side * 598.f;
            const FVector Quad[] = {{X, Y - 114, .8}, {X + 190, Y - 114, .8},
                {X + 190, Y + 114, .8}, {X, Y + 114, .8}};
            Water.Face(Quad, 4, FVector::UpVector);
            Trim.Stone(FVector(X + 95.f, Y + Side * 122.f, 1.9f), FVector(182, 18, 5),
                FRotator(0, .25f * Side, 0), 1.5f, 6.f);
        }
    }
    PointedArch(Wall, Trim, FVector(-2530, 0, 0), FVector::RightVector, 441.f, 482.f, 217.f, 83.f, false);
    for (float Side : {-1.f, 1.f})
    {
        BlockPath(FVector(-2530, Side * 441.f, 221.f), FVector(104, 62, 222));
        for (int32 Bay = 0; Bay < 4; ++Bay)
        {
            const float X = 2960.f + Bay * 497.f;
            PointedArch(Wall, Trim, FVector(X, Side * 838.f, 0), FVector::ForwardVector, 244.f,
                424.f, 148.f, 59.f, false);
            for (int32 Row = 0; Row < 8; ++Row)
            {
                if (Side < 0.f && (Bay == 2 || Bay == 3) && Row < 6)
                {
                    continue;
                }
                Wall.Stone(FVector(X + (Row % 2) * 20, Side * 947.f, 39.f + 77.f * Row),
                    FVector(506, 141, 78), FRotator::ZeroRotator, 3.f, (Row * 7) % 22);
            }
            if (Bay != 1 && !(Side < 0.f && Bay == 3))
            {
                Trim.Stone(FVector(X, Side * 634.f, 13), FVector(243, 147, 27), FRotator::ZeroRotator, 4.f, 9.f);
                Wall.Stone(FVector(X, Side * 650.f, 62), FVector(207, 103, 77), FRotator::ZeroRotator, 4.f);
                Trim.Stone(FVector(X, Side * 650.f, 110), FVector(227, 121, 24), FRotator::ZeroRotator, 4.f);
                BlockPath(FVector(X, Side * 641.f, 58), FVector(117, 73, 59));
            }
        }
    }
    for (float X : {2870.f, 3580.f, 4140.f, 4900.f})
    {
        PointedArch(Wall, Trim, FVector(X, 0, 0), FVector::RightVector, 805.f, 473.f, 164.f, 81.f, false);
    }
    for (float X : {3140.f, 3820.f, 4570.f})
    {
        for (int32 Side : {-1, 1})
        {
            Wall.Stone(FVector(X, Side * 565.f, 916.f), FVector(650, 620, 66),
                FRotator(Side * 2.f, 0, Side * 11.f), 6.f, 21.f);
        }
    }
    for (int32 Row = 0; Row < 10; ++Row)
    {
        for (int32 Block = -3; Block <= 3; ++Block)
        {
            Wall.Stone(FVector(4980, Block * 246.f + (Row % 2) * 26.f, 37.f + Row * 74.f),
                FVector(188, 247, 75), FRotator::ZeroRotator, 4.f, (Row * 3) % 21);
        }
    }
    const FVector Desk(DuelCourt::ArchiveDeskX, 0, 0);
    Trim.Stone(Desk + FVector(0, 0, 10), FVector(235, 438, 22), FRotator::ZeroRotator, 4.f);
    Wall.Stone(Desk + FVector(0, 0, 64), FVector(175, 326, 105), FRotator::ZeroRotator, 8.f, 15.f);
    Trim.Stone(Desk + FVector(0, 0, 123), FVector(208, 376, 20), FRotator::ZeroRotator, 4.f, 11.f);
    BlockPath(Desk + FVector(0, 0, 66), FVector(105, 188, 67));
    PointedArch(Ruin, Trim, FVector(AppealPassage.X, -704, 0), FVector::ForwardVector, 185.f, 268.f, 136.f, 59.f, false);
    LightPlane(AppealPassage + FVector(0, 0, 2), FVector2D(158), FRotator::ZeroRotator,
        EDuelLight::Seal, FLinearColor(.98f, .66f, .31f), 2.8f, .12f)->SetCastShadow(false);
    LightPlane(Desk + FVector(-8, 0, 137), FVector2D(135, 267), FRotator::ZeroRotator,
        EDuelLight::Seal, FLinearColor(.95f, .69f, .35f), 2.7f, .13f)->SetCastShadow(false);

    const auto PlaceScan = [&](UStaticMesh* Scan, const FVector& Foot, float Height, float Yaw)
    {
        const FBox Bounds = Scan->GetBoundingBox();
        const FVector Center = Bounds.GetCenter();
        const FVector Bottom(Center.X, Center.Y, Bounds.Min.Z);
        const double Scale = Height / FMath::Max(Bounds.GetSize().Z, 1.0);
        const FQuat Rotation(FRotator(0, Yaw, 0));
        UStaticMeshComponent* Piece = NewObject<UStaticMeshComponent>(this);
        Piece->SetupAttachment(SceneRoot);
        Piece->SetStaticMesh(Scan);
        Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Piece->SetCanEverAffectNavigation(false);
        Piece->SetRelativeTransform(FTransform(Rotation, Foot - Rotation.RotateVector(Bottom * Scale), FVector(Scale)));
        AddInstanceComponent(Piece);
        Piece->RegisterComponent();
        return Piece;
    };
    if (UStaticMesh* Gate = CourtScan(TEXT("large_iron_gate")))
    {
        const FVector Size = Gate->GetBoundingBox().GetSize();
        ArchiveGate = PlaceScan(Gate, FVector(DuelCourt::ArchiveGateX, 0, -1), 490.f, Size.X > Size.Y ? 90.f : 0.f);
        ArchiveGateBase = ArchiveGate->GetRelativeLocation();
        AppealGrille = PlaceScan(Gate, Appeal + FVector(ArchiveGateX, 0, -1), 490.f, Size.X > Size.Y ? 90.f : 0.f);
        AppealGrilleBase = AppealGrille->GetRelativeLocation();
        PrisonGate = PlaceScan(Gate, FVector(PrisonGateX - 7.f, 0, -1), 421.f, Size.X > Size.Y ? 90.f : 0.f);
        PrisonGateBase = PrisonGate->GetRelativeLocation();
    }
    if (UStaticMesh* Effigy = CourtScan(TEXT("gothic_statue")))
    {
        for (float Side : {-1.f, 1.f})
        {
            const FVector Plinth(DuelCourt::ArchiveGateX - 12.f, Side * 1085.f, 0);
            Wall.Stone(Plinth + FVector(0, 0, 45), FVector(375, 372, 92), FRotator(0, Side * 5.f, 0), 8, 23);
            Trim.Stone(Plinth + FVector(0, 0, 107), FVector(328, 329, 36), FRotator(0, Side * 5.f, 0), 6, 18);
            PlaceScan(Effigy, Plinth + FVector(-48, 0, 122), Side > 0 ? 495.f : 540.f, 180.f - Side * 12.f);
        }
        PlaceScan(Effigy, FVector(-8620, -634, LowCourtZ + 112), 477, 179);
        PlaceScan(Effigy, FVector(-8714, 728, LowCourtZ + 38), 284, 149);
        PlaceScan(Effigy, FVector(-6865, 1088, -28), 480, -94);
        PlaceScan(Effigy, Appeal + FVector(2110, -890, 0), 574, 168);
        PlaceScan(Effigy, Appeal + FVector(-330, 1350, 0), 416, -116);
        PlaceScan(Effigy, Appeal + FVector(3560, -430, 0), 482, 180);
    }
    if (UStaticMesh* Rootwood = CourtScan(TEXT("tree_stump_02")))
    {
        for (int32 I = 0; I < 8; ++I)
        {
            const float Side = I % 2 ? -1.f : 1.f;
            PlaceScan(Rootwood, FVector(-1600.f + I * 452.f, Side * (1635.f + 34.f * (I % 3)), -66),
                98.f + 24.f * (I % 3), 61.f * I);
        }
    }
    if (UStaticMesh* Cliff = CourtScan(TEXT("coastal_cliff_02")))
    {
        const FVector Size = Cliff->GetBoundingBox().GetSize();
        const float CrossCourt = Size.X > Size.Y ? 90.f : 0.f;
        UStaticMeshComponent* ArchiveRidge = PlaceScan(Cliff, FVector(5910, 0, -560), 1250, CrossCourt + 7.f);
        ArchiveRidge->AddRelativeLocation(FVector(0, ArchiveRidge->Bounds.BoxExtent.Y + 1550.f, 0));
        UStaticMeshComponent* SouthRidge = PlaceScan(Cliff, FVector(8710, 0, -1130), 2100, CrossCourt - 18.f);
        SouthRidge->AddRelativeLocation(FVector(0, -SouthRidge->Bounds.BoxExtent.Y - 1840.f, 0));
        PlaceScan(Cliff, FVector(1610, -3990, -510), 950, CrossCourt + 66.f);
        PlaceScan(Cliff, FVector(-1330, 3960, -655), 1100, CrossCourt - 68.f);
        UStaticMeshComponent* HallRidge = PlaceScan(Cliff, Appeal + FVector(1790, 0, -1120), 2360, CrossCourt - 39.f);
        HallRidge->AddRelativeLocation(FVector(0, HallRidge->Bounds.BoxExtent.Y + 1980.f, 0));
        UStaticMeshComponent* HallScarp = PlaceScan(Cliff, Appeal + FVector(-350, 0, -990), 1800, CrossCourt + 77.f);
        HallScarp->AddRelativeLocation(FVector(0, -HallScarp->Bounds.BoxExtent.Y - 2010.f, 0));
        // The scan has an open back. Face the rock normals toward the terrace and bury its ends in neighboring ridges.
        PlaceScan(Cliff, YukiOrigin + FVector(-220, -3230, -1015), 1440, 12.f);
        PlaceScan(Cliff, YukiOrigin + FVector(610, 3660, -1250), 1800, 173.f);
        PlaceScan(Cliff, YukiOrigin + FVector(5950, -2500, -870), 1370, 77.f);
        PlaceScan(Cliff, YukiOrigin + FVector(6090, 2910, -1000), 1510, 115.f);
        PlaceScan(Cliff, YukiOrigin + FVector(9560, 580, -1350), 2500, 94.f);
        PlaceScan(Cliff, YukiOrigin + FVector(13040, 4240, -1650), 3330, 109.f);
        PlaceScan(Cliff, YukiOrigin + FVector(16520, -3320, -1740), 3780, 74.f);
        for (float Side : {-1.f, 1.f})
        {
            UStaticMeshComponent* Ridge = PlaceScan(Cliff, FVector(-4520 - Side * 870.f, 0, -760),
                1320.f + Side * 125.f, CrossCourt + Side * 27.f);
            Ridge->AddRelativeLocation(FVector(0, Side * (Ridge->Bounds.BoxExtent.Y + 1620.f), 0));
            UStaticMeshComponent* GatehouseRidge = PlaceScan(Cliff, FVector(-9240, 0, LowCourtZ - 980.f),
                Side > 0.f ? 2260.f : 1530.f, CrossCourt + Side * 51.f);
            GatehouseRidge->AddRelativeLocation(FVector(0, Side * (GatehouseRidge->Bounds.BoxExtent.Y + 1260.f), 0));
        }
    }
    if (UStaticMesh* Foundation = CourtScan(TEXT("coastal_cliff_01")))
    {
        const FVector Size = Foundation->GetBoundingBox().GetSize();
        const float Face = Size.X > Size.Y ? 0.f : 90.f;
        for (float Side : {-1.f, 1.f})
        {
            const float Yaw = Face + (Side > 0.f ? 180.f : 0.f);
            PlaceScan(Foundation, Appeal + FVector(-790, Side * 3430.f, -1510), 1690, Yaw + Side * 12.f);
            PlaceScan(Foundation, YukiOrigin + FVector(-3550, Side * 2510.f, -1730), 1450, Yaw + Side * 9.f);
            PlaceScan(Foundation, YukiOrigin + FVector(640, Side * 3350.f, -1760), 1900, Yaw - Side * 18.f);
        }
    }
    if (UStaticMesh* Boulder = CourtScan(TEXT("boulder_01")))
    {
        for (int32 Side : {-1, 1})
        {
            PlaceScan(Boulder, FVector(2355, Side * 1345.f, -35), 210, Side * 89.f);
            PlaceScan(Boulder, FVector(2340, Side * 380.f, -49), 185, Side * 125.f);
            PlaceScan(Boulder, FVector(1290, Side * 1570.f, -44), 159, Side * 65.f);
            for (int32 I = 0; I < 11; ++I)
            {
                PlaceScan(Boulder, FVector(Random.FRandRange(-1740, 1830),
                    Side * Random.FRandRange(1410, 1640), -8), Random.FRandRange(25, 106),
                    Random.FRandRange(0, 360));
            }
            for (int32 I = 0; I < 9; ++I)
            {
                PlaceScan(Boulder, FVector(-10270.f + I * 195.f, Side * Random.FRandRange(875, 1280), LowCourtZ - 17),
                    Random.FRandRange(29, 173), Random.FRandRange(0, 360));
            }
        }
    }

    const auto AddPuddle = [&Water](const FVector& Center, float X, float Y, int32 Index)
    {
        FVector Edge[24];
        for (int32 Side = 0; Side < UE_ARRAY_COUNT(Edge); ++Side)
        {
            const float Angle = Side * (2.f * PI / UE_ARRAY_COUNT(Edge));
            const float Radius = 1.f + FMath::Cos(Angle * 3.f + Index) * .18f + FMath::Sin(Angle * 5.f) * .08f;
            Edge[Side] = Center + FVector(FMath::Cos(Angle) * X, FMath::Sin(Angle) * Y, 0) * Radius;
        }
        for (int32 Side = 0; Side < UE_ARRAY_COUNT(Edge); ++Side)
        {
            const FVector Triangle[] = {Center, Edge[Side], Edge[(Side + 1) % UE_ARRAY_COUNT(Edge)]};
            Water.Face(Triangle, 3, FVector::UpVector);
            Water.Colors.Last(1).A = 0.f;
            Water.Colors.Last(0).A = 0.f;
        }
    };
    for (int32 Index = 0; Index < 17; ++Index)
    {
        const FVector Center(Random.FRandRange(-1740, 1740), Random.FRandRange(-983, 983), .24);
        const float X = Random.FRandRange(37.f, 156.f);
        const float Y = Random.FRandRange(25.f, 102.f);
        AddPuddle(Center, X, Y, Index);
    }

    // The archived docket hangs on the rear wall of the evidence room.
    UMaterialInterface* Docket = DuelGeometry::ArtMaterial(TEXT("M_EvidenceDocket"));
    const float Starts[] = {0.f, .344f, .687f};
    const float Ends[] = {.325f, .665f, 1.f};
    for (int32 I = 0; I < 3; ++I)
    {
        const float Left = (Starts[I] - .5f) * 607.f;
        const float Right = (Ends[I] - .5f) * 607.f;
        const float X = 4850.f + FMath::Abs(I - 1) * 12.f;
        const float Bottom = 230.f + (I % 2) * 23.f;
        const float Top = 694.f + (I % 2) * 23.f;
        Wall.Stone(FVector(X + 12, (Left + Right) * .5f, (Top + Bottom) * .5f),
            FVector(24, Right - Left + 4, Top - Bottom + 9), FRotator::ZeroRotator, 6.f, 12.f + 5.f * I);
        UProceduralMeshComponent* Face = LightMesh(Docket);
        const TArray<FVector> Vertices{{X - 1, Left, Bottom + 22}, {X - 1, Left + 10, Top - 14},
            {X - 1, Right - 8, Top}, {X - 1, Right, Bottom}};
        const TArray<FVector2D> UVs{{Starts[I], 1}, {Starts[I], 0}, {Ends[I], 0}, {Ends[I], 1}};
        Face->CreateMeshSection_LinearColor(0, Vertices, {0, 2, 1, 0, 3, 2},
            TArray<FVector>( { -FVector::ForwardVector, -FVector::ForwardVector, -FVector::ForwardVector, -FVector::ForwardVector }),
            UVs, {}, {}, false, false);
        Trim.Stone(FVector(X + 9, (Left + Right) * .5f, Bottom - 15),
            FVector(49, Right - Left + 24, 19), FRotator(0, I - 1.f, 0), 3.f, 8.f);
    }
    const double VerdictPanelX = Appeal.X + 3639;
    const double VerdictPanelZ = Appeal.Z + 266;
    UProceduralMeshComponent* DocketAtVerdict = LightMesh(Docket);
    DocketAtVerdict->CreateMeshSection_LinearColor(0,
        {{VerdictPanelX, -258, VerdictPanelZ}, {VerdictPanelX, -258, VerdictPanelZ + 350},
            {VerdictPanelX, 258, VerdictPanelZ + 350}, {VerdictPanelX, 258, VerdictPanelZ}},
        {0, 2, 1, 0, 3, 2}, {-FVector::ForwardVector, -FVector::ForwardVector, -FVector::ForwardVector, -FVector::ForwardVector},
        {{0, 1}, {0, 0}, {1, 0}, {1, 1}}, {}, {}, false, false);
    PointedArch(Wall, Trim, FVector(2490, 0, -10), FVector::RightVector, 306.f, 558.f, 225.f, 91.f, false);
    Trim.Stone(FVector(2307, 0, 884), FVector(318, 702, 35), FRotator(0, 1.7f, 0), 7.f, 19.f);

    const FVector Final = YukiOrigin;
    const auto StoneWalk = [&](float Min, float Max, float Width, FStoneBatch& Batch)
    {
        BlockPath(Final + FVector((Min + Max) * .5f, 0, -35), FVector((Max - Min) * .5f + 3, Width, 35),
            FRotator::ZeroRotator, true);
        for (float X = Min; X < Max; X += 203.f)
        {
            for (float Y = -Width; Y < Width; Y += 177.f)
            {
                const float EndX = FMath::Min(X + 203.f, Max);
                const float EndY = FMath::Min(Y + 177.f, Width);
                const FVector Quad[] = {Final + FVector(X, Y, -.25f), Final + FVector(EndX, Y, -.25f),
                    Final + FVector(EndX, EndY, -.25f), Final + FVector(X, EndY, -.25f)};
                Batch.Face(Quad, 4, FVector::UpVector, .96f);
            }
        }
    };
    const FVector StairFoot = Final + FVector(YukiStairStartX, 0, -YukiStairRise);
    const FVector StairHead = Final + FVector(YukiStairEndX, 0, 0);
    const FVector StairRun = StairHead - StairFoot;
    const FRotator StairSlope = StairRun.Rotation();
    const float LandingStart = YukiArchiveExitX - (Final.X - Appeal.X);
    BlockPath(Final + FVector((LandingStart + YukiStairStartX) * .5f, 0, -YukiStairRise - 35.f),
        FVector((YukiStairStartX - LandingStart) * .5f + 8.f, 410, 35), FRotator::ZeroRotator, true);
    Steps.Stone(Final + FVector((LandingStart + YukiStairStartX) * .5f, 0, -YukiStairRise - 21.f),
        FVector(YukiStairStartX - LandingStart + 10.f, 820, 42), FRotator::ZeroRotator, 4.f);
    BlockPath((StairFoot + StairHead) * .5 - StairSlope.RotateVector(FVector(0, 0, 28)),
        FVector(StairRun.Size() * .5 + 7, 410, 28), StairSlope, true);
    constexpr int32 Treads = 28;
    const float TreadWidth = (YukiStairEndX - YukiStairStartX) / Treads;
    for (int32 Step = 0; Step < Treads; ++Step)
    {
        const FVector At = FMath::Lerp(StairFoot, StairHead, (Step + .5) / Treads);
        for (int32 Bay = -1; Bay <= 1; ++Bay)
        {
            Steps.Stone(At + FVector(0, Bay * 274.f, -17), FVector(TreadWidth + 1.f, 274, 48),
                FRotator::ZeroRotator, 3.f, (Step * 9 + Bay + 1) % 19);
        }
        for (float Side : {-1.f, 1.f})
        {
            Ruin.Stone(At + FVector(0, Side * 440.f, 35), FVector(TreadWidth + 3.f, 122, 238),
                FRotator::ZeroRotator, 4.f, Step % 17);
            if (Step % 9 == 3)
            {
                const FVector Pier = At + FVector(0, Side * 555.f, 0);
                PointedArch(Ruin, Trim, Pier, FVector::ForwardVector, 245.f, 503.f, 145.f, 64.f, false);
                if (!bCastleInterior)
                {
                    LightPlane(At + FVector(0, Side * 388.f, 120), FVector2D(78, 215), FRotator(90, 0, 0),
                        EDuelLight::Shaft, FLinearColor(1.f, .71f, .38f), 1.2f, .08f);
                }
            }
        }
    }
    PointedArch(Ruin, Trim, StairHead, FVector::RightVector, 402.f, 645.f, 228.f, 91.f, false);
    StoneWalk(-3065.f, -2080.f, 410.f, Steps);
    StoneWalk(-2170.f, 2140.f, 1290.f, HallFloor);
    StoneWalk(2090.f, 4430.f, 914.f, LowFloor);
    FRandomStream CourtRain(7238);
    for (int32 I = 0; I < 14; ++I)
    {
        const float Side = I % 2 ? -1.f : 1.f;
        const FVector Center = Final + FVector(-1775.f + I * 285.f, Side * CourtRain.FRandRange(899.f, 1090.f), .24f);
        AddPuddle(Center, CourtRain.FRandRange(46.f, 147.f), CourtRain.FRandRange(34.f, 82.f), I + 17);
    }
    PointedArch(Ruin, Trim, Final + FVector(-2230, 0, 0), FVector::RightVector, 279.f, 462.f, 187.f, 92.f, false);
    PointedArch(Wall, Trim, Final + FVector(YukiExitX, 0, 0), FVector::RightVector, 352.f, 514.f, 215.f, 94.f, false);
    PointedArch(Ruin, Trim, Final + FVector(YukiExitX + 46.f, 0, 0), FVector::RightVector, 430.f, 568.f, 144.f, 73.f, false);
    if (UMaterialInterface* Mist = DuelGeometry::ArtMaterial(TEXT("M_FogGate")))
    {
        YukiFog = DuelGeometry::Shape(this, SceneRoot, EShape::Plane, Final + FVector(GateX - 4.f, 0, 275),
            FVector(542, 550, 100), Mist, FRotationMatrix::MakeFromXY(FVector::RightVector, -FVector::UpVector).Rotator());
        YukiFog->SetCastShadow(false);
        YukiFog->SetVisibility(false);
    }
    LightPlane(YukiCheckpoint().GetLocation() + FVector(0, 0, -60), FVector2D(190), FRotator::ZeroRotator,
        EDuelLight::Seal, FLinearColor(.76f, .81f, 1.f), 2.6f, .1f);
    for (float X : {-2915.f, -2460.f})
    {
        for (float Side : {-1.f, 1.f})
        {
            const FVector Post = Final + FVector(X, Side * 332.f, 0);
            Ruin.Stone(Post + FVector(0, 0, 52), FVector(92, 100, 112), FRotator::ZeroRotator, 6.f, 14.f);
            Trim.Stone(Post + FVector(0, 0, 108), FVector(112, 122, 18), FRotator::ZeroRotator, 4.f);
            DuelGeometry::Shape(this, SceneRoot, EShape::Tube, Post + FVector(0, 0, 121), FVector(43, 43, 13), Brass);
            DuelGeometry::Shape(this, SceneRoot, EShape::Ball, Post + FVector(0, 0, 138), FVector(10, 11, 25), Flame)
                ->SetCastShadow(false);
        }
    }

    const FVector Rose = Final + FVector(YukiExitX + 53.f, 0, 1125.f);
    for (int32 Course = 0; Course < 10; ++Course)
    {
        const float Z = 824.f + Course * 73.f;
        const float Height = FMath::Max(0.f, static_cast<float>(FMath::Abs(Z - 1125.f)) - 37.f);
        const float Inner = FMath::Sqrt(FMath::Max(0.f, FMath::Square(361.f) - FMath::Square(Height)));
        const float Outer = 552.f - FMath::Max(0.f, Z - 1050.f) * .24f;
        for (float Side : {-1.f, 1.f})
        {
            for (float Y = Inner; Y < Outer - 5.f; Y += 126.f)
            {
                const float Edge = FMath::Min(Y + 128.f, Outer);
                Ruin.Stone(Final + FVector(YukiExitX + 91.f + Course % 2 * 7.f, Side * (Y + Edge) * .5f, Z),
                    FVector(155, Edge - Y, 74), FRotator::ZeroRotator, 4.f, (Course * 13) % 21);
            }
        }
    }
    constexpr int32 RoseStones = 32;
    for (int32 I = 0; I < RoseStones; ++I)
    {
        const float Angle = (I + .5f) * 2.f * PI / RoseStones;
        const FVector Radius(0, FMath::Cos(Angle), FMath::Sin(Angle));
        const FRotator Turn = FRotationMatrix::MakeFromXY(FVector::ForwardVector, FVector(0, -Radius.Z, Radius.Y)).Rotator();
        Ruin.Stone(Rose + Radius * 359.f, FVector(198, 76, 80), Turn, 5.f, (I * 3) % 12);
        Trim.Stone(Rose - FVector(104, 0, 0) + Radius * 325.f, FVector(32, 67, 24), Turn, 3.f);
    }
    if (UMaterialInterface* Glass = DuelGeometry::ArtMaterial(TEXT("M_WitnessGlass")))
    {
        DuelGeometry::Shape(this, SceneRoot, EShape::Plane, Rose - FVector(88, 0, 0), FVector(640, 640, 100),
            Glass, FRotationMatrix::MakeFromXY(-FVector::RightVector, -FVector::UpVector).Rotator())->SetCastShadow(false);
    }
    if (UMaterialInterface* WaterMaterial = DuelGeometry::ArtMaterial(TEXT("M_LowCourtWater")))
    {
        const FVector TidalShelf = Final + FVector(12200, 0, -578);
        DuelGeometry::Shape(this, SceneRoot, EShape::Plane, TidalShelf, FVector(23300, 36500, 100), WaterMaterial)
            ->SetCastShadow(false);
    }
    const float ArcadeX[] = {-1570.f, -725.f, 151.f, 1050.f, 1792.f};
    const float ArcadeSpring[] = {593.f, 455.f, 685.f, 489.f, 524.f};
    const float ArcadeSpan[] = {336.f, 285.f, 368.f, 310.f, 269.f};
    for (float Side : {-1.f, 1.f})
    {
        for (int32 Bay = 0; Bay < UE_ARRAY_COUNT(ArcadeX); ++Bay)
        {
            const float X = ArcadeX[Bay] + (Side > 0.f ? 33.f : -28.f) * (Bay % 3 - 1);
            const float Y = Side * (1192.f + (Bay % 2) * 77.f);
            const float Span = ArcadeSpan[Bay];
            const float Spring = ArcadeSpring[Bay] + Side * (Bay % 2 ? 40.f : -48.f);
            const bool bRuined = (Side > 0.f ? Bay == 1 || Bay == 4 : Bay == 2);
            PointedArch(Ruin, Trim, Final + FVector(X, Y, 0), FVector::ForwardVector, Span, Spring,
                139.f + (Bay % 3) * 18.f, 71.f + 9.f * (Bay % 2), bRuined);
            Trim.Stone(Final + FVector(X, Y + Side * 22.f, 19), FVector(Span * 2.f + 96.f, 177, 40),
                FRotator::ZeroRotator, 6.f, (Bay * 3) % 15);
            if (Bay == (Side > 0.f ? 2 : 3))
            {
                PointedArch(Wall, Trim, Final + FVector(X + Side * 31.f, Y + Side * 345.f, -148),
                    FVector::ForwardVector, Span + 99.f, Spring + 226.f, 206.f, 98.f, Side < 0.f);
            }
        }
        for (int32 Bay = 0; Bay < 4; ++Bay)
        {
            const FVector Pier = Final + FVector(2026.f, Side * (563.f + Bay * 193.f), 0);
            const int32 Courses = (Side > 0.f ? 8 : 6) + (Bay % 3) * 2;
            for (int32 Course = 0; Course < Courses; ++Course)
            {
                Ruin.Stone(Pier + FVector((Course % 2) * 15.f, 0, 35.f + Course * 70.f),
                    FVector(163, 198, 72), FRotator(0, Side * Bay * 1.3f, 0), 5.f, (Course * 5 + Bay * 13) % 22);
            }
        }
    }
    for (float Side : {-1.f, 1.f})
    {
        for (int32 Span = 0; Span < 20; ++Span)
        {
            const FVector Joint = Final + FVector(-1970.f + Span * 203.f, Side * (1115.f + 17.f * (Span % 2)), .50f);
            const FVector Quad[] = {Joint + FVector(-97, -12, 0), Joint + FVector(102, -13, 0),
                Joint + FVector(103, 16, 0), Joint + FVector(-100, 14, 0)};
            Trim.Face(Quad, 4, FVector::UpVector, .71f);
        }
    }
    if (UStaticMesh* Boulder = CourtScan(TEXT("boulder_01")))
    {
        FRandomStream Edges(9137);
        for (float Side : {-1.f, 1.f})
        {
            for (int32 I = 0; I < 12; ++I)
            {
                const float X = -1820.f + I * 334.f + Edges.FRandRange(-75.f, 75.f);
                const float Y = Side * Edges.FRandRange(1204.f, 1512.f);
                PlaceScan(Boulder, Final + FVector(X, Y, Y * Side > 1290.f ? -52.f : -9.f),
                    Edges.FRandRange(38.f, 138.f), Edges.FRandRange(0.f, 360.f));
            }
            PlaceScan(Boulder, Final + FVector(4320.f, Side * 1475.f, -518), 610.f + Side * 60.f, Side * 49.f);
            PlaceScan(Boulder, Final + FVector(4860.f, Side * 800.f, -552), 460.f - Side * 90.f, Side * 129.f);
        }
    }

    UMaterialInterface* Moss = DuelGeometry::ColorMaterial(this, FLinearColor(.13f, .17f, .057f), false, .87f, 0, true);
    UMaterialInterface* Leaf = DuelGeometry::ColorMaterial(this, FLinearColor(.20f, .31f, .078f), false, .76f, 0, true);
    UMaterialInterface* Blossom = DuelGeometry::ColorMaterial(this, FLinearColor(.91f, .80f, .57f), false, .66f);
    FStoneBatch GardenSoil(111.f);
    FRandomStream Garden(61483);
    for (float Side : {-1.f, 1.f})
    {
        for (int32 Bed = 0; Bed < 4; ++Bed)
        {
            const FVector Base = Final + FVector(2650.f + Bed * 456.f + Side * Bed * 14.f,
                Side * (634.f + Bed % 2 * 37.f), 3);
            const float SizeX = Bed == 1 ? 204.f : 169.f;
            const float SizeY = Bed == 2 ? 153.f : 188.f;
            FVector Border[10];
            for (int32 I = 0; I < UE_ARRAY_COUNT(Border); ++I)
            {
                const float Angle = I * 2.f * PI / UE_ARRAY_COUNT(Border);
                const float Worn = Garden.FRandRange(.83f, 1.13f);
                Border[I] = Base + FVector(FMath::Cos(Angle) * SizeX * Worn, FMath::Sin(Angle) * SizeY * Worn, 7.f);
            }
            for (int32 I = 0; I < UE_ARRAY_COUNT(Border); ++I)
            {
                const FVector A = Border[I];
                const FVector B = Border[(I + 1) % UE_ARRAY_COUNT(Border)];
                const FVector Patch[] = {Base + FVector(0, 0, 11.f), A, B};
                GardenSoil.Face(Patch, 3, FVector::UpVector, Garden.FRandRange(.72f, 1.f));
                if (I % 3 != Bed % 3)
                {
                    Trim.Stone((A + B) * .5 + FVector(0, 0, 1), FVector((B - A).Size2D() + 7.f, 22.f, 18.f),
                        (B - A).Rotation(), 4.f, (I * 3 + Bed) % 9);
                }
            }
            for (int32 Flower = 0; Flower < 16; ++Flower)
            {
                const float Angle = Garden.FRandRange(0, 2.f * PI);
                const float Radius = Garden.FRandRange(.20f, .79f);
                const FVector Root = Base + FVector(FMath::Cos(Angle) * Radius * SizeX,
                    FMath::Sin(Angle) * Radius * SizeY, 11.f);
                const float Stem = Garden.FRandRange(16.f, Flower % 3 ? 43.f : 58.f);
                const FVector Tip = Root + FVector(5, Garden.FRandRange(-9.f, 9.f), Stem);
                DuelGeometry::Rod(this, SceneRoot, Root, Tip, 1.2f, Leaf);
                DuelGeometry::Shape(this, SceneRoot, EShape::Ball, Root + FVector(-5, 3, Stem * .46f),
                    FVector(17, 5, 3), Leaf, FRotator(24, 36 + Flower * 43, 0));
                DuelGeometry::Shape(this, SceneRoot, EShape::Ball, Tip,
                    FVector(11, 12, 5), Flower % 4 ? Blossom : Wax, FRotator(20, Flower * 27, 0));
            }
        }
        for (int32 Rail = 0; Rail < 7; ++Rail)
        {
            const FVector At = Final + FVector(2390.f + Rail * 314.f, Side * 891.f, 0);
            Wall.Stone(At + FVector(0, 0, 39), FVector(79, 111, 82), FRotator::ZeroRotator, 5.f);
            Trim.Stone(At + FVector(0, 0, 82), FVector(100, 131, 17), FRotator::ZeroRotator, 4.f);
            if (Rail < 6)
            {
                Trim.Stone(At + FVector(157, 0, 52), FVector(265, 65, 26), FRotator::ZeroRotator, 3.f);
            }
        }
    }
    GardenSoil.Install(this, SceneRoot, Moss);
    if (UStaticMesh* Effigy = CourtScan(TEXT("gothic_statue")))
    {
        const FVector Left = Final + FVector(1665, -961, 0);
        const FVector Right = Final + FVector(1815, 968, 0);
        Trim.Stone(Left + FVector(0, 0, 66), FVector(272, 286, 135), FRotator(0, 8, 0), 8.f, 14.f);
        Trim.Stone(Right + FVector(0, 0, 45), FVector(255, 279, 93), FRotator(0, -4, 0), 8.f, 13.f);
        PlaceScan(Effigy, Left + FVector(0, 0, 135), 526, 168);
        PlaceScan(Effigy, Right + FVector(0, 0, 92), 487, -160);
    }
    Trim.Stone(Final + FVector(3610, 555, 37), FVector(288, 94, 29), FRotator(0, 11, 0), 5.f);
    Trim.Stone(Final + FVector(3514, 537, 19), FVector(57, 90, 39), FRotator(0, 11, 0), 4.f);
    Trim.Stone(Final + FVector(3703, 573, 19), FVector(57, 90, 39), FRotator(0, 11, 0), 4.f);

    const auto Install = [&](const FStoneBatch& Batch, const TCHAR* Material, const FLinearColor& Fallback)
    {
        UMaterialInterface* Surface = DuelGeometry::ArtMaterial(Material);
        Batch.Install(this, SceneRoot, Surface ? Surface : DuelGeometry::ColorMaterial(this, Fallback));
    };
    Install(Floor, TEXT("M_CourtStone"), FLinearColor(.165f, .184f, .203f));
    Install(LowFloor, TEXT("M_LowCourt"), FLinearColor(.17f, .19f, .16f));
    Install(Steps, TEXT("M_Steps"), FLinearColor(.182f, .19f, .207f));
    Install(ArchiveFloor, TEXT("M_ArchiveStone"), FLinearColor(.24f, .19f, .139f));
    Install(HallFloor, TEXT("M_AppealSlate"), FLinearColor(.205f, .165f, .123f));
    Install(Wall, TEXT("M_WallStone"), FLinearColor(.119f, .139f, .157f));
    Install(Trim, TEXT("M_TrimStone"), FLinearColor(.19f, .208f, .229f));
    Install(Ruin, TEXT("M_RuinedWall"), FLinearColor(.19f, .16f, .128f));
    Install(Flood, TEXT("M_LowCourtWater"), FLinearColor(.10f, .12f, .088f));
    Install(Water, TEXT("M_Puddle"), FLinearColor(.048f, .063f, .071f));
}

void ADuelArena::OpenPrisonGate()
{
    bPrisonGateOpen = true;
}

void ADuelArena::BuildCastle()
{
    using namespace DuelCastle;
    using DuelGeometry::Shape;
    const FVector HubBase = Hub;
    const FVector Furnace = FurnaceOrigin;
    const auto PlaceDetail = [&](UStaticMesh* Mesh, const FVector& Foot, float Height, float Yaw)
    {
        const FBox Bounds = Mesh->GetBoundingBox();
        const FVector Bottom(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Min.Z);
        const double Scale = Height / FMath::Max(Bounds.GetSize().Z, 1.0);
        const FQuat Rotation(FRotator(0, Yaw, 0));
        UStaticMeshComponent* Detail = NewObject<UStaticMeshComponent>(this);
        Detail->SetupAttachment(SceneRoot);
        Detail->SetStaticMesh(Mesh);
        Detail->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Detail->SetCanEverAffectNavigation(false);
        Detail->SetRelativeTransform(FTransform(Rotation, Foot - Rotation.RotateVector(Bottom * Scale), FVector(Scale)));
        AddInstanceComponent(Detail);
        Detail->RegisterComponent();
        return Detail;
    };
    FStoneBatch Paving(218.f);
    FStoneBatch Masonry(220.f);
    FStoneBatch Carving(129.f);
    FStoneBatch Ruin(246.f);
    FStoneBatch Ceilings(238.f);
    FStoneBatch AshStone(175.f);
    FStoneBatch Shell(306.f);
    struct FInterior
    {
        FVector A;
        FVector B;
        float Half;
        float Spring;
        int32 Finish;
    };
    TArray<FInterior> Interiors;
    const auto Cover = [&](TConstArrayView<FVector> Nodes, float Half, float Spring, int32 Finish)
    {
        for (int32 Index = 0; Index + 1 < Nodes.Num(); ++Index)
        {
            Interiors.Add({Nodes[Index], Nodes[Index + 1], Half, Spring, Finish});
        }
    };
    const auto Collider = [&](const FVector& At, const FVector& Half, const FRotator& Rotation, bool bCamera,
        const TCHAR* Kind = TEXT("CastleCollider"))
    {
        UBoxComponent* Box = NewObject<UBoxComponent>(this,
            MakeUniqueObjectName(this, UBoxComponent::StaticClass(), FName(Kind)));
        Box->SetupAttachment(SceneRoot);
        Box->SetRelativeLocationAndRotation(At, Rotation);
        Box->SetBoxExtent(Half);
        Box->SetCollisionObjectType(ECC_WorldStatic);
        Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Box->SetCollisionResponseToAllChannels(ECR_Ignore);
        Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
        Box->SetCollisionResponseToChannel(ECC_Camera, bCamera ? ECR_Block : ECR_Ignore);
        Box->SetHiddenInGame(true);
        AddInstanceComponent(Box);
        Box->RegisterComponent();
        return Box;
    };
    const auto Floor = [&](const FVector& Center, const FVector2D& Half, const FRotator& Slope,
        float Thickness, float Tint)
    {
        const FVector Along = Slope.RotateVector(FVector::ForwardVector);
        const FVector Across = Slope.RotateVector(FVector::RightVector);
        const FVector Up = Slope.RotateVector(FVector::UpVector);
        Collider(Center - Up * 34.f, FVector(Half.X, Half.Y, 34.f), Slope, false, TEXT("CastleFloor"));
        const FVector Points[] = {Center - Along * Half.X - Across * Half.Y,
            Center + Along * Half.X - Across * Half.Y, Center + Along * Half.X + Across * Half.Y,
            Center - Along * Half.X + Across * Half.Y};
        Paving.Face(Points, 4, Up, Tint);
        Masonry.Stone(Center - Up * (Thickness * .5f + 4.f), FVector(Half.X * 2, Half.Y * 2, Thickness),
            Slope, 14.f, 11.f);
    };
    const auto CrossReach = [&](const FVector& Foot, const FVector& Side, float Span, float Spring, float Depth)
    {
        PointedArch(Masonry, Carving, Foot, Side, Span, Spring, Depth, 59.f, false);
    };
    const auto Walk = [&](TConstArrayView<FVector> Nodes, float Half, float Tint, float Foundation, int32 Finish)
    {
        Cover(Nodes, Half, Finish == 2 ? 475.f : 580.f, Finish);
        for (int32 Index = 0; Index + 1 < Nodes.Num(); ++Index)
        {
            const FVector A = Nodes[Index];
            const FVector B = Nodes[Index + 1];
            const FVector Axis = B - A;
            const FVector Along = Axis.GetSafeNormal2D();
            const FRotator Slope = Axis.Rotation();
            const FRotator Yaw = Along.Rotation();
            const float Run = Axis.Size();
            Floor((A + B) * .5, FVector2D(Run * .5 + 9.f, Half), Slope, Foundation, Tint);
            if (Index != 0)
            {
                const FVector Previous = A - Nodes[Index - 1];
                const double Grade = FMath::Max(FMath::Abs(Axis.Z) / Axis.Size2D(),
                    FMath::Abs(Previous.Z) / Previous.Size2D());
                const double JointLength = FMath::Min<double>(Half, 25.0 / FMath::Max(Grade, .02));
                Floor(A - FVector(0, 0, 1.4), FVector2D(JointLength, Half), Yaw, Foundation * .80f, Tint * .98f);
            }
        }
    };
    const FVector FloodedWalk[] = {HubBase + FVector(-1340, -60, 0), {-2860, -5050, 184}, {-5720, -5450, -300},
        {-8610, -5450, -300}, {-11180, -4740, -300}, {-11160, -2330, -300}, {-9970, -840, -300}};
    const FVector EscortWalk[] = {HubBase + FVector(1390, 250, 0), {2930, -4430, 184}, {4470, -4260, 205},
        {6210, -4670, 205}, {7150, -3420, 205}, {7000, -1490, 205}, {7130, -808, 205}};
    const FVector FloodToFurnace[] = {{-8620, -5460, -300}, {-8810, -7920, -315}, {-6220, -9720, -345},
        {-4090, -10560, -320}, {-2850, -9340, -320}, {-1440, -9240, -320}, {-330, -8890, -320},
        {950, -9620, -320}, {710, -11040, -320}, {1650, -11290, -320}};
    const FVector EscortToFurnace[] = {{6200, -4660, 205}, {6610, -6500, 205}, {6160, -8220, -45},
        {3850, -8560, -320}, {1600, -9260, -320}, {950, -9620, -320}};
    const FVector FloodShortcut[] = {{-3150, -1140, 0}, {-2450, -2240, 18}, {-1900, -3350, 184}, {-1080, -4375, 184}};
    const FVector EscortShortcut[] = {{9085, -650, 370}, {9085, -2330, 370}, {7010, -2810, 205}, {5050, -3050, 205},
        {2580, -3640, 184}, HubBase + FVector(1260, 770, 0)};
    const FVector FurnaceShortcut[] = {{1460, -10630, -320}, {1410, -9410, -320}, {1010, -7420, -237},
        {575, -6100, 117}, HubBase + FVector(440, -790, 0)};
    const FVector EastWalk[] = {HubBase + FVector(820, 625, 0), {1180, -3710, 184}, {2090, -3050, 184}, {4270, -1870, 205},
        {6310, -2140, 205}, {8760, -2930, 267}, {11450, -3120, 370},
        {13790, -2480, 423}, {15140, -2250, 466}, {15140, -870, 466}, {15140, 0, 466}};
    Walk(FloodedWalk, 519.f, .82f, 740.f, 0);
    Walk(EscortWalk, 505.f, .88f, 590.f, 1);
    Walk(FloodToFurnace, 636.f, .72f, 580.f, 2);
    Walk(EscortToFurnace, 536.f, .75f, 720.f, 2);
    Walk(FloodShortcut, 419.f, .86f, 340.f, 0);
    Walk(EscortShortcut, 395.f, .84f, 560.f, 1);
    Walk(FurnaceShortcut, 410.f, .76f, 510.f, 2);
    Walk(EastWalk, 536.f, .94f, 890.f, 3);

    Floor(HubBase, FVector2D(1595, 1050), FRotator::ZeroRotator, 940.f, .99f);
    Floor(Furnace + FVector(-25, 0, 0), FVector2D(2540, 1300), FRotator::ZeroRotator, 795.f, .80f);
    const FVector HubRoom[] = {HubBase - FVector(1592, 0, 0), HubBase + FVector(1592, 0, 0)};
    const FVector FloodRoom[] = {{-10410, 0, -300}, {DuelCourt::StairBaseX, 0, -300}};
    const FVector PrisonStairs[] = {{DuelCourt::StairBaseX, 0, -300}, {DuelCourt::StairTopX, 0, 0}};
    const FVector PrisonHall[] = {{DuelCourt::StairTopX, 0, 0}, {-6090, 0, 0}, {-2530, 0, 0}, {2260, 0, 0}};
    const FVector ArchiveHall[] = {{2180, 0, 0}, {4830, 0, 0}};
    const FVector EscortLobby[] = {{7060, -800, 205}, {7110, 0, 205}, {7940, 0, 205}, {8520, 0, 370}};
    const FVector EscortHall[] = {{8520, 0, 370}, {12600, 0, 370}, {14555, 0, 370}};
    const FVector EscortDais[] = {{13010, 0, 370}, {14225, 0, 466}, {14602, 0, 466}};
    const FVector FurnaceHall[] = {Furnace + FVector(-2560, 0, 0), Furnace + FVector(2510, 0, 0)};
    const FVector Final = DuelCourt::YukiOrigin;
    const FVector FinalStairs[] = {Final + FVector(-6020, 0, -460), Final + FVector(-5390, 0, -460),
        Final + FVector(-3060, 0, 0), Final + FVector(-2170, 0, 0)};
    const FVector FinalHall[] = {Final + FVector(-2170, 0, 0), Final + FVector(520, 0, 0), Final + FVector(2110, 0, 0)};
    const FVector GardenHall[] = {Final + FVector(2110, 0, 0), Final + FVector(4380, 0, 0)};
    Cover(HubRoom, 1045.f, 1120.f, 3);
    Cover(FloodRoom, 1210.f, 540.f, 0);
    Cover(PrisonStairs, 490.f, 550.f, 0);
    Cover(PrisonHall, 1550.f, 1010.f, 0);
    Cover(ArchiveHall, 825.f, 610.f, 1);
    Cover(EscortLobby, 570.f, 530.f, 1);
    Cover(EscortHall, 1430.f, 990.f, 1);
    Cover(EscortDais, 712.f, 630.f, 1);
    Cover(FurnaceHall, 1290.f, 1040.f, 2);
    Cover(FinalStairs, 422.f, 815.f, 3);
    Cover(FinalHall, 1285.f, 1415.f, 3);
    Cover(GardenHall, 898.f, 790.f, 4);
    constexpr double JointOverlap = 92.0;
    struct FOpening
    {
        double Start;
        double End;
        double Lintel;
    };
    const auto Openings = [&](int32 Index, const FVector& A, const FVector& B, const FVector& Out)
    {
        TArray<FOpening> Result;
        const FVector Start = A + Out * 6.f;
        const FVector End = B + Out * 6.f;
        for (int32 Other = 0; Other < Interiors.Num(); ++Other)
        {
            if (Index == Other)
            {
                continue;
            }
            const FInterior& Span = Interiors[Other];
            const FVector Along = (Span.B - Span.A).GetSafeNormal2D();
            const FVector Side = FVector::CrossProduct(FVector::UpVector, Along);
            const double Run = FVector::Dist2D(Span.A, Span.B);
            const double X0 = FVector::DotProduct(Start - Span.A, Along);
            const double X1 = FVector::DotProduct(End - Span.A, Along);
            const double Z0 = FMath::Lerp(Span.A.Z, Span.B.Z, X0 / Run) - A.Z;
            const double Z1 = FMath::Lerp(Span.A.Z, Span.B.Z, X1 / Run) - B.Z;
            double First = 0.0;
            double Last = 1.0;
            const auto Intersect = [&](double From, double To, double Min, double Max)
            {
                if (FMath::IsNearlyEqual(From, To))
                {
                    return From >= Min && From <= Max;
                }
                const double T0 = (Min - From) / (To - From);
                const double T1 = (Max - From) / (To - From);
                First = FMath::Max(First, FMath::Min(T0, T1));
                Last = FMath::Min(Last, FMath::Max(T0, T1));
                return Last > First;
            };
            if (Intersect(X0, X1, -JointOverlap + 14.0, Run + JointOverlap - 14.0) &&
                Intersect(FVector::DotProduct(Start - Span.A, Side), FVector::DotProduct(End - Span.A, Side),
                    -Span.Half + 8.0, Span.Half - 8.0) && Intersect(Z0, Z1, -165.0, 165.0))
            {
                const double LowerCeiling = FMath::Min<double>(Interiors[Index].Spring,
                    Span.Spring + FMath::Min(FMath::Lerp(Z0, Z1, First), FMath::Lerp(Z0, Z1, Last)));
                Result.Add({First, Last, LowerCeiling - 32.0});
            }
        }
        return Result;
    };
    const auto SealWall = [&](int32 Index, const FVector& A, const FVector& B, const FVector& Out, double Ceiling)
    {
        const TArray<FOpening> Portals = Openings(Index, A, B, Out);
        TArray<double> Stops{0.0, 1.0};
        for (const FOpening& Portal : Portals)
        {
            Stops.Append({Portal.Start, Portal.End});
        }
        Stops.Sort();
        const FRotator Rotation = FRotationMatrix::MakeFromXY(B - A, Out).Rotator();
        for (int32 Piece = 0; Piece + 1 < Stops.Num(); ++Piece)
        {
            const double Start = Stops[Piece];
            const double End = Stops[Piece + 1];
            if (End - Start < UE_SMALL_NUMBER)
            {
                continue;
            }
            const double Mid = (Start + End) * .5;
            double Bottom = -122.0;
            for (const FOpening& Portal : Portals)
            {
                if (Mid >= Portal.Start && Mid <= Portal.End)
                {
                    Bottom = FMath::Max(Bottom, Portal.Lintel);
                }
            }
            const FVector At = FMath::Lerp(A, B, Mid) + Out * 56.f + FVector(0, 0, (Ceiling + Bottom) * .5);
            const FVector Size(FVector::Distance(A, B) * (End - Start) + 13.f, 140, Ceiling - Bottom);
            Shell.Stone(At, Size, Rotation, 1.8f);
            Collider(At, Size * .5, Rotation, true, TEXT("CastleShell"));
        }
    };
    UMaterialInterface* Iron = DuelGeometry::ArtMaterial(TEXT("M_ForgedSteel"));
    UMaterialInterface* Glass = DuelGeometry::ArtMaterial(TEXT("M_WitnessGlass"));
    UMaterialInterface* Wick = DuelGeometry::ColorMaterial(this, FLinearColor(3.3f, 1.25f, .34f), true);
    UStaticMesh* Lantern = CourtScan(TEXT("wooden_lantern_01"));
    const FLinearColor LanternColor(1.f, .74f, .49f);
    const auto Lamp = [&](const FVector& At, int32 Finish, const FVector& TowardRoom, bool bModel)
    {
        const FLinearColor Color = Finish == 2 ? FLinearColor(1.f, .64f, .39f) : LanternColor;
        if (bModel && Lantern)
        {
            PlaceDetail(Lantern, At - FVector(0, 0, 22), 48.f, 86.f);
        }
        UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
        Light->SetupAttachment(SceneRoot);
        Light->SetRelativeLocation(At + TowardRoom * 28.f + FVector(0, 0, 8));
        Light->SetIntensity(Finish == 3 ? 14400.f : 11200.f);
        Light->SetLightColor(Color);
        Light->SetAttenuationRadius(Finish == 3 ? 920.f : 790.f);
        Light->SetSourceRadius(23.f);
        Light->SetSpecularScale(Finish == 0 ? .28f : .53f);
        Light->SetCastShadows(true);
        Light->SetShadowBias(.19f);
        Light->SetVolumetricScatteringIntensity(.28f);
        AddInstanceComponent(Light);
        Light->RegisterComponent();
        if (Finish == 1 && At.Y < -2500.f)
        {
            EscortLanterns.Add(Light);
        }
    };
    const auto GuidingLight = [&](const FVector& Source, const FVector& Target, float Intensity,
        float Radius, float Cone)
    {
        USpotLightComponent* Light = NewObject<USpotLightComponent>(this);
        Light->SetupAttachment(SceneRoot);
        Light->SetRelativeLocationAndRotation(Source, (Target - Source).Rotation());
        Light->SetIntensity(Intensity);
        Light->SetLightColor(LanternColor);
        Light->SetInnerConeAngle(Cone * .47f);
        Light->SetOuterConeAngle(Cone);
        Light->SetAttenuationRadius(Radius);
        Light->SetSourceRadius(24.f);
        Light->SetSpecularScale(.26f);
        Light->SetCastShadows(true);
        Light->SetShadowBias(.18f);
        Light->SetVolumetricScatteringIntensity(.26f);
        AddInstanceComponent(Light);
        Light->RegisterComponent();
    };
    const auto Chandelier = [&](const FVector& Center, int32 Finish, bool bGrand)
    {
        const float Scale = bGrand ? 1.0f : .56f;
        const float Radius = 128.f * Scale;
        DuelGeometry::Rod(this, SceneRoot, Center + FVector(0, 0, 14.f), Center + FVector(0, 0, 450.f),
            4.f, Iron);
        Shape(this, SceneRoot, EShape::Tube, Center, FVector(Radius * 2, Radius * 2, 24.f * Scale), Iron);
        for (int32 Candle = 0; Candle < 6; ++Candle)
        {
            const FVector Out = FRotator(0, 60.f * Candle, 0).Vector();
            const FVector Base = Center + Out * Radius * .86f;
            Shape(this, SceneRoot, EShape::Tube, Base + FVector(0, 0, 12 * Scale), FVector(17, 17, 37) * Scale, Iron)
                ->SetCastShadow(false);
            Shape(this, SceneRoot, EShape::Cone, Base + FVector(0, 0, 35 * Scale), FVector(10, 10, 22) * Scale, Wick)
                ->SetCastShadow(false);
        }
        USpotLightComponent* Light = NewObject<USpotLightComponent>(this);
        Light->SetupAttachment(SceneRoot);
        Light->SetRelativeLocationAndRotation(Center - FVector(0, 0, 33), FRotator(-90, 0, 0));
        Light->SetIntensity(bGrand ? 64500.f : 26300.f);
        Light->SetLightColor(Finish == 2 ? FLinearColor(1.f, .68f, .43f) : LanternColor);
        Light->SetInnerConeAngle(29.f);
        Light->SetOuterConeAngle(61.f);
        Light->SetAttenuationRadius(bGrand ? 1550.f : 1170.f);
        Light->SetSourceRadius(bGrand ? 38.f : 22.f);
        Light->SetSpecularScale(.37f);
        Light->SetCastShadows(true);
        Light->SetShadowBias(.22f);
        Light->SetVolumetricScatteringIntensity(.21f);
        AddInstanceComponent(Light);
        Light->RegisterComponent();
    };
    FurnaceFire = NewObject<UPointLightComponent>(this);
    FurnaceFire->SetupAttachment(SceneRoot);
    FurnaceFire->SetRelativeLocation(Furnace + FVector(2130, 385, 172));
    FurnaceFire->SetIntensity(19300.f);
    FurnaceFire->SetLightColor(FLinearColor(1.f, .21f, .07f));
    FurnaceFire->SetAttenuationRadius(1480);
    FurnaceFire->SetCastShadows(true);
    FurnaceFire->SetCastVolumetricShadow(true);
    FurnaceFire->SetShadowBias(.18f);
    FurnaceFire->SetVolumetricScatteringIntensity(1.45f);
    AddInstanceComponent(FurnaceFire);
    FurnaceFire->RegisterComponent();
    const auto Window = [&](const FVector& Foot, const FVector& Along, const FVector& Out, float Width, bool bRose)
    {
        CrossReach(Foot + FVector(0, 0, 210), Along, Width * .53f, 212.f, 78.f);
        for (float Offset : {-Width * .37f, 0.f, Width * .37f})
        {
            DuelGeometry::Rod(this, SceneRoot, Foot + Along * Offset + FVector(0, 0, 220),
                Foot + Along * Offset + FVector(0, 0, 480), 9.f, Iron)->SetCastShadow(true);
        }
        if (Glass && bRose)
        {
            Shape(this, SceneRoot, EShape::Plane, Foot + Out * 89.f + FVector(0, 0, 401),
                FVector(Width, 458, 100), Glass,
                FRotationMatrix::MakeFromXY(Along, -FVector::UpVector).Rotator())->SetCastShadow(false);
        }
    };
    for (int32 Index = 0; Index < Interiors.Num(); ++Index)
    {
        const FInterior& Span = Interiors[Index];
        const FVector Along = (Span.B - Span.A).GetSafeNormal2D();
        const FVector Across = FVector::CrossProduct(FVector::UpVector, Along);
        const FVector Mid = (Span.A + Span.B) * .5;
        const FRotator Yaw = Along.Rotation();
        const FRotator Slope = (Span.B - Span.A).Rotation();
        const double Run = FVector::Dist2D(Span.A, Span.B);
        const float Vault = Span.Half * .53f;
        const float Ceiling = Span.Spring + Vault;
        const float Thickness = Span.Half > 900.f ? 169.f : 113.f;
        const bool bChamber = Span.Half > 825.f;
        const int32 Finish = Span.Finish;
        const bool bFinalChamber = bChamber && Finish == 3 && Mid.X > Final.X - 2200.f;
        FStoneBatch& Wall = Finish == 2 ? AshStone : Masonry;
        const FVector Overlap = (Span.B - Span.A) * (JointOverlap / Run);
        const FVector StartFoot = Span.A - Overlap;
        const FVector EndFoot = Span.B + Overlap;
        for (float Sign : {-1.f, 1.f})
        {
            const FVector Out = Across * Sign;
            SealWall(Index, StartFoot + Out * Span.Half, EndFoot + Out * Span.Half, Out, Ceiling + 128.f);
        }
        for (const FVector& End : {StartFoot, EndFoot})
        {
            const FVector Out = End.Equals(StartFoot) ? -Along : Along;
            SealWall(Index, End - Across * Span.Half, End + Across * Span.Half, Out, Ceiling + 128.f);
        }
        const auto ArchPoint = [&](const FVector& At, float Y)
        {
            const double Unit = FMath::Abs(Y) / Span.Half;
            const double Height = Vault * FMath::Sqrt(FMath::Max(0.0, 1.0 - Unit * Unit));
            return At + Across * Y + FVector(0, 0, Span.Spring + Height);
        };
        constexpr int32 Ribs = 12;
        const double CoverLength = FVector::Dist(StartFoot, EndFoot) + 28.f;
        Ceilings.Stone(Mid + FVector(0, 0, Ceiling + 82.f),
            FVector(CoverLength, Span.Half * 2.f + Thickness * 2.f, 96.f), Slope, 3.f);
        Collider(Mid + FVector(0, 0, Ceiling + 82.f), FVector(CoverLength * .5f, Span.Half + Thickness, 48),
            Slope, true, TEXT("CastleRoof"));
        const int32 Bays = FMath::Max(1, FMath::CeilToInt(Run / (bChamber ? 580.f : 335.f)));
        const float Bay = Run / Bays;
        for (int32 I = 0; I < Bays; ++I)
        {
            const FVector Foot = FMath::Lerp(Span.A, Span.B, (I + .5f) / Bays);
            const FVector Start = FMath::Lerp(Span.A, Span.B, I / static_cast<float>(Bays));
            const FVector End = FMath::Lerp(Span.A, Span.B, (I + 1) / static_cast<float>(Bays));
            for (int32 Rib = 0; Rib < Ribs; ++Rib)
            {
                const float Y0 = FMath::Lerp(-Span.Half, Span.Half, Rib / static_cast<float>(Ribs));
                const float Y1 = FMath::Lerp(-Span.Half, Span.Half, (Rib + 1) / static_cast<float>(Ribs));
                const FVector VaultPanel[] = {
                    ArchPoint(Start - Along * 2.f, Y0), ArchPoint(End + Along * 2.f, Y0),
                    ArchPoint(End + Along * 2.f, Y1), ArchPoint(Start - Along * 2.f, Y1)
                };
                if (Finish == 4 && Glass && (Rib == 5 || Rib == 6) && I % 2 == 1)
                {
                    const FVector Center = (VaultPanel[0] + VaultPanel[1] + VaultPanel[2] + VaultPanel[3]) * .25;
                    Shape(this, SceneRoot, EShape::Plane, Center - FVector(0, 0, 8),
                        FVector(Bay, Y1 - Y0, 100), Glass, FRotator(180, Yaw.Yaw, 0))->SetCastShadow(false);
                }
                else
                {
                    Ceilings.Face(VaultPanel, 4, -FVector::UpVector, Finish == 2 ? .63f : .93f);
                }
            }
            if (I % 2 == 0)
            {
                const FRotator AcrossYaw = Across.Rotation();
                for (int32 Rib = 0; Rib < Ribs; ++Rib)
                {
                    const FVector A = ArchPoint(Foot,
                        FMath::Lerp(-Span.Half, Span.Half, Rib / static_cast<float>(Ribs)));
                    const FVector B = ArchPoint(Foot,
                        FMath::Lerp(-Span.Half, Span.Half, (Rib + 1) / static_cast<float>(Ribs)));
                    const FRotator R = FRotationMatrix::MakeFromXY(B - A, Along).Rotator();
                    Carving.Stone((A + B) * .5 - FVector(0, 0, 7.f),
                        FVector(FVector::Distance(A, B) + 4.f, bChamber ? 116.f : 70.f, 44.f), R, 5.8f, 8.f);
                }
                Carving.Stone(Foot + FVector(0, 0, Ceiling - 27.f), FVector(164, 134, 71), AcrossYaw, 12.f, 9.f);
            }
            if (Finish != 4 && !bFinalChamber && I % (bChamber ? 3 : 6) == 1)
            {
                Chandelier(Foot + FVector(0, 0, Span.Spring - (bChamber ? 335.f : 146.f)), Finish, bChamber);
            }
            if (Finish == 4 && I % 2 == 1)
            {
                const FVector Origin = Foot + FVector(0, 0, Ceiling - 35.f);
                USpotLightComponent* Beam = NewObject<USpotLightComponent>(this);
                Beam->SetupAttachment(SceneRoot);
                Beam->SetRelativeLocationAndRotation(Origin, FRotator(-67.f, 32.f, 0));
                Beam->SetIntensity(415000.f);
                Beam->SetLightColor(FLinearColor(1.f, .83f, .60f));
                Beam->SetInnerConeAngle(14.f);
                Beam->SetOuterConeAngle(38.f);
                Beam->SetAttenuationRadius(2110.f);
                Beam->SetVolumetricScatteringIntensity(1.67f);
                Beam->SetCastShadows(true);
                Beam->SetCastVolumetricShadow(true);
                Beam->SetShadowBias(.15f);
                Beam->SetVisibility(false);
                AddInstanceComponent(Beam);
                Beam->RegisterComponent();
                GardenSunbeams.Add(Beam);
            }
            for (float Sign : {-1.f, 1.f})
            {
                const FVector Out = Across * Sign;
                const FVector Edge = Foot + Out * Span.Half;
                if (!Openings(Index, Start + Out * Span.Half, End + Out * Span.Half, Out).IsEmpty())
                {
                    continue;
                }
                const bool bWindow = Finish != 2 && I % 4 == 2 && Bay > 247.f;
                const FVector Face = Edge + Out * (Thickness * .5f);
                Collider(Face + FVector(0, 0, Span.Spring * .5f),
                    FVector(Bay * .5f + 2.f, Thickness * .5f, Span.Spring * .5f + 45.f), Slope, true, TEXT("CastlePier"));
                const float Course = bChamber ? 92.f : 78.f;
                const int32 Courses = FMath::CeilToInt((Span.Spring + 83.f) / Course);
                if (bChamber)
                {
                    const float Depth = bWindow ? Thickness * .51f : Thickness;
                    Wall.Stone(Face + Out * (bWindow ? Thickness * .28f : 0.f) +
                        FVector(0, 0, Span.Spring * .50f),
                        FVector(Bay + 3.f, Depth, Span.Spring + 114.f), Yaw, 13.f, 21.f);
                    for (float Z : {100.f, Span.Spring - 92.f})
                    {
                        Carving.Stone(Edge + FVector(0, 0, Z), FVector(Bay + 3.f, 65.f, 38.f), Yaw, 7.f, 7.f);
                    }
                }
                for (int32 Row = 0; Row < Courses; ++Row)
                {
                    if (bChamber)
                    {
                        break;
                    }
                    const float Z = (Row + .5f) * Course;
                    const bool bRecess = bWindow && Z >= 220.f && Z <= 475.f;
                    const float Depth = bRecess ? Thickness * .24f : Thickness;
                    const float Offset = bRecess ? Thickness * .38f : 0.f;
                    const int32 Pieces = bRecess ? 1 : Bay > 275.f ? 2 : 1;
                    for (int32 Piece = 0; Piece < Pieces; ++Piece)
                    {
                        const float Shift = (Piece + .5f) * Bay / Pieces - Bay * .5f;
                        const float Groove = FMath::Cos((Index * 13 + I * 3 + Row * 19 + Piece * 5.f) * .86f) * 3.4f;
                        Wall.Stone(Face + Out * (Offset + Groove) + Along * Shift + FVector(0, 0, Z),
                            FVector(Bay / Pieces + 2.2f, Depth, Course + 2.f), Yaw, 5.4f, FMath::Abs(Groove) * 6.f);
                    }
                }
                if (bWindow)
                {
                    Window(Edge - Out * 17.f, Along, Out, FMath::Min(244.f, Bay - 29.f), Finish == 3 || Finish == 4);
                }
                if (I % 2 == 0)
                {
                    Carving.Stone(Edge - Out * 12.f + FVector(0, 0, Span.Spring * .50f),
                        FVector(112, 86, Span.Spring + 17), Yaw, 11.f, 20.f);
                    Carving.Stone(Edge - Out * 13.f + FVector(0, 0, 42),
                        FVector(174, 140, 86), Yaw, 7.f, 18.f);
                    Carving.Stone(Edge - Out * 17.f + FVector(0, 0, Span.Spring - 28.f),
                        FVector(163, 132, 56.f), Yaw, 9.f, 7.f);
                }
                if ((I + (Sign > 0 ? 1 : 2)) % (bChamber ? 3 : 6) == 0)
                {
                    const FVector At = Edge - Out * 95.f + FVector(0, 0, bChamber ? 266.f : 207.f);
                    Carving.Stone(At - FVector(0, 0, 34), FVector(63, 67, 29), Yaw, 8.f, 7.f);
                    Lamp(At, Finish, -Out, true);
                }
            }
        }
    }
    for (float Sign : {-1.f, 1.f})
    {
        const FVector Foot = Furnace + FVector(20, Sign * 1295.f, 0);
        Collider(Foot + FVector(0, 0, 410), FVector(2580, 77.f, 410), FRotator::ZeroRotator, true);
        for (int32 Bay = -3; Bay <= 3; ++Bay)
        {
            const FVector Pier = Foot + FVector(Bay * 672.f, 0, 0);
            Masonry.Stone(Pier + FVector(0, 0, 380), FVector(684, 180, 770), FRotator::ZeroRotator, 20.f, 34.f);
            CrossReach(Pier - FVector(0, Sign * 108, 0), FVector::ForwardVector, 281.f, 458.f, 123.f);
        }
        const FVector Back = Furnace + FVector(2380, Sign * 645.f, 0);
        Collider(Back + FVector(0, 0, 358), FVector(105.f, 645.f, 358), FRotator::ZeroRotator, true);
        Ruin.Stone(Back + FVector(0, 0, 356), FVector(224, 1321, 734), FRotator::ZeroRotator, 21.f, 42.f);
    }
    CrossReach(Furnace + FVector(DuelCourt::GateX, 0, 0), FVector::RightVector, 320.f, 579.f, 248.f);
    const FVector Kiln = Furnace + FVector(2360, 0, 0);
    CrossReach(Kiln, FVector::RightVector, 348.f, 552.f, 278.f);
    Ruin.Stone(Kiln + FVector(338, 0, 460), FVector(370, 767, 940), FRotator::ZeroRotator, 32.f);
    Collider(Kiln + FVector(341, 0, 420), FVector(170, 400, 440), FRotator::ZeroRotator, true);
    for (float Offset : {-540.f, 590.f})
    {
        const FVector At = Final + FVector(Offset, 0, 1220);
        Chandelier(At, 3, true);
    }
    for (const FVector& Foot : {Final + FVector(-4860, -325, -349), Final + FVector(-3880, 325, -144)})
    {
        const FVector At = Foot + FVector(0, 0, 240);
        Carving.Stone(At - FVector(0, 0, 31), FVector(73, 74, 31), FRotator::ZeroRotator, 7.f);
        if (Lantern)
        {
            PlaceDetail(Lantern, At - FVector(0, 0, 17), 53.f, Foot.Y < 0.f ? 90.f : -90.f);
        }
        Lamp(At, 3, Foot.Y < 0.f ? FVector::RightVector : -FVector::RightVector, false);
        GuidingLight(At + FVector(0, Foot.Y < 0.f ? 27.f : -27.f, 18),
            Foot + FVector(155, -Foot.Y, 2), 58000.f, 960.f, 48.f);
    }
    if (Glass)
    {
        constexpr double Width = 392.0;
        const FVector Back = Final + FVector(4350, 0, 0);
        for (double Offset : {-558.0, 0.0, 558.0})
        {
            const FVector Foot = Back + FVector(0, Offset, 125);
            const double Size = Offset == 0.0 ? 1.0 : .83;
            CrossReach(Foot, FVector::RightVector, Width * .58 * Size, 374.f * Size, 95.f);
            Shape(this, SceneRoot, EShape::Plane, Foot + FVector(-8, 0, 353),
                FVector(Width * Size, 591 * Size, 100), Glass,
                FRotationMatrix::MakeFromXY(FVector::RightVector, -FVector::UpVector).Rotator())->SetCastShadow(false);
        }
    }
    const FVector TowerBase = HubBase + FVector(0, 1450, 0);
    Masonry.Stone(TowerBase + FVector(0, 0, 1330), FVector(1110, 970, 2710), FRotator::ZeroRotator, 22.f, 16.f);
    Collider(TowerBase + FVector(0, 0, 1340), FVector(555, 486, 1370), FRotator::ZeroRotator, true);
    for (float Z : {520.f, 1730.f, 2260.f, 2700.f})
    {
        Carving.Stone(TowerBase + FVector(0, 0, Z), FVector(1195, 1050, 76), FRotator::ZeroRotator, 13.f, 8.f);
    }
    for (float X : {-465.f, 465.f})
    {
        for (float Y : {-414.f, 414.f})
        {
            Masonry.Stone(TowerBase + FVector(X, Y, 1460), FVector(215, 211, 3010), FRotator::ZeroRotator, 18.f, 15.f);
        }
    }
    if (UStaticMesh* Fort = CourtScan(TEXT("modular_fort_01")))
    {
        for (const FVector& Foot : {FVector(-4050, -1510, -355), FVector(-6610, -6440, -940),
            FVector(3150, -2560, -467), FVector(9630, -4300, -474), FVector(13480, -3940, -445),
            FVector(18840, -1690, -638), FVector(22960, 2120, -730)})
        {
            PlaceDetail(Fort, Foot, 1860.f + FMath::Abs(Foot.X) * .022f, Foot.X * .008f);
        }
    }
    if (UStaticMesh* Door = CourtScan(TEXT("large_castle_door")))
    {
        for (float Side : {-1.f, 1.f})
        {
            const FVector Foot = HubBase + FVector(Side * 1015.f, 990, 0);
            const FBox Bounds = Door->GetBoundingBox();
            PlaceDetail(Door, Foot, 505.f, Bounds.GetSize().X > Bounds.GetSize().Y ? 0.f : 90.f);
        }
    }
    for (int32 Wing = 0; Wing < WingCount; ++Wing)
    {
        const FVector Recess = HubBase + FVector((Wing - 1) * 430.f, 949.f, 80.f);
        CrossReach(Recess, FVector::ForwardVector, 146.f, 358.f, 94.f);
        Lamp(Recess + FVector(0, -86, 364), 3, -FVector::RightVector, false);
        const FVector Ember = Recess + FVector(0, -79, 116);
        Carving.Stone(Ember - FVector(0, 0, 26), FVector(108, 60, 25), FRotator::ZeroRotator, 8.f);
        Shape(this, SceneRoot, EShape::Tube, Ember, FVector(55, 49, 37), Iron);
        VerdictLights.Add(Shape(this, SceneRoot, EShape::Cone, Ember + FVector(0, 0, 33),
            FVector(21, 21, 63), Wick));
        Cast<UStaticMeshComponent>(VerdictLights.Last())->SetCastShadow(false);
    }
    LightPlane(HubBase + FVector(0, 0, 1.5f), FVector2D(190, 190), FRotator::ZeroRotator, EDuelLight::Seal,
        FLinearColor(1.f, .73f, .38f), 2.3f, .12f)->SetCastShadow(false);

    UMaterialInterface* BlackIron = DuelGeometry::ArtMaterial(TEXT("M_ForgedSteel"));
    UMaterialInterface* Marker = DuelGeometry::ColorMaterial(this, FLinearColor(.58f, .31f, .11f), false, .5f, .72f);
    const auto Grille = [&](const FVector& At, const FRotator& Facing, float Half, UBoxComponent*& Blocker)
    {
        USceneComponent* Root = DuelGeometry::Joint(this, SceneRoot, At);
        Root->SetRelativeRotation(Facing);
        Blocker = Collider(At + FVector(0, 0, 214.f), FVector(40.f, Half, 215.f), Facing, false);
        for (float Across = -Half + 23.f; Across < Half; Across += 76.f)
        {
            Shape(this, Root, EShape::Box, FVector(0, Across, 208), FVector(26, 22, 414), BlackIron);
            Shape(this, Root, EShape::Cone, FVector(0, Across, 420), FVector(39, 39, 60), BlackIron);
        }
        for (float Height : {100.f, 348.f})
        {
            Shape(this, Root, EShape::Box, FVector(0, 0, Height), FVector(38, Half * 2, 28), BlackIron);
        }
        return Root;
    };
    const auto Display = [&](const FVector& At, float Yaw, bool bKey)
    {
        Carving.Stone(At + FVector(0, 0, 41), FVector(97, 97, 84), FRotator(0, Yaw, 0), 9.f, 6.f);
        Carving.Stone(At + FVector(0, 0, 86), FVector(135, 127, 24), FRotator(0, Yaw, 0), 7.f);
        Collider(At + FVector(0, 0, 46), FVector(53, 53, 47), FRotator(0, Yaw, 0), false);
        if (bKey)
        {
            Shape(this, SceneRoot, EShape::Tube, At + FVector(0, 0, 106), FVector(55, 55, 9), Marker);
            Shape(this, SceneRoot, EShape::Box, At + FVector(14, 22, 108), FVector(21, 75, 11), Marker,
                FRotator(0, Yaw + 28.f, 0));
        }
        else if (UMaterialInterface* Paper = DuelGeometry::ArtMaterial(TEXT("M_EvidenceDocket")))
        {
            Shape(this, SceneRoot, EShape::Plane, At + FVector(0, 0, 100), FVector(90, 77, 100), Paper,
                FRotator(0, Yaw, 0))->SetCastShadow(false);
        }
        const FVector Pin = At + FVector(-38, 40, 101.f);
        Shape(this, SceneRoot, EShape::Tube, Pin, FVector(23, 23, 12), Iron);
        Shape(this, SceneRoot, EShape::Cone, Pin + FVector(0, 0, 19), FVector(10, 10, 20), Wick)
            ->SetCastShadow(false);
        GuidingLight(Pin + FVector(0, 0, 64), At + FVector(0, 0, 92), 1180.f, 235.f, 54.f);
    };
    if (Lantern)
    {
        for (const FVector& Base : {HubBase + FVector(-720, -620, 0), HubBase + FVector(715, -623, 0),
            FVector(-2860, -5435, 184), FVector(-11160, -2790, -300), FVector(4450, -4640, 205),
            FVector(7200, -3150, 205), FVector(-2860, -9670, -320), FVector(660, -11430, -320),
            FVector(11450, -3530, 370), FVector(15140, -1340, 466), FVector(17400, 335, 845)})
        {
            Carving.Stone(Base + FVector(0, 0, 84), FVector(86, 81, 170), FRotator::ZeroRotator, 9.f, 12.f);
            PlaceDetail(Lantern, Base + FVector(0, 0, 165), 49.f, Base.X * .023f);
            UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
            Light->SetupAttachment(SceneRoot);
            Light->SetRelativeLocation(Base + FVector(0, 0, 200));
            Light->SetIntensity(8400.f);
            Light->SetLightColor(LanternColor);
            Light->SetAttenuationRadius(635.f);
            Light->SetSpecularScale(.35f);
            Light->SetCastShadows(true);
            Light->SetShadowBias(.19f);
            Light->SetVolumetricScatteringIntensity(.28f);
            AddInstanceComponent(Light);
            Light->RegisterComponent();
        }
    }
    if (UStaticMesh* Latch = CourtScan(TEXT("gate_latch_01")))
    {
        PlaceDetail(Latch, Wings[2].Clue[1] + FVector(0, 0, 101), 31.f, 80.f);
    }
    const FVector ShortcutDoor[] = {{-2518, -2135, 12}, {9085, -1660, 370}, {1390, -9630, -320}};
    for (int32 Wing = 0; Wing < WingCount; ++Wing)
    {
        UBoxComponent* Lock = nullptr;
        CastleGrilles.Add(Grille(ShortcutDoor[Wing], FRotator(0, Wing == 0 ? -58.f : 90.f, 0),
            Wing == 0 ? 417.f : 406.f, Lock));
        // Set the gate on the aisle. The switch sits on the side that faces the court.
        CastleShortcuts.Add(Lock);
        for (int32 Item = 0; Item < 2; ++Item)
        {
            if (Wing != 0 || Item != 1)
            {
                Display(Wings[Wing].Clue[Item], 15.f + 45.f * Wing, false);
            }
        }
        Display(Wings[Wing].Key, 25.f * Wing, true);
        Display(Record(Wing), -5.f, false);
        const FVector At = Wings[Wing].Court + FVector(DuelCourt::GateX, 0, 0);
        CastleFogBlocks.Add(Collider(At + FVector(0, 0, 305), FVector(43, 340, 305), FRotator::ZeroRotator, false));
        if (Wing == 2 && (DuelGeometry::ArtMaterial(TEXT("M_FogGate")) != nullptr))
        {
            FurnaceFog = Shape(this, SceneRoot, EShape::Plane, At + FVector(-6, 0, 281), FVector(627, 558, 100),
                DuelGeometry::ArtMaterial(TEXT("M_FogGate")),
                FRotationMatrix::MakeFromXY(FVector::RightVector, -FVector::UpVector).Rotator());
            FurnaceFog->SetCastShadow(false);
        }
        for (float Sign : {-1.f, 1.f})
        {
            const FVector Pylon = At + FVector(0, Sign * 870.f, 0);
            Masonry.Stone(Pylon + FVector(0, 0, 370), FVector(280, 1095, 742), FRotator::ZeroRotator, 16.f, 22.f);
            Collider(Pylon + FVector(0, 0, 370), FVector(134, 540, 370), FRotator::ZeroRotator, true);
        }
        const FVector ApproachLamp = At + FVector(-215, 249, 291);
        Carving.Stone(ApproachLamp - FVector(0, 0, 43), FVector(88, 76, 33), FRotator::ZeroRotator, 8.f);
        Lamp(ApproachLamp, Wing == 2 ? 2 : 1, -FVector::RightVector, true);
    }
    Display(IndexStone, 90.f, false);
    Display(FinalRecord(), 90.f, false);
    CrossReach(EastGate, FVector::ForwardVector, 466.f, 690.f, 267.f);
    UBoxComponent* East = nullptr;
    EastGrille = Grille(EastGate, FRotator(0, 90, 0), 572.f, East);
    EastBarrier = East;
    for (float Sign : {-1.f, 1.f})
    {
        const FVector Side = EastGate + FVector(Sign * 655.f, 0, 242.f);
        Masonry.Stone(Side, FVector(281, 250, 492), FRotator::ZeroRotator, 14.f);
        Collider(Side, FVector(138, 125, 247), FRotator::ZeroRotator, true);
    }
    const FVector FinalGate = DuelCourt::YukiOrigin + FVector(DuelCourt::GateX, 0, 0);
    CastleFogBlocks.Add(Collider(FinalGate + FVector(0, 0, 324), FVector(45, 445, 324), FRotator::ZeroRotator, false));
    const FVector Dawn = DuelCourt::YukiOrigin + FVector(DuelCourt::YukiExitX, 0, 0);
    UBoxComponent* Exit = nullptr;
    DawnGrille = Grille(Dawn, FRotator::ZeroRotator, 1120, Exit);
    DawnBarrier = Exit;
    for (float Sign : {-1.f, 1.f})
    {
        const FVector Stone = FinalGate + FVector(0, Sign * 890.f, 440);
        Masonry.Stone(Stone, FVector(234, 900, 902), FRotator::ZeroRotator, 15.f, 22.f);
        Collider(Stone, FVector(110, 452, 455), FRotator::ZeroRotator, true);
        const FVector SideOfDawn = Dawn + FVector(0, Sign * 1200.f, 338);
        Masonry.Stone(SideOfDawn, FVector(234, 244, 696), FRotator::ZeroRotator, 14.f, 20.f);
        Collider(SideOfDawn, FVector(110, 125, 351), FRotator::ZeroRotator, true);
    }
    if (UMaterialInterface* Water = DuelGeometry::ArtMaterial(TEXT("M_LowCourtWater")))
    {
        WaterSheet = Shape(this, SceneRoot, EShape::Plane, FVector(-9140, -4940, -276),
            FVector(5460, 7780, 100), Water);
        Cast<UStaticMeshComponent>(WaterSheet)->SetCastShadow(false);
        Shape(this, SceneRoot, EShape::Plane, FVector(4500, -4850, -1630), FVector(58600, 50500, 100), Water)->SetCastShadow(false);
    }
    const auto Install = [&](const FStoneBatch& Mesh, const TCHAR* Material)
    {
        Mesh.Install(this, SceneRoot, DuelGeometry::ArtMaterial(Material));
    };
    Install(Paving, TEXT("M_LowCourt"));
    Install(Masonry, TEXT("M_WallStone"));
    Install(Carving, TEXT("M_TrimStone"));
    Install(Ruin, TEXT("M_RuinedWall"));
    Install(Ceilings, TEXT("M_ArchiveStone"));
    Install(AshStone, TEXT("M_RuinedWall"));
    Install(Shell, TEXT("M_ArchiveStone"));
    for (int32 Wing = 0; Wing <= WingCount; ++Wing)
    {
        SetCastleFog(Wing, true);
    }
}

void ADuelArena::SetCastleFog(int32 Wing, bool bClosed)
{
    if (CastleFogBlocks.IsValidIndex(Wing))
    {
        CastleFogBlocks[Wing]->SetCollisionEnabled(bClosed ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
    }
    UStaticMeshComponent* Fog = Wing == 0 ? FogGate.Get() : Wing == 1 ? AppealFog.Get() :
        Wing == 2 ? FurnaceFog.Get() : YukiFog.Get();
    if (Fog)
    {
        Fog->SetVisibility(bClosed);
    }
}

void ADuelArena::ApplyCastleState(uint8 Seals, uint8 Shortcuts, bool bEastOpen, bool bFinalRecord)
{
    for (int32 Wing = 0; Wing < DuelCastle::WingCount; ++Wing)
    {
        const bool bOpen = (Shortcuts & DuelCastle::WingBit(Wing)) != 0;
        CastleShortcuts[Wing]->SetCollisionEnabled(bOpen ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
        CastleGrilles[Wing]->SetVisibility(!bOpen, true);
        VerdictLights[Wing]->SetVisibility((Seals & DuelCastle::WingBit(Wing)) == 0);
        if (Seals & DuelCastle::WingBit(Wing))
        {
            SetCastleFog(Wing, false);
        }
    }
    EastBarrier->SetCollisionEnabled(bEastOpen ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
    EastGrille->SetVisibility(!bEastOpen, true);
    DawnBarrier->SetCollisionEnabled(bFinalRecord ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
    DawnGrille->SetVisibility(!bFinalRecord, true);
    if (WaterSheet)
    {
        const bool bDrained = (Seals & (DuelCastle::WingBit(1) | DuelCastle::WingBit(2))) != 0;
        WaterSheet->SetRelativeLocation(FVector(-9140, -4940, bDrained ? -505.f : -276.f));
    }
    for (UPointLightComponent* Light : EscortLanterns)
    {
        Light->SetIntensity((Seals & DuelCastle::WingBit(0)) ? 5600.f : 11200.f);
    }
    if (FurnaceFire)
    {
        FurnaceFire->SetVisibility((Seals & DuelCastle::WingBit(1)) && !(Seals & DuelCastle::WingBit(2)));
    }
}

void ADuelArena::ShowWarrant(bool bVisible)
{
    if (WarrantStand)
    {
        WarrantStand->SetVisibility(bVisible, true);
    }
}

void ADuelArena::SetGateClosed(bool bClosed)
{
    if (FogGate)
    {
        FogGate->SetVisibility(bClosed && !bAppeal);
    }
    if (AppealFog)
    {
        AppealFog->SetVisibility(bClosed && bAppeal && !bYukiCourt);
    }
    if (YukiFog)
    {
        YukiFog->SetVisibility(bClosed && bYukiCourt);
    }
}

FVector ADuelArena::CourtOrigin() const
{
    if (bNorthOrigin) return NorthOrigin;
    return bYukiCourt ? DuelCourt::YukiOrigin : bFurnaceCourt ? DuelCastle::FurnaceOrigin : bAppeal ? DuelCourt::AppealOrigin : FVector::ZeroVector;
}

FBox2D ADuelArena::CourtBounds(double NorthClearance) const
{
    if (bNorthOrigin)
    {
        using namespace DuelNorthLayout;
        if (const FRoom* Room = Rooms().FindByPredicate([&](const FRoom& Candidate)
            { return Candidate.Kind == ERoomKind::Combat && FMath::IsNearlyEqual(Candidate.Bounds.Min.Z, NorthOrigin.Z, 1.); }))
        {
            // The wall boxes straddle the layout boundary. The face on the
            // room side is half a panel thickness inside the authored bounds.
            const FVector2D Inset(WallThickness * .5 + NorthClearance);
            return FBox2D(FVector2D(Room->Bounds.Min - NorthOrigin) + Inset,
                FVector2D(Room->Bounds.Max - NorthOrigin) - Inset);
        }
    }
    return FBox2D(FVector2D(-DuelCourt::HalfLength, -DuelCourt::HalfWidth),
        FVector2D(DuelCourt::HalfLength, DuelCourt::HalfWidth));
}

void ADuelArena::SetFurnaceCourt(bool bActive)
{
    bFurnaceCourt = bActive;
    SetAppeal(bAppeal);
}

FVector ADuelArena::GroundMark(const FVector& World) const
{
    const FVector Origin = CourtOrigin();
    const FVector At = World - Origin;
    const ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    if (bNorthOrigin && Mode && Mode->CanExplore()) return FVector(World.X, World.Y, Origin.Z + 8.f);
    return Origin + FVector(CourtBounds(8.).GetClosestPointTo(FVector2D(At)), 8.);
}

void ADuelArena::SetAppeal(bool bActive)
{
    bAppeal = bActive;
    for (int32 I = 0; I < CombatCameraWalls.Num(); ++I)
    {
        CombatCameraWalls[I]->SetRelativeLocation(CombatCameraWallCenters[I] + CourtOrigin());
    }
}

void ADuelArena::SetYukiCourt(bool bActive)
{
    bYukiCourt = bActive;
    SetAppeal(bAppeal);
}

void ADuelArena::SetYukiRouteOpen(bool bOpen)
{
    if (YukiRoute)
    {
        YukiRoute->SetCollisionEnabled(bOpen ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
    }
}

void ADuelArena::SetArchiveGateOpen(bool bOpen)
{
    bArchiveGateOpen = bOpen;
    if (!bOpen && ArchiveGate)
    {
        ArchiveGateLift = 0.f;
        ArchiveGate->SetRelativeLocation(ArchiveGateBase);
        ArchiveGate->SetVisibility(true);
    }
    if (!bOpen && AppealGrille)
    {
        AppealGrille->SetRelativeLocation(AppealGrilleBase);
        AppealGrille->SetVisibility(true);
    }
}

void ADuelArena::SetCombatCamera(bool bActive)
{
    for (UBoxComponent* Wall : CombatCameraWalls)
    {
        Wall->SetCollisionEnabled(bActive ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
    }
}

void ADuelArena::BuildLight()
{
    if (UMaterialInterface* SkyMaterial = DuelGeometry::ArtMaterial(TEXT("M_Overcast")))
    {
        UStaticMeshComponent* Dome = DuelGeometry::Shape(this, SceneRoot, EShape::Ball, FVector(0, 0, 0),
            FVector(148000), SkyMaterial);
        Dome->SetCastShadow(false);
    }

    ASkyLight* Sky = GetWorld()->SpawnActor<ASkyLight>();
    USkyLightComponent* Fill = Sky->GetLightComponent();
    Fill->SetMobility(EComponentMobility::Movable);
    Fill->SetIntensity(bCastleInterior ? .27f : .61f);
    Fill->SetLightColor(FLinearColor(.73f, .82f, 1.f));
    if (UTextureCube* HDR = LoadObject<UTextureCube>(nullptr,
        TEXT("/Game/DuelArt/Textures/T_Overcast.T_Overcast"), nullptr, LOAD_NoWarn | LOAD_Quiet))
    {
        Fill->SourceType = SLS_SpecifiedCubemap;
        Fill->SetCubemap(HDR);
    }
    else
    {
        Fill->RecaptureSky();
    }

    ADirectionalLight* Key = GetWorld()->SpawnActor<ADirectionalLight>(
        FVector(1500, -420, 1700), FRotator(-32, 147, 0));
    Moon = CastChecked<UDirectionalLightComponent>(Key->GetLightComponent());
    Moon->SetMobility(EComponentMobility::Movable);
    Moon->SetVolumetricScatteringIntensity(bCastleInterior ? 0.f : .92f);
    SetBattlePhase(1);

    if (!bCastleInterior)
    {
        ADirectionalLight* FillKey = GetWorld()->SpawnActor<ADirectionalLight>(
            FVector(-1500, 310, 1200), FRotator(-24, -12, 0));
        UDirectionalLightComponent* Bounce = CastChecked<UDirectionalLightComponent>(FillKey->GetLightComponent());
        Bounce->SetMobility(EComponentMobility::Movable);
        Bounce->SetCastShadows(false);
        Bounce->SetLightColor(FLinearColor(.69f, .78f, 1.f));
        Bounce->SetIntensity(.74f);
    }

    USpotLightComponent* GateLight = NewObject<USpotLightComponent>(this, TEXT("AtlasLight"));
    GateLight->SetupAttachment(SceneRoot);
    GateLight->SetMobility(EComponentMobility::Movable);
    GateLight->SetRelativeLocation(FVector(2680, -100, 613));
    GateLight->SetRelativeRotation(FRotator(-13, 178, 0));
    GateLight->SetIntensity(74000.f);
    GateLight->SetLightColor(FLinearColor(1.f, .73f, .39f));
    GateLight->SetInnerConeAngle(11.f);
    GateLight->SetOuterConeAngle(31.f);
    GateLight->SetAttenuationRadius(2340.f);
    GateLight->SetVolumetricScatteringIntensity(2.1f);
    GateLight->SetCastShadows(bCastleInterior);
    GateLight->SetCastVolumetricShadow(bCastleInterior);
    AddInstanceComponent(GateLight);
    GateLight->RegisterComponent();
    for (const FTransform& Rest : {DuelCourt::Checkpoint(), DuelCourt::AppealCheckpoint()})
    {
        UPointLightComponent* Respite = NewObject<UPointLightComponent>(this);
        Respite->SetupAttachment(SceneRoot);
        Respite->SetRelativeLocation(Rest.GetLocation() + FVector(0, 0, 42));
        Respite->SetIntensity(7400.f);
        Respite->SetLightColor(FLinearColor(1.f, .71f, .42f));
        Respite->SetAttenuationRadius(440.f);
        Respite->SetCastShadows(false);
        Respite->SetVolumetricScatteringIntensity(.85f);
        AddInstanceComponent(Respite);
        Respite->RegisterComponent();
    }

    UMaterialInterface* Iron = DuelGeometry::ColorMaterial(this, FLinearColor(.09f, .072f, .057f), false, .54f, .63f);
    UMaterialInterface* Coal = DuelGeometry::ColorMaterial(this, FLinearColor(3.2f, .72f, .11f, .9f), true);
    for (const FVector& At : {FVector(-9540, -553, -119), FVector(-8570, 586, -37),
        FVector(-7520, -550, 207), FVector(-6905, 930, 239), FVector(-6400, 281, 210),
        FVector(-4670, -676, 182), FVector(-2650, 560, 232),
        FVector(3030, -625, 306), FVector(3630, 626, 327), FVector(4490, -542, 298), FVector(4490, 542, 298)})
    {
        DuelGeometry::Shape(this, SceneRoot, EShape::Tube, At - FVector(0, 0, 19), FVector(31, 31, 16), Iron);
        DuelGeometry::Shape(this, SceneRoot, EShape::Cone, At - FVector(0, 0, 2), FVector(14, 14, 32), Coal)
            ->SetCastShadow(false);
        for (int32 Bar = 0; Bar < 6; ++Bar)
        {
            const FVector Edge = FRotator(0, Bar * 60.f, 0).Vector() * 15.f;
            DuelGeometry::Rod(this, SceneRoot, At + Edge - FVector(0, 0, 18), At + Edge * .67 + FVector(0, 0, 19), 1.7f, Iron);
        }
        const float GroundZ = At.X < DuelCourt::StairBaseX ? DuelCourt::LowCourtZ : 0.f;
        const FVector Mount = At.X > 0 ? FVector(At.X, At.Y < 0 ? -860.f : 860.f, At.Z - 80.f)
            : FVector(At.X, At.Y, GroundZ);
        DuelGeometry::Rod(this, SceneRoot, At - FVector(0, 0, 23), Mount, 5.f, Iron);
        UPointLightComponent* Sconce = NewObject<UPointLightComponent>(this);
        Sconce->SetupAttachment(SceneRoot);
        Sconce->SetRelativeLocation(At + FVector(0, 0, 15));
        Sconce->SetIntensity(At.X > 0 ? 19100.f : 9800.f);
        Sconce->SetLightColor(At.X > 0 ? FLinearColor(1.f, .68f, .37f) : FLinearColor(.96f, .57f, .24f));
        Sconce->SetAttenuationRadius(950.f);
        Sconce->SetVolumetricScatteringIntensity(1.16f);
        Sconce->SetCastShadows(bCastleInterior);
        AddInstanceComponent(Sconce);
        Sconce->RegisterComponent();
    }

    // Warm shafts mark the route out of the cold gatehouse and the evidence alcove.
    const FVector KeyPositions[] = {FVector(-8450, -470, 390), FVector(-6880, 1120, 450)};
    const FVector KeyTargets[] = {FVector(-8910, -220, -285), DuelCourt::Warrant + FVector(0, 0, 85)};
    for (int32 I = 0; I < UE_ARRAY_COUNT(KeyPositions); ++I)
    {
        USpotLightComponent* Lamp = NewObject<USpotLightComponent>(this);
        Lamp->SetupAttachment(SceneRoot);
        Lamp->SetRelativeLocationAndRotation(KeyPositions[I], (KeyTargets[I] - KeyPositions[I]).Rotation());
        Lamp->SetMobility(EComponentMobility::Movable);
        Lamp->SetIntensity(I == 0 ? 69000.f : 38000.f);
        Lamp->SetLightColor(I == 0 ? FLinearColor(.95f, .71f, .41f) : FLinearColor(1.f, .58f, .26f));
        Lamp->SetInnerConeAngle(14.f);
        Lamp->SetOuterConeAngle(I == 0 ? 32.f : 24.f);
        Lamp->SetAttenuationRadius(1460.f);
        Lamp->SetVolumetricScatteringIntensity(1.35f);
        Lamp->SetCastShadows(bCastleInterior);
        Lamp->SetCastVolumetricShadow(bCastleInterior);
        AddInstanceComponent(Lamp);
        Lamp->RegisterComponent();
    }

    const FVector Appeal = DuelCourt::AppealOrigin;
    const FVector HallLights[] = {
        Appeal + FVector(-2530, -480, 322), Appeal + FVector(410, -810, 1320),
        Appeal + FVector(1890, 690, 925), Appeal + FVector(3450, 0, 1025), FVector(4230, -882, 312),
        Appeal + FVector(3530, -345, 960), Appeal + FVector(3510, 330, 1020)};
    const FVector HallTargets[] = {
        Appeal + FVector(-3080, 0, -139), Appeal + FVector(-210, 120, 18),
        Appeal + FVector(990, 130, 74), Appeal + FVector(DuelCourt::VerdictDeskX, 0, DuelCourt::VerdictRise + 107.f), DuelCourt::AppealPassage,
        Appeal + FVector(2830, -404, 119), Appeal + FVector(2790, 368, 96)};
    const FLinearColor HallColors[] = {
        FLinearColor(.48f, .64f, 1.f), FLinearColor(1.f, .59f, .27f), FLinearColor(.57f, .73f, 1.f),
        FLinearColor(1.f, .65f, .34f), FLinearColor(1.f, .67f, .37f),
        FLinearColor(.30f, .68f, 1.f), FLinearColor(.98f, .40f, .23f)};
    for (int32 I = 0; I < UE_ARRAY_COUNT(HallLights); ++I)
    {
        USpotLightComponent* Lamp = NewObject<USpotLightComponent>(this);
        Lamp->SetupAttachment(SceneRoot);
        Lamp->SetRelativeLocationAndRotation(HallLights[I], (HallTargets[I] - HallLights[I]).Rotation());
        Lamp->SetMobility(EComponentMobility::Movable);
        const float Intensity[] = {42000.f, 122000.f, 84000.f, 114000.f, 42000.f, 56500.f, 63000.f};
        Lamp->SetIntensity(Intensity[I]);
        Lamp->SetLightColor(HallColors[I]);
        Lamp->SetInnerConeAngle(13.f);
        Lamp->SetOuterConeAngle(I >= 5 ? 22.f : I == 4 ? 29.f : 39.f);
        Lamp->SetAttenuationRadius(I == 4 ? 870.f : 1940.f);
        Lamp->SetVolumetricScatteringIntensity(I == 1 || I >= 5 ? 2.5f : 1.65f);
        Lamp->SetCastShadows(bCastleInterior);
        Lamp->SetCastVolumetricShadow(bCastleInterior);
        AddInstanceComponent(Lamp);
        Lamp->RegisterComponent();
    }

    const FVector Final = DuelCourt::YukiOrigin;
    struct FCourtLight
    {
        FVector At;
        FVector Target;
        FLinearColor Color;
        float Intensity;
        float Radius;
        float Cone;
        float Scattering;
    };
    const FCourtLight FinalLights[] = {
        {{-2810, -114, 392}, {-2650, 0, 2}, {.93f, .69f, .39f}, 21500.f, 850.f, 34.f, .90f},
        {{-2040, 110, 564}, {-2160, 0, 48}, {.56f, .71f, 1.f}, 54000.f, 1050.f, 29.f, 1.42f},
        {{1010, -1045, 1130}, {452, -310, 52}, {.61f, .72f, 1.f}, 103000.f, 1820.f, 30.f, .77f},
        {{1995, 316, 924}, {1190, 480, 150}, {.96f, .65f, .36f}, 67400.f, 1310.f, 34.f, 1.17f},
        {{2240, 0, 627}, {2405, 0, 22}, {.89f, .74f, .49f}, 24800.f, 920.f, 24.f, .79f}
    };
    for (const FCourtLight& Light : FinalLights)
    {
        USpotLightComponent* Lamp = NewObject<USpotLightComponent>(this);
        Lamp->SetupAttachment(SceneRoot);
        Lamp->SetRelativeLocationAndRotation(Final + Light.At, (Light.Target - Light.At).Rotation());
        Lamp->SetMobility(EComponentMobility::Movable);
        Lamp->SetLightColor(Light.Color);
        Lamp->SetIntensity(Light.Intensity);
        Lamp->SetAttenuationRadius(Light.Radius);
        Lamp->SetInnerConeAngle(Light.Cone * .53f);
        Lamp->SetOuterConeAngle(Light.Cone);
        Lamp->SetVolumetricScatteringIntensity(Light.Scattering);
        Lamp->SetCastShadows(bCastleInterior);
        Lamp->SetCastVolumetricShadow(bCastleInterior);
        AddInstanceComponent(Lamp);
        Lamp->RegisterComponent();
    }

    for (float X : {-1380.f, 1420.f})
    {
        for (float Y : {-1125.f, 1125.f})
        {
            USceneComponent* Post = DuelGeometry::Joint(this, SceneRoot, FVector(X, Y, 0));
            DuelGeometry::Shape(this, Post, EShape::Tube, FVector(0, 0, 10), FVector(42, 42, 20), Iron);
            DuelGeometry::Shape(this, Post, EShape::Tube, FVector(0, 0, 44), FVector(9, 9, 69), Iron);
            DuelGeometry::Shape(this, Post, EShape::Tube, FVector(0, 0, 77), FVector(46, 46, 13), Iron);
            for (int32 Bar = 0; Bar < 5; ++Bar)
            {
                const FVector Edge = FRotator(0, 72.f * Bar, 0).Vector() * 19.f;
                DuelGeometry::Rod(this, Post, Edge + FVector(0, 0, 78),
                    Edge * .53f + FVector(0, 0, 116), 1.7f, Iron);
            }
            DuelGeometry::Shape(this, Post, EShape::Cone, FVector(0, 0, 94),
                FVector(20, 17, 34), Coal, FRotator(-8, 0, 6))->SetCastShadow(false);
            UPointLightComponent* Ember = NewObject<UPointLightComponent>(this);
            Ember->SetupAttachment(Post);
            Ember->SetRelativeLocation(FVector(0, 0, 109));
            Ember->SetMobility(EComponentMobility::Movable);
            Ember->SetLightColor(FLinearColor(1.f, .44f, .15f));
            Ember->SetIntensity(6800.f);
            Ember->SetAttenuationRadius(660.f);
            Ember->SetCastShadows(false);
            AddInstanceComponent(Ember);
            Ember->RegisterComponent();
        }
    }

    AExponentialHeightFog* Mist = GetWorld()->SpawnActor<AExponentialHeightFog>(
        FVector(0, 0, 38), FRotator::ZeroRotator);
    UExponentialHeightFogComponent* Fog = Mist->GetComponent();
    CourtFog = Fog;
    Fog->SetFogDensity(bCastleInterior ? .028f : .061f);
    Fog->SetFogHeightFalloff(.39f);
    Fog->SetFogInscatteringColor(FLinearColor(.14f, .177f, .237f));
    Fog->SetFogMaxOpacity(.85f);
    Fog->SetVolumetricFog(true);
    Fog->SetVolumetricFogAlbedo(FColor(168, 183, 201));
    Fog->SetVolumetricFogDistance(7400.f);
    Fog->SetVolumetricFogExtinctionScale(1.65f);
    using namespace DuelCourt;
    const TArray<FVector> Banks = bCastleInterior ? TArray<FVector>{} : TArray<FVector>{
        FVector(-8080, 0, LowCourtZ), FVector(-4580, 0, 0), FVector::ZeroVector, AppealOrigin, YukiOrigin};
    for (const FVector& Region : Banks)
    {
        for (float Side : {-1.f, 1.f})
        {
            ULocalFogVolumeComponent* Bank = NewObject<ULocalFogVolumeComponent>(this);
            Bank->SetupAttachment(SceneRoot);
            Bank->SetRelativeLocation(Region + FVector(Side * 440, Side * 2470, 225));
            Bank->SetRelativeScale3D(FVector(3.65f));
            Bank->SetRadialFogExtinction(.23f);
            Bank->SetHeightFogExtinction(.48f);
            Bank->SetHeightFogFalloff(1820.f);
            Bank->SetHeightFogOffset(-.32f);
            Bank->SetFogAlbedo(FLinearColor(.46f, .55f, .68f));
            Bank->SetFogPhaseG(.28f);
            AddInstanceComponent(Bank);
            Bank->RegisterComponent();
        }
    }

    APostProcessVolume* Grade = GetWorld()->SpawnActor<APostProcessVolume>();
    Grade->bUnbound = true;
    auto& Settings = Grade->Settings;
    Settings.bOverride_ColorSaturation = true;
    Settings.ColorSaturation = FVector4(.84f, .91f, .95f, 1.f);
    Settings.bOverride_VignetteIntensity = true;
    Settings.VignetteIntensity = .33f;
    Settings.bOverride_BloomIntensity = true;
    Settings.BloomIntensity = .91f;
    Settings.bOverride_AutoExposureMinBrightness = true;
    Settings.bOverride_AutoExposureMaxBrightness = true;
    Settings.AutoExposureMinBrightness = 1.0f;
    Settings.AutoExposureMaxBrightness = 1.0f;
    if (bCastleInterior)
    {
        Settings.bOverride_AutoExposureBias = true;
        Settings.AutoExposureBias = 1.15f;
        Settings.bOverride_LumenSkylightLeaking = true;
        Settings.LumenSkylightLeaking = 0.f;
    }
}

void ADuelArena::SetBattlePhase(int32 Phase, bool bHiro)
{
    BattlePhase = Phase;
    if (Moon)
    {
        const FLinearColor Light[] = { FLinearColor(.77f, .85f, 1.f), FLinearColor(.83f, .73f, 1.f),
            FLinearColor(.58f, .49f, 1.f) };
        const FLinearColor ShadowLight[] = {Light[0], FLinearColor(1.f, .75f, .78f), FLinearColor(1.f, .49f, .63f)};
        Moon->SetLightColor(bHiro ? ShadowLight[BattlePhase - 1] : Light[BattlePhase - 1]);
        Moon->SetIntensity(bCastleInterior ? .46f : BattlePhase == 3 ? 4.65f : 3.97f);
    }
}

void ADuelArena::SetDawn(bool bDawn)
{
    if (Rain)
    {
        Rain->SetVisibility(!bDawn && !bCastleInterior);
    }
    SetRainMuted(bDawn, 1.8f);
    for (USpotLightComponent* Beam : GardenSunbeams)
    {
        Beam->SetVisibility(bDawn);
    }
    if (CourtFog)
    {
        CourtFog->SetFogDensity(bCastleInterior ? .028f : bDawn ? .038f : .061f);
        CourtFog->SetFogHeightFalloff(bDawn ? .21f : .39f);
        CourtFog->SetFogInscatteringColor(bDawn ? FLinearColor(.24f, .25f, .245f) : FLinearColor(.14f, .177f, .237f));
        CourtFog->SetDirectionalInscatteringColor(bDawn && !bCastleInterior ? FLinearColor(.51f, .33f, .17f) : FLinearColor::Black);
        CourtFog->SetDirectionalInscatteringStartDistance(bDawn ? 1850.f : 10000.f);
        CourtFog->SetDirectionalInscatteringExponent(8.f);
        CourtFog->SetVolumetricFogExtinctionScale(bDawn ? .73f : 1.65f);
    }
    if (bDawn && Moon && !bCastleInterior)
    {
        Moon->SetWorldRotation(FRotator(-18, -148, 0));
        Moon->SetLightColor(FLinearColor(1.f, .74f, .48f));
        Moon->SetIntensity(6.25f);
    }
    else if (Moon)
    {
        Moon->SetWorldRotation(FRotator(-32, 147, 0));
        SetBattlePhase(1);
    }
}

void ADuelArena::BuildRain()
{
    Rain = NewObject<UInstancedStaticMeshComponent>(this, TEXT("Drizzle"));
    Rain->SetupAttachment(SceneRoot);
    Rain->SetMobility(EComponentMobility::Movable);
    Rain->SetStaticMesh(DuelGeometry::Mesh(EShape::Box));
    Rain->SetMaterial(0, DuelGeometry::ColorMaterial(this, FLinearColor(.43f, .56f, .72f, .32f), true));
    Rain->SetCastShadow(false);
    Rain->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    AddInstanceComponent(Rain);
    Rain->RegisterComponent();
    for (int32 Index = 0; Index < 490; ++Index)
    {
        FVector Position(Random.FRandRange(-10200.f, 1850.f), Random.FRandRange(-1120.f, 1120.f),
            Random.FRandRange(12.f, 1150.f));
        RainPositions.Add(Position);
        RainTransforms.Emplace(FRotator(13, -7, 0), Position, FVector(.004, .004, .192));
        Rain->AddInstance(RainTransforms.Last());
    }
}

void ADuelArena::TickRain(float DeltaSeconds)
{
    if (bCastleInterior)
    {
        return;
    }
    const bool bEclipse = BattlePhase == 3;
    const FVector Wind = bEclipse ? FVector(339, -62, 1280) : FVector(155, -24, 950);
    const FRotator Slant(bEclipse ? 19.f : 13.f, -7.f, 0);
    const FVector Size(.004, .004, bEclipse ? .264 : .192);
    for (int32 Index = 0; Index < RainPositions.Num(); ++Index)
    {
        FVector& At = RainPositions[Index];
        At -= Wind * DeltaSeconds;
        const float RainFloor = At.X < DuelCourt::StairBaseX ? DuelCourt::LowCourtZ :
            FMath::GetMappedRangeValueClamped(FVector2D(DuelCourt::StairBaseX, DuelCourt::StairTopX),
                FVector2D(DuelCourt::LowCourtZ, 0), At.X);
        if (At.Z < RainFloor + 8.f)
        {
            At = FVector(Random.FRandRange(-10200, 1830), Random.FRandRange(-1140, 1140), 1140.f);
        }
        RainTransforms[Index] = FTransform(Slant, At, Size);
    }
    Rain->BatchUpdateInstancesTransforms(0, RainTransforms, false, true, true);
}
