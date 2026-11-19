#include "DuelBurstFx.h"

#include "DuelArena.h"
#include "DuelGame.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace DuelBurstFx
{
namespace
{
    struct FPalette { FLinearColor Edge, Accent, Smoke; };
    FPalette Palette(EStyle Style)
    {
        if (Style == EStyle::Carmine) return {{1.f,.018f,.13f,.89f},{.69f,.035f,.72f,.66f},{.085f,.012f,.031f,.48f}};
        if (Style == EStyle::Frost) return {{.055f,.66f,1.f,.90f},{.39f,.12f,1.f,.69f},{.035f,.09f,.22f,.43f}};
        return {{1.f,.39f,.025f,.91f},{1.f,.07f,.025f,.68f},{.11f,.034f,.025f,.49f}};
    }
    void Add(FPlan& Plan, EDuelLight Shape, const FVector& At, const FVector& Velocity,
        const FVector2D& Size, FLinearColor Tint, float Life, float Gravity = 0,
        const FVector& Axis = FVector::ZeroVector, float Spin = 0)
    {
        if (Plan.Emissions.Num() >= MaxEmissions || !FMath::IsFinite(Life) || Life <= 0) return;
        if (Shape == EDuelLight::Mist && Plan.Clouds >= MaxClouds) return;
        if (Shape == EDuelLight::Mist) ++Plan.Clouds;
        Plan.Emissions.Add({Shape,At,Velocity,Axis,Size,Tint,FMath::Min(Life,1.25f),Gravity,Spin});
    }
}

EStyle CharacterStyle(EDuelCharacter Character)
{
    return Character == EDuelCharacter::Hiro ? EStyle::Carmine :
        Character == EDuelCharacter::Yuki ? EStyle::Frost : EStyle::Ember;
}
EStyle TintStyle(const FLinearColor& Tint)
{
    if (Tint.B > Tint.R * 1.10f || (Tint.B > .6f && Tint.G > Tint.R * .75f)) return EStyle::Frost;
    return Tint.B > Tint.G * 1.25f && Tint.R > Tint.G * 1.8f ? EStyle::Carmine : EStyle::Ember;
}

FPlan Make(EStyle Style, EKind Kind, const FVector& At, float Radius,
    float Life, const FVector& TowardCamera, uint32 Seed)
{
    FPlan Plan;
    const FPalette C = Palette(Style);
    Plan.Light = C.Edge;
    if (!FMath::IsFinite(Radius) || Radius <= 0 || At.ContainsNaN()) return Plan;
    // Space follows the supplied attack radius. Detail count does not grow with it.
    const float R = FMath::Max(16.f, Radius);
    const bool bSmall = Kind == EKind::Impact;
    const bool bBoss = Kind == EKind::BossRelease;
    const bool bFront = Kind == EKind::Front;
    const float Seconds = FMath::Clamp(Life, .22f, .92f);
    const float Height = FMath::Clamp(R * (Style == EStyle::Frost ? .42f : .34f), 58.f, 620.f);
    const int32 Branches = bSmall ? 2 : 3;
    const int32 Segments = bSmall ? 3 : (bFront ? 4 : 6);
    FVector Near = TowardCamera.GetSafeNormal2D();
    if (Near.IsNearlyZero()) Near = FVector::ForwardVector;
    const FVector Side = FVector::CrossProduct(FVector::UpVector, Near);
    FRandomStream Noise(Seed ^ (0x57495443u + static_cast<uint32>(Style) * 173));

    // Three unequal bending fronts, not concentric rings. The front-facing 96 deg
    // remain open so the camera can still see the boss silhouette and floor.
    const auto Point = [&](int32 Branch, float T)
    {
        const float Wobble = FMath::Sin(T * PI * 1.7f + Branch * 1.1f);
        const float Angle = FMath::DegreesToRadians(57.f + Branch * 89.f + T * 20.f + 19.f * Wobble);
        const FVector Out = Near * FMath::Cos(Angle) + Side * FMath::Sin(Angle);
        const float Reach = R * (.22f + .55f * T + .035f * FMath::Sin(T * 11.f + Branch));
        float Z = 36.f + Height * (.12f + .64f * FMath::Sin(T * PI));
        if (Style == EStyle::Carmine) Z *= .63f;
        if (Style == EStyle::Frost) Z += Height * .15f * Branch;
        return At + Out * Reach + FVector(0,0,Z);
    };
    for (int32 Branch = 0; Branch < Branches; ++Branch)
    {
        for (int32 I = 0; I < Segments; ++I)
        {
            const float T0 = I / static_cast<float>(Segments), T1 = (I + 1.f) / Segments;
            const FVector A = Point(Branch,T0), B = Point(Branch,T1), Center = (A+B)*.5f;
            const FVector Tangent = (B-A).GetSafeNormal();
            const FVector Out = (Center-At).GetSafeNormal2D();
            // A sharp leading seam lasts briefly; detached smoke has the longer tail.
            Add(Plan,EDuelLight::Filament,Center,
                Out*(R*(bFront?.42f:.22f)) + Tangent*(R*.10f) + FVector(0,0,38),
                FVector2D(FVector::Dist(A,B)*1.12f,FMath::Clamp(R*.025f,4.f,24.f)*(I%2?.60f:1.f)),
                I%3 ? C.Edge : C.Accent,FMath::Min(Seconds*.60f,.40f),0,Tangent);
        }
        const int32 Lobes = bSmall ? 1 : (bFront ? 2 : 3);
        for (int32 Lobe = 0; Lobe < Lobes; ++Lobe)
        {
            const float T = .22f + Lobe*.28f;
            const FVector P = Point(Branch,T);
            const FVector Out = (P-At).GetSafeNormal2D();
            const FVector Curl = FVector::CrossProduct(FVector::UpVector,Out);
            const float Width = FMath::Clamp(R*(.25f+.035f*(Lobe%2)),50.f,570.f);
            const FVector Flow = Out*(R*(bFront?.45f:.26f)) + Curl*(R*(Branch%2?-.27f:.27f)) +
                FVector(0,0,Style==EStyle::Frost?105.f:72.f);
            const bool bDark = (Lobe+Branch)%2==0;
            FLinearColor Smoke = bDark ? C.Smoke : C.Edge;
            Smoke.A = bDark ? .42f : (Style==EStyle::Frost?.31f:.27f);
            Add(Plan,EDuelLight::Mist,P,Flow,FVector2D(Width,Width*(Style==EStyle::Frost?.75f:.60f)),
                Smoke,Seconds*(bSmall?.95f:1.18f),Style==EStyle::Carmine?26.f:-14.f,Curl,
                (Branch%2?-1.f:1.f)*(Style==EStyle::Carmine?68.f:42.f));
            if (bBoss && Lobe==1)
            {
                // A second-depth, darker fold makes the illuminated plume read as
                // a body rather than one bright flat billboard.
                Add(Plan,EDuelLight::Mist,P-Out*(R*.08f)+FVector(0,0,Height*.24f),Flow*.65f,
                    FVector2D(Width*.76f,Width*.63f),C.Smoke,Seconds*1.30f,-18.f,Curl,-31.f);
            }
        }
    }

    const int32 Chips = bSmall ? 6 : (bFront ? 8 : 12);
    for (int32 I=0; I<Chips; ++I)
    {
        const float T = (I+.35f)/Chips;
        const float Angle = FMath::DegreesToRadians(52.f+T*255.f+Noise.FRandRange(-8.f,8.f));
        const FVector Out = Near*FMath::Cos(Angle)+Side*FMath::Sin(Angle);
        const FVector Curl = FVector::CrossProduct(FVector::UpVector,Out);
        const float Lift = Style==EStyle::Frost ? 240.f : Style==EStyle::Ember ? 350.f : 125.f;
        const FVector Velocity = Out*R*(.32f+.27f*T) + Curl*R*(Style==EStyle::Carmine?.31f:.12f) +
            FVector(0,0,Lift*(.55f+.45f*T));
        const float Length = FMath::Clamp(R*.13f,20.f,120.f)*(I%3==0?1.f:.58f);
        const EDuelLight Shape = Style==EStyle::Ember ? EDuelLight::Feather : EDuelLight::Spark;
        Add(Plan,Shape,At+Out*(R*(.30f+.40f*T))+FVector(0,0,42+Height*.25f*T),Velocity,
            FVector2D(Length,Length*(Style==EStyle::Ember?.20f:.085f)),I%3?C.Edge:C.Accent,
            FMath::Min(Seconds*.82f,.66f),Style==EStyle::Frost?180.f:350.f,Velocity,
            Style==EStyle::Ember?(I%2?-92.f:105.f):0);
    }
    // Small side glints establish the initial hit; no full-screen white core.
    for (float Sign : {-1.f,1.f})
        Add(Plan,EDuelLight::Bloom,At+Side*(Sign*R*.22f)-Near*(R*.13f)+FVector(0,0,Height*.28f+24),
            Side*(Sign*R*.12f)+FVector(0,0,32),FVector2D(FMath::Clamp(R*.23f,20.f,180.f)),
            C.Edge.CopyWithNewOpacity(.58f),bSmall?.10f:.14f);
    return Plan;
}
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelBurstBudgetTest,"ManosabaDuel.Effects.BurstPlans",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FDuelBurstBudgetTest::RunTest(const FString& Parameters)
{
    using namespace DuelBurstFx;
    for (EStyle Style : {EStyle::Ember,EStyle::Carmine,EStyle::Frost})
    for (EKind Kind : {EKind::Impact,EKind::Detonation,EKind::BossRelease,EKind::Front})
    for (float R : {75.f,350.f,900.f,1700.f})
    {
        const FVector At(1000,-1700,3400);
        const auto P=Make(Style,Kind,At,R,.75f,FVector::ForwardVector,243);
        TestTrue(TEXT("Each event has a bounded particle and cloud budget"),
            P.Emissions.Num()>0 && P.Emissions.Num()<=MaxEmissions && P.Clouds<=MaxClouds);
        float Extent=0;
        for (const auto& E:P.Emissions)
        {
            TestTrue(TEXT("Finite cosmetic emission data"),!E.At.ContainsNaN()&&!E.Velocity.ContainsNaN()&&
                !E.Axis.ContainsNaN()&&E.Life>0&&E.Life<=1.25f&&E.Size.X>0&&E.Size.Y>0);
            Extent=FMath::Max(Extent,static_cast<float>(FVector::Dist2D(E.At,At)));
            if(E.Shape==EDuelLight::Mist)
                TestTrue(TEXT("Dense smoke leaves the near camera sector open"),
                    FVector::DotProduct((E.At-At).GetSafeNormal2D(),FVector::ForwardVector)<.82f);
        }
        TestTrue(TEXT("Visible fronts occupy the supplied radius without upscaling small attacks"),Extent>.58f*R && Extent<.95f*R);
    }
    const auto Ember=Make(EStyle::Ember,EKind::BossRelease,FVector::ZeroVector,900,.75,FVector::ForwardVector);
    const auto Frost=Make(EStyle::Frost,EKind::BossRelease,FVector::ZeroVector,900,.75,FVector::ForwardVector);
    TestTrue(TEXT("Boss palette and vertical flow differ"),Ember.Light!=Frost.Light &&
        Ember.Emissions[0].At.Z!=Frost.Emissions[0].At.Z);
    return !HasAnyErrors();
}
#endif
