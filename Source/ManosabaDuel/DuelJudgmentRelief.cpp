#include "DuelJudgmentRelief.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"

void DuelJudgmentRelief::Attach(AActor* Owner, USceneComponent* Root, int32 Index)
{
    static const TCHAR* const Meshes[] = {
        TEXT("/Game/DuelArt/Judgment/SM_JudgmentEye.SM_JudgmentEye"),
        TEXT("/Game/DuelArt/Judgment/SM_JudgmentScales.SM_JudgmentScales"),
        TEXT("/Game/DuelArt/Judgment/SM_JudgmentCrown.SM_JudgmentCrown")
    };
    if (!Owner || !Root || Index < 0 || Index >= UE_ARRAY_COUNT(Meshes)) return;
    auto* Mesh = LoadObject<UStaticMesh>(nullptr, Meshes[Index]);
    auto* Enamel = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/DuelArt/Judgment/M_JudgmentEnamel.M_JudgmentEnamel"));
    if (!ensure(Mesh && Enamel)) return;
    auto* Face = NewObject<UStaticMeshComponent>(Owner);
    Face->SetupAttachment(Root);
    Face->SetMobility(EComponentMobility::Movable);
    Face->SetStaticMesh(Mesh);
    Face->SetMaterial(0, Enamel);
    Face->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Face->SetCanEverAffectNavigation(false);
    Face->SetCastShadow(false);
    Face->bAffectDynamicIndirectLighting = false;
    // The Front axis of the glTF import can differ between legacy and Interchange importers.
    if (Mesh->GetBounds().BoxExtent.Y < Mesh->GetBounds().BoxExtent.X)
        Face->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));
    Face->ComponentTags.Add(TEXT("JudgmentEnamel347"));
    Owner->AddInstanceComponent(Face);
    Face->RegisterComponent();
}
