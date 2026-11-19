#pragma once

#include "CoreMinimal.h"

class AActor;
class UMaterialInterface;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

namespace DuelGeometry
{
    enum class EShape : uint8 { Box, Ball, Tube, Cone, Plane };

    UStaticMesh* Mesh(EShape Shape);
    UMaterialInterface* ArtMaterial(const TCHAR* Name);
    UMaterialInterface* ColorMaterial(UObject* Outer, const FLinearColor& Color, bool bGlow = false,
        float Roughness = .69f, float Metallic = 0.f, bool bCloth = false);
    UStaticMeshComponent* Shape(AActor* Owner, USceneComponent* Parent, EShape ShapeType,
        const FVector& Position, const FVector& Size, UMaterialInterface* Material,
        const FRotator& Rotation = FRotator::ZeroRotator);
    UStaticMeshComponent* Rod(AActor* Owner, USceneComponent* Parent, const FVector& A,
        const FVector& B, float Radius, UMaterialInterface* Material);
    USceneComponent* Joint(AActor* Owner, USceneComponent* Parent, const FVector& Position);
}
