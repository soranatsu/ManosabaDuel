#include "DuelArena.h"
#include "DuelGame.h"
#include "DuelNorthTower.h"
#include "DuelNorthInvestigationQA.h"

#if !UE_BUILD_SHIPPING
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

// Photographs the live production objects; no duplicate showroom meshes or extra lights.
struct FDuelJudgmentVisualFixture
{
    static void Fail(const TCHAR* Reason)
    {
        UE_LOG(LogTemp, Error, TEXT("Judgment visual: %s"), Reason);
        FPlatformMisc::RequestExitWithStatus(false, 1);
    }

    static FString Name(int32 Frame)
    {
        static const TCHAR* Names[] = {TEXT("JudgmentOverview"), TEXT("JudgmentEye"), TEXT("JudgmentScales"), TEXT("JudgmentCrown")};
        return Names[Frame];
    }

    static void Photograph(UWorld* World, ADuelArena* Arena, ADuelFighter* Player, ADuelFighter* Boss,
        ACameraActor* Camera, const FString& Output, int32 Frame)
    {
        if (!World || !Arena || !Boss || !Camera || Arena->CourtObjects.Num() != 3 || !Boss->JudgmentImmune())
        { Fail(TEXT("Production seals or ward disappeared")); return; }
        FVector Aim = FVector::ZeroVector, Eye;
        float Fov = 66.f;
        Player->SetActorHiddenInGame(Frame > 0);
        if (Frame == 0)
        {
            for (const auto& Seal : Arena->CourtObjects) Aim += Seal->GetComponentLocation() / 3.f;
            Aim += FVector(160, 0, 30);
            Eye = Aim + (Player->GetActorLocation() - Aim).GetSafeNormal2D() * 1800.f + FVector(0, 0, 900);
        }
        else
        {
            auto* Seal = Arena->CourtObjects[Frame - 1].Get();
            Aim = Seal->GetComponentLocation() + FVector(0, 0, 12);
            Eye = Aim + Seal->GetForwardVector() * 660.f + Seal->GetRightVector() * 80.f + FVector(0, 0, 30);
            Fov = 52.f;
        }
        Camera->SetActorLocationAndRotation(Eye, (Aim - Eye).Rotation());
        Camera->GetCameraComponent()->SetFieldOfView(Fov);
        const FString File = Output / (Name(Frame) + TEXT(".png"));
        FTimerHandle Settle;
        World->GetTimerManager().SetTimer(Settle, [World, Arena, Player, Boss, Camera, Output, Frame, File, Eye, Aim, Fov]()
        {
            int32 Enamel = 0, Active = 0;
            FString MeshPaths;
            for (const auto& Seal : Arena->CourtObjects)
            {
                if (Seal->ComponentHasTag(TEXT("JudgmentActiveTarget347"))) ++Active;
                TArray<USceneComponent*> Parts;
                Seal->GetChildrenComponents(true, Parts);
                for (auto* Part : Parts)
                    if (auto* Mesh = Cast<UStaticMeshComponent>(Part); Mesh && Mesh->ComponentHasTag(TEXT("JudgmentEnamel347")))
                    {
                        ++Enamel;
                        MeshPaths += Mesh->GetStaticMesh()->GetPathName() + TEXT("\n");
                    }
            }
            if (Enamel != 3 || Active != 1) { Fail(TEXT("Actual relief imports or the single gold target are missing")); return; }
            const FString Receipt = FString::Printf(TEXT("passed=1\nscene=%s\nhero=%s\nboss=%s\nphase=%d\njudgment_ward=%d\nseal_count=3\nenamel_meshes=%d\nactive_gold_targets=%d\nproduction_path=FinalGate->ReceiveDuelDamage->Awaken->TickCourtInteractions\ncamera_source=dedicated_offscreen_detail_camera\nadded_lights=0\ndesktop_input=none\nheld_after_production_spawn=1\nplayer_hidden_for_detail=%d\nseed=%u\norder=%d,%d,%d\neye=%s\naim=%s\nfov=%.1f\nmesh_paths=\n%s"),
                *Name(Frame), Player->CharacterName(), Boss->CharacterName(), Boss->Phase(), Boss->JudgmentImmune(), Enamel, Active,
                Frame > 0, Arena->JudgmentSeed, Arena->CourtOrder[0], Arena->CourtOrder[1], Arena->CourtOrder[2], *Eye.ToString(), *Aim.ToString(), Fov, *MeshPaths);
            if (!FFileHelper::SaveStringToFile(Receipt, *FPaths::ChangeExtension(File, TEXT("txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            { Fail(TEXT("Cannot save capture receipt")); return; }
            FScreenshotRequest::RequestScreenshot(File, true, false);
            FTimerHandle Saved;
            World->GetTimerManager().SetTimer(Saved, [World, Arena, Player, Boss, Camera, Output, Frame, File]()
            {
                if (IFileManager::Get().FileSize(*File) <= 0) { Fail(TEXT("The engine did not save the requested photograph")); return; }
                if (Frame == 3) FPlatformMisc::RequestExitWithStatus(false, 0);
                else Photograph(World, Arena, Player, Boss, Camera, Output, Frame + 1);
            }, .65f, false);
        }, 2.f, false);
    }

    static void Capture(const TArray<FString>& Args, UWorld* World)
    {
        if (!World || !Args.IsEmpty() || !FParse::Param(FCommandLine::Get(), TEXT("DuelNorthTower")) ||
            !FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")) || !FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen"))) return;
        auto* Mode = World->GetAuthGameMode<ADuelMode>();
        auto* Tower = Mode ? const_cast<ADuelNorthTower*>(Mode->NorthTower()) : nullptr;
        auto* Player = Mode ? const_cast<ADuelFighter*>(Mode->GetPlayerFighter()) : nullptr;
        auto* PC = World->GetFirstPlayerController();
        FString Output;
        if (!Mode || !Tower || !Player || !PC || !FParse::Value(FCommandLine::Get(), TEXT("DuelNorthCaptureOutput="), Output))
        { Fail(TEXT("The isolated tower or explicit capture output is missing")); return; }
        IFileManager::Get().MakeDirectory(*Output, true);
        for (int32 I = 0; I < 4; ++I)
            if (IFileManager::Get().FileExists(*(Output / (Name(I) + TEXT(".png")))))
            { Fail(TEXT("Refusing to overwrite an existing photograph")); return; }
        if (!Player->SelectCharacter(EDuelCharacter::Ema)) { Fail(TEXT("Cannot select protagonist")); return; }
        Tower->State.Reset(EDuelCharacter::Ema);
        Tower->State.bOpeningCellRequired = true;
        if (!Tower->State.ReadClue(EDuelNorthClue::OpeningDocket) || !Tower->State.OpenStartingCell())
        { Fail(TEXT("Cannot open the starting cell")); return; }
        for (int32 Battle = 0; Battle < 2; ++Battle)
            if (!DuelNorthInvestigationQA::Prepare(Tower->State, Battle) || !Tower->State.WinBattle(Battle) ||
                !Tower->State.PickupRelic(static_cast<EDuelNorthRelic>(Battle)) || !Tower->State.OpenDoor(static_cast<EDuelNorthDoor>(Battle)))
            { Fail(TEXT("Cannot prepare the genuine final-gate route")); return; }
        if (!DuelNorthInvestigationQA::Prepare(Tower->State, 2)) { Fail(TEXT("Cannot prepare final trial")); return; }
        Tower->RefreshGates();
        const auto* Gate = DuelNorthLayout::FindLandmark(TEXT("FinalGate"));
        if (!Gate) { Fail(TEXT("Final gate is missing")); return; }
        Player->SetActorLocation(Gate->Position + FVector(0, 0, 62), false, nullptr, ETeleportType::TeleportPhysics);
        Tower->Update(*Mode, 0.f);
        for (int32 I = 0; I < 12 && !Mode->GateOpen(); ++I) Mode->Confirm();
        if (!Mode->GateOpen()) { Fail(TEXT("The real final gate did not open")); return; }
        const FVector Origin = DuelNorthLayout::CourtOrigin(2);
        Player->SetActorLocation(Origin + FVector(-1050, -100, 62), false, nullptr, ETeleportType::TeleportPhysics);
        Tower->Update(*Mode, 0.f);
        auto* Boss = const_cast<ADuelFighter*>(Mode->GetBossFighter());
        auto* Arena = Boss ? Boss->Arena.Get() : nullptr;
        if (!Boss || !Arena || Mode->Outcome() != EDuelOutcome::Fighting || Boss->SelectedCharacter() != EDuelCharacter::Yuki ||
            !Boss->ReceiveDuelDamage(DuelCourt::BossStageHealth[0] + 1.f, Player->GetActorLocation()))
        { Fail(TEXT("The real final boss or phase transition was not reached")); return; }
        Boss->Tick(DuelCourt::Timing(EDuelMove::Awaken, false).Total() + .06f);
        if (Boss->Phase() != 2) { Fail(TEXT("Yuki did not reach phase two")); return; }
        Player->SetActorTickEnabled(false); Boss->SetActorTickEnabled(false);
        Arena->SetActorTickEnabled(false); Mode->SetActorTickEnabled(false);
        Player->GetCharacterMovement()->DisableMovement(); Boss->GetCharacterMovement()->DisableMovement();
        Player->bLockOn = false;
        Arena->ClearCombatEffects();
        Arena->TickCourtInteractions(Boss, 5.01f); Arena->TickCourtInteractions(Boss, 1.21f);
        Arena->TickEffects(1.21f);
        if (!Boss->JudgmentImmune() || Arena->CourtStage != ADuelArena::ECourtStage::Open || Arena->CourtObjects.Num() != 3)
        { Fail(TEXT("The production judgment did not spawn all three objects")); return; }
        Player->PoseFigure(0.f); Boss->PoseFigure(0.f);
        auto* Camera = World->SpawnActor<ACameraActor>();
        if (!Camera) { Fail(TEXT("Cannot create offscreen detail camera")); return; }
        PC->SetViewTarget(Camera);
        FTimerHandle Warm;
        World->GetTimerManager().SetTimer(Warm, [World, Arena, Player, Boss, Camera, Output]()
            { Photograph(World, Arena, Player, Boss, Camera, Output, 0); }, 3.f, false);
    }
};

namespace
{
    FAutoConsoleCommandWithWorldAndArgs JudgmentCapture(TEXT("Duel.CaptureJudgmentSeals"),
        TEXT("Save-free offscreen photographs of production judgment seals: overview plus three enamel details."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FDuelJudgmentVisualFixture::Capture));
}
#endif
