#include "DuelNorthGothic.h"
#include "DuelGeometry.h"
#include "DuelNorthLayout.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/DecalComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include <initializer_list>

#if WITH_DEV_AUTOMATION_TESTS
#include "DuelNorthTower.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#endif

namespace DuelNorthGothic
{
namespace
{
    using namespace DuelNorthLayout;
    struct FMeshRecipe
    {
        const TCHAR* Name;
        const TCHAR* Series;
        FBox LocalBounds;
        int32 TriangleCount;
        bool bWall;
    };
    const FMeshRecipe Recipes[] = {
#include "DuelNorthGothicMeshes.inl"
    };
    // The expanded custody wing uses 4,862 instances in the same 122 mesh batches.
    constexpr int32 MaxStoneInstances=5000;

    void WeatheredMasonry(AActor* Owner, USceneComponent* Parent)
    {
        auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/DuelArt/Materials/M_NorthDampStone.M_NorthDampStone"));
        if (!Material) return;
        struct FSeep { FVector Foot; float InwardY; float Width; float Reach; float Height; };
        // Drain mouths, wash bench, exterior window sills and the exposed parapet.
        const FSeep Seepage[] = {
            {{-1640,-1700,-1700},1,105,62,65}, {{1435,1700,-1700},-1,79,39,45},
            {{1640,-1500,0},1,112,69,38}, {{342,1700,700},-1,63,32,49},
            {{-1140,1700,3400},-1,81,34,54}, {{7200,-1200,3400},1,93,46,39}
        };
        int32 Seed = 0;
        for (const auto& Site : Seepage)
        {
            for (bool bWall : {true,false})
            {
                auto* Mark = NewObject<UDecalComponent>(Owner);
                Mark->SetupAttachment(Parent);
                Mark->SetMobility(EComponentMobility::Static);
                Mark->DecalSize = FVector(bWall ? 27.f : 7.f,Site.Width,bWall ? Site.Height : Site.Reach);
                const FVector Offset = bWall ? FVector(0,0,Site.Height*.77) :
                    FVector(0,Site.InwardY*Site.Reach*.81,1.5);
                Mark->SetRelativeLocationAndRotation(Site.Foot+Offset, bWall ?
                    FRotator(0,Site.InwardY>0?-90:90,0) : FRotator(-90,84+Seed%13,0));
                auto* Ink = UMaterialInstanceDynamic::Create(Material,Mark);
                Ink->SetScalarParameterValue(TEXT("Wall"),bWall?1.f:0.f);
                Ink->SetScalarParameterValue(TEXT("Seed"),float(++Seed));
                Mark->SetDecalMaterial(Ink);
                Mark->SetFadeScreenSize(.012f);
                Mark->ComponentTags.Add(TEXT("NorthSeepage"));
                Owner->AddInstanceComponent(Mark);
                Mark->RegisterComponent();
            }
        }
    }

    const FMeshRecipe* Recipe(FName Name)
    {
        return MakeArrayView(Recipes).FindByPredicate([&](const FMeshRecipe& R){ return Name==R.Name; });
    }

    bool MeetsBaseOpening(const FBox& Box)
    {
        for (const FPortal& Portal : Portals())
        {
            const double HalfWidth = Portal.Width * .5 + 4.;
            const FVector Size = FMath::Abs(Portal.Normal.X) > .5
                ? FVector(60., HalfWidth, Portal.Height * .5)
                : FVector(HalfWidth, 60., Portal.Height * .5);
            const FVector Centre = Portal.Center + FVector(0, 0, Portal.Height * .5);
            if (Box.Intersect(FBox(Centre - Size, Centre + Size))) return true;
        }
        return false;
    }

