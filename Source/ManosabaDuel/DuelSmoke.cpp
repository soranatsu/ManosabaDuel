#include "DuelNorthInvestigationQA.h"
#include "DuelGame.h"
#include "DuelBossDefense.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DuelArena.h"
#include "DuelCastle.h"
#include "DuelLiveCombatQA.h"
#include "DuelNorthNarrative.h"
#include "DuelNorthFinds.h"
#include "DuelNorthTower.h"
#include "DuelProgress.h"
#include "Camera/CameraTypes.h"
#include "Containers/Ticker.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

struct FDuelVisualQA
{
    static void ProfileCamera(const TArray<FString>& Args, UWorld* World)
    {
        ADuelMode* Mode = World ? World->GetAuthGameMode<ADuelMode>() : nullptr;
        if (!Mode || !Mode->PlayerFighter || !Mode->UsesCastle() || Args.Num() != 1 ||
            !FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")))
        {
            return;
        }
        ADuelFighter* Fighter = Mode->PlayerFighter;
        const bool bHub = Args[0] == TEXT("Hub");
        Fighter->SetActorLocation(bHub ? DuelCastle::Hub + FVector(0, 60, 62) : FVector(-11015, -3430, -238));
        Fighter->ViewPitch = -8.f;
        Fighter->ViewYaw = 0.f;
        TWeakObjectPtr<ADuelFighter> Player(Fighter);
        const double Start = FPlatformTime::Seconds();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Player, Start, Count = 0, bCapture = false](float) mutable
        {
            ADuelFighter* At = Player.Get();
            if (!At)
            {
                return false;
            }
            const double Time = FPlatformTime::Seconds() - Start;
            if (Time >= 4.0 && !bCapture)
            {
                GEngine->Exec(At->GetWorld(), TEXT("CsvProfile START"));
                bCapture = true;
            }
            At->ViewYaw = FRotator::NormalizeAxis(52.0 * FMath::Max(0.0, Time - 4.0));
            if (!bCapture || ++Count < 360)
            {
                return true;
            }
            GEngine->Exec(At->GetWorld(), TEXT("CsvProfile STOP"));
            UE_LOG(LogTemp, Display, TEXT("Castle camera profile: 360 rotating frames, %.3f s"), Time - 4.0);
            return false;
        }));
    }

    static void Capture(const TArray<FString>& Args, UWorld* World)
    {
        ADuelMode* Mode = World ? World->GetAuthGameMode<ADuelMode>() : nullptr;
        if (!Mode || !Mode->PlayerFighter || !Mode->BossFighter || Args.Num() != 1)
        {
            return;
        }

        const FString& Stage = Args[0];
        if (Stage == TEXT("Title") && Mode->Outcome() == EDuelOutcome::Title)
        {
            const FString File = FPaths::ProjectSavedDir() / TEXT("Screenshots/RainCourt-Title.png");
            FScreenshotRequest::RequestScreenshot(File, false, false);
            return;
        }
        if (Mode->Outcome() == EDuelOutcome::Title)
        {
            Mode->MenuSelection = 1;
            Mode->Confirm();
        }
        if (Mode->UsesCastle() && Stage.StartsWith(TEXT("Castle")))
        {
            ADuelFighter& Player = *Mode->PlayerFighter;
            Player.ViewPitch = -8.f;
            if (Stage == TEXT("CastleFlood"))
            {
                Player.SetActorLocation(FVector(-11015, -3430, -238));
                Player.ViewYaw = 92.f;
            }
            else if (Stage == TEXT("CastleFurnace"))
            {
                Player.SetActorLocation(FVector(-6010, -9750, -283));
                Player.ViewYaw = 338.f;
            }
            else if (Stage == TEXT("CastleEscort"))
            {
                Player.SetActorLocation(FVector(4650, -4310, 267));
                Player.ViewYaw = 346.f;
            }
            else if (Stage == TEXT("CastleFinal"))
            {
                const float X = 16900.f;
                const float Z = FMath::GetMappedRangeValueClamped(
                    FVector2D(DuelCourt::YukiOrigin.X + DuelCourt::YukiStairStartX, DuelCourt::YukiOrigin.X + DuelCourt::YukiStairEndX),
                    FVector2D(DuelCourt::YukiOrigin.Z - DuelCourt::YukiStairRise, DuelCourt::YukiOrigin.Z), X);
                Player.SetActorLocation(FVector(X, -25, Z + 66.f));
                Player.ViewYaw = 0.f;
            }
            else if (Stage == TEXT("CastleStairCorner"))
            {
                Player.SetActorLocation(FVector(15725, -145, 578));
                Player.ViewYaw = 43.f;
                Player.ViewPitch = 18.f;
            }
            else if (Stage == TEXT("CastleFinalHall"))
            {
                Mode->SetCastleCourt(DuelCastle::WingCount);
                Mode->SetBoss(EDuelCharacter::Yuki);
                Player.SetActorLocation(DuelCourt::YukiOrigin + FVector(-1120, 214, 62));
                Mode->ResetEncounter(true);
                Mode->BossFighter->BossPause = 5.f;
                Player.ToggleLock();
                Player.ViewYaw = 0.f;
            }
            else if (Stage == TEXT("CastleGarden"))
            {
                Mode->SetCastleCourt(DuelCastle::WingCount);
                Mode->bFinalRecord = true;
                Mode->RestoreCastleLayout();
                Mode->FinishTrial();
                Player.SetActorLocation(DuelCourt::YukiOrigin + FVector(2950, 0, 62));
                Player.ViewYaw = 0.f;
                Player.ViewPitch = 11.f;
            }
            else
            {
                Player.SetActorLocation(DuelCastle::Hub + FVector(22, -390, 62));
                Player.ViewYaw = 92.f;
            }
            const FString File = FPaths::ProjectSavedDir() / FString::Printf(TEXT("Screenshots/%s.png"), *Stage);
            FTimerHandle CaptureTimer;
            World->GetTimerManager().SetTimer(CaptureTimer, [File]()
            {
                FScreenshotRequest::RequestScreenshot(File, false, false);
            }, 2.05f, false);
            return;
        }
        const bool bProfile = Stage == TEXT("Profiles") || Stage == TEXT("AltProfiles");
        const bool bAlternate = Stage == TEXT("AltForms") || Stage == TEXT("AltProfiles");
        const bool bPreview = Stage == TEXT("Forms") || bAlternate || bProfile;
        const bool bMapAttack = Stage == TEXT("AtlasRain") || Stage == TEXT("NightLanes");
        const bool bLoadout = Stage == TEXT("Loadout");
        const bool bHoundfang = Stage == TEXT("Houndfang");
        const bool bRelics = Stage == TEXT("Relics") || Stage == TEXT("Offering");
        const bool bSable = Stage == TEXT("Sable");
        const bool bRest = Stage == TEXT("Rest") || Stage == TEXT("AppealRest");
        const bool bLockedDodge = Stage == TEXT("LockedDodge");
        const bool bGrip = Stage == TEXT("Grip") || Stage == TEXT("GripRight");
        const bool bAppealShot = Stage == TEXT("Appeal") || Stage == TEXT("Verdict") ||
            Stage == TEXT("HaloBreak") || Stage == TEXT("Claws") || Stage == TEXT("WitnessHall") ||
            Stage == TEXT("Wingfall") || Stage == TEXT("ShadowVolley") || Stage == TEXT("Crown") ||
            Stage == TEXT("PenLattice") || Stage == TEXT("CrossAmbush") || Stage == TEXT("Summon") || Stage == TEXT("DefeatLine");
        const bool bApproach = Stage == TEXT("Stairs") || Stage == TEXT("Cells") || Stage == TEXT("Shore");
        const bool bSignature = Stage == TEXT("WingVerdict") || Stage == TEXT("Tribunal") || Stage == TEXT("ShadowSnare");
        const bool bCaption = Stage == TEXT("Subtitle") || Stage == TEXT("Voice") || Stage == TEXT("HurtVoice");
        const bool bNewAttack = Stage == TEXT("Rush") || Stage == TEXT("FlankLeap") || Stage == TEXT("Pursuit");
        const bool bFinaleShot = Stage == TEXT("Yuki") || Stage == TEXT("YukiHiss") || Stage == TEXT("Dawn") ||
            Stage == TEXT("Prism") || Stage == TEXT("Collapse");
        if (bFinaleShot && !FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")))
        {
            UE_LOG(LogTemp, Warning, TEXT("Use -DuelNoSave to stage a finale screenshot without replacing the checkpoint."));
            return;
        }
        if (Stage != TEXT("Ready") && Stage != TEXT("Battle") && Stage != TEXT("Awaken") &&
            Stage != TEXT("Cross") && Stage != TEXT("Forge") && Stage != TEXT("Paw") && Stage != TEXT("Gate") &&
            Stage != TEXT("Pause") && Stage != TEXT("Pillar") && Stage != TEXT("Storm") && Stage != TEXT("Jump") &&
            Stage != TEXT("Warning") && Stage != TEXT("Afterglow") && Stage != TEXT("Swap") && !bPreview &&
            Stage != TEXT("Outer") && Stage != TEXT("Archive") && Stage != TEXT("Evidence") &&
            Stage != TEXT("Blade") && Stage != TEXT("Corner") && Stage != TEXT("Results") && Stage != TEXT("HiroForm") &&
            !bMapAttack && !bLoadout && !bHoundfang && !bRelics && !bSable && !bRest && !bLockedDodge && !bGrip && !bNewAttack && !bApproach && !bSignature &&
            !bCaption && !bAppealShot && !bFinaleShot)
        {
            return;
        }
        float Delay = 2.f;
        if (bPreview)
        {
            Mode->CycleCharacterView();
            if (bAlternate)
            {
                Mode->CycleCharacterView();
            }
            if (bProfile)
            {
                Mode->PlayerFighter->SetActorRotation(FRotator(0, 80, 0));
                Mode->BossFighter->SetActorRotation(FRotator(0, -80, 0));
            }
        }
        else if (Stage == TEXT("Gate"))
        {
            Mode->bPrisonGateOpen = true;
            Mode->PlayerFighter->SetActorLocation(FVector(DuelCourt::GateX - 188, 0, 62));
        }
        else if (bApproach)
        {
            const FVector Position = Stage == TEXT("Shore") ? FVector(-9295, -176, -238) :
                Stage == TEXT("Stairs") ? FVector(-8560, -67, -238) : FVector(-7185, 337, 62);
            Mode->PlayerFighter->SetActorLocation(Position);
            Mode->PlayerFighter->ViewYaw = Stage == TEXT("Cells") ? 55.f : 2.f;
            Mode->PlayerFighter->ViewPitch = Stage == TEXT("Stairs") ? -4.f : -11.f;
        }
        else if (Stage == TEXT("Outer"))
        {
            Mode->bWarrantFound = Mode->bPrisonGateOpen = true;
            Mode->Arena->OpenPrisonGate();
            Mode->PlayerFighter->SetActorLocationAndRotation(FVector(-4810, -1330, 62), FRotator(0, -2, 0));
            Mode->PlayerFighter->ViewYaw = -2.f;
            Mode->PlayerFighter->ViewPitch = -8.f;
        }
        else if (bRest)
        {
            Mode->bWarrantFound = Mode->bPrisonGateOpen = true;
            Mode->Arena->OpenPrisonGate();
            const bool bAppealRest = Stage == TEXT("AppealRest");
            if (bAppealRest)
            {
                Mode->EnterAppeal();
            }
            const FVector Rest = bAppealRest ? DuelCourt::AppealCheckpoint().GetLocation() : DuelCourt::Checkpoint().GetLocation();
            Mode->PlayerFighter->SetActorLocationAndRotation(Rest - FVector(227, 0, 0), FRotator::ZeroRotator);
        }
        else if (bLoadout)
        {
            Mode->PlayerFighter->SetWeapon(EDuelWeapon::Staff);
            Mode->PlayerFighter->SetWeapon(EDuelWeapon::Greatshield, EDuelHand::Left);
            Mode->PlayerFighter->LeftReserve = EDuelWeapon::Hammer;
            Mode->PlayerFighter->ArtSelection = EDuelArt::HiroBind;
            Mode->PlayerFighter->CycleSpell();
        }
        else if (bFinaleShot)
        {
            Mode->EnterAppeal();
            Mode->EnterYukiCourt();
            Mode->ResetEncounter();
            ADuelFighter& Player = *Mode->PlayerFighter;
            ADuelFighter& Boss = *Mode->BossFighter;
            const FVector Origin = DuelCourt::YukiOrigin;
            if (Stage == TEXT("Yuki"))
            {
                Player.SetActorLocation(Origin + FVector(-570, -82, 62));
                Boss.SetActorLocation(Origin + FVector(55, 35, 103));
                Boss.AppealIntro = 0.f;
                Boss.BossPause = 8.f;
                Boss.StartMove(EDuelMove::GlassShelter);
                Delay = 1.57f;
            }
            else if (Stage == TEXT("Prism") || Stage == TEXT("Collapse"))
            {
                Player.SetActorLocation(Origin + FVector(-520, -60, 62));
                Boss.SetActorLocation(Origin + FVector(130, 30, 103));
                for (int32 Phase = 0; Phase < 2; ++Phase)
                {
                    Boss.ReceiveDuelDamage(DuelCourt::BossStageHealth[Phase], Player.GetActorLocation());
                    Boss.TickMove(DuelCourt::Timing(Phase == 0 ? EDuelMove::Awaken : EDuelMove::Eclipse, Phase != 0).Total());
                }
                Mode->Arena->ClearCombatEffects();
                Mode->SubtitleUntil = 0.f;
                Boss.AppealIntro = 0.f;
                Boss.BossPause = 8.f;
                Boss.StartMove(Stage == TEXT("Prism") ? EDuelMove::PrismPursuit : EDuelMove::ArchiveCollapse);
                Delay = Stage == TEXT("Prism") ? 1.26f : 2.22f;
            }
            else if (Stage == TEXT("YukiHiss"))
            {
                Player.SetActorLocation(Origin + FVector(-385, -56, 62));
                Boss.SetActorLocationAndRotation(Origin + FVector(70, 27, 103), FRotator(0, 154, 0));
                Boss.EndCombat();
                // The idle breath peaks at 0.24 s in its 4.8 s period, with a 0.12 s boss offset.
                const double Hiss = FMath::Fmod(World->GetTimeSeconds(), 4.8);
                Delay = FMath::Fmod(.12 - Hiss + 4.8, 4.8);
                if (Delay < .68f)
                {
                    Delay += 4.8f;
                }
            }
            else
            {
                Mode->FinishTrial();
                Mode->Confirm();
                const FVector Position = Origin + FVector(2870, -64, 62);
                const FRotator Facing(0, 32, 0);
                Player.SetActorLocationAndRotation(Position, Facing);
                Player.ViewYaw = Facing.Yaw;
                Player.ViewPitch = -7.f;
                Delay = 2.4f;
            }
        }
        else if (Stage != TEXT("Ready") && Stage != TEXT("Pause"))
        {
            Mode->ResetEncounter();
            Delay = 3.1f;
        }
        if (Stage == TEXT("Archive") || Stage == TEXT("Evidence") || Stage == TEXT("Results"))
        {
            Mode->FighterFell(Mode->BossFighter);
            Mode->EnterArchive();
            if (Stage == TEXT("Evidence") || Stage == TEXT("Results"))
            {
                Mode->PlayerFighter->SetActorLocation(FVector(DuelCourt::ArchiveDeskX - 146, 0, 62));
                Mode->Confirm();
                if (Stage == TEXT("Results"))
                {
                    Mode->Navigate(1);
                    Mode->Confirm();
                }
            }
            else
            {
                Mode->PlayerFighter->SetActorLocation(FVector(3140, -78, 62));
                Mode->PlayerFighter->ViewPitch = -3.f;
            }
            Delay = 2.4f;
        }
        else if (Stage == TEXT("Corner"))
        {
            Mode->PlayerFighter->SetActorLocation(FVector(1673, 1013, 62));
            Mode->BossFighter->SetActorLocation(FVector(1045, 550, 103));
            Mode->BossFighter->BossPause = 8.f;
            Mode->PlayerFighter->SetWeapon(EDuelWeapon::Axe);
            Mode->PlayerFighter->SetWeapon(EDuelWeapon::Hammer, EDuelHand::Left);
            Delay = 2.f;
        }
        else if (Stage == TEXT("HiroForm"))
        {
            Mode->BossFighter->SelectCharacter(EDuelCharacter::Hiro);
            Mode->PlayerFighter->SetActorLocation(FVector(-447, -65, 62));
            Mode->BossFighter->SetActorLocation(FVector(-12, 23, 103));
            Mode->BossFighter->ReceiveDuelDamage(DuelCourt::BossStageHealth[0], Mode->PlayerFighter->GetActorLocation());
            Mode->BossFighter->TickMove(DuelCourt::Timing(EDuelMove::Awaken, false).Total());
            Mode->BossFighter->BossPause = 8.f;
            Delay = 1.6f;
        }
        else if (bAppealShot)
        {
            Mode->Decision = EDuelEnding::CallWitness;
            Mode->EnterAppeal();
            const FVector Origin = DuelCourt::AppealOrigin;
            if (Stage == TEXT("Appeal"))
            {
                Mode->PlayerFighter->SetActorLocation(Origin + FVector(-2990, 70, DuelCourt::AppealLowerZ + 62.f));
                Mode->PlayerFighter->ViewPitch = -4.f;
                Delay = 1.53f;
            }
            else
            {
                Mode->ResetEncounter();
                Mode->PlayerFighter->SetActorLocation(Origin + FVector(-805, -62, 62));
                Mode->BossFighter->SetActorLocation(Origin + FVector(80, 58, 103));
                Mode->BossFighter->AppealIntro = 0.f;
                Mode->BossFighter->BossPause = 9.f;
                if (Stage == TEXT("Verdict") || Stage == TEXT("Summon"))
                {
                    Mode->FighterFell(Mode->BossFighter);
                    Mode->EnterArchive();
                    for (const FVector& Mark : DuelCourt::RitualMarks)
                    {
                        Mode->PlayerFighter->SetActorLocation(Origin + Mark + FVector(-80, 0, 62));
                        Mode->Confirm();
                    }
                    Mode->PlayerFighter->SetActorLocation(Origin + FVector(DuelCourt::VerdictDeskX - 174.f, 0, DuelCourt::VerdictRise + 62.f));
                    if (Stage == TEXT("Verdict"))
                    {
                        Mode->Confirm();
                    }
                    else
                    {
                        Mode->PlayerFighter->ViewPitch = 8.f;
                    }
                    Delay = 1.25f;
                }
                else if (Stage == TEXT("DefeatLine"))
                {
                    Mode->BossFighter->SelectCharacter(EDuelCharacter::Hiro);
                    Mode->PlayerFighter->SelectCharacter(EDuelCharacter::Ema);
                    Mode->FighterFell(Mode->PlayerFighter);
                    Delay = 1.35f;
                }
                else if (Stage == TEXT("HaloBreak"))
                {
                    Mode->BossFighter->SelectCharacter(EDuelCharacter::Ema);
                    Mode->BossFighter->ReceiveDuelDamage(DuelCourt::BossStageHealth[0], Mode->PlayerFighter->GetActorLocation());
                    Mode->BossFighter->TickMove(DuelCourt::Timing(EDuelMove::Awaken, false).Total());
                    Mode->BossFighter->StartMove(EDuelMove::HaloBreak);
                    Delay = 1.13f;
                }
                else if (Stage == TEXT("WitnessHall"))
                {
                    Mode->PlayerFighter->SetActorLocation(Origin + FVector(2510, -62, 62));
                    Mode->PlayerFighter->bLockOn = false;
                    Mode->PlayerFighter->ViewPitch = 9.f;
                    Mode->FighterFell(Mode->BossFighter);
                    Mode->EnterArchive();
                    Mode->PlayerFighter->ViewPitch = 9.f;
                    Delay = 1.65f;
                }
                else if (Stage == TEXT("Crown"))
                {
                    Mode->PlayerFighter->SelectCharacter(EDuelCharacter::Ema);
                    Mode->PlayerFighter->ArtSelection = EDuelArt::EmaAbsolution;
                    Mode->PlayerFighter->SetActorLocation(Origin + FVector(-448, -30, 62));
                    Mode->PlayerFighter->Crimson();
                    Delay = .92f;
                }
                else if (Stage == TEXT("Wingfall") || Stage == TEXT("ShadowVolley") || Stage == TEXT("PenLattice") || Stage == TEXT("CrossAmbush"))
                {
                    const bool bHiro = Stage == TEXT("ShadowVolley") || Stage == TEXT("CrossAmbush");
                    Mode->BossFighter->SelectCharacter(bHiro ? EDuelCharacter::Hiro : EDuelCharacter::Ema);
                    Mode->BossFighter->ReceiveDuelDamage(DuelCourt::BossStageHealth[0], Mode->PlayerFighter->GetActorLocation());
                    Mode->BossFighter->TickMove(DuelCourt::Timing(EDuelMove::Awaken, false).Total());
                    Mode->SubtitleUntil = 0.f;
                    const EDuelMove Attack = Stage == TEXT("CrossAmbush") ? EDuelMove::CrossAmbush :
                        Stage == TEXT("PenLattice") ? EDuelMove::WrittenVerdict : bHiro ? EDuelMove::ShadowVolley : EDuelMove::Wingfall;
                    Mode->BossFighter->StartMove(Attack);
                    Delay = Attack == EDuelMove::CrossAmbush ? 1.36f : Attack == EDuelMove::WrittenVerdict ? 1.15f : bHiro ? 1.24f : 1.02f;
                }
                else
                {
                    Mode->BossFighter->SelectCharacter(EDuelCharacter::Hiro);
                    Mode->PlayerFighter->SetActorLocation(Origin + FVector(-575, -40, 62));
                    Mode->BossFighter->StartMove(EDuelMove::RakingClaws);
                    Delay = 1.19f;
                }
            }
        }
        else if (bNewAttack)
        {
            const bool bHiro = Stage == TEXT("Rush");
            Mode->BossFighter->SelectCharacter(bHiro ? EDuelCharacter::Hiro : EDuelCharacter::Ema);
            Mode->PlayerFighter->SetActorLocation(FVector(-720, -110, 62));
            Mode->BossFighter->SetActorLocation(FVector(265, 95, 103));
            if (Stage == TEXT("Pursuit"))
            {
                Mode->BossFighter->ReceiveDuelDamage(DuelCourt::BossStageHealth[0], Mode->PlayerFighter->GetActorLocation());
                Mode->BossFighter->TickMove(DuelCourt::Timing(EDuelMove::Awaken, false).Total());
                Mode->BossFighter->StartMove(EDuelMove::Pursuit);
                Delay = 1.26f;
            }
            else
            {
                Mode->BossFighter->StartMove(bHiro ? EDuelMove::TalonRush : EDuelMove::FlankLeap);
                Delay = bHiro ? DuelCourt::Timing(EDuelMove::TalonRush, false).Windup - .13f : 1.11f;
            }
        }
        else if (bSignature || bCaption)
        {
            const bool bHiro = Stage == TEXT("ShadowSnare");
            Mode->BossFighter->SelectCharacter(bHiro ? EDuelCharacter::Hiro : EDuelCharacter::Ema);
            Mode->PlayerFighter->SetActorLocation(FVector(-785, -126, 62));
            Mode->BossFighter->SetActorLocation(FVector(170, 76, 103));
            if (Stage == TEXT("Voice"))
            {
                Mode->BossFighter->StartMove(EDuelMove::FlankLeap);
                Delay = 1.50f;
            }
            else if (Stage == TEXT("HurtVoice"))
            {
                Mode->PlayerFighter->SetActorLocation(FVector(-510, -63, 62));
                Mode->BossFighter->BossPause = 7.f;
                FTimerHandle HitTimer;
                World->GetTimerManager().SetTimer(HitTimer, [Mode]
                {
                    Mode->PlayerFighter->ReceiveDuelDamage(70.f, Mode->BossFighter->GetActorLocation());
                }, .82f, false);
                Delay = 1.33f;
            }
            else
            {
                Mode->BossFighter->ReceiveDuelDamage(DuelCourt::BossStageHealth[0], Mode->PlayerFighter->GetActorLocation());
                Mode->BossFighter->TickMove(DuelCourt::Timing(EDuelMove::Awaken, false).Total());
                if (Stage == TEXT("Tribunal"))
                {
                    Mode->BossFighter->ReceiveDuelDamage(DuelCourt::BossStageHealth[1], Mode->PlayerFighter->GetActorLocation());
                    Mode->BossFighter->TickMove(DuelCourt::Timing(EDuelMove::Eclipse, true).Total());
                }
                const EDuelMove Attack = Stage == TEXT("Tribunal") ? EDuelMove::Tribunal :
                    bHiro ? EDuelMove::ShadowSnare : EDuelMove::WingVerdict;
                if (bSignature)
                {
                    Mode->BossFighter->StartMove(Attack);
                    Delay = DuelCourt::Timing(Attack, true).Windup + .14f;
                }
                else
                {
                    Mode->BossFighter->BossPause = 7.f;
                    Delay = .9f;
                }
            }
        }
        else if (bMapAttack)
        {
            const bool bAtlas = Stage == TEXT("AtlasRain");
            Mode->BossFighter->SelectCharacter(bAtlas ? EDuelCharacter::Ema : EDuelCharacter::Hiro);
            Mode->PlayerFighter->SetActorLocation(FVector(-950, -201, 62));
            Mode->BossFighter->SetActorLocation(FVector(-116, 80, 103));
            Mode->BossFighter->ReceiveDuelDamage(DuelCourt::BossStageHealth[0], Mode->PlayerFighter->GetActorLocation());
            Mode->BossFighter->TickMove(DuelCourt::Timing(EDuelMove::Awaken, false).Total());
            const EDuelMove Attack = bAtlas ? EDuelMove::AtlasRain : EDuelMove::NightLanes;
            Mode->BossFighter->StartMove(Attack);
            Delay = DuelCourt::Timing(Attack, true).Windup + .15f;
        }
        else if (Stage == TEXT("Blade"))
        {
            Mode->PlayerFighter->SetActorLocation(FVector(-278, 0, 62));
            Mode->BossFighter->SetActorLocation(FVector(-45, 0, 103));
            Mode->BossFighter->BossPause = 5.f;
            Mode->PlayerFighter->SetWeapon(EDuelWeapon::Greatblade, EDuelHand::Left);
            Mode->PlayerFighter->PressLeftHand();
            Mode->PlayerFighter->ReleaseLeftHand();
            Delay = .49f;
        }
        else if (Stage == TEXT("Awaken"))
        {
            Mode->PlayerFighter->SetActorLocation(FVector(-270, -45, 62));
            Mode->BossFighter->SetActorLocation(FVector(195, 0, 103));
            Mode->BossFighter->ReceiveDuelDamage((DuelCourt::BossStageHealth[0] + 1.f), Mode->PlayerFighter->GetActorLocation());
            Delay = 2.61f;
        }
        else if (Stage == TEXT("Cross"))
        {
            Mode->PlayerFighter->SetActorLocation(FVector(-490, 90, 62));
            Mode->BossFighter->SetActorLocation(FVector(265, -85, 103));
            Mode->BossFighter->ReceiveDuelDamage((DuelCourt::BossStageHealth[0] + 1.f), Mode->PlayerFighter->GetActorLocation());
            Mode->BossFighter->TickMove(DuelCourt::Timing(EDuelMove::Awaken, false).Total());
            Mode->BossFighter->ReceiveDuelDamage((DuelCourt::BossStageHealth[1] + 1.f), Mode->PlayerFighter->GetActorLocation());
            Mode->BossFighter->TickMove(DuelCourt::Timing(EDuelMove::Eclipse, true).Total());
            Mode->BossFighter->StartMove(EDuelMove::Cross);
            Mode->PlayerFighter->SetActorLocation(FVector(-565, 328, 62));
            Delay = 1.15f;
        }
        else if (Stage == TEXT("Forge"))
        {
            Mode->PlayerFighter->SetActorLocation(FVector(-375, -90, 62));
            Mode->BossFighter->SetActorLocation(FVector(-5, 20, 103));
            Mode->BossFighter->BossPause = 5.f;
            Mode->PlayerFighter->SetWeapon(EDuelWeapon::Greatblade);
            Mode->PlayerFighter->Crimson();
            Delay = 1.06f;
        }
        else if (bRelics)
        {
            ADuelFighter& Player = *Mode->PlayerFighter;
            Player.SelectCharacter(EDuelCharacter::Ema);
            Player.SetWeapon(EDuelWeapon::RitualSword);
            Player.SetWeapon(EDuelWeapon::YukiPen, EDuelHand::Left);
            Player.SetActorLocationAndRotation(FVector(-200, 0, 62), FRotator::ZeroRotator);
            Player.bLockOn = false;
            Player.ViewYaw = 178.f;
            Player.ViewPitch = -8.f;
            Mode->BossFighter->BossPause = 10.f;
            Mode->BossFighter->SetActorHiddenInGame(true);
            Delay = .82f;
            if (Stage == TEXT("Offering"))
            {
                Player.Crimson();
                FTimerHandle Swing;
                World->GetTimerManager().SetTimer(Swing, [&Player]() { Player.Cut(); }, 1.42f, false);
                Delay = 1.78f;
            }
        }
        else if (bHoundfang)
        {
            Mode->PlayerFighter->SetActorLocation(FVector(-220, -35, 62));
            Mode->BossFighter->SetActorLocation(FVector(105, 32, 103));
            Mode->BossFighter->BossPause = 5.f;
            Mode->PlayerFighter->SetWeapon(EDuelWeapon::Houndfang);
            Mode->PlayerFighter->Crimson();
            FTimerHandle Followup;
            World->GetTimerManager().SetTimer(Followup, [Mode]()
            {
                Mode->PlayerFighter->Crimson();
            }, .78f, false);
            Delay = 1.39f;
        }
        else if (bSable)
        {
            Mode->PlayerFighter->SetActorLocation(FVector(-425, -95, 62));
            Mode->BossFighter->SetActorLocation(FVector(145, 0, 103));
            Mode->BossFighter->BossPause = 5.f;
            Mode->PlayerFighter->SetWeapon(EDuelWeapon::Staff);
            Mode->PlayerFighter->ActiveSpellSlot = 2;
            Mode->PlayerFighter->Crimson();
            Delay = .53f;
        }
        else if (bGrip)
        {
            ADuelFighter& Player = *Mode->PlayerFighter;
            ADuelFighter& Boss = *Mode->BossFighter;
            const bool bRight = Stage == TEXT("GripRight");
            const EDuelHand ShieldHand = bRight ? EDuelHand::Right : EDuelHand::Left;
            const EDuelHand BladeHand = bRight ? EDuelHand::Left : EDuelHand::Right;
            Player.SetActorLocationAndRotation(FVector(-240, 0, 62), FRotator::ZeroRotator);
            Boss.SetActorLocation(FVector(710, bRight ? -670 : 620, 103));
            Boss.BossPause = 8.f;
            Player.bLockOn = false;
            Player.ViewYaw = bRight ? 159.f : 205.f;
            Player.ViewPitch = -10.f;
            Player.SetWeapon(bRight ? EDuelWeapon::Staff : EDuelWeapon::Houndfang, BladeHand);
            Player.SetWeapon(bRight ? EDuelWeapon::Greatshield : EDuelWeapon::Buckler, ShieldHand);
            FTimerHandle Raise, Lower;
            World->GetTimerManager().SetTimer(Raise, [&Player, ShieldHand]() { Player.PressHand(ShieldHand); }, .64f, false);
            World->GetTimerManager().SetTimer(Lower, [&Player, ShieldHand]() { Player.ReleaseHand(ShieldHand); }, 1.74f, false);
            Delay = 1.23f;
        }
        else if (bLockedDodge)
        {
            ADuelFighter& Player = *Mode->PlayerFighter;
            ADuelFighter& Boss = *Mode->BossFighter;
            Player.SetActorLocationAndRotation(FVector(-370, -45, 62), FRotator::ZeroRotator);
            Boss.SetActorLocationAndRotation(FVector(170, 0, 103), FRotator(0, 180, 0));
            Boss.BossPause = 8.f;
            APlayerController* Controller = CastChecked<APlayerController>(Player.GetController());
            const auto Key = [Controller](FKey Button, EInputEvent Event)
            {
                Controller->InputKey(FInputKeyParams(Button, Event, Event == IE_Pressed ? 1.0 : 0.0));
            };
            FTimerHandle WalkBack, StopBack, ShiftBack, RollBack, ShiftSide, RollSide, StopSide;
            World->GetTimerManager().SetTimer(WalkBack, [Key]() { Key(EKeys::S, IE_Pressed); }, .46f, false);
            World->GetTimerManager().SetTimer(ShiftBack, [Key]() { Key(EKeys::LeftShift, IE_Pressed); }, .94f, false);
            World->GetTimerManager().SetTimer(RollBack, [Key]() { Key(EKeys::LeftShift, IE_Released); }, 1.01f, false);
            World->GetTimerManager().SetTimer(StopBack, [Key]() { Key(EKeys::S, IE_Released); }, 1.09f, false);
            World->GetTimerManager().SetTimer(ShiftSide, [Key]() { Key(EKeys::LeftShift, IE_Pressed); }, 1.90f, false);
            World->GetTimerManager().SetTimer(RollSide, [Key]()
            {
                Key(EKeys::D, IE_Pressed);
                Key(EKeys::LeftShift, IE_Released);
            }, 1.97f, false);
            World->GetTimerManager().SetTimer(StopSide, [Key]() { Key(EKeys::D, IE_Released); }, 2.06f, false);
            Delay = .89f;
        }
        else if (Stage == TEXT("Paw"))
        {
            Mode->PlayerFighter->SetActorLocation(FVector(-325, 40, 62));
            Mode->BossFighter->SetActorLocation(FVector(-19, -35, 103));
            Mode->BossFighter->StartMove(EDuelMove::Paw);
            Delay = .56f;
        }
        else if (Stage == TEXT("Pillar") || Stage == TEXT("Warning") || Stage == TEXT("Afterglow"))
        {
            Mode->PlayerFighter->SetActorLocation(FVector(-545, -70, 62));
            Mode->BossFighter->SetActorLocation(FVector(170, 5, 103));
            Mode->BossFighter->StartMove(EDuelMove::Pillar);
            Delay = Stage == TEXT("Warning") ? .96f : (Stage == TEXT("Afterglow") ? 1.91f : 1.46f);
        }
        else if (Stage == TEXT("Storm"))
        {
            ADuelFighter& Boss = *Mode->BossFighter;
            ADuelFighter& Player = *Mode->PlayerFighter;
            Boss.SelectCharacter(EDuelCharacter::Ema);
            Boss.SetActorLocationAndRotation(FVector(168, 35, 103), FRotator(0, 180, 0));
            Player.SetActorLocation(FVector(-840, -65, 62));
            Boss.BossPhase = 3;
            Boss.FormProgress = 1.f;
            Mode->Arena->SetBattlePhase(3);
            Boss.StartMove(EDuelMove::Storm);
            FTimerHandle Roll;
            World->GetTimerManager().SetTimer(Roll, [&Player]()
            {
                Player.Right(1.f);
                Player.PressShift();
                Player.ReleaseShift();
                Player.Right(0.f);
            }, 1.95f, false);
            Delay = 1.68f;
        }
        else if (Stage == TEXT("Jump"))
        {
            Mode->PlayerFighter->SetActorLocation(FVector(-465, 40, 62));
            Mode->BossFighter->SetActorLocation(FVector(165, 0, 103));
            Mode->BossFighter->BossPause = 5.f;
            Mode->PlayerFighter->StartJump();
            Delay = .30f;
        }
        else if (Stage == TEXT("Swap"))
        {
            Mode->PlayerFighter->SelectCharacter(EDuelCharacter::Ema);
            Mode->BossFighter->SelectCharacter(EDuelCharacter::Hiro);
            Mode->PlayerFighter->SetActorLocation(FVector(-425, 32, 62));
            Mode->BossFighter->SetActorLocation(FVector(90, 0, 103));
            Mode->BossFighter->BossPause = 8.f;
            Delay = 2.2f;
        }

        const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        const FString File = Directory / FString::Printf(TEXT("RainCourt-%s.png"), *Stage);
        FTimerHandle Timer;
        const bool bPause = Stage == TEXT("Pause");
        World->GetTimerManager().SetTimer(Timer, [File, Mode, bPause, bLoadout]()
        {
            if (bPause || bLoadout)
            {
                Mode->TogglePause();
            }
            if (bLoadout)
            {
                Mode->Navigate(3);
                Mode->Confirm();
                Mode->Navigate(ADuelMode::FirstSpellRow + Mode->PlayerFighter->SpellSlot());
            }
            FScreenshotRequest::RequestScreenshot(File, false, false);
        }, Delay, false);
    }
};

