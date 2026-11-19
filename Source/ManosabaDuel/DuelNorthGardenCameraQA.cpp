#include "DuelGame.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Camera/CameraTypes.h"
#include "DuelNorthTower.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthGardenCameraTest,"ManosabaDuel.NorthTower.GardenCamera",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthGardenCameraTest::RunTest(const FString& Parameters)
{
    if(!TestTrue(TEXT("Garden camera QA uses an isolated no-save tower"),
        FParse::Param(FCommandLine::Get(),TEXT("DuelNorthTower")) &&
        FParse::Param(FCommandLine::Get(),TEXT("DuelNoSave"))))return false;
    ADuelNorthTower* Tower=nullptr;
    if(GEngine)for(const auto& C:GEngine->GetWorldContexts())if(C.World() && C.World()->IsGameWorld())
        for(TActorIterator<ADuelNorthTower> It(C.World());It;++It){Tower=*It;break;}
    auto* Mode=Tower?Tower->GetWorld()->GetAuthGameMode<ADuelMode>():nullptr;
    auto* Player=Mode?const_cast<ADuelFighter*>(Mode->GetPlayerFighter()):nullptr;
    if(!TestTrue(TEXT("Live tower, mode and player exist"),Tower && Mode && Player))return false;
    Tower->NewGame(*Mode);
    ON_SCOPE_EXIT {Tower->NewGame(*Mode);};
    constexpr float Step=1.f/60.f;
    const auto Place=[&](double X)
    {Player->SetActorLocation(FVector(X,0,3462),false,nullptr,ETeleportType::TeleportPhysics);};
    const auto Tick=[&]()
    {Player->TickCamera(Step);Player->CameraBoom->TickComponent(Step,LEVELTICK_All,nullptr);};
    const auto Hold=[&](int32 Frames){for(int32 I=0;I<Frames;++I)Tick();};
    Place(3000);Player->PrepareExploration();Hold(120);
    TestTrue(TEXT("The indoor exploration view retains its original pitch"),FMath::IsNearlyEqual(Player->ViewPitch,-15.f,.01f));
    float MaxPitchStep=0;
    for(int32 I=0;I<=420;++I)
    {
        Place(FMath::Min(6000.,3000.+455.*I*Step));
        const float Before=Player->ViewPitch;Tick();
        MaxPitchStep=FMath::Max(MaxPitchStep,FMath::Abs(Player->ViewPitch-Before));
    }
    Hold(120);
    FMinimalViewInfo Actual;Player->CalcCamera(0.f,Actual);
    TestTrue(TEXT("The normal outdoor camera naturally settles at two degrees up"),
        FMath::IsNearlyEqual(Player->ViewPitch,2.f,.12f) && FMath::IsNearlyEqual(Actual.Rotation.Pitch,2.f,.12f));
    TestTrue(TEXT("The sampled indoor-to-garden pitch changes smoothly at sixty hertz"),MaxPitchStep<.5f);
    Player->LookPitch(-3.f);const float ManualPitch=Player->ViewPitch;Hold(120);
    TestTrue(TEXT("Manual vertical look immediately cancels the garden guidance"),
        Player->bGardenCameraManual && FMath::IsNearlyEqual(Player->ViewPitch,ManualPitch,.001f));
    Place(3000);Hold(240);
    TestTrue(TEXT("Returning indoors retains the user's deliberate view angle"),FMath::IsNearlyEqual(Player->ViewPitch,ManualPitch,.001f));
    Player->PrepareExploration();Place(6000);Hold(240);Place(3000);Hold(240);
    TestTrue(TEXT("A guided view returns smoothly to its indoor default"),FMath::IsNearlyEqual(Player->ViewPitch,-15.f,.1f));
    Player->PrepareExploration();Place(6000);Player->ViewPitch=-10.f;Hold(120);
    TestTrue(TEXT("Entering with an already chosen pitch does not override it"),FMath::IsNearlyEqual(Player->ViewPitch,-10.f,.001f));
    AddInfo(FString::Printf(TEXT("GardenCamera: native FOV %.1f, outdoor pitch %.3f, maximum sixty-hertz transition %.4f degrees; manual override retained"),
        Actual.FOV,Actual.Rotation.Pitch,MaxPitchStep));
    return !HasAnyErrors();
}
#endif
