#include "DuelNorthLighting.h"
#include "DuelNorthArchive.h"
#include "DuelNorthLayout.h"
#include "DuelNorthRose.h"
#include "DuelNorthScenery.h"

#include "DuelNorthNarrative.h"
#include "DuelNorthTower.h"
#include "DuelGame.h"
#include "DuelGeometry.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "Components/CapsuleComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/LocalFogVolumeComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureCube.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "DuelGame.h"
#include "DuelNorthInteractables.h"
#include "DuelNorthTower.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#endif

namespace DuelNorthLighting
{
namespace
{
    using namespace DuelNorthLayout;
    constexpr float DungeonExposureEV=1.04f;
    constexpr float InteriorExposureEV=1.18f;
    constexpr float FinalExposureEV=1.55f;
    constexpr float GardenExposureEV=3.86f;
    constexpr double GuideHeight=136.;
    constexpr float GuideRadius=12.f;
    constexpr double GuideSpeed=DuelCourt::PlayerSprintSpeed;
    constexpr double GuideLead=630.;
    constexpr double GuideAcceleration=900.;
    constexpr double GuideBraking=1450.;
    const FLinearColor GuideYellow(1.f,.62f,.145f);
    const FLinearColor NorthBlueFill(.60f,.78f,1.f);

    struct FNode
    {
        FName Room;
        FVector Position,Target;
        float Candela=180,Radius=900,Temperature=3200;
        bool bSpot=false,bVolumetric=false,bFixture=true;
        bool bGuide=false,bWindow=false;
        FVector FixtureAnchor=FVector::ZeroVector,FixtureInward=FVector::ZeroVector;
        bool bCeilingFixture=false;
        float IndirectScale=1.f;
        FLinearColor LightTint=FLinearColor::White;
        bool bDiffuseFill=false;
        bool bPracticalCone=false;
    };
    struct FSlot
    {
        TWeakObjectPtr<UPointLightComponent> Light;
        int32 Node=INDEX_NONE;
        bool bSpot=false;
    };
    struct FFog
    {
        FName Room;
        TWeakObjectPtr<ULocalFogVolumeComponent> Volume;
    };
    struct FTorchFlame
    {
        TWeakObjectPtr<UParticleSystemComponent> Particles;
        int32 Node = INDEX_NONE;
    };
    struct FState
    {
        TArray<FNode> Nodes;
        TArray<FSlot> Slots;
        TArray<FFog> Fog;
        TArray<FTorchFlame> TorchFlames;
        TArray<int32> WallTorchNodes;
        TWeakObjectPtr<UPostProcessComponent> Grade;
        TWeakObjectPtr<UExponentialHeightFogComponent> Mist;
        TWeakObjectPtr<UDirectionalLightComponent> Sun;
        TWeakObjectPtr<USkyLightComponent> Sky;
        FLightingStats Stats;
        float SinceSelection=100,EV=InteriorExposureEV,Time=0,VictorySeconds=0,GuideBlend=0;
        EDuelOutcome PreviousOutcome=EDuelOutcome::Ready;
        TArray<FVector> GuidePath;
        int32 NextGuidePoint=0;
        double GuideVelocity=0.;
        TOptional<FVector> GuidePosition, GuideDisplayPosition, LastPlayerPosition;
        FVector GuideRight=FVector::ZeroVector;
        FVector GuideGoal=FVector::ZeroVector;
        TWeakObjectPtr<UPointLightComponent> GuideLight;
        TWeakObjectPtr<UStaticMeshComponent> GuideFlame;
        TWeakObjectPtr<UMaterialInstanceDynamic> GuideMaterial;
    };
    TMap<TWeakObjectPtr<AActor>,FState> States;

    template<class T> T* Attach(AActor* Owner,USceneComponent* Parent)
    {
        auto* C=NewObject<T>(Owner);C->SetupAttachment(Parent);
        C->SetMobility(EComponentMobility::Movable);
        Owner->AddInstanceComponent(C);return C;
    }

    void MountFixtures(FState& State)
    {
        for(FNode& N:State.Nodes)
        {
            if(!N.bFixture)continue;
            const FRoom* Room=FindRoom(N.Room);if(!Room)continue;
            if(N.bCeilingFixture)
            {
                // The two Mirror Court lamps are at |Y|=1250. The installed WebCourt
                // intrados is 850.247 cm above the floor there (55 segments,
                // Tudor 1600/950/70, 42 cm offset in prepare_north_gothic.py).
                // The socket enters that real vaulted stone by less than 1 cm.
                N.FixtureAnchor=FVector(N.Position.X,N.Position.Y,Room->Bounds.Min.Z+851);
                continue;
            }
            const FRoom* Enclosure=Room;
            if(!Enclosure->bEnclose)
                for(const FStairFlight& S:Stairs())
                    if(S.FromLanding==N.Room || S.ToLanding==N.Room)
                    {Enclosure=FindRoom(S.CoreRoom);break;}
            if(!Enclosure || !Enclosure->bEnclose)continue;
            float BestScore=TNumericLimits<float>::Max();FVector BestAt=N.Position,BestAnchor=FVector::ZeroVector,BestInward=FVector::ZeroVector;
            for(int32 Axis=0;Axis<2;++Axis)for(bool bMax:{false,true})
            {
                const int32 Along=1-Axis;
                const float Plane=bMax?Enclosure->Bounds.Max[Axis]:Enclosure->Bounds.Min[Axis];
                FVector Inward=FVector::ZeroVector;Inward[Axis]=bMax?-1:1;
                for(float Offset:{0.f,100.f,-100.f,220.f,-220.f,400.f,-400.f})
                {
                    FVector At=N.Position;At[Axis]=Plane+Inward[Axis]*65;
                    At[Along]=FMath::Clamp(float(At[Along]+Offset),float(Room->Bounds.Min[Along]+55),float(Room->Bounds.Max[Along]-55));
                    // Match Construction's global wall-plane cutouts, including
                    // windows and portals owned by the stair core rather than landing.
                    const bool Hole=Portals().ContainsByPredicate([&](const FPortal& P)
                    {
                        return FMath::Abs(P.Normal[Axis])>.9 && FMath::Abs(P.Center[Axis]-Plane)<1 &&
                            FMath::Abs(At[Along]-P.Center[Along])<P.Width*.5f+50 &&
                            At.Z+60>P.Center.Z && At.Z-34<P.Center.Z+P.Height;
                    });
                    if(Hole)continue;
                    const float Score=FVector::DistSquared(At,N.Position);
                    if(Score>=BestScore)continue;
                    BestScore=Score;BestAt=At;BestInward=Inward;
                    BestAnchor=At;BestAnchor[Axis]=Plane+Inward[Axis]*12; // Actual 24 cm shell's interior face.
                }
            }
            if(BestScore<TNumericLimits<float>::Max())
            {
                N.Position=BestAt;N.FixtureAnchor=BestAnchor;N.FixtureInward=BestInward;
                if(N.bPracticalCone && N.Target.IsNearlyZero())
                    N.Target=BestAt + BestInward * 365. - FVector(0,0,240);
            }
            else UE_LOG(LogTemp,Error,TEXT("No solid North fixture mount in %s"),*N.Room.ToString());
        }
    }

    bool UsesWallTorch(const FNode& Node)
    {
        const FRoom* Room=FindRoom(Node.Room);
        return Room && Node.bFixture && !Node.bCeilingFixture && !Node.FixtureInward.IsNearlyZero() &&
            Room->Floor!=EFloor::Garden && Room->Id!=TEXT("ChapelHall") &&
            Room->Id!=TEXT("FinalCourt") && Room->Id!=TEXT("FinalExit");
    }

    void AuthorStairFixtures(FState& State)
    {
        struct FMount { const TCHAR* Flight; float Along,Height,Candela,Radius; };
        // Alternate walls across the stacked passages. The nearest unscreened
        // source can serve two runs where the tread elevations overlap.
        const FMount Mounts[] = {
            {TEXT("DescentFlight0"),.25f,230,320,930},
            {TEXT("DescentFlight1"),.50f,390,320,930},
            {TEXT("DescentFlight3"),.25f,230,320,930},
            {TEXT("DescentFlight4"),.50f,465,320,930},
            {TEXT("ReturnFlight0"),.25f,230,320,930},
            {TEXT("ReturnFlight1"),.50f,390,320,930},
            {TEXT("ReturnFlight3"),.25f,230,320,930},
            {TEXT("ReturnFlight4"),.50f,465,320,930},
            {TEXT("ChapelStairFlight0"),.50f,235,320,900},
            {TEXT("ChapelStairFlight1"),.50f,235,320,900},
            {TEXT("MirrorStairFlight0"),.25f,230,320,930},
            {TEXT("MirrorStairFlight1"),.25f,230,320,930},
            {TEXT("MirrorStairFlight2"),.25f,465,320,930},
            {TEXT("FinalStairFlight0"),.50f,171,420,970},
            {TEXT("FinalStairFlight1"),.50f,340.5f,420,970},
            {TEXT("FinalStairFlight2"),.50f,340.5f,420,970},
        };
        for(const FMount& Mount:Mounts)
        {
            const FStairFlight* Flight=Stairs().FindByPredicate([&](const FStairFlight& S){return S.Id==Mount.Flight;});
            const FRoom* Core=Flight?FindRoom(Flight->CoreRoom):nullptr;
            if(!Core)
            {
                UE_LOG(LogTemp,Error,TEXT("North stair lantern has no enclosure for %s"),Mount.Flight);
                continue;
            }
            FVector At=FMath::Lerp(Flight->Start,Flight->End,Mount.Along);
            At.Y=At.Y<Core->Bounds.GetCenter().Y?Core->Bounds.Min.Y+65:Core->Bounds.Max.Y-65;
            At.Z+=Mount.Height;
            State.Nodes.Add({Core->Id,At,FVector::ZeroVector,Mount.Candela,Mount.Radius,
                Core->Floor==EFloor::Dungeon?3200.f:3500.f});
        }

        // The 10.2 m upper runs leave the lower west turn behind stair soffits.
        const FRoom* Turn=FindRoom(TEXT("FinalStairLanding1"));
        const FRoom* Core=FindRoom(TEXT("FinalStairCore"));
        if(Turn && Core)
            State.Nodes.Add({Core->Id,FVector(Core->Bounds.Min.X+65,Turn->Bounds.GetCenter().Y,Turn->Bounds.Min.Z+210),
                FVector::ZeroVector,250,650,3500});
    }

