#include "DuelSpellMarks.h"

#include <initializer_list>

namespace DuelSpellMarks
{
namespace
{
    FVector2D Polar(double Radius, double Degrees)
    {
        const double Angle = FMath::DegreesToRadians(Degrees);
        return FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
    }

    struct FStrokes
    {
        FVector Forward;
        FVector Side;
        FVector2D Extent;
        bool bLane;
        double Weight;
        TArray<FVector>& Vertices;
        TArray<int32>& Triangles;
        double HalfArc = 180.;

        FVector World(const FVector2D& P) const
        {
            return Forward * (P.X * Extent.X) + Side * (P.Y * Extent.Y);
        }

        FVector Clip(const FVector& P) const
        {
            if (!bLane)
            {
                if (HalfArc < 180.)
                {
                    const double X = FVector::DotProduct(P, Forward);
                    const double Y = FVector::DotProduct(P, Side);
                    const double Angle = FMath::RadiansToDegrees(FMath::Atan2(Y, X));
                    if (FMath::Abs(Angle) > HalfArc)
                    {
                        return World(Polar(FMath::Min(1., P.Size2D() / Extent.X), FMath::Clamp(Angle, -HalfArc, HalfArc)));
                    }
                }
                return P.GetClampedToMaxSize2D(Extent.X);
            }
            const double X = FMath::Clamp(FVector::DotProduct(P, Forward), -Extent.X, Extent.X);
            const double Y = FMath::Clamp(FVector::DotProduct(P, Side), -Extent.Y, Extent.Y);
            return Forward * X + Side * Y;
        }

        void Line(const FVector2D& A, const FVector2D& B, double From = 1., double To = 1.)
        {
            const FVector Start = World(A);
            const FVector End = World(B);
            const FVector Perp = FVector::CrossProduct(FVector::UpVector, (End - Start).GetSafeNormal2D()) * Weight;
            const int32 V = Vertices.Num();
            Vertices.Append({Clip(Start - Perp * From), Clip(Start + Perp * From),
                Clip(End + Perp * To), Clip(End - Perp * To)});
            Triangles.Append({V, V + 1, V + 2, V, V + 2, V + 3});
        }

        void Path(std::initializer_list<FVector2D> Points, double Width = 1.)
        {
            auto Prev = Points.begin();
            for (auto It = Prev + 1; It != Points.end(); ++It, ++Prev)
            {
                Line(*Prev, *It, Width, Width);
            }
        }

        void Arc(const FVector2D& Center, const FVector2D& Scale, double Start, double End,
            int32 Segments, double From = 1., double To = 1., double Tilt = 0.)
        {
            FVector2D Last = Center + (Polar(1., Start) * Scale).GetRotated(Tilt);
            for (int32 I = 1; I <= Segments; ++I)
            {
                const double T = static_cast<double>(I) / Segments;
                const FVector2D Next = Center + (Polar(1., FMath::Lerp(Start, End, T)) * Scale).GetRotated(Tilt);
                Line(Last, Next, FMath::Lerp(From, To, T - 1. / Segments), FMath::Lerp(From, To, T));
                Last = Next;
            }
        }

        void Polygon(int32 Sides, double Radius, double Tilt, double Width = 1.)
        {
            for (int32 I = 0; I < Sides; ++I)
            {
                Line(Polar(Radius, Tilt + I * 360. / Sides), Polar(Radius, Tilt + (I + 1) * 360. / Sides), Width, Width);
            }
        }

        void Leaf(const FVector2D& Root, const FVector2D& Tip, double Bow, double Width = 1.)
        {
            const FVector2D Spine = Tip - Root;
            const FVector2D Across(-Spine.Y, Spine.X);
            for (double Sign : {-1., 1.})
            {
                FVector2D Last = Root;
                for (int32 I = 1; I <= 5; ++I)
                {
                    const double T = I / 5.;
                    const FVector2D Next = Root + Spine * T + Across * (Sign * Bow * FMath::Sin(PI * T));
                    Line(Last, Next, Width * (1.05 - .65 * T), Width * (1. - .8 * T));
                    Last = Next;
                }
            }
        }

        void Rim(double Radius, double Width)
        {
            for (int32 I = 0; I < 4; ++I)
            {
                Arc(FVector2D::ZeroVector, FVector2D(Radius, Radius), I * 90. + 5., I * 90. + 85.,
                    7, Width, Width);
            }
        }

        void Banks(double Width = .65)
        {
            for (double Sign : {-1., 1.})
            {
                for (int32 I = 0; I < 4; ++I)
                {
                    const double X = -.992 + I * .50;
                    Line({X, Sign * .96}, {X + .47, Sign * .96}, Width, Width);
                }
            }
            for (double X : {-.992, .992})
            {
                Line({X, -.96}, {X, -.69}, Width, Width * .55);
                Line({X, .69}, {X, .96}, Width * .55, Width);
            }
        }
    };

    enum class EMotif { Feather, Claw, Crystal, Rune, Chain, Flame, Eye };
    struct FInsignia { EMotif Motif; int32 Stamps; double Tilt; double Length; };

