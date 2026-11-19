#include "DuelGame.h"

#include "CanvasItem.h"
#include "DuelNorthProgress.h"
#include "Engine/Canvas.h"
#include "GlobalRenderResources.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
    const FLinearColor MenuInk(.009f, .012f, .019f, .95f);
    const FLinearColor MenuPaper(.92f, .86f, .74f);
    const FLinearColor MenuGold(.93f, .68f, .35f);
    const FLinearColor MenuQuiet(.66f, .68f, .71f);
    const FLinearColor MenuRule(.63f, .44f, .25f, .36f);
}

void ADuelHUD::FadeBand(float X, float Y, float W, float H, const FLinearColor& Left, const FLinearColor& Right)
{
    if (W <= 0.f || H <= 0.f) return;
    const FVector2D TL(X, Y), TR(X + W, Y), BL(X, Y + H), BR(X + W, Y + H);
    FCanvasTriangleItem Band(TL, TR, BR, GWhiteTexture);
    Band.TriangleList[0].V0_Color = Left;
    Band.TriangleList[0].V1_Color = Right;
    Band.TriangleList[0].V2_Color = Right;
    FCanvasUVTri& Lower = Band.TriangleList.AddDefaulted_GetRef();
    Lower.V0_Pos = TL;
    Lower.V1_Pos = BR;
    Lower.V2_Pos = BL;
    Lower.V0_UV = Lower.V1_UV = Lower.V2_UV = FVector2D::ZeroVector;
    Lower.V0_Color = Lower.V2_Color = Left;
    Lower.V1_Color = Right;
    Band.BlendMode = SE_BLEND_Translucent;
    Canvas->DrawItem(Band);
}

void ADuelHUD::FocusBand(float X, float Y, float W, float H, float S)
{
    FadeBand(X, Y, W, H, FLinearColor(.40f, .24f, .105f, .54f), FLinearColor(.40f, .24f, .105f, .08f));
    const float Rule = FMath::Max(1.f, S);
    FadeBand(X, Y, W, Rule, MenuGold.CopyWithNewOpacity(.64f), MenuGold.CopyWithNewOpacity(0.f));
    FadeBand(X, Y + H - Rule, W, Rule, MenuGold.CopyWithNewOpacity(.40f), MenuGold.CopyWithNewOpacity(0.f));
    DrawRect(MenuGold, X, Y + 3.f * S, 2.f * S, H - 6.f * S);
}

float ADuelHUD::KeyHint(float X, float Y, float S, FStringView Key, FStringView Action)
{
    const float Font = 13.f * S;
    const float Width = FMath::Max(23.f * S, LabelWidth(Key, Font) + 12.f * S);
    const float Rule = FMath::Max(1.f, S);
    DrawRect(FLinearColor(.071f, .058f, .049f, .92f), X, Y, Width, 22.f * S);
    DrawRect(MenuRule, X, Y + 22.f * S - Rule, Width, Rule);
    Label(Key, X + Width * .5f, Y + 4.f * S, Font, MenuGold, true);
    Label(Action, X + Width + 8.f * S, Y + 4.f * S, Font, MenuPaper);
    return Width + LabelWidth(Action, Font) + 27.f * S;
}

