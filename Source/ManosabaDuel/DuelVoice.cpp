#include "DuelArena.h"
#include "DuelGraphicsSettings.h"

#include "Audio.h"
#include "Components/AudioComponent.h"
#include "DuelGame.h"
#include "DuelNorthNarrative.h"
#include "DuelNorthTower.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWaveProcedural.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#endif

namespace
{
    struct FVoiceSource
    {
        const TCHAR* Filename;
        const TCHAR* Subtitle;
        uint32 PCMChecksum = 0;
        const TCHAR* FollowupSubtitle = nullptr;
        float FollowupAt = 0.f;
    };

    struct FVoiceSources
    {
        FVoiceSource Primary;
        FVoiceSource Alternate{nullptr, nullptr};
    };

    constexpr FVoiceSource EmaHurt{TEXT("Ema_Hurt.wav"), TEXT("怎、怎么会……！"), 0x536b5a8c};
    constexpr FVoiceSources Sources[][static_cast<uint8>(EDuelVoice::Count)] = {
        {
            {{TEXT("Hiro_Attack.wav"), TEXT("（攻击喊声）")}},
            {{TEXT("Hiro_Art.wav"), TEXT("那是在撒谎吧。"), 0x24f33557}},
            {{TEXT("Hiro_Hurt.wav"), TEXT("（受击声）")}, {TEXT("Hiro_Attack.wav"), TEXT("呀……！？")}},
            {{TEXT("Hiro_Intro.wav"), TEXT("不要随便碰我。"), 0x94e0c2fe}},
            {{TEXT("Hiro_Witch.wav"), TEXT("你搞错了。我不是邪恶。"), 0x76c844ac}},
            {{TEXT("Hiro_Defeat.wav"), TEXT("我讨厌你的事实不会改变。永远不会。"), 0x7d3aace2}},
        },
        {
            {{TEXT("North_Ema_Kyan.wav"), TEXT("呀……！？"), 0xfe556260}, {TEXT("Ema_Exclamation.wav"), TEXT("真的假的……"), 0xccba10a0}},
            {{TEXT("Ema_Art.wav"), TEXT("（施放战技）")}, {TEXT("Ema_Exclamation.wav"), TEXT("真的假的……"), 0xccba10a0}},
            {EmaHurt},
            {{TEXT("Ema_HiroChan.wav"), TEXT("希罗酱！虽然发生了很多事，能再见到你真好！"), 0xdd4fffa5},
                {TEXT("Ema_Intro.wav"), TEXT("希罗！"), 0xb8058d0f}},
            {{TEXT("Ema_Witch.wav"), TEXT("明明大家都没有错……"), 0xf86dea24}},
            {{TEXT("Ema_Defeat.wav"), TEXT("怎、怎么会……！"), 0xec091659, TEXT("我怎么可能会撒谎……！"), 1.72f}},
        },
        {
            {},
            {},
            {},
            {{TEXT("Yuki_Intro.wav"), TEXT("如果这样就能让你们服气的话……"), 0x9edcbdd1}},
            {{TEXT("Yuki_Witch.wav"), TEXT("没有我的判决，一切都不算完。"), 0x6e717baf}},
            {{TEXT("Yuki_Defeat.wav"), TEXT("这一页还有空白。"), 0x52824aec, TEXT("等你们自己来写吧。"), 2.908f}},
            {{TEXT("Yuki_Loss.wav"), TEXT("证词的最后一句是我删的。"), 0xea47aae7, TEXT("我会在更正书上署名。"), 3.981f}},
        },
    };
    // Source IDs, exact edits and hashes are in COMBAT_VOICE_PROVENANCE346.json.
    // North never falls back to the old synthetic testimony dialogue.
    constexpr FVoiceSource NorthKiang{TEXT("North_Ema_Kyan.wav"), TEXT("呀……！？"), 0xfe556260};
    constexpr FVoiceSources NorthSources[][static_cast<uint8>(EDuelVoice::Count)] = {
        {
            {{TEXT("North_Hiro_Effort.wav"), TEXT("（呼气声）"), 0x2d4e02d6}},
            {{TEXT("North_Hiro_Art_Calm.wav"), TEXT("你在说谎。"), 0xa2131f4d},
                {TEXT("North_Hiro_Art_Resolve.wav"), TEXT("不会让你为所欲为……"), 0xa0506d23}},
            {{TEXT("North_Hiro_Hurt.wav"), TEXT("（受伤的喘息）"), 0x3d656f2a}},
            {{TEXT("North_Hiro_Intro.wav"), TEXT("我会保护你。"), 0x82dddd71}},
            {{TEXT("North_Hiro_Witch.wav"), TEXT("可恶！这种东西……！"), 0x5a0f6830}},
            {{TEXT("North_Hiro_Defeat.wav"), TEXT("（痛苦的呻吟）"), 0xda69b796}},
            {{TEXT("North_Hiro_Loss.wav"), TEXT("……是我的错。"), 0xb9a776cf}},
        },
        {
            {NorthKiang},
            {{TEXT("North_Ema_Art.wav"), TEXT("住、住手！"), 0xe04e7ed4},
                {TEXT("North_Ema_Art_Stop.wav"), TEXT("快、快住手……！"), 0x5b7affa6}},
            {{TEXT("North_Ema_Hurt.wav"), TEXT("（受伤的喘息）"), 0x6fa9bd03}},
            {{TEXT("North_Ema_HiroChan.wav"), TEXT("希罗酱！"), 0xf04033da}},
            {{TEXT("North_Ema_Witch.wav"), TEXT("（惊惧的喊声）"), 0xa6beacb7},
                {TEXT("North_Ema_Witch_Enough.wav"), TEXT("你说够了吗。"), 0x1fb98fc2}},
            {{TEXT("North_Ema_Defeat.wav"), TEXT("（痛苦的呻吟）"), 0x9586bed1}},
            {{TEXT("North_Ema_Loss.wav"), TEXT("……怎么会……"), 0x32899dce}},
        },
        {
            {},
            {{TEXT("North_Yuki_Art.wav"), TEXT("全都去死吧。"), 0xb6566dcb},
                {TEXT("North_Yuki_Art_Sentence.wav"), TEXT("判处——死刑。"), 0xfa6d7292}},
            {},
            {{TEXT("North_Yuki_Intro.wav"), TEXT("想说的话，就这些了吗？"), 0xbe8967a9},
                {TEXT("North_Yuki_Intro_Execution.wav"), TEXT("……处决全人类。"), 0x92e74d7a}},
            {{TEXT("North_Yuki_Witch.wav"), TEXT("我已经受够了。"), 0x0e6a7960}},
            {},
            {{TEXT("North_Yuki_Loss.wav"), TEXT("我早就已经输给你了。"), 0x4ad26dbb}},
        },
    };
    static_assert(UE_ARRAY_COUNT(NorthSources) == UE_ARRAY_COUNT(Sources));

    const FVoiceSources& VoiceSources(bool bNorth, int32 Character, EDuelVoice Cue)
    {
        return bNorth ? NorthSources[Character][static_cast<uint8>(Cue)] : Sources[Character][static_cast<uint8>(Cue)];
    }

    bool NorthCanSpeak(int32 Character, EDuelVoice Cue, bool bBoss)
    {
        if (Character < 0 || Character >= UE_ARRAY_COUNT(NorthSources) || static_cast<uint8>(Cue) >= static_cast<uint8>(EDuelVoice::Count)) return false;
        const FVoiceSource& Source = VoiceSources(true, Character, Cue).Primary;
        if (!Source.Filename || !Source.PCMChecksum) return false;
        if (Cue == EDuelVoice::Defeat) return !bBoss;
        if (Cue == EDuelVoice::Loss) return bBoss;
        return true;
    }

    bool NorthNamedCallMatches(EDuelCharacter Speaker, EDuelVoice Cue, EDuelCharacter Listener)
    {
        return Speaker != EDuelCharacter::Ema || Cue != EDuelVoice::Intro || Listener == EDuelCharacter::Hiro;
    }

