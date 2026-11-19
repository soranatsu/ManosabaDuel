#include "DuelCastle.h"

#include "DuelArena.h"
#include "DuelNorthTower.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

namespace
{
    constexpr const TCHAR* WingNames[] = {TEXT("沉水层"), TEXT("押送层"), TEXT("锅炉房")};
    constexpr const TCHAR* KeyNames[] = {
        TEXT("F   取走押送层的钥匙"), TEXT("F   取走锅炉房的钥匙"), TEXT("F   取走沉水层的钥匙")
    };
    constexpr const TCHAR* KeyNotes[] = {
        TEXT("水闸的钥匙牌写着押送层。外圈的路还通着。"),
        TEXT("看守把锅炉钥匙挂在外廊。"),
        TEXT("炉边铜牌背面刻着沉水层的钥匙编号。")
    };
    constexpr const TCHAR* CluePrompts[DuelCastle::WingCount][2] = {
        {TEXT("F   检查铁闸撞痕"), TEXT("F   翻看水牢扣押清册")},
        {TEXT("F   核对值班时钟"), TEXT("F   查看押送木牌背面")},
        {TEXT("F   察看断裂的表针"), TEXT("F   检查锅炉房内闩")}
    };
    constexpr const TCHAR* ClueNotes[DuelCastle::WingCount][2] = {
        {TEXT("闸杆只从里面弯了。她试过推开闸门。"), TEXT("押送清册记着鞋。鞋丢在水里时，警铃还没有响。")},
        {TEXT("二十三点十六分。这里记录的是传来火警的时刻。"), TEXT("证言只有前半句。她看见有人出来，没看清脸。")},
        {TEXT("表针断在火警之前。断掉的是蒸汽管。"), TEXT("里面的闩座向外折断。有人从火里推开过门。")}
    };
    constexpr const TCHAR* SealNotes[DuelCastle::WingCount] = {
        TEXT("第一块判词。她试图逃走，随后折返。"),
        TEXT("第二块判词。她看见有人出来，始终认不出那张脸。"),
        TEXT("第三块判词。她走进过火场。那个人是去救人的。")
    };
}

int32 ADuelMode::CastleMarks() const
{
    return !!(CastleSeals & 1) + !!(CastleSeals & 2) + !!(CastleSeals & 4);
}

bool ADuelMode::OwnsWeapon(EDuelWeapon Weapon) const
{
    return !NorthTowerActor || NorthTowerActor->OwnsWeapon(Weapon);
}

bool ADuelMode::UsesExpeditionLoot() const
{
    return NorthTowerActor && NorthTowerActor->UsesExpeditionLoot();
}

bool ADuelMode::KnowsSpell(EDuelArt Spell) const
{
    if (NorthTowerActor) return NorthTowerActor->KnowsSpell(Spell);
    switch (Spell)
    {
    case EDuelArt::LanternMote: return bCastleRoute && (CastleKeys & DuelCastle::WingBit(1));
    case EDuelArt::Blackwell: return bCastleRoute && (CastleClues & DuelCastle::ClueMask(0)) == DuelCastle::ClueMask(0);
    case EDuelArt::SaltArc: return bCastleRoute && (CastleKeys & DuelCastle::WingBit(0));
    case EDuelArt::WhiteMantle: return bCastleRoute && (CastleSeals & DuelCastle::WingBit(1));
    case EDuelArt::WitnessStep: return bCastleRoute && (CastleSeals & DuelCastle::WingBit(2));
    default: return MakeArrayView(DuelCourt::StaffSpells).Contains(Spell);
    }
}

bool ADuelMode::CanEnterCastleWing(int32 Wing) const
{
    const uint8 Clues = DuelCastle::ClueMask(Wing);
    return (CastleKeys & DuelCastle::WingBit(Wing)) != 0 && (CastleClues & Clues) == Clues;
}

int32 ADuelMode::NearbyFog() const
{
    if (Result != EDuelOutcome::Ready || !PlayerFighter)
    {
        return INDEX_NONE;
    }
    const FVector At = PlayerFighter->GetActorLocation();
    for (int32 Wing = 0; Wing < DuelCastle::WingCount; ++Wing)
    {
        const FVector Local = At - DuelCastle::Wings[Wing].Court;
        if (DuelCastle::Close(At, DuelCastle::Gate(Wing), 225.f) && Local.X < DuelCourt::GateX)
        {
            return Wing;
        }
    }
    if (bEastOpen && At.X < DuelCourt::YukiOrigin.X + DuelCourt::GateX &&
        DuelCastle::Close(At, DuelCourt::YukiOrigin + FVector(DuelCourt::GateX - 127.f, 0, 0), 225.f))
    {
        return DuelCastle::WingCount;
    }
    return INDEX_NONE;
}

