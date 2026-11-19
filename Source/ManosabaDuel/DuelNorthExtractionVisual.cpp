#include "DuelNorthExtractionVisual.h"

#include "DuelGeometry.h"
#include "Camera/CameraTypes.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

namespace DuelNorthExtractionVisual
{
namespace
{
    const FRotator HoverRotation(0, -90, 0);
    const FVector DoorSocket = FVector(0, -110, -35) * AirframeScale;
    const FVector Hover = BoardingPoint - HoverRotation.RotateVector(DoorSocket);
    constexpr double TurnRadius = 10200.;
    constexpr int32 RungCount = 16;
    constexpr double RungWidth = 91.;
    enum EFinish { Enamel, Rescue, Rubber, Canopy, Seat, Alloy, Hemp, CabinGlow, RedGlow, GreenGlow, FinishCount };

    struct FSurface
    {
        TArray<FVector> Vertices, Normals;
        TArray<FVector2D> UVs;
        TArray<int32> Indices;
        TArray<FProcMeshTangent> Tangents;

        void Face(TArrayView<const FVector> Points, const FVector& Out)
        {
            const FVector Along = (Points[1] - Points[0]).GetSafeNormal();
            FVector Normal = FVector::CrossProduct(Along, Points[2] - Points[0]).GetSafeNormal();
            if (Normal.IsNearlyZero()) return;
            const bool bFlip = FVector::DotProduct(Normal, Out) < 0;
            if (bFlip) Normal = -Normal;
            const FVector Across = FVector::CrossProduct(Normal, Along);
            const int32 First = Vertices.Num();
            for (const FVector& P : Points)
            {
                Vertices.Add(P);
                Normals.Add(Normal);
                UVs.Emplace(FVector::DotProduct(P - Points[0], Along) / 100., FVector::DotProduct(P - Points[0], Across) / 100.);
                Tangents.Emplace(Along, false);
            }
            for (int32 I = 1; I + 1 < Points.Num(); ++I)
                Indices.Append({First, First + (bFlip ? I : I + 1), First + (bFlip ? I + 1 : I)});
        }

        void Quad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Out)
        {
            const FVector Points[] = {A, B, C, D};
            Face(Points, Out);
        }

        void Prism(TArrayView<const FVector> Base, const FVector& Thickness)
        {
            TArray<FVector, TInlineAllocator<12>> Top;
            FVector Centre = Thickness * .5;
            for (const FVector& P : Base)
            {
                Centre += P / Base.Num();
                Top.Add(P + Thickness);
            }
            Face(Base, -Thickness);
            Face(Top, Thickness);
            for (int32 I = 0; I < Base.Num(); ++I)
            {
                const int32 J = (I + 1) % Base.Num();
                Quad(Base[I], Base[J], Top[J], Top[I], (Base[I] + Base[J] + Top[I] + Top[J]) * .25 - Centre);
            }
        }

        void Box(const FVector& At, const FVector& Size)
        {
            const FVector Lo = At - Size * .5, Hi = At + Size * .5;
            const FVector Base[] = {{Lo.X, Lo.Y, Lo.Z}, {Hi.X, Lo.Y, Lo.Z}, {Hi.X, Hi.Y, Lo.Z}, {Lo.X, Hi.Y, Lo.Z}};
            Prism(Base, FVector(0, 0, Size.Z));
        }

        void Tube(const FVector& A, const FVector& B, double RadiusA, double RadiusB, int32 Sides = 10)
        {
            const FVector Axis = (B - A).GetSafeNormal();
            FVector U = FVector::CrossProduct(Axis, FVector::UpVector).GetSafeNormal();
            if (U.IsNearlyZero()) U = FVector::RightVector;
            const FVector V = FVector::CrossProduct(Axis, U);
            for (int32 Side = 0; Side < Sides; ++Side)
            {
                const double T = Side * 2. * UE_DOUBLE_PI / Sides, T1 = (Side + 1) * 2. * UE_DOUBLE_PI / Sides;
                const FVector D = U * FMath::Cos(T) + V * FMath::Sin(T);
                const FVector E = U * FMath::Cos(T1) + V * FMath::Sin(T1);
                const FVector P = A + D * RadiusA, Q = A + E * RadiusA;
                const FVector R = B + D * RadiusB, S = B + E * RadiusB;
                Quad(P, Q, S, R, D + E);
                const FVector CapA[] = {A, P, Q}, CapB[] = {B, R, S};
                Face(CapA, -Axis);
                Face(CapB, Axis);
            }
        }

