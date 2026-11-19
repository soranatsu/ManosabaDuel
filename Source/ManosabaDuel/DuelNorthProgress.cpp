#include "DuelNorthProgress.h"

#include "DuelGame.h"
#include "DuelNorthFinds.h"

namespace
{
    template<typename T> bool Valid(T Value)
    {
        return static_cast<uint8>(Value) < static_cast<uint8>(T::Count);
    }
    template<typename T> uint8 Bit(T Value)
    {
        return Valid(Value) ? static_cast<uint8>(1u << static_cast<uint8>(Value)) : 0;
    }
    template<typename T> bool Has(uint8 Mask, T Value)
    {
        return (Mask & Bit(Value)) != 0;
    }
    constexpr EDuelNorthFloor BattleFloors[] = {EDuelNorthFloor::Dungeon, EDuelNorthFloor::Mirror, EDuelNorthFloor::Final};
    constexpr EDuelNorthFloor ClueFloors[] = {EDuelNorthFloor::Dungeon, EDuelNorthFloor::Dungeon,
        EDuelNorthFloor::Chapel, EDuelNorthFloor::Chapel, EDuelNorthFloor::Mirror, EDuelNorthFloor::Mirror,
        EDuelNorthFloor::Final, EDuelNorthFloor::Prison, EDuelNorthFloor::Chapel, EDuelNorthFloor::Final, EDuelNorthFloor::Prison,
        EDuelNorthFloor::Chapel, EDuelNorthFloor::Chapel, EDuelNorthFloor::Chapel, EDuelNorthFloor::Chapel};
    static_assert(UE_ARRAY_COUNT(ClueFloors) == static_cast<int32>(EDuelNorthClue::Count));
    constexpr uint8 SideClueStart = static_cast<uint8>(EDuelNorthClue::RefugeLedger);
    constexpr uint8 SideClueCount = static_cast<uint8>(EDuelNorthClue::Count) - SideClueStart;
    static_assert(SideClueCount <= 8);
    constexpr uint8 SideClueMask = (1u << SideClueCount) - 1u;
    constexpr uint8 ReunionStart = static_cast<uint8>(EDuelNorthDialogue::MirrorExit);
    constexpr uint8 BossDefeatStart = static_cast<uint8>(EDuelNorthDialogue::DungeonBossDefeat);
    static_assert(static_cast<uint8>(EDuelNorthDialogue::Count) - BossDefeatStart == FDuelNorthProgress::BattleCount);
    constexpr uint8 ExtendedDialogueCount = BossDefeatStart - ReunionStart;
    static_assert(ExtendedDialogueCount <= 8);
    constexpr uint8 ExtendedDialogueMask = (1u << ExtendedDialogueCount) - 1u;
    const TCHAR* CharacterName(EDuelCharacter Character)
    {
        return Character == EDuelCharacter::Ema ? TEXT("艾玛") : TEXT("希罗");
    }
}

void FDuelNorthProgress::Reset(EDuelCharacter Character)
{
    *this = FDuelNorthProgress();
    bExpeditionLoot = true;
    PlayerCharacter = static_cast<uint8>(Character == EDuelCharacter::Ema ? EDuelCharacter::Ema : EDuelCharacter::Hiro);
}

EDuelCharacter FDuelNorthProgress::Player() const
{
    return PlayerCharacter == static_cast<uint8>(EDuelCharacter::Ema) ? EDuelCharacter::Ema : EDuelCharacter::Hiro;
}

EDuelCharacter FDuelNorthProgress::Companion() const
{
    return Player() == EDuelCharacter::Ema ? EDuelCharacter::Hiro : EDuelCharacter::Ema;
}

EDuelCharacter FDuelNorthProgress::BossForBattle(int32 Battle) const
{
    return Battle == 0 ? Companion() : (Battle == 1 ? Player() : EDuelCharacter::Yuki);
}

EDuelNorthDialogue FDuelNorthProgress::BossDefeatDialogue(int32 Battle)
{
    return Battle >= 0 && Battle < BattleCount ?
        static_cast<EDuelNorthDialogue>(BossDefeatStart + Battle) : EDuelNorthDialogue::Count;
}

bool FDuelNorthProgress::HasWon(int32 Battle) const
{
    return Battle >= 0 && Battle < BattleCount && (Wins & (1u << Battle)) != 0;
}

namespace
{
    uint8 LampBit(FName Id)
    {
        static const FName Stations[] = {TEXT("BeaconChart"),TEXT("LensWorkshop"),TEXT("BeaconLetter"),TEXT("BeaconSwitch")};
        const int32 Index = MakeArrayView(Stations).Find(Id);
        return Index == INDEX_NONE ? 0 : static_cast<uint8>(1u << Index);
    }
}
bool FDuelNorthProgress::HasBeacon(FName Id) const { return (BeaconBits & LampBit(Id)) != 0; }
bool FDuelNorthProgress::RecordBeacon(FName Id)
{
    const uint8 Bit = LampBit(Id);
    if (!Bit || (BeaconBits & Bit) || !IsDoorOpen(EDuelNorthDoor::FinalStair)) return false;
    if (Bit != 1 && !(BeaconBits & 1)) return false;
    if (Bit == 8 && (BeaconBits & 7) != 7) return false;
    BeaconBits |= Bit;
    return true;
}

bool FDuelNorthProgress::HasInvestigation(int32 Battle) const
{
    // These are the stable reward bits of the two evidence tables in existing checkpoints.
    if (Battle == 0 || Battle == 1) return HasWon(Battle) || (CacheBits & (1u << Battle)) != 0;
    return Battle == 2 && (HasWon(2) || HasBeacon(TEXT("BeaconSwitch")));
}

bool FDuelNorthProgress::CanEnterBattle(int32 Battle) const
{
    if (Battle < 0 || Battle >= BattleCount || HasWon(Battle) || !HasInvestigation(Battle)) return false;
    if (Battle == 0) return true;
    return HasWon(Battle - 1) && IsDoorOpen(Battle == 1 ? EDuelNorthDoor::Chapel : EDuelNorthDoor::FinalStair);
}

bool FDuelNorthProgress::WinBattle(int32 Battle)
{
    if (!CanEnterBattle(Battle)) return false;
    Wins |= static_cast<uint8>(1u << Battle);
    ExploreFloor(BattleFloors[Battle]);
    return true;
}

bool FDuelNorthProgress::HasRelic(EDuelNorthRelic Relic) const { return Has(Relics, Relic); }

bool FDuelNorthProgress::RelicAvailable(EDuelNorthRelic Relic) const
{
    return Valid(Relic) && HasWon(static_cast<int32>(Relic)) && !HasRelic(Relic);
}

bool FDuelNorthProgress::PickupRelic(EDuelNorthRelic Relic)
{
    if (!RelicAvailable(Relic)) return false;
    Relics |= Bit(Relic);
    return true;
}

bool FDuelNorthProgress::IsDoorOpen(EDuelNorthDoor Door) const { return Has(Doors, Door); }

bool FDuelNorthProgress::CanOpenDoor(EDuelNorthDoor Door) const
{
    if (!Valid(Door) || IsDoorOpen(Door)) return false;
    if (Door == EDuelNorthDoor::Chapel) return HasRelic(EDuelNorthRelic::RustKey);
    if (Door == EDuelNorthDoor::FinalStair) return HasRelic(EDuelNorthRelic::MirrorKey);
    return Relics == 7 && HasFinalConversation();
}

bool FDuelNorthProgress::OpenDoor(EDuelNorthDoor Door)
{
    if (!CanOpenDoor(Door)) return false;
    Doors |= Bit(Door);
    return true;
}

bool FDuelNorthProgress::HasClue(EDuelNorthClue Clue) const
{
    const uint8 Index = static_cast<uint8>(Clue);
    return Valid(Clue) && (Index < SideClueStart ? Has(Clues, Clue) : (SideClues & (1u << (Index - SideClueStart))) != 0);
}