    int32 NorthVoicePriority(EDuelVoice Cue)
    {
        if (Cue == EDuelVoice::Defeat || Cue == EDuelVoice::Loss) return 3;
        if (Cue == EDuelVoice::Witch) return 2;
        return Cue == EDuelVoice::Intro ? 1 : 0;
    }

    bool VerifiedVoice(const FVoiceSource& Source, uint32 SampleRate, uint32 Checksum)
    {
        return SampleRate == 24000 && Source.PCMChecksum != 0 && Source.PCMChecksum == Checksum;
    }
    constexpr const TCHAR* EffortCaptions[] = {TEXT("（攻击喊声）"), TEXT("（施放战技）"), TEXT("（受击声）")};
    static_assert(UE_ARRAY_COUNT(EffortCaptions) == static_cast<uint8>(EDuelVoice::Intro));
    constexpr float NormalRain = .57f;
    constexpr float NormalScore = .94f;
    constexpr float WitchIdleGain = .01f;
    constexpr float WitchPhaseGain = .98f;
    constexpr int32 NorthTextureRate = 24000;
    constexpr float NorthTexturePeak = .16f;
    constexpr float NorthTextureGain = .30f;
    constexpr float VoiceMusicScale = .25118864f;
    constexpr float NorthVoiceMusicScale = .17782794f; // -15 dB relative to the normal layer gain.
    constexpr float VoiceDuckAttack = .055f;
    constexpr float NorthVoiceDuckAttack = .032f;
    constexpr float VoiceDuckRelease = .40f;
    constexpr float NorthCombatVoiceGain = 1.18f;
    constexpr float VoiceTargetRms = 3950.f;
    constexpr float VoicePeakLimit = 25200.f;

    bool VoiceClipActive(const UAudioComponent* Audio, double Now, double Until)
    {
        // IsPlaying includes pause. Game time also pauses, so a paused sentence
        // retains its mix target without treating a stopped component as speech.
        return Audio && Audio->IsPlaying() && Until > Now;
    }

    bool VoiceShouldDuck(bool bClipActive, float VoiceGain)
    {
        return bClipActive && VoiceGain > KINDA_SMALL_NUMBER;
    }

    float ScoreGain(bool bDuck, float Normal, bool bNorth)
    {
        return bDuck ? Normal * (bNorth ? NorthVoiceMusicScale : VoiceMusicScale) : Normal;
    }

    float RainGain(bool bDuck, bool bInterior, bool bNorth)
    {
        const float Bed = bDuck ? (bNorth ? .095f : .13f) : NormalRain;
        return Bed * (bInterior ? .31f : 1.f);
    }

    float PrepareVoicePCM(const FWaveModInfo& Header, float MaxGain, TArray<uint8>& PCM)
    {
        const int32 Samples = Header.SampleDataSize / sizeof(int16);
        int32 Peak = 0;
        double Power = 0.0;
        for (int32 I = 0; I < Samples; ++I)
        {
            int16 Sample;
            FMemory::Memcpy(&Sample, Header.SampleDataStart + I * sizeof(Sample), sizeof(Sample));
            Peak = FMath::Max(Peak, FMath::Abs(static_cast<int32>(Sample)));
            Power += FMath::Square(static_cast<double>(Sample));
        }
        const double RMS = FMath::Sqrt(Power / Samples);
        if (Peak < 96 || RMS < 26.0) return 0.f;

        const float Gain = FMath::Min3(MaxGain, VoicePeakLimit / Peak, static_cast<float>(VoiceTargetRms / RMS));
        PCM.SetNumUninitialized(Header.SampleDataSize);
        for (int32 I = 0; I < Samples; ++I)
        {
            int16 Sample;
            FMemory::Memcpy(&Sample, Header.SampleDataStart + I * sizeof(Sample), sizeof(Sample));
            const float Fade = FMath::Min(1.f, FMath::Min(I / (.012f * *Header.pSamplesPerSec),
                (Samples - 1 - I) / (.045f * *Header.pSamplesPerSec)));
            Sample = static_cast<int16>(Sample * Gain * Fade);
            FMemory::Memcpy(PCM.GetData() + I * sizeof(Sample), &Sample, sizeof(Sample));
        }
        return Gain;
    }

    float WitchFader(int32 Phase, bool bTexture)
    {
        if (Phase < 2) return WitchIdleGain;
        return bTexture && Phase == 2 ? .60f : WitchPhaseGain;
    }

    TArray<int16> MakeNorthWitchTexture(EDuelCharacter Character)
    {
        const bool bEma = Character == EDuelCharacter::Ema;
        FRandomStream Noise(bEma ? 1481 : 2817);
        const float Lower = 1.f - FMath::Exp(-2.f * PI * (bEma ? 620.f : 1250.f) / NorthTextureRate);
        const float Upper = 1.f - FMath::Exp(-2.f * PI * (bEma ? 1350.f : 2800.f) / NorthTextureRate);
        TArray<int16> PCM;
        PCM.SetNumUninitialized(4 * NorthTextureRate);
        constexpr int32 EdgeSamples = NorthTextureRate * 12 / 1000;
        float SmoothLow = 0.f;
        float SmoothHigh = 0.f;
        int32 Peak = 1;
        for (int32 I = 0; I < PCM.Num(); ++I)
        {
            const float White = Noise.FRandRange(-1.f, 1.f);
            SmoothLow += Lower * (White - SmoothLow);
            SmoothHigh += Upper * (White - SmoothHigh);
            const float LoopPhase = 2.f * PI * I / PCM.Num();
            const float Motion = .74f + .16f * FMath::Sin(LoopPhase) + .10f * FMath::Sin(3.f * LoopPhase + 1.3f);
            const float Edge = FMath::Min(1.f, float(FMath::Min(I, PCM.Num() - 1 - I)) / EdgeSamples);
            PCM[I] = static_cast<int16>((SmoothHigh - SmoothLow) * Motion * Edge * 16383.f);
            Peak = FMath::Max(Peak, FMath::Abs(int32(PCM[I])));
        }
        const float Normalize = NorthTexturePeak * 32767.f / Peak;
        for (int16& Sample : PCM) Sample = static_cast<int16>(Sample * Normalize);
        return PCM;
    }

    USoundWaveProcedural* NewNorthWitchTexture(UObject* Owner, EDuelCharacter Character)
    {
        // Atonal textures sit beneath the independent themes. The pitched fallback
        // stems stay paired with the fallback scores they were composed against.
        TArray<int16> PCM = MakeNorthWitchTexture(Character);
        auto* Wave = NewObject<USoundWaveProcedural>(Owner);
        Wave->SetSampleRate(NorthTextureRate);
        Wave->NumChannels = 1;
        Wave->Duration = float(PCM.Num()) / NorthTextureRate;
        Wave->bLooping = true;
        Wave->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
        Wave->QueueAudio(reinterpret_cast<const uint8*>(PCM.GetData()), PCM.Num() * sizeof(int16));
        Wave->OnSoundWaveProceduralUnderflow.BindLambda([Samples = MoveTemp(PCM)](USoundWaveProcedural* Source, int32 Needed)
        {
            if (Source->GetAvailableAudioByteCount() < Needed * int32(sizeof(int16)))
                Source->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * sizeof(int16));
        });
        return Wave;
    }
}

