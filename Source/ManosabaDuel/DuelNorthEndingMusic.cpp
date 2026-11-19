#include "DuelArena.h"
#include "DuelGraphicsSettings.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Audio.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sound/SoundWaveProcedural.h"

void ADuelArena::SetNorthEndingMusic(bool bPlaying)
{
    if (UAudioComponent* Current = NorthEndingMusic.Get())
    {
        const bool bFadingOut = Current->GetPlayState() == EAudioComponentPlayState::FadingOut;
        if (!bPlaying)
        {
            if (Current->IsPlaying() && !bFadingOut) Current->FadeOut(.45f, 0.f);
            return;
        }
        if (Current->IsPlaying() && !bFadingOut) return;
        Current->Stop();
    }
    if (!bPlaying || !GetWorld() || !GetWorld()->AllowAudioPlayback()) return;

    auto* Track = LoadObject<USoundBase>(nullptr,
        TEXT("/Game/DuelArt/Audio/S_LaVI_Bavellabion.S_LaVI_Bavellabion"));
    const float Gain = FMath::Max(.0001f, EndingMusicGain * FDuelGraphicsSettings::AudioGain(EDuelGraphicsRow::MusicAudio));
    if (auto* Audio = UGameplayStatics::CreateSound2D(this, Track, Gain))
    {
        Audio->bIsUISound = false;
        Audio->bStopWhenOwnerDestroyed = true;
        NorthEndingMusic = Audio;
        Audio->FadeIn(.30f);
        if (Audio->IsPlaying()) UE_LOG(LogTemp, Log, TEXT("North ending music started: %s"), *Track->GetPathName());
    }
}

void ADuelArena::MixNorthExplorationScore(bool bSpeaking, float Fade)
{
    if (auto* Audio = ExplorationMusic.Get()) Audio->AdjustVolume(Fade, bSpeaking ? .36f : 1.f);
    if (auto* Audio = NorthEndingMusic.Get(); Audio && Audio->GetPlayState() != EAudioComponentPlayState::FadingOut)
        Audio->AdjustVolume(Fade, bSpeaking ? .48f : 1.f);
}

void ADuelArena::SetNorthExplorationScore(FName Cue)
{
    if (Cue == ExplorationMusicCue || !GetWorld() || !GetWorld()->AllowAudioPlayback()) return;
    ExplorationMusicCue = Cue;
    if (auto* Old = ExplorationMusic.Get()) Old->FadeOut(.7f, 0.f);
    ExplorationMusic.Reset();
    if (Cue.IsNone()) return;
    TArray<uint8> File;
    FWaveModInfo Header;
    const FString Path = FPaths::ProjectContentDir() / TEXT("DuelVoices/Score") / (Cue.ToString()+TEXT(".wav"));
    if (!FFileHelper::LoadFileToArray(File, *Path) || !Header.ReadWaveInfo(File.GetData(), File.Num()) ||
        *Header.pFormatTag != 1 || *Header.pChannels != 1 || *Header.pBitsPerSample != 16 || *Header.pSamplesPerSec != 24000 || Header.SampleDataSize < 2)
    {
        UE_LOG(LogTemp, Warning, TEXT("Invalid exploration score %s"), *Path);
        return;
    }
    auto* Wave = NewObject<USoundWaveProcedural>(this);
    Wave->SetSampleRate(24000);
    Wave->NumChannels = 1;
    Wave->Duration = INDEFINITELY_LOOPING_DURATION;
    Wave->bLooping = true;
    Wave->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
    TArray<uint8> PCM(Header.SampleDataStart, Header.SampleDataSize);
    Wave->QueueAudio(PCM.GetData(), PCM.Num());
    Wave->OnSoundWaveProceduralUnderflow.BindLambda([Samples = MoveTemp(PCM)](USoundWaveProcedural* Source, int32 Needed)
    {
        if (Source->GetAvailableAudioByteCount() < Needed * int32(sizeof(int16))) Source->QueueAudio(Samples.GetData(), Samples.Num());
    });
    const float Gain = .48f * FDuelGraphicsSettings::AudioGain(EDuelGraphicsRow::MusicAudio);
    if (auto* Audio = UGameplayStatics::CreateSound2D(this, Wave, Gain))
    {
        ExplorationMusic = Audio;
        Audio->bIsUISound = false;
        Audio->bStopWhenOwnerDestroyed = true;
        Audio->FadeIn(.8f, FDuelGraphicsSettings::AudioGain(EDuelGraphicsRow::VoiceAudio) > KINDA_SMALL_NUMBER && ActiveVoice.IsValid() && VoiceUntil > GetWorld()->GetTimeSeconds() ? .36f : 1.f);
        UE_LOG(LogTemp, Log, TEXT("North exploration score started: %s"), *Cue.ToString());
    }
}
