#include "DuelNorthLayout.h"
#include "DuelNorthArchitecture.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DuelGame.h"
#include "CollisionShape.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthTraversalTest, "ManosabaDuel.NorthTower.Traversal",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthTraversalTest::RunTest(const FString& Parameters)
{
    using namespace DuelNorthLayout;
    ADuelMode* Mode=nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        if (Context.World() && (Mode=Context.World()->GetAuthGameMode<ADuelMode>())) break;
    }
    if (!TestTrue(TEXT("Use an isolated -DuelNorthTower -DuelNoSave game world"),
        FParse::Param(FCommandLine::Get(),TEXT("DuelNorthTower")) &&
        FParse::Param(FCommandLine::Get(),TEXT("DuelNoSave")) && Mode && Mode->GetPlayerFighter())) return false;

    UWorld* World=Mode->GetWorld();
    const ADuelFighter* Player=Mode->GetPlayerFighter();
    const float Radius=Player->GetCapsuleComponent()->GetScaledCapsuleRadius();
    const float HalfHeight=Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const float StepAllowance=FMath::Min(24.f,Player->GetCharacterMovement()->MaxStepHeight);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(NorthTowerTraversal),false,Player);
    int32 IgnoredGates=0;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->IsA<APawn>()) Query.AddIgnoredActor(*It);
        TInlineComponentArray<UPrimitiveComponent*> Components(*It);
        for (UPrimitiveComponent* Component : Components)
        {
            // Deliberately exclude only movable lock/fog barriers. Frames, walls and
            // all static construction remain in the collision query.
            if (Component->ComponentHasTag(TEXT("NorthTowerGate")))
            {
                Query.AddIgnoredComponent(Component);
                ++IgnoredGates;
            }
        }
    }
    const FRoute* Route=FindRoute(TEXT("MainWalk"));
    if (!TestNotNull(TEXT("Authored full main route"),Route)) return false;
    int32 Problems=0, Samples=0, RoofSamples=0;
    auto Fail=[&](const FString& Text)
    {
        if (Problems<24) AddError(Text);
        ++Problems;
    };
    const FCollisionShape Body=FCollisionShape::MakeCapsule(Radius,HalfHeight);
    for (int32 Segment=1; Segment<Route->Points.Num(); ++Segment)
    {
        const FVector A=Route->Points[Segment-1], B=Route->Points[Segment];
        const int32 Count=FMath::Max(1,FMath::CeilToInt(FVector::Dist(A,B)/40.f));
        FVector Previous=FVector::ZeroVector;
        bool bHavePrevious=false;
        for (int32 Index=0; Index<=Count; ++Index)
        {
            ++Samples;
            const FVector Expected=FMath::Lerp(A,B,static_cast<float>(Index)/Count);
            FHitResult Ground;
            if (!World->LineTraceSingleByChannel(Ground,Expected+FVector(0,0,60),Expected-FVector(0,0,80),ECC_Pawn,Query) ||
                Ground.ImpactNormal.Z<.65f || FMath::Abs(Ground.ImpactPoint.Z-Expected.Z)>23)
            {
                Fail(FString::Printf(TEXT("Main route ground discontinuity at segment %d near %s: hit=%s impact=%s"),
                    Segment,*Expected.ToString(),*GetNameSafe(Ground.GetComponent()),*Ground.ImpactPoint.ToString()));
                bHavePrevious=false;
                continue;
            }
            const FVector Centre(Expected.X,Expected.Y,Ground.ImpactPoint.Z+HalfHeight+2);
            // The body can legitimately rest one riser higher than a centreline ray
            // near a tread edge. Check the same step allowance CharacterMovement has.
            const bool bBodyClear=!World->OverlapBlockingTestByChannel(Centre,FQuat::Identity,ECC_Pawn,Body,Query);
            const FVector Raised=Centre+FVector(0,0,StepAllowance);
            if (!bBodyClear && World->OverlapBlockingTestByChannel(Raised,FQuat::Identity,ECC_Pawn,Body,Query))
                Fail(FString::Printf(TEXT("Main route capsule blocked at segment %d near %s"),Segment,*Centre.ToString()));
            if (bHavePrevious)
            {
                FHitResult Obstacle;
                if (World->SweepSingleByChannel(Obstacle,Previous,Raised,FQuat::Identity,ECC_Pawn,Body,Query))
                    Fail(FString::Printf(TEXT("Main route swept capsule blocked at segment %d near %s by %s"),
                        Segment,*Obstacle.ImpactPoint.ToString(),*GetNameSafe(Obstacle.GetComponent())));
            }
            Previous=Raised;
            bHavePrevious=true;
        }
    }

    // Ceiling collision coverage is a useful closure check; it does not establish
    // rendered opacity, correct shadows or freedom from visible light leaks.
    for (const FRoom& Room : Rooms())
    {
        if (!Room.bRoof || !Room.bEnclose || Room.Kind==ERoomKind::Stairwell) continue;
        for (float X : {.25f,.5f,.75f}) for (float Y : {.25f,.5f,.75f})
        {
            ++RoofSamples;
            const FVector Point(FMath::Lerp(Room.Bounds.Min.X,Room.Bounds.Max.X,X),
                FMath::Lerp(Room.Bounds.Min.Y,Room.Bounds.Max.Y,Y),Room.Bounds.Min.Z+200);
            FHitResult Roof;
            if (!World->LineTraceSingleByChannel(Roof,Point,FVector(Point.X,Point.Y,Room.Bounds.Max.Z+90),ECC_Pawn,Query))
                Fail(FString::Printf(TEXT("No ceiling collision above %s near %s"),*Room.Id.ToString(),*Point.ToString()));
        }
    }
    AddInfo(FString::Printf(TEXT("North Tower geometry traversal sampling: %d floor/body samples, %d roof rays, %d ignored lock barriers, capsule %.1f x %.1f cm, %d defects. This is collision sampling, not player-input traversal or visual leak acceptance."),
        Samples,RoofSamples,IgnoredGates,Radius,HalfHeight,Problems));
    return Problems==0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthEdgeGuardTest, "ManosabaDuel.NorthTower.EdgeGuards",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthEdgeGuardTest::RunTest(const FString& Parameters)
{
    ADuelMode* Mode=nullptr;
    for (const FWorldContext& Context:GEngine->GetWorldContexts())
        if (Context.World() && (Mode=Context.World()->GetAuthGameMode<ADuelMode>())) break;
    if (!TestTrue(TEXT("Use an isolated -DuelNorthTower -DuelNoSave game world"),
        FParse::Param(FCommandLine::Get(),TEXT("DuelNorthTower")) &&
        FParse::Param(FCommandLine::Get(),TEXT("DuelNoSave")) && Mode && Mode->GetPlayerFighter())) return false;

    TArray<FString> Errors;
    DuelNorthArchitecture::Validate(Errors);
    for (const FString& Error:Errors) AddError(Error);
    UWorld* World=Mode->GetWorld();
    const ADuelFighter* Player=Mode->GetPlayerFighter();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(NorthTowerEdgeGuards),false,Player);
    int32 GuardComponents=0;
    for (TActorIterator<AActor> It(World);It;++It)
    {
        TInlineComponentArray<UPrimitiveComponent*> Components(*It);
        for (UPrimitiveComponent* C:Components)
        {
            // Walls/floors cannot accidentally make an absent railing test pass.
            if (C->ComponentHasTag(TEXT("NorthTowerGuard"))) ++GuardComponents;
            else Query.AddIgnoredComponent(C);
        }
    }
    if (!TestTrue(TEXT("Architecture helper installed actual guard collisions"),GuardComponents>0)) return false;
    const float Radius=Player->GetCapsuleComponent()->GetScaledCapsuleRadius();
    const float HalfHeight=Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FCollisionShape Body=FCollisionShape::MakeCapsule(Radius,HalfHeight);
    int32 Samples=0,Failures=0;
    for (const auto& E:DuelNorthArchitecture::GuardEdges())
    {
        // Include the capsule near the ends and every 80 cm through the edge span.
        const int32 Count=FMath::Max(2,FMath::CeilToInt(FVector::Dist2D(E.Start,E.End)/80.f));
        for (int32 I=0;I<Count;++I)
        {
            ++Samples;
            const FVector Point=FMath::Lerp(E.Start,E.End,(I+.5f)/Count)+FVector(0,0,HalfHeight+5);
            FHitResult Hit;
            const bool bBlocked=World->SweepSingleByChannel(Hit,Point+E.Inward*90,Point-E.Inward*70,
                FQuat::Identity,ECC_Pawn,Body,Query);
            if (!bBlocked || !Hit.GetComponent() || !Hit.GetComponent()->ComponentHasTag(TEXT("NorthTowerGuard")))
            {
                if (Failures<20) AddError(FString::Printf(TEXT("Unprotected edge %s at %s"),*E.Id.ToString(),*Point.ToString()));
                ++Failures;
            }
        }
    }
    // A solid courtyard perimeter must preserve the authored bridge entrance.
    FHitResult Entrance;
    const FVector BridgeInside(5000,0,3400+HalfHeight+2),GardenInside(5300,0,3400+HalfHeight+2);
    TestFalse(TEXT("DawnBridge opening stays clear of guard collision"),
        World->SweepSingleByChannel(Entrance,BridgeInside,GardenInside,FQuat::Identity,ECC_Pawn,Body,Query));
    AddInfo(FString::Printf(TEXT("North Tower edge protection: %d guard components, %d edge sweeps, %d missing barriers. MainWalk collision clearance is checked separately by Traversal."),
        GuardComponents,Samples,Failures));
    return Errors.IsEmpty() && Failures==0;
}
#endif