bool FDuelNorthProgress::CanReadClue(EDuelNorthClue Clue) const
{
    if (!Valid(Clue) || !CanVisitFloor(ClueFloors[static_cast<uint8>(Clue)])) return false;
    if (Clue == EDuelNorthClue::OpeningDocket) return bOpeningCellRequired;
    if (Clue == EDuelNorthClue::Signature) return HasWon(1);
    if (Clue == EDuelNorthClue::FinalSeal || Clue == EDuelNorthClue::ReleaseOrder) return HasWon(2);
    return true;
}

bool FDuelNorthProgress::ReadClue(EDuelNorthClue Clue)
{
    if (!CanReadClue(Clue) || HasClue(Clue)) return false;
    const uint8 Index = static_cast<uint8>(Clue);
    if (Index < SideClueStart) Clues |= Bit(Clue);
    else SideClues |= 1u << (Index - SideClueStart);
    ExploreFloor(ClueFloors[Index]);
    return true;
}

bool FDuelNorthProgress::HasDialogue(EDuelNorthDialogue Dialogue) const
{
    const uint8 Index = static_cast<uint8>(Dialogue);
    if (!Valid(Dialogue)) return false;
    // Winning records the automatic dialogue in existing checkpoints, including saves made before it was added.
    if (Index >= BossDefeatStart) return HasWon(Index - BossDefeatStart);
    return Index < ReunionStart ? Has(Dialogues, Dialogue) : (Reunions & (1u << (Index - ReunionStart))) != 0;
}

bool FDuelNorthProgress::HasFinalConversation() const
{
    // Garden records the completed conversation in older checkpoints.
    return HasDialogue(EDuelNorthDialogue::FinalExit) || HasDialogue(EDuelNorthDialogue::Garden);
}

bool FDuelNorthProgress::DialogueAvailable(EDuelNorthDialogue Dialogue) const
{
    switch (Dialogue)
    {
    case EDuelNorthDialogue::NameWall:
    case EDuelNorthDialogue::LongTable:
    case EDuelNorthDialogue::DungeonDoor: return !HasWon(0);
    case EDuelNorthDialogue::RescuedCompanion: return HasWon(0) && !HasWon(1) && !HasDialogue(Dialogue);
    case EDuelNorthDialogue::ChapelBench:
    case EDuelNorthDialogue::MirrorDoor: return IsDoorOpen(EDuelNorthDoor::Chapel) && !HasWon(1);
    case EDuelNorthDialogue::MirrorExit: return HasWon(1) && !HasWon(2);
    case EDuelNorthDialogue::FinalExit: return HasWon(2) && !HasFinalConversation();
    case EDuelNorthDialogue::Garden: return CanViewEnding() && !HasDialogue(Dialogue);
    case EDuelNorthDialogue::CellEcho: return !HasWon(0) && !HasDialogue(Dialogue);
    case EDuelNorthDialogue::WaterGateEcho: return HasClue(EDuelNorthClue::WaterGateLog) && !HasDialogue(Dialogue);
    case EDuelNorthDialogue::ArchiveConversation: return HasClue(EDuelNorthClue::ChapelJudgments) && !HasDialogue(Dialogue);
    case EDuelNorthDialogue::FinalWarning: return HasBeacon(TEXT("BeaconSwitch")) && IsDoorOpen(EDuelNorthDoor::FinalStair) && !HasWon(2) && !HasDialogue(Dialogue);
    case EDuelNorthDialogue::DungeonBossDefeat:
    case EDuelNorthDialogue::MirrorBossDefeat:
    case EDuelNorthDialogue::FinalBossDefeat: return HasDialogue(Dialogue);
    default: return false;
    }
}

bool FDuelNorthProgress::CompleteDialogue(EDuelNorthDialogue Dialogue)
{
    if (!DialogueAvailable(Dialogue) || HasDialogue(Dialogue)) return false;
    const uint8 Index = static_cast<uint8>(Dialogue);
    if (Index < ReunionStart) Dialogues |= Bit(Dialogue);
    else Reunions |= 1u << (Index - ReunionStart);
    if (Dialogue == EDuelNorthDialogue::FinalExit || Dialogue == EDuelNorthDialogue::Garden) bCompanionFollowing = true;
    return true;
}

