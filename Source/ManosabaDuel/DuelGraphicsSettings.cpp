#include "DuelGraphicsSettings.h"

#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "RenderResource.h"
#include "RenderUtils.h"
#include "Scalability.h"
#include "SceneUtils.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#else
#include <cstdio>
#endif

namespace
{
    constexpr const TCHAR* SettingsSection = TEXT("ManosabaDuel.Graphics");
    constexpr EDuelGraphicsRow AudioStart = EDuelGraphicsRow::Master;
    constexpr const TCHAR* AudioKeys[] = {TEXT("MasterVolume"), TEXT("CombatVolume"), TEXT("VoiceVolume"), TEXT("MusicVolume")};
    int32 AudioLevels[] = {100, 100, 100, 100};

    int32 AudioIndex(EDuelGraphicsRow Row)
    {
        const int32 Index = int32(Row) - int32(AudioStart);
        return Index >= 0 && Index < UE_ARRAY_COUNT(AudioKeys) ? Index : INDEX_NONE;
    }

    constexpr const TCHAR* ScreenKey = TEXT("TSRPreset");
    constexpr const TCHAR* HardwareKey = TEXT("HardwareLumen");
    constexpr const TCHAR* SceneQualityKey = TEXT("SceneQuality");
    constexpr const TCHAR* ScreenVariable = TEXT("r.ScreenPercentage");
    constexpr const TCHAR* SecondaryVariable = TEXT("r.SecondaryScreenPercentage.GameViewport");
    constexpr const TCHAR* HardwareVariable = TEXT("r.RayTracing.Enable");

    struct FScalePreset
    {
        const TCHAR* Label;
        float Percentage;
    };
    constexpr FScalePreset Presets[] = {
        { TEXT("Native"), 100.f },
        { TEXT("Quality"), 75.f },
        { TEXT("Balanced"), 67.f },
        { TEXT("自适应 60 FPS"), 67.f },
    };
    constexpr int32 PresetCount = UE_ARRAY_COUNT(Presets);
    constexpr int32 AdaptivePreset = 3;
    constexpr const TCHAR* DynamicMode = TEXT("r.DynamicRes.OperationMode");

    IConsoleVariable* Find(const TCHAR* Name)
    {
        return IConsoleManager::Get().FindConsoleVariable(Name);
    }

    int32 ReadInt(const TCHAR* Name)
    {
        const IConsoleVariable* Variable = Find(Name);
        return Variable ? Variable->GetInt() : 0;
    }

    float ReadFloat(const TCHAR* Name)
    {
        const IConsoleVariable* Variable = Find(Name);
        return Variable ? Variable->GetFloat() : 0.f;
    }

    bool CanOverride(const IConsoleVariable* Variable)
    {
        return Variable && !Variable->TestFlags(ECVF_ReadOnly) &&
            (Variable->GetFlags() & ECVF_SetByMask) <= ECVF_SetByGameOverride;
    }

    bool HasRenderer()
    {
        return FApp::CanEverRender() && GIsRHIInitialized && GDynamicRHI;
    }

    bool HasTSR()
    {
        return HasRenderer() && GetDefaultAntiAliasingMethod(GMaxRHIFeatureLevel) == AAM_TSR;
    }

    bool CanSetTSR()
    {
        if (!HasTSR() || !GEngine || !CanOverride(Find(ScreenVariable)))
        {
            return false;
        }
        // An external fixed 100% output scale is compatible with our primary TSR controls.
        if (!CanOverride(Find(SecondaryVariable)) && !FMath::IsNearlyEqual(ReadFloat(SecondaryVariable), 100.f))
        {
            return false;
        }
        return ReadInt(DynamicMode) == 0 || CanOverride(Find(DynamicMode));
    }

    bool HasHardwareLumen()
    {
#if RHI_RAYTRACING
        if (!HasRenderer() || RHIGetInterfaceType() != ERHIInterfaceType::D3D12 ||
            GMaxRHIFeatureLevel < ERHIFeatureLevel::SM6 || !IsRayTracingAllowed() ||
            !RHISupportsRayTracing(GMaxRHIShaderPlatform) ||
            !(GRHISupportsInlineRayTracing || (GRHISupportsRayTracingShaders && GRHISupportsRayTracingDispatchIndirect)) ||
            ReadInt(TEXT("r.Lumen.HardwareRayTracing")) == 0)
        {
            return false;
        }
        return (ReadInt(TEXT("r.DynamicGlobalIlluminationMethod")) == 1 && ReadInt(TEXT("r.Lumen.DiffuseIndirect.Allow")) != 0) ||
            (ReadInt(TEXT("r.ReflectionMethod")) == 1 && ReadInt(TEXT("r.Lumen.Reflections.Allow")) != 0);
#else
        return false;
#endif
    }

