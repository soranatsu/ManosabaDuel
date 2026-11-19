#include "DuelNorthScenery.h"

#include "DuelGeometry.h"
#include "DuelNorthDressing.h"
#include "DuelNorthLayout.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RandomStream.h"
#include "ProceduralMeshComponent.h"
#include <initializer_list>

#if WITH_DEV_AUTOMATION_TESTS
#include "DuelNorthTower.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#endif

namespace DuelNorthScenery
{
namespace
{
    using namespace DuelNorthLayout;

    enum ESurface { Masonry, Dressed, Slate, Recess, Earth, Leaves, Cream, Amber, Oak, Paper, Bronze, Wax, Rock, Water, BlueGlass, WineGlass, SurfaceCount };

    struct FSection
    {
        TArray<FVector> Vertices, Normals;
        TArray<int32> Indices;
        TArray<FVector2D> UVs;
        TArray<FProcMeshTangent> Tangents;

        void Face(std::initializer_list<FVector> Points, const FVector& Outward)
        {
            const FVector* V = Points.begin();
            FVector Normal = FVector::CrossProduct(V[1] - V[0], V[2] - V[0]).GetSafeNormal();
            if (Normal.IsNearlyZero()) return;
            const bool bFlip = FVector::DotProduct(Normal, Outward) < 0;
            if (bFlip) Normal = -Normal;
            const FVector U = (V[1] - V[0]).GetSafeNormal();
            const FVector W = FVector::CrossProduct(Normal, U);
            const int32 First = Vertices.Num();
            for (const FVector& P : Points)
            {
                Vertices.Add(P);
                Normals.Add(Normal);
                Tangents.Emplace(U, false);
                UVs.Emplace(FVector::DotProduct(P - *V, U) / 100., FVector::DotProduct(P - *V, W) / 100.);
            }
            for (int32 I = 1; I + 1 < int32(Points.size()); ++I)
                Indices.Append({First, First + (bFlip ? I : I + 1), First + (bFlip ? I + 1 : I)});
        }
    };

    struct FNorthSceneryGeometry
    {
        FSection Sections[SurfaceCount];
        TArray<FBox> Solids;

        void Prism(ESurface Surface, const FVector (&Bottom)[4], const FVector (&Top)[4])
        {
            FVector Centre = FVector::ZeroVector;
            FBox Bounds(ForceInit);
            for (int32 I = 0; I < 4; ++I)
            {
                Centre += Bottom[I] + Top[I];
                Bounds += Bottom[I];
                Bounds += Top[I];
            }
            Centre /= 8.;
            Solids.Add(Bounds);
            auto& Mesh = Sections[Surface];
            Mesh.Face({Bottom[0], Bottom[1], Bottom[2], Bottom[3]}, Bottom[0] - Centre);
            Mesh.Face({Top[0], Top[1], Top[2], Top[3]}, Top[0] - Centre);
            for (int32 I = 0; I < 4; ++I)
            {
                const int32 J = (I + 1) % 4;
                Mesh.Face({Bottom[I], Bottom[J], Top[J], Top[I]}, (Bottom[I] + Bottom[J] + Top[I] + Top[J]) * .25 - Centre);
            }
        }

        void Taper(ESurface Surface, const FVector& Low, const FVector2D& LowHalf, const FVector& High, const FVector2D& HighHalf)
        {
            const FVector Bottom[] = {
                Low + FVector(-LowHalf.X, -LowHalf.Y, 0), Low + FVector(LowHalf.X, -LowHalf.Y, 0),
                Low + FVector(LowHalf.X, LowHalf.Y, 0), Low + FVector(-LowHalf.X, LowHalf.Y, 0)};
            const FVector Top[] = {
                High + FVector(-HighHalf.X, -HighHalf.Y, 0), High + FVector(HighHalf.X, -HighHalf.Y, 0),
                High + FVector(HighHalf.X, HighHalf.Y, 0), High + FVector(-HighHalf.X, HighHalf.Y, 0)};
            Prism(Surface, Bottom, Top);
        }

        void Box(ESurface Surface, const FVector& Centre, const FVector& Size)
        {
            const FVector2D Half(Size.X * .5, Size.Y * .5);
            const FVector Z = FVector::UpVector * (Size.Z * .5);
            Taper(Surface, Centre - Z, Half, Centre + Z, Half);
        }

        void Beam(ESurface Surface, const FVector& A, const FVector& B, const FVector& Face, double Width, double Depth)
        {
            const FVector U = FVector::CrossProduct(B - A, Face).GetSafeNormal() * (Width * .5);
            const FVector V = Face * (Depth * .5);
            const FVector Lower[] = {A - U - V, A + U - V, A + U + V, A - U + V};
            const FVector Upper[] = {B - U - V, B + U - V, B + U + V, B - U + V};
            Prism(Surface, Lower, Upper);
        }

        int32 TriangleCount() const
        {
            int32 Total = 0;
            for (const FSection& Section : Sections) Total += Section.Indices.Num() / 3;
            return Total;
        }
    };

    void ArrowSlit(FNorthSceneryGeometry& G, const FVector& Foot, const FVector& Normal, double Width, double Height)
    {
        const FVector U = FVector::CrossProduct(FVector::UpVector, Normal) * (Width * .5);
        const FVector L = Foot - U, R = Foot + U, Z(0, 0, Height - Width);
        const FVector Tip = Foot + FVector(0, 0, Height);
        G.Sections[Recess].Face({L, R, R + Z, L + Z}, Normal);
        G.Sections[Recess].Face({L + Z, R + Z, Tip}, Normal);
        const FVector Front = Normal * 3;
        for (const FVector& Side : {L, R})
        {
            G.Beam(Dressed, Side + Front, Side + Z + Front, Normal, 12, 18);
            G.Beam(Dressed, Side + Z + Front, Tip + Front, Normal, 12, 18);
        }
        G.Beam(Dressed, L + Front, R + Front, Normal, 14, 22);
    }

    void GardenPortalMasonry(FNorthSceneryGeometry& G, const FPortal& P)
    {
        const FRoom* Room = FindRoom(P.RoomA);
        if (!Room) return;
        const FVector Normal = P.Normal * FMath::Sign(FVector::DotProduct(P.Center - Room->Bounds.GetCenter(), P.Normal));
        const FVector U = FVector::CrossProduct(FVector::UpVector, Normal);
        const FVector O = P.Center + Normal * 38.;
        const double Half = P.Width * .5 + 28.;
        const double Head = P.Height + 35.;
        constexpr double Width = 40., Depth = 76.;
        const FVector L = O - U * Half, R = O + U * Half, Z(0, 0, Head);
        const FVector Tip = O + FVector(0, 0, Head + 140.);
        for (const FVector& Side : {L, R})
        {
            G.Beam(Dressed, Side, Side + Z, Normal, Width, Depth);
            const FVector Knuckle = FMath::Lerp(Side + Z, Tip, .54) + U * (FVector::DotProduct(Side - O, U) > 0 ? 22 : -22);
            G.Beam(Dressed, Side + Z, Knuckle, Normal, Width, Depth);
            G.Beam(Dressed, Knuckle, Tip, Normal, Width, Depth);
        }
    }

