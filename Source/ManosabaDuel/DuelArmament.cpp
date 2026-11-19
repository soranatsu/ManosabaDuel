#include "DuelGame.h"

#include "DuelArena.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"

namespace DuelCourt
{
    namespace
    {
        const FArmament Weapons[] =
        {
            {
                TEXT("绯纹细剑"), EDuelArt::Orb, FLinearColor(5.f, .04f, .13f),
                {{.20f, .14f, .34f, 15.f, 53.f}, 0.f, 62.f, 16.f},
                {{.23f, .14f, .38f, 16.f, 66.f}, 0.f, 76.f, 20.f},
                {{.44f, .20f, .54f, 27.f, 86.f}, 0.f, 115.f, 31.f},
            },
            {
                TEXT("沉金重刃"), EDuelArt::Burst, FLinearColor(5.5f, 1.3f, .24f),
                {{.36f, .19f, .50f, 22.f, 42.f}, 0.f, 95.f, 29.f},
                {{.42f, .20f, .54f, 23.f, 57.f}, 0.f, 113.f, 36.f},
                {{.65f, .24f, .70f, 34.f, 75.f}, 0.f, 174.f, 54.f},
            },
            {
                TEXT("雾行长枪"), EDuelArt::Thrust, FLinearColor(.22f, 3.6f, 5.1f),
                {{.25f, .15f, .31f, 16.f, 66.f}, 0.f, 56.f, 13.f},
                {{.29f, .19f, .37f, 18.f, 58.f}, 0.f, 73.f, 18.f},
                {{.46f, .18f, .50f, 25.f, 97.f}, 0.f, 112.f, 30.f},
            },
            {
                TEXT("裁决战斧"), EDuelArt::Whirl, FLinearColor(2.9f, .32f, 4.7f),
                {{.28f, .17f, .41f, 19.f, 51.f}, 0.f, 82.f, 25.f},
                {{.30f, .19f, .43f, 21.f, 62.f}, 0.f, 96.f, 31.f},
                {{.55f, .23f, .64f, 31.f, 76.f}, 0.f, 151.f, 46.f},
            },
            {
                TEXT("赎罪战锤"), EDuelArt::Burst, FLinearColor(5.0f, 2.6f, .34f),
                {{.32f, .18f, .43f, 21.f, 48.f}, 0.f, 74.f, 35.f},
                {{.36f, .19f, .47f, 22.f, 55.f}, 0.f, 88.f, 44.f},
                {{.61f, .24f, .68f, 36.f, 70.f}, 0.f, 156.f, 79.2f},
            },
            {TEXT("猎犬长牙"), EDuelArt::HoundFinesse, FLinearColor(2.6f, 2.4f, 2.1f),
                {{.30f, .18f, .43f, 21.f, 54.f}, 0.f, 93.f, 28.f},
                {{.35f, .20f, .49f, 22.f, 70.f}, 0.f, 108.f, 32.f},
                {{.53f, .24f, .62f, 31.f, 99.f}, 0.f, 152.f, 51.f}},
            {TEXT("魔女法杖"), EDuelArt::IceLance, FLinearColor(.70f, 1.64f, 4.5f),
                {{.26f, .17f, .39f, 16.f, 42.f}, 0.f, 59.f, 19.f},
                {{.28f, .19f, .39f, 17.f, 56.f}, 0.f, 69.f, 22.f},
                {{.45f, .23f, .52f, 24.f, 66.f}, 0.f, 97.f, 32.f}},
            {TEXT("小雪的钢笔"), EDuelArt::PenScript, FLinearColor(.50f, 2.40f, 4.3f),
                {{.22f, .15f, .31f, 14.f, 57.f}, 0.f, 60.f, 16.f},
                {{.24f, .17f, .33f, 15.f, 72.f}, 0.f, 70.f, 21.f},
                {{.41f, .21f, .46f, 23.f, 81.f}, 0.f, 103.f, 31.f}},
            {TEXT("烧火棍"), EDuelArt::PokerRake, FLinearColor(5.3f, 1.03f, .27f),
                {{.25f, .17f, .34f, 17.f, 55.f}, 0.f, 69.f, 23.f},
                {{.29f, .18f, .38f, 18.f, 76.f}, 0.f, 82.f, 29.f},
                {{.46f, .23f, .54f, 26.f, 108.f}, 0.f, 117.f, 43.f}},
            {TEXT("仪式剑"), EDuelArt::RitualOffering, FLinearColor(4.8f, 1.15f, .87f),
                {{.28f, .18f, .39f, 18.f, 56.f}, 0.f, 78.f, 22.f},
                {{.33f, .19f, .42f, 19.f, 66.f}, 0.f, 93.f, 27.f},
                {{.53f, .22f, .57f, 29.f, 88.f}, 0.f, 137.f, 40.f}},
        };

