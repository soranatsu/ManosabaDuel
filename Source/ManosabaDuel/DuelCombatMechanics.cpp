#include "DuelCombatMechanics.h"
#include "DuelArena.h"
#include "DuelGame.h"
#include "DuelGeometry.h"
#include "DuelNorthTower.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SceneView.h"
#include "RHI.h"

namespace
{
    const FVector Outwards[] = {FVector(0,-1,0), FVector(0,1,0), FVector(-1,0,0), FVector(1,0,0)};
}

void ADuelArena::TickMirrorCourt(ADuelFighter* Boss, float DeltaSeconds)
{
    const auto* Mode = GetWorld()->GetAuthGameMode<ADuelMode>();
    const auto* Player = Mode ? Mode->GetPlayerFighter() : nullptr;
    if (!Player || !Boss || Boss->BossEncounter != 1 || Boss->BossPhase < 2 ||
        !Mode->UsesNorthTower() || Mode->Outcome() != EDuelOutcome::Fighting)
    {
        if (MirrorPhase > 0) ClearMirrorCourt();
        return;
    }
    const bool bNew = MirrorWalls.IsEmpty();
    if (bNew || MirrorPressureAge < DuelCombatMechanics::MirrorFormTime)
    {
        // Form around both combatants. Before the glass sets, a runner can carry
        // its edge farther out; a settled mirror never pushes anyone sideways.
        const FVector Origin = CourtOrigin();
        const FBox2D Room = CourtBounds(0.);
        const FVector2D A(Player->GetActorLocation()-Origin), B(Boss->GetActorLocation()-Origin);
        FBox2D Inside(FVector2D::Min(A,B)-FVector2D(620.,740.), FVector2D::Max(A,B)+FVector2D(620.,740.));
        if (!bNew)
        {
            const FVector2D Mid(MirrorCenter-Origin), Half(MirrorHalfLength,MirrorHalfWidth);
            Inside += Mid-Half;
            Inside += Mid+Half;
        }
        Inside.Min = Room.GetClosestPointTo(Inside.Min);
        Inside.Max = Room.GetClosestPointTo(Inside.Max);
        MirrorCenter = Origin + FVector(Inside.GetCenter(),0);
        MirrorHalfLength = Inside.GetSize().X*.5f;
        MirrorHalfWidth = Inside.GetSize().Y*.5f;
    }
    if (bNew)
    {
        auto* Material = DuelGeometry::ArtMaterial(TEXT("M_HiroFogMirror"));
        for (const FVector& Out : Outwards)
        {
            auto* Root = DuelGeometry::Joint(this, SceneRoot, FVector::ZeroVector);
            MirrorWalls.Add(Root);
            Root->ComponentTags.Add(TEXT("MirrorPressureWall"));
            UMaterialInstanceDynamic* Glass = UMaterialInstanceDynamic::Create(Material,this);
            MirrorGlass.Add(Glass);
            const FVector Tangent = FVector::CrossProduct(FVector::UpVector,-Out);
            auto* Pane = DuelGeometry::Shape(this,Root,DuelGeometry::EShape::Plane,FVector(0,0,50),
                FVector(100,100,100),Glass,FRotationMatrix::MakeFromXZ(Tangent,-Out).Rotator());
            Pane->SetCastShadow(false);
            Pane->SetVisibleInRayTracing(false);
            if (Glass && FApp::CanEverRender() && !IsRunningCommandlet() && !GUsingNullRHI)
            {
                auto* RT = NewObject<UTextureRenderTarget2D>(this);
                RT->ClearColor = FLinearColor(0,0,0,1);
                const int32 Width=FDuelGraphicsSettings::LowMemoryProfile()?512:1024;
                RT->InitCustomFormat(Width,Width*9/16,PF_FloatRGBA,true);
                RT->UpdateResourceImmediate(true);
                auto* Capture = NewObject<USceneCaptureComponent2D>(this);
                Capture->SetupAttachment(SceneRoot);
                Capture->bCaptureEveryFrame = false;
                Capture->bCaptureOnMovement = false;
                Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
                Capture->ShowOnlyActorComponents(Boss,true);
                Capture->TextureTarget = RT;
                Capture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
                Capture->ShowFlags.SetAtmosphere(false);
                Capture->ShowFlags.SetFog(false);
                Capture->ShowFlags.SetSkyLighting(false);
                Capture->ShowFlags.SetDynamicShadows(false);
                Capture->ShowFlags.SetPostProcessing(false);
                Capture->ShowFlags.SetMotionBlur(false);
                AddInstanceComponent(Capture);
                Capture->RegisterComponent();
                MirrorCameras.Add(Capture);
                Glass->SetTextureParameterValue(TEXT("Reflection"),RT);
            }
        }
        Sound(EDuelSound::Charge, MirrorCenter);
        if (auto* Tower=const_cast<ADuelNorthTower*>(Mode->NorthTower())) Tower->ShowMechanicHint(1);
    }
    MirrorPhase=Boss->BossPhase;
    MirrorPressureAge+=FMath::Max(0.f,DeltaSeconds);
    for (int32 I=0; I<MirrorWalls.Num(); ++I)
    {
        const bool AlongX=I<2;
        const double Progress=FMath::Clamp((MirrorPressureAge-I*.075f)/(DuelCombatMechanics::MirrorFormTime-.225f),0.f,1.f);
        const double Lift=FMath::SmoothStep(0.,1.,Progress);
        MirrorWalls[I]->SetWorldLocation(MirrorCenter+Outwards[I]*(AlongX?MirrorHalfWidth:MirrorHalfLength));
        const double Height=FMath::Max(.001,Lift)*DuelCombatMechanics::MirrorHeight;
        auto* Pane=MirrorWalls[I]->GetChildComponent(0);
        Pane->SetRelativeLocation(FVector(0,0,Height*.5));
        Pane->SetRelativeScale3D(FVector((AlongX?MirrorHalfLength:MirrorHalfWidth)/50.,Height/100.,1.));
        if (MirrorGlass[I] && MirrorPressureAge <= DuelCombatMechanics::MirrorFormTime + DeltaSeconds)
            MirrorGlass[I]->SetScalarParameterValue(TEXT("Formation"),Progress);
    }
    if (MirrorPressureAge >= NextMirrorVolley && IsValid(Boss->Target) && Player->IsAlive())
    {
        const FLinearColor Silver(.55f,1.08f,1.8f);
        if (!bMirrorShotArmed)
        {
            const int32 Face=MirrorVolleyFace++ % 4;
            const FVector N=Outwards[Face];
            const FVector Plane=MirrorWalls[Face]->GetComponentLocation();
            MirrorShotOrigin=Boss->GetActorLocation();
            MirrorShotOrigin.Z=MirrorCenter.Z+94.f;
            FVector Goal=Player->GetActorLocation();
            Goal.Z=MirrorShotOrigin.Z;
            const FVector VirtualTarget=Goal-2.*FVector::DotProduct(Goal-Plane,N)*N;
            MirrorShotDirection=(VirtualTarget-MirrorShotOrigin).GetSafeNormal();
            const double Incidence=FVector::DotProduct(MirrorShotDirection,N);
            if (Incidence > .001)
            {
                const FVector Contact=MirrorShotOrigin+MirrorShotDirection*
                    (FVector::DotProduct(Plane-MirrorShotOrigin,N)/Incidence);
                Burst(MirrorShotOrigin,Silver,12,180.f);
                for (const FVector End : {MirrorShotOrigin,Goal})
                {
                    FVector Mid=(End+Contact)*.5; Mid.Z=MirrorCenter.Z+7;
                    Lane(Mid,(End-Contact).GetSafeNormal2D(),FVector::Dist2D(End,Contact)*.5f,34.f,
                        Silver,1.1f,false,true,false,EDuelMark::MirrorSeam);
                }
                bMirrorShotArmed=true;
            }
            NextMirrorVolley=MirrorPressureAge+1.1f;
        }
        else
        {
            LaunchThread(Boss->Target,MirrorShotOrigin,MirrorShotDirection,MirrorPhase==3?65.f:49.f,9.f,
                Silver,1460.f,33.f,4.f,true,0.f,true,0.f,EDuelMark::Prism);
            Sound(EDuelSound::FrostRecall,MirrorShotOrigin);
            bMirrorShotArmed=false;
            NextMirrorVolley=MirrorPressureAge+(MirrorPhase==3?4.8f:7.2f);
        }
    }
}

