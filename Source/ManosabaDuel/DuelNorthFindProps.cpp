#include "DuelNorthFinds.h"

#include "DuelGame.h"
#include "DuelGeometry.h"
#include "DuelNorthInteractables.h"
#include "DuelNorthLampworks.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "KismetProceduralMeshLibrary.h"
#include "ProceduralMeshComponent.h"

namespace DuelNorthFinds
{
namespace
{
    using DuelGeometry::EShape;
    enum EFinish : uint8 { Oak, DarkOak, Stone, Iron, Brass, Leather, Linen, Clay, Parchment, Salt, Glass, Wax, WaxImpression, FinishCount };

    UStaticMesh* DungeonProp(const TCHAR* Name)
    {
        return LoadObject<UStaticMesh>(nullptr,
            *FString::Printf(TEXT("/Game/MedievalDungeon/Meshes/Props/%s.%s"), Name, Name));
    }

    struct FPalette
    {
        UStaticMesh* Table = DungeonProp(TEXT("SM_Table"));
        UStaticMesh* Altar = DungeonProp(TEXT("SM_Alter"));
        UStaticMesh* Shapes[3] = {DuelGeometry::Mesh(EShape::Box), DuelGeometry::Mesh(EShape::Ball),
            DuelGeometry::Mesh(EShape::Tube)};
        UMaterialInterface* Materials[FinishCount];

        explicit FPalette(AActor* Owner)
        {
            const auto Paint = [Owner](float R, float G, float B, float Metal = 0.f, bool bCloth = false)
            {
                return DuelGeometry::ColorMaterial(Owner, FLinearColor(R, G, B), false, .82f, Metal, bCloth);
            };
            Materials[Oak] = DuelGeometry::ArtMaterial(TEXT("M_NorthOak"));
            Materials[DarkOak] = DuelGeometry::ArtMaterial(TEXT("MI_NorthDarkOak"));
            Materials[Stone] = DuelGeometry::ArtMaterial(TEXT("M_NorthDressedStone"));
            Materials[Iron] = Paint(.084f, .105f, .107f, .78f);
            Materials[Brass] = Paint(.48f, .30f, .11f, .66f);
            Materials[Leather] = Paint(.16f, .052f, .035f);
            Materials[Linen] = Paint(.21f, .255f, .30f, 0.f, true);
            Materials[Clay] = Paint(.36f, .19f, .095f);
            Materials[Parchment] = Paint(.61f, .45f, .24f);
            Materials[Salt] = Paint(.63f, .67f, .62f);
            Materials[Glass] = DuelGeometry::ArtMaterial(TEXT("M_WitnessGlass"));
            Materials[Wax] = Paint(.55f, .036f, .028f);
            Materials[WaxImpression] = Paint(.22f, .008f, .009f);
        }
    };

    struct FDisplay
    {
        AActor* Owner;
        const FFind& Find;
        const FPalette& Art;
        USceneComponent* Fixture;
        USceneComponent* Pickup;

        FDisplay(AActor* InOwner, USceneComponent* Parent, const FFind& InFind, const FPalette& InArt)
            : Owner(InOwner), Find(InFind), Art(InArt)
        {
            Fixture = NewObject<USceneComponent>(Owner);
            Fixture->SetupAttachment(Parent);
            Fixture->SetRelativeLocationAndRotation(Find.Position, Find.Rotation());
            Fixture->SetMobility(EComponentMobility::Static);
            Fixture->ComponentTags.Append({Find.Id, TEXT("NorthFindFixture")});
            Owner->AddInstanceComponent(Fixture);
            Fixture->RegisterComponent();

            Pickup = NewObject<USceneComponent>(Owner);
            Pickup->SetupAttachment(Fixture);
            Pickup->SetRelativeLocation(Find.LocalItem);
            Pickup->SetMobility(EComponentMobility::Static);
            Pickup->ComponentTags.Append({Find.Id, TEXT("NorthFindPickup")});
            Owner->AddInstanceComponent(Pickup);
            Pickup->RegisterComponent();

            auto* Body = NewObject<UBoxComponent>(Owner);
            Body->SetupAttachment(Fixture);
            Body->SetRelativeLocation(FVector(0, 0, Find.Size.Z * .5));
            Body->SetBoxExtent(Find.Size * .5);
            Body->SetMobility(EComponentMobility::Static);
            Body->SetCollisionObjectType(ECC_WorldStatic);
            Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Body->SetCollisionResponseToAllChannels(ECR_Ignore);
            Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
            Body->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
            Body->SetCanEverAffectNavigation(false);
            Body->ComponentTags.Append({Find.Id, TEXT("NorthFindBody")});
            Owner->AddInstanceComponent(Body);
            Body->RegisterComponent();
        }