        void Rod(const FVector& A, const FVector& B, double Radius) { Tube(A, B, Radius, Radius); }
    };

    template <class T>
    T* Attach(AActor* Owner, USceneComponent* Parent, const FVector& At = FVector::ZeroVector)
    {
        auto* C = NewObject<T>(Owner);
        C->SetupAttachment(Parent);
        C->SetMobility(EComponentMobility::Movable);
        C->SetRelativeLocation(At);
        Owner->AddInstanceComponent(C);
        return C;
    }

    USceneComponent* Joint(AActor* Owner, USceneComponent* Parent, const FVector& At = FVector::ZeroVector)
    {
        auto* C = Attach<USceneComponent>(Owner, Parent, At);
        C->RegisterComponent();
        return C;
    }

    UProceduralMeshComponent* Install(AActor* Owner, USceneComponent* Parent, const FSurface (&Sections)[FinishCount],
        UMaterialInterface* const (&Materials)[FinishCount], FName Tag, const FVector& At = FVector::ZeroVector,
        EComponentMobility::Type Mobility = EComponentMobility::Movable)
    {
        auto* C = Attach<UProceduralMeshComponent>(Owner, Parent, At);
        C->SetMobility(Mobility);
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->SetCanEverAffectNavigation(false);
        C->SetCastShadow(true);
        C->ComponentTags.Add(Tag);
        int32 Index = 0;
        for (int32 I = 0; I < FinishCount; ++I)
        {
            const FSurface& S = Sections[I];
            if (S.Indices.IsEmpty()) continue;
            C->CreateMeshSection_LinearColor(Index, S.Vertices, S.Indices, S.Normals, S.UVs, TArray<FLinearColor>(), S.Tangents, false);
            C->SetMaterial(Index++, Materials[I]);
        }
        C->RegisterComponent();
        return C;
    }

    UStaticMeshComponent* RescueMesh(AActor* Owner, USceneComponent* Parent, const TCHAR* Name, FName Tag)
    {
        const FString Path=FString::Printf(TEXT("/Game/DuelArt/NorthTower/Rescue/Meshes/%s.%s"),Name,Name);
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,*Path);
        if(!Mesh) { UE_LOG(LogTemp,Error,TEXT("Missing imported CC0 rescue mesh: %s"),*Path); return nullptr; }
        auto* C=Attach<UStaticMeshComponent>(Owner,Parent);
        C->SetStaticMesh(Mesh);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->SetCanEverAffectNavigation(false);C->SetCastShadow(true);C->ComponentTags.Add(Tag);
        C->RegisterComponent();return C;
    }

    double Extension(EPhase Phase, float T)
    {
        if (Phase == EPhase::Hold) return 1.;
        if (Phase == EPhase::Deploy) return FMath::SmoothStep(0., 1., double(T));
        if (Phase == EPhase::Retract) return 1. - FMath::SmoothStep(0., 1., double(T));
        return 0.;
    }
}

FTransform FlightTransform(EPhase Phase, float Progress01)
{
    const double T = FMath::Clamp(double(Progress01), 0., 1.);
    const bool bApproach = Phase == EPhase::Approach;
    if ((!bApproach && Phase != EPhase::Departure) || (bApproach && T >= 1.) || (!bApproach && T <= 0.))
        return FTransform(HoverRotation, Hover);
    // The same circle provides a tangent-continuous route in and out of the hover.
    const double Travel = bApproach ? T + T * T * (1. - T) : T * T * (2. - T);
    const double Angle = bApproach ? FMath::Lerp(FMath::DegreesToRadians(68.), UE_DOUBLE_PI, Travel)
        : UE_DOUBLE_PI + FMath::DegreesToRadians(113.) * Travel;
    const double Height = bApproach ? FMath::Lerp(7270., Hover.Z, Travel) : FMath::Lerp(Hover.Z, 8100., Travel);
    const FVector Centre = Hover + FVector(TurnRadius * (1. + FMath::Cos(Angle)), TurnRadius * FMath::Sin(Angle), Height - Hover.Z);
    const FVector Tangent(-FMath::Sin(Angle), FMath::Cos(Angle), 0);
    const double Bank = 10.5 * FMath::Sin(UE_DOUBLE_PI * (bApproach ? .12 + .88 * T : .91 * T));
    return FTransform(FRotator(-Bank * .41, Tangent.Rotation().Yaw, Bank), Centre);
}