    void AuthorNodes(FState& State)
    {
        for (const FRoom& R:Rooms())
        {
            if (!R.bFloor || R.Kind==ERoomKind::Landing || R.Kind==ERoomKind::Garden || R.Floor==EFloor::Garden) continue;
            const FVector Centre=R.Bounds.GetCenter(),Size=R.Bounds.GetSize();
            const float Z=R.Bounds.Min.Z;
            if (R.Kind==ERoomKind::Combat || R.Kind==ERoomKind::Hall)
            {
                const bool bDungeonCourt=R.Id==TEXT("DungeonCourt");
                const bool bFinalCourt=R.Id==TEXT("FinalCourt");
                const bool bMirrorCourt=R.Id==TEXT("MirrorCourt");
                const bool bPrisonHall=R.Id==TEXT("PrisonHall");
                const bool bChapel=R.Id==TEXT("ChapelHall");
                if(bChapel)
                {
                    // The stack faces look into the aisles. Bounce from high in each aisle so the next row does not shade them.
                    for (double X : {-1., 1.}) for (double Y : {-1., 1.})
                    {
                        FNode Bounce{R.Id, FVector(X * 1120., Y * 850., Z + 495.), FVector::ZeroVector,
                            570.f, 1730.f, 6450.f, false, false, false};
                        Bounce.LightTint = FLinearColor(.83f, .90f, 1.f);
                        Bounce.IndirectScale = 1.02f;
                        Bounce.bDiffuseFill = true;
                        State.Nodes.Add(Bounce);

                        // Glass-sided lanterns illuminate the masonry around their brackets as well as the aisle.
                        FNode Lamp{R.Id, FVector(X * 1955., Y * 1635., Z + 335.),
                            FVector::ZeroVector, 292.f, 1390.f, 3350.f, false, false, true};
                        Lamp.IndirectScale = .9f;
                        State.Nodes.Add(Lamp);
                    }
                    FNode Aisle{R.Id, FVector(0, 25, Z + 505.), FVector::ZeroVector,
                        395.f, 1880.f, 6500.f, false, false, false};
                    Aisle.LightTint = FLinearColor(.88f, .94f, 1.f);
                    Aisle.IndirectScale = .94f;
                    Aisle.bDiffuseFill = true;
                    State.Nodes.Add(Aisle);

                    // The stone between the rose windows needs diffuse light on its face.
                    FNode WindowWall{R.Id, FVector(0, 1135, Z + 405), FVector(0, 1670, Z + 335),
                        172.f, 1120.f, 6300.f, true, false, false};
                    WindowWall.LightTint = FLinearColor(.87f, .91f, 1.f);
                    WindowWall.IndirectScale = .90f;
                    WindowWall.bDiffuseFill = true;
                    State.Nodes.Add(WindowWall);
                }
                else
                {
                    // Broad blue bounce keeps the centre and landing readable between the practicals.
                    const FVector2D CourtPockets[] = {{-.31,-.15},{0,.29},{.31,-.12}};
                    const FVector2D LivingPockets[] = {{-.335,-.26},{-.20,.35},{.338,-.275}};
                    for(const FVector2D& Pocket : bPrisonHall ? MakeArrayView(LivingPockets) : MakeArrayView(CourtPockets))
                    {
                        FNode Fill{R.Id,FVector(Centre.X + Pocket.X * Size.X, Centre.Y + Pocket.Y * Size.Y,
                            Z + FMath::Min(590.,Size.Z * .72)),FVector::ZeroVector,
                            bFinalCourt ? 680.f : bPrisonHall ? 450.f : 600.f,bPrisonHall ? 1920.f : 2230.f,
                            bPrisonHall ? 6400.f : 7650.f,false,false,false};
                        Fill.LightTint=bPrisonHall ? FLinearColor(.82f,.87f,1.f) : NorthBlueFill;
                        Fill.IndirectScale=1.17f;
                        Fill.bDiffuseFill=true;
                        State.Nodes.Add(Fill);
                    }
                }

                if (!bChapel)
                    for(double Side : {-1.,1.})
                    {
                        const double Along = Size.X * (bDungeonCourt ? .27 : .37);
                        const double PerimeterY=Centre.Y+Side*(Size.Y*.5-65);
                        FVector Aim=FVector::ZeroVector;
                        const bool bJudgmentDais=bFinalCourt && Side<0.;
                        const bool bMirrorDais=bMirrorCourt && Side>0.;
                        if(bJudgmentDais) Aim=FVector(-2075,-1565,Z+105);
                        if(bMirrorDais) Aim=FVector(1410,1565,Z+119);
                        const float Power=bJudgmentDais || bMirrorDais ? 163.f : bFinalCourt ? 246.f : 209.f;
                        FNode Practical{R.Id,FVector(Centre.X+Side*Along,PerimeterY,Z+280),
                            Aim,Power,1350.f,bDungeonCourt ? 3050.f : 3430.f,true,false,true};
                        Practical.bPracticalCone=true;
                        Practical.IndirectScale=.94f;
                        State.Nodes.Add(Practical);
                    }
                if(bFinalCourt)
                    for(float Side : {-1.f,1.f})
                        State.Nodes.Add({R.Id,FVector(Side*1680,1635,Z+330),
                            FVector(Side*2270,0,Z+1520),2900,3450,5300,true,false,false});
                if(R.Kind==ERoomKind::Combat && !bFinalCourt)
                    for(float Side:{-1.f,1.f})
                    {
                        // The opposing lamps share the floor wash. The east
                        // lamp in B1 also reaches the unlit vault above it.
                        const FVector Target=bDungeonCourt && Side>0 ? FVector(Centre.X-480,350,Z+1220) :
                            FVector(Centre.X-Side*1500,1050,Z+280);
                        FNode Accent{R.Id,FVector(Centre.X+Side*1750,-1250,Z+650),Target,
                            bDungeonCourt?1580.f:2250.f,3500,bDungeonCourt?8100.f:7200.f,true,false,!bDungeonCourt};
                        Accent.IndirectScale=bDungeonCourt?1.29f:1.16f;
                        Accent.bCeilingFixture=!bDungeonCourt;
                        Accent.LightTint=bDungeonCourt?FLinearColor(.63f,.77f,1.f):FLinearColor(.67f,.82f,1.f);
                        State.Nodes.Add(Accent);
                    }
                const FPortal* Exit=nullptr;
                for(const FPortal& P:Portals())
                    if(!P.bWindow && (P.RoomA==R.Id||P.RoomB==R.Id) && FMath::Abs(P.Normal.X)>.9 &&
                        (!Exit||(R.Id==TEXT("ChapelHall")?P.Center.X<Exit->Center.X:P.Center.X>Exit->Center.X)))Exit=&P;
                if(Exit)
                {
                    const float Sign=Exit->Center.X>Centre.X?-1.f:1.f;
                    FNode Threshold{R.Id,Exit->Center+FVector(Sign*90,Exit->Width*.5+65,248),
                        Exit->Center+FVector(Sign*145,0,76),190.f,1040.f,3380.f,true,false,true,true,false};
                    Threshold.bPracticalCone=true;
                    State.Nodes.Add(Threshold);
                }
                continue;
            }
            const int32 Axis=Size.X>Size.Y?0:1;
            const int32 Count=FMath::Max(1,FMath::CeilToInt(Size[Axis]/1700));
            const float Span=Size[Axis]/Count;
            const float Radius=FMath::Clamp(Span*.65f+340.f,820.f,1475.f);
            FRandomStream LayoutVariation(GetTypeHash(R.Id.ToString()));
            for(int32 I=0;I<Count;++I)
            {
                // Give the long passages different gaps and irregularly spaced pools.
                const bool bLong = Count >= 2 && (R.Kind==ERoomKind::Corridor || R.Kind==ERoomKind::Gallery);
                const float Offset = bLong ? LayoutVariation.FRandRange(.33f,.67f) : .5f;
                const bool bNearWall = bLong ? LayoutVariation.RandRange(0,2) != 0 : true;
                FVector At=Centre;
                At[Axis]=FMath::Lerp(R.Bounds.Min[Axis],R.Bounds.Max[Axis],(I+Offset)/Count);
                At[1-Axis]=bNearWall ? R.Bounds.Min[1-Axis]+65 : R.Bounds.Max[1-Axis]-65;
                At.Z=FMath::Min(Z+265.f+(bLong ? LayoutVariation.FRandRange(-18.f,24.f) : 0.f),R.Bounds.Max.Z-80.f);
                const bool bGallery=R.Id==TEXT("SeaGallery");
                const bool bDungeon=R.Floor==EFloor::Dungeon;
                State.Nodes.Add({R.Id,At,FVector::ZeroVector,bGallery?96.f:202.f,Radius,
                    bGallery?4650.f:bDungeon?3530.f:4050.f,false,false,true});
            }
            if (R.Id==TEXT("DungeonAfter") || R.Id==TEXT("MirrorAfter") || R.Id==TEXT("FinalExit"))
            {
                // An opposing, close-range pool lights the far wall and the next threshold.
                State.Nodes.Add({R.Id,FVector(R.Bounds.Max.X-165,R.Bounds.Max.Y-65,Z+260),
                    FVector::ZeroVector,182.f,640.f,3470.f});
            }
        }
        for(const FPortal& P:Portals())
        {
            if(!P.bWindow)continue;
            const bool bRose=P.RoomA==TEXT("FinalCourt") && DuelNorthRose::IsRound(P);
            const bool bSea=P.Id==TEXT("SeaWindowCentre");
            const bool bPrison=P.RoomA==TEXT("PrisonHall") && IsExteriorWindow(P);
            const bool bChapel=P.RoomA==TEXT("ChapelHall") && IsExteriorWindow(P);
            if(!bRose && !bSea && !bPrison && !bChapel)continue;
            if(bRose && ((P.Center.X<0)!=(P.Center.Y<0)))continue;
            const FRoom* R=FindRoom(P.RoomA);if(!R)continue;
            const FVector Inward=P.Normal*FMath::Sign(FVector::DotProduct(R->Bounds.GetCenter()-P.Center,P.Normal));
            const FVector At=P.Center-Inward*55+FVector(0,0,P.Height*.55f);
            const float Reach=bRose?2800.f:bChapel?1550.f:1200.f;
            const bool bVolume=bSea || ((bPrison || bRose) && P.Center.X<0);
            FNode Accent{R->Id,At,At+Inward*Reach-FVector(0,0,bRose?650:bPrison?360:bChapel?215:100),
                bRose?9800.f:bPrison?(P.Center.X<0?1500.f:980.f):bChapel?465.f:460.f,
                bRose?3900.f:bSea?1440.f:bChapel?1500.f:1800.f,
                6900,true,bVolume,false,false,true};
            if(bChapel)
                Accent.LightTint=P.Center.X<0?FLinearColor(.96f,.79f,.61f):FLinearColor(.48f,.71f,1.f);
            State.Nodes.Add(Accent);
        }
        AuthorStairFixtures(State);
        MountFixtures(State);
    }

    void ApplyNode(FSlot& Slot,const FNode& Node)
    {
        UPointLightComponent* L=Slot.Light.Get();if(!L)return;
        const bool bTorch=UsesWallTorch(Node);
        L->SetRelativeLocation(Node.Position);
        L->SetAttenuationRadius(bTorch && !Node.bPracticalCone ? Node.Radius * 1.14f : Node.Radius);
        L->SetUseTemperature(true);
        L->SetTemperature(Node.Temperature);
        if(Node.bFixture && Node.Radius < 1600.f)
        {
            L->SetMaxDrawDistance(Node.Radius + 2850.f);
            L->SetMaxDistanceFadeRange(600.f);
        }
        L->SetLightColor(Node.LightTint);
        // Increase Lumen bounce from the in-room fixtures, with their existing
        // shadowed bounds and the tower's skylight leakage disabled.
        L->SetIndirectLightingIntensity(Node.IndirectScale);
        L->SetSourceRadius(Node.bDiffuseFill ? 132.f : Node.bPracticalCone ? 23.f : 10.f);
        L->SetSoftSourceRadius(Node.bDiffuseFill ? 165.f : Node.bPracticalCone ? 40.f : 18.f);
        if(Node.bDiffuseFill) L->SetSpecularScale(.10f);
        else if ((!Node.bWindow && Node.Temperature < 4500.f) || Node.Room == TEXT("SeaGallery"))
            L->SetSpecularScale(.22f);
        // Point lights never write an unshadowed glow through adjoining floors into fog.
        L->SetVolumetricScatteringIntensity(Node.bVolumetric?.45f:0.f);
        L->SetCastVolumetricShadow(Node.bVolumetric);
        if(auto* Spot=Cast<USpotLightComponent>(L))
        {
            Spot->SetRelativeRotation((Node.Target-Node.Position).Rotation());
            Spot->SetInnerConeAngle(Node.bDiffuseFill ? 38.f : Node.bWindow ? 32.f : 24.f);
            Spot->SetOuterConeAngle(Node.bDiffuseFill ? 65.f : Node.bWindow ? 55.f : Node.bPracticalCone ? 46.f : 50.f);
        }
    }

