#include "DuelNorthTower.h"
#include "DuelNorthNarrative.h"
#include "DuelNorthArchitecture.h"
#include "DuelNorthScenery.h"
#include "DuelNorthLegacyCollision.h"
#include "DuelNorthRose.h"
#include "DuelNorthGothic.h"
#include "DuelNorthInteractables.h"
#include "DuelNorthLighting.h"
#include "DuelNorthExtractionVisual.h"
#include "DuelNorthDoors.h"
#include "DuelGeometry.h"
#include "DuelGame.h"
#include "DuelArena.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "DistanceFieldAtlas.h"
#include "MeshCardBuild.h"
#include "Materials/MaterialInterface.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/App.h"
#include "HAL/IConsoleManager.h"
#include "PrimitiveSceneProxy.h"
#include "TimerManager.h"
#endif

using namespace DuelNorthLayout;
namespace
{
    void MountConfinementNumber(AActor* Owner,USceneComponent* Parent)
    {
        const auto* Gate=Portals().FindByPredicate([](const FPortal& P){return P.Id==TEXT("DungeonGate");});
        if (!Gate) return;
        // Put the permanent plaque on the approach wall, beyond the opening and arch trim.
        const FVector Centre=Gate->Center+FVector(-22,Gate->Width*.5+88.,150.);
        auto* Plate=DuelGeometry::Shape(Owner,Parent,DuelGeometry::EShape::Box,Centre,FVector(12,64,35),
            DuelGeometry::ColorMaterial(Owner,FLinearColor(.095f,.074f,.041f),false,.7f,.38f));
        Plate->SetCastShadow(false);
        auto* Number=NewObject<UTextRenderComponent>(Owner);
        Number->SetupAttachment(Parent);
        Number->SetMobility(EComponentMobility::Static);
        Number->SetRelativeLocationAndRotation(Centre+FVector(-7,0,0),FRotator(0,180,0));
        Number->SetText(FText::AsCultureInvariant(TEXT("03")));
        Number->SetHorizontalAlignment(EHTA_Center); Number->SetVerticalAlignment(EVRTA_TextCenter);
        Number->SetWorldSize(29.f); Number->SetTextRenderColor(FColor(232,222,191));
        Number->SetCollisionEnabled(ECollisionEnabled::NoCollision); Number->SetCastShadow(false);
        Owner->AddInstanceComponent(Number); Number->RegisterComponent();
    }
    UBoxComponent* Collision(AActor* Owner,const FVector& C,const FVector& D);
    void MountCustodyDetails(AActor* Owner, USceneComponent* Parent)
    {
        using namespace DuelGeometry;
        auto* Iron = ColorMaterial(Owner, FLinearColor(.055f,.069f,.071f), false, .67f, .76f);
        auto* Plate = ColorMaterial(Owner, FLinearColor(.13f,.105f,.065f), false, .81f, .25f);
        for (const auto& Cell : CustodyLabels())
        {
            const FPortal* Door=Portals().FindByPredicate([&](const FPortal& P){return P.Id==Cell.Door;});
            if (!Door) continue;
            const FVector Threshold=Door->Center;
            const float Half=Door->Width*.5f;
            const float Height=Door->Height-12.f;
            // Open grilles fold inside each cell; the doorway keeps its full capsule clearance.
            const FVector Hinge=Threshold+FVector(-Half+5,19,0);
            for(float Side:{-1.f,1.f})
                Shape(Owner,Parent,EShape::Box,Threshold+FVector(Side*(Half+7),0,Height*.5),FVector(9,16,Height),Iron);
            Shape(Owner,Parent,EShape::Box,Threshold+FVector(0,0,Height),FVector(Door->Width+22,18,10),Iron);
            for(int32 Bar=0;Bar<=7;++Bar)
                Shape(Owner,Parent,EShape::Box,Hinge+FVector(0,Door->Width*Bar/7.f,Height*.5),FVector(6,6,Height),Iron);
            for(float Z:{14.f,Height*.37f,Height*.73f,Height})
                Shape(Owner,Parent,EShape::Box,Hinge+FVector(0,Door->Width*.5f,Z),FVector(9,Door->Width+7,7),Iron);
            const FVector Leaf=Hinge+FVector(0,Door->Width*.5f,Height*.5f);
            Collision(Owner,Leaf,FVector(9,Door->Width+7,Height))->ComponentTags.Add(TEXT("NorthCustodyOpenGrille"));
            const FVector Label=Threshold+FVector(Half+70,-19,166);
            Shape(Owner,Parent,EShape::Box,Label,FVector(58,10,33),Plate)->SetCastShadow(false);
            auto* Number=NewObject<UTextRenderComponent>(Owner);
            Number->SetupAttachment(Parent);Number->SetMobility(EComponentMobility::Static);
            Number->SetRelativeLocationAndRotation(Label+FVector(0,-6,0),FRotator(0,-90,0));
            Number->SetText(FText::AsCultureInvariant(Cell.Number));Number->SetWorldSize(26.f);
            Number->ComponentTags.Append({TEXT("NorthCustodyPlaque"),Cell.Door});
            Number->SetHorizontalAlignment(EHTA_Center);Number->SetVerticalAlignment(EVRTA_TextCenter);
            Number->SetTextRenderColor(FColor(220,213,189));Number->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Number->SetCastShadow(false);Owner->AddInstanceComponent(Number);Number->RegisterComponent();
        }
    }
    // Baked panels supply both a distance field and Lumen surface cards.
    // Separate physics boxes keep doorways and stairs free of collision seams.
    enum class EPanel : uint8 { Wall, Floor, Roof };
    constexpr float MaxPanelSpan=400.f;
    constexpr float PanelSizes[]={12.5f,25.f,50.f,100.f,200.f,400.f};
    bool UsesWetPavers(EFloor Floor)
    {
        return Floor==EFloor::Dungeon || Floor==EFloor::Prison || Floor==EFloor::Mirror;
    }
    const TCHAR* PanelLabel(EPanel Kind)
    {
        return Kind==EPanel::Wall?TEXT("Wall"):(Kind==EPanel::Floor?TEXT("Floor"):TEXT("Roof"));
    }
    float NearestPanelSize(float Span)
    {
        float Best=PanelSizes[0],Error=TNumericLimits<float>::Max();
        for(float Candidate:PanelSizes)
        {
            const float Difference=FMath::Abs(FMath::Loge(Span/Candidate));
            if(Difference<Error) { Best=Candidate;Error=Difference; }
        }
        return Best;
    }
    struct FPanelGroup
    {
        EPanel Kind=EPanel::Wall;
        FString MeshName;
        FVector MeshSize=FVector::OneVector;
        bool bWetPavers=false;
        TArray<FTransform> Instances;
    };
    struct FBatch
    {
        explicit FBatch(EPanel InKind):Kind(InKind) {}
        EPanel Kind;
        int32 OriginalBoxes=0;
        TMap<FString,FPanelGroup> Panels;
        void Box(const FVector& C,const FVector& D,bool bWetPavers=false)
        {
            Box(C,D,Kind,bWetPavers);
        }
        void Box(const FVector& C,const FVector& D,EPanel PanelKind,bool bWetPavers=false)
        {
            ++OriginalBoxes;
            const bool bWall=PanelKind==EPanel::Wall;
            const int WidthAxis=bWall?(FMath::IsNearlyEqual(D.X,double(WallThickness))?1:0):0;
            const int HeightAxis=bWall?2:1;
            const int32 NX=FMath::Max(1,FMath::CeilToInt(D[WidthAxis]/MaxPanelSpan));
            const int32 NY=FMath::Max(1,FMath::CeilToInt(D[HeightAxis]/MaxPanelSpan));
            // Equal subdivisions avoid a thin remainder at the end of a wall.
            const float Width=D[WidthAxis]/NX,Height=D[HeightAxis]/NY;
            const float BaseW=NearestPanelSize(Width),BaseH=NearestPanelSize(Height);
            const FString Name=FString::Printf(TEXT("SM_North%s_W%d_H%d"),PanelLabel(PanelKind),
                FMath::RoundToInt(BaseW*10),FMath::RoundToInt(BaseH*10));
            FPanelGroup& Group=Panels.FindOrAdd(bWetPavers?Name+TEXT("_Wet"):Name);
            Group.Kind=PanelKind;
            Group.MeshName=Name;
            Group.bWetPavers=bWetPavers;
            Group.MeshSize=bWall?FVector(BaseW,WallThickness,BaseH):FVector(BaseW,BaseH,PanelKind==EPanel::Roof?50:FloorThickness);
            const FVector Scale=bWall?FVector(Width/BaseW,1,Height/BaseH):
                FVector(Width/BaseW,Height/BaseH,D.Z/(PanelKind==EPanel::Roof?50.f:FloorThickness));
            const FQuat Rotation=bWall&&WidthAxis==1?FRotator(0,90,0).Quaternion():FQuat::Identity;
            for(int32 X=0;X<NX;++X) for(int32 Y=0;Y<NY;++Y)
            {
                FVector At=C;
                At[WidthAxis]+=((X+.5f)/NX-.5f)*D[WidthAxis];
                At[HeightAxis]+=((Y+.5f)/NY-.5f)*D[HeightAxis];
                Group.Instances.Emplace(Rotation,At,Scale);
            }
        }
        void Install(AActor* Owner,UMaterialInterface* WallStone,UMaterialInterface* WetPavers,UMaterialInterface* DryPavers)
        {
            int32 Count=0,Fallbacks=0;
            for(auto& Entry:Panels)
            {
                FPanelGroup& Group=Entry.Value;
                const FString Path=FString::Printf(TEXT("/Game/DuelArt/NorthTower/Meshes/%s.%s"),*Group.MeshName,*Group.MeshName);
                UStaticMesh* Mesh=LoadObject<UStaticMesh>(nullptr,*Path);
                bool bFallback=!Mesh;
                if(!Mesh) Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
                const bool bFloor=Group.Kind==EPanel::Floor;
                UMaterialInterface* Material=bFloor?(Group.bWetPavers?WetPavers:DryPavers):WallStone;
                bFallback|=!Material;
                if(!Material)
                    Material=DuelGeometry::ArtMaterial(bFloor?TEXT("M_NorthFloor"):TEXT("M_NorthWall"));
                if(!Material)
                {
                    bFallback=true;
                    Material=LoadObject<UMaterialInterface>(nullptr,bFloor?
                        TEXT("/Game/DuelArt/Materials/M_CourtStone.M_CourtStone"):
                        TEXT("/Game/DuelArt/Materials/M_WallStone.M_WallStone"));
                }
                if(!Mesh) continue;
                // Only a missing mesh requires cube scaling. A material-only fallback
                // must still use the baked panel dimensions.
                if(!Mesh->GetName().StartsWith(TEXT("SM_North")))
                    for(FTransform& Instance:Group.Instances)
                        Instance.SetScale3D(Instance.GetScale3D()*Group.MeshSize/100.);
                auto* M=NewObject<UInstancedStaticMeshComponent>(Owner);
                M->SetupAttachment(Owner->GetRootComponent());M->SetMobility(EComponentMobility::Static);
                M->SetStaticMesh(Mesh);M->SetMaterial(0,Material);
                M->SetCollisionEnabled(ECollisionEnabled::NoCollision);M->SetCanEverAffectNavigation(false);
                M->SetAffectDistanceFieldLighting(true);M->SetAffectDynamicIndirectLighting(true);
                M->SetCastShadow(true);M->ComponentTags.Add(TEXT("NorthTowerShell"));
                if(Group.bWetPavers) M->ComponentTags.Add(TEXT("NorthWetPavers"));
                if(bFallback) { M->ComponentTags.Add(TEXT("NorthShellFallback"));++Fallbacks; }
                Owner->AddInstanceComponent(M);M->RegisterComponent();
                M->AddInstances(Group.Instances,false,false,false);
                // Nanite owns instance culling. A second CPU HISM tree adds no
                // benefit to these closed panels and complicates runtime updates.
                M->MarkRenderStateDirty();
                Count+=Group.Instances.Num();
            }
            UE_LOG(LogTemp,Display,TEXT("NorthTower shell %s: %d source boxes, %d ISM batches, %d panels, %d fallback batches"),
                PanelLabel(Kind),OriginalBoxes,Panels.Num(),Count,Fallbacks);
            if(Fallbacks) UE_LOG(LogTemp,Warning,TEXT("NorthTower GI resources incomplete: run SourceAssets/QA/prepare_north_materials.py before render acceptance."));
        }
    };
    struct FWall { int Axis; float Plane; FVector2D Lo,Hi; };
    bool Contains(const FWall& W,float U,float Z) { return U>W.Lo.X && U<W.Hi.X && Z>W.Lo.Y && Z<W.Hi.Y; }
    UBoxComponent* Collision(AActor* Owner,const FVector& C,const FVector& D)
    {
        auto* B=NewObject<UBoxComponent>(Owner);B->SetupAttachment(Owner->GetRootComponent());
        B->SetRelativeLocation(C);B->SetBoxExtent(D*.5);B->SetCollisionProfileName(TEXT("BlockAll"));
        B->SetMobility(EComponentMobility::Static);Owner->AddInstanceComponent(B);B->RegisterComponent();return B;
    }