FMinimalViewInfo FlightCamera(const FTransform& Vehicle, float DepartureProgress)
{
    const FVector Position = Vehicle.GetLocation();
    const FVector VehicleFocus = Position + FVector(0, 0, 55);
    const FRotator Heading(0., Vehicle.Rotator().Yaw, 0.);
    const FVector ChaseOffset = Heading.RotateVector(FVector(-1600, -1150, 570));
    FMinimalViewInfo View;
    View.Location = Position + ChaseOffset;
    View.FOV = 56.f;
    FVector Direction = VehicleFocus - View.Location;
    if (DepartureProgress > 0.f)
    {
        // Swing around the aircraft before settling on the prison beyond it.
        const FVector PrisonFocus(900, 0, 2800);
        const FVector Seaward = (Position - PrisonFocus).GetSafeNormal2D();
        const FVector Side = FVector::CrossProduct(FVector::UpVector, Seaward);
        const FVector LookBackOffset = Seaward * 3630. + Side * 1110. + FVector(0, 0, 950);
        const double Orbit = FMath::SmoothStep(0., .43, double(DepartureProgress));
        const FVector Start = ChaseOffset.GetSafeNormal();
        const FQuat Swing = FQuat::FindBetweenNormals(Start, LookBackOffset.GetSafeNormal());
        const FVector OrbitDirection = FQuat::Slerp(FQuat::Identity, Swing, Orbit).RotateVector(Start);
        View.Location = Position + OrbitDirection * FMath::Lerp(ChaseOffset.Size(), LookBackOffset.Size(), Orbit);

        const FVector AircraftAim = (VehicleFocus - View.Location).GetSafeNormal();
        const FVector PrisonAim = (PrisonFocus - View.Location).GetSafeNormal();
        const double Look = FMath::SmoothStep(.19, .43, double(DepartureProgress));
        const FQuat Pan = FQuat::FindBetweenNormals(AircraftAim, PrisonAim);
        Direction = FQuat::Slerp(FQuat::Identity, Pan, Look).RotateVector(AircraftAim);
        View.FOV = FMath::Lerp(56.f, 64.f, float(Orbit));
    }
    View.Rotation = Direction.Rotation();
    return View;
}

USceneComponent* BuildCallMarker(AActor* Owner, USceneComponent* Parent)
{
    if (!Owner || !Parent) return nullptr;
    FSurface M[FinishCount];
    M[Enamel].Box(FVector(0, 0, 9), FVector(66, 87, 18));
    M[Enamel].Box(FVector(0, 0, 40), FVector(47, 64, 45));
    M[Enamel].Box(FVector(0, 0, 65), FVector(63, 81, 9));
    M[Rubber].Box(FVector(1, 0, 84), FVector(46, 71, 29));
    M[Rescue].Box(FVector(1, 0, 82), FVector(49, 73, 13));
    M[Rubber].Box(FVector(-25, 0, 84), FVector(6, 65, 26));
    M[CabinGlow].Box(FVector(-29, -9, 86), FVector(2, 25, 8));
    for (double Y : {9., 23.}) M[Alloy].Tube(FVector(-26, Y, 85), FVector(-32, Y, 85), 5, 5, 8);
    M[Alloy].Rod(FVector(13, 22, 93), FVector(17, 24, 152), 2.6);
    M[Alloy].Rod(FVector(17, 18, 122), FVector(17, 30, 122), 1.7);
    M[Rubber].Rod(FVector(14, -22, 98), FVector(14, 10, 98), 3.3);
    M[Alloy].Rod(FVector(18,25,98),FVector(18,25,228),3.4);
    M[Rescue].Tube(FVector(18,25,226),FVector(18,25,236),10,9,12);
    M[CabinGlow].Tube(FVector(18,25,236),FVector(18,25,248),9,7,12);
    const FVector Signal=LadderFoot-(PickupMark+FVector(80,65,0))+FVector(0,0,3);
    // A rescue winch target sits in the open garden aisle, well inside the parapets.
    for(int32 I=0;I<36;++I)
    {
        if(I%6==0)continue;
        const double A=I*2.*UE_DOUBLE_PI/36.,B=(I+.72)*2.*UE_DOUBLE_PI/36.;
        M[Enamel].Rod(Signal+FVector(230*FMath::Cos(A),230*FMath::Sin(A),0),
            Signal+FVector(230*FMath::Cos(B),230*FMath::Sin(B),0),3.8);
    }
    for(double X:{-300.,300.})for(double Y:{-300.,300.})
    {
        const FVector Lamp=Signal+FVector(X,Y,-3);
        M[Enamel].Tube(Lamp,Lamp+FVector(0,0,24),17,13,12);
        M[CabinGlow].Tube(Lamp+FVector(0,0,24),Lamp+FVector(0,0,36),11,9,12);
        M[Alloy].Tube(Lamp+FVector(0,0,36),Lamp+FVector(0,0,39),15,14,12);
    }


    using DuelGeometry::ColorMaterial;
    UMaterialInterface* Materials[FinishCount] = {};
    Materials[Enamel] = DuelGeometry::ArtMaterial(TEXT("M_NorthDressedStone"));
    Materials[Rubber] = ColorMaterial(Owner, FLinearColor(.042f,.061f,.055f), false, .58f, .28f);
    Materials[Rescue] = ColorMaterial(Owner, FLinearColor(.45f,.17f,.033f), false, .53f, .31f);
    Materials[Alloy] = ColorMaterial(Owner, FLinearColor(.22f,.25f,.23f), false, .39f, .66f);
    Materials[CabinGlow] = ColorMaterial(Owner, FLinearColor(2.45f,1.08f,.23f), true);
    return Install(Owner, Parent, M, Materials, TEXT("NorthExtractionRadio"), PickupMark + FVector(80, 65, 0), EComponentMobility::Static);
}

