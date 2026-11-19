#include "DuelGame.h"

namespace
{
    float VisibleSeconds(float Value)
    {
        return FMath::IsFinite(Value) ? FMath::Max(0.f, Value) : 0.f;
    }

    FString Countdown(float Value)
    {
        const float Seconds = VisibleSeconds(Value);
        return Seconds > 0.f ? FString::Printf(TEXT("%.1f 秒"), FMath::CeilToInt(Seconds * 10.f) / 10.f) : TEXT("已恢复");
    }

    struct FJudgmentLayout
    {
        float Scale, X, Y, Width, Height, LabelSize, TimerSize;
    };

    FJudgmentLayout LockLayout(float Width, float Scale)
    {
        const float ReadableScale = FMath::Max(.75f, Scale);
        return {ReadableScale, 24.f * Scale, 154.f * Scale, FMath::Min(360.f * ReadableScale, Width * .44f),
            120.f * ReadableScale, FMath::Max(16.f, 18.f * ReadableScale), FMath::Max(18.f, 22.f * ReadableScale)};
    }
}

float ADuelHUD::DrawNorthJudgmentHUD(const ADuelMode& Mode, float W, float H, float S)
{
    (void)H;
    const auto* Player = Mode.GetPlayerFighter();
    const auto* Boss = Mode.GetBossFighter();
    if (Mode.Outcome() != EDuelOutcome::Fighting || !Player || !Boss || !Player->IsAlive() || !Boss->IsAlive()) return 0.f;

    const FLinearColor Ink(.016f, .019f, .031f, .94f), Paper(.94f, .89f, .78f, 1.f);
    const FLinearColor Gold(.92f, .69f, .38f, 1.f), Red(.98f, .43f, .37f, 1.f), Ready(.61f, .87f, .74f, 1.f);
    const float Move = VisibleSeconds(Player->JudgmentMoveLockSeconds());
    const float Weapon = VisibleSeconds(Player->JudgmentWeaponLockSeconds());
    const auto Layout = LockLayout(W, S);
    const float R = Layout.Scale;
    if (Boss->JudgmentImmune())
    {
        // Below the quiet objective strip, away from both characters' health bars and the combat center.
        const float Width = FMath::Min(360.f * R, W * .43f), X = W - Width - 28.f * S, Y = 105.f * S;
        const float Font = FMath::Max(14.f, 16.f * R), Pitch = Font + 5.f * R;
        const auto Lines = WrapLabel(TEXT("先打带金光和双环的判印"), Font, Width - 28.f * R);
        const float Height = 39.f * R + Lines.Num() * Pitch;
        DrawRect(Ink, X, Y, Width, Height);
        DrawRect(Gold, X, Y, FMath::Max(2.f, 2.f * R), Height);
        Label(TEXT("判决护罩 · 暂时无敌"), X + 14.f * R, Y + 10.f * R, FMath::Max(15.f, 18.f * R), Gold);
        for (int32 I = 0; I < Lines.Num(); ++I) Label(Lines[I], X + 14.f * R, Y + 36.f * R + I * Pitch, Font, Paper);
    }
    if (Move <= 0.f && Weapon <= 0.f) return 0.f;

    DrawRect(Ink, Layout.X, Layout.Y, Layout.Width, Layout.Height);
    DrawRect(Red, Layout.X, Layout.Y, FMath::Max(2.f, 2.f * R), Layout.Height);
    Label(TEXT("判决反噬"), Layout.X + 14.f * R, Layout.Y + 9.f * R, FMath::Max(14.f, 16.f * R), Gold);
    const auto Row = [&](FStringView Name, float Seconds, float Duration, float Top)
    {
        const FLinearColor Color = Seconds > 0.f ? Red : Ready;
        const FString Text = Countdown(Seconds);
        Label(Name, Layout.X + 14.f * R, Layout.Y + Top * R, Layout.LabelSize, Paper);
        Label(Text, Layout.X + Layout.Width - 14.f * R - LabelWidth(Text, Layout.TimerSize),
            Layout.Y + (Top - 1.f) * R, Layout.TimerSize, Color);
        const float BarY = Layout.Y + (Top + 28.f) * R;
        DrawRect(Paper.CopyWithNewOpacity(.16f), Layout.X + 14.f * R, BarY, Layout.Width - 28.f * R, 2.f * R);
        DrawRect(Color, Layout.X + 14.f * R, BarY, (Layout.Width - 28.f * R) * FMath::Clamp(Seconds / Duration, 0.f, 1.f), 2.f * R);
    };
    Row(TEXT("移动"), Move, 1.f, 35.f);
    Row(TEXT("武器 / 战技 / 施法"), Weapon, 5.f, 76.f);
    return Layout.Height + 8.f * R;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthJudgmentHUDTest, "ManosabaDuel.NorthTower.JudgmentHUD347",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthJudgmentHUDTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    TestEqual(TEXT("A full weapon lock has a readable five-second value"), Countdown(5.f), FString(TEXT("5.0 秒")));
    TestEqual(TEXT("A positive movement lock cannot look expired through rounding"), Countdown(.01f), FString(TEXT("0.1 秒")));
    TestEqual(TEXT("Movement recovery stays explicit while the separate weapon timer remains"), Countdown(0.f), FString(TEXT("已恢复")));
    TestEqual(TEXT("Negative leftover time never appears on screen"), Countdown(-.1f), FString(TEXT("已恢复")));
    for (const auto Size : {FVector2D(888, 500), FVector2D(1280, 720), FVector2D(1600, 900), FVector2D(1920, 1080)})
    {
        const auto L = LockLayout(Size.X, FMath::Min(Size.X / 1600.f, Size.Y / 900.f));
        TestTrue(TEXT("Countdowns stay readable at low and normal output resolutions"), L.TimerSize >= 18.f && L.LabelSize >= 16.f);
        TestTrue(TEXT("The lock card stays inside the left column below player meters"),
            L.X >= 0.f && L.X + L.Width < Size.X * .5f && L.Y + L.Height < Size.Y * .5f);
    }
    return !HasAnyErrors();
}
#endif
