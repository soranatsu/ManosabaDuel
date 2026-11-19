#include "DuelNorthTower.h"

#include "DuelGame.h"
#include "DuelNorthNarrative.h"
#include "Engine/World.h"

void ADuelNorthTower::ShowMechanicHint(int32 Battle)
{
    const auto* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    if (!Mode || Battle != ActiveFight || Battle < 0 || Battle >= FDuelNorthProgress::BattleCount ||
        Mode->Outcome() != EDuelOutcome::Fighting || !Mode->GetPlayerFighter() || !Mode->GetBossFighter() ||
        !Mode->GetPlayerFighter()->IsAlive() || !Mode->GetBossFighter()->IsAlive()) return;
    // A retry starts a new encounter; repeated ticks of the same mechanic cannot extend the hint.
    if (MechanicHintEncounter != Mode->DuelStartedAt || MechanicHintBattle != Battle)
    {
        MechanicHintEncounter = Mode->DuelStartedAt;
        MechanicHintBattle = Battle;
        bMechanicHintShown = false;
        MechanicHintLine.Reset();
    }
    if (bMechanicHintShown) return;
    if (Battle == 1)
    {
        MechanicHintLine = FString::Printf(TEXT("镜里的%s也会出招，打到镜面的弹还会弹回来，我得躲开再找本体！"),
            Mode->GetBossFighter()->CharacterName());
    }
    else if (Battle == 0)
    {
        const TCHAR* Attack = Mode->GetBossFighter()->SelectedCharacter() == EDuelCharacter::Ema ? TEXT("骨翼") : TEXT("影兽或飞矛");
        MechanicHintLine = FString::Printf(TEXT("%s的咒力系在悬链上！站到链后引%s撞链，亮线停住后横向闪开，断链后攻击仍会飞来；两链都断再反击！"),
            Mode->GetBossFighter()->CharacterName(), Attack);
    }
    else
    {
        MechanicHintLine = FString::Printf(TEXT("%s有护罩，先打金光双环标出的判印！依次破开三枚再反击；打错或超时会封腿1秒、封武器5秒。"),
            Mode->GetBossFighter()->CharacterName());
    }
    bMechanicHintShown = true;
    MechanicHintStartedAt = GetWorld()->GetTimeSeconds();
    if (NarrativeActor) NarrativeActor->PreloadThoughtPortrait(Mode->GetPlayerFighter()->SelectedCharacter());
}

void ADuelNorthTower::UpdateMechanicHint(const ADuelMode&)
{
    // Arena controllers trigger the one hint when a real court interaction starts, never on an unrelated move.
}

FStringView ADuelNorthTower::MechanicHintText(float& Age, float& Opacity) const
{
    Age = Opacity = 0.f;
    const auto* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    if (!bMechanicHintShown || !Mode || Mode->Outcome() != EDuelOutcome::Fighting ||
        ActiveFight != MechanicHintBattle || Mode->DuelStartedAt != MechanicHintEncounter ||
        !Mode->GetPlayerFighter() || !Mode->GetBossFighter() ||
        !Mode->GetPlayerFighter()->IsAlive() || !Mode->GetBossFighter()->IsAlive()) return {};
    Age = FMath::Max(0.f, float(GetWorld()->GetTimeSeconds() - MechanicHintStartedAt));
    if (Age >= 8.f) return {};
    Opacity = FMath::SmoothStep(0.f, .2f, Age) * (1.f - FMath::SmoothStep(7.2f, 8.f, Age));
    return MechanicHintLine;
}

