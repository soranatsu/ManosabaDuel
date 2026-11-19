#include "DuelGame.h"
#include "DuelBossDefense.h"

#include "DuelArena.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

namespace
{
    struct FChoice
    {
        EDuelMove Move;
        int32 FirstPhase;
        float Near;
        float Far;
        float Weight;
    };

    float MirrorRangedWeight(EDuelMove Move, bool bMirror, int32 Phase)
    {
        if (!bMirror || Phase < 2) return 1.f;
        const bool bRanged = Move == EDuelMove::OssuaryShears || Move == EDuelMove::Seals ||
            Move == EDuelMove::AtlasRain || Move == EDuelMove::Cross || Move == EDuelMove::WingVerdict ||
            Move == EDuelMove::Tribunal || Move == EDuelMove::BoneProcession || Move == EDuelMove::WrittenVerdict ||
            Move == EDuelMove::NightLanes || Move == EDuelMove::ShadowSnare || Move == EDuelMove::ShadowVolley;
        return bRanged ? (Phase == 3 ? 4.8f : 3.6f) : .48f;
    }

    constexpr EDuelMove EmaClose = EDuelMove::Pillar;
    constexpr EDuelMove HiroClose = EDuelMove::Paw;
    constexpr FChoice EmaAttacks[] = {
        {EDuelMove::OssuaryChain, 1, 0, 447, 6.7f},
        {EDuelMove::OssuaryShears, 2, 245, 1460, 11.5f},
        {EDuelMove::Leap, 1, 0, 1330, 4.1f},
        {EmaClose, 1, 0, 840, 3.1f}, {EDuelMove::Seals, 2, 180, 1440, 4.3f},
        {EDuelMove::Storm, 2, 0, 1010, 2.7f}, {EDuelMove::WingSweep, 2, 0, 690, 3.0f},
        {EDuelMove::AtlasRain, 2, 0, 1500, 4.8f}, {EDuelMove::Cross, 3, 135, 1500, 5.2f},
        {EDuelMove::FlankLeap, 1, 280, 1640, 5.2f}, {EDuelMove::Pursuit, 2, 205, 1610, 5.0f},
        {EDuelMove::WingVerdict, 2, 230, 1640, 5.8f}, {EDuelMove::Tribunal, 3, 0, 1760, 6.3f},
        {EDuelMove::HaloBreak, 2, 85, 1240, 5.5f},
        {EDuelMove::Wingfall, 2, 105, 1760, 6.5f},
        {EDuelMove::BoneProcession, 2, 170, 1480, 6.8f},
        {EDuelMove::WrittenVerdict, 2, 0, 1760, 6.9f},
    };
    constexpr FChoice HiroAttacks[] = {
        {EDuelMove::HoundChain, 1, 0, 415, 7.2f},
        {EDuelMove::HoundShade, 2, 265, 1090, 11.5f},
        {HiroClose, 1, 0, 420, 5.7f}, {EDuelMove::TalonRush, 1, 185, 1310, 6.6f},
        {EDuelMove::ShadowSweep, 2, 0, 700, 5.0f},
        {EDuelMove::NightLanes, 2, 0, 1610, 5.4f}, {EDuelMove::InwardReap, 3, 230, 1000, 5.6f},
        {EDuelMove::Reversal, 1, 145, 1160, 5.6f}, {EDuelMove::ShadowSnare, 2, 200, 1640, 5.8f},
        {EDuelMove::RakingClaws, 1, 165, 640, 5.2f},
        {EDuelMove::ShadowVolley, 2, 385, 1760, 6.0f},
        {EDuelMove::FeintCounter, 1, 110, 505, 7.5f},
        {EDuelMove::CrossAmbush, 2, 150, 1460, 7.5f},
    };
    constexpr FChoice YukiAttacks[] = {
        {EDuelMove::FrostStaffChain, 1, 0, 530, 7.2f}, {EDuelMove::IcicleRow, 1, 460, 1730, 5.6f},
        {EDuelMove::GlassShelter, 1, 0, 1800, 4.2f},
        {EDuelMove::MirrorRicochet, 1, 380, 1540, 11.0f},
        {EDuelMove::MirrorRiposte, 2, 0, 640, 12.5f},
        {EDuelMove::BrokenRecord, 1, 0, 1800, 5.4f},
        {EDuelMove::VerdictTether, 1, 0, 1350, 6.2f},
        {EDuelMove::SilentBell, 2, 0, 1800, 4.1f},
        {EDuelMove::MirrorEdict, 2, 0, 1800, 5.9f},
        {EDuelMove::PrismPursuit, 1, 0, 1650, 6.6f},
        {EDuelMove::ArchiveCollapse, 2, 0, 1720, 6.8f},
        {EDuelMove::FinalSentence, 2, 0, 1900, 9.6f},
    };
    constexpr float TetherRadius[] = {540.f, 644.f, 731.f};
    constexpr float TetherApproach = 845.f;
    constexpr float SilentBellHalfLength = 2240.f;
    constexpr float SilentBellHalfWidth(int32 Phase) { return Phase == 3 ? 359.f : 286.f; }
    constexpr float MirrorHalfLength = -DuelCourt::GateCrossX + 70.f;
    constexpr float MirrorPanelHalfDepth = 80.f;
    constexpr float MirrorPulseSpacing = .035f;
    constexpr float MirrorSweepTime = DuelBossDefense::MirrorSweep;
    constexpr float MirrorSecondWarning = DuelBossDefense::MirrorSecondWarning;
    constexpr float MirrorSecondStart = DuelBossDefense::MirrorSecondStart;
    constexpr float MirrorSecondTime = DuelBossDefense::MirrorSecondTime;
    constexpr float AtlasRadius[] = {290.f, 356.f, 403.f};
    constexpr float LaneWidth[] = {119.f, 145.f, 174.f};
    constexpr float VerdictScriptHalfLength(int32 Phase) { return Phase == 3 ? 1760.f : 1580.f; }
    constexpr float VerdictScriptHalfWidth(int32 Phase) { return Phase == 3 ? 161.f : 126.f; }
    constexpr float VerdictCrossHalfLength(int32 Phase) { return VerdictScriptHalfLength(Phase) - 130.f; }
    constexpr float WaveSpacing = .65f;
    constexpr float PursuitSpacing = DuelBossDefense::PursuitBeat;
    constexpr float SignatureSpacing = .61f;
    constexpr float VolleyLife = 1.19f;
    constexpr float SnareDelay = .44f;
    const FLinearColor Gold(1.f, .72f, .37f, .83f);
    const FLinearColor Violet(.62f, .32f, 1.f, .76f);
    const FLinearColor PaleGold(1.f, .88f, .63f, .77f);
    const FLinearColor DarkCarmine(.81f, .031f, .13f, .73f);
    const FLinearColor Carmine(1.f, .082f, .22f, .84f);
    const FLinearColor Rose(1.f, .33f, .60f, .82f);
    const FLinearColor YukiFrost(.52f, .88f, 1.5f, .92f);
    const FLinearColor MirrorIce(.34f, .85f, 1.55f, .91f);
    const FLinearColor MirrorLilac(.67f, .54f, 1.42f, .87f);

    EDuelMark CloseMark(DuelBossDefense::ECloseStyle Style)
    {
        using DuelBossDefense::ECloseStyle;
        switch (Style)
        {
        case ECloseStyle::BoneRake: return EDuelMark::BoneRake;
        case ECloseStyle::BoneSlam: return EDuelMark::BoneSlam;
        case ECloseStyle::HoundRake: return EDuelMark::HoundRake;
        case ECloseStyle::HoundLunge: return EDuelMark::HoundPounce;
        case ECloseStyle::HoundTail: return EDuelMark::HoundTurn;
        case ECloseStyle::IceThrust: return EDuelMark::FrostThrust;
        case ECloseStyle::IceHook: return EDuelMark::FrostHook;
        case ECloseStyle::IceReap: return EDuelMark::FrostReap;
        }
        return EDuelMark::Default;
    }

    void ShowMirrorSeam(ADuelArena& Arena, const FVector& Seam, float HalfWidth, float Life,
        const FLinearColor& Glass)
    {
        for (const float Side : {-1.f, 1.f})
        {
            Arena.Lane(Seam + FVector(0.f, Side * HalfWidth, .35f), FVector::ForwardVector,
                MirrorHalfLength, 19.f, PaleGold, Life, false, true, false, EDuelMark::MirrorSeam);
            Arena.Lane(Seam + FVector(0.f, Side * (HalfWidth + 25.f), 0.f), FVector::ForwardVector,
                MirrorHalfLength, 13.f, Glass, Life, false, true, false, EDuelMark::MirrorSeam);
        }
    }

    FLinearColor WaveTint(bool bAtlas, int32 Index)
    {
        return bAtlas ? (Index == 1 ? Violet : Gold) : (Index == 1 ? Rose : Carmine);
    }

    struct FLink
    {
        EDuelMove From;
        EDuelMove To;
        int32 FirstPhase;
        float Near;
        float Far;
        float Weight;
        int32 MinDifficulty = 0;
    };