        void Install(UMeshComponent* Part, const FTransform& Pose, bool bPickup, USceneComponent* Mount = nullptr) const
        {
            Part->SetupAttachment(Mount ? Mount : (bPickup ? Pickup : Fixture));
            Part->SetRelativeTransform(Pose);
            Part->SetMobility(Mount ? EComponentMobility::Movable : EComponentMobility::Static);
            Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Part->SetCanEverAffectNavigation(false);
            Part->SetCullDistance(2600.f);
            Owner->AddInstanceComponent(Part);
            Part->RegisterComponent();
        }

        void Fit(UStaticMesh* Mesh, UMaterialInterface* Finish, const FVector& Center,
            const FVector& Size, bool bPickup = false, const FQuat& Rotation = FQuat::Identity) const
        {
            if (!Mesh) return;
            const FBox Bounds = Mesh->GetBoundingBox();
            const FVector Scale = Size / Bounds.GetSize().ComponentMax(FVector(.01));
            const FVector At = Center - (bPickup ? Find.LocalItem : FVector::ZeroVector);
            auto* Part = NewObject<UStaticMeshComponent>(Owner);
            Part->SetStaticMesh(Mesh);
            if (Finish) Part->SetMaterial(0, Finish);
            Install(Part, FTransform(Rotation, At - Rotation.RotateVector(Scale * Bounds.GetCenter()), Scale), bPickup);
        }

        void Shape(EShape Mesh, EFinish Finish, const FVector& Center, const FVector& Size,
            bool bPickup = false, const FQuat& Rotation = FQuat::Identity) const
        {
            Fit(Art.Shapes[static_cast<uint8>(Mesh)], Art.Materials[Finish], Center, Size, bPickup, Rotation);
        }

        void Box(EFinish Finish, const FVector& Center, const FVector& Size,
            bool bPickup = false, const FQuat& Rotation = FQuat::Identity) const
        {
            Shape(EShape::Box, Finish, Center, Size, bPickup, Rotation);
        }

        void Table() const
        {
            if (Art.Table)
                Fit(Art.Table, nullptr, FVector(0, 0, Find.Size.Z * .5), Find.Size);
            else Bench(0, Find.Size.Y);
        }

        void Bench(double Y, double Depth) const
        {
            const double Z = Find.Size.Z, W = Find.Size.X;
            Box(Oak, FVector(0, Y, Z - 4), FVector(W, Depth, 8));
            Box(DarkOak, FVector(0, Y, Z - 11), FVector(W - 13, Depth - 4, 6));
            for (double X : {-W * .40, W * .40})
                for (double Foot : {Y - Depth * .33, Y + Depth * .33})
                    Box(DarkOak, FVector(X, Foot, (Z - 8) * .5), FVector(11, 10, Z - 8));
        }

        void Coffer(const FVector& Foot, const FVector& Size, EFinish Wood, bool bLid = true) const
        {
            const double W = Size.X, D = Size.Y, H = Size.Z;
            Box(Wood, Foot + FVector(0, 0, 2), FVector(W, D, 4));
            for (double X : {-W * .5 + 2, W * .5 - 2})
                Box(Wood, Foot + FVector(X, 0, H * .5), FVector(4, D, H));
            for (double Y : {-D * .5 + 2, D * .5 - 2})
                Box(Wood, Foot + FVector(0, Y, H * .5), FVector(W - 8, 4, H));
            for (double X : {-W * .31, W * .31})
                Box(Iron, Foot + FVector(X, D * .5 + 1, H * .5), FVector(6, 2, H));
            Box(Brass, Foot + FVector(0, D * .5 + 1.5, H - 6), FVector(10, 3, 9));
            if (bLid)
            {
                const FQuat Open(FVector::ForwardVector, FMath::DegreesToRadians(76.));
                const FVector Hinge = Foot + FVector(0, -D * .5 + 2, H);
                Box(Wood, Hinge + Open.RotateVector(FVector(0, D * .5, 0)), FVector(W, D, 4), false, Open);
                for (double X : {-W * .31, W * .31})
                    Box(Iron, Hinge + Open.RotateVector(FVector(X, D * .5, 2.4)),
                        FVector(6, D - 2, 2), false, Open);
            }
        }

