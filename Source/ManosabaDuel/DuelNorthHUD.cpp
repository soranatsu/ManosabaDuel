#include "DuelGame.h"

#include "CanvasItem.h"
#include "DuelNorthLayout.h"
#include "DuelNorthNarrative.h"
#include "DuelNorthTower.h"
#include "DuelNorthFinds.h"
#include "DuelNorthLampworks.h"
#include "Engine/Canvas.h"
#include "Engine/Texture.h"
#include "Kismet/GameplayStatics.h"

namespace
{
    const FLinearColor NorthInk(.016f, .019f, .031f, .94f);
    const FLinearColor NorthPaper(.94f, .89f, .78f, 1.f);
    const FLinearColor NorthStone(.30f, .34f, .38f, .78f);
    const FLinearColor NorthEdge(.58f, .59f, .55f, .95f);
    const FLinearColor NorthGold(.92f, .69f, .38f, 1.f);
    const FLinearColor NorthBlue(.50f, .73f, .85f, 1.f);
    const FLinearColor NorthViolet(.75f, .62f, .91f, 1.f);
    constexpr const TCHAR* FloorTabs[] = {TEXT("B1"), TEXT("1F"), TEXT("2F"), TEXT("3F"), TEXT("夹层"), TEXT("4F"), TEXT("月庭")};
    static_assert(UE_ARRAY_COUNT(FloorTabs) == static_cast<int32>(DuelNorthLayout::EFloor::Unknown));

    // The main approach faces +X. Draw that direction up and +Y to its right,
    // so positive Unreal yaw turns clockwise on the map as it does in play.
    FVector2D NorthMapPlane(const FVector& World)
    {
        return FVector2D(World.Y, -World.X);
    }

    const TCHAR* NorthRoomName(const DuelNorthLayout::FRoom& Room)
    {
        using DuelNorthLayout::ERoomKind;
        for (const auto& Cell : DuelNorthLayout::CustodyLabels())
            if (Room.Id == Cell.Room) return Cell.Name;
        if (Room.Id == TEXT("DungeonCustodyTurn")) return TEXT("羁押区侧廊");
        if (Room.Id == TEXT("DungeonCustodyGallery")) return TEXT("北侧牢房走廊");
        if (Room.Id == TEXT("ChapelHall")) return TEXT("档案室");
        if (Room.Id == TEXT("SeaGallery")) return TEXT("海廊");
        if (Room.Id == TEXT("PrisonHall")) return TEXT("生活大厅");
        if (Room.Id == TEXT("FinalCourt")) return TEXT("终审厅");
        if (Room.Id == TEXT("DungeonCourt")) return TEXT("03号禁闭室");
        if (Room.Id == TEXT("MirrorCourt")) return TEXT("镜厅");
        if (Room.Id == TEXT("BeaconChartRoom")) return TEXT("值班间");
        if (Room.Id == TEXT("BeaconGallery")) return TEXT("办公走廊");
        if (Room.Id == TEXT("BeaconCrossway")) return TEXT("勤务通道");
        if (Room.Id == TEXT("BeaconWorkshop")) return TEXT("信件检查室");
        if (Room.Id == TEXT("BeaconCabin")) return TEXT("看守住处");
        if (Room.Id == TEXT("BeaconOptics")) return TEXT("物证室");
        switch (Room.Kind)
        {
        case ERoomKind::Combat: return TEXT("战厅");
        case ERoomKind::Cell: return TEXT("牢房");
        case ERoomKind::Hall: return TEXT("大厅");
        case ERoomKind::Gallery: return TEXT("回廊");
        case ERoomKind::Garden: return TEXT("月庭");
        case ERoomKind::Antechamber: return TEXT("前室");
        case ERoomKind::Stairwell: return TEXT("楼梯");
        case ERoomKind::Landing: return TEXT("楼梯平台");
        default: return TEXT("通道");
        }
    }
}

