#include "DuelNorthInteractables.h"

#include "DuelGeometry.h"
#include "DuelNorthArchive.h"
#include "DuelNorthLayout.h"
#include "DuelNorthProgress.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/PointLightComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

namespace DuelNorthInteractables
{
namespace
{
    using namespace DuelNorthLayout;
    constexpr float WalkingMargin = 47.f;
    constexpr float ArchivePullDuration = .94f;
    const FName RootTag(TEXT("NorthInteractable"));
    enum ESurface { Stone, Timber, Iron, Paper, Wax, Glass, Gilt, SurfaceCount };

    double RouteDistance(const FVector& Point)
    {
        const FRoute* Route = FindRoute(TEXT("MainWalk"));
        double Nearest = 100000.;
        if (!Route) return 0;
        for (int32 I = 1; I < Route->Points.Num(); ++I)
        {
            const FVector A = Route->Points[I - 1], B = Route->Points[I];
            const FVector D(B.X - A.X, B.Y - A.Y, 0);
            const double T = D.SizeSquared() > .01 ? FMath::Clamp(FVector::DotProduct(Point - A, D) / D.SizeSquared(), 0., 1.) : 0.;
            const FVector Along = FMath::Lerp(A, B, T);
            if (FMath::Abs(Along.Z - Point.Z) > 110.f) continue;
            Nearest = FMath::Min(Nearest, FVector::Dist2D(Point, Along));
        }
        return Nearest;
    }

    bool InsideRoom(const FRoom& Room, const FVector& Center, const FVector& Size, const FRotator& Rotation)
    {
        for (int32 X : {-1, 1}) for (int32 Y : {-1, 1})
        {
            const FVector Corner = Center + Rotation.RotateVector(FVector(X * (Size.X + 8), Y * (Size.Y + 8), 0));
            if (Corner.X < Room.Bounds.Min.X || Corner.X > Room.Bounds.Max.X ||
                Corner.Y < Room.Bounds.Min.Y || Corner.Y > Room.Bounds.Max.Y) return false;
        }
        return Center.Z >= Room.Bounds.Min.Z - 1 && Center.Z + Size.Z + 10 < Room.Bounds.Max.Z;
    }

    FPlacement BesidePath(FName Id, EKind Kind, uint8 Index, const FVector& Marker, const FVector& Size)
    {
        FPlacement Result{Id, Kind, Index, Marker, FRotator::ZeroRotator, Size};
        const FRoom* Room = FindRoom(Locate(Marker + FVector(0, 0, 62)).Room);
        if (!Room) return Result;
        double Best = -100000.;
        for (float Radius : {105.f, 135.f, 165.f}) for (int32 Direction = 0; Direction < 16; ++Direction)
        {
            const float Angle = 2 * PI * Direction / 16;
            const FVector Offset(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0);
            const FVector Candidate = Marker + Offset;
            const FRotator Facing(0, (-Offset).Rotation().Yaw + 90.f, 0);
            if (!InsideRoom(*Room, Candidate, Size, Facing)) continue;
            const double Clearance = RouteDistance(Candidate) - FVector2D(Size.X, Size.Y).Size();
            const double Score = FMath::Min(Clearance, 190.) - Radius * .08;
            if (Clearance >= WalkingMargin && Score > Best)
            {
                Best = Score;
                Result.Position = Candidate;
                Result.Rotation = Facing;
            }
        }
        return Result;
    }

    struct FPlan
    {
        TArray<FPlacement> Items;
        FPlan()
        {
            const FName Relics[] = {TEXT("RustKey"), TEXT("MirrorKey"), TEXT("NameTag")};
            const FName Clues[] = {TEXT("CellVerdict"), TEXT("WaterGateLog"), TEXT("ChapelJudgments"),
                TEXT("SeaMark"), TEXT("OwnVerdict"), TEXT("Signature"), TEXT("FinalSeal")};
            const FVector ClueSizes[] = {FVector(29, 14, 95), FVector(36, 27, 75), FVector(36, 27, 79),
                FVector(55, 23, 53), FVector(29, 14, 95), FVector(36, 27, 75), FVector(32, 32, 36)};
            for (int32 I = 0; I < 3; ++I)
                if (const FLandmark* L = FindLandmark(Relics[I])) Items.Add(BesidePath(L->Id, EKind::Relic, I, L->Position, FVector(25, 25, 48)));
            for (int32 I = 0; I < 7; ++I)
                if (const FLandmark* L = FindLandmark(Clues[I])) Items.Add(BesidePath(L->Id, EKind::Clue, I, L->Position, ClueSizes[I]));
            const auto AddLedger = [&](FName Id, EDuelNorthClue Clue, const TCHAR* Title, const FVector& Size)
            {
                if (const FLandmark* L = FindLandmark(Id))
                    Items.Add({Id, EKind::Clue, uint8(Clue), L->Position, FRotator(0, 180, 0), Size, Title});
            };
            AddLedger(TEXT("RefugeLedger"), EDuelNorthClue::RefugeLedger, TEXT("配给底账"), FVector(63, 26, 158));
            const EDuelNorthClue ArchiveClues[] = {EDuelNorthClue::ArchiveTransferLedger, EDuelNorthClue::ArchiveSignatureRegister,
                EDuelNorthClue::ArchiveBindingNote, EDuelNorthClue::ArchiveNightIndex};
            static_assert(UE_ARRAY_COUNT(ArchiveClues) == UE_ARRAY_COUNT(DuelNorthArchive::Volumes));
            for (int32 Index = 0; Index < UE_ARRAY_COUNT(ArchiveClues); ++Index)
            {
                const auto& Volume = DuelNorthArchive::Volumes[Index];
                Items.Add({Volume.Id, EKind::Clue, uint8(ArchiveClues[Index]), Volume.Position(), Volume.Rotation(),
                    DuelNorthArchive::VolumeSize, Volume.Title, DuelNorthArchive::VolumeMount, Volume.bOptional});
            }
            if (const FLandmark* L = FindLandmark(TEXT("OpeningDocket")))
                Items.Add({L->Id, EKind::Clue, uint8(EDuelNorthClue::OpeningDocket), L->Position,
                    FRotator::ZeroRotator, FVector(36, 27, 75)});
            for (const FCheckpoint& C : Checkpoints())
                Items.Add(BesidePath(C.Id, EKind::Rest, uint8(C.Floor), C.Position, FVector(65, 25, 62)));
            const FName Locks[] = {TEXT("ChapelDoor"), TEXT("FinalStairDoor"), TEXT("TowerExit")};
            for (int32 I = 0; I < 3; ++I)
            {
                const FPortal* P = Portals().FindByPredicate([&](const FPortal& Value) { return Value.Id == Locks[I]; });
                const FRoom* Approach = P ? FindRoom(P->RoomA) : nullptr;
                if (!P || !Approach) continue;
                const float Sign = FVector::DotProduct(Approach->Bounds.GetCenter() - P->Center, P->Normal) >= 0 ? 1.f : -1.f;
                const FVector Front = P->Normal * Sign;
                const FVector Side = FVector::CrossProduct(Front, FVector::UpVector);
                // A small lock plate sits on the approach face of the jamb, not across the passage.
                const FVector At = P->Center + Front * 16.f + Side * (P->Width * .5f + 16.f);
                Items.Add({Locks[I], EKind::Lock, uint8(I), At, FRotator(0, Front.Rotation().Yaw + 90.f, 0), FVector(12, 6, 145)});
            }
        }
    };