    constexpr FLink HiroLinks[] = {
        {EDuelMove::TalonRush, EDuelMove::Paw,         1, 0,   355,  2.f},
        {EDuelMove::TalonRush, EDuelMove::ShadowSweep, 2, 0,   665,  3.f},
        {EDuelMove::TalonRush, EDuelMove::Reversal,    1, 200, 890,  2.5f},
        {EDuelMove::Paw, EDuelMove::Reversal,          1, 160, 800,  2.7f},
        {EDuelMove::Paw, EDuelMove::TalonRush,         1, 230, 1080, 2.3f},
        {EDuelMove::Reversal, EDuelMove::ShadowSweep,  2, 0,   660,  4.f},
        {EDuelMove::Reversal, EDuelMove::TalonRush,    1, 195, 950,  2.f},
        {EDuelMove::ShadowSweep, EDuelMove::TalonRush, 2, 150, 1110, 4.3f},
        {EDuelMove::ShadowSweep, EDuelMove::ShadowSnare,3, 300, 1250, 2.f},
        {EDuelMove::NightLanes, EDuelMove::Reversal,   2, 220, 1160, 3.2f},
        {EDuelMove::ShadowSnare, EDuelMove::TalonRush,  2, 220, 1120, 3.4f},
        {EDuelMove::InwardReap, EDuelMove::TalonRush,  3, 170, 1050, 3.5f},
        {EDuelMove::InwardReap, EDuelMove::Reversal,   3, 185, 930,  2.3f},
        {EDuelMove::TalonRush, EDuelMove::RakingClaws, 1, 165, 610,  2.9f},
        {EDuelMove::RakingClaws, EDuelMove::Reversal,  1, 195, 860,  2.8f},
        {EDuelMove::RakingClaws, EDuelMove::ShadowSnare, 2, 265, 1030, 2.9f},
        {EDuelMove::Reversal, EDuelMove::ShadowVolley,  2, 500, 1610, 3.6f},
        {EDuelMove::ShadowVolley, EDuelMove::Reversal, 2, 165, 1030, 3.4f},
        {EDuelMove::ShadowVolley, EDuelMove::TalonRush,2, 250, 1170, 4.f},
        {EDuelMove::TalonRush, EDuelMove::FeintCounter,1, 100, 500, 3.7f},
        {EDuelMove::FeintCounter, EDuelMove::RakingClaws,1, 105, 535, 3.9f},
        {EDuelMove::FeintCounter, EDuelMove::Reversal,1, 180, 920, 3.5f},
        {EDuelMove::Reversal, EDuelMove::CrossAmbush,2, 240, 1320, 4.3f},
        {EDuelMove::CrossAmbush, EDuelMove::FeintCounter,2, 115, 500, 3.2f},
        {EDuelMove::CrossAmbush, EDuelMove::ShadowVolley,3, 455, 1570, 3.6f},
        {EDuelMove::CrossAmbush, EDuelMove::TalonRush,2, 245, 1130, 4.3f},
        {EDuelMove::ShadowSnare, EDuelMove::CrossAmbush,3, 295, 1380, 3.8f},
        {EDuelMove::TalonRush, EDuelMove::HoundShade,   2, 265, 1090, 5.2f},
        {EDuelMove::Paw, EDuelMove::HoundChain,        1, 0,   357,  2.9f},
        {EDuelMove::Reversal, EDuelMove::HoundChain,   2, 130, 410,  3.2f},
        {EDuelMove::HoundChain, EDuelMove::CrossAmbush,2, 285, 1120, 4.8f, 1},
        {EDuelMove::HoundChain, EDuelMove::TalonRush,   2, 400, 955,  3.0f, 1},
    };
    constexpr FLink EmaLinks[] = {
        {EDuelMove::Leap, EDuelMove::Pillar,          1, 0,   465,  2.5f},
        {EDuelMove::Leap, EDuelMove::WingSweep,       2, 0,   660,  3.1f},
        {EDuelMove::Leap, EDuelMove::Seals,           2, 240, 1050, 2.6f},
        {EDuelMove::Seals, EDuelMove::FlankLeap,      2, 220, 1310, 3.3f},
        {EDuelMove::Pillar, EDuelMove::FlankLeap,     1, 285, 1330, 3.4f},
        {EDuelMove::Pillar, EDuelMove::Pursuit,       2, 220, 1400, 2.2f},
        {EDuelMove::Pursuit, EDuelMove::FlankLeap,    2, 220, 1530, 4.6f},
        {EDuelMove::FlankLeap, EDuelMove::WingSweep,  2, 0,   650,  3.5f},
        {EDuelMove::FlankLeap, EDuelMove::Pillar,     1, 0,   495,  2.7f},
        {EDuelMove::WingSweep, EDuelMove::WingVerdict,2, 220, 1150, 3.f},
        {EDuelMove::WingVerdict, EDuelMove::FlankLeap,2, 215, 1440, 3.5f},
        {EDuelMove::WingVerdict, EDuelMove::Seals,    2, 195, 1040, 2.f},
        {EDuelMove::AtlasRain, EDuelMove::WingSweep,  2, 0,   660,  3.1f},
        {EDuelMove::Tribunal, EDuelMove::FlankLeap,   3, 245, 1450, 3.7f},
        {EDuelMove::Tribunal, EDuelMove::WingSweep,   3, 0,   650,  3.3f},
        {EDuelMove::Cross, EDuelMove::WingSweep,      3, 0,   640,  3.4f},
        {EDuelMove::WingSweep, EDuelMove::HaloBreak,  2, 165, 990,  3.1f},
        {EDuelMove::HaloBreak, EDuelMove::Pursuit,    2, 285, 1150, 2.8f},
        {EDuelMove::HaloBreak, EDuelMove::FlankLeap,  2, 245, 1300, 3.5f},
        {EDuelMove::Pursuit, EDuelMove::Wingfall,    2, 240, 1720, 4.6f},
        {EDuelMove::Wingfall, EDuelMove::WingSweep,  2, 0,   655,  4.1f},
        {EDuelMove::Wingfall, EDuelMove::HaloBreak,  2, 185, 1080, 3.7f},
        {EDuelMove::Wingfall, EDuelMove::Tribunal,   3, 610, 1730, 4.4f},
        {EDuelMove::WingSweep, EDuelMove::BoneProcession,2, 205, 1360, 4.7f},
        {EDuelMove::BoneProcession, EDuelMove::Wingfall,2, 235, 1650, 3.9f},
        {EDuelMove::BoneProcession, EDuelMove::WrittenVerdict,3, 285, 1480, 3.1f},
        {EDuelMove::WrittenVerdict, EDuelMove::WingSweep,2, 0, 600, 3.6f},
        {EDuelMove::WrittenVerdict, EDuelMove::Wingfall,2, 310, 1470, 3.3f},
        {EDuelMove::FlankLeap, EDuelMove::OssuaryShears,2, 245, 1460, 5.2f},
        {EDuelMove::Leap, EDuelMove::OssuaryChain,      1, 0,   400,  4.2f},
        {EDuelMove::Pillar, EDuelMove::OssuaryChain,    1, 95,  395,  2.2f},
        {EDuelMove::FlankLeap, EDuelMove::OssuaryChain, 2, 0,   420,  3.3f},
        {EDuelMove::OssuaryChain, EDuelMove::Wingfall,  2, 340, 1540, 4.8f, 1},
        {EDuelMove::OssuaryChain, EDuelMove::HaloBreak, 2, 255, 1180, 3.6f, 1},
        {EDuelMove::AtlasRain, EDuelMove::Pursuit,      2, 385, 1460, 3.2f, 1},
    };
    constexpr FLink YukiLinks[] = {
        {EDuelMove::VerdictTether, EDuelMove::BrokenRecord, 1, 0, 1800, 2.4f},
        {EDuelMove::VerdictTether, EDuelMove::MirrorEdict, 2, 0, 1800, 3.7f},
        {EDuelMove::GlassShelter, EDuelMove::BrokenRecord, 1, 0, 1800, 2.6f},
        {EDuelMove::GlassShelter, EDuelMove::VerdictTether, 2, 0, 1350, 4.4f},
        {EDuelMove::BrokenRecord, EDuelMove::VerdictTether, 1, 0, 1110, 2.8f},
        {EDuelMove::BrokenRecord, EDuelMove::SilentBell, 2, 440, 1800, 2.6f},
        {EDuelMove::SilentBell, EDuelMove::GlassShelter, 2, 0, 1800, 2.8f},
        {EDuelMove::SilentBell, EDuelMove::MirrorEdict, 3, 0, 1800, 3.3f},
        {EDuelMove::MirrorEdict, EDuelMove::VerdictTether, 2, 0, 1350, 3.8f},
        {EDuelMove::MirrorEdict, EDuelMove::GlassShelter, 3, 0, 1800, 2.2f},
        {EDuelMove::GlassShelter, EDuelMove::PrismPursuit, 1, 0, 1650, 3.9f},
        {EDuelMove::PrismPursuit, EDuelMove::VerdictTether, 1, 0, 1160, 3.4f},
        {EDuelMove::PrismPursuit, EDuelMove::MirrorEdict, 2, 0, 1720, 3.7f},
        {EDuelMove::ArchiveCollapse, EDuelMove::PrismPursuit, 2, 0, 1610, 4.1f},
        {EDuelMove::MirrorEdict, EDuelMove::ArchiveCollapse, 3, 0, 1720, 2.8f},
        {EDuelMove::GlassShelter, EDuelMove::MirrorRicochet, 1, 380, 1540, 5.4f},
        {EDuelMove::VerdictTether, EDuelMove::MirrorRiposte, 2, 0, 640, 5.1f},
        {EDuelMove::FrostStaffChain, EDuelMove::MirrorRiposte, 2, 0, 640, 4.8f},
        {EDuelMove::IcicleRow, EDuelMove::PrismPursuit, 1, 490, 1650, 4.5f},
        {EDuelMove::IcicleRow, EDuelMove::FrostStaffChain, 1, 0, 530, 3.9f},
        {EDuelMove::PrismPursuit, EDuelMove::FrostStaffChain, 2, 0, 530, 5.0f},
        {EDuelMove::FrostStaffChain, EDuelMove::MirrorEdict, 2, 315, 1720, 4.3f},
        {EDuelMove::SilentBell, EDuelMove::IcicleRow, 2, 525, 1730, 3.9f},
        {EDuelMove::ArchiveCollapse, EDuelMove::MirrorRicochet, 3, 510, 1510, 4.1f},
    };

    constexpr bool HasAttack(const auto& Pool, EDuelMove Move)
    {
        for (const FChoice& Choice : Pool)
        {
            if (Choice.Move == Move)
            {
                return true;
            }
        }
        return false;
    }

    constexpr bool HasOwnLinks(const auto& Pool, const auto& Links)
    {
        for (const FLink& Link : Links)
        {
            if (!HasAttack(Pool, Link.From) || !HasAttack(Pool, Link.To))
            {
                return false;
            }
        }
        return true;
    }

    constexpr bool Disjoint(const auto& First, const auto& Second)
    {
        for (const FChoice& Choice : First)
        {
            if (HasAttack(Second, Choice.Move))
            {
                return false;
            }
        }
        return true;
    }

    static_assert(Disjoint(EmaAttacks, HiroAttacks) && Disjoint(EmaAttacks, YukiAttacks) && Disjoint(HiroAttacks, YukiAttacks),
        "Boss attack pools must be disjoint.");
    static_assert(HasOwnLinks(EmaAttacks, EmaLinks) && HasOwnLinks(HiroAttacks, HiroLinks) && HasOwnLinks(YukiAttacks, YukiLinks),
        "A boss combo must stay in the boss's own attack pool.");
}

EDuelMove ADuelFighter::PickBossAttack(float Distance)
{
    const TConstArrayView<FChoice> Pool = Character == EDuelCharacter::Yuki ? MakeArrayView(YukiAttacks) :
        Character == EDuelCharacter::Ema ? MakeArrayView(EmaAttacks) : MakeArrayView(HiroAttacks);
    struct FWeightedChoice { EDuelMove Move; float Weight; };
    TArray<FWeightedChoice, TInlineAllocator<UE_ARRAY_COUNT(EmaAttacks)>> Choices;
    const int32 Difficulty = BossDifficulty();
    const auto* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    const bool bMirror = BossEncounter == 1 && Mode && Mode->UsesNorthTower();
    float Total = 0.f;
    for (const FChoice& Candidate : Pool)
    {
        const bool bSignature = DuelBossDefense::IsSignature(Candidate.Move);
        const int32 FirstPhase = Candidate.FirstPhase - (bSignature && Difficulty == 1 ? 1 : 0);
        if (BossPhase >= FirstPhase && Distance >= Candidate.Near && Distance <= Candidate.Far &&
            Candidate.Move != LastBossAttack && CanReachWith(Candidate.Move, Distance))
        {
            const float ClosePressure = Candidate.Move == EDuelMove::VerdictTether && Distance < 430.f ? 1.38f : 1.f;
            const float SignatureWeight = bSignature ? 1.f + Difficulty * .31f : 1.f;
            const bool bIceChase = Candidate.Move == EDuelMove::IcicleRow || Candidate.Move == EDuelMove::PrismPursuit;
            const float IcePressure = bIceChase && Distance > 650.f ? 1.26f :
                Candidate.Move == EDuelMove::FrostStaffChain ? 1.28f : 1.f;
            const float Weight = Candidate.Weight * ClosePressure * SignatureWeight * IcePressure *
                MirrorRangedWeight(Candidate.Move, bMirror, BossPhase) *
                (Candidate.Move == PreviousBossAttack ? .27f : 1.f);
            Choices.Add({Candidate.Move, Weight});
            Total += Weight;
        }
    }
    float Sample = BossRandom.FRand() * Total;
    for (const FWeightedChoice& Choice : Choices)
    {
        Sample -= Choice.Weight;
        if (Sample <= 0.f)
        {
            return Choice.Move;
        }
    }
    if (!Choices.IsEmpty())
    {
        return Choices.Last().Move;
    }
    // If only the last move can reach, close the distance before taking another swing.
    if (Distance < 170.f)
    {
        return Character == EDuelCharacter::Yuki ? EDuelMove::BrokenRecord :
            Character == EDuelCharacter::Hiro ? HiroClose : EmaClose;
    }
    return EDuelMove::Idle;
}