    void KeepRoof(FNorthSceneryGeometry& G)
    {
        constexpr double Eave = 5230., Ridge = 6770., HalfSpan = 1810., Overhang = 2530.;
        for (double Side : {-1., 1.})
        {
            // Several raised slate courses catch the oblique sun without separate meshes.
            const auto Roof = [=](double X, double T, double Offset)
            { return FVector(X, Side * HalfSpan * T, FMath::Lerp(Ridge, Eave, T) + Offset); };
            for (int32 Course = 0; Course < 22; ++Course)
            {
                const double Low = Course / 22., High = (Course + 1) / 22.;
                G.Sections[Slate].Face({Roof(-Overhang, Low, 4), Roof(Overhang, Low, 4),
                    Roof(Overhang, High, 0), Roof(-Overhang, High, 0)}, FVector::UpVector);
                G.Sections[Slate].Face({Roof(-Overhang, Low, 0), Roof(Overhang, Low, 0),
                    Roof(Overhang, Low, 4), Roof(-Overhang, Low, 4)}, FVector(0, -Side, 0));
            }
            G.Box(Dressed, FVector(0, Side * (HalfSpan - 34), Eave + 3), FVector(2 * Overhang, 108, 69));
            const double X = 2415 * Side;
            G.Sections[Masonry].Face({FVector(X, -HalfSpan, Eave), FVector(X, HalfSpan, Eave), FVector(X, 0, Ridge)}, FVector(Side, 0, 0));
            for (double End : {-1., 1.})
                G.Beam(Dressed, FVector(X + Side * 42, End * HalfSpan, Eave + 42),
                    FVector(X + Side * 42, 0, Ridge + 42), FVector(Side, 0, 0), 63, 114);
            G.Box(Dressed, FVector(X, 0, Eave + 18), FVector(118, 2 * HalfSpan + 36, 72));
            const FVector Rose(X + Side * 11., 0., 5790.);
            const auto Wheel = [&](double Angle,double R,double Depth)
            { return Rose+FVector(Side*Depth,R*FMath::Cos(Angle),R*FMath::Sin(Angle)); };
            for(int32 I=0;I<32;++I)
            {
                const double A=I*(PI/16.), B=(I+1)*(PI/16.);
                const FVector Out(Side,0,0);
                const double PetalA=238.+68.*FMath::Cos(8.*A), PetalB=238.+68.*FMath::Cos(8.*B);
                G.Sections[Dressed].Face({Wheel(A,344,6),Wheel(B,344,6),Wheel(B,304,12),Wheel(A,304,12)},Out);
                G.Sections[I%4<2?BlueGlass:WineGlass].Face({Wheel(A,302,3),Wheel(B,302,3),
                    Wheel(B,PetalB*.94,3),Wheel(A,PetalA*.94,3)},Out);
                G.Sections[BlueGlass].Face({Rose,Wheel(A,PetalA*.92,1),Wheel(B,PetalB*.92,1)},Out);
                G.Beam(Dressed,Wheel(A,PetalA*.94,13),Wheel(B,PetalB*.94,13),Out,13,16);
                if(I%4==0) G.Beam(Dressed,Wheel(A,26,15),Wheel(A,304,15),Out,16,22);
            }
            for (double Bay : {-1., 1.})
                ArrowSlit(G, FVector(X + Side * 5, Bay * 568, 5425), FVector(Side, 0, 0), 130, 410);
        }
        G.Box(Slate, FVector(0, 0, Ridge + 28), FVector(2 * Overhang, 73, 65));
        // Gable shoulders and their steep stone finials are visible from sea
        // level as well as the east terrace; the plinths key into the eaves.
        for (double End : {-1.,1.}) for (double Side : {-1.,1.})
        {
            const auto At=[&](double Z) {return FVector(End*2420.,Side*1660.,Z);};
            G.Taper(Dressed,At(5180),{126,145},At(5270),{110,116});
            G.Box(Masonry,At(5465),{162,170,395});
            G.Box(Dressed,At(5675),{197,204,55});
            G.Taper(Slate,At(5696),{113,118},At(6250),{9,9});
            for(double Face : {-1.,1.})
                ArrowSlit(G,At(5360)+FVector(Face*82,0,0),FVector(Face,0,0),64,192);
            G.Box(Bronze,At(6275),{19,19,64});
        }
    }

    // The octagonal lantern straddles the actual roof ridge. Each open-looking
    // louvre is backed by a solid attic wall and framed by a pair of stone ribs.
    void RoofLantern(FNorthSceneryGeometry& G, double X,double Base,double Radius,double Scale)
    {
        const auto V=[&](int32 I,double R,double H)
        {
            const double A=(I+.5)*PI/4.;
            return FVector(X+R*FMath::Cos(A),R*FMath::Sin(A),Base+H*Scale);
        };
        struct FBelt { double RadiusFactor,Height; ESurface Surface; };
        const FBelt Belts[]={{1.,0,Masonry},{1.,390,Masonry},{1.07,410,Dressed},{1.07,450,Dressed},
            {.89,480,Slate},{.84,840,Masonry},{1.03,870,Dressed},{1.,902,Dressed},
            {.64,1090,Slate},{.06,1650,Slate},{.001,1730,Bronze}};
        for(int32 I=0;I<8;++I)
        {
            const FVector Out(FMath::Cos((I+1)*PI/4.),FMath::Sin((I+1)*PI/4.),0);
            for(int32 B=1; B<UE_ARRAY_COUNT(Belts);++B)
            {
                const auto& L=Belts[B-1];const auto& H=Belts[B];
                G.Sections[H.Surface].Face({V(I,Radius*L.RadiusFactor,L.Height),V(I+1,Radius*L.RadiusFactor,L.Height),
                    V(I+1,Radius*H.RadiusFactor,H.Height),V(I,Radius*H.RadiusFactor,H.Height)},Out+FVector(0,0,.2));
            }
            const FVector Middle=(V(I,Radius*.90,492)+V(I+1,Radius*.90,492))*.5;
            ArrowSlit(G,Middle,Out,Radius*.38,306*Scale);
            G.Beam(Dressed,V(I,Radius*.96,455),V(I,Radius*.94,843),Out,28*Scale,34*Scale);
        }
    }

    void FlyingButtress(FNorthSceneryGeometry& G, double X, double Side)
    {
        const auto At=[&](double Y,double Z) { return FVector(X,Side*Y,Z); };
        G.Taper(Masonry,At(1835,3434),{110,95},At(2032,3552),{92,276});
        G.Taper(Masonry,At(2210,3550),{88,185},At(2250,4690),{62,103});
        G.Box(Dressed,At(2250,4695),{157,226,52});
        G.Taper(Slate,At(2250,4712),{80,114},At(2250,5205),{10,14});
        for(int32 I=0;I<8;++I)
        {
            const auto Arch=[&](int32 J) {
                const double T=J/8.;
                return At(FMath::Lerp(2250.,1755.,T),4660.+460.*FMath::Sin(T*PI*.5));
            };
            G.Beam(Dressed,Arch(I),Arch(I+1),FVector(1,0,0),68.,104.);
        }
    }

    void Watchtower(FNorthSceneryGeometry& G, double X, double Y, double Crown, bool bSpire)
    {
        const auto At = [=](double Z) { return FVector(X, Y, Z); };
        G.Taper(Masonry, At(-1790), FVector2D(267, 495), At(1040), FVector2D(250, 383));
        G.Taper(Masonry, At(1038), FVector2D(250, 383), At(Crown - 350), FVector2D(212, 258));
        G.Box(Dressed, At(1040), FVector(535, 810, 84));
        G.Box(Dressed, At(2800), FVector(492, 712, 64));
        G.Taper(Dressed, At(Crown - 354), FVector2D(215, 264), At(Crown - 220), FVector2D(290, 350));
        G.Box(Dressed, At(Crown - 218), FVector(616, 756, 76));
        G.Box(Masonry, At(Crown - 122), FVector(552, 682, 125));
        G.Box(Dressed, At(Crown - 40), FVector(609, 728, 62));
        for (double Side : {-1., 1.})
            for (double Bay : {-246., -144., -42., 60., 162., 264.})
                G.Taper(Dressed, FVector(X + Side * 235., Y + Bay, Crown - 390.),
                    FVector2D(21, 28), FVector(X + Side * 257., Y + Bay, Crown - 244.), FVector2D(39, 43));
        for (double Offset : {-225., 0., 225.})
            ArrowSlit(G, FVector(X + 278, Y + Offset, Crown - 174), FVector::ForwardVector, 36, 104);
        if (bSpire)
        {
            G.Taper(Slate, At(Crown - 9), FVector2D(322, 397), At(Crown + 420), FVector2D(208, 238));
            G.Taper(Slate, At(Crown + 418), FVector2D(208, 238), At(Crown + 1760), FVector2D(17, 17));
            G.Box(Dressed, At(Crown + 1740), FVector(53, 53, 133));
            G.Box(Dressed, At(Crown + 1825), FVector(84, 84, 45));
            G.Box(Dressed, At(Crown + 1900), FVector(23, 23, 139));
        }
        else
        {
            for (double Side : {-1., 1.})
            {
                for (double Along : {-248., -78., 78., 248.})
                    G.Box(Dressed, At(Crown + 46) + FVector(Side * 235, Along, 0), FVector(87, 82, 116));
                for (double Along : {-104., 104.})
                    G.Box(Dressed, At(Crown + 46) + FVector(Along, Side * 300, 0), FVector(85, 85, 116));
            }
        }
    }

