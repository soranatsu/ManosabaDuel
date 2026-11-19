#include "DuelFrostMirror.h"

#include "DuelGeometry.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"

namespace
{
    struct FGlassMesh
    {
        TArray<FVector> Positions;
        TArray<int32> Indices;
        TArray<FVector> Normals;
        TArray<FVector2D> UVs;
        TArray<FLinearColor> Colors;

        void Triangle(FVector A, FVector B, FVector C, const FVector& Out,
            const FLinearColor& Role, float Length)
        {
            FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
            if (FVector::DotProduct(Normal, Out) < 0.f)
            {
                Swap(B, C);
                Normal *= -1.f;
            }
            const int32 Base = Positions.Num();
            Positions.Append({A, B, C});
            Indices.Append({Base, Base + 1, Base + 2});
            for (const FVector& Vertex : {A, B, C})
            {
                Normals.Add(Normal);
                UVs.Add(FVector2D(Vertex.X / Length + .5, Vertex.Z / Length + .5));
                Colors.Add(Role);
            }
        }
    };

    // Authored cut-glass silhouettes, counter-clockwise in X/Z; no random runtime tessellation.
    const FVector2D Profiles[3][9] = {
        {{-.11,-.49},{.07,-.45},{.17,-.26},{.14,.12},{.08,.46},{-.03,.50},{-.16,.19},{-.18,-.10},{-.13,-.32}},
        {{-.08,-.50},{.10,-.38},{.15,-.19},{.12,.20},{.02,.48},{-.07,.44},{-.13,.15},{-.18,-.05},{-.12,-.34}},
        {{-.13,-.43},{.05,-.50},{.16,-.28},{.13,.07},{.10,.35},{-.02,.49},{-.16,.27},{-.14,-.03},{-.17,-.25}}
    };

    FGlassMesh Build(int32 Variant, float Length)
    {
        FGlassMesh Mesh;
        Variant = FMath::Abs(Variant) % 3;
        Length = FMath::Clamp(Length, 40.f, 140.f);
        for (int32 Layer = 0; Layer < 3; ++Layer)
        {
            const int32 Profile = (Variant + Layer) % 3;
            const float Size = Layer == 0 ? 1.f : Layer == 1 ? .47f : .36f;
            const float L = Length * Size;
            const FVector Offset = Layer == 0 ? FVector::ZeroVector : Layer == 1 ?
                FVector(-.115f, .072f, .15f) * Length : FVector(.12f, -.06f, -.16f) * Length;
            const FQuat Tilt(FVector::YAxisVector, FMath::DegreesToRadians(
                Layer == 0 ? (Variant - 1) * 5.f : Layer == 1 ? -17.f : 23.f));
            const auto Place = [&](const FVector& V) { return Offset + Tilt.RotateVector(V); };
            const FVector Front = Tilt.RotateVector(FVector(0,-1,0));
            const FVector Back = -Front;
            const float Thickness = L * .063f;
            const FVector CenterFront(.008f * L, -Thickness * .5f - L * .012f, .035f * L);
            const FVector CenterBack(0, Thickness * .5f, 0);
            FVector InnerFront[9], OuterFront[9], InnerBack[9], OuterBack[9];
            for (int32 I = 0; I < 9; ++I)
            {
                const FVector2D P = Profiles[Profile][I] * L;
                OuterFront[I] = FVector(P.X, -Thickness * .29f, P.Y);
                OuterBack[I] = FVector(P.X, Thickness * .29f, P.Y);
                InnerFront[I] = FVector(P.X * .91f, -Thickness * .5f, P.Y * .975f);
                InnerBack[I] = FVector(P.X * .91f, Thickness * .5f, P.Y * .975f);
            }
            for (int32 I = 0; I < 9; ++I)
            {
                const int32 J = (I + 1) % 9;
                const FVector Out = Tilt.RotateVector(FVector(OuterFront[I].X + OuterFront[J].X, 0,
                    OuterFront[I].Z + OuterFront[J].Z).GetSafeNormal());
                const float Grain = .35f + .065f * ((I + Profile * 2) % 9);
                const FLinearColor FaceRole(0, 0, Grain, 1), EdgeRole(1, 0, .75f, 1);
                Mesh.Triangle(Place(CenterFront), Place(InnerFront[I]), Place(InnerFront[J]), Front, FaceRole, Length);
                Mesh.Triangle(Place(CenterBack), Place(InnerBack[I]), Place(InnerBack[J]), Back, FaceRole, Length);
                Mesh.Triangle(Place(InnerFront[I]), Place(OuterFront[I]), Place(OuterFront[J]), Out + Front, EdgeRole, Length);
                Mesh.Triangle(Place(InnerFront[I]), Place(OuterFront[J]), Place(InnerFront[J]), Out + Front, EdgeRole, Length);
                Mesh.Triangle(Place(InnerBack[I]), Place(OuterBack[J]), Place(OuterBack[I]), Out + Back, EdgeRole, Length);
                Mesh.Triangle(Place(InnerBack[I]), Place(InnerBack[J]), Place(OuterBack[J]), Out + Back, EdgeRole, Length);
                Mesh.Triangle(Place(OuterFront[I]), Place(OuterBack[I]), Place(OuterBack[J]), Out, EdgeRole, Length);
                Mesh.Triangle(Place(OuterFront[I]), Place(OuterBack[J]), Place(OuterFront[J]), Out, EdgeRole, Length);
            }

            // Follow the actual folded face, so hairline cracks neither float nor z-fight.
            const auto FrontY = [&](const FVector2D& P)
            {
                const FVector2D A(CenterFront.X, CenterFront.Z);
                for (int32 I = 0; I < 9; ++I)
                {
                    const FVector2D B(InnerFront[I].X, InnerFront[I].Z);
                    const FVector2D C(InnerFront[(I + 1) % 9].X, InnerFront[(I + 1) % 9].Z);
                    const double D = (B.Y-C.Y)*(A.X-C.X)+(C.X-B.X)*(A.Y-C.Y);
                    if (FMath::Abs(D) < .0001) continue;
                    const double U = ((B.Y-C.Y)*(P.X-C.X)+(C.X-B.X)*(P.Y-C.Y)) / D;
                    const double V = ((C.Y-A.Y)*(P.X-C.X)+(A.X-C.X)*(P.Y-C.Y)) / D;
                    const double W = 1.0-U-V;
                    if (U >= -.001 && V >= -.001 && W >= -.001)
                        return U*CenterFront.Y+V*InnerFront[I].Y+W*InnerFront[(I+1)%9].Y-.08;
                }
                return static_cast<double>(-Thickness*.5f-.08f);
            };
            const FVector2D Crack[3][4] = {
                {{-.018,.075},{.035,.18},{.012,.29},{.028,.38}},
                {{-.018,.075},{-.064,-.02},{-.103,-.09},{-.107,-.21}},
                {{-.018,.075},{.037,-.10},{.010,-.22},{.034,-.33}}
            };
            for (int32 Branch = 0; Branch < (Layer == 0 ? 3 : 2); ++Branch)
            {
                for (int32 Segment = 0; Segment < 3; ++Segment)
                {
                    const FVector2D A = Crack[Branch][Segment] * L, B = Crack[Branch][Segment+1] * L;
                    const FVector2D Delta = (B-A).GetSafeNormal();
                    const FVector2D Across(-Delta.Y, Delta.X);
                    const float HalfWidth = L * (.0021f - Segment * .00038f);
                    const auto Vertex = [&](const FVector2D& P) { return Place(FVector(P.X, FrontY(P), P.Y)); };
                    const FVector2D P0=A-Across*HalfWidth, P1=A+Across*HalfWidth;
                    const FVector2D P2=B+Across*HalfWidth*.60f, P3=B-Across*HalfWidth*.60f;
                    const FLinearColor CrackRole(0,1,.44f,1);
                    Mesh.Triangle(Vertex(P0),Vertex(P1),Vertex(P2),Front,CrackRole,Length);
                    Mesh.Triangle(Vertex(P0),Vertex(P2),Vertex(P3),Front,CrackRole,Length);
                }
            }
        }
        return Mesh;
    }
}

