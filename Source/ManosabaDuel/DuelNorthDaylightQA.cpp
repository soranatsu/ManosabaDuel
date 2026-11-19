#include "DuelNorthLighting.h"
#include "DuelNorthLayout.h"
#include "DuelNorthTower.h"
#include "DuelGame.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "DuelNorthRose.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Components/SkyLightComponent.h"
#include "Engine/Engine.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProceduralMeshComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthGardenDaylightTest,"ManosabaDuel.NorthTower.GardenDaylight",
    EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FDuelNorthGardenDaylightTest::RunTest(const FString& Parameters)
{
    ADuelMode* Mode=nullptr;
    for(const FWorldContext& C:GEngine->GetWorldContexts())
        if(C.World()&&(Mode=C.World()->GetAuthGameMode<ADuelMode>()))break;
    if(!TestTrue(TEXT("Run isolated -DuelNorthTower -DuelNoSave"),Mode&&Mode->GetPlayerFighter()&&
        FParse::Param(FCommandLine::Get(),TEXT("DuelNorthTower"))&&FParse::Param(FCommandLine::Get(),TEXT("DuelNoSave"))))return false;
    ADuelNorthTower* Tower=nullptr;
    for(TActorIterator<ADuelNorthTower> I(Mode->GetWorld());I;++I){Tower=*I;break;}
    if(!TestNotNull(TEXT("North Tower"),Tower))return false;
    TInlineComponentArray<UDirectionalLightComponent*> Suns(Tower);
    if(!TestEqual(TEXT("One fixed moonlight; no outdoor light pool"),Suns.Num(),1))return false;
    auto* Sun=Suns[0];const FVector TowardSun=-Sun->GetForwardVector();
    TestTrue(TEXT("Cool moonlight shines from the garden side toward the tower"),Sun->IsVisible()&&Sun->CastShadows&&
        Sun->Intensity==53.f&&Sun->Temperature==9100.f&&TowardSun.X>.7&&TowardSun.Z>.5);
    FCollisionQueryParams Ray(SCENE_QUERY_STAT(NorthGardenSun),false);
    Ray.AddIgnoredActor(Mode->GetPlayerFighter());if(Mode->GetBossFighter())Ray.AddIgnoredActor(Mode->GetBossFighter());
    int32 Clear=0,Blocked=0;
    for(const FVector& P:{FVector(5500,350,3510),FVector(5900,-400,3510),FVector(6500,0,3510),FVector(7150,0,3510)})
    {
        FHitResult Hit;const bool Occluded=Mode->GetWorld()->LineTraceSingleByChannel(Hit,P,P+TowardSun*20000,ECC_Visibility,Ray);
        TestFalse(*FString::Printf(TEXT("Garden has real direct sunlight at %s"),*P.ToString()),Occluded);if(!Occluded)++Clear;
    }
    for(const FVector& P:{FVector(4300,0,3510),FVector(2800,0,3510),FVector(-1500,-2600,3510),FVector(1090,-2430,2550)})
    {
        FHitResult Hit;const bool Occluded=Mode->GetWorld()->LineTraceSingleByChannel(Hit,P,P+TowardSun*20000,ECC_Visibility,Ray);
        TestTrue(*FString::Printf(TEXT("Covered bridge/stair/corridor remains shaded at %s"),*P.ToString()),Occluded);if(Occluded)++Blocked;
    }
    const FTransform Pose=Sun->GetComponentTransform();const float Lux=Sun->Intensity;
    for(const FVector& P:{FVector(6500,0,3462),FVector(4300,0,3462),FVector(0,0,3462)})
        for(int32 I=0;I<120;++I)DuelNorthLighting::Update(Tower,P,1.f/30);
    TestTrue(TEXT("Walking does not move or switch sunlight"),Sun->IsVisible()&&Sun->Intensity==Lux&&Sun->GetComponentTransform().Equals(Pose));
    DuelNorthLighting::Update(Tower,Mode->GetPlayerFighter()->GetActorLocation(),.51f);
    AddInfo(FString::Printf(TEXT("Real collision rays: %d/4 sunlit garden points and %d/4 protected indoor points. Does not establish rendered exposure, sky brightness, or GPU frame time."),Clear,Blocked));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthSideWindowsTest, "ManosabaDuel.NorthTower.SideWindows",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDuelNorthSideWindowsTest::RunTest(const FString& Parameters)
{
    using namespace DuelNorthLayout;
    ADuelMode* Mode = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
        if (Context.World() && (Mode = Context.World()->GetAuthGameMode<ADuelMode>())) break;
    if (!TestTrue(TEXT("Run isolated -DuelNorthTower -DuelNoSave"), Mode &&
        FParse::Param(FCommandLine::Get(), TEXT("DuelNorthTower")) && FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")))) return false;
    UWorld* World = Mode->GetWorld();
    ADuelNorthTower* Tower = nullptr;
    for (TActorIterator<ADuelNorthTower> It(World); It; ++It) { Tower = *It; break; }
    const FRoom* Court = FindRoom(TEXT("FinalCourt"));
    if (!TestTrue(TEXT("The Final Court is running"), Tower && Court)) return false;
    TInlineComponentArray<UDirectionalLightComponent*> Suns(Tower);
    if (!TestEqual(TEXT("Windows share the existing daylight"), Suns.Num(), 1)) return false;
    const FVector TowardSun = -Suns[0]->GetForwardVector();

    FCollisionQueryParams BodyRay(SCENE_QUERY_STAT(NorthSideWindow), false);
    if (Mode->GetPlayerFighter()) BodyRay.AddIgnoredActor(Mode->GetPlayerFighter());
    if (Mode->GetBossFighter()) BodyRay.AddIgnoredActor(Mode->GetBossFighter());
    FCollisionQueryParams LightRay = BodyRay;
    TInlineComponentArray<UBoxComponent*> Panes(Tower);
    for (auto* Pane : Panes) if (Pane->ComponentHasTag(TEXT("NorthWindowPane"))) LightRay.AddIgnoredComponent(Pane);

    int32 North = 0, South = 0;
    for (const FPortal& P : Portals())
    {
        if (!P.bWindow || P.RoomA != Court->Id || DuelNorthRose::IsRound(P)) continue;
        const bool bSunnySide = P.Center.Y < 0;
        ++(bSunnySide ? South : North);
        const FVector Out(0, bSunnySide ? -1. : 1., 0);
        const FVector Centre = P.Center + FVector(0, 0, P.Height * .5);
        TestTrue(TEXT("Side opening is square and cuts the Y wall"), FMath::IsNearlyEqual(P.Width, P.Height) && FMath::Abs(P.Normal.Y) > .9);
        const auto ThroughWall = [&](const FVector& At, ECollisionChannel Channel, const FCollisionQueryParams& Params, FHitResult& Hit)
        { return World->LineTraceSingleByChannel(Hit, At - Out * 45, At + Out * 45, Channel, Params); };
        FHitResult Hit;
        TestTrue(TEXT("Pane keeps the player inside the court"), ThroughWall(Centre, ECC_Pawn, BodyRay, Hit) &&
            Hit.GetComponent() && Hit.GetComponent()->ComponentHasTag(TEXT("NorthWindowPane")));
        const FVector IronCentre = P.Center + FVector(0, 0, P.Height * .36);
        TestTrue(TEXT("The recessed iron casts a physical shadow across the square"), ThroughWall(IronCentre, ECC_Visibility, LightRay, Hit) &&
            Hit.GetComponent() && Hit.GetComponent()->ComponentHasTag(TEXT("NorthFinalSideIron")));
        for (double X : {-132., 132.}) for (double Z : {-132., 132.})
            TestFalse(TEXT("All four square corners open to light"), ThroughWall(Centre + FVector(X, 0, Z), ECC_Visibility, LightRay, Hit));
        TestTrue(TEXT("Masonry above the square stays opaque"), ThroughWall(Centre + FVector(0, 0, P.Height * .5 + 35), ECC_Visibility, LightRay, Hit));
        TestTrue(TEXT("Masonry beside the square stays opaque"), ThroughWall(Centre + FVector(P.Width * .5 + 35, 0, 0), ECC_Visibility, LightRay, Hit));
        if (bSunnySide && TowardSun.Y < -.1 && TowardSun.Z > .1)
        {
            const FVector FloorPatch = Centre - TowardSun * ((Centre.Z - Court->Bounds.Min.Z - 5.) / TowardSun.Z);
            TestTrue(TEXT("Fixed sun projects the opening onto the walkable court"), Court->Bounds.IsInside(FloorPatch));
            TestFalse(TEXT("Floor patch sees the fixed sun through the side square"), World->LineTraceSingleByChannel(Hit,
                FloorPatch, FloorPatch + TowardSun * 16000., ECC_Visibility, LightRay));
        }
    }
    TestEqual(TEXT("Two portals in each long wall"), North, 2);
    TestEqual(TEXT("Two openings face the cool direct moonlight"), South, 2);
    TestTrue(TEXT("The fixed sun still faces the south squares"), TowardSun.Y < -.1 && TowardSun.Z > .1);

    TInlineComponentArray<UInstancedStaticMeshComponent*> Instances(Tower);
    int32 FramePieces = 0, IronPieces = 0, GlassPanes = 0;
    for (auto* C : Instances)
    {
        if (C->ComponentHasTag(TEXT("NorthFinalSideFrame")))
        {
            FramePieces += C->GetInstanceCount();
            TestTrue(TEXT("Dressed stone frames cast shadows"), C->CastShadow && C->GetMaterial(0));
        }
        if (C->ComponentHasTag(TEXT("NorthFinalSideIron")))
        {
            IronPieces += C->GetInstanceCount();
            TestTrue(TEXT("The ironwork casts shadows"), C->CastShadow && C->GetMaterial(0));
        }
        if (C->ComponentHasTag(TEXT("NorthFinalSideGlass")))
        {
            GlassPanes += C->GetInstanceCount();
            const auto* Material = C->GetMaterial(0);
            TestTrue(TEXT("Each clear single-layer pane transmits the fixed sunlight"),
                C->GetStaticMesh() && Material && Material->GetBlendMode() == BLEND_Translucent && !C->CastShadow);
        }
    }
    TestEqual(TEXT("Four stone edges per square"), FramePieces, 16);
    TestEqual(TEXT("Four iron pickets and two rails per square"), IronPieces, 24);
    TestEqual(TEXT("One thin glazing layer per square"), GlassPanes, 4);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthPrisonWindowsTest, "ManosabaDuel.NorthTower.PrisonWindows",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDuelNorthPrisonWindowsTest::RunTest(const FString& Parameters)
{
    using namespace DuelNorthLayout;
    ADuelMode* Mode = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
        if (Context.World() && (Mode = Context.World()->GetAuthGameMode<ADuelMode>())) break;
    if (!TestTrue(TEXT("Run isolated -DuelNorthTower -DuelNoSave"), Mode && Mode->GetPlayerFighter() &&
        FParse::Param(FCommandLine::Get(), TEXT("DuelNorthTower")) && FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")))) return false;
    UWorld* World = Mode->GetWorld();
    ADuelNorthTower* Tower = nullptr;
    for (TActorIterator<ADuelNorthTower> It(World); It; ++It) { Tower = *It; break; }
    if (!TestNotNull(TEXT("North Tower"), Tower)) return false;
    FCollisionQueryParams Ray(SCENE_QUERY_STAT(NorthPrisonWindow), false);
    Ray.AddIgnoredActor(Mode->GetPlayerFighter());
    if (Mode->GetBossFighter()) Ray.AddIgnoredActor(Mode->GetBossFighter());
    FHitResult Hit;
    const auto Trace = [&](const FVector& Start, const FVector& End)
    { return World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Ray); };

    const FVector FormerOpenings[] = {{-1000, 1500, 535}, {1200, 1500, 535}, {-2400, 0, 1365}, {2400, 0, 1365}};
    for (const FVector& Centre : FormerOpenings)
    {
        const FVector Normal = Centre.Y > 0 ? FVector::RightVector : FVector::ForwardVector;
        const FVector Across = FVector::CrossProduct(FVector::UpVector, Normal);
        for (double U : {-70., 0., 70.}) for (double Z : {-42., 0., 42.})
        {
            const FVector At = Centre + Across * U + FVector(0, 0, Z);
            TestTrue(TEXT("The former interior window wall is solid masonry"), Trace(At - Normal * 42., At + Normal * 42.) &&
                Hit.GetActor() == Tower && Hit.GetComponent() && !Hit.GetComponent()->ComponentHasTag(TEXT("NorthWindowPane")));
        }
    }

    const auto* PlayerCapsule = Mode->GetPlayerFighter()->GetCapsuleComponent();
    const FCollisionShape Capsule = FCollisionShape::MakeCapsule(PlayerCapsule->GetScaledCapsuleRadius(), PlayerCapsule->GetScaledCapsuleHalfHeight());
    int32 Slots = 0;
    for (const FPortal& Window : Portals())
    {
        if (!Window.bWindow || Window.RoomA == TEXT("FinalCourt")) continue;
        const FRoom* Room = FindRoom(Window.RoomA);
        if (!TestTrue(TEXT("Window on an exposed outside face"), Room && IsExteriorWindow(Window))) continue;
        if (DuelNorthRose::IsRound(Window)) continue;
        ++Slots;
        const FVector Out = WindowOutward(Window, *Room);
        const FVector Mid = Window.Center + FVector(0, 0, Window.Height * .5);
        TestFalse(*FString::Printf(TEXT("Clear air gap through %s"), *Window.Id.ToString()), Trace(Mid - Out * 90., Mid + Out * 140.));
        const FVector Rail = Window.Center + FVector(0, 0, Window.Height * .36);
        TestTrue(TEXT("Physical iron is recessed behind both stone faces"), Trace(Rail - Out * 90., Rail + Out * 140.) &&
            Hit.GetComponent() && Hit.GetComponent()->ComponentHasTag(TEXT("NorthPrisonWindowIron")) &&
            FMath::Abs(FVector::DotProduct(Hit.ImpactPoint - Rail, Out)) < 14.);
        const FVector Sill = Window.Center + FVector(0, 0, 4.);
        TestTrue(TEXT("The sill is a solid projection ahead of the grate"), Trace(Sill + Out * 135., Sill - Out * 90.) &&
            Hit.GetComponent() && Hit.GetComponent()->ComponentHasTag(TEXT("NorthPrisonWindowStone")) &&
            FVector::DotProduct(Hit.ImpactPoint - Sill, Out) > WallThickness);
        TestTrue(TEXT("A player-sized capsule cannot pass through the barred opening"), World->SweepSingleByChannel(Hit,
            Mid - Out * 115., Mid + Out * 170., FQuat::Identity, ECC_Pawn, Capsule, Ray));
    }
    TestEqual(TEXT("Two prison and three sea openings have recessed iron grilles"), Slots, 5);
    const TInlineComponentArray<UProceduralMeshComponent*> Panes(Tower);
    for (const FName WindowId : {FName(TEXT("ChapelNorthWindowWest")), FName(TEXT("ChapelNorthWindowEast"))})
    {
        int32 StainedPanes = 0;
        for (const auto* Glass : Panes)
        {
            if (!Glass->ComponentHasTag(WindowId) || !Glass->ComponentHasTag(TEXT("NorthRoseGlass"))) continue;
            ++StainedPanes;
            TestTrue(TEXT("Chapel glazing allows coloured light through"),
                !Glass->CastShadow && Glass->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
            TestEqual(TEXT("Chapel pane uses the coloured rose glass finish"), GetNameSafe(Glass->GetMaterial(0)),
                FString(TEXT("M_NorthRoseGlazing")));
        }
        TestEqual(*FString::Printf(TEXT("%s has one circular stained glass pane"), *WindowId.ToString()), StainedPanes, 1);
    }
    for (double X : {-1400., 0., 1400.})
    {
        const FVector View(X, -2200, 1000);
        const FVector Shore(X / 1400. * 4000., X == 0. ? -4550. : -5000., X == 0. ? -650. : -800.);
        TestFalse(TEXT("Each sea-window grille preserves a clear view to the shore"), Trace(View, Shore));
    }
    return !HasAnyErrors();
}
#endif