        const FWeaponArt Arts[] =
        {
            {TEXT("追星"), TEXT("发射绯红光弹，压制远处目标"),
                FLinearColor(5.f, .04f, .13f), {{.67f, .15f, .62f, 18.f}, 0.f, 134.f, 34.f}, 18.f},
            {TEXT("余烬地裂"), TEXT("砸开近身地面，积累大量架势伤害"),
                FLinearColor(5.5f, 1.3f, .24f), {{.70f, .24f, .71f, 23.f, 62.f}, 375.f, 196.f, 58.f}, 22.f},
            {TEXT("断潮突进"), TEXT("朝前突进，沿武器前端刺击"),
                FLinearColor(.22f, 3.6f, 5.1f), {{.42f, .31f, .54f, 18.f, 255.f}, 0.f, 172.f, 44.f}, 17.f},
            {TEXT("断誓回旋"), TEXT("沿武器轨迹扫击。战斧按住 E 后松手，满蓄力伤害 +18%、架势 +35%。"),
                FLinearColor(2.9f, .32f, 4.7f), {{.46f, .32f, .59f, 22.f, 105.f}, 0.f, 162.f, 55.f}, 21.f},
            {TEXT("绯影穿步"), TEXT("希罗 · 压低身形迅速穿步，以刃尖刺击"),
                FLinearColor(5.2f, .055f, .41f), {{.28f, .28f, .47f, 17.f, 288.f}, 0.f, 149.f, 39.f}, 17.f},
            {TEXT("双重封印"), TEXT("希罗 · 在前方落下血印，随后原地再爆发一次"),
                FLinearColor(4.5f, .11f, 1.38f), {{.66f, .45f, .65f, 21.f}, 240.f, 93.f, 26.f}, 22.f},
            {TEXT("圣裁坠光"), TEXT("艾玛 · 在锁定位置引下圣光，落点在起手时固定"),
                FLinearColor(5.6f, 2.7f, .56f), {{.83f, .18f, .63f, 21.f}, 246.f, 156.f, 43.f}, 24.f},
            {TEXT("白骨誓约"), TEXT("艾玛 · 短暂架势，接住正面可格挡攻击并反击"),
                FLinearColor(5.1f, 3.75f, 1.71f), {{.16f, .35f, .53f, 17.f}, 470.f, 152.f, 67.f}, 15.f},
            {TEXT("绯线三叠"), TEXT("希罗 · 中央刃影直进，两侧刃影沿弧线掠过"),
                FLinearColor(5.5f, .18f, .73f), {{.55f, .14f, .60f, 19.f}, 0.f, 56.f, 15.f}, 22.f},
            {TEXT("三重烛印"), TEXT("艾玛 · 左右烛印交错点燃，最后照亮中央"),
                FLinearColor(5.3f, 2.51f, .62f), {{.58f, .70f, .52f, 21.f}, 187.f, 91.f, 27.f}, 24.f},
            {TEXT("绯影返身"), TEXT("希罗 · 后撤避锋，从原位释放刃影"),
                FLinearColor(5.4f, .16f, .84f), {{.18f, .30f, .51f, 18.f, 270.f}, 0.f, 104.f, 31.f}, 17.f},
            {TEXT("赦罪冠环"), TEXT("艾玛 · 展开圣环，惩击环带内的敌人"),
                FLinearColor(5.6f, 3.69f, 1.12f), {{.76f, .20f, .68f, 23.f}, 705.f, 181.f, 51.f}, 28.f},
            {TEXT("猎犬步法"), TEXT("踏前挥刃，随后后撤。再次按战技可突进追斩"),
                FLinearColor(2.7f, 3.f, 3.3f), {{.20f, .32f, .24f, 21.f, 254.f}, 0.f, 116.f, 30.f}, 16.f},
            {TEXT("猎犬追斩"), TEXT("踏影穿步，沿弯刃斩入目标身前"),
                FLinearColor(2.9f, 3.2f, 4.2f), {{.20f, .30f, .36f, 23.f, 485.f}, 0.f, 129.f, 39.f}, 12.f},
            {TEXT("小雪的批注"), TEXT("首笔命中后，第二笔转向目标所在位置"),
                FLinearColor(.60f, 2.85f, 5.4f), {{.46f, .37f, .52f, 17.f}, 1050.f, 72.f, 31.f}, 21.f},
            {TEXT("焚证钩击"), TEXT("希罗 · 向前跨步挑击，余烬沿挑击轨迹飞溅"),
                FLinearColor(5.5f, .86f, .16f), {{.32f, .26f, .43f, 21.f, 205.f}, 0.f, 145.f, 63.f}, 19.f},
            {TEXT("冰棘"), TEXT("轻按 E 快放，按住蓄力后松手。满蓄力冰棘更快，伤害和架势 +38%。"),
                FLinearColor(.52f, 2.41f, 5.6f), {{.39f, .13f, .49f, 13.f}, 0.f, 102.f, 23.f}, 13.f},
            {TEXT("女巫星雨"), TEXT("法杖魔法 · 三枚星火以三角阵形落下"),
                FLinearColor(2.1f, .70f, 5.3f), {{.91f, .23f, .61f, 19.f}, 246.f, 175.f, 43.f}, 25.f},
            {TEXT("幽刃术"), TEXT("法杖魔法 · 向前踏步，扫出一对宽幅交错刃影"),
                FLinearColor(1.08f, 2.62f, 5.2f), {{.33f, .22f, .47f, 16.f, 135.f}, 515.f, 161.f, 43.f}, 18.f},
            {TEXT("回丝断证"), TEXT("希罗 · 从锁定点背后拉回刃丝，沿预示的轨迹切过敌人"),
                FLinearColor(4.1f, .12f, .60f), {{.55f, .12f, .45f, 17.f}, 840.f, 148.f, 43.f}, 20.f},
            {TEXT("守证辉幕"), TEXT("艾玛 · 四秒内减轻一次所受伤害，也能抵挡范围招式"),
                FLinearColor(4.1f, 3.1f, 1.55f), {{.33f, .10f, .42f, 13.f}, 0.f, 0.f, 0.f}, 20.f},
            {TEXT("献血仪式"), TEXT("献出当前生命的 70%，15 秒内普攻伤害 +100%，架势伤害 +50%"),
                FLinearColor(5.1f, .21f, .48f), {{.40f, .12f, .42f}, 0.f, 0.f, 0.f}, 0.f},
            {TEXT("引路灯灵"), TEXT("缓慢转向锁定目标的灯火。适合对付横向移动。"),
                FLinearColor(2.8f, 1.57f, .51f), {{.42f, .17f, .49f, 15.f}, 0.f, 124.f, 22.f}, 19.f},
            {TEXT("沉墨井"), TEXT("锁定地面，先后炸开两次。第二次覆盖更宽。"),
                FLinearColor(1.49f, .58f, 4.3f), {{.68f, .63f, .48f, 21.f}, 360.f, 124.f, 38.f}, 29.f},
            {TEXT("盐焰弧"), TEXT("扫过身前的宽扇面，对近身敌人累积架势。"),
                FLinearColor(4.4f, 1.63f, .44f), {{.47f, .24f, .50f, 23.f, 95.f}, 591.f, 132.f, 58.f}, 24.f},
            {TEXT("白幕护体"), TEXT("三秒内减轻下一次伤害，破幕时反击近身者。"),
                FLinearColor(2.7f, 3.25f, 4.2f), {{.29f, .12f, .34f, 14.f}, 375.f, 88.f, 35.f}, 22.f},
            {TEXT("证人步"), TEXT("迅速横移，留下一束从原位置射出的光。"),
                FLinearColor(.72f, 2.4f, 3.65f), {{.19f, .22f, .37f, 19.f, 325.f}, 0.f, 83.f, 24.f}, 23.f},
            {TEXT("星轨束缚"), TEXT("锁定时在目标脚下落印，命中后延缓架势恢复。"),
                FLinearColor(3.1f, .75f, 5.5f), {{.60f, .14f, .55f, 18.f}, 225.f, 116.f, 55.f}, 27.f},
            {TEXT("回霜环"), TEXT("冰环外展后折返，中心留空。每次施法至多命中一次。"),
                FLinearColor(.55f, 3.05f, 5.1f), {{.40f, .18f, .43f, 20.f}, 395.f, 134.f, 51.f}, 24.f},
            {TEXT("折光裁决"), TEXT("在锁定点布下六角镜印，镜脉破裂时造成高伤害与架势冲击。"),
                FLinearColor(.73f, 3.35f, 6.5f), {{.85f, .24f, .65f, 24.f}, 455.f, 248.f, 85.f}, 42.f},
            {TEXT("灼罪回锋"), TEXT("向后退开，火浪沿原位的扇形环带卷起，近身中心留空。"),
                FLinearColor(6.3f, 1.23f, .28f), {{.47f, .27f, .49f, 23.f, 154.f}, 760.f, 228.f, 74.f}, 37.f},
            {TEXT("衡罪十字"), TEXT("四角可以避开。按住 E 蓄力后松手，满蓄力伤害 +30%、架势 +80%。"),
                FLinearColor(5.8f, 3.32f, .75f), {{.71f, .19f, .52f, 21.f}, 715.f, 197.f, 66.f}, 32.f},
            {TEXT("月蚀潮"), TEXT("月牙暗处可避开。按住 E 蓄力后松手，满蓄力伤害 +30%，命中回复生命。"),
                FLinearColor(2.34f, 1.47f, 5.9f), {{.63f, .23f, .57f, 23.f}, 690.f, 218.f, 69.f}, 35.f},
        };
        static_assert(UE_ARRAY_COUNT(Weapons) == static_cast<int32>(EDuelWeapon::FirePoker) + 2);
        static_assert(UE_ARRAY_COUNT(Arts) == static_cast<int32>(EDuelArt::MoonCrescent));