FDuelNorthDialogueScript FDuelNorthProgress::DialogueScript(EDuelNorthDialogue Dialogue) const
{
    FDuelNorthDialogueScript Script;
    if (!DialogueAvailable(Dialogue)) return Script;
    const bool bEma = Player() == EDuelCharacter::Ema;
    using E = EDuelNorthExpression;
    const auto CompanionLine = [&](const TCHAR* Text, E Expression = E::Smile) { Script.Lines.Add({Companion(), Text, Expression}); };
    const auto PlayerLine = [&](const TCHAR* Text, E Expression = E::Smile) { Script.Lines.Add({Player(), Text, Expression}); };
    const auto BossLine = [&](int32 Battle, const TCHAR* Text, E Expression)
    {
        Script.Presence = EDuelNorthPresence::Person;
        Script.Lines.Add({BossForBattle(Battle), Text, Expression, true});
    };
    switch (Dialogue)
    {
    case EDuelNorthDialogue::DungeonBossDefeat:
        BossLine(0, bEma ? TEXT("艾玛……我看见你倒在门边，怎么叫都没应。你真的站在这儿？") :
            TEXT("希罗酱？我刚才明明看见你关上门走了。你怎么还在这儿？"), E::Smile);
        PlayerLine(bEma ? TEXT("我挡开你的爪子，你才听见我。希罗酱，看，我的手还在你手里。") :
            TEXT("我挡住了骨翼，你才听清我叫你。艾玛，握一下我的手。"), E::Smile);
        BossLine(0, bEma ? TEXT("手能动了。那封信写着你的名字。我一拆开，就看见你倒下了。") :
            TEXT("握住了……那一幕是从拆开你寄的信开始的。希罗酱，能陪我去查一下吗？"), E::Smile);
        break;
    case EDuelNorthDialogue::MirrorBossDefeat:
        BossLine(1, bEma ? TEXT("我又看见希罗酱把门关上了。可她在外头念信。我听见她说『别压扁』。") :
            TEXT("艾玛在门外念信。我听见她说枕头底下的纸响了。那是我给她包的面包。"), E::Smile);
        PlayerLine(bEma ? TEXT("那是她昨晚给我留的面包。幻象打散了，听，希罗酱还在门外叫我。") :
            TEXT("她就站在门外。打散镜子里的影子，我才听清她的声音。"), E::Smile);
        BossLine(1, bEma ? TEXT("手能松开了。碎镜后面有把钥匙。带上它，我想出去见希罗酱。") :
            TEXT("手上的黑纹退了。拿上碎镜后的钥匙，艾玛在等你。"), E::Smile);
        break;
    case EDuelNorthDialogue::FinalBossDefeat:
        BossLine(2, TEXT("施咒断了……她们的变化还没定形，黑纹会退下去。楼下的女孩，你们去看看她们吧。"), E::Concern);
        PlayerLine(bEma ? TEXT("希罗酱，小雪停手了。快去看看，有人站不起来咱们就扶一把。") :
            TEXT("艾玛，咒解了。楼下还有人关着，走，去把人接出来。"), E::Smile);
        BossLine(2, TEXT("审判是我设下的。我把失去族人的恨，报复在了她们身上。放行木牌在窗下，拿去开门吧。"), E::Neutral);
        break;
    case EDuelNorthDialogue::CellEcho:
        Script.Presence = EDuelNorthPresence::Voice;
        CompanionLine(bEma ? TEXT("艾玛，听见就敲一下。看守刚走。") :
            TEXT("希罗酱？贴着铁管听，能听到我吗？"), E::Smile);
        PlayerLine(bEma ? TEXT("希罗酱！你在哪里？") : TEXT("听见了，艾玛。你在哪儿？"), E::Smile);
        CompanionLine(bEma ? TEXT("我在楼下。这根铁管通着两个房间，声音传得上去。") :
            TEXT("我在楼下，门从外头锁上了。我也看不见门牌。"));
        PlayerLine(bEma ? TEXT("我去问你的饭送到哪儿。你在这儿等我。") :
            TEXT("先坐下，省点力气。我会找到你的门。"), E::Smile);
        CompanionLine(bEma ? TEXT("好。到了门口先叫我一声。") :
            TEXT("那你到了就叫我，我一听就知道是你。"), E::Smile);
        break;
    case EDuelNorthDialogue::WaterGateEcho:
        Script.Presence = HasWon(0) ? EDuelNorthPresence::Person : EDuelNorthPresence::Voice;
        CompanionLine(bEma ? TEXT("艾玛，是你碰了门外那只碗吗？") :
            TEXT("希罗酱？我听见碗在地上滚。"), E::Smile);
        PlayerLine(bEma ? TEXT("是我，差点踢翻了。饭还在里面。") :
            TEXT("是我踢到了。碗边还粘着一点米。"));
        CompanionLine(bEma ? TEXT("看守天天把碗往地上一放。我光听响动，以为他又回来了。") :
            TEXT("我等了好久。每次响一声，都以为门要开了。"), E::Smile);
        PlayerLine(bEma ? TEXT("我来就先叫你。希罗酱。") :
            TEXT("艾玛。我来了就会喊你。"), E::Smile);
        CompanionLine(bEma ? TEXT("嗯。再叫一声。") : TEXT("再叫一声，好不好？"), E::Smile);
        PlayerLine(bEma ? TEXT("希罗酱。") : TEXT("艾玛。"), E::Smile);
        break;
    case EDuelNorthDialogue::ArchiveConversation:
        Script.Presence = EDuelNorthPresence::Person;
        PlayerLine(bEma ? TEXT("希罗酱，你看，她说自己见过同伴动手。") :
            TEXT("这份笔录说她亲眼看到了。同一页又说，同伴整晚关在别处。"));
        CompanionLine(bEma ? TEXT("看守查过了。被指的人当时锁在别的房间。") :
            TEXT("她当时真信了吧？我刚才在楼下也是，眼前那一下太像真的了。"), E::Smile);
        PlayerLine(bEma ? TEXT("你在楼下看见我倒下时，也像真的一样？") :
            TEXT("先查她拆过什么，再看审判的人是怎么问的。"), E::Smile);
        CompanionLine(bEma ? TEXT("我还听得见你倒下的声音。那封信一定要查。") :
            TEXT("柜子里好像有几条收信记录，帮我一起够一下？"));
        PlayerLine(bEma ? TEXT("我们去对一对寄信和收信的两份账。") :
            TEXT("我来取。你帮我把这页折起来。"), E::Smile);
        CompanionLine(bEma ? TEXT("我看右边的架子。翻到了就叫你。") :
            TEXT("折好了。你取下来的时候小心，我托着。"), E::Smile);
        break;
    case EDuelNorthDialogue::FinalWarning:
        Script.Presence = EDuelNorthPresence::Voice;
        Script.Lines.Add({EDuelCharacter::Yuki, TEXT("日记你们都看到了。人类杀光了我的族人，我是最后的大魔女。"), E::Neutral});
        PlayerLine(bEma ? TEXT("楼下的女孩没有杀害你的族人。她们一直在求救，你却拿她们报仇。") :
            TEXT("你把她们关起来，逼她们变成魔女。她们没有伤害过你的族人。"), E::Angry);
        Script.Lines.Add({EDuelCharacter::Yuki, TEXT("我要让全人类都变成魔女。他们把魔女当怪物猎杀，就让他们也尝尝这种滋味。"), E::Resolve});
        PlayerLine(bEma ? TEXT("我们会把信还给她们，把人带出去。小雪，停下你的咒。") :
            TEXT("日记和戒指都在我们手里。你的咒能被打断，楼下的人还有救。"), E::Smile);
        Script.Lines.Add({EDuelCharacter::Yuki, TEXT("岛上的女孩只是开始。下一批信会送到岛外，我会让每一个人类都变成魔女。你们拦不住我。"), E::Angry});
        break;
    case EDuelNorthDialogue::NameWall:
        CompanionLine(bEma ? TEXT("艾玛，醒了就到走廊上来。") : TEXT("希罗酱，你终于醒了。"), E::Smile);
        if (bEma) CompanionLine(TEXT("顺着墙走到长桌。我昨晚把路问清楚了。"));
        break;
    case EDuelNorthDialogue::LongTable:
        CompanionLine(bEma ? TEXT("艾玛，地牢的看守在叫我。") : TEXT("希罗酱，楼下那扇门又响了。"), E::Smile);
        if (bEma) CompanionLine(TEXT("我先下去。等你来的时候，在门外叫我一声。"));
        break;
    case EDuelNorthDialogue::DungeonDoor:
        Script.Presence = EDuelNorthPresence::Voice;
        CompanionLine(bEma ? TEXT("艾玛？先别把手伸进来。") : TEXT("希罗酱？你离门远一点。"), E::Smile);
        PlayerLine(bEma ? TEXT("是我！你能站起来吗？") : TEXT("是我。艾玛，试着把手放平。"), E::Smile);
        CompanionLine(bEma ? TEXT("我想给你开门，手一下就扣进了门板。现在松不开。") :
            TEXT("我想把碗推给你，背后却长出东西来了。它一直在撞门。"), E::Concern);
        PlayerLine(bEma ? TEXT("希罗酱，我会拦住你的手。听见我叫你就应一声。") :
            TEXT("艾玛，我先挡住它。看清楚来的是谁。"), E::Smile);
        break;
    case EDuelNorthDialogue::RescuedCompanion:
        Script.Presence = EDuelNorthPresence::Person;
        CompanionLine(bEma ? TEXT("艾玛，我挥手的时候碰疼你了吗？") :
            TEXT("希罗酱，我的翅膀刚才撞到你了。能让我看看吗？"), E::Smile);
        PlayerLine(bEma ? TEXT("手指有点麻。希罗酱，你能伸手了，帮我揉一下吧。") :
            TEXT("擦了一下。艾玛，你还记得那封信是什么时候送来的？"));
        CompanionLine(bEma ? TEXT("好。艾玛，我收到那封信是晚上。送饭的看守把它放在碗边。") :
            TEXT("送晚饭的时候。看守拿进来的，我看见署名就拆了。"));
        PlayerLine(bEma ? TEXT("我昨晚交的信。走，我们去档案室查是不是同一封。") :
            TEXT("送信的人和时间都能查。我们去档案室找收发记录。"), E::Smile);
        CompanionLine(bEma ? TEXT("门边有钥匙。我来开路，你的手先歇一下。") :
            TEXT("门边那把钥匙能开楼上的门。我跟你去。"), E::Smile);
        break;
    case EDuelNorthDialogue::ChapelBench:
        CompanionLine(bEma ? TEXT("艾玛，坐这里能看见海。") : TEXT("希罗酱，坐这里能看见海。"), E::Smile);
        if (bEma) CompanionLine(TEXT("歇一会儿吗？等大家都出来了，我们再回来坐。"));
        break;
    case EDuelNorthDialogue::MirrorDoor:
        CompanionLine(bEma ? TEXT("艾玛，往门外听。我会在这儿叫你。") : TEXT("希罗酱，看看镜子，再看看我。"), E::Smile);
        if (bEma) CompanionLine(TEXT("等你出来，咱们一起走。"));
        break;
    case EDuelNorthDialogue::MirrorExit:
        Script.Presence = EDuelNorthPresence::Person;
        CompanionLine(bEma ? TEXT("艾玛！玻璃碎的时候，你碰到哪儿没有？") :
            TEXT("希罗酱！你刚才终于回应我了。"), E::Smile);
        PlayerLine(bEma ? TEXT("胳膊擦了一下。我听见你念信，才知道你还在。") :
            TEXT("你念到那半块面包，我就认出你了。手已经能松开。"));
        CompanionLine(bEma ? TEXT("我一直在。来，踩这块干净的地。") :
            TEXT("好。那我抱一下你，可以吗？"), E::Smile);
        PlayerLine(bEma ? TEXT("抱一下再走吧。还有送信的人，咱们得去查。") :
            TEXT("嗯。抱一下。再去看那封信是谁经手的。"));
        CompanionLine(bEma ? TEXT("好，我扶着你。那边全是碎玻璃。") :
            TEXT("好呀。碎玻璃我来拨开。"), E::Smile);
        break;
    case EDuelNorthDialogue::FinalExit:
        Script.Presence = EDuelNorthPresence::Person;
        CompanionLine(bEma ? TEXT("艾玛，楼下还有人等着。带上名单，咱们一间间去接。") :
            TEXT("希罗酱，我拿好名单了。下楼时咱们挨间叫她们。"), E::Smile);
        PlayerLine(bEma ? TEXT("好。先开海边的门，再回去接她们。走不动的，我们扶。") :
            TEXT("好。先开海边的门。接人的时候按名单数，大家到齐再走。"), E::Smile);
        CompanionLine(bEma ? TEXT("放行木牌在窗下。拿上它，我跟你一起去。") :
            TEXT("好呀。我拿着名单。希罗酱，别忘了窗下的放行木牌。"), E::Smile);
        break;
    case EDuelNorthDialogue::Garden:
        Script.Presence = EDuelNorthPresence::Person;
        if (bEma)
        {
            CompanionLine(TEXT("艾玛，站过来一点。风大。"));
            PlayerLine(TEXT("我就站你旁边。大家都到齐了吗？"));
            CompanionLine(TEXT("齐了。刚数过。"));
            PlayerLine(TEXT("那你歇一下吧。手给我，我摸摸还凉不凉。"));
            CompanionLine(TEXT("暖多了。艾玛，我喜欢你。刚才在楼下就想说。"));
            PlayerLine(TEXT("我也喜欢你呀，希罗酱。那半块面包还欠你一半。"), E::Smile);
            CompanionLine(TEXT("到了岸上买新的。我们一起去。"));
        }
        else
        {
            CompanionLine(TEXT("希罗酱，你听，是海呀。我还以为是关门的声音。"));
            PlayerLine(TEXT("是浪。大家在前面等船。"));
            CompanionLine(TEXT("地牢里我握着你的手，还以为是你冷。都出来了，我还在抖。"));
            PlayerLine(TEXT("手给我。我们坐一会儿。"));
            CompanionLine(TEXT("好多了。希罗酱，我喜欢你。刚才一路都在想怎么说。"));
            PlayerLine(TEXT("我也喜欢你。艾玛，牵着手吧。"), E::Smile);
            CompanionLine(TEXT("到了岸上，陪我买面包好不好？我想吃热的。"));
            PlayerLine(TEXT("好。一起吃。"));
        }
        break;
    default: break;
    }
    return Script;
}