    void BuildWallTorches(AActor* Owner,USceneComponent* Parent,const FState& State,
        UMaterialInterface* Flame,UMaterialInterface* Iron)
    {
        using namespace DuelGeometry;
        auto Batch=[&](EShape Shape,UMaterialInterface* Material,const TCHAR* Tag,UStaticMesh* AuthoredMesh=nullptr)
        {
            auto* MeshBatch=Attach<UInstancedStaticMeshComponent>(Owner,Parent);
            MeshBatch->SetMobility(EComponentMobility::Static);
            MeshBatch->SetStaticMesh(AuthoredMesh?AuthoredMesh:Mesh(Shape));
            if(Material)MeshBatch->SetMaterial(0,Material);
            MeshBatch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            MeshBatch->SetCanEverAffectNavigation(false);
            MeshBatch->bDisallowNanite=Shape==EShape::Plane;
            MeshBatch->SetVisibleInRayTracing(Shape!=EShape::Plane);
            MeshBatch->SetCullDistances(0,Shape==EShape::Plane?2800:4900);
            MeshBatch->SetCastShadow(false);
            MeshBatch->SetAffectDistanceFieldLighting(false);
            MeshBatch->SetAffectDynamicIndirectLighting(false);
            MeshBatch->ComponentTags.Add(Tag);
            MeshBatch->RegisterComponent();
            return MeshBatch;
        };
        auto* Braces=Batch(EShape::Tube,Iron,TEXT("NorthDungeonTorchIron"));
        auto* Plates=Batch(EShape::Box,Iron,TEXT("NorthDungeonTorchIron"));
        auto* Flames=Batch(EShape::Plane,Flame,TEXT("NorthDungeonTorchFlame"));
        UStaticMesh* Torch=LoadObject<UStaticMesh>(nullptr,
            TEXT("/Game/MedievalDungeon/Meshes/Props/SM_Torch.SM_Torch"));
        const FBox TorchBounds=Torch?Torch->GetBoundingBox():FBox(ForceInit);
        const FVector TorchSize=TorchBounds.IsValid?TorchBounds.GetSize():FVector::ZeroVector;
        const bool bUseAuthoredTorch=Torch && TorchSize.GetMin()>KINDA_SMALL_NUMBER;
        auto* Torches=bUseAuthoredTorch?Batch(EShape::Box,nullptr,TEXT("NorthMedievalDungeonTorch"),Torch):nullptr;
        auto Rod=[&](const FVector& A,const FVector& B,double Radius)
        {
            const FVector D=B-A;
            Braces->AddInstance(FTransform(FRotationMatrix::MakeFromZ(D).ToQuat(),
                (A+B)*.5,FVector(Radius/50.,Radius/50.,D.Size()/100.)));
        };
        const FVector Up=FVector::UpVector;
        for(const FNode& Node:State.Nodes)
        {
            if(!UsesWallTorch(Node))continue;
            const FVector At=Node.Position;
            const FVector In=Node.FixtureInward;
            const FVector Wall=Node.FixtureAnchor;
            const FVector Foot=At-Up*45.;
            const FVector Rim=At-Up*15.;
            const FVector Width=FVector::CrossProduct(Up,In);
            if(Torches)
            {
                // The authoring mesh's +Y faces the fire bowl. Its stone wall plate
                // sits at local Min.Y, with the bowl's lip near (0, 52.5, 39).
                constexpr double BowlY=52.5, BowlZ=39.;
                const FQuat Rotation=FRotator(0,In.Rotation().Yaw-90.,0).Quaternion();
                const double Scale=(At-Wall).Size2D()/(BowlY-TorchBounds.Min.Y);
                const FVector Seat(0,TorchBounds.Min.Y,BowlZ);
                const FVector Mount(Wall.X,Wall.Y,At.Z-16.);
                Torches->AddInstance(FTransform(Rotation,
                    Mount-In*1.5-Rotation.RotateVector(Seat*Scale),FVector(Scale)));
            }
            else
            {
                Plates->AddInstance(FTransform(In.Rotation(),Wall+In-Up*57.,FVector(.045,.23,.64)));
                Rod(Wall-Up*64.,Foot,2.5);
                Rod(Wall-Up*36.,Foot+Up*6.,2.0);
                Rod(Foot-Up*4.,Foot+Up*4.,8.);
                constexpr int32 Bars=8;
                for(int32 I=0;I<Bars;++I)
                {
                    const double A=I*(2.*PI/Bars),B=(I+1)*(2.*PI/Bars);
                    const FVector U=In*FMath::Cos(A)+Width*FMath::Sin(A);
                    const FVector V=In*FMath::Cos(B)+Width*FMath::Sin(B);
                    Rod(Foot+U*6.,Rim+U*15.,1.5);
                    Rod(Rim+U*15.,Rim+V*15.,1.8);
                }
            }
            for(const FVector& Across:{Width,In})
                Flames->AddInstance(FTransform(FRotationMatrix::MakeFromXY(Across,-Up).ToQuat(),
                    At+Up*9.,FVector(.40,.56,1.)));
        }
        if(Torches)UE_LOG(LogTemp,Display,TEXT("North wall lighting: %d Medieval Dungeon torch meshes"),
            Torches->GetInstanceCount());
    }

    void FinalSanctumDetails(AActor* Owner,USceneComponent* Parent)
    {
        const FRoom* Court=FindRoom(TEXT("FinalCourt"));if(!Court)return;
        auto Material=[](const TCHAR* Name)
        {return LoadObject<UMaterialInterface>(nullptr,*FString::Printf(TEXT("/Game/DuelArt/Materials/%s.%s"),Name,Name));};
        auto Batch=[&](const TCHAR* Asset,UMaterialInterface* Mat,const TCHAR* Tag,bool Shadow)
        {
            auto* Source=LoadObject<UStaticMesh>(nullptr,Asset);
            if(!Source||!Mat){UE_LOG(LogTemp,Error,TEXT("Missing Final Court detail %s"),Asset);return static_cast<UInstancedStaticMeshComponent*>(nullptr);}
            auto* C=Attach<UInstancedStaticMeshComponent>(Owner,Parent);C->SetMobility(EComponentMobility::Static);
            C->SetStaticMesh(Source);C->SetMaterial(0,Mat);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            C->SetCanEverAffectNavigation(false);C->SetCastShadow(Shadow);
            C->ComponentTags.Add(TEXT("NorthFinalSanctum"));C->ComponentTags.Add(Tag);C->RegisterComponent();return C;
        };
        auto* Moulding=Batch(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),Material(TEXT("M_NorthDressedStone")),TEXT("NorthFinalTracery"),true);
        auto* Support=Batch(TEXT("/Engine/BasicShapes/Cube.Cube"),Material(TEXT("M_NorthDressedStone")),TEXT("NorthFinalPlinth"),true);
        if(!Moulding||!Support)return;
        auto Rod=[&](const FVector& A,const FVector& B,float Radius)
        {
            const FVector D=B-A;
            Moulding->AddInstance(FTransform(FQuat::FindBetweenNormals(FVector::UpVector,D.GetSafeNormal()),
                (A+B)*.5,FVector(Radius/50,Radius/50,D.Size()/100)));
        };
        auto* Source=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/DuelArt/Scans/gothic_statue/gothic_statue.gothic_statue"));
        if(!Source){UE_LOG(LogTemp,Error,TEXT("Missing existing Gothic statue scan"));return;}
        const FBox Local=Source->GetBoundingBox();
        const float Scale=180.f/FMath::Max(1.f,float(Local.GetSize().Z));
        const FVector LocalFoot(Local.GetCenter().X,Local.GetCenter().Y,Local.Min.Z);
        const float Floor=Court->Bounds.Min.Z;
        for(float Side:{-1.f,1.f})
        {
            // End-wall statues are well above the fight, high-angle camera and
            // jump envelope. They do not intrude into the four side clerestories.
            const FVector Foot(Side*2270,0,Floor+1430);
            const FQuat Yaw=FRotator(0,Side*90,0).Quaternion();
            auto* Statue=Attach<UStaticMeshComponent>(Owner,Parent);Statue->SetMobility(EComponentMobility::Static);
            Statue->SetStaticMesh(Source);Statue->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Statue->SetCanEverAffectNavigation(false);Statue->SetCastShadow(true);
            Statue->SetRelativeTransform(FTransform(Yaw,Foot-Yaw.RotateVector(LocalFoot*Scale),FVector(Scale)));
            Statue->ComponentTags.Add(TEXT("NorthFinalSanctum"));Statue->ComponentTags.Add(TEXT("NorthFinalStatue"));Statue->RegisterComponent();
            auto Stone=[&](float X,float Z,const FVector& Size)
            {Support->AddInstance(FTransform(FQuat::Identity,FVector(Side*X,0,Floor+Z),Size/100.));};
            // The rear plate enters the existing inner wall face at |X|=2388;
            // the ledge and two corbels visibly connect it to the statue's feet.
            Stone(2265,1415,FVector(260,210,30));
            Stone(2268,1395,FVector(240,192,10));
            Stone(2390,1390,FVector(16,210,90));
            for(float Y:{-75.f,75.f})Rod(FVector(Side*2390,Y,Floor+1360),FVector(Side*2190,Y,Floor+1395),8.f);
        }
    }


    void SelectFog(FState& State,const FLocation& Here)
    {
        State.Stats.Room=Here.Room;
        int32 FogCount=0;
        for(FFog& F:State.Fog)if(auto* V=F.Volume.Get())
        {
            const bool Active=F.Room==Here.Room && FogCount<Budget().LocalFogVolumes;
            V->SetVisibility(Active);if(Active)++FogCount;
        }
        State.Stats.VisibleLocalFogVolumes=FogCount;
    }

    void SelectTorchFlames(FState& State, const FVector& Player)
    {
        if (State.TorchFlames.IsEmpty()) return;
        TArray<int32, TInlineAllocator<24>> Nearby;
        for (int32 I : State.WallTorchNodes)
        {
            const FNode& N = State.Nodes[I];
            if (FMath::Abs(N.Position.Z - Player.Z) < 485. &&
                FVector::DistSquared(N.Position, Player) < FMath::Square(1860.))
                Nearby.Add(I);
        }
        Nearby.Sort([&](int32 A, int32 B)
        {
            const auto Score = [&](int32 I)
            {
                const FNode& N = State.Nodes[I];
                return FVector::DistSquared(N.Position, Player) * (N.Room == State.Stats.Room ? .58 : 1.);
            };
            return Score(A) < Score(B);
        });
        Nearby.SetNum(FMath::Min(Nearby.Num(), State.TorchFlames.Num()));
        for (FTorchFlame& Flame : State.TorchFlames)
            if (Flame.Node != INDEX_NONE && !Nearby.Contains(Flame.Node))
            {
                if (auto* Particles = Flame.Particles.Get())
                {
                    Particles->DeactivateSystem();
                    Particles->SetVisibility(false);
                }
                Flame.Node = INDEX_NONE;
            }
        for (int32 I : Nearby)
        {
            if (State.TorchFlames.ContainsByPredicate([&](const FTorchFlame& Flame) { return Flame.Node == I; })) continue;
            auto* Slot = State.TorchFlames.FindByPredicate([](const FTorchFlame& Flame) { return Flame.Node == INDEX_NONE; });
            if (!Slot || !Slot->Particles.IsValid()) break;
            const FNode& Node = State.Nodes[I];
            auto* Particles = Slot->Particles.Get();
            Particles->SetRelativeLocation(Node.Position - FVector(0, 0, 19.));
            Particles->SetVisibility(true);
            Particles->ActivateSystem(true);
            Slot->Node = I;
        }
    }

    bool GuideClear(const UWorld& World, const FVector& From, const FVector& To, const FCollisionQueryParams& Query)
    {
        return !World.SweepTestByChannel(From, To, FQuat::Identity, ECC_Pawn,
            FCollisionShape::MakeSphere(GuideRadius), Query);
    }

    bool AddGuideLeg(const UWorld& World, const FVector& Destination, const FCollisionQueryParams& Query, TArray<FVector>& Path)
    {
        if (Path.Last().Equals(Destination, .1)) return true;
        if (!GuideClear(World, Path.Last(), Destination, Query)) return false;
        Path.Add(Destination);
        return true;
    }

    bool AddGuideStairs(const UWorld& World, const FRoom& Core, const FVector& Destination,
        const FCollisionQueryParams& Query, TArray<FVector>& Path)
    {
        const FRoute* Route = FindRoute(FName(*Core.Id.ToString().LeftChop(4))); // Stair route and its Core room share a stem.
        if (!Route || Route->Points.Num() < 2) return false;
        const FVector Lift(0, 0, GuideHeight);
        struct FProjection { int32 Segment = INDEX_NONE; FVector Point = FVector::ZeroVector; };
        const auto Project = [&](const FVector& End)
        {
            FProjection Best;
            double BestDistance = FMath::Square(260.);
            for (int32 I = 0; I + 1 < Route->Points.Num(); ++I)
            {
                const FVector At = FMath::ClosestPointOnSegment(End, Route->Points[I] + Lift, Route->Points[I + 1] + Lift);
                const double Distance = FVector::DistSquared(End, At);
                if (Distance < BestDistance && FMath::Abs(End.Z - At.Z) < 90. && GuideClear(World, End, At, Query))
                {
                    Best = {I, At};
                    BestDistance = Distance;
                }
            }
            return Best;
        };
        const FProjection A = Project(Path.Last()), B = Project(Destination);
        if (A.Segment == INDEX_NONE || B.Segment == INDEX_NONE || !AddGuideLeg(World, A.Point, Query, Path)) return false;
        // Keep every switchback and landing corner in order. Coincident turns
        // in XY on different flights are distinct route points in Z.
        for (int32 I = A.Segment + 1; I <= B.Segment; ++I)
            if (!AddGuideLeg(World, Route->Points[I] + Lift, Query, Path)) return false;
        for (int32 I = A.Segment; I > B.Segment; --I)
            if (!AddGuideLeg(World, Route->Points[I] + Lift, Query, Path)) return false;
        return AddGuideLeg(World, B.Point, Query, Path) && AddGuideLeg(World, Destination, Query, Path);
    }