void ADuelHUD::DrawTitle(const ADuelMode& Mode, float W, float H, float S)
{
    S = FMath::Min(S, FMath::Min(W / 710.f, H / 566.f));
    const float Left = 64.f * S;
    const float Top = FMath::Max(40.f * S, H * .5f - 225.f * S);
    const float Width = 383.f * S;
    const bool bNorth = Mode.UsesNorthTower();
    const bool bContinue = Mode.CanContinue();
    DrawRect(MenuInk.CopyWithNewOpacity(.25f), 0, 0, W, H);
    FadeBand(0, 0, FMath::Min(W, 660.f * S), H, MenuInk, MenuInk.CopyWithNewOpacity(0.f));
    Chrome(13, Left, Top + 2.f * S, 17.f * S, 17.f * S, MenuGold);
    FadeBand(Left + 26.f * S, Top + 10.f * S, Width - 26.f * S, FMath::Max(1.f, S),
        MenuRule, MenuRule.CopyWithNewOpacity(0.f));
    Label(bNorth ? TEXT("魔女岛") : TEXT("魔女审判"), Left, Top + 31.f * S, 75.f * S, MenuPaper);
    Label(bNorth ? TEXT("监狱北塔 · 带她们离开") : TEXT("缺页证言"), Left + 2.f * S, Top + 118.f * S, 32.f * S, MenuGold);
    FadeBand(Left, Top + 164.f * S, Width, FMath::Max(1.f, S), MenuGold.CopyWithNewOpacity(.58f),
        MenuGold.CopyWithNewOpacity(0.f));

    const TCHAR* Actions[] = {bNorth ? TEXT("继续旅程") : TEXT("继续审判"),
        bNorth ? TEXT("踏入北塔") : TEXT("开始新审判"), TEXT("退出")};
    float RowY = Top + 181.f * S;
    for (int32 I = bContinue ? 0 : 1; I < UE_ARRAY_COUNT(Actions); ++I)
    {
        const bool bHover = MenuButton(I, Left, RowY, Width, 39.f * S);
        const bool bFocused = bHover || Mode.Choice() == I;
        if (bFocused) FocusBand(Left, RowY, Width, 39.f * S, S);
        Chrome(13, Left + 11.f * S, RowY + 13.f * S, 12.f * S, 12.f * S,
            bFocused ? MenuGold : MenuRule);
        Label(Actions[I], Left + 34.f * S, RowY + 8.f * S, 22.f * S, bFocused ? MenuGold : MenuPaper);
        RowY += 44.f * S;
    }
    if (bNorth)
    {
        const auto Hero = Mode.GetPlayerFighter()->SelectedCharacter();
        Label(TEXT("主角"), Left + 1.f * S, RowY + 13.f * S, 14.f * S, MenuQuiet);
        for (int32 I = 0; I < 2; ++I)
        {
            const float X = Left + (55.f + I * 99.f) * S;
            const bool bHover = MenuButton(3 + I, X, RowY + 4.f * S, 91.f * S, 35.f * S);
            const bool bEquipped = Hero == (I == 0 ? EDuelCharacter::Ema : EDuelCharacter::Hiro);
            DrawRect(MenuInk.CopyWithNewOpacity(.84f), X, RowY + 4.f * S, 91.f * S, 35.f * S);
            if (bHover || bEquipped) FocusBand(X, RowY + 4.f * S, 91.f * S, 35.f * S, S);
            Label(I == 0 ? TEXT("艾玛") : TEXT("希罗"), X + 46.f * S, RowY + 13.f * S, 17.f * S,
                bHover || bEquipped ? MenuGold : MenuPaper, true);
        }
    }
    const float FooterY = H - 71.f * S;
    float KeyX = Left;
    KeyX += KeyHint(KeyX, FooterY, S, TEXT("W / S"), TEXT("选择"));
    if (bNorth && Mode.Choice() == 1) KeyX += KeyHint(KeyX, FooterY, S, TEXT("A / D"), TEXT("主角"));
    KeyHint(KeyX, FooterY, S, TEXT("F"), TEXT("确认"));
    Label(TEXT("《魔法少女的魔女审判》非官方二创"), Left, H - 35.f * S, 13.f * S, MenuQuiet);
    static const FString Version = []
    {
        FString Value;
        GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), Value, GGameIni);
        return Value;
    }();
    Label(Version, W - 34.f * S - LabelWidth(Version, 13.f * S), H - 35.f * S, 13.f * S, MenuQuiet);
}