    void BuildWallBases(AActor* Owner, USceneComponent* Parent, UMaterialInterface* Stone)
    {
        auto* Mesh = DuelGeometry::Mesh(DuelGeometry::EShape::Box);
        if (!Mesh || !Stone) return;
        TArray<FTransform> Courses, Corners;
        const auto Piece = [](TArray<FTransform>& Batch, const FVector& At, const FVector& Size)
        {
            Batch.Emplace(FQuat::Identity, At, Size / 100.);
        };
        for (const FRoom& Room : Rooms())
        {
            if (!Room.bEnclose || !Room.bFloor ||
                (Room.Floor != EFloor::Prison && Room.Floor != EFloor::Chapel && Room.Floor != EFloor::Dungeon)) continue;
            const bool bHall = Room.Kind == ERoomKind::Hall;
            const double Z = Room.Bounds.Min.Z;
            const double Toe = Room.Floor == EFloor::Dungeon ? 26. : bHall ? 43. : 31.;
            const double Rise = Room.Floor == EFloor::Dungeon ? 29. : bHall ? 47. : 36.;
            constexpr double Back = WallThickness * .5 - 3.;
            for (int32 Axis = 0; Axis < 2; ++Axis)
            {
                const int32 Along = 1 - Axis;
                const double From = Room.Bounds.Min[Along] + 12.;
                const double To = Room.Bounds.Max[Along] - 12.;
                for (double In : {-1., 1.})
                {
                    const double Plane = In > 0. ? Room.Bounds.Min[Axis] : Room.Bounds.Max[Axis];
                    TArray<FVector2D> Openings;
                    TArray<double> Stops{From, To};
                    for (const FPortal& P : Portals())
                    {
                        const bool bOnThisWall = FMath::Abs(P.Normal[Axis]) > .9;
                        const double HalfAcross = bOnThisWall ? 60. : P.Width * .5 + 4.;
                        const double Across = Plane + In * (Back + Toe) * .5;
                        if (FMath::Abs(P.Center[Axis] - Across) >= HalfAcross + (Toe - Back) * .5 ||
                            P.Center.Z >= Z + Rise || P.Center.Z + P.Height <= Z) continue;
                        const double Gap = bOnThisWall ? P.Width * .5 + 12. : 62.;
                        const double Left = FMath::Max(From, P.Center[Along] - Gap);
                        const double Right = FMath::Min(To, P.Center[Along] + Gap);
                        if (Right <= Left) continue;
                        Openings.Emplace(Left, Right);
                        Stops.AddUnique(Left);
                        Stops.AddUnique(Right);
                    }
                    const auto Course = [&](double A, double B, double Bottom, double Top, double Front)
                    {
                        FVector At(0, 0, Z + (Bottom + Top) * .5);
                        At[Axis] = Plane + In * (Back + Front) * .5;
                        At[Along] = (A + B) * .5;
                        FVector Size(0, 0, Top - Bottom);
                        Size[Axis] = Front - Back;
                        Size[Along] = B - A;
                        Piece(Courses, At, Size);
                    };
                    Stops.Sort();
                    for (int32 I = 1; I < Stops.Num(); ++I)
                    {
                        const double A = Stops[I - 1], B = Stops[I], Centre = (A + B) * .5;
                        if (B - A < 8. || Openings.ContainsByPredicate([&](const FVector2D& Hole)
                            { return Centre > Hole.X && Centre < Hole.Y; })) continue;
                        Course(A, B, -.7, 10., Toe);
                        Course(A, B, Rise - 4., Rise, Toe - 4.);
                        const int32 Blocks = FMath::CeilToInt((B - A) / 184.);
                        for (int32 Block = 0; Block < Blocks; ++Block)
                        {
                            const double L = FMath::Lerp(A, B, double(Block) / Blocks);
                            const double R = FMath::Lerp(A, B, double(Block + 1) / Blocks);
                            Course(L + .6, R - .6, 9.7, Rise - 3.8, Toe - 8. - double(Block % 2));
                        }
                    }
                }
            }
            for (double X : {-1., 1.}) for (double Y : {-1., 1.})
            {
                const FVector At(X > 0. ? Room.Bounds.Min.X + 21. : Room.Bounds.Max.X - 21.,
                    Y > 0. ? Room.Bounds.Min.Y + 21. : Room.Bounds.Max.Y - 21., Z);
                const double Width = bHall ? 111. : 71.;
                const FVector Extent(Width * .5, Width * .5, (Rise + 11.) * .5);
                if (MeetsBaseOpening(FBox(At + FVector(0, 0, Extent.Z) - Extent,
                    At + FVector(0, 0, Extent.Z) + Extent))) continue;
                Piece(Corners, At + FVector(0, 0, 5.5), FVector(Width, Width, 12.));
                Piece(Corners, At + FVector(0, 0, (Rise + 11.) * .5), FVector(Width - 13., Width - 13., Rise - 10.));
                Piece(Corners, At + FVector(0, 0, Rise + 3.2), FVector(Width - 5., Width - 5., 9.));
            }
        }
        const auto Install = [&](const TArray<FTransform>& Transforms, const TCHAR* Tag)
        {
            if (Transforms.IsEmpty()) return;
            auto* Batch = NewObject<UHierarchicalInstancedStaticMeshComponent>(Owner);
            Batch->bAutoRebuildTreeOnInstanceChanges = false;
            Batch->SetupAttachment(Parent);
            Batch->SetMobility(EComponentMobility::Static);
            Batch->SetStaticMesh(Mesh);
            Batch->SetMaterial(0, Stone);
            Batch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Batch->SetCanEverAffectNavigation(false);
            Batch->SetCastShadow(false);
            Batch->SetAffectDistanceFieldLighting(false);
            Batch->SetAffectDynamicIndirectLighting(false);
            Batch->SetVisibleInRayTracing(false);
            Batch->SetCullDistances(0, 5300);
            Batch->ComponentTags.Add(Tag);
            Owner->AddInstanceComponent(Batch);
            Batch->AddInstances(Transforms, false, false, false);
            Batch->BuildTreeIfOutdated(false, false);
            Batch->RegisterComponent();
        };
        Install(Courses, TEXT("NorthMasonryWallBases"));
        Install(Corners, TEXT("NorthMasonryCornerBases"));
        UE_LOG(LogTemp, Display, TEXT("North wall finish: %d wall-foot courses and %d interlocking corner-base pieces"),
            Courses.Num(), Corners.Num());
    }