EDuelMove ADuelFighter::PickBossFollowup(EDuelMove Last, float Distance)
{
    const int32 Difficulty = BossDifficulty();
    const auto* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    const bool bMirror = BossEncounter == 1 && Mode && Mode->UsesNorthTower();
    if (BossComboDepth >= BossPhase + Difficulty || DuelBossDefense::IsSignature(Last))
    {
        return EDuelMove::Idle;
    }
    const TConstArrayView<FLink> Links = Character == EDuelCharacter::Yuki ? MakeArrayView(YukiLinks) :
        Character == EDuelCharacter::Hiro ? MakeArrayView(HiroLinks) : MakeArrayView(EmaLinks);
    constexpr float FollowupPressure[] = {.12f, .46f, .72f};
    float StopWeight = 3.4f - (BossPhase - 1) * .68f - Difficulty * .69f - FollowupPressure[Difficulty] +
        BossComboDepth * .65f;
    if (Character == EDuelCharacter::Hiro)
    {
        StopWeight -= (BossPhase - 1) * .31f;
    }
    else if (Character == EDuelCharacter::Yuki)
    {
        StopWeight -= (BossPhase - 1) * .24f;
    }
    if (Last == EDuelMove::AtlasRain || Last == EDuelMove::Tribunal || Last == EDuelMove::ShadowSnare ||
        Last == EDuelMove::MirrorEdict || Last == EDuelMove::ArchiveCollapse)
    {
        StopWeight += 1.2f;
    }
    StopWeight = FMath::Max(.60f, StopWeight);
    const auto WeightAt = [&](const FLink& Link)
    {
        const int32 FirstPhase = Link.FirstPhase - (Difficulty == 1 && DuelBossDefense::IsSignature(Link.To) ? 1 : 0);
        return Link.From == Last && Difficulty >= Link.MinDifficulty && BossPhase >= FirstPhase &&
            Distance >= Link.Near && Distance <= Link.Far &&
            CanReachWith(Link.To, Distance)
            ? Link.Weight * MirrorRangedWeight(Link.To, bMirror, BossPhase) *
                (Link.To == PreviousBossAttack ? .42f : 1.f) : 0.f;
    };
    float Total = StopWeight;
    for (const FLink& Link : Links)
    {
        Total += WeightAt(Link);
    }
    float Draw = BossRandom.FRand() * Total - StopWeight;
    if (Draw >= 0.f)
    {
        for (const FLink& Link : Links)
        {
            const float Weight = WeightAt(Link);
            if (Weight > 0.f && (Draw -= Weight) <= 0.f)
            {
                return Link.To;
            }
        }
    }
    return EDuelMove::Idle;
}

void ADuelFighter::PrepareCloseChain()
{
    using namespace DuelBossDefense;
    const auto Hits = CloseHits(Move);
    MapWaves.SetNum(Hits.Num());
    for (int32 I = 0; I < Hits.Num(); ++I)
    {
        MapWaves[I].ImpactTime = Hits[I].At;
        MapWaves[I].WarnTime = Hits[I].At - CloseWarningTime;
        MapWaves[I].Reach = Hits[I].Reach + (BossPhase - 1) * 16.f;
    }
    Arena->Sound(EDuelSound::Charge, GetActorLocation());
}

void ADuelFighter::TickCloseChain(float LastAge)
{
    using namespace DuelBossDefense;
    const auto Hits = CloseHits(Move);
    const FLinearColor Tint = Character == EDuelCharacter::Ema ? PaleGold :
        Character == EDuelCharacter::Hiro ? Carmine : YukiFrost;
    for (int32 I = 0; I < MapWaves.Num(); ++I)
    {
        FMapWave& Beat = MapWaves[I];
        if (Beat.bHitDelivered) continue;
        if (MoveAge < Beat.WarnTime) return;
        const FCloseHit& Hit = Hits[I];
        const EDuelMark Mark = CloseMark(Hit.Style);
        if (Beat.Marks.IsEmpty())
        {
            const FVector Start = GetActorLocation();
            const FVector PlayerAt = Target->GetActorLocation();
            const FVector Aim = (PlayerAt - Start).GetSafeNormal2D(UE_SMALL_NUMBER, CastAxis);
            const FVector Across = FVector::CrossProduct(FVector::UpVector, Aim);
            const double Step = FMath::Min(static_cast<double>(Hit.Forward), FMath::Max(0.0, FVector::Dist2D(Start, PlayerAt) - 142.0));
            const FVector Court = Arena->CourtOrigin();
            const FVector Local = Start + Aim * Step + Across * (Hit.Lateral * OrbitSide) - Court;
            const FBox2D Bounds = Arena->CourtBounds(GetCapsuleComponent()->GetScaledCapsuleRadius());
            const FVector End = SweepNorthCourtTravel(Start,
                Court + FVector(Bounds.GetClosestPointTo(FVector2D(Local)), Local.Z));
            Beat.Marks = {Start, End};
            Beat.Axis = (PlayerAt - End).GetSafeNormal2D(UE_SMALL_NUMBER, Aim).RotateAngleAxis(Hit.AimBias * OrbitSide, FVector::UpVector);
            const FVector Floor = Arena->GroundMark(End);
            const float Lead = Beat.ImpactTime - MoveAge;
            if (Hit.Style == ECloseStyle::BoneSlam)
            {
                Arena->Sigil(Floor, Beat.Reach, Tint * .62f, Lead, Mark, Beat.Axis);
            }
            else
            {
                for (float Side : {-1.f, 1.f})
                {
                    const FVector Edge = Beat.Axis.RotateAngleAxis(Side * Hit.HalfArc, FVector::UpVector);
                    const float Half = Beat.Reach * .5f;
                    Arena->Lane(Floor + Edge * Half, Edge, Half, 13.f, Tint * .42f, Lead, false, true, false, Mark);
                }
            }
        }

        // The warning latches the aim, then the committed footwork sweeps physically to its endpoint.
        const auto Advance = [&](float Age)
        {
            return FMath::SmoothStep(0.f, 1.f, (Age - Beat.WarnTime - .09f) / (CloseWarningTime - .09f));
        };
        AddActorWorldOffset((Beat.Marks[1] - Beat.Marks[0]) * (Advance(MoveAge) - Advance(LastAge)), true);
        ClampToCourt();
        const float MotionTime = MoveAge - FMath::Max(LastAge, Beat.WarnTime);
        SetActorRotation(FMath::RInterpTo(GetActorRotation(), Beat.Axis.Rotation(), MotionTime, 15.f));
        if (MoveAge < Beat.ImpactTime) return;

        ActionDirection = Beat.Axis;
        SetActorRotation(ActionDirection.Rotation());
        const FVector Floor = Arena->GroundMark(GetActorLocation());
        if (Hit.Style == ECloseStyle::BoneSlam)
        {
            Arena->Detonate(Floor, Beat.Reach, Tint, .35f, Mark, ActionDirection);
            Arena->Sound(EDuelSound::HeavyImpact, Floor);
        }
        else if (Hit.Style == ECloseStyle::IceThrust || Hit.Style == ECloseStyle::HoundLunge)
        {
            Arena->Thrust(Floor + FVector(0, 0, 75), ActionDirection, Beat.Reach, Tint, true, Mark);
            Arena->Sound(EDuelSound::Cut, Floor);
        }
        else
        {
            const float Height = Hit.Style == ECloseStyle::HoundTail ? 48.f : 86.f;
            Arena->Slash(Floor + FVector(0, 0, Height), ActionDirection.Rotation().Yaw, Beat.Reach, true, Tint, Mark);
            Arena->Sound(EDuelSound::Cut, Floor);
        }
        Beat.bHitDelivered = true;
        FCollisionQueryParams Sight(SCENE_QUERY_STAT(BossCloseChainSight), false, this);
        Sight.AddIgnoredActor(Target.Get());
        FHitResult Obstruction;
        if (!GetWorld()->LineTraceSingleByChannel(Obstruction, GetActorLocation(), Target->GetActorLocation(), ECC_Visibility, Sight))
        {
            TryMelee(Beat.Reach, FMath::Cos(FMath::DegreesToRadians(Hit.HalfArc)),
                Hit.Damage + 4.f * (BossPhase - 1), 0.f, true, true);
        }
        return;
    }
}

void ADuelFighter::TickInwardReap(float LastAge, const DuelCourt::FMoveTiming& Spec)
{
    if (bHitDelivered || LastAge >= Spec.Windup + Spec.Active || MoveAge < Spec.Windup)
    {
        return;
    }
    const auto RadiusAt = [&](float Age)
    {
        const float Alpha = FMath::Clamp((Age - Spec.Windup) / Spec.Active, 0.f, 1.f);
        return FMath::Lerp(DuelCourt::ReapOuter, DuelCourt::ReapInner, Alpha);
    };
    const float Distance = FVector::Dist2D(CastOrigin, Target->GetActorLocation());
    if (Distance >= FMath::Max(DuelCourt::ReapInner, RadiusAt(MoveAge) - 27.f) &&
        Distance <= FMath::Min(DuelCourt::ReapOuter, RadiusAt(LastAge) + 27.f))
    {
        const float Feet = Target->GetActorLocation().Z - Arena->CourtOrigin().Z - Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        if (Feet < 43.f && HitGroundSpell(82.f, CastOrigin))
        {
            bHitDelivered = true;
        }
    }
}