void ADuelArena::LoadVoices()
{
    const ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    const bool bNorth = Mode && Mode->UsesNorthTower();
    const FString Folders[] = {
        FPaths::Combine(FPlatformProcess::UserSettingsDir(), FApp::GetProjectName(), TEXT("Saved"), TEXT("Voice")),
        FPaths::ProjectSavedDir() / TEXT("Voice"),
        FPaths::ProjectContentDir() / TEXT("DuelVoices"),
    };
    constexpr int64 MaxBytes = 512 * 1024;
    const auto Load = [&](FVoiceClip& Clip, const FVoiceSource& Source, int32 Character, EDuelVoice Cue)
    {
        if (!Source.Filename)
        {
            return false;
        }
        for (const FString& Folder : Folders)
        {
            const FString Path = Folder / Source.Filename;
            const int64 Size = IFileManager::Get().FileSize(*Path);
            TArray<uint8> Bytes;
            if (Size < 44 || Size > MaxBytes || !FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.Num() != Size)
            {
                continue;
            }
            FWaveModInfo Header;
            const bool bStory = Cue >= EDuelVoice::Intro;
            if (!Header.ReadWaveInfo(Bytes.GetData(), Bytes.Num()) ||
                *Header.pFormatTag != FWaveModInfo::WAVE_INFO_FORMAT_PCM || *Header.pChannels != 1 ||
                *Header.pBitsPerSample != 16 || *Header.pSamplesPerSec < 8000 || *Header.pSamplesPerSec > 48000 ||
                Header.SampleDataSize < *Header.pSamplesPerSec / 5 || (Header.SampleDataSize % 2) != 0 ||
                Header.SampleDataSize > *Header.pSamplesPerSec * (bStory ? 20 : 6))
            {
                UE_LOG(LogTemp, Warning, TEXT("Duel voice needs mono PCM16 audio within its cue duration: %s"), Source.Filename);
                continue;
            }

            const uint32 Checksum = FCrc::MemCrc32(Header.SampleDataStart, Header.SampleDataSize);
            const bool bHiro = Character == static_cast<int32>(EDuelCharacter::Hiro);
            const bool bLegacyHurt = !bNorth && bHiro && (Checksum == 0xe73953fd || Checksum == 0x10673ee1);
            const bool bLegacyRoute = bHiro && Cue == EDuelVoice::Hurt && FCString::Strcmp(Source.Filename, TEXT("Hiro_Attack.wav")) == 0;
            if ((bLegacyHurt && Cue != EDuelVoice::Hurt) || (bLegacyRoute && !bLegacyHurt))
            {
                continue;
            }
            const bool bVerified = VerifiedVoice(Source, *Header.pSamplesPerSec, Checksum);
            const bool bPreferredExclamation = Cue == EDuelVoice::Attack &&
                FCString::Strcmp(Source.Filename, TEXT("Ema_Exclamation.wav")) == 0;
            if ((bNorth || bStory || bPreferredExclamation) && !bVerified)
            {
                UE_LOG(LogTemp, Warning, TEXT("Duel voice does not match its subtitle: %s"), Source.Filename);
                continue;
            }

            if (PrepareVoicePCM(Header, 2.f, Clip.PCM) <= 0.f)
            {
                UE_LOG(LogTemp, Warning, TEXT("Duel voice is too quiet to play: %s"), Source.Filename);
                continue;
            }
            Clip.SampleRate = *Header.pSamplesPerSec;
            Clip.Subtitle = bLegacyHurt ? TEXT("呀……！？") :
                (bVerified ? Source.Subtitle : EffortCaptions[static_cast<uint8>(Cue)]);
            Clip.FollowupSubtitle = bVerified ? Source.FollowupSubtitle : nullptr;
            Clip.FollowupAt = bVerified ? Source.FollowupAt : 0.f;
            UE_LOG(LogTemp, Display, TEXT("Duel voice loaded: %s (cue=%d, verified=%d)"),
                Source.Filename, static_cast<int32>(Cue), bVerified || bLegacyHurt);
            return true;
        }
        return false;
    };
    for (int32 Character = 0; Character < UE_ARRAY_COUNT(Sources); ++Character)
    {
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Sources[Character]); ++Index)
        {
            FVoiceClip& Clip = Voices[Character][Index];
            const EDuelVoice Cue = static_cast<EDuelVoice>(Index);
            const FVoiceSources& Files = VoiceSources(bNorth, Character, Cue);
            Clip = FVoiceClip{};
            NorthVoiceAlternates[Character][Index] = FVoiceClip{};
            bNextNorthVoiceAlternate[Character][Index] = false;
            if (bNorth)
            {
                Load(Clip, Files.Primary, Character, Cue);
                Load(NorthVoiceAlternates[Character][Index], Files.Alternate, Character, Cue);
            }
            else if (!Load(Clip, Files.Primary, Character, Cue))
            {
                Load(Clip, Files.Alternate, Character, Cue);
            }
        }
    }
    if (bNorth)
        UE_LOG(LogTemp, Display, TEXT("North audio: 18 reviewed combat slots and 5 boss alternates use character-specific, checksum-locked recordings. The retired Hiro shout has no runtime route."));
}

bool ADuelArena::PlayNorthDialogueVoice(const FString& Recording)
{
    StopVoice(0.f);
    if (bVoicePaused || Recording.IsEmpty()) return false;

    constexpr int64 MaxBytes = 2 * 1024 * 1024;
    const int64 Size = IFileManager::Get().FileSize(*Recording);
    if (Size < 44 || Size > MaxBytes) return false;

    TArray<uint8> Bytes;
    FWaveModInfo Header;
    if (!FFileHelper::LoadFileToArray(Bytes, *Recording) || Bytes.Num() != Size ||
        !Header.ReadWaveInfo(Bytes.GetData(), Bytes.Num()) ||
        *Header.pFormatTag != FWaveModInfo::WAVE_INFO_FORMAT_PCM || *Header.pChannels != 1 ||
        *Header.pBitsPerSample != 16 || *Header.pSamplesPerSec < 8000 || *Header.pSamplesPerSec > 48000 ||
        Header.SampleDataSize < *Header.pSamplesPerSec / 5 || Header.SampleDataSize % 2 ||
        Header.SampleDataSize > *Header.pSamplesPerSec * 40)
    {
        UE_LOG(LogTemp, Warning, TEXT("North dialogue recording requires a mono PCM16 WAV of 0.1 to 20 seconds: %s"),
            *FPaths::GetCleanFilename(Recording));
        return false;
    }
    TArray<uint8> PCM;
    const float InputGain = PrepareVoicePCM(Header, 4.f, PCM);
    if (InputGain <= 0.f) return false;

    auto* Wave = NewObject<USoundWaveProcedural>(this);
    Wave->SetSampleRate(*Header.pSamplesPerSec);
    Wave->NumChannels = 1;
    Wave->Duration = Header.SampleDataSize / (2.f * *Header.pSamplesPerSec);
    Wave->SoundGroup = SOUNDGROUP_Voice;
    Wave->Priority = 6.f;
    Wave->bBypassVolumeScaleForPriority = true;
    Wave->QueueAudio(PCM.GetData(), PCM.Num());
    ActiveVoiceGain = 1.f;
    const float Volume = FMath::Max(.0001f, FDuelGraphicsSettings::AudioGain(EDuelGraphicsRow::VoiceAudio));
    UAudioComponent* Audio = UGameplayStatics::SpawnSound2D(this, Wave, Volume);
    if (!Audio) return false;
    ActiveVoice = Audio;
    Audio->ComponentTags.Add(TEXT("NorthDialogueVoice"));
    VoiceUntil = GetWorld()->GetTimeSeconds() + Wave->Duration + .14f;
    UpdateVoiceMix(true);
    UE_LOG(LogTemp, Display, TEXT("North local conversation recording started: %s, %.2f s, playing=%d, input gain=%.2fx"),
        *FPaths::GetCleanFilename(Recording), Wave->Duration, Audio->IsPlaying(), InputGain);
    return true;
}

bool ADuelArena::CanPlayVoice(EDuelCharacter Character, EDuelVoice Cue, double Now) const
{
    const uint8 Index = static_cast<uint8>(Character);
    const uint8 CueIndex = static_cast<uint8>(Cue);
    if (Index >= UE_ARRAY_COUNT(Voices) || CueIndex >= static_cast<uint8>(EDuelVoice::Count))
    {
        return false;
    }
    return Now >= NextVoiceAt && Now >= NextCharacterVoiceAt[Index] &&
        !Voices[Index][CueIndex].PCM.IsEmpty();
}

bool ADuelArena::PlayVoice(EDuelCharacter Character, EDuelVoice Cue, bool bBoss, const FVector& At)
{
    return StartVoice(Character, Cue, bBoss, At);
}