void ADuelHUD::DrawPause(const ADuelMode& Mode, float W, float H, float S)
{
    const int32 Count = Mode.PauseOptionCount();
    S = FMath::Min(S, FMath::Min(W / 510.f, H / (214.f + Count * 39.f)));
    const float X = 48.f * S, Width = 356.f * S;
    const float Height = (139.f + Count * 39.f) * S;
    const float Y = (H - Height) * .5f;
    Panel(X, Y, Width, Height, S);
    Label(TEXT("暂停"), X + 24.f * S, Y + 20.f * S, 34.f * S, MenuPaper);
    if (Mode.Outcome() == EDuelOutcome::Ready)
    {
        Label(Mode.AreaName(), X + 25.f * S, Y + 62.f * S, 14.f * S, MenuGold);
    }
    for (int32 I = 0; I < Count; ++I)
    {
        const float Row = Y + (87.f + I * 39.f) * S;
        const float RowX = X + 14.f * S;
        const float RowW = Width - 28.f * S;
        const bool bHover = MenuButton(I, RowX, Row, RowW, 35.f * S);
        const bool bFocused = I == Mode.Choice() || bHover;
        if (bFocused) FocusBand(RowX, Row, RowW, 35.f * S, S);
        FString Primary, Value;
        switch (Mode.PauseOption(I))
        {
        case EDuelPause::Resume: Primary = TEXT("继续旅程"); break;
        case EDuelPause::Player: Primary = TEXT("更换主角"); Value = Mode.GetPlayerFighter()->CharacterName(); break;
        case EDuelPause::Boss: Primary = TEXT("更换首领"); Value = Mode.GetBossFighter()->CharacterName(); break;
        case EDuelPause::Loadout: Primary = TEXT("武器与法术"); break;
        case EDuelPause::Inventory: Primary = TEXT("物品"); break;
        case EDuelPause::Controls: Primary = TEXT("按键说明"); break;
        case EDuelPause::Graphics: Primary = TEXT("图像与声音"); break;
        case EDuelPause::Checkpoint: Primary = TEXT("返回休整点"); break;
        case EDuelPause::Subtitles: Primary = TEXT("战斗语音字幕"); Value = Mode.EffortSubtitles() ? TEXT("开") : TEXT("关"); break;
        case EDuelPause::Title: Primary = TEXT("返回主菜单"); break;
        case EDuelPause::Quit: Primary = TEXT("退出游戏"); break;
        }
        Chrome(13, RowX + 10.f * S, Row + 12.f * S, 11.f * S, 11.f * S, bFocused ? MenuGold : MenuRule);
        Label(Primary, RowX + 31.f * S, Row + 8.f * S, 18.f * S, bFocused ? MenuGold : MenuPaper);
        Label(Value, X + Width - 26.f * S - LabelWidth(Value, 15.f * S), Row + 9.f * S, 15.f * S, MenuQuiet);
    }
    const float Footer = Y + Height - 39.f * S;
    float KeyX = X + 20.f * S;
    KeyX += KeyHint(KeyX, Footer, S, TEXT("W / S"), TEXT("选择"));
    KeyX += KeyHint(KeyX, Footer, S, TEXT("F"), TEXT("确认"));
    KeyHint(KeyX, Footer, S, TEXT("Esc"), TEXT("返回"));
}