void ADuelFighter::BeginMapAttack(const DuelCourt::FMoveTiming& Spec)
{
    const FVector Court = Arena->CourtOrigin();
    const FVector Origin = Arena->GroundMark(GetActorLocation());
    if (Move == EDuelMove::HaloBreak)
    {
        CastOrigin = Arena->GroundMark(GetActorLocation());
        Arena->Halo(CastOrigin, BossPhase == 3 ? 380.f : 450.f, BossPhase == 3 ? 1200.f : 1030.f,
            PaleGold, Spec.Windup, false, EDuelMark::HaloScript);
        Arena->Sound(EDuelSound::Charge, CastOrigin);
        return;
    }
    if (Move == EDuelMove::ShadowVolley)
    {
        MapWaves.SetNum(BossPhase == 3 ? 4 : 3);
        TickBossProjectiles(Spec);
        Arena->Sound(EDuelSound::Charge, Origin);
        return;
    }
    if (Move == EDuelMove::WingVerdict || Move == EDuelMove::Tribunal)
    {
        const bool bTribunal = Move == EDuelMove::Tribunal;
        const FVector Aim = Arena->GroundMark(Target->GetActorLocation());
        const int32 Count = BossPhase == 3 ? 3 : 2;
        MapWaves.SetNum(Count);
        for (int32 I = 0; I < Count; ++I)
        {
            FMapWave& Wave = MapWaves[I];
            Wave.WarnTime = I == 0 ? 0.f : Spec.Windup + I * SignatureSpacing - .70f;
            Wave.ImpactTime = Spec.Windup + I * SignatureSpacing;
            if (bTribunal)
            {
                Wave.Reach = 216.f + 30.f * I;
                if (I == Count - 1)
                {
                    continue;
                }
                const int32 Rays = 6 + I;
                for (int32 Ray = 0; Ray < Rays; ++Ray)
                {
                    const FVector Axis = FRotator(0, CastAxis.Rotation().Yaw + 360.f * Ray / Rays + I * 23.f, 0).Vector();
                    const FVector Mark = Aim + Axis * (492.f + I * 242.f);
                    if (FMath::Abs(Mark.X - Court.X) < DuelCourt::HalfLength + 30.f && FMath::Abs(Mark.Y - Court.Y) < DuelCourt::HalfWidth + 30.f)
                    {
                        Wave.Marks.Add(Mark);
                    }
                }
            }
            else
            {
                const float Yaw = CastAxis.Rotation().Yaw + (I == 0 ? 69.f : (I == 1 ? -47.f : 32.f)) * OrbitSide;
                Wave.Axis = FRotator(0, Yaw, 0).Vector();
                Wave.HalfLength = 1540.f;
                Wave.Reach = 118.f + 18.f * (BossPhase - 2);
                const FVector Across = FVector::CrossProduct(FVector::UpVector, Wave.Axis);
                constexpr float Gap = 449.f;
                Wave.Marks.Add(Aim + Across * (I == 0 ? Gap : -Gap) * OrbitSide);
                Wave.Marks.Add(Aim + Across * (I == 0 ? -Gap * .78f : Gap * .58f) * OrbitSide);
                if (I > 0)
                {
                    Wave.Marks.Add(Aim);
                }
            }
        }
        ShowMapWarning(0);
        Arena->Sound(EDuelSound::Charge, Origin);
        return;
    }
    if (Move == EDuelMove::Pursuit)
    {
        const int32 Count = BossPhase == 3 ? 4 : 3;
        MapWaves.SetNum(Count);
        for (int32 I = 0; I < Count; ++I)
        {
            FMapWave& Wave = MapWaves[I];
            Wave.Reach = BossPhase == 3 ? 263.f : 218.f;
            Wave.WarnTime = I * PursuitSpacing;
            Wave.ImpactTime = Wave.WarnTime + Spec.Windup;
        }
        MapWaves[0].Marks.Add(Arena->GroundMark(Target->GetActorLocation()));
        ShowMapWarning(0);
        Arena->Sound(EDuelSound::Charge, GetActorLocation());
        return;
    }
    const bool bAtlas = Move == EDuelMove::AtlasRain;
    const int32 WaveCount = BossPhase >= 3 ? 3 : 2;
    const int32 Parity = BossRandom.RandRange(0, 1);
    const int32 Nearest = FMath::Clamp(FMath::RoundToInt((Target->GetActorLocation().Y - Court.Y) / 300.0) + 3, 0, 6);
    const int32 Safe = FMath::Clamp(Nearest + (Parity == 0 ? 1 : -2), 0, 5);
    const int32 Direction = Safe <= 2 ? 1 : -1;
    MapWaves.Reset(WaveCount);
    for (int32 I = 0; I < WaveCount; ++I)
    {
        FMapWave& Wave = MapWaves.AddDefaulted_GetRef();
        Wave.Reach = bAtlas ? AtlasRadius[BossPhase - 1] : LaneWidth[BossPhase - 1];
        if (!bAtlas)
        {
            Wave.HalfLength = DuelCourt::HalfLength + 60.f;
        }
        Wave.ImpactTime = Spec.Windup + WaveSpacing * I;
        Wave.WarnTime = I == 0 ? 0.f : Wave.ImpactTime - .81f;
        if (bAtlas)
        {
            for (int32 Row = 0; Row < 3; ++Row)
            {
                for (int32 Col = 0; Col < 5; ++Col)
                {
                    if (((Row + Col + Parity + I) & 1) != 0)
                    {
                        continue;
                    }
                    const FVector At((Col - 2) * 660.f, (Row - 1) * 711.f, 8.f);
                    Wave.Marks.Add(Court + At);
                }
            }
        }
        else
        {
            const int32 Gap = Safe + I * Direction;
            for (int32 Row = 0; Row < 7; ++Row)
            {
                if (Row == Gap || Row == Gap + 1)
                {
                    continue;
                }
                const FVector At(0, (Row - 3) * 300.f, 8.f + I * .48f);
                Wave.Marks.Add(Court + At);
            }
        }
    }
    ShowMapWarning(0);
    Arena->Sound(EDuelSound::Charge, GetActorLocation());
}

bool ADuelFighter::IsGroundHazard() const
{
    switch (Move)
    {
    case EDuelMove::Idle:
        return false;
    case EDuelMove::Wingfall:
    case EDuelMove::TalonRush:
    case EDuelMove::Cross:
    case EDuelMove::HaloBreak:
    case EDuelMove::InwardReap:
    case EDuelMove::Storm:
    case EDuelMove::Pillar:
    case EDuelMove::Seals:
    case EDuelMove::BoneProcession:
    case EDuelMove::WrittenVerdict:
    case EDuelMove::CrossAmbush:
    case EDuelMove::ShadowSnare:
    case EDuelMove::ShadowSweep:
    case EDuelMove::Reversal:
    case EDuelMove::GlassShelter:
    case EDuelMove::BrokenRecord:
    case EDuelMove::SilentBell:
    case EDuelMove::VerdictTether:
    case EDuelMove::MirrorEdict:
    case EDuelMove::PrismPursuit:
    case EDuelMove::ArchiveCollapse:
        break;
    default:
        if (MapWaves.IsEmpty())
        {
            return false;
        }
        break;
    }
    const auto Spec = DuelCourt::Timing(Move, IsAwakened(), MoveWeapon, MoveArt);
    return MoveAge <= Spec.Windup + Spec.Active + .18f;
}

void ADuelFighter::TickBoneProcession(float LastAge, const DuelCourt::FMoveTiming& Spec)
{
    const bool bFinal = BossPhase == 3;
    constexpr float WaveDuration = DuelBossDefense::BoneSweep;
    const float Outer = bFinal ? 1538.f : 1314.f;
    for (int32 Wave = 0; Wave < (bFinal ? 2 : 1); ++Wave)
    {
        const float At = Spec.Windup + Wave * DuelBossDefense::BoneSecondStart;
        const float Inner = Wave == 0 ? 194.f : 336.f;
        if (LastAge < At && MoveAge >= At)
        {
            Arena->BoneWave(CastOrigin, Inner, Outer, WaveDuration, false, Wave != 0);
            if (bFinal && Wave == 0)
            {
                Arena->BoneWave(CastOrigin, 336.f, Outer, DuelBossDefense::BoneSecondStart, true, true);
            }
        }
        if (MoveAge < At || LastAge >= At + WaveDuration || BoneWaveHits[Wave])
        {
            continue;
        }
        const auto Front = [&](float Age)
        {
            return FMath::Lerp(Inner, Outer, FMath::Clamp((Age - At) / WaveDuration, 0.f, 1.f));
        };
        const float Range = FVector::Dist2D(Target->GetActorLocation(), CastOrigin);
        if (Range >= FMath::Max(Inner, Front(LastAge) - 39.f) && Range <= FMath::Min(Outer, Front(MoveAge) + 39.f))
        {
            BoneWaveHits[Wave] = true;
            const float Feet = Target->GetActorLocation().Z - Arena->CourtOrigin().Z - Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            if (Feet <= 32.f)
            {
                HitGroundSpell(bFinal ? 61.f : 52.f, CastOrigin);
            }
        }
        if (bCombatOver)
        {
            return;
        }
    }
}

void ADuelFighter::PrepareWrittenVerdict(const DuelCourt::FMoveTiming& Spec)
{
    CastOrigin = Arena->GroundMark(Target->GetActorLocation());
    CastAxis = ActionDirection.RotateAngleAxis(OrbitSide * 17.f, FVector::UpVector);
    const float Gap = BossPhase == 3 ? 211.f : 258.f;
    Arena->PenLattice(CastOrigin, CastAxis, VerdictScriptHalfLength(BossPhase), Gap,
        VerdictScriptHalfWidth(BossPhase), Spec.Windup, false);
    Arena->Sound(EDuelSound::Charge, CastOrigin);
}

void ADuelFighter::TickWrittenVerdict(float LastAge, const DuelCourt::FMoveTiming& Spec)
{
    const bool bFinal = BossPhase == 3;
    const float Gap = bFinal ? 211.f : 258.f;
    const float Width = VerdictScriptHalfWidth(BossPhase);
    const float CrossLength = VerdictCrossHalfLength(BossPhase);
    const auto Strike = [&](const FVector& Axis, float HalfLength, float Distance)
    {
        Arena->PenLattice(CastOrigin, Axis, HalfLength, Distance, Width, .61f, true);
        const FVector Across = FVector::CrossProduct(FVector::UpVector, Axis);
        const FVector Offset = Target->GetActorLocation() - CastOrigin;
        if (FMath::Abs(FVector::DotProduct(Offset, Axis)) <= HalfLength &&
            FMath::Abs(FMath::Abs(FVector::DotProduct(Offset, Across)) - Distance) <= Width)
        {
            HitGroundSpell(bFinal ? 74.f : 58.f, CastOrigin);
        }
    };
    if (LastAge < Spec.Windup && MoveAge >= Spec.Windup)
    {
        Strike(CastAxis, VerdictScriptHalfLength(BossPhase), Gap);
        if (!bCombatOver)
        {
            Arena->PenLattice(CastOrigin, CastAxis.RotateAngleAxis(72.f, FVector::UpVector), CrossLength, 0.f, Width, .76f, false);
        }
    }
    if (!bCombatOver && LastAge < Spec.Windup + .76f && MoveAge >= Spec.Windup + .76f)
    {
        Strike(CastAxis.RotateAngleAxis(72.f, FVector::UpVector), CrossLength, 0.f);
    }
}

void ADuelFighter::TickCrossAmbush(float LastAge, const DuelCourt::FMoveTiming& Spec)
{
    constexpr float SecondDelay = DuelBossDefense::AmbushSecondStart;
    constexpr float Lunge = DuelBossDefense::AmbushLunge;
    for (int32 Index = 0; Index < 2 && !bCombatOver; ++Index)
    {
        const float Start = Spec.Windup + Index * SecondDelay;
        const float End = Start + Lunge;
        if (MoveAge < Start || LastAge >= End)
        {
            continue;
        }
        const FVector From = Index == 0 ? SealCenters[0] : SealCenters[1];
        const FVector To = Index == 0 ? SealCenters[1] : SealCenters[2];
        const FVector Axis = (To - From).GetSafeNormal2D();
        const auto At = [&](float Age)
        {
            return FMath::Lerp(From, To, FMath::SmoothStep(0.f, 1.f, (Age - Start) / Lunge));
        };
        const FVector Before = At(LastAge);
        const FVector Now = At(MoveAge);
        if (LastAge < Start)
        {
            bHitDelivered = false;
            Arena->Burst(From + FVector(0, 0, 58), Carmine * 2.2f, 35, 570.f, true);
            Arena->Sound(EDuelSound::Cut, From);
            SetActorRotation(Axis.Rotation());
        }
        const FVector Current = GetActorLocation();
        SetActorLocation(SweepNorthCourtTravel(Current, FVector(Now.X, Now.Y, Current.Z)));
        const float Width = BossPhase == 3 ? 139.f : 109.f;
        const FVector Offset = Target->GetActorLocation() - Before;
        const float Along = FVector::DotProduct(Offset, Axis);
        const FVector Across = FVector::CrossProduct(FVector::UpVector, Axis);
        if (!bHitDelivered && Along >= 0.f && Along <= FVector::DotProduct(Now - Before, Axis) + 118.f &&
            FMath::Abs(FVector::DotProduct(Offset, Across)) <= Width)
        {
            bHitDelivered = true;
            Target->ReceiveDuelDamage(BossPhase == 3 ? 68.f : 55.f, Before, true);
        }
    }
}