    void BuildDungeonFinish(AActor* Owner, USceneComponent* Parent)
    {
        TMap<FString, UInstancedStaticMeshComponent*> Batches;
        const auto Batch = [&](const TCHAR* Folder, const TCHAR* MeshName)
        {
            const FString Name(MeshName);
            auto*& Existing = Batches.FindOrAdd(Name);
            if (Existing) return Existing;
            const FString Path = FString::Printf(TEXT("/Game/MedievalDungeon/Meshes/%s/%s.%s"), Folder, MeshName, MeshName);
            auto* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
            if (!Mesh) return Existing;
            Existing = NewObject<UInstancedStaticMeshComponent>(Owner);
            Existing->SetupAttachment(Parent);
            Existing->SetMobility(EComponentMobility::Static);
            Existing->SetStaticMesh(Mesh);
            Existing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Existing->SetCanEverAffectNavigation(false);
            Existing->ComponentTags.Add(TEXT("NorthMedievalDungeonArchitecture"));
            Owner->AddInstanceComponent(Existing);
            Existing->RegisterComponent();
            return Existing;
        };
        const auto ClearsOpenings = [](const FBox& Bounds)
        {
            for (const FPortal& Portal : Portals())
            {
                const FVector Size = FMath::Abs(Portal.Normal.X) > .5
                    ? FVector(76., Portal.Width * .5 + 18., Portal.Height * .5 + 14.)
                    : FVector(Portal.Width * .5 + 18., 76., Portal.Height * .5 + 14.);
                const FVector Centre = Portal.Center + FVector(0., 0., Portal.Height * .5);
                if (Bounds.Intersect(FBox(Centre - Size, Centre + Size))) return false;
            }
            return true;
        };

        constexpr TCHAR Crypt[] = TEXT("Architecture/Crypt");
        constexpr TCHAR Dungeon[] = TEXT("Architecture/Dungeon");
        constexpr TCHAR Props[] = TEXT("Props");
        struct FBay { const TCHAR* Room; double X; int32 Side; const TCHAR* Mesh; };
        const FBay Bays[] = {
            {TEXT("DungeonCourt"), -2080, 1, TEXT("SM_Crypt_Wall_Decorative_A")},
            {TEXT("DungeonCourt"), -830, 1, TEXT("SM_Crypt_Wall_Decorative_B")},
            {TEXT("DungeonCourt"), 810, 1, TEXT("SM_Crypt_Wall_Decorative_A")},
            {TEXT("DungeonCourt"), 2095, 1, TEXT("SM_Crypt_Wall_Decorative_C")},
            {TEXT("DungeonCourt"), -2095, -1, TEXT("SM_Crypt_Wall_Decorative_C")},
            {TEXT("DungeonCourt"), -875, -1, TEXT("SM_Crypt_Wall_Decorative_A")},
            {TEXT("DungeonCourt"), 830, -1, TEXT("SM_Crypt_Wall_Decorative_B")},
            {TEXT("DungeonCourt"), 2100, -1, TEXT("SM_Crypt_Wall_Decorative_A")},
            {TEXT("PrisonHall"), -1850, 1, TEXT("SM_Dungeon_Wall_Decorative_A")},
            {TEXT("PrisonHall"), 1890, 1, TEXT("SM_Dungeon_Wall_Decorative_A")},
            {TEXT("ChapelHall"), -1220, 1, TEXT("SM_Crypt_Wall_Decorative_B")},
            {TEXT("ChapelHall"), 1190, 1, TEXT("SM_Crypt_Wall_Decorative_C")},
            {TEXT("ChapelHall"), -2100, -1, TEXT("SM_Crypt_Wall_Decorative_A")},
            {TEXT("ChapelHall"), 2100, -1, TEXT("SM_Crypt_Wall_Decorative_B")},
            {TEXT("MirrorCourt"), -880, 1, TEXT("SM_Crypt_Wall_Decorative_C")},
            {TEXT("MirrorCourt"), 865, 1, TEXT("SM_Crypt_Wall_Decorative_B")},
            {TEXT("MirrorCourt"), -830, -1, TEXT("SM_Crypt_Wall_Decorative_B")},
            {TEXT("MirrorCourt"), 810, -1, TEXT("SM_Crypt_Wall_Decorative_C")},
            {TEXT("FinalCourt"), -1150, 1, TEXT("SM_Crypt_Wall_Decorative_C")},
            {TEXT("FinalCourt"), 1150, 1, TEXT("SM_Crypt_Wall_Decorative_C")},
            {TEXT("FinalCourt"), -1110, -1, TEXT("SM_Crypt_Wall_Decorative_B")},
            {TEXT("FinalCourt"), 1110, -1, TEXT("SM_Crypt_Wall_Decorative_B")},
        };
        for (const FBay& Bay : Bays)
        {
            const FRoom* Room = FindRoom(Bay.Room);
            const bool bPrisonBay = Room->Floor == EFloor::Prison;
            auto* Panel = Batch(bPrisonBay ? Dungeon : Crypt, Bay.Mesh);
            if (!Panel) continue;
            const FBox Local = Panel->GetStaticMesh()->GetBoundingBox();
            const FQuat Facing = FRotator(0., (Bay.Side > 0 ? 0. : 180.) - (bPrisonBay ? 90. : 0.), 0.).Quaternion();
            const FVector Scale = bPrisonBay ? FVector(.48, .47, .78) : FVector(.78, .53, 1.02);
            const double Wall = Bay.Side > 0 ? Room->Bounds.Max.Y : Room->Bounds.Min.Y;
            const FVector Pivot = bPrisonBay ? FVector(0., Local.GetCenter().Y, Local.Min.Z)
                                            : FVector(Local.GetCenter().X, 0., Local.Min.Z);
            const FVector Floor(Bay.X, Wall - Bay.Side * 14., Room->Bounds.Min.Z + .6);
            const FTransform Transform(Facing, Floor - Facing.RotateVector(Pivot * Scale), Scale);
            if (ClearsOpenings(Local.TransformBy(Transform))) Panel->AddInstance(Transform);
        }

        const TCHAR* VaultedRooms[] = {TEXT("DungeonCourt"), TEXT("PrisonHall"), TEXT("ChapelHall"),
            TEXT("MirrorCourt"), TEXT("FinalCourt"), TEXT("DungeonAnte"), TEXT("MirrorAnte"), TEXT("FinalAnte")};
        for (const TCHAR* Name : VaultedRooms)
        {
            const FRoom& Room = *FindRoom(Name);
            const bool bEntry = Room.Kind == ERoomKind::Antechamber;
            const bool bPrison = Room.Floor == EFloor::Prison;
            auto* Paving = Batch(bPrison ? Dungeon : Crypt, bPrison ? TEXT("SM_Floor") : TEXT("SM_Crypt_Floor"));
            if (Paving && (bEntry || bPrison || Room.Floor == EFloor::Chapel || Room.Floor == EFloor::Final))
            {
                const FBox Source = Paving->GetStaticMesh()->GetBoundingBox();
                const FVector Span = Room.Bounds.GetSize();
                const int32 Across = FMath::Max(1, FMath::RoundToInt(Span.X / Source.GetSize().X));
                const int32 Along = FMath::Max(1, FMath::RoundToInt(Span.Y / Source.GetSize().Y));
                const double Width = Span.X / Across, Depth = Span.Y / Along;
                const FVector Scale(Width / Source.GetSize().X, Depth / Source.GetSize().Y, 1.);
                for (int32 X = 0; X < Across; ++X)
                    for (int32 Y = 0; Y < Along; ++Y)
                    {
                        const FVector Origin = Room.Bounds.Min + FVector(X * Width, Y * Depth, .65);
                        Paving->AddInstance(FTransform(FQuat::Identity, Origin - Source.Min * Scale, Scale));
                    }
            }
            if (bEntry)
            {
                auto* Crown = Batch(Crypt, TEXT("SM_Crypt_Ceiling_Arch_A"));
                auto* Pier = Batch(Crypt, TEXT("SM_Crypt_Pillar"));
                const FVector Centre = Room.Bounds.GetCenter();
                const double Z = Room.Bounds.Min.Z;
                if (Crown && Pier)
                {
                    Crown->AddInstance(FTransform(FQuat::Identity, FVector(Centre.X, Centre.Y, Z + 432.),
                        FVector(1., Room.Bounds.GetSize().Y / 488., 1.)));
                    for (const int32 Side : {-1, 1})
                        Pier->AddInstance(FTransform(FQuat::Identity, FVector(Centre.X, Centre.Y + Side * 464., Z),
                            FVector(.68, .56, 1.16)));
                }
                continue;
            }
            auto* Trim = bPrison || Room.Floor == EFloor::Chapel ? nullptr : Batch(Crypt, TEXT("SM_Crypt_Wall_Floor_Trim"));
            auto* Pier = Batch(Dungeon, TEXT("SM_Pillar"));
            for (const int32 Side : {-1, 1})
            {
                const double Wall = Side > 0 ? Room.Bounds.Max.Y : Room.Bounds.Min.Y;
                const double Y = Wall - Side * 19.;
                const FQuat Facing = FRotator(0., Side > 0 ? 0. : 180., 0.).Quaternion();
                if (Trim)
                    for (double Left = Room.Bounds.Min.X + 16.; Left + 400. < Room.Bounds.Max.X - 12.; Left += 400.)
                    {
                        const FTransform T(Facing, FVector(Side > 0 ? Left + 400. : Left, Y, Room.Bounds.Min.Z),
                            FVector(.8, .8, .67));
                        if (ClearsOpenings(Trim->GetStaticMesh()->GetBoundingBox().TransformBy(T))) Trim->AddInstance(T);
                    }
                if (Pier && (bPrison || Room.Floor == EFloor::Dungeon || Room.Floor == EFloor::Chapel))
                    for (const double X : {-1920., -435., 435., 1910.})
                    {
                        const double Height = bPrison ? 530. : Room.Floor == EFloor::Chapel ? 455. : 560.;
                        const FTransform T(Facing, FVector(X, Y, Room.Bounds.Min.Z), FVector(.72, .62, Height / 450.));
                        if (ClearsOpenings(Pier->GetStaticMesh()->GetBoundingBox().TransformBy(T))) Pier->AddInstance(T);
                    }
            }
            if (!bPrison)
            {
                const double CrownHeight = Room.Floor == EFloor::Chapel ? 930. : Room.Floor == EFloor::Final ? 1690. : 1400.;
                if (auto* Chain = Batch(Props, TEXT("SM_Chandelier")))
                    for (const double X : {-900., 880.})
                        Chain->AddInstance(FTransform(FQuat::Identity, FVector(X, 0., Room.Bounds.Min.Z + CrownHeight - 5.),
                            FVector(1.55)));
            }
        }
        if (auto* Guardian = Batch(Props, TEXT("SM_Gargoyle_Statue_On_Stand")))
            for (const double Y : {-1565., 1565.})
                Guardian->AddInstance(FTransform(FRotator(0., 180., 0.), FVector(2200., Y, -1700.), FVector(.95)));

        int32 CornerCourses = 0;
        auto* EngagedPier = Batch(Crypt, TEXT("SM_Crypt_Pillar"));
        if (auto* Quoins = Batch(Dungeon, TEXT("SM_Edge_Stones")))
        {
            const FBox Stone = Quoins->GetStaticMesh()->GetBoundingBox();
            for (const FRoom& Room : Rooms())
            {
                if (!Room.bEnclose || !Room.bFloor || Room.Kind == ERoomKind::Landing) continue;
                const double Z = Room.Bounds.Min.Z;
                const double Height = Room.Bounds.GetSize().Z - 18.;
                for (const int32 XSide : {-1, 1})
                    for (const int32 YSide : {-1, 1})
                    {
                        const double X = XSide < 0 ? Room.Bounds.Min.X : Room.Bounds.Max.X;
                        const double Y = YSide < 0 ? Room.Bounds.Min.Y : Room.Bounds.Max.Y;
                        const bool bFormalRoom = Room.Kind == ERoomKind::Hall || Room.Kind == ERoomKind::Antechamber;
                        if (EngagedPier && bFormalRoom && Room.Floor != EFloor::Dungeon)
                        {
                            // The carved foot, shaft and capital meet both wall faces on the diagonal.
                            const FBox Local = EngagedPier->GetStaticMesh()->GetBoundingBox();
                            const double Width = Room.Kind == ERoomKind::Hall ? 86. : 57.;
                            const FVector Scale = FVector(Width, Width, Height) / Local.GetSize();
                            const FQuat Yaw = FVector(-XSide, -YSide, 0.).Rotation().Quaternion();
                            const FVector Foot(X-XSide*14., Y-YSide*14., Z);
                            const FVector Pivot(Local.GetCenter().X,Local.GetCenter().Y,Local.Min.Z);
                            const FTransform PierTransform(Yaw,Foot-Yaw.RotateVector(Pivot*Scale),Scale);
                            if (ClearsOpenings(Local.TransformBy(PierTransform)))
                            {
                                EngagedPier->AddInstance(PierTransform);
                                continue;
                            }
                        }
                        const double Pitch = 118. + 7.*(XSide+1) + 5.*(YSide+1);
                        const int32 CourseCount = FMath::CeilToInt(Height / Pitch);
                        for (int32 Course = 0; Course < CourseCount; ++Course)
                        {
                            const double Base = Course * Pitch;
                            const double Reach = FMath::Min(Pitch - 2., Height - Base);
                            if (Reach < 12.) continue;
                            const bool bTurn = (Course + (XSide == YSide)) % 2 == 0;
                            const double Bond = ((Course + XSide * YSide + 6) % 3 - 1) * 3.;
                            const FQuat Facing = FRotator(90., (bTurn ? 0. : 90.) + Bond, 0.).Quaternion();
                            const FVector At(X - XSide * (bTurn ? 16. : 23.),
                                Y - YSide * (bTurn ? 23. : 16.), Z + Base + Reach * .5);
                            const FVector Scale = FVector(Reach, bTurn ? 55. : 62., 46.) / Stone.GetSize();
                            const FTransform T(Facing, At - Facing.RotateVector(Stone.GetCenter() * Scale), Scale);
                            if (!ClearsOpenings(Stone.TransformBy(T))) continue;
                            Quoins->AddInstance(T);
                            ++CornerCourses;
                        }
                    }
            }
        }

        int32 Count = 0;
        for (const auto& Entry : Batches) if (Entry.Value) Count += Entry.Value->GetInstanceCount();
        UE_LOG(LogTemp, Display, TEXT("North Medieval Dungeon: %d architectural pieces in %d native mesh batches, %d interlocking corner courses"),
            Count, Batches.Num(), CornerCourses);
    }