void ADuelHUD::DrawSettings(const ADuelMode& Mode, float W, float H, float S)
{
    constexpr int32 Rows = static_cast<int32>(EDuelGraphicsRow::Count);
    const TCHAR* Labels[Rows] = {TEXT("TSR 渲染比例"), TEXT("Lumen 光线追踪"), TEXT("场景品质"),
        TEXT("主音量"), TEXT("战斗音效"), TEXT("语音"), TEXT("背景音乐")};
    const FDuelGraphicsSettings& Graphics = Mode.Graphics();
    S = FMath::Min(S, FMath::Min(W / 690.f, H / 580.f));
    const float Width = 638.f * S, Height = 533.f * S;
    const float X = (W - Width) * .5f, Y = (H - Height) * .5f;
    Panel(X, Y, Width, Height, S);
    Label(TEXT("图像与声音"), X + 29.f * S, Y + 19.f * S, 32.f * S, MenuPaper);
    DrawMenuBack(X + Width - 119.f * S, Y + 17.f * S, 91.f * S, 32.f * S, S, TEXT("Esc 返回"));
    const int32 FirstAudio = static_cast<int32>(EDuelGraphicsRow::Master);
    for (int32 I = 0; I < Rows; ++I)
    {
        const auto Row = static_cast<EDuelGraphicsRow>(I);
        const bool bAudio = I >= FirstAudio;
        const float RowY = Y + (97.f + I * 45.f + (bAudio ? 26.f : 0.f)) * S;
        if (I == 0 || I == FirstAudio)
        {
            Label(bAudio ? TEXT("声音") : TEXT("图像"), X + 29.f * S, RowY - 27.f * S, 14.f * S, MenuGold);
            FadeBand(X + 76.f * S, RowY - 19.f * S, Width - 105.f * S, FMath::Max(1.f, S), MenuRule, MenuRule.CopyWithNewOpacity(0.f));
        }
        const float MinusX = X + 342.f * S, PlusX = X + 565.f * S;
        const bool bEnabled = Graphics.CanAdjust(Row);
        const bool bMinus = bEnabled && MenuButton(I + Rows, MinusX, RowY + 1.f * S, 35.f * S, 36.f * S);
        const bool bPlus = bEnabled && MenuButton(I, PlusX, RowY + 1.f * S, 35.f * S, 36.f * S);
        const bool bFocused = Mode.GraphicsChoice() == I || bMinus || bPlus;
        if (bFocused) FocusBand(X + 19.f * S, RowY, Width - 38.f * S, 39.f * S, S);
        const FLinearColor Tone = bEnabled ? (bFocused ? MenuGold : MenuPaper) : MenuQuiet.CopyWithNewOpacity(.55f);
        Label(Labels[I], X + 35.f * S, RowY + 10.f * S, 17.f * S, Tone);
        DrawRect(MenuInk.CopyWithNewOpacity(.84f), MinusX, RowY + 4.f * S, 35.f * S, 29.f * S);
        DrawRect(MenuInk.CopyWithNewOpacity(.84f), PlusX, RowY + 4.f * S, 35.f * S, 29.f * S);
        if (bMinus) FocusBand(MinusX, RowY + 4.f * S, 35.f * S, 29.f * S, S);
        if (bPlus) FocusBand(PlusX, RowY + 4.f * S, 35.f * S, 29.f * S, S);
        Label(TEXT("‹"), MinusX + 17.5f * S, RowY + 10.f * S, 19.f * S, Tone, true);
        Label(TEXT("›"), PlusX + 17.5f * S, RowY + 10.f * S, 19.f * S, Tone, true);
        const FString Value = Graphics.Value(Row);
        if (bAudio)
        {
            const float Gain = FMath::Sqrt(FDuelGraphicsSettings::AudioGain(Row));
            DrawRect(MenuRule, X + 395.f * S, RowY + 17.f * S, 105.f * S, 4.f * S);
            DrawRect(MenuGold, X + 395.f * S, RowY + 17.f * S, Gain * 105.f * S, 4.f * S);
            Label(Value, X + 551.f * S - LabelWidth(Value, 15.f * S), RowY + 11.f * S, 15.f * S, Tone);
        }
        else
        {
            Label(Value, X + 471.f * S, RowY + 10.f * S, 17.f * S, Tone, true);
        }
    }
    const auto Selection = static_cast<EDuelGraphicsRow>(Mode.GraphicsChoice());
    const TCHAR* Status = Graphics.SaveFailed() ? TEXT("无法保存设置") :
        !Graphics.CanAdjust(Selection) ? TEXT("当前设备无法调整此项") :
        Selection == EDuelGraphicsRow::HardwareLumen ? TEXT("切换光追时可能短暂等待") : nullptr;
    if (Status) Label(Status, X + 29.f * S, Y + 449.f * S, 13.f * S, MenuQuiet);
    const float Footer = Y + 490.f * S;
    float KeyX = X + 29.f * S;
    KeyX += KeyHint(KeyX, Footer, S, TEXT("W / S"), TEXT("选择"));
    KeyX += KeyHint(KeyX, Footer, S, TEXT("A / D"), TEXT("调整"));
    KeyHint(KeyX, Footer, S, TEXT("Esc"), TEXT("返回"));
}