bool ADuelArena::StartVoice(EDuelCharacter Character, EDuelVoice Cue, bool bBoss, const FVector& At)
{
    const uint8 Index = static_cast<uint8>(Character);
    const uint8 CueIndex = static_cast<uint8>(Cue);
    if (Index >= UE_ARRAY_COUNT(Voices) || CueIndex >= static_cast<uint8>(EDuelVoice::Count) ||
        !GetWorld()->AllowAudioPlayback())
    {
        return false;
    }
    const ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    const bool bNorth = Mode && Mode->UsesNorthTower();
    if (bNorth && !NorthCanSpeak(Index, Cue, bBoss)) return false;
    if (bNorth && (!Mode->GetPlayerFighter() ||
        !NorthNamedCallMatches(Character, Cue, Mode->GetPlayerFighter()->SelectedCharacter()))) return false;
    const bool bNorthDefeat = bNorth && Cue == EDuelVoice::Defeat;
    if (bNorthDefeat && (Mode->Outcome() != EDuelOutcome::Defeat || !Mode->GetPlayerFighter() ||
        Mode->GetPlayerFighter()->IsAlive() || Mode->GetPlayerFighter()->SelectedCharacter() != Character))
    {
        return false;
    }
    if (bNorth && Cue == EDuelVoice::Loss && (!bBoss || !Mode->GetBossFighter() ||
        Mode->GetBossFighter()->IsAlive() || Mode->GetBossFighter()->SelectedCharacter() != Character)) return false;
    if (bNorth && Mode->NorthTower() && Mode->NorthTower()->Narrative() &&
        Mode->NorthTower()->Narrative()->IsTalking()) return false;
    const double Now = GetWorld()->GetTimeSeconds();
    // Entry, transformation and either side's defeat outrank routine effort cooldowns.
    const bool bStory = Cue >= EDuelVoice::Intro;
    const bool bValidScene = Mode && (Mode->Outcome() == EDuelOutcome::Fighting ||
        (Cue == EDuelVoice::Defeat && Mode->Outcome() == EDuelOutcome::Defeat) ||
        (Cue == EDuelVoice::Loss && Mode->Outcome() == EDuelOutcome::Victory));
    if (!bValidScene || bVoicePaused || (!bStory && (Mode->DialogueVisible() || !CanPlayVoice(Character, Cue, Now))))
    {
        return false;
    }
    const bool bHasAlternate = bNorth && bBoss && !NorthVoiceAlternates[Index][CueIndex].PCM.IsEmpty();
    const bool bAlternate = bHasAlternate && bNextNorthVoiceAlternate[Index][CueIndex];
    const FVoiceClip& Clip = bAlternate ? NorthVoiceAlternates[Index][CueIndex] : Voices[Index][CueIndex];
    if (Clip.PCM.IsEmpty() || Clip.SampleRate <= 0 || (Clip.PCM.Num() % sizeof(int16)) != 0)
    {
        return false;
    }
    const FName EventTag(*FString::Printf(TEXT("NorthVoice_%d_%d_%d"), Index, CueIndex, bBoss ? 1 : 0));
    const int32 Priority = NorthVoicePriority(Cue);
    if (bNorth && VoiceClipActive(ActiveVoice.Get(), Now, VoiceUntil))
    {
        const auto* Playing = ActiveVoice.Get();
        if (Playing->ComponentHasTag(TEXT("NorthDialogueVoice"))) return false;
        if (Playing->ComponentHasTag(EventTag)) return true;
        for (int32 Higher = Priority + 1; Higher <= 3; ++Higher)
            if (Playing->ComponentHasTag(FName(*FString::Printf(TEXT("NorthVoicePriority_%d"), Higher)))) return false;
    }
    if (!VoiceAttenuation)
    {
        VoiceAttenuation = NewObject<USoundAttenuation>(this);
        VoiceAttenuation->Attenuation.AttenuationShapeExtents = FVector(880);
        VoiceAttenuation->Attenuation.FalloffDistance = 2600.f;
    }
    USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(this);
    Wave->SetSampleRate(Clip.SampleRate);
    Wave->NumChannels = 1;
    Wave->Duration = Clip.PCM.Num() / (2.f * Clip.SampleRate);
    Wave->SoundGroup = SOUNDGROUP_Voice;
    Wave->Priority = bStory ? 6.f : 3.5f;
    Wave->bBypassVolumeScaleForPriority = true;
    Wave->QueueAudio(Clip.PCM.GetData(), Clip.PCM.Num());
    ActiveVoiceGain = bNorth ? NorthCombatVoiceGain : (bStory || !bBoss ? 1.f : 1.09f);
    const float VoiceVolume = FMath::Max(.0001f, ActiveVoiceGain * FDuelGraphicsSettings::AudioGain(EDuelGraphicsRow::VoiceAudio));
    UAudioComponent* Audio = bStory ? UGameplayStatics::SpawnSound2D(this, Wave, VoiceVolume) :
        UGameplayStatics::SpawnSoundAtLocation(this, Wave, At, FRotator::ZeroRotator,
            VoiceVolume, 1.f, 0.f, VoiceAttenuation);
    if (!Audio)
    {
        return false;
    }
    if (bHasAlternate) bNextNorthVoiceAlternate[Index][CueIndex] = !bAlternate;
    EndVoice(.045f);
    ActiveVoice = Audio;
    if (bNorth)
    {
        Audio->ComponentTags.Add(EventTag);
        Audio->ComponentTags.Add(FName(*FString::Printf(TEXT("NorthVoicePriority_%d"), Priority)));
    }
    VoiceUntil = Now + Wave->Duration + .14f; // bounded drain allowance for procedural audio.
    UpdateVoiceMix(true);
    CaptionUntil = Now + FMath::Max(bStory ? 2.65f : 1.65f, Wave->Duration + (bStory ? .9f : .48f));
    NextVoiceAt = FMath::Max(NextVoiceAt, Now + FMath::Max(bStory ? 3.1f : 5.2f, Wave->Duration + 1.1f));
    const float SpeakerSpacing = Cue == EDuelVoice::Hurt ? 10.f : bStory ? 5.6f : 8.5f;
    NextCharacterVoiceAt[Index] = FMath::Max(NextCharacterVoiceAt[Index], Now + SpeakerSpacing);
    bVoiceIsBoss = bBoss;
    bVoiceIsStory = bStory;
    Caption = Clip.Subtitle;
    FollowupCaption = Clip.FollowupSubtitle;
    FollowupCaptionAt = Now + Clip.FollowupAt;
    UE_LOG(LogTemp, Verbose, TEXT("Duel voice played: character=%d cue=%d boss=%d alternate=%d duration=%.2f caption=%.2f"),
        Index, CueIndex, bBoss, bAlternate, Wave->Duration, CaptionUntil - Now);
    return true;
}

bool ADuelArena::TryBossLine(EDuelCharacter Character, EDuelVoice Cue, const FVector& At)
{
    // Critical events bypass effort cooldowns; repeating an active event preserves its recording.
    return StartVoice(Character, Cue, true, At);
}

void ADuelArena::RefreshAudioLevels()
{
    const float Voice = FDuelGraphicsSettings::AudioGain(EDuelGraphicsRow::VoiceAudio);
    const float Music = FDuelGraphicsSettings::AudioGain(EDuelGraphicsRow::MusicAudio);
    if (auto* Audio = ActiveVoice.Get()) Audio->SetVolumeMultiplier(FMath::Max(.0001f, ActiveVoiceGain * Voice));
    if (BossScore) BossScore->SetVolumeMultiplier(FMath::Max(.0001f, BossScoreGain * Music));
    if (WitchScore) WitchScore->SetVolumeMultiplier(FMath::Max(.0001f, WitchScoreGain * Music));
    if (auto* Audio = NorthEndingMusic.Get()) Audio->SetVolumeMultiplier(FMath::Max(.0001f, EndingMusicGain * Music));
    if (auto* Audio = ExplorationMusic.Get()) Audio->SetVolumeMultiplier(.48f * Music);
    UpdateVoiceMix(true);
}

