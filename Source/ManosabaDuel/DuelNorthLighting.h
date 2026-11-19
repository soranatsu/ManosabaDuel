#pragma once
#include "CoreMinimal.h"
class AActor;
class USceneComponent;
namespace DuelNorthLayout { struct FLocation; }

namespace DuelNorthLighting
{
    struct FLightingBudget
    {
        // Global fixed authoring caps, not an enabled/near-player light pool.
        int32 ShadowPointLights=120;
        int32 ShadowSpotLights=32;
        int32 VolumetricShadowLights=3;
        int32 MaxDirectContributors=16;
        int32 MaxAttenuationContributors=40;
        int32 LocalFogVolumes=2;
        float SelectionInterval=.5f; // Fog and progression cue only.
        float FogViewDistance=3600.f;
    };
    struct FLightingStats
    {
        int32 AuthoredNodes=0;
        // Number of enabled fixed components; renderer visibility is separate.
        int32 VisiblePoints=0,VisibleSpots=0,VolumetricShadowLights=0,VisibleLocalFogVolumes=0;
        bool bGardenSun=false;
        float ExposureEV100=2.5f;
        FName Room;
        bool bGuidance=false,bVictoryGuide=false;
        FName GuidanceRoom;
        FVector GuidanceTarget=FVector::ZeroVector;
    };
    const FLightingBudget& Budget();
    FLightingStats Stats(AActor* Owner);
    void Build(AActor* Owner,USceneComponent* Parent);
    void Update(AActor* Owner,const FVector& PlayerPosition,float DeltaSeconds,
        const DuelNorthLayout::FLocation* KnownLocation=nullptr);
}
