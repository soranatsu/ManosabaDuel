#include "DuelNorthDoors.h"

#include "DuelGeometry.h"
#include "DuelNorthLayout.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "DuelNorthTower.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#endif

namespace DuelNorthDoors
{
namespace
{
    UStaticMeshComponent* Part(AActor* Owner, USceneComponent* Blocker, FName Id,
        UStaticMesh* Mesh, const FTransform& Relative, UMaterialInterface* Material = nullptr)
    {
        auto* Visual = NewObject<UStaticMeshComponent>(Owner);
        Visual->SetupAttachment(Blocker);
        Visual->SetStaticMesh(Mesh);
        Visual->SetRelativeTransform(Relative);
        Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Visual->SetCanEverAffectNavigation(false);
        Visual->SetMobility(EComponentMobility::Static);
        Visual->SetCastShadow(true);
        Visual->bCastHiddenShadow = false;
        Visual->ComponentTags.Add(TEXT("NorthTowerSolidDoor"));
        Visual->ComponentTags.Add(Id);
        if (Material) Visual->SetMaterial(0, Material);
        Owner->AddInstanceComponent(Visual);
        Visual->RegisterComponent();
        return Visual;
    }
}

bool Build(AActor* Owner, USceneComponent* Blocker, const DuelNorthLayout::FPortal& Portal)
{
    const bool bShortcut = Portal.Id == TEXT("ReturnLower") || Portal.Id == TEXT("SeaEastInner");
    const bool bGrille = Portal.Id == TEXT("ChapelDoor") || Portal.Id == TEXT("CellDoor") ||
        Portal.Id == TEXT("DungeonAfterDoor") || bShortcut;
    const bool bHeavy = Portal.Id == TEXT("FinalStairDoor");
    const bool bExit = Portal.Id == TEXT("TowerExit");
    if (!bGrille && !bHeavy && !bExit) return false;
    if (!Owner || !Blocker) return false;

    // Both CC0 scans are already cooked. Their glTF sources are X-wide/Y-up;
    // Unreal imports them as X-wide/Z-up. Keep all original material slots/UVs.
    const TCHAR* Asset = bGrille ? TEXT("large_iron_gate") : TEXT("large_castle_door");
    const FString Path = FString::Printf(TEXT("/Game/DuelArt/Scans/%s/%s.%s"), Asset, Asset, Asset);
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
    if (!Mesh || Mesh->GetBoundingBox().GetSize().GetMin() <= .01f)
    {
        UE_LOG(LogTemp, Error, TEXT("North solid door %s missing usable scan %s"), *Portal.Id.ToString(), *Path);
        return false;
    }
    const FBox Bounds = Mesh->GetBoundingBox();
    const FVector Size = Bounds.GetSize();
    const int32 WidthAxis = Size.X >= Size.Y ? 0 : 1;
    const int32 DepthAxis = 1 - WidthAxis;
    // The physical slab is 24 cm deep. Keep scan and hinges within that slab:
    // there are no visible surfaces extending into the free movement space.
    FVector Scale = FVector::OneVector;
    Scale[WidthAxis] = (Portal.Width + 2.f) / Size[WidthAxis];
    Scale[DepthAxis] = (bGrille ? 10.f : 16.f) / Size[DepthAxis];
    Scale.Z = (Portal.Height + 2.f) / Size.Z;
    const FVector Across(-Portal.Normal.Y, Portal.Normal.X, 0);
    const FRotator Facing(0, Across.Rotation().Yaw - (WidthAxis ? 90.f : 0.f), 0);
    // Blocker origin is the aperture centre. Compensate the actual imported pivot,
    // not an assumed base-origin: the scan slightly overlaps each stone jamb/sill.
    const FVector Offset = -Facing.RotateVector(Bounds.GetCenter() * Scale);
    auto* Scan = Part(Owner, Blocker, Portal.Id, Mesh, FTransform(Facing, Offset, Scale));
    if (bShortcut) Scan->ComponentTags.Add(TEXT("NorthTowerShortcutDoor"));

    if (!bGrille)
    {
        // The assembled scan preserves slightly ajar leaves (11.04 / -4.25 deg).
        // A dark, rough timber backing closes the narrow sight gaps without
        // pretending the imported frame can animate independently of its leaves.
        // It is hidden behind the PBR scan and stays inside the original blocker.
        UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
        UMaterialInterface* Timber = DuelGeometry::ColorMaterial(Owner, FLinearColor(.055f, .032f, .017f), false, .92f);
        if (Cube && Timber) Part(Owner, Blocker, Portal.Id, Cube,
            FTransform(Facing, Portal.Normal * 9.f, FVector(Portal.Width + 2.f, 1.5f, Portal.Height + 2.f) / 100.f), Timber);
    }

    if (bHeavy)
    {
        // Additional wrought-iron straps distinguish the heavy stair door from
        // the smaller timber exit. Reuse the existing PBR iron; no new lighting.
        auto* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/DuelArt/Materials/M_ForgedSteel.M_ForgedSteel"));
        UMaterialInstanceDynamic* Iron = Base ? UMaterialInstanceDynamic::Create(Base, Owner) : nullptr;
        if (Iron)
        {
            Iron->SetVectorParameterValue(TEXT("Color"), FLinearColor(.09f, .075f, .06f));
            Iron->SetScalarParameterValue(TEXT("Roughness"), .74f);
            Iron->SetScalarParameterValue(TEXT("Metallic"), .72f);
        }
        UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
        if (Cube && Iron) for (float Front : {-1.f, 1.f}) for (float Z : {-.30f, .27f})
        {
            // Separate bands per leaf preserve the centre seam and real hinge ends.
            for (float Leaf : {-1.f, 1.f})
            {
                const FVector At = Across * (Leaf * Portal.Width * .25f) +
                    Portal.Normal * (Front * 9.f) + FVector(0, 0, Portal.Height * Z);
                Part(Owner, Blocker, Portal.Id, Cube,
                    FTransform(Facing, At, FVector(Portal.Width * .44f, 2.f, 8.f) / 100.f), Iron);
            }
        }
    }
    UE_LOG(LogTemp, Display, TEXT("North solid door %s: %s fitted %.0fx%.0f cm, original UV/PBR, original 24cm blocker"),
        *Portal.Id.ToString(), Asset, Portal.Width, Portal.Height);
    return true;
}
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthGateVisualsTest, "ManosabaDuel.NorthTower.GateVisuals",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthGateVisualsTest::RunTest(const FString& Parameters)
{
    ADuelNorthTower* Tower = nullptr;
    if (GEngine) for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        if (!Context.World() || !Context.World()->IsGameWorld()) continue;
        for (TActorIterator<ADuelNorthTower> It(Context.World()); It; ++It) { Tower = *It; break; }
        if (Tower) break;
    }
    if (!TestTrue(TEXT("Isolated NorthTower -DuelNoSave world"), Tower &&
        FParse::Param(FCommandLine::Get(), TEXT("DuelNorthTower")) && FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")))) return false;
    TInlineComponentArray<UStaticMeshComponent*> Visuals(Tower);
    const FName StoryIds[] = {TEXT("CellDoor"), TEXT("DungeonAfterDoor"), TEXT("ChapelDoor"), TEXT("FinalStairDoor"), TEXT("TowerExit")};
    const FName ShortcutIds[] = {TEXT("ReturnLower"), TEXT("SeaEastInner")};
    TArray<FName> Ids;
    Ids.Append(StoryIds, UE_ARRAY_COUNT(StoryIds));
    Ids.Append(ShortcutIds, UE_ARRAY_COUNT(ShortcutIds));
    TSet<FName> SeenStories, SeenShortcuts;
    for (const FName Id : Ids)
    {
        TArray<UStaticMeshComponent*> Parts;
        UBoxComponent* Blocker = nullptr;
        for (auto* V : Visuals) if (V->ComponentHasTag(TEXT("NorthTowerSolidDoor")) && V->ComponentHasTag(Id))
        {
            Parts.Add(V);
            auto* Parent = Cast<UBoxComponent>(V->GetAttachParent());
            TestTrue(TEXT("Every solid part attaches directly to its gate blocker"), Parent && Parent->ComponentHasTag(TEXT("NorthTowerGate")));
            if (Parent) { TestTrue(TEXT("All parts share one blocker"), !Blocker || Blocker == Parent); Blocker = Parent; }
            TestEqual(TEXT("Door decoration does not change collision"), V->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
            TestTrue(TEXT("Physical door casts no hidden shadow after unlocking"), !V->bCastHiddenShadow);
            for (int32 Slot = 0; Slot < V->GetNumMaterials(); ++Slot)
                TestTrue(TEXT("Physical door has no fog material"), V->GetMaterial(Slot) && V->GetMaterial(Slot)->GetName() != TEXT("M_FogGate"));
        }
        if (!TestTrue(*FString::Printf(TEXT("%s physical parts exist"), *Id.ToString()), Parts.Num() > 0 && Blocker)) continue;
        const bool bShortcut = Id == TEXT("ReturnLower") || Id == TEXT("SeaEastInner");
        if (bShortcut)
        {
            SeenShortcuts.Add(Id);
            TestEqual(*FString::Printf(TEXT("%s uses one complete iron grille"), *Id.ToString()), Parts.Num(), 1);
            for (auto* V : Parts)
            {
                TestTrue(TEXT("Shortcut grilles have their own visual category"), V->ComponentHasTag(TEXT("NorthTowerShortcutDoor")));
                TestEqual(TEXT("Shortcut reuses the cooked iron gate scan"), GetNameSafe(V->GetStaticMesh()), FString(TEXT("large_iron_gate")));
            }
        }
        else
        {
            SeenStories.Add(Id);
            for (auto* V : Parts) TestFalse(TEXT("Story locks are not reclassified as shortcuts"), V->ComponentHasTag(TEXT("NorthTowerShortcutDoor")));
            if (Id == TEXT("DungeonAfterDoor"))
                TestEqual(TEXT("B1 battle exit visibly closes with iron"), GetNameSafe(Parts[0]->GetStaticMesh()),
                    FString(TEXT("large_iron_gate")));
        }
        const bool bWasOpen = Tower->IsGateOpen(Id);
        const FVector Extent = Blocker->GetUnscaledBoxExtent();
        for (auto* V : Parts)
        {
            if (!V->GetStaticMesh()) continue;
            const FBox Bound = V->GetStaticMesh()->GetBoundingBox().TransformBy(V->GetRelativeTransform());
            // All current North gates face X. Their visual depth is below 24 cm;
            // a 1 cm overlap at the jambs/sill prevents bright seam lines.
            TestTrue(TEXT("Door art stays inside original depth and portal seam allowance"),
                Bound.Min.X >= -Extent.X && Bound.Max.X <= Extent.X &&
                Bound.Min.Y >= -Extent.Y - 1.1f && Bound.Max.Y <= Extent.Y + 1.1f &&
                Bound.Min.Z >= -Extent.Z - 1.1f && Bound.Max.Z <= Extent.Z + 1.1f);
        }
        Tower->SetGateOpen(Id, true);
        for (auto* V : Parts) TestTrue(TEXT("Opening hides all door parts recursively"), V->bHiddenInGame);
        TestEqual(TEXT("Opened gate is passable"), Blocker->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
        Tower->SetGateOpen(Id, false);
        for (auto* V : Parts) TestFalse(TEXT("Closing restores all door parts recursively"), V->bHiddenInGame);
        TestEqual(TEXT("Closing restores original collision"), Blocker->GetCollisionEnabled(), ECollisionEnabled::QueryAndPhysics);
        TestTrue(TEXT("Visibility change preserves collision dimensions"), Extent.Equals(Blocker->GetUnscaledBoxExtent()));
        Tower->SetGateOpen(Id, bWasOpen);
    }
    TestEqual(TEXT("The cell, B1 battle exit and three keyed doors have physical art"), SeenStories.Num(), 5);
    TestEqual(TEXT("Two additional unique physical shortcut grilles"), SeenShortcuts.Num(), 2);
    // Fog gates carry their portal ID as a tag, independently of the keyed doors.
    for (const FName Id : {FName(TEXT("DungeonGate")), FName(TEXT("MirrorGate")), FName(TEXT("FinalGate"))})
    {
        int32 Count = 0;
        for (const auto* V : Visuals) if (V->ComponentHasTag(TEXT("NorthTowerFogDoor")) && V->ComponentHasTag(Id))
        { ++Count; TestEqual(TEXT("Combat entrance retains its fog material"), GetNameSafe(V->GetMaterial(0)), FString(TEXT("M_FogGate"))); }
        TestEqual(*FString::Printf(TEXT("%s retains one fog surface"), *Id.ToString()), Count, 1);
    }
    return !HasAnyErrors();
}
#endif