    FInsignia Insignia(EDuelMark Mark)
    {
        switch (Mark)
        {
        case EDuelMark::Bind: return {EMotif::Chain, 3, 0., 0.110};
        case EDuelMark::BindEcho: return {EMotif::Chain, 4, 7., 0.137};
        case EDuelMark::LightRosette: return {EMotif::Feather, 3, 0., 0.110};
        case EDuelMark::WitchRain: return {EMotif::Eye, 3, 0., 0.110};
        case EDuelMark::InkWell: return {EMotif::Flame, 3, 0., 0.110};
        case EDuelMark::InkRupture: return {EMotif::Flame, 4, 7., 0.137};
        case EDuelMark::Mantle: return {EMotif::Eye, 4, 7., 0.137};
        case EDuelMark::Aegis: return {EMotif::Rune, 4, 7., 0.137};
        case EDuelMark::Ward: return {EMotif::Rune, 3, 0., 0.110};
        case EDuelMark::Votive: return {EMotif::Feather, 4, 7., 0.137};
        case EDuelMark::Absolution: return {EMotif::Rune, 5, 14., 0.164};
        case EDuelMark::PenNib: return {EMotif::Eye, 4, 66., 0.137};
        case EDuelMark::CrossThread: return {EMotif::Chain, 5, 14., 0.164};
        case EDuelMark::Offering: return {EMotif::Flame, 5, 14., 0.164};
        case EDuelMark::EmberCrater: return {EMotif::Flame, 6, 21., 0.191};
        case EDuelMark::SaltArc: return {EMotif::Flame, 7, 28., 0.218};
        case EDuelMark::Sable: return {EMotif::Claw, 4, 7., 0.137};
        case EDuelMark::StarOrb: return {EMotif::Eye, 5, 14., 0.164};
        case EDuelMark::Step: return {EMotif::Claw, 3, 0., 0.110};
        case EDuelMark::IceLance: return {EMotif::Crystal, 3, 0., 0.110};
        case EDuelMark::Lantern: return {EMotif::Flame, 8, 35., 0.245};
        case EDuelMark::Witness: return {EMotif::Eye, 6, 21., 0.191};
        case EDuelMark::Thread: return {EMotif::Chain, 6, 21., 0.191};
        case EDuelMark::Reprise: return {EMotif::Eye, 7, 28., 0.218};
        case EDuelMark::Leap: return {EMotif::Feather, 5, 14., 0.164};
        case EDuelMark::Flank: return {EMotif::Feather, 6, 21., 0.191};
        case EDuelMark::Wingfall: return {EMotif::Feather, 7, 28., 0.218};
        case EDuelMark::Takeoff: return {EMotif::Feather, 8, 35., 0.245};
        case EDuelMark::Pillar: return {EMotif::Flame, 3, 59., 0.110};
        case EDuelMark::Storm: return {EMotif::Flame, 4, 66., 0.137};
        case EDuelMark::Scroll: return {EMotif::Rune, 6, 21., 0.191};
        case EDuelMark::CrossVerdict: return {EMotif::Rune, 7, 28., 0.218};
        case EDuelMark::Atlas: return {EMotif::Rune, 8, 35., 0.245};
        case EDuelMark::Pursuit: return {EMotif::Eye, 8, 35., 0.245};
        case EDuelMark::WingLane: return {EMotif::Feather, 3, 59., 0.110};
        case EDuelMark::Tribunal: return {EMotif::Rune, 3, 59., 0.110};
        case EDuelMark::HaloScript: return {EMotif::Rune, 4, 66., 0.137};
        case EDuelMark::HaloCore: return {EMotif::Rune, 5, 73., 0.164};
        case EDuelMark::Procession: return {EMotif::Feather, 4, 66., 0.137};
        case EDuelMark::WrittenVerdict: return {EMotif::Rune, 6, 80., 0.191};
        case EDuelMark::TalonRush: return {EMotif::Claw, 5, 14., 0.164};
        case EDuelMark::Reversal: return {EMotif::Claw, 6, 21., 0.191};
        case EDuelMark::NightLane: return {EMotif::Claw, 7, 28., 0.218};
        case EDuelMark::ShadowVolley: return {EMotif::Claw, 8, 35., 0.245};
        case EDuelMark::CrossAmbush: return {EMotif::Chain, 5, 73., 0.164};
        case EDuelMark::Snare: return {EMotif::Chain, 7, 28., 0.218};
        case EDuelMark::SnareTrap: return {EMotif::Chain, 8, 35., 0.245};
        case EDuelMark::Reap: return {EMotif::Claw, 3, 59., 0.110};
        case EDuelMark::TailSweep: return {EMotif::Claw, 4, 66., 0.137};
        case EDuelMark::BrokenRecord: return {EMotif::Chain, 4, 66., 0.137};
        case EDuelMark::SilentBell: return {EMotif::Rune, 7, 87., 0.218};
        case EDuelMark::BellCenter: return {EMotif::Rune, 8, 94., 0.245};
        case EDuelMark::Tether: return {EMotif::Chain, 3, 59., 0.110};
        case EDuelMark::MirrorSeam: return {EMotif::Crystal, 6, 21., 0.191};
        case EDuelMark::MirrorPanel: return {EMotif::Crystal, 7, 28., 0.218};
        case EDuelMark::Prism: return {EMotif::Crystal, 4, 7., 0.137};
        case EDuelMark::IcicleAim: return {EMotif::Crystal, 5, 14., 0.164};
        case EDuelMark::Archive: return {EMotif::Crystal, 8, 35., 0.245};
        case EDuelMark::GlassShelter: return {EMotif::Crystal, 3, 59., 0.110};
        case EDuelMark::BoneRake: return {EMotif::Feather, 5, 73., 0.164};
        case EDuelMark::BoneSlam: return {EMotif::Flame, 5, 73., 0.164};
        case EDuelMark::HoundRake: return {EMotif::Claw, 5, 73., 0.164};
        case EDuelMark::HoundTurn: return {EMotif::Claw, 6, 80., 0.191};
        case EDuelMark::FrostHook: return {EMotif::Crystal, 4, 66., 0.137};
        case EDuelMark::FrostReap: return {EMotif::Crystal, 5, 73., 0.164};
        case EDuelMark::StarTrack: return {EMotif::Eye, 3, 59., 0.110};
        case EDuelMark::ReturningFrost: return {EMotif::Crystal, 7, 87., 0.218};
        case EDuelMark::PrismSeal: return {EMotif::Crystal, 8, 94., 0.245};
        case EDuelMark::EmberRecoil: return {EMotif::Flame, 6, 80., 0.191};
        case EDuelMark::FrostThrust: return {EMotif::Crystal, 6, 80., 0.191};
        case EDuelMark::HoundPounce: return {EMotif::Claw, 7, 87., 0.218};
        default: return {EMotif::Rune, 0, 0., 0.};
        }
    }