UTexture* ADuelNorthTower::MechanicHintPortrait() const
{
    const auto* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    return NarrativeActor && Mode && Mode->GetPlayerFighter() ?
        NarrativeActor->ThoughtPortrait(Mode->GetPlayerFighter()->SelectedCharacter()) : nullptr;
}

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthMechanicHintsTest, "ManosabaDuel.NorthTower.MechanicHints",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthMechanicHintsTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    if (!TestTrue(TEXT("Mechanic hints run in an isolated save-free tower"),
        FParse::Param(FCommandLine::Get(), TEXT("DuelNorthTower")) &&
        FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("DuelNorthSaveTest")))) return false;
    ADuelMode* Mode = nullptr;
    for (const auto& Context : GEngine->GetWorldContexts())
        if (Context.World() && (Mode = Context.World()->GetAuthGameMode<ADuelMode>())) break;
    if (!TestTrue(TEXT("Actual tower is idle and unpaused"),
        Mode && Mode->NorthTowerActor && Mode->Outcome() != EDuelOutcome::Fighting &&
        !Mode->NorthTowerActor->BlocksPlayerInput() && !UGameplayStatics::IsGamePaused(Mode))) return false;
    auto* World = Mode->GetWorld();
    auto* Tower = Mode->NorthTowerActor.Get();
    const auto SavedNarrative = Tower->NarrativeActor;
    const auto SavedFight = Tower->ActiveFight, SavedHintBattle = Tower->MechanicHintBattle;
    const auto SavedHintEncounter = Tower->MechanicHintEncounter, SavedHintAt = Tower->MechanicHintStartedAt;
    const auto SavedHintShown = Tower->bMechanicHintShown;
    const auto SavedHintLine = Tower->MechanicHintLine;
    auto* Narrative = World->SpawnActor<ADuelNorthNarrative>();
    auto* Player = World->SpawnActor<ADuelFighter>();
    auto* Boss = World->SpawnActor<ADuelFighter>();
    const auto SavedPlayer = Mode->PlayerFighter, SavedBoss = Mode->BossFighter;
    const auto SavedOutcome = Mode->Result;
    const auto SavedStartedAt = Mode->DuelStartedAt;
    ON_SCOPE_EXIT
    {
        Mode->PlayerFighter = SavedPlayer;
        Mode->BossFighter = SavedBoss;
        Mode->Result = SavedOutcome;
        Mode->DuelStartedAt = SavedStartedAt;
        Tower->NarrativeActor = SavedNarrative;
        Tower->ActiveFight = SavedFight;
        Tower->MechanicHintBattle = SavedHintBattle;
        Tower->MechanicHintEncounter = SavedHintEncounter;
        Tower->MechanicHintStartedAt = SavedHintAt;
        Tower->bMechanicHintShown = SavedHintShown;
        Tower->MechanicHintLine = SavedHintLine;
        if (IsValid(Narrative)) Narrative->Destroy();
        if (IsValid(Player)) Player->Destroy();
        if (IsValid(Boss)) Boss->Destroy();
    };
    if (!TestTrue(TEXT("Independent hint fixtures spawn"), Tower && Narrative && Player && Boss)) return false;
    for (auto* Figure : {Player, Boss})
    {
        Figure->SetActorHiddenInGame(true);
        Figure->SetActorEnableCollision(false);
        Figure->SetActorTickEnabled(false);
    }
    Mode->PlayerFighter = Player; Mode->BossFighter = Boss; Mode->Result = EDuelOutcome::Fighting;
    Tower->NarrativeActor = Narrative;
    float Age = 0.f, Alpha = 0.f;
    for (const auto Route : {EDuelCharacter::Hiro, EDuelCharacter::Ema})
    {
        Player->Character = Route;
        for (const auto Identity : {EDuelCharacter::Ema, EDuelCharacter::Hiro, EDuelCharacter::Yuki})
        {
            Boss->Character = Identity;
            Boss->Move = EDuelMove::Idle;
            Tower->ActiveFight = Identity == EDuelCharacter::Yuki ? 2 : 0;
            Mode->DuelStartedAt += 1.;
            Tower->UpdateMechanicHint(*Mode);
            TestTrue(TEXT("No hint appears before the actual key move"), Tower->MechanicHintText(Age, Alpha).IsEmpty());
            Boss->Move = Identity == EDuelCharacter::Ema ? EDuelMove::BoneProcession :
                Identity == EDuelCharacter::Hiro ? EDuelMove::HoundShade : EDuelMove::GlassShelter;
            Tower->UpdateMechanicHint(*Mode);
            TestTrue(TEXT("Old ordinary moves cannot consume the new interaction hint"), Tower->MechanicHintText(Age, Alpha).IsEmpty());
            Tower->ShowMechanicHint(Tower->ActiveFight);
            Tower->MechanicHintStartedAt -= .5;
            const FString Text(Tower->MechanicHintText(Age, Alpha));
            TestTrue(TEXT("The actual boss identity selects the matching plain-language hint"),
                Text.Contains(Boss->CharacterName()) && (Identity == EDuelCharacter::Yuki ?
                    Text.Contains(TEXT("判印")) && Text.Contains(TEXT("三枚")) && Text.Contains(TEXT("封腿1秒")) && Text.Contains(TEXT("封武器5秒")) :
                    Text.Contains(TEXT("悬链")) && Text.Contains(TEXT("横向闪开")) && Text.Contains(TEXT("仍会飞来")) && !Text.Contains(TEXT("按F")) &&
                    Text.Contains(Identity == EDuelCharacter::Ema ? TEXT("骨翼") : TEXT("影兽"))));
            TestTrue(TEXT("A hint fades in while gameplay remains unpaused and accepts input"),
                Alpha > 0.f && !UGameplayStatics::IsGamePaused(World) &&
                !Narrative->BlocksInput() && !Tower->BlocksPlayerInput());
            TestTrue(TEXT("Hint portrait belongs to the current protagonist"),
                Tower->MechanicHintPortrait() == Narrative->ThoughtPortrait(Route));
            const double Began = Tower->MechanicHintStartedAt;
            Tower->UpdateMechanicHint(*Mode);
            TestEqual(TEXT("Repeated move ticks cannot restart or extend a hint"), Tower->MechanicHintStartedAt, Began);
            Tower->MechanicHintStartedAt -= 9.;
            Tower->ShowMechanicHint(Tower->ActiveFight);
            TestTrue(TEXT("Expired hint stays expired in the same fight"), Tower->MechanicHintText(Age, Alpha).IsEmpty());
            Mode->DuelStartedAt += 1.;
            Tower->UpdateMechanicHint(*Mode);
            Tower->ShowMechanicHint(Tower->ActiveFight);
            TestFalse(TEXT("A fresh retry can show its first key hint again"), Tower->MechanicHintText(Age, Alpha).IsEmpty());
        }
        for (const auto Identity : {EDuelCharacter::Ema, EDuelCharacter::Hiro})
        {
            Boss->Character = Identity;
            Boss->Move = Identity == EDuelCharacter::Ema ? EDuelMove::BoneProcession : EDuelMove::HoundShade;
            Tower->ActiveFight = 1; Mode->DuelStartedAt += 1.;
            Tower->UpdateMechanicHint(*Mode);
            TestTrue(TEXT("Mirror battle reserves its one hint for actual fog-mirror formation"),
                Tower->MechanicHintText(Age, Alpha).IsEmpty());
            Tower->ShowMechanicHint(0);
            TestTrue(TEXT("An event for another court cannot consume the mirror hint"),
                Tower->MechanicHintText(Age, Alpha).IsEmpty());
            Tower->ShowMechanicHint(1);
            const FString Text(Tower->MechanicHintText(Age, Alpha));
            TestTrue(TEXT("Fog-mirror hint names the real mirrored boss and explains reflection"),
                Text.Contains(Boss->CharacterName()) && Text.Contains(TEXT("镜面")) && Text.Contains(TEXT("弹回来")));
            Mode->Result = EDuelOutcome::Defeat;
            TestTrue(TEXT("Battle outcome hides the hint immediately"), Tower->MechanicHintText(Age, Alpha).IsEmpty());
            Mode->Result = EDuelOutcome::Fighting;
        }
    }
    return !HasAnyErrors();
}

#endif
