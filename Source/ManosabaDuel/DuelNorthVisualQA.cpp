#include "DuelArena.h"
#include "DuelGame.h"
#include "DuelNorthNarrative.h"
#include "DuelNorthInvestigationQA.h"
#include "DuelNorthTower.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "HAL/FileManager.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

struct FDuelNorthVisualFixture
{
    static void NormalView(ADuelFighter& Player,float Yaw,float Pitch)
    {
        Player.ViewYaw=Yaw;
        Player.ViewPitch=FMath::Clamp(Pitch,-37.f,12.f);
        Player.CameraImpulse=0;
    }

    static bool UnlockCell(ADuelNorthTower& Tower)
    {
        return Tower.State.ReadClue(EDuelNorthClue::OpeningDocket) && Tower.State.OpenStartingCell();
    }

    static bool OpenRescuedCompanion(ADuelNorthTower& Tower, ADuelMode& Mode, ADuelFighter& Player)
    {
        const auto* Meeting = DuelNorthLayout::FindLandmark(TEXT("RescuedCompanion"));
        if (!Meeting || !UnlockCell(Tower) || !DuelNorthInvestigationQA::Prepare(Tower.State, 0) ||
            !Tower.State.WinBattle(0)) return false;
        Tower.RefreshGates();
        Player.SetActorLocation(Meeting->Position + FVector(-95, 0, 62), false, nullptr, ETeleportType::TeleportPhysics);
        Tower.Update(Mode, 0.f);
        Mode.Confirm();
        const auto* Story = Tower.Narrative();
        return Story && Story->IsTalking() && Story->Subtitle().Character == EDuelCharacter::Ema &&
            Story->Subtitle().Line == 1 && Story->Subtitle().Count == 5;
    }

    static bool PreparePriorBattles(ADuelNorthTower& Tower, int32 Count)
    {
        if (!UnlockCell(Tower)) return false;
        for (int32 Battle = 0; Battle < Count; ++Battle)
        {
            if (!DuelNorthInvestigationQA::Prepare(Tower.State, Battle) || !Tower.State.WinBattle(Battle) ||
                !Tower.State.PickupRelic(static_cast<EDuelNorthRelic>(Battle)) ||
                !Tower.State.OpenDoor(static_cast<EDuelNorthDoor>(Battle))) return false;
        }
        Tower.RefreshGates();
        return true;
    }

    static bool PrepareFinalWarning(ADuelNorthTower& Tower)
    {
        if (!PreparePriorBattles(Tower,2) || !DuelNorthInvestigationQA::Prepare(Tower.State, 2)) return false;
        Tower.RefreshGates();
        return true;
    }

    static bool PrepareArchiveRegister(ADuelNorthTower& Tower)
    {
        return Tower.State.ReadClue(EDuelNorthClue::ArchiveTransferLedger);
    }

    static bool PrepareFindEvidence(ADuelNorthTower& Tower, bool bKitchen)
    {
        if (bKitchen) return Tower.State.ReadClue(EDuelNorthClue::RefugeLedger);
        return PrepareArchiveRegister(Tower) && Tower.State.ReadClue(EDuelNorthClue::ArchiveSignatureRegister);
    }

    static bool PrepareInspection(ADuelNorthTower& Tower, FName Station)
    {
        if (!PreparePriorBattles(Tower, 2)) return false;
        if (Station == TEXT("BeaconChart")) return true;
        if (!Tower.State.RecordBeacon(TEXT("BeaconChart")) || !Tower.State.RecordBeacon(TEXT("LensWorkshop"))) return false;
        return Station != TEXT("BeaconSwitch") || Tower.State.RecordBeacon(TEXT("BeaconLetter"));
    }

    static bool PrepareEnding(ADuelNorthTower& Tower)
    {
        if (!PrepareFinalWarning(Tower) || !Tower.State.WinBattle(2) ||
            !Tower.State.CompleteDialogue(EDuelNorthDialogue::FinalExit) ||
            !Tower.State.PickupRelic(EDuelNorthRelic::NameTag) ||
            !Tower.State.OpenDoor(EDuelNorthDoor::TowerExit)) return false;
        Tower.RefreshGates();
        return true;
    }

    static bool ConfirmStation(ADuelNorthTower& Tower, ADuelMode& Mode, FName Station)
    {
        Tower.Update(Mode, 0.f);
        if (Tower.Nearby != Station) return false;
        Mode.Confirm();
        return true;
    }

    static bool OpenPortraitLine(ADuelNorthTower& Tower, ADuelMode& Mode, ADuelFighter& Player,
        FName Marker, const FVector& Approach, int32 Line)
    {
        const auto* Landmark = DuelNorthLayout::FindLandmark(Marker);
        if (!Landmark || (Marker == TEXT("FinalWarning") && !PrepareFinalWarning(Tower))) return false;
        Player.SetActorLocation(Tower.LandmarkPosition(*Landmark) + Approach, false, nullptr, ETeleportType::TeleportPhysics);
        Tower.Update(Mode, 0.f);
        if (Tower.Nearby != Marker) return false;
        Mode.Confirm();
        const auto* Story = Tower.Narrative();
        for (int32 Press = 1; Press < Line && Story && Story->IsTalking(); ++Press) Mode.Confirm();
        return Story && Story->IsTalking() && Story->Subtitle().Line == Line;
    }

    static ADuelFighter* BeginFirstFight(ADuelNorthTower& Tower, ADuelMode& Mode, ADuelFighter& Player, const FVector& Position)
    {
        const auto* Gate = DuelNorthLayout::FindLandmark(TEXT("DungeonGate"));
        if (!Gate || !UnlockCell(Tower) || !DuelNorthInvestigationQA::Prepare(Tower.State, 0)) return nullptr;
        Player.SetActorLocation(Gate->Position + FVector(0, 0, 62), false, nullptr, ETeleportType::TeleportPhysics);
        Tower.Update(Mode, 0.f);
        for (int32 Press = 0; Press < 8 && !Mode.GateOpen(); ++Press) Mode.Confirm();
        if (!Mode.GateOpen()) return nullptr;

        Player.SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
        Tower.Update(Mode, 0.f);
        return Mode.Outcome() == EDuelOutcome::Fighting ? const_cast<ADuelFighter*>(Mode.GetBossFighter()) : nullptr;
    }

    static ADuelFighter* BeginMirrorPressure(ADuelNorthTower& Tower, ADuelMode& Mode, ADuelFighter& Player, const FVector& Position)
    {
        const auto* Gate=DuelNorthLayout::FindLandmark(TEXT("MirrorGate"));
        if(!Gate || !Player.SelectCharacter(EDuelCharacter::Ema) || !PreparePriorBattles(Tower,1) ||
            !DuelNorthInvestigationQA::Prepare(Tower.State,1))return nullptr;
        Tower.RefreshGates();
        Player.SetActorLocation(Gate->Position+FVector(0,0,62),false,nullptr,ETeleportType::TeleportPhysics);
        Tower.Update(Mode,0.f);
        for(int32 Press=0;Press<8 && !Mode.GateOpen();++Press)Mode.Confirm();
        if(!Mode.GateOpen())return nullptr;
        Player.SetActorLocation(Position,false,nullptr,ETeleportType::TeleportPhysics);Tower.Update(Mode,0.f);
        auto* Boss=const_cast<ADuelFighter*>(Mode.GetBossFighter());
        if(Mode.Outcome()!=EDuelOutcome::Fighting || !Boss || Boss->SelectedCharacter()!=EDuelCharacter::Hiro)return nullptr;
        for(int32 Stage=0;Stage<2;++Stage)
        {
            if(!Boss->ReceiveDuelDamage(DuelCourt::BossStageHealth[Stage]+1.f,Player.GetActorLocation()))return nullptr;
            const auto Transition=DuelCourt::Timing(Stage==0 ? EDuelMove::Awaken : EDuelMove::Eclipse,false);
            Boss->Tick(Transition.Windup+Transition.Active+Transition.Recovery+.06f);
        }
        Boss->SetActorTickEnabled(false);Boss->GetCharacterMovement()->DisableMovement();
        return Boss->Phase()==3 ? Boss : nullptr;
    }

    static bool EnterWitchPhase(ADuelFighter& Boss, const ADuelFighter& Player)
    {
        if (!Boss.ReceiveDuelDamage(DuelCourt::BossStageHealth[0] + 1.f, Player.GetActorLocation())) return false;
        Boss.Tick(DuelCourt::Timing(EDuelMove::Awaken, false).Windup + .03f);
        return Boss.Phase() == 2;
    }

    static ADuelFighter* BeginJudgmentHUD(ADuelNorthTower& Tower, ADuelMode& Mode, ADuelFighter& Player)
    {
        const auto* Gate = DuelNorthLayout::FindLandmark(TEXT("FinalGate"));
        if (!Gate || !PrepareFinalWarning(Tower)) return nullptr;
        Player.SetActorLocation(Gate->Position + FVector(0, 0, 62), false, nullptr, ETeleportType::TeleportPhysics);
        Tower.Update(Mode, 0.f);
        for (int32 Press = 0; Press < 12 && !Mode.GateOpen(); ++Press) Mode.Confirm();
        if (!Mode.GateOpen()) return nullptr;
        Player.SetActorLocation(DuelNorthLayout::CourtOrigin(2) + FVector(-1100, 0, 62), false, nullptr, ETeleportType::TeleportPhysics);
        Tower.Update(Mode, 0.f);
        auto* Boss = const_cast<ADuelFighter*>(Mode.GetBossFighter());
        if (Mode.Outcome() != EDuelOutcome::Fighting || !Boss || Boss->SelectedCharacter() != EDuelCharacter::Yuki) return nullptr;
        if (!Boss->Arena || !Boss->ReceiveDuelDamage(DuelCourt::BossStageHealth[0] + 1.f, Player.GetActorLocation())) return nullptr;
        Boss->Tick(DuelCourt::Timing(EDuelMove::Awaken, false).Total() + .06f);
        if (Boss->Phase() != 2) return nullptr;
        Boss->Arena->TickCourtInteractions(Boss, 5.01f);
        if (!Boss->JudgmentImmune()) return nullptr;
        Boss->SetActorTickEnabled(false);
        Boss->GetCharacterMovement()->DisableMovement();
        return Boss;
    }

    static bool ApplyJudgmentHUDPenalty(ADuelFighter& Player)
    {
        if (!Player.Arena) return false;
        Player.Arena->BeginCourtRetaliation(false);
        return Player.JudgmentMoveLockSeconds() > 0.f && Player.JudgmentWeaponLockSeconds() > 0.f;
    }

    static ADuelFighter* BeginEmaFlight(ADuelNorthTower& Tower, ADuelMode& Mode, ADuelFighter& Player, const FVector& Position)
    {
        auto* Boss = BeginFirstFight(Tower, Mode, Player, Position);
        if (!Boss || Boss->Character != EDuelCharacter::Ema ||
            !Boss->ReceiveDuelDamage(DuelCourt::BossStageHealth[0] + 1.f, Player.GetActorLocation())) return nullptr;
        Boss->Tick(DuelCourt::Timing(EDuelMove::Awaken, false).Total() + .06f);
        if (Boss->Phase() != 2) return nullptr;
        Boss->SetActorTickEnabled(false);
        Boss->GetCharacterMovement()->DisableMovement();
        Boss->SetActorLocationAndRotation(DuelNorthLayout::CourtOrigin(0) + FVector(550, 400, 103), FRotator(0, 180, 0));
        return Boss;
    }

    static bool EmaFlightFrame(ADuelFighter& Boss, ADuelArena& Arena, float Fraction)
    {
        Arena.ClearCombatEffects();
        Arena.SetActorTickEnabled(false);
        if (!Boss.StartMove(EDuelMove::OssuaryShears) || Boss.MapWaves.Num() != 2) return false;
        const float Until = Boss.MapWaves[0].ImpactTime + Boss.MapWaves[0].FlightTime * Fraction;
        while (Boss.MoveAge < Until && Boss.Move == EDuelMove::OssuaryShears)
        {
            const float Step = FMath::Min(.01f, Until - Boss.MoveAge);
            Boss.TickMove(Step);
            Boss.PoseFigure(Step);
            Arena.TickEffects(Step);
            Arena.TickParticles(Step);
        }
        return Boss.Move == EDuelMove::OssuaryShears;
    }

