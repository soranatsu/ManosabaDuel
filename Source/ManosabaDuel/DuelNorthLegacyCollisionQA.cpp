#include "DuelGame.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "DuelNorthArchitecture.h"
#include "DuelNorthLayout.h"
#include "DuelNorthTower.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthLegacyCollisionTest,"ManosabaDuel.NorthTower.LegacyCollision",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDuelNorthLegacyCollisionTest::RunTest(const FString& Parameters)
{
    using namespace DuelNorthLayout;
    if(!TestTrue(TEXT("Legacy collision inspection uses an isolated no-save tower"),
        FParse::Param(FCommandLine::Get(),TEXT("DuelNorthTower")) && FParse::Param(FCommandLine::Get(),TEXT("DuelNoSave"))))return false;
    ADuelMode* Mode=nullptr;
    if(GEngine)for(const auto& C:GEngine->GetWorldContexts())if(C.World() && (Mode=C.World()->GetAuthGameMode<ADuelMode>()))break;
    auto* Player=Mode?const_cast<ADuelFighter*>(Mode->GetPlayerFighter()):nullptr;
    const auto* Tower=Mode?Mode->NorthTower():nullptr;
    if(!TestTrue(TEXT("Live tower/player exist"),Player && Tower))return false;
    auto& World=*Mode->GetWorld();auto& Motion=*Player->GetCharacterMovement();
    const FTransform Saved=Player->GetActorTransform();const auto MovementMode=Motion.MovementMode;
    TArray<UPrimitiveComponent*> Shells;
    struct FSample {UPrimitiveComponent* Body;FBox Bounds;};TArray<FSample> Samples;
    int32 Furniture=0,Masonry=0;
    for(auto* B:TInlineComponentArray<UBoxComponent*>(Tower))if(B->ComponentHasTag(TEXT("NorthLegacyCollisionShell")))
    {
        Shells.Add(B);Samples.Add({B,B->Bounds.GetBox()});Furniture+=B->ComponentHasTag(TEXT("NorthLegacyFurniture"));
        TestTrue(TEXT("Reachable legacy solids use query-only simple boxes"),B->IsRegistered() &&
            B->GetCollisionEnabled()==ECollisionEnabled::QueryOnly && B->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block);
        TestTrue(TEXT("No collision shell is installed on the unreachable open sea"),
            FMath::Abs(B->GetComponentLocation().X)<10000. && FMath::Abs(B->GetComponentLocation().Y)<6000.);
    }
    for(auto* C:TInlineComponentArray<UInstancedStaticMeshComponent*>(Tower))if(C->ComponentHasTag(TEXT("NorthLegacyMasonry")))
    {
        Shells.Add(C);Masonry+=C->GetInstanceCount();
        TestTrue(TEXT("Masonry instances keep a hidden, query-only simple-cube batch per room"),!C->IsVisible() &&
            C->GetCollisionEnabled()==ECollisionEnabled::QueryOnly && C->GetStaticMesh() && C->GetStaticMesh()->GetFName()==TEXT("Cube"));
        for(int32 I=0;I<C->GetInstanceCount();++I)
        {FTransform Pose;if(C->GetInstanceTransform(I,Pose,true))Samples.Add({C,C->GetStaticMesh()->GetBoundingBox().TransformBy(Pose)});}
    }
    ON_SCOPE_EXIT
    {
        for(auto* B:Shells)B->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Motion.StopMovementImmediately();Player->SetActorTransform(Saved,false,nullptr,ETeleportType::TeleportPhysics);
        Motion.SetMovementMode(MovementMode);
    };
    TestTrue(TEXT("Both old furniture and imported masonry receive reachable shells"),Furniture>10 && Masonry>20);
    const float Radius=Player->GetCapsuleComponent()->GetScaledCapsuleRadius(),Half=Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const auto Shape=FCollisionShape::MakeCapsule(Radius,Half);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(LegacyCollisionInspection),false,Player);Query.AddIgnoredActor(Mode->GetBossFighter());
    for(auto* B:TInlineComponentArray<UBoxComponent*>(Tower))if(B->ComponentHasTag(TEXT("NorthTowerGate")))Query.AddIgnoredComponent(B);
    const auto Open=[&](const FVector& A,const FVector& B){return !World.SweepTestByChannel(A,B,FQuat::Identity,ECC_Pawn,Shape,Query);};
    struct FProbe {UPrimitiveComponent* Box;FBox Bounds;FVector Start,End;FName Room;EFloor Floor;};TArray<FProbe> Gaps;
    // Temporarily remove only the new shells; all original floors, furniture, gates and guards stay intact.
    for(auto* B:Shells)B->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    for(const auto& Sample:Samples)
    {
        auto* B=Sample.Body;
        const FRoom* Room=nullptr;for(const auto& R:Rooms())if(B->ComponentHasTag(R.Id)){Room=&R;break;}
        if(!Room)continue;
        const FBox Bounds=Sample.Bounds;
        FVector N=(Room->Bounds.GetCenter()-Bounds.GetCenter()).GetSafeNormal2D();
        N=FMath::Abs(N.X)>FMath::Abs(N.Y)?FVector(FMath::Sign(N.X),0,0):FVector(0,FMath::Sign(N.Y),0);
        const double Extent=FMath::Abs(N.X)*Bounds.GetExtent().X+FMath::Abs(N.Y)*Bounds.GetExtent().Y;
        FVector Face=Bounds.GetCenter()+N*Extent;Face.Z=Room->Bounds.Min.Z+Half+2.;
        const FVector Start=Face+N*(Radius+105.),End=Face+N*(Radius*.20);
        FHitResult Floor;
        if(!World.LineTraceSingleByChannel(Floor,Start-FVector(0,0,Half-5.),Start-FVector(0,0,Half+25.),ECC_Pawn,Query) ||
            World.OverlapBlockingTestByChannel(Start,FQuat::Identity,ECC_Pawn,Shape,Query) || !Open(Start,End))continue;
        Gaps.Add({B,Bounds,Start,End,Room->Id,Room->Floor});
    }
    for(auto* B:Shells)B->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    int32 Protected=0;
    for(const auto& P:Gaps)
    {
        FHitResult Hit;
        const bool Blocked=World.SweepSingleByChannel(Hit,P.Start,P.End,FQuat::Identity,ECC_Pawn,Shape,Query);
        if(Blocked && Hit.GetComponent() && Hit.GetComponent()->ComponentHasTag(TEXT("NorthLegacyCollisionShell")))++Protected;
    }
    TestTrue(TEXT("The audit reproduces actual previously clear capsule paths into visible old solids"),Gaps.Num()>10);
    TestEqual(TEXT("All reproduced penetration paths are now blocked by a new simple shell"),Protected,Gaps.Num());
    const auto Walk=[&](const FVector& Start,const FVector& End)
    {
        Motion.StopMovementImmediately();Player->SetActorLocation(Start,false,nullptr,ETeleportType::TeleportPhysics);Motion.SetMovementMode(MOVE_Walking);
        for(int32 I=0;I<120;++I)
        {
            const FVector D=End-Player->GetActorLocation();if(D.Size2D()<5.)break;
            Motion.AddInputVector(D.GetSafeNormal2D()*FMath::Clamp(D.Size2D()/40.,.1,1.));Motion.TickComponent(1.f/60.f,LEVELTICK_All,nullptr);
        }
        Motion.StopMovementImmediately();return FVector::Dist2D(Player->GetActorLocation(),End)<6.;
    };
    int32 RealWalks=0;TSet<FName> WalkedRooms;
    for(const auto& P:Gaps)
    {
        if(RealWalks>=12 || WalkedRooms.Contains(P.Room) || P.Bounds.Max.Z-FindRoom(P.Room)->Bounds.Min.Z<75.)continue;
        for(auto* B:Shells)B->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        const bool Before=Walk(P.Start,P.End);
        for(auto* B:Shells)B->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        const bool After=Walk(P.Start,P.End);
        TestTrue(*FString::Printf(TEXT("Real walking into old solid in %s succeeds before and stops after repair"),*P.Room.ToString()),Before && !After);
        if(Before && !After){++RealWalks;WalkedRooms.Add(P.Room);}
    }
    TestTrue(TEXT("Multiple actual movement fixtures independently confirm the sweep audit"),RealWalks>=2);
    int32 PortalsChecked=0;
    for(const auto& P:Portals())
    {
        if(P.bWindow)continue;
        const FVector Lift(0,0,Half+2.);
        bool NewBlocked=false;TArray<FHitResult> Hits;
        World.SweepMultiByChannel(Hits,P.Center+Lift-P.Normal*85.,P.Center+Lift+P.Normal*85.,FQuat::Identity,ECC_Pawn,Shape,Query);
        for(const auto& H:Hits)NewBlocked|=H.GetComponent() && H.GetComponent()->ComponentHasTag(TEXT("NorthLegacyCollisionShell"));
        TestFalse(*FString::Printf(TEXT("Legacy repair keeps portal %s traversable"),*P.Id.ToString()),NewBlocked);++PortalsChecked;
    }
    int32 Guards=0;
    for(const auto& E:DuelNorthArchitecture::GuardEdges())
    {
        const FVector At=(E.Start+E.End)*.5+FVector(0,0,Half+2.);
        TestFalse(TEXT("A reachable original stair/garden guard stops the real capsule"),Open(At+E.Inward*95.,At-E.Inward*95.));++Guards;
    }
    AddInfo(FString::Printf(TEXT("LegacyCollision: furniture=%d masonry=%d shapes in %d components; previously-clear capsule penetrations=%d protected=%d; actual walking pairs=%d; portals=%d guards=%d; no distant-coast shells"),
        Furniture,Masonry,Shells.Num(),Gaps.Num(),Protected,RealWalks,PortalsChecked,Guards));
    return !HasAnyErrors();
}
#endif
