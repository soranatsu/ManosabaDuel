#pragma once

#include "CoreMinimal.h"

enum class EDuelGraphicsRow : uint8 { TSR, HardwareLumen, SceneQuality, Master, CombatAudio, VoiceAudio, MusicAudio, Count };

struct FDuelGraphicsSettings
{
    void Restore();
    void Adjust(EDuelGraphicsRow Row, int32 Step);
    bool CanAdjust(EDuelGraphicsRow Row) const;
    FString Value(EDuelGraphicsRow Row) const;
    bool SaveFailed() const { return bSaveFailed; }
    static FString PreferencesPath();
    static float AudioGain(EDuelGraphicsRow Row);
    static bool LowMemoryProfile();

private:
    bool bSaveFailed = false;
};