FDuelNorthDialogueScript FDuelNorthProgress::InvestigationScript(FName EvidenceId) const
{
    FDuelNorthDialogueScript Script;
    Script.Presence = EDuelNorthPresence::Person;
    const bool bEma = Player() == EDuelCharacter::Ema;
    using E = EDuelNorthExpression;
    const auto Think = [&](const TCHAR* Ema, const TCHAR* Hiro, E Expression = E::Smile)
    {
        Script.Lines.Add({Player(), bEma ? Ema : Hiro, Expression});
    };
    const auto Partner = [&](const TCHAR* Hiro, const TCHAR* Ema, E Expression = E::Smile)
    {
        if (HasWon(0)) Script.Lines.Add({Companion(), bEma ? Hiro : Ema, Expression});
    };

    if (EvidenceId == TEXT("OpeningDocket") && HasClue(EDuelNorthClue::OpeningDocket))
    {
        Think(TEXT("这里是魔女岛的监狱。看守说，碰到心里最不敢碰的事，人就会变成魔女。"),
            TEXT("这里是魔女岛的监狱。看守说，碰到心里的禁忌，人就会魔女化。"));
        Think(TEXT("希罗酱的手张不开了。她昨晚还来敲我的门。我得去找她。"),
            TEXT("艾玛的手指已经僵了。看守会给她单独送饭，顺着这份饭就能找到她。"), E::Concern);
        Think(TEXT("先抬插销。希罗酱，等我一下。"),
            TEXT("插销在下面。艾玛，我去找你。"), E::Smile);
    }
    else if (EvidenceId == TEXT("RefugeLedger") && HasClue(EDuelNorthClue::RefugeLedger))
    {
        Think(TEXT("十三个人，十四份饭。多出来的那份用碗沿缺一角的旧碗。"),
            TEXT("单独送的那份饭用缺口碗。艾玛说的应该就是这份。"));
        Think(TEXT("去配膳台看那只碗。碗底夹着送饭房间的号码牌。"),
            TEXT("账上写了号码和房间。去配膳台看缺口碗的底。"), E::Smile);
    }
    else if (EvidenceId == TEXT("FindRations"))
    {
        if (HasInvestigation(0))
        {
            Think(TEXT("碗底是 03。希罗酱在地下禁闭室！"),
                TEXT("03 号是地下禁闭室。艾玛在那里。"), E::Smile);
            Think(TEXT("沿侧梯下去。门从外面开，我得赶紧去。"),
                TEXT("从侧梯下去，找挂着 03 的门。希望她的手还能动。"), E::Smile);
        }
        else
        {
            Think(TEXT("这儿有三只碗。先去查哪一只每天单独送。"),
                TEXT("先去配给底账查哪只碗送到楼下。"));
        }
    }
    else if (EvidenceId == TEXT("ArchiveTransferLedger") && HasClue(EDuelNorthClue::ArchiveTransferLedger))
    {
        Think(TEXT("这是我写给你的那一封。希罗酱，退信袋里也有你的笔迹。"),
            TEXT("我的原信还在退信袋里。旁边这封是艾玛亲手写的。"));
        Partner(TEXT("你的信是昨晚交出去的吧？这两封都被留下了。"),
            TEXT("你看，纸上还写着那半块面包。希罗酱，这是我写的。"), E::Smile);
        if (HasClue(EDuelNorthClue::ArchiveSignatureRegister))
        {
            Think(TEXT("把两封拿到校对台去。我们对一下真正送出去的信。"),
                TEXT("去校对台。把袋里的两封和收到的信放在一起。"), E::Smile);
        }
        else
        {
            Think(TEXT("那我们收到的是哪一封？去右边的架子找送信记录。"),
                TEXT("去看右边的送信记录。送到手里的那两封也得核对。"));
        }
    }
    else if (EvidenceId == TEXT("ArchiveSignatureRegister") && HasClue(EDuelNorthClue::ArchiveSignatureRegister))
    {
        Think(TEXT("一封给我，一封给希罗酱。压下来的红蜡印，右边那一瓣都缺角。"),
            TEXT("这是送信时压下的两个印。收信人不同，红蜡印一样。"));
        Partner(TEXT("我记得那个缺角。拆开时硌了我一下。"),
            TEXT("希罗酱，我封信用的是米糊。这个红蜡印我没用过。"), E::Smile);
        if (HasClue(EDuelNorthClue::ArchiveTransferLedger))
        {
            Think(TEXT("去校对台，再和袋里那两封对一对。"),
                TEXT("去校对台，把退信和送达的两封摆在一起。"), E::Smile);
        }
        else
        {
            Think(TEXT("我想看看我写的信去哪儿了。对面的书架上有退信登记。"),
                TEXT("先去左边查退信登记，看我们写的信有没有送出去。"));
        }
    }
    else if (EvidenceId == TEXT("FindCounterseal"))
    {
        if (HasInvestigation(1))
        {
            Think(TEXT("真信还在退信袋里。我们收到的那两封都被换过了。"),
                TEXT("原信一直在袋里。送到我们手上的两封是假信。"), E::Smile);
            Think(TEXT("我昨晚也拆过一封。我看见你把门关上，说再也不想见我。"),
                TEXT("昨晚我也拆了一封。我看见你倒在门边，叫你怎么都不应。"), E::Smile);
            Partner(TEXT("我在这儿。把真信带上。你再看到那一幕，就听我念信。"),
                TEXT("我就在这儿。咱们把真的两封拿上。我念给你听，好吗？"), E::Smile);
            Think(TEXT("我的手也开始发僵了。记录说，三楼镜厅的镜子还在维持幻象；打碎镜子，才能摆脱它。"),
                TEXT("我的手又僵了。三楼镜厅的镜子还在给我下咒，得打碎镜子，才不会再被幻象控制。"), E::Concern);
            Partner(TEXT("后门还通着送信人的办公区。先帮你解开这层咒，再去找是谁换了信。"),
                TEXT("我陪你去。真信我拿着，你再看见我出事，就听我念。"), E::Smile);
            Think(TEXT("希罗酱，陪我去三楼的镜厅。我要清醒地带你离开这里。"),
                TEXT("艾玛，我去三楼的镜厅解开这层咒，再带你出去。你在门边念信，我会一直听。"), E::Smile);
        }
        else
        {
            Think(TEXT("桌上有两封送到牢里的信。去把退信登记和送信记录找齐。"),
                TEXT("先去两边书架查寄出的信和送到手的信。"));
        }
    }
    else if (EvidenceId == TEXT("OwnVerdict") && HasClue(EDuelNorthClue::OwnVerdict) && !HasWon(1))
    {
        Think(TEXT("这张单子标出了我的镜子。希罗酱，我又看见那扇门了。"),
            TEXT("给我下咒的就是这面镜子。艾玛……我又看见你倒在门边。"), E::Startled);
        if (HasInvestigation(1))
        {
            Partner(TEXT("艾玛，我会念那封真信。你还记得我怕把面包压扁吗？"),
                TEXT("希罗酱，听我念。昨晚枕头底下那张纸在响，我听到了。"), E::Smile);
            Think(TEXT("你在我旁边。等我把镜子里的那个打散。"),
                TEXT("艾玛，站在门边继续念。我去打散镜子里的我。"), E::Smile);
        }
        else
        {
            Think(TEXT("我要先弄清那两封信。回档案室再查一遍。"),
                TEXT("先对过两封信，再进镜厅。"), E::Smile);
        }
    }
    else if (EvidenceId == TEXT("Signature") && HasClue(EDuelNorthClue::Signature) && !HasBeacon(TEXT("BeaconChart")))
    {
        Think(TEXT("这张单子说，送信前要先过办公区。钥匙也在这儿。"),
            TEXT("后门钥匙在这里。交接单让我们先去楼梯平台的值班室。"));
        Partner(TEXT("整层牢房的来信都要经他们的手。去问问那晚谁当值。"),
            TEXT("还有别人的信说不定也被扣了。希罗酱，我跟你去。"), E::Smile);
    }
    else if (EvidenceId == TEXT("BeaconChart") && HasBeacon(EvidenceId))
    {
        Think(TEXT("我们俩的信都在这一天被扣住了。前面还有别的女孩。"),
            TEXT("每次扣信，当班的人都转交信件检查室。我们的两封也是。"));
        Partner(TEXT("这个日期跟我们拆信的日子一样。去检查台看他们记了什么。"),
            TEXT("这里还记着谁被送去候审。希罗酱，去看看检查台。"), E::Smile);
    }
    else if (EvidenceId == TEXT("LensWorkshop") && HasBeacon(EvidenceId))
    {
        Think(TEXT("牢里只有她一个人，她却喊别人别推她。跟咱们拆信时一样。"),
            TEXT("信让女孩看见幻象，门外的人还在继续施法。被打断后，黑纹就淡了。"), E::Smile);
        Partner(TEXT("门外的人一停手，她的黑纹就淡了。咒还没解，有人在不停地催动它。"),
            TEXT("牢里没人推她。她也跟咱们一样，看见了幻象。只要打断施咒，她就有救。"), E::Smile);
        if (HasBeacon(TEXT("BeaconLetter")))
        {
            Think(TEXT("和日记对上了。她要把岛上的办法用到外面。去物证室找女孩们的信。"),
                TEXT("跟小雪日记里写的一样。打断她的咒，就能阻止变化。先去物证室对红蜡印。"), E::Smile);
        }
        else
        {
            Think(TEXT("这里没写是谁寄的。去看守住处找寄信人的日记。"),
                TEXT("得查谁让他们把信送进去。值班簿说日记留在看守住处。"), E::Smile);
        }
    }
    else if (EvidenceId == TEXT("BeaconLetter") && HasBeacon(EvidenceId))
    {
        Think(TEXT("小雪本来就是魔女。人类杀死了她的族人，她要把全人类都变成魔女来报复。"),
            TEXT("小雪是最后的大魔女。她的族人被人类杀死，所以她要让全人类变成魔女。"), bEma ? E::Smile : E::Angry);
        Partner(TEXT("这些女孩只是她在岛上的试验。她还要把这种信送出去。"),
            TEXT("可楼下的女孩没有伤害过她。她们连求救信都送不出去。"), E::Smile);
        Think(TEXT("变化还没定形。打断小雪的咒，就能救她们，也能拦住送往岛外的信。"),
            TEXT("她写了，未定形的变化还要靠她维持。打断她施咒，女孩们就有救。"), E::Smile);
        if (HasBeacon(TEXT("LensWorkshop")))
        {
            Think(TEXT("其他人的信还在物证室。我们先去把信拿出来。"),
                TEXT("去物证室。用小雪的戒指压一次红蜡，和假信上的缺角对一对。"), E::Smile);
        }
        else
        {
            Think(TEXT("咱们再去检查台。看看她的咒被打断时，女孩发生了什么。"),
                TEXT("先去检查台，把日记写的事和观察记录对上。"), E::Smile);
        }
    }
    else if (EvidenceId == TEXT("BeaconSwitch"))
    {
        if (HasBeacon(EvidenceId))
        {
            Think(TEXT("戒指压出来的红蜡印和假信上的一样。右边那一瓣都缺了角。"),
                TEXT("两封假信用的是小雪这枚戒指。红蜡封袋上也有那个缺角。"), E::Smile);
            Partner(TEXT("求救信都在这儿。先收好，等她们出来了再还给她们。"),
                TEXT("一封都别落下。等她们能出来了，咱们亲手还给她们。"), E::Smile);
            Think(TEXT("小雪还在楼上。打断她的咒，把女孩们接出来。那些要送去岛外的信也得拦下。"),
                TEXT("信先收好。去终审厅打断小雪施咒，救出女孩，也阻止她把咒带到岛外。"), E::Smile);
        }
        else
        {
            Think(TEXT("这里有好几袋信。得先看看值班记录和她留下的日记。"),
                TEXT("把值班记录、观察记录和日记找齐，再来给红蜡对印。"));
        }
    }
    return Script;
}