    void Engrave(FStrokes& S, EMotif Motif, const FVector2D& Centre, double Angle, const FVector2D& Scale)
    {
        const auto P = [&](double X, double Y) { return Centre + (FVector2D(X, Y) * Scale).GetRotated(Angle); };
        switch (Motif)
        {
        case EMotif::Feather:
            S.Leaf(P(-1., -.35), P(1., .37), .40, 1.3);
            S.Line(P(-.73, -.18), P(.66, .28), .85, .12);
            for (int32 I = 0; I < 4; ++I)
            {
                const double X = -.49 + .32 * I;
                S.Line(P(X, -.05), P(X + .25, -.76 + .17 * I), .82, .08);
                S.Line(P(X, -.05), P(X + .37, .81 - .12 * I), .82, .08);
            }
            break;
        case EMotif::Claw:
            for (double Y : {-.64, 0., .64})
                S.Path({P(-.95, Y - .24), P(-.34, Y + .15), P(.42, Y + .08), P(.98, Y - .49)}, 1.10);
            break;
        case EMotif::Crystal:
            S.Path({P(-.97, 0), P(-.38, -.79), P(.84, -.49), P(.99, 0),
                P(.84, .49), P(-.38, .79), P(-.97, 0)}, 1.16);
            S.Path({P(-.38, -.79), P(-.02, 0), P(.84, .49)}, .72);
            S.Path({P(-.38, .79), P(-.02, 0), P(.84, -.49)}, .72);
            break;
        case EMotif::Rune:
            S.Path({P(-.9, -.7), P(-.9, .7), P(.64, .7), P(.89, .27), P(.48, 0),
                P(.89, -.27), P(.64, -.7), P(-.9, -.7)}, 1.02);
            S.Path({P(-.61, -.25), P(-.26, -.25), P(.24, .25), P(.53, .25)}, .76);
            S.Line(P(-.28, .46), P(-.28, -.46), .82, .82);
            break;
        case EMotif::Chain:
            for (double X : {-.49, .49})
                S.Path({P(X - .51, 0), P(X - .18, -.56), P(X + .37, -.56), P(X + .51, 0),
                    P(X + .18, .56), P(X - .37, .56), P(X - .51, 0)}, .98);
            S.Line(P(-.31, 0), P(.31, 0), 1.42, 1.42);
            break;
        case EMotif::Flame:
            S.Path({P(-.94, -.52), P(-.74, .22), P(-.41, .46), P(-.23, -.19),
                P(.40, .12), P(.98, .64), P(.68, -.34), P(.12, -.68), P(-.94, -.52)}, 1.28);
            S.Leaf(P(-.51, -.24), P(.55, .08), .28, .80);
            break;
        case EMotif::Eye:
            S.Leaf(P(-1., 0.), P(1., 0.), .46, 1.10);
            S.Path({P(-.35, 0), P(0, -.44), P(.36, 0), P(0, .44), P(-.35, 0)}, .92);
            break;
        }
    }

    void Inlay(FStrokes& S, EDuelMark Mark, double Inner)
    {
        const FInsignia Style = Insignia(Mark);
        for (int32 I = 0; I < Style.Stamps; ++I)
        {
            if (S.bLane)
            {
                const double X = -.76 + 1.52 * (I + .5) / Style.Stamps;
                const double Direction = I % 2 ? -Style.Tilt : Style.Tilt;
                Engrave(S, Style.Motif, {X, I % 2 ? -.24 : .24}, Direction,
                    {FMath::Min(Style.Length, .75 / Style.Stamps), .35});
                continue;
            }
            const double Angle = S.HalfArc < 180. ? -S.HalfArc + (I + .5) * (2. * S.HalfArc / Style.Stamps) :
                Style.Tilt + I * 360. / Style.Stamps;
            const double Radius = FMath::Lerp(Inner, .95, .72);
            Engrave(S, Style.Motif, Polar(Radius, Angle), Angle + 90.,
                {Style.Length, FMath::Min(.10, (1. - Inner) * .14)});
        }
    }

    void Cross(FStrokes& S)
    {
        S.bLane = true; // The union of two rectangles has square ends.
        const double W = CrossHalfWidth / S.Extent.X;
        constexpr double E = .986;
        S.Path({{E,W},{W,W},{W,E},{-W,E},{-W,W},{-E,W},{-E,-W},{-W,-W},
            {-W,-E},{W,-E},{W,-W},{E,-W},{E,W}}, 1.50);
        for (double Angle : {0., 90., 180., 270.})
            for (double R : {.28, .53, .78})
                Engrave(S, EMotif::Rune, Polar(R, Angle), Angle, {.093, W * .76});
        S.Polygon(4, W * 1.20, 45., 1.20);
    }

    double CutRadius(double Angle)
    {
        const double A = FMath::DegreesToRadians(Angle);
        return -MoonCutOffset * FMath::Cos(A) + FMath::Sqrt(
            FMath::Square(MoonCutRadius) - FMath::Square(MoonCutOffset * FMath::Sin(A)));
    }

    void Crescent(FStrokes& S)
    {
        const double Crossing = FMath::RadiansToDegrees(FMath::Acos(
            (FMath::Square(MoonCutRadius) - 1. - FMath::Square(MoonCutOffset)) / (2. * MoonCutOffset)));
        S.Arc({}, {.993, .993}, -Crossing, Crossing, 48, 1.45, 1.45);
        for (int32 I = 1; I <= 48; ++I)
        {
            const double A = -Crossing + 2. * Crossing * (I - 1) / 48.;
            const double B = -Crossing + 2. * Crossing * I / 48.;
            S.Line(Polar(CutRadius(A), A), Polar(CutRadius(B), B), .92, .92);
        }
        for (double Angle : {-92., -66., -38., 0., 38., 66., 92.})
        {
            const double Inner = CutRadius(Angle);
            Engrave(S, EMotif::Eye, Polar(FMath::Lerp(Inner, .97, .53), Angle),
                Angle + 90., {.098, (1. - Inner) * .24});
        }
        S.Arc({}, {.955, .955}, -115., 115., 42, .52, .52);
    }

