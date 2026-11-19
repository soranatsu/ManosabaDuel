#include "DuelNorthScenery.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DuelNorthTower.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthNightSkyTest, "ManosabaDuel.NorthTower.NightSky",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthNightSkyTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    if (!TestTrue(TEXT("Night sky QA requires an isolated save-free tower"),
        FParse::Param(FCommandLine::Get(), TEXT("DuelNorthTower")) &&
        FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")))) return false;
    ADuelNorthTower* Tower = nullptr;
    if (GEngine) for (const auto& Context : GEngine->GetWorldContexts())
        if (Context.World() && Context.World()->IsGameWorld())
            for (TActorIterator<ADuelNorthTower> It(Context.World()); It; ++It) { Tower = *It; break; }
    if (!TestNotNull(TEXT("Live North Tower exists"), Tower)) return false;
    const FName Tags[] = {TEXT("NorthNightSky346"), TEXT("NorthVisibleMoon"), TEXT("NorthDriftingCloudLayer")};
    const FName Materials[] = {TEXT("M_NorthNightSky346"), TEXT("M_NorthMoon346"), TEXT("M_NorthMoonlitClouds346")};
    UStaticMeshComponent* Moon = nullptr;
    for (int32 I = 0; I < UE_ARRAY_COUNT(Tags); ++I)
    {
        int32 Count = 0;
        for (auto* Mesh : TInlineComponentArray<UStaticMeshComponent*>(Tower))
        {
            if (!Mesh->ComponentHasTag(Tags[I])) continue;
            ++Count;
            TestTrue(TEXT("Sky objects are visible, shadow-free and do not obstruct gameplay"),
                Mesh->IsVisible() && !Mesh->CastShadow && Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
            TestTrue(TEXT("Sky-sized geometry is excluded from Nanite, ray tracing and distance-field lighting"),
                Mesh->IsDisallowNanite() && !Mesh->bVisibleInRayTracing && !Mesh->bAffectDistanceFieldLighting);
            TestTrue(TEXT("Each object loads the unique 346 material instead of a fallback"),
                Mesh->GetMaterial(0) && Mesh->GetMaterial(0)->GetFName() == Materials[I]);
            if (I == 1) Moon = Mesh;
            if (I == 0) TestTrue(TEXT("Star background lies beyond the complete ocean horizon"),
                Mesh->Bounds.SphereRadius > 6500000.);
        }
        TestEqual(TEXT("Each sky layer is a single low-cost sphere"), Count, 1);
    }
    if (!Moon) return false;
    // The real player's camera uses a 532 cm arm, 76 cm shoulder, 115 cm pivot and 68-degree horizontal FOV.
    for (const FVector Player : {FVector(5500, 0, 3462), FVector(7350, -950, 3462)})
        for (const float Pitch : {2.f, 0.f})
        {
            const FRotator Rotation(Pitch, 0, 0);
            const FVector Camera = Player + FVector(0, 0, 115) + Rotation.RotateVector(FVector(-532, 76, 9));
            const FVector Delta = Moon->GetComponentLocation() - Camera;
            const auto Basis = FRotationMatrix(Rotation);
            const double Radius = Moon->Bounds.SphereRadius;
            const double Forward = FVector::DotProduct(Delta, Basis.GetUnitAxis(EAxis::X)) - Radius;
            const double Side = FMath::Abs(FVector::DotProduct(Delta, Basis.GetUnitAxis(EAxis::Y))) + Radius;
            const double Up = FMath::Abs(FVector::DotProduct(Delta, Basis.GetUnitAxis(EAxis::Z))) + Radius;
            const double HalfX = FMath::Tan(FMath::DegreesToRadians(34.));
            const double Elevation=FMath::RadiansToDegrees(FMath::Atan2(Delta.Z,Delta.Size2D()));
            TestTrue(TEXT("The moon is elevated twelve to sixteen degrees above the garden horizon"),Elevation>=12. && Elevation<=16.);
            TestTrue(TEXT("The complete moon fits the guided default and manually level 16:9 garden views"),
                Forward > 0 && Side < Forward * HalfX && Up < Forward * HalfX / (16. / 9.));
        }
    return !HasAnyErrors();
}

#endif
