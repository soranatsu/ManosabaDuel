#pragma once

#include "DuelGame.h"

// A second independent contact starts after the complete preceding collision
// window plus the earliest roll chain and a 40 ms scheduling margin.
namespace DuelBossDefense
{
    constexpr float HitGap = DuelCourt::RollChainAt + .04f;

    enum class ECloseStyle : uint8 { BoneRake, BoneSlam, HoundRake, HoundLunge, HoundTail, IceThrust, IceHook, IceReap };
    struct FCloseHit
    {
        float At;
        float Reach;
        float HalfArc;
        float Damage;
        float Forward;
        float Lateral;
        float AimBias;
        ECloseStyle Style;
    };

    constexpr FCloseHit Ossuary[] = {
        {.74f, 355.f, 62.f, 36.f,  92.f, -54.f, -13.f, ECloseStyle::BoneRake},
        {1.515f,372.f, 65.f, 39.f,  85.f,  58.f,  15.f, ECloseStyle::BoneRake},
        {2.44f,315.f,180.f, 48.f, 105.f,   0.f,   0.f, ECloseStyle::BoneSlam},
    };
    constexpr FCloseHit Hound[] = {
        {.62f, 320.f, 55.f, 31.f,  95.f,  68.f,  13.f, ECloseStyle::HoundRake},
        {1.345f,330.f, 58.f, 34.f,  65.f, -84.f, -15.f, ECloseStyle::HoundRake},
        {2.12f,395.f, 29.f, 39.f, 154.f,   0.f,   0.f, ECloseStyle::HoundLunge},
        {2.9825f,375.f,108.f, 45.f,  24.f,  53.f, -32.f, ECloseStyle::HoundTail},
    };
    constexpr FCloseHit FrostStaff[] = {
        {.70f, 420.f, 23.f, 32.f, 110.f,   0.f,   0.f, ECloseStyle::IceThrust},
        {1.4375f,340.f, 71.f, 37.f, -34.f, -55.f, -24.f, ECloseStyle::IceHook},
        {2.3f,412.f, 82.f, 44.f,  94.f,  51.f,  26.f, ECloseStyle::IceReap},
    };

    constexpr bool IsCloseChain(EDuelMove Move)
    {
        return Move == EDuelMove::OssuaryChain || Move == EDuelMove::HoundChain || Move == EDuelMove::FrostStaffChain;
    }

    inline TConstArrayView<FCloseHit> CloseHits(EDuelMove Move)
    {
        switch (Move)
        {
        case EDuelMove::OssuaryChain: return MakeArrayView(Ossuary);
        case EDuelMove::HoundChain: return MakeArrayView(Hound);
        case EDuelMove::FrostStaffChain: return MakeArrayView(FrostStaff);
        default: return {};
        }
    }

    constexpr float CloseWarningTime = .33f;
    constexpr float CloseMaxStep = .05f;
    constexpr float CloseMinGap = 2.f * DuelCourt::RollGuardDelay + DuelCourt::RollActiveTime +
        DuelCourt::RollRecoveryTime + CloseMaxStep;
    constexpr float CloseLastHitHold = .18f;
    constexpr float CloseRecovery = .63f;
    constexpr float IcicleBeat = .145f;
    constexpr float IcicleAimLead = .115f;
    constexpr float IcicleFirstRelease = .91f;
    constexpr float IcicleSpeed = 1960.f;
    constexpr int32 MaxIcicles = 6;

    template <int32 N>
    constexpr bool ValidCloseChain(const FCloseHit (&Hits)[N])
    {
        for (int32 I = 1; I < N; ++I)
        {
            if (Hits[I].At - Hits[I - 1].At < CloseMinGap) return false;
        }
        return N >= 3 && Hits[0].At > CloseWarningTime;
    }
    static_assert(ValidCloseChain(Ossuary) && ValidCloseChain(Hound) && ValidCloseChain(FrostStaff));
    static_assert(IcicleAimLead < IcicleBeat && IcicleBeat < DuelCourt::RollChainAt);
    constexpr bool HasProjectileCadence(EDuelMove Move)
    {
        return Move == EDuelMove::PrismPursuit || Move == EDuelMove::ShadowVolley;
    }
    constexpr bool IsSignature(EDuelMove Move)
    {
        return Move >= EDuelMove::OssuaryShears && Move <= EDuelMove::MirrorRiposte;
    }

    constexpr float WingFlight = .42f;
    constexpr float ShadeFlight = .46f;
    constexpr float ShadeBodyStart = ShadeFlight + HitGap + .06f;
    constexpr float ShadeBodyFlight = .29f;
    constexpr float ShadeBodyAimLead = .36f;
    constexpr float MirrorFirstFlight = .55f;
    constexpr float MirrorReturnStart = MirrorFirstFlight + HitGap + .065f;
    constexpr float MirrorReturnFlight = .55f;
    constexpr float MirrorAimLead = .36f;
    constexpr float RiposteGuardOpens = .24f;
    constexpr float RiposteGuardCloses = .82f;
    constexpr float RiposteFlight = .51f;
    constexpr float RiposteStep = .24f;

    constexpr float ShadowProjectileSpeed(int32 Phase) { return Phase == 3 ? 2150.f : 1870.f; }
    constexpr float SealBeat = HitGap;
    constexpr float SealActive = SealBeat * 2.f + .09f;
    constexpr float CrossBeat = HitGap;
    constexpr float CrossActive = CrossBeat + .14f;
    constexpr float PursuitBeat = HitGap;
    constexpr float PursuitActive = PursuitBeat * 3.f + .20f;
    constexpr float RakingBeat = HitGap;
    constexpr float RakingActive = RakingBeat + .16f;
    constexpr float AmbushLunge = .23f;
    constexpr float AmbushSecondStart = AmbushLunge + HitGap;
    constexpr float AmbushActive = AmbushSecondStart + AmbushLunge + .04f;
    constexpr float BoneSweep = .79f;
    constexpr float BoneSecondStart = BoneSweep + HitGap;
    constexpr float BoneActive = BoneSecondStart + BoneSweep + .01f;
    constexpr float MirrorSweep = .62f;
    constexpr float MirrorSecondStart = MirrorSweep + HitGap;
    constexpr float MirrorSecondWarning = MirrorSecondStart - .34f;
    constexpr float MirrorSecondTime = .51f;
    constexpr float MirrorActive = MirrorSecondStart + MirrorSecondTime;
    constexpr float RecordBeat = HitGap;
    // All four samples are visible before the first sequential detonation.
    constexpr float RecordWindup = RecordBeat * 3.f + .42f;
    constexpr float RecordActive = RecordBeat * 3.f + .32f;
    constexpr float EmaDashTime = .46f;
    constexpr float SentenceBeat = HitGap + .19f;
    constexpr float SentenceWarning = .48f;
    constexpr float SentenceCell = 600.f;
}
