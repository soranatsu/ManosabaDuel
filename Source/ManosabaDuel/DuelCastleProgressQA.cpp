#include "DuelCastle.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DuelArena.h"
#include "DuelProgress.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelCastleProgress, "ManosabaDuel.CastleProgress",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelCastleProgress::RunTest(const FString& Parameters)
{
    FString UserDirectory;
    const TCHAR* Command = FCommandLine::Get();
    const FString SaveDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
    const FString ProjectSaved = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Saved"));
    if (!FApp::CanEverRender() || !FParse::Param(Command, TEXT("DuelCastleRoute")) ||
        FParse::Param(Command, TEXT("DuelNoSave")) || FParse::Param(Command, TEXT("DuelNoTitle")) ||
        !FParse::Value(Command, TEXT("UserDir="), UserDirectory) || UserDirectory.IsEmpty() ||
        !FPaths::IsUnderDirectory(SaveDirectory, ProjectSaved) || FPaths::IsSamePath(SaveDirectory, ProjectSaved))
    {
        AddError(TEXT("Launch CastleProgress with rendering, -DuelCastleRoute, and a fresh -UserDir under the project's Saved directory."));
        return false;
    }

    constexpr const TCHAR* Slot = TEXT("MissingTestimonyCheckpoint");
    ADuelMode* Mode = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        if (Context.World() && (Mode = Context.World()->GetAuthGameMode<ADuelMode>()))
        {
            break;
        }
    }
    const FString SavePath = SaveDirectory / TEXT("SaveGames/MissingTestimonyCheckpoint.sav");
    if (!TestTrue(TEXT("The isolated castle starts at a fresh title with no existing checkpoint"),
        Mode && Mode->UsesCastle() && Mode->PlayerFighter && Mode->BossFighter && Mode->Arena &&
        Mode->Outcome() == EDuelOutcome::Title && !Mode->CanContinue() && !IFileManager::Get().FileExists(*SavePath)))
    {
        return false;
    }

    ADuelFighter& Player = *Mode->PlayerFighter;
    const auto Continue = [&]()
    {
        Mode->Result = EDuelOutcome::Title;
        Mode->MenuSelection = 0;
        Mode->Confirm();
    };
    const auto Touch = [&](const FVector& Feet)
    {
        Player.SetActorLocation(Feet + FVector(0, 0, 62));
        Mode->Confirm();
    };
    const auto Enter = [&](int32 Wing)
    {
        Touch(DuelCastle::Gate(Wing));
        Player.SetActorLocation(DuelCastle::Wings[Wing].Court + FVector(DuelCourt::GateCrossX + 45.f, 0, 62));
        Mode->Tick(0.f);
        return Mode->Outcome() == EDuelOutcome::Fighting && Mode->CastleDuel == Wing;
    };
    const auto AtHub = [&]()
    {
        return Mode->Outcome() == EDuelOutcome::Ready && !Mode->GateOpen() &&
            Player.GetActorLocation().Equals(DuelCastle::Spawn.GetLocation(), 4.f);
    };
    const auto HasSlots = [&](EDuelArt First, EDuelArt Second, EDuelArt Third, int32 Active)
    {
        return Player.SpellAt(0) == First && Player.SpellAt(1) == Second && Player.SpellAt(2) == Third &&
            Player.SpellSlot() == Active && Player.SelectedSpell() == Player.SpellAt(Active);
    };

    Mode->Confirm();
    TestTrue(TEXT("A fresh character has the three starter spells and no exploration spells"), AtHub() &&
        HasSlots(EDuelArt::IceLance, EDuelArt::WitchRain, EDuelArt::SableBlade, 0) &&
        !Mode->KnowsSpell(EDuelArt::LanternMote) && !Mode->KnowsSpell(EDuelArt::Blackwell) && !Mode->KnowsSpell(EDuelArt::SaltArc));
    Touch(DuelCastle::Wings[2].Key);
    Touch(DuelCastle::Wings[0].Key);
    Touch(DuelCastle::Wings[0].Clue[0]);
    TestTrue(TEXT("The cross-wing keys unlock lantern and salt, while the well still needs its second clue"),
        Mode->KnowsSpell(EDuelArt::LanternMote) && Mode->KnowsSpell(EDuelArt::SaltArc) && !Mode->KnowsSpell(EDuelArt::Blackwell));
    Touch(DuelCastle::Wings[0].Clue[1]);
    if (!TestTrue(TEXT("The complete first investigation unlocks the well and opens its duel"),
        Mode->KnowsSpell(EDuelArt::Blackwell) && Enter(0)))
    {
        return false;
    }
    Mode->FighterFell(Mode->BossFighter);
    Touch(DuelCastle::Record(0));
    Touch(DuelCastle::Wings[1].Key);
    for (const FVector& Clue : DuelCastle::Wings[1].Clue)
    {
        Touch(Clue);
    }

    Player.SetWeapon(EDuelWeapon::Staff);
    Mode->TogglePause();
    Mode->Navigate(2);
    Mode->Confirm();
    if (!TestTrue(TEXT("The castle pause menu opens its loadout"), Mode->ShowingLoadout()))
    {
        return false;
    }
    const EDuelArt Unlocked[] = {EDuelArt::LanternMote, EDuelArt::Blackwell, EDuelArt::SaltArc};
    for (int32 Index = DuelCourt::SpellSlotCount - 1; Index >= 0; --Index)
    {
        Mode->SelectLoadoutRow(ADuelMode::FirstSpellRow + Index);
        for (int32 Attempt = 0; Attempt < UE_ARRAY_COUNT(DuelCourt::StaffSpells) && Player.SpellAt(Index) != Unlocked[Index]; ++Attempt)
        {
            Mode->AdjustLoadout(1);
        }
    }
    Mode->TogglePause();
    Mode->TogglePause();
    Player.CycleSpell();
    if (!TestTrue(TEXT("The three unlocked spells can be prepared and cycled"),
        HasSlots(Unlocked[0], Unlocked[1], Unlocked[2], 1) && Player.ActiveArt() == EDuelArt::Blackwell))
    {
        return false;
    }
    Touch(DuelCastle::Hub);
    const TArray<uint8> Prepared{static_cast<uint8>(Unlocked[0]), static_cast<uint8>(Unlocked[1]), static_cast<uint8>(Unlocked[2])};
    const uint8 Clues = DuelCastle::ClueMask(0) | DuelCastle::ClueMask(1);
    const auto VerifySave = [&](uint8 Scars)
    {
        const UDuelProgress* Save = Cast<UDuelProgress>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
        return Save && Save->CastleFormat == 1 && Save->CastleKeys == DuelCastle::Whole &&
            Save->CastleClues == Clues && Save->CastleWins == DuelCastle::WingBit(0) &&
            Save->CastleSeals == DuelCastle::WingBit(0) && Save->CastleShortcuts == DuelCastle::WingBit(0) &&
            Save->VerdictScars == Scars && Save->StaffSpells == Prepared && Save->ActiveSpellSlot == 1 &&
            Save->FirstFight > 0 && !Save->bEastOpen && !Save->bFinalRecord && !Save->bFinished &&
            IFileManager::Get().FileSize(*SavePath) > 0;
    };
    if (!TestTrue(TEXT("Rest writes the current keys, observations, seal, spells and slot to the isolated save"), VerifySave(0)))
    {
        return false;
    }
    Player.SetWeapon(EDuelWeapon::Rapier);
    Player.AdjustSpellSlot(0, 1);
    Player.CycleSpell();
    Mode->CastleKeys = Mode->CastleClues = Mode->CastleSeals = Mode->CastleShortcuts = Mode->CastleWins = 0;
    Continue();
    if (!TestTrue(TEXT("Continue restores the staff and its chosen slots, with the read plaque preserved"), AtHub() &&
        Mode->CastleMarks() == 1 && Player.EquippedWeapon() == EDuelWeapon::Staff && HasSlots(Unlocked[0], Unlocked[1], Unlocked[2], 1) &&
        Mode->KnowsSpell(Unlocked[0]) && Mode->KnowsSpell(Unlocked[1]) && Mode->KnowsSpell(Unlocked[2])) ||
        !TestTrue(TEXT("Continue preserves the keys and evidence needed for the escort duel"), Enter(1)))
    {
        return false;
    }

    ADuelFighter& Boss = *Mode->BossFighter;
    const FVector Court = DuelCastle::Wings[1].Court;
    Player.SetActorLocation(Court + FVector(-420, 0, 62));
    Player.GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Boss.SetActorLocation(Court + FVector(230, 0, 62));
    const auto CastAtBoss = [&](EDuelArt Art)
    {
        const float RemainingFocus = Player.Focus - DuelCourt::FocusCost(Art);
        Player.Crimson();
        return TestTrue(TEXT("The selected spell starts and debits exactly its MP cost"),
            Player.Move == EDuelMove::Crimson && Player.MoveArt == Art && FMath::IsNearlyEqual(Player.Focus, RemainingFocus));
    };
    const auto DamageIs = [&](float OldHealth, float Damage)
    {
        return FMath::IsNearlyEqual(OldHealth - Boss.Health, Damage, .05f);
    };
    const auto& Well = DuelCourt::WeaponArt(EDuelArt::Blackwell).Strike;
    const float BeforeWell = Boss.Health;
    if (!CastAtBoss(EDuelArt::Blackwell))
    {
        return false;
    }
    Player.TickMove(Well.Timing.Windup + .02f);
    TestTrue(TEXT("The first well pulse damages the locked target"), DamageIs(BeforeWell, Well.Damage));
    Player.TickMove(.52f);
    TestTrue(TEXT("The second well pulse deals its additional damage"), DamageIs(BeforeWell, Well.Damage * 1.72f));
    Player.TickMove(Well.Timing.Recovery + Well.Timing.Active);

    Player.CycleSpell();
    Boss.SetActorLocation(Player.GetActorLocation() + FVector(420, 0, 0));
    const auto& Arc = DuelCourt::WeaponArt(EDuelArt::SaltArc).Strike;
    const float BeforeArc = Boss.Health;
    if (!CastAtBoss(EDuelArt::SaltArc))
    {
        return false;
    }
    Player.TickMove(Arc.Timing.Total());
    TestTrue(TEXT("The salt fan damages the boss in front and tops up the well's posture damage into a stagger"),
        DamageIs(BeforeArc, Arc.Damage) && Boss.IsStaggered());
    if (Boss.IsStaggered())
    {
        Boss.TickMove(2.f);
    }

    Player.CycleSpell();
    Boss.SetActorLocation(Player.GetActorLocation() + FVector(650, 0, 0));
    const auto& Lantern = DuelCourt::WeaponArt(EDuelArt::LanternMote).Strike;
    const float BeforeLantern = Boss.Health;
    if (!CastAtBoss(EDuelArt::LanternMote))
    {
        return false;
    }
    Player.TickMove(Lantern.Timing.Total());
    for (int32 Frame = 0; Frame < 45; ++Frame)
    {
        Mode->Arena->Tick(1.f / 60.f);
    }
    TestTrue(TEXT("The launched lantern curves back to hit the locked boss"), DamageIs(BeforeLantern, Lantern.Damage));

    Player.CycleSpell();
    const float LowFocus = DuelCourt::FocusCost(EDuelArt::Blackwell) - 1.f;
    Player.Focus = LowFocus;
    Player.Crimson();
    TestTrue(TEXT("An unaffordable active spell stays selected and neither starts nor debits MP"),
        Player.SpellSlot() == 1 && Player.Move == EDuelMove::Idle && FMath::IsNearlyEqual(Player.Focus, LowFocus));
    Player.ReceiveDuelDamage(DuelCourt::PlayerMaxHealth * 2.f, Boss.GetActorLocation());
    if (!TestTrue(TEXT("A lethal hit writes exactly one verdict scar and retains prior progress"),
        Mode->Outcome() == EDuelOutcome::Defeat && VerifySave(1)))
    {
        return false;
    }
    Mode->Confirm();
    TestTrue(TEXT("Confirm after defeat revives at the hub with full HP and MP"), AtHub() && Player.IsAlive() &&
        FMath::IsNearlyEqual(Player.HealthRatio(), 1.f) && FMath::IsNearlyEqual(Player.FocusRatio(), 1.f));
    Mode->CastleSeals = Mode->VerdictScars = 0;
    Continue();
    TestTrue(TEXT("Continue after defeat restores the prior plaque, spells and scar"), AtHub() &&
        Mode->CastleMarks() == 1 && Mode->VerdictScars == 1 && HasSlots(Unlocked[0], Unlocked[1], Unlocked[2], 1));

    UDuelProgress* Legacy = NewObject<UDuelProgress>();
    Legacy->bAppeal = true;
    Legacy->Spell = static_cast<uint8>(EDuelArt::WitchRain);
    if (!TestTrue(TEXT("The isolated slot accepts a pre-castle appeal save"), UGameplayStatics::SaveGameToSlot(Legacy, Slot, 0)))
    {
        return false;
    }
    Continue();
    TestTrue(TEXT("Appeal migration grants the first plaque and preserves the original single spell"), AtHub() &&
        Mode->CastleMarks() == 1 && Mode->KnowsSpell(EDuelArt::LanternMote) && Mode->KnowsSpell(EDuelArt::Blackwell) &&
        !Mode->KnowsSpell(EDuelArt::SaltArc) && HasSlots(EDuelArt::WitchRain, EDuelArt::IceLance, EDuelArt::SableBlade, 0));
    Legacy->bYukiCourt = true;
    if (!TestTrue(TEXT("The isolated slot accepts a pre-castle final court save"), UGameplayStatics::SaveGameToSlot(Legacy, Slot, 0)))
    {
        return false;
    }
    Continue();
    TestTrue(TEXT("Final court migration opens the eastern ascent with all three plaques"), AtHub() &&
        Mode->CastleMarks() == 3 && Mode->bEastOpen && Mode->KnowsSpell(EDuelArt::WhiteMantle) &&
        Mode->KnowsSpell(EDuelArt::WitnessStep) && HasSlots(EDuelArt::WitchRain, EDuelArt::IceLance, EDuelArt::SableBlade, 0));

    UDuelProgress* Locked = NewObject<UDuelProgress>();
    Locked->CastleFormat = 1;
    Locked->StaffSpells = {static_cast<uint8>(EDuelArt::WhiteMantle), static_cast<uint8>(EDuelArt::IceLance),
        static_cast<uint8>(EDuelArt::WitnessStep)};
    Locked->ActiveSpellSlot = 2;
    if (!TestTrue(TEXT("The isolated slot accepts prepared slots with missing unlocks"), UGameplayStatics::SaveGameToSlot(Locked, Slot, 0)))
    {
        return false;
    }
    Continue();
    TestTrue(TEXT("Locked entries fall back to distinct starter spells while keeping the selected slot"), AtHub() &&
        !Mode->KnowsSpell(EDuelArt::WhiteMantle) && !Mode->KnowsSpell(EDuelArt::WitnessStep) &&
        HasSlots(EDuelArt::WitchRain, EDuelArt::IceLance, EDuelArt::SableBlade, 2));
    AddInfo(FString::Printf(TEXT("Castle progress checkpoint and migrations exercised in %s"), *SavePath));
    return true;
}

#endif