FString FDuelNorthProgress::ClueText(EDuelNorthClue Clue) const
{
    if (!CanReadClue(Clue)) return FString();
    switch (Clue)
    {
    case EDuelNorthClue::CellVerdict:
        return HasInvestigation(0) ?
            FString::Printf(TEXT("03 号地下禁闭室。收押女孩 %s，晚饭单送。交班时先检查她的手还能不能握住碗。"), CharacterName(Companion())) :
            FString::Printf(TEXT("临时收押女孩 %s，晚饭单送。门牌看配给底账。"), CharacterName(Companion()));
    case EDuelNorthClue::WaterGateLog:
        return TEXT("禁闭室交班簿。房门从外面锁。饭碗放到脚边的小窗，门里有人问话时要先报来人的名字。");
    case EDuelNorthClue::ChapelJudgments:
        return TEXT("旧审判笔录。一个女孩说她看见同伴从背后推了她，手指随即发僵。看守核点过牢房，被指的女孩整晚关在别处。");
    case EDuelNorthClue::ArchiveTransferLedger:
        return TEXT("退信袋贴在《退信登记》里。艾玛和希罗亲笔写给彼此的两封信都在袋中，登记写着扣下未送。\n希罗写「面包我放回你枕头底下了。别压扁。」艾玛写「我听见枕头底下的纸响啦。面包我想和你分。」");
    case EDuelNorthClue::ArchiveSignatureRegister:
        return TEXT("《送信记录》留着送信时压下的红蜡印样。一封给艾玛，一封给希罗。两枚印都是三瓣花，右边一瓣缺角。送达的两封信都在校对台。\n附页：假信的咒连着三楼的镜厅。厅里的镜子还在，幻象就会反复出现；打碎镜子可解除这层束缚。后门通向送信人的办公区，钥匙留在镜后。");
    case EDuelNorthClue::SeaMark:
        return TEXT("石凳边刻着「散庭后，到这里等我。我们一起回家。」下面并排刻着两个名字。");
    case EDuelNorthClue::OwnVerdict:
        return FString::Printf(TEXT("三楼镜厅的候审单。写着 %s 名字的镜子就在厅内。她拆信后反复看见同伴出事，手指发僵。\n这面镜子一直在向她施咒，打碎镜子就能让幻象停下。后门钥匙藏在镜后。"),
            CharacterName(Player()));
    case EDuelNorthClue::Signature:
        return TEXT("碎镜后是去监狱办公区的钥匙。交接单写着「送进牢房的信先交值班室。办公区入口在长阶的第一个平台。」");
    case EDuelNorthClue::FinalSeal:
        return TEXT("小雪留给看守的判决已撕成两半。上头写着几个女孩的名字，罪名空着，旁边注着「审完再填」。");
    case EDuelNorthClue::RefugeLedger:
        return TEXT("候审十三人领晚饭。另送一份，用碗沿缺一角的旧碗。碗底夹着送饭房间的号码牌。\n01 是厨房，02 是医务室，03 是地下禁闭室。");
    case EDuelNorthClue::SealPrototype:
        return TEXT("医务室观察单。她听清同伴在叫她、认出对方以后，僵硬的手指才慢慢松开。留她坐了一会儿，给了杯温水。");
    case EDuelNorthClue::ReleaseOrder:
        return TEXT("放行名单摆在桌上。看守在页边写着「开门后按原名逐一放行。手脚还发僵的，扶她们一起走。」");
    case EDuelNorthClue::OpeningDocket:
        return Player() == EDuelCharacter::Ema
            ? TEXT("艾玛，铁门的插销可以从里面往上抬。她们把我带下楼了，我没看清门牌。看守说晚饭另送一份。手指越来越硬了。你下楼时慢一点。——希罗")
            : TEXT("希罗酱，门下面有个插销，你往上抬就能出来。看守带我下了楼，说要给我单独送饭。我写字的时候手有点张不开。——艾玛");
    case EDuelNorthClue::ArchiveBindingNote:
        return TEXT("「今天领饭时我没听见自己的名字，又被排到了最后。明天我要自己把名字写大一点给她看。」末尾有个重新写过的名字。");
    case EDuelNorthClue::ArchiveNightIndex:
        return TEXT("「锈铁钥匙留在地下禁闭室门边的挂钩上。手指发僵的姑娘醒了会口渴，夜班过去时带一杯水。」");
    default: return FString();
    }
}