    void TerraceAndBridge(FNorthSceneryGeometry& G)
    {
        constexpr double DeckUnderside = 3360.;
        G.Taper(Masonry, FVector(6350, 0, 2885), FVector2D(1118, 1024),
            FVector(6350, 0, DeckUnderside), FVector2D(1234, 1187));
        G.Box(Dressed, FVector(6350, 0, 3361), FVector(2488, 2396, 58));
        for (double Side : {-1., 1.})
        {
            G.Box(Dressed, FVector(6350, Side * 1175, 3277), FVector(2484, 74, 45));
            for (double X : {5315., 5860., 6415., 6965., 7415.})
                G.Taper(Masonry, FVector(X, Side * 1085, 2810), FVector2D(76, 99),
                    FVector(X, Side * 1180, 3285), FVector2D(42, 44));
        }
        G.Box(Dressed, FVector(7542, 0, 3277), FVector(76, 2377, 45));
        for (double X : {5288., 7425.}) for (double Side : {-1., 1.})
            G.Taper(Masonry, FVector(X, Side * 1010, 2230), FVector2D(137, 148),
                FVector(X, Side * 1095, 2990), FVector2D(95, 104));
        // The bridge deck shares its load between a tower-side abutment and the headland.
        G.Taper(Masonry, FVector(3508, 0, 2121), FVector2D(190, 331),
            FVector(3508, 0, 3370), FVector2D(164, 270));
        G.Taper(Masonry, FVector(4800, 0, 2310), FVector2D(243, 369),
            FVector(4800, 0, 3370), FVector2D(183, 270));
        constexpr int32 ArchCourses = 14;
        const auto Arch = [](int32 I)
        {
            const double T = I / double(ArchCourses);
            return FVector(FMath::Lerp(3625., 4685., T), 0, 2610. + 577. * FMath::Sin(PI * T));
        };
        for (int32 I = 0; I < ArchCourses; ++I)
        {
            const FVector A = Arch(I), B = Arch(I+1), N(0, 240, 0);
            const FVector Bottom[] = {A-N, B-N, B+N, A+N};
            const FVector Top[] = {
                FVector(A.X, -240, DeckUnderside), FVector(B.X, -240, DeckUnderside),
                FVector(B.X, 240, DeckUnderside), FVector(A.X, 240, DeckUnderside)};
            G.Prism(Masonry, Bottom, Top);
            for (double Side : {-1., 1.})
            {
                const FVector Plane(0, Side, 0);
                G.Beam(Dressed, A + Plane * 254, B + Plane * 254, Plane, 43, 46);
            }
        }
        G.Box(Dressed, FVector(4200, 0, 3358), FVector(1784, 540, 53));
    }

    void CloseKeepElevations(FNorthSceneryGeometry& G)
    {
        // Bridge the vertical setbacks between the enclosed floors from the outside.
        G.Box(Masonry, FVector(0, -1960, 1420), FVector(4824, 220, 540));
        G.Box(Dressed, FVector(0, -1963, 1689), FVector(4870, 246, 54));
        // A pitched stone cap joins the sea gallery ledge to the higher keep's wall.
        const FVector GalleryCapBase[] = {
            FVector(-2412, -1873, 1632), FVector(2412, -1873, 1632),
            FVector(2412, -1705, 1668), FVector(-2412, -1705, 1668)};
        const FVector GalleryCapTop[] = {
            FVector(-2412, -1873, 1674), FVector(2412, -1873, 1674),
            FVector(2412, -1705, 1715), FVector(-2412, -1705, 1715)};
        G.Prism(Dressed, GalleryCapBase, GalleryCapTop);
        for (double Side : {-1., 1.})
        {
            G.Box(Masonry, FVector(0, Side * 1760, 3324), FVector(4834, 117, 160));
            G.Box(Dressed, FVector(0, Side * 1775, 3429), FVector(4834, 139, 51));
            for (double X : {-1930., -720., 720., 1930.})
            {
                G.Taper(Masonry, FVector(X, Side * 1840, 3420), FVector2D(112, 125),
                    FVector(X, Side * 1776, 5090), FVector2D(62, 66));
                G.Box(Dressed, FVector(X, Side * 1790, 5105), FVector(158, 150, 47));
            }
        }
        // The cantilever-like west arrival wing bears on a wall between its piers.
        G.Taper(Masonry, FVector(-3500, -1460, 1140), FVector2D(181, 694),
            FVector(-3500, -1460, 3367), FVector2D(172, 670));
        for (double Y : {-2000., -850.})
        {
            G.Taper(Masonry, FVector(-3500, Y, 1140), FVector2D(148, 144),
                FVector(-3500, Y, 3367), FVector2D(130, 118));
            G.Box(Dressed, FVector(-3500, Y, 3325), FVector(317, 293, 74));
        }
    }

    FNorthSceneryGeometry Exterior()
    {
        FNorthSceneryGeometry G;
        // Above the existing island rock and outside the farthest dungeon/stair room.
        // The low south wall stays below the three downward lines of sight to the sea shelf.
        for (double Side : {-1., 1.})
        {
            G.Box(Masonry, FVector(Side * 3800, 0, -320), FVector(176, 5570, 2840));
            G.Box(Dressed, FVector(Side * 3828, 0, 1074), FVector(226, 5585, 54));
            G.Box(Masonry, FVector(0, Side * 2875, -730), FVector(7515, 245, 2020));
            G.Box(Dressed, FVector(0, Side * 2887, 319), FVector(7533, 263, 82));
            // Stagger the bond where the side curtain meets its base and coping.
            for (int32 Bay = -5; Bay <= 5; ++Bay)
            {
                const double Y = 473. * Bay;
                for (int32 Course = 0; Course < 4; ++Course)
                {
                    const double Offset = Course % 2 ? 93. : -57.;
                    G.Taper(Dressed, FVector(Side * 3888., Y + Offset, -1570. + 91. * Course),
                        FVector2D(34., 126. - 7. * (Course % 3)),
                        FVector(Side * 3889., Y + Offset + 5., -1570. + 91. * Course + 77.),
                        FVector2D(29., 115. - 7. * (Course % 3)));
                }
                G.Taper(Dressed, FVector(Side * 3911., Y, 947.), FVector2D(22., 36.),
                    FVector(Side * 3919., Y, 1048.), FVector2D(41., 47.));
            }
            // These wings join the east turrets to the cleared space outside the upper courts.
            G.Box(Masonry, FVector(3150, Side * 1860, 3010), FVector(1476, 620, 1555));
            G.Box(Dressed, FVector(3165, Side * 1860, 3775), FVector(1508, 670, 75));
            G.Taper(Slate, FVector(3194, Side * 1860, 3790), FVector2D(780, 350),
                FVector(3135, Side * 1860, 4110), FVector2D(720, 43));
        }
        Watchtower(G, 4010, 2015, 5565, true);
        Watchtower(G, 4000, -1960, 4760, false);
        // The outer curtain and rock shoulder carry the southeast tower together.
        G.Taper(Masonry, FVector(4202, -2655, -1785), FVector2D(475, 476),
            FVector(4093, -2505, 1070), FVector2D(286, 243));
        G.Box(Dressed, FVector(4080, -2508, 1076), FVector(598, 498, 47));
        TerraceAndBridge(G);
        CloseKeepElevations(G);

        G.Box(Dressed, FVector(0, -2383, 791), FVector(4805, 65, 53));
        G.Box(Dressed, FVector(0, -2399, 1138), FVector(4805, 92, 47));
        for (double X : {-2220., -630., 630., 2220.})
        {
            G.Box(Masonry, FVector(X, -2690, 408), FVector(284, 544, 106));
            G.Taper(Masonry, FVector(X, -2670, 455), FVector2D(139, 280),
                FVector(X, -2416, 1080), FVector2D(76, 58));
            G.Box(Dressed, FVector(X, -2433, 1095), FVector(180, 159, 45));
        }
        KeepRoof(G);
        RoofLantern(G,-350,6385,398,1.12);
        for(double Side:{-1.,1.})
            for(double X:{-1875.,0.,1875.}) FlyingButtress(G,Side < 0 && X > 1800 ? 2120. : X,Side);
        for (const FPortal& P : Portals()) if (P.Id == TEXT("GardenEntry")) GardenPortalMasonry(G, P);
        return G;
    }