void ADuelHUD::DrawControls(const ADuelMode& Mode, float W, float H, float S)
{
    S = FMath::Min(S, FMath::Min(W / 900.f, H / 445.f));
    const float Width = 846.f * S, Height = 399.f * S;
    const float X = (W - Width) * .5f, Y = (H - Height) * .5f;
    Panel(X, Y, Width, Height, S);
    Label(TEXT("按键说明"), X + 29.f * S, Y + 19.f * S, 32.f * S, MenuPaper);
    DrawMenuBack(X + Width - 137.f * S, Y + 17.f * S, 108.f * S, 32.f * S, S, TEXT("F / Esc 返回"));
    const float Left = X + 32.f * S, Right = X + 432.f * S;
    Label(TEXT("移动与探索"), Left, Y + 78.f * S, 17.f * S, MenuGold);
    Label(TEXT("战斗与配装"), Right, Y + 78.f * S, 17.f * S, MenuGold);
    DrawRect(MenuRule, Left, Y + 104.f * S, Width - 64.f * S, FMath::Max(1.f, S));
    const auto Entry = [&](float Col, int32 I, const TCHAR* Key, const TCHAR* Copy)
    {
        const float Row = Y + (120.f + I * 32.f) * S;
        KeyHint(Col, Row, S, Key, TEXT(""));
        Label(Copy, Col + 161.f * S, Row + 3.f * S, 15.f * S, MenuPaper);
    };
    Entry(Left, 0, TEXT("WASD"), TEXT("移动"));
    Entry(Left, 1, TEXT("Space"), TEXT("跳跃"));
    Entry(Left, 2, TEXT("Shift 短按"), TEXT("后闪 / 有方向时翻滚"));
    Entry(Left, 3, TEXT("Shift + 方向 长按"), TEXT("跑步"));
    Entry(Left, 4, TEXT("F"), TEXT("交互"));
    if (Mode.UsesNorthTower())
    {
        Entry(Left, 5, TEXT("M"), TEXT("楼层地图"));
        Entry(Left, 6, TEXT("G"), TEXT("随身灯"));
    }
    Entry(Right, 0, TEXT("Q"), TEXT("锁定"));
    Entry(Right, 1, TEXT("左键 / 右键"), TEXT("右手武器 / 左手武器"));
    Entry(Right, 2, TEXT("短按 / 长按武器键"), TEXT("轻击 / 重击"));
    Entry(Right, 3, TEXT("盾牌键"), TEXT("按住格挡，迎击时举盾弹反"));
    Entry(Right, 4, TEXT("E / 鼠标侧键"), TEXT("战技或施法"));
    Entry(Right, 5, TEXT("1 / 3"), TEXT("切换左手 / 右手"));
    Entry(Right, 6, TEXT("4 / 2 / R"), TEXT("切魔法 / 切道具 / 使用道具"));
}

