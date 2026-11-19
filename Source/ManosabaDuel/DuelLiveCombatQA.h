#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "DuelGame.h"

// The same battle policy and observations serve isolated matches and full journeys.
struct FDuelLiveCombat
{
    static constexpr double TimeLimit = 160.0;
    double Started = 0;
    double NextReaction = 0;
    double ShieldUntil = 0;
    EDuelMove Observed = EDuelMove::Idle;
    EDuelMove PreviousPlayerMove = EDuelMove::Idle;
    int32 Dodged = 0;
    int32 Parried = 0;
    int32 Hits = 0;
    int32 PeakPhase = 1;
    float Health = DuelCourt::BossMaxHealth;
    float PlayerHealth = DuelCourt::PlayerMaxHealth;
    uint8 Phases = 0;
    TSet<EDuelMove> Moves;

    void Begin(ADuelMode& Mode, int32 Seed);
    void Observe(ADuelMode& Mode, int32 Hearing);
    void Drive(ADuelMode& Mode, double Now, int32 Hearing);
};

#endif