    void Lane(FStrokes& S, EDuelMark Mark)
    {
        S.Banks(Mark == EDuelMark::MirrorSeam ? .8 : .62);
        switch (Mark)
        {
        case EDuelMark::PenNib:
        case EDuelMark::WrittenVerdict:
            for (int32 I = 0; I < 4; ++I)
            {
                const double X = -.78 + I * .51;
                S.Path({{X - .18, 0.}, {X - .04, -.57}, {X + .18, 0.}, {X - .04, .57}, {X - .18, 0.}}, .85);
                S.Line({X - .10, 0.}, {X + .14, 0.}, .65, .15);
                if (Mark == EDuelMark::WrittenVerdict)
                {
                    S.Path({{X - .22, .14}, {X - .17, .55}, {X - .10, .60}}, .7);
                }
            }
            break;
        case EDuelMark::CrossThread:
        case EDuelMark::Snare:
            for (int32 I = 0; I < 8; ++I)
            {
                const double X = -.90 + I * .245;
                const double Side = I % 2 ? 1. : -1.;
                S.Path({{X - .07, Side * .74}, {X, 0.}, {X + .14, -Side * .68}}, .9);
                if (Mark == EDuelMark::Snare)
                {
                    S.Path({{X - .08, -Side * .30}, {X, 0.}, {X + .07, Side * .30}}, .72);
                }
            }
            break;
        case EDuelMark::TalonRush:
            for (int32 I = 0; I < 4; ++I)
            {
                const double X = -.69 + I * .42;
                for (int32 Claw = -1; Claw <= 1; ++Claw)
                {
                    const double Y = Claw * .48;
                    S.Line({X - .17, Y - .34}, {X + .01, Y}, .28, 1.30);
                    S.Line({X + .01, Y}, {X + .20, Y + .14}, 1.30, .13);
                }
            }
            break;
        case EDuelMark::CrossAmbush:
            for (int32 I = 0; I < 4; ++I)
            {
                const double X = -.76 + I * .48;
                S.Path({{X - .22, -.81}, {X - .08, -.31}, {X + .05, .21}, {X + .18, .76}}, 1.15);
                S.Path({{X - .22, .81}, {X - .09, .33}, {X + .14, -.29}, {X + .21, -.72}}, .73);
                S.Line({X + .05, .21}, {X + .23, .06}, .86, .16);
                S.Line({X + .14, -.30}, {X + .19, -.82}, .78, .14);
            }
            break;
        case EDuelMark::Reversal:
        case EDuelMark::ShadowVolley:
            for (int32 I = 0; I < 5; ++I)
            {
                const double X = -.78 + I * .35;
                S.Path({{X, -.77}, {X + .20, 0.}, {X, .77}}, .92);
                S.Line({X - .12, 0.}, {X + .16, 0.}, .12, 1.02);
                if (Mark == EDuelMark::Reversal)
                {
                    S.Path({{X - .13, -.47}, {X - .01, 0.}, {X - .13, .47}}, .48);
                }
            }
            break;
        case EDuelMark::NightLane:
            for (int32 I = 0; I < 5; ++I)
            {
                const double X = -.79 + I * .40;
                S.Arc({X, .0}, {.17, .77}, -112., 112., 7, .25, 1.);
                S.Arc({X + .04, .0}, {.10, .52}, -84., 86., 5, .98, .17);
            }
            break;
        case EDuelMark::CrossVerdict:
            for (int32 I = 0; I < 5; ++I)
            {
                const double X = -.80 + .38 * I;
                S.Path({{X - .11, 0.}, {X, -.71}, {X + .25, 0.}, {X, .71}, {X - .11, 0.}}, .87);
                S.Line({X - .15, 0.}, {X + .29, 0.}, .2, 1.17);
                S.Line({X, -.78}, {X, .78}, 1.07, 1.07);
                S.Line({X + .08, -.36}, {X + .08, .36}, .51, .51);
            }
            break;
        case EDuelMark::WingLane:
            for (int32 I = 0; I < 4; ++I)
            {
                const double X = -.76 + I * .49;
                S.Line({X - .18, 0.}, {X + .18, 0.}, .4, 1.25);
                for (double Side : {-1., 1.})
                {
                    S.Path({{X - .18, Side * .76}, {X + .047, Side * .42}, {X + .18, 0.}}, .95);
                    S.Line({X - .088, Side * .61}, {X + .067, Side * .25}, .25, .86);
                }
            }
            break;
        case EDuelMark::SilentBell:
            for (double Sign : {-1., 1.})
            {
                for (int32 I = 1; I <= 4; ++I)
                {
                    const double X = Sign * (I * .205 + .043);
                    S.Arc({X, 0.}, {.12, .72 - .055 * I}, Sign > 0. ? -80. : 100.,
                        Sign > 0. ? 80. : 260., 5, .26, 1.);
                }
            }
            break;
        case EDuelMark::Prism:
            for (int32 I = 0; I < 4; ++I)
            {
                const double X = -.82 + I * .45;
                S.Path({{X, -.70}, {X + .25, 0.}, {X, .70}, {X, -.70}});
                S.Line({X - .10, -.16}, {X + .25, 0.}, .23, .70);
                S.Line({X + .25, 0.}, {X + .38, .63}, .70, .18);
            }
            break;
        case EDuelMark::IcicleAim:
            for (int32 I = 0; I < 5; ++I)
            {
                const double X = -.84 + I * .36;
                S.Path({{X - .12, -.39}, {X + .25, 0.}, {X - .12, .39}, {X - .04, 0.}, {X - .12, -.39}}, .94);
                S.Line({X + .03, 0.}, {X + .23, 0.}, .41, .14);
            }
            break;
        case EDuelMark::MirrorSeam:
        case EDuelMark::MirrorPanel:
            for (int32 I = 0; I < 6; ++I)
            {
                const double X = -.98 + I * .335;
                const double Hand = I % 2 ? -1. : 1.;
                S.Path({{X, Hand * .93}, {X + .14, -.16 * Hand}, {X + .31, -Hand * .86}}, .88);
                if (Mark == EDuelMark::MirrorPanel)
                {
                    S.Line({X + .14, -.16 * Hand}, {X + .26, Hand * .92}, .85, .17);
                }
            }
            break;
        default:
            break;
        }
    }

    void Sweep(FStrokes& S, double Inner, double HalfArc)
    {
        const FVector2D Center = FVector2D::ZeroVector;
        const double Near = Inner + (1. - Inner) * .046;
        S.Arc(Center, {.986, .986}, -HalfArc, HalfArc, 28, 1.22, .79);
        S.Arc(Center, {Near, Near}, -HalfArc, HalfArc, 28, .52, .52);
        for (double Sign : {-1., 1.})
        {
            S.Line(Polar(Near, Sign * HalfArc), Polar(.986, Sign * HalfArc), .51, 1.18);
        }
        const auto Band = [Near](double T) { return FMath::Lerp(Near, .963, T); };
        for (int32 I = 0; I < 10; ++I)
        {
            const double Step = 2. * HalfArc / 10.;
            const double A = -HalfArc + (I + .5) * Step;
            const double Twist = I % 2 ? -.37 * Step : .37 * Step;
            S.Path({Polar(Band(.94), A - Twist), Polar(Band(.69), A + Twist * .31),
                Polar(Band(.24), A + Twist), Polar(Band(.44), A - Twist * .21)}, 1.15);
            S.Line(Polar(Band(.69), A + Twist * .31), Polar(Band(.54), A - Twist * .70), .85, .13);
        }
    }