    struct FCliffLevel
    {
        FVector Centre;
        FVector2D Radius;
    };

    template<int32 Sectors = 22>
    void ClosedCliff(FNorthSceneryGeometry& G, TArrayView<const FCliffLevel> Levels)
    {
        auto& Mesh = G.Sections[Rock];
        FBox Bounds(ForceInit);
        FVector Previous[Sectors];
        for (int32 Row = 0; Row < Levels.Num(); ++Row)
        {
            const FCliffLevel& Level = Levels[Row];
            FVector Rim[Sectors];
            for (int32 I = 0; I < Sectors; ++I)
            {
                const double T = 2. * PI * I / Sectors;
                const double Shape = .97 + .047 * FMath::Sin(T * 5. + Row * .24) + .035 * FMath::Cos(T * 7. - Row * .30)
                    + .024 * FMath::Abs(FMath::Sin(T * 11. + Row * .11));
                const double Lift = (Row > 0 && Row+1 < Levels.Num()) ? 133. * FMath::Cos(T * 3. + Row) : 0.;
                Rim[I] = Level.Centre + FVector(FMath::Cos(T) * Level.Radius.X * Shape,
                    FMath::Sin(T) * Level.Radius.Y * Shape, Lift);
                Bounds += Rim[I];
            }
            for (int32 I = 0; I < Sectors; ++I)
            {
                const int32 J = (I+1) % Sectors;
                if (Row == 0) Mesh.Face({Level.Centre, Rim[J], Rim[I]}, -FVector::UpVector);
                if (Row+1 == Levels.Num()) Mesh.Face({Level.Centre, Rim[I], Rim[J]}, FVector::UpVector);
                if (Row > 0)
                {
                    const FVector Out(Rim[I].X + Rim[J].X - 2 * Level.Centre.X,
                        Rim[I].Y + Rim[J].Y - 2 * Level.Centre.Y, 0);
                    Mesh.Face({Previous[I], Previous[J], Rim[I]}, Out);
                    Mesh.Face({Rim[I], Previous[J], Rim[J]}, Out);
                }
            }
            for (int32 I = 0; I < Sectors; ++I) Previous[I] = Rim[I];
        }
        G.Solids.Add(Bounds);
    }

    void SouthRevetment(FNorthSceneryGeometry& G)
    {
        // The scanned outer shelf forms the seaward edge of one rock bank.
        // The inward crest stays below the original three sea-window sightlines.
        struct FShoulder { double X, InnerY, InnerZ, OuterY, OuterZ; };
        const FShoulder Stations[] = {
            {-7850, -3830, -800, -4530, -885}, {-6840, -3440, -360, -4670, -785},
            {-5200, -3140, -35, -4610, -785}, {-3700, -2930, 110, -4610, -760},
            {-2000, -2960, 95, -4710, -828}, {0, -2940, 125, -4880, -865},
            {1650, -2940, 120, -4645, -800}, {3320, -2910, 110, -4550, -780},
            {4500, -2900, 165, -4490, -780}, {5400, -2700, 170, -4250, -800},
            {6200, -2220, -220, -3530, -960}, {6800, -1640, -900, -2590, -1290}
        };
        const auto Ridge = [](const FShoulder& S, int32 Index)
        {
            return FVector(S.X, FMath::Lerp(S.InnerY, S.OuterY, .48),
                FMath::Lerp(S.InnerZ, S.OuterZ, .48) + 54. * FMath::Sin(Index * 2.1));
        };
        for (int32 I = 1; I < UE_ARRAY_COUNT(Stations); ++I)
        {
            const FShoulder& A = Stations[I - 1];
            const FShoulder& B = Stations[I];
            const FVector MidA = Ridge(A, I - 1), MidB = Ridge(B, I);
            const FVector Inland[] = {FVector(A.X, A.InnerY, A.InnerZ), FVector(B.X, B.InnerY, B.InnerZ), MidB, MidA};
            const FVector Seaward[] = {MidA, MidB, FVector(B.X, B.OuterY, B.OuterZ), FVector(A.X, A.OuterY, A.OuterZ)};
            for (const FVector* Surface : {Inland, Seaward})
            {
                const FVector Top[] = {Surface[0], Surface[1], Surface[2], Surface[3]};
                const FVector Bottom[] = {
                    FVector(Top[0].X, Top[0].Y, -3440), FVector(Top[1].X, Top[1].Y, -3440),
                    FVector(Top[2].X, Top[2].Y, -3440), FVector(Top[3].X, Top[3].Y, -3440)};
                G.Prism(Rock, Bottom, Top);
            }
        }
    }

    FNorthSceneryGeometry CoastalSurface(const FNorthSceneryGeometry& Volumes)
    {
        // Rasterize the union of overlapping rock forms to one continuous skin.
        // A terrace can cover several forms without exposing a cap, a back face
        // or two intersecting surfaces when seen from the leaving aircraft.
        constexpr double Cell=148., X0=-9050., Y0=-6100., Bed=-3150.;
        constexpr int32 NX=132, NY=92;
        TArray<double> Heights;
        Heights.Init(Bed,NX*NY);
        const FSection& Cuts=Volumes.Sections[Rock];
        for(int32 I=0;I<Cuts.Indices.Num();I+=3)
        {
            const FVector& A=Cuts.Vertices[Cuts.Indices[I]];
            const FVector B=Cuts.Vertices[Cuts.Indices[I+1]]-A, C=Cuts.Vertices[Cuts.Indices[I+2]]-A;
            const double Det=B.X*C.Y-C.X*B.Y;
            if(FMath::Abs(Det)<.01)continue;
            const int32 L=FMath::Max(0,FMath::CeilToInt((A.X+FMath::Min3(0.,B.X,C.X)-X0)/Cell));
            const int32 R=FMath::Min(NX-1,FMath::FloorToInt((A.X+FMath::Max3(0.,B.X,C.X)-X0)/Cell));
            const int32 S=FMath::Max(0,FMath::CeilToInt((A.Y+FMath::Min3(0.,B.Y,C.Y)-Y0)/Cell));
            const int32 N=FMath::Min(NY-1,FMath::FloorToInt((A.Y+FMath::Max3(0.,B.Y,C.Y)-Y0)/Cell));
            for(int32 Y=S;Y<=N;++Y) for(int32 X=L;X<=R;++X)
            {
                const double DX=X0+X*Cell-A.X, DY=Y0+Y*Cell-A.Y;
                const double U=(DX*C.Y-DY*C.X)/Det, V=(B.X*DY-B.Y*DX)/Det;
                if(U>=-.00001 && V>=-.00001 && U+V<=1.00001)
                    Heights[Y*NX+X]=FMath::Max(Heights[Y*NX+X],A.Z+U*B.Z+V*C.Z);
            }
        }
        FNorthSceneryGeometry G;
        FSection& RockMesh=G.Sections[Rock];
        for(int32 Y=0;Y<NY;++Y) for(int32 X=0;X<NX;++X)
        {
            const double Z=Heights[Y*NX+X];
            const double DX=Heights[Y*NX+FMath::Min(X+1,NX-1)]-Heights[Y*NX+FMath::Max(0,X-1)];
            const double DY=Heights[FMath::Min(Y+1,NY-1)*NX+X]-Heights[FMath::Max(0,Y-1)*NX+X];
            const FVector Normal=FVector(-DX,-DY,2.*Cell).GetSafeNormal();
            const double Relief=(FMath::Sin(X*.67+Y*.24)*12.+FMath::Cos(Y*.89-X*.35)*9.-21.)*(1.-Normal.Z);
            const FVector P(X0+Cell*X,Y0+Cell*Y,Z+Relief);
            RockMesh.Vertices.Add(P);
            RockMesh.Normals.Add(Normal);
            RockMesh.UVs.Add(FVector2D(P.X+P.Z*.43,P.Y+P.Z*.57)/350.);
            RockMesh.Tangents.Emplace(FVector::CrossProduct(FVector::RightVector,Normal).GetSafeNormal(),false);
        }
        // Omit the submerged grid beyond the rock; close every exposed edge
        // below the swell trough, so even a low sea view has a solid coastline.
        TSet<FIntPoint> Edges;
        TMap<int32,int32> BottomIds;
        const auto Bottom=[&](int32 Top)
        {
            if(const int32* Index=BottomIds.Find(Top)) return *Index;
            const FVector Surface=RockMesh.Vertices[Top];
            const int32 New=RockMesh.Vertices.Add(FVector(Surface.X,Surface.Y,Bed-60.));
            RockMesh.Normals.Add(-FVector::UpVector);RockMesh.UVs.Add(FVector2D(Surface)/350.);
            RockMesh.Tangents.Emplace(FVector::ForwardVector,false);
            BottomIds.Add(Top,New);return New;
        };
        for(int32 Y=0;Y<NY-1;++Y) for(int32 X=0;X<NX-1;++X)
        {
            const int32 A=Y*NX+X,B=A+1,D=A+NX,C=D+1;
            const double Highest=FMath::Max(FMath::Max(Heights[A],Heights[B]),FMath::Max(Heights[C],Heights[D]));
            if(Highest < SeaLevelCm-430.) continue;
            RockMesh.Indices.Append({A,C,B,A,D,C});
            const int32 A0=Bottom(A),B0=Bottom(B),C0=Bottom(C),D0=Bottom(D);
            RockMesh.Indices.Append({A0,B0,C0,A0,C0,D0});
            for(const FIntPoint Edge : {FIntPoint(A,B),FIntPoint(B,C),FIntPoint(C,D),FIntPoint(D,A)})
                if(!Edges.Remove(FIntPoint(Edge.Y,Edge.X)))Edges.Add(Edge);
            G.Solids.Emplace(
                FVector(X0+X*Cell,Y0+Y*Cell,Bed-60.),FVector(X0+(X+1)*Cell,Y0+(Y+1)*Cell,Highest));
        }
        for(const FIntPoint Edge:Edges)
        {
            const int32 A=Edge.X,B=Edge.Y,A0=Bottom(A),B0=Bottom(B);
            RockMesh.Indices.Append({A,B,B0,A,B0,A0});
        }
        return G;
    }