    static bool EmaFlightReceipt(const ADuelFighter& Boss, const ADuelFighter& Player, FString& Receipt)
    {
        int32 Bodies = 0;
        Receipt += FString::Printf(TEXT("real_move=OssuaryShears\nmove_age=%.4f\ncaster_position=%s\nplayer_position=%s\nplayer_hp=%.3f\n"),
            Boss.MoveAge, *Boss.GetActorLocation().ToString(), *Player.GetActorLocation().ToString(), Player.Health);
        for (const auto& Wave : Boss.MapWaves)
            if (auto* Mesh = Cast<UProceduralMeshComponent>(Wave.Preview.Get());
                Mesh && Mesh->ComponentHasTag(TEXT("EmaFlyingBoneWing346")) && Mesh->IsVisible())
            {
                const auto* Surface = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
                const auto* Section = Mesh->GetProcMeshSection(0);
                FLinearColor Fill, Edge;
                if (!Surface || !Surface->GetMaterial() || Surface->GetMaterial()->GetFName() != TEXT("M_NorthSpellFacet") ||
                    !Surface->GetVectorParameterValue(FName(TEXT("Tint")), Fill) ||
                    !Surface->GetVectorParameterValue(FName(TEXT("HotFacet")), Edge) || !Section) return false;
                ++Bodies;
                Receipt += FString::Printf(TEXT("projectile_%d_position=%s\nprojectile_%d_triangles=%d\nprojectile_%d_material=M_NorthSpellFacet\nprojectile_%d_fill=%s\nprojectile_%d_edge=%s\n"),
                    Bodies, *Mesh->GetComponentLocation().ToString(), Bodies, Section->ProcIndexBuffer.Num() / 3,
                    Bodies, Bodies, *Fill.ToString(), Bodies, *Edge.ToString());
            }
        Receipt += FString::Printf(TEXT("independent_flying_bodies=%d\n"), Bodies);
        return Bodies == 2;
    }

};

namespace
{
    struct FPortraitProbe
    {
        const TCHAR* Name;
        const TCHAR* Marker;
        EDuelCharacter Player;
        FVector Approach;
        int32 Line;
        EDuelCharacter Speaker;
        EDuelNorthExpression Expression;
    };

    const FPortraitProbe PersonalPortraitProbes[] = {
        {TEXT("HiroSmile"), TEXT("CellEcho"), EDuelCharacter::Ema, {50,0,62}, 3, EDuelCharacter::Hiro, EDuelNorthExpression::Smile},
        {TEXT("EmaSmile"), TEXT("CellEcho"), EDuelCharacter::Hiro, {50,0,62}, 3, EDuelCharacter::Ema, EDuelNorthExpression::Smile},
        {TEXT("HiroOpeningSmile"), TEXT("OpeningDocket"), EDuelCharacter::Hiro, {0,-95,62}, 3, EDuelCharacter::Hiro, EDuelNorthExpression::Smile},
        {TEXT("HiroAngry"), TEXT("FinalWarning"), EDuelCharacter::Hiro, {48,-67,62}, 2, EDuelCharacter::Hiro, EDuelNorthExpression::Angry},
        {TEXT("HiroCallsYuki"), TEXT("FinalWarning"), EDuelCharacter::Hiro, {48,-67,62}, 4, EDuelCharacter::Hiro, EDuelNorthExpression::Smile},
        {TEXT("YukiAdmission"), TEXT("FinalWarning"), EDuelCharacter::Ema, {48,-67,62}, 1, EDuelCharacter::Yuki, EDuelNorthExpression::Neutral},
    };