    bool AddGuideRoom(const UWorld& World, const FRoom& Room, const FVector& Destination,
        const FCollisionQueryParams& Query, TArray<FVector>& Path)
    {
        if (Room.Kind == ERoomKind::Stairwell) return AddGuideStairs(World, Room, Destination, Query, Path);
        if (Path.Last().Equals(Destination, .1)) return true;
        const bool bArchive = Room.Id == TEXT("ChapelHall");
        const auto* Mode = bArchive ? World.GetAuthGameMode<ADuelMode>() : nullptr;
        const auto* Player = Mode ? Mode->GetPlayerFighter() : nullptr;
        const auto* Capsule = Player ? Player->GetCapsuleComponent() : nullptr;
        const FVector Lift(0, 0, GuideHeight);
        const auto CanTravel = [&](const FVector& A, const FVector& B)
        {
            if (!GuideClear(World, A, B, Query)) return false;
            if (!Capsule) return true;
            const double HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
            const FVector AtFeet(0, 0, GuideHeight - HalfHeight - 2.);
            const auto Body = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), HalfHeight);
            const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
            return !World.SweepTestByChannel(A - AtFeet, B - AtFeet, FQuat::Identity, ECC_Pawn, Body, Query, Responses);
        };
        if (CanTravel(Path.Last(), Destination))
        {
            Path.Add(Destination);
            return true;
        }

        // An authored walking point can take the cue around room furnishings.
        // Every edge in this small room-local search still uses the real sweep.
        TArray<FVector> Points{Path.Last(), Destination};
        if (const FRoute* Walk = FindRoute(TEXT("MainWalk")))
            for (const FVector& At : Walk->Points)
                if (FMath::Abs(At.Z - Room.Bounds.Min.Z) < 1. && Room.Bounds.IsInside(At + Lift))
                    Points.AddUnique(At + Lift);
        if (bArchive)
            for (double X : DuelNorthArchive::AisleX)
                for (double Y : {0., -DuelNorthArchive::CrossAisle, DuelNorthArchive::CrossAisle,
                    -DuelNorthArchive::AisleEnd, DuelNorthArchive::AisleEnd})
                    Points.AddUnique(FVector(X, Y, Room.Bounds.Min.Z + GuideHeight));
        TArray<int32> Queue{0}, Parent;
        Parent.Init(INDEX_NONE, Points.Num());
        Parent[0] = 0;
        for (int32 I = 0; I < Queue.Num(); ++I)
        {
            const int32 Node = Queue[I];
            for (int32 Next = 1; Next < Points.Num(); ++Next)
            {
                if (Parent[Next] != INDEX_NONE || !CanTravel(Points[Node], Points[Next])) continue;
                Parent[Next] = Node;
                if (Next == 1)
                {
                    TArray<FVector> ReversePath;
                    for (int32 At = 1; At != 0; At = Parent[At]) ReversePath.Add(Points[At]);
                    for (int32 J = ReversePath.Num() - 1; J >= 0; --J) Path.Add(ReversePath[J]);
                    return true;
                }
                Queue.Add(Next);
            }
        }
        return false;
    }

    bool BuildGuidePath(const UWorld& World, const ADuelNorthTower::FObjectiveRoute& Objective, const FVector& Start,
        const FCollisionQueryParams& Query, TArray<FVector>& Path)
    {
        Path.Reset();
        Path.Add(Start);
        const FRoom* Room = FindRoom(Objective.StartRoom);
        if (!Room || World.OverlapBlockingTestByChannel(Start, FQuat::Identity, ECC_Pawn,
            FCollisionShape::MakeSphere(GuideRadius), Query)) return false;
        const FVector Lift(0, 0, GuideHeight);
        for (const FPortal* Portal : Objective.Doors)
        {
            const FRoom* Next = FindRoom(Portal->RoomA == Room->Id ? Portal->RoomB : Portal->RoomA);
            if (!Next) return false;
            const FVector Direction = Portal->Normal *
                FMath::Sign(FVector::DotProduct(Next->Bounds.GetCenter() - Room->Bounds.GetCenter(), Portal->Normal));
            const FVector Centre = Portal->Center + Lift;
            if (!AddGuideRoom(World, *Room, Centre - Direction * 58., Query, Path) ||
                !AddGuideLeg(World, Centre + Direction * 58., Query, Path)) return false;
            Room = Next;
        }
        return AddGuideRoom(World, *Room, Objective.Goal + Lift, Query, Path);
    }

    FCollisionQueryParams GuideCollision(const ADuelMode& Mode, const ADuelNorthTower& Tower)
    {
        FCollisionQueryParams Query(SCENE_QUERY_STAT(NorthGuideTravel), false);
        if (const auto* Player = Mode.GetPlayerFighter()) Query.AddIgnoredActor(Player);
        if (const auto* Boss = Mode.GetBossFighter()) Query.AddIgnoredActor(Boss);
        if (const auto* Narrative = Tower.Narrative())
            if (const auto* Companion = Narrative->Companion()) Query.AddIgnoredActor(Companion);
        return Query;
    }

    void SelectGuidance(const ADuelNorthTower& Tower, FState& State, const FVector& Player, const FCollisionQueryParams& Query)
    {
        const FLocation Here = Locate(Player);
        if (Here.Room.IsNone()) return;
        const FVector Home(Player.X, Player.Y, Here.SurfaceZ + GuideHeight);
        const FVector Start = State.GuidePosition.Get(Home);
        ADuelNorthTower::FObjectiveRoute Objective;
        if (!Tower.BuildObjectiveRoute(Locate(Start, GuideHeight), Here.Floor, Objective))
        {
            State.GuidePath.Reset();
            return;
        }
        if (!State.GuidePath.IsEmpty() && State.GuideGoal.Equals(Objective.Goal, 24.)) return;
        TArray<FVector> Path;
        if (!BuildGuidePath(*Tower.GetWorld(), Objective, Start, Query, Path))
        {
            State.GuidePath.Reset();
            return;
        }
        State.GuidePosition = Start;
        State.GuideGoal = Objective.Goal;
        State.GuidePath = MoveTemp(Path);
        State.NextGuidePoint = 1;
    }

    void AdvanceGuide(const UWorld& World, FState& State, const FVector& Player, double Delta,
        const FCollisionQueryParams& Query)
    {
        if (!State.GuidePath.IsValidIndex(State.NextGuidePoint))
        {
            State.GuideVelocity = 0.;
            return;
        }
        if (Delta <= 0. || !State.GuidePosition.IsSet()) return;
        FVector& At = State.GuidePosition.GetValue();
        const FVector Next = State.GuidePath[State.NextGuidePoint];
        const FVector Leg = Next - At;
        const FVector Direction = Leg.GetSafeNormal();
        const double Remaining = Leg.Size();
        const FVector Offset = At - (Player + FVector(0, 0, GuideHeight - 62.));
        const double Along = FVector::DotProduct(Offset, Direction);
        const double Reach = FMath::Square(Along) + FMath::Square(GuideLead) - Offset.SizeSquared();
        const double UntilLeash = FMath::Max(0., FMath::Sqrt(FMath::Max(0., Reach)) - Along);

        const double Cruise = GuideSpeed * (.82 + .18 * FMath::Sin(State.Time * 2.45));
        double ArrivalSpeed = 0.;
        if (const FVector* After = State.GuidePath.IsValidIndex(State.NextGuidePoint + 1) ?
            &State.GuidePath[State.NextGuidePoint + 1] : nullptr)
        {
            const double Alignment = FVector::DotProduct(Direction, (*After - Next).GetSafeNormal());
            ArrivalSpeed = FMath::Lerp(145., Cruise, FMath::SmoothStep(.25, 1., Alignment));
        }
        const double TargetSpeed = FMath::Min3(Cruise,
            FMath::Sqrt(FMath::Square(ArrivalSpeed) + 2. * GuideBraking * Remaining),
            FMath::Sqrt(2. * GuideBraking * UntilLeash));
        State.GuideVelocity = FMath::FInterpConstantTo(State.GuideVelocity, TargetSpeed, Delta,
            TargetSpeed < State.GuideVelocity ? GuideBraking : GuideAcceleration);
        const double Step = FMath::Min3(Delta * State.GuideVelocity, Remaining, UntilLeash);
        const FVector Candidate = At + Direction * Step;
        if (Step > 0. && !GuideClear(World, At, Candidate, Query))
        {
            State.GuidePath.Reset();
            State.GuideVelocity = 0.;
            return;
        }
        At = Candidate;
        if (Remaining - Step < .001 && UntilLeash >= Remaining)
        {
            At = Next;
            ++State.NextGuidePoint;
            State.GuideVelocity = FMath::Min(State.GuideVelocity, ArrivalSpeed);
        }
    }

    TOptional<FVector> FloatGuide(const UWorld& World, FState& State, double Delta,
        const FCollisionQueryParams& Query)
    {
        const FVector At = State.GuidePosition.GetValue();
        double Clearance = 0.;
        if (State.GuidePath.IsValidIndex(State.NextGuidePoint))
        {
            const FVector& From = State.GuidePath[State.NextGuidePoint - 1];
            const FVector& To = State.GuidePath[State.NextGuidePoint];
            const FVector Right = FVector::CrossProduct(FVector::UpVector, (To - From).GetSafeNormal2D());
            State.GuideRight = FMath::VInterpTo(State.GuideRight, Right, Delta, 7.);
            const double DistanceToCorner = FMath::Min(FVector::Distance(At, From), FVector::Distance(At, To));
            Clearance = FMath::SmoothStep(0., 110., DistanceToCorner) * State.GuideVelocity / GuideSpeed;
        }
        const double T = State.Time;
        const FVector Drift = State.GuideRight * (24. * Clearance * FMath::Sin(T * 2.9)) +
            FVector(0., 0., (6. + 10. * Clearance) * FMath::Sin(T * 2.05) + 2. * Clearance * FMath::Sin(T * 3.4));
        FVector Floating = At + Drift;
        FHitResult Hit;
        if (World.SweepSingleByChannel(Hit, At, Floating, FQuat::Identity, ECC_Pawn,
            FCollisionShape::MakeSphere(GuideRadius), Query))
        {
            if (Hit.bStartPenetrating) return {};
            const double Margin = 1. / FMath::Max(1., Drift.Size());
            Floating = At + Drift * FMath::Max(0., double(Hit.Time) - Margin);
        }
        if (State.GuideDisplayPosition.IsSet() && !GuideClear(World, State.GuideDisplayPosition.GetValue(), Floating, Query))
            Floating = At;
        State.GuideDisplayPosition = Floating;
        return Floating;
    }
}

const FLightingBudget& Budget(){static const FLightingBudget Value;return Value;}
FLightingStats Stats(AActor* Owner)
{
    if(const FState* State=States.Find(TWeakObjectPtr<AActor>(Owner)))return State->Stats;
    return {};
}