    FNorthSceneryGeometry CliffBackfill()
    {
        FNorthSceneryGeometry G;
        const FCliffLevel NorthBase[] = {
            {{0, 0, -3420}, {4430, 3500}},
            {{0, 0, -2230}, {4310, 3440}},
            {{0, 0, -1738}, {3920, 2960}}
        };
        const FCliffLevel Headland[] = {
            {{6270, 0, -3200}, {2320, 2050}},
            {{6320, -35, -1810}, {2240, 1780}},
            {{6320, 50, -150}, {2110, 1660}},
            {{6410, -45, 1420}, {1920, 1550}},
            {{6440, 45, 2670}, {1630, 1350}},
            {{6370, 0, 3240}, {1240, 1095}}
        };
        // Screen rays through the largest shaded cuts met the south scan
        // layers at X 7356–7616, Y -1619–-1568, and Z 749–2744.
        const FCliffLevel HeadlandSpur[] = {
            {{7515, -1280, -2440}, {1000, 720}},
            {{7520, -1315, -1120}, {954, 692}},
            {{7490, -1360, 190}, {854, 639}},
            {{7460, -1340, 1170}, {754, 624}},
            {{7440, -1320, 2140}, {664, 601}},
            {{7400, -1290, 2930}, {514, 483}},
            {{7200, -1120, 3290}, {338, 312}},
        };
        // A lower bank ties the seaward shelf back into the tower rock.
        const FCliffLevel SouthApron[] = {
            {{-380, -4060, -3310}, {5660, 1230}},
            {{-395, -4090, -2200}, {5400, 1180}},
            {{-435, -4020, -1560}, {4970, 950}},
            {{-370, -4120, -939}, {4290, 659}},
        };
        ClosedCliff<42>(G, NorthBase);
        ClosedCliff<51>(G, Headland);
        // Northern tidal benches tie the rectangular prison curtain back into
        // the island. Both shelves have real sides and extend below low water.
        const FCliffLevel NorthRidge[] = {
            {{-870,4210,-2940},{4650,1320}},{{-950,4340,-2070},{4490,1110}},
            {{-1120,4230,-1600},{4110,895}},{{-1190,4170,-1120},{3420,590}},
            {{-1600,4240,-855},{2020,390}}
        };
        ClosedCliff<43>(G,NorthRidge);
        const FCliffLevel WestReef[] = {
            {{-4420,90,-2980},{1690,3180}},{{-4520,120,-2100},{1550,3020}},
            {{-4320,150,-1910},{1050,2490}},{{-4150,180,-1746},{780,1910}}
        };
        ClosedCliff<37>(G,WestReef);
        const FCliffLevel EastFooting[] = {
            {{4680, -1880, -3150}, {802, 1080}}, {{4625, -1810, -1810}, {740, 990}},
            {{4690, -1740, -510}, {790, 955}}, {{4700, -1720, 910}, {818, 925}},
            {{4800, -1670, 1740}, {842, 876}}, {{4870, -1610, 2390}, {880, 785}},
            {{5100, -1585, 3010}, {683, 674}}, {{5180, -1530, 3325}, {630, 655}},
        };
        ClosedCliff(G, HeadlandSpur);
        ClosedCliff(G, SouthApron);
        ClosedCliff(G, EastFooting);
        // Solid shoulder beneath the east guard. Shorter south scans sit in the
        // slope instead of exposing their thin upper edges beside the terrace.
        const FCliffLevel GardenEastShoulder[] = {
            {{8150, -88, -3260}, {1325, 1910}},
            {{8200, -80, -1700}, {1200, 1880}},
            {{8210, -165, -100}, {1110, 1790}},
            {{8240, -510, 1160}, {1065, 1540}},
            {{8195, -780, 2040}, {930, 1170}},
            {{8090, -1000, 2510}, {780, 950}},
            {{7965, -1170, 2930}, {715, 790}},
            {{7800, -1250, 3328}, {620, 540}},
        };
        ClosedCliff<43>(G, GardenEastShoulder);
        SouthRevetment(G);
        return CoastalSurface(G);
    }

    FNorthSceneryGeometry RescueStationIsland()
    {
        FNorthSceneryGeometry G;
        const FCliffLevel Levels[] = {
            {FVector(15000,-8500,-3100),FVector2D(3100,2600)},
            {FVector(15000,-8500,-2130),FVector2D(2850,2390)},
            {FVector(15000,-8500,-1640),FVector2D(2340,2050)},
            {FVector(15000,-8500,-1510),FVector2D(1920,1680)}};
        ClosedCliff<22>(G,Levels);
        G.Box(Dressed,FVector(15000,-8500,-1510),FVector(2480,2630,42));
        return G;
    }