    bool RunNorthPortraitQA(float)
    {
        const TCHAR* Command = FCommandLine::Get();
        FString Scene;
        if (!FParse::Value(Command, TEXT("DuelNorthPortraitQA="), Scene)) return false;
        const bool bWitch = Scene == TEXT("WitchEma");
        const auto* Probe = MakeArrayView(PersonalPortraitProbes).FindByPredicate([&](const FPortraitProbe& P) { return Scene == P.Name; });
        if ((!Probe && !bWitch && Scene != TEXT("RescuedCompanion")) ||
            !FParse::Param(Command, TEXT("DuelNoSave")) || !FParse::Param(Command, TEXT("RenderOffscreen")))
        {
            FPlatformMisc::RequestExitWithStatus(false, 2);
            return false;
        }

        UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
        auto* Mode = World ? World->GetAuthGameMode<ADuelMode>() : nullptr;
        if (!Mode || !Mode->NorthTower() || !Mode->GetPlayerFighter()) return true;

        const FString Saved = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
        const FString Directory = Saved / TEXT("NorthPortraitQA");
        const FString Screenshot = Directory / (Scene + TEXT(".png"));
        const FString ReceiptFile = FPaths::ChangeExtension(Screenshot, TEXT("txt"));
        if (IFileManager::Get().FileExists(*ReceiptFile) || IFileManager::Get().FileExists(*Screenshot) ||
            !IFileManager::Get().MakeDirectory(*Directory, true))
        {
            FPlatformMisc::RequestExitWithStatus(false, 2);
            return false;
        }
        const auto Fail = [ReceiptFile](const TCHAR* Reason)
        {
            FFileHelper::SaveStringToFile(FString(TEXT("error=")) + Reason + TEXT("\n"), *ReceiptFile);
            FPlatformMisc::RequestExitWithStatus(false, 1);
        };
        auto* Player = const_cast<ADuelFighter*>(Mode->GetPlayerFighter());
        auto* Tower = const_cast<ADuelNorthTower*>(Mode->NorthTower());
        const EDuelCharacter Protagonist = Probe ? Probe->Player : EDuelCharacter::Hiro;
        if (!Player->SelectCharacter(Protagonist))
        {
            Fail(TEXT("Could not select the protagonist"));
            return false;
        }
        if (Mode->Outcome() == EDuelOutcome::Title) Mode->Confirm();
        else Tower->NewGame(*Mode);

        ADuelFighter* Boss = nullptr;
        if (bWitch)
        {
            const FVector Position = DuelNorthLayout::CourtOrigin(0) + FVector(-630, 100, 62);
            Boss = FDuelNorthVisualFixture::BeginFirstFight(*Tower, *Mode, *Player, Position);
            if (!Boss || Boss->SelectedCharacter() != EDuelCharacter::Ema)
            {
                Fail(TEXT("Could not start the first fight with Ema"));
                return false;
            }
            Boss->SetActorTickEnabled(false);
            Boss->GetCharacterMovement()->DisableMovement();
        }
        else if (Probe ? !FDuelNorthVisualFixture::OpenPortraitLine(*Tower, *Mode, *Player, Probe->Marker, Probe->Approach, Probe->Line) :
            !FDuelNorthVisualFixture::OpenRescuedCompanion(*Tower, *Mode, *Player))
        {
            Fail(TEXT("Could not open the requested dialogue line"));
            return false;
        }
        Player->GetCharacterMovement()->DisableMovement();

        const auto Capture = [World, Mode, Tower, Boss, Saved, Scene, Screenshot, ReceiptFile, Fail, Probe, Protagonist]()
        {
            const auto* Story = Tower->Narrative();
            float Opacity = 0.f;
            const UTexture* Art = Boss ? Mode->NorthWitchPortrait(Opacity) : Story->DialoguePortrait();
            if (!Art || !Art->GetResource() || (Boss ? Boss->Phase() != 2 || Opacity < .90f : !Story->UsesLocalPortrait()))
            {
                Fail(TEXT("The live HUD did not receive the requested local portrait"));
                return;
            }
            FString Receipt = FString::Printf(TEXT("build=%s\nsaved=%s\nscene=%s\nplayer=%s\nportrait=local\nportrait_size=%.0fx%.0f\n"),
                UE_BUILD_SHIPPING ? TEXT("Shipping") : TEXT("Development"), *Saved, *Scene,
                Protagonist == EDuelCharacter::Hiro ? TEXT("Hiro") : TEXT("Ema"),
                Art->GetSurfaceWidth(), Art->GetSurfaceHeight());
            if (Boss) Receipt += FString::Printf(TEXT("boss=Ema\nphase=2\nopacity=%.3f\n"), Opacity);
            else
            {
                const auto Subtitle = Story->Subtitle();
                if (Probe && (Subtitle.Character != Probe->Speaker || Subtitle.Expression != Probe->Expression || Subtitle.Line != Probe->Line))
                {
                    Fail(TEXT("The live HUD switched to a different speaker or expression"));
                    return;
                }
                Receipt += FString::Printf(TEXT("speaker=%s\nline=%d/%d\nexpression=%d\nsubtitle=%s\n"), *Subtitle.Speaker,
                    Subtitle.Line, Subtitle.Count, int32(Subtitle.Expression), *Subtitle.Text);
            }
            FScreenshotRequest::RequestScreenshot(Screenshot, true, false);
            FTimerHandle CheckFile;
            World->GetTimerManager().SetTimer(CheckFile, [Receipt, Screenshot, ReceiptFile, Fail]()
            {
                if (IFileManager::Get().FileSize(*Screenshot) <= 0 ||
                    !FFileHelper::SaveStringToFile(Receipt, *ReceiptFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
                {
                    Fail(TEXT("Could not save the rendered screenshot and receipt"));
                    return;
                }
                FPlatformMisc::RequestExitWithStatus(false, 0);
            }, 1.f, false);
        };

        FTimerHandle Warmup;
        World->GetTimerManager().SetTimer(Warmup, [World, Player, Boss, Capture, Fail]()
        {
            if (!Boss)
            {
                Capture();
                return;
            }
            if (!FDuelNorthVisualFixture::EnterWitchPhase(*Boss, *Player))
            {
                Fail(TEXT("Could not enter Ema's witch phase"));
                return;
            }
            FTimerHandle Frame;
            World->GetTimerManager().SetTimer(Frame, Capture, .45f, false);
        }, 4.f, false);
        return false;
    }

    [[maybe_unused]] const auto PortraitQATicker =
        FTSTicker::GetCoreTicker().AddTicker(TEXT("DuelNorthPortraitQA"), .1f, &RunNorthPortraitQA);
}

#if WITH_DEV_AUTOMATION_TESTS

#include "DuelBossDefense.h"
#include "DuelNorthFinds.h"
#include "DuelNorthInteractables.h"
#include "Components/StaticMeshComponent.h"
#include "DuelNorthLighting.h"
#include "DuelNorthExtractionVisual.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "EngineUtils.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

namespace
{
    using DuelNorthExtractionVisual::EPhase;
    enum class ESigilShot : uint8 { None, Wing, Dash, Ice, Sector, StarTrack, ReturningFrost, GildedCross, MoonCrescent };

    enum class EEffectShot : uint8 { None, IceShards, IcicleRow, IceLance, HoundShade, ShadowVolley };

    struct FNorthView
    {
        const TCHAR* Name;
        FVector Player,Eye,Aim;
        float Fov=68.f;
        EPhase Flight=EPhase::Hidden;
        float FlightProgress=0.f;
        TOptional<float> WayLightYaw;
        ESigilShot Sigil=ESigilShot::None;
        EEffectShot Effect=EEffectShot::None;
        float EmaFlightFraction=-1.f;
    };
    const FNorthView Views[] = {
        {TEXT("CustodyGallery"),{-3000,1900,-1638},{-3120,1815,-1515},{1200,2020,-1510},80.f},
        {TEXT("CustodySingle"),{-2375,2400,-1638},{-2205,2290,-1520},{-2520,2650,-1590},75.f},
        {TEXT("CustodyShared"),{-700,2360,-1638},{-375,2275,-1510},{-880,2650,-1560},88.f},
        {TEXT("CustodySide04"),{325,2400,-1638},{530,2260,-1520},{150,2660,-1570},74.f},
        {TEXT("RescueTarget"),{6350,-300,3462},{5900,-690,3650},{6660,0,3435},75.f},
        {TEXT("RescueHelicopter"),{6350,-300,3462},{5700,-1120,4130},{6740,0,4360},67.f,EPhase::Hold,1.f},
        {TEXT("RescueCabin"),{6350,-300,3462},{6360,-280,4410},{6780,-80,4380},67.f,EPhase::Hold,1.f},
        {TEXT("RescueStation"),{6550,800,3462},{6900,850,3820},{15000,-8500,800},60.f},
        {TEXT("MoonSea"),{6750,-850,3462},{6750,-900,3610},{220000,-440000,230000},68.f},
        {TEXT("SeaLiving"),{6700,-900,3462},{6700,-1050,3600},{14000,-16000,-1200},72.f},
        {TEXT("MirrorPressure"),{-1100,0,1762},{-1840,-470,2200},{1400,0,1985},80.f},
        {TEXT("MirrorPressureForming"),{-1100,0,1762},{-1250,-340,1930},{990,360,1900},72.f},
        {TEXT("EmaFlight01"),{-650,-150,-1638},{-60,-1510,-1190},{30,80,-1570},77.f,EPhase::Hidden,0.f,{},ESigilShot::None,EEffectShot::None,.16f},
        {TEXT("EmaFlight02"),{-650,-150,-1638},{-60,-1510,-1190},{30,80,-1570},77.f,EPhase::Hidden,0.f,{},ESigilShot::None,EEffectShot::None,.32f},
        {TEXT("EmaFlight03"),{-650,-150,-1638},{-60,-1510,-1190},{30,80,-1570},77.f,EPhase::Hidden,0.f,{},ESigilShot::None,EEffectShot::None,.48f},
        {TEXT("EmaFlight04"),{-650,-150,-1638},{-60,-1510,-1190},{30,80,-1570},77.f,EPhase::Hidden,0.f,{},ESigilShot::None,EEffectShot::None,.64f},
        {TEXT("EmaFlight05"),{-650,-150,-1638},{-60,-1510,-1190},{30,80,-1570},77.f,EPhase::Hidden,0.f,{},ESigilShot::None,EEffectShot::None,.80f},
        {TEXT("Cell"),{-3000,1200,62},{-3070,1425,160},{-2690,1120,145}},
        {TEXT("CellTalk01"),{-3130,1230,62},{-3130,1420,160},{-2750,1170,135}},
        {TEXT("CellTalk02"),{-3130,1230,62},{-3130,1420,160},{-2750,1170,135}},
        {TEXT("CellTalk03"),{-3130,1230,62},{-3130,1420,160},{-2750,1170,135}},
        {TEXT("CellTalk04"),{-3130,1230,62},{-3130,1420,160},{-2750,1170,135}},
        {TEXT("FinalTalk"),{-2822,228,3462},{-3240,84,3610},{-2445,92,3562},66.f},
        {TEXT("WitchEma"),{-630,100,-1638},{-1150,-340,-1320},{270,185,-1490},67.f},
        {TEXT("WitchHiro"),{-630,100,-1638},{-1150,-340,-1320},{270,185,-1490},67.f},
        {TEXT("Prison"),{-1500,0,62},{-1850,-150,190},{600,220,220}},
        {TEXT("PrisonWash"),{1470,-970,62},{1460,-960,169},{1640,-1280,85},66.f},
        {TEXT("DungeonDrain"),{-1430,-1230,-1638},{-1455,-1300,-1548},{-1640,-1676,-1650},59.f},
        {TEXT("MirrorInspection"),{980,980,1762},{858,888,1916},{1440,1610,1840},66.f},
        {TEXT("FinalVestments"),{-1440,-1030,3462},{-1330,-950,3630},{-2050,-1590,3530},68.f},
        {TEXT("LampSpirit"),{-3030,1200,62},{-3182,1235,158},{-2770,1115,130},67.f},
        {TEXT("RestFire"),{-1080,-480,62},{},{},64.f},
        {TEXT("DungeonGate"),{-2900,-220,-1638},{-3150,-240,-1500},{-2050,0,-1460}},
        {TEXT("Confinement03"),{-2670,88,-1638},{-2785,225,-1500},{-2429,208,-1518},66.f},
        {TEXT("DungeonTorch"),{-2880,280,-1638},{-2730,314,-1450},{-3236,175,-1408},58.f},
        {TEXT("CellWallFoot"),{-2650,1920,-1638},{-2750,1885,-1580},{-2390,1709,-1675},65.f},
        {TEXT("Chapel"),{-1800,0,762},{-2100,-500,920},{500,100,1050}},
        {TEXT("ArchiveHall"),{-1910,0,762},{-2200,0,966},{330,0,872},86.f},
        {TEXT("ArchiveNorthStacks"),{-530,595,762},{-540,476,895},{-780,1350,843},75.f},
        {TEXT("ArchiveSouthStacks"),{1040,-752,762},{1035,-690,894},{1170,-1445,833},74.f},
        {TEXT("InspectionRegisterTalk"),{-130,-2220,2340},{10,-2486,2430},{-229,-2180,2366},62.f},
        {TEXT("InspectionOrderTalk"),{-1750,-3270,2340},{-1535,-3080,2429},{-1671,-3416,2368},62.f},
        {TEXT("DiaryPage"),{-1750,-3270,2340},{-1535,-3080,2429},{-1671,-3416,2368},62.f},
        {TEXT("DiaryReveal"),{-1750,-3270,2340},{-1535,-3080,2429},{-1671,-3416,2368},62.f},
        {TEXT("SealDeskBags"),{-2890,-2330,2340},{-2668,-2540,2465},{-2774,-2400,2327},62.f},
        {TEXT("SealDeskRing"),{-2890,-2330,2340},{-2668,-2540,2465},{-2774,-2400,2327},62.f},
        {TEXT("SealDeskOpened"),{-2890,-2330,2340},{-2668,-2540,2465},{-2938,-2267,2331},62.f},
        {TEXT("ChapelStainedGlass"),{0,800,762},{0,820,965},{0,1690,945},64.f},
        {TEXT("SeaGallery"),{-1400,-2120,762},{-2180,-2130,900},{1700,-2120,900}},
        {TEXT("MirrorGate"),{-2800,0,1762},{-3150,-300,1930},{-2000,0,1960}},
        {TEXT("MirrorCourt"),{-1700,-100,1762},{-1980,-460,1950},{1430,750,2150},72.f},
        {TEXT("WayLightStair"),{1610,-2600,1762},{1886,-2488,1918},{1185,-2620,1904},72.f,EPhase::Hidden,0.f,TOptional<float>(180.f)},
        {TEXT("WayLightThreshold"),{-3085,-2600,-1638},{-2810,-2480,-1495},{-3410,-2600,-1636},72.f,EPhase::Hidden,0.f,TOptional<float>(180.f)},
        {TEXT("StairFixtures"),{1610,-2600,1762},{1886,-2488,1918},{1185,-2620,1904},72.f},
        {TEXT("DungeonRoom"),{510,1180,-1638},{350,1200,-1480},{2080,985,-1230},72.f},
        {TEXT("DungeonFinish"),{-2035,1360,-1638},{-2160,1330,-1485},{1610,1150,-1395},66.f},
        {TEXT("PrisonGrille"),{-1000,-600,62},{-1160,-1050,407},{-915,-1500,535},55.f},
        {TEXT("FinalExitLamps"),{2050,-180,3462},{1780,-150,3590},{3010,0,3630},67.f},
        {TEXT("FinalCourt"),{-1700,0,3462},{-2050,-500,3690},{900,180,3800}},
        {TEXT("FinalSideWindows"),{0,0,3462},{-180,1400,3690},{0,-1490,3720}},
        {TEXT("FinalSideNorth"),{0,0,3462},{180,-1400,3690},{0,1490,3720}},
        {TEXT("Garden"),{6500,0,3462},{5350,-800,3710},{7400,0,3650}},
        {TEXT("GardenCoast"),{6100,350,3462},{5630,945,3780},{7620,-1630,3650}},
        {TEXT("PrisonEnding"),{7450,0,3462},{5630,945,3780},{7620,-1630,3650}},
        {TEXT("GardenLookback"),{7140,-270,3462},{7280,-790,3980},{2990,80,5010}},
        {TEXT("GardenCrown"),{7500,1080,3462},{7556,1435,3710},{7789,-467,3136},68.f},
        {TEXT("ExtractionHover"),{7200,-690,3462},{4750,-2000,3860},{6760,0,4230},69.f,EPhase::Hold,1.f},
        {TEXT("ExtractionBank"),{7200,-690,3462},{12500,-2240,6930},{7875,-2680,4890},79.f,EPhase::Departure,.38f},
        {TEXT("ExtractionFinalLookback"),{7200,-690,3462},{},{},58.f,EPhase::Departure,1.f},
        {TEXT("IslandExterior"),{6500,0,3462},{13600,-12000,8100},{1850,0,2500}},
        {TEXT("SeaWindowWest"),{-1400,-2190,762},{-1400,-2200,1000},{-4000,-5000,-800}},
        {TEXT("SigilWingNear"),{0,0,1762},{-480,-520,2330},{0,0,1708},65.f,EPhase::Hidden,0.f,{},ESigilShot::Wing},
        {TEXT("SigilWingFull"),{0,0,1762},{-1500,-1120,2960},{0,0,1708},75.f,EPhase::Hidden,0.f,{},ESigilShot::Wing},
        {TEXT("SigilDashNear"),{0,0,-1638},{-530,-840,-1090},{0,0,-1692},72.f,EPhase::Hidden,0.f,{},ESigilShot::Dash},
        {TEXT("SigilDashFull"),{0,0,-1638},{-1460,-1180,-395},{0,0,-1692},78.f,EPhase::Hidden,0.f,{},ESigilShot::Dash},
        {TEXT("SigilIceNear"),{0,0,3462},{-720,-460,4430},{0,0,3408},70.f,EPhase::Hidden,0.f,{},ESigilShot::Ice},
        {TEXT("SigilIceFull"),{0,0,3462},{-1370,-490,4990},{0,0,3408},101.f,EPhase::Hidden,0.f,{},ESigilShot::Ice},
        {TEXT("SigilSector"),{0,0,-1638},{-720,-800,-610},{0,0,-1692},75.f,EPhase::Hidden,0.f,{},ESigilShot::Sector},
        {TEXT("SigilStarNear"),{0,0,1762},{-335,-430,2230},{0,0,1708},65.f,EPhase::Hidden,0.f,{},ESigilShot::StarTrack},
        {TEXT("PrisonLife"),{-1590,-440,62},{-1765,-380,203},{-270,-1070,123},73.f},
        {TEXT("PrisonBunks"),{-1690,730,62},{-1825,665,196},{-915,1225,100},68.f},
        {TEXT("PrisonKitchen"),{1410,-920,62},{1410,-900,176},{1470,-1440,98},69.f},
        {TEXT("PrisonHallwayEast"),{3500,-1930,62},{3480,-1950,179},{3510,-515,137},61.f},
        {TEXT("ChapelReading"),{-1250,270,762},{-1250,270,949},{-1140,1230,889},70.f},
        {TEXT("ChapelStacks"),{1920,-1475,762},{1980,-1490,986},{-1180,-665,866},74.f},
        {TEXT("ChapelAisle"),{0,-1410,762},{-30,-1450,962},{0,1510,974},73.f},
        {TEXT("ChapelOuterWest"),{-1610,585,762},{-1390,432,972},{-2300,1420,1060},68.f},
        {TEXT("ChapelOuterEast"),{1510,465,762},{1320,355,966},{2300,1420,1050},68.f},
        {TEXT("ChapelWallBase"),{1980,1200,762},{1960,1130,847},{2360,1618,757},63.f},
        {TEXT("ChapelHallwayEast"),{3500,1600,762},{3480,1640,889},{3510,330,846},60.f},
        {TEXT("MirrorStand"),{-3190,-102,1762},{-3155,-45,1845},{-2845,-337,1815},66.f},
        {TEXT("Waterworks"),{-1060,-1160,-1638},{-1065,-1170,-1502},{-1380,-1590,-1555},64.f},
        {TEXT("CellLife"),{-3000,1200,62},{-2940,1260,155},{-3230,960,60},70.f},
        {TEXT("ArchivePost"),{3510,2260,762},{3180,2250,862},{2300,2385,824},62.f},
        {TEXT("MirrorPolish"),{785,1180,1762},{747,1160,1886},{1110,1616,1801},68.f},
        {TEXT("JudgmentScale"),{-560,1230,3462},{-420,1250,3555},{-820,1660,3525},66.f},
        {TEXT("MoonGardenSeat"),{6750,-740,3462},{6620,-680,3530},{6875,-1095,3470},67.f},
        {TEXT("YukiIceShards"),{0,0,3462},{-780,-640,3820},{120,80,3600},73.f,EPhase::Hidden,0.f,{},ESigilShot::None,EEffectShot::IceShards},
        {TEXT("YukiIcicleRow"),{0,0,3462},{-260,-465,3645},{70,0,3510},62.f,EPhase::Hidden,0.f,{},ESigilShot::None,EEffectShot::IcicleRow},
        {TEXT("YukiIceLance"),{0,0,3462},{-245,-455,3630},{-135,0,3507},60.f,EPhase::Hidden,0.f,{},ESigilShot::None,EEffectShot::IceLance},
        {TEXT("HiroHoundShade"),{0,0,1762},{290,-370,1891},{0,0,1810},60.f,EPhase::Hidden,0.f,{},ESigilShot::None,EEffectShot::HoundShade},
        {TEXT("HiroShadowVolley"),{0,0,1762},{-240,-485,1925},{-83,0,1770},60.f,EPhase::Hidden,0.f,{},ESigilShot::None,EEffectShot::ShadowVolley},
        {TEXT("SigilNewCross"),{0,0,1762},{-560,-710,2810},{0,0,1708},75.f,EPhase::Hidden,0.f,{},ESigilShot::GildedCross},
        {TEXT("SigilNewMoon"),{0,0,1762},{-570,-725,2810},{0,0,1708},75.f,EPhase::Hidden,0.f,{},ESigilShot::MoonCrescent},
        {TEXT("SigilFrostNear"),{0,0,3462},{-550,-570,4090},{0,0,3408},70.f,EPhase::Hidden,0.f,{},ESigilShot::ReturningFrost}
    };

    void SpawnSigil(ADuelArena& Arena, ESigilShot Shot)
    {
        const FVector At = Arena.GroundMark(Arena.CourtOrigin());
        switch (Shot)
        {
        case ESigilShot::Wing:
            Arena.Sigil(At, 403.f, FLinearColor(2.7f, .94f, .17f), .73f + .66f * .94f,
                EDuelMark::Wingfall, FVector::ForwardVector);
            break;
        case ESigilShot::Dash:
            Arena.Lane(At, FVector::ForwardVector, 665.5f, 158.f, FLinearColor(5.f, .025f, .09f),
                .56f, false, false, false, EDuelMark::TalonRush);
            break;
        case ESigilShot::Ice:
            Arena.Halo(At, DuelCourt::ArchiveInner, DuelCourt::ArchiveOuter,
                FLinearColor(.34f, .85f, 1.55f, .91f) * .42f, 1.03f, false, EDuelMark::Archive);
            break;
        case ESigilShot::Sector:
            Arena.TailSweepWarning(At, 0.f, DuelCourt::TailSweepReach(3), DuelCourt::TailSweepArc(3), .86f);
            break;
        case ESigilShot::StarTrack:
        {
            const auto& Art = DuelCourt::WeaponArt(EDuelArt::StarTrack);
            const auto Timing = DuelCourt::Timing(EDuelMove::Crimson, false, EDuelWeapon::Staff, EDuelArt::StarTrack);
            Arena.Sigil(At, Art.Strike.Reach, Art.Glow, Timing.Windup, EDuelMark::StarTrack);
            break;
        }
        case ESigilShot::ReturningFrost:
        {
            const auto& Art = DuelCourt::WeaponArt(EDuelArt::ReturningFrost);
            const auto Timing = DuelCourt::Timing(EDuelMove::Crimson, false, EDuelWeapon::Staff, EDuelArt::ReturningFrost);
            Arena.Halo(At, DuelCourt::ReturningFrostInner, Art.Strike.Reach, Art.Glow,
                Timing.Windup, false, EDuelMark::ReturningFrost);
            break;
        }
        case ESigilShot::GildedCross:
            Arena.Sigil(At, DuelCourt::WeaponArt(EDuelArt::GildedCross).Strike.Reach, FLinearColor(5.8f, 3.32f, .75f),
                .85f, EDuelMark::GildedCross, FVector::ForwardVector);
            break;
        case ESigilShot::MoonCrescent:
            Arena.Sigil(At, DuelCourt::WeaponArt(EDuelArt::MoonCrescent).Strike.Reach, FLinearColor(2.34f, 1.47f, 5.9f),
                .85f, EDuelMark::MoonCrescent, FVector::ForwardVector);
            break;
        default:
            break;
        }
    }

    using FEffectSamples = TArray<TWeakObjectPtr<USceneComponent>>;

    void SpawnEffectSample(ADuelArena& Arena, EEffectShot Shot, FEffectSamples& Samples)
    {
        const bool bHiro = Shot == EEffectShot::HoundShade || Shot == EEffectShot::ShadowVolley;
        const FVector Origin = DuelNorthLayout::CourtOrigin(bHiro ? 1 : 2);
        Arena.SetNorthOrigin(Origin);
        const FVector Axis = FVector::ForwardVector;
        const FLinearColor Ice(.34f, .85f, 1.55f, .91f);
        const FLinearColor Rose(1.31f, .112f, .31f, .93f);
        switch (Shot)
        {
        case EEffectShot::IceShards:
        {
            const TInlineComponentArray<UProceduralMeshComponent*> Before(&Arena);
            Arena.GlassVerdict(Arena.GroundMark(Origin), 254.f, .78f, true);
            for (auto* Candidate : TInlineComponentArray<UProceduralMeshComponent*>(&Arena))
                if (!Before.Contains(Candidate)) Samples.Add(Candidate);
            break;
        }
        case EEffectShot::IcicleRow:
            for (int32 I = -2; I <= 2; ++I)
            {
                const FLinearColor Colour = I % 2 == 0 ? Ice : FLinearColor(.72f, .54f, 1.41f, .91f);
                Samples.Add(Arena.StageIcicle(Origin + FVector(-104, 83 * I, 108 + 12 * (I % 2 != 0)), Axis, Colour, 1.0f));
            }
            break;
        case EEffectShot::IceLance:
            Samples.Add(Arena.LaunchThread(nullptr, Origin + FVector(-425, 0, 108), Axis,
                32, 0, Ice, 2030, 55, .80f, true, 0.f, false, 0.f, EDuelMark::IceLance));
            break;
        case EEffectShot::HoundShade:
            Samples.Add(Arena.StageBossSignature(Origin + FVector(0, 0, 105), Axis, EDuelMove::HoundShade, Rose, 1.0f));
            break;
        case EEffectShot::ShadowVolley:
            Samples.Add(Arena.LaunchThread(nullptr, Origin + FVector(-325, 0, 42), Axis,
                51, 0, Rose * 2.2f, DuelBossDefense::ShadowProjectileSpeed(3), 114, .95f,
                true, 0.f, true, 0.f, EDuelMark::ShadowVolley));
            break;
        default:
            break;
        }
    }

    bool CheckSigil(ADuelArena& Arena, ESigilShot Shot, FString& Receipt)
    {
        const float SafeRadius = Shot == ESigilShot::ReturningFrost ? DuelCourt::ReturningFrostInner :
            Shot == ESigilShot::Sector ? DuelCourt::TailSweepInner : Shot == ESigilShot::Ice ? DuelCourt::ArchiveInner : 0.f;
        for (const auto* Art : TInlineComponentArray<UStaticMeshComponent*>(&Arena))
        {
            if (!Art->ComponentHasTag(TEXT("NorthSigilArtwork")) || !Art->IsRegistered() || !Art->IsVisible()) continue;
            if (SafeRadius > 0.f)
            {
                const auto* Material = Cast<UMaterialInstanceDynamic>(Art->GetMaterial(0));
                float Fraction = 0.f;
                if (!Material || (!Material->GetScalarParameterValue(FName(TEXT("Inner")), Fraction) &&
                    !Material->GetScalarParameterValue(FName(TEXT("InnerFraction")), Fraction)) ||
                    50. * Art->GetComponentScale().X * Fraction + 11. < SafeRadius)
                    return false;
            }
            Receipt += FString::Printf(TEXT("sigil_origin=%s\nsafe_radius=%.1f\n"),
                *Art->GetComponentLocation().ToString(), SafeRadius);
            if (const auto* Material = Cast<UMaterialInstanceDynamic>(Art->GetMaterial(0)))
            {
                float Spin = 0.f;
                const bool bAtlasRotation = Material->GetScalarParameterValue(FName(TEXT("Spin")), Spin);
                Receipt += FString::Printf(TEXT("sigil_material=%s\nsigil_atlas_spin_rad_s=%.2f\n"),
                    bAtlasRotation ? TEXT("NorthHighSigil") : TEXT("NorthFiligree"), Spin);
            }
            return true;
        }
        for (auto* Mesh : TInlineComponentArray<UProceduralMeshComponent*>(&Arena))
        {
            if (!Mesh->ComponentHasTag(TEXT("NorthUniqueSigil")) || !Mesh->IsRegistered() || !Mesh->IsVisible()) continue;
            const auto* Section = Mesh->GetProcMeshSection(0);
            if (!Section || Section->ProcIndexBuffer.Num() < 12) continue;
            if (SafeRadius > 0.f)
                for (const auto& Vertex : Section->ProcVertexBuffer)
                    if (Vertex.Position.Size2D() + 11. < SafeRadius) return false;
            Receipt += FString::Printf(TEXT("sigil_triangles=%d\nsigil_origin=%s\nsafe_radius=%.1f\n"),
                Section->ProcIndexBuffer.Num() / 3, *Mesh->GetComponentLocation().ToString(), SafeRadius);
            return true;
        }
        return false;
    }

    void CaptureNorth(const TArray<FString>& Args,UWorld* World)
    {
        if(!World || Args.Num()!=1 || !FParse::Param(FCommandLine::Get(),TEXT("DuelNorthTower")) ||
            !FParse::Param(FCommandLine::Get(),TEXT("DuelNoSave")))return;
        const auto* View=MakeArrayView(Views).FindByPredicate([&](const FNorthView& V){return Args[0]==V.Name;});
        FNorthView ObjectView{};
        const bool bKitchenPuzzle = Args[0] == TEXT("KitchenRationPuzzle");
        const bool bArchivePuzzle = Args[0] == TEXT("ArchiveMailPuzzle");
        const bool bFindPuzzle = bKitchenPuzzle || bArchivePuzzle;
        const FName ObjectId = bKitchenPuzzle ? FName(TEXT("FindRations")) :
            bArchivePuzzle ? FName(TEXT("FindCounterseal")) : FName(*Args[0]);
        const int32 FindIndex = View ? INDEX_NONE : DuelNorthFinds::IndexOf(ObjectId);
        const auto* Find = FindIndex == INDEX_NONE ? nullptr : &DuelNorthFinds::All()[FindIndex];
        const auto* Ledger = View ? nullptr : DuelNorthInteractables::Ledger(ObjectId);
        const bool bObjectShot = Find || Ledger;
        if (bObjectShot)
        {
            const FVector Base = Find ? Find->Position : Ledger->Position;
            const FVector Front = Find ? Find->Front() : Ledger->Front();
            const double HalfDepth = Find ? Find->Size.Y * .5 : Ledger->Size.Y;
            const double Height = Ledger ? Ledger->Size.Z * .52 : Find->ItemPosition().Z - Base.Z - 20.;
            const FVector Side = FVector::CrossProduct(FVector::UpVector, Front);
            if (Ledger && Ledger->IsArchiveVolume())
                ObjectView = {Ledger->LedgerTitle, Ledger->Approach() + FVector(0, 0, 62),
                    Base + Front * (HalfDepth + 174.) + Side * 49. + FVector(0, 0, 153),
                    Ledger->ReadingPoint(), 59.f};
            else
                ObjectView = {Find ? Find->Title : Ledger->LedgerTitle,
                    Base + Front * (HalfDepth + 87.) + FVector(0, 0, 62),
                    Base + Front * (HalfDepth + (Ledger ? 192. : 164.)) + Side * 52. + FVector(0, 0, 159),
                    Base + FVector(0, 0, Height), 65.f};
            View = &ObjectView;
        }
        auto* Mode=World->GetAuthGameMode<ADuelMode>();
        auto* PC=World->GetFirstPlayerController();
        if(!View || !Mode || !PC || !Mode->NorthTower() || !Mode->GetPlayerFighter())return;
        FString Directory;
        if(!FParse::Value(FCommandLine::Get(),TEXT("DuelNorthCaptureOutput="),Directory))
            Directory=FPaths::ProjectSavedDir()/TEXT("NorthTower200/Visual");
        Directory=FPaths::ConvertRelativePathToFull(Directory);
        IFileManager::Get().MakeDirectory(*Directory,true);
        const FString File=Directory/(Args[0]+TEXT(".png"));
        if(IFileManager::Get().FileExists(*File))
        {UE_LOG(LogTemp,Error,TEXT("North capture refuses existing image: %s"),*File);return;}
        UGameUserSettings::RequestResolutionChange(1920,1080,EWindowMode::Windowed,false);
        auto* Player=const_cast<ADuelFighter*>(Mode->GetPlayerFighter());
        auto* Tower=const_cast<ADuelNorthTower*>(Mode->NorthTower());
        const bool bExit=FParse::Param(FCommandLine::Get(),TEXT("DuelQAQuit"));
        const bool bFinalTalk = Args[0] == TEXT("FinalTalk");
        const bool bCellTalk = Args[0].StartsWith(TEXT("CellTalk"));
        const bool bChartTalk = Args[0] == TEXT("InspectionRegisterTalk");
        const bool bLetterTalk = Args[0] == TEXT("InspectionOrderTalk");
        const bool bDiaryPage = Args[0] == TEXT("DiaryPage") || Args[0] == TEXT("DiaryReveal");
        const bool bSealRing = Args[0] == TEXT("SealDeskRing");
        const bool bSealOpened = Args[0] == TEXT("SealDeskOpened");
        const bool bSealPuzzle = bSealRing || bSealOpened || Args[0] == TEXT("SealDeskBags");
        const bool bInspection = bChartTalk || bLetterTalk || bDiaryPage || bSealPuzzle;
        const FName Station = bChartTalk ? FName(TEXT("BeaconChart")) : bLetterTalk || bDiaryPage ? FName(TEXT("BeaconLetter")) :
            bSealPuzzle ? FName(TEXT("BeaconSwitch")) : NAME_None;
        const bool bDialogueShot = bFinalTalk || bCellTalk || bChartTalk || bLetterTalk || bSealOpened;
        const bool bEndingShot = Args[0] == TEXT("PrisonEnding");
        const bool bWitchShot = Args[0] == TEXT("WitchEma") || Args[0] == TEXT("WitchHiro");
        const bool bMirrorPressure = Args[0].StartsWith(TEXT("MirrorPressure"));
        const bool bEmaFlight = View->EmaFlightFraction >= 0.f;
        const auto Fail = [bExit](const TCHAR* Reason)
        {
            UE_LOG(LogTemp, Error, TEXT("North capture: %s"), Reason);
            if (bExit) FPlatformMisc::RequestExitWithStatus(false,1);
        };
        TWeakObjectPtr<ADuelFighter> WitchBoss;
        TWeakObjectPtr<ADuelFighter> FlyingBoss;
        if ((bWitchShot || bEmaFlight) && !Player->SelectCharacter(bEmaFlight || Args[0] == TEXT("WitchEma") ? EDuelCharacter::Hiro : EDuelCharacter::Ema))
        {
            Fail(TEXT("Could not select the protagonist for the witch fixture"));
            return;
        }
        Tower->NewGame(*Mode);
        using DuelNorthLayout::EFloor;
        const EFloor ShotFloor = DuelNorthLayout::Locate(View->Player).Floor;
        const int32 PriorBattles = bFinalTalk || ShotFloor == EFloor::Final ? 2 :
            (ShotFloor == EFloor::Chapel || ShotFloor == EFloor::Mirror ? 1 : 0);
        const bool bArchiveShot = ShotFloor == EFloor::Chapel && Args[0].StartsWith(TEXT("Archive"));
        if (bFinalTalk ? !FDuelNorthVisualFixture::PrepareFinalWarning(*Tower) :
            (bObjectShot || bArchiveShot) && !FDuelNorthVisualFixture::PreparePriorBattles(*Tower, PriorBattles))
        {
            Fail(TEXT("Could not prepare the prior victories and the final stair"));
            return;
        }
        if (ObjectId == TEXT("ArchiveSignatureRegister") && !FDuelNorthVisualFixture::PrepareArchiveRegister(*Tower))
        {
            Fail(TEXT("Could not prepare the prior shelf record for the eastern archive fixture"));
            return;
        }
        if (bInspection && !FDuelNorthVisualFixture::PrepareInspection(*Tower, Station))
        {
            Fail(TEXT("Could not prepare the story before inspecting the prison mail evidence"));
            return;
        }
        if ((bFindPuzzle && !FDuelNorthVisualFixture::PrepareFindEvidence(*Tower, bKitchenPuzzle)) ||
            (bEndingShot && !FDuelNorthVisualFixture::PrepareEnding(*Tower)))
        {
            Fail(TEXT("Could not prepare the earlier evidence or victories for this scene"));
            return;
        }
        if (bEmaFlight)
        {
            FlyingBoss = FDuelNorthVisualFixture::BeginEmaFlight(*Tower, *Mode, *Player, View->Player);
            if (!FlyingBoss.IsValid()) { Fail(TEXT("Could not enter Ema's real ranged second phase")); return; }
        }
        else if(bMirrorPressure)
        {
            auto* Boss=FDuelNorthVisualFixture::BeginMirrorPressure(*Tower,*Mode,*Player,View->Player);
            ADuelArena* Arena=nullptr;
            for(TActorIterator<ADuelArena> It(World);It;++It){Arena=*It;break;}
            if(!Boss || !Arena) { Fail(TEXT("Could not enter the real third-phase Hiro mirror fight"));return; }
            Arena->ClearMirrorCourt();
            Arena->TickMirrorCourt(Boss,Args[0].EndsWith(TEXT("Forming")) ? .90f : 2.4f);
        }
        else if (bWitchShot)
        {
            WitchBoss = FDuelNorthVisualFixture::BeginFirstFight(*Tower, *Mode, *Player, View->Player);
            if (!WitchBoss.IsValid())
            {
                Fail(TEXT("Could not start the first fight for the witch fixture"));
                return;
            }
            WitchBoss->SetActorTickEnabled(false);
            WitchBoss->GetCharacterMovement()->DisableMovement();
        }
        else if (bInspection || bEndingShot)
        {
            const auto* Marker = DuelNorthLayout::FindLandmark(bEndingShot ? FName(TEXT("GardenEdge")) : Station);
            if (!Marker)
            {
                Fail(TEXT("The requested prison investigation or ending scene has no landmark"));
                return;
            }
            Player->SetActorLocation(Marker->Position + FVector(0, 0, 62), false, nullptr, ETeleportType::TeleportPhysics);
        }
        else Player->SetActorLocation(View->Player,false,nullptr,ETeleportType::TeleportPhysics);
        Player->GetCharacterMovement()->DisableMovement();
        const auto Tap = [&](const FKey& Key)
        {
            if (!PC->PlayerInput || !Player->InputComponent) return false;
            const TArray<UInputComponent*> Input{Player->InputComponent.Get()};
            constexpr float Frame = 1.f / 60.f;
            for (const EInputEvent Event : {IE_Pressed, IE_Released})
            {
                PC->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, Key, Event,
                    Event == IE_Pressed ? 1.f : 0.f, false, 0));
                PC->PlayerInput->ProcessInputStack(Input, Frame, false);
                Tower->Update(*Mode, Frame);
            }
            return true;
        };
        if (bSealPuzzle || bFindPuzzle)
        {
            const int32 Puzzle = bSealPuzzle ? 2 : FindIndex;
            if (!FDuelNorthVisualFixture::ConfirmStation(*Tower, *Mode, bSealPuzzle ? Station : ObjectId) ||
                Tower->GetPuzzle() != Puzzle || Tower->GetPuzzleRound() != 0)
            {
                Fail(TEXT("Live F did not open the puzzle at its physical object"));
                return;
            }
            bool bReady;
            if (bSealPuzzle)
                bReady = Tap(EKeys::D) && (!(bSealRing || bSealOpened) ||
                    (Tap(EKeys::F) && Tap(EKeys::A) && (!bSealOpened || Tap(EKeys::F))));
            else
                bReady = (!bKitchenPuzzle || Tap(EKeys::D)) && Tap(EKeys::F) &&
                    Tap(EKeys::D) && (!bKitchenPuzzle || Tap(EKeys::D));
            if (!bReady)
            {
                Fail(TEXT("Capture requires the actual bound puzzle controls"));
                return;
            }
            if (bSealOpened && (!Tower->Narrative() || !Tower->Narrative()->IsTalking() || Tower->GetPuzzle() != INDEX_NONE))
            {
                Fail(TEXT("Opening the sealed bag must start the evidence dialogue"));
                return;
            }
        }
        else if (bInspection || bEndingShot)
        {
            if (!FDuelNorthVisualFixture::ConfirmStation(*Tower, *Mode, bEndingShot ? FName(TEXT("GardenEdge")) : Station) ||
                !Tower->Narrative() || !(bEndingShot ? Tower->Narrative()->EndingVisible() : Tower->Narrative()->IsTalking()))
            {
                Fail(TEXT("Live F did not open the prison evidence or ending scene"));
                return;
            }
        }
        else if (bDialogueShot)
        {
            const int32 Line = bFinalTalk ? 1 : FCString::Atoi(*Args[0].Right(2));
            Tower->Update(*Mode,0.f);
            Mode->Confirm();
            for (int32 I = 1; I < Line; ++I) Mode->Confirm();
            const auto* Narrative = Tower->Narrative();
            if (!Narrative || !Narrative->IsTalking() || Narrative->Subtitle().Line != Line ||
                (bFinalTalk && Narrative->Subtitle().Character != EDuelCharacter::Yuki))
            {
                Fail(TEXT("Could not open the requested live conversation line"));
                return;
            }
        }
        if (bDialogueShot && Tower->Narrative() && Tower->Narrative()->IsReadingDocument())
        {
            Mode->Confirm();
            if (Tower->Narrative()->IsReadingDocument()) Mode->Confirm();
        }
        if (bDiaryPage)
        {
            if (!Tower->Narrative() || !Tower->Narrative()->IsReadingDocument())
            {
                Fail(TEXT("Diary capture requires the original paper before the analysis"));
                return;
            }
            if (Args[0] == TEXT("DiaryPage")) Mode->Confirm();
        }
        if (FParse::Param(FCommandLine::Get(),TEXT("DuelNorthColdLightOnly")))
        {
            for (auto* Light : TInlineComponentArray<UPointLightComponent*>(Tower))
                if (Light->ComponentHasTag(TEXT("NorthRestFire")) || Light->ComponentHasTag(TEXT("NorthTowerGuidanceLight")))
                    Light->SetHiddenInGame(true);
                else if (Light->ComponentHasTag(TEXT("NorthTowerFixedLight")) && Light->Temperature < 4500.f)
                    Light->SetIntensity(0.f);
            for (auto* Light : TInlineComponentArray<USpotLightComponent*>(Player))
                Light->SetIntensity(0.f);
        }
        const bool bSigil=View->Sigil!=ESigilShot::None;
        const bool bEffect = View->Effect != EEffectShot::None;
        const TSharedRef<FEffectSamples> Samples = MakeShared<FEffectSamples>();
        const bool bSigilBaseline=bSigil && FParse::Param(FCommandLine::Get(),TEXT("DuelNorthSigilBaseline"));
        float SigilAge=.20f;
        if(bSigil) FParse::Value(FCommandLine::Get(),TEXT("DuelNorthSigilAge="),SigilAge);
        SigilAge=FMath::Clamp(SigilAge,.09f,.45f);
        TWeakObjectPtr<ADuelArena> SigilArena;
        if(bSigil || bEffect || bEmaFlight)
        {
            for(TActorIterator<ADuelArena> It(World);It;++It) {SigilArena=*It;break;}
            if(!SigilArena.IsValid())
            {
                UE_LOG(LogTemp,Error,TEXT("North effect capture could not find the combat arena"));
                if(bExit)FPlatformMisc::RequestExitWithStatus(false,1);
                return;
            }
        }
        const bool bWayLight=View->WayLightYaw.IsSet();
        const bool bOriginalWayLight=bWayLight && FParse::Param(FCommandLine::Get(),TEXT("DuelOriginalWayLight"));
        Player->SetActorHiddenInGame(!bWayLight && !bWitchShot && !bEmaFlight);
        TWeakObjectPtr<USpotLightComponent> WayLight;
        if(bWayLight)
        {
            Player->SetActorRotation(FRotator(0,View->WayLightYaw.GetValue(),0),ETeleportType::TeleportPhysics);
            TInlineComponentArray<USpotLightComponent*> Lights(Player);
            for(auto* Light:Lights)
                if(Light->GetFName()==TEXT("NorthWayLight")) { WayLight=Light;break; }
            if(!WayLight.IsValid())
            {
                UE_LOG(LogTemp,Error,TEXT("North ground capture could not find the player's way light"));
                if(bExit)FPlatformMisc::RequestExitWithStatus(false,1);
                return;
            }
            if(bOriginalWayLight)
            {
                WayLight->SetRelativeLocation(FVector(22,0,-17));
                WayLight->SetRelativeRotation(FRotator(-11.5f,0,0));
                WayLight->SetIntensity(18.f);
                WayLight->SetAttenuationRadius(360.f);
                WayLight->SetInnerConeAngle(14.f);
                WayLight->SetOuterConeAngle(24.f);
            }
            if (FParse::Param(FCommandLine::Get(), TEXT("DuelWayLightNoBeam")))
                WayLight->SetIntensity(0.f);
        }
        const bool bFlight=View->Flight!=EPhase::Hidden;
        FTransform Aircraft=FTransform::Identity;
        if(bFlight)
        {
            using namespace DuelNorthExtractionVisual;
            // Freeze one light-warmed pose of the production airframe for each still.
            FVisual Visual=Build(Tower,Tower->GetRootComponent());
            if(!Visual.Root.IsValid())
            {
                UE_LOG(LogTemp,Error,TEXT("North aircraft capture could not create the runtime visuals"));
                if(bExit)FPlatformMisc::RequestExitWithStatus(false,1);
                return;
            }
            SetPose(Visual,View->Flight,View->FlightProgress,.847f);
            Aircraft=Visual.Root->GetComponentTransform();
        }
        FMinimalViewInfo CameraView;
        CameraView.Location=View->Eye; CameraView.Rotation=(View->Aim-View->Eye).Rotation(); CameraView.FOV=View->Fov;
        if (Args[0]==TEXT("ExtractionFinalLookback")) CameraView=DuelNorthExtractionVisual::FlightCamera(Aircraft,View->FlightProgress);
        if (Args[0] == TEXT("RestFire"))
            if (const auto* Rest = DuelNorthInteractables::Placements().FindByPredicate([](const auto& P){return P.Id == TEXT("PrisonRest");}))
            {
                CameraView.Location = Rest->Position + FVector(-160,137,152);
                CameraView.Rotation = (Rest->Position + FVector(0,0,58) - CameraView.Location).Rotation();
            }
        auto* Camera=World->SpawnActor<ACameraActor>(CameraView.Location,CameraView.Rotation);
        Camera->GetCameraComponent()->SetFieldOfView(CameraView.FOV);
        PC->SetViewTarget(Camera);
        const bool bShowHUD = bDialogueShot || bDiaryPage || bWitchShot || bObjectShot || bSealPuzzle || bEndingShot
            || Args[0] == TEXT("MirrorPressureForming");
        if(PC->GetHUD())PC->GetHUD()->bShowHUD=bShowHUD;
        const bool bExpectWitch = !FParse::Param(FCommandLine::Get(), TEXT("DuelNorthWitchFallback"));
        FTimerHandle Warmup;
        const auto Capture=[World,Tower,Mode,Player,Camera,File,ViewValue=*View,bExit,bFlight,Aircraft,WayLight,bWayLight,bOriginalWayLight,
            CameraView,bSigil,bSigilBaseline,SigilArena,SigilAge,bEffect,Samples,bDialogueShot,bWitchShot,bShowHUD,
            bFindPuzzle,bKitchenPuzzle,bSealPuzzle,bSealRing,bSealOpened,bEndingShot,bDiaryPage,bExpectWitch,WitchBoss,Fail,bEmaFlight,FlyingBoss]()
        {
            const auto Light=DuelNorthLighting::Stats(Tower);
            FString Receipt=FString::Printf(TEXT("view=%s\nplayer=%s\neye=%s\nrotation=%s\npoint_lights=%d\nspot_lights=%d\nev100=%.3f\n"),
                ViewValue.Name,*ViewValue.Player.ToString(),*CameraView.Location.ToString(),*CameraView.Rotation.ToString(),Light.VisiblePoints,Light.VisibleSpots,Light.ExposureEV100);
            if (bEmaFlight && (!FlyingBoss.IsValid() || !FDuelNorthVisualFixture::EmaFlightReceipt(*FlyingBoss, *Player, Receipt)))
            {
                Fail(TEXT("The independent Ema projectiles lost their 3D body, colour material or live flight state"));
                return;
            }
            if(bSigil)
            {
                auto* Arena=SigilArena.Get();
                if(!bSigilBaseline && (!Arena || !CheckSigil(*Arena,ViewValue.Sigil,Receipt)))
                {
                    UE_LOG(LogTemp,Error,TEXT("North sigil capture lacks the required live art or safety mask"));
                    if(bExit)FPlatformMisc::RequestExitWithStatus(false,1);
                    return;
                }
                int32 Artwork = 0;
                if (Arena)
                    for (const auto* Component : TInlineComponentArray<UStaticMeshComponent*>(Arena))
                        Artwork += Component->ComponentHasTag(TEXT("NorthSigilArtwork")) && Component->IsVisible();
                Receipt+=FString::Printf(TEXT("sigil_baseline=%d\nsigil_capture_delay=%.3f\nartwork_layers=%d\n"),
                    bSigilBaseline ? 1 : 0,SigilAge,Artwork);
            }
            if (bEffect)
            {
                int32 Facets = 0, Triangles = 0;
                const auto CheckFacet = [&](USceneComponent* Component)
                {
                    auto* Mesh = Cast<UProceduralMeshComponent>(Component);
                    if (!Mesh || !Mesh->IsRegistered() || !Mesh->IsVisible()) return;
                    auto* Interface = Mesh->GetMaterial(0);
                    const auto* Material = Interface ? Interface->GetMaterial() : nullptr;
                    const auto* Section = Mesh->GetProcMeshSection(0);
                    if (!Material || Material->GetFName() != TEXT("M_NorthSpellFacet") ||
                        !Section || Section->ProcIndexBuffer.IsEmpty()) return;
                    ++Facets;
                    Triangles += Section->ProcIndexBuffer.Num() / 3;
                };
                for (const auto& Weak : *Samples)
                    if (auto* Component = Weak.Get())
                    {
                        CheckFacet(Component);
                        for (USceneComponent* Child : Component->GetAttachChildren()) CheckFacet(Child);
                    }
                if (Facets == 0)
                {
                    Fail(TEXT("The production effect has no live SpellFacet body"));
                    return;
                }
                Receipt += FString::Printf(TEXT("production_effect=%d\nmaterial=M_NorthSpellFacet\nfacet_bodies=%d\nfacet_triangles=%d\n"),
                    int32(ViewValue.Effect), Facets, Triangles);
            }
            if(FStringView(ViewValue.Name).StartsWith(TEXT("MirrorPressure")))
            {
                int32 Walls=0, ReflectedPixels=0;
                for(TActorIterator<ADuelArena> It(World);It;++It)
                {
                    for(const auto* C:TInlineComponentArray<USceneComponent*>(*It))
                        Walls+=C->ComponentHasTag(TEXT("MirrorPressureWall")) && C->IsVisible();
                    for(auto* C:TInlineComponentArray<USceneCaptureComponent2D*>(*It))
                        if(C->TextureTarget && C->CaptureSource == SCS_SceneColorHDR && (!C->ShowOnlyActors.IsEmpty() || !C->ShowOnlyComponents.IsEmpty()))
                        {
                            TArray<FLinearColor> Pixels;
                            if (C->TextureTarget->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels))
                                for(const auto& P:Pixels) ReflectedPixels+=P.A<.65f && FMath::Max3(P.R,P.G,P.B)>.008f;
                        }
                }
                if(Walls!=4) { Fail(TEXT("Mirror chamber capture lost one of its four fog mirrors"));return; }
                if(!ReflectedPixels) { Fail(TEXT("No boss pixels rendered in the fog mirrors"));return; }
                Receipt+=FString::Printf(TEXT("mirror_pressure_walls=%d\nboss_reflection_pixels=%d\n"),Walls,ReflectedPixels);
            }
            if(bFlight)
            {
                FVector2D Resolution=FVector2D::ZeroVector;
                if(auto* Viewport=World->GetGameViewport())Viewport->GetViewportSize(Resolution);
                if(Resolution!=FVector2D(1920,1080))
                {
                    UE_LOG(LogTemp,Error,TEXT("North aircraft capture expected 1920x1080 and got %s"),*Resolution.ToString());
                    if(bExit)FPlatformMisc::RequestExitWithStatus(false,1);
                    return;
                }
                Receipt+=FString::Printf(TEXT("resolution=1920x1080\nfov=%.1f\nflight_phase=%s\nflight_progress=%.3f\naircraft_location=%s\naircraft_rotation=%s\n"),
                    CameraView.FOV,ViewValue.Flight==EPhase::Hold?TEXT("Hold"):TEXT("Departure"),ViewValue.FlightProgress,
                    *Aircraft.GetLocation().ToString(),*Aircraft.Rotator().ToString());
            }
            if(bWayLight)
            {
                const auto* Spot=WayLight.Get();
                const FRotator Facing(0,ViewValue.WayLightYaw.GetValue(),0);
                if(!Spot || !Spot->IsRegistered() || !Spot->IsVisible() ||
                    FVector::DotProduct(Spot->GetForwardVector().GetSafeNormal2D(),Facing.Vector())<.999)
                {
                    UE_LOG(LogTemp,Error,TEXT("North ground capture lost the active, forward-facing way light"));
                    if(bExit)FPlatformMisc::RequestExitWithStatus(false,1);
                    return;
                }
                Receipt+=FString::Printf(TEXT("way_light=%s\nplayer_yaw=%.1f\nlamp_location=%s\nlamp_rotation=%s\ncandela=%.1f\nradius_cm=%.1f\ninner_degrees=%.1f\nouter_degrees=%.1f\n"),
                    bOriginalWayLight?TEXT("original"):TEXT("production"),Facing.Yaw,
                    *Spot->GetComponentLocation().ToString(),*Spot->GetComponentRotation().ToString(),
                    Spot->Intensity,Spot->AttenuationRadius,Spot->InnerConeAngle,Spot->OuterConeAngle);
            }
            if (FStringView(ViewValue.Name) == TEXT("LampSpirit"))
                for (const auto* Orb : TInlineComponentArray<UStaticMeshComponent*>(Tower))
                    if (Orb->ComponentHasTag(TEXT("NorthTowerGuidanceFlame")) && Orb->IsVisible())
                    {
                        Camera->SetActorRotation((Orb->GetComponentLocation() - Camera->GetActorLocation()).Rotation());
                        Receipt+=FString::Printf(TEXT("lamp_spirit=%s\n"),*Orb->GetComponentLocation().ToString());
                        break;
                    }
            if (bDiaryPage)
            {
                const auto* Narrative = Tower->Narrative();
                if (!Narrative || !Narrative->IsReadingDocument() || !Narrative->Subtitle().Text.IsEmpty())
                {
                    Fail(TEXT("Diary capture lost the original paper or prematurely started the analysis"));
                    return;
                }
                Receipt += FString::Printf(TEXT("document_title=%s\nvisible_characters=%d/%d\ndocument=%s\n"),
                    *Narrative->DocumentTitle(), Narrative->DocumentVisibleCharacters(), Narrative->DocumentText().Len(),
                    *Narrative->DocumentText());
            }
            if (bDialogueShot)
            {
                const auto* Narrative = Tower->Narrative();
                const auto Subtitle = Narrative->Subtitle();
                const UTexture* Art = Narrative->DialoguePortrait();
                if (Subtitle.Text.IsEmpty() || !Art || !Art->GetResource())
                {
                    Fail(TEXT("North dialogue capture lost its live portrait or subtitle"));
                    return;
                }
                Receipt += FString::Printf(TEXT("speaker=%s\nline=%d/%d\nsubtitle=%s\nportrait=%s\nexpression=%d\nportrait_size=%.0fx%.0f\n"),
                    *Subtitle.Speaker, Subtitle.Line, Subtitle.Count, *Subtitle.Text,
                    Narrative->UsesLocalPortrait() ? TEXT("local") : TEXT("model"), static_cast<int32>(Subtitle.Expression),
                    Art->GetSurfaceWidth(), Art->GetSurfaceHeight());
            }
            if (bWitchShot)
            {
                float Opacity = 0.f;
                UTexture* Art = Mode->NorthWitchPortrait(Opacity);
                if (!WitchBoss.IsValid() || WitchBoss->Phase() != 2 || (Art != nullptr) != bExpectWitch ||
                    (bExpectWitch && (!Art->GetResource() || Opacity < .90f)))
                {
                    Fail(TEXT("The witch portrait or its fallback was incorrect at the combat transition"));
                    return;
                }
                Receipt += FString::Printf(TEXT("boss=%s\nphase=%d\nwitch_portrait=%s\nopacity=%.3f\n"),
                    WitchBoss->CharacterName(), WitchBoss->Phase(), Art ? TEXT("local") : TEXT("none"), Opacity);
            }
            if (bFindPuzzle || (bSealPuzzle && !bSealOpened))
            {
                const int32 Puzzle = bSealPuzzle ? 2 : bKitchenPuzzle ? 0 : 1;
                const int32 Round = bFindPuzzle || bSealRing ? 1 : 0;
                const int32 Choice = bKitchenPuzzle ? 2 : bSealRing ? 0 : 1;
                if (Tower->GetPuzzle() != Puzzle || Tower->GetPuzzleRound() != Round || Tower->GetPuzzleChoice() != Choice)
                {
                    Fail(TEXT("Prison capture lost its live puzzle round and selection"));
                    return;
                }
                Receipt += FString::Printf(TEXT("puzzle=%d\nround=%d\nchoice=%d\n"), Puzzle, Round, Choice);
                if (bFindPuzzle)
                    Receipt += FString::Printf(TEXT("question=%s\nanswer=%s\n"), DuelNorthFinds::Question(Puzzle, Round),
                        DuelNorthFinds::Answer(Puzzle, Round, Choice));
            }
            if (FStringView(ViewValue.Name) == TEXT("Confinement03"))
            {
                const auto* Gate = DuelNorthLayout::Portals().FindByPredicate([](const auto& P) { return P.Id == TEXT("DungeonGate"); });
                const UTextRenderComponent* Plaque = nullptr;
                for (const auto* Number : TInlineComponentArray<UTextRenderComponent*>(Tower))
                    if (Number->Text.ToString() == TEXT("03")) { Plaque = Number; break; }
                if (!Gate || Gate->RoomB != TEXT("DungeonCourt") || !Plaque || !Plaque->IsRegistered() ||
                    !Plaque->IsVisible() || Plaque->bHiddenInGame ||
                    !Plaque->GetComponentLocation().Equals(Gate->Center + FVector(-29, Gate->Width * .5 + 88., 150.), .25))
                {
                    Fail(TEXT("The 03 plaque is missing from the approach side of the actual dungeon fog"));
                    return;
                }
                Receipt += FString::Printf(TEXT("plaque=03\nportal=DungeonGate\nroom=DungeonCourt\nplaque_at=%s\n"),
                    *Plaque->GetComponentLocation().ToString());
            }
            if (bEndingShot)
            {
                if (!Tower->Narrative()->EndingVisible())
                {
                    Fail(TEXT("The live prison ending page closed before rendering"));
                    return;
                }
                Receipt += FString::Printf(TEXT("ending=%s\n"), FDuelNorthProgress::EndingText());
            }
            FFileHelper::SaveStringToFile(Receipt,*FPaths::ChangeExtension(File,TEXT("txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            FScreenshotRequest::RequestScreenshot(File,bShowHUD,false);
            UE_LOG(LogTemp,Display,TEXT("North visual capture requested: %s"),*File);
            if(bExit)
            {
                FTimerHandle Quit;
                World->GetTimerManager().SetTimer(Quit,[File,Mode,Player,bWitchShot,bExpectWitch,WitchBoss]()
                {
                    const bool bSaved=IFileManager::Get().FileSize(*File)>0;
                    if(!bSaved)UE_LOG(LogTemp,Error,TEXT("North visual capture did not write %s"),*File);
                    bool bWitchLifecycle = true;
                    if (bWitchShot)
                    {
                        float Opacity = 0.f;
                        auto* Boss = WitchBoss.Get();
                        const bool bExpired = !Mode->NorthWitchPortrait(Opacity);
                        const bool bDefeated = Boss && Player->ReceiveDuelDamage(DuelCourt::PlayerMaxHealth * 3.f,
                            Boss->GetActorLocation()) && Mode->Outcome() == EDuelOutcome::Defeat;
                        if (bDefeated) Mode->Confirm();
                        const bool bReset = bDefeated && Mode->Outcome() == EDuelOutcome::Ready &&
                            !Mode->NorthWitchPortrait(Opacity);
                        if (bReset) Mode->ResetEncounter();
                        const bool bAgain = bReset && Boss->Phase() == 1 && FDuelNorthVisualFixture::EnterWitchPhase(*Boss, *Player) &&
                            (Mode->NorthWitchPortrait(Opacity) != nullptr) == bExpectWitch;
                        bWitchLifecycle = bExpired && bReset && bAgain;
                        UE_LOG(LogTemp, Display, TEXT("North witch lifecycle: expiry=%d defeat_retry=%d second_transition=%d"),
                            bExpired ? 1 : 0, bReset ? 1 : 0, bAgain ? 1 : 0);
                    }
                    FPlatformMisc::RequestExitWithStatus(false,bSaved && bWitchLifecycle ? 0 : 1);
                },bWitchShot || bFlight ? 2.f : 1.f,false);
            }
        };
        if (bEmaFlight)
        {
            World->GetTimerManager().SetTimer(Warmup, [World, View, FlyingBoss, SigilArena, Capture, Fail]()
            {
                if (!FlyingBoss.IsValid() || !SigilArena.IsValid() ||
                    !FDuelNorthVisualFixture::EmaFlightFrame(*FlyingBoss, *SigilArena, View->EmaFlightFraction))
                {
                    Fail(TEXT("Could not sample the actual OssuaryShears flight")); return;
                }
                FTimerHandle Frame;
                World->GetTimerManager().SetTimer(Frame, Capture, .08f, false);
            }, 4.f, false);
        }
        else if (bWitchShot)
        {
            World->GetTimerManager().SetTimer(Warmup, [World, Player, WitchBoss, Capture, Fail]()
            {
                auto* Boss = WitchBoss.Get();
                if (!Boss || !FDuelNorthVisualFixture::EnterWitchPhase(*Boss, *Player))
                {
                    Fail(TEXT("Could not trigger the live first phase transition"));
                    return;
                }
                FTimerHandle Frame;
                World->GetTimerManager().SetTimer(Frame, Capture, .45f, false);
            }, 4.f, false);
        }
        else if (bEffect)
        {
            World->GetTimerManager().SetTimer(Warmup, [World, View, SigilArena, Samples, Capture]()
            {
                if (auto* Arena = SigilArena.Get()) SpawnEffectSample(*Arena, View->Effect, *Samples);
                FTimerHandle Frame;
                World->GetTimerManager().SetTimer(Frame, Capture, .14f, false);
            }, 4.f, false);
        }
        else if(bSigil)
        {
            World->GetTimerManager().SetTimer(Warmup,[World,View,SigilArena,SigilAge,bSigilBaseline,Capture]()
            {
                if(auto* Arena=SigilArena.Get();Arena && !bSigilBaseline)SpawnSigil(*Arena,View->Sigil);
                FTimerHandle Frame;
                World->GetTimerManager().SetTimer(Frame,Capture,SigilAge,false);
            },4.f,false);
        }
        else World->GetTimerManager().SetTimer(Warmup,Capture,4.f,false);
    }
    FAutoConsoleCommandWithWorldAndArgs NorthCaptureCommand(TEXT("Duel.CaptureNorth"),
        TEXT("Capture a named North Tower view in an isolated game. Interior views include PrisonLife, PrisonBunks, PrisonKitchen, PrisonHallwayEast, ChapelReading, ChapelStacks, ChapelAisle, ChapelOuterWest, ChapelOuterEast, ChapelWallBase and ChapelHallwayEast."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CaptureNorth));

    void CaptureNorthSequence(const TArray<FString>& Args,UWorld* World)
    {
        if(!World || Args.Num()!=1 || !FParse::Param(FCommandLine::Get(),TEXT("DuelNorthTower")) ||
            !FParse::Param(FCommandLine::Get(),TEXT("DuelNoSave")))return;
        struct FSequenceView {const TCHAR* Name;FVector At;float Yaw,Pitch;bool bFlight=false;bool bWalk=false;};
        const FSequenceView Sequences[]={
            {TEXT("GardenExitNormal"),{5500,0,3462},0,-15},
            {TEXT("GardenCliffNormal"),{7350,-950,3462},0,-15},
            {TEXT("GardenTransitionNormal"),{3000,0,3462},0,-15,false,true},
            {TEXT("GardenSeaNormal"),{7350,-950,3462},-65,-15},
            {TEXT("ExtractionDepartureNormal"),{6350,-300,3462},0,-15,true}
        };
        const auto* View=MakeArrayView(Sequences).FindByPredicate([&](const auto& V){return Args[0]==V.Name;});
        auto* Mode=World->GetAuthGameMode<ADuelMode>();
        auto* PC=World->GetFirstPlayerController();
        if(!View || !Mode || !PC || !Mode->NorthTower() || !Mode->GetPlayerFighter())return;
        const bool bExit=FParse::Param(FCommandLine::Get(),TEXT("DuelQAQuit"));
        const auto Fail=[bExit](const TCHAR* Why)
        {UE_LOG(LogTemp,Error,TEXT("North sequence: %s"),Why);if(bExit)FPlatformMisc::RequestExitWithStatus(false,1);};
        auto* Tower=const_cast<ADuelNorthTower*>(Mode->NorthTower());
        auto* Player=const_cast<ADuelFighter*>(Mode->GetPlayerFighter());
        Tower->NewGame(*Mode);
        if(!FDuelNorthVisualFixture::PrepareEnding(*Tower)){Fail(TEXT("Could not open the actual garden exit"));return;}
        Player->SetActorLocation(View->At,false,nullptr,ETeleportType::TeleportPhysics);
        if(View->bWalk)Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        else Player->GetCharacterMovement()->DisableMovement();
        FDuelNorthVisualFixture::NormalView(*Player,View->Yaw,View->Pitch);
        Tower->Update(*Mode,0.f);
        PC->SetViewTarget(Player);
        if(PC->GetHUD())PC->GetHUD()->bShowHUD=!View->bFlight;
        UGameUserSettings::RequestResolutionChange(1920,1080,EWindowMode::Windowed,false);
        FString Directory;
        if(!FParse::Value(FCommandLine::Get(),TEXT("DuelNorthCaptureOutput="),Directory))Directory=FPaths::ProjectSavedDir()/TEXT("NorthTower345/Sequence");
        Directory=FPaths::ConvertRelativePathToFull(Directory);
        IFileManager::Get().MakeDirectory(*Directory,true);
        const FString Name=View->Name;
        TArray<FString> Files;
        for(int32 I=0;I<(View->bWalk?17:9);++I)
        {
            Files.Add(Directory/FString::Printf(TEXT("%s-%02d.png"),*Name,I));
            if(IFileManager::Get().FileExists(*Files.Last())){Fail(TEXT("Refusing to overwrite a sequence frame"));return;}
        }
        const bool bFlight=View->bFlight;
        const bool bWalk=View->bWalk;
        const float Started=World->GetTimeSeconds()+4.f;
        const auto Flight=MakeShared<DuelNorthExtractionVisual::FVisual>();
        const auto FlightTimer=MakeShared<FTimerHandle>();
        ACameraActor* FlightCamera=nullptr;
        if(bFlight)
        {
            *Flight=DuelNorthExtractionVisual::Build(Tower,Tower->GetRootComponent());
            if(!Flight->Root.IsValid()){Fail(TEXT("Missing the production rescue aircraft"));return;}
            FlightCamera=World->SpawnActor<ACameraActor>();
            PC->SetViewTarget(FlightCamera);
            World->GetTimerManager().SetTimer(*FlightTimer,[World,Flight,FlightCamera,Started]()
            {
                const float Progress=FMath::Clamp((World->GetTimeSeconds()-Started)/DuelNorthExtractionVisual::DepartureSeconds+.03f,.03f,1.f);
                DuelNorthExtractionVisual::SetPose(*Flight,EPhase::Departure,Progress,World->GetTimeSeconds());
                const auto V=DuelNorthExtractionVisual::FlightCamera(Flight->Root->GetComponentTransform(),Progress);
                FlightCamera->SetActorLocationAndRotation(V.Location,V.Rotation);
                FlightCamera->GetCameraComponent()->SetFieldOfView(V.FOV);
            },1.f/60.f,true);
        }
        else if(bWalk)
        {
            World->GetTimerManager().SetTimer(*FlightTimer,[World,Player,Started]()
            {
                if(World->GetTimeSeconds()<Started || Player->GetActorLocation().X>=6000.)return;
                Player->AddMovementInput(FVector::ForwardVector,1.f);
            },1.f/60.f,true);
        }
        const auto Counter=MakeShared<int32>(0);
        const auto FrameTimer=MakeShared<FTimerHandle>();
        World->GetTimerManager().SetTimer(*FrameTimer,[World,Tower,Player,PC,FlightCamera,Flight,FlightTimer,FrameTimer,Counter,Files,Name,Started,bFlight,bWalk,bExit,Fail]()
        {
            const int32 I=(*Counter)++;
            if(PC->GetViewTarget()!=(bFlight?static_cast<AActor*>(FlightCamera):static_cast<AActor*>(Player)))
            {Fail(TEXT("Sequence lost its normal production camera"));World->GetTimerManager().ClearTimer(*FrameTimer);return;}
            FMinimalViewInfo Actual;
            if(bFlight)FlightCamera->CalcCamera(0.f,Actual);else Player->CalcCamera(0.f,Actual);
            FString Receipt=FString::Printf(TEXT("sequence=%s\nframe=%d\nworld_seconds=%.5f\nsequence_seconds=%.5f\ncamera_source=%s\nview_target=%s\nplayer=%s\neye=%s\nrotation=%s\nfov=%.3f\nroom=%s\n"),
                *Name,I,World->GetTimeSeconds(),World->GetTimeSeconds()-Started,bFlight?TEXT("ProductionFlightCamera"):TEXT("Player.CalcCamera"),
                *GetNameSafe(PC->GetViewTarget()),*Player->GetActorLocation().ToString(),*Actual.Location.ToString(),*Actual.Rotation.ToString(),Actual.FOV,
                *DuelNorthLayout::Locate(Player->GetActorLocation()).Room.ToString());
            if(bFlight)Receipt+=FString::Printf(TEXT("aircraft=%s\n"),*Flight->Root->GetComponentLocation().ToString());
            if(bWalk)Receipt+=TEXT("movement=ProductionCharacterMovement_Walking\n");
            FFileHelper::SaveStringToFile(Receipt,*FPaths::ChangeExtension(Files[I],TEXT("txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            FScreenshotRequest::RequestScreenshot(Files[I],!bFlight,false);
            UE_LOG(LogTemp,Display,TEXT("North sequence requested: %s"),*Files[I]);
            if(I==Files.Num()-1)
            {
                FTimerHandle Finish;
                World->GetTimerManager().SetTimer(Finish,[Files,bExit]()
                {
                    bool bComplete=true;
                    for(const auto& File:Files)bComplete&=IFileManager::Get().FileSize(*File)>0;
                    UE_LOG(LogTemp,Display,TEXT("North sequence complete: frames=%d saved=%d"),Files.Num(),bComplete?1:0);
                    if(bExit)FPlatformMisc::RequestExitWithStatus(false,bComplete?0:1);
                },2.f,false);
                World->GetTimerManager().ClearTimer(*FlightTimer);
                // Clear our delegate last: it owns every capture used above.
                World->GetTimerManager().ClearTimer(*FrameTimer);
            }
        },.5f,true,4.f);
    }
    FAutoConsoleCommandWithWorldAndArgs NorthSequenceCommand(TEXT("Duel.CaptureNorthSequence"),
        TEXT("Capture real-camera frames: GardenExitNormal, GardenCliffNormal, GardenSeaNormal, ExtractionDepartureNormal; GardenTransitionNormal walks from the indoor exit onto the terrace over eight seconds."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CaptureNorthSequence));

    void CaptureJudgmentPenalty(const TArray<FString>& Args, UWorld* World)
    {
        if (!World || Args.Num() != 1 || (Args[0] != TEXT("Early") && Args[0] != TEXT("Moving") && Args[0] != TEXT("Sequence")) ||
            !FParse::Param(FCommandLine::Get(), TEXT("DuelNorthTower")) ||
            !FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")) ||
            !FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen"))) return;
        auto* Mode = World->GetAuthGameMode<ADuelMode>();
        auto* Tower = Mode ? const_cast<ADuelNorthTower*>(Mode->NorthTower()) : nullptr;
        auto* Player = Mode ? const_cast<ADuelFighter*>(Mode->GetPlayerFighter()) : nullptr;
        auto* PC = World->GetFirstPlayerController();
        FString Output;
        if (!Mode || !Tower || !Player || !PC || !FParse::Value(FCommandLine::Get(), TEXT("DuelNorthCaptureOutput="), Output) ||
            FPaths::IsRelative(Output) || !IFileManager::Get().MakeDirectory(*Output, true)) return;
        const FString File = Output / (TEXT("JudgmentPenalty") + Args[0] + TEXT(".png"));
        const FString Receipt = FPaths::ChangeExtension(File, TEXT("txt"));
        if (IFileManager::Get().FileExists(*File) || IFileManager::Get().FileExists(*Receipt))
        { FPlatformMisc::RequestExitWithStatus(false, 2); return; }
        const auto Fail = [Receipt](const TCHAR* Reason)
        {
            FFileHelper::SaveStringToFile(FString(TEXT("passed=0\nerror=")) + Reason + TEXT("\n"), *Receipt);
            FPlatformMisc::RequestExitWithStatus(false, 1);
        };
        FString Hero;
        FParse::Value(FCommandLine::Get(), TEXT("DuelJudgmentHero="), Hero);
        if (!Player->SelectCharacter(Hero == TEXT("Ema") ? EDuelCharacter::Ema : EDuelCharacter::Hiro))
        { Fail(TEXT("Could not select the requested protagonist")); return; }
        Tower->NewGame(*Mode);
        if (!FDuelNorthVisualFixture::BeginJudgmentHUD(*Tower, *Mode, *Player))
        { Fail(TEXT("Could not enter the real Yuki encounter")); return; }
        Tower->ShowMechanicHint(2);
        PC->SetViewTarget(Player);
        if (PC->GetHUD()) PC->GetHUD()->bShowHUD = true;
        const bool bSequence = Args[0] == TEXT("Sequence");
        const FString FrameDirectory = Output / TEXT("JudgmentPenaltySequence");
        if (bSequence && (IFileManager::Get().DirectoryExists(*FrameDirectory) ||
            !IFileManager::Get().MakeDirectory(*FrameDirectory, false)))
        { Fail(TEXT("Sequence frames require a new directory")); return; }
        const float Delay = Args[0] == TEXT("Early") ? .2f : 1.2f;
        FTimerHandle Warmup;
        World->GetTimerManager().SetTimer(Warmup, [World, Mode, Player, PC, File, Receipt, Fail, Delay, bSequence, FrameDirectory]()
        {
            if (!FDuelNorthVisualFixture::ApplyJudgmentHUDPenalty(*Player))
            { Fail(TEXT("The production judgment failure path did not apply its two locks")); return; }
            const double Began = World->GetTimeSeconds();
            if (bSequence)
            {
                auto Files = MakeShared<TArray<FString>>();
                auto Rows = MakeShared<TArray<FString>>();
                Rows->Add(TEXT("frame,world_elapsed,move_lock_seconds,weapon_lock_seconds"));
                auto Frames = MakeShared<FTimerHandle>();
                World->GetTimerManager().SetTimer(*Frames, [World, Mode, Player, PC, Receipt, Fail, FrameDirectory, Began, Files, Rows, Frames]()
                {
                    if (FScreenshotRequest::IsScreenshotRequested()) return;
                    const double Elapsed = World->GetTimeSeconds() - Began;
                    if (Mode->Outcome() != EDuelOutcome::Fighting || PC->GetViewTarget() != Player || Elapsed > 8.)
                    { Fail(TEXT("Countdown sequence lost its real encounter or player camera")); World->GetTimerManager().ClearTimer(*Frames); return; }
                    const int32 Index = Files->Num();
                    const FString Frame = FrameDirectory / FString::Printf(TEXT("Frame-%03d.png"), Index);
                    Files->Add(Frame);
                    Rows->Add(FString::Printf(TEXT("%d,%.6f,%.6f,%.6f"), Index, Elapsed,
                        Player->JudgmentMoveLockSeconds(), Player->JudgmentWeaponLockSeconds()));
                    FScreenshotRequest::RequestScreenshot(Frame, true, false);
                    if (Elapsed >= 5.5)
                    {
                        FTimerHandle Finish;
                        World->GetTimerManager().SetTimer(Finish, [Receipt, FrameDirectory, Files, Rows]()
                        {
                            bool Complete = Files->Num() >= 20;
                            for (const auto& Image : *Files) Complete &= IFileManager::Get().FileSize(*Image) > 0;
                            Complete &= FFileHelper::SaveStringArrayToFile(*Rows, *(FrameDirectory / TEXT("timers.csv")));
                            Complete &= FFileHelper::SaveStringToFile(FString::Printf(TEXT("passed=%d\nframes=%d\ncamera_source=Player.CalcCamera\nproduction_method=BeginCourtRetaliation->ApplyJudgmentPenalty\ntiming=actual_world_seconds_in_timers.csv\ndesktop_input=none\n"),
                                Complete ? 1 : 0, Files->Num()), *Receipt, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
                            FPlatformMisc::RequestExitWithStatus(false, Complete ? 0 : 1);
                        }, 1.f, false);
                        // Clear last because the repeating delegate owns its captured values.
                        World->GetTimerManager().ClearTimer(*Frames);
                    }
                }, .1f, true);
                return;
            }
            FTimerHandle Sample;
            World->GetTimerManager().SetTimer(Sample, [World, Mode, Player, PC, File, Receipt, Fail, Delay, Began]()
            {
                const float Move = Player->JudgmentMoveLockSeconds(), Weapon = Player->JudgmentWeaponLockSeconds();
                const float Elapsed = World->GetTimeSeconds() - Began;
                if (Mode->Outcome() != EDuelOutcome::Fighting || PC->GetViewTarget() != Player ||
                    !Mode->GetBossFighter() || !Mode->GetBossFighter()->JudgmentImmune() ||
                    FMath::Abs(Elapsed - Delay) > .35f || Weapon <= 0.f || (Delay < 1.f ? Move <= 0.f : Move > 0.f))
                { Fail(TEXT("The real lock timers or normal player camera did not reach the requested stage")); return; }
                FMinimalViewInfo View;
                Player->CalcCamera(0.f, View);
                const FString Result = FString::Printf(TEXT("passed=1\nproduction_method=BeginCourtRetaliation->ApplyJudgmentPenalty\njudgment_ward=1\nrequested_seconds=%.3f\nactual_world_seconds=%.3f\nmove_lock_seconds=%.3f\nweapon_lock_seconds=%.3f\nplayer=%s\ncamera_source=Player.CalcCamera\neye=%s\nrotation=%s\ndesktop_input=none\n"),
                    Delay, Elapsed, Move, Weapon, Player->CharacterName(), *View.Location.ToString(), *View.Rotation.ToString());
                if (!FFileHelper::SaveStringToFile(Result, *Receipt, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
                { Fail(TEXT("Could not save the real countdown receipt")); return; }
                FScreenshotRequest::RequestScreenshot(File, true, false);
                FTimerHandle Finish;
                World->GetTimerManager().SetTimer(Finish, [File]()
                { FPlatformMisc::RequestExitWithStatus(false, IFileManager::Get().FileSize(*File) > 0 ? 0 : 1); }, 1.f, false);
            }, Delay, false);
        }, 4.f, false);
    }
    FAutoConsoleCommandWithWorldAndArgs JudgmentPenaltyCommand(TEXT("Duel.CaptureJudgmentPenalty"),
        TEXT("Capture real-world-time lock HUD stages Early, Moving or a full Sequence in an isolated offscreen tower."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CaptureJudgmentPenalty));
}
#endif