void Build(AActor* Owner,USceneComponent* Parent)
{
    if(!Owner || !Parent || States.Contains(TWeakObjectPtr<AActor>(Owner)))return;
    for(auto It=States.CreateIterator();It;++It)if(!It.Key().IsValid())It.RemoveCurrent();
    FState& State=States.Add(TWeakObjectPtr<AActor>(Owner));AuthorNodes(State);
    State.Stats.AuthoredNodes=State.Nodes.Num();
    for(int32 I=0;I<State.Nodes.Num();++I)
    {
        const FNode& N=State.Nodes[I];
        UPointLightComponent* L=N.bSpot?static_cast<UPointLightComponent*>(Attach<USpotLightComponent>(Owner,Parent)):
            Attach<UPointLightComponent>(Owner,Parent);
        const auto* Room=FindRoom(N.Room);
        const bool bCloseWall=Room && FMath::Min(Room->Bounds.GetSize().X,Room->Bounds.GetSize().Y) <= 610.;
        const float TorchPower=bCloseWall ? .61f : .73f;
        L->SetIntensityUnits(ELightUnits::Candelas);
        const bool bWallTorch=UsesWallTorch(N);
        if(bWallTorch) State.WallTorchNodes.Add(I);
        L->SetIntensity(N.Candela * (bWallTorch ? TorchPower : 1.f));
        L->SetVisibility(true);
        L->SetCastShadows(true);L->SetShadowBias(.25f);L->SetShadowSlopeBias(.4f);L->ContactShadowLength=0.f;
        L->ComponentTags.Add(TEXT("NorthTowerFixedLight"));L->ComponentTags.Add(N.Room);
        FSlot Slot;Slot.Light=L;Slot.Node=I;Slot.bSpot=N.bSpot;ApplyNode(Slot,N);State.Slots.Add(Slot);L->RegisterComponent();
        if(N.bSpot)++State.Stats.VisibleSpots;else ++State.Stats.VisiblePoints;
        if(N.bVolumetric)++State.Stats.VolumetricShadowLights;
    }
    UE_LOG(LogTemp,Display,TEXT("North fixed environment: %d point / %d spot / %d volume-shadow sources; no proximity switching"),
        State.Stats.VisiblePoints,State.Stats.VisibleSpots,State.Stats.VolumetricShadowLights);

    FinalSanctumDetails(Owner,Parent);

    auto* DungeonFlame=DuelGeometry::ArtMaterial(TEXT("M_NorthDungeonFlame"));
    auto* Iron=DuelGeometry::ColorMaterial(Owner,FLinearColor(.075f,.081f,.088f),false,.65f,.78f);
    if(DungeonFlame)BuildWallTorches(Owner,Parent,State,DungeonFlame,Iron);
    else UE_LOG(LogTemp,Warning,TEXT("Dungeon torch material missing; run SourceAssets/QA/prepare_north_dungeon_materials.py."));
    if (FApp::CanEverRender())
        if (auto* Fire = LoadObject<UParticleSystem>(nullptr,
            TEXT("/Game/MedievalDungeon/Particles/P_Torch_Fire.P_Torch_Fire")))
        {
            constexpr int32 MaxDrawnTorches = 4;
            for (int32 I = 0; I < MaxDrawnTorches; ++I)
            {
                auto* Particles = Attach<UParticleSystemComponent>(Owner, Parent);
                Particles->SetTemplate(Fire);
                Particles->SetAutoActivate(false);
                Particles->SetRelativeScale3D(FVector(.61));
                Particles->SetVisibility(false);
                Particles->SetCastShadow(false);
                Particles->RegisterComponent();
                State.TorchFlames.Add({Particles, INDEX_NONE});
            }
        }
    const auto bLantern=[DungeonFlame](const FNode& Node)
    {return Node.bFixture && !(DungeonFlame && UsesWallTorch(Node));};
    // Scan-backed fixtures locate the real emitters. They are decorative, collision-free,
    // instanced geometry; they do not each allocate another light or shadow map.
    if(auto* Lantern=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/DuelArt/Scans/wooden_lantern_01/wooden_lantern_01.wooden_lantern_01")))
    {
        auto* Fixtures=Attach<UInstancedStaticMeshComponent>(Owner,Parent);Fixtures->SetMobility(EComponentMobility::Static);
        Fixtures->SetStaticMesh(Lantern);Fixtures->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Fixtures->bDisallowNanite=true; // This scanned mesh has a real translucent glass section.
        Fixtures->SetAffectDistanceFieldLighting(false);Fixtures->SetAffectDynamicIndirectLighting(false);
        if(auto* Frame=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/DuelArt/Materials/M_NorthLanternFrame.M_NorthLanternFrame")))Fixtures->SetMaterial(0,Frame);
        if(auto* Glass=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/DuelArt/Materials/M_NorthLanternGlass.M_NorthLanternGlass")))Fixtures->SetMaterial(1,Glass);
        Fixtures->ComponentTags.Add(TEXT("NorthTowerLantern"));
        Fixtures->SetCastShadow(false);Fixtures->RegisterComponent();
        const FBoxSphereBounds Bounds=Lantern->GetBounds();const float Scale=65.f/FMath::Max(1.f,float(Bounds.BoxExtent.Z*2));
        for(const FNode& N:State.Nodes)if(bLantern(N))
            Fixtures->AddInstance(FTransform(FQuat::Identity,N.Position-Bounds.Origin*Scale,FVector(Scale)));
        // Two shared instance batches supply visible, physically connected iron
        // brackets and ceiling rods. They add no lights, shadow maps or collision.
        auto Support=[&](const TCHAR* Path)
        {
            auto* C=Attach<UInstancedStaticMeshComponent>(Owner,Parent);C->SetMobility(EComponentMobility::Static);
            C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,Path));C->SetMaterial(0,Iron);
            C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetCastShadow(false);
            C->SetAffectDistanceFieldLighting(false);C->SetAffectDynamicIndirectLighting(false);
            C->ComponentTags.Add(TEXT("NorthTowerFixtureSupport"));C->RegisterComponent();return C;
        };
        auto* Rods=Support(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
        auto* Plates=Support(TEXT("/Engine/BasicShapes/Cube.Cube"));
        auto Rod=[&](const FVector& A,const FVector& B,float Radius)
        {
            const FVector Delta=B-A;
            Rods->AddInstance(FTransform(FQuat::FindBetweenNormals(FVector::UpVector,Delta.GetSafeNormal()),
                (A+B)*.5,FVector(Radius/50,Radius/50,Delta.Size()/100)));
        };
        for(const FNode& N:State.Nodes)if(bLantern(N))
        {
            if(N.bCeilingFixture)
            {
                Rod(N.Position+FVector(0,0,31),N.FixtureAnchor,1.6f);
                Rod(N.FixtureAnchor-FVector(0,0,2),N.FixtureAnchor,7.f);
            }
            else if(!N.FixtureInward.IsNearlyZero())
            {
                const FVector Wall=N.FixtureAnchor,Tip=N.Position+FVector(0,0,44);
                const FQuat Rotation=N.FixtureInward.Rotation().Quaternion();
                Plates->AddInstance(FTransform(Rotation,Wall+N.FixtureInward+FVector(0,0,30),FVector(.03,.16,.40)));
                Rod(Wall+FVector(0,0,44),Tip,1.8f);
                Rod(Wall+FVector(0,0,12),Tip-N.FixtureInward*5,1.4f);
                Rod(Tip,N.Position+FVector(0,0,31),1.6f);
                // Visible fastening heads at the upper and lower plate ends.
                for(float Z:{15.f,45.f})Rod(Wall+FVector(0,0,Z),Wall+N.FixtureInward*4+FVector(0,0,Z),2.5f);
            }
        }
        auto* Cores=Attach<UInstancedStaticMeshComponent>(Owner,Parent);Cores->SetMobility(EComponentMobility::Static);
        Cores->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));
        Cores->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/DuelArt/Materials/M_NorthCandle.M_NorthCandle")));
        Cores->SetCollisionEnabled(ECollisionEnabled::NoCollision);Cores->SetCastShadow(false);
        Cores->SetAffectDynamicIndirectLighting(false);Cores->SetAffectDistanceFieldLighting(false);Cores->RegisterComponent();
        for(const FNode& N:State.Nodes)if(bLantern(N))
            Cores->AddInstance(FTransform(FQuat::Identity,N.Position-FVector(0,0,5),FVector(.055,.055,.12)));
    }

    // The progression guide has its own light and follows the walking route.
    auto* Guide=Attach<UPointLightComponent>(Owner,Parent);State.GuideLight=Guide;
    Guide->SetIntensityUnits(ELightUnits::Candelas);Guide->SetIntensity(0);Guide->SetVisibility(false);
    Guide->SetAttenuationRadius(315);Guide->SetCastShadows(true);Guide->SetCastVolumetricShadow(false);
    Guide->SetVolumetricScatteringIntensity(0);Guide->SetSourceRadius(5);Guide->SetShadowBias(.25f);
    Guide->SetUseTemperature(false);Guide->SetLightColor(GuideYellow);Guide->ComponentTags.Add(TEXT("NorthTowerGuidanceLight"));Guide->RegisterComponent();
    auto* Flame=Attach<UStaticMeshComponent>(Owner,Parent);State.GuideFlame=Flame;
    Flame->SetStaticMesh(DuelGeometry::Mesh(DuelGeometry::EShape::Plane));
    auto* FlameMaterial=UMaterialInstanceDynamic::Create(DuelGeometry::ArtMaterial(TEXT("M_NorthFiligree")),Owner);
    State.GuideMaterial=FlameMaterial;
    if (FlameMaterial)
    {
        FlameMaterial->SetTextureParameterValue(TEXT("Artwork"),LoadObject<UTexture2D>(nullptr,
            TEXT("/Game/DuelArt/Textures/T_Kenney_light_01.T_Kenney_light_01")));
        FlameMaterial->SetVectorParameterValue(TEXT("Tint"), FLinearColor(1.f,.73f,.34f));
        FlameMaterial->SetScalarParameterValue(TEXT("TileSize"), 1.f);
        FlameMaterial->SetScalarParameterValue(TEXT("LumaMask"), 1.f);
        FlameMaterial->SetScalarParameterValue(TEXT("Gain"),3.2f);
        Flame->SetMaterial(0,FlameMaterial);
    }
    Flame->SetCollisionEnabled(ECollisionEnabled::NoCollision);Flame->SetCanEverAffectNavigation(false);
    Flame->SetCastShadow(false);Flame->SetAffectDynamicIndirectLighting(false);Flame->SetAffectDistanceFieldLighting(false);
    Flame->SetVisibility(false);Flame->ComponentTags.Add(TEXT("NorthTowerGuidanceFlame"));Flame->RegisterComponent();

    auto* Grade=Attach<UPostProcessComponent>(Owner,Parent);State.Grade=Grade;
    Grade->bUnbound=true;Grade->Priority=100;Grade->BlendWeight=1;
    auto& P=Grade->Settings;
    P.bOverride_ColorSaturation=true;P.ColorSaturation=FVector4(1,1,1,.90f);
    P.bOverride_ColorGammaShadows=true;P.ColorGammaShadows=FVector4(.995,1.015,1.065,1.);
    P.bOverride_BloomIntensity=true;P.BloomIntensity=.24f;
    P.bOverride_BloomThreshold=true;P.BloomThreshold=2.10f;
    P.bOverride_VignetteIntensity=true;P.VignetteIntensity=.12f;
    P.bOverride_MotionBlurAmount=true;P.MotionBlurAmount=0;
    P.bOverride_SceneFringeIntensity=true;P.SceneFringeIntensity=0;
    P.bOverride_AutoExposureMethod=true;P.AutoExposureMethod=AEM_Histogram;
    P.bOverride_AutoExposureMinBrightness=true;P.bOverride_AutoExposureMaxBrightness=true;
    P.AutoExposureMinBrightness=P.AutoExposureMaxBrightness=State.EV;
    P.bOverride_AutoExposureBias=true;P.AutoExposureBias=0;
    P.bOverride_LocalExposureHighlightContrastScale=true;P.LocalExposureHighlightContrastScale=.78f;
    P.bOverride_LocalExposureShadowContrastScale=true;P.LocalExposureShadowContrastScale=.83f;
    P.bOverride_LocalExposureDetailStrength=true;P.LocalExposureDetailStrength=1;
    P.bOverride_LocalExposureBlurredLuminanceBlend=true;P.LocalExposureBlurredLuminanceBlend=.6f;
    P.bOverride_LumenSkylightLeaking=true;P.LumenSkylightLeaking=0;
    Grade->RegisterComponent();

    auto* Mist=Attach<UExponentialHeightFogComponent>(Owner,Parent);State.Mist=Mist;
    Mist->SetRelativeLocation(FVector(0,0,-1800));Mist->SetFogDensity(.0035f);Mist->SetFogHeightFalloff(.025f);
    Mist->SetFogInscatteringColor(FLinearColor(.10f,.17f,.27f));Mist->SetFogMaxOpacity(.22f);
    Mist->SetStartDistance(180);Mist->SetVolumetricFog(true);Mist->SetVolumetricFogDistance(Budget().FogViewDistance);
    Mist->SetVolumetricFogStartDistance(160);Mist->SetVolumetricFogNearFadeInDistance(240);
    Mist->SetVolumetricFogScatteringDistribution(.18f);Mist->SetVolumetricFogAlbedo(FColor(185,190,195));
    Mist->SetVolumetricFogExtinctionScale(.45f);Mist->RegisterComponent();
    for(const FRoom& R:Rooms())
    {
        if(R.Kind!=ERoomKind::Combat && R.Kind!=ERoomKind::Hall && R.Id!=TEXT("SeaGallery"))continue;
        const FVector Size=R.Bounds.GetSize();
        const float Radius=FMath::Min(560.f,float(FMath::Min3(Size.X,Size.Y,Size.Z)*.5-35));
        auto* Fog=Attach<ULocalFogVolumeComponent>(Owner,Parent);
        Fog->SetRelativeLocation(FVector(R.Bounds.GetCenter().X,R.Bounds.GetCenter().Y,R.Bounds.Min.Z+Radius+12));
        Fog->SetRelativeScale3D(FVector(Radius/ULocalFogVolumeComponent::GetBaseVolumeSize()));
        const bool bDeepInterior=R.Id==TEXT("DungeonCourt")||R.Id==TEXT("PrisonHall")||R.Id==TEXT("MirrorCourt");
        Fog->SetRadialFogExtinction(bDeepInterior?.034f:.012f);
        Fog->SetHeightFogExtinction(bDeepInterior?.052f:.025f);Fog->SetHeightFogFalloff(280);
        Fog->SetHeightFogOffset(-.45f);
        Fog->SetFogAlbedo(bDeepInterior?FLinearColor(.46f,.57f,.74f):FLinearColor(.58f,.60f,.63f));
        Fog->SetFogPhaseG(.18f);
        Fog->SetVisibility(false);Fog->RegisterComponent();State.Fog.Add({R.Id,Fog});
    }
    // The PC renderer resolves every unoccluded sky pixel. A dome carrying
    // IsSky would disable that pass wherever its surface was culled.
    auto* Atmosphere=Attach<USkyAtmosphereComponent>(Owner,Parent);
    Atmosphere->TransformMode=ESkyAtmosphereTransformMode::PlanetTopAtComponentTransform;
    Atmosphere->SetRelativeLocation(FVector(0,0,DuelNorthScenery::SeaLevelCm));
    Atmosphere->SetBottomRadius(DuelNorthScenery::PlanetRadiusKm);
    Atmosphere->SetGroundAlbedo(FColor(37,65,82));
    Atmosphere->SetSkyLuminanceFactor(FLinearColor(.13f,.18f,.29f));
    Atmosphere->SetMieScatteringScale(.0048f);
    Atmosphere->SetHeightFogContribution(0.f);
    Atmosphere->SetAerialPerspectiveStartDepth(.18f);
    Atmosphere->RegisterComponent();
    auto* Sun=Attach<UDirectionalLightComponent>(Owner,Parent);State.Sun=Sun;
    Sun->SetRelativeRotation(FRotator(-34,150,0));Sun->SetIntensity(53.f);Sun->SetVisibility(true);Sun->SetCastShadows(true);
    Sun->SetUseTemperature(true);Sun->SetTemperature(9100);
    Sun->SetLightColor(FLinearColor(.56f,.76f,1.f));
    Sun->SetAtmosphereSunLight(true);
    Sun->SetAtmosphereSunDiskColorScale(FLinearColor::Black);
    Sun->SetLightSourceAngle(.57f);
    Sun->SetVolumetricScatteringIntensity(0);Sun->RegisterComponent();
    auto* Sky=Attach<USkyLightComponent>(Owner,Parent);State.Sky=Sky;Sky->SetIntensity(.25f);Sky->SetVisibility(true);Sky->SetCastShadows(true);
    Sky->SetLightColor(FLinearColor(.47f,.65f,1.f));
    if(auto* HDR=LoadObject<UTextureCube>(nullptr,TEXT("/Game/DuelArt/Textures/T_Overcast.T_Overcast")))
    {Sky->SourceType=SLS_SpecifiedCubemap;Sky->SetCubemap(HDR);}
    Sky->RegisterComponent();
    Update(Owner,SpawnSurface()+FVector(0,0,62),1.f);
}