static FAutoConsoleCommandWithWorldAndArgs CaptureDuel(
    TEXT("Duel.Capture"), TEXT("Capture a staged frame, including Houndfang, Storm, Outer, Yuki and Dawn."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FDuelVisualQA::Capture));

static FAutoConsoleCommandWithWorldAndArgs ProfileDuelCamera(
    TEXT("Duel.ProfileCamera"), TEXT("Capture rotational frame times in the castle Hub or Flood gallery."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FDuelVisualQA::ProfileCamera));

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelCastleSmoke, "ManosabaDuel.CastleInvestigation",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelCastleSmoke::RunTest(const FString& Parameters)
{
    ADuelMode* Game = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        if (Context.World() && (Game = Context.World()->GetAuthGameMode<ADuelMode>()))
        {
            break;
        }
    }
    if (!TestTrue(TEXT("The investigation is the active route"), Game && Game->UsesCastle() && Game->PlayerFighter) ||
        !FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")))
    {
        return false;
    }
    ADuelFighter& Player = *Game->PlayerFighter;
    const auto Touch = [Game, &Player](const FVector& At)
    {
        Player.SetActorLocation(At + FVector(0, 0, 62));
        Game->Confirm();
    };
    const auto Traversable = [Game, &Player](const FVector& At)
    {
        FHitResult Hit;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(DuelCastleFloor), false, &Player);
        return Game->GetWorld()->LineTraceSingleByChannel(Hit, At + FVector(0, 0, 75),
            At - FVector(0, 0, 68), ECC_Pawn, Query) && Hit.ImpactNormal.Z > .66f &&
            FMath::Abs(Hit.Location.Z - At.Z) < 46.f;
    };
    TestTrue(TEXT("The clock tower and both adjacent approaches have walkable surfaces"),
        Traversable(DuelCastle::Hub) && Traversable(FVector(-2860, -5050, 184)) &&
        Traversable(FVector(2930, -4430, 184)));
    const int32 Order[][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    for (int32 Path = 0; Path < UE_ARRAY_COUNT(Order); ++Path)
    {
        Game->CastleKeys = Game->CastleClues = Game->CastleSeals = Game->CastleShortcuts = Game->CastleWins = 0;
        Game->bEastOpen = Game->bFinalRecord = false;
        Player.SelectCharacter(Path % 2 ? EDuelCharacter::Ema : EDuelCharacter::Hiro);
        Game->ReturnToCastleHub();
        Touch(DuelCastle::IndexStone);
        TestFalse(TEXT("The east door requires all three records"), Game->bEastOpen);
        TestFalse(TEXT("A new character has no exploration spells"), Game->KnowsSpell(EDuelArt::WhiteMantle));
        for (int32 Step = 0; Step < 3; ++Step)
        {
            const int32 Wing = Order[Path][Step];
            const auto& Zone = DuelCastle::Wings[Wing];
            Touch(DuelCastle::Gate(Wing));
            TestFalse(TEXT("An unexamined inner gate remains closed"), Game->GateOpen());
            for (const FVector& Clue : Zone.Clue)
            {
                Touch(Clue);
            }
            if (Step == 0)
            {
                Touch(Zone.Key);
                Touch(DuelCastle::Gate(Wing));
                TestFalse(TEXT("A key for a different hearing cannot open the gate"), Game->GateOpen());
            }
            const int32 KeyWing = (Wing + DuelCastle::WingCount - 1) % DuelCastle::WingCount;
            Touch(DuelCastle::Wings[KeyWing].Key);
            Touch(DuelCastle::Gate(Wing));
            if (!TestTrue(TEXT("A neighboring key and both observations release the gate"),
                Game->GateOpen() && Game->Outcome() == EDuelOutcome::Ready))
            {
                return false;
            }
            Player.SetActorLocation(Zone.Court + FVector(DuelCourt::GateCrossX + 30.f, 0, 62));
            Game->Tick(0.f);
            TestTrue(TEXT("Crossing an open threshold starts the selected fight"), Game->Outcome() == EDuelOutcome::Fighting &&
                Game->CastleDuel == Wing);
            if (Wing == 0)
            {
                TestTrue(TEXT("The flooded hearing uses the selected protagonist's phantom"),
                    Game->BossFighter->SelectedCharacter() == Player.SelectedCharacter());
            }
            Game->FighterFell(Game->BossFighter);
            if (Path == 0 && Step == 0)
            {
                Game->ReturnToCastleHub();
                Touch(DuelCastle::Gate(Wing));
                TestFalse(TEXT("A defeated phantom stays defeated before collecting its plaque"), Game->GateOpen());
            }
            Touch(DuelCastle::Record(Wing));
            TestEqual(TEXT("The recovered inscription survives leaving the chamber"), Game->CastleMarks(), Step + 1);
            Game->ReturnToCastleHub();
        }
        Touch(DuelCastle::IndexStone);
        TestTrue(TEXT("Every order can finish reconstructing the testimony"), Game->bEastOpen);
        TestTrue(TEXT("All five additional spells are unlocked by completing the investigation"),
            Game->KnowsSpell(EDuelArt::LanternMote) && Game->KnowsSpell(EDuelArt::Blackwell) && Game->KnowsSpell(EDuelArt::SaltArc) &&
            Game->KnowsSpell(EDuelArt::WhiteMantle) && Game->KnowsSpell(EDuelArt::WitnessStep));
        const FVector Final = DuelCourt::YukiOrigin;
        Touch(Final + FVector(DuelCourt::GateX - 125, 0, 0));
        Player.SetActorLocation(Final + FVector(DuelCourt::GateCrossX + 30.f, 0, 62));
        Game->Tick(0.f);
        TestTrue(TEXT("The final threshold loads Yuki after the evidence is accepted"), Game->Outcome() == EDuelOutcome::Fighting &&
            Game->BossFighter->SelectedCharacter() == EDuelCharacter::Yuki);
        Game->FighterFell(Game->BossFighter);
        Player.SetActorLocation(Final + FVector(DuelCourt::YukiGardenX + 30.f, 0, 62));
        Game->Tick(0.f);
        TestTrue(TEXT("The final verdict is read before entering the dawn garden"),
            Game->Outcome() == EDuelOutcome::Victory && !Game->bFinalRecord);
        Touch(DuelCastle::FinalRecord());
        Player.SetActorLocation(Final + FVector(DuelCourt::YukiGardenX + 30.f, 0, 62));
        Game->Tick(0.f);
        TestTrue(TEXT("Reading the final page lets the player enter the dawn garden"), Game->Outcome() == EDuelOutcome::Epilogue);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelCastleWalk, "ManosabaDuel.CastleInteriorWalk",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelCastleWalk::RunTest(const FString& Parameters)
{
    ADuelMode* Game = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        if (Context.World() && (Game = Context.World()->GetAuthGameMode<ADuelMode>()))
        {
            break;
        }
    }
    if (!TestTrue(TEXT("The walk uses the normal castle route"), Game && Game->UsesCastle() && Game->GetPlayerFighter()) ||
        !FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")))
    {
        return false;
    }
    TWeakObjectPtr<ADuelMode> World(Game);
    const TArray<FVector> route{
        DuelCastle::Hub + FVector(0, 0, 62),
        DuelCastle::Hub + FVector(-900, -43, 62),
        FVector(-2855, -5050, 246), FVector(-5700, -5449, -232), FVector(-8560, -5450, -238),
        FVector(-8760, -7590, -252), FVector(-6235, -9725, -283), FVector(-4110, -10450, -258),
        FVector(-2865, -9370, -258), FVector(-1600, -9260, -258), FVector(910, -9610, -258),
        FVector(1550, -9265, -258), FVector(3820, -8560, -258), FVector(6090, -8190, 18),
        FVector(6600, -6500, 266), FVector(6210, -4670, 267), FVector(4490, -4260, 264),
        FVector(2930, -4430, 246), DuelCastle::Hub + FVector(1300, 220, 62),
        DuelCastle::Hub + FVector(250, 60, 62)
    };
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World, route, Index = 0, Due = 0.0]() mutable
    {
        ADuelMode* Mode = World.Get();
        if (!Mode || !Mode->GetPlayerFighter())
        {
            AddError(TEXT("The castle unloaded during a traversal"));
            return true;
        }
        ADuelFighter* Fighter = const_cast<ADuelFighter*>(Mode->GetPlayerFighter());
        const FVector At = Fighter->GetActorLocation();
        const FVector Goal = route[Index];
        const FVector Off = Goal - At;
        const double Now = Mode->GetWorld()->GetTimeSeconds();
        if (Due == 0.0)
        {
            Due = Now + 5.0 + Off.Size2D() / 247.0;
        }
        if (Off.Size2D() <= 83.0 && FMath::Abs(Off.Z) < 133.f)
        {
            FCollisionQueryParams Trace(SCENE_QUERY_STAT(CastleCovered), false, Fighter);
            const FVector Eye = At + FVector(0, 0, 100);
            int32 ClosedRays = 0;
            FHitResult Hit;
            for (int32 Ray = 0; Ray < 12; ++Ray)
            {
                const FVector Axis = FRotator(0.f, Ray * 30.f, 0.f).Vector();
                ClosedRays += Mode->GetWorld()->LineTraceSingleByChannel(Hit, Eye,
                    Eye + Axis * 2520.f + FVector(0, 0, 2520), ECC_Camera, Trace) ? 1 : 0;
            }
            TestEqual(FString::Printf(TEXT("Route point %d blocks exterior sightlines from every direction"), Index),
                ClosedRays, 12);
            Due = 0.0;
            return ++Index == route.Num();
        }
        if (Now > Due)
        {
            AddError(FString::Printf(TEXT("Interior walk stopped at point %d at %s with target %s"),
                Index, *At.ToString(), *Goal.ToString()));
            return true;
        }
        Fighter->AddMovementInput(Off.GetSafeNormal2D(),
            FMath::Clamp(Off.Size2D() / 190.0, .35, 1.0));
        return false;
    }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelCastleFinalWalk, "ManosabaDuel.CastleFinalWalk",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelCastleFinalWalk::RunTest(const FString& Parameters)
{
    ADuelMode* Trial = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        if (Context.World() && (Trial = Context.World()->GetAuthGameMode<ADuelMode>()))
        {
            break;
        }
    }
    if (!TestTrue(TEXT("The finale walk uses an isolated castle"), Trial && Trial->UsesCastle() &&
        Trial->PlayerFighter && FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave"))))
    {
        return false;
    }
    const FVector Final = DuelCourt::YukiOrigin;
    ADuelFighter& Player = *Trial->PlayerFighter;
    Trial->CastleKeys = Trial->CastleSeals = Trial->CastleShortcuts = Trial->CastleWins = DuelCastle::Whole;
    Trial->CastleClues = 63;
    Trial->bEastOpen = Trial->bFinalRecord = false;
    Trial->ReturnToCastleHub();
    Player.SetActorLocation(DuelCastle::IndexStone + FVector(-148, 0, 62));
    Trial->Confirm();
    if (!TestTrue(TEXT("The completed testimonies open the eastern stair"), Trial->bEastOpen))
    {
        return false;
    }
    const FCollisionQueryParams Walls(SCENE_QUERY_STAT(CastleDoorProbes), false, &Player);
    const auto Blocked = [&](const FVector& At, const FVector& Axis, float Cross)
    {
        FHitResult Hit;
        return Trial->GetWorld()->SweepSingleByChannel(Hit, At - Axis * Cross, At + Axis * Cross,
            FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(34.f, 60.f), Walls);
    };
    for (const float Range : {-562.f, 0.f, 570.f})
    {
        TestTrue(TEXT("The final fog and its shoulders seal the open gallery"), Blocked(
            Final + FVector(DuelCourt::GateX, Range, 62), FVector::ForwardVector, 210.f));
    }
    for (const float Range : {-1200.f, 0.f, 1200.f})
    {
        TestTrue(TEXT("The garden stays sealed while the last page is missing"), Blocked(
            Final + FVector(DuelCourt::YukiExitX, Range, 62), FVector::ForwardVector, 185.f));
    }
    struct FStep
    {
        FVector Feet;
        const TCHAR* Action = nullptr;
    };
    const TArray<FStep> Route{
        {{860, -5260, 184}}, {{1085, -5260, 184}}, {{1220, -4790, 184}}, {{820, -4325, 184}},
        {{1180, -3710, 184}}, {{2090, -3050, 184}}, {{4270, -1870, 205}}, {{6310, -2140, 225}},
        {{8760, -2930, 319}}, {{11450, -3120, 370}}, {{13790, -2480, 423}}, {{15140, -2250, 466}},
        {{15140, -1725, 466}}, {{15140, -1340, 466}}, {{15140, -870, 466}}, {{15140, -233, 466}},
        {{15170, 0, 466}}, {{15470, 0, 466}}, {{16635, 0, 696}}, {{17800, 0, 926}}, {{18180, 0, 926}},
        {{18573, 0, 926}, TEXT("Open the fog")}, {{19065, 0, 926}, TEXT("Win the final duel")},
        {{20490, -250, 926}}, {{22400, -260, 926}}, {{22635, -350, 926}, TEXT("Read the returned page")},
        {{22870, -120, 926}}, {{23045, 0, 926}}, {{23430, 0, 926}, TEXT("Enter the garden")},
        {{23985, 80, 926}, TEXT("Speak with the companion")}
    };
    const TWeakObjectPtr<ADuelMode> World(Trial);
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World, Route, Index = 0, Due = 0.0]() mutable
    {
        ADuelMode* Mode = World.Get();
        if (!Mode)
        {
            AddError(TEXT("The finale world unloaded"));
            return true;
        }
        ADuelFighter* Fighter = Mode->PlayerFighter;
        const FStep& Step = Route[Index];
        const FVector At = Fighter->GetActorLocation();
        const FVector Offset = Step.Feet + FVector(0, 0, 62) - At;
        const double Now = Mode->GetWorld()->GetTimeSeconds();
        if (Due == 0.0)
        {
            Due = Now + 5.5 + Offset.Size2D() / 200.0;
        }
        if (Offset.Size2D() > 63.0 || FMath::Abs(Offset.Z) >= 116.f)
        {
            if (Now > Due)
            {
                FHitResult Obstacle;
                const FVector Probe = At + Offset.GetSafeNormal2D() * 132.f;
                const FCollisionQueryParams Query(SCENE_QUERY_STAT(FinaleWalkObstacle), false, Fighter);
                Mode->GetWorld()->SweepSingleByChannel(Obstacle, At, Probe, FQuat::Identity, ECC_Pawn,
                    FCollisionShape::MakeCapsule(34.f, 60.f), Query);
                AddError(FString::Printf(TEXT("Finale walk stopped at step %d near %s toward %s; obstacle %s at %s"),
                    Index, *At.ToString(), *Step.Feet.ToString(), *GetNameSafe(Obstacle.GetComponent()), *Obstacle.ImpactPoint.ToString()));
                return true;
            }
            Fighter->AddMovementInput(Offset.GetSafeNormal2D(), FMath::Clamp(Offset.Size2D() / 150.0, .34, 1.0));
            return false;
        }
        if (Step.Action)
        {
            const FStringView Action(Step.Action);
            if (Action == TEXT("Win the final duel"))
            {
                if (!TestTrue(Step.Action, Mode->Outcome() == EDuelOutcome::Fighting && Mode->InYukiCourt()))
                {
                    AddError(FString::Printf(TEXT("Final threshold at %s, mode %d, gate %d, Yuki court %d"),
                        *At.ToString(), static_cast<int32>(Mode->Outcome()), Mode->GateOpen(), Mode->InYukiCourt()));
                    return true;
                }
                Mode->FighterFell(Mode->BossFighter);
                Mode->BossFighter->SetActorHiddenInGame(true);
            }
            else if (Action == TEXT("Enter the garden"))
            {
                if (!TestTrue(Step.Action, Mode->Outcome() == EDuelOutcome::Epilogue))
                {
                    return true;
                }
            }
            else
            {
                Mode->Confirm();
                if (Action == TEXT("Speak with the companion") && Mode->ReadingArchive())
                {
                    for (int32 Page = 0; Page < 3; ++Page)
                    {
                        Mode->Confirm();
                    }
                }
                const bool bActionSucceeded = Action == TEXT("Open the fog") ? Mode->GateOpen() :
                    Action == TEXT("Read the returned page") ? Mode->bFinalRecord : Mode->bCompanionFollowing;
                if (!TestTrue(Step.Action, bActionSucceeded))
                {
                    return true;
                }
            }
        }
        const FCollisionQueryParams Roof(SCENE_QUERY_STAT(FinaleWalkRoof), false, Fighter);
        FHitResult Ceiling;
        TestTrue(FString::Printf(TEXT("Finale walk point %d has a ceiling"), Index),
            Mode->GetWorld()->LineTraceSingleByChannel(Ceiling, At + FVector(0, 0, 80),
                At + FVector(0, 0, 3100), ECC_Camera, Roof));
        Due = 0.0;
        return ++Index == Route.Num();
    }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelMatchSmoke, "ManosabaDuel.MatchFlow",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelMatchSmoke::RunTest(const FString& Parameters)
{
    ADuelMode* Mode = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        if (UWorld* World = Context.World())
        {
            Mode = World->GetAuthGameMode<ADuelMode>();
            if (Mode)
            {
                break;
            }
        }
    }
    if (!TestNotNull(TEXT("Rain Court game mode"), Mode) ||
        !TestNotNull(TEXT("PlayerFighter"), Mode->PlayerFighter.Get()) ||
        !TestNotNull(TEXT("BossFighter"), Mode->BossFighter.Get()) ||
        !TestNotNull(TEXT("Arena"), Mode->Arena.Get()))
    {
        return false;
    }
    ADuelFighter* PlayerFighter = Mode->PlayerFighter.Get();
    ADuelFighter* BossFighter = Mode->BossFighter.Get();
    const auto Near = [](float Actual, float Expected)
    {
        return FMath::IsNearlyEqual(Actual, Expected, .01f);
    };
    const auto BossDamage = [](float Raw) { return Raw * (160.f / 145.f) * 1.12f; };
    TestTrue(TEXT("Health and stamina start at the new caps"), Near(PlayerFighter->Health, 160.f) &&
        Near(PlayerFighter->Stamina, 130.f));
    const auto Position = [PlayerFighter, BossFighter](float HiroX, float EmaX)
    {
        PlayerFighter->SetActorLocationAndRotation(FVector(HiroX, 0, 62), FRotator::ZeroRotator);
        BossFighter->SetActorLocationAndRotation(FVector(EmaX, 0, 103), FRotator(0, 180, 0));
    };
    const auto EnterGate = [PlayerFighter]()
    {
        PlayerFighter->SetActorLocation(FVector(DuelCourt::GateX - 125, 0, 62));
        PlayerFighter->ConfirmDuel();
        ADuelMode* Mode = PlayerFighter->GetWorld()->GetAuthGameMode<ADuelMode>();
        if (Mode->GateOpen())
        {
            PlayerFighter->SetActorLocation(FVector(DuelCourt::GateCrossX + 5.f, 0, 62));
            Mode->Tick(0.f);
        }
    };

    TestTrue(TEXT("The investigation opens three meters below the prison hall"), Mode->Outcome() == EDuelOutcome::Ready &&
        PlayerFighter->GetActorLocation().Equals(DuelCourt::LowerCheckpoint().GetLocation(), 1.f));
    PlayerFighter->ConfirmDuel();
    TestTrue(TEXT("Confirmation at the checkpoint cannot start combat"), Mode->Outcome() == EDuelOutcome::Ready);
    PlayerFighter->PressRightHand();
    PlayerFighter->ReleaseRightHand();
    TestTrue(TEXT("The outside checkpoint allows regular attacks"), PlayerFighter->Move == EDuelMove::Cut);
    PlayerFighter->TickMove(.71f);
    PlayerFighter->Crimson();
    PlayerFighter->TickMove(.70f);
    TestTrue(TEXT("Outside casting spends MP and creates a visible projectile"),
        PlayerFighter->Move == EDuelMove::Crimson && Near(PlayerFighter->Focus, DuelCourt::MaxFocus - 23.f) && !Mode->Arena->Orbs.IsEmpty());
    PlayerFighter->TickMove(.82f);
    PlayerFighter->RestAtCheckpoint();
    const auto FindFloor = [&](float X, float Y)
    {
        FHitResult Hit;
        FCollisionQueryParams Params;
        Params.AddIgnoredActor(PlayerFighter);
        const bool bFloor = Mode->GetWorld()->LineTraceSingleByChannel(Hit, FVector(X, Y, 150), FVector(X, Y, -500), ECC_Pawn, Params);
        return bFloor ? Hit.ImpactPoint.Z : -999.0;
    };
    TestTrue(TEXT("The gatehouse floor and the stair collision form a continuous three-meter rise"),
        FMath::IsNearlyEqual(FindFloor(-9190.f, 0.f), -300.0, 1.0) &&
        FMath::IsNearlyEqual(FindFloor(-7905.f, 0.f), -150.0, 1.0) &&
        FMath::IsNearlyZero(FindFloor(-7290.f, 0.f), 1.0));
    PlayerFighter->SetActorLocation(FVector(DuelCourt::PrisonGateX - 145.f, 0, 62));
    PlayerFighter->ConfirmDuel();
    TestTrue(TEXT("The portcullis stays locked until the warrant is taken"),
        Mode->NearPrisonGate() && !Mode->PrisonGateOpen() && !Mode->HasWarrant());
    FHitResult Block;
    PlayerFighter->AddActorWorldOffset(FVector(280, 0, 0), true, &Block);
    TestTrue(TEXT("The portcullis physically blocks the main path"), Block.bBlockingHit &&
        PlayerFighter->GetActorLocation().X < DuelCourt::PrisonGateX);
    EnterGate();
    TestTrue(TEXT("The fog does not start combat while the previous gate is locked"), Mode->Outcome() == EDuelOutcome::Ready);
    PlayerFighter->SetActorLocation(DuelCourt::Warrant + FVector(-112, -43, 62));
    PlayerFighter->ConfirmDuel();
    TestTrue(TEXT("The warrant is collected at the side alcove"), Mode->HasWarrant() && !Mode->NearWarrant());
    PlayerFighter->SetActorLocation(FVector(DuelCourt::PrisonGateX - 142.f, 0, 62));
    PlayerFighter->ConfirmDuel();
    Mode->Arena->Tick(.55f);
    FHitResult Pass;
    PlayerFighter->AddActorWorldOffset(FVector(270, 0, 0), true, &Pass);
    TestTrue(TEXT("The warrant raises the iron gate and allows passage"),
        Mode->PrisonGateOpen() && !Pass.bBlockingHit && PlayerFighter->GetActorLocation().X > DuelCourt::PrisonGateX);
    PlayerFighter->SetActorLocation(FVector(-5150, 850, 62));
    PlayerFighter->ClampToCourt();
    TestTrue(TEXT("The western embankment is walkable beyond the old approach"),
        PlayerFighter->GetActorLocation().Equals(FVector(-5150, 850, 62), 1.f));
    for (float Side : {-1.f, 1.f})
    {
        const FVector Gallery(-5120, Side * (DuelCourt::OuterGalleryHalfWidth - 68.f), 62);
        PlayerFighter->SetActorLocation(Gallery);
        PlayerFighter->ClampToCourt();
        FHitResult FloorHit;
        const FVector Ground = Gallery - FVector(0, 0, 100);
        FCollisionQueryParams FloorQuery(SCENE_QUERY_STAT(OuterGalleryFloor), false, PlayerFighter);
        TestTrue(TEXT("Both new exterior galleries are traversable and have supporting ground"),
            PlayerFighter->GetActorLocation().Equals(Gallery, 1.f) &&
            Mode->GetWorld()->LineTraceSingleByChannel(FloorHit, Gallery, Ground, ECC_Pawn, FloorQuery) &&
            FMath::IsNearlyZero(FloorHit.ImpactPoint.Z, 3.f));
    }
    PlayerFighter->SetActorLocation(DuelCourt::Checkpoint().GetLocation());
    PlayerFighter->ConfirmDuel();
    TestTrue(TEXT("The fog antechamber can be registered as the new checkpoint"), Mode->AntechamberReached());
    if (!TestNotNull(TEXT("PlayerFighter uses the bound character mesh"), PlayerFighter->CharacterMesh.Get()) ||
        !TestNotNull(TEXT("BossFighter uses the bound character mesh"), BossFighter->CharacterMesh.Get()))
    {
        return false;
    }
    const USkinnedAsset* HiroStanding = PlayerFighter->CharacterMesh->GetSkinnedAsset();
    const USkinnedAsset* EmaQuadruped = BossFighter->CharacterMesh->GetSkinnedAsset();
    const auto HasCurrentBindPose = [](const ADuelFighter* Fighter)
    {
        const UPoseableMeshComponent* Mesh = Fighter->CharacterMesh.Get();
        const TArray<FTransform>& Reference = Mesh->GetSkinnedAsset()->GetRefSkeleton().GetRefBonePose();
        const TArray<FTransform>& Local = Mesh->GetBoneSpaceTransforms();
        if (Local.Num() != Reference.Num())
        {
            return false;
        }
        for (int32 I = 0; I < Local.Num(); ++I)
        {
            if (!Local[I].GetTranslation().Equals(Reference[I].GetTranslation(), .01))
            {
                return false;
            }
        }
        return true;
    };
    PlayerFighter->PauseDuel();
    const auto ConfirmPauseOption = [&](EDuelPause Option)
    {
        for (int32 I = 0; I < Mode->PauseOptionCount() && Mode->PauseOption(Mode->MenuSelection) != Option; ++I)
            PlayerFighter->MenuForward();
        if (Mode->MenuPage() != EDuelMenuPage::Pause || Mode->PauseOption(Mode->MenuSelection) != Option) return false;
        PlayerFighter->ConfirmDuel();
        return true;
    };
    TestTrue(TEXT("Pause includes the equipment page"), ConfirmPauseOption(EDuelPause::Loadout));
    TestTrue(TEXT("The S menu action reaches the loadout"), Mode->ShowingLoadout() && Mode->LoadoutChoice() == 0);
    const auto ChooseEquipmentByKeys = [&](int32 Row, int32 Value)
    {
        for (int32 I = 0; I < ADuelMode::LoadoutRows && Mode->LoadoutChoice() != Row; ++I)
            PlayerFighter->MenuForward();
        if (Mode->MenuPage() != EDuelMenuPage::Equipment || !Mode->LoadoutChoices().Contains(Value)) return false;
        PlayerFighter->ConfirmDuel();
        const int32 Count = Mode->LoadoutChoices().Num();
        for (int32 I = 0; I < Count && Mode->LoadoutCandidate != Value; ++I) PlayerFighter->CycleRightHand();
        if (Mode->LoadoutCandidate != Value) return false;
        PlayerFighter->ConfirmDuel();
        return Mode->EquippedLoadoutValue(Row) == Value;
    };
    TestTrue(TEXT("The main hand can equip the hammer through keyboard selection"),
        ChooseEquipmentByKeys(0, static_cast<int32>(EDuelWeapon::Hammer)));
    TestTrue(TEXT("The offhand can equip the fire poker through keyboard selection"),
        ChooseEquipmentByKeys(1, static_cast<int32>(EDuelWeapon::FirePoker)));
    const int32 OriginalFlasks = PlayerFighter->HealthFlaskAllocation;
    TestTrue(TEXT("The flask row can set its allocation through keyboard selection"),
        ChooseEquipmentByKeys(ADuelMode::FlaskLoadoutRow, OriginalFlasks - 1));
    TestTrue(TEXT("The main weapon slots change independently of magic and flask allocation"),
        PlayerFighter->EquippedWeapon() == EDuelWeapon::Hammer &&
        PlayerFighter->EquippedWeapon(EDuelHand::Left) == EDuelWeapon::FirePoker && Mode->LoadoutChoice() == ADuelMode::FlaskLoadoutRow);
    TestTrue(TEXT("The first prepared slot can equip Witch Rain through keyboard selection"),
        ChooseEquipmentByKeys(ADuelMode::FirstSpellRow, static_cast<int32>(EDuelArt::WitchRain)));
    TestTrue(TEXT("Changing a prepared spell swaps its prior slot and preserves three distinct spells"),
        PlayerFighter->SpellAt(0) == EDuelArt::WitchRain && PlayerFighter->SpellAt(1) == EDuelArt::IceLance &&
        PlayerFighter->SpellAt(2) == EDuelArt::SableBlade && PlayerFighter->SelectedSpell() == EDuelArt::WitchRain);
    PlayerFighter->CycleSpell();
    TestEqual(TEXT("Spell cycling stays out of the pause menu"), PlayerFighter->SpellSlot(), 0);
    PlayerFighter->CycleLeftHand();
    PlayerFighter->HealthFlaskAllocation = DuelCourt::DefaultHealthFlasks;
    PlayerFighter->PauseDuel();
    TestTrue(TEXT("Escape backs out of the loadout and keeps the pause menu open"),
        !Mode->ShowingLoadout() && UGameplayStatics::IsGamePaused(Mode));
    PlayerFighter->PauseDuel();
    PlayerFighter->SetWeapon(EDuelWeapon::Rapier);
    PlayerFighter->SetWeapon(EDuelWeapon::Buckler, EDuelHand::Left);
    PlayerFighter->ArtSelection = EDuelArt::Default;

    PlayerFighter->PauseDuel();
    TestTrue(TEXT("Pause offers the player selection on this route"), ConfirmPauseOption(EDuelPause::Player));
    TestTrue(TEXT("Pause offers the boss selection on this route"), ConfirmPauseOption(EDuelPause::Boss));
    TestTrue(TEXT("Pause config independently equips Ema standing and Hiro quadruped"),
        PlayerFighter->SelectedCharacter() == EDuelCharacter::Ema && BossFighter->SelectedCharacter() == EDuelCharacter::Hiro &&
        PlayerFighter->CharacterMesh->GetSkinnedAsset()->GetFName() == FName(TEXT("SK_EmaStand")) &&
        BossFighter->CharacterMesh->GetSkinnedAsset()->GetFName() == FName(TEXT("SK_HiroQuad")));
    TestTrue(TEXT("Switching both roles reloads the new models' joint locations"),
        HasCurrentBindPose(PlayerFighter) && HasCurrentBindPose(BossFighter));
    ConfirmPauseOption(EDuelPause::Boss);
    ConfirmPauseOption(EDuelPause::Player);
    PlayerFighter->PauseDuel();
    PlayerFighter->CycleCharacterView();
    TestEqual(TEXT("First character view displays the combat forms"), Mode->CharacterView(), 1);
    const double Facing = PlayerFighter->GetActorRotation().Yaw;
    PlayerFighter->Right(1.f);
    PlayerFighter->Tick(.2f);
    PlayerFighter->Right(0.f);
    TestTrue(TEXT("Directional input turns the characters in preview"),
        FMath::IsNearlyEqual(FMath::FindDeltaAngleDegrees(Facing, PlayerFighter->GetActorRotation().Yaw), 14.4, .01));
    PlayerFighter->CycleCharacterView();
    TestTrue(TEXT("Second character view loads both alternate models"), Mode->CharacterView() == 2 &&
        PlayerFighter->CharacterMesh->GetSkinnedAsset() != HiroStanding &&
        BossFighter->CharacterMesh->GetSkinnedAsset() != EmaQuadruped);
    TestTrue(TEXT("Alternate forms pose against their own skeletons"),
        HasCurrentBindPose(PlayerFighter) && HasCurrentBindPose(BossFighter));
    PlayerFighter->EquipGreatblade();
    TestTrue(TEXT("The display poses suppress equipment changes"), PlayerFighter->EquippedWeapon() == EDuelWeapon::Rapier);
    PlayerFighter->EquipRapier();
    PlayerFighter->Cut();
    TestTrue(TEXT("The model preview cannot accidentally attack"), PlayerFighter->Move == EDuelMove::Idle);
    PlayerFighter->ConfirmDuel();
    EnterGate();
    TestTrue(TEXT("Start input begins the match"), Mode->Outcome() == EDuelOutcome::Fighting);
    TestTrue(TEXT("Beginning combat restores the default forms"), Mode->CharacterView() == 0 &&
        PlayerFighter->CharacterMesh->GetSkinnedAsset() == HiroStanding &&
        BossFighter->CharacterMesh->GetSkinnedAsset() == EmaQuadruped);
    PlayerFighter->PauseDuel();
    TestTrue(TEXT("Pause stops world simulation"), UGameplayStatics::IsGamePaused(Mode));
    PlayerFighter->PauseDuel();
    TestFalse(TEXT("Pause input resumes the match"), UGameplayStatics::IsGamePaused(Mode));

    for (const FVector& Edge : {FVector(-1672, 0, 62), FVector(1672, 0, 62), FVector(0, -1044, 62), FVector(0, 1044, 62)})
    {
        PlayerFighter->SetActorLocation(Edge);
        BossFighter->SetActorLocation(FVector(0, 0, 103));
        BossFighter->PoseFigure(0.f);
        PlayerFighter->ViewYaw = (BossFighter->GetActorLocation() - Edge).Rotation().Yaw;
        PlayerFighter->TickCamera(.2f);
        PlayerFighter->CameraBoom->TickComponent(.2f, LEVELTICK_All, nullptr);
        FMinimalViewInfo View;
        PlayerFighter->CalcCamera(.2f, View);
        TestTrue(TEXT("The arena walls leave room behind the player and keep the camera inside"),
            FMath::Abs(View.Location.X) <= DuelCourt::CameraLimitX && FMath::Abs(View.Location.Y) <= DuelCourt::CameraLimitY &&
            FVector::Dist2D(View.Location, Edge) > 210.f);
        TestTrue(TEXT("The lock-on mark is at the center of the actual camera view"),
            FVector::DotProduct(View.Rotation.Vector(), (BossFighter->LockPoint() - View.Location).GetSafeNormal()) > .9999);
    }
    Mode->ResetEncounter();

    Position(-460.f, 0.f);
    BossFighter->PoseFigure(0.f);
    constexpr float ViewFrame = 1.f / 60.f;
    const auto AdvanceView = [&]()
    {
        PlayerFighter->TickCamera(ViewFrame);
        PlayerFighter->CameraBoom->TickComponent(ViewFrame, LEVELTICK_All, nullptr);
    };
    // The prior corner leaves a decaying offset. Allow 1 mm at the 2 s sample.
    for (int32 Frame = 0; Frame < 120; ++Frame) AdvanceView();
    const FVector InitialFocus = BossFighter->LockPoint();
    const FVector BeforeMove = PlayerFighter->CameraFocus;
    const double InitialHeight = PlayerFighter->CameraBoom->TargetOffset.Z;
    BossFighter->Figure->SetRelativeRotation(FRotator(26, 37, 24));
    TestTrue(TEXT("An animated pose does not shake the lock point"),
        BossFighter->LockPoint().Equals(InitialFocus, .01));

    BossFighter->AddActorWorldOffset(FVector(0, 100, 0));
    const FVector MovedFocus = BossFighter->LockPoint();
    PlayerFighter->CameraImpulse = 1.2f;
    AdvanceView();
    const FVector FirstFocus = PlayerFighter->CameraFocus;
    const double Followed = FVector::Distance(FirstFocus, BeforeMove);
    const double FirstError = FVector::Distance(FirstFocus, MovedFocus);
    const double FirstHeight = PlayerFighter->CameraBoom->TargetOffset.Z;
    TestTrue(TEXT("In open court the settled and following camera pivots are within 1 mm of 205 cm"),
        FMath::IsNearlyEqual(InitialHeight, 205.0, .1) && FMath::IsNearlyEqual(FirstHeight, 205.0, .1));
    TestTrue(TEXT("One camera frame follows smoothly toward the boss's 100 cm sidestep"),
        BeforeMove.Equals(InitialFocus, .1) && FMath::IsNearlyEqual(FVector::Distance(InitialFocus, MovedFocus), 100.0, .1) &&
        Followed > 1.0 && Followed < 35.0 && FirstError < FVector::Distance(BeforeMove, MovedFocus));

    FMinimalViewInfo StableView;
    PlayerFighter->CalcCamera(ViewFrame, StableView);
    const double AimDot = FVector::DotProduct(StableView.Rotation.Vector(), (FirstFocus - StableView.Location).GetSafeNormal());
    TestTrue(TEXT("The locked view centers its filtered point and keeps a level horizon through hit impulse"),
        Near(StableView.Rotation.Roll, 0.f) && AimDot > .9999);
    for (int32 Frame = 0; Frame < 60; ++Frame) AdvanceView();
    const double SettledError = FVector::Distance(PlayerFighter->CameraFocus, MovedFocus);
    TestTrue(TEXT("The filtered camera focus converges after the boss stops moving"), SettledError < 1.0);
    AddInfo(FString::Printf(TEXT("MatchFlow camera: pivot_cm=%.5f->%.5f first_step_cm=%.3f first_error_cm=%.3f settled_error_cm=%.3f roll_deg=%.5f aim_dot=%.6f"),
        InitialHeight, FirstHeight, Followed, FirstError, SettledError, StableView.Rotation.Roll, AimDot));
    Mode->ResetEncounter();

    TArray<FInputAxisKeyMapping> PitchKeys;
    GetDefault<UInputSettings>()->GetAxisMappingByName(TEXT("LookPitch"), PitchKeys);
    const FInputAxisKeyMapping* MousePitch = PitchKeys.FindByPredicate([](const FInputAxisKeyMapping& Key)
    {
        return Key.Key == EKeys::MouseY;
    });
    const float BeforePitch = PlayerFighter->ViewPitch;
    PlayerFighter->bLockOn = false;
    if (Mode->PointerClosedFrame == GFrameCounter)
    {
        PlayerFighter->LookPitch(MousePitch ? MousePitch->Scale : 0.f);
        TestEqual(TEXT("Closing a menu consumes pending pointer movement for that frame"), PlayerFighter->ViewPitch, BeforePitch);
        // This synchronous replay shares a frame across all key events.
        Mode->PointerClosedFrame = GFrameCounter - 1;
    }
    PlayerFighter->LookPitch(MousePitch ? MousePitch->Scale : 0.f);
    TestTrue(TEXT("Windows mouse-up input raises the free camera"), MousePitch && PlayerFighter->ViewPitch > BeforePitch);
    Mode->ResetEncounter();

    PlayerFighter->SetWeapon(EDuelWeapon::Rapier, EDuelHand::Left);
    PlayerFighter->PressLeftHand();
    PlayerFighter->ReleaseLeftHand();
    TestTrue(TEXT("The right mouse button releases a left-hand light attack"),
        PlayerFighter->Move == EDuelMove::Cut && PlayerFighter->MoveHand == EDuelHand::Left);
    Mode->ResetEncounter();
    PlayerFighter->PressRightHand();
    PlayerFighter->HandPresses[0].Since -= .33;
    PlayerFighter->TickHands();
    PlayerFighter->ReleaseRightHand();
    TestTrue(TEXT("Holding the blade input commits one heavy attack"),
        PlayerFighter->Move == EDuelMove::Heavy && PlayerFighter->QueuedMove == EDuelMove::Idle);
    Mode->ResetEncounter();
    PlayerFighter->SetWeapon(EDuelWeapon::Greatshield);
    PlayerFighter->PressRightHand();
    TestTrue(TEXT("The main-hand greatshield guards while the left mouse button is held"),
        PlayerFighter->IsGuarding(EDuelHand::Right));
    PlayerFighter->PoseFigure(.18f);
    const auto ShieldFace = [PlayerFighter](const USceneComponent& Grip)
    {
        return PlayerFighter->GetActorTransform().InverseTransformPosition(
            Grip.GetComponentTransform().TransformPosition(FVector(6.f, 0.f, 9.f)));
    };
    const FVector RightShield = ShieldFace(*PlayerFighter->Weapon);
    TestTrue(TEXT("The right-hand shield face is held in front of the chest"),
        RightShield.X > 25.f && RightShield.Z > 6.f &&
        FVector::DotProduct(PlayerFighter->GetActorForwardVector(), PlayerFighter->Weapon->GetForwardVector()) > .95f);
    PlayerFighter->ReleaseRightHand();
    TestTrue(TEXT("Shield release lowers the shield without attacking"),
        !PlayerFighter->IsGuarding() && PlayerFighter->Move == EDuelMove::Idle);
    PlayerFighter->Crimson();
    TestTrue(TEXT("The weapon art uses the left weapon when the main hand holds a shield"),
        PlayerFighter->Move == EDuelMove::Crimson && PlayerFighter->MoveHand == EDuelHand::Left);
    PlayerFighter->SetWeapon(EDuelWeapon::Rapier);
    PlayerFighter->SetWeapon(EDuelWeapon::Buckler, EDuelHand::Left);
    Mode->ResetEncounter();

    Position(0.f, 460.f);
    PlayerFighter->SetActorRotation(FRotator(0, 180, 0));
    PlayerFighter->PressLeftHand();
    PlayerFighter->PoseFigure(.18f);
    const FVector Shield = ShieldFace(*PlayerFighter->LeftWeapon);
    TestTrue(TEXT("Pressing locked guard raises the left shield without snapping the actor"),
        PlayerFighter->GetActorForwardVector().Equals(-FVector::ForwardVector, .01) && Shield.X > 25.f &&
        Shield.Z > 6.f && Shield.Z < 66.f &&
        FVector::DotProduct(PlayerFighter->GetActorForwardVector(), PlayerFighter->LeftWeapon->GetForwardVector()) > .95f);
    PlayerFighter->Forward(-1.f);
    for (int32 Frame = 0; Frame < 20; ++Frame)
    {
        PlayerFighter->ConsumeMovementInputVector();
        PlayerFighter->TickPlayer(.02f);
    }
    TestTrue(TEXT("A raised locked shield faces the boss while backward movement is preserved"),
        FVector::DotProduct(PlayerFighter->GetActorForwardVector(),
            (BossFighter->GetActorLocation() - PlayerFighter->GetActorLocation()).GetSafeNormal2D()) > .999f &&
        PlayerFighter->GetPendingMovementInputVector().X < -.5f);
    PlayerFighter->ConsumeMovementInputVector();
    PlayerFighter->Forward(0.f);
    PlayerFighter->ReleaseLeftHand();
    for (EDuelWeapon Gear : {EDuelWeapon::Rapier, EDuelWeapon::Greatblade, EDuelWeapon::Houndfang, EDuelWeapon::Staff})
    {
        PlayerFighter->SetWeapon(Gear);
        PlayerFighter->SetWeapon(Gear, EDuelHand::Left);
        PlayerFighter->PoseFigure(0.f);
        TestTrue(TEXT("Both resting grips angle upward without leaving the hands"),
            PlayerFighter->Weapon->GetForwardVector().Z > .69f && PlayerFighter->LeftWeapon->GetForwardVector().Z > .69f &&
            PlayerFighter->Weapon->GetComponentLocation().Equals(
                PlayerFighter->CharacterMesh->GetBoneLocationByName(TEXT("DuelGrip"), EBoneSpaces::WorldSpace), .1f) &&
            PlayerFighter->LeftWeapon->GetComponentLocation().Equals(
                PlayerFighter->CharacterMesh->GetBoneLocationByName(TEXT("DuelFlask"), EBoneSpaces::WorldSpace), .1f));
    }
    PlayerFighter->SetWeapon(EDuelWeapon::Rapier);
    PlayerFighter->SetWeapon(EDuelWeapon::Buckler, EDuelHand::Left);
    Mode->ResetEncounter();

    PlayerFighter->StartJump();
    PlayerFighter->CheckJumpInput(.016f);
    TestTrue(TEXT("Jump produces a physical rise"), PlayerFighter->GetCharacterMovement()->IsFalling() &&
        PlayerFighter->GetVelocity().Z > 300.f);
    PlayerFighter->ReleaseJump();
    const FVector Axes[] = {FVector(1, 0, 0), FVector(-1, 0, 0), FVector(0, 1, 0), FVector(0, -1, 0)};
    for (const FVector& Axis : Axes)
    {
        Mode->ResetEncounter();
        PlayerFighter->Forward(Axis.X);
        PlayerFighter->Right(Axis.Y);
        PlayerFighter->TickPlayer(.10f);
        PlayerFighter->Forward(0.f);
        PlayerFighter->Right(0.f);
        PlayerFighter->TickPlayer(.10f);
        FMinimalViewInfo LockedView;
        PlayerFighter->TickCamera(.2f);
        PlayerFighter->CalcCamera(.2f, LockedView);
        TestTrue(TEXT("Locked movement faces the input even after release while the camera aims at the boss"),
            PlayerFighter->IsLocked() && PlayerFighter->GetActorForwardVector().Equals(Axis, .01) &&
            FVector::DotProduct(LockedView.Rotation.Vector(),
                (BossFighter->LockPoint() - LockedView.Location).GetSafeNormal()) > .9999);
        PlayerFighter->ViewYaw = 0.f;
        PlayerFighter->SetActorRotation(FRotator(0, (-Axis).Rotation().Yaw, 0));
        PlayerFighter->Forward(Axis.X);
        PlayerFighter->Right(Axis.Y);
        PlayerFighter->PressShift();
        PlayerFighter->ReleaseShift();
        const FVector BeforeRoll = PlayerFighter->GetActorLocation();
        PlayerFighter->TickMove(.009f);
        TestTrue(TEXT("The initial roll brace has no translation or invulnerability"),
            !PlayerFighter->InRollWindow() && PlayerFighter->GetActorLocation().Equals(BeforeRoll, .01));
        PlayerFighter->TickMove(.038f);
        TestFalse(TEXT("The roll is vulnerable at 47 ms"), PlayerFighter->InRollWindow());
        PlayerFighter->TickMove(.002f);
        TestTrue(TEXT("The roll becomes invulnerable at about 48 ms"), PlayerFighter->InRollWindow());
        PlayerFighter->TickMove(.105f);
        TestTrue(TEXT("The roll preserves its 438 ms movement curve"),
            Near(FVector::Dist2D(PlayerFighter->GetActorLocation(), BeforeRoll),
                345.f * FMath::SmoothStep(0.f, 1.f, (.154f - DuelCourt::RollMotionDelay) / DuelCourt::RollMotionTime)));
        PlayerFighter->PoseFigure(0.f);
        const FVector RolledUp = PlayerFighter->Figure->GetRelativeRotation().RotateVector(FVector::UpVector);
        TestTrue(TEXT("A directional tap turns the actor and tumbles forward in local space"),
            PlayerFighter->Move == EDuelMove::Roll && RolledUp.X > .55 &&
            PlayerFighter->GetActorForwardVector().Equals(Axis, .01) && PlayerFighter->ActionDirection.Equals(Axis, .01));
        PlayerFighter->TickMove(.321f);
        TestTrue(TEXT("The roll completes 345 cm of travel by 475 ms"),
            Near(FVector::Dist2D(PlayerFighter->GetActorLocation(), BeforeRoll), 345.f));
        PlayerFighter->TickMove(.011f);
        TestTrue(TEXT("Roll recovery returns control by 486 ms"), PlayerFighter->Move == EDuelMove::Idle);
    }
    Mode->ResetEncounter();
    APlayerController* Controller = CastChecked<APlayerController>(PlayerFighter->GetController());
    UPlayerInput* Input = Controller->PlayerInput;
    const TArray<UInputComponent*> InputStack{PlayerFighter->InputComponent};
    const auto Key = [Controller](FKey Button, EInputEvent Event)
    {
        Controller->InputKey(FInputKeyParams(Button, Event, Event == IE_Pressed ? 1.0 : 0.0));
    };
    Key(EKeys::LeftShift, IE_Pressed);
    Input->ProcessInputStack(InputStack, .016f, false);
    TestTrue(TEXT("Left Shift arms a pending press without starting a move"),
        PlayerFighter->ShiftPressedAt >= 0.0 && PlayerFighter->Move == EDuelMove::Idle);
    PlayerFighter->ShiftPressedAt = Mode->GetWorld()->GetTimeSeconds() - .16;
    Key(EKeys::S, IE_Pressed);
    Key(EKeys::D, IE_Pressed);
    Key(EKeys::LeftShift, IE_Released);
    Input->ProcessInputStack(InputStack, .016f, false);
    const FVector Diagonal = FVector(-1, 1, 0).GetSafeNormal();
    TestTrue(TEXT("A 160 ms Shift tap with a release-frame diagonal immediately starts a directional roll"),
        PlayerFighter->Move == EDuelMove::Roll && Near(PlayerFighter->MoveAge, 0.f) &&
        PlayerFighter->ActionDirection.Equals(Diagonal, .01) && PlayerFighter->GetActorForwardVector().Equals(Diagonal, .01));
    Key(EKeys::S, IE_Released);
    Key(EKeys::D, IE_Released);
    Input->ProcessInputStack(InputStack, .016f, false);
    Mode->ResetEncounter();
    Key(EKeys::RightShift, IE_Pressed);
    Input->ProcessInputStack(InputStack, .016f, false);
    TestTrue(TEXT("Right Shift arms the same pending press"),
        PlayerFighter->ShiftPressedAt >= 0.0 && PlayerFighter->Move == EDuelMove::Idle);
    PlayerFighter->ShiftPressedAt = Mode->GetWorld()->GetTimeSeconds() - .16;
    Key(EKeys::RightShift, IE_Released);
    Input->ProcessInputStack(InputStack, .016f, false);
    TestTrue(TEXT("A neutral 160 ms right Shift tap backsteps immediately"),
        PlayerFighter->Move == EDuelMove::Backstep && Near(PlayerFighter->MoveAge, 0.f));
    Mode->ResetEncounter();
    const float HalfRollCost = DuelCourt::Timing(EDuelMove::Roll, false).Cost * .5f;
    PlayerFighter->Stamina = HalfRollCost;
    PlayerFighter->Forward(-1.f);
    PlayerFighter->PressShift();
    PlayerFighter->ReleaseShift();
    TestTrue(TEXT("Exactly half the roll cost is insufficient"), PlayerFighter->Move == EDuelMove::Idle &&
        Near(PlayerFighter->Stamina, HalfRollCost));
    PlayerFighter->Stamina = 10.f;
    PlayerFighter->Cut();
    PlayerFighter->RequestMove(EDuelMove::Heavy);
    PlayerFighter->Crimson();
    TestTrue(TEXT("Attacks still require the entire stamina cost"), PlayerFighter->Move == EDuelMove::Idle &&
        Near(PlayerFighter->Stamina, 10.f));
    PlayerFighter->PressShift();
    PlayerFighter->ReleaseShift();
    TestTrue(TEXT("A roll above half its cost may spend the last stamina"), PlayerFighter->Move == EDuelMove::Roll &&
        Near(PlayerFighter->Stamina, 0.f));
    PlayerFighter->TickMove(.50f);
    PlayerFighter->PressShift();
    PlayerFighter->ReleaseShift();
    TestTrue(TEXT("An exhausted player cannot roll again"), PlayerFighter->Move == EDuelMove::Idle);

    Mode->ResetEncounter();
    PlayerFighter->Stamina = DuelCourt::Timing(EDuelMove::Roll, false).Cost * 1.55f;
    PlayerFighter->Forward(-1.f);
    PlayerFighter->PressShift();
    PlayerFighter->ReleaseShift();
    PlayerFighter->TickMove(.25f);
    PlayerFighter->Forward(0.f);
    PlayerFighter->Right(1.f);
    PlayerFighter->PressShift();
    PlayerFighter->ReleaseShift();
    PlayerFighter->TickMove(.18f);
    TestTrue(TEXT("The buffered second roll starts during recovery, turns and can spend the last stamina"),
        PlayerFighter->Move == EDuelMove::Roll && Near(PlayerFighter->MoveAge, .43f - DuelCourt::RollChainAt) &&
        PlayerFighter->ActionDirection.Equals(FVector::RightVector, .01) && Near(PlayerFighter->Stamina, 0.f));
    PlayerFighter->Right(0.f);
    Mode->ResetEncounter();
    PlayerFighter->Forward(1.f);
    PlayerFighter->PressShift();
    PlayerFighter->ShiftPressedAt = Mode->GetWorld()->GetTimeSeconds() - .16;
    PlayerFighter->TickPlayer(.016f);
    TestTrue(TEXT("Directional Shift before the hold threshold walks with no stamina drain"),
        Near(PlayerFighter->GetCharacterMovement()->MaxWalkSpeed, 455.f) && Near(PlayerFighter->Stamina, DuelCourt::MaxStamina));
    PlayerFighter->ShiftPressedAt = Mode->GetWorld()->GetTimeSeconds() - .25;
    PlayerFighter->TickPlayer(.10f);
    TestTrue(TEXT("Holding Shift with a direction commits sprint"),
        Near(PlayerFighter->GetCharacterMovement()->MaxWalkSpeed, 680.f) && PlayerFighter->Stamina < DuelCourt::MaxStamina);
    PlayerFighter->Forward(0.f);
    PlayerFighter->TickPlayer(.016f);
    PlayerFighter->ReleaseShift();
    PlayerFighter->TickPlayer(.50f);
    TestTrue(TEXT("Releasing a committed sprint after movement stops leaves no dodge waiting"),
        Near(PlayerFighter->GetCharacterMovement()->MaxWalkSpeed, 455.f) && PlayerFighter->Move == EDuelMove::Idle &&
        PlayerFighter->QueuedMove == EDuelMove::Idle);
    Mode->ResetEncounter();

    for (const EDuelHand Hand : {EDuelHand::Left, EDuelHand::Right})
    {
        UCharacterMovementComponent* Movement = PlayerFighter->GetCharacterMovement();
        PlayerFighter->Forward(1.f);
        PlayerFighter->PressShift();
        PlayerFighter->ShiftPressedAt = Mode->GetWorld()->GetTimeSeconds() - .25;
        Movement->Velocity = FVector(680.f, 0, 0);
        PlayerFighter->ConsumeMovementInputVector();
        const EDuelWeapon Next = PlayerFighter->EquippedWeapon(Hand) == EDuelWeapon::Greatblade ?
            EDuelWeapon::Rapier : EDuelWeapon::Greatblade;
        PlayerFighter->RequestWeapon(Next, Hand);
        TestTrue(TEXT("Starting either hand swap retains sprint momentum"),
            PlayerFighter->Move == EDuelMove::Equip && Near(Movement->Velocity.Size2D(), 680.f));
        PlayerFighter->TickPlayer(.20f);
        TestTrue(TEXT("Both weapon slots swap while sprint input continues"),
            PlayerFighter->EquippedWeapon(Hand) == Next && Near(Movement->MaxWalkSpeed, 680.f) &&
            !PlayerFighter->ConsumeMovementInputVector().IsNearlyZero());
        Mode->ResetEncounter();
    }
    const EDuelItem PreviouslySelectedItem = PlayerFighter->Item;
    for (const EDuelItem Item : {EDuelItem::Flask, EDuelItem::FocusFlask})
    {
        UCharacterMovementComponent* Movement = PlayerFighter->GetCharacterMovement();
        const bool bStartedSprinting = Item == EDuelItem::FocusFlask;
        PlayerFighter->Item = Item;
        PlayerFighter->Health = DuelCourt::PlayerMaxHealth * .4f;
        PlayerFighter->Focus = PlayerFighter->FocusMax() * .4f;
        PlayerFighter->Forward(1.f);
        if (bStartedSprinting)
        {
            PlayerFighter->PressShift();
            PlayerFighter->ShiftPressedAt = Mode->GetWorld()->GetTimeSeconds() - .25;
            PlayerFighter->TickPlayer(0.f);
        }
        PlayerFighter->Stamina = 40.f;
        PlayerFighter->RecoveryDelay = .10f;
        const float InitialSpeed = bStartedSprinting ? 680.f : 455.f;
        Movement->Velocity = FVector(InitialSpeed, 0, 0);
        PlayerFighter->ConsumeMovementInputVector();
        const FVector BeforeFlask = PlayerFighter->GetActorLocation();
        const int32 Doses = PlayerFighter->ItemCount();
        PlayerFighter->Flask();
        TestTrue(TEXT("Both flasks set the slower cap at startup and brake without zeroing momentum"),
            PlayerFighter->Move == EDuelMove::Flask && Near(Movement->MaxWalkSpeed, 250.f) &&
            Near(Movement->Velocity.Size2D(), InitialSpeed));
        constexpr float DrinkFrame = 1.f / 60.f;
        for (int32 Frame = 0; Frame < 18; ++Frame)
        {
            PlayerFighter->TickPlayer(DrinkFrame);
            Movement->TickComponent(DrinkFrame, LEVELTICK_All, nullptr);
        }
        TestTrue(TEXT("Both flasks allow visibly slower movement and ordinary stamina recovery with Shift held or released"),
            Near(PlayerFighter->Stamina, 45.6f) && Near(Movement->MaxWalkSpeed, 250.f) &&
            Movement->Velocity.Size2D() > 0.0 && Movement->Velocity.Size2D() <= 253.0 &&
            FVector::DistSquared2D(PlayerFighter->GetActorLocation(), BeforeFlask) > FMath::Square(40.0));
        PlayerFighter->TickPlayer(.20f);
        const float Restored = Item == EDuelItem::Flask ? PlayerFighter->Health / DuelCourt::PlayerMaxHealth :
            PlayerFighter->Focus / PlayerFighter->FocusMax();
        TestTrue(TEXT("Drinking while moving restores the expected resource and consumes exactly one dose"),
            Near(Restored, .9f) && PlayerFighter->ItemCount() == Doses - 1);
        PlayerFighter->TickPlayer(.50f);
        TestTrue(TEXT("The flask speed cap ends with the drink"),
            PlayerFighter->Move == EDuelMove::Idle && Near(Movement->MaxWalkSpeed, 455.f));
        Mode->ResetEncounter();
    }
    PlayerFighter->Item = PreviouslySelectedItem;
    PlayerFighter->SetWeapon(EDuelWeapon::Rapier);
    PlayerFighter->SetWeapon(EDuelWeapon::Buckler, EDuelHand::Left);
    Mode->ResetEncounter();

    Position(0.f, 180.f);
    PlayerFighter->Cut();
    const float BeforeCut = DuelCourt::Timing(EDuelMove::Cut, false).Windup - .01f;
    PlayerFighter->TickMove(BeforeCut);
    TestTrue(TEXT("Blade windup deals no damage"), Near(BossFighter->Health, DuelCourt::BossMaxHealth));
    PlayerFighter->PoseFigure(0.f);
    TestTrue(TEXT("The animated right sleeve follows the weapon motion"),
        FMath::Abs(PlayerFighter->CharacterMesh->GetBoneRotationByName(TEXT("DuelLimb3"), EBoneSpaces::ComponentSpace).Pitch) > 12.f);
    TestTrue(TEXT("The hilt tracks the right hand"), PlayerFighter->Weapon->GetComponentLocation().Equals(
        PlayerFighter->CharacterMesh->GetBoneLocationByName(TEXT("DuelGrip"), EBoneSpaces::WorldSpace), .1f));
    PlayerFighter->TickMove(.30f - BeforeCut);
    TestTrue(TEXT("The first blade hit lands once and produces a contact hold"),
        Near(BossFighter->Health, DuelCourt::BossMaxHealth - 62.f) && Near(PlayerFighter->Hitstop, .030f));
    const float HitAge = PlayerFighter->MoveAge;
    PlayerFighter->Tick(.015f);
    TestTrue(TEXT("The contact hold suspends the striking pose"), Near(PlayerFighter->MoveAge, HitAge));
    PlayerFighter->Cut();
    PlayerFighter->TickMove(.40f);
    TestTrue(TEXT("Second blade input is buffered"), PlayerFighter->Move == EDuelMove::Backcut);
    PlayerFighter->TickMove(.34f);
    TestTrue(TEXT("Backcut lands and spends stamina"),
        Near(BossFighter->Health, DuelCourt::BossMaxHealth - 138.f) &&
        Near(PlayerFighter->Stamina, DuelCourt::MaxStamina - 31.f * DuelCourt::StaminaCostScale));

    Mode->ResetEncounter();
    Position(0.f, 186.f);
    PlayerFighter->SetWeapon(EDuelWeapon::Greatblade, EDuelHand::Left);
    PlayerFighter->PressLeftHand();
    PlayerFighter->ReleaseLeftHand();
    PlayerFighter->TickMove(.62f);
    TestTrue(TEXT("A left-hand sweep deals one hit across a skipped active window"),
        PlayerFighter->MoveHand == EDuelHand::Left && Near(BossFighter->Health, DuelCourt::BossMaxHealth - 95.f));
    Mode->ResetEncounter();
    Position(0.f, 545.f);
    PlayerFighter->Cut();
    PlayerFighter->TickMove(.65f);
    TestTrue(TEXT("A cut beyond the painted blade's path misses"),
        Near(BossFighter->Health, DuelCourt::BossMaxHealth));
    PlayerFighter->SetWeapon(EDuelWeapon::Buckler, EDuelHand::Left);

    Mode->ResetEncounter();
    PlayerFighter->PressShift();
    PlayerFighter->ReleaseShift();
    TestTrue(TEXT("A neutral tap backsteps without tumbling"), PlayerFighter->Move == EDuelMove::Backstep &&
        PlayerFighter->ActionDirection.Equals(-PlayerFighter->GetActorForwardVector(), .01));
    PlayerFighter->TickMove(.11f);
    TestFalse(TEXT("A hit inside the dodge window is avoided"), PlayerFighter->ReceiveDuelDamage(32.f, BossFighter->GetActorLocation()));
    PlayerFighter->TickMove(.23f);
    TestTrue(TEXT("A hit after the dodge window lands"), PlayerFighter->ReceiveDuelDamage(32.f, BossFighter->GetActorLocation()));
    PlayerFighter->TickMove(.40f);
    PlayerFighter->Flask();
    PlayerFighter->TickMove(DuelCourt::Timing(EDuelMove::Flask, false).Windup - .01f);
    TestEqual(TEXT("No flask is spent before drinking"), PlayerFighter->FlaskCount(), DuelCourt::DefaultHealthFlasks);
    PlayerFighter->TickMove(.02f);
    TestTrue(TEXT("A completed drink restores health and spends one flask"),
        Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth) && PlayerFighter->FlaskCount() == DuelCourt::DefaultHealthFlasks - 1);

    Mode->ResetEncounter();
    PlayerFighter->Health = 30.f;
    PlayerFighter->Flask();
    PlayerFighter->TickMove(.51f);
    TestTrue(TEXT("A red flask restores half of maximum health"),
        Near(PlayerFighter->Health, 110.f) && PlayerFighter->FlaskCount() == DuelCourt::DefaultHealthFlasks - 1);

    Mode->ResetEncounter();
    PlayerFighter->Stamina = 31.f;
    PlayerFighter->Focus = 27.f;
    PlayerFighter->MenuDown();
    PlayerFighter->Flask();
    PlayerFighter->TickMove(.51f);
    TestTrue(TEXT("The blue flask restores half of maximum MP and leaves stamina unchanged"),
        Near(PlayerFighter->Stamina, 31.f) && Near(PlayerFighter->Focus, 82.f) &&
        PlayerFighter->ItemCount() == DuelCourt::DefaultFocusFlasks - 1 && Mode->Outcome() == EDuelOutcome::Fighting);
    PlayerFighter->TickMove(.60f);
    PlayerFighter->SetWeapon(EDuelWeapon::Staff);
    PlayerFighter->Focus = 27.f;
    PlayerFighter->Flask();
    PlayerFighter->TickMove(.51f);
    TestTrue(TEXT("A blue flask restores the same 55 MP while holding a staff"),
        Near(PlayerFighter->Focus, 82.f) && PlayerFighter->ItemCount() == DuelCourt::DefaultFocusFlasks - 2);
    PlayerFighter->SetWeapon(EDuelWeapon::Rapier);
    PlayerFighter->Item = EDuelItem::Flask;
    Mode->ResetEncounter();
    Position(0.f, 180.f);
    PlayerFighter->PressLeftHand();
    PlayerFighter->HandPresses[static_cast<uint8>(EDuelHand::Left)].Since -= .20;
    PlayerFighter->ReceiveDuelDamage(40.f, BossFighter->GetActorLocation(), true);
    TestTrue(TEXT("The left-hand buckler absorbs a frontal attack using stamina"),
        Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth - BossDamage(6.8f)) &&
        Near(PlayerFighter->Stamina, DuelCourt::MaxStamina - 24.f * DuelCourt::StaminaCostScale));
    PlayerFighter->ReleaseLeftHand();

    Mode->ResetEncounter();
    Position(-450.f, 0.f);
    PlayerFighter->Crimson();
    PlayerFighter->TickMove(.69f);
    Mode->Arena->Tick(.31f);
    TestTrue(TEXT("Crimson orb reaches BossFighter"),
        Near(BossFighter->Health, DuelCourt::BossMaxHealth - 134.f) &&
        Near(PlayerFighter->Stamina, DuelCourt::MaxStamina - 18.f * DuelCourt::StaminaCostScale) &&
        Near(PlayerFighter->Focus, DuelCourt::MaxFocus - 23.f));
    Mode->ResetEncounter();
    Position(0.f, 1580.f);
    PlayerFighter->Crimson();
    PlayerFighter->TickMove(.69f);
    Mode->Arena->Tick(1.08f);
    TestTrue(TEXT("The longer crimson orb reaches a target past its original flight limit"),
        Near(BossFighter->Health, DuelCourt::BossMaxHealth - 134.f));
    Mode->ResetEncounter();
    PlayerFighter->Focus = 22.f;
    PlayerFighter->Crimson();
    TestTrue(TEXT("The new weapon art cost must be available before casting"),
        PlayerFighter->Move == EDuelMove::Idle && Near(PlayerFighter->Focus, 22.f));

    PlayerFighter->SetWeapon(EDuelWeapon::Staff);
    PlayerFighter->SetWeapon(EDuelWeapon::Staff, EDuelHand::Left);
    TestTrue(TEXT("Equipping staves preserves current and maximum MP"),
        Near(PlayerFighter->FocusMax(), 110.f) && Near(PlayerFighter->Focus, 22.f));
    PlayerFighter->SetWeapon(EDuelWeapon::Greatshield);
    PlayerFighter->Focus = 8.f;
    PlayerFighter->Crimson();
    TestTrue(TEXT("The left staff spends eight MP for Ice Lance at the exact cost threshold"),
        PlayerFighter->Move == EDuelMove::Crimson && PlayerFighter->MoveHand == EDuelHand::Left &&
        PlayerFighter->MoveArt == EDuelArt::IceLance && Near(PlayerFighter->Focus, 0.f));
    PlayerFighter->SetWeapon(EDuelWeapon::Rapier);
    PlayerFighter->SetWeapon(EDuelWeapon::Buckler, EDuelHand::Left);
    TestTrue(TEXT("A staff in the offhand leaves a non-staff weapon art at full cost"), DuelCourt::FocusCost(EDuelArt::Orb) == 23);

    const auto HasActionKey = [](FName Action, const FKey& Key)
    {
        TArray<FInputActionKeyMapping> Keys;
        GetDefault<UInputSettings>()->GetActionMappingByName(Action, Keys);
        return Keys.ContainsByPredicate([&](const FInputActionKeyMapping& Mapping) { return Mapping.Key == Key; });
    };
    TestTrue(TEXT("Scroll up and 4 are mapped to the prepared-spell action"),
        HasActionKey(TEXT("CycleSpell"), EKeys::MouseScrollUp) && HasActionKey(TEXT("CycleSpell"), EKeys::Four));
    TestTrue(TEXT("Scroll down and 2 are mapped to the item action"),
        HasActionKey(TEXT("CycleItem"), EKeys::MouseScrollDown) && HasActionKey(TEXT("CycleItem"), EKeys::Two));
    Mode->ResetEncounter();
    PlayerFighter->SetWeapon(EDuelWeapon::Staff);
    PlayerFighter->RestAtCheckpoint();
    TestTrue(TEXT("The default first staff slot selects Ice Lance"),
        PlayerFighter->SpellSlot() == 0 && PlayerFighter->ActiveArt() == EDuelArt::IceLance);
    PlayerFighter->CycleSpell();
    PlayerFighter->Crimson();
    constexpr float Cost = 16.f;
    TestTrue(TEXT("Staff casting uses the cycled spell and spends its MP cost"),
        PlayerFighter->SpellSlot() == 1 && PlayerFighter->Move == EDuelMove::Crimson &&
        PlayerFighter->MoveArt == EDuelArt::WitchRain && Near(PlayerFighter->Focus, PlayerFighter->FocusMax() - Cost));
    PlayerFighter->CycleSpell();
    TestTrue(TEXT("Cycling during an active spell only selects the next cast"),
        PlayerFighter->SpellSlot() == 2 && PlayerFighter->ActiveArt() == EDuelArt::SableBlade &&
        PlayerFighter->MoveArt == EDuelArt::WitchRain && Near(PlayerFighter->Focus, PlayerFighter->FocusMax() - Cost));
    PlayerFighter->CycleSpell();
    TestTrue(TEXT("Cycling wraps back to the first prepared spell"),
        PlayerFighter->SpellSlot() == 0 && PlayerFighter->ActiveArt() == EDuelArt::IceLance);
    PlayerFighter->SetWeapon(EDuelWeapon::Rapier);

    Mode->ResetEncounter();
    Position(0.f, 420.f);
    PlayerFighter->Right(1.f);
    PlayerFighter->Cut();
    PlayerFighter->TickMove(.17f);
    TestTrue(TEXT("A lateral light attack steps diagonally while facing the locked boss"),
        PlayerFighter->GetActorLocation().Y > 0.f && PlayerFighter->GetActorForwardVector().Equals(FVector::ForwardVector, .01));
    PlayerFighter->TickMove(.22f);
    PlayerFighter->PressShift();
    PlayerFighter->ReleaseShift();
    PlayerFighter->TickMove(.072f);
    TestTrue(TEXT("Buffered evasions can leave light-attack recovery by 462 ms"),
        PlayerFighter->Move == EDuelMove::Roll && PlayerFighter->ActionDirection.Equals(FVector::RightVector, .01));
    PlayerFighter->Right(0.f);

    Mode->ResetEncounter();
    PlayerFighter->SelectCharacter(EDuelCharacter::Ema);
    PlayerFighter->SetWeapon(EDuelWeapon::YukiPen);
    Position(0.f, 505.f);
    PlayerFighter->Crimson();
    PlayerFighter->TickMove(.47f);
    const int32 PenFirstDamage = BossFighter->RecentDamage();
    TestEqual(TEXT("The steel pen deals 72 damage on the first stroke"), PenFirstDamage, 72);
    BossFighter->AddActorWorldOffset(FVector(290, 115, 0));
    PlayerFighter->TickMove(.32f);
    const int32 PenFollowupDamage = BossFighter->RecentDamage() - PenFirstDamage;
    TestEqual(TEXT("The tracked second pen stroke adds 108 damage"), PenFollowupDamage, 108);
    AddInfo(FString::Printf(TEXT("Steel pen strike damage: first=%d, tracked follow-up=%d"), PenFirstDamage, PenFollowupDamage));
    Mode->ResetEncounter();
    Position(0.f, 880.f);
    PlayerFighter->Crimson();
    BossFighter->AddActorWorldOffset(FVector(0, 120, 0));
    PlayerFighter->TickMove(.81f);
    TestTrue(TEXT("A missed first stroke does not retarget the second"), BossFighter->RecentDamage() == 0);
    PlayerFighter->SetWeapon(EDuelWeapon::Rapier);
    PlayerFighter->SelectCharacter(EDuelCharacter::Hiro);

    for (const EDuelHand Hand : {EDuelHand::Right, EDuelHand::Left})
    {
        Mode->ResetEncounter();
        PlayerFighter->SetWeapon(EDuelWeapon::RitualSword, Hand);
        Position(0.f, 192.f);
        PlayerFighter->RequestMove(EDuelMove::Cut, Hand);
        PlayerFighter->TickMove(.55f);
        TestTrue(TEXT("Ritual sword light attacks hit with either hand"),
            PlayerFighter->MoveWeapon == EDuelWeapon::RitualSword && BossFighter->RecentDamage() == 78);
        PlayerFighter->SetWeapon(Hand == EDuelHand::Right ? EDuelWeapon::Rapier : EDuelWeapon::Buckler, Hand);
    }

    Mode->ResetEncounter();
    Position(0.f, 192.f);
    PlayerFighter->SetWeapon(EDuelWeapon::RitualSword);
    PlayerFighter->ArtSelection = EDuelArt::Orb;
    PlayerFighter->Health = 100.f;
    PlayerFighter->Focus = 54.f;
    PlayerFighter->Crimson();
    TestTrue(TEXT("Insufficient MP prevents the blood offering"), PlayerFighter->Move == EDuelMove::Idle &&
        Near(PlayerFighter->Health, 100.f));
    PlayerFighter->Focus = DuelCourt::MaxFocus;
    PlayerFighter->Crimson();
    PlayerFighter->TickMove(.95f);
    TestTrue(TEXT("The offering spends half the MP bar and 70 percent of current health for 15 seconds"),
        PlayerFighter->MoveArt == EDuelArt::RitualOffering && Near(PlayerFighter->Health, 30.f) &&
        Near(PlayerFighter->Focus, 55.f) && Near(PlayerFighter->RitualOfferingSeconds(), 15.f));
    ADuelArena& OfferingArena = *Mode->Arena;
    OfferingArena.TickRitualBlade(PlayerFighter, .016f);
    TestTrue(TEXT("Offering emits blood particles from the equipped sword"), OfferingArena.Particles.ContainsByPredicate(
        [](const ADuelArena::FParticle& Particle) { return Particle.bOffering; }));
    PlayerFighter->RequestMove(EDuelMove::Cut);
    PlayerFighter->TickMove(.55f);
    TestEqual(TEXT("An empowered light attack deals double damage"), BossFighter->RecentDamage(), 156);
    TestTrue(TEXT("An empowered light attack adds 50 percent posture damage and leaves a blood trail"),
        Near(BossFighter->Posture, 33.f) && OfferingArena.BladeSamples.ContainsByPredicate(
        [](const ADuelArena::FBladeSample& Sample) { return Sample.bOffering; }));
    PlayerFighter->RitualOfferingUntil = PlayerFighter->GetWorld()->GetTimeSeconds() - .01;
    OfferingArena.TickRitualBlade(PlayerFighter, .016f);
    TestTrue(TEXT("Blood particles and trails clear as soon as the offering expires"),
        !OfferingArena.Particles.ContainsByPredicate([](const ADuelArena::FParticle& Particle) { return Particle.bOffering; }) &&
        !OfferingArena.BladeSamples.ContainsByPredicate([](const ADuelArena::FBladeSample& Sample) { return Sample.bOffering; }));
    PlayerFighter->TickMove(.50f);
    Position(0.f, 192.f);
    PlayerFighter->RequestMove(EDuelMove::Cut);
    PlayerFighter->TickMove(.55f);
    TestEqual(TEXT("Normal damage returns after the offering expires"), BossFighter->RecentDamage(), 234);
    TestTrue(TEXT("Normal posture damage returns after the offering expires"), Near(BossFighter->Posture, 55.f));
    PlayerFighter->ArtSelection = EDuelArt::Default;

    Mode->ResetEncounter();
    Position(0.f, 325.f);
    PlayerFighter->SetWeapon(EDuelWeapon::Houndfang);
    PlayerFighter->Crimson();
    PlayerFighter->TickMove(.19f);
    TestTrue(TEXT("Houndfang steps forward before the attack window"),
        PlayerFighter->GetActorLocation().X > 82.f && BossFighter->RecentDamage() == 0);
    PlayerFighter->TickMove(.15f);
    TestTrue(TEXT("The forward Houndfang blade strikes at 3.25 metres for 116"),
        BossFighter->RecentDamage() == 116 && Mode->Arena->Orbs.IsEmpty());
    PlayerFighter->TickMove(.43f);
    TestTrue(TEXT("Houndfang leaves melee range after the cut"), Near(PlayerFighter->GetActorLocation().X, -254.f));
    BossFighter->SetActorLocation(FVector(985, 0, 103));
    const double BeforeChase = PlayerFighter->GetActorLocation().X;
    PlayerFighter->Crimson();
    PlayerFighter->TickMove(.51f);
    TestTrue(TEXT("Houndfang follow-up advances 485 cm and spends its own MP"),
        PlayerFighter->MoveArt == EDuelArt::HoundChase && Near(PlayerFighter->GetActorLocation().X - BeforeChase, 485.f) &&
        Near(PlayerFighter->Focus, DuelCourt::MaxFocus - 35.f));

    Mode->ResetEncounter();
    Position(0.f, 550.f);
    PlayerFighter->Crimson();
    PlayerFighter->TickMove(.61f);
    TestTrue(TEXT("Houndfang misses a target outside the physical sweep and spawns no projectile"),
        BossFighter->RecentDamage() == 0 && Mode->Arena->Orbs.IsEmpty());

    for (const float Range : {570.f, 690.f})
    {
        Mode->ResetEncounter();
        PlayerFighter->SetWeapon(EDuelWeapon::Staff);
        PlayerFighter->ActiveSpellSlot = 2;
        Position(0.f, Range);
        PlayerFighter->Crimson();
        PlayerFighter->TickMove(.58f);
        TestTrue(TEXT("Sable Blade hits at 5.70 metres with its step and misses at 6.90 metres"),
            BossFighter->RecentDamage() == (Range < 600.f ? 161 : 0));
    }

    Mode->ResetEncounter();
    Position(0.f, 410.f);
    PlayerFighter->SelectCharacter(EDuelCharacter::Hiro);
    PlayerFighter->SetWeapon(EDuelWeapon::FirePoker);
    PlayerFighter->Crimson();
    PlayerFighter->TickMove(.60f);
    TestTrue(TEXT("The fire poker art advances into a hit at extended range"),
        PlayerFighter->GetActorLocation().X > 200.f && Near(BossFighter->Health, DuelCourt::BossMaxHealth - 145.f));
    Mode->ResetEncounter();
    Position(0.f, 550.f);
    PlayerFighter->Crimson();
    PlayerFighter->TickMove(.60f);
    TestTrue(TEXT("The fire poker art still misses past the extended blade path"),
        Near(BossFighter->Health, DuelCourt::BossMaxHealth));
    PlayerFighter->SetWeapon(EDuelWeapon::Rapier);

    Mode->ResetEncounter();
    Position(0.f, 460.f);
    TestTrue(TEXT("BossFighter commits to a leap"), BossFighter->StartMove(EDuelMove::Leap));
    BossFighter->TickMove(1.25f);
    TestTrue(TEXT("The leap hits within its marked landing radius"),
        Near(BossFighter->GetActorLocation().X, 126.f) && Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth - BossDamage(54.f)));
    Mode->ResetEncounter();
    Position(0.f, 460.f);
    BossFighter->StartMove(EDuelMove::Leap);
    PlayerFighter->SetActorLocation(FVector(0, 360, 62));
    BossFighter->TickMove(1.25f);
    TestTrue(TEXT("Moving out of the marked landing radius avoids the leap"), Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth));

    Mode->ResetEncounter();
    Position(620.f, 0.f);
    BossFighter->ReceiveDuelDamage((DuelCourt::BossStageHealth[0] + 1.f), PlayerFighter->GetActorLocation());
    TestTrue(TEXT("Depleting the first stage starts the phase transition"), BossFighter->Move == EDuelMove::Awaken);
    BossFighter->TickMove(.93f);
    TestTrue(TEXT("BossFighter awakens during the transition"), BossFighter->IsAwakened());
    BossFighter->TickMove(1.4f);
    BossFighter->PoseFigure(.75f);
    TestTrue(TEXT("Only Ema's awakened form shows the bone wings"),
        BossFighter->Wings[0]->IsVisible() && !BossFighter->ShadowClaws[0]->IsVisible());
    TestTrue(TEXT("The second phase can cast a shockwave"), BossFighter->StartMove(EDuelMove::Storm));
    BossFighter->TickMove(1.90f);
    TestTrue(TEXT("The wave has not reached PlayerFighter early"), Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth));
    BossFighter->TickMove(.29f);
    BossFighter->TickMove(.24f);
    TestTrue(TEXT("The expanding wave hits once at the proportional damage"), Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth - BossDamage(74.f)));
    BossFighter->TickMove(DuelCourt::Timing(EDuelMove::Storm, true).Total());
    const auto StormAt = [&](float Distance)
    {
        Mode->ResetEncounter();
        Position(Distance, 0.f);
        BossFighter->BossPhase = 3;
        BossFighter->StartMove(EDuelMove::Storm);
        const auto Timing = DuelCourt::Timing(EDuelMove::Storm, true);
        for (int32 Frame = 0; Frame < FMath::CeilToInt(Timing.Total() * 60.f); ++Frame)
        {
            BossFighter->TickMove(1.f / 60.f);
        }
        return PlayerFighter->Health;
    };
    TestTrue(TEXT("The third-stage storm reaches its enlarged boundary and leaves the outside safe"),
        Near(StormAt(1020.f), DuelCourt::PlayerMaxHealth - BossDamage(82.f)) &&
        Near(StormAt(1100.f), DuelCourt::PlayerMaxHealth));
    Mode->ResetEncounter();
    Position(620.f, 0.f);
    BossFighter->ReceiveDuelDamage(DuelCourt::BossStageHealth[0] + 1.f, PlayerFighter->GetActorLocation());
    BossFighter->TickMove(DuelCourt::Timing(EDuelMove::Awaken, false).Total());

    BossFighter->ReceiveDuelDamage(1000.f, PlayerFighter->GetActorLocation());
    TestTrue(TEXT("A lethal strike cannot skip the third stage"), BossFighter->IsAlive() &&
        Near(BossFighter->Health, DuelCourt::BossStageHealth[2]));
    BossFighter->TickMove(DuelCourt::Timing(EDuelMove::Eclipse, true).Total());
    BossFighter->ReceiveDuelDamage(1000.f, PlayerFighter->GetActorLocation());
    TestTrue(TEXT("Defeating BossFighter resolves to victory"), Mode->Outcome() == EDuelOutcome::Victory);
    PlayerFighter->ConfirmDuel();
    TestTrue(TEXT("Victory opens the gate while the evidence choice remains undecided"), Mode->Outcome() == EDuelOutcome::Victory &&
        Mode->CanExplore() && !PlayerFighter->IsLocked() && Mode->Ending() == EDuelEnding::Undecided);
    const FVector Entry(DuelCourt::ArchiveEntryX - 55.f, 0, 62);
    PlayerFighter->SetActorLocation(Entry);
    Mode->Tick(0.f);
    TestTrue(TEXT("Walking through the east arch changes rooms in place"), Mode->Outcome() == EDuelOutcome::Archive &&
        PlayerFighter->GetActorLocation().Equals(Entry));
    PlayerFighter->ConfirmDuel();
    TestFalse(TEXT("The archive entrance leaves the evidence desk to be explored"), Mode->ReadingArchive());
    PlayerFighter->SetActorLocation(FVector(DuelCourt::ArchiveDeskX - 151.f, 0, 62));
    PlayerFighter->ConfirmDuel();
    TestTrue(TEXT("The clear time and evidence open at the archive desk"),
        Mode->ReadingArchive() && !Mode->CanExplore() && Mode->DuelSeconds() > 0);
    PlayerFighter->MenuForward();
    PlayerFighter->ConfirmDuel();
    TestTrue(TEXT("The second evidence option records a witness summons"), Mode->ReadingArchive() &&
        Mode->Ending() == EDuelEnding::CallWitness);
    PlayerFighter->ConfirmDuel();
    TestTrue(TEXT("Closing the record allows exploration again"), !Mode->ReadingArchive() && Mode->CanExplore());
    Mode->ReturnToCheckpoint();
    EnterGate();
    TestTrue(TEXT("Rematch restores both fighters and phase one"),
        Mode->Outcome() == EDuelOutcome::Fighting && Near(PlayerFighter->HealthRatio(), 1.f) &&
        Near(BossFighter->HealthRatio(), 1.f) && !BossFighter->IsAwakened() &&
        PlayerFighter->FlaskCount() == DuelCourt::DefaultHealthFlasks);
    PlayerFighter->ReceiveDuelDamage(1000.f, BossFighter->GetActorLocation());
    TestTrue(TEXT("Losing PlayerFighter resolves to defeat"), Mode->Outcome() == EDuelOutcome::Defeat);
    PlayerFighter->ConfirmDuel();
    TestTrue(TEXT("Defeat revives the player at the checkpoint, outside combat"),
        Mode->Outcome() == EDuelOutcome::Ready && PlayerFighter->GetActorLocation().X < DuelCourt::GateX && PlayerFighter->IsAlive());
    EnterGate();
    TestTrue(TEXT("The duel restarts after defeat"), Mode->Outcome() == EDuelOutcome::Fighting && PlayerFighter->IsAlive());

    Position(0.f, 255.f);
    PlayerFighter->EquipGreatblade();
    const auto EquipTiming = DuelCourt::Timing(EDuelMove::Equip, false);
    PlayerFighter->TickMove(EquipTiming.Windup - .01f);
    TestTrue(TEXT("Weapon change commits at the equip keyframe"), PlayerFighter->EquippedWeapon() == EDuelWeapon::Rapier);
    PlayerFighter->TickMove(EquipTiming.Active + EquipTiming.Recovery + .02f);
    TestTrue(TEXT("The greatblade is equipped"), PlayerFighter->EquippedWeapon() == EDuelWeapon::Greatblade);
    PlayerFighter->RequestMove(EDuelMove::Heavy);
    PlayerFighter->EquipSpear();
    PlayerFighter->TickMove(DuelCourt::Timing(EDuelMove::Heavy, false, EDuelWeapon::Greatblade).Windup - .01f);
    TestTrue(TEXT("Heavy damage and weapon switching stay locked during windup"),
        PlayerFighter->EquippedWeapon() == EDuelWeapon::Greatblade && Near(BossFighter->HealthRatio(), 1.f));
    PlayerFighter->TickMove(.96f);
    TestTrue(TEXT("Greatblade heavy deals damage and posture"), Near(BossFighter->Health, DuelCourt::BossMaxHealth - 174.f)
        && Near(BossFighter->Posture, 54.f));
    PlayerFighter->RequestMove(EDuelMove::Heavy);
    PlayerFighter->TickMove(1.60f);
    TestTrue(TEXT("Two charged heavy hits break posture"), BossFighter->IsStaggered() && Near(BossFighter->Posture, 0.f));
    const float StaggerHealth = BossFighter->Health;
    BossFighter->ReceiveDuelDamage(40.f, PlayerFighter->GetActorLocation());
    TestTrue(TEXT("The stagger opening raises damage"), Near(BossFighter->Health, StaggerHealth - 52.f));

    Mode->ResetEncounter();
    BossFighter->StartMove(EDuelMove::Seals);
    BossFighter->DamagePosture(DuelCourt::MaxPosture);
    TestTrue(TEXT("Full posture interrupts a spell before impact"), BossFighter->IsStaggered() &&
        BossFighter->MapWaves.IsEmpty() && BossFighter->BossFollowup == EDuelMove::Idle);

    Mode->ResetEncounter();
    PlayerFighter->SetWeapon(EDuelWeapon::Spear);
    Position(0.f, 485.f);
    PlayerFighter->Crimson();
    PlayerFighter->TickMove(.67f);
    TestTrue(TEXT("The spear art advances and lands a forward thrust"), PlayerFighter->GetActorLocation().X > 100.f &&
        Near(BossFighter->Health, DuelCourt::BossMaxHealth - 172.f));

    Mode->ResetEncounter();
    PlayerFighter->SetWeapon(EDuelWeapon::Rapier);
    Position(0.f, 205.f);
    BossFighter->StartMove(EDuelMove::Paw);
    PlayerFighter->PressLeftHand();
    PlayerFighter->HandPresses[static_cast<uint8>(EDuelHand::Left)].Since -= .16;
    BossFighter->TickMove(.77f);
    TestTrue(TEXT("A timed front parry cancels the claw strike and opens a brief punish"),
        Near(PlayerFighter->HealthRatio(), 1.f) && BossFighter->IsGuardReeling() && Near(BossFighter->Posture, 36.f));
    PlayerFighter->ReleaseLeftHand();
    PlayerFighter->TickMove(.24f);
    BossFighter->TickMove(.24f);
    PlayerFighter->Cut();
    PlayerFighter->TickMove(.39f);
    TestTrue(TEXT("A quick counterattack fits before the guard recoil ends"),
        BossFighter->IsGuardReeling() && BossFighter->RecentDamage() == 62);
    BossFighter->ReceiveDuelDamage(34.f, PlayerFighter->GetActorLocation());
    TestEqual(TEXT("Recent damage totals actual HP lost across successive hits"), BossFighter->RecentDamage(), 96);
    Mode->ResetEncounter();
    TestEqual(TEXT("Resetting the encounter clears the damage display"), BossFighter->RecentDamage(), 0);
    Position(0.f, 205.f);
    PlayerFighter->SetWeapon(EDuelWeapon::Buckler, EDuelHand::Left);
    PlayerFighter->Stamina = 10.f;
    PlayerFighter->PressLeftHand();
    PlayerFighter->HandPresses[static_cast<uint8>(EDuelHand::Left)].Since -= .20;
    BossFighter->StartMove(EDuelMove::Paw);
    BossFighter->TickMove(.77f);
    TestTrue(TEXT("Guarding beyond the stamina budget breaks stance and drains the remainder"),
        PlayerFighter->IsGuardBroken() && Near(PlayerFighter->Stamina, 0.f) &&
        Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth - BossDamage(42.f)));

    Mode->ResetEncounter();
    Position(620.f, 0.f);
    BossFighter->ReceiveDuelDamage((DuelCourt::BossStageHealth[0] + 1.f), PlayerFighter->GetActorLocation());
    BossFighter->TickMove(DuelCourt::Timing(EDuelMove::Awaken, false).Total());
    BossFighter->StartMove(EDuelMove::Seals);
    const auto SealTiming = DuelCourt::Timing(EDuelMove::Seals, true);
    BossFighter->TickMove(SealTiming.Windup - .01f);
    TestTrue(TEXT("The first seal has a dodge window"), Near(PlayerFighter->HealthRatio(), 1.f));
    BossFighter->TickMove(.02f);
    PlayerFighter->SetActorLocation(FVector(15, -820, 62));
    BossFighter->TickMove(SealTiming.Total() - BossFighter->MoveAge + .001f);
    TestTrue(TEXT("A seal hits once and later marks can be avoided"),
        Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth - BossDamage(44.f)));
    BossFighter->ReceiveDuelDamage((DuelCourt::BossStageHealth[1] + 1.f), PlayerFighter->GetActorLocation());
    TestTrue(TEXT("Depleting the second stage starts the third phase transition"), BossFighter->Move == EDuelMove::Eclipse);
    BossFighter->TickMove(DuelCourt::Timing(EDuelMove::Eclipse, true).Total());
    TestTrue(TEXT("Eclipse and its dialogue begin"), BossFighter->Phase() == 3 && Mode->DialoguePhase() == 3);
    Position(605.f, 0.f);
    BossFighter->StartMove(EDuelMove::Cross);
    const FVector Center = BossFighter->CastOrigin;
    const float BeforeCross = PlayerFighter->Health;
    PlayerFighter->SetActorLocation(FVector(Center.X + 235.f, Center.Y + 240.f, 62));
    BossFighter->TickMove(1.04f);
    TestTrue(TEXT("The first lane can be dodged"), Near(PlayerFighter->Health, BeforeCross));
    PlayerFighter->SetActorLocation(FVector(Center.X, Center.Y + 255.f, 62));
    BossFighter->TickMove(.41f);
    TestTrue(TEXT("The perpendicular lane strikes after the delay"), Near(PlayerFighter->Health, BeforeCross - BossDamage(63.f)));

    const auto AdvancePhase = [&](int32 Phase)
    {
        const EDuelMove Transition = Phase == 2 ? EDuelMove::Awaken : EDuelMove::Eclipse;
        BossFighter->ReceiveDuelDamage(DuelCourt::BossStageHealth[Phase - 2], PlayerFighter->GetActorLocation());
        BossFighter->TickMove(DuelCourt::Timing(Transition, Phase == 3).Total());
    };
    const auto SampleAttacks = [&](EDuelCharacter Character)
    {
        BossFighter->SelectCharacter(Character);
        BossFighter->BossRandom.Initialize(941);
        TSet<EDuelMove> Attacks;
        for (int32 I = 0; I < 170; ++I)
        {
            Attacks.Add(BossFighter->PickBossAttack(I % 2 ? 360.f : 840.f));
        }
        return Attacks;
    };
    const TSet<EDuelMove> EmaMoves = SampleAttacks(EDuelCharacter::Ema);
    const TSet<EDuelMove> HiroMoves = SampleAttacks(EDuelCharacter::Hiro);
    for (const EDuelMove Attack : EmaMoves)
    {
        TestFalse(TEXT("The two boss attack pools do not share moves"), HiroMoves.Contains(Attack));
    }
    TestTrue(TEXT("Each boss has its own selection of signature attacks"), EmaMoves.Num() >= 13 && HiroMoves.Num() >= 9 &&
        EmaMoves.Contains(EDuelMove::WingVerdict) && EmaMoves.Contains(EDuelMove::Tribunal) &&
        EmaMoves.Contains(EDuelMove::WingSweep) && EmaMoves.Contains(EDuelMove::Wingfall) &&
        HiroMoves.Contains(EDuelMove::Reversal) && HiroMoves.Contains(EDuelMove::ShadowSnare) &&
        HiroMoves.Contains(EDuelMove::ShadowSweep) && HiroMoves.Contains(EDuelMove::ShadowVolley));
    TestTrue(TEXT("Each witch has attacks with a unique mechanical response"),
        EmaMoves.Contains(EDuelMove::BoneProcession) && EmaMoves.Contains(EDuelMove::WrittenVerdict) &&
        HiroMoves.Contains(EDuelMove::FeintCounter) && HiroMoves.Contains(EDuelMove::CrossAmbush));
    bool bQueuedCombo = false;
    bool bFinishedCombo = false;
    bool bReversalCombo = false;
    BossFighter->BossComboDepth = 0;
    for (int32 I = 0; I < 92; ++I)
    {
        const EDuelMove Follow = BossFighter->PickBossFollowup(EDuelMove::TalonRush, 330.f);
        bQueuedCombo |= Follow == EDuelMove::ShadowSweep;
        bReversalCombo |= Follow == EDuelMove::Reversal;
        bFinishedCombo |= Follow == EDuelMove::Idle;
    }
    BossFighter->BossComboDepth = 3;
    TestTrue(TEXT("Hiro's rush can end, sweep, or leap sideways, with a maximum of four attacks"),
        bQueuedCombo && bReversalCombo && bFinishedCombo &&
        BossFighter->PickBossFollowup(EDuelMove::TalonRush, 330.f) == EDuelMove::Idle);

    Mode->ResetEncounter();
    Position(0.f, 600.f);
    AdvancePhase(2);
    BossFighter->PoseFigure(.75f);
    TestTrue(TEXT("Hiro's second form shows claws and a longer shadow tail without wings"),
        !BossFighter->Wings[0]->IsVisible() && BossFighter->ShadowClaws[0]->IsVisible() &&
        BossFighter->ShadowTail && BossFighter->ShadowTail->IsVisible() && BossFighter->Tail->GetRelativeScale3D().X > 1.2);
    BossFighter->StartMove(EDuelMove::NightLanes);
    if (!TestEqual(TEXT("Hiro's lane barrage has two waves in stage two"), BossFighter->MapWaves.Num(), 2))
    {
        return false;
    }
    const ADuelFighter::FMapWave FirstLanes = BossFighter->MapWaves[0];
    const ADuelFighter::FMapWave SecondLanes = BossFighter->MapWaves[1];
    for (int32 Row = 0; Row < 7; ++Row)
    {
        const float Y = (Row - 3) * 300.f;
        if (!FirstLanes.Marks.ContainsByPredicate([Y](const FVector& Mark) { return FMath::IsNearlyEqual(Mark.Y, Y); }))
        {
            PlayerFighter->SetActorLocation(FVector(0, Y, 62));
            break;
        }
    }
    BossFighter->TickMove(FirstLanes.ImpactTime + .01f);
    TestTrue(TEXT("The unmarked lane is safe"), Near(PlayerFighter->HealthRatio(), 1.f));
    PlayerFighter->SetActorLocation(FVector(-900, SecondLanes.Marks[0].Y, 62));
    BossFighter->TickMove(SecondLanes.ImpactTime - FirstLanes.ImpactTime + .08f);
    TestTrue(TEXT("A marked lane hits once in its wave"), Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth - BossDamage(53.f)));

    Mode->ResetEncounter();
    Position(820.f, 0.f);
    const DuelCourt::FMoveTiming RushTiming = DuelCourt::Timing(EDuelMove::TalonRush, false);
    TestTrue(TEXT("Hiro's rush starts after the shorter readable warning"), Near(RushTiming.Windup, .56f));
    BossFighter->StartMove(EDuelMove::TalonRush);
    BossFighter->TickMove(RushTiming.Windup + .015f);
    TestTrue(TEXT("The start of the rush does not hit the far end of the lane"),
        BossFighter->GetActorLocation().X < 30.f && Near(PlayerFighter->HealthRatio(), 1.f));
    BossFighter->TickMove(.395f);
    TestTrue(TEXT("Hiro's rush crosses most of the marked lane and hits inside it"),
        BossFighter->GetActorLocation().X > 650.f && Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth - BossDamage(49.f)));
    Mode->ResetEncounter();
    Position(515.f, 0.f);
    BossFighter->StartMove(EDuelMove::TalonRush);
    BossFighter->TickMove(RushTiming.Windup - .12f);
    PlayerFighter->Right(1.f);
    PlayerFighter->PressShift();
    PlayerFighter->ReleaseShift();
    for (int32 Frame = 0; Frame < 31; ++Frame)
    {
        if (PlayerFighter->Move != EDuelMove::Idle)
        {
            PlayerFighter->TickMove(.018f);
        }
        BossFighter->TickMove(.018f);
    }
    TestTrue(TEXT("A side roll during the shorter warning escapes the rush lane"),
        Near(PlayerFighter->HealthRatio(), 1.f) && FMath::Abs(PlayerFighter->GetActorLocation().Y) > 250.f);

    Mode->ResetEncounter();
    Position(610.f, 0.f);
    AdvancePhase(2);
    AdvancePhase(3);
    BossFighter->StartMove(EDuelMove::InwardReap);
    BossFighter->TickMove(1.38f);
    TestTrue(TEXT("The inward wave deals no damage ahead of its front"), Near(PlayerFighter->HealthRatio(), 1.f));
    BossFighter->TickMove(.41f);
    TestTrue(TEXT("The inward front hits at the marked radius"), Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth - BossDamage(82.f)));

    Mode->ResetEncounter();
    BossFighter->SelectCharacter(EDuelCharacter::Ema);
    Position(0.f, 540.f);
    AdvancePhase(2);
    AdvancePhase(3);
    BossFighter->StartMove(EDuelMove::AtlasRain);
    if (!TestEqual(TEXT("Ema's rain has three waves in stage three"), BossFighter->MapWaves.Num(), 3))
    {
        return false;
    }
    const ADuelFighter::FMapWave FirstRain = BossFighter->MapWaves[0];
    const FVector At = FirstRain.Marks[0];
    PlayerFighter->SetActorLocation(FVector(At.X, At.Y, 62));
    BossFighter->TickMove(FirstRain.ImpactTime - .01f);
    TestTrue(TEXT("Standing in a rain sigil during its warning is safe"), Near(PlayerFighter->HealthRatio(), 1.f));
    BossFighter->TickMove(.02f);
    TestTrue(TEXT("The first rain wave lands once on a painted sigil"), Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth - BossDamage(65.f)));
    BossFighter->TickMove(.65f);
    TestTrue(TEXT("The alternating rain wave leaves the first mark safe"),
        Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth - BossDamage(65.f)));

    const auto PillarDamageAt = [&](int32 Phase, float Distance)
    {
        Mode->ResetEncounter();
        Position(Distance, 0.f);
        for (int32 Next = 2; Next <= Phase; ++Next)
        {
            AdvancePhase(Next);
        }
        BossFighter->StartMove(EDuelMove::Pillar);
        BossFighter->TickMove(1.32f);
        return DuelCourt::PlayerMaxHealth - PlayerFighter->Health;
    };
    TestTrue(TEXT("The pillar footprint expands in each stage"),
        Near(PillarDamageAt(1, 480.f), 0.f) && PillarDamageAt(2, 480.f) > 0.f &&
        Near(PillarDamageAt(2, 545.f), 0.f) && PillarDamageAt(3, 545.f) > 0.f);
    const float FirstDamage = PillarDamageAt(1, 390.f);
    const float SecondDamage = PillarDamageAt(2, 390.f);
    const float ThirdDamage = PillarDamageAt(3, 390.f);
    TestTrue(TEXT("The same boss attack hits progressively harder"),
        Near(FirstDamage, BossDamage(67.f)) && Near(SecondDamage, BossDamage(73.f)) && Near(ThirdDamage, BossDamage(81.f)));

    Mode->ResetEncounter();
    Position(760.f, -420.f);
    BossFighter->BossRandom.Initialize(714);
    BossFighter->StartMove(EDuelMove::FlankLeap);
    const FVector Landing = BossFighter->CastOrigin;
    TestTrue(TEXT("Ema's flank leap marks an offset route longer than ten meters"),
        BossFighter->Travel > 1000.f && FMath::Abs(Landing.Y) > 180.f);
    PlayerFighter->SetActorLocation(FVector(Landing.X, Landing.Y, 62));
    BossFighter->TickMove(.81f);
    TestTrue(TEXT("The flank leap deals damage only at its landing"), Near(PlayerFighter->HealthRatio(), 1.f));
    BossFighter->TickMove(.61f);
    TestTrue(TEXT("Ema lands on the marked flank and hits its disk"),
        FVector::Dist2D(BossFighter->GetActorLocation(), Landing) < 3.f &&
        Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth - BossDamage(49.f)));

    Mode->ResetEncounter();
    Position(650.f, 0.f);
    AdvancePhase(2);
    AdvancePhase(3);
    BossFighter->StartMove(EDuelMove::Pursuit);
    if (!TestEqual(TEXT("Ema's third-stage pursuit casts four waves"), BossFighter->MapWaves.Num(), 4))
    {
        return false;
    }
    PlayerFighter->SetActorLocation(FVector(650, 380, 62));
    BossFighter->TickMove(DuelBossDefense::PursuitBeat + .02f);
    const auto& SecondMark = BossFighter->MapWaves[1].Marks;
    TestTrue(TEXT("The next pursuit mark uses the new player position"),
        SecondMark.Num() == 1 && Near(SecondMark[0].Y, 380.f));
    PlayerFighter->SetActorLocation(FVector(650, 730, 62));
    BossFighter->TickMove(.39f);
    TestTrue(TEXT("Leaving a marked disk is safe and the next disk stays fixed after warning"),
        Near(PlayerFighter->HealthRatio(), 1.f) && SecondMark.Num() == 1 && Near(SecondMark[0].Y, 380.f));
    PlayerFighter->SetActorLocation(FVector(650, 380, 62));
    BossFighter->TickMove(BossFighter->MapWaves[1].ImpactTime - BossFighter->MoveAge + .01f);
    TestTrue(TEXT("Ema moves sideways while the warned mark strikes"),
        FMath::Abs(BossFighter->GetActorLocation().Y) > 120.f &&
        Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth - BossDamage(56.f)));

    Mode->ResetEncounter();
    Position(0.f, 182.f);
    PlayerFighter->SetWeapon(EDuelWeapon::Hammer, EDuelHand::Left);
    PlayerFighter->StartMove(EDuelMove::Heavy, EDuelHand::Left);
    PlayerFighter->TickMove(.93f);
    TestTrue(TEXT("The left-hand hammer can break posture with a visible heavy hit"),
        Near(BossFighter->Health, DuelCourt::BossMaxHealth - 156.f) && Near(BossFighter->Posture, 79.2f));

    Mode->ResetEncounter();
    Position(0.f, 190.f);
    PlayerFighter->SetWeapon(EDuelWeapon::Axe);
    PlayerFighter->ArtSelection = EDuelArt::Whirl;
    PlayerFighter->Crimson();
    PlayerFighter->ReleaseCrimson();
    PlayerFighter->TickMove(.85f);
    TestTrue(TEXT("The whirl art hits along the new axe's sweep"),
        PlayerFighter->MoveArt == EDuelArt::Whirl && Near(BossFighter->Health, DuelCourt::BossMaxHealth - 162.f));

    Mode->ResetEncounter();
    Position(0.f, 245.f);
    PlayerFighter->SetWeapon(EDuelWeapon::Rapier);
    PlayerFighter->ArtSelection = EDuelArt::Burst;
    PlayerFighter->Crimson();
    PlayerFighter->TickMove(1.04f);
    TestTrue(TEXT("An alternate art replaces the rapier's native ranged attack"),
        PlayerFighter->MoveArt == EDuelArt::Burst && Near(BossFighter->Health, DuelCourt::BossMaxHealth - 196.f));
    PlayerFighter->ArtSelection = EDuelArt::Default;

    const EDuelArt HiroArt = DuelCourt::NextArt(EDuelArt::Whirl, EDuelCharacter::Hiro, 1);
    const EDuelArt EmaArt = DuelCourt::NextArt(EDuelArt::Whirl, EDuelCharacter::Ema, 1);
    TestTrue(TEXT("Each character's loadout cycles four exclusive arts and wraps back to the weapon default"),
        HiroArt == EDuelArt::HiroStep && EmaArt == EDuelArt::EmaLight &&
        DuelCourt::NextArt(HiroArt, EDuelCharacter::Hiro, 1) == EDuelArt::HiroBind &&
        DuelCourt::NextArt(EmaArt, EDuelCharacter::Ema, 1) == EDuelArt::EmaWard &&
        DuelCourt::NextArt(EDuelArt::HiroBind, EDuelCharacter::Hiro, 1) == EDuelArt::HiroThreads &&
        DuelCourt::NextArt(EDuelArt::EmaWard, EDuelCharacter::Ema, 1) == EDuelArt::EmaVotive &&
        DuelCourt::NextArt(EDuelArt::HiroThreads, EDuelCharacter::Hiro, 1) == EDuelArt::HiroReprise &&
        DuelCourt::NextArt(EDuelArt::EmaVotive, EDuelCharacter::Ema, 1) == EDuelArt::EmaAbsolution &&
        DuelCourt::NextArt(EDuelArt::HiroReprise, EDuelCharacter::Hiro, 1) == EDuelArt::HiroCrossThread &&
        DuelCourt::NextArt(EDuelArt::EmaAbsolution, EDuelCharacter::Ema, 1) == EDuelArt::EmaAegis &&
        !DuelCourt::SupportsArt(EDuelArt::HiroCrossThread, EDuelCharacter::Ema) &&
        !DuelCourt::SupportsArt(EDuelArt::EmaAegis, EDuelCharacter::Hiro));

    Mode->ResetEncounter();
    PlayerFighter->SelectCharacter(EDuelCharacter::Hiro);
    Position(0.f, 393.f);
    PlayerFighter->ArtSelection = EDuelArt::HiroBind;
    PlayerFighter->Crimson();
    PlayerFighter->TickMove(.67f);
    TestTrue(TEXT("Hiro's first seal strikes the warned disk"),
        Near(BossFighter->Health, DuelCourt::BossMaxHealth - 93.f));
    PlayerFighter->TickMove(.40f);
    TestTrue(TEXT("Hiro's second seal hits the same disk after the pause"),
        Near(BossFighter->Health, DuelCourt::BossMaxHealth - 93.f * 1.75f));
    Mode->ResetEncounter();
    Position(0.f, 373.f);
    PlayerFighter->ArtSelection = EDuelArt::HiroStep;
    PlayerFighter->Crimson();
    PlayerFighter->TickMove(.57f);
    TestTrue(TEXT("Hiro's step advances and connects through the blade"),
        PlayerFighter->GetActorLocation().X > 200.f && Near(BossFighter->Health, DuelCourt::BossMaxHealth - 149.f));

    PlayerFighter->SelectCharacter(EDuelCharacter::Ema);
    TestTrue(TEXT("A character change clears an unavailable exclusive art"),
        PlayerFighter->SelectedArt() == EDuelArt::Default);
    Mode->ResetEncounter();
    Position(0.f, 780.f);
    PlayerFighter->ArtSelection = EDuelArt::EmaLight;
    PlayerFighter->Crimson();
    const FVector LightMark = PlayerFighter->CastOrigin;
    BossFighter->AddActorWorldOffset(FVector(0, 280, 0));
    PlayerFighter->TickMove(.84f);
    TestTrue(TEXT("Ema's falling light locks its origin and can miss"),
        Near(LightMark.X, 780.f) && Near(BossFighter->HealthRatio(), 1.f));
    Mode->ResetEncounter();
    Position(0.f, 760.f);
    PlayerFighter->Crimson();
    PlayerFighter->TickMove(.84f);
    TestTrue(TEXT("Ema's falling light hits inside the fixed disk"),
        Near(BossFighter->Health, DuelCourt::BossMaxHealth - 156.f));
    Mode->ResetEncounter();
    Position(0.f, 253.f);
    PlayerFighter->ArtSelection = EDuelArt::EmaWard;
    PlayerFighter->Crimson();
    PlayerFighter->TickMove(.25f);
    PlayerFighter->ReceiveDuelDamage(49.f, BossFighter->GetActorLocation(), true);
    TestTrue(TEXT("Ema's front counter prevents the hit and strikes back once"),
        Near(PlayerFighter->HealthRatio(), 1.f) && PlayerFighter->bHitDelivered &&
        Near(BossFighter->Health, DuelCourt::BossMaxHealth - 152.f) && Near(BossFighter->Posture, 67.f));
    PlayerFighter->ReceiveDuelDamage(49.f, BossFighter->GetActorLocation(), true);
    TestTrue(TEXT("The same ward cannot absorb a second hit"),
        Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth - BossDamage(49.f)));

    Mode->ResetEncounter();
    BossFighter->SelectCharacter(EDuelCharacter::Hiro);
    Position(435.f, -135.f);
    BossFighter->StartMove(EDuelMove::Reversal);
    const FVector ReversalMark = BossFighter->CastOrigin;
    const auto ReversalTiming = DuelCourt::Timing(EDuelMove::Reversal, false);
    TestTrue(TEXT("Hiro's reversal moves to a lateral position"), FMath::Abs(ReversalMark.Y) >= 180.f);
    BossFighter->TickMove(ReversalTiming.Windup + DuelCourt::ReversalStepTime + .01f);
    TestTrue(TEXT("Reversal lands safely before the thrust is announced"),
        FVector::Dist2D(BossFighter->GetActorLocation(), ReversalMark) < 2.f &&
        Near(PlayerFighter->HealthRatio(), 1.f));
    const FVector ReversalAcross = FVector::CrossProduct(FVector::UpVector, BossFighter->CastAxis);
    const FVector SideOfThrust = ReversalMark + BossFighter->CastAxis * 180.f +
        ReversalAcross * (DuelCourt::ReversalHalfWidth(1) + 18.f);
    PlayerFighter->SetActorLocation(FVector(SideOfThrust.X, SideOfThrust.Y, 62));
    BossFighter->TickMove(DuelCourt::ReversalCueTime + .01f);
    TestTrue(TEXT("Reversal's close side is safe outside the thrust"), Near(PlayerFighter->HealthRatio(), 1.f));

    Mode->ResetEncounter();
    Position(435.f, -135.f);
    BossFighter->StartMove(EDuelMove::Reversal);
    BossFighter->TickMove(ReversalTiming.Windup + DuelCourt::ReversalStepTime + .01f);
    BossFighter->TickMove(DuelCourt::ReversalCueTime + .01f);
    TestTrue(TEXT("Reversal deals damage down the announced thrust lane"),
        Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth - BossDamage(52.f)));

    const auto CheckTailSweep = [&](bool bAirborne)
    {
        Mode->ResetEncounter();
        Position(365.f, 0.f);
        AdvancePhase(2);
        BossFighter->StartMove(EDuelMove::ShadowSweep);
        const auto Sweep = DuelCourt::Timing(EDuelMove::ShadowSweep, true);
        const FVector Before = BossFighter->GetActorLocation();
        if (bAirborne)
        {
            PlayerFighter->SetActorLocation(FVector(365, 0, 115));
        }
        BossFighter->TickMove(Sweep.Windup + .10f);
        TestTrue(TEXT("The tail sweep does not hit ahead of its leading edge"), Near(PlayerFighter->HealthRatio(), 1.f));
        BossFighter->TickMove(Sweep.Active);
        TestTrue(TEXT("The tail sweep stays planted and can be jumped"), BossFighter->GetActorLocation().Equals(Before) &&
            Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth - (bAirborne ? 0.f : BossDamage(59.f))));
    };
    CheckTailSweep(false);
    CheckTailSweep(true);

    Mode->ResetEncounter();
    Position(655.f, 0.f);
    AdvancePhase(2);
    BossFighter->StartMove(EDuelMove::ShadowSnare);
    const auto Snare = DuelCourt::Timing(EDuelMove::ShadowSnare, true);
    if (!TestEqual(TEXT("Hiro places two shadow threads in stage two"), BossFighter->ShadowSnares.Num(), 2))
    {
        return false;
    }
    const FVector Thread = BossFighter->ShadowSnares[0].Center;
    PlayerFighter->SetActorLocation(FVector(Thread.X, Thread.Y, 115));
    BossFighter->TickMove(Snare.Windup + .02f);
    TestFalse(TEXT("Jumping across an armed thread does not trigger it"), BossFighter->ShadowSnares[0].bTriggered);
    PlayerFighter->SetActorLocation(FVector(Thread.X, Thread.Y, 62));
    BossFighter->TickMove(.02f);
    TestTrue(TEXT("Touching an armed thread locks the strike position with a delay"),
        BossFighter->ShadowSnares[0].bTriggered && Near(PlayerFighter->HealthRatio(), 1.f));
    BossFighter->TickMove(.46f);
    TestTrue(TEXT("The triggered snare hits the recorded point"),
        Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth - BossDamage(51.f)));

    Mode->ResetEncounter();
    BossFighter->SelectCharacter(EDuelCharacter::Ema);
    Position(655.f, 0.f);
    AdvancePhase(2);
    BossFighter->StartMove(EDuelMove::WingVerdict);
    if (!TestEqual(TEXT("Ema's bone-wing lanes fire twice in phase two"), BossFighter->MapWaves.Num(), 2))
    {
        return false;
    }
    const auto Wave = BossFighter->MapWaves[0];
    TestTrue(TEXT("Ema's first warning places diagonal lanes"), Wave.Marks.Num() == 2 && FMath::Abs(Wave.Axis.Y) > .32);
    const FVector WingLane = Wave.Marks[0];
    PlayerFighter->SetActorLocation(FVector(WingLane.X, WingLane.Y, 62));
    BossFighter->TickMove(Wave.ImpactTime - .01f);
    TestTrue(TEXT("Ema's lane warning does not deal damage"), Near(PlayerFighter->HealthRatio(), 1.f));
    BossFighter->TickMove(.02f);
    TestTrue(TEXT("Ema's lanes hit at the announced time"), Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth - BossDamage(49.f)));
    Mode->ResetEncounter();
    Position(495.f, -150.f);
    AdvancePhase(2);
    AdvancePhase(3);
    BossFighter->StartMove(EDuelMove::Tribunal);
    if (!TestEqual(TEXT("The third-stage tribunal fires three waves"), BossFighter->MapWaves.Num(), 3))
    {
        return false;
    }
    TestTrue(TEXT("The tribunal opens with a safe center and radial columns"),
        BossFighter->MapWaves[0].Marks.Num() == 6 && BossFighter->MapWaves[2].Marks.IsEmpty());
    BossFighter->TickMove(BossFighter->MapWaves[2].WarnTime + .01f);
    const FVector Locked = BossFighter->MapWaves[2].Marks[0];
    TestTrue(TEXT("The tribunal's last strike warns at the current position"), Near(Locked.X, 495.f));
    PlayerFighter->SetActorLocation(FVector(495, 491, 62));
    BossFighter->TickMove(.70f);
    TestTrue(TEXT("Moving away from the last tribunal mark avoids its blow"), Near(PlayerFighter->HealthRatio(), 1.f));

    Mode->ResetEncounter();
    Position(0.f, 589.f);
    BossFighter->BossPause = .22f;
    BossFighter->OrbitSide = 1.f;
    BossFighter->TickBoss(.04f);
    TestTrue(TEXT("The boss advances and strafes between attacks"),
        BossFighter->GetPendingMovementInputVector().X < -.5f &&
        FMath::Abs(BossFighter->GetPendingMovementInputVector().Y) > .2f);
    BossFighter->ConsumeMovementInputVector();

    ADuelArena& Audio = *Mode->Arena;
    const int32 Ema = static_cast<int32>(EDuelCharacter::Ema);
    const int32 Attack = static_cast<int32>(EDuelVoice::Attack);
    const int32 Art = static_cast<int32>(EDuelVoice::Art);
    const auto PreviousAttackClip = Audio.Voices[Ema][Attack];
    const auto PreviousArtClip = Audio.Voices[Ema][Art];
    Audio.Voices[Ema][Attack].PCM.SetNumZeroed(2);
    Audio.Voices[Ema][Art].PCM.Reset();
    Audio.NextVoiceAt = 13.2;
    Audio.NextCharacterVoiceAt[Ema] = 15.8;
    TestTrue(TEXT("Voices obey the global and character spacing and missing arts never reuse attacks"),
        !Audio.CanPlayVoice(EDuelCharacter::Ema, EDuelVoice::Attack, 13.1) &&
        !Audio.CanPlayVoice(EDuelCharacter::Ema, EDuelVoice::Attack, 15.7) &&
        Audio.CanPlayVoice(EDuelCharacter::Ema, EDuelVoice::Attack, 16.) &&
        !Audio.CanPlayVoice(EDuelCharacter::Ema, EDuelVoice::Art, 25.));
    Audio.Voices[Ema][Attack] = PreviousAttackClip;
    Audio.Voices[Ema][Art] = PreviousArtClip;
    Audio.NextVoiceAt = 0.;
    Audio.NextCharacterVoiceAt[Ema] = 0.;
    Mode->AnnouncePhase(2);
    TestTrue(TEXT("Phase dialogue shows its speaker and takes precedence over vocal efforts"),
        Mode->CombatCaption() != nullptr && Mode->CaptionSpeaker().Contains(TEXT("首领")));
    Mode->TogglePause();
    Mode->Navigate(-1);
    TestTrue(TEXT("The pause menu offers a separate return to title"), Mode->PauseOption(Mode->Choice()) == EDuelPause::Title);
    while (Mode->PauseOption(Mode->Choice()) != EDuelPause::Subtitles)
    {
        Mode->Navigate(-1);
    }
    Mode->Confirm();
    TestTrue(TEXT("The voice caption setting leaves the phase dialogue readable"),
        !Mode->EffortSubtitles() && Audio.bVoicePaused && Mode->CombatCaption() != nullptr);
    Mode->Confirm();
    Mode->TogglePause();
    TestTrue(TEXT("Voice and subtitles resume with the game"), Mode->EffortSubtitles() && !Audio.bVoicePaused);

    Mode->ResetEncounter();
    Mode->FighterFell(BossFighter);
    Mode->EnterArchive();
    PlayerFighter->SetActorLocation(DuelCourt::AppealPassage + FVector(0, 0, 62));
    Mode->Confirm();
    TestFalse(TEXT("The archive side passage waits for the evidence decision"), Mode->InAppeal());
    PlayerFighter->SetActorLocation(FVector(DuelCourt::ArchiveDeskX - 150, 0, 62));
    Mode->Confirm();
    Mode->ChooseEnding(EDuelEnding::CallWitness);
    Mode->Confirm();
    const EDuelCharacter FirstBoss = BossFighter->SelectedCharacter();
    PlayerFighter->SetActorLocation(DuelCourt::AppealPassage + FVector(0, 0, 62));
    Mode->Confirm();
    const FVector EndOfPassage(7095, -151, 267);
    TestTrue(TEXT("The evidence opens the passage without moving the player"), Mode->Outcome() == EDuelOutcome::Passage &&
        PlayerFighter->GetActorLocation().Equals(DuelCourt::AppealPassage + FVector(0, 0, 62)));
    PlayerFighter->SetActorLocation(EndOfPassage);
    Mode->Tick(0.f);
    TestTrue(TEXT("The evidence choice opens the second hearing with the other boss"),
        Mode->InAppeal() && Mode->Outcome() == EDuelOutcome::Ready && Mode->FirstHearingSeconds() > 0 &&
        Mode->Ending() == EDuelEnding::CallWitness && BossFighter->SelectedCharacter() != FirstBoss &&
        PlayerFighter->GetActorLocation().Equals(EndOfPassage, .01));
    const FVector Origin = DuelCourt::AppealOrigin;
    const auto EnterAppealGate = [&]
    {
        PlayerFighter->SetActorLocation(Origin + FVector(DuelCourt::GateX - 104, 0, 62));
        Mode->Confirm();
        PlayerFighter->SetActorLocation(Origin + FVector(DuelCourt::GateCrossX + 8, 0, 62));
        Mode->Tick(0.f);
    };
    const auto AppealPosition = [&](float PlayerX, float BossX)
    {
        PlayerFighter->SetActorLocationAndRotation(Origin + FVector(PlayerX, 0, 62), FRotator::ZeroRotator);
        BossFighter->SetActorLocationAndRotation(Origin + FVector(BossX, 0, 103), FRotator(0, 180, 0));
    };
    EnterAppealGate();
    TestTrue(TEXT("The second fog starts both fighters in the elevated court"), Mode->Outcome() == EDuelOutcome::Fighting &&
        PlayerFighter->GetActorLocation().Equals(Origin + FVector(DuelCourt::GateCrossX + 8, 0, 62), .01) &&
        BossFighter->GetActorLocation().Equals(DuelCourt::Start(true, Origin).GetLocation(), .01));
    BossFighter->SelectCharacter(EDuelCharacter::Hiro);
    AppealPosition(440.f, 0.f);
    BossFighter->StartMove(EDuelMove::RakingClaws);
    const auto Claws = DuelCourt::Timing(EDuelMove::RakingClaws, false);
    BossFighter->TickMove(Claws.Windup + .01f);
    TestTrue(TEXT("The first claw misses from outside its reach"), Near(PlayerFighter->HealthRatio(), 1.f));
    BossFighter->TickMove(DuelBossDefense::RakingBeat);
    TestTrue(TEXT("The later claw can catch a target the opening swipe misses"),
        Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth - BossDamage(36.f)));

    for (const float Frame : {1.f / 30.f, 1.f / 60.f})
    {
        Mode->ResetEncounter();
        AppealPosition(235.f, 0.f);
        BossFighter->StartMove(EDuelMove::RakingClaws);
        BossFighter->TickMove(Claws.Windup + .001f);
        TestTrue(TEXT("The opening claw inflicts hitstun"), PlayerFighter->Move == EDuelMove::Hurt &&
            Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth - BossDamage(32.f)));
        float AfterHit = 0.f;
        for (; AfterHit < .12f; AfterHit += Frame)
        {
            PlayerFighter->TickMove(Frame);
            BossFighter->TickMove(Frame);
        }
        PlayerFighter->Right(1.f);
        PlayerFighter->PressShift();
        PlayerFighter->ReleaseShift();
        TestTrue(TEXT("A tap during hitstun buffers the roll without interrupting the recoil"),
            PlayerFighter->Move == EDuelMove::Hurt && PlayerFighter->QueuedMove == EDuelMove::Roll);
        PlayerFighter->Right(0.f);
        for (; AfterHit < DuelBossDefense::RakingBeat; AfterHit += Frame)
        {
            PlayerFighter->TickMove(Frame);
            BossFighter->TickMove(Frame);
        }
        TestTrue(FString::Printf(TEXT("Buffered roll opens before the second claw at %d fps"), FMath::RoundToInt(1.f / Frame)),
            PlayerFighter->InRollWindow() && PlayerFighter->ActionDirection.Equals(FVector::RightVector, .01) &&
            PlayerFighter->GetActorForwardVector().Equals(FVector::RightVector, .01) &&
            Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth - BossDamage(32.f)) &&
            FVector::Dist2D(PlayerFighter->GetActorLocation(), BossFighter->GetActorLocation()) < 325.f);
    }

    Mode->ResetEncounter();
    BossFighter->SelectCharacter(EDuelCharacter::Hiro);
    AppealPosition(747.f, 0.f);
    AdvancePhase(2);
    BossFighter->StartMove(EDuelMove::ShadowVolley);
    const auto Volley = DuelCourt::Timing(EDuelMove::ShadowVolley, true);
    TestTrue(TEXT("The first shadow lane is shown before launch"), BossFighter->MapWaves.Num() == 3 &&
        BossFighter->MapWaves[0].Marks.Num() == 1 && Audio.Orbs.IsEmpty());
    const FVector LockedAxis = BossFighter->MapWaves[0].Axis;
    PlayerFighter->AddActorWorldOffset(FVector(0, 192, 0));
    BossFighter->TickMove(Volley.Windup + .01f);
    Audio.TickOrbs(.42f);
    TestTrue(TEXT("Stepping off the shown lane avoids the projectile"), Near(PlayerFighter->HealthRatio(), 1.f) &&
        BossFighter->MapWaves[0].Axis.Equals(LockedAxis) && !Audio.Orbs.IsEmpty() && BossFighter->MapWaves[1].Marks.IsEmpty());
    Audio.TickOrbs(1.2f);
    Audio.TickOrbs(.01f);
    BossFighter->TickMove(.01f);
    TestTrue(TEXT("The next shadow lane samples the new position after the projectile expires"),
        BossFighter->MapWaves[1].Marks.Num() == 1 && !BossFighter->MapWaves[1].Axis.Equals(LockedAxis, .02));
    Mode->ResetEncounter();
    AppealPosition(747.f, 0.f);
    AdvancePhase(2);
    AdvancePhase(3);
    BossFighter->StartMove(EDuelMove::ShadowVolley);
    TestTrue(TEXT("Hiro's final phase adds a wider fourth lane"), BossFighter->MapWaves.Num() == 4 && BossFighter->MapWaves[0].Reach == 114.f);

    Mode->ResetEncounter();
    AppealPosition(205.f, 0.f);
    BossFighter->StartMove(EDuelMove::FeintCounter);
    BossFighter->TickMove(.25f);
    const FVector GuardedContact = BossFighter->GetActorLocation() + FVector(86, 0, 28);
    TestFalse(TEXT("Hiro's bait deflects an early frontal blade"),
        BossFighter->ReceiveDuelDamage(72.f, PlayerFighter->GetActorLocation(), false, &GuardedContact));
    TestTrue(TEXT("The feint advances to a readable counter windup"), BossFighter->bFeintTriggered && BossFighter->MoveAge > .6f);

    Mode->ResetEncounter();
    AppealPosition(550.f, 0.f);
    AdvancePhase(2);
    BossFighter->StartMove(EDuelMove::CrossAmbush);
    const auto Ambush = DuelCourt::Timing(EDuelMove::CrossAmbush, true);
    const FVector SidePoint = BossFighter->SealCenters[1];
    const FVector EndPoint = BossFighter->SealCenters[2];
    PlayerFighter->SetActorLocation(Origin + FVector(180, -875, 62));
    BossFighter->TickMove(Ambush.Windup + .24f);
    TestTrue(TEXT("Hiro physically lunges to the first announced endpoint"),
        FVector::Dist2D(BossFighter->GetActorLocation(), SidePoint) < 1.f && Near(PlayerFighter->HealthRatio(), 1.f));
    BossFighter->TickMove(Ambush.Windup + DuelBossDefense::AmbushSecondStart + DuelBossDefense::AmbushLunge - BossFighter->MoveAge + .01f);
    TestTrue(TEXT("The second lunge crosses back along its own route"), FVector::Dist2D(BossFighter->GetActorLocation(), EndPoint) < 1.f);

    Mode->ResetEncounter();
    BossFighter->SelectCharacter(EDuelCharacter::Ema);
    AppealPosition(225.f, 0.f);
    AdvancePhase(2);
    BossFighter->StartMove(EDuelMove::Wingfall);
    const auto Fall = DuelCourt::Timing(EDuelMove::Wingfall, true);
    TestTrue(TEXT("Wingfall has distinct departure and landing sites"),
        FVector::Dist2D(BossFighter->CastOrigin, BossFighter->SealCenters[0]) > 320.f);
    BossFighter->TickMove(Fall.Windup + .01f);
    TestTrue(TEXT("The takeoff circle hits before the dive"), Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth - BossDamage(48.f)));
    const FVector WingfallLanding = BossFighter->CastOrigin;
    PlayerFighter->SetActorLocation(FVector(WingfallLanding.X, WingfallLanding.Y, Origin.Z + 62.f));
    BossFighter->TickMove(Fall.Active);
    TestTrue(TEXT("Wingfall lands on its mark and strikes separately"),
        FVector::Dist2D(BossFighter->GetActorLocation(), WingfallLanding) < 3.f &&
        Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth - BossDamage(48.f + 70.f)));

    Mode->ResetEncounter();
    AppealPosition(440.f, 0.f);
    AdvancePhase(2);
    BossFighter->StartMove(EDuelMove::WrittenVerdict);
    PlayerFighter->TickCamera(.18f);
    TestTrue(TEXT("Visible attack lanes open the camera's ground framing"), PlayerFighter->HazardFraming > .55f &&
        PlayerFighter->CameraBoom->TargetOffset.Z > 132.f);
    const auto InkTiming = DuelCourt::Timing(EDuelMove::WrittenVerdict, true);
    BossFighter->TickMove(InkTiming.Windup + .01f);
    TestTrue(TEXT("The lane between the written lines remains safe for the first stroke"), Near(PlayerFighter->HealthRatio(), 1.f));
    BossFighter->TickMove(.76f);
    TestTrue(TEXT("The later diagonal stroke crosses the formerly safe lane"), Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth - BossDamage(58.f)));

    Mode->ResetEncounter();
    AppealPosition(530.f, 0.f);
    AdvancePhase(2);
    BossFighter->StartMove(EDuelMove::BoneProcession);
    PlayerFighter->SetActorLocation(Origin + FVector(530.f, 0, 120.f));
    BossFighter->TickMove(1.21f);
    TestTrue(TEXT("Jumping over the traveling feather wave avoids it"), Near(PlayerFighter->HealthRatio(), 1.f));

    Mode->ResetEncounter();
    BossFighter->SelectCharacter(EDuelCharacter::Ema);
    AppealPosition(240.f, 0.f);
    AdvancePhase(2);
    BossFighter->StartMove(EDuelMove::HaloBreak);
    const auto Halo = DuelCourt::Timing(EDuelMove::HaloBreak, true);
    TestTrue(TEXT("An area attack marks the actual second floor"), Near(BossFighter->CastOrigin.Z, Origin.Z + 8.f));
    BossFighter->TickMove(Halo.Windup + .01f);
    TestTrue(TEXT("The first halo leaves the inner disk safe"), Near(PlayerFighter->HealthRatio(), 1.f));
    BossFighter->TickMove(.69f);
    TestTrue(TEXT("The warned inner disk detonates later"), Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth - BossDamage(54.f)));
    Mode->ResetEncounter();
    AppealPosition(630.f, 0.f);
    AdvancePhase(2);
    BossFighter->StartMove(EDuelMove::HaloBreak);
    BossFighter->TickMove(Halo.Windup + .02f);
    TestTrue(TEXT("The marked outer band is hit before the center"), Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth - BossDamage(65.f)));

    Mode->ResetEncounter();
    PlayerFighter->SelectCharacter(EDuelCharacter::Ema);
    AppealPosition(0.f, 818.f);
    PlayerFighter->ArtSelection = EDuelArt::EmaVotive;
    PlayerFighter->Crimson();
    PlayerFighter->TickMove(DuelCourt::Timing(EDuelMove::Crimson, false, EDuelWeapon::Rapier, EDuelArt::EmaVotive).Windup + .57f);
    TestTrue(TEXT("The last votive seal has its own windup and elevation"), Near(BossFighter->HealthRatio(), 1.f) &&
        Near(PlayerFighter->SealCenters[2].Z, Origin.Z + 8.f));
    PlayerFighter->TickMove(.06f);
    TestTrue(TEXT("The last votive seal hits inside its own disk"), Near(BossFighter->Health, DuelCourt::BossMaxHealth - 91.f));
    Mode->ResetEncounter();
    PlayerFighter->SelectCharacter(EDuelCharacter::Hiro);
    AppealPosition(0.f, 172.f);
    PlayerFighter->ArtSelection = EDuelArt::HiroThreads;
    PlayerFighter->Crimson();
    PlayerFighter->TickMove(.57f);
    Audio.TickOrbs(.15f);
    TestTrue(TEXT("At close range all three threads can connect"),
        Near(BossFighter->Health, DuelCourt::BossMaxHealth - 168.f));
    Mode->ResetEncounter();
    AppealPosition(0.f, 570.f);
    PlayerFighter->Crimson();
    PlayerFighter->TickMove(.57f);
    Audio.TickOrbs(.42f);
    TestTrue(TEXT("Farther away the two outside threads can miss"),
        Near(BossFighter->Health, DuelCourt::BossMaxHealth - 56.f));

    Mode->ResetEncounter();
    AppealPosition(0.f, 446.f);
    PlayerFighter->ArtSelection = EDuelArt::HiroReprise;
    PlayerFighter->Crimson();
    PlayerFighter->TickMove(.48f);
    Audio.TickOrbs(.28f);
    TestTrue(TEXT("Hiro retreats while a returning blade crosses the former position"),
        Near(PlayerFighter->GetActorLocation().X, Origin.X - 270.f) &&
        Near(BossFighter->Health, DuelCourt::BossMaxHealth - 104.f));
    for (const float Range : {201.f, 480.f, 661.f, 745.f})
    {
        Mode->ResetEncounter();
        AppealPosition(0.f, Range);
        PlayerFighter->SelectCharacter(EDuelCharacter::Ema);
        PlayerFighter->ArtSelection = EDuelArt::EmaAbsolution;
        PlayerFighter->Crimson();
        PlayerFighter->TickMove(.77f);
        TestTrue(TEXT("The crown deals damage in its ring and leaves the inner disk clear"),
            Near(BossFighter->Health, DuelCourt::BossMaxHealth - (Range > 242.f && Range < 705.f ? 181.f : 0.f)));
    }

    Mode->ResetEncounter();
    AppealPosition(0.f, 400.f);
    PlayerFighter->ArtSelection = EDuelArt::EmaAegis;
    PlayerFighter->Crimson();
    PlayerFighter->TickMove(.34f);
    TestTrue(TEXT("Aegis spends focus and protects against an unblockable hit"),
        Near(PlayerFighter->Focus, DuelCourt::MaxFocus - 25.f) && PlayerFighter->HasAegis() &&
        PlayerFighter->ReceiveDuelDamage(40.f, BossFighter->GetActorLocation(), false) &&
        Near(PlayerFighter->Health, DuelCourt::PlayerMaxHealth - BossDamage(16.f)));
    TestFalse(TEXT("Aegis is consumed after one hit"), PlayerFighter->HasAegis());

    Mode->ResetEncounter();
    AppealPosition(0.f, 440.f);
    PlayerFighter->SelectCharacter(EDuelCharacter::Hiro);
    PlayerFighter->ArtSelection = EDuelArt::HiroCrossThread;
    PlayerFighter->Crimson();
    PlayerFighter->TickMove(.56f);
    Audio.TickOrbs(.25f);
    TestTrue(TEXT("The returning thread hits from the far side of the target"),
        Near(BossFighter->Health, DuelCourt::BossMaxHealth - 148.f) &&
        PlayerFighter->CastOrigin.X > BossFighter->GetActorLocation().X && Near(PlayerFighter->Focus, DuelCourt::MaxFocus - 25.f));

    Mode->ResetEncounter();
    PlayerFighter->ReceiveDuelDamage(1000.f, BossFighter->GetActorLocation());
    Mode->Confirm();
    TestTrue(TEXT("Defeat in the second hearing returns to its own checkpoint with the decision intact"),
        Mode->InAppeal() && Mode->Ending() == EDuelEnding::CallWitness && Mode->Outcome() == EDuelOutcome::Ready &&
        PlayerFighter->GetActorLocation().Equals(DuelCourt::AppealCheckpoint().GetLocation(), .01));
    EnterAppealGate();
    Mode->FighterFell(BossFighter);
    PlayerFighter->SetActorLocation(Origin + FVector(DuelCourt::ArchiveEntryX - 50.f, 0, 62));
    Mode->Tick(0.f);
    for (const FVector& Mark : DuelCourt::RitualMarks)
    {
        PlayerFighter->SetActorLocation(Origin + Mark + FVector(-80, 0, 62));
        Mode->Confirm();
    }
    PlayerFighter->SetActorLocation(Origin + FVector(DuelCourt::VerdictDeskX - 160.f, 0, DuelCourt::VerdictRise + 62.f));
    Mode->Confirm();
    TestTrue(TEXT("The second victory opens the final ruling with both fight times"), Mode->ReadingArchive() &&
        Mode->InAppeal() && Mode->FirstHearingSeconds() > 0 && Mode->DuelSeconds() > 0);
    Mode->Confirm();
    TestTrue(TEXT("The final ruling can be closed and reread"), !Mode->ReadingArchive() && Mode->NearArchiveDesk());
    Mode->bAppeal = false;
    Mode->Arena->SetAppeal(false);
    Mode->ReturnToCheckpoint();
    return true;
}