    bool HardwareLumenActive()
    {
        return HasHardwareLumen() && IsRayTracingEnabled(GMaxRHIShaderPlatform);
    }

    bool CanSetHardwareLumen()
    {
        return HasHardwareLumen() && GetRayTracingMode() == ERayTracingMode::Dynamic && CanOverride(Find(HardwareVariable));
    }

    int32 CurrentPreset()
    {
        if (ReadInt(DynamicMode) == 2 && GRHISupportsDynamicResolution) return AdaptivePreset;
        const float Percentage = ReadFloat(ScreenVariable);
        for (int32 Index = 0; Index < PresetCount; ++Index)
        {
            if (FMath::IsNearlyEqual(Percentage, Presets[Index].Percentage, .01f))
            {
                return Index;
            }
        }
        return INDEX_NONE;
    }

    bool ApplyTSR(int32 Preset)
    {
        if (!CanSetTSR())
        {
            return false;
        }
        if (Preset == AdaptivePreset && (!GRHISupportsDynamicResolution || !CanOverride(Find(DynamicMode)))) return false;
        if (CanOverride(Find(DynamicMode)))
            Find(DynamicMode)->Set(Preset == AdaptivePreset ? 2 : 0, ECVF_SetByGameOverride);
        if (IConsoleVariable* Secondary = Find(SecondaryVariable); !FMath::IsNearlyEqual(Secondary->GetFloat(), 100.f))
        {
            Secondary->Set(100.f, ECVF_SetByGameOverride);
        }
        const float Percentage = Presets[Preset].Percentage;
        IConsoleVariable* Screen = Find(ScreenVariable);
        if (!FMath::IsNearlyEqual(Screen->GetFloat(), Percentage, .01f))
        {
            Screen->Set(Percentage, ECVF_SetByGameOverride);
        }
        return CurrentPreset() == Preset && FMath::IsNearlyEqual(ReadFloat(SecondaryVariable), 100.f);
    }

    bool ApplyHardwareLumen(bool bEnable)
    {
        if (!CanSetHardwareLumen())
        {
            return false;
        }
        IConsoleVariable* Variable = Find(HardwareVariable);
        if (Variable->GetInt() != int32(bEnable))
        {
            Variable->Set(int32(bEnable), ECVF_SetByGameOverride);
        }
        return HardwareLumenActive() == bEnable;
    }

    FConfigFile ReadPreferences(const FString& Path)
    {
        FConfigFile Preferences;
        if (IFileManager::Get().FileExists(*Path))
        {
            Preferences.Read(Path);
        }
        return Preferences;
    }

