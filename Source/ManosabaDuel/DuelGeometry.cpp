#include "DuelGeometry.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

UStaticMesh* DuelGeometry::Mesh(EShape Shape)
{
    const TCHAR* Name = TEXT("Cube");
    switch (Shape)
    {
    case EShape::Ball: Name = TEXT("Sphere"); break;
    case EShape::Tube: Name = TEXT("Cylinder"); break;
    case EShape::Cone: Name = TEXT("Cone"); break;
    case EShape::Plane: Name = TEXT("Plane"); break;
    default: break;
    }
    const FString Path = FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name);
    return LoadObject<UStaticMesh>(nullptr, *Path);
}

UMaterialInterface* DuelGeometry::ArtMaterial(const TCHAR* Name)
{
    const FString Path = FString::Printf(TEXT("/Game/DuelArt/Materials/%s.%s"), Name, Name);
    if (UMaterialInterface* Loaded = FindObject<UMaterialInterface>(nullptr, *Path))
    {
        return Loaded;
    }
    return LoadObject<UMaterialInterface>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
}

UMaterialInterface* DuelGeometry::ColorMaterial(UObject* Outer, const FLinearColor& Color, bool bGlow,
    float Roughness, float Metallic, bool bCloth)
{
    const TCHAR* Surface = bGlow ? TEXT("M_ArcLight") : (bCloth ? TEXT("M_WovenCloth")
        : (Metallic >= .60f ? TEXT("M_ForgedSteel") : TEXT("M_Figure")));
    UMaterialInterface* Parent = ArtMaterial(Surface);
    if (!Parent && !bGlow)
    {
        Parent = ArtMaterial(TEXT("M_Figure"));
    }
    if (!Parent)
    {
        Parent = LoadObject<UMaterialInterface>(nullptr,
            TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    }
    if (!Parent)
    {
        return UMaterial::GetDefaultMaterial(MD_Surface);
    }
    UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Parent, Outer);
    Instance->SetVectorParameterValue(TEXT("Color"), Color);
    Instance->SetVectorParameterValue(TEXT("BaseColor"), Color);
    if (!bGlow)
    {
        Instance->SetScalarParameterValue(TEXT("Roughness"), Roughness);
        Instance->SetScalarParameterValue(TEXT("Metallic"), Metallic);
    }
    return Instance;
}

UStaticMeshComponent* DuelGeometry::Shape(AActor* Owner, USceneComponent* Parent, EShape ShapeType,
    const FVector& Position, const FVector& Size, UMaterialInterface* Material, const FRotator& Rotation)
{
    UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Owner);
    Component->SetMobility(EComponentMobility::Movable);
    Component->SetupAttachment(Parent);
    Component->SetStaticMesh(Mesh(ShapeType));
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component->SetRelativeLocationAndRotation(Position, Rotation);
    Component->SetRelativeScale3D(Size / 100.0);
    Component->SetMaterial(0, Material);
    Owner->AddInstanceComponent(Component);
    Component->RegisterComponent();
    return Component;
}

UStaticMeshComponent* DuelGeometry::Rod(AActor* Owner, USceneComponent* Parent, const FVector& A,
    const FVector& B, float Radius, UMaterialInterface* Material)
{
    const FVector Axis = B - A;
    return Shape(Owner, Parent, EShape::Tube, (A + B) * 0.5,
        FVector(Radius * 2, Radius * 2, Axis.Size() + 1), Material, FRotationMatrix::MakeFromZ(Axis).Rotator());
}

USceneComponent* DuelGeometry::Joint(AActor* Owner, USceneComponent* Parent, const FVector& Position)
{
    USceneComponent* Component = NewObject<USceneComponent>(Owner);
    Component->SetupAttachment(Parent);
    Component->SetRelativeLocation(Position);
    Owner->AddInstanceComponent(Component);
    Component->RegisterComponent();
    return Component;
}
