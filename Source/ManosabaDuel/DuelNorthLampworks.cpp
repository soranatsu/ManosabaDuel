#include "DuelNorthLampworks.h"

#include "DuelGeometry.h"
#include "DuelNorthLayout.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"

namespace DuelNorthLampworks
{
using DuelGeometry::EShape;
const TCHAR* Stations[] = {TEXT("BeaconChart"), TEXT("LensWorkshop"), TEXT("BeaconLetter"), TEXT("BeaconSwitch")};
int32 Index(FName Id)
{
    return MakeArrayView(Stations).IndexOfByPredicate([Id](const TCHAR* Name) { return Id == Name; });
}

FVector Surface(int32 I)
{
    const FVector Walk = DuelNorthLayout::FindLandmark(Stations[I])->Position;
    return Walk + FVector(0, I == 2 ? -81. : 81., 82.);
}

namespace
{
    enum EPart
    {
        FirstBag, RedBag, LastBag, ChippedRing, WholeRing, BagFlap, BagLetters, UnbrokenWax, BrokenWax,
        WrongImpression, MatchedImpression, PartCount
    };
    const FVector BagPlaces[] = {{56, 2, 0}, {0, 2, 0}, {-56, 2, 0}};
    const FVector RingPlaces[] = {{-37, 30, 0}, {-64, 30, 0}};
    const FVector TestWax(44, -24, 2.5);

    void Mark(AActor* Owner, USceneComponent* Parent, const FVector& Center, double Radius,
        UMaterialInterface* Ink, bool bChipped)
    {
        const auto Petal = [&](TConstArrayView<FVector2D> Points)
        {
            for (int32 I = 1; I < Points.Num(); ++I)
            {
                const auto Point = [&](int32 J)
                {
                    return Center - FVector(Points[J].X, Points[J].Y, 0) * Radius;
                };
                DuelGeometry::Rod(Owner, Parent, Point(I - 1), Point(I), .049f * Radius, Ink);
            }
        };
        Petal(MakeArrayView(TopPetal));
        Petal(MakeArrayView(LeftPetal));
        Petal(bChipped ? MakeArrayView(ChippedPetal) : MakeArrayView(RightPetal));
    }