bool FDuelNorthProgress::OwnsWeapon(EDuelWeapon Weapon) const
{
    const bool bStarter = Weapon == EDuelWeapon::Rapier || Weapon == EDuelWeapon::Buckler ||
        Weapon == EDuelWeapon::Staff || Weapon == EDuelWeapon::Empty || Weapon == DuelCourt::PersonalWeapon(Player());
    return DuelCourt::CanEquip(Weapon, Player(), EDuelHand::Left) &&
        (!bExpeditionLoot || bStarter || DuelNorthFinds::HasWeapon(CacheBits, Weapon));
}

bool FDuelNorthProgress::KnowsSpell(EDuelArt Spell) const
{
    if (Spell == EDuelArt::IceLance || Spell == EDuelArt::WitchRain) return true;
    const bool bFound = DuelNorthFinds::HasSpell(CacheBits, Spell);
    if (bExpeditionLoot || bFound) return bFound;
    switch (Spell)
    {
    case EDuelArt::SableBlade:
    case EDuelArt::StarTrack:
    case EDuelArt::ReturningFrost:
    case EDuelArt::PrismSeal:
    case EDuelArt::EmberRecoil:
    case EDuelArt::GildedCross:
    case EDuelArt::MoonCrescent: return true;
    case EDuelArt::LanternMote: return HasClue(EDuelNorthClue::CellVerdict) || HasClue(EDuelNorthClue::RefugeLedger);
    case EDuelArt::SaltArc: return HasClue(EDuelNorthClue::WaterGateLog);
    case EDuelArt::Blackwell: return HasClue(EDuelNorthClue::CellVerdict) && HasClue(EDuelNorthClue::WaterGateLog);
    case EDuelArt::WhiteMantle: return HasClue(EDuelNorthClue::ChapelJudgments) || HasClue(EDuelNorthClue::SealPrototype);
    case EDuelArt::WitnessStep: return HasWon(1) && HasClue(EDuelNorthClue::Signature);
    default: return false;
    }
}

bool FDuelNorthProgress::CanVisitFloor(EDuelNorthFloor Floor) const
{
    switch (Floor)
    {
    case EDuelNorthFloor::Dungeon:
    case EDuelNorthFloor::Prison: return true;
    case EDuelNorthFloor::Chapel:
    case EDuelNorthFloor::Mirror: return IsDoorOpen(EDuelNorthDoor::Chapel);
    case EDuelNorthFloor::Beacon:
    case EDuelNorthFloor::Final: return IsDoorOpen(EDuelNorthDoor::FinalStair);
    case EDuelNorthFloor::Garden: return IsDoorOpen(EDuelNorthDoor::TowerExit);
    default: return false;
    }
}

bool FDuelNorthProgress::HasExplored(EDuelNorthFloor Floor) const { return Has(ExploredFloors, Floor); }

bool FDuelNorthProgress::ExploreFloor(EDuelNorthFloor Floor)
{
    if (!CanVisitFloor(Floor) || HasExplored(Floor)) return false;
    ExploredFloors |= Bit(Floor);
    return true;
}

bool FDuelNorthProgress::CanActivateCheckpoint(EDuelNorthFloor Floor) const
{
    return Valid(Floor) && Floor != EDuelNorthFloor::Garden && Floor != EDuelNorthFloor::Beacon && CanVisitFloor(Floor) &&
        (Floor != EDuelNorthFloor::Prison || IsOpeningCellOpen());
}

bool FDuelNorthProgress::ActivateCheckpoint(EDuelNorthFloor Floor)
{
    if (!CanActivateCheckpoint(Floor)) return false;
    const bool bFirstPrisonRest = Floor == EDuelNorthFloor::Prison && bOpeningCellRequired && !bPrisonRestActivated;
    const bool bChanged = Checkpoint != static_cast<uint8>(Floor) || !Has(VisitedCheckpoints, Floor) ||
        !Has(ActivatedCheckpoints, Floor) || bFirstPrisonRest;
    bPrisonRestActivated |= bFirstPrisonRest;
    Checkpoint = static_cast<uint8>(Floor);
    VisitedCheckpoints |= Bit(Floor);
    ActivatedCheckpoints |= Bit(Floor);
    ExploreFloor(Floor);
    return bChanged;
}

bool FDuelNorthProgress::IsCheckpointActivated(EDuelNorthFloor Floor) const
{
    if (!CanActivateCheckpoint(Floor)) return false;
    // Before 3.4.6 only Prison was seeded automatically; all other visited bits came from resting.
    const uint8 LegacyProof = (VisitedCheckpoints & ~Bit(EDuelNorthFloor::Prison)) |
        (bPrisonRestActivated ? Bit(EDuelNorthFloor::Prison) : 0);
    return Has(ActivatedCheckpoints | LegacyProof, Floor);
}