void ADuelFighter::TickHaloBreak(float LastAge, const DuelCourt::FMoveTiming& Spec)
{
    const float Distance = FVector::Dist2D(CastOrigin, Target->GetActorLocation());
    const float Inner = BossPhase == 3 ? 380.f : 450.f;
    const float Outer = BossPhase == 3 ? 1200.f : 1030.f;
    constexpr float Delay = .68f;
    const float Core = BossPhase == 3 ? 505.f : 470.f;
    if (LastAge < Spec.Windup && MoveAge >= Spec.Windup)
    {
        Arena->Halo(CastOrigin, Inner, Outer, PaleGold, .53f, true, EDuelMark::HaloScript);
        if (Distance >= Inner && Distance <= Outer)
        {
            HitGroundSpell(BossPhase == 3 ? 77.f : 65.f, CastOrigin);
        }
        if (!bCombatOver)
        {
            Arena->Sigil(CastOrigin, Core, Gold, FMath::Max(.1f, Spec.Windup + Delay - MoveAge), EDuelMark::HaloCore);
        }
    }
    if (!bCombatOver && LastAge < Spec.Windup + Delay && MoveAge >= Spec.Windup + Delay)
    {
        Arena->Detonate(CastOrigin, Core, Gold, .61f, EDuelMark::HaloCore);
        if (Distance <= Core)
        {
            HitGroundSpell(BossPhase == 3 ? 66.f : 54.f, CastOrigin);
        }
    }
}

void ADuelFighter::PrepareShadowSnare(const DuelCourt::FMoveTiming& Spec)
{
    const FVector Aim = Arena->GroundMark(Target->GetActorLocation());
    const int32 Count = BossPhase == 3 ? 3 : 2;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        FShadowSnare& Snare = ShadowSnares.AddDefaulted_GetRef();
        const float Angle = (Index == 0 ? -19.f : Index == 1 ? 27.f : 89.f) * OrbitSide;
        Snare.Axis = ActionDirection.RotateAngleAxis(Angle, FVector::UpVector);
        const FVector Across = FVector::CrossProduct(FVector::UpVector, Snare.Axis);
        Snare.Center = Arena->GroundMark(Aim + Across * (Index == 0 ? -202.f : Index == 1 ? 202.f : -262.f));
        Snare.Thread = Arena->SnareThread(Snare.Center, Snare.Axis, DuelCourt::SnareLength(BossPhase),
            DuelCourt::SnareWidth, Spec.Windup + Spec.Active);
    }
    Arena->Sound(EDuelSound::Charge, Aim);
}

void ADuelFighter::TickShadowSnare(float LastAge, const DuelCourt::FMoveTiming& Spec)
{
    const FVector PlayerAt = Target->GetActorLocation();
    const float Feet = PlayerAt.Z - Arena->CourtOrigin().Z - Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const float Radius = DuelCourt::SnareRadius(BossPhase);
    const float End = Spec.Windup + Spec.Active;
    for (FShadowSnare& Snare : ShadowSnares)
    {
        if (LastAge < End - SnareDelay && MoveAge >= End - SnareDelay)
        {
            if (USceneComponent* Thread = Snare.Thread.Get())
            {
                Thread->SetVisibility(false, true);
            }
        }
        if (!Snare.bTriggered && MoveAge >= Spec.Windup && MoveAge < End - SnareDelay && Feet < 23.f)
        {
            const FVector Across = FVector::CrossProduct(FVector::UpVector, Snare.Axis);
            const FVector Offset = PlayerAt - Snare.Center;
            if (FMath::Abs(FVector::DotProduct(Offset, Snare.Axis)) <= DuelCourt::SnareLength(BossPhase) &&
                FMath::Abs(FVector::DotProduct(Offset, Across)) <= DuelCourt::SnareWidth)
            {
                Snare.bTriggered = true;
                Snare.StrikeAt = MoveAge + SnareDelay;
                Snare.Strike = Arena->GroundMark(PlayerAt);
                if (USceneComponent* Thread = Snare.Thread.Get())
                {
                    Thread->SetVisibility(false, true);
                }
                Arena->Sigil(Snare.Strike, Radius, Rose, SnareDelay, EDuelMark::SnareTrap, Snare.Axis);
                Arena->Burst(Snare.Strike + FVector(0, 0, 18), Carmine, 9, 165.f);
            }
        }
        if (Snare.bTriggered && LastAge < Snare.StrikeAt && MoveAge >= Snare.StrikeAt)
        {
            Arena->Detonate(Snare.Strike, Radius, DarkCarmine, .48f, EDuelMark::SnareTrap, Snare.Axis);
            if (FVector::DistSquared2D(PlayerAt, Snare.Strike) <= FMath::Square(Radius))
            {
                HitGroundSpell(BossPhase == 3 ? 66.f : 51.f, Snare.Strike);
            }
            if (bCombatOver)
            {
                return;
            }
        }
    }
}

void ADuelFighter::TickShadowSweep(float LastAge, const DuelCourt::FMoveTiming& Spec)
{
    if (MoveAge < Spec.Windup || LastAge >= Spec.Windup + Spec.Active)
    {
        return;
    }
    const float Arc = DuelCourt::TailSweepArc(BossPhase);
    const float Reach = DuelCourt::TailSweepReach(BossPhase);
    const auto AngleAt = [&](float Age)
    {
        const float Alpha = FMath::Clamp((Age - Spec.Windup) / Spec.Active, 0.f, 1.f);
        return FMath::Lerp(-Arc, Arc, Alpha) * OrbitSide;
    };
    const float From = AngleAt(LastAge);
    const float To = AngleAt(MoveAge);
    if (LastAge < Spec.Windup)
    {
        Arena->Sound(EDuelSound::Cut, CastOrigin);
    }
    if (LastAge < Spec.Windup || FMath::FloorToInt((LastAge - Spec.Windup) / .034f) !=
        FMath::FloorToInt((MoveAge - Spec.Windup) / .034f))
    {
        Arena->TailSweepTrail(CastOrigin, CastAxis.RotateAngleAxis(To, FVector::UpVector), Reach);
    }
    const FVector PlayerAt = Target->GetActorLocation();
    const FVector Offset = PlayerAt - CastOrigin;
    const float Distance = Offset.Size2D();
    const float Angle = FMath::FindDeltaAngleDegrees(CastAxis.Rotation().Yaw, Offset.Rotation().Yaw);
    const float Feet = PlayerAt.Z - Arena->CourtOrigin().Z - Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    if (!bHitDelivered && Distance >= DuelCourt::TailSweepInner && Distance <= Reach &&
        Angle >= FMath::Max(-Arc, FMath::Min(From, To) - 9.f) &&
        Angle <= FMath::Min(Arc, FMath::Max(From, To) + 9.f))
    {
        bHitDelivered = true;
        if (Feet < 36.f)
        {
            Target->ReceiveDuelDamage(BossPhase == 3 ? 73.f : 59.f, CastOrigin, true);
        }
    }
}

void ADuelFighter::TickReversal(float LastAge, const DuelCourt::FMoveTiming& Spec)
{
    const float StepEnd = Spec.Windup + DuelCourt::ReversalStepTime;
    if (LastAge < StepEnd && MoveAge >= Spec.Windup)
    {
        const float Progress = FMath::SmoothStep(0.f, 1.f, (MoveAge - Spec.Windup) / DuelCourt::ReversalStepTime);
        const FVector Position = FMath::Lerp(SealCenters[0], CastOrigin, Progress);
        const FVector Offset = FVector(Position.X, Position.Y, GetActorLocation().Z) - GetActorLocation();
        AddActorWorldOffset(Offset, true);
        if (LastAge < Spec.Windup)
        {
            Arena->Burst(SealCenters[0] + FVector(0, 0, 25), Carmine, 29, 450.f);
        }
    }
    const float Reach = DuelCourt::ReversalReach(BossPhase);
    const float Width = DuelCourt::ReversalHalfWidth(BossPhase);
    if (LastAge < StepEnd && MoveAge >= StepEnd)
    {
        CastOrigin = Arena->GroundMark(GetActorLocation());
        CastAxis = (Target->GetActorLocation() - CastOrigin).GetSafeNormal2D(UE_SMALL_NUMBER, CastAxis);
        SetActorRotation(CastAxis.Rotation());
        Arena->Lane(CastOrigin + CastAxis * (Reach * .5f), CastAxis, Reach * .5f, Width,
            DarkCarmine, DuelCourt::ReversalCueTime, false, false, false, EDuelMark::Reversal);
        Arena->Sound(EDuelSound::Charge, CastOrigin);
    }
    if (LastAge < StepEnd + DuelCourt::ReversalCueTime && MoveAge >= StepEnd + DuelCourt::ReversalCueTime)
    {
        Arena->Thrust(CastOrigin + FVector(0, 0, 55), CastAxis, Reach, Carmine, true, EDuelMark::Reversal);
        Arena->Sound(EDuelSound::Cut, CastOrigin);
        const FVector Offset = Target->GetActorLocation() - CastOrigin;
        const FVector Across = FVector::CrossProduct(FVector::UpVector, CastAxis);
        const double Along = FVector::DotProduct(Offset, CastAxis);
        if (Along >= 0 && Along <= Reach && FMath::Abs(FVector::DotProduct(Offset, Across)) <= Width)
        {
            Target->ReceiveDuelDamage(52.f + (BossPhase - 1) * 11.f, CastOrigin, true);
        }
    }
}

