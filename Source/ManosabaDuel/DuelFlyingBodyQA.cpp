#include "DuelGame.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "DuelArena.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "ProceduralMeshComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelFlyingBodyQA, "ManosabaDuel.FlyingBodies346",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelFlyingBodyQA::RunTest(const FString&)
{
    if (!TestTrue(TEXT("Flying-body QA disables saves in its isolated game"), FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")))) return false;
    ADuelMode* Mode = nullptr;
    for (const auto& Context : GEngine->GetWorldContexts())
        if (Context.World() && (Mode = Context.World()->GetAuthGameMode<ADuelMode>())) break;
    if (!TestTrue(TEXT("The real arena and both fighters exist"), Mode && Mode->Arena && Mode->PlayerFighter && Mode->BossFighter)) return false;
    auto* Player = Mode->PlayerFighter.Get();
    auto* Boss = Mode->BossFighter.Get();
    const auto Character = Boss->Character;
    const auto Shield = Player->Offhand;
    ON_SCOPE_EXIT { Boss->Character = Character; Player->SetWeapon(Shield, EDuelHand::Left); Mode->ResetEncounter(); };
    struct FCase { EDuelCharacter Character; EDuelMove Move; const TCHAR* Label; };
    const FCase Cases[] = {{EDuelCharacter::Ema, EDuelMove::OssuaryShears, TEXT("Ema's six-feather bone wing")},
        {EDuelCharacter::Hiro, EDuelMove::HoundShade, TEXT("Hiro's hound shade")},
        {EDuelCharacter::Yuki, EDuelMove::MirrorRicochet, TEXT("Yuki's mirror projectile")}};
    TArray<int32> Triangles;
    const auto Reset = [&](const FCase& Case)
    {
        Mode->ResetEncounter();
        const FVector Origin = Mode->Arena->CourtOrigin();
        Player->SetActorLocationAndRotation(Origin + FVector(0, 0, 62), FRotator::ZeroRotator);
        Boss->SetActorLocationAndRotation(Origin + FVector(650, 0, 103), FRotator(0, 180, 0));
        Boss->Character = Case.Character;
        Boss->BossPhase = 3;
        Player->SetWeapon(EDuelWeapon::Empty, EDuelHand::Left);
        return Boss->StartMove(Case.Move);
    };
    for (const auto& Case : Cases)
    {
        if (!TestTrue(Case.Label, Reset(Case)) || Boss->MapWaves.IsEmpty()) continue;
        auto* Body = Cast<UProceduralMeshComponent>(Boss->MapWaves[0].Preview.Get());
        if (!TestNotNull(TEXT("The independent projectile owns a real mesh"), Body)) continue;
        const auto* Section = Body->GetProcMeshSection(0);
        auto* Surface = Cast<UMaterialInstanceDynamic>(Body->GetMaterial(0));
        if (!TestTrue(TEXT("The flying body has geometry and a live material instance"), Section && Surface && Surface->GetMaterial())) return false;
        FLinearColor Fill = FLinearColor::Black, Edge = FLinearColor::Black;
        TestTrue(TEXT("A closed flying mesh uses the unlit 3D facet shader instead of a planar blade-wake shader"),
            Section && !Section->ProcIndexBuffer.IsEmpty() && Surface && Surface->GetMaterial() &&
            Surface->GetMaterial()->GetFName() == TEXT("M_NorthSpellFacet") &&
            Surface->GetMaterial()->GetShadingModels().HasShadingModel(MSM_Unlit) &&
            Surface->GetVectorParameterValue(FName(TEXT("Tint")), Fill) && Surface->GetVectorParameterValue(FName(TEXT("HotFacet")), Edge) &&
            FMath::Max3(Edge.R, Edge.G, Edge.B) > 1.f && Body->GetProcMeshSection(1));
        if (Section) Triangles.Add(Section->ProcIndexBuffer.Num());
        if (Case.Character == EDuelCharacter::Ema)
            TestTrue(TEXT("Ema's airborne wing has its own ivory-gold ridges rather than the frost or shadow palette"),
                Edge.R > Edge.G && Edge.G > Edge.B && Fill.R > Fill.B && Body->ComponentHasTag(TEXT("EmaFlyingBoneWing346")));
        const FVector Caster = Boss->GetActorLocation();
        FVector First;
        for (int32 Frame = 0; Frame < 5; ++Frame)
        {
            const float Age = Boss->MapWaves[0].ImpactTime + Boss->MapWaves[0].FlightTime * (.12f + Frame * .16f);
            while (Boss->MoveAge < Age && Boss->Move == Case.Move) Boss->TickMove(FMath::Min(.01f, Age - Boss->MoveAge));
            if (!TestTrue(TEXT("The actual flying object is still visible during its authored flight"), IsValid(Body) && Body->IsVisible())) break;
            if (Frame == 0) First = Body->GetComponentLocation();
            TestTrue(TEXT("The flying object moves independently while the caster remains beside it"),
                Boss->GetActorLocation().Equals(Caster, .1) && FVector::Dist2D(Body->GetComponentLocation(), Caster) > 90.);
            AddInfo(FString::Printf(TEXT("Flying body %s frame=%d caster=%s projectile=%s material=%s"), Case.Label, Frame,
                *Caster.ToString(), *Body->GetComponentLocation().ToString(), *Surface->GetMaterial()->GetName()));
        }
        TestTrue(TEXT("The five samples track actual travel rather than a stationary cosmetic prop"),
            FVector::Dist2D(First, Body->GetComponentLocation()) > 200.);
        const auto Spec = DuelCourt::Timing(Case.Move, true);
        while (Boss->Move == Case.Move && Boss->MoveAge < Spec.Windup + Spec.Active) Boss->TickMove(.01f);
        TestTrue(TEXT("The independent projectile produces real HP damage on its flight path"), Player->Health < DuelCourt::PlayerMaxHealth);
    }
    TestTrue(TEXT("The three production signatures retain different geometries"),
        Triangles.Num() == 3 && Triangles[0] != Triangles[1] && Triangles[1] != Triangles[2] && Triangles[0] != Triangles[2]);
    const auto& Ema = Cases[0];
    if (Reset(Ema))
    {
        Player->Move = EDuelMove::Roll;
        Player->MoveAge = DuelCourt::RollGuardDelay + .01f;
        const auto Spec = DuelCourt::Timing(Ema.Move, true);
        while (Boss->Move == Ema.Move && Boss->MoveAge < Spec.Windup + Spec.Active) Boss->TickMove(.01f);
        TestEqual(TEXT("Actual roll immunity rejects the independent Ema wing hit"), Player->Health, DuelCourt::PlayerMaxHealth);
    }
    if (Reset(Ema))
    {
        Player->SetWeapon(EDuelWeapon::Buckler, EDuelHand::Left);
        Player->PressHand(EDuelHand::Left);
        Player->HandPresses[uint8(EDuelHand::Left)].bParryReady = false;
        const auto Spec = DuelCourt::Timing(Ema.Move, true);
        while (Boss->Move == Ema.Move && Boss->MoveAge < Spec.Windup + Spec.Active) Boss->TickMove(.01f);
        TestTrue(TEXT("The same actual Ema wing spends held-shield stamina and only chip HP"),
            Player->Stamina < DuelCourt::MaxStamina && Player->Health < DuelCourt::PlayerMaxHealth &&
            Player->Health > DuelCourt::PlayerMaxHealth - 10.f);
    }
    return !HasAnyErrors();
}
#endif