        constexpr EDuelArt HiroArts[] = {EDuelArt::Default, EDuelArt::Orb, EDuelArt::Burst, EDuelArt::Thrust,
            EDuelArt::Whirl, EDuelArt::HiroStep, EDuelArt::HiroBind, EDuelArt::HiroThreads, EDuelArt::HiroReprise, EDuelArt::HiroCrossThread};
        constexpr EDuelArt EmaArts[] = {EDuelArt::Default, EDuelArt::Orb, EDuelArt::Burst, EDuelArt::Thrust,
            EDuelArt::Whirl, EDuelArt::EmaLight, EDuelArt::EmaWard, EDuelArt::EmaVotive, EDuelArt::EmaAbsolution, EDuelArt::EmaAegis};

        TConstArrayView<EDuelArt> AvailableArts(EDuelCharacter Character)
        {
            return Character == EDuelCharacter::Hiro ? MakeArrayView(HiroArts) : MakeArrayView(EmaArts);
        }
    }

    const FArmament& Armament(EDuelWeapon Weapon)
    {
        check(IsBlade(Weapon));
        return Weapon == EDuelWeapon::RitualSword ? Weapons[UE_ARRAY_COUNT(Weapons) - 1] : Weapons[static_cast<uint8>(Weapon)];
    }

    bool IsBlade(EDuelWeapon Weapon)
    {
        return Weapon <= EDuelWeapon::FirePoker || Weapon == EDuelWeapon::RitualSword;
    }

    bool CanEquip(EDuelWeapon Weapon, EDuelCharacter Character, EDuelHand Hand)
    {
        return Weapon <= EDuelWeapon::RitualSword && (Weapon != EDuelWeapon::Empty || Hand == EDuelHand::Left) &&
            (Weapon != EDuelWeapon::YukiPen || Character == EDuelCharacter::Ema) &&
            (Weapon != EDuelWeapon::FirePoker || Character == EDuelCharacter::Hiro);
    }

    EDuelWeapon NextWeapon(EDuelWeapon Current, EDuelHand Hand, EDuelCharacter Character, int32 Step, EDuelWeapon Other)
    {
        constexpr int32 Count = static_cast<int32>(EDuelWeapon::RitualSword) + 1;
        int32 Index = static_cast<int32>(Current);
        do
        {
            Index = (Index + Step + Count) % Count;
            Current = static_cast<EDuelWeapon>(Index);
        } while (Current == Other || !CanEquip(Current, Character, Hand));
        return Current;
    }

    EDuelArt ResolveArt(EDuelWeapon Weapon, EDuelArt Selection)
    {
        if (!IsBlade(Weapon))
        {
            return EDuelArt::Default;
        }
        const bool bFixed = (Weapon >= EDuelWeapon::Houndfang && Weapon <= EDuelWeapon::FirePoker) ||
            Weapon == EDuelWeapon::RitualSword;
        return Selection == EDuelArt::Default || bFixed ? Armament(Weapon).NativeArt : Selection;
    }

    bool SupportsArt(EDuelArt Art, EDuelCharacter Character)
    {
        return AvailableArts(Character).Contains(Art);
    }

    EDuelArt NextArt(EDuelArt Current, EDuelCharacter Character, int32 Step)
    {
        const TConstArrayView<EDuelArt> ArtsForCharacter = AvailableArts(Character);
        const int32 Count = ArtsForCharacter.Num();
        const int32 Index = FMath::Max(0, ArtsForCharacter.IndexOfByKey(Current));
        return ArtsForCharacter[(Index + Step + Count) % Count];
    }

    bool IsBladeArt(EDuelArt Art)
    {
        return Art == EDuelArt::Thrust || Art == EDuelArt::Whirl || Art == EDuelArt::HiroStep ||
            Art == EDuelArt::HoundFinesse || Art == EDuelArt::HoundChase || Art == EDuelArt::PokerRake;
    }

    const FWeaponArt& WeaponArt(EDuelArt Art)
    {
        check(Art > EDuelArt::Default && Art <= EDuelArt::MoonCrescent);
        return Arts[static_cast<int32>(Art) - 1];
    }

    int32 FocusCost(EDuelArt Art)
    {
        if (Art == EDuelArt::RitualOffering)
        {
            return FMath::RoundToInt(MaxFocus * .5f);
        }
        if (Art == EDuelArt::Default)
        {
            return 0;
        }
        const float Scale = MakeArrayView(StaffSpells).Contains(Art) ? StaffSpellCostScale : WeaponArtCostScale;
        return FMath::RoundToInt(WeaponArt(Art).FocusCost * Scale);
    }

    bool IsShield(EDuelWeapon Weapon)
    {
        return Weapon == EDuelWeapon::Buckler || Weapon == EDuelWeapon::Greatshield;
    }

    const TCHAR* EquipmentName(EDuelWeapon Weapon)
    {
        switch (Weapon)
        {
        case EDuelWeapon::Buckler: return TEXT("薄银盾");
        case EDuelWeapon::Greatshield: return TEXT("断碑巨盾");
        case EDuelWeapon::Empty: return TEXT("空手");
        default: return Armament(Weapon).Name;
        }
    }

