#include "DuelNorthArchitecture.h"
#include "DuelNorthLayout.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

namespace DuelNorthArchitecture
{
namespace
{
    using namespace DuelNorthLayout;

    struct FEdges
    {
        TArray<FGuardEdge> Data;

        void Add(const FString& Id, const FVector& A, const FVector& B, const FVector& Inward, bool bStone)
        {
            if (FVector::Dist2D(A,B)<2) return;
            Data.Add({FName(*Id),A,B,Inward,140,12,bStone});
        }

        // Subtract only real door openings and the connected flight widths. A landing
        // is not an unguarded rectangle, nor may its rails close the turn between runs.
        void Boundary(const FRoom& Room, int32 Axis, bool bMax)
        {
            const int32 Along=1-Axis;
            const float Plane=bMax?Room.Bounds.Max[Axis]:Room.Bounds.Min[Axis];
            const float Z=Room.Bounds.Min.Z;
            TArray<FVector2D> Openings;
            for (const FPortal& P:Portals())
            {
                if (P.bWindow || FMath::Abs(P.Normal[Axis])<.9 || FMath::Abs(P.Center[Axis]-Plane)>1 ||
                    FMath::Abs(P.Center.Z-Z)>1) continue;
                // Platforms share their enclosure's portal, rather than owning a second door.
                const bool bRoomPortal=P.RoomA==Room.Id || P.RoomB==Room.Id ||
                    Room.Kind==ERoomKind::Landing;
                if (bRoomPortal) Openings.Emplace(P.Center[Along]-P.Width*.5f,P.Center[Along]+P.Width*.5f);
            }
            for (const FStairFlight& S:Stairs())
            {
                if (Axis!=0) continue;
                const FVector* End=nullptr;
                if (S.FromLanding==Room.Id) End=&S.Start;
                if (S.ToLanding==Room.Id) End=&S.End;
                if (End && FMath::Abs((*End)[Axis]-Plane)<1)
                    Openings.Emplace((*End)[Along]-S.Width*.5f,(*End)[Along]+S.Width*.5f);
            }
            const float Lo=Room.Bounds.Min[Along], Hi=Room.Bounds.Max[Along];
            TArray<float> Cuts{Lo,Hi};
            for (const auto& O:Openings)
            {
                Cuts.AddUnique(FMath::Clamp(float(O.X),Lo,Hi));
                Cuts.AddUnique(FMath::Clamp(float(O.Y),Lo,Hi));
            }
            Cuts.Sort();
            FVector Inward=FVector::ZeroVector;Inward[Axis]=bMax?-1:1;
            for (int32 I=1;I<Cuts.Num();++I)
            {
                const float Mid=(Cuts[I-1]+Cuts[I])*.5f;
                if (Openings.ContainsByPredicate([&](const FVector2D& O){return Mid>O.X-.1 && Mid<O.Y+.1;})) continue;
                FVector A(0,0,Z),B(0,0,Z);A[Axis]=B[Axis]=Plane;A[Along]=Cuts[I-1];B[Along]=Cuts[I];
                Add(FString::Printf(TEXT("%sGuard%d%d_%d"),*Room.Id.ToString(),Axis,bMax?1:0,I),A,B,Inward,true);
            }
        }

        FEdges()
        {
            for (const FStairFlight& S:Stairs())
            {
                const FVector Direction=(S.End-S.Start).GetSafeNormal2D();
                const FVector Side(-Direction.Y,Direction.X,0);
                for (int32 Sign:{-1,1})
                {
                    const FVector Offset=Side*(Sign*S.Width*.5f);
                    Add(FString::Printf(TEXT("%sSide%d"),*S.Id.ToString(),Sign),S.Start+Offset,S.End+Offset,-Sign*Side,false);
                }
            }
            for (const FRoom& R:Rooms())
            {
                if (R.Kind!=ERoomKind::Landing && R.Id!=TEXT("DawnGarden")) continue;
                for (int32 Axis=0;Axis<2;++Axis) for (bool bMax:{false,true}) Boundary(R,Axis,bMax);
            }
        }
    };

    struct FMesh
    {
        TArray<FVector> Vertices,Normals;
        TArray<int32> Indices;
        TArray<FVector2D> UVs;
        TArray<FProcMeshTangent> Tangents;