bool FDuelNorthProgress::OpenStartingCell()
{
    if (IsOpeningCellOpen() || !HasClue(EDuelNorthClue::OpeningDocket)) return false;
    bOpeningCellUnlocked = true;
    return true;
}

bool FDuelNorthProgress::UsesStartingCellCheckpoint() const
{
    return bOpeningCellRequired && !bPrisonRestActivated && Checkpoint == static_cast<uint8>(EDuelNorthFloor::Prison);
}

bool FDuelNorthProgress::IsShortcutOpen(EDuelNorthShortcut Shortcut) const { return Has(Shortcuts, Shortcut); }

bool FDuelNorthProgress::OpenShortcut(EDuelNorthShortcut Shortcut)
{
    if (!Valid(Shortcut) || IsShortcutOpen(Shortcut)) return false;
    if (Shortcut == EDuelNorthShortcut::DungeonReturn ? !HasWon(0) : !IsDoorOpen(EDuelNorthDoor::Chapel)) return false;
    Shortcuts |= Bit(Shortcut);
    return true;
}

bool FDuelNorthProgress::CanViewEnding() const
{
    return IsDoorOpen(EDuelNorthDoor::TowerExit) && HasFinalConversation();
}

bool FDuelNorthProgress::ViewEnding()
{
    if (!CanViewEnding() || bEndingSeen) return false;
    bEndingSeen = true;
    return true;
}

bool FDuelNorthProgress::CompleteExtraction()
{
    if (!CanViewEnding() || bExtractionSeen) return false;
    bExtractionSeen = true;
    return true;
}

FDuelNorthObjective FDuelNorthProgress::NextObjective() const
{
    if (!IsOpeningCellOpen())
    {
        return HasClue(EDuelNorthClue::OpeningDocket) ?
            FDuelNorthObjective{EDuelNorthFloor::Prison, TEXT("CellDoor"), TEXT("1F 牢房 · 抬起铁门下方的插销") } :
            FDuelNorthObjective{EDuelNorthFloor::Prison, TEXT("OpeningDocket"), TEXT("1F 牢房 · 找墙边桌上的纸条") };
    }
    if (!HasInvestigation(0))
    {
        if (!HasClue(EDuelNorthClue::RefugeLedger))
            return {EDuelNorthFloor::Prison, TEXT("RefugeLedger"), TEXT("1F 生活区 · 到北廊查配给底账，找同伴那份饭")};
        return {EDuelNorthFloor::Prison, TEXT("FindRations"), TEXT("1F 配膳间 · 到配膳台查缺口碗的碗底")};
    }
    if (!HasWon(0)) return {EDuelNorthFloor::Dungeon, TEXT("DungeonGate"), TEXT("B1 地牢 · 下侧梯去 03 号禁闭室救同伴")};
    if (!HasRelic(EDuelNorthRelic::RustKey)) return {EDuelNorthFloor::Dungeon, TEXT("RustKey"), TEXT("B1 禁闭室 · 拾取门边的锈铁钥匙")};
    if (!IsDoorOpen(EDuelNorthDoor::Chapel)) return {EDuelNorthFloor::Prison, TEXT("ChapelDoor"), TEXT("1F 上行平台 · 用锈铁钥匙打开档案室铁门")};
    if (!HasWon(1))
    {
        if (!HasInvestigation(1))
        {
            if (!HasClue(EDuelNorthClue::ArchiveTransferLedger))
                return {EDuelNorthFloor::Chapel, TEXT("ArchiveTransferLedger"), TEXT("2F 档案室 · 从入口往里走，在左架找《退信登记》")};
            if (!HasClue(EDuelNorthClue::ArchiveSignatureRegister))
                return {EDuelNorthFloor::Chapel, TEXT("ArchiveSignatureRegister"), TEXT("2F 档案室 · 在靠入口的右架找《送信记录》")};
            return {EDuelNorthFloor::Chapel, TEXT("FindCounterseal"), TEXT("2F 档案室 · 到校对台核对寄出和收到的信")};
        }
        if (!HasClue(EDuelNorthClue::OwnVerdict)) return {EDuelNorthFloor::Mirror, TEXT("OwnVerdict"), TEXT("3F 镜厅前室 · 查看候审单")};
        return {EDuelNorthFloor::Mirror, TEXT("MirrorGate"), TEXT("3F 镜厅 · 带上真信，打碎给自己下咒的镜子")};
    }
    if (!HasRelic(EDuelNorthRelic::MirrorKey))
    {
        if (!HasClue(EDuelNorthClue::Signature)) return {EDuelNorthFloor::Mirror, TEXT("Signature"), TEXT("3F 镜厅 · 查碎镜后的值班交接单")};
        return {EDuelNorthFloor::Mirror, TEXT("MirrorKey"), TEXT("3F 镜厅 · 拾取办公区钥匙")};
    }
    if (!IsDoorOpen(EDuelNorthDoor::FinalStair)) return {EDuelNorthFloor::Mirror, TEXT("FinalStairDoor"), TEXT("3F 镜厅后门 · 用钥匙打开通往办公区的门")};
    if (!HasWon(2) && !HasBeacon(TEXT("BeaconSwitch")))
    {
        if (!HasBeacon(TEXT("BeaconChart"))) return {EDuelNorthFloor::Beacon, TEXT("BeaconChart"), TEXT("监狱办公区 · 从长阶第一个平台进值班室，查值班记录")};
        if (!HasBeacon(TEXT("LensWorkshop"))) return {EDuelNorthFloor::Beacon, TEXT("LensWorkshop"), TEXT("监狱办公区 · 到信件检查室看观察记录")};
        if (!HasBeacon(TEXT("BeaconLetter"))) return {EDuelNorthFloor::Beacon, TEXT("BeaconLetter"), TEXT("监狱办公区 · 到看守住处找寄信人的日记")};
        return {EDuelNorthFloor::Beacon, TEXT("BeaconSwitch"), TEXT("监狱办公区 · 到物证室给红蜡对印，找女孩们的信")};
    }
    if (!HasWon(2)) return {EDuelNorthFloor::Final, TEXT("FinalGate"), TEXT("4F 终审厅 · 打败小雪，解除女孩们身上的咒")};
    if (!HasFinalConversation()) return {EDuelNorthFloor::Final, TEXT("FinalExitCompanion"), TEXT("4F 终审厅 · 去出口内侧与同伴会合")};
    if (!HasRelic(EDuelNorthRelic::NameTag)) return {EDuelNorthFloor::Final, TEXT("NameTag"), TEXT("4F 终审厅 · 到窗下取放行木牌")};
    if (!IsDoorOpen(EDuelNorthDoor::TowerExit)) return {EDuelNorthFloor::Final, TEXT("TowerExit"), TEXT("4F 塔顶小门 · 用放行木牌打开海边出口")};
    if (!bEndingSeen && bExtractionSeen) return {EDuelNorthFloor::Garden, TEXT("GardenEdge"), TEXT("月庭 · 到海崖尽头回看海边结局")};
    if (!bEndingSeen) return {EDuelNorthFloor::Garden, TEXT("GardenEdge"), TEXT("月庭 · 和同伴一起走到海崖尽头")};
    return {EDuelNorthFloor::Garden, TEXT("GardenEdge"), TEXT("月庭 · 在海崖尽头可以重看结局")};
}