void ADuelHUD::DrawNorthHUD(const ADuelMode& Mode, float W, float H, float S)
{
    using namespace DuelNorthLayout;
    const ADuelNorthTower* Tower = Mode.NorthTower();
    const ADuelFighter* Player = Mode.GetPlayerFighter();
    if (!Tower || !Player || Mode.Outcome() == EDuelOutcome::Title || UGameplayStatics::IsGamePaused(this))
    {
        return;
    }
    const EFloor CurrentFloor = Tower->CurrentFloor();
    const FString Objective = Tower->Objective(false);
    const ADuelNorthNarrative* Narrative = Tower->Narrative();
    const bool bTalking = Narrative && Narrative->IsTalking();
    const bool bEnding = Narrative && Narrative->EndingVisible();
    const bool bExtraction = Narrative && Narrative->ExtractionInProgress();
    const bool bMap = Tower->MapVisible() && !bTalking && !bEnding;
    const EFloor Floor = bMap ? Mode.ViewedMapFloor() : CurrentFloor;
    const EDuelOutcome Outcome = Mode.Outcome();
    const double Now = GetWorld()->GetTimeSeconds();
    if (PreviousNorthOutcome != Outcome)
    {
        PreviousNorthOutcome = Outcome;
        NorthOutcomeChangedAt = Now;
    }

    const auto Slip = [&](float X, float Y, float Width, float Height, const FLinearColor& Accent)
    {
        const float Corner = 8.f * S;
        const float Rule = FMath::Max(1.f, S);
        DrawRect(NorthInk, X, Y, Width - Corner, Height);
        DrawRect(NorthInk, X + Width - Corner, Y + Corner, Corner, Height - Corner);
        const FLinearColor Edge = Accent.CopyWithNewOpacity(.50f);
        DrawLine(X, Y, X + Width - Corner, Y, Edge, Rule);
        DrawLine(X + Width - Corner, Y, X + Width, Y + Corner, Edge, Rule);
        DrawLine(X + 12.f * S, Y + Height, X + Width, Y + Height, Edge.CopyWithNewOpacity(.28f), Rule);
    };

    if (Tower->GetPuzzle() != INDEX_NONE)
    {
        const int32 Id = Tower->GetPuzzle(), Round = Tower->GetPuzzleRound();
        const int32 Choice = Tower->GetPuzzleChoice();
        const bool bEvidenceTable = Id == 2;
        const bool bCompareSeals = Round == 1 && Id != 0;
        const float Width = FMath::Min(580.f * S, W * .51f);
        const float X = W - Width - 27.f * S, Y = H * .24f;
        const float Inset = 21.f * S, Gap = 10.f * S;
        const float CardWidth = (Width - 2.f * Inset - Gap) * .5f;
        const TCHAR* Titles[3][2] = {
            {TEXT("配给底账"), TEXT("配膳台")},
            {TEXT("退信登记"), TEXT("送信记录")},
            {Round ? TEXT("假信蜡印") : TEXT("小雪的日记"), Round ? TEXT("印戒试压") : TEXT("物证桌")},
        };
        const TCHAR* Facts[3][2][2] = {
            {
                {TEXT("十三人领饭。另送一份，用缺口碗。"), TEXT("大碗和小碗的碗沿完整。中间那只缺了一块。")},
                {TEXT("01 厨房，02 医务室，03 地下禁闭室。"), TEXT("翻过缺口碗，碗底夹着一块 03 号房牌。")},
            }, {
                {TEXT("退信袋的夹签列着两封亲笔原件。"), TEXT("两人都登记签收。对应的两封信放在桌上。")},
                {TEXT("退信袋还封着两人的亲笔信。"), TEXT("并排看桌上两封送达信背面的红蜡印。")},
            }, {
                {TEXT("求救信用红蜡封袋，留在物证室。"), TEXT("公文、退件和扣下的信分别装袋。")},
                {TEXT("两封假信的蜡印都缺了同一个角。"), TEXT("用选中的印戒在空白红蜡上试压。")},
            },
        };
        const auto Left = WrapLabel(Facts[Id][Round][0], 15.f * S, CardWidth - 20.f * S);
        const auto Right = WrapLabel(Facts[Id][Round][1], 15.f * S, CardWidth - 20.f * S);
        const float GlyphHeight = bCompareSeals ? 57.f : 0.f;
        const float CardH = (34.f + 19.f * FMath::Max(Left.Num(), Right.Num()) + GlyphHeight) * S;
        const float QuestionY = Y + 54.f * S + CardH;
        const auto Lines = WrapLabel(DuelNorthFinds::Question(Id, Round), 20.f * S, Width - 2.f * Inset);
        const float ChoicesY = QuestionY + (10.f + 25.f * Lines.Num()) * S;
        const int32 Choices = bEvidenceTable && Round == 1 ? 2 : 3;
        const auto Hint = WrapLabel(Tower->PuzzleHint(), 15.f * S, Width - 2.f * Inset);
        const float FeedbackY = ChoicesY + (Choices * 29.f + 4.f) * S;
        const float ControlsY = FeedbackY + (24.f + 19.f * Hint.Num()) * S;
        Slip(X, Y, Width, ControlsY - Y + 27.f * S, NorthGold);
        Label(bEvidenceTable ? (Round ? TEXT("物证桌上的印戒") : TEXT("物证桌上的信袋")) : DuelNorthFinds::All()[Id].Title,
            X + Inset, Y + 14.f * S, 17.f * S, NorthGold);
        Label(Round ? TEXT("二 / 二") : TEXT("一 / 二"), X + Width - 70.f * S, Y + 17.f * S, 13.f * S, NorthBlue);

        const auto SealGlyph = [&](float CX, float CY, bool bChipped, bool bRing)
        {
            const FLinearColor Face = bRing ? FLinearColor(.55f, .39f, .21f, 1.f) : FLinearColor(.60f, .12f, .095f, 1.f);
            const FLinearColor Edge = bRing ? NorthGold : FLinearColor(.99f, .42f, .32f, 1.f);
            const FLinearColor Mark = bRing ? NorthInk : FLinearColor(.99f, .80f, .67f, 1.f);
            for (int32 Row = -19; Row <= 19; ++Row)
            {
                const float Span = FMath::Sqrt(400.f - Row * Row) * S;
                DrawLine(CX - Span, CY + Row * S, CX + Span, CY + Row * S, Face, FMath::Max(1.f, S));
            }
            for (int32 I = 0; I < 24; ++I)
            {
                const double A = 2. * PI * I / 24., B = 2. * PI * (I + 1) / 24.;
                DrawLine(CX + FMath::Cos(A) * 20. * S, CY + FMath::Sin(A) * 20. * S,
                    CX + FMath::Cos(B) * 20. * S, CY + FMath::Sin(B) * 20. * S, Edge, FMath::Max(1.f, S));
            }
            const auto Petal = [&](TConstArrayView<FVector2D> Points)
            {
                for (int32 I = 1; I < Points.Num(); ++I)
                    DrawLine(CX + Points[I - 1].X * 14.f * S, CY + Points[I - 1].Y * 14.f * S,
                        CX + Points[I].X * 14.f * S, CY + Points[I].Y * 14.f * S, Mark, 1.65f * S);
            };
            Petal(MakeArrayView(DuelNorthLampworks::TopPetal));
            Petal(MakeArrayView(DuelNorthLampworks::LeftPetal));
            Petal(bChipped ? MakeArrayView(DuelNorthLampworks::ChippedPetal) : MakeArrayView(DuelNorthLampworks::RightPetal));
        };
        for (int32 K = 0; K < 2; ++K)
        {
            const float CardX = X + Inset + K * (CardWidth + Gap), CardY = Y + 41.f * S;
            DrawRect(NorthGold.CopyWithNewOpacity(.085f), CardX, CardY, CardWidth, CardH);
            Label(Titles[Id][K], CardX + 10.f * S, CardY + 6.f * S, 14.f * S, NorthGold);
            const auto& Copy = K == 0 ? Left : Right;
            for (int32 I = 0; I < Copy.Num(); ++I)
                Label(Copy[I], CardX + 10.f * S, CardY + (26.f + 19.f * I) * S, 15.f * S, NorthPaper);
            if (bCompareSeals)
            {
                const float IconY = CardY + CardH - 28.f * S;
                if (bEvidenceTable)
                    SealGlyph(CardX + CardWidth * .5f, IconY, K == 0 || Choice == 0, K == 1);
                else if (K == 1)
                {
                    SealGlyph(CardX + CardWidth * .35f, IconY, true, false);
                    SealGlyph(CardX + CardWidth * .65f, IconY, true, false);
                }
            }
        }
        for (int32 I = 0; I < Lines.Num(); ++I)
            Label(Lines[I], X + Inset, QuestionY + 25.f * I * S, 20.f * S, NorthPaper);
        for (int32 I = 0; I < Choices; ++I)
        {
            const bool bSelected = Choice == I;
            const float Row = ChoicesY + I * 29.f * S;
            if (bSelected)
                DrawRect(NorthGold.CopyWithNewOpacity(.18f), X + Inset - 5.f * S, Row, Width - 2.f * Inset + 10.f * S, 27.f * S);
            Label(DuelNorthFinds::Answer(Id, Round, I), X + Inset + 10.f * S, Row + 4.f * S, 17.f * S,
                bSelected ? NorthGold : NorthPaper);
        }
        for (int32 I = 0; I < Hint.Num(); ++I)
            Label(Hint[I], X + Inset, FeedbackY + 19.f * I * S, 15.f * S, NorthBlue);
        const TCHAR* Controls = bEvidenceTable
            ? (Round ? TEXT("A / D  选择     F  压印     Esc  放下") : TEXT("A / D  选择     F  取袋     Esc  放下"))
            : TEXT("A / D  选择     F  确认     Esc  放下");
        Label(Controls, X + Inset, ControlsY, 14.f * S, NorthGold);
        return;
    }
    if (Narrative && Narrative->IsReadingDocument())
    {
        const float PageW = FMath::Min(800.f * S, W - 80.f * S);
        const float Margin = 51.f * S;
        float Size = 24.f * S, Pitch = 35.f * S;
        auto FullLines = WrapLabel(Narrative->DocumentText(), Size, PageW - 2.f * Margin);
        while (176.f * S + FullLines.Num() * Pitch > H - 74.f * S && Size > 15.f * S)
        {
            Size -= S;
            Pitch = Size * 1.46f;
            FullLines = WrapLabel(Narrative->DocumentText(), Size, PageW - 2.f * Margin);
        }
        const float PageH = 176.f * S + FullLines.Num() * Pitch;
        const float PageX = (W - PageW) * .5f, PageY = (H - PageH) * .5f;
        const FLinearColor Paper(.87f, .77f, .53f, 1.f), Ink(.11f, .08f, .045f, 1.f);
        DrawRect(FLinearColor(.004f, .006f, .010f, .73f), 0, 0, W, H);
        DrawRect(FLinearColor(0.f, 0.f, 0.f, .35f), PageX + 9.f * S, PageY + 12.f * S, PageW, PageH);
        DrawRect(Paper, PageX, PageY, PageW, PageH);
        DrawRect(FLinearColor(.56f, .40f, .18f, .18f), PageX, PageY, 6.f * S, PageH);
        DrawRect(FLinearColor(.98f, .89f, .67f, .22f), PageX + 7.f * S, PageY + 7.f * S, PageW - 14.f * S, PageH - 14.f * S);
        DrawLine(PageX + Margin, PageY + 69.f * S, PageX + PageW - Margin, PageY + 69.f * S,
            Ink.CopyWithNewOpacity(.35f), S);
        Label(Narrative->DocumentTitle(), W * .5f, PageY + 27.f * S, 26.f * S, Ink, true);
        int32 Remaining = Narrative->DocumentVisibleCharacters();
        for (int32 I = 0; I < FullLines.Num(); ++I)
        {
            const int32 Count = FMath::Clamp(Remaining, 0, FullLines[I].Len());
            if (Count > 0) Label(FullLines[I].Left(Count), PageX + Margin, PageY + 92.f * S + I * Pitch, Size, Ink);
            Remaining -= FullLines[I].Len();
        }
        const bool bRevealing = Narrative->DocumentVisibleCharacters() < Narrative->DocumentText().Len();
        const TCHAR* Action = bRevealing ? TEXT("显示全文") : Narrative->IsReadingEvidence() ? TEXT("开始分析") : TEXT("收起纸页");
        Label(FString(TEXT("F  ")) + Action + TEXT("     Esc  放下"), W * .5f, PageY + PageH - 43.f * S,
            16.f * S, Ink.CopyWithNewOpacity(.8f), true);
        return;
    }
    if (bEnding)
    {
        const float PageW = FMath::Min(700.f * S, W - 64.f * S);
        const bool bFlight = Narrative->ExtractionEndingVisible();
        const TArray<FString> Lines = WrapLabel(bFlight ? FDuelNorthProgress::ExtractionEndingText() :
            FDuelNorthProgress::EndingText(), 21.f * S, PageW - 90.f * S);
        const float PageH = (174.f + Lines.Num() * 32.f) * S;
        const float PageX = (W - PageW) * .5f;
        const float PageY = (H - PageH) * .5f;
        DrawRect(FLinearColor(.004f, .006f, .010f, .66f), 0, 0, W, H);
        Panel(PageX, PageY, PageW, PageH, S);
        Label(bFlight ? TEXT("离岛") : TEXT("北塔之外"), W * .5f, PageY + 31.f * S, 30.f * S, NorthPaper, true);
        DrawLine(PageX + 44.f * S, PageY + 77.f * S, PageX + PageW - 44.f * S, PageY + 77.f * S,
            NorthGold.CopyWithNewOpacity(.55f), S);
        for (int32 I = 0; I < Lines.Num(); ++I)
        {
            Label(Lines[I], W * .5f, PageY + (100.f + I * 32.f) * S, 21.f * S, NorthPaper, true);
        }
        Label(Tower->Prompt(), W * .5f, PageY + PageH - 39.f * S, 15.f * S, NorthGold, true);
        return;
    }
    if (Outcome == EDuelOutcome::Defeat)
    {
        const float Show = FMath::SmoothStep(0.f, .45f, static_cast<float>(Now - NorthOutcomeChangedAt));
        const FLinearColor DefeatTint(.79f, .28f, .28f, Show);
        DrawRect(FLinearColor(.013f, .007f, .010f, .46f * Show), 0, 0, W, H);
        const float Y = H * .36f, Width = FMath::Min(730.f * S, W - 60.f * S);
        const float X = (W - Width) * .5f;
        FadeBand(X, Y, Width * .5f, 112.f * S, NorthInk.CopyWithNewOpacity(0.f), NorthInk.CopyWithNewOpacity(.94f * Show));
        FadeBand(W * .5f, Y, Width * .5f, 112.f * S, NorthInk.CopyWithNewOpacity(.94f * Show), NorthInk.CopyWithNewOpacity(0.f));
        Label(TEXT("败退"), W * .5f, Y + 17.f * S, 49.f * S, DefeatTint, true);
        KeyHint(W * .5f - 63.f * S, Y + 82.f * S, S, TEXT("F"), TEXT("回到休整点"));
        return;
    }
    const float TaskWidth = FMath::Min(384.f * S, W * .43f);
    const float TaskX = W - TaskWidth - 28.f * S;
    const float TaskY = 52.f * S;
    if (!bMap)
    {
        const float JudgmentHintShift = !bTalking ? DrawNorthJudgmentHUD(Mode, W, H, S) : 0.f;
        float HintAge = 0.f, HintAlpha = 0.f;
        const FStringView Hint = Tower->MechanicHintText(HintAge, HintAlpha);
        if (Outcome == EDuelOutcome::Fighting && !bTalking && !Hint.IsEmpty() && HintAlpha > 0.f)
        {
            // Below all player meters/status labels; the fighting center and boss bar stay clear.
            const float X = 24.f * S, Y = 154.f * S + JudgmentHintShift;
            const float Width = FMath::Min(438.f * S, W * .46f);
            const float PortraitH = 142.f * S, Inset = 13.f * S;
            UTexture* Portrait = Tower->MechanicHintPortrait();
            const bool bHasPortrait = Portrait && Portrait->GetResource() && Portrait->GetSurfaceHeight() > 0.f;
            const float PortraitW = bHasPortrait ? PortraitH * Portrait->GetSurfaceWidth() / Portrait->GetSurfaceHeight() : 0.f;
            const float TextX = X + PortraitW + Inset * (bHasPortrait ? 2.f : 1.f);
            const float Font = 19.f * S, Pitch = 26.f * S;
            const auto Lines = WrapLabel(FString(Hint), Font, Width - (TextX - X) - Inset);
            const float Height = FMath::Max(PortraitH + 9.f * S, 45.f * S + Lines.Num() * Pitch);
            FadeBand(X, Y, Width, Height, NorthInk.CopyWithNewOpacity(.89f * HintAlpha),
                NorthInk.CopyWithNewOpacity(.28f * HintAlpha));
            if (bHasPortrait)
            {
                FCanvasTileItem Image(FVector2D(X + Inset, Y + Height - PortraitH), Portrait->GetResource(),
                    FVector2D(PortraitW, PortraitH), FLinearColor(1, 1, 1, HintAlpha));
                Image.BlendMode = SE_BLEND_Translucent;
                Canvas->DrawItem(Image);
            }
            const FLinearColor Accent = Player->SelectedCharacter() == EDuelCharacter::Hiro ? NorthBlue : NorthGold;
            Label(Player->CharacterName(), TextX, Y + 12.f * S, 15.f * S, Accent.CopyWithNewOpacity(HintAlpha));
            for (int32 I = 0; I < Lines.Num(); ++I)
                Label(Lines[I], TextX, Y + 39.f * S + I * Pitch, Font, NorthPaper.CopyWithNewOpacity(HintAlpha));
        }
        if (Outcome == EDuelOutcome::Victory && Now - NorthOutcomeChangedAt < 3.6 && !bTalking)
        {
            const float Age = static_cast<float>(Now - NorthOutcomeChangedAt);
            const float Alpha = FMath::SmoothStep(0.f, .38f, Age) * (1.f - FMath::SmoothStep(2.2f, 3.6f, Age));
            const float Width = FMath::Min(480.f * S, W * .67f);
            const float X = (W - Width) * .5f, Y = H * .29f;
            FadeBand(X, Y, Width * .5f, 78.f * S, NorthInk.CopyWithNewOpacity(0.f), NorthInk.CopyWithNewOpacity(.88f * Alpha));
            FadeBand(W * .5f, Y, Width * .5f, 78.f * S, NorthInk.CopyWithNewOpacity(.88f * Alpha), NorthInk.CopyWithNewOpacity(0.f));
            Chrome(13, W * .5f - 6.f * S, Y + 8.f * S, 12.f * S, 12.f * S, NorthGold.CopyWithNewOpacity(Alpha));
            Label(TEXT("魔女化已解除"), W * .5f, Y + 30.f * S, 31.f * S, NorthGold.CopyWithNewOpacity(Alpha), true);
        }
        if (!bExtraction && !bTalking)
        {
            const FString FloorTitle(FloorName(CurrentFloor));
            const bool bQuiet = bTalking || Outcome == EDuelOutcome::Fighting;
            FString Place, Action;
            if (!Objective.Split(TEXT(" · "), &Place, &Action)) Action = Objective;
            if (Place == FloorTitle) Place.Reset();
            const float Pad = 17.f * S;
            const float BodyX = TaskX + Pad;
            const float BodyW = TaskWidth - Pad * 2.f;
            const float ActionSize = 18.f * S;
            const TArray<FString> Lines = bQuiet ? TArray<FString>() : WrapLabel(Action, ActionSize, BodyW);
            const bool bShowPlace = !bQuiet && !Place.IsEmpty();
            const float ActionY = TaskY + (bShowPlace ? 96.f : 74.f) * S;
            const float TaskHeight = bQuiet ? 41.f * S :
                ActionY - TaskY + (23.f * Lines.Num() + 12.f) * S;
            Slip(TaskX, TaskY, TaskWidth, TaskHeight, NorthGold);
            Chrome(13, BodyX, TaskY + 12.f * S, 13.f * S, 13.f * S, NorthGold);
            Label(FloorTitle, BodyX + 23.f * S, TaskY + 11.f * S, 16.f * S, NorthGold);
            if (!bQuiet)
            {
                if (Mode.CanExplore())
                {
                    const float KeyCentre = BodyX + BodyW - 44.f * S;
                    DrawRect(NorthGold.CopyWithNewOpacity(.16f), KeyCentre - 9.f * S, TaskY + 10.f * S, 18.f * S, 18.f * S);
                    Label(TEXT("M"), KeyCentre, TaskY + 11.f * S, 13.f * S, NorthPaper, true);
                    Label(TEXT("地图"), KeyCentre + 15.f * S, TaskY + 11.f * S, 13.f * S, NorthPaper);
                }

                const float Gap = 4.f * S;
                const float Segment = (BodyW - (UE_ARRAY_COUNT(FloorTabs) - 1) * Gap) / UE_ARRAY_COUNT(FloorTabs);
                for (int32 I = 0; I < UE_ARRAY_COUNT(FloorTabs); ++I)
                {
                    const auto TrailFloor = MapOrder[I];
                    const bool bHere = TrailFloor == CurrentFloor;
                    const FLinearColor Color = bHere ? NorthGold : Tower->HasExploredFloor(TrailFloor) ?
                        NorthBlue.CopyWithNewOpacity(.62f) : NorthEdge.CopyWithNewOpacity(.20f);
                    const float X = BodyX + I * (Segment + Gap);
                    DrawRect(Color, X, TaskY + 39.f * S, Segment, FMath::Max(1.f, (bHere ? 2.5f : 1.5f) * S));
                }
                if (PreviousNorthObjective != Objective)
                {
                    PreviousNorthObjective = Objective;
                    NorthObjectiveChangedAt = Now;
                }
                const float Age = static_cast<float>(Now - NorthObjectiveChangedAt);
                const float Reveal = FMath::SmoothStep(0.f, .34f, Age);
                if (Reveal < 1.f)
                {
                    DrawRect(NorthGold.CopyWithNewOpacity(.68f * (1.f - Reveal)), BodyX,
                        TaskY + 44.f * S, BodyW * Reveal, FMath::Max(1.f, S));
                }
                Label(Tower->RescueObjective(),BodyX,TaskY+51.f*S,15.f*S,NorthGold);
                if (bShowPlace) Label(Place, BodyX, TaskY + 74.f * S, 13.f * S, NorthBlue);
                const FLinearColor Copy = NorthPaper.CopyWithNewOpacity(.50f + .50f * Reveal);
                for (int32 I = 0; I < Lines.Num(); ++I)
                {
                    Label(Lines[I], BodyX, ActionY + I * 23.f * S, ActionSize, Copy);
                }
            }

            float DetailY = TaskY + TaskHeight + 10.f * S;
            float WitchAlpha = 0.f;
            UTexture* Witch = Mode.NorthWitchPortrait(WitchAlpha);
            if (Witch && Witch->GetResource() && Witch->GetSurfaceWidth() > 0.f && Witch->GetSurfaceHeight() > 0.f && WitchAlpha > 0.f)
            {
                const float Aspect = Witch->GetSurfaceWidth() / Witch->GetSurfaceHeight();
                const float ArtH = FMath::Min(H * .22f, (W * .15f) / Aspect);
                const float ArtW = ArtH * Aspect;
                const float X = W - 28.f * S - ArtW;
                FadeBand(X - 9.f * S, DetailY, ArtW + 9.f * S, ArtH,
                    NorthInk.CopyWithNewOpacity(0.f), NorthInk.CopyWithNewOpacity(.49f * WitchAlpha));
                FCanvasTileItem Image(FVector2D(X, DetailY), Witch->GetResource(),
                    FVector2D(ArtW, ArtH), FLinearColor(1.f, 1.f, 1.f, WitchAlpha));
                Image.BlendMode = SE_BLEND_Translucent;
                Canvas->DrawItem(Image);
                DetailY += ArtH + 7.f * S;
            }
            const FString Treasure = Tower->RecentTreasure();
            if (!Treasure.IsEmpty())
            {
                const auto Names = WrapLabel(Treasure, 17.f*S, BodyW-16.f*S);
                const float Height = (27.f+22.f*Names.Num())*S;
                Slip(TaskX, DetailY, TaskWidth, Height, NorthGold);
                Label(TEXT("探索所得"),BodyX,DetailY+6.f*S,12.f*S,NorthGold);
                for (int32 I=0; I<Names.Num(); ++I)
                    Label(Names[I],BodyX,DetailY+(22.f+22.f*I)*S,17.f*S,NorthPaper);
                DetailY += Height+10.f*S;
            }
            if (const TCHAR* Item = Tower->RecentRelicName())
            {
                const float ToastHeight = 49.f * S;
                Slip(TaskX, DetailY, TaskWidth, ToastHeight, NorthViolet);
                Chrome(13, BodyX, DetailY + 16.f * S, 13.f * S, 13.f * S, NorthViolet);
                Label(TEXT("获得信物"), BodyX + 23.f * S, DetailY + 7.f * S, 11.f * S, NorthViolet);
                Label(Item, BodyX + 23.f * S, DetailY + 21.f * S, 17.f * S, NorthPaper);
                Label(TEXT("×1"), BodyX + BodyW - LabelWidth(TEXT("×1"), 15.f * S),
                    DetailY + 23.f * S, 15.f * S, NorthViolet);
                DetailY += ToastHeight + 10.f * S;
            }

            const FStringView Notice = Tower->VisibleNotice();
            if (!bQuiet && !Notice.IsEmpty() && !FStringView(Objective).StartsWith(Notice))
            {
                const TArray<FString> NoticeLines = WrapLabel(FString(Notice), 15.f * S, BodyW);
                Slip(TaskX, DetailY, TaskWidth, (24.f + 21.f * NoticeLines.Num()) * S, NorthBlue);
                for (int32 I = 0; I < NoticeLines.Num(); ++I)
                {
                    Label(NoticeLines[I], BodyX, DetailY + (10.f + I * 21.f) * S, 15.f * S, NorthPaper);
                }
            }
        }
        const FString Prompt = Tower->Prompt();
        const FDuelNorthSubtitle Subtitle = Narrative ? Narrative->Subtitle() : FDuelNorthSubtitle{};
        if (!Subtitle.Text.IsEmpty())
        {
            const bool bHasPrompt = !Prompt.IsEmpty();
            const float DialogueW = FMath::Min((bTalking ? 890.f : 690.f) * S, W - 72.f * S);
            const float FontSize = (bTalking ? 25.f : 21.f) * S;
            const TArray<FString> Lines = WrapLabel(Subtitle.Text, FontSize, DialogueW - 60.f * S);
            const float LinePitch = (bTalking ? 34.f : 29.f) * S;
            const float DialogueH = (bHasPrompt ? 91.f : 52.f) * S +
                FMath::Max(bTalking ? 2 : 1, Lines.Num()) * LinePitch;
            const float DialogueX = (W - DialogueW) * .5f;
            const float DialogueY = H - DialogueH - (bTalking ? 34.f : 39.f) * S;
            const FLinearColor Accent = !bTalking ? NorthGold :
                Subtitle.Character == EDuelCharacter::Yuki ? NorthViolet :
                Subtitle.Character == EDuelCharacter::Hiro ? NorthBlue : NorthGold;

            if (bTalking)
            {
                DrawRect(FLinearColor(.006f, .010f, .021f, .47f), 0, 0, W, H);
                UTexture* Portrait = Narrative->DialoguePortrait();
                if (Portrait && Portrait->GetResource() && Portrait->GetSurfaceHeight() > 0.f)
                {
                    const bool bLocal = Narrative->UsesLocalPortrait();
                    const float Age = FMath::Max(0.f, static_cast<float>(Now - Narrative->DialogueLineStarted()));
                    const float Entrance = FMath::SmoothStep(0.f, .24f, Age);
                    const float Aspect = Portrait->GetSurfaceWidth() / Portrait->GetSurfaceHeight();
                    const float TargetHeight = FMath::Min(DialogueY - 48.f * S, (bLocal ? .61f : .52f) * H);
                    const float PortraitH = FMath::Min(TargetHeight, DialogueW * .68f / Aspect);
                    const float PortraitW = PortraitH * Aspect;
                    const float Slide = 11.f * S * (1.f - Entrance);
                    const float Float = bLocal ? 1.5f * S * FMath::Sin(Age * 2.4f) : 0.f;
                    const float PortraitX = (W - PortraitW) * .5f;
                    const float PortraitY = DialogueY - PortraitH - 12.f * S + Slide + Float;
                    if (!bLocal)
                    {
                        DrawRect(Accent.CopyWithNewOpacity(.31f * Entrance),
                            PortraitX - S, PortraitY - S, PortraitW + 2.f * S, PortraitH + 2.f * S);
                    }
                    FCanvasTileItem Frame(FVector2D(PortraitX, PortraitY), Portrait->GetResource(),
                        FVector2D(PortraitW, PortraitH), FLinearColor(Entrance, Entrance, Entrance, Entrance));
                    Frame.BlendMode = bLocal ? SE_BLEND_Translucent : SE_BLEND_Opaque;
                    Canvas->DrawItem(Frame);
                    if (!bLocal)
                    {
                        DrawRect(NorthInk, PortraitX + 7.f * S, PortraitY + 7.f * S, 64.f * S, 19.f * S);
                        Label(TEXT("北塔模型"), PortraitX + 12.f * S, PortraitY + 10.f * S, 11.f * S, NorthPaper);
                    }
                }
            }

            if (bTalking && Narrative->IsReadingEvidence() && !Tower->EvidenceNote().IsEmpty())
            {
                const float NoteW=FMath::Min(285.f*S,W*.225f), Left=24.f*S, Top=H*.255f;
                const auto Copy=WrapLabel(Tower->EvidenceNote(),14.f*S,NoteW-28.f*S);
                const float NoteH=(40.f+20.f*Copy.Num())*S;
                Slip(Left,Top,NoteW,NoteH,NorthGold);
                Label(TEXT("手上的线索"),Left+14.f*S,Top+8.f*S,13.f*S,NorthGold);
                for(int32 I=0; I<Copy.Num(); ++I)
                    Label(Copy[I],Left+14.f*S,Top+(31.f+20.f*I)*S,14.f*S,NorthPaper);
            }
            Slip(DialogueX, DialogueY, DialogueW, DialogueH, Accent);
            Chrome(13, DialogueX + 24.f * S, DialogueY + 15.f * S, 11.f * S, 11.f * S, Accent);
            Label(Subtitle.Speaker, DialogueX + 43.f * S, DialogueY + 11.f * S, (bTalking ? 21.f : 16.f) * S, Accent);
            if (bTalking && Subtitle.Presence != EDuelNorthPresence::Person)
            {
                const TCHAR* Context = Subtitle.Presence == EDuelNorthPresence::Echo ? TEXT("先前的回忆") : TEXT("传声");
                const float NameW = LabelWidth(Subtitle.Speaker, 21.f * S);
                Label(Context, DialogueX + NameW + 57.f * S, DialogueY + 17.f * S, 13.f * S, NorthPaper);
            }
            if (bTalking)
            {
                Label(FString::Printf(TEXT("%d / %d"), Subtitle.Line, Subtitle.Count),
                    DialogueX + DialogueW - 61.f * S, DialogueY + 14.f * S, 13.f * S, NorthPaper);
                const float ProgressW = 66.f * S;
                const float Progress = static_cast<float>(Subtitle.Line) / FMath::Max(1, Subtitle.Count);
                DrawRect(NorthGold.CopyWithNewOpacity(.24f), DialogueX + DialogueW - 95.f * S,
                    DialogueY + 33.f * S, ProgressW, S);
                DrawRect(NorthGold, DialogueX + DialogueW - 95.f * S, DialogueY + 33.f * S, ProgressW * Progress, S);
            }
            for (int32 I = 0; I < Lines.Num(); ++I)
            {
                Label(Lines[I], DialogueX + 25.f * S, DialogueY + 43.f * S + I * LinePitch,
                    FontSize, NorthPaper);
            }
            if (bHasPrompt)
            {
                const float ActionsY = DialogueY + DialogueH - 34.f * S;
                if (bTalking)
                {
                    const float BeforeSkip = KeyHint(DialogueX + 25.f * S, ActionsY, S, TEXT("F"),
                        Subtitle.Line < Subtitle.Count ? TEXT("下一句") : TEXT("结束交谈"));
                    KeyHint(DialogueX + 25.f * S + BeforeSkip, ActionsY, S, TEXT("Esc"), TEXT("跳过"));
                }
                else Label(Prompt, DialogueX + 25.f * S, ActionsY + 2.f * S, 14.f * S, NorthGold);
            }
        }
        else if (!Prompt.IsEmpty())
        {
            const bool bConfirm = Prompt.StartsWith(TEXT("F  "));
            const FString Copy = bConfirm ? Prompt.Mid(3) : Prompt;
            const float Size = 17.f * S;
            const float KeySpace = bConfirm ? 39.f * S : 0.f;
            const float MaxWidth = FMath::Min(570.f * S, W - 72.f * S);
            const TArray<FString> Lines = WrapLabel(Copy, Size, MaxWidth - KeySpace - 26.f * S);
            float TextWidth = 0.f;
            for (const FString& Line : Lines) TextWidth = FMath::Max(TextWidth, LabelWidth(Line, Size));
            const float Width = TextWidth + KeySpace + 26.f * S;
            const float Height = (22.f + Lines.Num() * 22.f) * S;
            const float X = (W - Width) * .5f;
            const float Y = H - 99.f * S - Height;
            Slip(X, Y, Width, Height, NorthGold);
            if (bConfirm)
            {
                DrawRect(NorthGold.CopyWithNewOpacity(.21f), X + 11.f * S, Y + 9.f * S, 26.f * S, 26.f * S);
                Label(TEXT("F"), X + 24.f * S, Y + 12.f * S, 17.f * S, NorthGold, true);
            }
            for (int32 I = 0; I < Lines.Num(); ++I)
            {
                Label(Lines[I], X + KeySpace + 13.f * S, Y + (11.f + I * 22.f) * S, Size, NorthPaper);
            }
        }
        return;
    }

    S = FMath::Min(S, FMath::Min(W / 1104.f, H / 694.f));
    const float MapW = 1040.f * S;
    const float MapH = 638.f * S;
    const float X = (W - MapW) * .5f;
    const float Y = (H - MapH) * .5f;
    const float Rule = FMath::Max(1.f, S);
    const float MapLeft = X + 189.f * S;
    const float MapTop = Y + 87.f * S;
    const float ViewW = 603.f * S, ViewH = 474.f * S;
    const float NotesX = X + 817.f * S;
    const float NotesW = 195.f * S;
    const FLinearColor PlanInk(.020f, .025f, .038f, .99f);
    const FLinearColor PlanGold(.76f, .57f, .33f, .66f);
    const FLinearColor PlanGrid(.46f, .43f, .36f, .11f);
    DrawRect(FLinearColor(.004f, .006f, .010f, .73f), 0, 0, W, H);
    Panel(X, Y, MapW, MapH, S);
    Label(TEXT("北塔地图"), X + 23.f * S, Y + 20.f * S, 31.f * S, NorthPaper);
    Label(FloorName(Floor), MapLeft, Y + 26.f * S, 20.f * S, NorthGold);
    DrawMenuBack(X + MapW - 148.f * S, Y + 19.f * S, 124.f * S, 33.f * S, S, TEXT("M / Esc 关闭"));
    DrawRect(PlanGold.CopyWithNewOpacity(.39f), X + 23.f * S, Y + 66.f * S, MapW - 46.f * S, Rule);
    Label(TEXT("楼层"), X + 23.f * S, MapTop + 1.f * S, 16.f * S, NorthGold);
    for (int32 Index = UE_ARRAY_COUNT(FloorTabs) - 1; Index >= 0; --Index)
    {
        const EFloor Tab = MapOrder[Index];
        const float RowX = X + 16.f * S;
        const float RowY = MapTop + (31.f + (UE_ARRAY_COUNT(FloorTabs) - 1 - Index) * 45.f) * S;
        const bool bKnown = Tower->HasExploredFloor(Tab);
        const bool bHover = bKnown && MenuButton(static_cast<int32>(Tab), RowX, RowY, 153.f * S, 40.f * S);
        const bool bViewed = Tab == Floor;
        if (bViewed || bHover) FocusBand(RowX, RowY, 153.f * S, 40.f * S, S);
        if (Tab == CurrentFloor) Chrome(13, RowX + 135.f * S, RowY + 15.f * S, 10.f * S, 10.f * S, NorthBlue);
        const TCHAR* TabLabel = bKnown ? FloorName(Tab) : FloorTabs[Index];
        Label(TabLabel, RowX + 11.f * S, RowY + 12.f * S, 14.f * S,
            bViewed || bHover ? NorthGold : NorthPaper.CopyWithNewOpacity(bKnown ? 1.f : .40f));
    }
    const float CurrentY = MapTop + (39.f + UE_ARRAY_COUNT(FloorTabs) * 45.f) * S;
    Label(TEXT("当前位置"), X + 23.f * S, CurrentY, 13.f * S, NorthBlue);
    Label(FloorName(CurrentFloor), X + 23.f * S, CurrentY + 23.f * S, 15.f * S, NorthPaper);

    DrawRect(PlanInk, MapLeft, MapTop, ViewW, ViewH);
    for (float GX = 26.f; GX < ViewW / S; GX += 49.f)
    {
        DrawRect(PlanGrid, MapLeft + GX * S, MapTop, Rule, ViewH);
    }
    for (float GY = 22.f; GY < ViewH / S; GY += 49.f)
    {
        DrawRect(PlanGrid, MapLeft, MapTop + GY * S, ViewW, Rule);
    }
    DrawRect(PlanGold, MapLeft, MapTop, ViewW, Rule);
    DrawRect(PlanGold, MapLeft, MapTop + ViewH, ViewW, Rule);
    DrawRect(PlanGold, MapLeft, MapTop, Rule, ViewH);
    DrawRect(PlanGold, MapLeft + ViewW, MapTop, Rule, ViewH);
    const float HintY = Y + MapH - 45.f * S;
    float HintX = X + 24.f * S;
    HintX += KeyHint(HintX, HintY, S, TEXT("A / D"), TEXT("切换楼层"));
    HintX += KeyHint(HintX, HintY, S, TEXT("W / S / ↑ / ↓"), TEXT("选择休整点"));
    HintX += KeyHint(HintX, HintY, S, TEXT("F / Enter"), TEXT("旅行"));
    KeyHint(HintX, HintY, S, TEXT("M / Esc"), TEXT("关闭地图"));

    if (Floor == EFloor::Unknown)
    {
        Label(TEXT("正在确认所在楼层"), MapLeft + ViewW * .5f, MapTop + ViewH * .5f, 20.f * S, NorthPaper, true);
        return;
    }

    // Keep the scale stable across exploration. Only visited interior outlines and their thresholds are painted.
    FBox2D Bounds(ForceInit);
    for (const FRoom& Room : Rooms())
    {
        if (Room.Floor == Floor)
        {
            Bounds += FVector2D(Room.Bounds.Min.X, Room.Bounds.Min.Y);
            Bounds += FVector2D(Room.Bounds.Max.X, Room.Bounds.Max.Y);
        }
    }
    for (const FStairFlight& Flight : Stairs())
    {
        if (Flight.FromFloor != Floor && Flight.ToFloor != Floor) continue;
        if (const FRoom* Core = FindRoom(Flight.CoreRoom))
        {
            Bounds += FVector2D(Core->Bounds.Min.X, Core->Bounds.Min.Y);
            Bounds += FVector2D(Core->Bounds.Max.X, Core->Bounds.Max.Y);
        }
    }
    if (!Bounds.bIsValid) return;
    const FVector2D Span = Bounds.GetSize();
    const double Scale = FMath::Min((ViewW - 70.f * S) / FMath::Max(1.0, Span.Y),
        (ViewH - 66.f * S) / FMath::Max(1.0, Span.X));
    const FVector2D Middle(MapLeft + ViewW * .5f, MapTop + ViewH * .5f);
    const FVector2D Centre = Bounds.GetCenter();
    const auto Project = [&](const FVector& At)
    {
        return Middle + NorthMapPlane(At - FVector(Centre.X, Centre.Y, 0)) * Scale;
    };
    const auto Line = [&](const FVector2D& A, const FVector2D& B, const FLinearColor& Color, float Width = 1.f)
    {
        DrawLine(A.X, A.Y, B.X, B.Y, Color, FMath::Max(1.f, Width * S));
    };
    const auto Arrow = [&](const FVector2D& At, const FVector2D& Direction, float Radius, const FLinearColor& Color)
    {
        const FVector2D Side(-Direction.Y, Direction.X);
        Line(At - Direction * Radius, At + Direction * Radius, Color, 1.6f);
        Line(At + Direction * Radius, At + Side * Radius * .72f, Color, 1.6f);
        Line(At + Direction * Radius, At - Side * Radius * .72f, Color, 1.6f);
    };
    const auto Diamond = [&](const FVector2D& At, float Radius, const FLinearColor& Color)
    {
        const FVector2D Points[] = {At + FVector2D(0, -Radius), At + FVector2D(Radius, 0),
            At + FVector2D(0, Radius), At + FVector2D(-Radius, 0)};
        for (int32 I = 0; I < 4; ++I) Line(Points[I], Points[(I + 1) % 4], Color, 1.6f);
    };
    const auto RoomBox = [&](const FRoom& Room)
    {
        return FBox2D(Project(FVector(Room.Bounds.Max.X, Room.Bounds.Min.Y, 0)),
            Project(FVector(Room.Bounds.Min.X, Room.Bounds.Max.Y, 0)));
    };
    const auto PaintRoom = [&](const FBox2D& Box, const FLinearColor& Fill, const FLinearColor& Edge, bool bSolid)
    {
        const FVector2D Size = Box.GetSize();
        const FVector2D TL = Box.Min, BR = Box.Max;
        if (bSolid) DrawRect(Fill, TL.X, TL.Y, Size.X, Size.Y);
        Line(TL, FVector2D(BR.X, TL.Y), Edge, 1.35f);
        Line(FVector2D(BR.X, TL.Y), BR, Edge, 1.35f);
        Line(BR, FVector2D(TL.X, BR.Y), Edge, 1.35f);
        Line(FVector2D(TL.X, BR.Y), TL, Edge, 1.35f);
    };
    const FVector PlayerAt = Player->GetActorLocation();
    const FLocation Location = Locate(PlayerAt);
    const FRoom* FocusRoom = nullptr;
    for (const FRoom& Room : Rooms())
    {
        if (Room.Floor != Floor || Room.Kind == ERoomKind::Stairwell || !Tower->IsRoomExplored(Room.Id)) continue;
        const FBox2D Box = RoomBox(Room);
        const bool bHere = Floor == CurrentFloor && Location.Room == Room.Id;
        const bool bHover = bMenuPointerKnown && Box.IsInside(MenuPointer);
        if (bHover) FocusRoom = &Room;
        PaintRoom(Box, bHere ? FLinearColor(.105f, .20f, .26f, .94f) : FLinearColor(.105f, .089f, .079f, .98f),
            bHere ? NorthBlue : bHover ? NorthGold : PlanGold, true);
    }
    TSet<FName> StairCores;
    for (const FStairFlight& Flight : Stairs())
    {
        if (Flight.FromFloor != Floor && Flight.ToFloor != Floor) continue;
        if (!Tower->IsRoomExplored(Flight.CoreRoom) && !Tower->IsRoomExplored(Flight.FromLanding) &&
            !Tower->IsRoomExplored(Flight.ToLanding)) continue;
        if (const FRoom* Core = FindRoom(Flight.CoreRoom); Core && !StairCores.Contains(Core->Id))
        {
            PaintRoom(RoomBox(*Core), NorthStone, NorthBlue.CopyWithNewOpacity(.47f), false);
            StairCores.Add(Core->Id);
        }
        const FVector Across = FVector::CrossProduct((Flight.End - Flight.Start).GetSafeNormal2D(), FVector::UpVector)
            * Flight.Width * .39f;
        Line(Project(Flight.Start - Across), Project(Flight.End - Across), NorthBlue.CopyWithNewOpacity(.53f));
        Line(Project(Flight.Start + Across), Project(Flight.End + Across), NorthBlue.CopyWithNewOpacity(.53f));
        for (int32 I = 0; I <= 5; ++I)
        {
            const FVector At = FMath::Lerp(Flight.Start, Flight.End, I / 5.f);
            Line(Project(At - Across), Project(At + Across), NorthBlue.CopyWithNewOpacity(.44f));
        }
        const FVector2D Start = Project(Flight.Start), End = Project(Flight.End);
        const FVector2D Direction = (Floor == Flight.FromFloor ? End - Start : Start - End).GetSafeNormal();
        Arrow((Start + End) * .5f, Direction, 3.6f * S, NorthBlue);
    }
    for (const FPortal& Portal : Portals())
    {
        if (Portal.bWindow) continue;
        const FRoom* A = FindRoom(Portal.RoomA);
        const FRoom* B = FindRoom(Portal.RoomB);
        if ((!A || A->Floor != Floor) && (!B || B->Floor != Floor)) continue;
        const bool bKnownA = A && (Tower->IsRoomExplored(A->Id) || StairCores.Contains(A->Id));
        const bool bKnownB = B && (Tower->IsRoomExplored(B->Id) || StairCores.Contains(B->Id));
        if (!bKnownA && !bKnownB) continue;
        const FVector Across = FVector::CrossProduct(Portal.Normal, FVector::UpVector).GetSafeNormal() * Portal.Width * .5f;
        const FVector2D Near = Project(Portal.Center - Across), Far = Project(Portal.Center + Across);
        const FVector2D Center = Project(Portal.Center);
        const bool bOpen = Portal.Gate == EGate::Open || Tower->IsGateOpen(Portal.Id);
        Line(Near, Far, PlanInk, 4.f);
        if (bOpen)
        {
            Line(Near, FMath::Lerp(Near, Far, .28f), NorthPaper, 1.65f);
            Line(FMath::Lerp(Near, Far, .72f), Far, NorthPaper, 1.65f);
            const FRoom* Beyond = bKnownA ? B : A;
            if (bKnownA != bKnownB && Beyond)
            {
                const float Sign = FVector::DotProduct(Beyond->Bounds.GetCenter() - Portal.Center, Portal.Normal) >= 0. ? 1.f : -1.f;
                const FVector2D Direction = NorthMapPlane(Portal.Normal * Sign).GetSafeNormal();
                Arrow(Center + Direction * 8.f * S, Direction, 4.f * S, NorthPaper);
            }
        }
        else
        {
            Line(Near, Far, NorthGold, 2.5f);
            Diamond(Center, 4.f * S, NorthGold);
        }
    }

    ADuelNorthTower::FObjectiveRoute Route;
    if (Floor == CurrentFloor && Tower->BuildObjectiveRoute(Location, CurrentFloor, Route))
    {
        // Trace the visible part of the route. End it at a frontier or the outgoing stair.
        FVector2D From = Project(PlayerAt);
        FName Previous = Route.StartRoom;
        const auto Dashes = [&](const FVector2D& To)
        {
            const FVector2D Direction = (To - From).GetSafeNormal();
            const double Distance = FVector2D::Distance(From, To);
            for (double D = 7.f * S; D + 3.f * S < Distance; D += 9.f * S)
                Line(From + Direction * D, From + Direction * (D + 4.f * S), NorthViolet.CopyWithNewOpacity(.68f), 1.55f);
        };
        TOptional<FVector2D> RouteEnd;
        bool bReached = true;
        for (const FPortal* Portal : Route.Doors)
        {
            const FName Next = Portal->RoomA == Previous ? Portal->RoomB : Portal->RoomA;
            const FRoom* Room = FindRoom(Next);
            if (FMath::Abs(Portal->Center.Z - Location.SurfaceZ) > 200.f) { bReached = false; break; }
            const FVector2D To = Project(Portal->Center);
            Dashes(To);
            RouteEnd = To;
            if ((!Tower->IsRoomExplored(Next) && !StairCores.Contains(Next)) || !Room || Room->Floor != Floor)
            {
                bReached = false;
                break;
            }
            From = To;
            Previous = Next;
        }
        if (bReached && Route.GoalFloor == Floor)
        {
            RouteEnd = Project(Route.Goal);
            Dashes(RouteEnd.GetValue());
        }
        if (RouteEnd.IsSet())
        {
            Diamond(RouteEnd.GetValue(), 11.f * S, NorthViolet.CopyWithNewOpacity(.47f));
            Diamond(RouteEnd.GetValue(), 6.f * S, NorthViolet);
        }
    }
    for (const FRoom& Room : Rooms())
    {
        if (Room.Floor != Floor || !Tower->IsRoomExplored(Room.Id) || Room.Kind == ERoomKind::Corridor ||
            Room.Kind == ERoomKind::Stairwell || Room.Kind == ERoomKind::Landing) continue;
        const FBox2D Box = RoomBox(Room);
        const TCHAR* Name = NorthRoomName(Room);
        if (Box.GetSize().X >= LabelWidth(Name, 12.f * S) + 15.f * S && Box.GetSize().Y > 23.f * S)
        {
            Label(Name, Box.Min.X + 6.f * S, Box.Min.Y + 5.f * S, 12.f * S, NorthPaper);
        }
    }
    for (const FCheckpoint& Checkpoint : Checkpoints())
    {
        if (Checkpoint.Floor != Floor) continue;
        const FLocation At = Locate(Checkpoint.Position + FVector(0, 0, 62.f));
        const bool bLit = Tower->IsTravelPointActivated(Checkpoint.Floor);
        if (!Tower->IsRoomExplored(At.Room) && !bLit) continue;
        const FVector2D Point = Project(Checkpoint.Position);
        const bool bSelected = Tower->SelectedTravelPoint() == Checkpoint.Floor;
        const bool bHover = Tower->CanTravelTo(Checkpoint.Floor) &&
            MenuButton(ADuelNorthTower::TravelIconMenuBase + static_cast<int32>(Checkpoint.Floor),
                Point.X - 14.f * S, Point.Y - 14.f * S, 28.f * S, 28.f * S);
        if (bSelected || bHover) Diamond(Point, 13.f * S, NorthGold);
        Chrome(13, Point.X - 6.f * S, Point.Y - 6.f * S, 12.f * S, 12.f * S,
            bLit ? NorthGold : NorthStone);
    }
    if (Floor == CurrentFloor)
    {
        const FVector2D At = Project(PlayerAt);
        const FVector2D Direction = NorthMapPlane(Player->GetActorForwardVector()).GetSafeNormal();
        const FVector2D Side(-Direction.Y, Direction.X);
        const FVector2D Tip = At + Direction * 12.f * S;
        const FVector2D Left = At - Direction * 7.f * S + Side * 6.f * S;
        const FVector2D Right = At - Direction * 7.f * S - Side * 6.f * S;
        Line(Tip, Left, NorthBlue, 2.6f);
        Line(Left, Right, NorthBlue, 2.6f);
        Line(Right, Tip, NorthBlue, 2.6f);
    }

    const FRoom* Room = FocusRoom ? FocusRoom : Floor == CurrentFloor ? FindRoom(Location.Room) : nullptr;
    Label(Room && Tower->IsRoomExplored(Room->Id) ? NorthRoomName(*Room) : FloorName(Floor),
        NotesX, MapTop + 11.f * S, 19.f * S, NorthPaper);
    Label(TEXT("当前目标"), NotesX, MapTop + 59.f * S, 14.f * S, NorthViolet);
    const TArray<FString> MapTask = WrapLabel(Objective, 14.f * S, NotesW);
    for (int32 I = 0; I < MapTask.Num(); ++I)
    {
        Label(MapTask[I], NotesX, MapTop + (83.f + I * 21.f) * S, 14.f * S, NorthPaper);
    }
    DrawRect(PlanGold.CopyWithNewOpacity(.40f), NotesX, MapTop + 221.f * S, NotesW, Rule);
    Label(TEXT("快速旅行"), NotesX, MapTop + 235.f * S, 16.f * S, NorthGold);
    Label(TEXT("只可前往已点亮的休整点"), NotesX, MapTop + 258.f * S, 11.f * S, NorthPaper);
    int32 TravelRow = 0, Available = 0;
    for (const auto RestFloor : MapOrder)
    {
        if (RestFloor == EFloor::Garden || RestFloor == EFloor::Beacon || !Tower->HasExploredFloor(RestFloor)) continue;
        const float RowY = MapTop + (279.f + TravelRow++ * 28.f) * S;
        const bool bLit = Tower->IsTravelPointActivated(RestFloor);
        const bool bAllowed = Tower->CanTravelTo(RestFloor);
        Available += bAllowed;
        const bool bHover = bAllowed && MenuButton(ADuelNorthTower::TravelMenuBase + static_cast<int32>(RestFloor),
            NotesX - 4.f * S, RowY - 3.f * S, NotesW + 8.f * S, 25.f * S);
        const bool bSelected = Tower->SelectedTravelPoint() == RestFloor;
        if (bSelected || bHover) FocusBand(NotesX - 4.f * S, RowY - 3.f * S, NotesW + 8.f * S, 25.f * S, S);
        Chrome(13, NotesX, RowY + 2.f * S, 11.f * S, 11.f * S, bLit ? NorthGold : NorthStone);
        Label(ADuelNorthTower::TravelPointName(RestFloor), NotesX + 20.f * S, RowY, 12.f * S,
            bLit ? NorthPaper : NorthPaper.CopyWithNewOpacity(.40f));
    }
    const float ConfirmY = MapTop + 425.f * S;
    const bool bCanConfirm = Tower->CanTravelTo(Tower->SelectedTravelPoint());
    const bool bConfirmHover = bCanConfirm && MenuButton(ADuelNorthTower::TravelMenuConfirm,
        NotesX, ConfirmY, NotesW, 26.f * S);
    if (bCanConfirm || bConfirmHover) FocusBand(NotesX, ConfirmY, NotesW, 26.f * S, S);
    Label(bCanConfirm ? TEXT("F / Enter  前往所选休整点") : TEXT("选择金色火灯后确认"),
        NotesX + 5.f * S, ConfirmY + 5.f * S, 11.f * S, bCanConfirm ? NorthGold : NorthStone);
    const FString TravelNote = !Tower->TravelMessage().IsEmpty() ? Tower->TravelMessage() :
        Available == 0 ? TEXT("靠近休整点按 F，点亮后才能旅行。") : TEXT("金色火灯：已点亮  灰色：未点亮");
    const auto TravelLines = WrapLabel(TravelNote, 10.f * S, NotesW);
    for (int32 I = 0; I < FMath::Min(2, TravelLines.Num()); ++I)
        Label(TravelLines[I], NotesX, MapTop + (459.f + I * 13.f) * S, 10.f * S, NorthPaper);
}