    struct FMesh
    {
        TArray<FVector> V, N;
        TArray<int32> I;
        TArray<FVector2D> UV;
        TArray<FProcMeshTangent> T;

        void Face(const FVector& A, const FVector& B, const FVector& C, const FVector& D,
            FVector2D U0 = FVector2D(0, 0), FVector2D U1 = FVector2D(1, 1))
        {
            const int32 Base = V.Num();
            const FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
            V.Append({A, B, C, D});
            for (int32 K = 0; K < 4; ++K) { N.Add(Normal); T.Emplace((B - A).GetSafeNormal(), false); }
            UV.Append({U0, FVector2D(U1.X, U0.Y), U1, FVector2D(U0.X, U1.Y)});
            I.Append({Base, Base + 2, Base + 1, Base, Base + 3, Base + 2});
        }
        void Triangle(const FVector& A, const FVector& B, const FVector& C)
        {
            const int32 Base = V.Num();
            const FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
            V.Append({A, B, C});
            for (int32 K = 0; K < 3; ++K) { N.Add(Normal); T.Emplace((B - A).GetSafeNormal(), false); }
            UV.Append({FVector2D(.5, .5), FVector2D(0, 0), FVector2D(1, 1)});
            I.Append({Base, Base + 2, Base + 1});
        }
        void Prism(const TArray<FVector2D>& Points, float Low, float High)
        {
            FVector2D Center(0, 0);
            for (const auto& P : Points) Center += P;
            Center /= Points.Num();
            for (int32 K = 0; K < Points.Num(); ++K)
            {
                const auto A = Points[K], B = Points[(K + 1) % Points.Num()];
                Face(FVector(A.X, A.Y, Low), FVector(B.X, B.Y, Low), FVector(B.X, B.Y, High), FVector(A.X, A.Y, High));
                Triangle(FVector(Center.X, Center.Y, High), FVector(A.X, A.Y, High), FVector(B.X, B.Y, High));
                Triangle(FVector(Center.X, Center.Y, Low), FVector(B.X, B.Y, Low), FVector(A.X, A.Y, Low));
            }
        }
        void Box(const FVector& Center, const FVector& Size, float Bevel = 1.f)
        {
            const float X = Size.X * .5f, Y = Size.Y * .5f;
            const float B = FMath::Min(Bevel, FMath::Min(X, Y) * .45f);
            TArray<FVector2D> P{{-X + B, -Y}, {X - B, -Y}, {X, -Y + B}, {X, Y - B},
                {X - B, Y}, {-X + B, Y}, {-X, Y - B}, {-X, -Y + B}};
            for (auto& Point : P) Point += FVector2D(Center.X, Center.Y);
            Prism(P, Center.Z - Size.Z * .5f, Center.Z + Size.Z * .5f);
        }
        void Tube(const FVector& A, const FVector& B, float Radius, int32 Sides = 10)
        {
            const FVector Axis = (B - A).GetSafeNormal();
            FVector U = FVector::CrossProduct(Axis, FVector::UpVector).GetSafeNormal();
            if (U.IsNearlyZero()) U = FVector::RightVector;
            const FVector Vect = FVector::CrossProduct(Axis, U).GetSafeNormal();
            for (int32 K = 0; K < Sides; ++K)
            {
                const float T0 = 2 * PI * K / Sides, T1 = 2 * PI * (K + 1) / Sides;
                const FVector R0 = (U * FMath::Cos(T0) + Vect * FMath::Sin(T0)) * Radius;
                const FVector R1 = (U * FMath::Cos(T1) + Vect * FMath::Sin(T1)) * Radius;
                Face(A + R0, A + R1, B + R1, B + R0);
                Triangle(A, A + R1, A + R0); Triangle(B, B + R0, B + R1);
            }
        }
        void Arc(const FVector& Center, const FVector& U, const FVector& Vect, float Radius, float Thickness,
            float End = 2 * PI, int32 Segments = 24)
        {
            for (int32 K = 0; K < Segments; ++K)
            {
                const float A = End * K / Segments, B = End * (K + 1) / Segments;
                Tube(Center + Radius * (U * FMath::Cos(A) + Vect * FMath::Sin(A)),
                    Center + Radius * (U * FMath::Cos(B) + Vect * FMath::Sin(B)), Thickness, 6);
            }
        }
        void Page(const FTransform& Pose, float Width, float Height)
        {
            const auto Point = [&](float U, float PageV)
            {
                return Pose.TransformPosition(FVector((U - .5f) * Width, (PageV - .5f) * Height,
                    .65f * FMath::Sin(U * PI) + .8f * U * U * PageV));
            };
            for (int32 X = 0; X < 6; ++X) for (int32 Y = 0; Y < 8; ++Y)
            {
                const float U = X / 6.f, PageV = Y / 8.f, U1 = (X + 1) / 6.f, V1 = (Y + 1) / 8.f;
                Face(Point(U, PageV), Point(U1, PageV), Point(U1, V1), Point(U, V1), FVector2D(U, PageV), FVector2D(U1, V1));
            }
        }
    };

    struct FCueMesh
    {
        TArray<FVector> Vertices;
        TArray<int32> Triangles;
        TArray<FVector2D> UVs;
        TArray<FLinearColor> Colors;

        void Vertex(const FVector& At, float Alpha)
        {
            Vertices.Add(At);
            // Sample the bloom halo outside the atlas material's warm core.
            UVs.Emplace(.68, .5);
            Colors.Emplace(1, 1, 1, Alpha);
        }

        void Glow(const FVector& Center, const FVector& U, const FVector& V, float Strength, float EdgeAlpha = .22f, int32 Sides = 12)
        {
            constexpr float Ranges[] = {.22f, .48f, 1.f};
            const float Falloff[] = {.81f, EdgeAlpha, 0.f};
            const int32 Base = Vertices.Num();
            Vertex(Center, Strength);
            for (int32 Ring = 0; Ring < UE_ARRAY_COUNT(Ranges); ++Ring)
            {
                for (int32 Side = 0; Side < Sides; ++Side)
                {
                    const float Angle = 2.f * PI * Side / Sides;
                    const FVector Offset = U * FMath::Cos(Angle) + V * FMath::Sin(Angle);
                    Vertex(Center + Offset * Ranges[Ring], Strength * Falloff[Ring]);
                    const int32 A = Base + 1 + Ring * Sides + Side;
                    const int32 B = Base + 1 + Ring * Sides + (Side + 1) % Sides;
                    if (Ring == 0) Triangles.Append({Base, B, A});
                    else Triangles.Append({A - Sides, B, A, A - Sides, B - Sides, B});
                }
            }
        }

