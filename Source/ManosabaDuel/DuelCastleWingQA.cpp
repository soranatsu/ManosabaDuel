#include "DuelCastle.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CollisionShape.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelCastleWingWalk, "ManosabaDuel.CastleWingWalk",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelCastleWingWalk::RunTest(const FString& Parameters)
{
    ADuelMode* Game = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        if (Context.World() && (Game = Context.World()->GetAuthGameMode<ADuelMode>()))
        {
            break;
        }
    }
    if (!TestTrue(TEXT("Launch the isolated wing walk with -DuelCastleRoute -DuelNoSave"),
        FParse::Param(FCommandLine::Get(), TEXT("DuelCastleRoute")) &&
        FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")) &&
        Game && Game->UsesCastle() && Game->GetPlayerFighter() && Game->CastleMarks() == 0 &&
        !Game->KnowsSpell(EDuelArt::SaltArc) && !Game->KnowsSpell(EDuelArt::LanternMote)))
    {
        return false;
    }

    enum class EAction { Walk, Key, Clue, Fog, Fight, Plaque, Reset };
    struct FStep
    {
        FVector Feet;
        EAction Action = EAction::Walk;
        int32 Wing = INDEX_NONE;
    };
    const auto AppendCourt = [](TArray<FStep>& Steps, int32 Wing)
    {
        const FVector Court = DuelCastle::Wings[Wing].Court;
        Steps.Add({DuelCastle::Gate(Wing), EAction::Fog, Wing});
        Steps.Add({Court + FVector(DuelCourt::GateCrossX + 95.f, 0, 0), EAction::Fight, Wing});
        Steps.Add({Court + FVector(690, 0, 0)});
        Steps.Add({DuelCastle::Record(Wing) + FVector(-155, -25, 0), EAction::Plaque, Wing});
    };

    const FVector Hub = DuelCastle::Hub;
    const FVector WestHub = Hub + FVector(-900, -43, 0);
    const FVector WestRamp(-2855, -5050, 184);
    const FVector WestBottom(-5700, -5449, -300);
    const FVector WestJunction(-8560, -5450, -300);
    const FVector EastHub = Hub + FVector(1300, 220, 0);
    const FVector EscortBend(2930, -4430, 184);
    const FVector EscortKey(4280, -4280, 205);
    const FVector FurnaceKey(-460, -8922, -320);
    const FVector FurnaceKeyEast(-160, -8940, -320);

    // The outer boiler alcove holds the first inner-door key. The checkpoint resets only the walking origin.
    TArray<FStep> Route{
        {Hub}, {WestHub}, {WestRamp}, {WestBottom}, {WestJunction},
        {{-8760, -7590, -314}}, {{-6235, -9725, -345}}, {{-4110, -10450, -320}},
        {{-2850, -9485, -320}}, {{-1600, -9260, -320}}, {{-640, -8970, -320}},
        {FurnaceKey, EAction::Key, 2}, {Hub, EAction::Reset},

        {Hub}, {WestHub}, {WestRamp}, {WestBottom}, {WestJunction},
        {{-11160, -4760, -300}}, {{-11165, -2490, -300}, EAction::Key, 0},
        {{-11005, -2490, -300}}, {{-11005, -2195, -300}},
        {{-10020, -885, -300}, EAction::Clue, 0}, {{-10010, -510, -300}}, {{-9620, 0, -300}},
        {{-8550, 0, -300}}, {{-8380, 0, -300}}, {{-7900, 0, -148}}, {{-7420, 0, 0}},
        {{-7130, 530, 0}}, {{-7065, 705, 0}, EAction::Clue, 0}, {{-6700, 0, 0}},
        {{-6050, 0, 0}}, {{-2535, 0, 0}}
    };
    AppendCourt(Route, 0);
    const TArray<FStep> Escort{
        {Hub, EAction::Reset}, {Hub}, {EastHub}, {EscortBend}, {EscortKey, EAction::Key, 1},
        {{4350, -4400, 205}}, {{4610, -4415, 205}}, {{6040, -4620, 205}, EAction::Clue, 1},
        {{6095, -4450, 205}}, {{6290, -4485, 205}}, {{7150, -3420, 205}}, {{7000, -1490, 205}},
        {{7110, -800, 205}}, {{7110, -525, 205}}, {{7335, -376, 205}, EAction::Clue, 1},
        {{7240, -180, 205}}, {{7110, 0, 205}}, {{7940, 0, 205}}
    };
    Route.Append(Escort);
    AppendCourt(Route, 1);
    const TArray<FStep> Furnace{
        {Hub, EAction::Reset}, {Hub}, {EastHub}, {EscortBend}, {EscortKey},
        {{4350, -4400, 205}}, {{4610, -4415, 205}}, {{6020, -4780, 205}}, {{6240, -4845, 205}},
        {{6610, -6500, 205}}, {{6160, -8220, -45}}, {{3850, -8560, -320}}, {{1600, -9260, -320}},
        {{1120, -9400, -320}}, {{900, -9475, -320}}, {{900, -9615, -320}}, {FurnaceKeyEast},
        {{-205, -9065, -320}}, {{-540, -9070, -320}}, {{-1440, -9240, -320}},
        {{-2700, -9325, -320}, EAction::Clue, 2}, {{-1440, -9240, -320}},
        {{-540, -9070, -320}}, {{-205, -9065, -320}}, {{900, -9615, -320}},
        {{735, -10870, -320}, EAction::Clue, 2}, {{900, -11040, -320}}, {{1045, -11180, -320}},
        {{1650, -11290, -320}}
    };
    Route.Append(Furnace);
    AppendCourt(Route, 2);

    Game->ReturnToCheckpoint();
    const TWeakObjectPtr<ADuelMode> World(Game);
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, World, Route, Index = 0, Due = 0.0]() mutable
    {
        ADuelMode* Mode = World.Get();
        if (!Mode || !Mode->GetPlayerFighter())
        {
            AddError(TEXT("The castle unloaded during the wing walk"));
            return true;
        }
        ADuelFighter* Player = const_cast<ADuelFighter*>(Mode->GetPlayerFighter());
        const FStep& Step = Route[Index];
        const FVector At = Player->GetActorLocation();
        const FVector Offset = Step.Feet + FVector(0, 0, 62) - At;
        const double Now = Mode->GetWorld()->GetTimeSeconds();
        if (Due == 0.0)
        {
            Due = Now + 5.5 + Offset.Size2D() / 200.0;
        }

        const bool bEvidence = Step.Action == EAction::Key || Step.Action == EAction::Clue;
        const double Reach = bEvidence ? 21.0 : Step.Action == EAction::Walk ? 56.0 : 42.0;
        if (Step.Action != EAction::Reset && (Offset.Size2D() > Reach || FMath::Abs(Offset.Z) >= 106.f))
        {
            if (Now > Due)
            {
                FHitResult Obstacle;
                const FCollisionQueryParams Query(SCENE_QUERY_STAT(CastleWingWalkObstacle), false, Player);
                Mode->GetWorld()->SweepSingleByChannel(Obstacle, At, At + Offset.GetSafeNormal2D() * 132.f,
                    FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(34.f, 60.f), Query);
                AddError(FString::Printf(TEXT("Wing walk stopped at step %d near %s toward %s. Obstacle %s at %s"),
                    Index, *At.ToString(), *Step.Feet.ToString(), *GetNameSafe(Obstacle.GetComponent()),
                    *Obstacle.ImpactPoint.ToString()));
                return true;
            }
            Player->AddMovementInput(Offset.GetSafeNormal2D(), FMath::Clamp(Offset.Size2D() / 150.0, .34, 1.0));
            return false;
        }

        bool bSucceeded = true;
        switch (Step.Action)
        {
        case EAction::Walk:
            break;
        case EAction::Key:
        case EAction::Clue:
        {
            const TCHAR* Prompt = Mode->CastlePrompt();
            const TCHAR* Previous = Mode->ActiveInvestigation();
            Mode->Confirm();
            const TCHAR* Read = Mode->ActiveInvestigation();
            const TCHAR* After = Mode->CastlePrompt();
            bSucceeded = Prompt && FStringView(Prompt).StartsWith(TEXT("F   ")) && Read && Read != Previous &&
                (Step.Action != EAction::Key || Prompt != After);
            break;
        }
        case EAction::Fog:
        {
            const FCollisionQueryParams Door(SCENE_QUERY_STAT(CastleWingFog), false, Player);
            const FVector Center = DuelCastle::Wings[Step.Wing].Court + FVector(DuelCourt::GateX, 0, 65);
            const auto Blocked = [&]
            {
                return Mode->GetWorld()->SweepTestByChannel(Center - FVector(96, 0, 0), Center + FVector(96, 0, 0),
                    FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(34.f, 60.f), Door);
            };
            const bool bInitiallyClosed = !Mode->GateOpen() && Blocked();
            Mode->Confirm();
            bSucceeded = bInitiallyClosed && Mode->GateOpen() && !Blocked();
            break;
        }
        case EAction::Fight:
        {
            const ADuelFighter* Boss = Mode->GetBossFighter();
            bSucceeded = Mode->Outcome() == EDuelOutcome::Fighting && Boss && Mode->InAppeal() == (Step.Wing == 1) &&
                (Boss->SelectedCharacter() == Player->SelectedCharacter()) == (Step.Wing != 1);
            if (bSucceeded)
            {
                Mode->FighterFell(Boss);
                bSucceeded = Mode->Outcome() == EDuelOutcome::Victory;
            }
            break;
        }
        case EAction::Plaque:
            Mode->Confirm();
            bSucceeded = Mode->CastleMarks() == Step.Wing + 1 && Mode->Outcome() == EDuelOutcome::Ready;
            if (bSucceeded)
            {
                AddInfo(FString::Printf(TEXT("Wing %d reached its locked fog on foot, crossed it, and read the verdict"),
                    Step.Wing + 1));
            }
            break;
        case EAction::Reset:
            Mode->ReturnToCheckpoint();
            bSucceeded = Player->GetActorLocation().Equals(DuelCastle::Spawn.GetLocation(), 4.f);
            break;
        }

        if (!bSucceeded)
        {
            const TCHAR* Prompt = Mode->CastlePrompt();
            AddError(FString::Printf(TEXT("Wing walk action %d failed at step %d, wing %d, position %s. Gate %d, outcome %d, marks %d, prompt %s"),
                static_cast<int32>(Step.Action), Index, Step.Wing + 1, *At.ToString(), Mode->GateOpen(),
                static_cast<int32>(Mode->Outcome()), Mode->CastleMarks(), Prompt ? Prompt : TEXT("none")));
            return true;
        }
        Due = 0.0;
        return ++Index == Route.Num();
    }));
    return true;
}

#endif