    const TCHAR* EquipmentEffect(EDuelWeapon Weapon)
    {
        switch (Weapon)
        {
        case EDuelWeapon::Rapier: return TEXT("快攻接斩。重击对准剑尖，伤害 +22%。");
        case EDuelWeapon::Greatblade: return TEXT("重击抓收势，伤害 +18%、架势 +10%。");
        case EDuelWeapon::Spear: return TEXT("迎正面用枪尖，伤害 +25%、架势 +18%。");
        case EDuelWeapon::Axe: return TEXT("按住 E 蓄回旋，亮起后松手破架势。");
        case EDuelWeapon::Hammer: return TEXT("目标架势达 15 后再重击，可多打 20% 架势。");
        case EDuelWeapon::Houndfang: return TEXT("E 踏前斜斩再后撤，短时间内再按 E 追斩 4.85 米。");
        case EDuelWeapon::Staff: return TEXT("法术 MP 约减 37%。冰棘、十字、月蚀可按住 E 蓄力。");
        case EDuelWeapon::YukiPen: return TEXT("首笔命中锁定敌人，0.32 秒后追补一笔，伤害为首笔的 1.5 倍。");
        case EDuelWeapon::FirePoker: return TEXT("钩击截住正在移动的敌人，额外造成 25 点架势伤害。");
        case EDuelWeapon::Buckler: return TEXT("适合贴身盾反。反击窗口 0.18 秒，架势伤害 36。");
        case EDuelWeapon::Greatshield: return TEXT("挡得更宽，耗力较少。反击窗口 0.12 秒，架势伤害 49。");
        case EDuelWeapon::Empty: return TEXT("副手留空，主手照常使用。");
        case EDuelWeapon::RitualSword: return TEXT("献出当前生命的 70%，普攻伤害翻倍，持续 15 秒。");
        default: return nullptr;
        }
    }

    FVector2D BladeSpan(EDuelWeapon Weapon)
    {
        switch (Weapon)
        {
        case EDuelWeapon::Rapier: return FVector2D(17, 132);
        case EDuelWeapon::Greatblade: return FVector2D(17, 185);
        case EDuelWeapon::Spear: return FVector2D(154, 235);
        case EDuelWeapon::Axe: return FVector2D(95, 166);
        case EDuelWeapon::Hammer: return FVector2D(91, 142);
        case EDuelWeapon::Houndfang: return FVector2D(19, 182);
        case EDuelWeapon::Staff: return FVector2D(36, 146);
        case EDuelWeapon::YukiPen: return FVector2D(103, 129);
        case EDuelWeapon::FirePoker: return FVector2D(24, 147);
        case EDuelWeapon::RitualSword: return FVector2D(24, 169);
        default: return FVector2D::ZeroVector;
        }
    }

    const FStrike* Strike(EDuelWeapon Weapon, EDuelMove Move, EDuelArt Art)
    {
        if (!IsBlade(Weapon))
        {
            return nullptr;
        }
        const FArmament& Set = Armament(Weapon);
        switch (Move)
        {
        case EDuelMove::Cut: return &Set.Cut;
        case EDuelMove::Backcut: return &Set.Follow;
        case EDuelMove::Heavy: return &Set.Heavy;
        case EDuelMove::Crimson: return &WeaponArt(Art == EDuelArt::Default ? Set.NativeArt : Art).Strike;
        default: return nullptr;
        }
    }
}

FVector ADuelFighter::ArtGround(const FVector& At) const
{
    if (!bCombatOver)
    {
        return Arena->GroundMark(At);
    }
    const FVector Here = GetActorLocation();
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(DuelArtFloor), false, this);
    FHitResult Wall;
    FVector Destination(At.X, At.Y, Here.Z);
    if (GetWorld()->LineTraceSingleByChannel(Wall, Here, Destination, ECC_Pawn, Query))
    {
        Destination = Wall.Location - (Destination - Here).GetSafeNormal2D() * 12.f;
    }
    FHitResult Floor;
    if (GetWorld()->LineTraceSingleByChannel(Floor, Destination + FVector(0, 0, 46), Destination - FVector(0, 0, 640), ECC_Pawn, Query))
    {
        return Floor.ImpactPoint + FVector(0, 0, 8);
    }
    return Destination - FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 8.f);
}

void ADuelFighter::ClearChargeSeal()
{
    if (auto* Seal = ChargeSeal.Get()) Seal->DestroyComponent();
    ChargeSeal.Reset();
}