    void Annulus(FStrokes& S, EDuelMark Mark, double Inner)
    {
        // All ornamental paths stay in the annulus. Keep the hollow floor readable.
        const auto Band = [Inner](double T) { return FMath::Lerp(Inner, .975, T); };
        S.Rim(.987, .70);
        S.Rim(FMath::Min(.98, Inner + FMath::Max((1. - Inner) * .048, S.Weight * 1.25 / S.Extent.X)), 1.);
        for (int32 I = 0; I < 8; ++I)
        {
            const double A = I * 45.;
            switch (Mark)
            {
            case EDuelMark::Reap:
                S.Path({Polar(Band(.88), A - 13.), Polar(Band(.63), A), Polar(Band(.20), A + 12.),
                    Polar(Band(.23), A - 3.), Polar(Band(.54), A - 12.)}, 1.1);
                break;
            case EDuelMark::Archive:
            case EDuelMark::GlassShelter:
            case EDuelMark::ReturningFrost:
                S.Path({Polar(Band(.34), A - 14.), Polar(Band(.86), A - 6.), Polar(Band(.69), A + 11.),
                    Polar(Band(.34), A - 14.)}, .90);
                S.Line(Polar(Band(.60), A - 5.), Polar(Band(.97), A + 13.), .8, .1);
                S.Line(Polar(Band(.69), A + 11.), Polar(Band(.74), A + 20.), .85, .16);
                break;
            case EDuelMark::Procession:
                S.Leaf(Polar(Band(.22), A - 12.), Polar(Band(.90), A + 12.), .43);
                break;
            case EDuelMark::Mantle:
                S.Leaf(Polar(Band(.15), A - 11.), Polar(Band(.81), A + 14.), .29, .92);
                break;
            case EDuelMark::InkWell:
                S.Path({Polar(Band(.89), A - 18.), Polar(Band(.60), A - 5.), Polar(Band(.36), A + 12.),
                    Polar(Band(.22), A + 24.)}, 1.1);
                break;
            case EDuelMark::HaloScript:
                S.Path({Polar(Band(.24), A - 11.), Polar(Band(.28), A + 13.), Polar(Band(.78), A + 9.),
                    Polar(Band(.90), A), Polar(Band(.85), A - 13.), Polar(Band(.24), A - 11.)}, 1.05);
                for (int32 J = 0; J < 3; ++J)
                {
                    const double R = Band(.38 + J * .18);
                    S.Path({Polar(R, A - 8.), Polar(R, A - 2.), Polar(R + .025, A + 3.), Polar(R, A + 7.)}, .73);
                }
                break;
            case EDuelMark::Offering:
                S.Path({Polar(Band(.32), A - 10.), Polar(Band(.87), A), Polar(Band(.32), A + 10.),
                    Polar(Band(.32), A - 10.)}, 1.24);
                S.Line(Polar(Band(.21), A), Polar(Band(.57), A), .80, .24);
                break;
            default:
                S.Path({Polar(Band(.32), A - 16.), Polar(Band(.84), A), Polar(Band(.32), A + 16.)}, 1.1);
                S.Leaf(Polar(Band(.27), A), Polar(Band(.77), A), 1.36, .76);
                break;
            }
        }
    }

    void EmberFan(FStrokes& S, double Inner, double HalfArc)
    {
        const FVector2D Center = FVector2D::ZeroVector;
        S.Arc(Center, {.985, .985}, -HalfArc, HalfArc, 30, 1.16);
        S.Arc(Center, {Inner, Inner}, -HalfArc, HalfArc, 18, .83);
        for (double Side : {-1., 1.})
            S.Line(Polar(Inner, Side * HalfArc), Polar(.985, Side * HalfArc), 1.07, .25);
        for (int32 I = 0; I < 9; ++I)
        {
            const double A = FMath::Lerp(-HalfArc + 11., HalfArc - 11., I / 8.);
            const double Depth = I % 2 ? .48 : .57;
            S.Path({Polar(Inner + .07, A - 4.), Polar(.42, A + 2.),
                Polar(Depth, A - 6.), Polar(.87, A + 4.)}, .94);
            S.Leaf(Polar(Depth, A - 5.), Polar(.85, A + 4.), .58, 1.16);
        }
    }