void FDuelLiveCombat::Begin(ADuelMode& Mode, int32 Seed)
{
    *this = FDuelLiveCombat{Mode.GetWorld()->GetTimeSeconds()};
    Health = Mode.BossFighter->Health;
    PlayerHealth = Mode.PlayerFighter->Health;
    Mode.BossFighter->BossRandom.Initialize(Seed);
}

void FDuelLiveCombat::Observe(ADuelMode& Mode, int32 Hearing)
{
    const ADuelMode* Trial = &Mode;
    const ADuelFighter* Player = Mode.PlayerFighter.Get();
    const ADuelFighter* Boss = Mode.BossFighter.Get();
    PeakPhase = FMath::Max(PeakPhase, Boss->Phase());
    Phases |= 1u << FMath::Clamp(Boss->Phase() - 1, 0, 2);
    if (Trial->Outcome() == EDuelOutcome::Victory || Boss->Health + UE_KINDA_SMALL_NUMBER < Health)
    {
        ++Hits;
        Health = Trial->Outcome() == EDuelOutcome::Victory ? 0.f : Boss->Health;
    }
    if (Player->Health + UE_KINDA_SMALL_NUMBER < PlayerHealth)
    {
        const FVector TowardBoss = (Boss->GetActorLocation() - Player->GetActorLocation()).GetSafeNormal2D();
        UE_LOG(LogTemp, Display, TEXT("Live duel hit: hearing=%d boss_move=%d boss_age=%.2f previous_player_move=%d range=%.0f facing=%.2f HP=%.1f stamina=%.1f"),
            Hearing, static_cast<int32>(Boss->Move), Boss->MoveAge, static_cast<int32>(PreviousPlayerMove),
            FVector::Dist2D(Player->GetActorLocation(), Boss->GetActorLocation()),
            FVector::DotProduct(Player->GetActorForwardVector(), TowardBoss), Player->Health, Player->Stamina);
    }
    PlayerHealth = Player->Health;
    PreviousPlayerMove = Player->Move;
}