    struct FPlacement
    {
        TArray<FPiece> Data;
        void Add(const FString& Mesh,FName Room,const FVector& At,float Yaw=0,const FVector& Scale=FVector::OneVector,bool bSolid=false)
        {
            Data.Add({FName(*Mesh),Room,FTransform(FRotator(0,Yaw,0),At,Scale),bSolid});
        }
        void Family(const FString& Name,FName Room,const FVector& At,float Yaw,const FVector& Scale)
        {
            for(const auto& R:Recipes) if(Name==R.Series) Add(R.Name,Room,At,Yaw,Scale);
        }
        void Arch(const FString& Name,FName Room,const FVector& At,float Yaw=0,const FVector& Scale=FVector::OneVector)
        {
            // The source half is at negative local X. A half-turn supplies the
            // other half without a negative scale or reversed normal winding.
            Family(Name,Room,At,Yaw,Scale);Family(Name,Room,At,Yaw+180,Scale);
        }
        void Shaft(FName Room,const FVector& At,float Height,float Yaw,bool bCluster)
        {
            if(Height<150)return;
            Add(TEXT("SM_NG_Base"),Room,At,Yaw,FVector::OneVector,true);
            const float Stem=Height-125;
            const int32 Segments=FMath::Max(1,FMath::CeilToInt(Stem/300));
            for(int32 I=0;I<Segments;++I)
                Add(bCluster?TEXT("SM_NG_Cluster"):TEXT("SM_NG_Pier"),Room,
                    At+FVector(0,0,55+Stem*I/Segments),Yaw,FVector(1,1,Stem/Segments/300),true);
            Add(TEXT("SM_NG_Capital"),Room,At+FVector(0,0,Height-70),Yaw,FVector::OneVector,true);
        }
        void Vault(const TCHAR* RoomName,const TCHAR* FamilyName,float Spring,std::initializer_list<float> Bays)
        {
            const FRoom* R=FindRoom(RoomName);if(!R)return;
            const float Z=R->Bounds.Min.Z;
            const FString Web = FString(TEXT("Web")) + FamilyName;
            const FString Rib = FString(TEXT("Rib")) + FamilyName;
            double Crown = 0.;
            for (const auto& Mesh : Recipes)
                if (Web == Mesh.Series || Rib == Mesh.Series) Crown = FMath::Max(Crown, Mesh.LocalBounds.Max.Z);
            // Keep the wall spring above the windows and bury the back of the vault
            // inside the roof slab, below the next storey's walkable floor.
            const double Limit = R->Bounds.GetSize().Z + RoofThickness - 2. - Spring;
            const double RiseScale = FMath::Min(1., Limit / Crown);
            const FVector VaultScale(1., 1., RiseScale);
            for(float X=R->Bounds.Min.X+150;X<R->Bounds.Max.X;X+=300)
                Arch(Web,R->Id,FVector(X,0,Z+Spring),90,VaultScale);
            for(float X:Bays)
            {
                Arch(Rib,R->Id,FVector(X,0,Z+Spring),90,VaultScale);
                for(int Side:{-1,1})
                    Shaft(R->Id,FVector(X,Side*1600,Z),Spring,90,true);
                Add(TEXT("SM_NG_Key"),R->Id,FVector(X,0,Z+Spring+
                    (FString(FamilyName)==TEXT("Chapel")?520:(FString(FamilyName)==TEXT("Final")?1050:950))*RiseScale-75),
                    90, FVector(1., 1., RiseScale));
            }
        }
        void Doorway(const FPortal& P)
        {
            const bool Gate=P.Gate==EGate::DungeonFog || P.Gate==EGate::MirrorFog || P.Gate==EGate::FinalFog;
            const bool bStandard=FMath::IsNearlyEqual(P.Width,500.f) && P.Height>=360 && P.Height<=400;
            const FRoom* A = FindRoom(P.RoomA);
            const FRoom* B = FindRoom(P.RoomB);
            const bool bMasonryOpening = A && B && A->bEnclose && B->bEnclose &&
                A->Kind != ERoomKind::Landing && B->Kind != ERoomKind::Landing &&
                A->Kind != ERoomKind::Stairwell && B->Kind != ERoomKind::Stairwell &&
                FMath::IsNearlyEqual(A->Bounds.Min.Z, B->Bounds.Min.Z, 1.);
            const bool bFitted=!Gate && !bStandard && (P.Gate!=EGate::Open || bMasonryOpening);
            if(P.bWindow || (!Gate && !bFitted && !bStandard))return;
            const float Yaw=P.Normal.Rotation().Yaw+90;
            const FVector Along=FRotator(0,Yaw,0).RotateVector(FVector::ForwardVector);
            const float Spring=bFitted?P.Height*.52f:(Gate?P.Height-460:P.Height-280);
            const FVector Scale=bFitted?FVector(P.Width/500.f,1,(P.Height-Spring)/280.f):FVector::OneVector;
            const int Layers=Gate?(P.Gate==EGate::FinalFog?3:2):2;
            const FString Prefix=Gate?TEXT("GateL"):TEXT("PrisonL");
            const FVector Crown=P.Center+FVector(0,0,Spring);
            for(int Layer=0;Layer<Layers;++Layer)
            {
                Arch(Prefix+FString::FromInt(Layer),P.RoomA,Crown,Yaw,Scale);
                const float Side=P.Width*.5f+(13+26*Layer)*Scale.X;
                for(int Sign:{-1,1})
                    Add(TEXT("SM_NG_Jamb"),P.RoomA,P.Center+Along*(Side*Sign),Yaw,
                        FVector(Scale.X,(40.f+20.f*Layer)/80.f,Spring/200.f),!bFitted);
            }
            const FString Fill=Gate?(Layers==3?TEXT("GateFill3"):TEXT("GateFill2")):TEXT("PrisonFill2");
            Arch(Fill,P.RoomA,Crown,Yaw,Scale);
        }