void ADuelArena::StartScore(EDuelCharacter Character)
{
    StopScore();
    if (!GetWorld()->AllowAudioPlayback())
    {
        return;
    }
    const bool bDuck = VoiceShouldDuck(VoiceClipActive(ActiveVoice.Get(), GetWorld()->GetTimeSeconds(), VoiceUntil),
        FDuelGraphicsSettings::AudioGain(EDuelGraphicsRow::VoiceAudio));
    const float AttackFade = bNorthOrigin ? NorthVoiceDuckAttack : VoiceDuckAttack;
    const auto StartTrack = [this, bDuck](USoundBase* Track, float Gain, float InitialFader)
    {
        const float Volume = FMath::Max(.0001f, Gain * FDuelGraphicsSettings::AudioGain(EDuelGraphicsRow::MusicAudio));
        UAudioComponent* Audio = Track ? UGameplayStatics::SpawnSound2D(this, Track, Volume) : nullptr;
        if (Audio)
        {
            // Spawn gain is the original layer gain; only the volume fader ducks.
            // Scaling both would double the attenuation and persist after speech.
            Audio->AdjustVolume(0.f, ScoreGain(bDuck, InitialFader, bNorthOrigin));
            Audio->SetPaused(bVoicePaused);
        }
        return Audio;
    };
    const auto Start = [&StartTrack](const TCHAR* Name, float Gain, float InitialFader = 1.f)
    {
        const FString Path = FString::Printf(TEXT("/Game/DuelArt/Audio/%s.%s"), Name, Name);
        return StartTrack(LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet), Gain, InitialFader);
    };
    if (Character != EDuelCharacter::Yuki)
    {
        // Source EBU R128 levels: LaVI -6.3 LUFS; MAdEaR -6.5 LUFS.
        BossScoreGain = Character == EDuelCharacter::Ema ? .262f : .268f;
        WitchScoreGain = NorthTextureGain;
        BossScore = Start(Character == EDuelCharacter::Ema ? TEXT("S_LaVI_Bavellabion") : TEXT("S_MAdEaR_MAdEaR"), BossScoreGain);
        if (BossScore)
        {
            BossScore->AdjustVolume(bDuck ? AttackFade : .45f, ScoreGain(bDuck, NormalScore, bNorthOrigin));
            if (const ADuelMode* Mode = GetWorld()->GetAuthGameMode<ADuelMode>(); Mode && Mode->UsesNorthTower())
            {
                WitchScore = StartTrack(NewNorthWitchTexture(this, Character), NorthTextureGain, WitchIdleGain);
                bNorthWitchTexture = WitchScore != nullptr;
            }
            return;
        }
    }
    BossScoreGain = Character == EDuelCharacter::Yuki ? 1.62f : .95f;
    WitchScoreGain = Character == EDuelCharacter::Yuki ? .61f : .41f;
    BossScore = Start(Character == EDuelCharacter::Yuki ? TEXT("S_YukiTrial") :
        Character == EDuelCharacter::Ema ? TEXT("S_EmaTrial") : TEXT("S_HiroTrial"), BossScoreGain);
    WitchScore = Start(Character == EDuelCharacter::Yuki ? TEXT("S_YukiWitch") :
        Character == EDuelCharacter::Ema ? TEXT("S_EmaWitch") : TEXT("S_HiroWitch"), WitchScoreGain, WitchIdleGain);
    if (BossScore)
    {
        BossScore->AdjustVolume(bDuck ? AttackFade : .78f, ScoreGain(bDuck, NormalScore, bNorthOrigin));
    }
    if (WitchScore)
    {
        WitchScore->AdjustVolume(.02f, ScoreGain(bDuck, WitchFader(ScorePhase, bNorthWitchTexture), bNorthOrigin));
    }
}

void ADuelArena::IntensifyScore(int32 Phase)
{
    ScorePhase = Phase;
    const bool bDuck = VoiceShouldDuck(VoiceClipActive(ActiveVoice.Get(), GetWorld()->GetTimeSeconds(), VoiceUntil),
        FDuelGraphicsSettings::AudioGain(EDuelGraphicsRow::VoiceAudio));
    if (bDuck)
    {
        // A phase ramp cannot override the ongoing speech duck on either layer.
        UpdateVoiceMix(true);
    }
    else if (WitchScore)
    {
        WitchScore->AdjustVolume(1.42f, WitchFader(ScorePhase, bNorthWitchTexture));
    }
}

void ADuelArena::StopScore(float Fade)
{
    for (UAudioComponent* Track : {BossScore.Get(), WitchScore.Get()})
    {
        if (Track)
        {
            if (Fade <= 0.f) Track->Stop();
            else
            {
                Track->SetPaused(false);
                Track->FadeOut(Fade, 0.f);
            }
        }
    }
    BossScore = WitchScore = nullptr;
    ScorePhase = 1;
    bNorthWitchTexture = false;
}

void ADuelArena::StopVoice(float Fade)
{
    EndVoice(Fade);
    if (Fade <= 0.f)
    {
        if (UAudioComponent* Audio = EndingVoice.Get())
        {
            Audio->Stop();
        }
        EndingVoice.Reset();
    }
    Caption = nullptr;
    FollowupCaption = nullptr;
    FollowupCaptionAt = 0.0;
    CaptionUntil = 0.0;
    bVoiceIsStory = false;
    UpdateVoiceMix(false);
}

void ADuelArena::EndVoice(float Fade)
{
    if (UAudioComponent* Audio = ActiveVoice.Get())
    {
        if (UAudioComponent* Prior = EndingVoice.Get())
        {
            Prior->Stop();
        }
        if (bVoicePaused || Fade <= 0.f)
        {
            Audio->Stop();
            EndingVoice.Reset();
        }
        else
        {
            Audio->FadeOut(Fade, 0.f);
            EndingVoice = Audio;
        }
    }
    ActiveVoice.Reset();
    VoiceUntil = 0.0;
}

void ADuelArena::TickVoice()
{
    if (bVoicePaused)
    {
        return;
    }
    const double Now = GetWorld()->GetTimeSeconds();
    if (VoiceUntil > 0.0 && !VoiceClipActive(ActiveVoice.Get(), Now, VoiceUntil))
    {
        EndVoice(0.f);
        UpdateVoiceMix(false);
    }
    if (Caption && Now >= CaptionUntil)
    {
        Caption = nullptr;
        FollowupCaption = nullptr;
        bVoiceIsStory = false;
    }
}

void ADuelArena::UpdateVoiceMix(bool bDuck)
{
    // Captions, cooldown and a stale weak pointer alone must never duck music.
    bDuck = VoiceShouldDuck(bDuck && VoiceClipActive(ActiveVoice.Get(), GetWorld()->GetTimeSeconds(), VoiceUntil),
        FDuelGraphicsSettings::AudioGain(EDuelGraphicsRow::VoiceAudio));
    const float Fade = bDuck ? (bNorthOrigin ? NorthVoiceDuckAttack : VoiceDuckAttack) : VoiceDuckRelease;
    MixNorthExplorationScore(bDuck, Fade);
    if (RainBed && !bRainMuted)
    {
        RainBed->AdjustVolume(Fade, RainGain(bDuck, bCastleInterior, bNorthOrigin));
    }
    if (BossScore)
    {
        BossScore->AdjustVolume(Fade, ScoreGain(bDuck, NormalScore, bNorthOrigin));
    }
    if (WitchScore)
    {
        WitchScore->AdjustVolume(Fade, ScoreGain(bDuck, WitchFader(ScorePhase, bNorthWitchTexture), bNorthOrigin));
    }
}

void ADuelArena::SetRainMuted(bool bMuted, float Fade)
{
    bRainMuted = bMuted;
    if (RainBed)
    {
        // A zero fade target stops the loop. Keep it live through dawn and chapter reloads.
        const bool bDuck = VoiceShouldDuck(VoiceClipActive(ActiveVoice.Get(), GetWorld()->GetTimeSeconds(), VoiceUntil),
            FDuelGraphicsSettings::AudioGain(EDuelGraphicsRow::VoiceAudio));
        RainBed->AdjustVolume(Fade, bRainMuted ? .0001f : RainGain(bDuck, bCastleInterior, bNorthOrigin));
    }
}