const TCHAR* ADuelMode::ActiveInvestigation() const
{
    return GetWorld()->GetTimeSeconds() < InvestigationUntil ? InvestigationLine : nullptr;
}

void ADuelMode::TellInvestigation(const TCHAR* Text)
{
    InvestigationLine = Text;
    InvestigationUntil = GetWorld()->GetTimeSeconds() + 5.6;
}

const TCHAR* ADuelMode::CastleArea() const
{
    if (bYukiCourt)
    {
        return Result == EDuelOutcome::Epilogue ? TEXT("月庭") : TEXT("终审庭");
    }
    if (CastleDuel >= 0 && CastleDuel < DuelCastle::WingCount && Result != EDuelOutcome::Ready)
    {
        return WingNames[CastleDuel];
    }
    const FVector At = PlayerFighter->GetActorLocation();
    if (At.X > DuelCastle::EastGate.X - 390.f)
    {
        return TEXT("裁决长阶");
    }
    if (At.Y < -7900.f)
    {
        return WingNames[2];
    }
    if (At.X < -3250.f)
    {
        return WingNames[0];
    }
    return At.X > 2700.f ? WingNames[1] : TEXT("中庭钟楼");
}

FVector ADuelMode::InvestigationTarget() const
{
    const FVector At = PlayerFighter->GetActorLocation();
    if (bYukiCourt)
    {
        return Result == EDuelOutcome::Victory && !bFinalRecord ? DuelCastle::FinalRecord() : DuelCourt::YukiOrigin;
    }
    if (bEastOpen)
    {
        return DuelCastle::EastGate;
    }
    if (CastleSeals == DuelCastle::Whole)
    {
        return DuelCastle::IndexStone;
    }
    FVector Target = DuelCastle::Hub;
    double Distance = TNumericLimits<double>::Max();
    const auto Consider = [&](const FVector& Point)
    {
        if (const double Range = FVector::DistSquared(At, Point); Range < Distance)
        {
            Target = Point;
            Distance = Range;
        }
    };
    for (int32 Wing = 0; Wing < DuelCastle::WingCount; ++Wing)
    {
        const uint8 Bit = DuelCastle::WingBit(Wing);
        const DuelCastle::FWing& Zone = DuelCastle::Wings[Wing];
        if (!(CastleKeys & DuelCastle::WingBit((Wing + 1) % DuelCastle::WingCount)))
        {
            Consider(Zone.Key);
        }
        if (CastleSeals & Bit)
        {
            continue;
        }
        if (CastleWins & Bit)
        {
            Consider(DuelCastle::Record(Wing));
            continue;
        }
        for (int32 Item = 0; Item < 2; ++Item)
        {
            if (!(CastleClues & (1 << (Wing * 2 + Item))))
            {
                Consider(Zone.Clue[Item]);
            }
        }
        if (CanEnterCastleWing(Wing))
        {
            Consider(DuelCastle::Gate(Wing));
        }
    }
    return Target;
}