void ADuelFighter::PrepareArt(const DuelCourt::FMoveTiming& Spec)
{
    const DuelCourt::FWeaponArt& Art = DuelCourt::WeaponArt(MoveArt);
    const FVector At = GetActorLocation();
    Arena->Flash(At + ActionDirection * 45.f + FVector(0, 0, 38), Art.Glow, 3300.f, Spec.Windup, 470.f);
    Arena->Sound(EDuelSound::Charge, At);
    switch (MoveArt)
    {
    case EDuelArt::Orb:
        Arena->Sigil(ArtGround(At), 105.f, Art.Glow, Spec.Windup, EDuelMark::StarOrb, ActionDirection);
        break;
    case EDuelArt::IceLance:
        Arena->Sigil(ArtGround(At), 119.f, Art.Glow, Spec.Windup, EDuelMark::IceLance, ActionDirection);
        break;
    case EDuelArt::LanternMote:
        Arena->Sigil(ArtGround(At), 109.f, Art.Glow, Spec.Windup, EDuelMark::Lantern, ActionDirection);
        break;
    case EDuelArt::SableBlade:
        Arena->Sigil(ArtGround(At), 132.f, Art.Glow, Spec.Windup, EDuelMark::Sable, ActionDirection);
        break;
    case EDuelArt::SaltArc:
        Arena->Sigil(ArtGround(At), 127.f, Art.Glow, Spec.Windup, EDuelMark::SaltArc, ActionDirection);
        break;
    case EDuelArt::HiroStep:
        Arena->Sigil(ArtGround(At), 105.f, Art.Glow, Spec.Windup, EDuelMark::Step, ActionDirection);
        break;
    case EDuelArt::HiroThreads:
        Arena->Sigil(ArtGround(At), 119.f, Art.Glow, Spec.Windup, EDuelMark::Thread, ActionDirection);
        break;
    case EDuelArt::HiroBind:
        CastOrigin = ArtGround(At + ActionDirection * 435.f);
        Arena->Sigil(CastOrigin, Art.Strike.Reach, Art.Glow, Spec.Windup, EDuelMark::Bind, ActionDirection);
        break;
    case EDuelArt::EmaLight:
    case EDuelArt::WitchRain:
    {
        const double Range = bLockOn ? FMath::Min(FVector::Dist2D(At, Target->GetActorLocation()), 1190.0) : 620.0;
        CastOrigin = ArtGround(At + ActionDirection * Range);
        if (MoveArt == EDuelArt::WitchRain)
        {
            for (int32 I = 0; I < SealCenters.Num(); ++I)
            {
                const FVector Axis = ActionDirection.RotateAngleAxis(120.f * I, FVector::UpVector);
                SealCenters[I] = ArtGround(CastOrigin + Axis * 102.f);
                Arena->Sigil(SealCenters[I], Art.Strike.Reach * .68f, Art.Glow, Spec.Windup,
                    EDuelMark::WitchRain, ActionDirection);
            }
        }
        else
        {
            Arena->Sigil(CastOrigin, Art.Strike.Reach, Art.Glow, Spec.Windup, EDuelMark::LightRosette, ActionDirection);
        }
        break;
    }
    case EDuelArt::Blackwell:
    {
        const double Range = bLockOn && IsValid(Target) ? FMath::Min(FVector::Dist2D(At, Target->GetActorLocation()), 1360.0) : 685.0;
        CastOrigin = ArtGround(At + ActionDirection * Range);
        Arena->Halo(CastOrigin, 52.f, Art.Strike.Reach, Art.Glow * .57f, Spec.Windup + .52f, false, EDuelMark::InkWell);
        Arena->Sigil(CastOrigin, Art.Strike.Reach * .62f, Art.Glow, Spec.Windup, EDuelMark::InkWell);
        break;
    }
    case EDuelArt::WhiteMantle:
        Arena->Halo(ArtGround(At), 78.f, 183.f, Art.Glow, Spec.Windup + .23f, false, EDuelMark::Mantle);
        break;
    case EDuelArt::StarTrack:
    {
        const FVector Offset = bLockOn && IsValid(Target) ?
            (Target->GetActorLocation() - At).GetClampedToMaxSize2D(1240.) : ActionDirection * 620.;
        CastOrigin = ArtGround(At + Offset);
        Arena->Sigil(CastOrigin, Art.Strike.Reach, Art.Glow, Spec.Windup, EDuelMark::StarTrack);
        break;
    }
    case EDuelArt::ReturningFrost:
        CastOrigin = ArtGround(At);
        Arena->Halo(CastOrigin, DuelCourt::ReturningFrostInner, Art.Strike.Reach, Art.Glow,
            Spec.Windup, false, EDuelMark::ReturningFrost);
        break;
    case EDuelArt::PrismSeal:
    {
        const FVector Offset = bLockOn && IsValid(Target) ?
            (Target->GetActorLocation() - At).GetClampedToMaxSize2D(1380.) : ActionDirection * 675.;
        CastOrigin = ArtGround(At + Offset);
        CastAxis = ActionDirection;
        Arena->Sigil(CastOrigin, Art.Strike.Reach, Art.Glow, Spec.Windup, EDuelMark::PrismSeal, CastAxis);
        break;
    }
    case EDuelArt::EmberRecoil:
        CastOrigin = ArtGround(At);
        CastAxis = ActionDirection;
        Arena->EmberRecoil(CastOrigin, CastAxis, Art.Strike.Reach, Art.Glow, Spec.Windup, false);
        ActionDirection = -ActionDirection;
        break;
    case EDuelArt::GildedCross:
    case EDuelArt::MoonCrescent:
    {
        const bool bCross = MoveArt == EDuelArt::GildedCross;
        const FVector Offset = bCross ? (bLockOn && IsValid(Target) ?
            (Target->GetActorLocation() - At).GetClampedToMaxSize2D(1210.) : ActionDirection * 665.) : FVector::ZeroVector;
        CastOrigin = ArtGround(At + Offset);
        CastAxis = ActionDirection;
        ChargeSeal = Arena->DrawChargeSeal(CastOrigin, CastAxis, Art.Strike.Reach, Art.Glow, Spec.Windup,
            DuelCourt::ChargeExtension(MoveWeapon, MoveArt), bCross ? EDuelMark::GildedCross : EDuelMark::MoonCrescent);
        break;
    }
    case EDuelArt::WitnessStep:
        CastOrigin = At + FVector(0, 0, 42.f);
        CastAxis = ActionDirection;
        Arena->Sigil(ArtGround(At), 119.f, Art.Glow, Spec.Windup, EDuelMark::Witness, CastAxis);
        break;
    case EDuelArt::PenScript:
        CastOrigin = ArtGround(At + ActionDirection * 505.f);
        CastAxis = ActionDirection;
        Arena->Lane(CastOrigin, CastAxis, Art.Strike.Reach, 62.f, Art.Glow * .42f, Spec.Windup,
            false, false, false, EDuelMark::PenNib);
        break;
    case EDuelArt::EmaWard:
        Arena->Sigil(ArtGround(At), 119.f, Art.Glow, Spec.Windup + Spec.Active, EDuelMark::Ward, ActionDirection);
        break;
    case EDuelArt::EmaAegis:
        Arena->Sigil(ArtGround(At), 152.f, Art.Glow, Spec.Windup, EDuelMark::Aegis, ActionDirection);
        break;
    case EDuelArt::EmaVotive:
    {
        const FVector Across = FVector::CrossProduct(FVector::UpVector, ActionDirection);
        constexpr float Offset[] = {-163.f, 163.f, 0.f};
        for (int32 I = 0; I < SealCenters.Num(); ++I)
        {
            SealCenters[I] = ArtGround(At + ActionDirection * (231.f + I * 292.f) + Across * Offset[I]);
            Arena->Sigil(SealCenters[I], Art.Strike.Reach, Art.Glow, Spec.Windup + I * .29f,
                EDuelMark::Votive, ActionDirection);
        }
        break;
    }
    case EDuelArt::HiroReprise:
        CastOrigin = At + FVector(0, 0, 39.f);
        CastAxis = ActionDirection;
        Arena->Sigil(ArtGround(At), 119.f, Art.Glow, Spec.Windup, EDuelMark::Reprise, CastAxis);
        ActionDirection = -ActionDirection;
        break;
    case EDuelArt::EmaAbsolution:
        CastOrigin = ArtGround(At);
        Arena->Halo(CastOrigin, 242.f, Art.Strike.Reach, Art.Glow * .45f, Spec.Windup, false, EDuelMark::Absolution);
        break;
    case EDuelArt::HiroCrossThread:
    {
        const double Range = bLockOn ? FMath::Min(FVector::Dist2D(At, Target->GetActorLocation()), 930.0) : 440.0;
        const FVector Across = FVector::CrossProduct(FVector::UpVector, ActionDirection);
        const FVector Anchor = ArtGround(At + ActionDirection * Range);
        CastOrigin = ArtGround(Anchor + ActionDirection * 235.f + Across * 125.f);
        CastAxis = (Anchor - CastOrigin).GetSafeNormal2D(UE_SMALL_NUMBER, -ActionDirection);
        Arena->Lane(CastOrigin + CastAxis * (Art.Strike.Reach * .5f), CastAxis, Art.Strike.Reach * .5f,
            71.f, Art.Glow * .43f, Spec.Windup, false, true, false, EDuelMark::CrossThread);
        break;
    }
    default:
        break;
    }
}