void ADuelArena::PauseVoice(bool bPaused)
{
    bVoicePaused = bPaused;
    if (UAudioComponent* Audio = ActiveVoice.Get())
    {
        Audio->SetPaused(bPaused);
    }
    if (bPaused)
    {
        if (UAudioComponent* Audio = EndingVoice.Get())
        {
            Audio->Stop();
        }
        EndingVoice.Reset();
    }
    if (BossScore)
    {
        BossScore->SetPaused(bPaused);
    }
    if (WitchScore)
    {
        WitchScore->SetPaused(bPaused);
    }
    if (auto* Audio = ExplorationMusic.Get()) Audio->SetPaused(bPaused);
    if (auto* Audio = NorthEndingMusic.Get()) Audio->SetPaused(bPaused);
    if (!bPaused)
    {
        // Resume the real clip and reassert its mix before a phase/start ramp can
        // raise music. If the clip ended while paused, restore over the release.
        TickVoice();
        UpdateVoiceMix(VoiceClipActive(ActiveVoice.Get(), GetWorld()->GetTimeSeconds(), VoiceUntil));
    }
}

const TCHAR* ADuelArena::VoiceCaption() const
{
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now >= CaptionUntil)
    {
        return nullptr;
    }
    return FollowupCaption && Now >= FollowupCaptionAt ? FollowupCaption : Caption;
}

float ADuelArena::VoiceCaptionOpacity() const
{
    return FMath::SmoothStep(0.f, .31f, static_cast<float>(CaptionUntil - GetWorld()->GetTimeSeconds()));
}

void ADuelFighter::Speak(EDuelVoice Cue)
{
    Arena->PlayVoice(Character, Cue, bBoss, GetActorLocation() + FVector(0, 0, 66));
}