const TCHAR* ADuelMode::CastlePrompt() const
{
    if (!PlayerFighter || (Result != EDuelOutcome::Ready && Result != EDuelOutcome::Victory))
    {
        return nullptr;
    }
    const FVector At = PlayerFighter->GetActorLocation();
    if (Result == EDuelOutcome::Victory)
    {
        if (bYukiCourt)
        {
            return bFinalRecord ? TEXT("穿过东门，前往月庭") : TEXT("到证人席前归还证词末页");
        }
        return TEXT("F   读取判词铭牌");
    }
    if (DuelCastle::Close(At, DuelCastle::Hub, 250.f))
    {
        return TEXT("F   在钟楼休息并记录");
    }
    if (DuelCastle::Close(At, DuelCastle::IndexStone, 230.f))
    {
        return bEastOpen ? TEXT("F   回读完整证词") : TEXT("F   核对三面钟的判词");
    }
    if (const int32 Wing = NearbyFog(); Wing != INDEX_NONE)
    {
        if (Wing == DuelCastle::WingCount)
        {
            return bGateOpen ? TEXT("沿台阶走进终审庭") : TEXT("F   打开终审雾门");
        }
        if (CastleSeals & DuelCastle::WingBit(Wing))
        {
            return TEXT("这一面钟已静止。判词可在中庭重读。");
        }
        if (CastleWins & DuelCastle::WingBit(Wing))
        {
            return TEXT("走入庭内，从证人席取回铭牌。");
        }
        if (bGateOpen && CastleDuel == Wing)
        {
            return TEXT("走入雾门");
        }
        return CanEnterCastleWing(Wing) ? TEXT("F   核对证物并打开内门") :
            TEXT("内门锁着。从相邻区取到本门钥匙，并查阅此处的两件证物。");
    }
    for (int32 Wing = 0; Wing < DuelCastle::WingCount; ++Wing)
    {
        const DuelCastle::FWing& Zone = DuelCastle::Wings[Wing];
        if ((CastleWins & DuelCastle::WingBit(Wing)) && !(CastleSeals & DuelCastle::WingBit(Wing)) &&
            DuelCastle::Close(At, DuelCastle::Record(Wing), 278.f))
        {
            return TEXT("F   读取判词铭牌");
        }
        const uint8 Key = DuelCastle::WingBit((Wing + 1) % DuelCastle::WingCount);
        if (!(CastleKeys & Key) && DuelCastle::Close(At, Zone.Key))
        {
            return KeyNames[Wing];
        }
        for (int32 Item = 0; Item < 2; ++Item)
        {
            if (DuelCastle::Close(At, Zone.Clue[Item]))
            {
                return CluePrompts[Wing][Item];
            }
        }
        if (!(CastleShortcuts & DuelCastle::WingBit(Wing)) && DuelCastle::Close(At, Zone.Shortcut, 176.f))
        {
            return TEXT("F   从内侧抬起近路铁闸");
        }
    }
    if (!bEastOpen && DuelCastle::Close(At, DuelCastle::EastGate, 385.f))
    {
        return TEXT("东门等三面钟停摆。返回中庭核对铭牌。");
    }
    return nullptr;
}

void ADuelMode::SetCastleCourt(int32 Wing)
{
    CastleDuel = Wing;
    bAppeal = Wing == 1 || Wing == DuelCastle::WingCount;
    bYukiCourt = Wing == DuelCastle::WingCount;
    Arena->SetYukiCourt(bYukiCourt);
    Arena->SetAppeal(bAppeal);
    Arena->SetFurnaceCourt(Wing == 2);
}

void ADuelMode::RestoreCastleLayout()
{
    Arena->ApplyCastleState(CastleSeals, CastleShortcuts, bEastOpen, bFinalRecord);
    for (int32 Wing = 0; Wing < DuelCastle::WingCount; ++Wing)
    {
        if (CastleWins & DuelCastle::WingBit(Wing))
        {
            Arena->SetCastleFog(Wing, false);
        }
    }
    if (bFinalRecord)
    {
        Arena->SetCastleFog(DuelCastle::WingCount, false);
    }
    if ((CastleKeys & ~DuelCastle::WingBit(0)) != 0)
    {
        Arena->OpenPrisonGate();
    }
}

void ADuelMode::ReturnToCastleHub()
{
    bGateOpen = false;
    SetCastleCourt(INDEX_NONE);
    Arena->SetDawn(false);
    SetBoss(PlayerFighter->SelectedCharacter());
    ResetEncounter();
    Result = EDuelOutcome::Ready;
    Arena->SetCombatCamera(false);
    for (int32 Wing = 0; Wing <= DuelCastle::WingCount; ++Wing)
    {
        Arena->SetCastleFog(Wing, true);
    }
    RestoreCastleLayout();
    PlayerFighter->SetActorLocationAndRotation(DuelCastle::Spawn.GetLocation(), DuelCastle::Spawn.GetRotation(),
        false, nullptr, ETeleportType::TeleportPhysics);
    PlayerFighter->GetCharacterMovement()->StopMovementImmediately();
    PlayerFighter->EndCombat();
    PlayerFighter->PrepareExploration();
    PlayerFighter->ViewYaw = DuelCastle::Spawn.Rotator().Yaw;
    PlayerFighter->SetActorRotation(DuelCastle::Spawn.Rotator());
    BossFighter->EndCombat();
    BossFighter->SetActorHiddenInGame(true);
    BossFighter->SetActorEnableCollision(false);
}