void ADuelFighter::PrepareYukiAttack(const DuelCourt::FMoveTiming& Spec)
{
    const FVector Aim = Arena->GroundMark(Target->GetActorLocation());
    if (Move == EDuelMove::IcicleRow)
    {
        PrepareIcicleRow(Spec);
    }
    else if (Move == EDuelMove::PrismPursuit)
    {
        MapWaves.SetNum(BossPhase == 1 ? 2 : 3);
        TickBossProjectiles(Spec);
    }
    else if (Move == EDuelMove::ArchiveCollapse)
    {
        const FVector Court = Arena->CourtOrigin();
        const FVector Local = Aim - Court;
        CastOrigin = Court + FVector(FMath::Clamp(Local.X, -650.0, 650.0),
            FMath::Clamp(Local.Y, -370.0, 370.0), 8.f);
        BoneWaveHits[0] = BoneWaveHits[1] = false;
        Arena->Halo(CastOrigin, DuelCourt::ArchiveInner, DuelCourt::ArchiveOuter, MirrorIce * .42f,
            Spec.Windup, false, EDuelMark::Archive);
    }
    else if (Move == EDuelMove::GlassShelter)
    {
        CastOrigin = Arena->GroundMark(Aim + FVector(-170, OrbitSide * 140, 0));
        Arena->GlassVerdict(CastOrigin, 380.f - BossPhase * 42.f, Spec.Windup, false);
    }
    else if (Move == EDuelMove::SilentBell)
    {
        CastOrigin = Aim;
        CastAxis = ActionDirection.RotateAngleAxis(OrbitSide * 44.f, FVector::UpVector);
        Arena->Lane(Aim, CastAxis, SilentBellHalfLength, SilentBellHalfWidth(BossPhase), YukiFrost,
            Spec.Windup, false, true, false, EDuelMark::SilentBell);
        Arena->Sigil(Aim, 133.f, PaleGold, Spec.Windup, EDuelMark::BellCenter, CastAxis);
    }
    else if (Move == EDuelMove::VerdictTether)
    {
        SealCenters[0] = Arena->GroundMark(GetActorLocation());
        const float Reach = FMath::Clamp(static_cast<float>(FVector::Dist2D(GetActorLocation(), Target->GetActorLocation()))
            - 510.f, 0.f, TetherApproach);
        CastOrigin = Arena->GroundMark(SealCenters[0] + ActionDirection * Reach);
        bVerdictTetherHeld = false;
        const float Radius = TetherRadius[BossPhase - 1];
        Arena->Sigil(CastOrigin, Radius, MirrorLilac, Spec.Windup + Spec.Active, EDuelMark::Tether);
        Arena->Burst(CastOrigin + FVector(0.f, 0.f, 24.f), MirrorLilac, 16, 165.f, true);
    }
    else if (Move == EDuelMove::FinalSentence)
    {
        const FVector Court = Arena->CourtOrigin();
        const FBox2D Bounds = Arena->CourtBounds(8.);
        const int32 Columns = FMath::Max(2, FMath::CeilToInt(Bounds.GetSize().X / DuelBossDefense::SentenceCell));
        const int32 Rows = FMath::Max(2, FMath::CeilToInt(Bounds.GetSize().Y / DuelBossDefense::SentenceCell));
        const float CellX = Bounds.GetSize().X / Columns;
        const float CellY = Bounds.GetSize().Y / Rows;
        const int32 Offset = BossPhase == 3 ? BossRandom.RandRange(0, 1) : 0;
        MapWaves.SetNum(2);
        for (int32 I = 0; I < 2; ++I)
        {
            FMapWave& Wave = MapWaves[I];
            Wave.HalfLength = CellX * .5f - 12.f;
            Wave.Reach = CellY * .5f - 12.f;
            Wave.ImpactTime = Spec.Windup + I * DuelBossDefense::SentenceBeat;
            Wave.WarnTime = I == 0 ? 0.f : Wave.ImpactTime - DuelBossDefense::SentenceWarning;
            for (int32 X = 0; X < Columns; ++X)
                for (int32 Y = 0; Y < Rows; ++Y)
                    if ((X + Y + Offset) % 2 == I)
                        Wave.Marks.Add(Court + FVector(Bounds.Min.X + (X + .5f) * CellX,
                            Bounds.Min.Y + (Y + .5f) * CellY, 8.f));
        }
        for (const FVector& At : MapWaves[0].Marks)
            Arena->Lane(At, FVector::ForwardVector, MapWaves[0].HalfLength, MapWaves[0].Reach,
                MirrorIce, Spec.Windup, false, true, false, EDuelMark::Archive);
    }
    else if (Move == EDuelMove::MirrorEdict)
    {
        const FVector Court = Arena->CourtOrigin();
        const float RelativeY = static_cast<float>(Aim.Y - Court.Y);
        const float Side = FMath::Abs(RelativeY) > 450.f ? FMath::Sign(RelativeY) :
            (BossRandom.RandRange(0, 1) == 0 ? -1.f : 1.f);
        const float SeamY = Side * (BossPhase == 3 ? BossRandom.FRandRange(205.f, 269.f)
            : BossRandom.FRandRange(238.f, 321.f));
        const int32 Count = BossPhase == 3 ? 2 : 1;
        MapWaves.SetNum(Count);
        for (int32 I = 0; I < Count; ++I)
        {
            FMapWave& Wave = MapWaves[I];
            Wave.Marks.Add(Court + FVector(0.f, I == 0 ? SeamY : -.27f * SeamY, 8.f));
            Wave.Reach = BossPhase == 3 ? (I == 0 ? 163.f : 117.f) : 192.f;
            Wave.WarnTime = I == 0 ? 0.f : Spec.Windup + MirrorSecondWarning;
            Wave.ImpactTime = Spec.Windup + (I == 0 ? 0.f : MirrorSecondStart);
        }
        ShowMirrorSeam(*Arena, MapWaves[0].Marks[0], MapWaves[0].Reach, Spec.Windup + MirrorSweepTime, MirrorIce);
    }
    else
    {
        const int32 Count = BossPhase == 3 ? 4 : BossPhase == 2 ? 3 : 2;
        for (int32 I = 0; I < Count; ++I)
        {
            FMapWave& Wave = MapWaves.AddDefaulted_GetRef();
            Wave.Reach = 134.f + BossPhase * 36.f;
            Wave.WarnTime = I * DuelBossDefense::RecordBeat;
            Wave.ImpactTime = Spec.Windup + I * DuelBossDefense::RecordBeat;
        }
        MapWaves[0].Marks.Add(Aim);
        Arena->Sigil(Aim, MapWaves[0].Reach, YukiFrost, MapWaves[0].ImpactTime, EDuelMark::BrokenRecord);
    }
    Arena->Sound(EDuelSound::Charge, Aim);
}

void ADuelFighter::TickYukiAttack(float LastAge, const DuelCourt::FMoveTiming& Spec)
{
    if (Move == EDuelMove::FinalSentence)
    {
        for (int32 I = 0; I < MapWaves.Num() && !bCombatOver; ++I)
        {
            FMapWave& Wave = MapWaves[I];
            const FLinearColor Tint = I == 0 ? MirrorIce : MirrorLilac;
            if (I > 0 && LastAge < Wave.WarnTime && MoveAge >= Wave.WarnTime)
            {
                for (const FVector& At : Wave.Marks)
                    Arena->Lane(At, FVector::ForwardVector, Wave.HalfLength, Wave.Reach, Tint,
                        FMath::Max(.03f, Wave.ImpactTime - MoveAge), false, true, false, EDuelMark::Archive);
                Arena->Sound(EDuelSound::Charge, Arena->CourtOrigin());
            }
            if (Wave.bHitDelivered || LastAge >= Wave.ImpactTime || MoveAge < Wave.ImpactTime) continue;
            Wave.bHitDelivered = true;
            bool bHit = false;
            FVector HitFrom = Arena->CourtOrigin();
            for (const FVector& At : Wave.Marks)
            {
                Arena->Lane(At, FVector::ForwardVector, Wave.HalfLength, Wave.Reach, Tint,
                    .20f, true, true, false, EDuelMark::Archive);
                const FVector Offset = Target->GetActorLocation() - At;
                if (FMath::Abs(Offset.X) <= Wave.HalfLength && FMath::Abs(Offset.Y) <= Wave.Reach)
                {
                    bHit = true;
                    HitFrom = At;
                }
            }
            Arena->Sound(EDuelSound::VerdictBell, Arena->CourtOrigin());
            if (bHit) HitGroundSpell(BossPhase == 3 ? 60.f : 51.f, HitFrom);
        }
        return;
    }
    if (Move == EDuelMove::IcicleRow)
    {
        TickIcicleRow();
        return;
    }
    if (Move == EDuelMove::ArchiveCollapse)
    {
        TickArchiveCollapse(LastAge, Spec);
        return;
    }
    if (Move == EDuelMove::GlassShelter)
    {
        if (LastAge < Spec.Windup && MoveAge >= Spec.Windup)
        {
            const float Radius = 380.f - BossPhase * 42.f;
            Arena->GlassVerdict(CastOrigin, Radius, .78f, true);
            if (FVector::DistSquared2D(Target->GetActorLocation(), CastOrigin) > FMath::Square(Radius))
            {
                HitGroundSpell(55.f + 14.f * BossPhase, CastOrigin);
            }
        }
        return;
    }
    if (Move == EDuelMove::SilentBell)
    {
        const FVector Across = FVector::CrossProduct(FVector::UpVector, CastAxis);
        const float Width = SilentBellHalfWidth(BossPhase);
        if (LastAge < Spec.Windup && MoveAge >= Spec.Windup)
        {
            Arena->Lane(CastOrigin, CastAxis, SilentBellHalfLength, Width, YukiFrost,
                Spec.Active, true, true, false, EDuelMark::SilentBell);
            Arena->Sound(EDuelSound::VerdictBell, CastOrigin);
        }
        const FVector Offset = Target->GetActorLocation() - CastOrigin;
        const float Drift = Target->GetVelocity().Size2D();
        if (!bHitDelivered && MoveAge >= Spec.Windup && LastAge < Spec.Windup + Spec.Active &&
            Drift >= 90.f && FMath::Abs(FVector::DotProduct(Offset, Across)) <= Width &&
            FMath::Abs(FVector::DotProduct(Offset, CastAxis)) <= SilentBellHalfLength)
        {
            bHitDelivered = true;
            Arena->Burst(Target->GetActorLocation(), YukiFrost, 43, 447.f, true);
            HitGroundSpell(BossPhase == 3 ? 83.f : 69.f, CastOrigin);
        }
        return;
    }
    if (Move == EDuelMove::VerdictTether)
    {
        const float Radius = TetherRadius[BossPhase - 1];
        const float Range = static_cast<float>(FVector::Dist2D(Target->GetActorLocation(), CastOrigin));
        if (LastAge < Spec.Windup)
        {
            const float Progress = FMath::SmoothStep(0.f, 1.f, FMath::Clamp(MoveAge / Spec.Windup, 0.f, 1.f));
            const FVector Desired = FMath::Lerp(SealCenters[0], CastOrigin, Progress);
            const FVector Step = Desired - GetActorLocation();
            AddActorWorldOffset(FVector(Step.X, Step.Y, 0.f), true);
            if (MoveAge >= Spec.Windup && Range <= Radius)
            {
                bVerdictTetherHeld = true;
                Arena->Sound(EDuelSound::VerdictBell, CastOrigin);
            }
        }
        if (!bVerdictTetherHeld || MoveAge < Spec.Windup)
        {
            return;
        }
        if (Range > Radius)
        {
            bVerdictTetherHeld = false;
            Arena->Burst(Target->GetActorLocation() + FVector(0.f, 0.f, 24.f), YukiFrost, 19, 285.f, true);
            Arena->Sound(EDuelSound::Cut, Target->GetActorLocation());
            return;
        }
        const float StrikeAt = Spec.Windup + Spec.Active;
        if (LastAge < StrikeAt && MoveAge >= StrikeAt)
        {
            bVerdictTetherHeld = false;
            Arena->BossBurst(CastOrigin, Radius, FLinearColor(.52f, .69f, 1.f, .84f), .76f);
            HitGroundSpell(51.f + 9.f * BossPhase, CastOrigin);
            return;
        }
        if (LastAge < Spec.Windup || FMath::FloorToInt(LastAge / .11f) != FMath::FloorToInt(MoveAge / .11f))
        {
            const FVector Start = CastOrigin + FVector(0.f, 0.f, 120.f);
            const FVector Aim = Target->GetActorLocation() + FVector(0.f, 0.f, 43.f);
            const FVector Link = Aim - Start;
            Arena->Thrust(Start, Link.GetSafeNormal(), static_cast<float>(Link.Size()), MirrorLilac, false, EDuelMark::Tether);
        }
        return;
    }
    if (Move == EDuelMove::MirrorEdict)
    {
        const FVector Court = Arena->CourtOrigin();
        const FVector PlayerAt = Target->GetActorLocation();
        const float RelativeY = static_cast<float>(PlayerAt.Y - Court.Y);
        for (int32 I = 0; I < MapWaves.Num() && !bCombatOver; ++I)
        {
            FMapWave& Wave = MapWaves[I];
            const bool bSecond = I > 0;
            const FLinearColor& Glass = bSecond ? MirrorLilac : MirrorIce;
            const FVector& Seam = Wave.Marks[0];
            const float SweepTime = bSecond ? MirrorSecondTime : MirrorSweepTime;
            if (bSecond && LastAge < Wave.WarnTime && MoveAge >= Wave.WarnTime)
            {
                ShowMirrorSeam(*Arena, Seam, Wave.Reach, Wave.ImpactTime - Wave.WarnTime + SweepTime, Glass);
                Arena->Sound(EDuelSound::Charge, Seam);
            }
            if (MoveAge < Wave.ImpactTime || LastAge >= Wave.ImpactTime + SweepTime)
            {
                continue;
            }
            if (LastAge < Wave.ImpactTime)
            {
                Arena->Sound(EDuelSound::VerdictBell, Seam);
            }
            const float LastProgress = FMath::Clamp((LastAge - Wave.ImpactTime) / SweepTime, 0.f, 1.f);
            const float Progress = FMath::Clamp((MoveAge - Wave.ImpactTime) / SweepTime, 0.f, 1.f);
            const bool bDraw = LastAge < Wave.ImpactTime ||
                FMath::FloorToInt(LastAge / MirrorPulseSpacing) != FMath::FloorToInt(MoveAge / MirrorPulseSpacing);
            for (const float Side : {-1.f, 1.f})
            {
                const float Edge = static_cast<float>(Seam.Y - Court.Y) + Side * Wave.Reach;
                const float Outer = Side * DuelCourt::HalfWidth;
                const float Front = FMath::Lerp(Outer, Edge, FMath::SmoothStep(0.f, 1.f, Progress));
                const float PreviousFront = FMath::Lerp(Outer, Edge, FMath::SmoothStep(0.f, 1.f, LastProgress));
                // Include the full travel between frames without biting into the marked safe seam.
                const float Inner = Front + Side * .5f;
                const float Rim = PreviousFront + Side * (2.f * MirrorPanelHalfDepth);
                if (bDraw)
                {
                    const FVector Panel = Court + FVector(0.f, Front + Side * MirrorPanelHalfDepth, 8.5f);
                    Arena->Lane(Panel, FVector::ForwardVector, MirrorHalfLength, MirrorPanelHalfDepth,
                        Glass, .07f, true, true, false, EDuelMark::MirrorPanel);
                    if (LastAge < Wave.ImpactTime || FMath::FloorToInt(LastAge / .15f) != FMath::FloorToInt(MoveAge / .15f))
                    {
                        const FVector Center = Panel + FVector(FMath::Clamp(PlayerAt.X - Court.X,
                            -static_cast<double>(DuelCourt::HalfLength), static_cast<double>(DuelCourt::HalfLength)), 0.f, 20.f);
                        Arena->Burst(Center, Glass, 12, 340.f, true);
                    }
                }
                if (!Wave.bHitDelivered && FMath::Abs(PlayerAt.X - Court.X) <= MirrorHalfLength &&
                    Side * (RelativeY - Inner) >= 0.f && Side * (RelativeY - Rim) <= 0.f)
                {
                    Wave.bHitDelivered = true;
                    Arena->Burst(PlayerAt, Glass, 21, 370.f, true);
                    HitGroundSpell(bSecond ? 67.f : 43.f + 9.f * BossPhase, Court + FVector(0.f, Front, 8.f));
                }
                if (bCombatOver)
                {
                    break;
                }
            }
        }
        return;
    }

    for (int32 I = 0; I < MapWaves.Num() && !bCombatOver; ++I)
    {
        FMapWave& Wave = MapWaves[I];
        if (I > 0 && LastAge < Wave.WarnTime && MoveAge >= Wave.WarnTime)
        {
            const FVector At = Arena->GroundMark(Target->GetActorLocation());
            Wave.Marks.Add(At);
            Arena->Sigil(At, Wave.Reach, YukiFrost, FMath::Max(.03f, Wave.ImpactTime - MoveAge), EDuelMark::BrokenRecord);
        }
        if (LastAge >= Wave.ImpactTime || MoveAge < Wave.ImpactTime || Wave.Marks.IsEmpty())
        {
            continue;
        }
        const FVector At = Wave.Marks[0];
        Wave.bHitDelivered = true;
        Arena->Rainfall({At}, Wave.Reach, YukiFrost, true, EDuelMark::BrokenRecord);
        if (FVector::DistSquared2D(Target->GetActorLocation(), At) <= FMath::Square(Wave.Reach))
        {
            HitGroundSpell(43.f + 10.f * BossPhase, At);
        }
    }
}

