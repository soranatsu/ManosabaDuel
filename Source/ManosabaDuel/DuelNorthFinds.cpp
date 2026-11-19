#include "DuelNorthFinds.h"
#include "DuelGame.h"

namespace DuelNorthFinds
{
using F = EDuelNorthFloor;
using W = EDuelWeapon;
using A = EDuelArt;
using P = EFixture;
// A cache's position in this list is part of the checkpoint format.
static const FFind Finds[] = {
    {TEXT("FindRations"), TEXT("配膳台上的三只碗"), TEXT("翻查"), {1180,-1115,0}, 0, {176,74,80}, {20,20,83},
        P::RationTable, F::Prison, W::Spear, A::LanternMote,
        TEXT("缺口碗底的 03 号房牌对上了配给底账。多送的那份饭进了地下禁闭室。"), true},
    {TEXT("FindCounterseal"), TEXT("校对台上的信件"), TEXT("核对"), {-810,255,700}, 180, {180,84,83}, {15,18,96},
        P::CollationDesk, F::Chapel, W::Greatshield, A::PrismSeal,
        TEXT("两封亲笔信还封在退信袋里。送到两人手里的信压着同一枚带缺角的红蜡印。有人换过信。"), true},
    {TEXT("FindFirebox"), TEXT("值夜人的火箱"), TEXT("检查"), {3020,-305,0}, 0, {140,66,76}, {15,27,104},
        P::Firebox, F::Prison, W::Axe, A::EmberRecoil,
        TEXT("你从火箱里取出铁斧，斧尾还缠着旧布。值夜人怕斧头碰响栏杆，把刚睡下的人惊醒。")},
    {TEXT("FindSmith"), TEXT("修锁匠的工具袋"), TEXT("解开"), {-3615,-515,0}, -90, {126,66,76}, {19,16,83.3},
        P::ToolRoll, F::Prison, W::Hammer, A::SableBlade,
        TEXT("你解开修锁台上的工具袋，从袋底取出锤子。几把备用钥匙都锉掉了最后一齿。")},
    {TEXT("FindBrine"), TEXT("地下库房盐匣中的重刃"), TEXT("取出"), {-3615,-520,-1700}, -90, {132,64,59}, {-22,29,62},
        P::SaltCoffer, F::Dungeon, W::Greatblade, A::SaltArc,
        TEXT("你从地下库房的盐匣里抽出重刃。拂去剑脊上的盐，露出底下刻的术式。")},
    {TEXT("FindSluice"), TEXT("巡夜看守的旧刀鞘"), TEXT("拾起"), {2860,785,-1700}, 0, {146,66,77}, {-35,10,83.6},
        P::SluiceShelf, F::Dungeon, W::Houndfang, A::Blackwell,
        TEXT("你从旧鞘里抽出猎犬长牙。内侧刻了四次换岗时间，最后一道刻痕旁写着「就是今晚」。")},
    {TEXT("FindCoast"), TEXT("海廊信筒里的信"), TEXT("抽出"), {-580,-1974,700}, 180, {116,52,74}, {26,8,85},
        P::LetterTube, F::Chapel, W::Empty, A::ReturningFrost,
        TEXT("你从海廊信筒里抽出一封未寄出的信。术式夹在信纸里，折痕已经磨软。")},
    {TEXT("FindVeil"), TEXT("06号牢房的护身袖袋"), TEXT("翻读"), {-1040,2410,-1700}, 180, {116,55,81}, {15,17,89},
        P::SewingCabinet, F::Dungeon, W::Empty, A::WhiteMantle,
        TEXT("06号多人羁押室的旧袖袋里，银线绣着：『白幕覆肩，护住同行的人。』你沿针脚抄下术式。\n习得魔法：白幕护体。")},
    {TEXT("FindStar"), TEXT("观测者的镜盒"), TEXT("查看"), {-3615,1500,1700}, -90, {120,65,76}, {13,15,98},
        P::MirrorCase, F::Mirror, W::Empty, A::StarTrack,
        TEXT("你从镜盒里取出星位图。镜面边沿刻着一句话『下次我们到外面看』。")},
    {TEXT("FindWitness"), TEXT("证人席下的剑匣"), TEXT("拉开"), {2820,805,1700}, 0, {160,68,54}, {-27,35,42},
        P::WitnessBench, F::Mirror, W::RitualSword, A::WitnessStep,
        TEXT("你拉开证人席下的剑匣，取出仪式剑。锁眼里塞着一截蜡，锁舌始终扣不实。")},
    {TEXT("FindVotive"), TEXT("祭台上的灯盏"), TEXT("抬起"), {-1450,-2692,3400}, 0, {132,66,85}, {20,16,88},
        P::VotiveAltar, F::Final, W::Empty, A::GildedCross,
        TEXT("你从熄灭的灯盏底下抽出一片金箔。纸迎着光，浮出浅浅的十字。")},
    {TEXT("FindMoon"), TEXT("终审席边的月历"), TEXT("翻看"), {-3100,-395,3400}, 0, {165,52,49}, {23,10,57},
        P::WaitingBench, F::Final, W::Empty, A::MoonCrescent,
        TEXT("月历最后一页每天都有一道墨痕。终审那格旁边写着「把她们的名字都记住」。")}
};
static_assert(UE_ARRAY_COUNT(Finds) < 32);
TConstArrayView<FFind> All() { return MakeArrayView(Finds); }
int32 IndexOf(FName Id) { return All().IndexOfByPredicate([Id](const FFind& Find) { return Find.Id == Id; }); }
uint32 AllCacheBits() { return (1u << UE_ARRAY_COUNT(Finds)) - 1u; }
bool HasSpell(uint32 Bits, EDuelArt Spell)
{
    if (Spell == A::Default) return false;
    for (int32 I=0; I<All().Num(); ++I) if ((Bits & (1u<<I)) && Finds[I].Spell == Spell) return true;
    return false;
}
bool HasWeapon(uint32 Bits, EDuelWeapon Weapon)
{
    if (Weapon == W::Empty) return false;
    for (int32 I=0; I<All().Num(); ++I) if ((Bits & (1u<<I)) && Finds[I].Weapon == Weapon) return true;
    return false;
}
FString Rewards(const FFind& Find)
{
    const FString Magic = FString(TEXT("魔法　")) + DuelCourt::WeaponArt(Find.Spell).Name;
    if (Find.Weapon == W::Empty) return Magic;
    const TCHAR* Category = DuelCourt::IsShield(Find.Weapon) ? TEXT("盾牌　") : TEXT("武器　");
    return FString(Category) + DuelCourt::EquipmentName(Find.Weapon) + TEXT("\n") + Magic;
}
struct FRound { const TCHAR* Question; const TCHAR* Choices[3]; int32 Key; };
static constexpr FRound Rounds[3][2] = {
    {
        {TEXT("额外送出的那份饭装在哪只碗里？"), {TEXT("大号的整碗"), TEXT("碗沿缺一角的旧碗"), TEXT("小号的整碗")}, 1},
        {TEXT("03 号房牌指向哪里？"), {TEXT("01 厨房"), TEXT("02 医务室"), TEXT("03 地下禁闭室")}, 2},
    }, {
        {TEXT("她们自己写的两封信，最后放在哪儿？"), {TEXT("都留在退信袋里"), TEXT("已经送到对方手里"), TEXT("被看守烧掉了")}, 0},
        {TEXT("两封假信的哪一处是一样的？"),
            {TEXT("信封折起来的纸角"), TEXT("缺了一角的红蜡印"), TEXT("收信人的名字")}, 1},
    }, {
        {TEXT("小雪把扣下的求救信封在哪只袋里？"),
            {TEXT("麻绳扎口袋"), TEXT("红蜡封袋"), TEXT("黑漆公文袋")}, 1},
        {TEXT("用哪枚戒指，能压出假信上的花印？"),
            {TEXT("右边花瓣缺了一角的戒指"), TEXT("花瓣都完整的戒指"), nullptr}, 0},
    },
};
const TCHAR* Question(int32 Puzzle, int32 Round) { return Rounds[Puzzle][Round].Question; }
const TCHAR* Answer(int32 Puzzle, int32 Round, int32 Choice) { return Rounds[Puzzle][Round].Choices[Choice]; }
bool Correct(int32 Puzzle, int32 Round, int32 Choice) { return Rounds[Puzzle][Round].Key == Choice; }
}