        void Wisp(const FVector& Across, const FVector& Bend, float Height, float Width, float Phase, float Strength)
        {
            constexpr int32 Rows = 16;
            constexpr float Columns[] = {-1.f, -.46f, 0.f, .46f, 1.f};
            constexpr float Falloff[] = {0.f, .24f, 1.f, .24f, 0.f};
            constexpr int32 ColumnCount = UE_ARRAY_COUNT(Columns);
            const int32 Base = Vertices.Num();
            for (int32 Row = 0; Row <= Rows; ++Row)
            {
                const float T = float(Row) / Rows;
                const float Sway = FMath::Sin(7.2f * T + Phase) - FMath::Sin(Phase);
                const float Curl = FMath::Sin(13.2f * T + Phase * 2.f) - FMath::Sin(Phase * 2.f);
                const FVector Center = Bend * ((Sway * 6.f + Curl * 1.5f) * T) +
                    FVector(0, 0, 3.f + Height * T);
                const float Radius = FMath::Lerp(Width, .18f, T) * (.84f + .16f * FMath::Cos(T * 11.f + Phase));
                const float Light = Strength * FMath::Pow(1.f - T, 1.35f) *
                    (.84f + .16f * FMath::Sin(T * 18.f + Phase));
                for (int32 Column = 0; Column < ColumnCount; ++Column)
                {
                    Vertex(Center + Across * (Radius * Columns[Column]), Light * Falloff[Column]);
                    if (Row == 0 || Column == 0) continue;
                    const int32 D = Base + Row * ColumnCount + Column;
                    Triangles.Append({D - ColumnCount - 1, D, D - ColumnCount, D - ColumnCount - 1, D - 1, D});
                }
            }
        }
    };

    struct FVisual
    {
        FPlacement Placement;
        TWeakObjectPtr<UProceduralMeshComponent> Object;
        TWeakObjectPtr<UProceduralMeshComponent> Base;
        TWeakObjectPtr<UBoxComponent> Body;
        TWeakObjectPtr<UProceduralMeshComponent> Accent;
        TWeakObjectPtr<UMaterialInstanceDynamic> EdgeMaterial;
        bool bVisible = true;
        bool bBaseVisible = true;
        float Highlight = 0.f;
        TWeakObjectPtr<UPointLightComponent> RestFire;
        float PullRemaining = 0.f;
    };
    struct FOwnerState { TArray<FVisual> Items; float Time = 0.f; };
    TMap<TWeakObjectPtr<AActor>, FOwnerState> Built;

    UProceduralMeshComponent* Install(AActor* Owner, USceneComponent* Parent, const FPlacement& Place,
        const FMesh* Parts, UMaterialInterface* const* Materials, int32 Count, bool bEdge = false)
    {
        auto* Mesh = NewObject<UProceduralMeshComponent>(Owner);
        Mesh->SetupAttachment(Parent);
        Mesh->SetRelativeLocationAndRotation(Place.Position + FVector(0, 0, Place.MountHeight), Place.Rotation);
        Mesh->SetMobility(Place.IsArchiveVolume() ? EComponentMobility::Movable : EComponentMobility::Static);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetCastShadow(!bEdge && !Place.IsArchiveVolume());
        Mesh->SetCullDistance(bEdge || Place.IsArchiveVolume() ? 2800.f : 5500.f);
        Mesh->ComponentTags.Add(RootTag); Mesh->ComponentTags.Add(Place.Id);
        if (bEdge) Mesh->ComponentTags.Add(TEXT("NorthInteractionEdge"));
        if (Place.IsArchiveVolume()) Mesh->ComponentTags.Add(TEXT("NorthArchiveVolume"));
        for (int32 K = 0; K < Count; ++K)
        {
            if (Parts[K].V.IsEmpty()) continue;
            Mesh->CreateMeshSection(K, Parts[K].V, Parts[K].I, Parts[K].N, Parts[K].UV, TArray<FColor>(), Parts[K].T, false);
            Mesh->SetMaterial(K, Materials[K]);
        }
        Owner->AddInstanceComponent(Mesh); Mesh->RegisterComponent();
        return Mesh;
    }

    UBoxComponent* BodyCollision(AActor* Owner, USceneComponent* Parent, const FPlacement& Place, float Height)
    {
        auto* Body = NewObject<UBoxComponent>(Owner);
        Body->SetupAttachment(Parent); Body->SetRelativeLocationAndRotation(Place.Position + FVector(0, 0, Height * .5f), Place.Rotation);
        Body->SetBoxExtent(FVector(Place.Size.X, Place.Size.Y, Height * .5f));
        Body->SetCollisionProfileName(TEXT("BlockAll")); Body->SetMobility(EComponentMobility::Static);
        // Do not let a low table hide its own interaction ray or push the third-person camera.
        Body->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
        Body->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
        Body->ComponentTags.Add(TEXT("NorthInteractableBody")); Body->ComponentTags.Add(Place.Id);
        Owner->AddInstanceComponent(Body); Body->RegisterComponent();
        return Body;
    }

    void Book(FMesh* Parts, float Z, float Width = 40.f, float Depth = 30.f, bool bStamped = true)
    {
        Parts[Timber].Box(FVector(0, 0, Z), FVector(Width + 3, Depth + 3, 3), 1.4f);
        for (int32 Layer = 0; Layer < 3; ++Layer)
            Parts[Paper].Box(FVector(0, 0, Z + 1.8f + Layer * .45f), FVector(Width - Layer * .7f, Depth - Layer * .25f, .32f), .5f);
        Parts[Paper].Page(FTransform(FVector(-Width * .255f, 0, Z + 3.2f)), Width * .47f, Depth);
        Parts[Paper].Page(FTransform(FVector(Width * .255f, 0, Z + 3.4f)), Width * .47f, Depth);
        // Fine ink rules imply manuscript density; readable, route-specific text is supplied by the HUD.
        for (int32 Side : {-1, 1}) for (int32 Line = 0; Line < 9; ++Line)
        {
            const float Y = -Depth * .36f + Line * Depth * .08f;
            const float Start = Side < 0 ? -Width * .43f : Width * .08f;
            const float End = Start + Width * (.27f + .018f * (Line % 3));
            Parts[Iron].Tube(FVector(Start, Y, Z + 4.7f), FVector(End, Y + .16f, Z + 4.7f), .10f, 4);
        }
        Parts[Iron].Tube(FVector(0, -Depth * .48f, Z + 3.3f), FVector(0, Depth * .48f, Z + 3.3f), .35f, 6);
        if (bStamped)
        {
            Parts[Wax].Tube(FVector(Width * .23f, Depth * .22f, Z + 4), FVector(Width * .23f, Depth * .22f, Z + 5), 3.3f, 14);
            Parts[Gilt].Arc(FVector(Width * .23f, Depth * .22f, Z + 5.15f), FVector::ForwardVector, FVector::RightVector, 2.2f, .16f);
        }
    }