        // Closed tubes encode the existing forged bars and stone post profiles.
        void Tube(const FVector& A,const FVector& B,float Radius,int32 Sides=8,bool bCapEnds=true)
        {
            const FVector Axis=(B-A).GetSafeNormal();
            if (Axis.IsNearlyZero()) return;
            FVector U=FVector::CrossProduct(Axis,FVector::UpVector).GetSafeNormal();
            if (U.IsNearlyZero()) U=FVector::RightVector;
            const FVector V=FVector::CrossProduct(Axis,U).GetSafeNormal();
            for (int32 Side=0;Side<Sides;++Side)
            {
                const float T0=2*PI*Side/Sides,T1=2*PI*(Side+1)/Sides;
                const FVector R0=U*FMath::Cos(T0)+V*FMath::Sin(T0);
                const FVector R1=U*FMath::Cos(T1)+V*FMath::Sin(T1);
                const FVector N=(R0+R1).GetSafeNormal();
                const int32 Base=Vertices.Num();
                Vertices.Append({A+Radius*R0,A+Radius*R1,B+Radius*R1,B+Radius*R0});
                for (int32 K=0;K<4;++K) {Normals.Add(N);Tangents.Emplace(Axis,false);}
                const float Length=FVector::Dist(A,B)/100.f;
                UVs.Append({FVector2D(0,0),FVector2D(.25,0),FVector2D(.25,Length),FVector2D(0,Length)});
                Indices.Append({Base,Base+2,Base+1,Base,Base+3,Base+2});
                // Pickets terminate inside both solid rails.
                for (int32 Cap=0;bCapEnds && Cap<2;++Cap)
                {
                    const FVector C=Cap?B:A;const FVector Normal=Cap?Axis:-Axis;
                    const int32 F=Vertices.Num();
                    Vertices.Append({C,C+Radius*R0,C+Radius*R1});
                    for (int32 K=0;K<3;++K) {Normals.Add(Normal);Tangents.Emplace(U,false);}
                    UVs.Append({FVector2D(.5,.5),FVector2D(0,0),FVector2D(1,0)});
                    if(Cap) Indices.Append({F,F+2,F+1});else Indices.Append({F,F+1,F+2});
                }
            }
        }

        bool Install(AActor* Owner,USceneComponent* Parent,const TCHAR* Material)
        {
            if (Indices.IsEmpty()) return false;
            auto* Mesh=NewObject<UProceduralMeshComponent>(Owner);
            Mesh->SetupAttachment(Parent);Mesh->SetMobility(EComponentMobility::Static);
            Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Mesh->SetCastShadow(true);
            Mesh->CreateMeshSection(0,Vertices,Indices,Normals,UVs,TArray<FColor>(),Tangents,false);
            Mesh->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,Material));
            Mesh->ComponentTags.Add(TEXT("NorthTowerGuardVisual"));
            Owner->AddInstanceComponent(Mesh);Mesh->RegisterComponent();
            return true;
        }
    };

    void Collision(AActor* Owner,USceneComponent* Parent,const FGuardEdge& Edge)
    {
        // Vertical short panels follow the rise without a tall invisible wall beneath
        // the full flight. They overlap slightly at joints so no capsule can slip through.
        const int32 Count=FMath::Max(1,FMath::CeilToInt(FVector::Dist2D(Edge.Start,Edge.End)/100.f));
        for (int32 I=0;I<Count;++I)
        {
            const FVector A=FMath::Lerp(Edge.Start,Edge.End,float(I)/Count);
            const FVector B=FMath::Lerp(Edge.Start,Edge.End,float(I+1)/Count);
            FVector C=(A+B)*.5;
            const float Bottom=FMath::Min(A.Z,B.Z)-8,Top=FMath::Max(A.Z,B.Z)+Edge.Height;
            C.Z=(Bottom+Top)*.5f;
            const bool bX=FMath::Abs(B.X-A.X)>FMath::Abs(B.Y-A.Y);
            FVector Size(Edge.Thickness,Edge.Thickness,Top-Bottom);
            Size[bX?0:1]=FVector::Dist2D(A,B)+.4;
            auto* Block=NewObject<UBoxComponent>(Owner);Block->SetupAttachment(Parent);
            Block->SetRelativeLocation(C);Block->SetBoxExtent(Size*.5);
            Block->SetCollisionProfileName(TEXT("BlockAll"));Block->SetMobility(EComponentMobility::Static);
            Block->ComponentTags.Add(TEXT("NorthTowerGuard"));Block->ComponentTags.Add(Edge.Id);
            Owner->AddInstanceComponent(Block);Block->RegisterComponent();
        }
    }
}

const TArray<FGuardEdge>& GuardEdges()
{
    static const FEdges Edges;
    return Edges.Data;
}

