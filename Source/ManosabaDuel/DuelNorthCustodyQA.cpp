#if WITH_DEV_AUTOMATION_TESTS
#include "DuelGame.h"
#include "DuelNorthLayout.h"
#include "DuelNorthExtractionVisual.h"
#include "DuelNorthTower.h"
#include "Components/CapsuleComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthCustodyTest, "ManosabaDuel.NorthTower.Custody",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDuelNorthCustodyTest::RunTest(const FString& Parameters)
{
    using namespace DuelNorthLayout;
    if(!TestTrue(TEXT("Custody geometry QA runs in an isolated no-save North Tower"),
        FParse::Param(FCommandLine::Get(),TEXT("DuelNorthTower")) &&
        FParse::Param(FCommandLine::Get(),TEXT("DuelNoSave")))) return false;
    UWorld* World=nullptr;
    const ADuelFighter* Player=nullptr;
    for(const FWorldContext& Context:GEngine->GetWorldContexts())
        if(Context.World()) if(const auto* Mode=Context.World()->GetAuthGameMode<ADuelMode>())
            if(Mode->GetPlayerFighter()) { World=Context.World();Player=Mode->GetPlayerFighter();break; }
    if(!TestTrue(TEXT("Use the running player's real capsule and the generated collision world"),World && Player)) return false;
    TArray<FString> Errors;
    TestTrue(TEXT("Every authored route, including the custody branch, has fitting portals"),Validate(Errors));
    for(const FString& Error:Errors)AddError(Error);
    const auto* Court=FindRoom(TEXT("DungeonCourt"));
    if(!TestNotNull(TEXT("03 court remains present"),Court))return false;
    TestTrue(TEXT("03 retains its original clear volume"),
        Court->Bounds.Min.Equals(FVector(-2400,-1700,-1700)) && Court->Bounds.Max.Equals(FVector(2400,1700,-150)));
    const auto* CompanionCell=FindRoom(TEXT("CompanionCell"));
    TestTrue(TEXT("The existing companion cell retains its original volume"),CompanionCell &&
        CompanionCell->Bounds.Min.Equals(FVector(-3300,-1800,-1700)) &&
        CompanionCell->Bounds.Max.Equals(FVector(-2700,-1000,-1340)));
    TSet<FName> Reachable{FName(TEXT("DungeonAnte"))};
    bool bChanged=true;
    while(bChanged)
    {
        bChanged=false;
        for(const FPortal& P:Portals())
        {
            if(P.bWindow || P.Gate!=EGate::Open)continue;
            if(Reachable.Contains(P.RoomA) && !Reachable.Contains(P.RoomB)) { Reachable.Add(P.RoomB);bChanged=true; }
            if(Reachable.Contains(P.RoomB) && !Reachable.Contains(P.RoomA)) { Reachable.Add(P.RoomA);bChanged=true; }
        }
    }
    const TCHAR* Cells[]={TEXT("DungeonCell01"),TEXT("DungeonCell02"),TEXT("DungeonSharedCell"),
        TEXT("DungeonSideCell04"),TEXT("DungeonSideCell05"),TEXT("DungeonSharedCell06")};
    for(const TCHAR* Id:Cells)TestTrue(*FString::Printf(TEXT("%s is reachable before the fog without entering 03"),Id),Reachable.Contains(Id));
    const auto Labels=CustodyLabels();
    TestEqual(TEXT("All six corridor cells share one plaque/map label table"),Labels.Num(),6);
    double PreviousX=-TNumericLimits<double>::Max();
    for(int32 I=0;I<Labels.Num();++I)
    {
        const auto& Cell=Labels[I];
        const auto* Door=Portals().FindByPredicate([&](const FPortal& P){return P.Id==Cell.Door;});
        const UTextRenderComponent* Plaque=nullptr;
        for(const auto* Text:TInlineComponentArray<UTextRenderComponent*>(World->GetAuthGameMode<ADuelMode>()->NorthTower()))
            if(Text->ComponentHasTag(TEXT("NorthCustodyPlaque")) && Text->ComponentHasTag(Cell.Door)){Plaque=Text;break;}
        TestTrue(*FString::Printf(TEXT("%s uses sequential %02d on its real plaque and map, with no duplicate 03"),*Cell.Room.ToString(),I+4),
            FCString::Atoi(Cell.Number)==I+4 && FCString::Atoi(Cell.Number)!=3 &&
            FString(Cell.Name).StartsWith(Cell.Number) && Door && Door->RoomB==Cell.Room && Door->Center.X>PreviousX &&
            Plaque && Plaque->IsRegistered() && Plaque->IsVisible() && Plaque->Text.ToString()==Cell.Number);
        if(Door)PreviousX=Door->Center.X;
    }
    TestFalse(TEXT("The custody wing cannot bypass the closed combat fog"),Reachable.Contains(TEXT("DungeonCourt")));
    TestFalse(TEXT("The custody wing cannot bypass the post-battle exit"),Reachable.Contains(TEXT("DungeonAfter")));
    const auto* Route=FindRoute(TEXT("CustodyWalk"));
    if(!TestNotNull(TEXT("Authored custody inspection route"),Route))return false;
    const auto* Capsule=Player->GetCapsuleComponent();
    const FCollisionShape Shape=FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight());
    const FVector Lift(0,0,Capsule->GetScaledCapsuleHalfHeight()+2.5);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(NorthCustodyClearance),false);
    for(TActorIterator<ADuelFighter> It(World);It;++It)Query.AddIgnoredActor(*It);
    for(int32 I=1;I<Route->Points.Num();++I)
    {
        FHitResult Hit;
        const bool bBlocked=World->SweepSingleByChannel(Hit,Route->Points[I-1]+Lift,Route->Points[I]+Lift,
            FQuat::Identity,ECC_Pawn,Shape,Query);
        TestFalse(*FString::Printf(TEXT("Custody segment %d clears doors, open grilles and furniture (%s)"),I,*GetNameSafe(Hit.GetComponent())),bBlocked);
        FHitResult Floor;
        const FVector At=Route->Points[I];
        TestTrue(*FString::Printf(TEXT("Custody waypoint %d has a real floor"),I),
            World->LineTraceSingleByChannel(Floor,At+FVector(0,0,30),At-FVector(0,0,60),ECC_Visibility,Query));
    }
    const auto* Garden=FindRoom(TEXT("DawnGarden"));
    TestTrue(TEXT("The rescue ladder and radio have at least six metres of garden-edge clearance"),Garden &&
        Garden->Bounds.ExpandBy(FVector(-600,-600,0)).IsInside(FVector(DuelNorthExtractionVisual::LadderFoot.X,DuelNorthExtractionVisual::LadderFoot.Y,3500)) &&
        Garden->Bounds.ExpandBy(FVector(-600,-600,0)).IsInside(FVector(DuelNorthExtractionVisual::PickupMark.X,DuelNorthExtractionVisual::PickupMark.Y,3500)));
    return !HasAnyErrors();
}
#endif