void ADuelMode::TickCastle(const FVector& At)
{
    if (Result == EDuelOutcome::Epilogue || Result == EDuelOutcome::Title)
    {
        return;
    }
    if (At.Z < DuelCastle::DropLimit && (Result == EDuelOutcome::Ready || Result == EDuelOutcome::Victory))
    {
        ReturnToCastleHub();
        return;
    }
    if (bFinalRecord && Result == EDuelOutcome::Ready && At.X > DuelCourt::YukiOrigin.X + DuelCourt::GateCrossX &&
        At.Y < 260.f && At.Y > -260.f)
    {
        SetCastleCourt(DuelCastle::WingCount);
        Result = EDuelOutcome::Victory;
        Arena->SetDawn(true);
    }
    const FVector Local = At - Arena->CourtOrigin();
    if (Result == EDuelOutcome::Ready && bGateOpen && Local.X >= DuelCourt::GateCrossX &&
        Local.X < DuelCourt::GateCrossX + 645.f && FMath::Abs(Local.Y) < 242.f && FMath::Abs(Local.Z - 62.f) < 158.f)
    {
        ResetEncounter(true);
    }
    else if (bYukiCourt && Result == EDuelOutcome::Victory && bFinalRecord &&
        Local.X >= DuelCourt::YukiGardenX && FMath::Abs(Local.Y) < 250.f)
    {
        FinishTrial();
    }
}