void ADuelHUD::DrawInventory(const ADuelMode& Mode, float W, float H, float S)
{
    S = FMath::Min(S, FMath::Min(W / 956.f, H / 525.f));
    const float Width = 900.f * S, Height = 472.f * S;
    const float X = (W - Width) * .5f, Y = (H - Height) * .5f;
    const ADuelFighter& Fighter = *Mode.GetPlayerFighter();
    const int32 Active = static_cast<int32>(Fighter.EquippedItem());
    const int32 Selected = Mode.InventorySelection;
    const TArray<int32> Entries = Mode.InventoryChoices();
    const auto Name = [](int32 Id)
    {
        if (Id < 2) return Id == 0 ? TEXT("血瓶") : TEXT("蓝瓶");
        return FDuelNorthProgress::RelicName(static_cast<EDuelNorthRelic>(Id - 2));
    };
    const auto Icon = [&](int32 Id, float Left, float Top, float Size)
    {
        if (Id < 2)
        {
            InventoryIcon(Id == 0 ? 12 : 13, Left, Top, Size, FLinearColor::White);
        }
        else
        {
            const float Glyph = Size * .64f;
            Chrome(13, Left + (Size - Glyph) * .5f, Top + (Size - Glyph) * .5f, Glyph, Glyph,
                FLinearColor(.78f, .63f, .95f, .93f));
        }
    };
    Panel(X, Y, Width, Height, S);
    Label(TEXT("物品"), X + 27.f * S, Y + 18.f * S, 34.f * S, MenuPaper);
    DrawMenuBack(X + Width - 133.f * S, Y + 19.f * S, 105.f * S, 32.f * S, S, TEXT("Esc 返回"));
    const float Rule = FMath::Max(1.f, S);
    DrawRect(MenuRule, X + 27.f * S, Y + 65.f * S, Width - 54.f * S, Rule);
    DrawRect(MenuRule, X + 371.f * S, Y + 79.f * S, Rule, 322.f * S);
    const float RowX = X + 26.f * S;
    const float RowW = 318.f * S;
    Label(TEXT("药瓶"), RowX, Y + 83.f * S, 15.f * S, MenuGold);
    int32 Shown = Selected;
    for (int32 I = 0; I < Entries.Num(); ++I)
    {
        const int32 Id = Entries[I];
        const float RowY = Y + (104.f + I * 53.f + (Id >= 2 ? 25.f : 0.f)) * S;
        if (I == 2) Label(TEXT("信物"), RowX, RowY - 21.f * S, 15.f * S, MenuGold);
        const bool bHover = MenuButton(Id, RowX, RowY, RowW, 46.f * S);
        if (bHover) Shown = Id;
        const bool bFocus = bHover || Selected == Id;
        if (bFocus) FocusBand(RowX, RowY, RowW, 46.f * S, S);
        Icon(Id, RowX + 3.f * S, RowY + 3.f * S, 39.f * S);
        Label(Name(Id), RowX + 49.f * S, RowY + 13.f * S, 16.f * S, bFocus ? MenuGold : MenuPaper);
        const int32 Count = Id < 2 ? Fighter.ItemCount(static_cast<EDuelItem>(Id)) : 1;
        const FString Detail = FString::Printf(TEXT("×%d"), Count);
        Label(Detail, RowX + RowW - 8.f * S - LabelWidth(Detail, 14.f * S), RowY + 14.f * S, 14.f * S, MenuPaper);
        if (Id == Active) Label(TEXT("快捷"), RowX + 230.f * S, RowY + 14.f * S, 13.f * S, MenuGold);
    }
    const float Detail = X + 400.f * S;
    const float DetailW = Width - 428.f * S;
    FadeBand(Detail, Y + 83.f * S, DetailW, 134.f * S, FLinearColor(.14f, .104f, .063f, .40f),
        FLinearColor(.035f, .030f, .044f, .24f));
    Icon(Shown, Detail + 12.f * S, Y + 95.f * S, 104.f * S);
    Label(Shown < 2 ? TEXT("消耗品") : TEXT("北塔信物"), Detail + 133.f * S, Y + 114.f * S,
        14.f * S, MenuGold);
    Label(Name(Shown), Detail + 133.f * S, Y + 143.f * S, 26.f * S, MenuPaper);

    if (Shown < 2)
    {
        const FString Effect = FString::Printf(TEXT("使用后恢复最大%s的 %d%%。"), Shown == 0 ? TEXT("生命") : TEXT("魔力"),
            FMath::RoundToInt(DuelCourt::FlaskRestoreFraction * 100.f));
        Label(Effect, Detail, Y + 234.f * S, 17.f * S, MenuPaper);
        Label(TEXT("休整时补满。"), Detail, Y + 265.f * S, 15.f * S, MenuQuiet);
        const int32 Allocated = Shown == 0 ? Fighter.FlaskAllocation() : DuelCourt::TotalFlasks - Fighter.FlaskAllocation();
        const FString Doses = FString::Printf(TEXT("剩余 %d   /   配给 %d"), Fighter.ItemCount(static_cast<EDuelItem>(Shown)), Allocated);
        Label(Doses, Detail, Y + 313.f * S, 17.f * S, MenuGold);
        if (Selected == Shown)
        {
            const float AdjustY = Y + 349.f * S;
            const bool bMinus = Allocated > 0 && MenuButton(-2, Detail, AdjustY, 43.f * S, 32.f * S);
            const bool bPlus = Allocated < DuelCourt::TotalFlasks && MenuButton(-3, Detail + 156.f * S, AdjustY, 43.f * S, 32.f * S);
            DrawRect(MenuInk, Detail, AdjustY, 43.f * S, 32.f * S);
            DrawRect(MenuInk, Detail + 156.f * S, AdjustY, 43.f * S, 32.f * S);
            if (bMinus) FocusBand(Detail, AdjustY, 43.f * S, 32.f * S, S);
            if (bPlus) FocusBand(Detail + 156.f * S, AdjustY, 43.f * S, 32.f * S, S);
            Label(TEXT("−"), Detail + 21.5f * S, AdjustY + 7.f * S, 18.f * S,
                Allocated > 0 ? MenuGold : MenuRule, true);
            Label(TEXT("配给"), Detail + 99.5f * S, AdjustY + 7.f * S, 16.f * S, MenuPaper, true);
            Label(TEXT("+"), Detail + 177.5f * S, AdjustY + 7.f * S, 18.f * S,
                Allocated < DuelCourt::TotalFlasks ? MenuGold : MenuRule, true);
            KeyHint(Detail + 235.f * S, AdjustY + 5.f * S, S, TEXT("R"),
                Active == Shown ? TEXT("在快捷槽使用") : TEXT("快捷槽未选用"));
        }
    }
    else
    {
        const TCHAR* Uses[] = {TEXT("打开通往二楼档案室的铁门。"), TEXT("打开镜厅后门，前往监狱办公区。"),
            TEXT("刻着自己的名字，取回后可以开启通往月庭的门。")};
        const TArray<FString> Lines = WrapLabel(Uses[Shown - 2], 17.f * S, DetailW);
        for (int32 I = 0; I < Lines.Num(); ++I) Label(Lines[I], Detail, Y + (235.f + I * 28.f) * S, 17.f * S, MenuPaper);
    }
    DrawRect(MenuRule, X + 27.f * S, Y + 407.f * S, Width - 54.f * S, Rule);
    float KeyX = X + 27.f * S;
    const float KeyY = Y + 426.f * S;
    KeyX += KeyHint(KeyX, KeyY, S, TEXT("W / S"), TEXT("选择物品"));
    if (Selected < 2)
    {
        KeyX += KeyHint(KeyX, KeyY, S, TEXT("A / D"), TEXT("调整配给"));
        KeyX += KeyHint(KeyX, KeyY, S, TEXT("F"), TEXT("设为快捷物品"));
    }
    KeyHint(KeyX, KeyY, S, TEXT("Esc"), TEXT("返回"));
}
