#include "DuelArena.h"
#include "DuelGame.h"
#include "DuelNorthTower.h"
#include "DuelNorthInvestigationQA.h"
#if !UE_BUILD_SHIPPING
#include "Camera/CameraTypes.h"
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

// Only the save-free QA command can hold these production states for visual review.
// Contacts, movement and transitions below use the same methods as live combat.
struct FDuelDungeonChainsVisualFixture
{
    static void Capture(const TArray<FString>& Args,UWorld* World)
    {
        if(!World || Args.Num()!=2 || !FParse::Param(FCommandLine::Get(),TEXT("DuelNorthTower")) ||
            !FParse::Param(FCommandLine::Get(),TEXT("DuelNoSave")))return;
        auto* Mode=World->GetAuthGameMode<ADuelMode>();
        auto* Tower=Mode?const_cast<ADuelNorthTower*>(Mode->NorthTower()):nullptr;
        auto* Player=Mode?const_cast<ADuelFighter*>(Mode->GetPlayerFighter()):nullptr;
        auto* PC=World->GetFirstPlayerController();
        const auto Fail=[](const TCHAR* Reason)
        {UE_LOG(LogTemp,Error,TEXT("DungeonChains visual: %s"),Reason);FPlatformMisc::RequestExitWithStatus(false,1);};
        if(!Tower || !Player || !PC || (Args[0]!=TEXT("Hiro") && Args[0]!=TEXT("Ema")) ||
            (Args[1]!=TEXT("Warning") && Args[1]!=TEXT("Flight") && Args[1]!=TEXT("Counter")))
        {Fail(TEXT("Invalid route, state or world"));return;}
        FString Output;
        if(!FParse::Value(FCommandLine::Get(),TEXT("DuelNorthCaptureOutput="),Output))
            Output=FPaths::ProjectSavedDir()/TEXT("DungeonChains347");
        IFileManager::Get().MakeDirectory(*Output,true);
        const FString Name=Args[0]+Args[1],File=Output/(Name+TEXT(".png"));
        if(IFileManager::Get().FileExists(*File)){Fail(TEXT("Refusing to overwrite an existing photograph"));return;}
        const auto Hero=Args[0]==TEXT("Hiro")?EDuelCharacter::Hiro:EDuelCharacter::Ema;
        if(!Player->SelectCharacter(Hero)){Fail(TEXT("Cannot select the requested protagonist"));return;}
        Tower->State.Reset(Hero);
        Tower->State.bOpeningCellRequired=true;
        const auto* Gate=DuelNorthLayout::FindLandmark(TEXT("DungeonGate"));
        if(!Gate || !Tower->State.ReadClue(EDuelNorthClue::OpeningDocket) || !Tower->State.OpenStartingCell() ||
            !DuelNorthInvestigationQA::Prepare(Tower->State,0))
        {Fail(TEXT("Cannot prepare the genuine dungeon gate"));return;}
        Tower->RefreshGates();
        Player->SetActorLocation(Gate->Position+FVector(0,0,62),false,nullptr,ETeleportType::TeleportPhysics);
        Tower->Update(*Mode,0.f);
        for(int32 I=0;I<8 && !Mode->GateOpen();++I)Mode->Confirm();
        if(!Mode->GateOpen()){Fail(TEXT("The genuine dungeon gate did not open"));return;}
        const FVector Origin=DuelNorthLayout::CourtOrigin(0);
        Player->SetActorLocation(Origin+FVector(-1050,0,62),false,nullptr,ETeleportType::TeleportPhysics);
        Tower->Update(*Mode,0.f);
        auto* Boss=const_cast<ADuelFighter*>(Mode->GetBossFighter());
        auto* Arena=Mode->Arena.Get();
        if(!Boss || !Arena || Mode->Outcome()!=EDuelOutcome::Fighting || Boss->Character==Hero ||
            !Boss->ReceiveDuelDamage(DuelCourt::BossStageHealth[0]+1.f,Player->GetActorLocation()))
        {Fail(TEXT("The real route boss or phase transition was not reached"));return;}
        const auto Transition=DuelCourt::Timing(EDuelMove::Awaken,true);
        Boss->Tick(Transition.Windup+Transition.Active+Transition.Recovery+.06f);
        if(Boss->Phase()!=2){Fail(TEXT("The genuine boss did not reach phase two"));return;}
        Player->SetActorTickEnabled(false);Boss->SetActorTickEnabled(false);Arena->SetActorTickEnabled(false);
        Mode->SetActorTickEnabled(false);
        Player->GetCharacterMovement()->DisableMovement();Boss->GetCharacterMovement()->DisableMovement();
        Player->ForwardAxis=Player->RightAxis=0;Player->bLockOn=false;Player->ViewPitch=-10.f;
        Arena->ClearCombatEffects();
        Arena->TickCourtInteractions(Boss,5.01f);Arena->TickCourtInteractions(Boss,1.26f);
        Arena->TickEffects(1.26f);
        using Stage=FDuelDungeonChainsState::EStage;
        if(Arena->DungeonChains.Stage!=Stage::Active){Fail(TEXT("Production chains failed to activate"));return;}
        auto& S=Arena->DungeonChains;
        const auto Advance=[&](float Seconds)
        {
            const int32 Steps=FMath::CeilToInt(Seconds/.01f);const float Dt=Seconds/Steps;
            for(int32 I=0;I<Steps;++I)
            {
                if(Player->Move!=EDuelMove::Idle)Player->TickMove(Dt);
                if(Boss->Move==EDuelMove::Stagger)Boss->TickMove(Dt);
                Arena->TickEffects(Dt);Arena->TickOrbs(Dt);Arena->TickCourtInteractions(Boss,Dt);Arena->TickParticles(Dt);
            }
        };
        const auto Bait=[&](int32 I)
        {
            Player->Move=EDuelMove::Idle;Player->SetActorLocation(S.Points[I]+(S.Points[I]-S.Source).GetSafeNormal2D()*350.-FVector(0,0,73));
            Player->ViewYaw=(S.Source-Player->GetActorLocation()).Rotation().Yaw;
            Player->SetActorRotation(FRotator(0,Player->ViewYaw,0));S.NextVolley=S.Clock;Advance(.02f);
        };
        Bait(0);
        if(Args[1]==TEXT("Warning"))Advance(.65f);
        else if(Args[1]==TEXT("Flight"))Advance(1.95f);
        else
        {
            for(int32 I=0;I<2;++I)
            {
                if(I)Bait(I);
                Advance(1.02f);Player->RightAxis=I?1.f:-1.f;
                const float Health=Player->Health;const FVector Before=Player->GetActorLocation();
                Player->PressShift();Player->ReleaseShift();
                if(Player->Move!=EDuelMove::Roll || FMath::Abs(FVector::DotProduct(Player->DodgeDirection,(S.Aim-S.Source).GetSafeNormal2D()))>.45)
                {Fail(TEXT("The real Shift input did not begin a lateral dodge"));return;}
                Advance(2.6f);Player->RightAxis=0;
                if(Player->Health!=Health || FVector::Dist2D(Player->GetActorLocation(),Before)<200.)
                {Fail(TEXT("The genuine lateral dodge did not clear the aimed projectile without damage"));return;}
                if(S.Progress()!=I+1){Fail(TEXT("A real baited projectile did not sever the intended chain"));return;}
            }
            Player->SetActorLocation(Boss->GetActorLocation()+FVector(-420,0,-41));
            Player->ViewYaw=0;Player->SetActorRotation(FRotator::ZeroRotator);
            if(S.Stage!=Stage::Counter){Fail(TEXT("The earned counter window is not live"));return;}
        }
        Player->PoseFigure(0.f);Boss->PoseFigure(0.f);Player->CameraImpulse=0;
        for(int32 I=0;I<120;++I)Player->TickCamera(1.f/60.f);
        PC->SetViewTarget(Player);
        FTimerHandle Warm;
        World->GetTimerManager().SetTimer(Warm,[World,Player,Boss,Arena,File,Name,Fail]()
        {
            FMinimalViewInfo View;Player->CalcCamera(0.f,View);
            const auto& C=Arena->DungeonChains;
            const FString Receipt=FString::Printf(TEXT("passed=1\nhero=%s\nboss=%s\nphase=%d\nprogress=%d\nshots=%d\nstate=%d\nclock=%.3f\nplayer_hp=%.3f\ncamera_source=Player.CalcCamera\neye=%s\nrotation=%s\nheld_after_actual_production_tick=1\ndesktop_input=none\nprompt=%s\n"),
                Player->CharacterName(),Boss->CharacterName(),Boss->Phase(),C.Progress(),C.Shots,int32(C.Stage),C.Clock,Player->Health,
                *View.Location.ToString(),*View.Rotation.ToString(),*Arena->DungeonChainsPrompt());
            FFileHelper::SaveStringToFile(Receipt,*FPaths::ChangeExtension(File,TEXT("txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            FScreenshotRequest::RequestScreenshot(File,true,false);
            UE_LOG(LogTemp,Display,TEXT("DungeonChains visual requested %s via actual player camera"),*Name);
            FTimerHandle Exit;
            World->GetTimerManager().SetTimer(Exit,[File,Fail]()
            {
                if(IFileManager::Get().FileSize(*File)<=0){Fail(TEXT("No rendered photograph was saved"));return;}
                FPlatformMisc::RequestExitWithStatus(false,0);
            },1.f,false);
        },4.f,false);
    }
};

namespace
{
    FAutoConsoleCommandWithWorldAndArgs DungeonChainsCapture(TEXT("Duel.CaptureDungeonChains"),
        TEXT("Save-free normal-player-camera photograph: Hiro/Ema Warning/Flight/Counter."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FDuelDungeonChainsVisualFixture::Capture));
}
#endif