bool FDuelNorthProgress::Validate(FString& Reason) const
{
    Reason.Reset();
    const auto Fail = [&](const TCHAR* Text) { Reason = Text; return false; };
    if (Format != CurrentFormat) return Fail(TEXT("Unsupported North Tower save format"));
    if (PlayerCharacter > static_cast<uint8>(EDuelCharacter::Ema)) return Fail(TEXT("Invalid player character"));
    if ((Wins != 0 && Wins != 1 && Wins != 3 && Wins != 7) || (Relics & ~7) || (Doors & ~7) ||
        (Clues & ~127) || (Dialogues & ~127) || (SideClues & ~SideClueMask) || (Reunions & ~ExtendedDialogueMask) ||
        (Shortcuts & ~3) || (ExploredFloors & ~127) || (BeaconBits & ~15) || (BeaconBits && !IsDoorOpen(EDuelNorthDoor::FinalStair)) || ((BeaconBits & 8) && (BeaconBits & 7) != 7) || ((BeaconBits & 6) && !(BeaconBits & 1)) || (CacheBits & ~DuelNorthFinds::AllCacheBits()) ||
        (VisitedCheckpoints & ~31) || (ActivatedCheckpoints & ~31) || (ActivatedCheckpoints & ~VisitedCheckpoints) ||
        Checkpoint >= static_cast<uint8>(EDuelNorthFloor::Garden))
        return Fail(TEXT("Invalid progress mask or checkpoint"));
    if (Relics & ~Wins) return Fail(TEXT("Relic without its boss victory"));
    if ((bOpeningCellUnlocked && (!bOpeningCellRequired || !HasClue(EDuelNorthClue::OpeningDocket))) ||
        (bPrisonRestActivated && (!bOpeningCellRequired || !bOpeningCellUnlocked)) || (!IsOpeningCellOpen() && Wins))
        return Fail(TEXT("Opening cell progress is inconsistent"));
    if (IsDoorOpen(EDuelNorthDoor::Chapel) && !HasRelic(EDuelNorthRelic::RustKey)) return Fail(TEXT("Chapel door without rust key"));
    if (IsDoorOpen(EDuelNorthDoor::FinalStair) && !HasRelic(EDuelNorthRelic::MirrorKey)) return Fail(TEXT("Final stair without mirror key"));
    if (IsDoorOpen(EDuelNorthDoor::TowerExit) && Relics != 7) return Fail(TEXT("Tower exit without all three relics"));
    if (HasWon(1) && !IsDoorOpen(EDuelNorthDoor::Chapel)) return Fail(TEXT("Mirror victory before chapel access"));
    if (HasWon(2) && !IsDoorOpen(EDuelNorthDoor::FinalStair)) return Fail(TEXT("Final victory before final stair access"));
    if (!(ExploredFloors & Bit(EDuelNorthFloor::Prison)) || !(VisitedCheckpoints & Bit(EDuelNorthFloor::Prison)) ||
        !Has(VisitedCheckpoints, static_cast<EDuelNorthFloor>(Checkpoint)) || (VisitedCheckpoints & ~ExploredFloors))
        return Fail(TEXT("Checkpoint discovery is inconsistent"));
    for (uint8 Index = 0; Index < static_cast<uint8>(EDuelNorthFloor::Count); ++Index)
    {
        const EDuelNorthFloor Floor = static_cast<EDuelNorthFloor>(Index);
        if (HasExplored(Floor) && !CanVisitFloor(Floor)) return Fail(TEXT("Explored floor behind a locked route"));
    }
    for (uint8 Index = 0; Index < static_cast<uint8>(EDuelNorthClue::Count); ++Index)
    {
        const EDuelNorthClue Clue = static_cast<EDuelNorthClue>(Index);
        if (HasClue(Clue) && !CanReadClue(Clue)) return Fail(TEXT("Clue revealed before its story state"));
    }
    if (HasDialogue(EDuelNorthDialogue::RescuedCompanion) && !HasWon(0)) return Fail(TEXT("Rescue dialogue before rescue"));
    if ((HasDialogue(EDuelNorthDialogue::ChapelBench) || HasDialogue(EDuelNorthDialogue::MirrorDoor)) && !IsDoorOpen(EDuelNorthDoor::Chapel))
        return Fail(TEXT("Upper dialogue before chapel access"));
    if (HasDialogue(EDuelNorthDialogue::Garden) && !IsDoorOpen(EDuelNorthDoor::TowerExit)) return Fail(TEXT("Garden dialogue before exit"));
    if ((HasDialogue(EDuelNorthDialogue::MirrorExit) && !HasWon(1)) ||
        (HasDialogue(EDuelNorthDialogue::FinalExit) && !HasWon(2))) return Fail(TEXT("Reunion before battle victory"));
    if ((HasDialogue(EDuelNorthDialogue::WaterGateEcho) && !HasClue(EDuelNorthClue::WaterGateLog)) ||
        (HasDialogue(EDuelNorthDialogue::ArchiveConversation) && !HasClue(EDuelNorthClue::ChapelJudgments)) ||
        (HasDialogue(EDuelNorthDialogue::FinalWarning) && !IsDoorOpen(EDuelNorthDoor::FinalStair)))
        return Fail(TEXT("Conversation before its discovery"));
    if ((bCompanionFollowing && !HasFinalConversation()) || (HasDialogue(EDuelNorthDialogue::Garden) && !bCompanionFollowing))
        return Fail(TEXT("Following does not match completed final conversation"));
    if (bEndingSeen && !CanViewEnding()) return Fail(TEXT("Ending before final conversation or tower exit"));
    if (bExtractionSeen && !CanViewEnding()) return Fail(TEXT("Extraction before final conversation or tower exit"));
    if (IsShortcutOpen(EDuelNorthShortcut::DungeonReturn) && !HasWon(0)) return Fail(TEXT("Return shortcut before rescue"));
    if (IsShortcutOpen(EDuelNorthShortcut::ChapelLoop) && !IsDoorOpen(EDuelNorthDoor::Chapel)) return Fail(TEXT("Chapel shortcut before chapel access"));
    return true;
}

const TCHAR* FDuelNorthProgress::FloorName(EDuelNorthFloor Floor)
{
    static constexpr const TCHAR* Names[] = {TEXT("B1 地牢"), TEXT("1F 牢房与生活区"), TEXT("2F 档案室"), TEXT("3F 镜厅"),
        TEXT("4F 终审厅"), TEXT("海崖月庭"), TEXT("监狱办公区")};
    static_assert(UE_ARRAY_COUNT(Names) == static_cast<int32>(EDuelNorthFloor::Count));
    return Valid(Floor) ? Names[static_cast<uint8>(Floor)] : TEXT("层间阶梯");
}

const TCHAR* FDuelNorthProgress::RelicName(EDuelNorthRelic Relic)
{
    static constexpr const TCHAR* Names[] = {TEXT("锈铁钥匙"), TEXT("办公区钥匙"), TEXT("放行木牌")};
    return Valid(Relic) ? Names[static_cast<uint8>(Relic)] : TEXT("");
}

const TCHAR* FDuelNorthProgress::ClueTitle(EDuelNorthClue Clue)
{
    static constexpr const TCHAR* Names[] = {TEXT("禁闭登记"), TEXT("禁闭室交班簿"), TEXT("旧审判记录"), TEXT("石凳上的刻痕"),
        TEXT("镜厅候审单"), TEXT("值班交接单"), TEXT("撕开的判决书"), TEXT("配给底账"), TEXT("医务室观察单"), TEXT("放行名单"), TEXT("折过三次的纸条"),
        TEXT("退信登记"), TEXT("送信记录"), TEXT("夹在书脊里的留言"), TEXT("夜班看守的便签")};
    static_assert(UE_ARRAY_COUNT(Names) == static_cast<int32>(EDuelNorthClue::Count));
    return Valid(Clue) ? Names[static_cast<uint8>(Clue)] : TEXT("");
}

const TCHAR* FDuelNorthProgress::EndingText()
{
    return TEXT("小雪停手后，女孩们身上的咒都解开了。艾玛和希罗把大家接到海边，一起离开魔女岛。\n后来，艾玛和希罗住在一起。星期天，她们牵着手去买面包，下雨也照去。");
}

const TCHAR* FDuelNorthProgress::ExtractionEndingText()
{
    return TEXT("小雪停手后，女孩们身上的咒都解开了。等最后一条救生船离开码头，艾玛和希罗才爬上直升机的绳梯。\n北塔在舷窗外越变越小。她们靠在一起，握着彼此的手，说好落地先去吃早饭。");
}
