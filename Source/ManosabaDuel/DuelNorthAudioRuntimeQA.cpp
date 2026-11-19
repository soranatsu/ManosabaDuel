#if WITH_DEV_AUTOMATION_TESTS

#include "AudioDevice.h"
#include "Containers/Ticker.h"
#include "DuelArena.h"
#include "DuelGame.h"
#include "DuelNorthLayout.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "ISubmixBufferListener.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/BufferArchive.h"
#include "Sound/SoundSubmix.h"

namespace
{
    constexpr int32 CaptureRate = 48000;
    constexpr int32 CaptureChannels = 2;
    bool bNorthCaptureActive = false;

    class FNorthMixListener final : public ISubmixBufferListener
    {
    public:
        explicit FNorthMixListener(int32 DurationSeconds) : FrameLimit(DurationSeconds * CaptureRate)
        {
            Data.Reserve(FrameLimit * CaptureChannels);
        }

        const FString& GetListenerName() const override
        {
            static const FString Name(TEXT("NorthTowerAudioQA"));
            return Name;
        }

        bool IsRenderingAudio() const override { return true; }

        void OnNewSubmixBuffer(const USoundSubmix*, float* Audio, int32 Count, int32 Channels, int32 Rate, double) override
        {
            FScopeLock Lock(&Mutex);
            if (bClosed) return;
            if (!Audio || Channels != CaptureChannels || Rate != CaptureRate || Count % CaptureChannels != 0)
            {
                bBadFormat = true;
                return;
            }
            const int32 Remaining = FrameLimit * CaptureChannels - Data.Num();
            Data.Append(Audio, FMath::Min(Count, Remaining));
        }

        int32 Frames() const
        {
            FScopeLock Lock(&Mutex);
            return bBadFormat ? -1 : Data.Num() / CaptureChannels;
        }

        TArray<float> Close()
        {
            FScopeLock Lock(&Mutex);
            bClosed = true;
            return MoveTemp(Data);
        }

    private:
        mutable FCriticalSection Mutex;
        const int32 FrameLimit;
        TArray<float> Data;
        bool bBadFormat = false;
        bool bClosed = false;
    };

    bool WriteNativeWave(TArray<float>& Samples, const FString& File)
    {
        FBufferArchive Bytes;
        const auto Tag = [&Bytes](const ANSICHAR* Name) { Bytes.Serialize(const_cast<ANSICHAR*>(Name), 4); };
        uint32 DataSize = Samples.Num() * sizeof(float);
        uint32 RiffSize = 48 + DataSize;
        uint32 FormatSize = 16, Rate = CaptureRate, ByteRate = CaptureRate * CaptureChannels * sizeof(float);
        uint16 Format = 3, Channels = CaptureChannels, Align = CaptureChannels * sizeof(float), Bits = 32;
        uint32 FactSize = 4, Frames = Samples.Num() / CaptureChannels;
        Tag("RIFF"); Bytes << RiffSize;
        Tag("WAVE"); Tag("fmt ");
        Bytes << FormatSize << Format << Channels << Rate << ByteRate << Align << Bits;
        Tag("fact"); Bytes << FactSize << Frames;
        Tag("data"); Bytes << DataSize;
        Bytes.Serialize(Samples.GetData(), DataSize);
        return FFileHelper::SaveArrayToFile(Bytes, *File);
    }

    bool UpdateCaptureListener(UWorld& World, FAudioDevice& Device, float Delta, FVector* ReportLocation = nullptr)
    {
        const APlayerController* PlayerController = World.GetFirstPlayerController();
        if (!PlayerController) return false;
        FVector Location, Front, Right;
        PlayerController->GetAudioListenerPosition(Location, Front, Right);
        FTransform Transform(FRotationMatrix::MakeFromXY(Front, Right));
        Transform.SetTranslation(Location);
        Transform.NormalizeRotation();
        if (!Transform.IsValid()) return false;

        // The game viewport normally does this in Draw. NullRHI skips that path.
        Device.SetListener(&World, 0, Transform, Delta);
        FVector AttenuationOverride;
        if (PlayerController->GetAudioListenerAttenuationOverridePosition(AttenuationOverride))
            Device.SetListenerAttenuationOverride(0, AttenuationOverride);
        else
            Device.ClearListenerAttenuationOverride(0);
        if (ReportLocation) *ReportLocation = Location;
        return true;
    }