    void ArchiveVolume(FMesh* Parts, const FPlacement& P)
    {
        const double W = P.Size.X * 2., D = P.Size.Y * 2., H = P.Size.Z;
        const auto Clue = static_cast<EDuelNorthClue>(P.Index);
        const ESurface Cover = Clue == EDuelNorthClue::ArchiveTransferLedger ? Wax :
            Clue == EDuelNorthClue::ArchiveSignatureRegister ? Iron : Timber;
        Parts[Paper].Box(FVector(0, -.8, H * .5), FVector(W - 2., D - 2.1, H - 2.), 1.1f);
        for (double Side : {-1., 1.})
            Parts[Cover].Box(FVector(Side * (W * .5 - .7), 0, H * .5), FVector(1.4, D, H), .58f);
        Parts[Cover].Box(FVector(0, D * .5 - 1.9, H * .5), FVector(W, 3.8, H), 1.55f);
        for (double Height : {.14, .86})
            Parts[Gilt].Box(FVector(0, D * .5 + .2, H * Height), FVector(W * .78, .45, .85), .17f);
        Parts[Paper].Box(FVector(0, D * .5 + .36, H * .59), FVector(W * .69, .55, H * .22), .2f);
        for (double Line : {-1., 1.})
            Parts[Timber].Box(FVector(0, D * .5 + .68, H * .59 + Line * 1.15), FVector(W * .46, .26, .38), .08f);
        if (Clue == EDuelNorthClue::ArchiveTransferLedger)
        {
            Parts[Paper].Box(FVector(-W * .17, D * .5 - 4, H + .5), FVector(W * .37, 15, 1.9), .28f);
            Parts[Gilt].Box(FVector(0, D * .5 + .8, H * .23), FVector(W * .43, .65, 3.7), .4f);
        }
        if (Clue == EDuelNorthClue::ArchiveSignatureRegister)
        {
            for (double X : {-W * .19, W * .19})
                Parts[Wax].Tube(FVector(X, D * .5 + .55, H * .25), FVector(X, D * .5 + 1.25, H * .25), 1.44f, 12);
        }
        if (P.bOptional)
        {
            Parts[Paper].Box(FVector(W * .24, D * .5 + .8, H * .88), FVector(W * .27, 1.8, H * .21), .22f);
            Parts[Wax].Box(FVector(W * .24, D * .5 + 1.76, H * .86), FVector(2.35, .42, 2.4), .14f);
        }
    }

    void Bench(FMesh* Parts, float Width, bool bRest)
    {
        Parts[Stone].Box(FVector(0, 0, 43), FVector(Width, 42, 8), 5);
        Parts[Stone].Box(FVector(0, 0, 37), FVector(Width - 8, 35, 5), 3);
        for (int32 Side : {-1, 1})
        {
            Parts[Stone].Box(FVector(Side * (Width * .5f - 14), 0, 18), FVector(13, 30, 36), 2);
            Parts[Stone].Box(FVector(Side * (Width * .5f - 14), 0, 3), FVector(22, 37, 6), 2);
        }
        Parts[Iron].Tube(FVector(-Width * .38f, -17.8f, 37), FVector(Width * .38f, -17.8f, 37), .65f, 6);
        for (int32 K = -2; K <= 2; ++K)
            Parts[Iron].Tube(FVector(K * 4, -5, 47.15f), FVector(K * 4 + 2, 3, 47.15f), .22f, 5);
        if (bRest)
        {
            // A folded travelling cloth and a closed book identify a place to rest.
            Parts[Timber].Box(FVector(34, 0, 49), FVector(27, 23, 4), 2);
            Parts[Paper].Box(FVector(34, 0, 51), FVector(24, 21, 2), 1);
            Parts[Timber].Box(FVector(34, 0, 53), FVector(27, 23, 1.4f), 1);
            Parts[Gilt].Box(FVector(34, 0, 53.9f), FVector(1.2f, 21, .2f), .1f);
            Parts[Iron].Tube(FVector(-26,0,47), FVector(-26,0,52), 16.f, 16);
            Parts[Gilt].Tube(FVector(-26,0,52), FVector(-26,0,54), 13.f, 16);
            for (int32 I = 0; I < 7; ++I)
            {
                const FVector Out = FRotator(0,I * (360. / 7.),0).Vector();
                Parts[Stone].Tube(FVector(-26,0,54) + Out * 9, FVector(-26,0,54) + Out * 11 + FVector(0,0,3), 3.f, 6);
            }
        }
    }

    void Table(FMesh* Parts)
    {
        Parts[Timber].Box(FVector(0, 0, 63), FVector(70, 50, 5), 3);
        Parts[Timber].Box(FVector(0, 0, 57), FVector(61, 42, 5), 2);
        for (int32 X : {-1, 1}) for (int32 Y : {-1, 1})
        {
            const FVector Foot(X * 26, Y * 16, 0);
            Parts[Timber].Tube(Foot + FVector(0, 0, 2), Foot + FVector(0, 0, 59), 3.8f, 8);
            Parts[Iron].Tube(Foot + FVector(0, 0, 8), Foot + FVector(0, 0, 11), 4.15f, 8);
        }
        Parts[Timber].Tube(FVector(-26, 0, 17), FVector(26, 0, 17), 2.4f, 8);
        Book(Parts, 67, 39, 31);
        Parts[Iron].Tube(FVector(24, -15, 66.2f), FVector(30, 12, 66.2f), .55f, 8);
        for (int32 K = 0; K < 8; ++K)
            Parts[Paper].Triangle(FVector(27, K * 1.5f, 66.5f), FVector(21 - K * .3f, K * 1.5f + 4, 67), FVector(27, K * 1.5f + 2, 66.6f));
    }

    void LedgerShelf(FMesh* Parts, const FPlacement& P)
    {
        const double W = P.Size.X * 2, D = P.Size.Y * 2, H = P.Size.Z;
        const double Upper = H - 36.;
        Parts[Timber].Box(FVector(0, 0, 4), FVector(W, D, 8), 2);
        Parts[Timber].Box(FVector(0, -P.Size.Y + 2, H * .5), FVector(W - 12, 4, H), 1);
        Parts[Timber].Box(FVector(0, 0, H - 4), FVector(W, D, 8), 2);
        for (double X : {-P.Size.X + 4., P.Size.X - 4.})
        {
            Parts[Timber].Box(FVector(X, 0, H * .5), FVector(8, D, H), 2);
            Parts[Gilt].Box(FVector(X, P.Size.Y + .25, H - 21), FVector(3.8, .8, 18), .3f);
        }
        Parts[Timber].Box(FVector(0, 0, LedgerDeckHeight - 3), FVector(W - 6, D, 6), 2);
        Parts[Timber].Box(FVector(0, -5, Upper - 2), FVector(W - 8, D - 10, 4), 1);
        for (double X : {-W * .23, W * .23})
        {
            Parts[Timber].Box(FVector(X, P.Size.Y - 4, 42), FVector(W * .44, 6, 66), 3);
            Parts[Iron].Box(FVector(X, P.Size.Y - .6, 36), FVector(W * .35, 2, 3), 1);
            Parts[Gilt].Box(FVector(X, P.Size.Y + 1.4, 54), FVector(12, 4.4, 4), 1);
        }
        for (int32 I = 0; I < 9; ++I)
        {
            const double X = (I - 4) * W / 11.;
            const double Z = 20. + (I * 5 + P.Index) % 10;
            const auto Cover = I % 3 == 0 ? Wax : I % 3 == 1 ? Iron : Timber;
            Parts[Cover].Box(FVector(X, -9, Upper + Z * .5), FVector(9.2 + I % 3, 25, Z), .7f);
            Parts[Paper].Box(FVector(X, -8, Upper + Z), FVector(6.5, 21, .45), .3f);
            Parts[Gilt].Box(FVector(X, 3.7, Upper + Z * .71), FVector(5.2, .45, 1.8), .2f);
        }
    }