    void RescueStationAndWeather(AActor* Owner,USceneComponent* Parent)
    {
        const TCHAR* TowerPath=TEXT("/Game/DuelArt/NorthTower/Rescue/Meshes/SM_NorthRescueTower.SM_NorthRescueTower");
        if(auto* Tower=LoadObject<UStaticMesh>(nullptr,TowerPath))
        {
            auto* C=NewObject<UStaticMeshComponent>(Owner);C->SetupAttachment(Parent);
            C->SetMobility(EComponentMobility::Static);C->SetStaticMesh(Tower);
            C->SetRelativeLocationAndRotation(FVector(15000,-8500,-1488),FRotator(0,15,0));
            C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetCanEverAffectNavigation(false);
            C->SetCastShadow(true);C->SetCullDistance(65000);C->ComponentTags.Add(TEXT("NorthCoastRescueTower"));
            Owner->AddInstanceComponent(C);C->RegisterComponent();
        }
        else UE_LOG(LogTemp,Error,TEXT("Missing imported CC0 coastal rescue tower"));
        const auto SkyMaterial=[](const TCHAR* Name)
        {
            const FString Path=FString::Printf(TEXT("/Game/DuelArt/NorthTower/NightSky346/%s.%s"),Name,Name);
            return LoadObject<UMaterialInterface>(nullptr,*Path);
        };
        const auto SkySphere=[&](const FVector& Position,const FVector& Size,UMaterialInterface* Material,
            FName Tag,const FRotator& Rotation=FRotator::ZeroRotator)
        {
            // Set render exclusions before registration: a sky-sized Nanite shadow proxy is never valid.
            auto* C=NewObject<UStaticMeshComponent>(Owner);
            C->SetupAttachment(Parent);C->SetMobility(EComponentMobility::Static);
            C->SetStaticMesh(DuelGeometry::Mesh(DuelGeometry::EShape::Ball));
            C->SetRelativeLocationAndRotation(Position,Rotation);C->SetRelativeScale3D(Size/100.);
            C->SetMaterial(0,Material);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            C->SetCanEverAffectNavigation(false);C->SetCastShadow(false);
            C->bDisallowNanite=true;C->SetVisibleInRayTracing(false);
            C->SetAffectDynamicIndirectLighting(false);C->SetAffectDistanceFieldLighting(false);
            C->SetCullDistance(0);C->ComponentTags.Add(Tag);
            Owner->AddInstanceComponent(C);C->RegisterComponent();
            return C;
        };
        if(auto* NightMaterial=SkyMaterial(TEXT("M_NorthNightSky346")))
        {
            // Beyond the complete 65 km ocean: no horizon clipping, shadows, collision, or runtime noise.
            SkySphere(FVector(0,0,SeaLevelCm),FVector(24000000),NightMaterial,TEXT("NorthNightSky346"));
        }
        if(auto* MoonMaterial=SkyMaterial(TEXT("M_NorthMoon346")))
        {
            // About fourteen degrees above the normal garden horizon, clear of both HUD panels.
            SkySphere(FVector(380000,35000,100000),FVector(14000),MoonMaterial,
                TEXT("NorthVisibleMoon"),FRotator(0,-85,0));
        }
        if(auto* CloudMaterial=SkyMaterial(TEXT("M_NorthMoonlitClouds346")))
        {
            // A single inside-visible shell reaches the horizon; high flat sheets missed normal player views.
            auto* Clouds=SkySphere(FVector(0,0,SeaLevelCm),FVector(650000),CloudMaterial,
                TEXT("NorthDriftingCloudLayer"));
            Clouds->SetTranslucentSortPriority(-10);
        }
    }

    FNorthSceneryGeometry DistantOcean()
    {
        FNorthSceneryGeometry G;
        constexpr int32 Sectors = 192;
        constexpr double GroundRadius = PlanetRadiusKm * 100000.;
        // Visible coastal water needs vertices for the material's metre-high travelling swell.
        TArray<double> Rings;
        for(double R=400.;R<=24000.;R+=400.)Rings.Add(R);
        const double OuterRings[] = {30000.,38000.,46000.,62000.,85000.,125000.,260000.,490000.,850000.,1320000.,
            1940000., 2640000., 3380000., 4270000., 5260000., 6500000.};
        Rings.Append(OuterRings,UE_ARRAY_COUNT(OuterRings));
        const auto Point = [](double R, int32 I)
        {
            const double A = 2. * PI * (I % Sectors) / Sectors;
            // Stable spherical sagitta, with the same radius as the sky's sea-level surface.
            const double Z = SeaLevelCm - R * R / (GroundRadius + FMath::Sqrt(GroundRadius * GroundRadius - R * R));
            return FVector(R * FMath::Cos(A), R * FMath::Sin(A), Z);
        };
        for (int32 I = 0; I < Sectors; ++I)
        {
            G.Sections[Water].Face({FVector(0, 0, SeaLevelCm), Point(Rings[0], I), Point(Rings[0], I+1)}, FVector::UpVector);
            for (int32 R = 1; R < Rings.Num(); ++R)
                G.Sections[Water].Face({Point(Rings[R-1], I), Point(Rings[R], I),
                    Point(Rings[R], I+1), Point(Rings[R-1], I+1)}, FVector::UpVector);
        }
        return G;
    }

    void Blade(FSection& Mesh, const FVector& A, const FVector& B, const FVector& Cross)
    {
        const FVector N = FVector::CrossProduct(Cross, B - A).GetSafeNormal();
        Mesh.Face({A - Cross, A + Cross, B}, N);
        Mesh.Face({A - Cross, A + Cross, B}, -N);
    }

    void Garden(FNorthSceneryGeometry& Beds, FNorthSceneryGeometry& Plants)
    {
        struct FBed { FVector Centre; FVector2D Radius; int32 Flowers; };
        const FBed Islands[] = {
            {{5605, -865, 3400}, {222, 125}, 9},
            {{6280, -951, 3400}, {294, 149}, 14},
            {{7030, -920, 3400}, {244, 137}, 11},
            {{6010, 946, 3400}, {244, 130}, 12},
            {{6800, 942, 3400}, {318, 156}, 15},
        };
        FRandomStream Random(63103);
        for (const FBed& Bed : Islands)
        {
            FVector Edge[12];
            for (int32 I = 0; I < UE_ARRAY_COUNT(Edge); ++I)
            {
                const double A = I * 2 * PI / UE_ARRAY_COUNT(Edge);
                const double Irregular = Random.FRandRange(.91f, 1.06f);
                Edge[I] = Bed.Centre + FVector(Bed.Radius.X * FMath::Cos(A) * Irregular,
                    Bed.Radius.Y * FMath::Sin(A) * Irregular, 5);
            }
            for (int32 I = 0; I < UE_ARRAY_COUNT(Edge); ++I)
            {
                const FVector A = Edge[I], B = Edge[(I + 1) % UE_ARRAY_COUNT(Edge)];
                Beds.Sections[Earth].Face({A, B, Bed.Centre + FVector(0, 0, 15)}, FVector::UpVector);
                if (I % 4 != 2) Beds.Beam(Dressed, A, B, FVector::UpVector, 19, Random.FRandRange(10, 20));
            }
            for (int32 I = 0; I < 54; ++I)
            {
                const double A = Random.FRandRange(0, 2 * PI), R = Random.FRandRange(.12f, .85f);
                const FVector Start = Bed.Centre + FVector(Bed.Radius.X * FMath::Cos(A) * R,
                    Bed.Radius.Y * FMath::Sin(A) * R, 14.);
                const bool bFlower = I < Bed.Flowers;
                const double Height = Random.FRandRange(bFlower ? 27 : 14, bFlower ? 47 : 38);
                const FVector Tip = Start + FVector(10 * FMath::Cos(A), 10 * FMath::Sin(A), Height);
                const FVector U(FMath::Cos(A), FMath::Sin(A), 0);
                Blade(Plants.Sections[Leaves], Start, Tip, U * (bFlower ? 1.1 : Random.FRandRange(2.2f, 4.9f)));
                if (!bFlower) continue;
                Blade(Plants.Sections[Leaves], FMath::Lerp(Start, Tip, .43), Start - U * 11 + FVector(0, 0, Height * .60),
                    FVector(-U.Y, U.X, .15) * 3.2);
                auto& Petals = Plants.Sections[I % 5 == 0 ? Amber : Cream];
                for (int32 J = 0; J < 5; ++J)
                {
                    const double Angle = A + J * 2 * PI / 5;
                    const FVector Dir(FMath::Cos(Angle), FMath::Sin(Angle), 0);
                    const FVector V(-Dir.Y * 2.65, Dir.X * 2.65, 0);
                    Petals.Face({Tip, Tip + Dir * 4 + V, Tip + Dir * 7.4 + FVector(0, 0, 1.9), Tip + Dir * 4 - V}, FVector::UpVector);
                }
            }
        }
    }