void ADuelArena::UpdateMirrorImages(float DeltaSeconds)
{
    auto* PC=GetWorld()->GetFirstPlayerController();
    if (!PC || !PC->PlayerCameraManager) return;
    MirrorCaptureClock += DeltaSeconds;
    if (MirrorCaptureClock < 1.f/50.f) return;
    MirrorCaptureClock=0.f;
    const auto& View=PC->PlayerCameraManager->GetCameraCacheView();
    const FRotationMatrix Basis(View.Rotation);
    FSceneViewProjectionData Projection;
    const auto* Local=PC->GetLocalPlayer();
    const bool HaveProjection=Local && Local->ViewportClient && Local->ViewportClient->Viewport &&
        Local->GetProjectionData(Local->ViewportClient->Viewport,Projection);
    for (int32 Try=0; Try<MirrorCameras.Num(); ++Try)
    {
        const int32 I=MirrorCaptureIndex;
        MirrorCaptureIndex=(MirrorCaptureIndex+1) % MirrorCameras.Num();
        const FVector N=Outwards[I];
        const FVector Plane=MirrorWalls[I]->GetComponentLocation();
        auto* Capture=MirrorCameras[I].Get();
        // Only a plane into which the viewer can look needs a fresh portrait.
        if (FVector::DotProduct(Plane-View.Location,N) <= 0. ||
            FVector::DotProduct(View.Rotation.Vector(),N) < -.28) continue;
        const FVector ReflectedEye=View.Location-2.*FVector::DotProduct(View.Location-Plane,N)*N;
        Capture->SetWorldLocationAndRotation(ReflectedEye, FRotationMatrix::MakeFromXZ(
            Basis.GetUnitAxis(EAxis::X).MirrorByVector(N),Basis.GetUnitAxis(EAxis::Z).MirrorByVector(N)).Rotator());
        Capture->FOVAngle=View.FOV;
        Capture->bUseCustomProjectionMatrix=HaveProjection;
        if(HaveProjection) Capture->CustomProjectionMatrix=Projection.ProjectionMatrix;
        const double Px=HaveProjection?Projection.ProjectionMatrix.M[0][0]:1./FMath::Tan(FMath::DegreesToRadians(View.FOV*.5));
        const double Py=HaveProjection?Projection.ProjectionMatrix.M[1][1]:Px*16./9.;
        auto* Glass=MirrorGlass[I].Get();
        Glass->SetVectorParameterValue(TEXT("Eye"),FLinearColor(View.Location));
        Glass->SetVectorParameterValue(TEXT("ViewX"),FLinearColor(Basis.GetUnitAxis(EAxis::Y)*Px));
        Glass->SetVectorParameterValue(TEXT("ViewY"),FLinearColor(Basis.GetUnitAxis(EAxis::Z)*Py));
        Glass->SetVectorParameterValue(TEXT("ViewZ"),FLinearColor(Basis.GetUnitAxis(EAxis::X)));
        Capture->CaptureScene();
        break;
    }
}