void ADuelFighter::BindStrike(const DuelCourt::FStrike& Attack, bool bEcho)
{
    const FLinearColor& Tint = DuelCourt::WeaponArt(MoveArt).Glow;
    Arena->Detonate(CastOrigin, Attack.Reach, bEcho ? Tint : Tint * .66f, bEcho ? .62f : .31f,
        bEcho ? EDuelMark::BindEcho : EDuelMark::Bind);
    if (!bEcho)
    {
        Arena->Sigil(CastOrigin + FVector(0, 0, 1), Attack.Reach, Tint, .39f, EDuelMark::BindEcho);
    }
    if (FVector::DistSquared2D(Target->GetActorLocation(), CastOrigin) <= FMath::Square(Attack.Reach) &&
        Target->ReceiveDuelDamage(Attack.Damage * (bEcho ? .75f : 1.f), CastOrigin))
    {
        Target->DamagePosture(Attack.Posture * (bEcho ? .89f : 1.f));
    }
}

void ADuelFighter::StrikeReturningFrost(const DuelCourt::FStrike& Attack)
{
    if (bHitDelivered || bCombatOver || !IsValid(Target)) return;
    const FVector At = Target->GetActorLocation();
    const double Distance = FVector::DistSquared2D(At, CastOrigin);
    const double Height = At.Z - Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - CastOrigin.Z + 8.;
    if (Distance >= FMath::Square(DuelCourt::ReturningFrostInner) && Distance <= FMath::Square(Attack.Reach) &&
        Height >= -30. && Height <= 72. && Target->ReceiveDuelDamage(Attack.Damage, CastOrigin))
    {
        bHitDelivered = true;
        Target->DamagePosture(Attack.Posture);
        Arena->SpellContact(At + FVector(0, 0, 24), (At - CastOrigin).GetSafeNormal2D(),
            DuelCourt::WeaponArt(MoveArt).Glow, false);
    }
}