    enum class ENorthMixAction : uint8
    {
        Checkpoint, Door, Relic, Fog, Footstep, Theme, Phase2, Charge, Pause, Resume,
        Phase3, Dense, Victory, Restore, DefeatSetup, Defeat, Retry, EndingStart, EndingStop, ArchiveShelves, GardenLanding,
        StartWalk, StartSprint, StopWalking
    };

    struct FNorthMixMarker
    {
        float At;
        ENorthMixAction Action;
        const TCHAR* Name;
    };

    bool NorthMixAction(ENorthMixAction Action, ADuelMode& Mode, ADuelArena& Arena, ADuelFighter& Player, EDuelCharacter Character)
    {
        const FVector At = Player.GetActorLocation();
        switch (Action)
        {
        case ENorthMixAction::StartWalk:
        case ENorthMixAction::StartSprint:
        case ENorthMixAction::StopWalking:
        {
            auto* PC = Mode.GetWorld()->GetFirstPlayerController();
            if (!PC) return false;
            if (Action == ENorthMixAction::StartWalk)
            {
                Player.SetActorLocation(FVector(-1730,-1510,-1637));
                PC->SetControlRotation(FRotator::ZeroRotator);
            }
            const bool bPressed=Action!=ENorthMixAction::StopWalking;
            const auto Key=[&](FKey Code) {PC->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE,
                Code,bPressed?IE_Pressed:IE_Released,bPressed?1.f:0.f,false,0));};
            if(Action!=ENorthMixAction::StartWalk) Key(EKeys::LeftShift);
            if(Action!=ENorthMixAction::StartSprint) Key(EKeys::W);
            UE_LOG(LogTemp,Display,TEXT("Recorded gait: action=%d position=%s speed=%.1f"),int32(Action),
                *Player.GetActorLocation().ToString(),Player.GetVelocity().Size2D());
            return true;
        }
        case ENorthMixAction::Checkpoint: Arena.Sound(EDuelSound::Checkpoint, At); break;
        case ENorthMixAction::Door: Arena.Sound(EDuelSound::DoorLatch, At); break;
        case ENorthMixAction::Relic: Arena.Sound(EDuelSound::RelicPickup, At); break;
        case ENorthMixAction::Fog: Arena.Sound(EDuelSound::FogAdmission, At); break;
        case ENorthMixAction::Footstep: Arena.Sound(EDuelSound::Footstep, At); break;
        case ENorthMixAction::Theme: Arena.StartScore(Character); break;
        case ENorthMixAction::Phase2: Arena.IntensifyScore(2); break;
        case ENorthMixAction::Charge: Arena.Sound(EDuelSound::Charge, At); break;
        case ENorthMixAction::Pause:
            Mode.TogglePause();
            return UGameplayStatics::IsGamePaused(&Mode);
        case ENorthMixAction::Resume:
            Mode.TogglePause();
            return !UGameplayStatics::IsGamePaused(&Mode);
        case ENorthMixAction::Phase3:
            Arena.IntensifyScore(3);
            Arena.Sound(EDuelSound::VerdictBell, At);
            break;
        case ENorthMixAction::Dense:
            Arena.Sound(EDuelSound::Pulse, At);
            Arena.Sound(EDuelSound::Charge, At);
            Arena.Sound(EDuelSound::HeavyImpact, At);
            break;
        case ENorthMixAction::Victory:
            Arena.StopScore();
            Arena.Sound(EDuelSound::Victory, At);
            break;
        case ENorthMixAction::Restore:
            Mode.ReturnToCheckpoint();
            return Mode.Outcome() == EDuelOutcome::Ready;
        case ENorthMixAction::DefeatSetup:
            if (Player.SelectedCharacter() != Character && !Player.SelectCharacter(Character)) return false;
            Mode.ResetEncounter(true);
            return Mode.Outcome() == EDuelOutcome::Fighting;
        case ENorthMixAction::Defeat:
            if (!Mode.GetBossFighter() || !Player.ReceiveDuelDamage(DuelCourt::PlayerMaxHealth * 4.f, Mode.GetBossFighter()->GetActorLocation())) return false;
            return Mode.Outcome() == EDuelOutcome::Defeat;
        case ENorthMixAction::Retry:
            Mode.Confirm();
            return Mode.Outcome() == EDuelOutcome::Ready;
        case ENorthMixAction::ArchiveShelves:
        case ENorthMixAction::GardenLanding:
        {
            const FName Anchor = Action == ENorthMixAction::ArchiveShelves ? TEXT("ChapelJudgments") : TEXT("GardenEdge");
            const auto* Landmark = DuelNorthLayout::FindLandmark(Anchor);
            return Landmark && Player.SetActorLocation(Landmark->Position + FVector(0,0,64), false, nullptr, ETeleportType::TeleportPhysics);
        }
        case ENorthMixAction::EndingStart: Arena.SetNorthEndingMusic(true); break;
        case ENorthMixAction::EndingStop: Arena.SetNorthEndingMusic(false); break;
        }
        return true;
    }

    void StartNorthRuntimeCapture(const TArray<FString>& Args, UWorld* World)
    {
        const TCHAR* Command = FCommandLine::Get();
        const auto Fail = [](const TCHAR* Message)
        {
            UE_LOG(LogTemp, Error, TEXT("NorthAudioCapture: %s"), Message);
            FPlatformMisc::RequestExitWithStatus(false, 2);
        };
        if (!FParse::Param(Command, TEXT("DuelCaptureMix"))) return;
        auto* Mode = World ? World->GetAuthGameMode<ADuelMode>() : nullptr;
        FString Output, UserDir;
        if (bNorthCaptureActive || Args.Num() != 1 || !Mode || !Mode->UsesNorthTower() ||
            !FParse::Param(Command, TEXT("DeterministicAudio")) || !FParse::Param(Command, TEXT("DuelNoSave")) ||
            !FParse::Param(Command, TEXT("NullRHI")) || !FParse::Value(Command, TEXT("UserDir="), UserDir) || FPaths::IsRelative(UserDir) ||
            !FParse::Value(Command, TEXT("DuelMixOutput="), Output) || FPaths::IsRelative(Output) ||
            !World->AllowAudioPlayback() || !FApp::CanEverRenderAudio())
        {
            Fail(TEXT("Expected one scenario, a North Tower world, offline audio, NullRHI, no save and absolute test directories."));
            return;
        }
        FAudioDeviceHandle Device = World->GetAudioDevice();
        if (!Device || !Device->IsNonRealtime()) { Fail(TEXT("Non-realtime renderer was not created.")); return; }
        auto* Player = Cast<ADuelFighter>(UGameplayStatics::GetPlayerPawn(World, 0));
        ADuelArena* Arena = nullptr;
        for (TActorIterator<ADuelArena> It(World); It; ++It) { Arena = *It; break; }
        if (!Player || !Arena || !GEngine || !GEngine->UseSound())
        { Fail(TEXT("Player, arena and sound playback must exist before recording.")); return; }

        using A = ENorthMixAction;
        EDuelCharacter Character = EDuelCharacter::Ema;
        TArray<FNorthMixMarker> Markers;
        int32 Seconds = 0;
        const FString& Scenario = Args[0];
        if (Scenario == TEXT("StoneBoots"))
        {
            Seconds=9;
            Markers={{1.f,A::StartWalk,TEXT("walk_on_dungeon_flagstones")},{4.f,A::StartSprint,TEXT("hold_shift")},
                {7.0f,A::StopWalking,TEXT("release_movement")}};
        }
        else if (Scenario == TEXT("FloorMusic"))
        {
            Seconds = 13;
            Markers = {{4.f, A::ArchiveShelves, TEXT("enter_archive")}, {8.5f, A::GardenLanding, TEXT("enter_garden")}};
        }
        else if (Scenario == TEXT("Gate"))
        {
            Seconds = 10;
            Markers = {{1.2f, A::Checkpoint, TEXT("checkpoint")}, {3.f, A::Door, TEXT("door_latch")},
                {4.75f, A::Relic, TEXT("relic")}, {6.5f, A::Fog, TEXT("fog_admission")},
                {8.2f, A::Footstep, TEXT("footstep")}, {8.9f, A::Victory, TEXT("nonspatial_victory_control")}};
        }
        else if (Scenario == TEXT("Ema") || Scenario == TEXT("Hiro") || Scenario == TEXT("Yuki"))
        {
            Character = Scenario == TEXT("Ema") ? EDuelCharacter::Ema : Scenario == TEXT("Hiro") ? EDuelCharacter::Hiro : EDuelCharacter::Yuki;
            Seconds = 22;
            Markers = {{1.f, A::Theme, TEXT("theme_start")}, {4.2f, A::Charge, TEXT("phase1_charge")},
                {5.5f, A::Phase2, TEXT("phase2_layer")}, {8.5f, A::Pause, TEXT("pause")}, {10.5f, A::Resume, TEXT("resume")},
                {13.5f, A::Phase3, TEXT("phase3_layer_and_bell")}, {16.5f, A::Dense, TEXT("dense_pulse_charge_impact")},
                {18.5f, A::Victory, TEXT("victory_mix")}, {20.5f, A::Restore, TEXT("checkpoint_restore")}};
        }
        else if (Scenario == TEXT("EmaDefeat") || Scenario == TEXT("HiroDefeat"))
        {
            Character = Scenario == TEXT("EmaDefeat") ? EDuelCharacter::Ema : EDuelCharacter::Hiro;
            Seconds = 10;
            Markers = {{1.f, A::DefeatSetup, TEXT("actual_ready_to_fighting")}, {4.f, A::Defeat, TEXT("actual_fighter_fell")},
                {6.6f, A::Retry, TEXT("actual_confirm_respawn")}};
        }
        else if (Scenario == TEXT("Ending"))
        {
            Seconds = 12;
            Markers = {{1.f, A::EndingStart, TEXT("ending_music_start")}, {4.f, A::Pause, TEXT("pause")},
                {6.0f, A::Resume, TEXT("resume")}, {8.5f, A::EndingStop, TEXT("ending_music_stop")}};
        }
        else { Fail(TEXT("Expected FloorMusic, Gate, Ema, Hiro, Yuki, EmaDefeat, HiroDefeat or Ending.")); return; }

        FString Base = Output / Scenario;
        if (!IFileManager::Get().MakeDirectory(*Output, true) || FPaths::FileExists(Base + TEXT(".wav")) ||
            FPaths::FileExists(Base + TEXT("-events.csv")))
        {
            Fail(TEXT("Output path must be writable and the scenario files must be new."));
            return;
        }
        // Background volume is overridden only after the offline device is verified.
        FApp::SetUnfocusedVolumeMultiplier(1.f);
        FApp::SetVolumeMultiplier(1.f);
        FTransform PriorListener;
        Device->GetListenerTransform(0, PriorListener);
        FVector ActiveListener;
        if (!UpdateCaptureListener(*World, *Device, 0.f, &ActiveListener))
        { Fail(TEXT("Could not send the player camera listener to the offline device.")); return; }
        UE_LOG(LogTemp, Display, TEXT("NorthAudioCapture: listener %s -> %s, pawn distance %.2f cm, app volume %.3f, device muted=%d"),
            *PriorListener.GetLocation().ToCompactString(), *ActiveListener.ToCompactString(),
            FVector::Distance(ActiveListener, Player->GetActorLocation()), FApp::GetVolumeMultiplier(), Device->IsAudioDeviceMuted());
        auto Listener = MakeShared<FNorthMixListener, ESPMode::ThreadSafe>(Seconds);
        Device->RegisterSubmixBufferListener(Listener, Device->GetMainSubmixObject());
        bNorthCaptureActive = true;
        UE_LOG(LogTemp, Display, TEXT("NorthAudioCapture: offline main submix attached, scenario=%s"), *Scenario);
        FTSTicker::GetCoreTicker().AddTicker(TEXT("NorthTowerAudioQA"), 0.f,
            [Device = MoveTemp(Device), Listener, Markers = MoveTemp(Markers), Seconds, Base, Character,
                ModeRef = TWeakObjectPtr<ADuelMode>(Mode), ArenaRef = TWeakObjectPtr<ADuelArena>(Arena),
                PlayerRef = TWeakObjectPtr<ADuelFighter>(Player), Event = 0,
                Began = FPlatformTime::Seconds(), Rows = TArray<FString>{TEXT("event,mix_frame,world_seconds,paused,ok")}, Fail](float Delta) mutable
            {
                int32 Frames = Listener->Frames();
                bool bValid = Frames >= 0 && ModeRef.IsValid() && ArenaRef.IsValid() && PlayerRef.IsValid() &&
                    FPlatformTime::Seconds() - Began < 150. && UpdateCaptureListener(*ModeRef->GetWorld(), *Device, Delta);
                while (bValid && Event < Markers.Num() && Frames >= FMath::RoundToInt(Markers[Event].At * CaptureRate))
                {
                    const auto& Mark = Markers[Event++];
                    bValid = NorthMixAction(Mark.Action, *ModeRef.Get(), *ArenaRef.Get(), *PlayerRef.Get(), Character);
                    Rows.Add(FString::Printf(TEXT("%s,%d,%.6f,%d,%d"), Mark.Name, Frames, ModeRef->GetWorld()->GetTimeSeconds(),
                        UGameplayStatics::IsGamePaused(ModeRef.Get()), bValid));
                }
                if (bValid && Frames < Seconds * CaptureRate) return true;
                Device->UnregisterSubmixBufferListener(Listener, Device->GetMainSubmixObject());
                auto Samples = Listener->Close();
                bNorthCaptureActive = false;
                const bool bWroteEvents = FFileHelper::SaveStringArrayToFile(Rows, *(Base + TEXT("-events.csv")));
                if (!bValid || !bWroteEvents || Event != Markers.Num() || Samples.Num() != Seconds * CaptureRate * CaptureChannels ||
                    !WriteNativeWave(Samples, Base + TEXT(".wav")))
                {
                    Fail(TEXT("Capture did not complete with the expected native format, actions and frame count."));
                    return false;
                }
                float Peak = 0.f;
                int32 Invalid = 0;
                for (float Sample : Samples)
                {
                    if (FMath::IsFinite(Sample)) Peak = FMath::Max(Peak, FMath::Abs(Sample));
                    else ++Invalid;
                }
                UE_LOG(LogTemp, Display, TEXT("NorthAudioCapture: saved %d exact 48 kHz stereo float frames to %s (peak=%.8f, nonfinite=%d)"),
                    Frames, *Base, Peak, Invalid);
                if (Peak <= 1.e-4f || Invalid)
                {
                    Fail(TEXT("Native samples are silent or nonfinite. The WAV and the action CSV are retained."));
                    return false;
                }
                FPlatformMisc::RequestExitWithStatus(false, 0);
                return false;
            });
    }

    FAutoConsoleCommandWithWorldAndArgs NorthRuntimeAudioCommand(
        TEXT("Duel.CaptureNorthAudio"), TEXT("Record an isolated North Tower runtime audio scenario."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&StartNorthRuntimeCapture));
}
#endif