    UProceduralMeshComponent* Triangle(AActor* Owner, USceneComponent* Parent, const FVector& A,
        const FVector& B, const FVector& C, UMaterialInterface* Material)
    {
        auto* Part = NewObject<UProceduralMeshComponent>(Owner);
        Part->SetupAttachment(Parent);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetCanEverAffectNavigation(false);
        Part->CreateMeshSection_LinearColor(0, {A, B, C}, {0, 1, 2, 2, 1, 0},
            {FVector::UpVector, FVector::UpVector, FVector::UpVector}, {{0, 0}, {1, 0}, {.5, 1}}, {}, {}, false);
        Part->SetMaterial(0, Material);
        Owner->AddInstanceComponent(Part);
        Part->RegisterComponent();
        return Part;
    }
}

void BuildEnvelope(AActor* Owner, USceneComponent* Parent, const FVector& Center, const FVector2D& Size,
    UMaterialInterface* Paper, UMaterialInterface* Fold)
{
    DuelGeometry::Shape(Owner, Parent, EShape::Box, Center, FVector(Size, 1.6), Paper);
    const FVector A = Center + FVector(-Size.X * .49, Size.Y * .47, .87);
    const FVector B = Center + FVector(Size.X * .49, Size.Y * .47, .87);
    const FVector Tip = Center + FVector(0, -Size.Y * .09, 1.05);
    Triangle(Owner, Parent, A, B, Tip, Fold);
    DuelGeometry::Rod(Owner, Parent, Center + FVector(-Size.X * .49, -Size.Y * .47, .88), Tip, .22f, Fold);
    DuelGeometry::Rod(Owner, Parent, Center + FVector(Size.X * .49, -Size.Y * .47, .88), Tip, .22f, Fold);
}

void BuildWaxSeal(AActor* Owner, USceneComponent* Parent, const FVector& Center, double Radius,
    UMaterialInterface* Wax, UMaterialInterface* Impression, bool bChipped)
{
    DuelGeometry::Shape(Owner, Parent, EShape::Tube, Center, {2 * Radius, 2 * Radius, 1.75}, Wax);
    DuelGeometry::Shape(Owner, Parent, EShape::Tube, Center + FVector(0, 0, .8), {1.70 * Radius, 1.70 * Radius, .66}, Wax);
    Mark(Owner, Parent, Center + FVector(0, 0, 1.15), .72 * Radius, Impression, bChipped);
}

void Build(AActor* Owner, USceneComponent* Parent, TArray<TObjectPtr<USceneComponent>>& Parts)
{
    auto* Wood = DuelGeometry::ColorMaterial(Owner, FLinearColor(.17f, .083f, .042f));
    auto* Brass = DuelGeometry::ColorMaterial(Owner, FLinearColor(.63f, .39f, .14f), false, .45f, .68f);
    auto* Iron = DuelGeometry::ColorMaterial(Owner, FLinearColor(.09f, .095f, .112f), false, .52f, .71f);
    auto* Paper = DuelGeometry::ColorMaterial(Owner, FLinearColor(.82f, .69f, .48f));
    auto* Fold = DuelGeometry::ColorMaterial(Owner, FLinearColor(.55f, .42f, .27f));
    auto* Ink = DuelGeometry::ColorMaterial(Owner, FLinearColor(.077f, .063f, .053f));
    auto* Cloth = DuelGeometry::ColorMaterial(Owner, FLinearColor(.36f, .26f, .16f), false, .96f, 0.f, true);
    auto* Leather = DuelGeometry::ColorMaterial(Owner, FLinearColor(.14f, .082f, .037f));
    auto* Wax = DuelGeometry::ColorMaterial(Owner, FLinearColor(.55f, .034f, .03f), false, .52f);
    auto* Relief = DuelGeometry::ColorMaterial(Owner, FLinearColor(.27f, .013f, .014f), false, .71f);

    auto Mesh = [](const TCHAR* Name)
    {
        return LoadObject<UStaticMesh>(nullptr,
            *FString::Printf(TEXT("/Game/MedievalDungeon/Meshes/Props/%s.%s"), Name, Name));
    };
    auto* Table = Mesh(TEXT("SM_Table"));
    auto* Jar = Mesh(TEXT("SM_Pot_B_Complete"));
    auto* Bed = Mesh(TEXT("SM_Bed"));
    auto* Candle = Mesh(TEXT("SM_Candle"));
    auto* IronStand = Mesh(TEXT("SM_CandleStick_Iron"));
    const auto Furnish = [&](UStaticMesh* Model, const FVector& Centre, const FVector& Dims, FName Room)
    {
        if (!Model) return;
        auto* Part = NewObject<UStaticMeshComponent>(Owner);
        const auto Bounds = Model->GetBounds();
        const FVector Scale = Dims / (Bounds.BoxExtent * 2.);
        Part->SetupAttachment(Parent);
        Part->SetMobility(EComponentMobility::Static);
        Part->SetStaticMesh(Model);
        Part->SetRelativeScale3D(Scale);
        Part->SetRelativeLocation(Centre - Scale * Bounds.Origin);
        Part->SetCanEverAffectNavigation(false);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->ComponentTags.Append({TEXT("NorthEvidenceFurniture"), Room});
        Owner->AddInstanceComponent(Part);
        Part->RegisterComponent();
    };
    const auto Prop = [&](USceneComponent* Group, EShape S, const FVector& At, const FVector& Size,
        UMaterialInterface* Mat, FRotator Rot = FRotator::ZeroRotator)
    {
        return DuelGeometry::Shape(Owner, Group, S, At, Size, Mat, Rot);
    };
    const auto Body = [&](const FVector& Center, const FVector& Size)
    {
        auto* Part = Prop(Parent, EShape::Box, Center, Size, Wood);
        Part->SetHiddenInGame(true);
        Part->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Part->SetCollisionResponseToAllChannels(ECR_Ignore);
        Part->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
        Part->SetCastShadow(false);
        return Part;
    };
    const auto Page = [&](USceneComponent* Group, const FVector& Center, const FVector2D& Size)
    {
        Prop(Group, EShape::Box, Center, FVector(Size, .7), Paper);
        for (int32 I = 0; I < 5; ++I)
            Prop(Group, EShape::Box, Center + FVector(-Size.X * .06, (I - 2) * Size.Y * .13, .37),
                FVector(Size.X * (.73 - .06 * (I % 3)), .63, .10), Ink);
    };

    const FName Rooms[] = {TEXT("BeaconChartRoom"), TEXT("BeaconWorkshop"), TEXT("BeaconCabin"), TEXT("BeaconOptics")};
    for (int32 I = 0; I < UE_ARRAY_COUNT(Rooms); ++I)
    {
        const FVector Bench = Surface(I);
        Furnish(Table, Bench - FVector(0, 0, 42), {172, 82, 80}, Rooms[I]);
        Body(Bench - FVector(0, 0, 41), {166, 78, 79})->ComponentTags.Append(
            {TEXT("NorthBeaconTableBody"), TEXT("NorthEvidenceTableBody"), FName(Stations[I])});
    }

    auto* Desk = DuelGeometry::Joint(Owner, Parent, Surface(0));
    Desk->ComponentTags.Add(TEXT("NorthDutyLedger"));
    Prop(Desk, EShape::Box, {-14, 1, .4}, {101, 52, 3.3}, Leather);
    Page(Desk, {-38, 1, 2.5}, {45, 49});
    Page(Desk, {9, 1, 2.8}, {45, 49});
    Prop(Desk, EShape::Box, {-14, 1, 3.0}, {2, 46, .6}, Leather);
    Prop(Desk, EShape::Tube, {61, 5, 5.5}, {12, 12, 13}, Iron);
    DuelGeometry::Rod(Owner, Desk, {62, 5, 10}, {51, 9, 31}, .8f, Fold);

    Desk = DuelGeometry::Joint(Owner, Parent, Surface(1));
    Desk->ComponentTags.Add(TEXT("NorthObservationRecord"));
    auto* Notebook = DuelGeometry::Joint(Owner, Desk, {-24, -4, 0});
    Notebook->SetRelativeRotation(FRotator(0, -9, 0));
    Prop(Notebook, EShape::Box, {0, 0, .9}, {63, 51, 4.2}, Wood);
    Page(Notebook, {0, 0, 3.4}, {54, 44});
    for (int32 I = 0; I < 4; ++I)
        Prop(Notebook, EShape::Box, {13., (1 - I) * 6., 3.9}, {3, 3, .4}, Wax);
    Prop(Desk, EShape::Box, {40, 8, 4}, {44, 42, 9}, Wood);
    for (int32 I = 0; I < 2; ++I)
        BuildEnvelope(Owner, Desk, {36. + 7. * I, 7. - 7. * I, 9. + 2.5 * I}, {33, 24}, Paper, Fold);

    Desk = DuelGeometry::Joint(Owner, Parent, Surface(2));
    Desk->ComponentTags.Add(TEXT("NorthSnowOrder"));
    auto* Order = DuelGeometry::Joint(Owner, Desk, {12, -4, 0});
    Order->SetRelativeRotation(FRotator(0, 167, 0));
    BuildEnvelope(Owner, Order, {-13, 3, .7}, {46, 32}, Fold, Leather);
    Page(Order, {9, -3, 2}, {41, 36});
    BuildWaxSeal(Owner, Order, {23, -12, 3.2}, 5.3, Wax, Relief, true);
    Furnish(Jar, Surface(2) + FVector(-46, -16, 12), {18, 18, 26}, Rooms[2]);

    Desk = DuelGeometry::Joint(Owner, Parent, Surface(3));
    Desk->ComponentTags.Add(TEXT("NorthMailEvidenceDesk"));
    Parts.SetNum(PartCount);
    const auto Rig = [&](EPart Part, FName Tag, const FVector& At = FVector::ZeroVector, FName Identity = NAME_None)
    {
        USceneComponent* Mount = DuelGeometry::Joint(Owner, Part >= BagFlap && Part <= BrokenWax ? Parts[RedBag].Get() : Desk, At);
        Mount->ComponentTags.Add(Tag);
        if (!Identity.IsNone()) Mount->ComponentTags.Add(Identity);
        Parts[Part] = Mount;
        return Mount;
    };
    for (int32 I = 0; I < UE_ARRAY_COUNT(BagPlaces); ++I)
    {
        USceneComponent* Bag = Rig(static_cast<EPart>(FirstBag + I), TEXT("NorthEvidenceBag"), BagPlaces[I],
            FName(*FString::Printf(TEXT("EvidenceBag%d"), I)));
        auto* BagSurface = I == 2 ? Iron : Cloth;
        auto* Binding = I == 2 ? Wood : Leather;
        Prop(Bag, EShape::Box, {0, 0, 2}, {45, 32, 5}, BagSurface, FRotator(0, I == 0 ? -6 : 0, 0));
        Prop(Bag, EShape::Ball, {0, 0, 2.3}, {49, 35, 8.7}, BagSurface);
        if (I != 1) BuildEnvelope(Owner, Bag, {0, 1, 5.6}, {43, 30}, BagSurface, Binding);
        const FVector Seam(-20, -11, 5.8);
        DuelGeometry::Rod(Owner, Bag, Seam, Seam + FVector(40, 0, 0), .44f, Binding);
        if (I == 0)
        {
            for (double X : {-4., 4.})
                DuelGeometry::Rod(Owner, Bag, {X + 6, 15, 6.4}, {X - 6, -15, 6.4}, .62f, Fold);
        }
        if (I == 2)
            Prop(Bag, EShape::Box, {0, -4, 7}, {14, 8, 1.5}, Brass);
    }

    USceneComponent* Flap = Rig(BagFlap, TEXT("NorthEvidenceBagFlap"), {0, 15, 7.25});
    Triangle(Owner, Flap, {-21, 0, 0}, {21, 0, 0}, {0, -23, 0}, Leather);
    USceneComponent* Letters = Rig(BagLetters, TEXT("NorthSeizedLetters"), {0, -6, 8});
    for (int32 I = 0; I < 4; ++I)
        Page(Letters, {-.8 * I, -3.5 * I, 1.1 * I}, {29, 16});
    BuildWaxSeal(Owner, Rig(UnbrokenWax, TEXT("NorthSeizedMailIntactSeal"), {0, -7, 7.8}), FVector::ZeroVector,
        7.5, Wax, Relief, true);
    USceneComponent* Fragments = Rig(BrokenWax, TEXT("NorthSeizedMailBrokenSeal"), {-15, -9, 6});
    Prop(Fragments, EShape::Box, {-2, -1, .3}, {7, 6, 1.1}, Wax, FRotator(0, -28, 0));
    Prop(Fragments, EShape::Box, {6, -5, 1}, {5, 4, 1.1}, Wax, FRotator(0, 26, 0));

    // Stationery and separate comparison wax keep the original seals intact during testing.
    Prop(Desk, EShape::Box, {32, -24, 1}, {54, 28, 2}, Paper);
    Prop(Desk, EShape::Tube, TestWax, {19, 19, 1.6}, Wax);
    BuildWaxSeal(Owner, Desk, {19, -24, 2.8}, 9.0, Wax, Relief, true);
    BuildWaxSeal(Owner, Rig(WrongImpression, TEXT("NorthEvidenceWrongImpression"), TestWax), FVector(0, 0, .4),
        9.5, Wax, Relief, false);
    BuildWaxSeal(Owner, Rig(MatchedImpression, TEXT("NorthEvidenceMatchedImpression"), TestWax), FVector(0, 0, .4),
        9.5, Wax, Relief, true);
    for (int32 I = 0; I < UE_ARRAY_COUNT(RingPlaces); ++I)
    {
        auto* Ring = Rig(static_cast<EPart>(ChippedRing + I), TEXT("NorthEvidenceRing"), RingPlaces[I],
            FName(*FString::Printf(TEXT("EvidenceRing%d"), I)));
        for (int32 Segment = 0; Segment < 12; ++Segment)
        {
            const auto Point = [](int32 J)
            {
                const double Angle = 2. * PI * J / 12;
                return FVector(FMath::Cos(Angle) * 5.7, 0, 6 + FMath::Sin(Angle) * 5.7);
            };
            DuelGeometry::Rod(Owner, Ring, Point(Segment), Point(Segment + 1), 1.14f, Brass);
        }
        Prop(Ring, EShape::Tube, {0, 0, 12.2}, {17, 17, 3.5}, I ? Iron : Brass);
        Mark(Owner, Ring, {0, 0, 14.0}, 6.2, Ink, I == 0);
    }

    Furnish(Bed, {-1355, -3225, 2308}, {126, 203, 60}, Rooms[2]);
    Body({-1355, -3225, 2307}, {118, 199, 58});
    Furnish(Table, {-2050, -3325, 2314}, {118, 60, 72}, Rooms[2]);
    Furnish(IronStand, {-2081, -3320, 2368}, {17, 17, 38}, Rooms[2]);
    Furnish(Candle, {-2081, -3320, 2390}, {9, 9, 15}, Rooms[2]);
    Furnish(Table, {-1130, -1810, 2317}, {167, 67, 78}, Rooms[1]);
    auto* Shelf = DuelGeometry::Joint(Owner, Parent, {-1130, -1810, 2357});
    for (int32 I = 0; I < 3; ++I)
    {
        Prop(Shelf, EShape::Box, {-48. + I * 46., 0, 6.4}, {36, 44, 13}, Wood);
        BuildEnvelope(Owner, Shelf, {-51. + I * 46., 0, 14}, {29, 20}, Fold, Leather);
    }
    Furnish(Table, {-1995, -1830, 2315}, {202, 68, 74}, TEXT("BeaconCrossway"));
    for (int32 I = 0; I < 3; ++I)
        Furnish(Jar, {-2055. + I * 52., -1830., 2373.}, {29, 25, 43}, TEXT("BeaconCrossway"));
    Furnish(Jar, {-1940, -1864, 2301}, {47, 46, 46}, TEXT("BeaconCrossway"));
    Furnish(Jar, {-740, -2640, 2300}, {45, 40, 44}, TEXT("BeaconGallery"));
    Furnish(Jar, {-660, -2650, 2310}, {64, 56, 64}, TEXT("BeaconGallery"));
    ShowEvidenceTable(Parts, ETableView::Idle);
}

void ShowEvidenceTable(TArray<TObjectPtr<USceneComponent>>& Parts, ETableView View, int32 Choice)
{
    if (Parts.Num() != PartCount) return;
    const bool bOpen = View == ETableView::Open;
    const bool bBagOnTray = bOpen || View == ETableView::SelectRing || View == ETableView::WrongRing;
    for (int32 I = 0; I < UE_ARRAY_COUNT(BagPlaces); ++I)
    {
        const double Lift = View == ETableView::SelectBag && Choice == I ? 7. : 0.;
        Parts[FirstBag + I]->SetRelativeLocation(bBagOnTray && I == 1 ? FVector(0, -18, 2) :
            BagPlaces[I] + FVector(0, 0, Lift));
    }
    for (int32 I = 0; I < UE_ARRAY_COUNT(RingPlaces); ++I)
    {
        const bool bSelected = View == ETableView::SelectRing && Choice == I;
        const bool bPressed = (View == ETableView::WrongRing && I == 1) || (bOpen && I == 0);
        Parts[ChippedRing + I]->SetRelativeLocation(bPressed ? TestWax + FVector(13, 4, 0) :
            bSelected ? TestWax + FVector(0, 0, 7) : RingPlaces[I]);
    }
    Parts[BagFlap]->SetRelativeRotation(FRotator(0, 0, bOpen ? 103. : 0.));
    Parts[BagLetters]->SetHiddenInGame(!bOpen, true);
    Parts[UnbrokenWax]->SetHiddenInGame(bOpen, true);
    Parts[BrokenWax]->SetHiddenInGame(!bOpen, true);
    Parts[WrongImpression]->SetHiddenInGame(View != ETableView::WrongRing, true);
    Parts[MatchedImpression]->SetHiddenInGame(!bOpen, true);
}
}