USceneComponent* DuelFrostMirror::Create(AActor* Owner, USceneComponent* Parent, int32 Variant, float Length)
{
    if (!Owner || !Parent) return nullptr;
    UMaterialInterface* Material = DuelGeometry::ArtMaterial(TEXT("M_YukiFrostMirror"));
    if (!Material) Material = DuelGeometry::ColorMaterial(Owner, FLinearColor(.53f,.67f,.76f), false, .42f, .14f);
    FGlassMesh Data = Build(Variant, Length);
    UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Owner);
    Mesh->SetupAttachment(Parent);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCastShadow(false);
    Mesh->SetMaterial(0, Material);
    Mesh->CreateMeshSection_LinearColor(0, Data.Positions, Data.Indices, Data.Normals,
        Data.UVs, Data.Colors, {}, false, false);
    Owner->AddInstanceComponent(Mesh);
    Mesh->RegisterComponent();
    return Mesh;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelFrostMirrorGeometry, "ManosabaDuel.NorthTower.FrostMirrorGeometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDuelFrostMirrorGeometry::RunTest(const FString&)
{
    for (int32 Variant=0;Variant<3;++Variant)
    {
        for (float Length : {78.f,105.f})
        {
            const FGlassMesh Mesh=Build(Variant,Length);
            TestEqual(TEXT("Three closed 72-triangle plates plus 42 crack triangles"), Mesh.Indices.Num()/3,258);
            TestEqual(TEXT("Every vertex has a surface role"),Mesh.Positions.Num(),Mesh.Colors.Num());
            FBox Bounds(ForceInit);
            for (int32 I=0;I<Mesh.Positions.Num();++I)
            {
                const FVector P=Mesh.Positions[I];
                TestFalse(TEXT("Finite geometry"),P.ContainsNaN());
                TestTrue(TEXT("Unit hard-surface normals"),Mesh.Normals[I].IsNormalized());
                Bounds+=P;
            }
            TestTrue(TEXT("Thin fractured plates, not a thick spindle"),Bounds.GetSize().Y < Length*.19f);
            TestTrue(TEXT("Stable long-axis silhouette"),Bounds.GetSize().Z > Length*.89f && Bounds.GetSize().Z < Length*1.12f);
            TestTrue(TEXT("Crown does not widen into wing shapes"),Bounds.GetSize().X < Length*.57f);
            for(int32 I=0;I<Mesh.Indices.Num();I+=3)
            {
                const FVector A=Mesh.Positions[Mesh.Indices[I]],B=Mesh.Positions[Mesh.Indices[I+1]],C=Mesh.Positions[Mesh.Indices[I+2]];
                TestTrue(TEXT("No degenerate fragment triangles"),FVector::CrossProduct(B-A,C-A).SizeSquared()>.000001);
            }
        }
    }
    return true;
}
#endif