void ADuelMode::ConfirmCastle()
{
    if (!PlayerFighter || FigureView != 0)
    {
        return;
    }
    const FVector At = PlayerFighter->GetActorLocation();
    if (Result == EDuelOutcome::Victory)
    {
        if (bYukiCourt && !bFinalRecord && DuelCastle::Close(At, DuelCastle::FinalRecord(), 285.f))
        {
            bFinalRecord = true;
            RestoreCastleLayout();
            Arena->SetDawn(true);
            TellInvestigation(TEXT("月代雪把末页归还。完整证词解除魔女化。"));
            Arena->TryBossLine(EDuelCharacter::Yuki, EDuelVoice::Loss, At);
            WriteCheckpoint();
        }
        if (bYukiCourt)
        {
            return;
        }
    }
    if (Result != EDuelOutcome::Ready && Result != EDuelOutcome::Victory)
    {
        return;
    }
    for (int32 Wing = 0; Wing < DuelCastle::WingCount; ++Wing)
    {
        const uint8 Bit = DuelCastle::WingBit(Wing);
        if ((CastleWins & Bit) && !(CastleSeals & Bit) && DuelCastle::Close(At, DuelCastle::Record(Wing), 278.f))
        {
            CastleSeals |= Bit;
            CastleShortcuts |= Bit;
            RestoreCastleLayout();
            SetCastleCourt(INDEX_NONE);
            CompletedDuelSeconds = 0;
            Result = EDuelOutcome::Ready;
            Arena->Sound(EDuelSound::VerdictBell, At);
            TellInvestigation(Wing == 0 ? SealNotes[Wing] : Wing == 1 ?
                TEXT("第二块判词。她没认出那张脸。学会白幕护体。") :
                TEXT("第三块判词。她走进火场救人。学会证人步。"));
            WriteCheckpoint();
            return;
        }
    }
    if (Result == EDuelOutcome::Victory)
    {
        return;
    }
    if (DuelCastle::Close(At, DuelCastle::Hub, 250.f))
    {
        PlayerFighter->RestAtCheckpoint();
        WriteCheckpoint();
        Arena->Sound(EDuelSound::Flask, At);
        TellInvestigation(TEXT("钟楼已记录进度。"));
        return;
    }
    if (DuelCastle::Close(At, DuelCastle::IndexStone, 230.f))
    {
        if (CastleSeals == DuelCastle::Whole)
        {
            bEastOpen = true;
            RestoreCastleLayout();
            TellInvestigation(TEXT("她逃过、看过，也回去救过人。三件事发生在同一夜。东门开了。"));
            Arena->Sound(EDuelSound::VerdictBell, DuelCastle::EastGate);
            WriteCheckpoint();
        }
        else
        {
            TellInvestigation(TEXT("还有钟声。到沉水层、押送层和锅炉房取回铭牌。"));
        }
        return;
    }
    if (const int32 Wing = NearbyFog(); Wing != INDEX_NONE)
    {
        const bool bFinal = Wing == DuelCastle::WingCount;
        if (bGateOpen && CastleDuel == Wing)
        {
            return;
        }
        if (!bFinal && (CastleWins & DuelCastle::WingBit(Wing) || !CanEnterCastleWing(Wing)))
        {
            return;
        }
        if (bGateOpen && CastleDuel != INDEX_NONE)
        {
            Arena->SetCastleFog(CastleDuel, true);
        }
        SetCastleCourt(Wing);
        const EDuelCharacter Own = PlayerFighter->SelectedCharacter();
        const EDuelCharacter Other = Own == EDuelCharacter::Ema ? EDuelCharacter::Hiro : EDuelCharacter::Ema;
        SetBoss(bFinal ? EDuelCharacter::Yuki : Wing == 1 ? Other : Own);
        BossFighter->ResetFighter();
        BossFighter->EndCombat();
        BossFighter->SetActorHiddenInGame(true);
        BossFighter->SetActorEnableCollision(false);
        if (bFinal)
        {
            PlayerFighter->RestAtCheckpoint();
        }
        bGateOpen = true;
        Arena->SetCastleFog(Wing, false);
        Arena->Sound(EDuelSound::VerdictBell, At);
        TellInvestigation(bFinal ? TEXT("月代雪：最后一句在我这里。") : Wing == 0 ?
            TEXT("艾玛和希罗的证词上，有一个被涂掉的名字。") : TEXT("判词来自同一页供词。"));
        return;
    }
    for (int32 Wing = 0; Wing < DuelCastle::WingCount; ++Wing)
    {
        const DuelCastle::FWing& Zone = DuelCastle::Wings[Wing];
        const uint8 Key = DuelCastle::WingBit((Wing + 1) % DuelCastle::WingCount);
        if (!(CastleKeys & Key) && DuelCastle::Close(At, Zone.Key))
        {
            CastleKeys |= Key;
            TellInvestigation(Wing == 0 ? TEXT("取到押送层钥匙。学会引路灯灵。") : Wing == 2 ?
                TEXT("取到沉水层钥匙。学会盐焰弧。") : KeyNotes[Wing]);
            RestoreCastleLayout();
            WriteCheckpoint();
            return;
        }
        for (int32 Item = 0; Item < 2; ++Item)
        {
            if (DuelCastle::Close(At, Zone.Clue[Item]))
            {
                const bool bKnewWell = KnowsSpell(EDuelArt::Blackwell);
                CastleClues |= static_cast<uint8>(1 << (Wing * 2 + Item));
                const bool bHiro = PlayerFighter->SelectedCharacter() == EDuelCharacter::Hiro;
                const TCHAR* Line = ClueNotes[Wing][Item];
                if (Wing == 0 && Item == 0 && bHiro)
                {
                    Line = TEXT("这道撞痕是我留下的。当时水已经没过台阶。");
                }
                else if (Wing == 0 && Item == 1 && !bHiro)
                {
                    Line = TEXT("是我的鞋。我想起来了，警铃响之前我就回去了。");
                }
                else if (Wing == 1 && Item == 1 && (CastleSeals & DuelCastle::WingBit(2)))
                {
                    Line = TEXT("木牌翻开了。背面写着两次押送，墨迹来自同一夜。");
                }
                else if (Wing == 2 && Item == 0 && (CastleSeals & DuelCastle::WingBit(0)))
                {
                    Line = TEXT("冷灰下面有字。断针停住时，值班人还没记下火警。");
                }
                TellInvestigation(Line);
                if (!bKnewWell && KnowsSpell(EDuelArt::Blackwell))
                {
                    TellInvestigation(TEXT("沉水层物证齐了。学会沉墨井。"));
                }
                WriteCheckpoint();
                return;
            }
        }
        if (!(CastleShortcuts & DuelCastle::WingBit(Wing)) && DuelCastle::Close(At, Zone.Shortcut, 176.f))
        {
            CastleShortcuts |= DuelCastle::WingBit(Wing);
            TellInvestigation(TEXT("近路通向中庭钟楼。"));
            RestoreCastleLayout();
            WriteCheckpoint();
            return;
        }
    }
}
