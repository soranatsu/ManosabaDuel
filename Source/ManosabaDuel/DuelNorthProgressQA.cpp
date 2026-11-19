#include "DuelNorthInvestigationQA.h"
#include "DuelNorthProgress.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DuelGame.h"
#include "DuelNorthFinds.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/Class.h"

namespace
{
    TArray<uint8> SaveNorthState(FDuelNorthProgress State)
    {
        TArray<uint8> Bytes;
        FMemoryWriter Writer(Bytes, true);
        FDuelNorthProgress::StaticStruct()->SerializeItem(Writer, &State, nullptr);
        return Bytes;
    }

    FDuelNorthProgress LoadNorthState(TArray<uint8>& Bytes)
    {
        FDuelNorthProgress State;
        FMemoryReader Reader(Bytes, true);
        FDuelNorthProgress::StaticStruct()->SerializeItem(Reader, &State, nullptr);
        return State;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthProgressFlow, "ManosabaDuel.NorthTower.Progress",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthProgressFlow::RunTest(const FString& Parameters)
{
    for (const EDuelCharacter Player : {EDuelCharacter::Hiro, EDuelCharacter::Ema})
    {
        FDuelNorthProgress State;
        State.Reset(Player);
        State.bOpeningCellRequired = true;
        const EDuelCharacter Other = Player == EDuelCharacter::Hiro ? EDuelCharacter::Ema : EDuelCharacter::Hiro;
        TestTrue(TEXT("New expedition owns its personal weapon, rapier, buckler, staff and empty offhand"),
            State.bExpeditionLoot && State.CacheBits == 0 && State.OwnsWeapon(DuelCourt::PersonalWeapon(Player)) &&
            State.OwnsWeapon(EDuelWeapon::Rapier) && State.OwnsWeapon(EDuelWeapon::Buckler) &&
            State.OwnsWeapon(EDuelWeapon::Staff) && State.OwnsWeapon(EDuelWeapon::Empty) &&
            !State.OwnsWeapon(DuelCourt::PersonalWeapon(Other)) && !State.OwnsWeapon(EDuelWeapon::Greatblade));
        for (EDuelArt Spell : DuelCourt::StaffSpells)
            TestEqual(TEXT("Only Ice Lance and Witch Rain start learned"), State.KnowsSpell(Spell),
                Spell == EDuelArt::IceLance || Spell == EDuelArt::WitchRain);
        FDuelNorthProgress Collected = State;
        Collected.CacheBits = DuelNorthFinds::AllCacheBits();
        for (EDuelArt Spell : DuelCourt::StaffSpells)
            TestTrue(TEXT("Every staff spell is reachable in the loot catalogue"), Collected.KnowsSpell(Spell));
        for (int32 GearId = 0; GearId <= int32(EDuelWeapon::RitualSword); ++GearId)
        {
            const auto Gear = static_cast<EDuelWeapon>(GearId);
            TestEqual(TEXT("Every character-legal type of equipment is obtainable"), Collected.OwnsWeapon(Gear),
                DuelCourt::CanEquip(Gear, Player, EDuelHand::Left));
        }
        TArray<uint8> LootBytes = SaveNorthState(Collected);
        const FDuelNorthProgress LootRoundTrip = LoadNorthState(LootBytes);
        TestTrue(TEXT("Tagged serialization retains the expedition flag and all loot bits"), LootRoundTrip.bExpeditionLoot &&
            LootRoundTrip.CacheBits == Collected.CacheBits && LootRoundTrip.KnowsSpell(EDuelArt::GildedCross));
        const auto Legacy = [&] { FDuelNorthProgress Old = State; Old.bExpeditionLoot = false; return Old; };
        TestTrue(TEXT("An old save retains formerly available equipment and advanced spells"),
            Legacy().OwnsWeapon(EDuelWeapon::RitualSword) && Legacy().KnowsSpell(EDuelArt::SableBlade));
        TestTrue(TEXT("Each route first rescues its companion, then fights itself, then Yuki"),
            State.BossForBattle(0) == Other && State.BossForBattle(1) == Player && State.BossForBattle(2) == EDuelCharacter::Yuki);
        TestTrue(TEXT("The fog waits for the ration investigation"), !State.CanEnterBattle(0) &&
            !State.CanEnterBattle(1) && !State.CanEnterBattle(2));
        TestFalse(TEXT("A key cannot be picked before its victory"), State.PickupRelic(EDuelNorthRelic::RustKey));
        TestFalse(TEXT("A locked upper checkpoint cannot be activated"), State.ActivateCheckpoint(EDuelNorthFloor::Final));
        TestFalse(TEXT("A new game cannot finish or start final following"), State.ViewEnding() ||
            State.CompleteDialogue(EDuelNorthDialogue::FinalExit) || State.bCompanionFollowing);
        TestTrue(TEXT("New game requires the docket inside the locked cell"),
            !State.IsOpeningCellOpen() && State.UsesStartingCellCheckpoint() &&
            State.NextObjective().Floor == EDuelNorthFloor::Prison && State.NextObjective().Target == TEXT("OpeningDocket"));
        TestFalse(TEXT("The cell cannot unlock before its docket is read"), State.OpenStartingCell());

        const auto CheckRestore = [&](const TCHAR* Label)
        {
            FString Reason;
            TestTrue(FString::Printf(TEXT("%s: original state is valid (%s)"), Label, *Reason), State.Validate(Reason));
            TArray<uint8> Bytes = SaveNorthState(State);
            FDuelNorthProgress Restored = LoadNorthState(Bytes);
            TestTrue(FString::Printf(TEXT("%s: UPROPERTY serialization preserves all fields"), Label),
                SaveNorthState(Restored) == Bytes);
            TestTrue(FString::Printf(TEXT("%s: restored state is valid"), Label), Restored.Validate(Reason));
            TestTrue(FString::Printf(TEXT("%s: objective survives restoration"), Label),
                State.NextObjective().Target == Restored.NextObjective().Target &&
                State.NextObjective().Floor == Restored.NextObjective().Floor);
            State = Restored;
        };
        CheckRestore(TEXT("New game in locked cell"));
        TestTrue(TEXT("The docket is collected while the cell stays closed"),
            State.ReadClue(EDuelNorthClue::OpeningDocket) && State.HasClue(EDuelNorthClue::OpeningDocket) &&
            !State.IsOpeningCellOpen() && State.NextObjective().Target == TEXT("CellDoor"));
        CheckRestore(TEXT("Docket read before exit"));
        TestTrue(TEXT("The cell opens onto the prison ledger investigation"),
            State.OpenStartingCell() && State.IsOpeningCellOpen() && State.UsesStartingCellCheckpoint() &&
            State.NextObjective().Floor == EDuelNorthFloor::Prison && State.NextObjective().Target == TEXT("RefugeLedger"));
        TestFalse(TEXT("An already open cell cannot unlock again"), State.OpenStartingCell());
        CheckRestore(TEXT("Cell opened before first rest"));
        State.ActivateCheckpoint(EDuelNorthFloor::Prison);
        TestTrue(TEXT("First actual rest replaces the starting cell as the prison checkpoint"),
            State.bPrisonRestActivated && !State.UsesStartingCellCheckpoint());
        CheckRestore(TEXT("First prison rest"));
        TestTrue(TEXT("The ration ledger supplies the bowl reference and basement cell number before the first battle"),
            State.ReadClue(EDuelNorthClue::RefugeLedger) && State.ClueText(EDuelNorthClue::RefugeLedger).Contains(TEXT("碗沿缺一角")) &&
            State.ClueText(EDuelNorthClue::RefugeLedger).Contains(TEXT("03 是地下禁闭室")));
        TestFalse(TEXT("Upper-floor records remain behind their own doors"),
            State.CanReadClue(EDuelNorthClue::SealPrototype) || State.CanReadClue(EDuelNorthClue::ReleaseOrder));

        const auto Conversation = [&](EDuelNorthDialogue Dialogue, EDuelNorthPresence ExpectedPresence)
        {
            const FDuelNorthDialogueScript Script = State.DialogueScript(Dialogue);
            TestTrue(TEXT("The available conversation uses the correct speaker and presence"),
                Script.Presence == ExpectedPresence && !Script.Lines.IsEmpty() && Script.Lines[0].Speaker == Other);
            if (!Script.Lines.IsEmpty())
            {
                TestTrue(TEXT("Companion addresses the selected protagonist consistently"),
                    Script.Lines[0].Text.Contains(Player == EDuelCharacter::Ema ? TEXT("艾玛") : TEXT("希罗酱")));
            }
            TestTrue(TEXT("A complete conversation is recorded"), State.CompleteDialogue(Dialogue));
            TestFalse(TEXT("Replay does not grant duplicate progress"), State.CompleteDialogue(Dialogue));
            const bool bRetired = Dialogue == EDuelNorthDialogue::RescuedCompanion || Dialogue == EDuelNorthDialogue::FinalExit ||
                Dialogue == EDuelNorthDialogue::Garden;
            TestEqual(TEXT("One-time companion scenes retire on completion"), State.DialogueScript(Dialogue).Lines.IsEmpty(), bRetired);
        };
        TestTrue(TEXT("The cell conversation is an optional discovery"),
            State.CompleteDialogue(EDuelNorthDialogue::CellEcho) && State.NextObjective().Target == TEXT("FindRations"));
        Conversation(EDuelNorthDialogue::NameWall, EDuelNorthPresence::Echo);
        Conversation(EDuelNorthDialogue::LongTable, EDuelNorthPresence::Echo);
        Conversation(EDuelNorthDialogue::DungeonDoor, EDuelNorthPresence::Voice);
        TestFalse(TEXT("Rescued person is absent before the first victory"), State.DialogueAvailable(EDuelNorthDialogue::RescuedCompanion));
        TestTrue(TEXT("The dungeon checkpoint can be activated and revisited"), State.ActivateCheckpoint(EDuelNorthFloor::Dungeon) &&
            State.CanActivateCheckpoint(EDuelNorthFloor::Dungeon));
        State.ReadClue(EDuelNorthClue::CellVerdict);
        const FString BeforeRationInvestigation = State.ClueText(EDuelNorthClue::CellVerdict);
        TestTrue(TEXT("The confinement delivery slip names the correct companion"),
            BeforeRationInvestigation.Contains(Other == EDuelCharacter::Ema ? TEXT("艾玛") : TEXT("希罗")));
        TestTrue(TEXT("Before the ration investigation the slip sends the player to the ledger without revealing the cell number"),
            BeforeRationInvestigation.Contains(TEXT("晚饭单送")) && BeforeRationInvestigation.Contains(TEXT("门牌看配给底账")) &&
            !BeforeRationInvestigation.Contains(TEXT("03")) && !BeforeRationInvestigation.Contains(TEXT("握住碗")));
        TestTrue(TEXT("Old checkpoints retain the first clue's Lantern Mote. New ones require the cache."),
            Legacy().KnowsSpell(EDuelArt::LanternMote) && !State.KnowsSpell(EDuelArt::LanternMote));
        TestFalse(TEXT("The old well still needs both clues"), Legacy().KnowsSpell(EDuelArt::Blackwell));
        State.ReadClue(EDuelNorthClue::WaterGateLog);
        TestTrue(TEXT("The confinement conversation records its completion independently"), State.CompleteDialogue(EDuelNorthDialogue::WaterGateEcho));
        TestTrue(TEXT("The two dungeon clues still unlock salt and well for old checkpoints"),
            Legacy().KnowsSpell(EDuelArt::SaltArc) && Legacy().KnowsSpell(EDuelArt::Blackwell) &&
            !State.KnowsSpell(EDuelArt::SaltArc) && !State.KnowsSpell(EDuelArt::Blackwell));

        TestFalse(TEXT("Reading alone cannot open the first fog"), State.WinBattle(0));
        TestTrue(TEXT("Resolving the ration puzzle unlocks the first fog"),
            DuelNorthInvestigationQA::Prepare(State, 0) && State.NextObjective().Target == TEXT("DungeonGate"));
        const FString AfterRationInvestigation = State.ClueText(EDuelNorthClue::CellVerdict);
        TestTrue(TEXT("After the ration investigation the same companion's slip identifies cell 03 and the impaired hand"),
            AfterRationInvestigation.Contains(Other == EDuelCharacter::Ema ? TEXT("艾玛") : TEXT("希罗")) &&
            AfterRationInvestigation.Contains(TEXT("03 号地下禁闭室")) && AfterRationInvestigation.Contains(TEXT("还能不能握住碗")) &&
            AfterRationInvestigation != BeforeRationInvestigation);
        TestTrue(TEXT("First victory is recorded"), State.WinBattle(0));
        TestFalse(TEXT("A duplicate victory has no side effects"), State.WinBattle(0));
        TestTrue(TEXT("Victory spawns a pickup but does not collect it or unlock a door"),
            State.RelicAvailable(EDuelNorthRelic::RustKey) && !State.HasRelic(EDuelNorthRelic::RustKey) &&
            !State.IsDoorOpen(EDuelNorthDoor::Chapel));
        CheckRestore(TEXT("Victory before pickup"));
        TestTrue(TEXT("Uncollected key is still available after restore"), State.RelicAvailable(EDuelNorthRelic::RustKey));
        Conversation(EDuelNorthDialogue::RescuedCompanion, EDuelNorthPresence::Person);
        TestFalse(TEXT("The rescue conversation keeps the garden ending locked"), State.CanViewEnding());
        TestTrue(TEXT("The return shortcut opens after rescue"), State.OpenShortcut(EDuelNorthShortcut::DungeonReturn));
        TestTrue(TEXT("The rust key is picked up separately"), State.PickupRelic(EDuelNorthRelic::RustKey));
        TestFalse(TEXT("The collected key does not respawn"), State.RelicAvailable(EDuelNorthRelic::RustKey));
        CheckRestore(TEXT("Pickup before opening"));
        TestTrue(TEXT("The objective now leads to the actual one-floor-up door"),
            State.NextObjective().Target == TEXT("ChapelDoor") && State.NextObjective().Floor == EDuelNorthFloor::Prison);
        TestTrue(TEXT("The chapel unlock checks the relic"), State.OpenDoor(EDuelNorthDoor::Chapel));
        TestTrue(TEXT("Unlocking keeps the relic in the inventory"), State.HasRelic(EDuelNorthRelic::RustKey));
        CheckRestore(TEXT("Door opened"));
        TestTrue(TEXT("The open door remains open after restoration"), State.IsDoorOpen(EDuelNorthDoor::Chapel));
        TestTrue(TEXT("Archive investigation and final stair both stay locked"), !State.CanEnterBattle(1) && !State.CanEnterBattle(2));
        TestFalse(TEXT("The rescued person has left the lower floor after the chapel opens"), State.DialogueAvailable(EDuelNorthDialogue::RescuedCompanion));

        State.ActivateCheckpoint(EDuelNorthFloor::Chapel);
        TestTrue(TEXT("Both optional shelf discoveries can be recorded independently of the required books"),
            State.ReadClue(EDuelNorthClue::ArchiveBindingNote) && State.ReadClue(EDuelNorthClue::ArchiveNightIndex));
        State.OpenShortcut(EDuelNorthShortcut::ChapelLoop);
        Conversation(EDuelNorthDialogue::ChapelBench, EDuelNorthPresence::Echo);
        State.ReadClue(EDuelNorthClue::ChapelJudgments);
        TestTrue(TEXT("The archive conversation records its completion independently"), State.CompleteDialogue(EDuelNorthDialogue::ArchiveConversation));
        TestEqual(TEXT("Investigation first points to the returned-mail register"),State.NextObjective().Target,FName(TEXT("ArchiveTransferLedger")));
        State.ReadClue(EDuelNorthClue::ArchiveTransferLedger);
        TestEqual(TEXT("Next the guide leads to the delivery log"),State.NextObjective().Target,FName(TEXT("ArchiveSignatureRegister")));
        State.ReadClue(EDuelNorthClue::ArchiveSignatureRegister);
        TestTrue(TEXT("The two ledgers document both withheld originals and matching seals on the false letters"),
            State.ClueText(EDuelNorthClue::ArchiveTransferLedger).Contains(TEXT("退信袋")) &&
            State.ClueText(EDuelNorthClue::ArchiveTransferLedger).Contains(TEXT("扣下未送")) &&
            State.ClueText(EDuelNorthClue::ArchiveSignatureRegister).Contains(TEXT("红蜡")) &&
            State.ClueText(EDuelNorthClue::ArchiveSignatureRegister).Contains(TEXT("缺角")));
        TestTrue(TEXT("Both folios lead to the desk, the mirror fog stays locked"),
            State.NextObjective().Target == TEXT("FindCounterseal") && !State.WinBattle(1));
        TestTrue(TEXT("Cross-check opens the mirror fog"),DuelNorthInvestigationQA::Prepare(State,1) && State.CanEnterBattle(1));
        const auto MirrorReason = State.InvestigationScript(TEXT("FindCounterseal"));
        TestTrue(TEXT("Both protagonists enter the mirror hall to break their own curse and leave together"),
            MirrorReason.Lines.ContainsByPredicate([](const FDuelNorthDialogueLine& Line)
                { return Line.Text.Contains(TEXT("打碎镜子")) && Line.Text.Contains(TEXT("幻象")); }) &&
            MirrorReason.Lines.Last().Speaker == Player && MirrorReason.Lines.Last().Text.Contains(TEXT("镜厅")) &&
            State.ClueText(EDuelNorthClue::ArchiveSignatureRegister).Contains(TEXT("钥匙留在镜后")));
        State.ReadClue(EDuelNorthClue::SeaMark);
        TestTrue(TEXT("The infirmary records that recognition of a companion eased the symptoms"),
            State.ReadClue(EDuelNorthClue::SealPrototype) && State.ClueText(EDuelNorthClue::SealPrototype).Contains(TEXT("认出对方")) &&
            State.ClueText(EDuelNorthClue::SealPrototype).Contains(TEXT("僵硬的手指才慢慢松开")));
        TestTrue(TEXT("The old chapel unlock is retained without granting the expedition spell by reading"),
            Legacy().KnowsSpell(EDuelArt::WhiteMantle) && !State.KnowsSpell(EDuelArt::WhiteMantle));
        Conversation(EDuelNorthDialogue::MirrorDoor, EDuelNorthPresence::Echo);
        State.ActivateCheckpoint(EDuelNorthFloor::Mirror);
        State.ReadClue(EDuelNorthClue::OwnVerdict);
        TestTrue(TEXT("The mirror rubbing identifies the selected player's name"), State.ClueText(EDuelNorthClue::OwnVerdict).Contains(
            Player == EDuelCharacter::Ema ? TEXT("艾玛") : TEXT("希罗")));
        TestFalse(TEXT("The watch handoff cannot be read before the mirror victory"), State.CanReadClue(EDuelNorthClue::Signature));
        TestFalse(TEXT("The companion's mirror reunion waits for the victory"), State.DialogueAvailable(EDuelNorthDialogue::MirrorExit));
        TestTrue(TEXT("Mirror victory is distinct from its key"), State.WinBattle(1) && !State.HasRelic(EDuelNorthRelic::MirrorKey));
        CheckRestore(TEXT("Mirror victory before reading or pickup"));
        Conversation(EDuelNorthDialogue::MirrorExit, EDuelNorthPresence::Person);
        TestTrue(TEXT("The watch handoff becomes the next objective"), State.NextObjective().Target == TEXT("Signature"));
        State.ReadClue(EDuelNorthClue::Signature);
        TestTrue(TEXT("Old checkpoints retain Witness Step after the mirror signature"),
            Legacy().KnowsSpell(EDuelArt::WitnessStep) && !State.KnowsSpell(EDuelArt::WitnessStep));
        State.PickupRelic(EDuelNorthRelic::MirrorKey);
        TestFalse(TEXT("A mirror key alone cannot open the tower exit"), State.OpenDoor(EDuelNorthDoor::TowerExit));
        State.OpenDoor(EDuelNorthDoor::FinalStair);
        const FName Chart(TEXT("BeaconChart")), Lens(TEXT("LensWorkshop")), Letter(TEXT("BeaconLetter")), Switch(TEXT("BeaconSwitch"));
        TestTrue(TEXT("The mirror exit leads to the prison inspection rooms with the final fight still locked"),
            State.NextObjective().Target == Chart && State.NextObjective().Floor == EDuelNorthFloor::Beacon &&
            !State.CanEnterBattle(2) && !State.DialogueAvailable(EDuelNorthDialogue::FinalWarning));
        TestFalse(TEXT("Side evidence and the seal comparison wait for the inspection register"),
            State.RecordBeacon(Lens) || State.RecordBeacon(Letter) || State.RecordBeacon(Switch));
        TestTrue(TEXT("Reading the inspection register leads to the two side rooms"), State.RecordBeacon(Chart) && State.NextObjective().Target == Lens);
        FDuelNorthProgress LetterFirst = State;
        TestTrue(TEXT("The two side rooms can be visited in either order"),
            LetterFirst.RecordBeacon(Letter) && LetterFirst.NextObjective().Target == Lens &&
            !LetterFirst.RecordBeacon(Switch) && LetterFirst.RecordBeacon(Lens) && LetterFirst.NextObjective().Target == Switch);
        TestTrue(TEXT("Examining the observation record first preserves the investigation order and leaves the culprit undisclosed"),
            State.RecordBeacon(Lens) && State.NextObjective().Target == Letter &&
            !State.InvestigationScript(Lens).Lines.ContainsByPredicate([](const FDuelNorthDialogueLine& Line)
                { return Line.Text.Contains(TEXT("小雪")); }));
        CheckRestore(TEXT("Inspection register and observation record examined"));
        TestFalse(TEXT("The office order is required before the seal comparison"), State.RecordBeacon(Switch));
        const FString Observation = State.BeaconEvidenceText(Lens);
        TestTrue(TEXT("Two observations connect the isolated recipient's symptoms to an ongoing external spell"),
            Observation.Contains(TEXT("牢里只有收信的女孩一人")) && Observation.Contains(TEXT("手上长出黑纹")) &&
            Observation.Contains(TEXT("寄信的人站在门外施法")) && Observation.Contains(TEXT("停下施法")) &&
            Observation.Contains(TEXT("女孩手上的黑纹便淡下去")) &&
            Observation.Contains(TEXT("今晚换了个女孩拆信")));
        TestTrue(TEXT("The office order leads to the seal desk"), State.RecordBeacon(Letter) && State.NextObjective().Target == Switch);
        const FString Order = State.BeaconEvidenceText(Letter);
        TestTrue(TEXT("Yuki's diary establishes her existing witch identity, revenge, worldwide plan and interruptible spell"),
            Order.Contains(TEXT("小雪")) && Order.Contains(TEXT("求救信扣下")) && Order.Contains(TEXT("我的戒指")) &&
            Order.Contains(TEXT("人类杀光了我的族人")) && Order.Contains(TEXT("我生来就是魔女")) &&
            Order.Contains(TEXT("最后的大魔女")) && Order.Contains(TEXT("全人类都变成魔女")) &&
            Order.Contains(TEXT("下一批信要送到岛外")) && Order.Contains(TEXT("施法被打断")) &&
            Order.Contains(TEXT("还没定形的变化")) && !Order.Contains(TEXT("又爬回我手上")));
        TestTrue(TEXT("Opening the intercepted letter bag releases the final floor's fight"),
            State.RecordBeacon(Switch) && State.CanEnterBattle(2) && State.NextObjective().Target == TEXT("FinalGate"));
        const FString Proof = State.BeaconEvidenceText(Switch);
        TestTrue(TEXT("The opened bag and matching damaged ring connect the withheld pleas and both forged letters to Yuki's signed order"),
            Proof.Contains(TEXT("求救信")) && Proof.Contains(TEXT("戒指压出的印与两封假信的红蜡印相合")) &&
            Proof.Contains(TEXT("右边一瓣都缺角")) && Order.Contains(TEXT("小雪")) && Order.Contains(TEXT("我的戒指")));
        State.ActivateCheckpoint(EDuelNorthFloor::Final);
        const auto Confrontation = State.DialogueScript(EDuelNorthDialogue::FinalWarning);
        TestTrue(TEXT("Yuki confirms her revenge and knowingly persists in turning humanity into witches"),
            Confrontation.Lines.Num() == 5 && Confrontation.Lines[0].Speaker == EDuelCharacter::Yuki &&
            Confrontation.Lines[0].Text.Contains(TEXT("最后的大魔女")) &&
            Confrontation.Lines[2].Text.Contains(TEXT("全人类都变成魔女")) &&
            Confrontation.Lines[2].Text.Contains(TEXT("猎杀")) &&
            Confrontation.Lines.Last().Text.Contains(TEXT("每一个人类都变成魔女")));
        TestTrue(TEXT("The final warning records its completion independently"), State.CompleteDialogue(EDuelNorthDialogue::FinalWarning));
        CheckRestore(TEXT("Final checkpoint"));
        TestFalse(TEXT("The companion's final reunion waits for the victory"), State.DialogueAvailable(EDuelNorthDialogue::FinalExit));
        TestTrue(TEXT("Final victory points to the battlefield reunion with the pickup still available"),
            State.WinBattle(2) && State.RelicAvailable(EDuelNorthRelic::NameTag) &&
            State.NextObjective().Target == TEXT("FinalExitCompanion"));
        const auto DefeatedYuki = State.DialogueScript(FDuelNorthProgress::BossDefeatDialogue(2));
        TestTrue(TEXT("Yuki's defeat interrupts the spell before the girls' transformations become permanent"),
            DefeatedYuki.Lines.Num() == 3 && DefeatedYuki.Lines[0].Speaker == EDuelCharacter::Yuki &&
            DefeatedYuki.Lines[0].Text.Contains(TEXT("施咒断了")) && DefeatedYuki.Lines[0].Text.Contains(TEXT("变化还没定形")));
        State.ReadClue(EDuelNorthClue::FinalSeal);
        TestTrue(TEXT("The release list for the girls can be read after the final battle"),
            State.ReadClue(EDuelNorthClue::ReleaseOrder) && State.ClueText(EDuelNorthClue::ReleaseOrder).Contains(TEXT("按原名逐一放行")));
        TestFalse(TEXT("Reading an optional record twice does not grant another discovery"), State.ReadClue(EDuelNorthClue::ReleaseOrder));
        const FDuelNorthDialogueScript Reunion = State.DialogueScript(EDuelNorthDialogue::FinalExit);
        TestEqual(TEXT("The court reunion is a brief exchange before heading outside"), Reunion.Lines.Num(), 3);
        TestFalse(TEXT("The confession belongs to the rooftop script"),
            Reunion.Lines.ContainsByPredicate([](const FDuelNorthDialogueLine& Line) { return Line.Text.Contains(TEXT("喜欢你")); }));
        FDuelNorthProgress BeforeReunion = State;
        BeforeReunion.PickupRelic(EDuelNorthRelic::NameTag);
        TestFalse(TEXT("Collecting the keepsakes still leaves the exit awaiting the one final conversation"),
            BeforeReunion.OpenDoor(EDuelNorthDoor::TowerExit));
        Conversation(EDuelNorthDialogue::FinalExit, EDuelNorthPresence::Person);
        TestTrue(TEXT("Following starts inside the tower and sends the player to the remaining pickup"),
            State.bCompanionFollowing && !State.CanViewEnding() && State.NextObjective().Target == TEXT("NameTag"));
        TestFalse(TEXT("The rooftop dialogue requires the open tower exit"), State.DialogueAvailable(EDuelNorthDialogue::Garden));
        CheckRestore(TEXT("Final conversation before name tag"));
        State.PickupRelic(EDuelNorthRelic::NameTag);
        TestTrue(TEXT("Reunion plus all three keepsakes opens the exit"), State.OpenDoor(EDuelNorthDoor::TowerExit) && State.Relics == 7);
        TestTrue(TEXT("The rooftop conversation and cliff ending are both available"), State.CanViewEnding() && !State.bEndingSeen &&
            State.NextObjective().Target == TEXT("GardenEdge") && State.DialogueAvailable(EDuelNorthDialogue::Garden));
        const FDuelNorthDialogueScript Garden = State.DialogueScript(EDuelNorthDialogue::Garden);
        for (const EDuelCharacter Speaker : {Player, Other})
        {
            TestTrue(TEXT("Both characters express their feelings on the rooftop"),
                Garden.Lines.ContainsByPredicate([&](const FDuelNorthDialogueLine& Line)
                { return Line.Speaker == Speaker && Line.Text.Contains(TEXT("喜欢你")); }));
        }
        TestFalse(TEXT("Garden is not a sixth checkpoint"), State.ActivateCheckpoint(EDuelNorthFloor::Garden));
        State.ExploreFloor(EDuelNorthFloor::Garden);
        CheckRestore(TEXT("Exit with companion following"));
        Conversation(EDuelNorthDialogue::Garden, EDuelNorthPresence::Person);
        CheckRestore(TEXT("Rooftop conversation after leaving the tower"));
        TestTrue(TEXT("The rooftop talk preserves the companion and leaves both ending flags separate"),
            State.bCompanionFollowing && !State.bEndingSeen && !State.bExtractionSeen && State.CanViewEnding());
        TestTrue(TEXT("The ending is explicitly requested after walking to the final interaction"), State.ViewEnding());
        for (const TCHAR* Page : {FDuelNorthProgress::EndingText(), FDuelNorthProgress::ExtractionEndingText()})
        {
            const FString Text(Page);
            TestTrue(TEXT("Both endings explicitly release the girls when Yuki stops maintaining the curse"),
                Text.Contains(TEXT("小雪停手后")) && Text.Contains(TEXT("女孩们身上的咒都解开了")) && Text.Contains(TEXT("艾玛和希罗")));
        }
        const FString WalkingEnding(FDuelNorthProgress::EndingText()), AirliftEnding(FDuelNorthProgress::ExtractionEndingText());
        TestTrue(TEXT("The walking ending leaves the island and preserves the hand-in-hand epilogue"),
            WalkingEnding.Contains(TEXT("离开魔女岛")) && WalkingEnding.Contains(TEXT("牵着手")));
        TestTrue(TEXT("The airlift waits for the last lifeboat and preserves the couple's shared departure"),
            AirliftEnding.Contains(TEXT("最后一条救生船离开码头")) && AirliftEnding.Contains(TEXT("直升机的绳梯")) &&
            AirliftEnding.Contains(TEXT("握着彼此的手")));
        CheckRestore(TEXT("Ending seen"));
        TestTrue(TEXT("Ending is replayable and following persists"), State.CanViewEnding() && State.bCompanionFollowing && State.bEndingSeen);
        TestFalse(TEXT("Viewing the ending again does not re-grant progress"), State.ViewEnding());
        for (uint8 Index = 0; Index < static_cast<uint8>(EDuelNorthDialogue::Count); ++Index)
        {
            const auto Dialogue = static_cast<EDuelNorthDialogue>(Index);
            TestTrue(TEXT("The completed conversations are all preserved"), State.HasDialogue(Dialogue));
        }
        for (uint8 Index = 0; Index < static_cast<uint8>(EDuelNorthClue::Count); ++Index)
            TestTrue(TEXT("The optional and main clues all survive checkpoint restoration"), State.HasClue(static_cast<EDuelNorthClue>(Index)));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthProgressValidation, "ManosabaDuel.NorthTower.ProgressValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthProgressValidation::RunTest(const FString& Parameters)
{
    FDuelNorthProgress Fresh;
    FString Reason;
    TestTrue(TEXT("Fresh legacy progress validates with an open cell and its original checkpoint"),
        Fresh.Validate(Reason) && Fresh.IsOpeningCellOpen() && !Fresh.UsesStartingCellCheckpoint());
    TestFalse(TEXT("Negative battle indices do not shift a mask or advance"), Fresh.HasWon(-1) || Fresh.WinBattle(-1));
    TestFalse(TEXT("Out-of-range battle indices do not advance"), Fresh.WinBattle(3));
    TestFalse(TEXT("Unknown enum values cannot mutate progress"),
        Fresh.PickupRelic(static_cast<EDuelNorthRelic>(255)) || Fresh.OpenDoor(static_cast<EDuelNorthDoor>(255)) ||
        Fresh.ReadClue(static_cast<EDuelNorthClue>(255)) || Fresh.CompleteDialogue(static_cast<EDuelNorthDialogue>(255)) ||
        Fresh.ExploreFloor(EDuelNorthFloor::Unknown) || Fresh.ActivateCheckpoint(EDuelNorthFloor::Unknown) ||
        Fresh.OpenShortcut(static_cast<EDuelNorthShortcut>(255)));
    const auto Reject = [&](FDuelNorthProgress Invalid, const TCHAR* Label)
    {
        TestFalse(Label, Invalid.Validate(Reason));
        TestFalse(TEXT("A corrupt save has an actionable rejection reason"), Reason.IsEmpty());
    };
    FDuelNorthProgress Bad = Fresh; Bad.Format = 2; Reject(Bad, TEXT("A future format is not misread as current"));
    Bad = Fresh; Bad.PlayerCharacter = 2; Reject(Bad, TEXT("A Boss cannot be restored as the human protagonist"));
    Bad = Fresh; Bad.Wins = 4; Reject(Bad, TEXT("A final victory cannot skip its prerequisites"));
    Bad = Fresh; Bad.Relics = 1; Reject(Bad, TEXT("A key cannot precede its boss"));
    Bad = Fresh; Bad.Doors = 1; Reject(Bad, TEXT("A chapel door cannot be open without its key"));
    Bad = Fresh; Bad.bOpeningCellRequired = true; Bad.bOpeningCellUnlocked = true;
    Reject(Bad, TEXT("The starting cell cannot be open without its own docket"));
    Bad = Fresh; Bad.Clues = 1 << static_cast<uint8>(EDuelNorthClue::Signature); Reject(Bad, TEXT("The watch handoff cannot leak before mirror victory"));
    Bad = Fresh; Bad.Dialogues = 1 << static_cast<uint8>(EDuelNorthDialogue::Garden); Reject(Bad, TEXT("The garden cannot be completed before the exit"));
    Bad = Fresh; Bad.ExploredFloors |= 1 << static_cast<uint8>(EDuelNorthFloor::Final); Reject(Bad, TEXT("An inaccessible final floor is not silently accepted"));
    Bad = Fresh; Bad.Checkpoint = static_cast<uint8>(EDuelNorthFloor::Dungeon); Reject(Bad, TEXT("The active checkpoint must be discovered"));
    Bad = Fresh; Bad.Shortcuts = 1; Reject(Bad, TEXT("The rescue return shortcut must follow its victory"));
    Bad = Fresh; Bad.bCompanionFollowing = true; Reject(Bad, TEXT("Final following requires the finished conversation"));
    Bad = Fresh; Bad.bEndingSeen = true; Reject(Bad, TEXT("An ending flag cannot skip the whole story"));
    Bad = Fresh; Bad.Relics = 128; Reject(Bad, TEXT("Unknown high mask bits are rejected"));

    FDuelNorthProgress Direct;
    for (int32 Battle = 0; Battle < FDuelNorthProgress::BattleCount; ++Battle)
    {
        TestTrue(TEXT("Required investigations lead to each encounter"), DuelNorthInvestigationQA::Prepare(Direct,Battle) && Direct.WinBattle(Battle));
        TestTrue(TEXT("Each victory's separate pickup is available"), Direct.PickupRelic(static_cast<EDuelNorthRelic>(Battle)));
        if (Battle < 2) TestTrue(TEXT("Earlier doors retain their relic requirements"), Direct.OpenDoor(static_cast<EDuelNorthDoor>(Battle)));
    }
    TestTrue(TEXT("Skipping optional reading is still a valid save"), Direct.Validate(Reason));
    TestTrue(TEXT("The direct route still ends at the companion, not an unsolicited ending"),
        Direct.NextObjective().Target == TEXT("FinalExitCompanion") && !Direct.CanViewEnding());
    TestTrue(TEXT("The one final conversation unlocks the remaining door"), Direct.CompleteDialogue(EDuelNorthDialogue::FinalExit) &&
        Direct.OpenDoor(EDuelNorthDoor::TowerExit));
    FDuelNorthProgress Legacy = Direct;
    Legacy.bCompanionFollowing = false;
    TestTrue(TEXT("A legacy save after the original court reunion can finish immediately"),
        Legacy.Validate(Reason) && Legacy.CanViewEnding() && !Legacy.DialogueAvailable(EDuelNorthDialogue::FinalExit));
    TestTrue(TEXT("A legacy court-only save can complete the new rooftop scene"),
        Legacy.CompleteDialogue(EDuelNorthDialogue::Garden) && Legacy.bCompanionFollowing && Legacy.Validate(Reason));
    Legacy = Direct;
    Legacy.bCompanionFollowing = false;
    Legacy.Reunions = 0;
    TestTrue(TEXT("A legacy checkpoint with an open gate and no reunion returns to the same court meeting"),
        Legacy.Validate(Reason) && !Legacy.CanViewEnding() && Legacy.NextObjective().Target == TEXT("FinalExitCompanion"));
    Legacy.Dialogues = 1 << static_cast<uint8>(EDuelNorthDialogue::Garden);
    Legacy.bCompanionFollowing = true;
    Legacy.ViewEnding();
    Legacy.CompleteExtraction();
    TArray<uint8> LegacyBytes = SaveNorthState(Legacy);
    Legacy = LoadNorthState(LegacyBytes);
    TestTrue(TEXT("A legacy garden conversation and both ending flags survive serialization"), Legacy.Validate(Reason) &&
        Legacy.CanViewEnding() && Legacy.bEndingSeen && Legacy.bExtractionSeen && !Legacy.DialogueAvailable(EDuelNorthDialogue::FinalExit) &&
        !Legacy.DialogueAvailable(EDuelNorthDialogue::Garden));
    return true;
}

#endif