        void Paper(const FVector& Top, const FVector2D& Size, bool bPickup = true) const
        {
            Box(Parchment, Top - FVector(0, 0, 1.3), FVector(Size, 1.8), bPickup);
            for (int32 Row = 0; Row < 4; ++Row)
                Box(DarkOak, Top + FVector(-Size.X * .13, (Row - 1.5) * Size.Y * .16, -.32),
                    FVector(Size.X * (.6 - Row * .05), .55, .12), bPickup);
        }

        void Crescent(const FVector& At) const
        {
            constexpr int32 Samples = 16;
            TArray<FVector> Points;
            TArray<FVector2D> UVs;
            TArray<int32> Indices;
            for (int32 I = 0; I <= Samples; ++I)
            {
                const double T = (double(I) / Samples - .5) * PI;
                const double X = 9. * FMath::Cos(T), Y = 9. * FMath::Sin(T);
                const double Lift = .23 * FMath::Abs(FMath::Sin(T));
                Points.Append({FVector(X, Y, Lift), FVector(X * .43, Y, Lift)});
                UVs.Append({FVector2D(1, double(I) / Samples), FVector2D(0, double(I) / Samples)});
                const int32 A = 2 * (I - 1);
                if (I > 0) Indices.Append({A, A + 1, A + 2, A + 1, A + 3, A + 2});
            }
            TArray<FVector> Normals;
            Normals.Init(FVector::UpVector, Points.Num());
            auto* Part = NewObject<UProceduralMeshComponent>(Owner);
            Part->CreateMeshSection_LinearColor(0, Points, Indices, Normals, UVs, {}, {}, false);
            Part->SetMaterial(0, Art.Materials[Parchment]);
            Install(Part, FTransform(At - Find.LocalItem), true);
        }

        void ClayBowl(const FVector& Foot, double Radius, double Height, bool bChipped,
            EFinish Finish = Clay, USceneComponent* Mount = nullptr) const
        {
            constexpr int32 Sides = 24;
            const FVector2D Profile[] = {{.3, 0}, {.56, 0}, {.83, .42}, {1., 1.}, {.84, 1.}, {.7, .45}, {.34, .2}, {0., .2}};
            TArray<FVector> Points;
            TArray<FVector2D> UVs;
            TArray<int32> Indices;
            for (int32 I = 0; I < UE_ARRAY_COUNT(Profile); ++I)
                for (int32 Side = 0; Side <= Sides; ++Side)
                {
                    const double Angle = 2. * PI * Side / Sides;
                    const bool bChip = bChipped && (Side == 5 || Side == 6) && (I == 3 || I == 4);
                    Points.Add(Foot + FVector(FMath::Cos(Angle) * Radius * Profile[I].X,
                        FMath::Sin(Angle) * Radius * Profile[I].X, Height * Profile[I].Y * (bChip ? .66 : 1.)));
                    UVs.Emplace(double(Side) / Sides, double(I) / (UE_ARRAY_COUNT(Profile) - 1));
                    if (Side == 0 || I == 0) continue;
                    const int32 C = Points.Num() - 1, B = C - Sides - 1;
                    Indices.Append({B - 1, C, B, B - 1, C - 1, C});
                }
            TArray<FVector> Normals;
            TArray<FProcMeshTangent> Tangents;
            UKismetProceduralMeshLibrary::CalculateTangentsForMesh(Points, Indices, UVs, Normals, Tangents);
            auto* Part = NewObject<UProceduralMeshComponent>(Owner);
            Part->CreateMeshSection_LinearColor(0, Points, Indices, Normals, UVs, {}, Tangents, false);
            Part->SetMaterial(0, Art.Materials[Finish]);
            Install(Part, FTransform::Identity, false, Mount);
        }

