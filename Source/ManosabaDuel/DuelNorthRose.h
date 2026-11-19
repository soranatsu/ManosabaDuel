#pragma once

class AActor;
class USceneComponent;
namespace DuelNorthLayout { struct FPortal; }

namespace DuelNorthRose
{
    inline constexpr float CentreZ = 4608.f;
    inline constexpr float RoughDiameter = 560.f;
    inline constexpr float ClearRadius = 250.f;
    inline constexpr float SillZ = CentreZ - RoughDiameter * .5f;

    inline constexpr float ChapelRoughDiameter = 246.f;

    float ApertureRadius(const DuelNorthLayout::FPortal& Portal);
    bool IsRound(const DuelNorthLayout::FPortal& Portal);
    void Build(AActor* Owner, USceneComponent* Parent);
}