void FDuelLiveCombat::Drive(ADuelMode& Mode, double Now, int32 Hearing)
{
    ADuelMode* Trial = &Mode;
    ADuelFighter* Player = Mode.PlayerFighter.Get();
    ADuelFighter* Boss = Mode.BossFighter.Get();
    if (Boss->Move != Observed)
    {
        Observed = Boss->Move;
        if (Player->Move == EDuelMove::Parry && Boss->IsStaggered())
        {
            ++Parried;
        }
        if (Boss->Move >= EDuelMove::Paw && Boss->Move != EDuelMove::Awaken && Boss->Move != EDuelMove::Eclipse && !Boss->IsStaggered())
        {
            Moves.Add(Boss->Move);
        }
    }
    const FVector Offset = Boss->GetActorLocation() - Player->GetActorLocation();
    const double Range = Offset.Size2D();
    const FVector PlayerAt = Player->GetActorLocation();
    const FVector Toward = Offset.GetSafeNormal2D();
    const FVector Side = FVector::CrossProduct(FVector::UpVector, Toward);
    const EDuelMove Move = Boss->Move;
    const auto CloseHits = DuelBossDefense::CloseHits(Move);
    const bool bCloseChain = !CloseHits.IsEmpty();
    const bool bIceRow = Move == EDuelMove::IcicleRow;
    const bool bSignature = DuelBossDefense::IsSignature(Move);
    const bool bAgainstHiro = Boss->SelectedCharacter() == EDuelCharacter::Hiro;
    const auto Timing = DuelCourt::Timing(Boss->Move, Boss->IsAwakened());
    const float Windup = Timing.Windup;
    const float Age = Boss->MoveAge;
    const int32 Stage = Boss->Phase() - 1;
    float Impact = TNumericLimits<float>::Max();
    bool bParryable = false;
    bool bJumpable = false;
    FVector Escape = Side;
    const auto Register = [&](float When, bool bThreatens, const FVector& Away, bool bParry = false, bool bJump = false)
    {
        if (bThreatens && When > Age && When < Impact)
        {
            Impact = When;
            Escape = Away.GetSafeNormal2D(UE_SMALL_NUMBER, Side);
            bParryable = bParry;
            bJumpable = bJump;
        }
    };
    const auto Circle = [&](const FVector& Center, float Radius, float When, bool bJump = false)
    {
        const FVector Away = PlayerAt - Center;
        Register(When, Away.SizeSquared2D() <= FMath::Square(Radius + 16.f), Away, false, bJump);
    };
    const auto Lane = [&](const FVector& Center, const FVector& Axis, double HalfLength, float Width, float When, bool bParry = false)
    {
        const FVector Across = FVector::CrossProduct(FVector::UpVector, Axis);
        const FVector FromCenter = PlayerAt - Center;
        const double AcrossDistance = FVector::DotProduct(FromCenter, Across);
        Register(When, FMath::Abs(AcrossDistance) <= Width + 12.f &&
            FMath::Abs(FVector::DotProduct(FromCenter, Axis)) <= HalfLength, Across * (AcrossDistance < 0 ? -1 : 1), bParry);
    };
    const auto ForwardStrike = [&](float Reach, float When)
    {
        const bool bAhead = FVector::DotProduct(Boss->ActionDirection, -Toward) >= -.04f;
        Register(When, bAhead && Range <= Reach + 16.f, -Toward, true);
    };
    const auto SweepAge = [](float Fraction)
    {
        // Invert the smooth-step used by the rush trajectory.
        const float T = FMath::Clamp(Fraction, 0.f, 1.f);
        return .5f - FMath::Sin(FMath::Asin(1.f - 2.f * T) / 3.f);
    };
    switch (Move)
    {
    case EDuelMove::Pillar:
    {
        constexpr float Reach[] = {455, 511, 568};
        Circle(Boss->GetActorLocation(), Reach[Stage], Windup);
        break;
    }
    case EDuelMove::Leap:
    case EDuelMove::FlankLeap:
    case EDuelMove::Wingfall:
    {
        constexpr float Leap[] = {232, 264, 301};
        constexpr float Flank[] = {266, 291, 318};
        constexpr float Landing[] = {0, 344, 403};
        constexpr float Takeoff[] = {0, 321, 398};
        if (Move == EDuelMove::Wingfall)
        {
            Circle(Boss->SealCenters[0], Takeoff[Stage], Windup);
        }
        Circle(Boss->CastOrigin, Move == EDuelMove::Leap ? Leap[Stage] : Move == EDuelMove::FlankLeap ? Flank[Stage] : Landing[Stage],
            Windup + Timing.Active * .94f);
        break;
    }
    case EDuelMove::Seals:
        for (int32 I = 0; I < Boss->SealCenters.Num(); ++I)
        {
            Circle(Boss->SealCenters[I], Stage == 2 ? 177.f : 152.f, Windup + DuelBossDefense::SealBeat * I);
        }
        break;
    case EDuelMove::WingSweep:
    case EDuelMove::Paw:
    case EDuelMove::FeintCounter:
    {
        constexpr float Paw[] = {276, 306, 349};
        constexpr float Wing[] = {365, 410, 462};
        const float TravelLeft = 1.f - FMath::SmoothStep(0.f, 1.f, (Age - Windup) / Timing.Active);
        const float Reach = Move == EDuelMove::WingSweep ? Wing[Stage] : Move == EDuelMove::Paw ? Paw[Stage] : 292.f + Stage * 46.f;
        ForwardStrike(Reach + Boss->Travel * TravelLeft * .66f, Windup + Timing.Active * .61f);
        break;
    }
    case EDuelMove::RakingClaws:
        ForwardStrike(290.f + Stage * 45.f + Boss->Travel * .32f, Windup);
        ForwardStrike(325.f + Stage * 51.f + Boss->Travel * .35f, Windup + DuelBossDefense::RakingBeat);
        break;
    case EDuelMove::Storm:
    case EDuelMove::InwardReap:
    {
        const float Radius = Move == EDuelMove::Storm ? DuelCourt::StormRadius[Stage] : DuelCourt::ReapOuter;
        const double Distance = Move == EDuelMove::Storm ? Range : FVector::Dist2D(PlayerAt, Boss->CastOrigin);
        const double Progress = Move == EDuelMove::Storm ? (Distance - 102) / (Radius - 70.f) :
            (Radius - Distance - 27.f) / (Radius - DuelCourt::ReapInner);
        Register(Windup + Timing.Active * FMath::Clamp(static_cast<float>(Progress), 0.f, 1.f),
            !Boss->bHitDelivered && Distance <= Radius, -Toward, false, true);
        break;
    }
    case EDuelMove::BoneProcession:
    {
        const float Outer = Stage == 2 ? 1538.f : 1314.f;
        const double Distance = FVector::Dist2D(PlayerAt, Boss->CastOrigin);
        for (int32 I = 0; I < (Stage == 2 ? 2 : 1); ++I)
        {
            const float Inner = I == 0 ? 194.f : 336.f;
            const float Progress = FMath::Clamp(static_cast<float>((Distance - Inner - 39.f) / (Outer - Inner)), 0.f, 1.f);
            Register(Windup + DuelBossDefense::BoneSecondStart * I + DuelBossDefense::BoneSweep * Progress, !Boss->BoneWaveHits[I] && Distance >= Inner && Distance <= Outer,
                PlayerAt - Boss->CastOrigin, false, true);
        }
        break;
    }
    case EDuelMove::Cross:
        Lane(Boss->CastOrigin, Boss->ActionDirection, 1190, 92.f, Windup);
        Lane(Boss->CastOrigin, FVector::CrossProduct(FVector::UpVector, Boss->ActionDirection), 1190, 92.f, Windup + DuelBossDefense::CrossBeat);
        break;
    case EDuelMove::WrittenVerdict:
    {
        const FVector Across = FVector::CrossProduct(FVector::UpVector, Boss->CastAxis);
        const float Gap = Stage == 2 ? 211.f : 258.f;
        const float Width = Stage == 2 ? 136.f : 108.f;
        Lane(Boss->CastOrigin + Gap * Across, Boss->CastAxis, 1480, Width, Windup);
        Lane(Boss->CastOrigin - Gap * Across, Boss->CastAxis, 1480, Width, Windup);
        Lane(Boss->CastOrigin, Boss->CastAxis.RotateAngleAxis(72.f, FVector::UpVector), 1310, Width, Windup + .76f);
        break;
    }
    case EDuelMove::HaloBreak:
    {
        const FVector Away = PlayerAt - Boss->CastOrigin;
        const double Distance = Away.Size2D();
        const float Inner = Stage == 2 ? 380.f : 450.f;
        const float Outer = Stage == 2 ? 1200.f : 1030.f;
        Register(Windup, Distance >= Inner - 14.f && Distance <= Outer + 14.f, Distance - Inner < Outer - Distance ? -Away : Away);
        Circle(Boss->CastOrigin, Stage == 2 ? 505.f : 470.f, Windup + .68f);
        break;
    }
    case EDuelMove::Reversal:
    {
        const double Reach = DuelCourt::ReversalReach(Boss->Phase());
        const float StepEnd = Windup + DuelCourt::ReversalStepTime;
        if (Age >= StepEnd)
        {
            Lane(Boss->CastOrigin + Boss->CastAxis * (Reach * .5), Boss->CastAxis, Reach * .5,
                DuelCourt::ReversalHalfWidth(Boss->Phase()), StepEnd + DuelCourt::ReversalCueTime, true);
        }
        break;
    }
    case EDuelMove::TalonRush:
    {
        constexpr float Width[] = {113.f, 126.f, 142.f};
        const double Along = FVector::DotProduct(PlayerAt - Boss->CastOrigin, Boss->ActionDirection) + Boss->RushHalfLength - 56.f;
        const float When = Windup + Timing.Active * SweepAge(static_cast<float>((Along - 140.f) / FMath::Max(1.f, Boss->Travel)));
        Lane(Boss->CastOrigin, Boss->ActionDirection, Boss->RushHalfLength, Width[Stage], When, true);
        break;
    }
    case EDuelMove::ShadowSweep:
    {
        const float Arc = DuelCourt::TailSweepArc(Boss->Phase());
        const FVector Away = PlayerAt - Boss->CastOrigin;
        const float Angle = FMath::FindDeltaAngleDegrees(Boss->CastAxis.Rotation().Yaw, Away.Rotation().Yaw);
        const float Fraction = (Angle * Boss->OrbitSide + Arc - 9.f) / (Arc * 2.f);
        Register(Windup + Timing.Active * FMath::Clamp(Fraction, 0.f, 1.f),
            !Boss->bHitDelivered && Away.Size2D() >= DuelCourt::TailSweepInner && Away.Size2D() <= DuelCourt::TailSweepReach(Boss->Phase()) &&
            FMath::Abs(Angle) <= Arc, Away, true, true);
        break;
    }
    case EDuelMove::CrossAmbush:
        for (int32 I = 0; I < 2; ++I)
        {
            const FVector From = Boss->SealCenters[I];
            const FVector Path = Boss->SealCenters[I + 1] - From;
            const FVector Axis = Path.GetSafeNormal2D();
            const float Along = FVector::DotProduct(PlayerAt - From, Axis);
            Lane(From + (Path + Axis * 118.f) * .5, Axis, (Path.Size2D() + 118.0) * .5, Stage == 2 ? 139.f : 109.f,
                Windup + DuelBossDefense::AmbushSecondStart * I + DuelBossDefense::AmbushLunge * SweepAge((Along - 118.f) / FMath::Max(1.0, Path.Size2D())), true);
        }
        break;
    case EDuelMove::ShadowSnare:
        for (const auto& Snare : Boss->ShadowSnares)
        {
            if (Snare.bTriggered)
            {
                Circle(Snare.Strike, DuelCourt::SnareRadius(Boss->Phase()), Snare.StrikeAt);
            }
        }
        break;
    case EDuelMove::ArchiveCollapse:
    {
        const FVector Away = PlayerAt - Boss->CastOrigin;
        const double Radius = Away.Size2D();
        if (!Boss->BoneWaveHits[0] && Radius >= DuelCourt::ArchiveInner && Radius <= DuelCourt::ArchiveOuter)
        {
            const double Progress = (DuelCourt::ArchiveOuter - Radius) /
                (DuelCourt::ArchiveOuter - DuelCourt::ArchiveInner);
            Register(Windup + DuelCourt::ArchiveSweepTime * Progress, true, Away, false, true);
        }
        if (Boss->Phase() == 3 && !Boss->BoneWaveHits[1] && Radius >= 70.f && Radius <= DuelCourt::ArchiveOuter)
        {
            const double Progress = (Radius - 70.f) / (DuelCourt::ArchiveOuter - 70.f);
            Register(Windup + DuelCourt::ArchiveReturnAt + DuelCourt::ArchiveReturnTime * Progress, true, -Away, false, true);
        }
        break;
    }
    default:
        break;
    }
    float NextCloseContact = TNumericLimits<float>::Max();
    for (int32 I = 0; I < Boss->MapWaves.Num(); ++I)
    {
        const ADuelFighter::FMapWave& Wave = Boss->MapWaves[I];
        if (bSignature)
        {
            if (Wave.Marks.Num() != 2 || Wave.FlightTime <= 0.f || Wave.bHitDelivered ||
                (Move == EDuelMove::OssuaryShears && Boss->bHitDelivered) ||
                (Move == EDuelMove::MirrorRicochet && I >= 2 && Boss->MapWaves[2].bHitDelivered))
                continue;
            const FVector Start = Wave.Marks[0];
            const FVector Route = Wave.Marks[1] - Start;
            const FVector Axis = Route.GetSafeNormal2D();
            const double Length = Route.Size2D();
            const double Width = Wave.Reach + Player->GetCapsuleComponent()->GetScaledCapsuleRadius();
            const double Along = FVector::DotProduct(PlayerAt - Start, Axis);
            const float When = Wave.ImpactTime + Wave.FlightTime *
                FMath::Clamp((Along - Width) / FMath::Max(1., Length), 0., 1.);
            const bool bPhysical = (Move == EDuelMove::HoundShade && I == 1) ||
                (Move == EDuelMove::MirrorRiposte && !Boss->bFeintTriggered);
            Lane(Start + Route * .5, Axis, Length * .5 + Width, Width, When, bPhysical);
            continue;
        }
        if (bCloseChain)
        {
            if (Wave.bHitDelivered) continue;
            NextCloseContact = FMath::Min(NextCloseContact, Wave.ImpactTime);
            if (Wave.Marks.Num() != 2) continue;
            // The two markers are footwork endpoints. The threat is one arc at
            // the marked landing position, with the facing fixed at warning time.
            const FVector FromLanding = PlayerAt - Wave.Marks[1];
            const float HalfArc = FMath::Min(180.f, CloseHits[I].HalfArc + 4.f);
            const bool bInArc = FromLanding.SizeSquared2D() <= FMath::Square(Wave.Reach + 14.f) &&
                FVector::DotProduct(Wave.Axis, FromLanding.GetSafeNormal2D()) >= FMath::Cos(FMath::DegreesToRadians(HalfArc));
            const FVector Across = FVector::CrossProduct(FVector::UpVector, Wave.Axis);
            const float Flank = FVector::DotProduct(FromLanding, Across) < 0.0 ? -1.f : 1.f;
            Register(Wave.ImpactTime, bInArc, Across * Flank - Toward * .18f, true);
            continue;
        }
        if (bIceRow)
        {
            const USceneComponent* Projectile = Wave.Projectile.Get();
            if (Wave.Marks.Num() != 2 || (Wave.bHitDelivered && !Projectile)) continue;
            const FVector From = Projectile ? Projectile->GetComponentLocation() : Wave.Marks[0];
            const double Left = Wave.HalfLength * 2.0 - FVector::DotProduct(From - Wave.Marks[0], Wave.Axis);
            if (Left <= 0.0) continue;
            const double Along = FVector::DotProduct(PlayerAt - From, Wave.Axis);
            const float Arrival = (Projectile ? Age : Wave.ImpactTime) +
                FMath::Max(0.0, Along - Wave.Reach) / DuelBossDefense::IcicleSpeed;
            Lane(From + Wave.Axis * Left * .5, Wave.Axis, Left * .5, Wave.Reach, Arrival);
            continue;
        }
        if (Wave.ProjectileRetiredAt >= 0.f || (Wave.bHitDelivered && DuelBossDefense::HasProjectileCadence(Move) &&
            !Wave.Projectile.IsValid()))
        {
            continue;
        }
        if (Move == EDuelMove::MirrorEdict)
        {
            // The mirror front has two moving edges and cannot use a stationary lane prediction.
            continue;
        }
        for (const FVector& Mark : Wave.Marks)
        {
            if (Move == EDuelMove::PrismPursuit)
            {
                const double Along = FVector::DotProduct(PlayerAt - Mark, Wave.Axis);
                Lane(Mark + Wave.Axis * Wave.HalfLength, Wave.Axis, Wave.HalfLength, Wave.Reach,
                    Wave.ImpactTime + FMath::Max(0.0, Along - Wave.Reach) / DuelCourt::PrismSpeed);
                continue;
            }
            if (Move == EDuelMove::AtlasRain || Move == EDuelMove::Pursuit || Move == EDuelMove::Tribunal || Move == EDuelMove::BrokenRecord)
            {
                Circle(Mark, Wave.Reach, Wave.ImpactTime);
            }
            else
            {
                const double Travel = FVector::DotProduct(PlayerAt - Mark, Wave.Axis) + Wave.HalfLength;
                const float When = Move == EDuelMove::ShadowVolley ?
                    Wave.ImpactTime + FMath::Max(0.0, Travel - Wave.Reach) / DuelBossDefense::ShadowProjectileSpeed(Boss->Phase()) : Wave.ImpactTime;
                Lane(Mark, Wave.Axis, Wave.HalfLength, Wave.Reach, When);
            }
        }
    }
    const float ToImpact = Impact - Age;
    const bool bThreat = Move >= EDuelMove::Paw && Move != EDuelMove::Awaken && Move != EDuelMove::Eclipse;
    const bool bIdle = Player->Move == EDuelMove::Idle;
    const bool bIncoming = ToImpact <= .23f && ToImpact >= .024f;
    const bool bCanFace = FVector::DotProduct(Player->GetActorForwardVector(), Toward) > .36f;
    const bool bCanGuard = bIdle && bCanFace && Player->Stamina >= DuelCourt::ParryStaminaCost &&
        DuelCourt::IsShield(Player->EquippedWeapon(EDuelHand::Left));
    if (Now < ShieldUntil && Player->IsGuarding(EDuelHand::Left) && (!bIncoming || bParryable))
    {
        return;
    }
    if (bIncoming && Now >= NextReaction && Player->GetCharacterMovement()->IsMovingOnGround())
    {
        if (bIdle && bJumpable)
        {
            Player->StartJump();
            NextReaction = Now + .35;
            return;
        }
        if (bCanGuard && bParryable && ToImpact <= .105f)
        {
            Player->PressLeftHand();
            ShieldUntil = Now + ToImpact + .07;
            return;
        }
        if ((!bParryable || !bCanGuard) && ToImpact <= .155f && Player->Stamina > DuelCourt::Timing(EDuelMove::Roll, false).Cost * .5f &&
            Player->Move != EDuelMove::Roll && Player->Move != EDuelMove::Backstep)
        {
            const FVector Step = PlayerAt + Escape * DuelCourt::RollDistance - Trial->Arena->CourtOrigin();
            if (FMath::Abs(Step.X) > DuelCourt::HalfLength || FMath::Abs(Step.Y) > DuelCourt::HalfWidth)
            {
                Escape = -Escape;
            }
            const FVector Forward = FRotator(0, Player->ViewYaw, 0).Vector();
            const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
            Player->Forward(FVector::DotProduct(Forward, Escape));
            Player->Right(FVector::DotProduct(Right, Escape));
            Player->PressShift();
            Player->ReleaseShift();
            Player->Forward(0.f);
            Player->Right(0.f);
            if (Player->Move == EDuelMove::Roll || Player->QueuedMove == EDuelMove::Roll)
            {
                NextReaction = Now + .43;
                ++Dodged;
                return;
            }
        }
    }
    Player->ReleaseLeftHand();
    if (bIdle && Player->GetCharacterMovement()->IsMovingOnGround())
    {
        if (bSignature && ToImpact <= .40f && !bParryable)
        {
            const FVector Candidate = PlayerAt + Escape * 360.f - Trial->Arena->CourtOrigin();
            const bool bRoom = Trial->Arena->CourtBounds(60.).IsInside(FVector2D(Candidate));
            Player->AddMovementInput(bRoom ? Escape : -Escape);
            return;
        }
        const float RecoveryAt = Boss->MoveRecoveryAt(Timing);
        const float MoveRemaining = RecoveryAt == TNumericLimits<float>::Max() ?
            FMath::Max(DuelBossDefense::HitGap, Timing.Total() - Age) : RecoveryAt + Timing.Recovery - Age;
        const bool bCloseRecovery = bCloseChain && !Boss->MapWaves.IsEmpty() && Boss->MapWaves.Last().bHitDelivered;
        const bool bRecovering = bThreat && (bCloseRecovery || Age > RecoveryAt + .015f);
        const bool bOpening = (Move == EDuelMove::Idle && Boss->BossPause > .24f) || Boss->IsStaggered() || bRecovering;
        const bool bHeal = Player->Health <= 104.f && Player->FlaskCount() > 0;
        const bool bCommitted = Boss->IsStaggered() || (bThreat && Age > .12f);
        const float NextWindup = bRecovering ? MoveRemaining : 0.f;
        if (Hearing == 3)
        {
            const bool bShelter = Move == EDuelMove::GlassShelter && Age <= Windup + .025f;
            const bool bBell = Move == EDuelMove::SilentBell && Age < Windup + Timing.Active + .025f;
            const bool bRewind = Move == EDuelMove::BrokenRecord &&
                Age <= Windup + (Boss->MapWaves.Num() - 1) * DuelBossDefense::RecordBeat + .025f;
            if (Move == EDuelMove::VerdictTether && Age < Windup + Timing.Active &&
                (Age < Windup || Boss->bVerdictTetherHeld))
            {
                const FVector Court = Trial->Arena->CourtOrigin();
                const FVector Away = (PlayerAt - Boss->CastOrigin).GetSafeNormal2D(UE_SMALL_NUMBER, -Toward);
                const FVector Local = PlayerAt - Court;
                const FVector Ahead = Local + Away * 300.f;
                const bool bCanRunStraight = FMath::Abs(Ahead.X) < DuelCourt::HalfLength &&
                    FMath::Abs(Ahead.Y) < DuelCourt::HalfWidth;
                const FVector AlongEdge = FMath::Abs(Local.X) > DuelCourt::HalfLength - 315.f
                    ? FVector(0, FMath::Abs(Local.Y) > 735.f ? -FMath::Sign(Local.Y) :
                        FMath::Sign(PlayerAt.Y - Boss->CastOrigin.Y), 0)
                    : FVector(FMath::Sign(PlayerAt.X - Boss->CastOrigin.X), 0, 0);
                Player->AddMovementInput(bCanRunStraight ? Away : AlongEdge);
                return;
            }
            if (Move == EDuelMove::MirrorEdict && Age < Windup + Timing.Active && !Boss->MapWaves.IsEmpty())
            {
                const int32 LastWave = Boss->MapWaves.Num() - 1;
                const bool bSecond = LastWave > 0 && Age >= Boss->MapWaves[LastWave].WarnTime + .19f;
                const auto& Wave = Boss->MapWaves[bSecond ? LastWave : 0];
                const double SideOffset = Wave.Marks[0].Y - PlayerAt.Y;
                if (FMath::Abs(SideOffset) > 50.0)
                {
                    Player->AddMovementInput(FVector(0, FMath::Sign(SideOffset), 0));
                    return;
                }
            }
            const float Radius = 380.f - 42.f * Boss->Phase();
            const bool bDeepInside = FVector::Dist2D(PlayerAt, Boss->CastOrigin) < Radius - 105.f;
            if (bShelter && !bDeepInside)
            {
                if (FVector::Dist2D(PlayerAt, Boss->CastOrigin) > Radius - 54.f)
                {
                    Player->AddMovementInput((Boss->CastOrigin - PlayerAt).GetSafeNormal2D());
                }
                return;
            }
            if (bRewind)
            {
                // Walk as marks are recorded, then use the ordinary roll input for imminent hits.
                const FVector Local = PlayerAt - Trial->Arena->CourtOrigin();
                const FVector Rim = Local + Side * 340.f;
                const bool bRoom = FMath::Abs(Rim.X) < DuelCourt::HalfLength && FMath::Abs(Rim.Y) < DuelCourt::HalfWidth;
                Player->AddMovementInput(ToImpact <= .58f ? Escape : ((bRoom ? Side : -Side) + Toward * .55f).GetSafeNormal2D());
                return;
            }
            float SafeTime = Move == EDuelMove::Idle ? Boss->BossPause + .64f : MoveRemaining + .64f;
            if (Move == EDuelMove::PrismPursuit || Move == EDuelMove::ArchiveCollapse || bIceRow || bSignature)
            {
                SafeTime = FMath::Min(SafeTime, ToImpact);
                if (bSignature && Move == EDuelMove::MirrorRicochet && Age < Windup + DuelBossDefense::MirrorReturnStart)
                    SafeTime = FMath::Min(SafeTime, Windup + DuelBossDefense::MirrorReturnStart - Age);
            }
            if (bCloseChain) SafeTime = FMath::Min(SafeTime, NextCloseContact - Age);
            if ((Move == EDuelMove::PrismPursuit || bIceRow) && ToImpact <= .64f)
            {
                const FVector Rim = PlayerAt + Escape * 325.f - Trial->Arena->CourtOrigin();
                const bool bRoom = FMath::Abs(Rim.X) < DuelCourt::HalfLength && FMath::Abs(Rim.Y) < DuelCourt::HalfWidth;
                Player->AddMovementInput(bRoom ? Escape : -Escape);
                return;
            }
            if (bHeal && SafeTime >= 1.02f)
            {
                Player->Flask();
            }
            else if (bBell && Age >= Windup - .34f)
            {
                // Let movement brake before the bell. A stationary cast is safe while it rings.
                if (Range < 850.0 && Range > 290.0 && SafeTime > 1.64f &&
                    Player->Stamina >= 51.f && Player->HasFocus(Player->ActiveArt()))
                {
                    Player->Crimson();
                }
            }
            else if (Move != EDuelMove::Awaken && Move != EDuelMove::Eclipse &&
                Range < 276.0 && Player->Stamina >= 52.f && SafeTime > 1.59f)
            {
                Player->RequestMove(EDuelMove::Heavy);
            }
            else if (Move != EDuelMove::Awaken && Move != EDuelMove::Eclipse &&
                Range < 294.0 && Player->Stamina >= 44.f && SafeTime > 1.06f)
            {
                Player->Cut();
            }
            else if (!bShelter && Range > 224.0)
            {
                Player->AddMovementInput(Toward);
            }
            return;
        }
        if (bAgainstHiro)
        {
            const float CutTime = DuelCourt::Timing(EDuelMove::Cut, false, Player->EquippedWeapon()).Total();
            const float SafeTime = FMath::Min(ToImpact, MoveRemaining + .48f);
            if (bHeal && Boss->IsStaggered() && SafeTime > .95f)
            {
                Player->Flask();
            }
            else if (ToImpact <= .52f && !bJumpable && !bParryable)
            {
                Player->AddMovementInput(Escape);
            }
            else if (Boss->IsStaggered() && Range < 231.0 && Player->Stamina >= 35.f && SafeTime > 1.24f)
            {
                Player->RequestMove(EDuelMove::Heavy);
            }
            else if (Range < 237.0 && Player->Stamina >= 34.f && SafeTime > CutTime + .09f &&
                (Boss->IsStaggered() || bRecovering))
            {
                Player->Cut();
            }
            else if (Range > 208.0)
            {
                Player->AddMovementInput(Toward);
            }
            return;
        }
        if (bHeal && (bCommitted || Range > 850.f) && ToImpact > .72f)
        {
            Player->Flask();
        }
        else if (ToImpact <= .52f && !bJumpable && !bParryable)
        {
            Player->AddMovementInput(Escape);
        }
        else if (bOpening && Range > 345.0 && Range < 825.0 && ToImpact > 1.12f &&
            Player->Stamina >= 51.f && Player->HasFocus(Player->ActiveArt()))
        {
            Player->Crimson();
        }
        else if (bOpening && Range < 325.0 && Player->Stamina >= 43.f &&
            (Boss->IsStaggered() || NextWindup > .31f || Move == EDuelMove::Idle))
        {
            if (Boss->IsStaggered() && Range < 285.f)
            {
                Player->RequestMove(EDuelMove::Heavy);
            }
            else
            {
                Player->Cut();
            }
        }
        else if (Range > 255.0)
        {
            Player->AddMovementInput(Toward);
        }
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelLiveMatch, "ManosabaDuel.LiveMatches",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelLiveMatch::RunTest(const FString& Parameters)
{
    ADuelMode* Mode = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        if (Context.World() && (Mode = Context.World()->GetAuthGameMode<ADuelMode>()))
        {
            break;
        }
    }
    if (!TestNotNull(TEXT("A running game world"), Mode) || !Mode->PlayerFighter || !Mode->BossFighter)
    {
        return false;
    }
    const bool bNorth = Mode->UsesNorthTower();
    int32 Battle = 0, Seed = 27131;
    EDuelCharacter Expected = EDuelCharacter::Ema;
    if (bNorth)
    {
        const TCHAR* Command = FCommandLine::Get();
        FString Hero;
        if (!TestTrue(TEXT("North live runs require -DuelNoSave -DuelLiveHero=Ema|Hiro -DuelLiveBattle=1|2|3"),
            FParse::Param(Command, TEXT("DuelNoSave")) && Mode->NorthTowerActor &&
            FParse::Value(Command, TEXT("DuelLiveHero="), Hero) && (Hero == TEXT("Ema") || Hero == TEXT("Hiro")) &&
            FParse::Value(Command, TEXT("DuelLiveBattle="), Battle) && Battle >= 1 && Battle <= 3)) return false;
        --Battle;
        FParse::Value(Command, TEXT("DuelLiveSeed="), Seed);
        ADuelNorthTower& Tower = *Mode->NorthTowerActor;
        ADuelFighter& Player = *Mode->PlayerFighter;
        const EDuelCharacter Character = Hero == TEXT("Ema") ? EDuelCharacter::Ema : EDuelCharacter::Hiro;
        if (!TestTrue(TEXT("Selected protagonist model is available"), Player.SelectCharacter(Character))) return false;
        Tower.NewGame(*Mode);
        // Seed the prerequisite progression for this independent arena case.
        // All damage and both phase changes in the selected match are live.
        for (int32 Previous = 0; Previous < Battle; ++Previous)
        {
            if (!TestTrue(TEXT("Prepare preceding arena gates"), DuelNorthInvestigationQA::Prepare(Tower.State, Previous) && Tower.State.WinBattle(Previous) &&
                Tower.State.PickupRelic(static_cast<EDuelNorthRelic>(Previous)) &&
                Tower.State.OpenDoor(static_cast<EDuelNorthDoor>(Previous)))) return false;
        }
        if (!TestTrue(TEXT("Complete the chosen arena investigation fixture"), DuelNorthInvestigationQA::Prepare(Tower.State,Battle))) return false;
        if (Battle > 0) Tower.State.CacheBits = DuelNorthFinds::AllCacheBits();
        Tower.Respawn(*Mode);
        Expected = Tower.State.BossForBattle(Battle);
        const EDuelWeapon Blade = Battle == 0 || Expected == EDuelCharacter::Hiro ? EDuelWeapon::Rapier : EDuelWeapon::Greatblade;
        const EDuelWeapon Shield = Battle == 0 ? EDuelWeapon::Buckler : EDuelWeapon::Greatshield;
        if (!TestTrue(TEXT("Live encounter fixture uses owned equipment"), Tower.State.OwnsWeapon(Blade) && Tower.State.OwnsWeapon(Shield)))
            return false;
        Player.SetWeapon(Blade);
        Player.SetWeapon(Shield, EDuelHand::Left);
        Player.ArtSelection = Character == EDuelCharacter::Ema ? EDuelArt::EmaLight : EDuelArt::HiroThreads;
        const FName GateIds[] = {TEXT("DungeonGate"), TEXT("MirrorGate"), TEXT("FinalGate")};
        const auto* Gate = DuelNorthLayout::FindLandmark(GateIds[Battle]);
        if (!TestNotNull(TEXT("Authored encounter threshold"), Gate)) return false;
        Player.SetActorLocationAndRotation(Gate->Position + FVector(0, 0, Player.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f),
            FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
        Player.ViewYaw = 0;
        Mode->Tick(0);
        for (int32 Press = 0; Press < 8 && !Mode->GateOpen(); ++Press) Mode->Confirm();
        if (!TestTrue(TEXT("F arms the actual fog door before entering combat"), Mode->GateOpen() && Tower.ActiveFight == Battle &&
            Mode->Outcome() == EDuelOutcome::Ready && Mode->BossFighter->SelectedCharacter() == Expected)) return false;
        AddInfo(FString::Printf(TEXT("North live case: hero %s, battle %d, boss %d, seed %d. Prior gates prepared; selected match uses real movement, damage and AI."),
            *Hero, Battle + 1, int32(Expected), Seed));
    }
    else
    {
        Mode->PlayerFighter->SelectCharacter(EDuelCharacter::Hiro);
        Mode->PlayerFighter->SetWeapon(EDuelWeapon::Greatblade);
        Mode->PlayerFighter->ArtSelection = EDuelArt::HiroThreads;
        Mode->BossFighter->SelectCharacter(EDuelCharacter::Ema);
        Mode->ResetEncounter();
        Mode->BossFighter->BossRandom.Initialize(Seed);
    }
    const double Began = Mode->GetWorld()->GetTimeSeconds();
    const TWeakObjectPtr<ADuelMode> Game(Mode);
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Game, Run = FDuelLiveCombat{Began}, VictoryWalkStarted = 0.0, Hearing = bNorth ? Battle + 1 : 1,
        bNorth, Battle, Expected, Seed, bAdmitted = !bNorth]() mutable
    {
        ADuelMode* Trial = Game.Get();
        if (!Trial || !Trial->PlayerFighter || !Trial->BossFighter)
        {
            AddError(TEXT("The live duel unloaded"));
            return true;
        }
        ADuelFighter* Player = Trial->PlayerFighter;
        ADuelFighter* Boss = Trial->BossFighter;
        const double Now = Trial->GetWorld()->GetTimeSeconds();
        if (!bAdmitted)
        {
            if (Trial->Outcome() != EDuelOutcome::Fighting)
            {
                if (Now - Run.Started > 8.0 || !Trial->GateOpen())
                {
                    AddError(TEXT("The player could not walk through the armed North Tower fog"));
                    return true;
                }
                Player->AddMovementInput(FVector::ForwardVector);
                return false;
            }
            if (!TestTrue(TEXT("North battle starts at its authored origin with the expected boss"),
                Trial->Arena->CourtOrigin().Equals(DuelNorthLayout::CourtOrigin(Battle), 1.f) &&
                Boss->SelectedCharacter() == Expected && Boss->BossDifficulty() == Battle)) return true;
            Run.Begin(*Trial, Seed);
            bAdmitted = true;
        }
        if (VictoryWalkStarted > 0)
        {
            if (Trial->Outcome() == EDuelOutcome::Epilogue)
            {
                TestTrue(TEXT("Three live victories lead on foot to the reunion"),
                    Trial->HearingsTime() > 0 && Trial->RescuedFighter &&
                    Trial->RescuedFighter->SelectedCharacter() == EDuelCharacter::Hiro);
                return true;
            }
            if (Now - VictoryWalkStarted > 16.0)
            {
                AddError(FString::Printf(TEXT("The live finale walk stopped at %s"), *Player->GetActorLocation().ToString()));
                return true;
            }
            const FVector At = Player->GetActorLocation() - DuelCourt::YukiOrigin;
            const float Ahead = At.X < 1885.f ? 1960.f : DuelCourt::YukiGardenX + 35.f;
            Player->AddMovementInput((FVector(Ahead, 0, 62) - At).GetSafeNormal2D());
            return false;
        }
        Run.Observe(*Trial, Hearing);
        if (Trial->Outcome() != EDuelOutcome::Fighting || Now - Run.Started > FDuelLiveCombat::TimeLimit)
        {
            const TCHAR* Name = Boss->CharacterName();
            AddInfo(FString::Printf(TEXT("Live hearing %d %s: %.1f s, phase %d, %d attack types, %d dodges, %d parries, %d landed hits, %.1f player HP, %.1f boss HP"),
                Hearing, Name, Now - Run.Started, Run.PeakPhase, Run.Moves.Num(), Run.Dodged, Run.Parried, Run.Hits,
                Player->Health, Run.Health));
            if (bNorth)
            {
                const ADuelNorthTower* Tower = Trial->NorthTower();
                TestTrue(TEXT("Real hits reach every phase and defeat the correct North boss"),
                    Trial->Outcome() == EDuelOutcome::Victory && !Boss->IsAlive() && Boss->bCombatOver &&
                    Run.Phases == 7 && Run.Hits > 0 && Run.Moves.Num() >= 3);
                TestTrue(TEXT("Real combat victory records its own battle and exposes the unclaimed relic"),
                    Tower && Tower->State.HasWon(Battle) && Tower->State.RelicAvailable(static_cast<EDuelNorthRelic>(Battle)));
                return true;
            }
            const bool Won = Trial->Outcome() == EDuelOutcome::Victory && Boss->bCombatOver && Run.PeakPhase == 3 &&
                (Hearing == 3 ? !Boss->IsAlive() : Boss->bAlternateForm);
            if (!TestTrue(TEXT("The fight can be completed through real attacks and phase changes"), Won))
            {
                return true;
            }
            if (Hearing == 3)
            {
                TestTrue(TEXT("The live final hearing mixes at least three attacks and uses a new mechanic"),
                    Run.Moves.Num() >= 3 &&
                    (Run.Moves.Contains(EDuelMove::VerdictTether) || Run.Moves.Contains(EDuelMove::MirrorEdict)));
                VictoryWalkStarted = Now;
                return false;
            }
            if (Hearing == 2)
            {
                Trial->EnterArchive();
                for (const FVector& Seal : DuelCourt::RitualMarks)
                {
                    Player->SetActorLocation(DuelCourt::AppealOrigin + Seal + FVector(0, 89, 62));
                    Trial->Confirm();
                }
                if (!TestTrue(TEXT("The second live victory allows the three evidence seals"), Trial->RitualComplete()))
                {
                    return true;
                }
                const FVector Final = DuelCourt::YukiOrigin;
                Player->SetActorLocation(DuelCourt::AppealOrigin +
                    FVector(DuelCourt::YukiArchiveExitX + 29.f, 0, DuelCourt::VerdictRise + 62.f));
                Trial->Tick(0.f);
                if (!TestTrue(TEXT("The final court opens after the seals"),
                    Trial->InYukiCourt() && Trial->Outcome() == EDuelOutcome::Ready))
                {
                    return true;
                }
                Player->SetActorLocation(DuelCourt::YukiCheckpoint().GetLocation());
                Trial->Confirm();
                Player->SetWeapon(EDuelWeapon::Greatblade);
                Player->SetActorLocation(Final + FVector(DuelCourt::GateX - 81.f, 0, 62));
                Trial->Confirm();
                Player->SetActorLocation(Final + FVector(DuelCourt::GateCrossX + 8.f, 0, 62));
                Trial->Tick(0.f);
                Boss = Trial->BossFighter;
                if (!TestTrue(TEXT("Rest and fog admit the player with full resources against Yuki"),
                    Boss && Boss->SelectedCharacter() == EDuelCharacter::Yuki && Trial->Outcome() == EDuelOutcome::Fighting &&
                    Player->Health == DuelCourt::PlayerMaxHealth && Player->Focus == DuelCourt::MaxFocus))
                {
                    return true;
                }
                Boss->BossRandom.Initialize(68371);
                Run = FDuelLiveCombat{Now};
                ++Hearing;
                return false;
            }
            Trial->EnterArchive();
            Player->SetActorLocation(FVector(DuelCourt::ArchiveDeskX - 162.f, 0, 62));
            Trial->Confirm();
            Trial->ChooseEnding(EDuelEnding::CallWitness);
            Trial->Confirm();
            Player->SetActorLocation(FVector(7095, -150, 267));
            Trial->EnterAppeal();
            Player->RestAtCheckpoint();
            Player->SetActorLocation(DuelCourt::AppealOrigin + FVector(DuelCourt::GateX - 97.f, 0, 62));
            Trial->Confirm();
            Player->SetActorLocation(DuelCourt::AppealOrigin + FVector(DuelCourt::GateCrossX + 8.f, 0, 62));
            Trial->Tick(0.f);
            if (!TestTrue(TEXT("The normal evidence decision starts the second fight against the other boss"),
                Trial->InAppeal() && Trial->Outcome() == EDuelOutcome::Fighting &&
                Boss->SelectedCharacter() == EDuelCharacter::Hiro))
            {
                return true;
            }
            Player->SelectCharacter(EDuelCharacter::Ema);
            Player->SetWeapon(EDuelWeapon::Rapier);
            Player->ArtSelection = EDuelArt::EmaLight;
            Boss->BossRandom.Initialize(84197);
            Run = FDuelLiveCombat{Now};
            ++Hearing;
            return false;
        }

        Run.Drive(*Trial, Now, Hearing);
        return false;
    }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelApproachSmoke, "ManosabaDuel.ApproachTraversal",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelApproachSmoke::RunTest(const FString& Parameters)
{
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        ADuelMode* Mode = Context.World() ? Context.World()->GetAuthGameMode<ADuelMode>() : nullptr;
        if (!Mode || Mode->Outcome() != EDuelOutcome::Ready)
        {
            continue;
        }
        ADuelFighter* Player = const_cast<ADuelFighter*>(Mode->GetPlayerFighter());
        if (!TestNotNull(TEXT("Controllable player for the prison approach"), Player))
        {
            return false;
        }
        if (!TestTrue(TEXT("Exploration starts at the lower checkpoint with the portcullis closed"),
            Player->GetActorLocation().Equals(DuelCourt::LowerCheckpoint().GetLocation(), 5.0) && !Mode->PrisonGateOpen()))
        {
            return false;
        }
        enum class EStop { Walk, StairTop, Warrant, Portcullis, Checkpoint, Fog };
        struct FWaypoint { FVector At; EStop Stop; };
        const TArray<FWaypoint> Route{
            {FVector(-8860, 0, 0), EStop::Walk},
            {FVector(DuelCourt::StairTopX + 169.f, 0, 0), EStop::StairTop},
            {FVector(-7065, 380, 0), EStop::Walk},
            {DuelCourt::Warrant + FVector(-22, -136, 0), EStop::Warrant},
            {FVector(-6590, 105, 0), EStop::Walk},
            {FVector(DuelCourt::PrisonGateX - 155.f, 0, 0), EStop::Portcullis},
            {FVector(DuelCourt::PrisonGateX + 340.f, 0, 0), EStop::Walk},
            {DuelCourt::Checkpoint().GetLocation(), EStop::Checkpoint},
            {FVector(DuelCourt::GateX - 154.f, 0, 0), EStop::Fog},
        };
        const TWeakObjectPtr<ADuelFighter> Walker(Player);
        const TWeakObjectPtr<ADuelMode> Game(Mode);
        const double Deadline = Context.World()->GetTimeSeconds() + 32.0;
        ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Walker, Game, Deadline, Route, Stop = 0]() mutable
        {
            ADuelFighter* Fighter = Walker.Get();
            ADuelMode* Trial = Game.Get();
            if (!Fighter || !Trial)
            {
                AddError(TEXT("The world unloaded during the approach"));
                return true;
            }
            const FVector At = Fighter->GetActorLocation();
            if (Fighter->GetWorld()->GetTimeSeconds() >= Deadline)
            {
                AddError(FString::Printf(TEXT("The approach timed out at waypoint %d (%s)"), Stop, *At.ToString()));
                return true;
            }
            const FWaypoint& Waypoint = Route[Stop];
            const FVector Offset = Waypoint.At - At;
            if (Waypoint.Stop == EStop::Fog && Trial->GateOpen())
            {
                Fighter->AddMovementInput(FVector::ForwardVector);
                return false;
            }
            if (Waypoint.Stop == EStop::Fog && Trial->Outcome() == EDuelOutcome::Fighting)
            {
                TestTrue(TEXT("The first threshold is crossed on foot"), At.X >= DuelCourt::GateCrossX);
                return true;
            }
            if (Offset.Size2D() > 37.0)
            {
                Fighter->AddMovementInput(Offset.GetSafeNormal2D(), FMath::Clamp(Offset.Size2D() / 108.0, .38, 1.0));
                return false;
            }
            switch (Waypoint.Stop)
            {
            case EStop::StairTop:
                TestTrue(TEXT("Walking climbs the full three-meter staircase"),
                    FMath::IsNearlyEqual(At.Z, 62.0, 4.0) && Fighter->GetCharacterMovement()->IsMovingOnGround());
                break;
            case EStop::Warrant:
                Trial->Confirm();
                TestTrue(TEXT("Walking into the alcove brings the warrant into reach"), Trial->HasWarrant());
                break;
            case EStop::Portcullis:
                Trial->Confirm();
                TestTrue(TEXT("The portcullis opens from its reachable front"), Trial->PrisonGateOpen());
                break;
            case EStop::Checkpoint:
                Trial->Confirm();
                TestTrue(TEXT("The second checkpoint is reachable on foot"), Trial->AntechamberReached());
                break;
            case EStop::Fog:
                if (Trial->Outcome() == EDuelOutcome::Ready)
                {
                    if (!Trial->GateOpen())
                    {
                        Trial->Confirm();
                    }
                    Fighter->AddMovementInput(FVector::ForwardVector);
                    return false;
                }
                TestTrue(TEXT("The full approach leads to the fight"), Trial->Outcome() == EDuelOutcome::Fighting);
                return true;
            case EStop::Walk:
                break;
            }
            ++Stop;
            return false;
        }));
        return true;
    }
    AddError(TEXT("A fresh exploration world is required for the staircase test"));
    return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelAppealSmoke, "ManosabaDuel.AppealTraversal",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelAppealSmoke::RunTest(const FString& Parameters)
{
    ADuelMode* Mode = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        if (Context.World())
        {
            Mode = Context.World()->GetAuthGameMode<ADuelMode>();
            if (Mode)
            {
                break;
            }
        }
    }
    if (!TestNotNull(TEXT("A fresh game world"), Mode) || !TestNotNull(TEXT("The playable pawn"), Mode->PlayerFighter.Get()))
    {
        return false;
    }
    ADuelFighter* Player = Mode->PlayerFighter;
    Mode->ResetEncounter();
    Mode->FighterFell(Mode->BossFighter);
    Mode->EnterArchive();
    Player->SetActorLocation(FVector(DuelCourt::ArchiveDeskX - 150, 0, 62));
    Mode->Confirm();
    Mode->ChooseEnding(EDuelEnding::SubmitReceipt);
    Mode->Confirm();
    TestTrue(TEXT("The alternate evidence decision is registered"),
        Mode->Ending() == EDuelEnding::SubmitReceipt && !Mode->ReadingArchive());

    enum class EAction { Walk, Passage, Rest, OpenFog, CrossFog, CrossArchive, Seal, Finish };
    struct FWaypoint { FVector At; EAction Action; };
    const FVector Origin = DuelCourt::AppealOrigin;
    const TArray<FWaypoint> Route{
        {FVector(4130, -335, 62), EAction::Walk},
        {DuelCourt::AppealPassage + FVector(0, 0, 62), EAction::Passage},
        {FVector(4670, -1140, 62), EAction::Walk},
        {FVector(6820, -1140, 263), EAction::Walk},
        {DuelCourt::PassageTurn + FVector(0, 0, 62), EAction::Walk},
        {FVector(7110, -160, 267), EAction::Walk},
        {DuelCourt::AppealCheckpoint().GetLocation(), EAction::Rest},
        {Origin + FVector(DuelCourt::AppealRampStartX - 110.f, 0, 0), EAction::Walk},
        {Origin + FVector(DuelCourt::GateX - 81.f, 0, 62), EAction::OpenFog},
        {Origin + FVector(DuelCourt::GateCrossX + 41.f, 0, 62), EAction::CrossFog},
        {Origin + FVector(DuelCourt::ArchiveEntryX + 8.f, 0, 62), EAction::CrossArchive},
        {Origin + DuelCourt::RitualMarks[0] + FVector(-81, 0, 62), EAction::Seal},
        {Origin + FVector(3105, 0, DuelCourt::VerdictRise + 62.f), EAction::Walk},
        {Origin + DuelCourt::RitualMarks[1] + FVector(-80, 0, 62), EAction::Seal},
        {Origin + FVector(3411, 310, DuelCourt::VerdictRise + 62.f), EAction::Walk},
        {Origin + DuelCourt::RitualMarks[2] + FVector(0, 89, 62), EAction::Seal},
        {Origin + FVector(3130, 310, DuelCourt::VerdictRise + 62.f), EAction::Walk},
        {Origin + FVector(DuelCourt::VerdictDeskX - 159.f, 0, DuelCourt::VerdictRise + 62.f), EAction::Finish},
    };
    const TWeakObjectPtr<ADuelMode> Game(Mode);
    const double Deadline = Mode->GetWorld()->GetTimeSeconds() + 51.0;
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Game, Route, Deadline, Step = 0]() mutable
    {
        ADuelMode* Trial = Game.Get();
        ADuelFighter* Fighter = Trial ? Trial->PlayerFighter.Get() : nullptr;
        if (!Fighter)
        {
            AddError(TEXT("The appeal world unloaded during traversal"));
            return true;
        }
        const FVector At = Fighter->GetActorLocation();
        if (Trial->GetWorld()->GetTimeSeconds() >= Deadline)
        {
            AddError(FString::Printf(TEXT("The appeal route stopped at waypoint %d (%s)"), Step, *At.ToString()));
            return true;
        }
        const FWaypoint& Point = Route[Step];
        const FVector Offset = Point.At - At;
        const float Stop = Point.Action == EAction::CrossFog || Point.Action == EAction::OpenFog ? 19.f : 36.f;
        if (Offset.Size2D() > Stop)
        {
            Fighter->AddMovementInput(Offset.GetSafeNormal2D(), FMath::Clamp(Offset.Size2D() / 97.0, .36, 1.0));
            return false;
        }
        switch (Point.Action)
        {
        case EAction::Passage:
            Trial->Confirm();
            if (!TestTrue(TEXT("The archive opens a physical passage in place"), Trial->Outcome() == EDuelOutcome::Passage &&
                Fighter->GetActorLocation().Equals(At)))
            {
                return true;
            }
            break;
        case EAction::Rest:
            Fighter->Health = 42.f;
            Fighter->Focus = 15.f;
            Fighter->Flasks = Fighter->FocusFlasks = 0;
            Trial->Confirm();
            TestTrue(TEXT("The next checkpoint restores both meters and the prepared flask allocation"),
                Trial->InAppeal() && Fighter->HealthRatio() == 1.f && Fighter->FocusRatio() == 1.f &&
                Fighter->FlaskCount() == DuelCourt::DefaultHealthFlasks && Fighter->FocusFlasks == DuelCourt::DefaultFocusFlasks);
            break;
        case EAction::OpenFog:
            if (!TestTrue(TEXT("Normal movement climbs to the elevated fog"), FMath::IsNearlyEqual(At.Z, Point.At.Z, 9.0)))
            {
                return true;
            }
            Trial->Confirm();
            TestTrue(TEXT("Opening the fog does not start combat in place"), Trial->GateOpen() && Trial->Outcome() == EDuelOutcome::Ready);
            break;
        case EAction::CrossFog:
            if (!TestTrue(TEXT("Walking across the threshold starts the elevated fight"), Trial->Outcome() == EDuelOutcome::Fighting))
            {
                return true;
            }
            Trial->FighterFell(Trial->BossFighter);
            break;
        case EAction::CrossArchive:
            TestTrue(TEXT("The second victory arch opens into the judgment chamber"), Trial->Outcome() == EDuelOutcome::Archive);
            break;
        case EAction::Seal:
            Trial->Confirm();
            break;
        case EAction::Finish:
            TestTrue(TEXT("Walking up the final steps reaches the raised judgment dais"),
                FMath::IsNearlyEqual(At.Z, Point.At.Z, 9.0));
            Trial->Confirm();
            TestTrue(TEXT("The second ruling can be reached on foot through the victory grille"),
                Trial->ReadingArchive() && Trial->Ending() == EDuelEnding::SubmitReceipt);
            return true;
        case EAction::Walk:
            break;
        }
        ++Step;
        return false;
    }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelFinaleSmoke, "ManosabaDuel.Finale",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelFinaleSmoke::RunTest(const FString& Parameters)
{
    if (FApp::CanEverRender() && !FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")))
    {
        AddError(TEXT("Run Finale with -DuelNoSave to preserve the local checkpoint."));
        return false;
    }
    ADuelMode* Mode = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        if (Context.World() && (Mode = Context.World()->GetAuthGameMode<ADuelMode>()))
        {
            break;
        }
    }
    if (!TestTrue(TEXT("The finale test has a fresh game world"), Mode && Mode->PlayerFighter &&
        Mode->BossFighter && !Mode->InAppeal() && !Mode->InYukiCourt()))
    {
        return false;
    }
    ADuelFighter* Player = Mode->PlayerFighter;
    Mode->ResetEncounter();
    Mode->FighterFell(Mode->BossFighter);
    TestTrue(TEXT("The first hearing returns the fallen witch to her standing form"),
        Mode->Outcome() == EDuelOutcome::Victory && Mode->BossFighter->bAlternateForm && Mode->BossFighter->IsAlive());
    Mode->EnterArchive();
    Player->SetActorLocation(FVector(DuelCourt::ArchiveDeskX - 150.f, 0, 62));
    Mode->Confirm();
    Mode->ChooseEnding(EDuelEnding::CallWitness);
    Mode->Confirm();
    Mode->EnterAppeal();
    Mode->ResetEncounter();
    Mode->FighterFell(Mode->BossFighter);
    TestTrue(TEXT("The second hearing rescues the other witch"),
        Mode->Outcome() == EDuelOutcome::Victory && Mode->BossFighter->bAlternateForm && Mode->BossFighter->IsAlive() &&
        Mode->BossFighter->SelectedCharacter() != Mode->FirstBoss);
    Mode->EnterArchive();
    Player->SetActorLocation(DuelCourt::AppealOrigin + FVector(2760, 0, 75));

    enum class EAction { Walk, Seal, Record, Door, Rest, OpenFog, Lose, CheckAttacks, Ending, Reunion };
    struct FWaypoint { FVector At; EAction Action; };
    const FVector Appeal = DuelCourt::AppealOrigin;
    const FVector Final = DuelCourt::YukiOrigin;
    const float DaisHeight = DuelCourt::VerdictRise + 62.f;
    const FVector OpenFog = Final + FVector(DuelCourt::GateX - 81.f, 0, 62);
    const FVector CrossFog = Final + FVector(DuelCourt::GateCrossX + 41.f, 0, 62);
    const TArray<FWaypoint> Route{
        {Appeal + DuelCourt::RitualMarks[0] + FVector(-81, 0, 62), EAction::Seal},
        {Appeal + FVector(3105, 0, DaisHeight), EAction::Walk},
        {Appeal + DuelCourt::RitualMarks[1] + FVector(-80, 0, 62), EAction::Seal},
        {Appeal + FVector(3411, 310, DaisHeight), EAction::Walk},
        {Appeal + DuelCourt::RitualMarks[2] + FVector(0, 89, 62), EAction::Seal},
        {Appeal + FVector(3130, 310, DaisHeight), EAction::Walk},
        {Appeal + FVector(DuelCourt::VerdictDeskX - 159.f, 0, DaisHeight), EAction::Record},
        {Appeal + FVector(3095, 306, DaisHeight), EAction::Walk},
        {Appeal + FVector(3490, 306, DaisHeight), EAction::Walk},
        {Appeal + FVector(DuelCourt::YukiArchiveExitX + 29.f, 0, DaisHeight), EAction::Door},
        {DuelCourt::YukiCheckpoint().GetLocation(), EAction::Rest},
        {OpenFog, EAction::OpenFog},
        {CrossFog, EAction::Lose},
        {OpenFog, EAction::OpenFog},
        {CrossFog, EAction::CheckAttacks},
        {Final + FVector(DuelCourt::YukiGardenX + 35.f, 0, 62), EAction::Ending},
        {Final + FVector(3180, 35, 62), EAction::Reunion},
    };
    const TWeakObjectPtr<ADuelMode> Game(Mode);
    const double Deadline = Mode->GetWorld()->GetTimeSeconds() + 52.0;
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Game, Route, Deadline, Final, Step = 0, Previous = Player->GetActorLocation()]() mutable
    {
        ADuelMode* Trial = Game.Get();
        ADuelFighter* Fighter = Trial ? Trial->PlayerFighter.Get() : nullptr;
        ADuelFighter* Boss = Trial ? Trial->BossFighter.Get() : nullptr;
        if (!Fighter || !Boss)
        {
            AddError(TEXT("The finale world unloaded during traversal"));
            return true;
        }
        const FVector At = Fighter->GetActorLocation();
        if (Trial->GetWorld()->GetTimeSeconds() >= Deadline)
        {
            AddError(FString::Printf(TEXT("The finale route stopped at waypoint %d (%s)"), Step, *At.ToString()));
            return true;
        }
        const FWaypoint& Point = Route[Step];
        const FVector Offset = Point.At - At;
        if (Point.Action == EAction::Door && !TestTrue(TEXT("The final-court approach stays continuous"),
            FVector::Dist(At, Previous) <= 155.f))
        {
            return true;
        }
        Previous = At;
        const float Stop = Point.Action == EAction::Door || Point.Action == EAction::OpenFog ||
            Point.Action == EAction::Lose || Point.Action == EAction::CheckAttacks || Point.Action == EAction::Ending ? 17.f : 34.f;
        if (Offset.Size2D() > Stop)
        {
            Fighter->AddMovementInput(Offset.GetSafeNormal2D(), FMath::Clamp(Offset.Size2D() / 96.0, .35, 1.0));
            return false;
        }
        switch (Point.Action)
        {
        case EAction::Seal:
            if (!TestTrue(TEXT("A ritual seal can be reached on foot"), Trial->RitualMark() != INDEX_NONE))
            {
                return true;
            }
            Trial->Confirm();
            break;
        case EAction::Record:
            Trial->Confirm();
            if (!TestTrue(TEXT("The completed ritual opens the record before the final court"),
                Trial->RitualComplete() && Trial->ReadingArchive() && Trial->Ending() == EDuelEnding::CallWitness))
            {
                return true;
            }
            Trial->Confirm();
            break;
        case EAction::Door:
            if (!TestTrue(TEXT("Crossing the raised doorway introduces Yuki without moving the player to a spawn"),
                Trial->InYukiCourt() && Trial->Outcome() == EDuelOutcome::Ready &&
                Boss->SelectedCharacter() == EDuelCharacter::Yuki && FMath::IsNearlyEqual(At.Z, Point.At.Z, 9.0)))
            {
                return true;
            }
            break;
        case EAction::Rest:
            Fighter->Health = 42.f;
            Fighter->Focus = 15.f;
            Fighter->Flasks = Fighter->FocusFlasks = 0;
            Trial->Confirm();
            TestTrue(TEXT("The final checkpoint refills health, focus and flasks in place"),
                Trial->NearRestPoint() && Fighter->GetActorLocation().Equals(At) && Fighter->HealthRatio() == 1.f &&
                Fighter->FocusRatio() == 1.f && Fighter->FlaskCount() == DuelCourt::DefaultHealthFlasks &&
                Fighter->FocusFlasks == DuelCourt::DefaultFocusFlasks);
            break;
        case EAction::OpenFog:
            Trial->Confirm();
            if (!TestTrue(TEXT("Opening the third fog leaves the player outside until they walk in"),
                Trial->GateOpen() && Trial->Outcome() == EDuelOutcome::Ready && Fighter->GetActorLocation().Equals(At)))
            {
                return true;
            }
            break;
        case EAction::Lose:
            if (!TestTrue(TEXT("Walking past the third fog starts Yuki's fight"),
                Trial->Outcome() == EDuelOutcome::Fighting && Trial->InYukiCourt() &&
                Boss->SelectedCharacter() == EDuelCharacter::Yuki))
            {
                return true;
            }
            Fighter->ReceiveDuelDamage(1000.f, Boss->GetActorLocation());
            TestTrue(TEXT("Losing to Yuki enters the defeat state"), Trial->Outcome() == EDuelOutcome::Defeat);
            Trial->Confirm();
            TestTrue(TEXT("Retry returns to the final checkpoint with the witness decision intact"),
                Trial->Outcome() == EDuelOutcome::Ready && Trial->Ending() == EDuelEnding::CallWitness &&
                Fighter->GetActorLocation().Equals(DuelCourt::YukiCheckpoint().GetLocation(), .01));
            break;
        case EAction::CheckAttacks:
        {
            if (!TestTrue(TEXT("The final hearing can be entered again"), Trial->Outcome() == EDuelOutcome::Fighting))
            {
                return true;
            }
            const auto ResetTestPositions = [Trial, Fighter, Boss, Final]()
            {
                Trial->ResetEncounter();
                Fighter->SetActorLocation(Final + FVector(-410, 0, 62));
                Boss->SetActorLocation(Final + FVector(120, 0, 103));
            };
            const auto AtFloor = [Fighter, Final](const FVector& Mark)
            {
                Fighter->SetActorLocation(FVector(Mark.X, Mark.Y, Final.Z + 62.f));
            };
            const auto Took = [Fighter](float Damage)
            {
                return FMath::IsNearlyEqual(Fighter->Health,
                    DuelCourt::PlayerMaxHealth - Damage * DuelCourt::BossDamageScale, .01f);
            };
            const auto EnterPhaseTwo = [Fighter, Boss]()
            {
                Boss->ReceiveDuelDamage(DuelCourt::BossStageHealth[0], Fighter->GetActorLocation());
                Boss->TickMove(DuelCourt::Timing(EDuelMove::Awaken, false).Total());
            };
            const auto EnterPhaseThree = [Fighter, Boss, EnterPhaseTwo]()
            {
                EnterPhaseTwo();
                Boss->ReceiveDuelDamage(DuelCourt::BossStageHealth[1], Fighter->GetActorLocation());
                Boss->TickMove(DuelCourt::Timing(EDuelMove::Eclipse, true).Total());
            };

            ResetTestPositions();
            const float GlassImpact = DuelCourt::Timing(EDuelMove::GlassShelter, false).Windup;
            Boss->StartMove(EDuelMove::GlassShelter);
            AtFloor(Boss->CastOrigin + FVector(0, 335, 0));
            Boss->TickMove(GlassImpact + .01f);
            TestTrue(TEXT("Glass Shelter protects the inside edge of its ring"), Took(0.f));
            ResetTestPositions();
            Boss->StartMove(EDuelMove::GlassShelter);
            AtFloor(Boss->CastOrigin + FVector(0, 341, 0));
            Boss->TickMove(GlassImpact - .01f);
            TestTrue(TEXT("Glass Shelter gives time to enter the safe zone"), Took(0.f));
            Boss->TickMove(.02f);
            TestTrue(TEXT("Glass Shelter damages outside the ring after the warning"), Took(69.f));

            ResetTestPositions();
            EnterPhaseTwo();
            Boss->StartMove(EDuelMove::BrokenRecord);
            if (!TestTrue(TEXT("Broken Record lays three marks and releases them in drawing order"),
                Boss->MapWaves.Num() == 3 && Boss->MapWaves[0].ImpactTime < Boss->MapWaves[1].ImpactTime &&
                Boss->MapWaves[1].ImpactTime < Boss->MapWaves[2].ImpactTime))
            {
                return true;
            }
            const FVector FirstMark = Boss->MapWaves[0].Marks[0];
            AtFloor(FirstMark + FVector(0, 480, 0));
            Boss->TickMove(Boss->MapWaves[1].WarnTime + .01f);
            AtFloor(FirstMark + FVector(0, -480, 0));
            Boss->TickMove(Boss->MapWaves[2].WarnTime - Boss->MapWaves[1].WarnTime);
            AtFloor(FirstMark);
            Boss->TickMove(Boss->MapWaves[0].ImpactTime - Boss->MoveAge + .01f);
            TestTrue(TEXT("The first drawn mark explodes first after its warning"), Took(63.f));
            const float AfterFirst = Fighter->Health;
            Boss->TickMove(Boss->MapWaves[2].ImpactTime - Boss->MoveAge + .01f);
            TestTrue(TEXT("Later drawn marks do not explode again at the first mark"),
                FMath::IsNearlyEqual(Fighter->Health, AfterFirst, .01f));

            ResetTestPositions();
            EnterPhaseTwo();
            Boss->StartMove(EDuelMove::SilentBell);
            const float BellImpact = DuelCourt::Timing(EDuelMove::SilentBell, true).Windup;
            Fighter->GetCharacterMovement()->Velocity = FVector::ZeroVector;
            Boss->TickMove(BellImpact + .01f);
            TestTrue(TEXT("Silent Bell spares a player who stands still in the marked lane"), Took(0.f));
            Fighter->GetCharacterMovement()->Velocity = FVector(170, 0, 0);
            Boss->TickMove(.10f);
            TestTrue(TEXT("Moving in Silent Bell's active lane triggers damage"), Took(69.f));

            ResetTestPositions();
            const auto Tether = DuelCourt::Timing(EDuelMove::VerdictTether, false);
            if (!TestTrue(TEXT("Yuki starts the tether"), Boss->StartMove(EDuelMove::VerdictTether)))
            {
                return true;
            }
            AtFloor(Boss->CastOrigin + FVector(0, 539, 0));
            Boss->TickMove(Tether.Windup + .01f);
            TestTrue(TEXT("The tether attaches inside the announced circle"), Boss->bVerdictTetherHeld && Took(0.f));
            AtFloor(Boss->CastOrigin + FVector(0, 541, 0));
            Boss->TickMove(.08f);
            TestFalse(TEXT("Leaving the circle breaks the tether"), Boss->bVerdictTetherHeld);
            AtFloor(Boss->CastOrigin + FVector(0, 530, 0));
            Boss->TickMove(Tether.Active);
            TestTrue(TEXT("Reentering the circle after breaking a tether does not retarget the player"), Took(0.f));

            ResetTestPositions();
            Boss->StartMove(EDuelMove::VerdictTether);
            AtFloor(Boss->CastOrigin + FVector(0, 539, 0));
            Boss->TickMove(Tether.Windup + Tether.Active - .01f);
            TestTrue(TEXT("The tether gives time to break the link"), Took(0.f));
            Boss->TickMove(.02f);
            Boss->TickMove(.08f);
            TestTrue(TEXT("Staying linked deals one hit at the end of the tether"), Took(60.f));

            ResetTestPositions();
            EnterPhaseTwo();
            const auto Mirror = DuelCourt::Timing(EDuelMove::MirrorEdict, true);
            if (!TestTrue(TEXT("Phase two produces one mirror sweep"),
                Boss->StartMove(EDuelMove::MirrorEdict) && Boss->MapWaves.Num() == 1))
            {
                return true;
            }
            const auto& OutsideWave = Boss->MapWaves[0];
            AtFloor(OutsideWave.Marks[0] + FVector(0, OutsideWave.Reach + 30.f, 0));
            Boss->TickMove(Mirror.Windup + .05f);
            TestTrue(TEXT("The mirror sweep has to travel from the side of the room to reach the player"), Took(0.f));
            Boss->TickMove(.60f);
            Boss->TickMove(.06f);
            TestTrue(TEXT("A mirror sweep hits once outside the marked safe seam"), Took(61.f));

            ResetTestPositions();
            EnterPhaseThree();
            if (!TestTrue(TEXT("The final phase adds a shifted, narrower safe seam"),
                Boss->StartMove(EDuelMove::MirrorEdict) && Boss->MapWaves.Num() == 2 &&
                Boss->MapWaves[1].Reach < Boss->MapWaves[0].Reach &&
                Boss->MapWaves[0].Marks[0].Y * Boss->MapWaves[1].Marks[0].Y < 0.f))
            {
                return true;
            }
            AtFloor(Boss->MapWaves[0].Marks[0]);
            Boss->TickMove(Mirror.Windup + .65f);
            TestTrue(TEXT("The first mirror seam is safe in the final phase"), Took(0.f));
            AtFloor(Boss->MapWaves[1].Marks[0]);
            Boss->TickMove(Mirror.Windup + DuelBossDefense::MirrorActive - Boss->MoveAge + .01f);
            TestTrue(TEXT("Crossing to the shifted mirror seam avoids the second sweep"), Took(0.f));

            ResetTestPositions();
            Boss->StartMove(EDuelMove::PrismPursuit);
            TestTrue(TEXT("Yuki's first form launches a pair of independently aimed prisms"),
                Boss->MapWaves.Num() == 2 && Boss->MapWaves[0].Marks.Num() == 1 && Boss->MapWaves[1].Marks.IsEmpty());
            const FVector FirstAxis = Boss->MapWaves[0].Axis;
            AtFloor(Final + FVector(-490, 285, 0));
            Boss->TickMove(.81f);
            TestTrue(TEXT("The warned prism launches and moves Yuki sideways"),
                Boss->MapWaves[0].bHitDelivered && Boss->MapWaves[1].Marks.IsEmpty() &&
                FMath::Abs(Boss->GetActorLocation().Y - Final.Y) > 0.f);
            Trial->Arena->Tick(1.40f);
            Trial->Arena->Tick(.01f);
            Boss->TickMove(.01f);
            TestTrue(TEXT("After the first projectile resolves, the opposite prism records the new position"),
                Boss->MapWaves[1].Marks.Num() == 1 && FVector::DotProduct(FirstAxis, Boss->MapWaves[1].Axis) < .60f);

            ResetTestPositions();
            EnterPhaseThree();
            Boss->PoseFigure(.75f);
            TestTrue(TEXT("Yuki's final form has six visible prisms"),
                Boss->YukiPrisms.Num() == 6 && Boss->YukiPrisms.Last()->IsVisible());
            Boss->StartMove(EDuelMove::ArchiveCollapse);
            const auto Collapse = DuelCourt::Timing(EDuelMove::ArchiveCollapse, true);
            AtFloor(Boss->CastOrigin + FVector(0, 625, 0));
            Fighter->SetActorLocation(Fighter->GetActorLocation() + FVector(0, 0, 92.f));
            Boss->TickMove(Collapse.Windup + .50f);
            TestTrue(TEXT("Jump height clears the first inward wave"), Took(0.f));
            AtFloor(Boss->CastOrigin + FVector(0, 625, 0));
            Boss->TickMove(.37f);
            TestTrue(TEXT("The first wave has passed before the final-stage return"), Took(0.f));
            Boss->TickMove(Collapse.Windup + DuelCourt::ArchiveReturnAt + DuelCourt::ArchiveReturnTime * .6f - Boss->MoveAge);
            TestTrue(TEXT("The final-stage return crosses the same position from the other direction"),
                Boss->BoneWaveHits[1] && Took(53.f));

            ResetTestPositions();
            EnterPhaseThree();
            const bool bReachedFinalPhase = Boss->Phase() == 3;
            Boss->ReceiveDuelDamage(DuelCourt::BossStageHealth[2], Fighter->GetActorLocation());
            TestTrue(TEXT("Yuki's final health threshold yields victory and opens the garden route"),
                bReachedFinalPhase && Trial->Outcome() == EDuelOutcome::Victory && !Boss->IsAlive());
            break;
        }
        case EAction::Ending:
            if (!TestTrue(TEXT("Walking through the victory arch enters the epilogue with the rescued companion"),
                Trial->Outcome() == EDuelOutcome::Epilogue && !Trial->ReadingArchive() && Trial->HearingsTime() >= 3 &&
                Trial->RescuedFighter && Trial->RescuedFighter->bAlternateForm && Trial->RescuedFighter->IsAlive() &&
                Trial->RescuedFighter->SelectedCharacter() != Fighter->SelectedCharacter()))
            {
                return true;
            }
            Trial->Confirm();
            break;
        case EAction::Reunion:
            Trial->Confirm();
            TestTrue(TEXT("The garden reunion starts dialogue on interaction"), Trial->ReadingArchive() && Trial->Choice() == 0);
            TestTrue(TEXT("The rescued companion stays beside the garden after the player walks to her"),
                Trial->RescuedFighter &&
                FVector::Dist2D(Trial->RescuedFighter->GetActorLocation(), At) <= 260.f &&
                FMath::Abs(Trial->RescuedFighter->GetActorLocation().Z - At.Z) <= 55.f);
            Trial->Confirm();
            Trial->Confirm();
            Trial->Confirm();
            TestTrue(TEXT("Finishing the dialogue returns control in the garden"), !Trial->ReadingArchive());
            return true;
        case EAction::Walk:
            break;
        }
        ++Step;
        return false;
    }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelCheckpointSmoke, "ManosabaDuel.Checkpoint",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelCheckpointSmoke::RunTest(const FString& Parameters)
{
    FString UserDirectory;
    const bool bIsolated = FParse::Value(FCommandLine::Get(), TEXT("UserDir="), UserDirectory) && !UserDirectory.IsEmpty();
    const FString SaveDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
    const FString ProjectSaved = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Saved"));
    if (!FApp::CanEverRender() || FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")) ||
        !bIsolated || !FPaths::IsUnderDirectory(SaveDirectory, ProjectSaved) ||
        FPaths::IsSamePath(SaveDirectory, ProjectSaved))
    {
        AddError(TEXT("Run Checkpoint with rendering enabled and -UserDir set to a fresh subfolder of the project's Saved directory."));
        return false;
    }
    ADuelMode* Mode = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        if (Context.World() && (Mode = Context.World()->GetAuthGameMode<ADuelMode>()))
        {
            break;
        }
    }
    if (!TestTrue(TEXT("The isolated save test has a fresh title screen and an unused checkpoint"),
        Mode && Mode->PlayerFighter && Mode->BossFighter && Mode->Outcome() == EDuelOutcome::Title && !Mode->CanContinue()))
    {
        return false;
    }
    ADuelFighter& Player = *Mode->PlayerFighter;
    Mode->Confirm();
    Player.SelectCharacter(EDuelCharacter::Ema);
    Player.SetWeapon(EDuelWeapon::YukiPen);
    Player.SetWeapon(EDuelWeapon::Greatshield, EDuelHand::Left);
    Player.RightReserve = EDuelWeapon::RitualSword;
    Player.LeftReserve = EDuelWeapon::Spear;
    Player.ArtSelection = EDuelArt::EmaVotive;
    Player.AdjustSpellSlot(0, -1);
    Player.CycleSpell();
    Player.HealthFlaskAllocation = 3;
    Mode->EnterAppeal();
    Mode->FirstDuelSeconds = 54;
    Mode->Decision = EDuelEnding::CallWitness;
    Mode->CompletedDuelSeconds = 69;
    Mode->EnterYukiCourt();
    Player.SetActorLocation(DuelCourt::YukiCheckpoint().GetLocation());
    Mode->Confirm();
    if (!TestTrue(TEXT("Confirm at the last rest point writes a Continue checkpoint"), Mode->CanContinue()))
    {
        return false;
    }
    constexpr const TCHAR* Slot = TEXT("MissingTestimonyCheckpoint");
    const FString SavePath = SaveDirectory / TEXT("SaveGames/MissingTestimonyCheckpoint.sav");
    const UDuelProgress* ThirdCourt = Cast<UDuelProgress>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
    if (!TestTrue(TEXT("The final court checkpoint is on disk inside the isolated directory"),
        IFileManager::Get().FileSize(*SavePath) > 0 && ThirdCourt && ThirdCourt->bYukiCourt && !ThirdCourt->bFinished &&
        ThirdCourt->FlaskBudget == DuelCourt::TotalFlasks))
    {
        return false;
    }
    const auto Continue = [&]()
    {
        Mode->Result = EDuelOutcome::Title;
        Mode->MenuSelection = 0;
        Mode->Confirm();
    };
    Player.SelectCharacter(EDuelCharacter::Hiro);
    Player.SetWeapon(EDuelWeapon::Rapier);
    Player.SetWeapon(EDuelWeapon::Buckler, EDuelHand::Left);
    Player.AdjustSpellSlot(0, 1);
    Player.CycleSpell();
    Player.HealthFlaskAllocation = 1;
    Continue();
    TestTrue(TEXT("Continue restores the equipped and reserve gear, prepared art, spell and flasks"),
        Player.SelectedCharacter() == EDuelCharacter::Ema && Player.EquippedWeapon() == EDuelWeapon::YukiPen &&
        Player.EquippedWeapon(EDuelHand::Left) == EDuelWeapon::Greatshield && Player.ReserveWeapon(EDuelHand::Right) == EDuelWeapon::RitualSword &&
        Player.ReserveWeapon(EDuelHand::Left) == EDuelWeapon::Spear && Player.SelectedArt() == EDuelArt::EmaVotive &&
        Player.FlaskCount() == 3 && Player.FocusFlasks == DuelCourt::TotalFlasks - 3);
    TestTrue(TEXT("Continue restores all three prepared spells and the active slot"),
        Player.SpellAt(0) == EDuelArt::SableBlade && Player.SpellAt(1) == EDuelArt::WitchRain &&
        Player.SpellAt(2) == EDuelArt::IceLance && Player.SpellSlot() == 1 && Player.SelectedSpell() == EDuelArt::WitchRain);
    TestTrue(TEXT("Continue restores the final rest point, Yuki and the earlier hearings"),
        Mode->Outcome() == EDuelOutcome::Ready && Mode->InYukiCourt() && !Mode->GateOpen() &&
        Mode->BossFighter->SelectedCharacter() == EDuelCharacter::Yuki && Mode->Ending() == EDuelEnding::CallWitness &&
        Mode->HearingsTime() == 123 && Player.GetActorLocation().Equals(DuelCourt::YukiCheckpoint().GetLocation(), .01));

    Mode->ResetEncounter();
    Mode->FighterFell(Mode->BossFighter);
    Mode->CompletedDuelSeconds = 81;
    Player.SetActorLocation(DuelCourt::YukiOrigin + FVector(DuelCourt::YukiGardenX + 30, 0, 62));
    Mode->Tick(0.f);
    const UDuelProgress* Finished = Cast<UDuelProgress>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
    TestTrue(TEXT("Reaching the epilogue records completion and all three hearing times"),
        Finished && Finished->bFinished && Finished->FirstFight == 54 && Finished->SecondFight == 69 && Finished->FinalFight == 81);
    Continue();
    TestTrue(TEXT("Continuing a completed game returns to the epilogue with the companion and correct result"),
        Mode->Outcome() == EDuelOutcome::Epilogue && !Mode->ReadingArchive() && Mode->HearingsTime() == 204 &&
        Mode->RescuedFighter && Mode->RescuedFighter->SelectedCharacter() == EDuelCharacter::Hiro &&
        Player.GetActorLocation().X >= DuelCourt::YukiOrigin.X + DuelCourt::YukiGardenX);
    UDuelProgress* Legacy = NewObject<UDuelProgress>();
    Legacy->Spell = static_cast<uint8>(EDuelArt::WitchRain);
    if (TestTrue(TEXT("A legacy-shaped single-spell checkpoint is written to the isolated profile"),
        UGameplayStatics::SaveGameToSlot(Legacy, Slot, 0)))
    {
        Continue();
        TestTrue(TEXT("Legacy saves keep the original spell active and fill the other two slots"),
            Player.SpellSlot() == 0 && Player.SelectedSpell() == EDuelArt::WitchRain &&
            Player.SpellAt(1) == EDuelArt::IceLance && Player.SpellAt(2) == EDuelArt::SableBlade);
        TestTrue(TEXT("The old default flask allocation migrates to four red and three blue"),
            Player.FlaskCount() == DuelCourt::DefaultHealthFlasks && Player.FocusFlasks == DuelCourt::DefaultFocusFlasks);
        Legacy->BloodFlasks = 1;
        if (TestTrue(TEXT("The isolated profile can store a legacy custom flask allocation"),
            UGameplayStatics::SaveGameToSlot(Legacy, Slot, 0)))
        {
            Continue();
            TestTrue(TEXT("A legacy custom red allocation survives the expanded budget"),
                Player.FlaskCount() == 1 && Player.FocusFlasks == DuelCourt::TotalFlasks - 1);
        }
    }
    AddInfo(FString::Printf(TEXT("Round-trip checkpoint: %s"), *SavePath));
    return true;
}

#endif
