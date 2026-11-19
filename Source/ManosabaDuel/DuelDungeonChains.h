#pragma once
#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
class ADuelFighter;
class USceneComponent;

struct FDuelDungeonChainsState
{
    enum class EStage : uint8 { Waiting, Raising, Active, Counter, Cooldown };
    TWeakObjectPtr<ADuelFighter> Boss;
    TStaticArray<TWeakObjectPtr<USceneComponent>,2> Chains;
    TWeakObjectPtr<USceneComponent> Emitter,Guide;
    TArray<TWeakObjectPtr<USceneComponent>> Bolts;
    TStaticArray<FVector,2> Points;
    FVector Source=FVector::ZeroVector,Aim=FVector::ZeroVector;
    EStage Stage=EStage::Waiting;
    float Clock=0.f,Deadline=0.f,ActiveUntil=0.f,NextCycle=5.f,NextVolley=0.f,FireAt=0.f,FeedbackUntil=0.f;
    uint8 Broken=0;
    bool bAiming=false,bPendingCounter=false;
    int32 Cycle=0,Shots=0;
    FString Feedback;
    int32 Progress() const {return int32((Broken&1)!=0)+int32((Broken&2)!=0);}
};