    void Radial(FStrokes& S, EDuelMark Mark)
    {
        switch (Mark)
        {
        case EDuelMark::Ward: case EDuelMark::Aegis: case EDuelMark::SaltArc: case EDuelMark::Sable:
        case EDuelMark::StarOrb: case EDuelMark::Step: case EDuelMark::IceLance: case EDuelMark::Lantern:
        case EDuelMark::Witness: case EDuelMark::Thread: case EDuelMark::Reprise: case EDuelMark::PrismSeal:
            break;
        default:
            S.Rim(.986, .58);
            break;
        }
        const FVector2D Center = FVector2D::ZeroVector;
        switch (Mark)
        {
        case EDuelMark::PrismSeal:
            S.Polygon(6, .994, 0., 1.15);
            S.Polygon(6, .69, 30., .70);
            S.Polygon(3, .29, 90., .99);
            for (int32 I = 0; I < 6; ++I)
            {
                const double A = I * 60.;
                S.Path({Polar(.23, A), Polar(.41, A + 15.), Polar(.63, A + 3.), Polar(.87, A + 16.)}, .80);
                S.Path({Polar(.70, A - 9.), Polar(.94, A), Polar(.70, A + 9.)}, .97);
                S.Leaf(Polar(.51, A + 5.), Polar(.81, A + 18.), .24, .84);
            }
            break;
        case EDuelMark::StarTrack:
            S.Polygon(8, .69, 22.5, .86);
            S.Polygon(4, .21, 45., 1.12);
            for (int32 I = 0; I < 4; ++I)
            {
                const double A = I * 90.;
                S.Arc(Center, {.52, .52}, A + 10., A + 68., 8, .65, .94);
                S.Path({Polar(.69, A - 13.), Polar(.91, A), Polar(.69, A + 13.)}, .82);
                S.Arc(Polar(.20, A + 24.), {.55, .15}, -105., 96., 24, .65, .31, A + 36.);
                S.Leaf(Polar(.29, A + 6.), Polar(.73, A + 29.), .31, .69);
                for (int32 Tick = 0; Tick < 4; ++Tick)
                    S.Line(Polar(.81, A + 17. + Tick * 7.), Polar(.91, A + 14. + Tick * 6.), .50, .39);
            }
            break;
        case EDuelMark::StarOrb:
            for (int32 I = 0; I < 8; ++I)
            {
                const double A = I * 45.;
                const double R = I % 2 ? .35 : .84;
                S.Path({Polar(.24, A - 13.), Polar(R, A), Polar(.24, A + 13.)}, 1.14);
            }
            S.Polygon(4, .18, 45., .77);
            break;
        case EDuelMark::IceLance:
            S.Path({{-.62, 0.}, {.76, 0.}, {.34, .25}, {.05, 0.}, {.34, -.25}, {.76, 0.}}, 1.14);
            for (double Side : {-1., 1.})
            {
                S.Path({{-.58, Side * .52}, {-.20, Side * .34}, {.07, 0.}, {-.48, Side * .20}}, .87);
            }
            break;
        case EDuelMark::Lantern:
            S.Path({{-.59, -.24}, {-.36, -.48}, {.20, -.42}, {.47, -.22},
                {.47, .22}, {.20, .42}, {-.36, .48}, {-.59, .24}, {-.59, -.24}}, 1.04);
            S.Arc({.48, 0.}, {.26, .22}, -90., 90., 11, .85, .85);
            S.Leaf({-.32, .0}, {.26, .11}, .41, 1.07);
            break;
        case EDuelMark::Witness:
            S.Leaf({-.79, 0.}, {.79, 0.}, .36, 1.15);
            S.Polygon(4, .29, 0., 1.08);
            S.Line({-.48, 0.}, {-.27, 0.}, .18, .74);
            S.Line({.26, 0.}, {.53, 0.}, .92, .12);
            break;
        case EDuelMark::Thread:
            for (double Side : {-1., 0., 1.})
            {
                S.Arc({-.07, .12 * Side}, {.71, .39}, -156., -29., 11, .92, .11, Side * 34.);
            }
            S.Leaf({.27, 0.}, {.75, .05}, .38, 1.03);
            break;
        case EDuelMark::Step:
        case EDuelMark::Reprise:
        {
            const double Aim = Mark == EDuelMark::Step ? 1. : -1.;
            for (int32 I = 0; I < 3; ++I)
            {
                const double X = Aim * (-.58 + I * .32);
                const double Width = .49 - .08 * I;
                S.Path({{X, -Width}, {X + Aim * .26, 0.}, {X, Width}}, 1.21);
                if (Mark == EDuelMark::Reprise)
                {
                    S.Line({X + .05, .64}, {X - .14, .37}, .28, .94);
                }
            }
            break;
        }
        case EDuelMark::Sable:
            S.Arc({-.12, 0.}, {.72, .62}, -119., 112., 17, .24, 1.20, 24.);
            S.Arc({.12, 0.}, {.72, .62}, 61., 292., 17, 1.20, .24, 24.);
            S.Line({-.66, -.43}, {.69, .44}, .22, .84);
            break;
        case EDuelMark::SaltArc:
            S.Arc(Center, {.83, .83}, -79., 79., 17, .87, .87);
            for (int32 I = 0; I < 5; ++I)
            {
                const double A = -66. + I * 33.;
                S.Path({Polar(.26, A - 12.), Polar(.43, A + 7.), Polar(.72, A - 2.)}, 1.14);
            }
            break;
        case EDuelMark::Bind:
        case EDuelMark::BindEcho:
        case EDuelMark::SnareTrap:
            for (int32 I = 0; I < 4; ++I)
            {
                const double A = 45. + I * 90.;
                const double R = Mark == EDuelMark::BindEcho ? .57 : .76;
                S.Path({Polar(R, A), Polar(.53, A + 43.), Polar(.68, A + 95.),
                    Polar(.29, A + 121.), Polar(.24, A + 176.)}, 1.0);
                S.Path({Polar(.69, A - 13.), Polar(.40, A - 22.), Polar(.26, A + 9.)}, .71);
            }
            if (Mark == EDuelMark::SnareTrap)
            {
                S.Polygon(4, .31, 45., 1.18);
            }
            break;
        case EDuelMark::LightRosette:
            for (int32 I = 0; I < 5; ++I)
            {
                const double A = I * 72.;
                S.Leaf(Polar(.14, A), Polar(.86, A), .34, 1.22);
            }
            S.Polygon(5, .16, 0., .77);
            break;
        case EDuelMark::Pillar:
            S.Polygon(8, .32, 22.5, 1.27);
            for (int32 I = 0; I < 8; ++I)
            {
                const double A = I * 45 + 22.5;
                S.Line(Polar(.28, A), Polar(.88, A + 7.), 1.18, .30);
                S.Path({Polar(.47, A - 13.), Polar(.63, A + 1.), Polar(.61, A + 14.)}, .94);
                S.Line(Polar(.69, A + 2.), Polar(.78, A + 15.), .83, .17);
            }
            break;
        case EDuelMark::WitchRain:
            S.Polygon(3, .89, 90., 1.);
            for (int32 I = 0; I < 3; ++I)
            {
                const double A = 90. + I * 120.;
                S.Leaf(Polar(.23, A), Polar(.64, A), .41, 1.15);
                S.Line(Polar(.18, A + 60.), Polar(.34, A + 60.), .25, 1.05);
            }
            break;
        case EDuelMark::InkWell:
        case EDuelMark::InkRupture:
        case EDuelMark::Storm:
        {
            const int32 Arms = Mark == EDuelMark::Storm ? 5 : 3;
            for (int32 I = 0; I < Arms; ++I)
            {
                const double A = I * 360. / Arms;
                FVector2D Last = Polar(.23, A);
                for (int32 J = 1; J <= 10; ++J)
                {
                    const double T = J / 10.;
                    const FVector2D Next = Polar(.23 + T * .67, A + T * (Mark == EDuelMark::InkWell ? 102. : -86.));
                    S.Line(Last, Next, 1.43 - T, 1.2 - T);
                    Last = Next;
                }
            }
            break;
        }
        case EDuelMark::Mantle:
            for (double Side : {-1., 1.})
            {
                S.Arc({-.17, .0}, {.62, .66}, Side * 10., Side * 156., 12, 1.37, .42);
                S.Path({{-.41, Side * .57}, {-.22, Side * .34}, {.55, Side * .17}}, 1.04);
            }
            S.Leaf({.10, 0.}, {-.62, 0.}, .44);
            break;
        case EDuelMark::Ward:
        case EDuelMark::Aegis:
            S.Path({{.21, -.72}, {-.30, -.64}, {-.67, -.37}, {-.78, 0.}, {-.67, .37}, {-.30, .64},
                {.21, .72}, {.64, 0.}, {.21, -.72}}, 1.13);
            S.Line({-.52, 0.}, {.45, 0.}, .46, 1.25);
            for (double Side : {-1., 1.})
            {
                S.Leaf({-.35, Side * .12}, {.19, Side * .55}, .38, .90);
                if (Mark == EDuelMark::Aegis)
                {
                    S.Path({{-.64, Side * .48}, {-.80, Side * .31}, {-.91, 0.}}, .92);
                }
            }
            break;
        case EDuelMark::Votive:
            S.Path({{-.58, -.38}, {-.58, .38}, {-.25, .38}}, 1.15);
            S.Line({-.35, -.30}, {.17, -.30}, 1.04, .72);
            S.Line({-.35, .30}, {.17, .30}, 1.04, .72);
            S.Arc({.17, .0}, {.13, .30}, 0., 360., 12, .75, .75);
            S.Leaf({.25, 0.}, {.80, .10}, .40, 1.13);
            S.Arc(Center, {.85, .76}, 124., 236., 9, .23, 1.08);
            break;
        case EDuelMark::Offering:
            S.Path({{-.68, 0.}, {-.27, -.43}, {.14, -.40}, {.43, 0.},
                {.14, .40}, {-.27, .43}, {-.68, 0.}}, .90);
            S.Line({-.52, 0.}, {.79, 0.}, .58, .16);
            S.Line({-.25, -.54}, {-.25, .54}, 1.30, 1.30);
            break;
        case EDuelMark::PenNib:
            S.Path({{.80, 0.}, {-.24, .46}, {-.66, .33}, {-.66, -.33},
                {-.24, -.46}, {.80, 0.}, {.23, 0.}}, 1.02);
            S.Arc({.09, 0.}, {.14, .14}, 0., 360., 9, 1., .55);
            break;
        case EDuelMark::Leap:
            S.Path({{.72, 0.}, {-.18, .62}, {-.39, .39}, {-.57, .58}, {-.84, .34}, {-.49, 0.},
                {-.84, -.34}, {-.57, -.58}, {-.39, -.39}, {-.18, -.62}, {.72, 0.}}, 1.1);
            S.Line({-.49, 0.}, {.68, 0.}, .3, 1.14);
            break;
        case EDuelMark::Flank:
            S.Arc({-.03, .05}, {.78, .60}, -40., 225., 18, 1.15, .28, 20.);
            S.Arc({-.07, -.03}, {.63, .39}, 208., 425., 12, 1.25, .31, -15.);
            S.Leaf({-.55, -.24}, {.64, .31}, .22, .89);
            S.Path({{.29, .59}, {.64, .31}, {.66, -.02}}, 1.13);
            break;
        case EDuelMark::Wingfall:
        case EDuelMark::Takeoff:
            for (double Sign : {-1., 1.})
            {
                const double Twist = Mark == EDuelMark::Takeoff ? -1. : 1.;
                S.Leaf({-.58 * Twist, Sign * .12}, {.44 * Twist, Sign * .71}, .27, 1.16);
                for (int32 I = 0; I < 4; ++I)
                {
                    const double T = I * .18;
                    S.Line({Twist * (-.48 + T), Sign * (.20 + T * .58)},
                        {Twist * (.16 + T * .86), Sign * (.18 + T)}, .87, .13);
                }
                if (Mark == EDuelMark::Wingfall)
                {
                    S.Leaf({-.20, Sign * .04}, {.84, Sign * .23}, .31, .91);
                }
            }
            break;
        case EDuelMark::Scroll:
        case EDuelMark::Tribunal:
        case EDuelMark::HaloCore:
            S.Path({{-.53, -.59}, {.38, -.59}, {.66, -.38}, {.66, .55}, {-.46, .55}, {-.67, .34},
                {-.67, -.41}, {-.53, -.59}}, 1.08);
            for (int32 I = 0; I < 3; ++I)
            {
                const double X = -.38 + .28 * I;
                S.Path({{X, -.36}, {X + .08, -.12}, {X, .08}, {X + .04, .31}}, .72);
            }
            if (Mark != EDuelMark::Scroll)
            {
                S.Polygon(Mark == EDuelMark::Tribunal ? 6 : 4, .88, 0., .85);
                S.Polygon(4, .17, 45., 1.10);
            }
            break;
        case EDuelMark::Atlas:
            S.Polygon(6, .88, 0., 1.18);
            S.Polygon(6, .56, 30., .72);
            for (int32 I = 0; I < 6; ++I)
            {
                S.Line(Polar(.39, I * 60.), Polar(.85, I * 60.), .30, .87);
            }
            S.Path({{-.19, 0.}, {.19, 0.}}, .82);
            S.Path({{0., -.19}, {0., .19}}, .82);
            break;
        case EDuelMark::Pursuit:
            for (double Side : {-1., 1.})
            {
                S.Path({{-.73, Side * .20}, {-.38, Side * .55}, {.21, Side * .52}, {.67, Side * .18}}, 1.);
                S.Path({{.08, Side * .28}, {.55, 0.}, {.08, -Side * .28}}, .70);
            }
            S.Line({-.66, 0.}, {-.27, 0.}, .1, 1.25);
            break;
        case EDuelMark::Archive:
            S.Polygon(6, .71, 30., 1.16);
            S.Path({{-.61, .31}, {-.30, .07}, {-.06, .22}, {.22, -.10}, {.63, -.27}}, .95);
            S.Path({{-.08, .69}, {-.06, .22}, {-.14, -.22}, {-.36, -.57}}, .80);
            S.Line({.34, .56}, {.22, -.10}, 1.05, .69);
            break;
        case EDuelMark::BrokenRecord:
            for (int32 I = 0; I < 3; ++I)
            {
                const double Radius = .36 + I * .22;
                S.Arc({.04, .05}, {Radius, Radius * .83}, 21. + I * 7., 205. - I * 9., 10, 1.15, .40);
                S.Arc({-.07, -.03}, {Radius, Radius * .84}, 236. + I * 7., 337., 7, .32, .92);
            }
            S.Path({{-.86, -.27}, {-.39, -.04}, {-.18, .19}, {.16, .12}, {.44, .34}, {.76, .33}}, 1.24);
            break;
        case EDuelMark::BellCenter:
            S.Arc({.01, 0.}, {.59, .43}, -100., 100., 13, 1.1, 1.1);
            S.Path({{.02, -.45}, {-.31, -.57}, {-.39, -.64}, {-.39, .64}, {-.31, .57}, {.02, .45}}, 1.22);
            S.Leaf({-.43, 0.}, {-.79, 0.}, .62, .96);
            break;
        case EDuelMark::Tether:
            S.Polygon(8, .76, 22.5, 1.10);
            for (int32 I = 0; I < 8; ++I)
            {
                const double A = I * 45. + 22.5;
                S.Path({Polar(.76, A), Polar(.62, A + 11.), Polar(.37, A),
                    Polar(.62, A - 11.), Polar(.76, A)}, .90);
            }
            break;
        case EDuelMark::EmberCrater:
            for (int32 I = 0; I < 6; ++I)
            {
                const double A = I * 60.;
                S.Path({Polar(.12, A), Polar(.44, A + 16.), Polar(.86, A - 7.), Polar(.91, A + 13.)}, 1.04);
                S.Line(Polar(.44, A + 16.), Polar(.67, A + 40.), .86, .12);
            }
            break;
        default:
            break;
        }
    }
}

void Build(EDuelMark Mark, const FVector& Axis, const FVector2D& Extent, bool bLane, float Inner,
    TArray<FVector>& Vertices, TArray<int32>& Triangles, float HalfArc, float EndScale)
{
    const FVector Forward = Axis.GetSafeNormal2D(UE_SMALL_NUMBER, FVector::ForwardVector);
    FStrokes Strokes{Forward, FVector::CrossProduct(FVector::UpVector, Forward), Extent, bLane,
        FMath::Clamp(Extent.GetMin() * .031, 4.0, 17.0) / FMath::Max(1.f, EndScale), Vertices, Triangles, HalfArc};
    if (Mark == EDuelMark::GildedCross) { Cross(Strokes); return; }
    if (Mark == EDuelMark::MoonCrescent) { Crescent(Strokes); return; }
    if (Mark == EDuelMark::EmberRecoil)
    {
        EmberFan(Strokes, Inner / Extent.X, HalfArc);
    }
    else if (Mark == EDuelMark::TailSweep)
    {
        Sweep(Strokes, Inner / Extent.X, HalfArc);
    }
    else if (bLane)
    {
        Lane(Strokes, Mark);
    }
    else if (Inner > 0.f && Inner < Extent.X)
    {
        Annulus(Strokes, Mark, Inner / Extent.X);
    }
    else
    {
        Radial(Strokes, Mark);
    }
    Inlay(Strokes, Mark, FMath::Clamp(Inner / Extent.X, 0., .98));
}

void BuildGlassField(const FVector2D& Safe, const FVector2D& Extent, float SafeRadius,
    TArray<FVector>& Vertices, TArray<int32>& Triangles)
{
    FStrokes Floor{FVector::ForwardVector, FVector::RightVector, Extent, true, 8., Vertices, Triangles};
    const double Clearance = SafeRadius + Floor.Weight * 1.35;
    const auto Vein = [&](const FVector2D& A, const FVector2D& B, double From, double To)
    {
        const FVector2D D = B - A;
        const double LengthSq = D.SizeSquared();
        if (LengthSq < 1.)
        {
            return;
        }
        // Split the stroke at the safe disk, allowing for the width of the glowing geometry.
        const double Dot = FVector2D::DotProduct(A - Safe, D);
        const double Disc = Dot * Dot - LengthSq * ((A - Safe).SizeSquared() - FMath::Square(Clearance));
        if (Disc > 0.)
        {
            const double Root = FMath::Sqrt(Disc);
            const double Enter = (-Dot - Root) / LengthSq;
            const double Exit = (-Dot + Root) / LengthSq;
            if (Enter < 1. && Exit > 0.)
            {
                if (Enter > .002)
                {
                    Floor.Line(A / Extent, (A + D * Enter) / Extent, From, To);
                }
                if (Exit < .998)
                {
                    Floor.Line((A + D * Exit) / Extent, B / Extent, From, To);
                }
                return;
            }
        }
        Floor.Line(A / Extent, B / Extent, From, To);
    };
    constexpr int32 Columns = 13;
    constexpr int32 Rows = 8;
    FVector2D Points[Rows + 1][Columns + 1];
    for (int32 Y = 0; Y <= Rows; ++Y)
    {
        for (int32 X = 0; X <= Columns; ++X)
        {
            const double XOffset = X == 0 || X == Columns ? 0. : .046 * FMath::Sin(X * 2.9 + Y * 4.1);
            const double YOffset = Y == 0 || Y == Rows ? 0. : .067 * FMath::Cos(X * 3.5 + Y * 2.3);
            Points[Y][X] = Extent * FVector2D(-.989 + 1.978 * X / Columns + XOffset,
                -.989 + 1.978 * Y / Rows + YOffset);
        }
    }
    for (int32 Y = 0; Y < Rows; ++Y)
    {
        for (int32 X = 0; X < Columns; ++X)
        {
            const FVector2D A = Points[Y][X];
            const FVector2D B = Points[Y][X + 1];
            const FVector2D C = Points[Y + 1][X];
            const FVector2D D = Points[Y + 1][X + 1];
            const bool bSlant = (X + 3 * Y) % 4 != 0;
            const FVector2D Corner = ((bSlant ? A + D : B + C) + (A + B + C + D) * .5) * .25 +
                FVector2D(36. * FMath::Sin(X + Y * 4.9), 46. * FMath::Cos(X * 5.3 - Y));
            if ((X * 7 + Y) % 5 != 1)
            {
                Vein(A, B, .48, .38);
            }
            if ((X + Y * 5) % 4 != 3)
            {
                Vein(A, C, .41, .52);
            }
            Vein(bSlant ? A : B, Corner, .43, .89);
            Vein(Corner, bSlant ? D : C, .89, .31);
            const FVector2D Fork = FMath::Lerp(Corner, bSlant ? C : D, .44) + FVector2D(-21, 28);
            Vein(Corner, Fork, .74, .29);
            Vein(Fork, FMath::Lerp(Fork, bSlant ? C : D, .61), .29, .06);
        }
    }
    const double RimRadius = Clearance + 3.5;
    for (int32 I = 0; I < 64; ++I)
    {
        const FVector2D A = Safe + Polar(RimRadius, I * 360. / 64.);
        const FVector2D B = Safe + Polar(RimRadius, (I + 1) * 360. / 64.);
        if (FMath::Abs(A.X) < Extent.X && FMath::Abs(A.Y) < Extent.Y &&
            FMath::Abs(B.X) < Extent.X && FMath::Abs(B.Y) < Extent.Y)
        {
            Vein(A, B, 1.05, 1.05);
        }
    }
}
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelSpellMarkIdentityTest, "ManosabaDuel.NorthTower.SpellMarkIdentity",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelSpellMarkIdentityTest::RunTest(const FString& Parameters)
{
    for (const bool bLane : {false, true})
    {
        TSet<uint32> Silhouettes;
        for (int32 Index = 1; Index < int32(EDuelMark::Count); ++Index)
        {
            // The two custom footprints are posed in world space, not stamped as lanes.
            if (bLane && (EDuelMark(Index) == EDuelMark::GildedCross || EDuelMark(Index) == EDuelMark::MoonCrescent)) continue;
            TArray<FVector> Vertices;
            TArray<int32> Triangles;
            DuelSpellMarks::Build(EDuelMark(Index), FVector::ForwardVector, bLane ? FVector2D(600., 124.) : FVector2D(600.),
                bLane, EDuelMark(Index) == EDuelMark::EmberRecoil ? 144.f : 0.f, Vertices, Triangles,
                EDuelMark(Index) == EDuelMark::EmberRecoil ? 72.f : 180.f);
            TestTrue(FString::Printf(TEXT("Mark %d has authored geometry"), Index), !Vertices.IsEmpty() && !Triangles.IsEmpty());
            const uint32 Shape = FCrc::MemCrc32(Vertices.GetData(), Vertices.Num() * sizeof(FVector));
            TestTrue(FString::Printf(TEXT("Mark %d has its own shape in %s mode"), Index, bLane ? TEXT("lane") : TEXT("radial")),
                !Silhouettes.Contains(Shape));
            Silhouettes.Add(Shape);
        }
    }
    return !HasAnyErrors();
}
#endif