FBox2D ADuelArena::MirrorCourtBounds(const FBox2D& Bounds, float Clearance) const
{
    if (!MirrorPhase || MirrorPressureAge<DuelCombatMechanics::MirrorFormTime) return Bounds;
    const FVector2D Mid(MirrorCenter-CourtOrigin()), Half(MirrorHalfLength-Clearance,MirrorHalfWidth-Clearance);
    return FBox2D(FVector2D::Max(Bounds.Min,Mid-Half),FVector2D::Min(Bounds.Max,Mid+Half));
}

void ADuelArena::ClearMirrorCourt()
{
    for (USceneCaptureComponent2D* Capture:MirrorCameras)
        if (Capture) Capture->DestroyComponent();
    MirrorCameras.Reset();
    MirrorGlass.Reset();
    for (USceneComponent* Wall : MirrorWalls)
    {
        if (!Wall) continue;
        TArray<USceneComponent*> Descendants;
        Wall->GetChildrenComponents(true, Descendants);
        for (int32 I = Descendants.Num()-1; I>=0; --I) Descendants[I]->DestroyComponent();
        Wall->DestroyComponent();
    }
    MirrorWalls.Reset();
    MirrorHalfWidth=MirrorHalfLength=MirrorPressureAge=MirrorCaptureClock=0.f;
    MirrorPhase=MirrorCaptureIndex=0;
    MirrorVolleyFace=0;
    bMirrorShotArmed=false;
    NextMirrorVolley=4.f;
}
