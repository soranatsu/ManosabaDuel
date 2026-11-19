#pragma once

class AActor;
class USceneComponent;

namespace DuelNorthScenery
{
    inline constexpr double SeaLevelCm = -2125.;
    inline constexpr float PlanetRadiusKm = 6360.f;
    void Build(AActor* Owner, USceneComponent* Parent);
}