    void InstallWindowBatch(AActor* Owner, const TArray<FTransform>& Instances, DuelGeometry::EShape Shape,
        UMaterialInterface* Material, const TCHAR* Tag, bool bGlazing = false)
    {
        if (Instances.IsEmpty()) return;
        auto* Batch = NewObject<UInstancedStaticMeshComponent>(Owner);
        Batch->SetupAttachment(Owner->GetRootComponent());
        Batch->SetMobility(EComponentMobility::Static);
        Batch->SetStaticMesh(DuelGeometry::Mesh(Shape));
        Batch->SetMaterial(0, Material);
        Batch->SetCollisionProfileName(TEXT("BlockAll"));
        Batch->SetCollisionEnabled(bGlazing ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
        Batch->SetCanEverAffectNavigation(false);
        Batch->SetCastShadow(!bGlazing);
        Batch->bDisallowNanite = bGlazing;
        Batch->ComponentTags.Add(Tag);
        Owner->AddInstanceComponent(Batch);
        Batch->RegisterComponent();
        Batch->AddInstances(Instances, false, false, false);
    }

    void BuildWindowFrames(AActor* Owner)
    {
        using namespace DuelGeometry;
        TArray<FTransform> PrisonStone, PrisonIron, CourtStone, CourtIron, CourtGlass;
        for (const FPortal& P : Portals())
        {
            if (!P.bWindow || !IsExteriorWindow(P)) continue;
            const FRoom* Room = FindRoom(P.RoomA);
            const FVector Out = WindowOutward(P, *Room);
            const FVector Along = FVector::CrossProduct(FVector::UpVector, Out);
            const FQuat Rotation = Along.Rotation().Quaternion();
            const FVector Centre = P.Center + FVector(0, 0, P.Height * .5);
            const bool bRound = DuelNorthRose::IsRound(P);
            const bool bFinal = P.RoomA == TEXT("FinalCourt");
            const FQuat GlassRotation = FRotationMatrix::MakeFromXY(Along, -FVector::UpVector).ToQuat();
            if (bFinal || bRound)
            {
                FVector Extent(P.Width, 16, P.Height);
                if (FMath::Abs(Out.X) > .5) Swap(Extent.X, Extent.Y);
                Collision(Owner, Centre, Extent)->ComponentTags.Add(TEXT("NorthWindowPane"));
            }
            if (bRound) continue;

            const auto Piece = [&](TArray<FTransform>& Batch, double U, double Depth, double Z, const FVector& Size)
            {
                Batch.Emplace(Rotation, P.Center + Along * U + Out * Depth + FVector(0, 0, Z), Size / 100.);
            };
            double ClearSpan;
            if (bFinal)
            {
                constexpr double RimWidth = 32., RevealDepth = 68.;
                const double Rim = P.Width * .5 + 2.;
                ClearSpan = 2. * Rim - RimWidth;
                for (double Sign : {-1., 1.})
                {
                    Piece(CourtStone, Sign * Rim, 0, P.Height * .5, FVector(RimWidth, RevealDepth, ClearSpan));
                    Piece(CourtStone, 0, 0, P.Height * .5 + Sign * Rim, FVector(2. * Rim + RimWidth, RevealDepth, RimWidth));
                }
                const double GlassScale = (ClearSpan + 2.) / 100.;
                CourtGlass.Emplace(GlassRotation, Centre - Out * 23., FVector(GlassScale, GlassScale, 1));
            }
            else
            {
                // Heavy jambs surround a shallow weathered sill. The iron sits inside the stone reveal.
                const double Jamb = P.RoomA == TEXT("SeaGallery") ? 26. : 38.;
                ClearSpan = P.Width + 6. - Jamb;
                for (double Sign : {-1., 1.})
                    Piece(PrisonStone, Sign * (P.Width * .5 + 3.), 0, P.Height * .5, FVector(Jamb, 76, P.Height));
                Piece(PrisonStone, 0, 0, P.Height + 4., FVector(P.Width + 58., 88, 38));
                Piece(PrisonStone, 0, 8, -9, FVector(P.Width + 84., 102, 28));
                Piece(PrisonStone, 0, 11, 5, FVector(P.Width + 92., 116, 12));
            }

            TArray<FTransform>& Iron = bFinal ? CourtIron : PrisonIron;
            const int32 Pickets = P.Width >= 300.f ? 4 : 2;
            for (int32 I = 1; I <= Pickets; ++I)
                Piece(Iron, ClearSpan * (double(I) / (Pickets + 1) - .5), 4, P.Height * .5, FVector(10, 14, P.Height - 15.));
            for (double Level : {.36, .70})
                Piece(Iron, 0, 4, P.Height * Level, FVector(ClearSpan + 12., 19, 8));
        }
        auto* Stone = ArtMaterial(TEXT("M_NorthDressedStone"));
        auto* Iron = ColorMaterial(Owner, FLinearColor(.075f, .081f, .088f), false, .65f, .78f);
        InstallWindowBatch(Owner, PrisonStone, EShape::Box, Stone, TEXT("NorthPrisonWindowStone"));
        InstallWindowBatch(Owner, PrisonIron, EShape::Box, Iron, TEXT("NorthPrisonWindowIron"));
        InstallWindowBatch(Owner, CourtStone, EShape::Box, Stone, TEXT("NorthFinalSideFrame"));
        InstallWindowBatch(Owner, CourtIron, EShape::Box, Iron, TEXT("NorthFinalSideIron"));
        InstallWindowBatch(Owner, CourtGlass, EShape::Plane, ArtMaterial(TEXT("M_NorthWindowGlass")), TEXT("NorthFinalSideGlass"), true);
    }

    void BuildOpeningCellFurnishings(AActor* Owner)
    {
        using namespace DuelGeometry;
        const FVector Bed(-3170, 1360, 0);
        auto* Timber = ColorMaterial(Owner, FLinearColor(.10f, .058f, .032f), false, .86f);
        auto* Canvas = ColorMaterial(Owner, FLinearColor(.21f, .20f, .16f), false, .94f, 0.f, true);
        auto* Blanket = ColorMaterial(Owner, FLinearColor(.16f, .066f, .065f), false, .96f, 0.f, true);
        USceneComponent* Root = Owner->GetRootComponent();
        Shape(Owner, Root, EShape::Box, Bed + FVector(0, 0, 31), FVector(190, 90, 10), Timber);
        Shape(Owner, Root, EShape::Box, Bed + FVector(0, 0, 42), FVector(178, 82, 13), Canvas);
        Shape(Owner, Root, EShape::Box, Bed + FVector(43, 0, 51), FVector(84, 84, 6), Blanket);
        Shape(Owner, Root, EShape::Ball, Bed + FVector(-58, 0, 50), FVector(44, 65, 14), Canvas);
        for (float End : {-1.f, 1.f})
            for (float Side : {-1.f, 1.f})
                Shape(Owner, Root, EShape::Box, Bed + FVector(End * 85, Side * 39, 16), FVector(10, 9, 32), Timber);
        Collision(Owner, Bed + FVector(0, 0, 26), FVector(190, 90, 52))->ComponentTags.Add(TEXT("NorthStartingCellBed"));
    }
}
void ADuelNorthTower::Build()
{
    BuildOpeningCellFurnishings(this);
    FBatch Floor(EPanel::Floor),Wall(EPanel::Wall);TMap<FString,TArray<FWall>> Groups;
    for(const auto& R:Rooms())
    {
        const FVector C=R.Bounds.GetCenter(),D=R.Bounds.GetSize();
        if(R.bFloor) { const FVector At(C.X,C.Y,R.Bounds.Min.Z-FloorThickness*.5),Size(D.X,D.Y,FloorThickness);Floor.Box(At,Size,UsesWetPavers(R.Floor));Collision(this,At,Size); }
        // The next floor can be only 50 cm above this ceiling. A 50 cm roof
        // made its top coplanar with that floor (visible flickering brown patches).
        // Keep the underside/clear height fixed and bury the roof top in the slab.
        if(R.bRoof) { const FVector At(C.X,C.Y,R.Bounds.Max.Z+RoofThickness*.5),Size(D.X,D.Y,RoofThickness);Wall.Box(At,Size,EPanel::Roof);Collision(this,At,Size); }
        if(!R.bEnclose) continue;
        for(int Axis=0;Axis<2;++Axis) for(int S:{0,1})
        {
            const float P=S?R.Bounds.Max[Axis]:R.Bounds.Min[Axis];
            Groups.FindOrAdd(FString::Printf(TEXT("%d:%d"),Axis,FMath::RoundToInt(P))).Add(
                {Axis,P,FVector2D(R.Bounds.Min[1-Axis],R.Bounds.Min.Z),FVector2D(R.Bounds.Max[1-Axis],R.Bounds.Max.Z)});
        }
    }
    // Partition shared wall planes once. Coincident rooms never create coplanar skins.
    for(auto& Pair:Groups)
    {
        const auto& Rects=Pair.Value;const int Axis=Rects[0].Axis;const float Plane=Rects[0].Plane;
        TArray<float> U,Z;TArray<FWall> Holes;
        for(const auto& R:Rects) { U.AddUnique(R.Lo.X);U.AddUnique(R.Hi.X);Z.AddUnique(R.Lo.Y);Z.AddUnique(R.Hi.Y); }
        for(const auto& P:Portals()) if(FMath::Abs(P.Normal[Axis])>.9 && FMath::Abs(P.Center[Axis]-Plane)<1 &&
            (!P.bWindow || IsExteriorWindow(P)))
        {
            FWall H{Axis,Plane,{P.Center[1-Axis]-P.Width*.5f,P.Center.Z},{P.Center[1-Axis]+P.Width*.5f,P.Center.Z+P.Height}};
            Holes.Add(H);U.AddUnique(H.Lo.X);U.AddUnique(H.Hi.X);Z.AddUnique(H.Lo.Y);Z.AddUnique(H.Hi.Y);
        }
        U.Sort();Z.Sort();
        for(int X=0;X+1<U.Num();++X) for(int Y=0;Y+1<Z.Num();++Y)
        {
            const float MidU=(U[X]+U[X+1])*.5f,MidZ=(Z[Y]+Z[Y+1])*.5f;
            if(!Rects.ContainsByPredicate([&](const FWall& R){return Contains(R,MidU,MidZ);}) ||
                Holes.ContainsByPredicate([&](const FWall& R){return Contains(R,MidU,MidZ);})) continue;
            FVector C=FVector::ZeroVector,D(WallThickness,WallThickness,Z[Y+1]-Z[Y]);C[Axis]=Plane;C[1-Axis]=MidU;C.Z=MidZ;D[1-Axis]=U[X+1]-U[X];
            Wall.Box(C,D);Collision(this,C,D);
        }
    }
    for(const auto& S:Stairs())
    {
        const FVector Dir=(S.End-S.Start).GetSafeNormal2D();const bool bX=FMath::Abs(Dir.X)>.5;
        const bool bWetPavers=UsesWetPavers(S.FromFloor);
        for(int K=0;K<S.StepCount;++K)
        {
            FVector At=FMath::Lerp(S.Start,S.End,(K+.5f)/S.StepCount);At.Z=S.Start.Z+(K+1)*S.Riser()-20;
            FVector D(bX?S.Tread()+.2f:S.Width,bX?S.Width:S.Tread()+.2f,40);
            Floor.Box(At,D,bWetPavers);Collision(this,At,D);
        }
    }
    DuelNorthArchitecture::Build(this,RootComponent);
    auto* WallStone=DuelGeometry::ArtMaterial(TEXT("M_NorthDungeonWall"));
    auto* WetPavers=DuelGeometry::ArtMaterial(TEXT("M_NorthDungeonPavers"));
    auto* DryPavers=DuelGeometry::ArtMaterial(TEXT("M_NorthTowerDryPavers"));
    if(!WallStone || !WetPavers || !DryPavers)
        UE_LOG(LogTemp,Warning,TEXT("Shared tower masonry missing; run SourceAssets/QA/prepare_north_dungeon_materials.py."));
    Floor.Install(this,WallStone,WetPavers,DryPavers);
    Wall.Install(this,WallStone,WetPavers,DryPavers);
    DuelNorthRose::Build(this,RootComponent);
    BuildWindowFrames(this);
    DuelNorthGothic::Build(this,RootComponent,WallStone);
    for(const auto& P:Portals()) if(P.Gate!=EGate::Open)
    {
        FVector D(P.Width,24,P.Height);if(FMath::Abs(P.Normal.X)>.5) Swap(D.X,D.Y);
        auto* Block=Collision(this,P.Center+FVector(0,0,P.Height*.5),D);
        Block->ComponentTags.Add(TEXT("NorthTowerGate"));Gates.Add(P.Id,Block);
        if(DuelNorthDoors::Build(this,Block,P)) continue;
        auto* Visual=NewObject<UStaticMeshComponent>(this);Visual->SetupAttachment(Block);
        Visual->ComponentTags.Add(TEXT("NorthTowerFogDoor"));Visual->ComponentTags.Add(P.Id);
        Visual->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Visual->SetRelativeScale3D(D/100.f);Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Visual->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/DuelArt/Materials/M_FogGate.M_FogGate")));
        Visual->SetCastShadow(false);Visual->SetMobility(EComponentMobility::Static);AddInstanceComponent(Visual);Visual->RegisterComponent();
    }
    MountConfinementNumber(this,RootComponent);
    MountCustodyDetails(this,RootComponent);
    DuelNorthInteractables::Build(this,RootComponent);
    DuelNorthScenery::Build(this,RootComponent);
    DuelNorthLegacyCollision::Build(this,RootComponent);
    DuelNorthExtractionVisual::BuildCallMarker(this,RootComponent);
    DuelNorthLighting::Build(this,RootComponent);
}

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    void NorthRenderProbe(UWorld* World)
    {
        if(!World || !FParse::Param(FCommandLine::Get(),TEXT("DuelNoSave")))return;
        FTimerHandle Timer;
        World->GetTimerManager().SetTimer(Timer,[World]()
        {
            for(TActorIterator<ADuelNorthTower> It(World);It;++It)
            {
                TInlineComponentArray<UInstancedStaticMeshComponent*> Components(*It);
                for(const auto* C:Components) if(C->ComponentHasTag(TEXT("NorthTowerShell")))
                {
                    const auto* Proxy=C->GetSceneProxy();const UStaticMesh* Mesh=C->GetStaticMesh();
                    const auto* Data=Mesh?Mesh->GetRenderData():nullptr;
                    const auto* DF=Data && Data->LODResources.Num()?Data->LODResources[0].DistanceFieldData:nullptr;
                    const auto* Cards=Data && Data->LODResources.Num()?Data->LODResources[0].CardRepresentationData:nullptr;
                    UE_LOG(LogTemp,Display,TEXT("NorthRenderProbe %s instances=%d indirect=%d distancefield=%d proxy=%d lumenVisible=%d supportsDF=%d drawn=%d opaque=%d dfValid=%d cards=%d bounds=%s"),
                        *GetNameSafe(Mesh),C->GetInstanceCount(),C->bAffectDynamicIndirectLighting,C->bAffectDistanceFieldLighting,
                        Proxy!=nullptr,Proxy?Proxy->IsVisibleInLumenScene():false,Proxy?Proxy->SupportsDistanceFieldRepresentation():false,
                        Proxy?Proxy->IsDrawnInGame():false,Proxy?Proxy->IsOpaqueOrMasked():false,DF&&DF->IsValid(),
                        Cards?Cards->MeshCardsBuildData.CardBuildData.Num():0,*C->Bounds.GetBox().GetSize().ToString());
                }
            }
            if(auto* Stats=IConsoleManager::Get().FindConsoleVariable(TEXT("r.LumenScene.DumpStats")))Stats->Set(4,ECVF_SetByConsole);
        },2.f,false);
    }
    FAutoConsoleCommandWithWorld NorthProbeCommand(TEXT("Duel.NorthRenderProbe"),
        TEXT("Report North shell's actual populated render proxies and Lumen cache after warmup."),
        FConsoleCommandWithWorldDelegate::CreateStatic(&NorthRenderProbe));
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthRenderGeometryTest,"ManosabaDuel.NorthTower.RenderGeometry",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthRenderGeometryTest::RunTest(const FString& Parameters)
{
    ADuelNorthTower* Tower=nullptr;
    if(GEngine) for(const FWorldContext& Context:GEngine->GetWorldContexts())
    {
        if(!Context.World() || !Context.World()->IsGameWorld()) continue;
        for(TActorIterator<ADuelNorthTower> It(Context.World());It;++It) { Tower=*It;break; }
        if(Tower) break;
    }
    if(!TestTrue(TEXT("Run in an isolated -DuelNorthTower -DuelNoSave game world"),Tower &&
        FParse::Param(FCommandLine::Get(),TEXT("DuelNorthTower")) && FParse::Param(FCommandLine::Get(),TEXT("DuelNoSave")))) return false;
    TInlineComponentArray<UInstancedStaticMeshComponent*> Components(Tower);
    TSet<const UStaticMesh*> Checked;
    int32 Batches=0,Instances=0,Fallbacks=0,InvalidTransforms=0,WetBatches=0,DryBatches=0;
    float LargestPanel=0,MinScale=100,MaxScale=0;
    for(const auto* Component:Components)
    {
        if(!Component->ComponentHasTag(TEXT("NorthTowerShell"))) continue;
        ++Batches;Instances+=Component->GetInstanceCount();
        Fallbacks+=Component->ComponentHasTag(TEXT("NorthShellFallback"))?1:0;
        TestEqual(TEXT("Visual panels never add collision seams"),Component->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
        const UStaticMesh* Mesh=Component->GetStaticMesh();
        if(!TestNotNull(TEXT("Shell static mesh"),Mesh)) continue;
        const FString Material=GetNameSafe(Component->GetMaterial(0));
        const bool bPavers=Mesh->GetName().StartsWith(TEXT("SM_NorthFloor"));
        const bool bWet=Component->ComponentHasTag(TEXT("NorthWetPavers"));
        WetBatches+=bPavers&&bWet?1:0;
        DryBatches+=bPavers&&!bWet?1:0;
        const TCHAR* Expected=bPavers?(bWet?TEXT("M_NorthDungeonPavers"):TEXT("M_NorthTowerDryPavers")):
            TEXT("M_NorthDungeonWall");
        TestEqual(TEXT("The shared finish matches its wall or paving panel"),Material,FString(Expected));
        if(FApp::CanEverRender())
        {
            const auto* Proxy=Component->GetSceneProxy();
            TestTrue(*FString::Printf(TEXT("%s runtime proxy is admitted to Lumen"),*Mesh->GetName()),
                Proxy && Proxy->IsVisibleInLumenScene());
        }
        if(!Checked.Contains(Mesh))
        {
            Checked.Add(Mesh);
            const FStaticMeshRenderData* Data=Mesh->GetRenderData();
            TestTrue(*FString::Printf(TEXT("%s Nanite enabled"),*Mesh->GetName()),Mesh->IsNaniteEnabled());
            TestTrue(*FString::Printf(TEXT("%s actual Nanite render data"),*Mesh->GetName()),Mesh->HasValidNaniteData());
            if(TestTrue(*FString::Printf(TEXT("%s built LOD render data"),*Mesh->GetName()),Data && Data->LODResources.Num()>0))
            {
                const auto* DF=Data->LODResources[0].DistanceFieldData;
                const auto* Cards=Data->LODResources[0].CardRepresentationData;
                TestTrue(*FString::Printf(TEXT("%s populated distance-field mip data"),*Mesh->GetName()),DF && DF->IsValid());
                TestTrue(*FString::Printf(TEXT("%s nonempty Lumen cards"),*Mesh->GetName()),Cards && Cards->MeshCardsBuildData.CardBuildData.Num()>0);
                AddInfo(FString::Printf(TEXT("%s DF=%s cards=%d"),*Mesh->GetName(),DF?*DF->Mips[0].IndirectionDimensions.ToString():TEXT("missing"),
                    Cards?Cards->MeshCardsBuildData.CardBuildData.Num():0));
            }
        }
        for(int32 Index=0;Index<Component->GetInstanceCount();++Index)
        {
            FTransform Transform;
            if(!Component->GetInstanceTransform(Index,Transform,false) || Transform.ContainsNaN()) { ++InvalidTransforms;continue; }
            const FVector Scale=Transform.GetScale3D().GetAbs();
            const FVector Size=Mesh->GetBounds().BoxExtent*2*Scale;
            LargestPanel=FMath::Max(LargestPanel,static_cast<float>(Size.GetMax()));
            MinScale=FMath::Min(MinScale,static_cast<float>(Scale.GetMin()));
            MaxScale=FMath::Max(MaxScale,static_cast<float>(Scale.GetMax()));
        }
    }
    TestTrue(TEXT("Shared shell uses the 108-mesh library and both kinds of paving finish"),
        WetBatches>0 && DryBatches>0 && Checked.Num()<=108 && Batches<=Checked.Num()+FMath::Min(WetBatches,DryBatches));
    TestTrue(TEXT("Shell instance budget <= 16000 closed panels"),Instances>0 && Instances<=16000);
    TestEqual(TEXT("No preview fallback resources"),Fallbacks,0);
    TestEqual(TEXT("All panel transforms finite"),InvalidTransforms,0);
    TestTrue(TEXT("Every individual panel <= 4 m on each axis"),LargestPanel<=400.1f);
    TestTrue(TEXT("Baked templates avoid extreme non-uniform instance scaling"),MinScale>=.5f && MaxScale<=1.42f);
    AddInfo(FString::Printf(TEXT("North shell: %d batches/%d instances/%d unique meshes, max panel %.2f cm, scale %.3f..%.3f. DF/card data availability is not a rendered GI or performance measurement."),
        Batches,Instances,Checked.Num(),LargestPanel,MinScale,MaxScale));
    return !HasAnyErrors();
}
#endif