    void OptionalLore(FNorthSceneryGeometry& G)
    {
        // Each fixture is shallow enough to sit in the side margin of its room.
        if (const auto* L = FindLandmark(TEXT("RefugeLedger")))
        {
            const FVector P = L->Position;
            G.Box(Oak, P + FVector(0, 12, 48), FVector(100, 48, 92));
            G.Box(Dressed, P + FVector(0, 7, 5), FVector(108, 62, 11));
            G.Box(Oak, P + FVector(0, 7, 95), FVector(112, 64, 8));
            for (double Side : {-1., 1.})
            {
                G.Box(Oak, P + FVector(Side * 40, -14, 50), FVector(12, 11, 79));
                G.Box(Oak, P + FVector(Side * 16, -1, 102), FVector(28, 43, 4));
                G.Box(Paper, P + FVector(Side * 15, -1, 105), FVector(26, 40, 2));
                for (int32 I = 0; I < 5; ++I)
                    G.Box(Oak, P + FVector(Side * 15, -15 + 6 * I, 106.05), FVector(18 - (I % 3) * 2., 1, .12));
            }
        }
        if (const auto* L = FindLandmark(TEXT("SealPrototype")))
        {
            const FVector P = L->Position;
            G.Box(Oak, P + FVector(0, 0, 84), FVector(74, 57, 7));
            for (double X : {-27., 27.}) for (double Y : {-18., 18.})
                G.Box(Oak, P + FVector(X, Y, 41), FVector(7, 7, 82));
            G.Box(Oak, P + FVector(-8, -3, 88.4), FVector(50, 40, 1.5));
            G.Box(Paper, P + FVector(-8, -3, 89.6), FVector(43, 33, .7));
            G.Box(Bronze, P + FVector(-8, 14, 90.5), FVector(18, 3, .9));
            for (int32 I = 0; I < 6; ++I)
                G.Box(Recess, P + FVector(-11, 8 - I * 4.6, 90.06), FVector(23 + (I % 3) * 3., .45, .12));
            G.Beam(Bronze, P + FVector(25, -16, 89), P + FVector(25, 7, 89), FVector::UpVector, 1.2, 1.2);
        }
        if (const auto* L = FindLandmark(TEXT("ReleaseOrder")))
        {
            const FVector P = L->Position;
            const FVector DeskFace = FVector(0, 30, 62).GetSafeNormal();
            G.Box(Dressed, P + FVector(0, -22, 47), FVector(91, 88, 93));
            G.Box(Oak, P + FVector(0, -21, 96), FVector(109, 98, 13));
            G.Beam(Oak, P + FVector(0, 14, 104), P + FVector(0, -48, 134), DeskFace, 80, 9);
            const FVector PaperOffset = DeskFace * 5.6;
            G.Beam(Paper, P + FVector(0, 7, 107.4) + PaperOffset, P + FVector(0, -40, 130.1) + PaperOffset, DeskFace, 62, 2);
            for (int32 I = 0; I < 7; ++I)
            {
                const FVector Row = P + FMath::Lerp(FVector(0, -40, 130.1), FVector(0, 7, 107.4), (I + 1.) / 9.) +
                    PaperOffset + DeskFace * 1.1;
                G.Beam(Recess, Row + FVector(-23, 0, 0), Row + FVector(5 - (I % 3) * 2., 0, 0), DeskFace, .55, .12);
                G.Beam(Recess, Row + FVector(15, 0, 0), Row + FVector(24, 0, 0), DeskFace, .55, .12);
            }
            for (double X : {-37., 37.})
                G.Beam(Bronze, P + FVector(X, 18, 107), P + FVector(X, -50, 139), DeskFace, 5, 5);
        }
    }

