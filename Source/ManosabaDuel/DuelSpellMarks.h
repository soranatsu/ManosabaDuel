#pragma once

#include "CoreMinimal.h"

enum class EDuelMark : uint8
{
    Default,
    Bind, BindEcho, LightRosette, WitchRain, InkWell, InkRupture, Mantle, Aegis, Ward, Votive, Absolution,
    PenNib, CrossThread, Offering, EmberCrater, SaltArc, Sable, StarOrb, Step, IceLance, Lantern, Witness, Thread, Reprise,
    Leap, Flank, Wingfall, Takeoff, Pillar, Storm, Scroll, CrossVerdict, Atlas, Pursuit, WingLane,
    Tribunal, HaloScript, HaloCore, Procession, WrittenVerdict,
    TalonRush, Reversal, NightLane, ShadowVolley, CrossAmbush, Snare, SnareTrap, Reap, TailSweep,
    BrokenRecord, SilentBell, BellCenter, Tether, MirrorSeam, MirrorPanel, Prism, IcicleAim, Archive, GlassShelter,
    BoneRake, BoneSlam, HoundRake, HoundTurn, FrostHook, FrostReap,
    StarTrack, ReturningFrost, PrismSeal, EmberRecoil, FrostThrust, HoundPounce,
    GildedCross, MoonCrescent, Count,
};

namespace DuelSpellMarks
{
    inline constexpr float CrossHalfWidth = 112.f;
    inline constexpr float MoonCutOffset = .28f;
    inline constexpr float MoonCutRadius = .79f;

    void Build(EDuelMark Mark, const FVector& Axis, const FVector2D& Extent, bool bLane, float Inner,
        TArray<FVector>& Vertices, TArray<int32>& Triangles, float HalfArc = 180.f, float EndScale = 1.f);
    void BuildGlassField(const FVector2D& Safe, const FVector2D& Extent, float SafeRadius,
        TArray<FVector>& Vertices, TArray<int32>& Triangles);
}