void ADuelFighter::CommitArt(const DuelCourt::FStrike& Attack)
{
    const FLinearColor& Tint = DuelCourt::WeaponArt(MoveArt).Glow;
    switch (MoveArt)
    {
    case EDuelArt::HoundFinesse:
        HoundFollowupUntil = GetWorld()->GetTimeSeconds() + 1.26;
        break;
    case EDuelArt::HoundChase:
    case EDuelArt::PokerRake:
        break;
    case EDuelArt::PenScript:
        bHitDelivered = StrikeLine(CastOrigin, CastAxis, Attack.Reach, 62.f, Attack.Damage, Tint * .67f, true);
        if (bHitDelivered && Target->IsAlive())
        {
            CastOrigin = ArtGround(Target->GetActorLocation());
            FMapWave& Finish = MapWaves.AddDefaulted_GetRef();
            Finish.Preview = Arena->DrawSigilArtwork(EDuelMark::PenNib, CastOrigin,
                FVector::CrossProduct(FVector::UpVector, CastAxis),
                FVector2D(Attack.Reach * .61f, 66.f), Tint * .74f, .34f, false, true, 0.f, 1.f, 180.f);
            Arena->SpellContact(Target->GetActorLocation() + FVector(0, 0, 24), CastAxis, Tint, false);
        }
        return;
    case EDuelArt::IceLance:
    {
        const FVector At = GetActorLocation() + ActionDirection * 73.f + FVector(0, 0, 48.f);
        const float Force = bChargedArt ? 1.38f : 1.f;
        Arena->LaunchThread(Target, At, ActionDirection, Attack.Damage * Force, Attack.Posture * Force, Tint,
            bChargedArt ? 2480.f : 2080.f, bChargedArt ? 100.f : 82.f, 1.22f,
            false, 0.f, true, 0.f, EDuelMark::IceLance);
        if (bChargedArt) Arena->Burst(At,Tint,34,510.f,true);
        Arena->Thrust(At, ActionDirection, 234.f, Tint, true, EDuelMark::IceLance);
        return;
    }
    case EDuelArt::LanternMote:
    {
        const FVector At = GetActorLocation() + ActionDirection * 80.f + FVector(0, 0, 58.f);
        const FVector Axis = ActionDirection.RotateAngleAxis(16.f, FVector::UpVector);
        Arena->LaunchThread(Target, At, Axis, Attack.Damage, Attack.Posture, Tint, 1090.f, 101.f, 1.65f,
            false, 0.f, true, bLockOn ? 145.f : 0.f, EDuelMark::Lantern);
        Arena->Sound(EDuelSound::Cut, At);
        return;
    }
    case EDuelArt::Blackwell:
    {
        const float Near = Attack.Reach * .62f;
        Arena->Detonate(CastOrigin, Near, Tint, .47f, EDuelMark::InkWell);
        if (!bCombatOver && IsValid(Target) && FVector::DistSquared2D(Target->GetActorLocation(), CastOrigin) <= FMath::Square(Near) &&
            Target->ReceiveDuelDamage(Attack.Damage, CastOrigin))
        {
            Target->DamagePosture(Attack.Posture);
        }
        return;
    }
    case EDuelArt::SaltArc:
    {
        const FVector At = ArtGround(GetActorLocation());
        constexpr float HalfArc = 75.f;
        Arena->SableSweep(At, ActionDirection, Attack.Reach, Tint, HalfArc, EDuelMark::SaltArc);
        TryMelee(Attack.Reach, FMath::Cos(FMath::DegreesToRadians(HalfArc)), Attack.Damage, Attack.Posture);
        return;
    }
    case EDuelArt::WhiteMantle:
        AegisUntil = GetWorld()->GetTimeSeconds() + 3.0;
        bMantleCounter = true;
        Arena->Burst(GetActorLocation(), Tint, 27, 200.f, true);
        Arena->Sound(EDuelSound::Block, GetActorLocation());
        return;
    case EDuelArt::ReturningFrost:
        Arena->Halo(CastOrigin, DuelCourt::ReturningFrostInner, Attack.Reach, Tint,
            DuelCourt::ReturningFrostRecall, true, EDuelMark::ReturningFrost);
        StrikeReturningFrost(Attack);
        return;
    case EDuelArt::StarTrack:
    {
        Arena->Detonate(CastOrigin, Attack.Reach, Tint, .46f, EDuelMark::StarTrack, ActionDirection);
        if (bCombatOver || !IsValid(Target)) return;
        const FVector TargetAt = Target->GetActorLocation();
        const double Distance = FVector::Dist2D(TargetAt, CastOrigin);
        const double Feet = TargetAt.Z - Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        const double Height = Feet - (CastOrigin.Z - 8.);
        if (Distance <= Attack.Reach && Height >= -30. && Height <= 96. &&
            Target->ReceiveDuelDamage(Attack.Damage, CastOrigin))
        {
            const float Previous = Target->Posture;
            Target->DamagePosture(Attack.Posture);
            if (Target->Posture > Previous)
                Target->PostureDelay = FMath::Max(Target->PostureDelay, 3.2f);
            Arena->SpellContact(TargetAt + FVector(0, 0, 24), ActionDirection, Tint, false);
        }
        return;
    }
    case EDuelArt::PrismSeal:
    case EDuelArt::EmberRecoil:
    {
        const bool bPrism = MoveArt == EDuelArt::PrismSeal;
        if (bPrism) Arena->Detonate(CastOrigin, Attack.Reach, Tint, .58f, EDuelMark::PrismSeal, CastAxis);
        else Arena->EmberRecoil(CastOrigin, CastAxis, Attack.Reach, Tint, .49f, true);
        if (bCombatOver || !IsValid(Target)) return;
        const FVector Offset = Target->GetActorLocation() - CastOrigin;
        const double Height = Offset.Z - Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 8.;
        if (Height < -30. || Height > (bPrism ? 105. : 85.)) return;

        bool bWithin = true;
        if (bPrism)
        {
            const double Apothem = Attack.Reach * DuelCourt::PrismSealApothem;
            for (int32 Edge = 0; Edge < 6; ++Edge)
            {
                const FVector Normal = CastAxis.RotateAngleAxis(30.f + 60.f * Edge, FVector::UpVector);
                if (FVector::DotProduct(Offset, Normal) > Apothem + .05)
                {
                    bWithin = false;
                    break;
                }
            }
        }
        else
        {
            const double Range = Offset.Size2D();
            const double Cosine = FVector::DotProduct(CastAxis, Offset.GetSafeNormal2D());
            bWithin = Range >= Attack.Reach * DuelCourt::EmberRecoilInner && Range <= Attack.Reach &&
                Cosine >= FMath::Cos(FMath::DegreesToRadians(DuelCourt::EmberRecoilHalfArc));
        }
        const float Before = Target->Health;
        if (bWithin && Target->ReceiveDuelDamage(Attack.Damage, CastOrigin))
        {
            Target->DamagePosture(Attack.Posture);
            if (Target->Health < Before)
                Arena->SpellContact(Target->GetActorLocation() + FVector(0,0,24), CastAxis, Tint, true);
        }
        return;
    }
    case EDuelArt::GildedCross:
    case EDuelArt::MoonCrescent:
    {
        const bool bCross = MoveArt == EDuelArt::GildedCross;
        Arena->Detonate(CastOrigin, Attack.Reach, Tint, .54f,
            bCross ? EDuelMark::GildedCross : EDuelMark::MoonCrescent, CastAxis);
        if (bCombatOver || !IsValid(Target)) return;
        const FVector Offset = Target->GetActorLocation() - CastOrigin;
        const double Height = Offset.Z - Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 8.;
        if (Height < -30. || Height > 90.) return;
        const double Along = FMath::Abs(FVector::DotProduct(Offset, CastAxis));
        const double Across = FMath::Abs(FVector::DotProduct(Offset, FVector::CrossProduct(FVector::UpVector, CastAxis)));
        const FVector LunarCut = Offset + CastAxis * (Attack.Reach * DuelSpellMarks::MoonCutOffset);
        const bool bWithin = bCross ?
            (FMath::Max(Along, Across) <= Attack.Reach && FMath::Min(Along, Across) <= DuelSpellMarks::CrossHalfWidth) :
            (Offset.SizeSquared2D() <= FMath::Square(Attack.Reach) &&
                LunarCut.SizeSquared2D() >= FMath::Square(Attack.Reach * DuelSpellMarks::MoonCutRadius));
        const float Before = Target->Health;
        const float Damage = Attack.Damage * (bChargedArt ? 1.30f : 1.f);
        if (bWithin && Target->ReceiveDuelDamage(Damage, CastOrigin))
        {
            const float Dealt = FMath::Max(0.f, Before - Target->Health);
            Target->DamagePosture(Attack.Posture * (bChargedArt && bCross ? 1.80f : 1.f));
            if (Dealt > 0.f)
            {
                if (bChargedArt && !bCross)
                    Health = FMath::Min(DuelCourt::PlayerMaxHealth, Health + FMath::Min(62.f, Dealt * .30f));
                Arena->SpellContact(Target->GetActorLocation() + FVector(0,0,24),CastAxis,Tint,true);
            }
        }
        return;
    }
    case EDuelArt::WitnessStep:
    {
        const FVector Side = FVector::CrossProduct(FVector::UpVector, CastAxis);
        const FVector Input = MoveInput();
        const float Direction = FVector::DotProduct(Input, Side) < -.14f ? -1.f : 1.f;
        AddActorWorldOffset(Side * Direction * Attack.Timing.Distance, true);
        Arena->LaunchThread(Target, CastOrigin, CastAxis, Attack.Damage, Attack.Posture, Tint, 1720.f, 86.f, 1.1f,
            false, 0.f, true, 0.f, EDuelMark::Witness);
        Arena->Burst(GetActorLocation(), Tint, 32, 315.f, true);
        return;
    }
    case EDuelArt::SableBlade:
    {
        const FVector At = ArtGround(GetActorLocation());
        constexpr float HalfArc = 64.f;
        Arena->SableSweep(At, ActionDirection, Attack.Reach, Tint, HalfArc);
        if (!bCombatOver)
        {
            TryMelee(Attack.Reach, FMath::Cos(FMath::DegreesToRadians(HalfArc)), Attack.Damage, Attack.Posture);
        }
        return;
    }
    case EDuelArt::Orb:
        Arena->LaunchCrimson(this, Target, ActionDirection, Attack.Damage, Attack.Posture);
        return;
    case EDuelArt::HiroBind:
        BindStrike(Attack, false);
        return;
    case EDuelArt::HiroStep:
        Arena->Burst(GetActorLocation() + FVector(0, 0, 26), Tint, 27, 370.f, true);
        break;
    case EDuelArt::HiroThreads:
    {
        const FVector At = GetActorLocation() + FVector(0, 0, 39.f);
        const FVector Across = FVector::CrossProduct(FVector::UpVector, ActionDirection);
        for (int32 I = -1; I <= 1; ++I)
        {
            const FVector Axis = ActionDirection.RotateAngleAxis(11.f * I, FVector::UpVector);
            Arena->LaunchThread(Target, At + ActionDirection * 79.f + Across * I * 32.f, Axis,
                Attack.Damage, Attack.Posture, Tint, 1610.f - 110.f * FMath::Abs(I), 79.f, .93f,
                false, 58.f * I, true, 0.f, EDuelMark::Thread);
        }
        Arena->Sound(EDuelSound::Cut, At);
        return;
    }
    case EDuelArt::EmaVotive:
        StrikeVotive(Attack, 0);
        return;
    case EDuelArt::HiroReprise:
    {
        const FVector At = CastOrigin + CastAxis * 63.f;
        Arena->LaunchThread(Target, At, CastAxis, Attack.Damage, Attack.Posture, Tint, 1430.f, 91.f, .88f,
            false, 0.f, true, 0.f, EDuelMark::Reprise);
        Arena->Slash(CastOrigin, CastAxis.Rotation().Yaw, 231.f, false, Tint);
        Arena->Burst(At, Tint, 31, 498.f, true);
        Arena->Sound(EDuelSound::Cut, At);
        return;
    }
    case EDuelArt::EmaAbsolution:
        StrikeAbsolution(Attack);
        return;
    case EDuelArt::HiroCrossThread:
    {
        constexpr float Speed = 1950.f;
        const FVector At = CastOrigin + FVector(0, 0, 41);
        Arena->LaunchThread(Target, At, CastAxis, Attack.Damage, Attack.Posture, Tint, Speed, 71.f, Attack.Reach / Speed,
            false, 0.f, true, 0.f, EDuelMark::CrossThread);
        Arena->Thrust(At, CastAxis, 240.f, Tint, true, EDuelMark::CrossThread);
        Arena->Sound(EDuelSound::Cut, At);
        return;
    }
    case EDuelArt::EmaAegis:
        AegisUntil = GetWorld()->GetTimeSeconds() + 4.0;
        bMantleCounter = false;
        Arena->Sigil(ArtGround(GetActorLocation()), 152.f, Tint, .42f, EDuelMark::Aegis, ActionDirection);
        Arena->Burst(GetActorLocation(), Tint, 29, 180.f);
        Arena->Flash(GetActorLocation() + FVector(0, 0, 44), Tint, 6100.f, .46f, 420.f);
        Arena->Sound(EDuelSound::Block, GetActorLocation());
        return;
    case EDuelArt::RitualOffering:
    {
        Health *= 1.f - DuelCourt::RitualOfferingHealthCost;
        RitualOfferingUntil = GetWorld()->GetTimeSeconds() + DuelCourt::RitualOfferingDuration;
        const FVector At = ArtGround(GetActorLocation());
        Arena->Halo(At, 62.f, 182.f, Tint, .75f, true, EDuelMark::Offering);
        Arena->Burst(GetActorLocation() + FVector(0, 0, 55), Tint, 24, 200.f, true);
        Arena->Sound(EDuelSound::Pulse, At);
        return;
    }
    case EDuelArt::EmaWard:
        return;
    case EDuelArt::WitchRain:
    {
        const float Radius = Attack.Reach * .68f;
        Arena->Rainfall({SealCenters[0], SealCenters[1], SealCenters[2]}, Radius, Tint * .63f, true,
            EDuelMark::WitchRain, ActionDirection);
        for (const FVector& Mark : SealCenters)
        {
            if (!bCombatOver && FVector::DistSquared2D(Target->GetActorLocation(), Mark) <= FMath::Square(Radius) &&
                Target->ReceiveDuelDamage(Attack.Damage, Mark))
            {
                Target->DamagePosture(Attack.Posture);
                break;
            }
        }
        return;
    }
    default:
        break;
    }
    if (MoveArt == EDuelArt::Burst || MoveArt == EDuelArt::EmaLight)
    {
        const bool bLight = MoveArt != EDuelArt::Burst;
        const bool bHammer = !bLight && MoveWeapon == EDuelWeapon::Hammer;
        const float Radius = Attack.Reach * (bHammer ? .83f : 1.f);
        const FVector At = bLight ? CastOrigin : ArtGround(GetActorLocation());
        if (bLight)
        {
            Arena->Rainfall({At}, Attack.Reach, Tint * .57f, false, EDuelMark::LightRosette, ActionDirection);
        }
        else
        {
            Arena->Detonate(At, Radius, Tint, .76f, EDuelMark::EmberCrater, ActionDirection);
        }
        if (!bCombatOver && FVector::DistSquared2D(Target->GetActorLocation(), At) <= FMath::Square(Radius) &&
            Target->ReceiveDuelDamage(Attack.Damage, At))
        {
            CameraImpulse = FMath::Max(CameraImpulse, .84f);
            Target->DamagePosture(Attack.Posture * (bHammer ? 1.36f : 1.f));
        }
    }
    else
    {
        Arena->Sound(EDuelSound::Cut, GetActorLocation());
    }
}

