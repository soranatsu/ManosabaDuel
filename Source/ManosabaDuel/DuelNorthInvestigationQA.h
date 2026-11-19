#pragma once

#include "DuelNorthProgress.h"

namespace DuelNorthInvestigationQA
{
    // Seed completed investigations for fixtures that exercise dialogue, movement, or checkpoint restoration.
    inline bool Prepare(FDuelNorthProgress& State, int32 Battle)
    {
        if (State.HasInvestigation(Battle)) return true;
        if (Battle == 2)
        {
            for (const TCHAR* Id : {TEXT("BeaconChart"), TEXT("LensWorkshop"), TEXT("BeaconLetter"), TEXT("BeaconSwitch")})
                if (!State.HasBeacon(Id) && !State.RecordBeacon(Id)) return false;
            return State.HasInvestigation(2);
        }
        const auto Read = [&](EDuelNorthClue Clue) { return State.HasClue(Clue) || State.ReadClue(Clue); };
        const bool bEvidence = Battle == 0 ? Read(EDuelNorthClue::RefugeLedger) :
            Battle == 1 && Read(EDuelNorthClue::ArchiveTransferLedger) && Read(EDuelNorthClue::ArchiveSignatureRegister);
        if (!bEvidence) return false;
        State.CacheBits |= 1u << Battle;
        return State.HasInvestigation(Battle);
    }
}