    void NoticeBoard(FMesh* Parts)
    {
        for (int32 X : {-1, 1})
        {
            Parts[Timber].Box(FVector(X * 21, 0, 4), FVector(9, 29, 8), 2);
            Parts[Timber].Box(FVector(X * 21, 0, 46), FVector(6, 9, 88), 1.5f);
        }
        Parts[Timber].Box(FVector(0, 0, 58), FVector(51, 11, 62), 2);
        Parts[Paper].Page(FTransform(FQuat(FVector::ForwardVector, PI * .5f), FVector(0, -6.4f, 59)), 34, 49);
        Parts[Iron].Box(FVector(0, -8.1f, 81), FVector(15, 2.4, 3), .8f);
        for (int32 Line = 0; Line < 9; ++Line)
        {
            const float Z = 71 - Line * 3.6f;
            Parts[Iron].Tube(FVector(-12, -8.5f, Z), FVector(10 + (Line % 3) * 1.3f, -8.5f, Z), .22f, 5);
        }
    }

    void TornVerdict(FMesh* Parts)
    {
        Parts[Timber].Box(FVector(0, 0, 30.5f), FVector(60, 48, 5), 2);
        for (double X : {-23., 23.})
            for (double Y : {-16., 16.})
                Parts[Timber].Box(FVector(X, Y, 14), FVector(7, 7, 28), 1.4f);
        Parts[Paper].Prism({{-22, -16}, {-1, -16}, {-2, -11}, {-.5, -7}, {-4, -3}, {-.5, 2}, {-2.2, 7}, {1, 14}, {-22, 14}}, 33.05f, 33.7f);
        Parts[Paper].Prism({{4, -15}, {23, -12}, {21, 17}, {6, 14}, {3, 7}, {5, 3}, {1.3, -3}, {4.8, -7}, {3, -11}}, 33.1f, 33.8f);
        for (int32 I = 0; I < 6; ++I)
        {
            const double Y = -10. + 4. * I;
            Parts[Iron].Tube(FVector(-18, Y, 33.8), FVector(-6 - (I % 3), Y, 33.8), .22f, 5);
            Parts[Iron].Tube(FVector(7, Y + 1, 33.9), FVector(18 - (I % 2), Y + 2.4, 33.9), .22f, 5);
        }
    }

    void Relic(FMesh* Parts, int32 Index)
    {
        const float Z = 29.f;
        if (Index == 0)
        {
            const FVector Bow(-10, 0, Z + 1);
            Parts[Iron].Arc(Bow, FVector::ForwardVector, FVector::RightVector, 7.f, 1.4f);
            Parts[Iron].Arc(Bow, FVector::ForwardVector, FVector::RightVector, 4.4f, .55f);
            Parts[Iron].Tube(FVector(-3, 0, Z + 1), FVector(20, 0, Z + 1), 1.5f);
            for (int32 X : {10, 16, 20}) Parts[Iron].Box(FVector(X, 3.5f, Z + 1), FVector(2.7f, 7, 2.5f), .4f);
        }
        else if (Index == 1)
        {
            for (double Y : {-7., 7.})
                Parts[Gilt].Tube(FVector(-15, Y, Z + 1), FVector(-2, Y, Z + 1), 1.6f, 8);
            for (double X : {-15., -2.})
                Parts[Gilt].Tube(FVector(X, -7, Z + 1), FVector(X, 7, Z + 1), 1.6f, 8);
            Parts[Iron].Tube(FVector(-2, 0, Z + 1), FVector(20, 0, Z + 1), 1.5f, 8);
            for (int32 X : {9, 15, 20})
                Parts[Iron].Box(FVector(X, 3.1f, Z + 1), FVector(2.8f, 6.2f, 2.7f), .4f);
        }
        else
        {
            Parts[Timber].Prism({{-10, -16}, {8, -16}, {11, -13}, {11, 10}, {5, 16}, {-6, 16}, {-11, 10}, {-11, -13}},
                Z - 1.25f, Z + 1.25f);
            Parts[Gilt].Box(FVector(0, -14, Z + 1.45f), FVector(18.8, 2.2, .6), .55f);
            Parts[Gilt].Arc(FVector(0, 11, Z + 1.4f), FVector::ForwardVector, FVector::RightVector, 2.1f, .55f);
            Parts[Gilt].Arc(FVector(0, 16.5f, Z + 1.7f), FVector::ForwardVector, FVector::RightVector, 4.1f, .66f);
            for (double X : {-5., 5.})
                Parts[Paper].Tube(FVector(X, -7, Z + 1.35f), FVector(X, 5, Z + 1.35f), .44f, 6);
            Parts[Paper].Tube(FVector(-5, 5, Z + 1.35f), FVector(5, 5, Z + 1.35f), .44f, 6);
        }
    }

    FVector CueOrigin(const FPlacement& P)
    {
        if (P.LedgerTitle) return P.ReadingPoint();
        FVector Local;
        if (P.Kind == EKind::Relic)
            Local = P.Index == uint8(EDuelNorthRelic::RustKey) ? FVector(6, 0, 32.3f) : FVector(0, 0, 31.4f);
        else if (P.Index == uint8(EDuelNorthClue::SeaMark))
            Local = FVector(-30, 0, 48.3f);
        else if (P.Index == uint8(EDuelNorthClue::CellVerdict) || P.Index == uint8(EDuelNorthClue::OwnVerdict))
            Local = FVector(0, -12, 8.9f);
        else if (P.Index == uint8(EDuelNorthClue::FinalSeal))
            Local = FVector(0, 0, 35.5f);
        else
            Local = FVector(-6, 0, 72.5f);
        return P.Position + P.Rotation.RotateVector(Local);
    }

