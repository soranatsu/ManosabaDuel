#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "DuelProgress.generated.h"

UCLASS()
class UDuelProgress final : public USaveGame
{
    GENERATED_BODY()

public:
    UPROPERTY() bool bAppeal = false;
    UPROPERTY() bool bYukiCourt = false;
    UPROPERTY() bool bFinished = false;
    UPROPERTY() bool bCompanionFollowing = false;
    UPROPERTY() uint8 Character = 0;
    UPROPERTY() uint8 FirstBoss = 1;
    UPROPERTY() uint8 Evidence = 0;
    UPROPERTY() uint8 Right = 0;
    UPROPERTY() uint8 Left = 0;
    UPROPERTY() uint8 RightReserve = 0;
    UPROPERTY() uint8 LeftReserve = 0;
    UPROPERTY() uint8 Art = 0;
    // Legacy single-spell field. Kept to read checkpoints written before spell slots.
    UPROPERTY() uint8 Spell = 0;
    UPROPERTY() TArray<uint8> StaffSpells;
    UPROPERTY() uint8 ActiveSpellSlot = 0;
    UPROPERTY() int32 BloodFlasks = 3;
    UPROPERTY() int32 FlaskBudget = 0;
    UPROPERTY() int32 FirstFight = 0;
    UPROPERTY() int32 SecondFight = 0;
    UPROPERTY() int32 FinalFight = 0;
    UPROPERTY() uint8 CastleFormat = 0;
    UPROPERTY() uint8 CastleKeys = 0;
    UPROPERTY() uint8 CastleClues = 0;
    UPROPERTY() uint8 CastleSeals = 0;
    UPROPERTY() uint8 CastleShortcuts = 0;
    UPROPERTY() uint8 CastleWins = 0;
    UPROPERTY() uint8 VerdictScars = 0;
    UPROPERTY() bool bEastOpen = false;
    UPROPERTY() bool bFinalRecord = false;
};