#if WITH_DEV_AUTOMATION_TESTS
#include "DuelNorthNarrative.h"
#include "DuelNorthLayout.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthAudioTest, "ManosabaDuel.NorthTower.AudioRouting",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthAudioTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    if (!TestTrue(TEXT("Audio QA needs isolated North Tower without saves"),
        FParse::Param(FCommandLine::Get(), TEXT("DuelNorthTower")) && FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")))) return false;
    UWorld* World = nullptr;
    if (GEngine)
        for (const auto& Context : GEngine->GetWorldContexts())
            if (Context.World() && Context.World()->IsGameWorld() && Context.World()->GetAuthGameMode<ADuelMode>() &&
                Context.World()->GetAuthGameMode<ADuelMode>()->UsesNorthTower()) { World = Context.World(); break; }
    if (!TestNotNull(TEXT("An actual North Tower world is running"), World)) return false;
    ADuelArena* Arena = nullptr;
    for (TActorIterator<ADuelArena> It(World); It; ++It) { Arena = *It; break; }
    if (!TestNotNull(TEXT("Actual Arena has loaded its voice table"), Arena)) return false;

    TArray<uint8> Bytes;
    FWaveModInfo Wave;
    if (!TestTrue(TEXT("Staged Kiang reaction exists and is a readable WAV"),
        FFileHelper::LoadFileToArray(Bytes, *(FPaths::ProjectContentDir() / TEXT("DuelVoices") / NorthKiang.Filename)) &&
        Wave.ReadWaveInfo(Bytes.GetData(), Bytes.Num()))) return false;
    TestEqual(TEXT("Reaction is mono"), *Wave.pChannels, static_cast<uint16>(1));
    TestEqual(TEXT("Reaction is PCM16"), *Wave.pBitsPerSample, static_cast<uint16>(16));
    const uint32 CRC = FCrc::MemCrc32(Wave.SampleDataStart, Wave.SampleDataSize);
    TestTrue(TEXT("Exact reviewed reaction passes the runtime checksum gate"), VerifiedVoice(NorthKiang, *Wave.pSamplesPerSec, CRC));
    TestFalse(TEXT("A changed same-named recording is rejected"), VerifiedVoice(NorthKiang, *Wave.pSamplesPerSec, CRC ^ 1u));
    TestFalse(TEXT("A recording with the wrong rate is rejected"), VerifiedVoice(NorthKiang, 48000, CRC));
    const float Duration = Wave.SampleDataSize / (2.f * *Wave.pSamplesPerSec);
    TestTrue(TEXT("Act two trial 08 game take retains its full voiced reaction"),
        FMath::IsNearlyEqual(Duration, .39225f, .001f));

    int32 Loaded = 0, Alternates = 0, AllowedBossCues = 0;
    TSet<uint32> DistinctRecordings;
    for (int32 Character = 0; Character < UE_ARRAY_COUNT(NorthSources); ++Character)
    {
        for (int32 Index = 0; Index < static_cast<int32>(EDuelVoice::Count); ++Index)
        {
            const EDuelVoice Cue = static_cast<EDuelVoice>(Index);
            const auto& Source = VoiceSources(true, Character, Cue);
            if (Source.Primary.Filename) ++Loaded;
            if (Source.Alternate.Filename) ++Alternates;
            for (int32 Variant = 0; Variant < 2; ++Variant)
            {
                const auto& File = Variant == 0 ? Source.Primary : Source.Alternate;
                const auto& Clip = Variant == 0 ? Arena->Voices[Character][Index] : Arena->NorthVoiceAlternates[Character][Index];
                const FString Label = FString::Printf(TEXT("character=%d cue=%d variant=%d"), Character, Index, Variant);
                TestTrue(*(Label + TEXT(" cannot route the retired Hiro shout")), File.PCMChecksum != 0x46145fb8);
                if (!File.Filename)
                {
                    TestTrue(*(Label + TEXT(" has no legacy PCM fallback")), Clip.PCM.IsEmpty());
                    continue;
                }
                DistinctRecordings.Add(File.PCMChecksum);
                TestFalse(*(Label + TEXT(" is actually loaded in Arena")), Clip.PCM.IsEmpty());
                TestEqual(*(Label + TEXT(" uses 24 kHz")), Clip.SampleRate, 24000);
                TestTrue(*(Label + TEXT(" preserves the exact caption")),
                    Clip.Subtitle && FCString::Strcmp(Clip.Subtitle, File.Subtitle) == 0);
                TArray<uint8> StagedBytes;
                FWaveModInfo StagedWave;
                const FString Recording = FPaths::ProjectContentDir() / TEXT("DuelVoices") / File.Filename;
                const bool bStaged = FFileHelper::LoadFileToArray(StagedBytes, *Recording) &&
                    StagedWave.ReadWaveInfo(StagedBytes.GetData(), StagedBytes.Num());
                if (!TestTrue(*(Label + TEXT(" ships its exact reviewed recording")), bStaged)) return false;
                TestTrue(*(Label + TEXT(" raw PCM passes its own checksum gate")),
                    *StagedWave.pChannels == 1 && *StagedWave.pBitsPerSample == 16 &&
                    VerifiedVoice(File, *StagedWave.pSamplesPerSec,
                        FCrc::MemCrc32(StagedWave.SampleDataStart, StagedWave.SampleDataSize)));
                TestTrue(*(Label + TEXT(" cannot load a same-named substituted recording")),
                    !VerifiedVoice(File, *StagedWave.pSamplesPerSec, File.PCMChecksum ^ 1u));
                if (File.PCMChecksum == NorthKiang.PCMChecksum && !Clip.PCM.IsEmpty())
                {
                    int32 Peak = 0; double Energy = 0;
                    for (int32 Offset = 0; Offset + 1 < Clip.PCM.Num(); Offset += sizeof(int16))
                    {
                        int16 Sample; FMemory::Memcpy(&Sample, Clip.PCM.GetData() + Offset, sizeof(Sample));
                        Peak = FMath::Max(Peak, FMath::Abs(int32(Sample)));
                        Energy += double(Sample) * Sample;
                    }
                    const double RMS = FMath::Sqrt(Energy / (Clip.PCM.Num() / sizeof(int16)));
                    TestTrue(*(Label + TEXT(" Game reaction reaches the voice mix without clipping")),
                        RMS > 3000. && RMS <= 3951. && Peak <= 25200);
                }
                if (NorthCanSpeak(Character, Cue, true))
                    TestNull(*(Label + TEXT(" cannot continue into an old spoken sentence")), Clip.FollowupSubtitle);
                if (Character == static_cast<int32>(EDuelCharacter::Yuki))
                    TestTrue(*(Label + TEXT(" never selects old synthetic testimony dialogue")),
                        FStringView(File.Filename).StartsWith(TEXT("North_Yuki_")));
            }
            if (NorthCanSpeak(Character, Cue, true)) ++AllowedBossCues;
        }
    }
    TestEqual(TEXT("Eighteen reviewed runtime slots load from exact files"), Loaded, 18);
    TestEqual(TEXT("Bosses have five independently verified event alternates"), Alternates, 5);
    TestEqual(TEXT("All primary and alternate recordings are distinct"), DistinctRecordings.Num(), 23);
    TestEqual(TEXT("Bosses use sixteen appropriate cues while player defeat remains separate"), AllowedBossCues, 16);
    TestFalse(TEXT("The replacement Hiro line rejects a saved override containing the retired shout"),
        VerifiedVoice(VoiceSources(true, 0, EDuelVoice::Art).Primary, 24000, 0x46145fb8));
    TestTrue(TEXT("Human and transformed Hiro can use its own verified combat line"),
        NorthCanSpeak(0, EDuelVoice::Art, false) && NorthCanSpeak(0, EDuelVoice::Art, true));
    TestTrue(TEXT("Human and transformed Ema use a real hurt reaction"),
        NorthCanSpeak(1, EDuelVoice::Hurt, false) && NorthCanSpeak(1, EDuelVoice::Hurt, true));
    TestTrue(TEXT("Yuki uses distinct real recordings for spells, entry, transformation and loss"),
        NorthCanSpeak(2, EDuelVoice::Art, true) && NorthCanSpeak(2, EDuelVoice::Intro, true) &&
        NorthCanSpeak(2, EDuelVoice::Witch, true) && NorthCanSpeak(2, EDuelVoice::Loss, true));
    TestTrue(TEXT("Hiro-chan is allowed only when Ema is addressing Hiro"),
        NorthNamedCallMatches(EDuelCharacter::Ema, EDuelVoice::Intro, EDuelCharacter::Hiro) &&
        !NorthNamedCallMatches(EDuelCharacter::Ema, EDuelVoice::Intro, EDuelCharacter::Ema) &&
        !NorthNamedCallMatches(EDuelCharacter::Ema, EDuelVoice::Intro, EDuelCharacter::Yuki));
    TestTrue(TEXT("Entry and transformation bypass effort cooldowns; either defeat outranks transformation"),
        NorthVoicePriority(EDuelVoice::Attack) == 0 && NorthVoicePriority(EDuelVoice::Art) == 0 &&
        NorthVoicePriority(EDuelVoice::Hurt) == 0 && NorthVoicePriority(EDuelVoice::Intro) == 1 &&
        NorthVoicePriority(EDuelVoice::Witch) == 2 && NorthVoicePriority(EDuelVoice::Defeat) == 3 &&
        NorthVoicePriority(EDuelVoice::Loss) == 3);
    TestTrue(TEXT("Human Ema can speak at defeat"), NorthCanSpeak(1, EDuelVoice::Defeat, false));
    TestFalse(TEXT("Ema boss cannot speak at the player's defeat"), NorthCanSpeak(1, EDuelVoice::Defeat, true));
    TestTrue(TEXT("Human Hiro has its own recorded defeat reaction"), NorthCanSpeak(0, EDuelVoice::Defeat, false));
    TestTrue(TEXT("Hiro and Ema have separate boss-loss recordings that players cannot use as defeat"),
        NorthCanSpeak(0, EDuelVoice::Loss, true) && NorthCanSpeak(1, EDuelVoice::Loss, true) &&
        !NorthCanSpeak(0, EDuelVoice::Loss, false) && !NorthCanSpeak(1, EDuelVoice::Loss, false));
    const auto& Defeat = Arena->Voices[static_cast<uint8>(EDuelCharacter::Ema)][static_cast<uint8>(EDuelVoice::Defeat)];
    TestEqual(TEXT("Ema defeat retains the complete reviewed half-second reaction"), Defeat.PCM.Num(), 26400);
    TestNull(TEXT("Ema defeat has no unrelated followup line"), Defeat.FollowupSubtitle);
    TestFalse(TEXT("Invalid character is rejected"), NorthCanSpeak(-1, EDuelVoice::Attack, true));
    TestFalse(TEXT("Invalid cue is rejected"), NorthCanSpeak(1, EDuelVoice::Count, true));
    TestEqual(TEXT("Legacy Yuki witch voice remains routed separately"), VoiceSources(false, 2, EDuelVoice::Witch).Primary.PCMChecksum, 0x6e717bafu);
    TestEqual(TEXT("Legacy Yuki defeat voice remains routed separately"), VoiceSources(false, 2, EDuelVoice::Defeat).Primary.PCMChecksum, 0x52824aecu);
    TestEqual(TEXT("Legacy Yuki conclusion remains routed separately"), VoiceSources(false, 2, EDuelVoice::Loss).Primary.PCMChecksum, 0xea47aae7u);

    const TArray<int16> EmaTexture = MakeNorthWitchTexture(EDuelCharacter::Ema);
    const TArray<int16> HiroTexture = MakeNorthWitchTexture(EDuelCharacter::Hiro);
    TestTrue(TEXT("Ema and Hiro phase textures use distinct samples"), EmaTexture != HiroTexture);
    for (const auto* Texture : {&EmaTexture, &HiroTexture})
    {
        TestEqual(TEXT("Phase texture loops every four seconds"), Texture->Num(), 4 * NorthTextureRate);
        TestTrue(TEXT("Loop seam reaches zero on both sides"), (*Texture)[0] == 0 && Texture->Last() == 0);
        int32 Peak = 0;
        for (int16 Sample : *Texture) Peak = FMath::Max(Peak, FMath::Abs(int32(Sample)));
        TestTrue(TEXT("Texture leaves at least 26 dBFS of peak headroom at full layer gain"),
            Peak > 0 && Peak * NorthTextureGain * WitchPhaseGain < .05f * 32767.f);
    }
    TestTrue(TEXT("Named-theme textures rise in both phases; Yuki's arrangement keeps its original P2 lift"),
        WitchFader(1, true) < WitchFader(2, true) && WitchFader(2, true) < WitchFader(3, true) &&
        WitchFader(1, false) == WitchIdleGain && WitchFader(2, false) == WitchPhaseGain && WitchFader(3, false) == WitchPhaseGain);
    TestTrue(TEXT("North speech ducks both music layers by fifteen decibels at every phase"),
        FMath::IsNearlyEqual(ScoreGain(true, NormalScore, true), NormalScore * NorthVoiceMusicScale) &&
        FMath::IsNearlyEqual(ScoreGain(true, WitchFader(3, true), true), WitchPhaseGain * NorthVoiceMusicScale));
    TestTrue(TEXT("A muted or stopped voice cannot duck music; the first nonzero slider step can"),
        !VoiceShouldDuck(true, 0.f) && !VoiceShouldDuck(true, KINDA_SMALL_NUMBER) &&
        !VoiceShouldDuck(false, 1.f) && VoiceShouldDuck(true, FMath::Square(.05f)));
    TestTrue(TEXT("Dialogue retains independent volume and restores the normal music and rain gains"),
        ScoreGain(false, NormalScore, true) == NormalScore &&
        RainGain(true, true, true) < RainGain(true, true, false) &&
        RainGain(false, true, true) == RainGain(false, true, false) && NorthVoiceDuckAttack < VoiceDuckAttack);
    TestTrue(TEXT("North combat speech keeps digital headroom after the gain increase"),
        NorthCombatVoiceGain * VoicePeakLimit < 32767.f);
    // Attenuate a copy: the installed game reaction is already at playback level.
    TArray<uint8> QuietBytes;
    QuietBytes.Append(Wave.SampleDataStart,static_cast<int32>(Wave.SampleDataSize));
    for(int32 Offset=0;Offset+1<QuietBytes.Num();Offset+=2)
    {
        int16 Sample;
        FMemory::Memcpy(&Sample,QuietBytes.GetData()+Offset,2);
        Sample/=16;
        FMemory::Memcpy(QuietBytes.GetData()+Offset,&Sample,2);
    }
    auto* OriginalSamples=Wave.SampleDataStart;
    Wave.SampleDataStart=QuietBytes.GetData();
    TArray<uint8> QuietDialoguePCM;
    TestTrue(TEXT("Local dialogue can lift a low-level recording without changing its duration"),
        PrepareVoicePCM(Wave, 4.f, QuietDialoguePCM) == 4.f && QuietDialoguePCM.Num() == static_cast<int32>(Wave.SampleDataSize));

    Wave.SampleDataStart=OriginalSamples;
    if (FParse::Param(FCommandLine::Get(), TEXT("DuelCheckAudioComponents")))
    {
        if (!TestTrue(TEXT("Component checks require a running audio device"),
            World->AllowAudioPlayback() && World->GetAudioDeviceRaw() != nullptr)) return false;
        ADuelMode* Mode = World->GetAuthGameMode<ADuelMode>();
        Mode->ReturnToCheckpoint();
        Mode->ResetEncounter(true);
        for (int32 Character = 0; Character < UE_ARRAY_COUNT(NorthSources); ++Character)
        {
            for (int32 Index = 0; Index < static_cast<int32>(EDuelVoice::Count); ++Index)
            {
                const auto Cue = static_cast<EDuelVoice>(Index);
                const auto& Source = VoiceSources(true, Character, Cue);
                if (!Source.Alternate.Filename) continue;
                Arena->bNextNorthVoiceAlternate[Character][Index] = false;
                for (int32 Play = 0; Play < 3; ++Play)
                {
                    Arena->StopVoice(0.f);
                    Arena->NextVoiceAt = Arena->NextCharacterVoiceAt[Character] = 0.;
                    const auto& Expected = Play == 1 ? Source.Alternate : Source.Primary;
                    TestTrue(TEXT("A real boss voice component cycles primary, alternate, primary with its own caption"),
                        Arena->TryBossLine(static_cast<EDuelCharacter>(Character), Cue, Mode->GetBossFighter()->GetActorLocation()) &&
                        Arena->ActiveVoice.IsValid() && Arena->Caption && FCString::Strcmp(Arena->Caption, Expected.Subtitle) == 0 &&
                        Arena->bNextNorthVoiceAlternate[Character][Index] == (Play != 1));
                    const auto* Active = Arena->ActiveVoice.Get();
                    Arena->TryBossLine(static_cast<EDuelCharacter>(Character), Cue, Mode->GetBossFighter()->GetActorLocation());
                    TestTrue(TEXT("An active duplicate or cooldown rejection does not consume the next recording"),
                        Arena->ActiveVoice.Get() == Active && Arena->bNextNorthVoiceAlternate[Character][Index] == (Play != 1));
                }
            }
        }
        Arena->StopVoice(0.f);
        Arena->NextVoiceAt = Arena->NextCharacterVoiceAt[0] = 0.;
        TestTrue(TEXT("Player Hiro keeps the restrained primary without consuming a boss alternate"),
            Arena->PlayVoice(EDuelCharacter::Hiro, EDuelVoice::Art, false, Mode->GetPlayerFighter()->GetActorLocation()) &&
            Arena->Caption && FCString::Strcmp(Arena->Caption, NorthSources[0][static_cast<uint8>(EDuelVoice::Art)].Primary.Subtitle) == 0 &&
            Arena->bNextNorthVoiceAlternate[0][static_cast<uint8>(EDuelVoice::Art)]);
        Mode->ReturnToCheckpoint();
        const EDuelCharacter Characters[] = {EDuelCharacter::Ema, EDuelCharacter::Hiro, EDuelCharacter::Yuki};
        const TCHAR* Themes[] = {TEXT("S_LaVI_Bavellabion"), TEXT("S_MAdEaR_MAdEaR"), TEXT("S_YukiTrial")};
        for (int32 I = 0; I < UE_ARRAY_COUNT(Characters); ++I)
        {
            Arena->StartScore(Characters[I]);
            auto* Main = Arena->BossScore.Get();
            auto* Layer = Arena->WitchScore.Get();
            if (TestNotNull(TEXT("Theme starts"), Main) && TestNotNull(TEXT("Witch layer starts"), Layer))
            {
                TestTrue(TEXT("Theme is the selected original sound"), Main->GetSound() && Main->GetSound()->GetName() == Themes[I]);
                TestTrue(TEXT("Layer belongs to the selected theme"), Layer->GetSound() &&
                    (Characters[I] == EDuelCharacter::Yuki ? Layer->GetSound()->GetName() == TEXT("S_YukiWitch") :
                        Layer->GetSound()->IsA<USoundWaveProcedural>()));
                Arena->IntensifyScore(2);
                TestEqual(TEXT("Score advances to phase 2"), Arena->ScorePhase, 2);
                Arena->PauseVoice(true);
                TestTrue(TEXT("Pause freezes both layers"), Main->GetPlayState() == EAudioComponentPlayState::Paused &&
                    Layer->GetPlayState() == EAudioComponentPlayState::Paused);
                Arena->PauseVoice(false);
                TestTrue(TEXT("Resume restores the same components"), Main == Arena->BossScore.Get() && Layer == Arena->WitchScore.Get() &&
                    Main->IsPlaying() && Main->GetPlayState() != EAudioComponentPlayState::Paused &&
                    Layer->IsPlaying() && Layer->GetPlayState() != EAudioComponentPlayState::Paused);
                Arena->IntensifyScore(3);
                TestEqual(TEXT("Score advances to phase 3"), Arena->ScorePhase, 3);
            }
            Arena->PauseVoice(true);
            Arena->StopScore(0.f);
            TestTrue(TEXT("Checkpoint cleanup stops paused layers and resets to phase 1"), !Arena->BossScore && !Arena->WitchScore &&
                Arena->ScorePhase == 1 && !Arena->bNorthWitchTexture && (!Main || !Main->IsPlaying()) && (!Layer || !Layer->IsPlaying()));
            Arena->PauseVoice(false);
        }
    }

    ADuelFighter* Player = Cast<ADuelFighter>(UGameplayStatics::GetPlayerPawn(World, 0));
    const auto* NameWall = DuelNorthLayout::FindLandmark(TEXT("NameWall"));
    if (!TestNotNull(TEXT("Actual player exists for dialogue handoff"), Player) ||
        !TestNotNull(TEXT("Name-wall dialogue has a map marker"), NameWall)) return false;
    const FTransform OriginalTransform = Player->GetActorTransform();
    const FVector OriginalVelocity = Player->GetCharacterMovement()->Velocity;
    ADuelNorthNarrative* Narrative = World->SpawnActor<ADuelNorthNarrative>();
    ON_SCOPE_EXIT
    {
        if (IsValid(Narrative)) Narrative->Destroy();
        Player->SetActorTransform(OriginalTransform, false, nullptr, ETeleportType::TeleportPhysics);
        Player->GetCharacterMovement()->Velocity = OriginalVelocity;
        Arena->StopVoice();
    };
    if (!TestNotNull(TEXT("Narrative handoff adapter spawns"), Narrative)) return false;
    FDuelNorthProgress State;
    State.Reset(Player->SelectedCharacter());
    Player->SetActorLocation(NameWall->Position + FVector(150, 0, 62), false, nullptr, ETeleportType::TeleportPhysics);
    Narrative->Restore(State, *Player, *Arena);
    Narrative->Update(State, *Player, 1.f / 60.f, true);
    Arena->Caption = TEXT("Old unrelated caption");
    Arena->CaptionUntil = World->GetTimeSeconds() + 30.;
    Arena->bVoiceIsStory = true;
    TestNotNull(TEXT("Handoff fixture has a residual old caption"), Arena->VoiceCaption());
    TestTrue(TEXT("F begins the actual new name-wall conversation"), Narrative->Confirm(TEXT("NameWall"), State).bHandled);
    TestNull(TEXT("New dialogue removes the previous combat caption"), Arena->VoiceCaption());
    TestFalse(TEXT("New dialogue clears old voice-story mode"), Arena->VoiceIsStory());
    AddInfo(TEXT("Validated loaded PCM, routing and phase gains. Speaker checks require captured audio or direct listening."));
    return true;
}
#endif