    void Install(AActor* Owner, USceneComponent* Parent, const FNorthSceneryGeometry& G,
        UMaterialInterface* const (&Materials)[SurfaceCount], const TCHAR* Tag, bool bShadow, float Cull)
    {
        if (!G.TriangleCount()) return;
        auto* Mesh = NewObject<UProceduralMeshComponent>(Owner);
        Mesh->SetupAttachment(Parent);
        Mesh->SetMobility(EComponentMobility::Static);
        Mesh->SetCanEverAffectNavigation(false);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetCastShadow(bShadow);
        Mesh->SetAffectDistanceFieldLighting(false);
        Mesh->SetAffectDynamicIndirectLighting(false);
        Mesh->SetCullDistance(Cull);
        Mesh->ComponentTags.Add(Tag);
        if(FName(Tag)==TEXT("NorthOcean"))Mesh->SetBoundsScale(1.02f);
        int32 Slot = 0;
        for (int32 I = 0; I < SurfaceCount; ++I)
        {
            const FSection& Section = G.Sections[I];
            if (Section.Indices.IsEmpty()) continue;
            Mesh->CreateMeshSection(Slot, Section.Vertices, Section.Indices, Section.Normals, Section.UVs,
                TArray<FColor>(), Section.Tangents, false);
            Mesh->SetMaterial(Slot, Materials[I]);
            ++Slot;
        }
        Owner->AddInstanceComponent(Mesh);
        Mesh->RegisterComponent();
        UE_LOG(LogTemp, Display, TEXT("North scenery %s: %d material sections, %d triangles"), Tag, Slot, G.TriangleCount());
    }
}

void Build(AActor* Owner, USceneComponent* Parent)
{
    if (!Owner || !Parent) return;
    UMaterialInterface* Wall = DuelGeometry::ArtMaterial(TEXT("M_NorthWall"));
    UMaterialInterface* Trim = DuelGeometry::ArtMaterial(TEXT("M_NorthDressedStone"));
    if (!Wall || !Trim)
    {
        UE_LOG(LogTemp, Error, TEXT("North scenery needs the existing North wall and dressed-stone materials"));
        return;
    }
    UMaterialInstanceDynamic* Roof = UMaterialInstanceDynamic::Create(DuelGeometry::ArtMaterial(TEXT("M_SeaWardLeadSlate")), Owner);
    Roof->SetVectorParameterValue(TEXT("StoneTint"), FLinearColor(.33f, .45f, .63f));
    Roof->SetScalarParameterValue(TEXT("WorldTileCm"), 82.f);
    UMaterialInterface* Rampart = DuelGeometry::ArtMaterial(TEXT("M_SeaWardAshlar"));
    if (!Rampart) Rampart = Wall;
    UMaterialInterface* Cliff = DuelGeometry::ArtMaterial(TEXT("M_SeaWardBedrock"));
    UMaterialInterface* Ocean = DuelGeometry::ArtMaterial(TEXT("M_NorthLivingSea"));
    if (!Cliff || !Ocean)
        UE_LOG(LogTemp, Warning, TEXT("North coast materials incomplete: prepare the dedicated North coastal assets"));
    const auto Colour = [Owner](float R, float G, float B, float Roughness = .81f)
    { return DuelGeometry::ColorMaterial(Owner, FLinearColor(R, G, B), false, Roughness); };
    UMaterialInterface* Materials[SurfaceCount] = {
        Rampart, DuelGeometry::ArtMaterial(TEXT("M_SeaWardCarving")), Roof, Colour(.022f, .026f, .031f), Colour(.10f, .073f, .039f),
        Colour(.18f, .29f, .070f), Colour(.86f, .78f, .56f), Colour(.94f, .55f, .14f),
        Colour(.12f, .065f, .030f), Colour(.70f, .59f, .40f), Colour(.30f, .18f, .064f, .59f), Colour(.25f, .028f, .029f),
        Cliff ? Cliff : Rampart, Ocean ? Ocean : Colour(.025f, .08f, .12f),
        DuelGeometry::ColorMaterial(Owner,FLinearColor(.12,.34,.61),true),
        DuelGeometry::ColorMaterial(Owner,FLinearColor(.53,.13,.20),true)
    };
    Install(Owner, Parent, Exterior(), Materials, TEXT("NorthExteriorMasonry"), true, 0);
    Install(Owner, Parent, CliffBackfill(), Materials, TEXT("NorthCliffBackfill"), true, 0);
    Install(Owner, Parent, DistantOcean(), Materials, TEXT("NorthOcean"), false, 0);
    Install(Owner, Parent, RescueStationIsland(), Materials, TEXT("NorthRescueStationIsland"), true, 65000);
    RescueStationAndWeather(Owner, Parent);
    FNorthSceneryGeometry Beds, Plants, Lore;
    Garden(Beds, Plants);
    OptionalLore(Lore);
    Install(Owner, Parent, Beds, Materials, TEXT("NorthGardenBeds"), true, 7200);
    Install(Owner, Parent, Plants, Materials, TEXT("NorthGardenPlants"), false, 7200);
    Install(Owner, Parent, Lore, Materials, TEXT("NorthOptionalLore"), true, 0);
    DuelNorthDressing::Build(Owner, Parent);
}
}

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    using namespace DuelNorthScenery;

    bool SceneryOverlaps(const FBox& A, const FBox& B)
    {
        for (int32 Axis = 0; Axis < 3; ++Axis)
            if (A.Max[Axis] <= B.Min[Axis] + .01 || A.Min[Axis] >= B.Max[Axis] - .01)
                return false;
        return true;
    }

    bool SceneryBlocksView(const FNorthSceneryGeometry& Geometry, const FVector& Start, const FVector& End)
    {
        for (const FSection& Section : Geometry.Sections)
            for (int32 I = 0; I < Section.Indices.Num(); I += 3)
            {
                FVector Hit, Normal;
                if (FMath::SegmentTriangleIntersection(Start, End, Section.Vertices[Section.Indices[I]],
                    Section.Vertices[Section.Indices[I + 1]], Section.Vertices[Section.Indices[I + 2]], Hit, Normal)) return true;
            }
        return false;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthSceneryTest, "ManosabaDuel.NorthTower.ExteriorScenery",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDuelNorthSceneryTest::RunTest(const FString& Parameters)
{
    using namespace DuelNorthLayout;
    using namespace DuelNorthScenery;
    const FNorthSceneryGeometry Building = Exterior();
    const FNorthSceneryGeometry RockMass = CliffBackfill();
    // Procedural faces duplicate vertices for stone normals. Weld positions
    // before counting, otherwise every hard crease looks like an open edge.
    const FSection& Skin = RockMass.Sections[Rock];
    TMap<FVector,int32> VertexIds;
    TMap<FIntPoint,int32> EdgeUses;
    for (int32 I=0;I<Skin.Indices.Num();I+=3)
    {
        int32 Corner[3];
        for(int32 J=0;J<3;++J)
        {
            const FVector& P=Skin.Vertices[Skin.Indices[I+J]];
            const int32 Next=VertexIds.Num();
            Corner[J]=VertexIds.FindOrAdd(P,Next);
        }
        for(int32 J=0;J<3;++J)
        {
            const int32 A=Corner[J], B=Corner[(J+1)%3];
            ++EdgeUses.FindOrAdd(FIntPoint(FMath::Min(A,B),FMath::Max(A,B)));
        }
    }
    int32 OpenEdges=0;
    for(const auto& Edge : EdgeUses) if(Edge.Value%2) ++OpenEdges;
    TestEqual(TEXT("The entire sea-cliff backing has no exposed mesh edges"), OpenEdges,0);
    for (const FNorthSceneryGeometry* Outside : {&Building, &RockMass}) for (const FBox& Box : Outside->Solids)
    {
        for (const FRoom& Room : Rooms())
            if (Room.Kind != ERoomKind::Garden && SceneryOverlaps(Box, Room.Bounds))
                AddError(FString::Printf(TEXT("Exterior stone crosses clear room %s: %s"), *Room.Id.ToString(), *Box.ToString()));
        for (const FPortal& P : Portals())
        {
            const FVector Along(-P.Normal.Y, P.Normal.X, 0);
            const FVector Extent = Along.GetAbs() * (P.Width * .5) + P.Normal.GetAbs() * 100 + FVector(0, 0, P.Height * .5);
            const FVector Centre = P.Center + FVector(0, 0, P.Height * .5);
            if (SceneryOverlaps(Box, FBox(Centre - Extent, Centre + Extent)))
                AddError(FString::Printf(TEXT("Exterior stone enters opening %s"), *P.Id.ToString()));
        }
    }

    for (double X : {-1400., 0., 1400.})
    {
        const FVector View(X, -2200, 1000);
        const FVector Shore(X / 1400 * 4000, X == 0 ? -4550 : -5000, X == 0 ? -650 : -800);
        TestFalse(TEXT("The original sea-window sightline still reaches its lower shore sector"), SceneryBlocksView(Building, View, Shore));
    }
    for (const FPortal& P : Portals())
    {
        if (!P.bWindow) continue;
        const FRoom* Room = FindRoom(P.RoomA);
        if (!TestNotNull(TEXT("Exterior window belongs to a room"), Room)) continue;
        const FVector Mid = P.Center + FVector(0, 0, P.Height * .5);
        constexpr double Distance = 4800.;
        const FVector ToSky = WindowOutward(P, *Room) * Distance + FVector(0, 0, Distance * ExteriorSightlineSlope);
        TestFalse(*FString::Printf(TEXT("Exterior dressing leaves daylight visible from %s"), *P.Id.ToString()),
            SceneryBlocksView(Building, Mid, Mid + ToSky) || SceneryBlocksView(RockMass, Mid, Mid + ToSky));
    }
    FNorthSceneryGeometry Beds, Plants, Lore;
    Garden(Beds, Plants);
    OptionalLore(Lore);
    for (const FNorthSceneryGeometry* G : {&Beds, &Plants}) for (const FSection& Section : G->Sections)
        for (const FVector& V : Section.Vertices)
            if (V.X < 5270 || V.X > 7530 || FMath::Abs(V.Y) < 690 || FMath::Abs(V.Y) > 1170 || V.Z > 3470)
                AddError(FString::Printf(TEXT("Garden border dressing enters the walk or guard: %s"), *V.ToString()));

    ADuelNorthTower* Tower = nullptr;
    if (GEngine) for (const auto& Context : GEngine->GetWorldContexts()) if (Context.World() && Context.World()->IsGameWorld())
        for (TActorIterator<ADuelNorthTower> It(Context.World()); It; ++It) { Tower = *It; break; }
    if (!TestTrue(TEXT("An isolated North Tower is running"), Tower && FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave"))))
        return false;
    TInlineComponentArray<USkyAtmosphereComponent*> Atmospheres(Tower);
    if (TestEqual(TEXT("North Tower owns one native full-view sky"), Atmospheres.Num(), 1))
    {
        const USkyAtmosphereComponent* Sky = Atmospheres[0];
        TestTrue(TEXT("Sea and sky share the planet radius and sea level"),
            Sky->TransformMode == ESkyAtmosphereTransformMode::PlanetTopAtComponentTransform
            && Sky->BottomRadius == PlanetRadiusKm && Sky->GetRelativeLocation().Z == SeaLevelCm);
        TestTrue(TEXT("Aerial perspective starts beyond the indoor sightline"), Sky->AerialPerspectiveStartDepth >= .18f);
    }
    const FName Tags[] = {TEXT("NorthExteriorMasonry"), TEXT("NorthGardenBeds"), TEXT("NorthGardenPlants"),
        TEXT("NorthCliffBackfill"), TEXT("NorthOptionalLore"), TEXT("NorthOcean")};
    const int32 Expected[] = {Building.TriangleCount(), Beds.TriangleCount(), Plants.TriangleCount(),
        RockMass.TriangleCount(), Lore.TriangleCount(), DistantOcean().TriangleCount()};
    TInlineComponentArray<UProceduralMeshComponent*> Components(Tower);
    int32 Total = 0, Found = 0;
    for (auto* Component : Components)
        for (int32 I = 0; I < UE_ARRAY_COUNT(Tags); ++I) if (Component->ComponentHasTag(Tags[I]))
        {
            int32 Triangles = 0;
            ++Found;
            TestTrue(TEXT("Dressing adds no gameplay or camera collision"), Component->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
            TestEqual(TEXT("Stone and grounded fixtures cast real shadows"), bool(Component->CastShadow), I != 2 && I != 5);
            for (int32 S = 0; S < Component->GetNumSections(); ++S)
            {
                const FProcMeshSection* Section = Component->GetProcMeshSection(S);
                if (TestNotNull(TEXT("Installed procedural section"), Section))
                    Triangles += Section->ProcIndexBuffer.Num() / 3;
                TestNotNull(TEXT("Installed section material"), Component->GetMaterial(S));
            }
            TestEqual(TEXT("All authored dressing triangles are installed"), Triangles, Expected[I]);
            Total += Triangles;
        }
    TestEqual(TEXT("Bounded scenery batches"), Found, Expected[4] > 0 ? 6 : 5);
    TestTrue(TEXT("The complete skyline, watertight land and sea fit the 72000-triangle budget"), Total > 0 && Total <= 72000);
    AddInfo(FString::Printf(TEXT("North exterior and scenery: %d batches, %d triangles, no new collision or lights"), Found, Total));
    return !HasAnyErrors();
}
#endif
