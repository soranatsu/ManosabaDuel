#pragma once
#include "CoreMinimal.h"
#include "DuelNorthProgress.h"

enum class EDuelWeapon : uint8;
class AActor;
class UProceduralMeshComponent;
class USceneComponent;

namespace DuelNorthFinds
{
    enum class EFixture : uint8
    {
        RationTable, CollationDesk, Firebox, ToolRoll, SaltCoffer, SluiceShelf,
        LetterTube, SewingCabinet, MirrorCase, WitnessBench, VotiveAltar, WaitingBench
    };

    inline constexpr double Reach = 165.;

    struct FFind
    {
        FName Id;
        const TCHAR* Title;
        const TCHAR* Action;
        // Floor-level centre. Local +Y is the side used by the player.
        FVector Position;
        float Yaw;
        FVector Size;
        FVector LocalItem;
        EFixture Fixture;
        EDuelNorthFloor Floor;
        EDuelWeapon Weapon;
        EDuelArt Spell;
        const TCHAR* Discovery;
        bool bPuzzle = false;

        FRotator Rotation() const { return FRotator(0, Yaw, 0); }
        FVector Front() const { return Rotation().RotateVector(FVector::RightVector); }
        FVector ItemPosition() const { return Position + Rotation().RotateVector(LocalItem); }
    };
    TConstArrayView<FFind> All();
    void BuildProps(AActor* Owner, USceneComponent* Parent, TConstArrayView<TObjectPtr<USceneComponent>> Weapons,
        TMap<FName, TObjectPtr<UProceduralMeshComponent>>& Cues, TObjectPtr<USceneComponent>& ChippedBowl);
    void ShowRationBowl(USceneComponent* Bowl, bool bTurned);
    int32 IndexOf(FName Id);
    uint32 AllCacheBits();
    bool HasSpell(uint32 Bits, EDuelArt Spell);
    bool HasWeapon(uint32 Bits, EDuelWeapon Weapon);
    FString Rewards(const FFind& Find);
    const TCHAR* Question(int32 Puzzle, int32 Round);
    const TCHAR* Answer(int32 Puzzle, int32 Round, int32 Choice);
    bool Correct(int32 Puzzle, int32 Round, int32 Choice);
}