void ADuelFighter::PrepareIcicleRow(const DuelCourt::FMoveTiming& Spec)
{
    using namespace DuelBossDefense;
    const int32 Count = BossPhase == 3 ? MaxIcicles : MaxIcicles - 1;
    MapWaves.SetNum(Count);
    const FVector Court = Arena->CourtOrigin();
    const FVector Local = GetActorLocation() - Court;
    FVector Across = FVector::CrossProduct(FVector::UpVector, CastAxis);
    const double Inward = FVector::DotProduct(-Local, Across);
    Across *= FMath::Abs(Inward) > 100.0 ? FMath::Sign(Inward) : OrbitSide;
    constexpr double Spacing = 83;
    const double Span = (Count - 1) * Spacing * .5;
    const FBox2D Bounds = Arena->CourtBounds().ExpandBy(-FVector2D(
        93. + Span * FMath::Abs(Across.X), 93. + Span * FMath::Abs(Across.Y)));
    const FVector Proposed = Local + Across * (160.0 + Span) - CastAxis * 34.f;
    const FVector Center = Court + FVector(Bounds.GetClosestPointTo(FVector2D(Proposed)), 0.);
    for (int32 I = 0; I < Count; ++I)
    {
        FMapWave& Wave = MapWaves[I];
        const FVector From = Arena->GroundMark(Center + Across * (I * Spacing - Span)) + FVector(0, 0, 108.f + 12.f * (I % 2));
        Wave.Marks.Add(From);
        Wave.Axis = CastAxis;
        Wave.Reach = 43.f + 4.f * BossPhase;
        Wave.ImpactTime = Spec.Windup + I * IcicleBeat;
        Wave.WarnTime = Wave.ImpactTime - IcicleAimLead;
        Wave.Preview = Arena->StageIcicle(From, Wave.Axis, I % 2 ? MirrorLilac : MirrorIce, Spec.Total() + .15f);
    }
}

void ADuelFighter::TickIcicleRow()
{
    using namespace DuelBossDefense;
    for (int32 I = 0; I < MapWaves.Num(); ++I)
    {
        FMapWave& Wave = MapWaves[I];
        if (Wave.bHitDelivered) continue;
        if (MoveAge < Wave.WarnTime) return;
        const FVector From = Wave.Marks[0];
        const FLinearColor Tint = I % 2 ? MirrorLilac : MirrorIce;
        if (Wave.Marks.Num() == 1)
        {
            const FVector Aim = Target->GetActorLocation();
            Wave.Marks.Add(Aim);
            Wave.Axis = (Aim - From).GetSafeNormal2D(UE_SMALL_NUMBER, CastAxis);
            Wave.HalfLength = FMath::Clamp(static_cast<float>(FVector::Dist2D(From, Aim)) + 410.f, 950.f, 2500.f) * .5f;
            if (USceneComponent* Preview = Wave.Preview.Get()) Preview->SetWorldRotation(Wave.Axis.Rotation());
            Arena->Lane(Arena->GroundMark(From) + Wave.Axis * Wave.HalfLength, Wave.Axis,
                Wave.HalfLength, Wave.Reach, Tint * .68f, FMath::Max(.035f, Wave.ImpactTime - MoveAge),
                false, true, false, EDuelMark::IcicleAim);
        }
        if (MoveAge < Wave.ImpactTime) return;
        Wave.bHitDelivered = true;
        if (USceneComponent* Preview = Wave.Preview.Get()) Preview->DestroyComponent();
        Wave.Preview.Reset();
        Wave.Projectile = Arena->LaunchThread(Target, From, Wave.Axis, 23.f + 3.f * BossPhase, 0.f, Tint,
            IcicleSpeed, Wave.Reach, Wave.HalfLength * 2.f / IcicleSpeed, true, 0.f, false, 0.f, EDuelMark::IceLance);
        Arena->Thrust(From, Wave.Axis, 300.f, Tint, true, EDuelMark::IceLance);
        Arena->Sound(EDuelSound::Cut, From);
    }
}

void ADuelFighter::ClearAttackPreviews()
{
    for (FMapWave& Wave : MapWaves)
    {
        if (USceneComponent* Preview = Wave.Preview.Get()) Preview->DestroyComponent();
        if (USceneComponent* Guide = Wave.Guide.Get()) Guide->DestroyComponent();
        Wave.Preview.Reset();
        Wave.Guide.Reset();
    }
}

void ADuelFighter::TickBossProjectiles(const DuelCourt::FMoveTiming& Spec)
{
    const bool bPrism = Move == EDuelMove::PrismPursuit;
    const float ShadowSpeed = DuelBossDefense::ShadowProjectileSpeed(BossPhase);
    for (int32 I = 0; I < MapWaves.Num(); ++I)
    {
        FMapWave& Wave = MapWaves[I];
        if (Wave.ProjectileRetiredAt >= 0.f)
        {
            continue;
        }
        if (Wave.bHitDelivered)
        {
            // The arena retires a projectile at a contact (including a dodge) or after its flight.
            if (Wave.Projectile.IsValid())
            {
                return;
            }
            Wave.ProjectileRetiredAt = MoveAge;
            continue;
        }

        const FLinearColor Tint = bPrism ? (I % 2 == 0 ? MirrorIce : MirrorLilac) :
            (I % 2 == 0 ? Carmine * 2.2f : Rose * 1.9f);
        if (Wave.Marks.IsEmpty())
        {
            Wave.WarnTime = MoveAge;
            Wave.ImpactTime = MoveAge + (I == 0 ? Spec.Windup : DuelBossDefense::HitGap);
            if (bPrism)
            {
                const FVector Court = Arena->CourtOrigin();
                const FVector Aim = Arena->GroundMark(Target->GetActorLocation());
                const float Side = OrbitSide * (I % 2 == 0 ? 1.f : -1.f);
                const FVector Local = Aim - Court;
                const FVector From = Court + FVector(FMath::Clamp(Local.X - 625.f + I * 420.f, -1480.0, 1480.0),
                    Side * 970.f, 44.f + 8.f * I);
                Wave.Marks.Add(From);
                Wave.Axis = (Aim - From).GetSafeNormal2D(UE_SMALL_NUMBER, FVector(0.f, -Side, 0.f));
                Wave.Reach = 70.f + 16.f * BossPhase;
                Wave.HalfLength = FMath::Clamp(static_cast<float>(FVector::Dist2D(From, Aim)) + 400.f, 480.f, 2470.f) * .5f;
                Arena->Lane(From + Wave.Axis * Wave.HalfLength + FVector(0, 0, 8 - (From.Z - Court.Z)),
                    Wave.Axis, Wave.HalfLength, Wave.Reach, Tint * .77f, Wave.ImpactTime - MoveAge,
                    false, true, false, EDuelMark::Prism);
                Arena->Burst(From + FVector(0, 0, 70), Tint, 20, 265.f, true);
            }
            else
            {
                Wave.HalfLength = ShadowSpeed * VolleyLife * .5f;
                Wave.Reach = BossPhase == 3 ? 114.f : 91.f;
                ShowMapWarning(I);
            }
            return;
        }
        if (MoveAge < Wave.ImpactTime)
        {
            return;
        }

        Wave.bHitDelivered = true;
        Wave.ImpactTime = MoveAge;
        const FVector From = bPrism ? Wave.Marks[0] : Wave.Marks[0] - Wave.Axis * Wave.HalfLength + FVector(0, 0, 34);
        if (bPrism)
        {
            Wave.Projectile = Arena->LaunchThread(Target, From, Wave.Axis, 25.f + 4.f * BossPhase, 0.f, Tint,
                DuelCourt::PrismSpeed, Wave.Reach, Wave.HalfLength * 2.f / DuelCourt::PrismSpeed,
                true, 0.f, false, 0.f, EDuelMark::Prism);
            Arena->Thrust(From, Wave.Axis, 285.f, Tint, true, EDuelMark::Prism);
        }
        else
        {
            Wave.Projectile = Arena->LaunchThread(Target, From, Wave.Axis, BossPhase == 3 ? 51.f : 39.f, 0.f, Tint,
                ShadowSpeed, Wave.Reach, VolleyLife, true, 0.f, true, 0.f, EDuelMark::ShadowVolley);
            Arena->Burst(From, Tint, 19, 460.f, true);
        }
        Arena->Sound(EDuelSound::Cut, From);
        return;
    }
}