void Build(AActor* Owner,USceneComponent* Parent)
{
    if (!Owner || !Parent || Owner->ActorHasTag(TEXT("NorthTowerArchitectureBuilt"))) return;
    Owner->Tags.Add(TEXT("NorthTowerArchitectureBuilt"));
    struct FGuardMeshes { FMesh Iron, Stone; };
    TMap<FIntVector,FGuardMeshes> Batches;
    for (const FGuardEdge& E:GuardEdges())
    {
        Collision(Owner,Parent,E);
        // Keep a complete edge together. Its triangle positions and collider stay
        // the same, while each component gets tighter draw and shadow bounds.
        const FVector Middle = (E.Start + E.End) * .5;
        const FIntVector Cell(
            FMath::FloorToInt32(Middle.X / 2000.),
            FMath::FloorToInt32(Middle.Y / 2000.),
            FMath::FloorToInt32(Middle.Z / 1050.));
        FGuardMeshes& Group = Batches.FindOrAdd(Cell);
        FMesh& Iron = Group.Iron;
        FMesh& Stone = Group.Stone;
        Iron.Tube(E.Start+FVector(0,0,E.Height-4),E.End+FVector(0,0,E.Height-4),4);
        Iron.Tube(E.Start+FVector(0,0,21),E.End+FVector(0,0,21),3);
        const float Run=FVector::Dist2D(E.Start,E.End);
        const int32 Pickets=FMath::Max(1,FMath::CeilToInt(Run/22.f));
        for (int32 I=0;I<=Pickets;++I)
        {
            const FVector P=FMath::Lerp(E.Start,E.End,float(I)/Pickets);
            Iron.Tube(P+FVector(0,0,19),P+FVector(0,0,E.Height-5),1.9f,6,false);
        }
        const int32 Bays=FMath::Max(1,FMath::CeilToInt(Run/140.f));
        for (int32 I=0;I<=Bays;++I)
        {
            const FVector P=FMath::Lerp(E.Start,E.End,float(I)/Bays);
            if (E.bStonePosts)
            {
                Stone.Tube(P,P+FVector(0,0,15),14);
                Stone.Tube(P+FVector(0,0,15),P+FVector(0,0,125),8);
                Stone.Tube(P+FVector(0,0,125),P+FVector(0,0,132),12);
                Stone.Tube(P+FVector(0,0,132),P+FVector(0,0,142),10);
            }
            else Iron.Tube(P,P+FVector(0,0,143),5);
        }
        // Repeating pointed arches are restrained tracery, rather than random ornaments.
        for (int32 Bay=0;Bay<Bays;++Bay)
        {
            const FVector A=FMath::Lerp(E.Start,E.End,float(Bay)/Bays);
            const FVector B=FMath::Lerp(E.Start,E.End,float(Bay+1)/Bays);
            for (int32 Step=0;Step<8;++Step)
            {
                const float T0=float(Step)/8,T1=float(Step+1)/8;
                const float H0=69+53*FMath::Sqrt(1-FMath::Abs(T0-.5f)*2);
                const float H1=69+53*FMath::Sqrt(1-FMath::Abs(T1-.5f)*2);
                Iron.Tube(FMath::Lerp(A,B,T0)+FVector(0,0,H0),FMath::Lerp(A,B,T1)+FVector(0,0,H1),2,6);
            }
        }
    }
    int32 Meshes = 0, Triangles = 0;
    for (auto& Pair : Batches)
    {
        FGuardMeshes& Group = Pair.Value;
        Meshes += Group.Iron.Install(Owner,Parent,TEXT("/Game/DuelArt/Materials/M_ForgedSteel.M_ForgedSteel")) ? 1 : 0;
        Meshes += Group.Stone.Install(Owner,Parent,TEXT("/Game/DuelArt/Materials/M_TrimStone.M_TrimStone")) ? 1 : 0;
        Triangles += (Group.Iron.Indices.Num() + Group.Stone.Indices.Num()) / 3;
    }
    UE_LOG(LogTemp, Display, TEXT("North guard visuals: %d spatial groups, %d shadow-casting components, %d triangles"),
        Batches.Num(), Meshes, Triangles);
}

bool Validate(TArray<FString>& Errors)
{
    const DuelNorthLayout::FRoute* Route=DuelNorthLayout::FindRoute(TEXT("MainWalk"));
    if (!Route) {Errors.Add(TEXT("Missing MainWalk route"));return false;}
    TSet<FName> Names;
    for (const FGuardEdge& E:GuardEdges())
    {
        if (Names.Contains(E.Id)) Errors.Add(FString::Printf(TEXT("Duplicate guard %s"),*E.Id.ToString()));
        Names.Add(E.Id);
        const FVector Axis=(E.End-E.Start).GetSafeNormal2D();
        const double Run=FVector::Dist2D(E.Start,E.End);
        if (Run<2 || E.Height<130 || FMath::Abs(FVector::DotProduct(Axis,E.Inward))>.01)
            Errors.Add(FString::Printf(TEXT("Invalid edge guard %s"),*E.Id.ToString()));
        for (int32 Segment=1;Segment<Route->Points.Num();++Segment)
        {
            const FVector A=Route->Points[Segment-1],B=Route->Points[Segment];
            const int32 Count=FMath::Max(1,FMath::CeilToInt(FVector::Dist(A,B)/35.f));
            for (int32 I=0;I<=Count;++I)
            {
                const FVector P=FMath::Lerp(A,B,float(I)/Count);
                const double T=FMath::Clamp(FVector::DotProduct(P-E.Start,Axis)/Run,0.,1.);
                const FVector Closest=FMath::Lerp(E.Start,E.End,T);
                if (P.Z+124<Closest.Z-8 || P.Z>Closest.Z+E.Height+2) continue;
                if (FVector::Dist2D(P,Closest)<42+E.Thickness*.5+1)
                {
                    Errors.Add(FString::Printf(TEXT("Guard %s intrudes on MainWalk segment %d at %s"),
                        *E.Id.ToString(),Segment,*P.ToString()));break;
                }
            }
        }
    }
    return Errors.IsEmpty();
}
}