void Update(AActor* Owner,const FVector& PlayerPosition,float DeltaSeconds,const FLocation* KnownLocation)
{
    FState* State=States.Find(TWeakObjectPtr<AActor>(Owner));if(!State)return;
    const float Dt=FMath::Clamp(DeltaSeconds,0.f,.25f);
    State->SinceSelection+=DeltaSeconds;State->Time+=Dt;
    State->VictorySeconds=FMath::Max(0.f,State->VictorySeconds-Dt);
    const FLocation Here=KnownLocation?*KnownLocation:Locate(PlayerPosition);
    const bool Select = State->SinceSelection >= Budget().SelectionInterval;
    if (Select)
    {
        SelectFog(*State, Here);
        SelectTorchFlames(*State, PlayerPosition);
        State->SinceSelection = 0;
    }
    const auto* Mode = Owner->GetWorld() ? Owner->GetWorld()->GetAuthGameMode<ADuelMode>() : nullptr;
    const auto* Tower = Cast<ADuelNorthTower>(Owner);
    const auto* Narrative = Tower ? Tower->Narrative() : nullptr;
    const EDuelOutcome Outcome = Mode ? Mode->Outcome() : EDuelOutcome::Title;
    if (Outcome == EDuelOutcome::Victory && State->PreviousOutcome != EDuelOutcome::Victory) State->VictorySeconds = 18.f;
    State->PreviousOutcome = Outcome;
    const bool bMovedToAnotherArea = (State->LastPlayerPosition.IsSet() &&
        FVector::DistSquared(PlayerPosition, State->LastPlayerPosition.GetValue()) > FMath::Square(750.)) ||
        (State->GuidePosition.IsSet() && FVector::DistSquared(PlayerPosition, State->GuidePosition.GetValue()) > FMath::Square(1450.));
    State->LastPlayerPosition = PlayerPosition;
    TOptional<FVector> GuideDisplay;
    if (!Mode || !Tower || Mode->NorthTower() != Tower || Outcome == EDuelOutcome::Fighting ||
        Outcome == EDuelOutcome::Defeat || Outcome == EDuelOutcome::Title ||
        (Narrative && Narrative->ExtractionInProgress()) || bMovedToAnotherArea)
    {
        // Hide before reseeding on the next selection after a checkpoint move.
        State->GuidePath.Reset();
        State->GuidePosition.Reset();
        State->GuideDisplayPosition.Reset();
        State->GuideRight = FVector::ZeroVector;
        State->GuideBlend=0;
        State->GuideVelocity=0.;
    }
    else
    {
        const FCollisionQueryParams Query = GuideCollision(*Mode, *Tower);
        if (Select) SelectGuidance(*Tower, *State, PlayerPosition, Query);
        AdvanceGuide(*Owner->GetWorld(), *State, PlayerPosition, Dt, Query);
        if (State->GuidePosition.IsSet() && (!State->GuidePath.IsEmpty() || State->GuideBlend > .001f))
        {
            GuideDisplay = FloatGuide(*Owner->GetWorld(), *State, Dt, Query);
            if (!GuideDisplay.IsSet())
            {
                State->GuidePath.Reset();
                State->GuidePosition.Reset();
                State->GuideDisplayPosition.Reset();
            }
        }
    }
    State->Stats.bGuidance = !State->GuidePath.IsEmpty();
    State->GuideBlend=FMath::FInterpConstantTo(State->GuideBlend,State->Stats.bGuidance?1.f:0.f,Dt,3.f);
    const bool Lit=GuideDisplay.IsSet() && State->GuideBlend>.001f;
    const FVector GuideAt = Lit ? State->GuidePosition.GetValue() : FVector::ZeroVector;
    if (!Lit && !State->Stats.bGuidance)
    {
        State->GuidePosition.Reset();
        State->GuideDisplayPosition.Reset();
        State->GuideRight = FVector::ZeroVector;
    }
    State->Stats.GuidanceRoom = Lit ? Locate(GuideAt, GuideHeight).Room : NAME_None;
    State->Stats.GuidanceTarget = State->GuidePath.IsValidIndex(State->NextGuidePoint) ?
        State->GuidePath[State->NextGuidePoint] : GuideAt;
    const bool Victory=State->VictorySeconds>0;
    State->Stats.bVictoryGuide=Lit&&Victory;
    const float Moving = float(State->GuideVelocity / GuideSpeed);
    const float Pulse = .91f + (.09f - .035f * Moving) * FMath::Sin(State->Time * 2.05f);
    const FVector Floating = GuideDisplay.Get(FVector::ZeroVector);
    if(auto* Light=State->GuideLight.Get())
    {
        Light->SetVisibility(Lit);Light->SetIntensity(Lit?(Victory?48.f:32.f)*State->GuideBlend*Pulse:0.f);
        if(Lit)Light->SetRelativeLocation(Floating);
    }
    if(auto* Flame=State->GuideFlame.Get())
    {
        Flame->SetVisibility(Lit);
        if (Lit)
        {
            Flame->SetRelativeLocation(Floating);
            Flame->SetRelativeScale3D(FVector(.31 * Pulse, .31 * Pulse, 1.));
            if (const auto* PC = Owner->GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
            {
                const FVector Eye = PC->PlayerCameraManager->GetCameraLocation();
                Flame->SetWorldRotation(FRotationMatrix::MakeFromZ(Eye - Flame->GetComponentLocation()).Rotator());
            }
        }
    }
    if (auto* Material = State->GuideMaterial.Get())
        Material->SetScalarParameterValue(TEXT("Opacity"), State->GuideBlend * (.71f + .18f * Pulse));
    const EFloor Floor=Here.Floor;
    float TargetEV=InteriorExposureEV;
    if(Floor==EFloor::Dungeon)TargetEV=DungeonExposureEV;
    if(Floor==EFloor::Final)TargetEV=FinalExposureEV;
    if(Floor==EFloor::Garden)
        TargetEV=FMath::Lerp(FinalExposureEV,GardenExposureEV,FMath::SmoothStep(4350.f,6100.f,float(PlayerPosition.X)));
    State->EV=FMath::FInterpTo(State->EV,TargetEV,Dt,.85f);State->Stats.ExposureEV100=State->EV;
    if(auto* Grade=State->Grade.Get())Grade->Settings.AutoExposureMinBrightness=Grade->Settings.AutoExposureMaxBrightness=State->EV;
    State->Stats.bGardenSun=true;
}
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthGuidanceTest,"ManosabaDuel.NorthTower.GuidanceRoute",
    EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FDuelNorthGuidanceTest::RunTest(const FString& Parameters)
{
    using namespace DuelNorthLayout;
    using namespace DuelNorthLighting;
    ADuelMode* Mode = nullptr;
    if (GEngine) for (const FWorldContext& Context : GEngine->GetWorldContexts())
        if (Context.World() && (Mode = Context.World()->GetAuthGameMode<ADuelMode>())) break;
    const auto* Tower = Mode ? Mode->NorthTower() : nullptr;
    if (!TestTrue(TEXT("Use an isolated North Tower game with real collision"), Tower &&
        FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")))) return false;
    UWorld& World = *Tower->GetWorld();
    const FCollisionQueryParams Query = GuideCollision(*Mode, *Tower);
    const FVector Lift(0, 0, GuideHeight);
    TestFalse(TEXT("The actual solid antechamber wall blocks the guide sphere"),
        GuideClear(World, FVector(-2850, -415, -1700) + Lift, FVector(-2850, -590, -1700) + Lift, Query));

    const FPortal* Cell = Portals().FindByPredicate([](const FPortal& P) { return P.Id == TEXT("CellDoor"); });
    if (TestNotNull(TEXT("Initial cell gate aperture"), Cell))
    {
        const bool WasOpen = Tower->IsGateOpen(Cell->Id);
        const FVector Near = Cell->Center + Lift - Cell->Normal * 58.;
        const FVector Far = Cell->Center + Lift + Cell->Normal * 58.;
        auto* MutableTower = const_cast<ADuelNorthTower*>(Tower);
        MutableTower->SetGateOpen(Cell->Id, false);
        TestFalse(TEXT("A previously planned segment still stops at a shut gate"), GuideClear(World, Near, Far, Query));
        FState Closing;
        Closing.GuidePosition = Near;
        Closing.GuidePath = {Near, Far};
        Closing.NextGuidePoint = 1;
        Closing.GuideVelocity = GuideSpeed;
        AdvanceGuide(World, Closing, Near - FVector(0, 0, GuideHeight - 62.), .25, Query);
        TestTrue(TEXT("The moving cue abandons a route that now crosses the shut gate"),
            Closing.GuidePath.IsEmpty() && Closing.GuidePosition.GetValue().Equals(Near, .01));
        MutableTower->SetGateOpen(Cell->Id, true);
        TestTrue(TEXT("The same segment uses the real aperture after opening"), GuideClear(World, Near, Far, Query));
        MutableTower->SetGateOpen(Cell->Id, WasOpen);
    }

    int32 CheckedCores = 0;
    for (const FRoute& Steps : Routes())
    {
        if (Steps.Id == TEXT("MainWalk")) continue;
        const FRoom* Core = FindRoom(FName(*(Steps.Id.ToString() + TEXT("Core"))));
        if (!TestNotNull(TEXT("Every stair route has a physical enclosure"), Core)) continue;
        ++CheckedCores;
        for (bool Reverse : {false, true})
        {
            const int32 Last = Steps.Points.Num() - 1;
            TArray<FVector> Travel{Steps.Points[Reverse ? Last : 0] + Lift};
            const bool bBuilt = AddGuideStairs(World, *Core, Steps.Points[Reverse ? 0 : Last] + Lift, Query, Travel);
            const FString Description = FString::Printf(TEXT("%s %s"), *Steps.Id.ToString(), Reverse ? TEXT("down") : TEXT("up"));
            if (!TestTrue(*Description, bBuilt && Travel.Num() == Steps.Points.Num())) continue;
            for (int32 I = 0; I < Travel.Num(); ++I)
                TestTrue(TEXT("Guide retains every stair turn in order"),
                    Travel[I].Equals(Steps.Points[Reverse ? Last - I : I] + Lift, .1));
        }
    }
    TestEqual(TEXT("All five authored switchbacks checked in both directions"), CheckedCores, 5);

    const auto* WestVolume = DuelNorthInteractables::Ledger(TEXT("ArchiveTransferLedger"));
    const auto* EastVolume = DuelNorthInteractables::Ledger(TEXT("ArchiveSignatureRegister"));
    if (TestTrue(TEXT("Both main archive volumes supply guide destinations at their shelves"), WestVolume && EastVolume))
    {
        const FVector West = WestVolume->Approach(), East = EastVolume->Approach();
        const FVector Arrivals[] = {{-2235, 0, West.Z}, {2235, 0, West.Z}, {0, -1525, West.Z}};
        const FName Hall(TEXT("ChapelHall"));
        const auto CheckArchivePath = [&](const FVector& From, const FVector& To)
        {
            const ADuelNorthTower::FObjectiveRoute Objective{To, EFloor::Chapel, Hall, {}};
            TArray<FVector> Path;
            const bool bBuilt = BuildGuidePath(World, Objective, From + Lift, Query, Path);
            TestTrue(FString::Printf(TEXT("The archive guide reaches %s from %s through the installed stacks"),
                *To.ToCompactString(), *From.ToCompactString()), bBuilt && Path.Num() >= 2 && Path.Last().Equals(To + Lift, .1));
            return bBuilt ? Path.Num() : 0;
        };
        for (const FVector& Arrival : Arrivals)
        {
            CheckArchivePath(Arrival, West);
            CheckArchivePath(Arrival, East);
        }
        TestTrue(TEXT("Moving between the two volumes routes around intervening racks"),
            CheckArchivePath(West, East) > 2 && CheckArchivePath(East, West) > 2);
    }

    constexpr float Frame = 1.f / 60.f;
    const FVector EyeLift(0, 0, GuideHeight - 62.);
    const FVector Start(3500, -1800, GuideHeight), End(3500, 1300, GuideHeight);
    FState Corridor;
    Corridor.GuidePosition = Start;
    Corridor.GuidePath = {Start, End};
    Corridor.NextGuidePoint = 1;
    double Slow = GuideSpeed, Fast = 0., Left = 0., Right = 0., Low = 0., High = 0.;
    bool bClear = true;
    for (int32 I = 0; I < 240; ++I)
    {
        const FVector Before = Corridor.GuidePosition.GetValue();
        Corridor.Time += Frame;
        AdvanceGuide(World, Corridor, Before - EyeLift, Frame, Query);
        const FVector At = Corridor.GuidePosition.GetValue();
        const TOptional<FVector> Floating = FloatGuide(World, Corridor, Frame, Query);
        bClear &= Floating.IsSet() && GuideClear(World, At, Floating.Get(At), Query);
        const FVector Drift = Floating.Get(At) - At;
        Left = FMath::Min(Left, Drift.X);
        Right = FMath::Max(Right, Drift.X);
        Low = FMath::Min(Low, Drift.Z);
        High = FMath::Max(High, Drift.Z);
        const double Speed = (At.Y - Before.Y) / Frame;
        if (I >= 55) Slow = FMath::Min(Slow, Speed);
        Fast = FMath::Max(Fast, Speed);
    }
    TestTrue(TEXT("Straight travel visibly varies speed while remaining capped at sprint speed"),
        Corridor.GuidePosition.GetValue().Y > Start.Y + 1600. && Fast > 595. && Fast <= GuideSpeed + .01 && Fast - Slow > 130.);
    TestTrue(TEXT("The cue sweeps to both sides and changes height in the real corridor"), bClear &&
        Left < -10. && Right > 10. && Low < -8. && High > 8.);
    AddInfo(FString::Printf(TEXT("Guide corridor speed %.1f–%.1f cm/s, lateral %.1f–%.1f cm, height %.1f–%.1f cm"),
        Slow, Fast, Left, Right, Low, High));

    const FVector HoldPoint = Corridor.GuidePosition.GetValue();
    const FVector WaitingPlayer = HoldPoint - EyeLift - FVector(0., GuideLead, 0.);
    Low = High = 0.;
    bClear = true;
    for (int32 I = 0; I < 140; ++I)
    {
        Corridor.Time += Frame;
        AdvanceGuide(World, Corridor, WaitingPlayer, Frame, Query);
        const TOptional<FVector> Floating = FloatGuide(World, Corridor, Frame, Query);
        bClear &= Floating.IsSet() && GuideClear(World, HoldPoint, Floating.Get(HoldPoint), Query);
        const double Height = Floating.Get(HoldPoint).Z - HoldPoint.Z;
        if (I >= 30)
        {
            Low = FMath::Min(Low, Height);
            High = FMath::Max(High, Height);
        }
    }
    TestTrue(TEXT("At the lead boundary the route stops and the visible cue keeps breathing"), bClear &&
        Corridor.GuideVelocity < .01 && Corridor.GuidePosition.GetValue().Equals(HoldPoint, .01) &&
        High - Low > 7. && High - Low < 15.);
    AdvanceGuide(World, Corridor, WaitingPlayer + FVector(0., 135., 0.), .15, Query);
    TestTrue(TEXT("The guide resumes when the player closes the gap"),
        Corridor.GuidePosition.GetValue().Y > HoldPoint.Y + 5.);
    AddInfo(FString::Printf(TEXT("Guide paused height %.1f–%.1f cm at the %.0f cm lead boundary"), Low, High, GuideLead));

    const FRoute* Final = FindRoute(TEXT("FinalStair"));
    if (TestNotNull(TEXT("Final switchback for motion check"), Final))
    {
        for (int32 I = 1; I + 1 < Final->Points.Num(); ++I)
        {
            const FVector A = Final->Points[I - 1] + Lift, B = Final->Points[I] + Lift, C = Final->Points[I + 1] + Lift;
            if (FMath::Abs(FVector::DotProduct((B - A).GetSafeNormal(), (C - B).GetSafeNormal())) > .1) continue;
            FState Trial;
            Trial.GuidePosition = B + (A - B).GetSafeNormal() * 12.;
            Trial.GuidePath = {Trial.GuidePosition.GetValue(), B, C};
            Trial.NextGuidePoint = 1;
            const FVector Player = B - FVector(0, 0, GuideHeight - 62.);
            Trial.Time = .71f;
            FloatGuide(World, Trial, .25, Query);
            AdvanceGuide(World, Trial, Player, .25, Query);
            TestTrue(TEXT("A long frame reaches the corner without rounding it"), Trial.NextGuidePoint == 2 &&
                Trial.GuidePosition.GetValue().Equals(B, .1));
            const TOptional<FVector> AtTurn = FloatGuide(World, Trial, .25, Query);
            TestTrue(TEXT("The visible cue centers itself at the stair turn and fits the actual stairwell"), AtTurn.IsSet() &&
                FVector::Dist2D(AtTurn.Get(B), B) < .01 && GuideClear(World, B, AtTurn.Get(B), Query));
            AdvanceGuide(World, Trial, Player, .25, Query);
            const FVector Step = Trial.GuidePosition.GetValue() - B;
            const FVector Leg = C - B;
            const TOptional<FVector> AfterTurn = FloatGuide(World, Trial, .25, Query);
            TestTrue(TEXT("Guide accelerates along the next leg at or below sprint speed"),
                FVector::DotProduct(Step, Leg) > 0. &&
                FVector::CrossProduct(Step, Leg.GetSafeNormal()).Size() < .1 &&
                Step.Size() <= .25 * GuideSpeed + .1 && AfterTurn.IsSet() &&
                GuideClear(World, Trial.GuidePosition.GetValue(), AfterTurn.Get(B), Query));
            break;
        }
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthLightingBudgetTest,"ManosabaDuel.NorthTower.LightingBudget",
    EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FDuelNorthLightingBudgetTest::RunTest(const FString& Parameters)
{
    ADuelMode* Mode=nullptr;
    for(const FWorldContext& Context:GEngine->GetWorldContexts())
        if(Context.World() && (Mode=Context.World()->GetAuthGameMode<ADuelMode>()))break;
    if(!TestTrue(TEXT("Use an isolated -DuelNorthTower -DuelNoSave game world"),
        FParse::Param(FCommandLine::Get(),TEXT("DuelNorthTower")) &&
        FParse::Param(FCommandLine::Get(),TEXT("DuelNoSave")) && Mode && Mode->GetPlayerFighter()))return false;
    ADuelNorthTower* Tower=nullptr;
    for(TActorIterator<ADuelNorthTower> It(Mode->GetWorld());It;++It){Tower=*It;break;}
    if(!TestNotNull(TEXT("North Tower actor"),Tower))return false;
    using namespace DuelNorthLighting;
    TInlineComponentArray<UPointLightComponent*> Lights(Tower);
    struct FSnapshot { UPointLightComponent* Light;FTransform Transform;float Intensity,Radius,Temperature; };
    TArray<FSnapshot> Fixed;
    int32 Point=0,Spot=0,Volume=0,Guides=0,RestFires=0,ArchiveCandles=0;
    for(auto* L:Lights)
    {
        if(L->ComponentHasTag(TEXT("NorthTowerGuidanceLight"))){++Guides;continue;}
        if(L->ComponentHasTag(TEXT("NorthArchiveCandle")))
        {
            ++ArchiveCandles;
            TestTrue(TEXT("Shelf candle lights its own furniture without a shadow map or volumetric pass"),
                L->IsVisible() && L->Intensity > 0 && !L->CastShadows && L->AttenuationRadius <= 235.f &&
                L->VolumetricScatteringIntensity == 0 && L->MaxDrawDistance <= 1900.f);
            continue;
        }
        if (L->ComponentHasTag(TEXT("NorthRestFire")))
        {
            ++RestFires;
            TestTrue(TEXT("A rest flame is a local shadow-free source"), !L->CastShadows && L->AttenuationRadius <= 500.f);
            continue;
        }
        TestTrue(TEXT("Environment has fixed physical lights rather than a moving pool"),L->ComponentHasTag(TEXT("NorthTowerFixedLight")));
        TestTrue(TEXT("Every fixed light is always enabled and casts shadows"),L->IsVisible()&&L->Intensity>0&&L->CastShadows);
        TestTrue(TEXT("No legacy large attenuation spheres"),L->AttenuationRadius<=3901.f);
        Fixed.Add({L,L->GetComponentTransform(),L->Intensity,L->AttenuationRadius,L->Temperature});
        if(Cast<USpotLightComponent>(L))++Spot;else ++Point;
        if(L->bCastVolumetricShadow)++Volume;
    }
    TestEqual(TEXT("One separate progression candle; no extra navigation light pool"),Guides,1);
    TestEqual(TEXT("All rest points have a separate gold fire"), RestFires, DuelNorthLayout::Checkpoints().Num());
    TestTrue(TEXT("Reading desks on the archive floor have installed practical lights"), ArchiveCandles >= 2);
    TestTrue(TEXT("Authored fixed-light counts stay inside declared caps"),Point<=Budget().ShadowPointLights&&Spot<=Budget().ShadowSpotLights&&Volume<=Budget().VolumetricShadowLights);
    TestEqual(TEXT("Every authored environment node has a real fixed light"),Fixed.Num(),Stats(Tower).AuthoredNodes);
    FCollisionQueryParams Ray(SCENE_QUERY_STAT(NorthFixedLightVisibility),false);
    Ray.AddIgnoredActor(Mode->GetPlayerFighter());
    if(Mode->GetBossFighter())Ray.AddIgnoredActor(Mode->GetBossFighter());
    // Glass collision stops the player, not light. Solid window frames and the
    // architectural shell remain real occluders in this visibility estimate.
    TInlineComponentArray<UBoxComponent*> Bodies(Tower);
    for(auto* B:Bodies)if(B->ComponentHasTag(TEXT("NorthWindowPane")))Ray.AddIgnoredComponent(B);
    int32 StairFixtures=0,MountedTorches=0,FlamePlanes=0;
    TSet<EFloor> TorchFloors;
    if(const FState* State=States.Find(TWeakObjectPtr<AActor>(Tower)))
        for(const FNode& N:State->Nodes)
        {
            const FRoom* Room=FindRoom(N.Room);
            if(Room && UsesWallTorch(N))
            {
                ++MountedTorches;
                TorchFloors.Add(Room->Floor);
            }
            if(!Room || Room->Kind!=ERoomKind::Stairwell)continue;
            ++StairFixtures;
            TestTrue(TEXT("Stair emitter has a real wall bracket, a short reach and no volumetric shadow"),
                N.bFixture && !N.bSpot && !N.bVolumetric && N.Radius<=970.f &&
                (N.Position-N.FixtureAnchor).Equals(N.FixtureInward*53.,.5) && !N.FixtureInward.IsNearlyZero());
        }
    TestEqual(TEXT("Seventeen mounted stair fixtures cover all twenty-one runs"),StairFixtures,17);
    TInlineComponentArray<UInstancedStaticMeshComponent*> Fixtures(Tower);
    for(const auto* Fixture:Fixtures)
        if(Fixture->ComponentHasTag(TEXT("NorthDungeonTorchFlame")) || Fixture->ComponentHasTag(TEXT("NorthDungeonTorchIron")))
        {
            TestEqual(TEXT("Wall fixture art adds no collision"),Fixture->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
            if(Fixture->ComponentHasTag(TEXT("NorthDungeonTorchFlame")))FlamePlanes+=Fixture->GetInstanceCount();
        }
    TestEqual(TEXT("Every interior floor uses the shared iron torch"),TorchFloors.Num(),int32(UE_ARRAY_COUNT(MapOrder)) - 1);
    TestTrue(TEXT("Each iron torch has a fixed lamp and crossed flame"),MountedTorches>0 && FlamePlanes==2*MountedTorches);
    FCollisionQueryParams TreadRay=Ray;
    // The navigation rail boxes fill the gaps between the actual iron bars.
    // Test the architectural occlusion of the floor, roof and tread solids.
    for(auto* B:Bodies)if(B->ComponentHasTag(TEXT("NorthTowerGuard")))TreadRay.AddIgnoredComponent(B);
    const auto HasFixedLight=[&](const FVector& At, const FVector& Face = FVector::ZeroVector, FName Room = NAME_None)
    {
        return Fixed.ContainsByPredicate([&](const FSnapshot& S)
        {
            const FVector Beam=At-S.Light->GetComponentLocation();
            if (!Room.IsNone() && !S.Light->ComponentHasTag(Room)) return false;
            if (!Face.IsNearlyZero() && FVector::DotProduct(-Beam.GetSafeNormal(), Face) < .075) return false;
            if(Beam.SizeSquared()>FMath::Square(S.Radius))return false;
            if(const auto* Spot=Cast<USpotLightComponent>(S.Light))
                if(FVector::DotProduct(Beam.GetSafeNormal(),Spot->GetForwardVector())<FMath::Cos(FMath::DegreesToRadians(Spot->OuterConeAngle)))
                    return false;
            FHitResult Hit;
            return !Mode->GetWorld()->LineTraceSingleByChannel(Hit,At,S.Light->GetComponentLocation(),ECC_Visibility,TreadRay);
        });
    };
    int32 LitTreads=0,TreadSamples=0,LitLandings=0,LandingSamples=0;
    for(const FStairFlight& S:Stairs())
        for(float T:{.05f,.2f,.35f,.5f,.65f,.8f,.95f})
        {
            const int32 Step=FMath::Min(S.StepCount-1,FMath::FloorToInt(double(T)*S.StepCount+1.e-5));
            FVector At=FMath::Lerp(S.Start,S.End,(Step+.5)/S.StepCount);
            At.Z=S.Start.Z+(Step+1)*S.Riser()+5;
            for(double Y:{-70.,0.,70.})
            {++TreadSamples;if(HasFixedLight(At+FVector(0,Y,0)))++LitTreads;}
        }
    for(const FRoom& R:Rooms())if(R.Kind==ERoomKind::Landing)
    {
        FVector At=R.Bounds.GetCenter();At.Z=R.Bounds.Min.Z+5;
        for(double Y:{-170.,0.,170.})
        {++LandingSamples;if(HasFixedLight(At+FVector(0,Y,0)))++LitLandings;}
    }
    TestEqual(TEXT("Tread samples reach a fixed lamp through the real shell"),LitTreads,TreadSamples);
    TestEqual(TEXT("Landing and bend samples reach a fixed lamp through the real shell"),LitLandings,LandingSamples);
    if (const FRoom* Archive = FindRoom(TEXT("ChapelHall")))
    {
        using namespace DuelNorthArchive;
        const double Z = Archive->Bounds.Min.Z;
        for (double X : {-2160., 2160.}) for (double Y : {-1460., 1460.})
            for (double Height : {285., 600.})
                TestTrue(TEXT("The archive's outer wall bays have clear light paths at cabinet and vault height"),
                    HasFixedLight(FVector(X, Y, Z + Height)));
        int32 Samples = 0, LitFaces = 0;
        for (int32 Side : {-1, 1})
            for (int32 Row = 0; Row < UE_ARRAY_COUNT(Rows); ++Row)
                for (int32 Bay = 0; Bay < Rows[Row].Bays; ++Bay)
                {
                    const FVector Front = FRotator(0, Rows[Row].Yaw + 180., 0).RotateVector(FVector::RightVector);
                    for (double Height : {108., 173.})
                    {
                        ++Samples;
                        const FVector At = FVector(BayPosition(Side, Row, Bay), Z + Height) + Front * (ShelfDepth * .5 + 2.);
                        if (HasFixedLight(At, Front, Archive->Id)) ++LitFaces;
                        else AddInfo(FString::Printf(TEXT("Archive stack face without a direct source at %s"), *At.ToCompactString()));
                    }
                }
        TestEqual(TEXT("Every archive bay has a fixed source on its spine side at both reading heights"), LitFaces, Samples);
    }
    const auto* Route=DuelNorthLayout::FindRoute(TEXT("MainWalk"));
    if(!TestNotNull(TEXT("Main route"),Route))return false;
    int32 MaxDirect=0,MaxPotential=0,Changed=0;FVector Worst=FVector::ZeroVector;
    for(const FVector& P:Route->Points)
    {
        Update(Tower,P+FVector(0,0,62),.51f);
        int32 Direct=0,Potential=0;
        const FVector Sample=P+FVector(0,0,110);
        for(const FSnapshot& S:Fixed)
        {
            auto* L=S.Light;
            if(!L->IsVisible() || !L->GetComponentTransform().Equals(S.Transform) ||
                L->Intensity!=S.Intensity || L->AttenuationRadius!=S.Radius || L->Temperature!=S.Temperature)++Changed;
            const FVector FromLight=Sample-L->GetComponentLocation();
            if(FromLight.SizeSquared()>FMath::Square(L->AttenuationRadius))continue;
            if(const auto* Cone=Cast<USpotLightComponent>(L))
                if(FVector::DotProduct(FromLight.GetSafeNormal(),L->GetForwardVector())<FMath::Cos(FMath::DegreesToRadians(Cone->OuterConeAngle)))continue;
            ++Potential;
            FHitResult Hit;
            if(!Mode->GetWorld()->LineTraceSingleByChannel(Hit,Sample,L->GetComponentLocation(),ECC_Visibility,Ray))++Direct;
        }
        MaxPotential=FMath::Max(MaxPotential,Potential);
        if(Direct>MaxDirect){MaxDirect=Direct;Worst=Sample;}
    }
    Update(Tower,Mode->GetPlayerFighter()->GetActorLocation(),.51f);
    TestEqual(TEXT("Walking never moves, switches or dims environment lights"),Changed,0);
    TestTrue(TEXT("Actual unobstructed local-light overlap stays bounded"),MaxDirect<=Budget().MaxDirectContributors);
    TestTrue(TEXT("Conservative attenuation/cone overlap also stays bounded"),MaxPotential<=Budget().MaxAttenuationContributors);
    AddInfo(FString::Printf(TEXT("Fixed environment: %d point + %d spot, %d volumetric shadows, 1 progression candle; max unoccluded direct contributors %d at %s; conservative cone/radius overlap %d. Real world collision rays and cone/radius tests, not a GPU timing or image-quality result."),
        Point,Spot,Volume,MaxDirect,*Worst.ToString(),MaxPotential));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthSanctumDetailsTest,"ManosabaDuel.NorthTower.FinalSanctum",
    EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FDuelNorthSanctumDetailsTest::RunTest(const FString& Parameters)
{
    ADuelMode* Mode=nullptr;
    for(const FWorldContext& Context:GEngine->GetWorldContexts())
        if(Context.World()&&(Mode=Context.World()->GetAuthGameMode<ADuelMode>()))break;
    if(!TestTrue(TEXT("Run in -DuelNorthTower -DuelNoSave"),Mode&&
        FParse::Param(FCommandLine::Get(),TEXT("DuelNorthTower"))&&FParse::Param(FCommandLine::Get(),TEXT("DuelNoSave"))))return false;
    ADuelNorthTower* Tower=nullptr;
    for(TActorIterator<ADuelNorthTower> It(Mode->GetWorld());It;++It){Tower=*It;break;}
    if(!TestNotNull(TEXT("North Tower actor"),Tower))return false;
    const auto* Court=DuelNorthLayout::FindRoom(TEXT("FinalCourt"));
    if(!TestNotNull(TEXT("Final Court metadata"),Court))return false;
    const float Floor=Court->Bounds.Min.Z;
    int32 Statues=0,Tracery=0,Plinth=0,Groups=0;
    TInlineComponentArray<UStaticMeshComponent*> Meshes(Tower);
    for(auto* C:Meshes)if(C->ComponentHasTag(TEXT("NorthFinalSanctum")))
    {
        ++Groups;
        TestEqual(TEXT("High architectural details cannot snag a character"),C->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
        if(!TestTrue(TEXT("Existing detail asset loaded"), IsValid(C->GetStaticMesh())))continue;
        auto* Instances=Cast<UInstancedStaticMeshComponent>(C);
        const int32 Count=Instances?Instances->GetInstanceCount():1;
        const bool Statue=C->ComponentHasTag(TEXT("NorthFinalStatue"));
        if(Statue)Statues+=Count;
        if(C->ComponentHasTag(TEXT("NorthFinalTracery")))Tracery+=Count;
        if(C->ComponentHasTag(TEXT("NorthFinalPlinth")))Plinth+=Count;
        for(int32 I=0;I<Count;++I)
        {
            FTransform T=C->GetRelativeTransform();if(Instances)Instances->GetInstanceTransform(I,T,false);
            const FBox B=C->GetStaticMesh()->GetBoundingBox().TransformBy(T);
            // 1345 cm is the lowest rear fixing; the conservative current camera
            // envelope including jump/lag/probe is below 1292 cm (offline audit).
            if(!C->ComponentHasTag(TEXT("NorthFinalTracery")))
                TestTrue(TEXT("Sculpture/support clears conservative camera envelope"),B.Min.Z>=Floor+1344.9);
            if(Statue)
            {
                TestTrue(TEXT("Statues fit below vault and outside gameplay rectangle"),B.Max.Z<=Floor+1610.1 &&
                    FMath::Min(FMath::Abs(B.Min.X),FMath::Abs(B.Max.X))>2100 && FMath::Max(FMath::Abs(B.Min.Y),FMath::Abs(B.Max.Y))<110);
                TestTrue(TEXT("Feet meet the supported plinth top"),FMath::Abs(B.Min.Z-(Floor+1430))<.1);
            }
        }
    }
    TestEqual(TEXT("Two wall-supported PBR statues"),Statues,2);
    TestEqual(TEXT("Four stone corbels support the statues"),Tracery,4);
    TestEqual(TEXT("Three connected support pieces per statue"),Plinth,6);
    TestEqual(TEXT("Two stone instance batches and two statues"),Groups,4);
    AddInfo(TEXT("Actual loaded mesh bounds/counts/collision checked. This does not replace lit camera and frame-time verification."));
    return !HasAnyErrors();
}

#endif