    bool Available(const FPlacement& P, const FDuelNorthProgress& State)
    {
        if (P.Kind == EKind::Relic) return State.RelicAvailable(static_cast<EDuelNorthRelic>(P.Index));
        if (P.Kind == EKind::Clue) return State.CanReadClue(static_cast<EDuelNorthClue>(P.Index));
        if (P.Kind == EKind::Rest) return State.CanActivateCheckpoint(static_cast<EDuelNorthFloor>(P.Index));
        return !State.IsDoorOpen(static_cast<EDuelNorthDoor>(P.Index));
    }
}

const TArray<FPlacement>& Placements() { static const FPlan Plan; return Plan.Items; }

const FPlacement* Ledger(FName Id)
{
    return Placements().FindByPredicate([Id](const FPlacement& Item) { return Item.LedgerTitle && Item.Id == Id; });
}

bool IsVisible(const FPlacement& Placement, const FDuelNorthProgress& State)
{
    if (Placement.Kind == EKind::Relic) return State.RelicAvailable(static_cast<EDuelNorthRelic>(Placement.Index));
    if (Placement.Kind == EKind::Clue) return State.CanReadClue(static_cast<EDuelNorthClue>(Placement.Index));
    if (Placement.Kind == EKind::Lock) return !State.IsDoorOpen(static_cast<EDuelNorthDoor>(Placement.Index));
    return true;
}

static UProceduralMeshComponent* BuildReadingAccent(AActor* Owner, USceneComponent* Parent, FName Id, const FVector& Origin,
    const FPlacement* ShelfVolume)
{
    auto* Material = UMaterialInstanceDynamic::Create(DuelGeometry::ArtMaterial(TEXT("M_SacredFx")), Owner);
    if (!Material) return nullptr;
    Material->SetVectorParameterValue(TEXT("TileOrigin"), FLinearColor(.25f, .50f, 0, 0));
    Material->SetScalarParameterValue(TEXT("TileSize"), .25f);
    Material->SetScalarParameterValue(TEXT("SoftDepth"), 2.f);
    Material->SetScalarParameterValue(TEXT("Gain"), ShelfVolume ? 14.f : 12.f);

    FCueMesh Light;
    const float Phase = float(GetTypeHash(Id.ToString()) & 255) * (2.f * PI / 256.f);
    const FVector U(FMath::Cos(Phase), FMath::Sin(Phase), 0);
    const FVector V(-U.Y, U.X, 0);
    if (ShelfVolume)
    {
        const FVector Across = ShelfVolume->Rotation.Vector();
        Light.Glow(FVector::ZeroVector, Across * 6.8, FVector::UpVector * 8.1, .94f, .09f, 24);
        Light.Glow(FVector::ZeroVector, ShelfVolume->Front() * 1.8, FVector::UpVector * 6.2, .57f, .09f, 24);
    }
    else
    {
        const FVector Core(0, 0, 3.4f);
        Light.Glow(Core, U * 13.f, FVector::UpVector * 6.8f, .70f);
        Light.Glow(Core, V * 11.f, FVector::UpVector * 7.5f, .65f);
        Light.Glow(FVector(0, 0, .45f), U * 15.f, V * 12.f, .09f);
        Light.Wisp(U, V, 66.f + 6.f * FMath::Sin(Phase), 4.4f, Phase, .67f);
        Light.Wisp(V, U, 48.f + 5.f * FMath::Cos(Phase), 3.5f, Phase + 1.6f, .46f);
        const FVector Diagonal = (U + V).GetSafeNormal();
        Light.Wisp(Diagonal, FVector(-Diagonal.Y, Diagonal.X, 0), 36.f, 2.1f, Phase - .9f, .34f);
    }

    auto* Mesh = NewObject<UProceduralMeshComponent>(Owner);
    Mesh->SetupAttachment(Parent);
    Mesh->SetRelativeLocation(Origin);
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCanEverAffectNavigation(false);
    Mesh->SetCastShadow(false);
    Mesh->SetAffectDynamicIndirectLighting(false);
    Mesh->SetAffectDistanceFieldLighting(false);
    Mesh->SetCullDistance(2800.f);
    Mesh->ComponentTags.Add(Id);
    Mesh->ComponentTags.Add(TEXT("NorthStoryCue"));
    if (ShelfVolume) Mesh->ComponentTags.Add(TEXT("NorthArchiveReadingCue"));
    Mesh->CreateMeshSection_LinearColor(0, Light.Vertices, Light.Triangles, {}, Light.UVs, Light.Colors, {}, false, false);
    Mesh->SetMaterial(0, Material);
    Mesh->SetHiddenInGame(true);
    Owner->AddInstanceComponent(Mesh);
    Mesh->RegisterComponent();
    return Mesh;
}

UProceduralMeshComponent* BuildStoryCue(AActor* Owner, USceneComponent* Parent, FName Id, const FVector& Origin)
{
    return BuildReadingAccent(Owner, Parent, Id, Origin, nullptr);
}

void UpdateStoryCue(UProceduralMeshComponent* Cue, bool bShow, ECueTone Tone, float Strength, float Seconds)
{
    if (!Cue) return;
    Cue->SetHiddenInGame(!bShow);
    if (!bShow) return;
    if (auto* Material = Cast<UMaterialInstanceDynamic>(Cue->GetMaterial(0)))
    {
        const FVector At = Cue->GetComponentLocation();
        const float Breath = .95f + .05f * FMath::Sin(Seconds * 1.55f + float(At.X * .017 + At.Y * .013));
        const FLinearColor Tint = Tone == ECueTone::Treasure ? FLinearColor(1.f, .73f, .19f) :
            Tone == ECueTone::Rest ? FLinearColor(1.f, .62f, .15f) :
            Tone == ECueTone::Pickup ? FLinearColor(.55f, .14f, 1.f) : FLinearColor(.83f, .94f, 1.f);
        Material->SetVectorParameterValue(TEXT("Tint"), Tint);
        Material->SetScalarParameterValue(TEXT("Opacity"), Strength * Breath);
    }
}

void Build(AActor* Owner, USceneComponent* Parent)
{
    if (!Owner || !Parent || Built.Contains(Owner)) return;
    for (auto It = Built.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
    FOwnerState& Set = Built.Add(Owner);
    UMaterialInterface* Materials[SurfaceCount];
    Materials[Stone] = DuelGeometry::ArtMaterial(TEXT("M_TrimStone"));
    Materials[Timber] = DuelGeometry::ColorMaterial(Owner, FLinearColor(.12f, .065f, .034f), false, .85f, .05f);
    Materials[Iron] = DuelGeometry::ColorMaterial(Owner, FLinearColor(.115f, .085f, .052f), false, .72f, .84f);
    // The old EvidenceDocket texture contains the previous plot's timestamps. Do not put those back into the new story.
    Materials[Paper] = DuelGeometry::ColorMaterial(Owner, FLinearColor(.58f, .50f, .35f), false, .93f);
    Materials[Wax] = DuelGeometry::ColorMaterial(Owner, FLinearColor(.24f, .021f, .026f), false, .53f);
    Materials[Glass] = DuelGeometry::ArtMaterial(TEXT("M_WitnessGlass"));
    Materials[Gilt] = DuelGeometry::ColorMaterial(Owner, FLinearColor(.37f, .23f, .074f), false, .42f, .88f);
    for (auto& M : Materials) if (!M) M = Materials[Iron];
    for (const FPlacement& P : Placements())
    {
        FMesh Parts[SurfaceCount], Edge;
        UProceduralMeshComponent* BaseMesh = nullptr;
        UBoxComponent* Body = nullptr;
        if (P.Kind == EKind::Relic)
        {
            FMesh Base;
            Base.Box(FVector(0, 0, 4), FVector(48, 48, 8), 5);
            Base.Box(FVector(0, 0, 15), FVector(39, 39, 14), 4);
            Base.Box(FVector(0, 0, 24), FVector(46, 46, 4), 5);
            UMaterialInterface* StoneOnly[] = {Materials[Stone]};
            BaseMesh = Install(Owner, Parent, P, &Base, StoneOnly, 1);
            Body = BodyCollision(Owner, Parent, P, 26);
            Relic(Parts, P.Index);
        }
        else if (P.IsArchiveVolume())
        {
            ArchiveVolume(Parts, P);
        }
        else if (P.LedgerTitle)
        {
            FMesh Shelf[SurfaceCount];
            LedgerShelf(Shelf, P);
            UMaterialInterface* ShelfMaterials[SurfaceCount];
            for (int32 Index = 0; Index < SurfaceCount; ++Index) ShelfMaterials[Index] = Materials[Index];
            if (auto* Oak = DuelGeometry::ArtMaterial(TEXT("M_NorthOak"))) ShelfMaterials[Timber] = Oak;
            BaseMesh = Install(Owner, Parent, P, Shelf, ShelfMaterials, SurfaceCount);
            BaseMesh->ComponentTags.Add(TEXT("NorthLedgerShelf"));
            Body = BodyCollision(Owner, Parent, P, P.Size.Z);

            constexpr float BookZ = LedgerDeckHeight + .5;
            Book(Parts, BookZ, 52, 30, false);
            for (int32 Row = 0; Row < 3; ++Row)
            {
                const double Y = -9. + Row * 8.;
                constexpr float Z = BookZ + 5.1f;
                Parts[Iron].Arc(FVector(-19, Y, Z), FVector::ForwardVector, FVector::RightVector, 1.45f, .28f, 2 * PI, 10);
                Parts[Iron].Tube(FVector(-14, Y - 1.5, Z), FVector(-14, Y + 1.5, Z), .24f, 6);
                if (Row != 0)
                {
                    Parts[Iron].Tube(FVector(-16, Y - 1.5, Z), FVector(-14, Y - 1.5, Z), .24f, 6);
                    Parts[Iron].Tube(FVector(-16, Y + 1.5, Z), FVector(-14, Y + 1.5, Z), .24f, 6);
                }
                if (Row == 2) Parts[Iron].Tube(FVector(-15.7, Y, Z), FVector(-14, Y, Z), .24f, 6);
            }
            for (auto& Part : Parts)
                for (FVector& Point : Part.V) Point.Y += P.Size.Y * .37;
        }
        else if (P.Kind == EKind::Rest || (P.Kind == EKind::Clue && P.Index == uint8(EDuelNorthClue::SeaMark)))
        {
            const bool bRest = P.Kind == EKind::Rest;
            Bench(Parts, bRest ? 128.f : 106.f, bRest);
            Body = BodyCollision(Owner, Parent, P, 47);
        }
        else if (P.Kind == EKind::Lock && P.Index == uint8(EDuelNorthDoor::TowerExit))
        {
            Parts[Iron].Box(FVector(0, 0, 111), FVector(29, 9, 40), 3);
            Parts[Gilt].Box(FVector(0, -4.9, 111), FVector(24, 1.4, 34), 2);
            Parts[Timber].Box(FVector(0, -5.9, 111), FVector(15, .9, 26), 1);
            for (double X : {-8., 8.})
                Parts[Iron].Box(FVector(X, -6.9, 111), FVector(3, 5, 29), .8);
            Parts[Iron].Box(FVector(0, -6.9, 97), FVector(18, 5, 3), .8);
            Edge.Tube(FVector(-6, -8.9, 123), FVector(6, -8.9, 123), .25f, 8);
        }
        else if (P.Kind == EKind::Lock)
        {
            Parts[Iron].Box(FVector(0, 0, 111), FVector(22, 8, 30), 3);
            Parts[Gilt].Box(FVector(0, -4.2f, 111), FVector(17, .7f, 25), 2);
            Parts[Iron].Arc(FVector(0, 0, 129), FVector::ForwardVector, FVector::UpVector, 7.f, 1.7f, PI, 16);
            Parts[Iron].Tube(FVector(-7, 0, 122), FVector(-7, 0, 129), 1.7f);
            Parts[Iron].Tube(FVector(7, 0, 122), FVector(7, 0, 129), 1.7f);
            Parts[Iron].Tube(FVector(0, -4.8f, 111), FVector(0, -5.1f, 111), 2.1f, 12);
            Parts[Iron].Box(FVector(0, -5, 107.5f), FVector(2.5f, .3f, 5), .2f);
            Edge.Arc(FVector(0, -5.3f, 111), FVector::ForwardVector, FVector::UpVector, 3.1f, .22f);
        }
        else if (P.Index == uint8(EDuelNorthClue::CellVerdict) || P.Index == uint8(EDuelNorthClue::OwnVerdict))
        {
            NoticeBoard(Parts); Body = BodyCollision(Owner, Parent, P, 92);
        }
        else if (P.Index == uint8(EDuelNorthClue::FinalSeal))
        {
            TornVerdict(Parts);
            Body = BodyCollision(Owner, Parent, P, 33);
        }
        else
        {
            Table(Parts); Body = BodyCollision(Owner, Parent, P, 65);
        }
        auto* Object = Install(Owner, Parent, P, Parts, Materials, SurfaceCount);
        if (P.Kind == EKind::Relic || P.Kind == EKind::Clue || P.Kind == EKind::Rest)
        {
            const bool bRest = P.Kind == EKind::Rest;
            const FVector Origin = bRest ? P.Position + P.Rotation.RotateVector(FVector(-26,0,56)) : CueOrigin(P);
            auto* Light = BuildReadingAccent(Owner, Parent, P.Id, Origin, P.IsArchiveVolume() ? &P : nullptr);
            if (Light && P.LedgerTitle && !P.IsArchiveVolume())
                Light->SetRelativeScale3D(FVector(.8, .8, (P.Size.Z - 40. - (LedgerDeckHeight + 6.2)) / 80.));
            FVisual& Item = Set.Items.AddDefaulted_GetRef();
            Item = {P, Object, BaseMesh, Body, Light};
            if (bRest)
            {
                auto* Fire = NewObject<UPointLightComponent>(Owner);
                Fire->SetupAttachment(Parent);
                Fire->SetMobility(EComponentMobility::Movable);
                Fire->SetRelativeLocation(Origin + FVector(0,0,46));
                Fire->SetIntensityUnits(ELightUnits::Candelas);
                Fire->SetAttenuationRadius(490.f);
                Fire->SetLightColor(FLinearColor(1.f,.70f,.35f));
                Fire->SetSourceRadius(18.f);
                Fire->SetSpecularScale(.18f);
                Fire->SetCastShadows(false);
                Fire->SetVolumetricScatteringIntensity(.14f);
                Fire->SetVisibility(false);
                Fire->ComponentTags.Add(TEXT("NorthRestFire"));
                Owner->AddInstanceComponent(Fire);
                Fire->RegisterComponent();
                Item.RestFire = Fire;
            }
            continue;
        }
        auto* Glow = Cast<UMaterialInstanceDynamic>(DuelGeometry::ColorMaterial(Owner, FLinearColor::Black, true));
        UMaterialInterface* GlowOnly[] = {Glow ? static_cast<UMaterialInterface*>(Glow) : Materials[Gilt]};
        auto* EdgeMesh = Install(Owner, Parent, P, &Edge, GlowOnly, 1, true);
        EdgeMesh->SetHiddenInGame(true);
        Set.Items.Add({P, Object, BaseMesh, Body, EdgeMesh, Glow, true, true, 0});
    }
    FDuelNorthProgress Initial;
    Update(Owner, Initial, NAME_None, 0, false);
}

void Update(AActor* Owner, const FDuelNorthProgress& State, FName Nearby, float DeltaSeconds, bool bShowCues)
{
    FOwnerState* Set = Built.Find(Owner);
    if (!Set) return;
    Set->Time = FMath::Fmod(Set->Time + FMath::Max(0.f, DeltaSeconds), 10000.f);
    for (FVisual& V : Set->Items)
    {
        const bool Visible = IsVisible(V.Placement, State);
        const bool bBodyVisible = Visible || V.Placement.IsArchiveVolume();
        if (V.bVisible != bBodyVisible)
        {
            if (V.Object.IsValid()) V.Object->SetHiddenInGame(!bBodyVisible);
            V.bVisible = bBodyVisible;
        }
        if (V.PullRemaining > 0.f)
        {
            V.PullRemaining = FMath::Max(0.f, V.PullRemaining - DeltaSeconds);
            const float T = 1.f - V.PullRemaining / ArchivePullDuration;
            const double Travel = FMath::SmoothStep(0.f, .23f, T) * (1.f - FMath::SmoothStep(.49f, 1.f, T)) * 36.;
            const FVector Offset = V.Placement.Front() * Travel;
            if (auto* Object = V.Object.Get())
                Object->SetRelativeLocation(V.Placement.Position + FVector(0, 0, V.Placement.MountHeight) + Offset);
            if (auto* Cue = V.Accent.Get()) Cue->SetRelativeLocation(V.Placement.ReadingPoint() + Offset);
        }
        // Loot plinths and post-battle evidence do not add obstacles to a live arena.
        const bool BaseVisible = V.Placement.LedgerTitle ||
            (V.Placement.Kind == EKind::Relic ? State.HasWon(V.Placement.Index) : Visible);
        if (V.bBaseVisible != BaseVisible)
        {
            if (V.Base.IsValid()) V.Base->SetHiddenInGame(!BaseVisible);
            if (V.Body.IsValid()) V.Body->SetCollisionEnabled(BaseVisible ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
            V.bBaseVisible = BaseVisible;
        }
        const bool Near = Nearby == V.Placement.Id && Available(V.Placement, State);
        const bool bPickup = V.Placement.Kind == EKind::Relic;
        const bool bReading = V.Placement.Kind == EKind::Clue;
        const bool bRest = V.Placement.Kind == EKind::Rest;
        const bool bReadLedger = V.Placement.LedgerTitle && State.HasClue(static_cast<EDuelNorthClue>(V.Placement.Index));
        const float ReadingIdle = V.Placement.IsArchiveVolume() ? (V.Placement.bOptional ? .53f : .73f) : .43f;
        const float Idle = bRest ? .74f : bPickup ? .72f : bReading && !bReadLedger ? ReadingIdle : 0.f;
        const float Goal = Visible && bShowCues ? (Near ? .82f : Idle) : 0.f;
        V.Highlight = FMath::FInterpTo(V.Highlight, Goal, DeltaSeconds, Near ? 8.f : 5.f);
        if (!Visible || !bShowCues) V.Highlight = 0.f;
        if (bPickup || bReading || bRest)
        {
            const ECueTone Tone = bRest ? ECueTone::Rest : bPickup ? ECueTone::Pickup : ECueTone::Reading;
            UpdateStoryCue(V.Accent.Get(), V.Highlight >= .005f, Tone, V.Highlight, Set->Time);
            if (auto* Fire = V.RestFire.Get())
            {
                const bool bLit = Visible && bShowCues;
                Fire->SetVisibility(bLit);
                if (bLit)
                {
                    const float Flicker = 1.f + .075f * FMath::Sin(Set->Time * 4.1f) + .04f * FMath::Sin(Set->Time * 12.3f);
                    Fire->SetIntensity(78.f * Flicker);
                    if (auto* Accent = V.Accent.Get())
                        Accent->SetRelativeScale3D(FVector(1.28, 1.28, 1.05 * Flicker));
                }
            }
            continue;
        }
        if (V.Accent.IsValid()) V.Accent->SetHiddenInGame(V.Highlight < .005f);
        if (V.EdgeMaterial.IsValid() && V.Highlight >= .005f)
        {
            const float Breath = .96f + .04f * FMath::Sin(Set->Time * 1.7f);
            const FLinearColor Cue(.88f, .55f, .19f);
            V.EdgeMaterial->SetVectorParameterValue(TEXT("Color"), Cue * V.Highlight * Breath);
        }
    }
}

void PullArchiveVolume(AActor* Owner, FName Id)
{
    if (FOwnerState* Set = Built.Find(Owner))
        if (FVisual* Visual = Set->Items.FindByPredicate([Id](const FVisual& V)
            { return V.Placement.Id == Id && V.Placement.IsArchiveVolume(); }))
            Visual->PullRemaining = ArchivePullDuration;
}

bool Validate(TArray<FString>& Errors)
{
    const auto& All = Placements();
    constexpr int32 RelicCount = 3, OtherClues = 9, LockCount = 3;
    if (All.Num() != RelicCount + OtherClues + UE_ARRAY_COUNT(DuelNorthArchive::Volumes) + Checkpoints().Num() + LockCount)
        Errors.Add(TEXT("Unexpected number of relics, physical clues, rests and lock plates"));
    TSet<FName> Ids;
    for (const FPlacement& P : All)
    {
        if (Ids.Contains(P.Id)) Errors.Add(FString::Printf(TEXT("Duplicate interactable %s"), *P.Id.ToString()));
        Ids.Add(P.Id);
        if (P.Kind == EKind::Lock) continue;
        const FRoom* Room = FindRoom(Locate(P.Position + FVector(0, 0, 62)).Room);
        if (!Room || !InsideRoom(*Room, P.Position, P.Size, P.Rotation))
            Errors.Add(FString::Printf(TEXT("Interactable %s exceeds its room"), *P.Id.ToString()));
        const double Clearance = RouteDistance(P.Position) - FVector2D(P.Size.X, P.Size.Y).Size();
        if (Clearance < WalkingMargin)
            Errors.Add(FString::Printf(TEXT("Interactable %s leaves only %.1f cm for the MainWalk capsule"), *P.Id.ToString(), Clearance));
    }
    return Errors.IsEmpty();
}
}

#if WITH_DEV_AUTOMATION_TESTS
#include "DuelNorthInvestigationQA.h"
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthInteractablesTest, "ManosabaDuel.NorthTower.Interactables",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDuelNorthInteractablesTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    TArray<FString> Errors;
    DuelNorthInteractables::Validate(Errors);
    for (const auto& Error : Errors) AddError(Error);
    FDuelNorthProgress State;
    for (int32 Battle = 0; Battle < 3; ++Battle)
    {
        const auto* Relic = DuelNorthInteractables::Placements().FindByPredicate([&](const auto& P)
        { return P.Kind == DuelNorthInteractables::EKind::Relic && P.Index == Battle; });
        if (!TestNotNull(TEXT("Each battle has one physical relic"), Relic)) return false;
        TestFalse(TEXT("Relic is not displayed before its battle is won"), DuelNorthInteractables::IsVisible(*Relic, State));
        if (!TestTrue(TEXT("Complete the required investigation and win the battle"),
            DuelNorthInvestigationQA::Prepare(State, Battle) && State.WinBattle(Battle))) return false;
        TestTrue(TEXT("Winning makes the dropped object visible"), DuelNorthInteractables::IsVisible(*Relic, State));
        State.PickupRelic(static_cast<EDuelNorthRelic>(Battle));
        TestFalse(TEXT("Picking up hides only the dropped object"), DuelNorthInteractables::IsVisible(*Relic, State));
        const auto* Lock = DuelNorthInteractables::Placements().FindByPredicate([&](const auto& P)
        { return P.Kind == DuelNorthInteractables::EKind::Lock && P.Index == Battle; });
        if (!TestNotNull(TEXT("Each token door has its own visible lock"), Lock)) return false;
        TestTrue(TEXT("Possessing a relic does not erase an unopened lock"), DuelNorthInteractables::IsVisible(*Lock, State));
        if (Battle == 2)
            TestTrue(TEXT("The final reunion supplies the last exit prerequisite"), State.CompleteDialogue(EDuelNorthDialogue::FinalExit));
        State.OpenDoor(static_cast<EDuelNorthDoor>(Battle));
        TestFalse(TEXT("Unlocking removes the lock instead of leaving a closed padlock"), DuelNorthInteractables::IsVisible(*Lock, State));
    }
    return Errors.IsEmpty();
}
#endif
