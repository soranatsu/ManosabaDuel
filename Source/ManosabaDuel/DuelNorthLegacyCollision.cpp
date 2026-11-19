#include "DuelNorthLegacyCollision.h"
#include "DuelNorthLayout.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"

namespace DuelNorthLegacyCollision
{
void Build(AActor* Owner,USceneComponent* Parent)
{
    if(!Owner || !Parent)return;
    const double Started=FPlatformTime::Seconds();
    struct FShell {FBox Box;FName Room,Mesh;};
    TArray<FShell> Shells;
    int32 Inspected=0,Overhead=0;
    for(const auto* C:TInlineComponentArray<UInstancedStaticMeshComponent*>(Owner))
    {
        if(!C->GetStaticMesh() || !(C->ComponentHasTag(TEXT("NorthMedievalDungeonArchitecture")) ||
            C->ComponentHasTag(TEXT("NorthMasonryWallBases")) || C->ComponentHasTag(TEXT("NorthMasonryCornerBases"))))continue;
        const FString Name=C->GetStaticMesh()->GetName();
        if(Name==TEXT("SM_Floor") || Name==TEXT("SM_Crypt_Floor") || Name.Contains(TEXT("Ceiling")) || Name.Contains(TEXT("Chandelier")))continue;
        const FBox Local=C->GetStaticMesh()->GetBoundingBox();
        for(int32 I=0;I<C->GetInstanceCount();++I)
        {
            FTransform Pose;if(!C->GetInstanceTransform(I,Pose,true))continue;
            const FBox Model=Local.TransformBy(Pose);++Inspected;
            bool bReachable=false;
            for(const auto& R:DuelNorthLayout::Rooms())
            {
                if(!R.bFloor || !Model.IntersectXY(R.Bounds) || Model.Max.Z<R.Bounds.Min.Z+12. || Model.Min.Z>R.Bounds.Min.Z+160.)continue;
                FBox Reach=R.Bounds.ExpandBy(FVector(-DuelNorthLayout::WallThickness*.5,-DuelNorthLayout::WallThickness*.5,0));
                Reach.Max.Z=R.Bounds.Min.Z+160.;
                const FBox Box(Model.Min.ComponentMax(Reach.Min),Model.Max.ComponentMin(Reach.Max));
                if(Box.GetSize().GetMin()<1. || FMath::Max(Box.GetSize().X,Box.GetSize().Y)<12.)continue;
                bReachable=true;
                if(Shells.ContainsByPredicate([&](const FShell& S){return S.Box.IsInsideOrOn(Box.Min) && S.Box.IsInsideOrOn(Box.Max);}))continue;
                Shells.RemoveAll([&](const FShell& S){return Box.IsInsideOrOn(S.Box.Min) && Box.IsInsideOrOn(S.Box.Max);});
                Shells.Add({Box,R.Id,C->GetStaticMesh()->GetFName()});
            }
            if(!bReachable)++Overhead;
        }
    }
    const int32 Unmerged=Shells.Num();
    // Merge only identical cross-sections on a straight axis with no open gap.
    for(int32 Axis=0;Axis<3;++Axis)
    {
        const int32 A=(Axis+1)%3,B=(Axis+2)%3;
        TMap<FString,TArray<FShell>> Lines;
        for(const auto& S:Shells)
        {
            const FString Key=FString::Printf(TEXT("%s|%s|%d|%d|%d|%d"),*S.Room.ToString(),*S.Mesh.ToString(),
                FMath::RoundToInt(S.Box.Min[A]*10.),FMath::RoundToInt(S.Box.Max[A]*10.),
                FMath::RoundToInt(S.Box.Min[B]*10.),FMath::RoundToInt(S.Box.Max[B]*10.));
            Lines.FindOrAdd(Key).Add(S);
        }
        Shells.Reset();
        for(auto& Pair:Lines)
        {
            auto& Line=Pair.Value;Line.Sort([&](const FShell& L,const FShell& R){return L.Box.Min[Axis]<R.Box.Min[Axis];});
            FShell Last=Line[0];
            for(int32 I=1;I<Line.Num();++I)
            {
                if(Line[I].Box.Min[Axis]<=Last.Box.Max[Axis]+.01)Last.Box+=Line[I].Box;
                else{Shells.Add(Last);Last=Line[I];}
            }
            Shells.Add(Last);
        }
    }
    auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    if(!Cube){UE_LOG(LogTemp,Error,TEXT("Legacy collision requires the engine simple cube"));return;}
    TMap<FName,TArray<FTransform>> Rooms;
    TMap<FName,int32> Types;
    for(const auto& S:Shells)
    {
        Rooms.FindOrAdd(S.Room).Add(FTransform(FQuat::Identity,S.Box.GetCenter(),S.Box.GetSize()/100.).GetRelativeTransform(Parent->GetComponentTransform()));
        ++Types.FindOrAdd(S.Mesh);
    }
    for(const auto& Pair:Rooms)
    {
        auto* C=NewObject<UInstancedStaticMeshComponent>(Owner);C->SetupAttachment(Parent);C->SetMobility(EComponentMobility::Static);
        C->SetStaticMesh(Cube);C->SetVisibility(false);C->SetHiddenInGame(true);C->SetCastShadow(false);
        C->bDisallowNanite=true;C->SetVisibleInRayTracing(false);C->SetAffectDistanceFieldLighting(false);C->SetAffectDynamicIndirectLighting(false);
        C->SetCollisionObjectType(ECC_WorldStatic);C->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        C->SetCollisionResponseToAllChannels(ECR_Ignore);C->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);
        C->SetCollisionResponseToChannel(ECC_Camera,ECR_Block);C->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
        C->SetCanEverAffectNavigation(false);C->ComponentTags.Append({TEXT("NorthLegacyCollisionShell"),TEXT("NorthLegacyMasonry"),Pair.Key});
        Owner->AddInstanceComponent(C);C->AddInstances(Pair.Value,false,false,false);C->RegisterComponent();
    }
    for(const auto& Type:Types)UE_LOG(LogTemp,Display,TEXT("North legacy collision type %s: %d simple boxes"),*Type.Key.ToString(),Type.Value);
    UE_LOG(LogTemp,Display,TEXT("North legacy architecture collision: inspected=%d unreachable=%d boxes_before_merge=%d boxes=%d components=%d build_ms=%.3f; hidden simple-box instances, no render/tick or distant-coast collision"),
        Inspected,Overhead,Unmerged,Shells.Num(),Rooms.Num(),(FPlatformTime::Seconds()-Started)*1000.);
}
}