        void Antechamber(const TCHAR* RoomName)
        {
            const auto* Ante=FindRoom(RoomName);
            if(!Ante)return;
            // Vault strips span the existing chamber and leave space at both lintels.
            const FVector Centre=Ante->Bounds.GetCenter();
            const FVector Span=Ante->Bounds.GetSize();
            const double Bay=(Span.X-40.)/3.;
            const double Spring=Ante->Bounds.Max.Z-311.;
            const FVector Scale(Span.Y/3364.,Bay/300.,.52);
            for(double Offset:{-Bay,0.,Bay})
                Arch(TEXT("WebChapel"),Ante->Id,FVector(Centre.X+Offset,Centre.Y,Spring),90,Scale);
            for(double Offset:{-Bay*.5,Bay*.5})
                Arch(TEXT("RibChapel"),Ante->Id,FVector(Centre.X+Offset,Centre.Y,Spring),90,Scale);
        }

        void DungeonAndPrisonDetails()
        {
            if (const auto* Hall=FindRoom(TEXT("PrisonHall")))
            {
                // The rib spring sits below the ceiling. Bays and corbels clear
                // both south-facing exterior light slots and the north doorway.
                const double Z=Hall->Bounds.Min.Z;
                for(double X:{-1800.,-1090.,-700.,700.,1090.,1800.})
                {
                    Arch(TEXT("RibChapel"),Hall->Id,FVector(X,0,Z+500),90,FVector(.92,1,.26));
                    for(double Side:{-1.,1.})
                        Add(TEXT("SM_NG_Cornice"),Hall->Id,FVector(X,Side*1480.,Z+472),0,FVector(.28,.82,.74));
                }
            }
        }
        FPlacement()
        {
            for(const FPortal& P:Portals())Doorway(P);
            Vault(TEXT("DungeonCourt"),TEXT("Court"),450,{-1600,0,1600});
            Vault(TEXT("ChapelHall"),TEXT("Chapel"),425,{-1800,-600,600,1800});
            Vault(TEXT("MirrorCourt"),TEXT("Court"),450,{-1600,0,1600});
            Vault(TEXT("FinalCourt"),TEXT("Final"),640,{-1800,-600,600,1800});
            for(const TCHAR* RoomName:{TEXT("DungeonAnte"),TEXT("MirrorAnte"),TEXT("FinalAnte")})
                Antechamber(RoomName);
            DungeonAndPrisonDetails();

            // Shallow blind arcades break up the starting hall without narrowing
            // its central route or disguising them as accessible doorways.
            if(const auto* R=FindRoom(TEXT("PrisonHall")))
                for(float X:{-1350.f,-450.f,450.f,1350.f})for(int Side:{-1,1})
                {
                    const float Y=Side*1475.f;
                    Arch(TEXT("Blind"),R->Id,FVector(X,Y,250));
                    for(int J:{-1,1})
                        Add(TEXT("SM_NG_Jamb"),R->Id,FVector(X+J*161,Y,0),0,FVector(.77f,.425f,1.25f),true);
                }
            // Copings have a four-centimetre profile below the existing surface;
            // they add visual edges only, never a second physics riser.
            for(const FStairFlight& S:Stairs())
            {
                const FVector Step=(S.End-S.Start)/S.StepCount;
                for(int I=0;I<S.StepCount;++I)
                    Add(TEXT("SM_NG_Nosing"),S.CoreRoom,S.Start+Step*I+FVector(0,0,Step.Z),90);
            }
        }
    };
}

const TArray<FPiece>& Pieces()
{
    static const FPlacement Placement;
    return Placement.Data;
}

bool Validate(TArray<FString>& Errors)
{
    for(const FPiece& P:Pieces())
    {
        const FMeshRecipe* R=Recipe(P.Mesh);
        if(!R || !FindRoom(P.Room) || P.Transform.ContainsNaN())
        { Errors.Add(FString::Printf(TEXT("Invalid gothic placement %s"),*P.Mesh.ToString()));continue; }
        const FVector Scale=P.Transform.GetScale3D();
        if(Scale.GetMin()<=0 || Scale.GetMax()>2)
            Errors.Add(FString::Printf(TEXT("Excessive or mirrored gothic scale %s"),*P.Mesh.ToString()));
        const auto* Room=FindRoom(P.Room);
        const FBox Bounds=R->LocalBounds.TransformBy(P.Transform);
        if (R->bWall && FStringView(R->Series).StartsWith(TEXT("Web")) && Room->bRoof &&
            Bounds.Max.Z > Room->Bounds.Max.Z + RoofThickness + .1)
            Errors.Add(FString::Printf(TEXT("Vault penetrates the roof slab in %s: %s"), *P.Room.ToString(), *P.Mesh.ToString()));
        if(Room->Kind==ERoomKind::Combat && P.bSolid &&
            Bounds.Min.Y<1450 && Bounds.Max.Y>-1450 && Bounds.Min.X<2200 && Bounds.Max.X>-2200)
            Errors.Add(FString::Printf(TEXT("Gothic solid enters active battle floor: %s %s"),*P.Room.ToString(),*P.Mesh.ToString()));
    }
    if(Pieces().Num()>MaxStoneInstances)Errors.Add(TEXT("Gothic instance budget exceeded"));
    return Errors.IsEmpty();
}

void Build(AActor* Owner,USceneComponent* Parent,UMaterialInterface* WallStone)
{
    if(!Owner || !Parent)return;
    TMap<FName,TArray<const FPiece*>> Groups[2];
    for(const FPiece& P:Pieces())
        Groups[P.bSolid].FindOrAdd(P.Mesh).Add(&P);
    if(!WallStone)WallStone=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/DuelArt/Materials/M_NorthWall.M_NorthWall"));
    auto* Dressed=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/DuelArt/Materials/M_NorthDressedStone.M_NorthDressedStone"));
    int32 Instances=0,Missing=0,Batches=0;
    for(int32 Solid:{0,1}) for(const auto& Group:Groups[Solid])
    {
        const FMeshRecipe* R=Recipe(Group.Key);if(!R)continue;
        const FString Name=Group.Key.ToString();
        const FString Path=FString::Printf(TEXT("/Game/DuelArt/NorthTower/Gothic/%s.%s"),*Name,*Name);
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,*Path);
        if(!Mesh){++Missing;UE_LOG(LogTemp,Error,TEXT("Missing North Gothic mesh %s"),*Path);continue;}
        // Decorative copies span several enclosed storeys. Cluster larger sets
        // so an occluded wing need not submit the whole tower's copies.
        UInstancedStaticMeshComponent* C = !Solid && Group.Value.Num() >= 32
            ? static_cast<UInstancedStaticMeshComponent*>(NewObject<UHierarchicalInstancedStaticMeshComponent>(Owner))
            : NewObject<UInstancedStaticMeshComponent>(Owner);
        C->SetupAttachment(Parent);C->SetMobility(EComponentMobility::Static);C->SetStaticMesh(Mesh);
        C->SetMaterial(0,R->bWall?WallStone:Dressed);
        C->SetCollisionEnabled(Solid?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
        C->SetCollisionResponseToAllChannels(ECR_Block);
        C->SetCastShadow(true);C->bAffectDistanceFieldLighting=true;C->bAffectDynamicIndirectLighting=true;
        C->ComponentTags.Add(TEXT("NorthTowerGothic"));
        if(Solid)C->ComponentTags.Add(TEXT("NorthTowerGothicSolid"));
        Owner->AddInstanceComponent(C);
        TArray<FTransform> Transforms;
        for(const FPiece* P:Group.Value)Transforms.Add(P->Transform);
        C->AddInstances(Transforms,false,false);C->RegisterComponent();
        Instances+=Transforms.Num();
        ++Batches;
    }
    BuildDungeonFinish(Owner, Parent);
    BuildWallBases(Owner, Parent, Dressed ? Dressed : WallStone);
    WeatheredMasonry(Owner, Parent);
    UE_LOG(LogTemp,Display,TEXT("North Gothic: %d mesh batches, %d stone instances, %d missing resources"),Batches,Instances,Missing);
}
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthGothicTest,"ManosabaDuel.NorthTower.GothicDetails",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDuelNorthGothicTest::RunTest(const FString& Parameters)
{
    TArray<FString> Errors;DuelNorthGothic::Validate(Errors);for(const auto& E:Errors)AddError(E);
    ADuelNorthTower* Tower=nullptr;
    if(GEngine)for(const auto& W:GEngine->GetWorldContexts())if(W.World() && W.World()->IsGameWorld())
        for(TActorIterator<ADuelNorthTower> It(W.World());It;++It){Tower=*It;break;}
    if(!TestTrue(TEXT("An isolated North Tower is running"),Tower && FParse::Param(FCommandLine::Get(),TEXT("DuelNoSave"))))return false;
    TInlineComponentArray<UInstancedStaticMeshComponent*> Components(Tower);
    int32 Actual=0,Batches=0;
    for(const auto* C:Components)if(C->ComponentHasTag(TEXT("NorthTowerGothic")))
    {
        ++Batches;Actual+=C->GetInstanceCount();
        const UStaticMesh* Mesh=C->GetStaticMesh();
        if(!TestNotNull(TEXT("Actual dressed stone mesh loaded"),Mesh))continue;
        TestTrue(TEXT("Dressed stone retains actual Nanite data"),Mesh->HasValidNaniteData());
        TestNotNull(TEXT("Dressed stone material loaded"),C->GetMaterial(0));
        TestEqual(TEXT("Only low supports add collision"),C->GetCollisionEnabled()!=ECollisionEnabled::NoCollision,
            C->ComponentHasTag(TEXT("NorthTowerGothicSolid")));
    }
    TestEqual(TEXT("All authored details were installed"),Actual,DuelNorthGothic::Pieces().Num());
    int32 WallCourses = 0, CornerPieces = 0, DungeonCourses = 0, ReservedOpenings = 0;
    for (const auto* Component : Components)
    {
        const bool bWall = Component->ComponentHasTag(TEXT("NorthMasonryWallBases"));
        const bool bCorner = Component->ComponentHasTag(TEXT("NorthMasonryCornerBases"));
        if (!bWall && !bCorner) continue;
        TestTrue(TEXT("Wall transitions are registered visual geometry"),
            Component->IsRegistered() && Component->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
        (bWall ? WallCourses : CornerPieces) += Component->GetInstanceCount();
        const FBox Source = Component->GetStaticMesh()->GetBoundingBox();
        for (int32 I = 0; I < Component->GetInstanceCount(); ++I)
        {
            FTransform T;
            if (!Component->GetInstanceTransform(I, T, false)) continue;
            if (DuelNorthGothic::MeetsBaseOpening(Source.TransformBy(T))) ++ReservedOpenings;
            if (bWall && T.GetLocation().Z < -1600.) ++DungeonCourses;
        }
    }
    TestTrue(TEXT("Wall runs and reinforced corners span both occupied floors"), WallCourses > 200 && CornerPieces > 100);
    TestEqual(TEXT("The masonry base keeps the actual portal opening clear"), ReservedOpenings, 0);
    TestTrue(TEXT("B1 has its own installed stone wall-foot courses"), DungeonCourses > 100);
    int32 Seepage = 0;
    for (const auto* Mark : TInlineComponentArray<UDecalComponent*>(Tower))
    {
        if (!Mark->ComponentHasTag(TEXT("NorthSeepage"))) continue;
        ++Seepage;
        TestTrue(TEXT("Wet masonry relief has a registered local decal"), Mark->IsRegistered() &&
            Mark->GetDecalMaterial() && Mark->DecalSize.X <= 27. && Mark->DecalSize.Y <= 115.);
    }
    TestEqual(TEXT("Six sources of seepage meet the wall and floor"), Seepage, 12);
    TestTrue(TEXT("Shared masonry consolidates the detail kit to at most 125 batches"),Batches>0 && Batches<=125);
    const auto* Mirror = DuelNorthLayout::FindRoom(TEXT("MirrorCourt"));
    const auto* Chapel = DuelNorthLayout::FindRoom(TEXT("ChapelHall"));
    if (Mirror && Chapel)
    {
        for (const auto& Piece : DuelNorthGothic::Pieces())
        {
            if (Piece.Room != Chapel->Id) continue;
            const auto* Mesh = DuelNorthGothic::Recipe(Piece.Mesh);
            if (!Mesh) continue;
            const FBox Bounds = Mesh->LocalBounds.TransformBy(Piece.Transform);
            TestTrue(TEXT("No chapel mesh crosses the mirror arena floor"),
                Bounds.Max.Z < Mirror->Bounds.Min.Z || !Bounds.IntersectXY(Mirror->Bounds));
        }
    }
    AddInfo(FString::Printf(TEXT("Gothic runtime %d batches / %d pieces. Asset installation and battle-floor clearance only; rendered finish and physical walking need their own checks."),Batches,Actual));
    return !HasAnyErrors();
}
#endif