    bool SaveValue(const TCHAR* Key, int32 Value)
    {
        const TCHAR* Command = FCommandLine::Get();
        if (FParse::Param(Command, TEXT("nowrite")) ||
            (FParse::Param(Command, TEXT("Multiprocess")) && !FParse::Param(Command, TEXT("MultiprocessSaveConfig"))))
        {
            return true;
        }

        const FString Path = FDuelGraphicsSettings::PreferencesPath();
        FConfigFile Preferences = ReadPreferences(Path);
        Preferences.SetInt64(SettingsSection, Key, Value);
        FString Contents;
        Preferences.WriteToString(Contents);

        const FString Temp = FPaths::CreateTempFilename(*FPaths::GetPath(Path), TEXT("DuelGraphics-"));
        if (FFileHelper::SaveStringToFile(Contents, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        {
#if PLATFORM_WINDOWS
            if (::MoveFileExW(*Temp, *Path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
#else
            if (std::rename(TCHAR_TO_UTF8(*Temp), TCHAR_TO_UTF8(*Path)) == 0)
#endif
            {
                return true;
            }
        }
        IFileManager::Get().Delete(*Temp, false, false, true);
        return false;
    }

    enum class EScenePreset : int32 { Balanced, High, Performance, Count };

    bool UsesLowMemoryProfile()
    {
        const uint64 Dedicated = GRHIGlobals.GpuInfo.DedicatedVideoMemory;
        return FParse::Param(FCommandLine::Get(), TEXT("DuelLowVRAM")) ||
            (Dedicated > 0 && Dedicated <= 4ull * 1024 * 1024 * 1024);
    }

    EScenePreset CurrentSceneQuality()
    {
        const Scalability::FQualityLevels Current = Scalability::GetQualityLevels();
        if (Current.GlobalIlluminationQuality == 0) return EScenePreset::Performance;
        return Current.ShadowQuality >= 3 && Current.GlobalIlluminationQuality >= 3 ?
            EScenePreset::High : EScenePreset::Balanced;
    }

    void ApplySceneQuality(EScenePreset Preset)
    {
        Scalability::FQualityLevels Quality = Scalability::GetQualityLevels();
        const bool bHigh = Preset == EScenePreset::High;
        const bool bFast = Preset == EScenePreset::Performance;
        Quality.ShadowQuality = bHigh ? 3 : 1;
        Quality.GlobalIlluminationQuality = bHigh ? 3 : (bFast ? 0 : 1);
        Quality.ReflectionQuality = bHigh ? 3 : (bFast ? 0 : 1);
        Quality.AntiAliasingQuality = bHigh ? 3 : (bFast ? 1 : 2);
        Quality.PostProcessQuality = bHigh ? 3 : (bFast ? 1 : 2);
        Quality.EffectsQuality = bHigh ? 3 : 2;
        if (UsesLowMemoryProfile()) Quality.TextureQuality = 2;
        Scalability::SetQualityLevels(Quality);
        // Fixed tower rooms can overlap more than the engine's default 16 shadowed lights.
        if (auto* Variable = Find(TEXT("r.Shadow.Virtual.OnePassProjection.MaxLightsPerPixel")); CanOverride(Variable))
            Variable->Set(32, ECVF_SetByGameOverride);
        if (UsesLowMemoryProfile())
        {
            struct FLimit { const TCHAR* Name; int32 Value; };
            static constexpr FLimit Limits[] = {
                {TEXT("r.Streaming.PoolSize"), 640}, {TEXT("r.Streaming.LimitPoolSizeToVRAM"), 1},
                {TEXT("r.Streaming.MaxTempMemoryAllowed"), 32}, {TEXT("r.Streaming.MaxEffectiveScreenSize"), 1600},
                {TEXT("r.Nanite.Streaming.StreamingPoolSize"), 160}, {TEXT("r.Shadow.Virtual.MaxPhysicalPages"), 512},
                {TEXT("r.DynamicRes.MaxScreenPercentage"), 64}, {TEXT("r.SkeletalMeshLODBias"), 1}
            };
            for (const auto& Limit : Limits)
                if (auto* Variable = Find(Limit.Name); CanOverride(Variable))
                    Variable->Set(Limit.Value, ECVF_SetByGameOverride);
        }
    }

    void LogActiveGraphics()
    {
        UE_LOG(LogTemp, Display, TEXT("Duel graphics: TSR=%d primary=%.0f%% output=%.0f%% HWRTAvailable=%d LumenHardwareActive=%d shadow_lights_per_pixel=%d"),
            HasTSR(), ReadFloat(ScreenVariable), ReadFloat(SecondaryVariable), HasHardwareLumen(), HardwareLumenActive(),
            ReadInt(TEXT("r.Shadow.Virtual.OnePassProjection.MaxLightsPerPixel")));
    }
}

FString FDuelGraphicsSettings::PreferencesPath()
{
    // The engine can reset GameUserSettings during validation. This file is owned by the duel.
    return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("DuelGraphics.ini"));
}

float FDuelGraphicsSettings::AudioGain(EDuelGraphicsRow Row)
{
    const int32 Index = AudioIndex(Row);
    return Index != INDEX_NONE ? FMath::Square(AudioLevels[Index] * .01f) : 1.f;
}

void FDuelGraphicsSettings::Restore()
{
    const FConfigFile Preferences = ReadPreferences(PreferencesPath());
    for (int32 I = 0; I < UE_ARRAY_COUNT(AudioKeys); ++I)
    {
        int32 Volume = 100;
        Preferences.GetInt(SettingsSection, AudioKeys[I], Volume);
        AudioLevels[I] = FMath::Clamp(Volume, 0, 100);
    }
    FApp::SetVolumeMultiplier(AudioGain(EDuelGraphicsRow::Master));
    if (!HasRenderer() || (FParse::Param(FCommandLine::Get(), TEXT("DuelPerfProbe")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("DuelPerfUsePlayerProfile"))))
    {
        return;
    }
    const int32 DefaultScalePreset = GRHISupportsDynamicResolution && CanOverride(Find(DynamicMode)) ? AdaptivePreset : 2;
    int32 Preset = DefaultScalePreset;
    int32 Hardware = 0;
    int32 SceneQuality = UsesLowMemoryProfile() ? int32(EScenePreset::Performance) : int32(EScenePreset::Balanced);
    Preferences.GetInt(SettingsSection, ScreenKey, Preset);
    Preferences.GetInt(SettingsSection, HardwareKey, Hardware);
    Preferences.GetInt(SettingsSection, SceneQualityKey, SceneQuality);
    if (FParse::Param(FCommandLine::Get(), TEXT("DuelLowVRAM")))
    {
        SceneQuality = int32(EScenePreset::Performance);
        Preset = DefaultScalePreset;
        Hardware = 0;
    }
    ApplySceneQuality(SceneQuality >= 0 && SceneQuality < int32(EScenePreset::Count) ?
        EScenePreset(SceneQuality) : EScenePreset::Balanced);
    if (!ApplyTSR(Preset >= 0 && Preset < PresetCount ? Preset : DefaultScalePreset))
        ApplyTSR(2);
    ApplyHardwareLumen(Hardware == 1);
    UE_LOG(LogTemp, Display, TEXT("North memory profile: low=%d dedicated_mib=%llu pool_mib=%d nanite_mib=%d shadow_pages=%d"),
        UsesLowMemoryProfile(), GRHIGlobals.GpuInfo.DedicatedVideoMemory >> 20, ReadInt(TEXT("r.Streaming.PoolSize")),
        ReadInt(TEXT("r.Nanite.Streaming.StreamingPoolSize")), ReadInt(TEXT("r.Shadow.Virtual.MaxPhysicalPages")));
    LogActiveGraphics();
}

bool FDuelGraphicsSettings::CanAdjust(EDuelGraphicsRow Row) const
{
    if (AudioIndex(Row) != INDEX_NONE) return true;
    if (Row == EDuelGraphicsRow::SceneQuality) return HasRenderer();
    return Row == EDuelGraphicsRow::TSR ? CanSetTSR() : CanSetHardwareLumen();
}

FString FDuelGraphicsSettings::Value(EDuelGraphicsRow Row) const
{
    const int32 Index = AudioIndex(Row);
    if (Index != INDEX_NONE) return FString::Printf(TEXT("%d%%"), AudioLevels[Index]);
    if (Row == EDuelGraphicsRow::SceneQuality)
    {
        if (!HasRenderer()) return TEXT("当前不可用");
        switch (CurrentSceneQuality())
        {
        case EScenePreset::High: return TEXT("最高");
        case EScenePreset::Performance: return TEXT("流畅");
        default: return TEXT("平衡");
        }
    }
    if (Row == EDuelGraphicsRow::HardwareLumen)
    {
        return HasHardwareLumen() ? (HardwareLumenActive() ? TEXT("开") : TEXT("关")) : TEXT("当前不可用");
    }
    if (!HasTSR())
    {
        return TEXT("当前不可用");
    }
    const int32 Preset = CurrentPreset();
    if (Preset == AdaptivePreset) return Presets[Preset].Label;
    if (Preset != INDEX_NONE && FMath::IsNearlyEqual(ReadFloat(SecondaryVariable), 100.f))
    {
        return FString::Printf(TEXT("%s %.0f%%"), Presets[Preset].Label, Presets[Preset].Percentage);
    }
    const float Percentage = ReadFloat(ScreenVariable);
    return Percentage > 0.f ? FString::Printf(TEXT("%.0f%%"), Percentage) : TEXT("Auto");
}

void FDuelGraphicsSettings::Adjust(EDuelGraphicsRow Row, int32 Step)
{
    if (!CanAdjust(Row)) return;
    const int32 Index = AudioIndex(Row);
    if (Index != INDEX_NONE)
    {
        const int32 Next = FMath::Clamp(AudioLevels[Index] + (Step < 0 ? -5 : 5), 0, 100);
        if (Next == AudioLevels[Index]) return;
        AudioLevels[Index] = Next;
        if (Row == AudioStart) FApp::SetVolumeMultiplier(AudioGain(Row));
        bSaveFailed = !SaveValue(AudioKeys[Index], Next);
        return;
    }
    if (Row == EDuelGraphicsRow::SceneQuality)
    {
        const int32 Count = int32(EScenePreset::Count);
        const int32 Next = (int32(CurrentSceneQuality()) + Count + (Step < 0 ? -1 : 1)) % Count;
        ApplySceneQuality(EScenePreset(Next));
        bSaveFailed = !SaveValue(SceneQualityKey, Next);
        LogActiveGraphics();
        return;
    }
    if (Row == EDuelGraphicsRow::TSR)
    {
        const int32 Current = FMath::Max(0, CurrentPreset());
        const int32 Available = GRHISupportsDynamicResolution && CanOverride(Find(DynamicMode)) ? PresetCount : PresetCount-1;
        const int32 Next = (Current + Available + (Step < 0 ? -1 : 1)) % Available;
        if (!ApplyTSR(Next))
        {
            return;
        }
        bSaveFailed = !SaveValue(ScreenKey, Next);
    }
    else
    {
        const bool bEnable = !HardwareLumenActive();
        if (!ApplyHardwareLumen(bEnable))
        {
            return;
        }
        bSaveFailed = !SaveValue(HardwareKey, int32(bEnable));
    }
    LogActiveGraphics();
}

bool FDuelGraphicsSettings::LowMemoryProfile() { return UsesLowMemoryProfile(); }