FVisual Build(AActor* Owner, USceneComponent* Parent)
{
    if (!Owner || !Parent) return {};
    using DuelGeometry::ColorMaterial;
    UMaterialInterface* Mats[FinishCount] = {
        ColorMaterial(Owner, FLinearColor(.49f,.53f,.49f), false, .35f, .38f),
        ColorMaterial(Owner, FLinearColor(.82f,.19f,.042f), false, .38f, .27f),
        ColorMaterial(Owner, FLinearColor(.035f,.046f,.047f), false, .73f, .08f),
        ColorMaterial(Owner, FLinearColor(.045f,.105f,.144f), false, .18f, .5f),
        ColorMaterial(Owner, FLinearColor(.19f,.135f,.058f), false, .76f, 0.f, true),
        ColorMaterial(Owner, FLinearColor(.25f,.28f,.26f), false, .29f, .64f),
        ColorMaterial(Owner, FLinearColor(.33f,.20f,.092f), false, .83f, 0.f),
        ColorMaterial(Owner, FLinearColor(3.8f,1.65f,.42f), true),
        ColorMaterial(Owner, FLinearColor(2.75f,.14f,.025f), true),
        ColorMaterial(Owner, FLinearColor(.19f,2.1f,.74f), true),
    };
    FVisual Result;
    auto* Root = Joint(Owner, Parent, Hover);
    Root->ComponentTags.Add(TEXT("NorthExtractionHelicopter"));
    Root->SetRelativeRotation(HoverRotation);
    Result.Root = Root;
    auto* Body = Joint(Owner,Root);
    Body->SetRelativeScale3D(FVector(AirframeScale));
    if (!RescueMesh(Owner,Body,TEXT("SM_NorthRescueHelicopter"),TEXT("NorthExtractionAirframe")))
    { Root->DestroyComponent();return {}; }
    // Imported pivots retain the source model's rotor axes in centimetres.
    auto* Main=Joint(Owner,Body,FVector(-32,0,157));
    auto* Tail=Joint(Owner,Body,FVector(-688,27,67));
    if (!RescueMesh(Owner,Main,TEXT("SM_NorthRescueMainRotor"),TEXT("NorthExtractionRotors")) ||
        !RescueMesh(Owner,Tail,TEXT("SM_NorthRescueTailRotor"),TEXT("NorthExtractionRotors")))
    { Root->DestroyComponent();return {}; }
    Result.MainRotor = Main;
    Result.TailRotor = Tail;

    auto* Lamp = Attach<UPointLightComponent>(Owner, Root, FVector(-27, -83, 80) * AirframeScale);
    Lamp->SetIntensityUnits(ELightUnits::Candelas);
    Lamp->SetIntensity(72.f);
    Lamp->SetAttenuationRadius(340.f);
    Lamp->SetCastShadows(false);
    Lamp->SetVolumetricScatteringIntensity(0.f);
    Lamp->SetUseTemperature(true);
    Lamp->SetTemperature(2720.f);
    Lamp->SetVisibility(false);
    Lamp->RegisterComponent();
    Result.CabinLight = Lamp;

    auto* Ladder = Joint(Owner, Root, DoorSocket);
    Result.Ladder = Ladder;
    FSurface Ropes[FinishCount];
    const double Drop = BoardingPoint.Z - LadderFoot.Z;
    for (double X : {-42., 42.}) Ropes[Hemp].Tube(FVector(X, 0, 0), FVector(X, 0, -Drop), 2.3, 2.3, 8);
    Result.Ropes = Install(Owner, Ladder, Ropes, Mats, TEXT("NorthExtractionRopes"));
    auto* Rungs = Attach<UInstancedStaticMeshComponent>(Owner, Ladder);
    Rungs->SetStaticMesh(DuelGeometry::Mesh(DuelGeometry::EShape::Tube));
    Rungs->SetMaterial(0, Mats[Hemp]);
    Rungs->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Rungs->SetCanEverAffectNavigation(false);
    Rungs->SetCastShadow(true);
    Rungs->ComponentTags.Add(TEXT("NorthExtractionRungs"));
    Rungs->RegisterComponent();
    for (int32 I = 0; I < RungCount; ++I) Rungs->AddInstance(FTransform(FQuat::Identity, FVector::ZeroVector, FVector(.001)));
    Result.Rungs = Rungs;
    Root->SetHiddenInGame(true, true);
    return Result;
}