void ADuelFighter::TickArchiveCollapse(float LastAge, const DuelCourt::FMoveTiming& Spec)
{
    using namespace DuelCourt;
    const float First = Spec.Windup;
    const float Second = First + ArchiveReturnAt;
    if (LastAge < First && MoveAge >= First)
    {
        Arena->InwardWave(CastOrigin, ArchiveOuter, ArchiveInner, ArchiveSweepTime, MirrorIce, EDuelMark::Archive);
        Arena->Sound(EDuelSound::VerdictBell, CastOrigin);
    }
    if (BossPhase == 3 && LastAge < Second && MoveAge >= Second)
    {
        Arena->StormFront(CastOrigin, ArchiveOuter, MirrorLilac, ArchiveReturnTime, EDuelMark::Archive);
        Arena->Burst(CastOrigin + FVector(0, 0, 62), MirrorLilac, 39, 430.f, true);
        Arena->Sound(EDuelSound::VerdictBell, CastOrigin);
    }
    if (BossPhase == 3 && LastAge < Second - DuelBossDefense::HitGap && MoveAge >= Second - DuelBossDefense::HitGap)
    {
        Arena->Sigil(CastOrigin, ArchiveInner, MirrorLilac, DuelBossDefense::HitGap, EDuelMark::Archive);
        Arena->Flash(CastOrigin + FVector(0, 0, 88), MirrorLilac, 11400.f, DuelBossDefense::HitGap, 490.f);
    }
    const float Distance = FVector::Dist2D(Target->GetActorLocation(), CastOrigin);
    const double Height = Target->GetActorLocation().Z - Arena->CourtOrigin().Z -
        Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    for (int32 I = 0; I < (BossPhase == 3 ? 2 : 1) && !bCombatOver; ++I)
    {
        const float At = I == 0 ? First : Second;
        const float Length = I == 0 ? ArchiveSweepTime : ArchiveReturnTime;
        if (BoneWaveHits[I] || LastAge >= At + Length || MoveAge < At)
        {
            continue;
        }
        const auto Front = [&](float Age)
        {
            const float Alpha = FMath::Clamp((Age - At) / Length, 0.f, 1.f);
            return I == 0 ? FMath::Lerp(ArchiveOuter, ArchiveInner, Alpha) : FMath::Lerp(70.f, ArchiveOuter, Alpha);
        };
        const float A = Front(LastAge);
        const float B = Front(MoveAge);
        const float Inner = I == 0 ? ArchiveInner : 70.f;
        if (Distance >= FMath::Max(Inner, FMath::Min(A, B) - 34.f) &&
            Distance <= FMath::Min(ArchiveOuter, FMath::Max(A, B) + 34.f) && Height < 40.0)
        {
            BoneWaveHits[I] = true;
            HitGroundSpell(I == 0 ? 50.f + 4.f * BossPhase : 53.f, CastOrigin);
        }
    }
}

void ADuelFighter::ShowMapWarning(int32 Index)
{
    const bool bTribunal = Move == EDuelMove::Tribunal;
    const bool bCircles = Move == EDuelMove::AtlasRain || Move == EDuelMove::Pursuit || bTribunal;
    FMapWave& Wave = MapWaves[Index];
    if (Move == EDuelMove::ShadowVolley)
    {
        const FVector Base = Arena->GroundMark(GetActorLocation());
        const FVector Aim = (Target->GetActorLocation() - Base).GetSafeNormal2D(UE_SMALL_NUMBER, ActionDirection);
        const float Offset = Index == 0 || Index == MapWaves.Num() - 1 ? 0.f : (Index == 1 ? 11.f : -11.f) * OrbitSide;
        Wave.Axis = Aim.RotateAngleAxis(Offset, FVector::UpVector);
        Wave.Marks.Add(Base + Wave.Axis * Wave.HalfLength);
    }
    const FLinearColor Tint = Move == EDuelMove::WingVerdict ? Gold : bTribunal ? PaleGold :
        Move == EDuelMove::ShadowVolley ? DarkCarmine : Move == EDuelMove::Pursuit ? Gold : WaveTint(bCircles, Index);
    const float Life = FMath::Max(.03f, Wave.ImpactTime - MoveAge);
    for (const FVector& At : Wave.Marks)
    {
        if (bCircles)
        {
            const EDuelMark Mark = bTribunal ? EDuelMark::Tribunal :
                Move == EDuelMove::Pursuit ? EDuelMark::Pursuit : EDuelMark::Atlas;
            Arena->Sigil(At, Wave.Reach, Tint, Life, Mark, CastAxis);
        }
        else
        {
            const EDuelMark Mark = Move == EDuelMove::WingVerdict ? EDuelMark::WingLane :
                Move == EDuelMove::ShadowVolley ? EDuelMark::ShadowVolley : EDuelMark::NightLane;
            Arena->Lane(At, Wave.Axis, Wave.HalfLength, Wave.Reach, Tint, Life, false, true, false, Mark);
        }
    }
}

void ADuelFighter::TickMapAttack(float LastAge)
{
    const bool bAtlas = Move == EDuelMove::AtlasRain;
    const bool bPursuit = Move == EDuelMove::Pursuit;
    const bool bTribunal = Move == EDuelMove::Tribunal;
    const bool bWing = Move == EDuelMove::WingVerdict;
    for (int32 I = 0; I < MapWaves.Num() && !bCombatOver; ++I)
    {
        FMapWave& Wave = MapWaves[I];
        if (LastAge < Wave.WarnTime && MoveAge >= Wave.WarnTime)
        {
            if (bPursuit || (bTribunal && I == MapWaves.Num() - 1))
            {
                const FVector Aim = Arena->GroundMark(Target->GetActorLocation());
                Wave.Marks.Add(Aim);
                if (bPursuit && BossPhase == 3 && I >= 2)
                {
                    const FVector Toward = (Aim - GetActorLocation()).GetSafeNormal2D(UE_SMALL_NUMBER, CastAxis);
                    const FVector Across = FVector::CrossProduct(FVector::UpVector, Toward) * OrbitSide;
                    const FVector Flank = Arena->GroundMark(Aim + Toward * 248.f + Across * (I == 2 ? 547.f : -547.f));
                    if (FVector::DistSquared2D(Aim, Flank) >= FMath::Square(Wave.Reach * 1.55f))
                    {
                        Wave.Marks.Add(Flank);
                    }
                }
            }
            if (MoveAge < Wave.ImpactTime)
            {
                ShowMapWarning(I);
            }
        }
        if (LastAge >= Wave.ImpactTime || MoveAge < Wave.ImpactTime)
        {
            continue;
        }
        const FLinearColor Tint = bWing ? Gold : bTribunal ? PaleGold : bPursuit ? Gold : WaveTint(bAtlas, I);
        const FVector PlayerAt = Target->GetActorLocation();
        bool bHit = false;
        FVector HitSource = GetActorLocation();
        double NearestMark = TNumericLimits<double>::Max();
        if (bAtlas || bPursuit || bTribunal)
        {
            if (bAtlas || bTribunal)
            {
                Arena->Rainfall(Wave.Marks, Wave.Reach, Tint, bTribunal,
                    bTribunal ? EDuelMark::Tribunal : EDuelMark::Atlas, CastAxis);
            }
            for (const FVector& At : Wave.Marks)
            {
                if (bPursuit)
                {
                    Arena->Detonate(At, Wave.Reach, Tint, .44f, EDuelMark::Pursuit, CastAxis);
                }
                const double DistanceSq = FVector::DistSquared2D(PlayerAt, At);
                if (DistanceSq <= FMath::Square(Wave.Reach) && DistanceSq < NearestMark)
                {
                    bHit = true;
                    NearestMark = DistanceSq;
                    HitSource = At;
                }
            }
        }
        else
        {
            const FVector Across = FVector::CrossProduct(FVector::UpVector, Wave.Axis);
            for (const FVector& At : Wave.Marks)
            {
                Arena->Lane(At, Wave.Axis, Wave.HalfLength, Wave.Reach, Tint, .46f, true, true, bWing,
                    bWing ? EDuelMark::WingLane : EDuelMark::NightLane);
                const FVector Offset = PlayerAt - At;
                const double AcrossDistance = FMath::Abs(FVector::DotProduct(Offset, Across));
                if (AcrossDistance <= Wave.Reach &&
                    FMath::Abs(FVector::DotProduct(Offset, Wave.Axis)) <= Wave.HalfLength && AcrossDistance < NearestMark)
                {
                    bHit = true;
                    NearestMark = AcrossDistance;
                    HitSource = At - Wave.Axis * Wave.HalfLength;
                }
            }
            const FVector Origin = Arena->CourtOrigin();
            Arena->Flash(FVector(Origin.X, PlayerAt.Y, Origin.Z + 158.f), Tint, 21400.f, .42f, 1420.f);
            Arena->Sound(EDuelSound::Pulse, PlayerAt);
        }
        if (bHit)
        {
            const float Damage = bPursuit ? (BossPhase == 3 ? 56.f : 45.f) : bTribunal ? 55.f + I * 7.f :
                bWing ? 49.f + (BossPhase - 2) * 10.f :
                (BossPhase == 3 ? 65.f : 53.f);
            const bool bCircle = bAtlas || bPursuit || bTribunal;
            if (HitGroundSpell(Damage, HitSource, bCircle ? nullptr : &HitSource, !bWing))
                Target->CameraImpulse = FMath::Max(Target->CameraImpulse, .84f);
        }
    }
}