        void Weapon(const USceneComponent* Model, FVector At, double Scale,
            const FRotator& Rotation = FRotator::ZeroRotator) const
        {
            if (!Model) return;
            const FTransform InPickup(Rotation, At - Find.LocalItem, FVector(Scale));
            TArray<USceneComponent*> Pieces;
            Model->GetChildrenComponents(true, Pieces);
            for (USceneComponent* Source : Pieces)
            {
                UMeshComponent* Copy = nullptr;
                if (const auto* Static = Cast<UStaticMeshComponent>(Source))
                {
                    auto* Part = NewObject<UStaticMeshComponent>(Owner);
                    Part->SetStaticMesh(Static->GetStaticMesh());
                    Copy = Part;
                }
                else if (auto* Procedural = Cast<UProceduralMeshComponent>(Source))
                {
                    auto* Part = NewObject<UProceduralMeshComponent>(Owner);
                    for (int32 Index = 0; Index < Procedural->GetNumSections(); ++Index)
                        if (const FProcMeshSection* Section = Procedural->GetProcMeshSection(Index))
                            Part->SetProcMeshSection(Index, *Section);
                    Copy = Part;
                }
                if (!Copy) continue;
                const auto* SourceMesh = CastChecked<UMeshComponent>(Source);
                for (int32 Index = 0; Index < SourceMesh->GetNumMaterials(); ++Index)
                    Copy->SetMaterial(Index, SourceMesh->GetMaterial(Index));
                Copy->ComponentTags.Add(TEXT("NorthFindWeapon"));
                Install(Copy, Source->GetComponentTransform().GetRelativeTransform(Model->GetComponentTransform()) * InPickup, true);
            }
        }
    };