void ADuelFighter::StrikeAbsolution(const DuelCourt::FStrike& Attack)
{
    constexpr float Inner = 242.f;
    const FLinearColor& Tint = DuelCourt::WeaponArt(MoveArt).Glow;
    Arena->Halo(CastOrigin, Inner, Attack.Reach, Tint * .57f, .61f, true, EDuelMark::Absolution);
    const double Distance = FVector::Dist2D(Target->GetActorLocation(), CastOrigin);
    if (Distance >= Inner && Distance <= Attack.Reach && Target->ReceiveDuelDamage(Attack.Damage, CastOrigin))
    {
        Target->DamagePosture(Attack.Posture);
    }
}

void ADuelFighter::StrikeVotive(const DuelCourt::FStrike& Attack, int32 Index)
{
    const FVector At = SealCenters[Index];
    const FLinearColor& Tint = DuelCourt::WeaponArt(MoveArt).Glow;
    const TArray<FVector> Mark{At};
    Arena->Rainfall(Mark, Attack.Reach, Tint * .56f, true, EDuelMark::Votive, ActionDirection);
    if (FVector::DistSquared2D(At, Target->GetActorLocation()) <= FMath::Square(Attack.Reach) &&
        Target->ReceiveDuelDamage(Attack.Damage, At))
    {
        Target->DamagePosture(Attack.Posture);
    }
}

bool ADuelFighter::TryWardCounter(const FVector& Source)
{
    if (Move != EDuelMove::Crimson || MoveArt != EDuelArt::EmaWard || bHitDelivered || !IsValid(Target))
    {
        return false;
    }
    const DuelCourt::FWeaponArt& Art = DuelCourt::WeaponArt(MoveArt);
    const FVector At = GetActorLocation();
    const FVector Toward = (Source - At).GetSafeNormal2D();
    const DuelCourt::FMoveTiming Window = DuelCourt::Timing(Move, IsAwakened(), MoveWeapon, MoveArt);
    if (MoveAge < Window.Windup || MoveAge >= Window.Windup + Window.Active ||
        FVector::DotProduct(ActionDirection, Toward) < .32f)
    {
        return false;
    }
    bHitDelivered = true;
    Speak(EDuelVoice::Art);
    Arena->Slash(At + FVector(0, 0, 25), ActionDirection.Rotation().Yaw, Art.Strike.Reach, false, Art.Glow);
    Arena->Burst(At + Toward * 90.f, Art.Glow, 45, 465.f, true);
    Arena->Sound(EDuelSound::Block, At);
    CameraImpulse = FMath::Max(CameraImpulse, .7f);
    if (FVector::DistSquared2D(At, Target->GetActorLocation()) <= FMath::Square(Art.Strike.Reach) &&
        Target->ReceiveDuelDamage(Art.Strike.Damage, At))
    {
        Target->DamagePosture(Art.Strike.Posture);
    }
    return true;
}