void SetPose(FVisual& Visual, EPhase Phase, float Progress01, float RotorSeconds)
{
    auto* Root = Visual.Root.Get();
    if (!Root) return;
    const bool bHidden = Phase == EPhase::Hidden;
    if (Visual.bHidden != bHidden)
    {
        Root->SetHiddenInGame(bHidden, true);
        if (auto* Lamp = Visual.CabinLight.Get()) Lamp->SetVisibility(!bHidden);
        Visual.bHidden = bHidden;
        Visual.LastExtension = -1.f;
    }
    if (bHidden) return;

    Root->SetRelativeTransform(FlightTransform(Phase, Progress01));
    if (auto* Rotor = Visual.MainRotor.Get()) Rotor->SetRelativeRotation(FRotator(0, FMath::Fmod(RotorSeconds * 1216.f, 360.f), 0));
    if (auto* Rotor = Visual.TailRotor.Get()) Rotor->SetRelativeRotation(FRotator(FMath::Fmod(RotorSeconds * -2530.f, 360.f), 0, 0));
    const float Extended = Extension(Phase, FMath::Clamp(Progress01, 0.f, 1.f));
    if (FMath::IsNearlyEqual(Extended, Visual.LastExtension, .00001f)) return;
    Visual.LastExtension = Extended;
    const double Length = (BoardingPoint.Z - LadderFoot.Z) * Extended;
    if (auto* Ladder = Visual.Ladder.Get()) Ladder->SetHiddenInGame(Length < 2., true);
    if (auto* Ropes = Visual.Ropes.Get()) Ropes->SetRelativeScale3D(FVector(1, 1, FMath::Max(.002, double(Extended))));
    if (auto* Rungs = Visual.Rungs.Get())
    {
        const FQuat Across = FQuat::FindBetweenNormals(FVector::UpVector, FVector::ForwardVector);
        const FVector Size(.061, .061, RungWidth / 100.);
        TArray<FTransform, TInlineAllocator<RungCount>> Steps;
        for (int32 I = 0; I < RungCount; ++I)
        {
            const double Z = -Length + 21. + I * RungSpacing;
            if (Z <= -12.) Steps.Emplace(Across, FVector(0, 0, Z), Size);
            else Steps.Emplace(FQuat::Identity, FVector::ZeroVector, FVector(.001));
        }
        Rungs->BatchUpdateInstancesTransforms(0, TArrayView<const FTransform>(Steps), false, true, true);
    }
}
}