    void Dress(const FDisplay& D, const USceneComponent* Weapon, TObjectPtr<USceneComponent>& ChippedBowl)
    {
        const auto& Find = D.Find;
        const double H = Find.Size.Z;
        const FVector& Mark = Find.LocalItem;
        const FQuat Sideways = FRotator(90, 0, 0).Quaternion();
        switch (Find.Fixture)
        {
        case EFixture::RationTable:
        {
            D.Table();
            D.Box(Linen, FVector(4, -3, H + .7), FVector(132, 65, 1.4));
            D.ClayBowl(FVector(-41, 3, H + 1.4), 14, 10, false);
            D.ClayBowl(FVector(48, 3, H + 1.4), 10, 8, false);
            ChippedBowl = DuelGeometry::Joint(D.Owner, D.Fixture, {5, 3, H + 1.4});
            ChippedBowl->ComponentTags.Add(TEXT("NorthChippedRationBowl"));
            D.ClayBowl(FVector::ZeroVector, 13, 10, true, Clay, ChippedBowl.Get());
            auto* Tag = DuelGeometry::Joint(D.Owner, ChippedBowl.Get(), {0, 0, -.54});
            Tag->ComponentTags.Add(TEXT("NorthRationRoomTag03"));
            DuelGeometry::Shape(D.Owner, Tag, EShape::Box, FVector::ZeroVector, {12, 7, .8}, D.Art.Materials[Brass]);
            const auto Ink = [&](double X, double Y, double W, double L)
            {
                DuelGeometry::Shape(D.Owner, Tag, EShape::Box, {X, Y, -.42}, {W, L, .18}, D.Art.Materials[Iron]);
            };
            for (double Row : {-2.15, 2.15})
            {
                Ink(-2.65, Row, 2.9, .62);
                Ink(2.55, Row, 2.9, .62);
            }
            Ink(2.55, 0, 2.7, .62);
            Ink(-4.25, 0, .62, 4.8);
            Ink(-1.05, 0, .62, 4.8);
            Ink(4.05, 0, .62, 4.8);
            D.Paper(Mark, FVector2D(20, 14));
            D.Box(Oak, FVector(0, 31, 23), FVector(157, 18, 5));
            for (double X : {-67., 67.})
                D.Box(Iron, FVector(X, 20, 18), FVector(7, 28, 8));
            D.Weapon(Weapon, FVector(-41, 31, 29), .53);
            break;
        }
        case EFixture::CollationDesk:
        {
            D.Table();
            D.Box(Linen, FVector(0, -2, H + .6), FVector(132, 75, 1.2));
            const FVector Hold(-44, -6, H + 1.8);
            auto* ReturnBag = DuelGeometry::Joint(D.Owner, D.Fixture, Hold);
            ReturnBag->ComponentTags.Add(TEXT("NorthReturnedOriginals"));
            DuelGeometry::Shape(D.Owner, ReturnBag, EShape::Ball, {0, 0, 2}, {59, 47, 8}, D.Art.Materials[Linen]);
            DuelNorthLampworks::BuildEnvelope(D.Owner, ReturnBag, {0, 0, 4.9}, {53, 42},
                D.Art.Materials[Linen], D.Art.Materials[Leather]);
            for (double X : {-9., 11.})
            {
                auto* Original = DuelGeometry::Joint(D.Owner, ReturnBag, {X, 17, 6.2});
                Original->SetRelativeRotation(FRotator(0, X * .5, 0));
                DuelNorthLampworks::BuildEnvelope(D.Owner, Original, FVector::ZeroVector, {17, 20},
                    D.Art.Materials[Parchment], D.Art.Materials[Clay]);
            }
            DuelGeometry::Shape(D.Owner, ReturnBag, EShape::Box, {0, 10, 6.5}, {46, 5, 2}, D.Art.Materials[Leather]);
            DuelGeometry::Shape(D.Owner, ReturnBag, EShape::Box, {0, 12, 8}, {8, 7, 3}, D.Art.Materials[Brass]);
            DuelGeometry::Shape(D.Owner, ReturnBag, EShape::Tube, {0, 12, 9.6}, {2.1, 2.1, .4}, D.Art.Materials[Iron]);
            for (int32 I = 0; I < 2; ++I)
            {
                auto* Copy = DuelGeometry::Joint(D.Owner, D.Fixture, {11. + 39. * I, -7. - 7. * I, H + 3.0});
                Copy->SetRelativeRotation(FRotator(0, 176. + I * 11., 0));
                Copy->ComponentTags.Add(TEXT("NorthDeliveredFalseLetter"));
                DuelNorthLampworks::BuildEnvelope(D.Owner, Copy, FVector::ZeroVector, {34, 25},
                    D.Art.Materials[Parchment], D.Art.Materials[Clay]);
                DuelNorthLampworks::BuildWaxSeal(D.Owner, Copy, {0, -3, 1.6}, 6.9,
                    D.Art.Materials[Wax], D.Art.Materials[WaxImpression], true);
            }
            D.Paper(Mark, FVector2D(31, 18));
            D.Weapon(Weapon, FVector(-39, 25, 26), .68, FRotator(0, 90, -12));
            break;
        }
        case EFixture::Firebox:
            D.Table();
            D.Coffer(FVector(0, 0, H), FVector(128, 58, 26), Iron);
            D.Box(Leather, FVector(3, 0, H + 5), FVector(113, 44, 3));
            D.Box(Linen, FVector(-30, 1, H + 11), FVector(37, 34, 10), true, FRotator(0, 7, 0).Quaternion());
            D.Paper(Mark, FVector2D(24, 11));
            D.Weapon(Weapon, FVector(-32, -1, H + 13), .54);
            break;
        case EFixture::ToolRoll:
            D.Table();
            D.Box(Leather, FVector(16, 3, H + 2.1), FVector(101, 55, 4.2), false, FRotator(0, 4, 0).Quaternion());
            D.Shape(EShape::Tube, Leather, FVector(13, -20, H + 5), FVector(12, 12, 88), false, Sideways);
            D.Box(Linen, FVector(42, 19, H + 4), FVector(27, 14, 3));
            for (double X : {-47., -33.})
            {
                D.Shape(EShape::Tube, Brass, FVector(X, 22, H + 1.4), FVector(11, 11, 2.8));
                D.Box(Brass, FVector(X, 10, H + 1.4), FVector(3.5, 17, 2.8));
                D.Box(Brass, FVector(X + 2.5, 3, H + 1.4), FVector(8, 3.5, 2.8));
            }
            D.Paper(Mark, FVector2D(17, 19));
            D.Weapon(Weapon, FVector(-23, -1, H + 10), .52, FRotator(0, 10, 0));
            break;
        case EFixture::SaltCoffer:
            D.Box(Stone, FVector(0, 0, 13.5), FVector(Find.Size.X, Find.Size.Y, 27));
            D.Coffer(FVector(0, 0, 27), FVector(130, 62, H - 27), Oak);
            D.Box(Salt, FVector(0, 0, 33), FVector(121, 51, 5));
            D.Box(Stone, FVector(-42, 10, 41), FVector(22, 23, 12));
            D.Box(Stone, FVector(44, -4, 41), FVector(21, 24, 12));
            for (int32 I = 0; I < 5; ++I)
                D.Shape(EShape::Ball, Salt, FVector(-48 + I * 22, 20 - I, 38), FVector(21, 11, 9 - I));
            D.Shape(EShape::Ball, Salt, Mark - FVector(0, 0, 2), FVector(20, 5, 2.5), true);
            D.Shape(EShape::Ball, Salt, Mark + FVector(7, 0, -2.5), FVector(11, 4, 1.5), true);
            D.Weapon(Weapon, FVector(-36, 11, 49), .51, FRotator(0, -10, 0));
            break;
        case EFixture::SluiceShelf:
            D.Bench(0, Find.Size.Y);
            D.Box(Oak, FVector(0, -26, H + 8), FVector(137, 8, 17));
            D.Box(Leather, FVector(12, -3, H + 2.6), FVector(97, 18, 4), false, FRotator(0, -7, 0).Quaternion());
            D.Box(Leather, FVector(49, -4, H + 2.6), FVector(34, 13, 4), false, FRotator(0, 13, 0).Quaternion());
            for (double X : {-24., 17., 44.})
                D.Box(Iron, FVector(X, -2, H + 4), FVector(3, 19, 2));
            D.Paper(Mark, FVector2D(21, 16));
            D.Weapon(Weapon, FVector(-32, 6, H + 8), .52, FRotator(0, -8, 0));
            break;
        case EFixture::LetterTube:
            D.Bench(0, Find.Size.Y);
            D.Box(DarkOak, FVector(-12, 0, H + 3), FVector(73, 22, 6));
            D.Shape(EShape::Tube, Iron, FVector(-13, 3, H + 11), FVector(19, 19, 62), false, Sideways);
            for (double X : {-41., 12.})
                D.Shape(EShape::Tube, Brass, FVector(X, 3, H + 11), FVector(22, 22, 6), false, Sideways);
            D.Shape(EShape::Tube, Brass, FVector(39, -12, H + 3), FVector(18, 18, 6));
            D.Paper(Mark, FVector2D(26, 21));
            break;
        case EFixture::SewingCabinet:
            D.Box(DarkOak, FVector(0, 0, H * .5 - 2), Find.Size - FVector(8, 7, 4));
            D.Box(Oak, FVector(0, 0, H - 3), FVector(Find.Size.X, Find.Size.Y, 6));
            for (double Z : {18., 42., 64.})
            {
                D.Box(Oak, FVector(0, 25, Z), FVector(103, 4, 19));
                D.Box(Brass, FVector(0, 28, Z + 1), FVector(14, 4, 4));
            }
            D.Box(Linen, FVector(7, 6, H + 3), FVector(66, 37, 5), false, FRotator(0, -8, 0).Quaternion());
            D.Box(Linen, FVector(13, 25, H - 4), FVector(30, 3, 16));
            D.Box(Linen, Mark - FVector(0, 0, 2.2), FVector(26, 18, 3.2), true, FRotator(0, 5, 0).Quaternion());
            for (int32 Stitch = 0; Stitch < 8; ++Stitch)
                D.Box(Salt, Mark + FVector(-10 + Stitch * 2.8, (Stitch % 2) * 2 - 1, -.43),
                    FVector(2.1, .6, .3), true, FRotator(0, Stitch % 2 ? -28 : 28, 0).Quaternion());
            break;
        case EFixture::MirrorCase:
            D.Table();
            D.Coffer(FVector(8, 1, H), FVector(72, 55, 20), DarkOak);
            D.Box(Linen, FVector(13, 14, H + 12), FVector(42, 28, 15.5));
            D.Shape(EShape::Tube, Brass, FVector(4, -8, H + 27), FVector(42, 42, 4), false, FRotator(0, 0, -66).Quaternion());
            D.Shape(EShape::Tube, Glass, FVector(4, -5, H + 28), FVector(36, 36, 1.6), false, FRotator(0, 0, -66).Quaternion());
            D.Paper(Mark, FVector2D(29, 23));
            for (const FVector2D Star : {FVector2D(-8, -7), FVector2D(-1, -2), FVector2D(8, -4), FVector2D(7, 5)})
                D.Shape(EShape::Ball, Brass, Mark + FVector(Star, -.25), FVector(1.6, 1.6, .8), true);
            break;
        case EFixture::WitnessBench:
            D.Bench(-11., 45.);
            for (double X : {-68., 68.})
                D.Box(Oak, FVector(X, -28, 79), FVector(8, 8, 57));
            D.Box(DarkOak, FVector(0, -28, 96), FVector(157, 9, 20));
            for (double X : {-65., 65.})
                D.Box(Iron, FVector(X, 5, 17), FVector(7, 54, 5));
            D.Coffer(FVector(0, 10, 15), FVector(143, 49, 24), DarkOak, false);
            D.Paper(Mark, FVector2D(20, 9));
            D.Shape(EShape::Ball, Leather, Mark - FVector(0, 4.5, 2), FVector(8, 3, 6));
            D.Weapon(Weapon, FVector(-36, 15, 30), .55);
            break;
        case EFixture::VotiveAltar:
            if (D.Art.Altar) D.Fit(D.Art.Altar, nullptr, FVector(0, 0, H * .5), Find.Size);
            else D.Box(Stone, FVector(0, 0, H * .5), Find.Size);
            D.Box(Leather, FVector(0, 0, H + .65), FVector(86, 63, 1.3));
            D.Shape(EShape::Tube, Brass, FVector(16, 4, H + 3), FVector(20, 20, 4));
            D.ClayBowl(FVector(16, 4, H + 5), 15, 12, false, Brass);
            D.Shape(EShape::Tube, Iron, FVector(16, 4, H + 10), FVector(2, 2, 7));
            D.Box(Brass, Mark - FVector(0, 0, 1.1), FVector(20, 16, 1.5), true);
            D.Box(Leather, Mark - FVector(0, 0, .30), FVector(1, 11, .15), true);
            D.Box(Leather, Mark + FVector(0, -1.8, -.3), FVector(8, 1, .15), true);
            break;
        case EFixture::WaitingBench:
            D.Bench(0, Find.Size.Y);
            D.Box(Leather, FVector(8, 5, H + 2), FVector(64, 40, 4));
            D.Paper(FVector(-8, 5, H + 7), FVector2D(28, 36), false);
            D.Box(Parchment, FVector(23, 5, H + 5), FVector(27, 36, 2));
            for (int32 Day = 0; Day < 4; ++Day)
                D.Box(DarkOak, FVector(19 + Day * 3.7, -5, H + 6.15), FVector(1.7, 8, .15));
            D.Crescent(Mark - FVector(4.5, 0, 1.75));
            break;
        }
    }
}

void BuildProps(AActor* Owner, USceneComponent* Parent, TConstArrayView<TObjectPtr<USceneComponent>> Weapons,
    TMap<FName, TObjectPtr<UProceduralMeshComponent>>& Cues, TObjectPtr<USceneComponent>& ChippedBowl)
{
    const FPalette Palette(Owner);
    for (const FFind& Find : All())
    {
        const FDisplay Display(Owner, Parent, Find, Palette);
        const int32 Index = static_cast<int32>(Find.Weapon);
        Dress(Display, Weapons.IsValidIndex(Index) ? Weapons[Index].Get() : nullptr, ChippedBowl);
        auto* Cue = DuelNorthInteractables::BuildStoryCue(Owner, Display.Pickup, Find.Id, FVector::ZeroVector);
        if (Cue) Cue->SetRelativeScale3D(FVector(.79, .79, .56));
        Cues.Add(Find.Id, Cue);
    }
}

void ShowRationBowl(USceneComponent* Bowl, bool bTurned)
{
    if (!Bowl) return;
    Bowl->SetRelativeLocationAndRotation(FVector(5, bTurned ? 8 : 3, All()[0].Size.Z + (bTurned ? 14. : 1.4)),
        FRotator(0, 0, bTurned ? -118. : 0.));
}
}
