#pragma once

#include "Async/TaskGraphInterfaces.h"
#include "DuelSpellMarks.h"
#include "DuelDungeonChains.h"
#include "GameFramework/Actor.h"
#include "DuelArena.generated.h"

class ADuelFighter;
class UBoxComponent;
class UAudioComponent;
class USoundAttenuation;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class USpotLightComponent;
class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UProceduralMeshComponent;
class UStaticMeshComponent;
class UTexture2D;
class USceneCaptureComponent2D;

enum class EDuelSound : uint8
{
    Cut, Impact, HeavyImpact, Block, Roll, Flask, Charge, Pulse, Victory, Defeat, RainAtlas, Footstep, VerdictBell,
    RelicPickup, DoorLatch, Checkpoint, FogAdmission, ParryChime, SpellReady, FrostRecall
};
enum class EDuelCharacter : uint8;
enum class EDuelMove : uint8;
enum class EDuelVoice : uint8 { Attack, Art, Hurt, Intro, Witch, Defeat, Loss, Count };
enum class EDuelLight : uint8 { Mote, Spark, Feather, Star, Filament, Seal, Ring, Shaft, Lane, Bloom, Mist };

UCLASS()
class MANOSABADUEL_API ADuelArena final : public AActor
{
    GENERATED_BODY()

public:
    ADuelArena();
    virtual void Tick(float DeltaSeconds) override;

    void Ring(const FVector& At, float Radius, const FLinearColor& Color,
        float Life, float EndScale = 1.f, float Width = 5.f);
    void Slash(const FVector& At, float Yaw, float Radius, bool bBoss,
        const FLinearColor& Tint = FLinearColor::Transparent, EDuelMark Mark = EDuelMark::Default);
    void Thrust(const FVector& At, const FVector& Axis, float Reach, const FLinearColor& Tint, bool bSkill,
        EDuelMark Mark = EDuelMark::Default);
    void ClawWake(const FVector& At, const FVector& Axis, float HalfWidth, int32 Phase);
    USceneComponent* WingSweep(const FVector& At, const FVector& Axis, float Radius, float HalfArc,
        const FLinearColor& Tint, float Life, bool bStrike);
    void Sigil(const FVector& At, float Radius, const FLinearColor& Tint, float Windup,
        EDuelMark Mark = EDuelMark::Default, const FVector& Axis = FVector::ForwardVector);
    USceneComponent* DrawChargeSeal(const FVector& At, const FVector& Axis, float Radius, const FLinearColor& Tint,
        float Windup, float Hold, EDuelMark Mark);
    void Lane(const FVector& At, const FVector& Axis, float HalfLength, float HalfWidth,
        const FLinearColor& Tint, float Life, bool bStrike, bool bQuiet = false, bool bFeathered = false,
        EDuelMark Mark = EDuelMark::Default);
    void Rainfall(const TArray<FVector>& Marks, float Radius, const FLinearColor& Tint, bool bTribunal = false,
        EDuelMark Mark = EDuelMark::Default, const FVector& Axis = FVector::ForwardVector);
    void InwardWave(const FVector& At, float Outer, float Inner, float Life, const FLinearColor& Tint,
        EDuelMark Mark = EDuelMark::Default);
    void BoneWave(const FVector& At, float Inner, float Outer, float Life, bool bWarning, bool bSecond);
    void TailSweepWarning(const FVector& At, float Yaw, float Reach, float Arc, float Life);
    void TailSweepTrail(const FVector& At, const FVector& Axis, float Reach);
    void GlassVerdict(const FVector& Safe, float Radius, float Life, bool bStrike);
    USceneComponent* SnareThread(const FVector& At, const FVector& Axis, float Length, float Width, float Life);
    void PenLattice(const FVector& At, const FVector& Axis, float HalfLength, float Gap, float Width, float Life, bool bStrike);
    void Halo(const FVector& At, float Inner, float Outer, const FLinearColor& Tint, float Life, bool bStrike,
        EDuelMark Mark = EDuelMark::Default);
    void Detonate(const FVector& At, float Radius, const FLinearColor& Tint, float Life,
        EDuelMark Mark = EDuelMark::Default, const FVector& Axis = FVector::ForwardVector);
    void BossBurst(const FVector& At, float Radius, const FLinearColor& Tint, float Strength);
    void StormFront(const FVector& At, float Radius, const FLinearColor& Tint, float Life,
        EDuelMark Mark = EDuelMark::Default);
    void EmberRecoil(const FVector& At, const FVector& Axis, float Radius, const FLinearColor& Tint,
        float Life, bool bStrike);
    void SableSweep(const FVector& At, const FVector& Axis, float Radius, const FLinearColor& Tint, float HalfArc,
        EDuelMark Mark = EDuelMark::Default);
    void Burst(const FVector& At, const FLinearColor& Color, int32 Count, float Speed, bool bPetals = false);
    void WitchTransformation(const FVector& At, EDuelCharacter Character, int32 Phase, float Duration, bool bRelease);
    void Pillar(const FVector& At, float Radius, bool bStorm, EDuelMark Mark = EDuelMark::Default);
    void Flash(const FVector& At, const FLinearColor& Color, float Intensity, float Life, float Radius);
    void TrackBlade(const FVector& Hilt, const FVector& Tip, const FLinearColor& Color,
        bool bHeavy, bool bNewSwing, bool bOffering = false);
    void BladeImpact(const FVector& At, const FVector& Axis, const FLinearColor& Tint, bool bHeavy);
    void BladeStep(const FVector& At, const FVector& Axis, bool bHeavy);
    void SpellContact(const FVector& At, const FVector& Axis, const FLinearColor& Tint, bool bHeavy);
    void CastAccent(EDuelMark Mark, const FVector& At, const FVector& Axis, const FLinearColor& Tint, bool bReady);
    void FrostReturn(const FVector& At, float Inner, float Outer, const FLinearColor& Tint);
    void ShieldContact(const FVector& At, const FVector& Toward, bool bParried);
    void PrecacheSigilArtwork();
    UStaticMeshComponent* DrawSigilArtwork(EDuelMark Mark, const FVector& At, const FVector& Axis, const FVector2D& Extent,
        const FLinearColor& Tint, float Life, bool bStrike, bool bLane, float Inner, float EndScale, float HalfArc);
    void LaunchCrimson(ADuelFighter* From, ADuelFighter* Target, const FVector& Direction, float Damage, float Posture);
    USceneComponent* StageIcicle(const FVector& At, const FVector& Axis, const FLinearColor& Tint, float Life);
    USceneComponent* StageBossSignature(const FVector& At, const FVector& Axis, EDuelMove Move, const FLinearColor& Tint,
        float Life);
    USceneComponent* TraceBossSignature(const FVector& From, const FVector& To, const FLinearColor& Tint, float Life);
    USceneComponent* LaunchThread(ADuelFighter* Target, const FVector& At, const FVector& Axis, float Damage, float Posture,
        const FLinearColor& Tint, float Speed, float Radius, float Life, bool bParryable = false,
        float TurnDegreesPerSecond = 0.f, bool bRecoil = true, float HomingDegreesPerSecond = 0.f,
        EDuelMark Mark = EDuelMark::Default);
    void Sound(EDuelSound Cue, const FVector& At);
    void PlayFootfall(const FVector& At, float Effort, bool bLocalPlayer = false);
    bool PlayVoice(EDuelCharacter Character, EDuelVoice Cue, bool bBoss, const FVector& At);
    void StopVoice(float Fade = .08f);
    bool PlayNorthDialogueVoice(const FString& Recording);
    bool TryBossLine(EDuelCharacter Character, EDuelVoice Cue, const FVector& At);
    void StartScore(EDuelCharacter Character);
    void IntensifyScore(int32 Phase);
    void StopScore(float Fade = .58f);
    const TCHAR* VoiceCaption() const;
    float VoiceCaptionOpacity() const;
    double CombatVoiceEndTime() const { return VoiceUntil; }
    bool VoiceIsBoss() const { return bVoiceIsBoss; }
    bool VoiceIsStory() const { return bVoiceIsStory; }
    void OpenPrisonGate();
    void SetBattlePhase(int32 Phase, bool bHiro = false);
    void SetAppeal(bool bActive);
    void SetYukiCourt(bool bActive);
    void SetYukiRouteOpen(bool bOpen);
    void SetDawn(bool bDawn);
    void SetNorthEndingMusic(bool bPlaying);
    void SetNorthExplorationScore(FName Cue);
    void MixNorthExplorationScore(bool bSpeaking, float Fade);
    void RefreshAudioLevels();
    FVector CourtOrigin() const;
    FBox2D CourtBounds(double NorthClearance = 0.) const;
    void SetNorthOrigin(const FVector& Origin) { NorthOrigin = Origin; bNorthOrigin = true; }
    FVector GroundMark(const FVector& World) const;
    void SetGateClosed(bool bClosed);
    void RitualTrace(const FVector& At, bool bFinal);
    void SetArchiveGateOpen(bool bOpen);
    void SetCombatCamera(bool bActive);
    void ClearCombatEffects();
    void TickMirrorCourt(ADuelFighter* Boss, float DeltaSeconds);
    void TickCourtInteractions(ADuelFighter* Boss, float DeltaSeconds);
    void TickDungeonChains(ADuelFighter* Boss, float DeltaSeconds);
    void ClearDungeonChains();
    bool HitDungeonChains(ADuelFighter& Caster, const FVector& From, const FVector& To, float Radius);
    FString DungeonChainsPrompt() const;
    bool JudgmentProtects(const ADuelFighter& Fighter) const;
    void ClearCourtInteractions();
    bool TryCourtInteraction(ADuelFighter& Player);
    bool HitCourtSeal(ADuelFighter& Attacker, const FVector& From, const FVector& To, float Radius, bool bProjectile);
    bool CourtSealAimPoint(FVector& Point) const;
    FString CourtInteractionPrompt() const;
    void ClearMirrorCourt();
    void UpdateMirrorImages(float DeltaSeconds);
    FBox2D MirrorCourtBounds(const FBox2D& Bounds, float Clearance) const;
    void SetFurnaceCourt(bool bActive);
    void ApplyCastleState(uint8 Seals, uint8 Shortcuts, bool bEastOpen, bool bFinalRecord);
    void SetCastleFog(int32 Wing, bool bClosed);

protected:
    virtual void BeginPlay() override;

private:
    friend class FDuelNorthFastTravelTest;
    friend class FDuelMatchSmoke;
    friend class FDuelBossDefenseQA;
    friend class FDuelCombatMechanicsQA;
    friend class FDuelCourtInteractionsQA;
    friend class FDuelJudgment347QA;
    friend struct FDuelJudgmentVisualFixture;
    friend class FDuelDungeonChainsQA;
    friend struct FDuelDungeonChainsVisualFixture;
    friend struct FDuelNorthVisualFixture;
    friend class FDuelNorthAudioTest;
    friend class ADuelNorthTower;

    struct FVoiceClip
    {
        TArray<uint8> PCM;
        int32 SampleRate = 0;
        const TCHAR* Subtitle = nullptr;
        const TCHAR* FollowupSubtitle = nullptr;
        float FollowupAt = 0.f;
    };
    static constexpr int32 VoiceCharacters = 3;
    FVoiceClip Voices[VoiceCharacters][static_cast<uint8>(EDuelVoice::Count)];
    FVoiceClip NorthVoiceAlternates[VoiceCharacters][static_cast<uint8>(EDuelVoice::Count)];
    bool bNextNorthVoiceAlternate[VoiceCharacters][static_cast<uint8>(EDuelVoice::Count)] = {};
    UPROPERTY() TObjectPtr<USoundAttenuation> VoiceAttenuation;
    UPROPERTY() TObjectPtr<USoundAttenuation> EffectsAttenuation;
    TArray<TArray<uint8>> StepTakes;
    UPROPERTY() TObjectPtr<USoundAttenuation> StepAttenuation;
    FRandomStream StepVariation{34601};
    int32 LastStepTake = INDEX_NONE;
    void LoadFootfalls();
    UPROPERTY() TObjectPtr<UAudioComponent> RainBed;
    float ActiveVoiceGain = 1.f;
    float BossScoreGain = .65f;
    float WitchScoreGain = .3f;
    static constexpr float EndingMusicGain = .245f;
    UPROPERTY() TObjectPtr<UAudioComponent> BossScore;
    UPROPERTY() TObjectPtr<UAudioComponent> WitchScore;
    int32 ScorePhase = 1;
    bool bNorthWitchTexture = false;
    bool bRainMuted = false;
    TWeakObjectPtr<UAudioComponent> ActiveVoice;
    TWeakObjectPtr<UAudioComponent> EndingVoice;
    const TCHAR* Caption = nullptr;
    const TCHAR* FollowupCaption = nullptr;
    double FollowupCaptionAt = 0.0;
    double CaptionUntil = 0.0;
    double VoiceUntil = 0.0;
    double NextVoiceAt = 0.0;
    double NextCharacterVoiceAt[VoiceCharacters] = {};
    bool bVoiceIsBoss = false;
    bool bVoiceIsStory = false;
    bool bVoicePaused = false;
    void LoadVoices();
    bool StartVoice(EDuelCharacter Character, EDuelVoice Cue, bool bBoss, const FVector& At);
    bool CanPlayVoice(EDuelCharacter Character, EDuelVoice Cue, double Now) const;
    void EndVoice(float Fade);
    void TickVoice();
    void UpdateVoiceMix(bool bDuck);
    void SetRainMuted(bool bMuted, float Fade);
    void PauseVoice(bool bPaused);

    struct FEffect
    {
        TWeakObjectPtr<USceneComponent> Component;
        TWeakObjectPtr<UMaterialInstanceDynamic> Material;
        FVector StartScale = FVector::OneVector;
        FVector Velocity = FVector::ZeroVector;
        FVector ScaleMultiplier = FVector::OneVector;
        float Age = 0.f;
        float Life = 1.f;
        float LightIntensity = 0.f;
        float Gravity = 0.f;
        float Spin = 0.f;
        float OpacityBase = 1.f;
        float Breath = 0.f;
        bool bWarning = false;
        FLinearColor MaterialColor = FLinearColor::Transparent;
    };

    struct FParticle
    {
        FVector At;
        FVector Velocity;
        FVector Axis;
        FVector2D Size;
        FLinearColor Tint;
        EDuelLight Shape;
        float Age = 0.f;
        float Life;
        float Gravity;
        float Spin;
        bool bOffering = false;
    };

    struct FBladeSample
    {
        FVector Hilt;
        FVector Tip;
        FLinearColor Tint;
        float Age = 0.f;
        bool bHeavy = false;
        bool bStartsSwing = false;
        bool bOffering = false;
    };

    struct FCrimsonOrb
    {
        TWeakObjectPtr<ADuelFighter> Caster;
        TWeakObjectPtr<USceneComponent> Visual;
        TWeakObjectPtr<ADuelFighter> Target;
        FVector Velocity = FVector::ZeroVector;
        float Age = 0.f;
        float Damage = 0.f;
        float Posture = 0.f;
        float Radius = 106.f;
        float Life = .94f;
        FLinearColor Tint = FLinearColor::Red;
        float EmissionDelay = 0.f;
        float TurnDegreesPerSecond = 0.f;
        float HomingDegreesPerSecond = 0.f;
        EDuelMark Mark = EDuelMark::Default;
        bool bParryable = false;
        bool bRecoil = true;
        int32 MirrorBounces = 0;
    };

    UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Rain;
    UPROPERTY() TObjectPtr<UDirectionalLightComponent> Moon;
    UPROPERTY() TObjectPtr<UExponentialHeightFogComponent> CourtFog;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> FogGate;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> AppealFog;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> YukiFog;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> FurnaceFog;
    UPROPERTY() TArray<TObjectPtr<UBoxComponent>> CastleFogBlocks;
    UPROPERTY() TArray<TObjectPtr<UBoxComponent>> CastleShortcuts;
    UPROPERTY() TArray<TObjectPtr<USceneComponent>> CastleGrilles;
    UPROPERTY() TArray<TObjectPtr<USceneComponent>> VerdictLights;
    UPROPERTY() TObjectPtr<UBoxComponent> EastBarrier;
    UPROPERTY() TObjectPtr<USceneComponent> EastGrille;
    UPROPERTY() TObjectPtr<UBoxComponent> DawnBarrier;
    UPROPERTY() TObjectPtr<USceneComponent> DawnGrille;
    UPROPERTY() TObjectPtr<USceneComponent> WaterSheet;
    UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> EscortLanterns;
    UPROPERTY() TObjectPtr<UPointLightComponent> FurnaceFire;
    UPROPERTY() TArray<TObjectPtr<USpotLightComponent>> GardenSunbeams;
    bool bFurnaceCourt = false;
    bool bCastleInterior = false;
    bool bNorthOrigin = false;
    FVector NorthOrigin = FVector::ZeroVector;
    TWeakObjectPtr<UAudioComponent> NorthEndingMusic;
    TWeakObjectPtr<UAudioComponent> ExplorationMusic;
    FName ExplorationMusicCue;
    UPROPERTY() TObjectPtr<UBoxComponent> YukiRoute;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> AppealGrille;
    UPROPERTY() TObjectPtr<UBoxComponent> PrisonGateBlocker;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> PrisonGate;
    UPROPERTY() TObjectPtr<USceneComponent> WarrantStand;
    FVector PrisonGateBase = FVector::ZeroVector;
    float PrisonGateLift = 0.f;
    bool bPrisonGateOpen = false;
    void ShowWarrant(bool bVisible);
    friend class ADuelMode;
    UPROPERTY() TArray<TObjectPtr<UBoxComponent>> CombatCameraWalls;
    TArray<FVector> CombatCameraWallCenters;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> ArchiveGate;
    FVector ArchiveGateBase = FVector::ZeroVector;
    FVector AppealGrilleBase = FVector::ZeroVector;
    float ArchiveGateLift = 0.f;
    bool bArchiveGateOpen = false;
    bool bAppeal = false;
    bool bYukiCourt = false;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> ParticleMesh;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> MistMesh;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> BladeMesh;
    UPROPERTY() TArray<TObjectPtr<UTexture2D>> SigilSheets;
    struct FEffectMeshBuffer
    {
        TArray<FVector> Positions;
        TArray<int32> Indices;
        TArray<FVector2D> UVs;
        TArray<FLinearColor> Colors;
        void Upload(UProceduralMeshComponent& Mesh);
        void Reset();
    };
    FEffectMeshBuffer ParticleBuffer;
    FEffectMeshBuffer MistBuffer;
    FEffectMeshBuffer BladeBuffer;
    int32 BattlePhase = 1;
    TArray<FVector> RainPositions;
    TArray<FTransform> RainTransforms;
    TArray<FEffect> Effects;
    TArray<FCrimsonOrb> Orbs;
    UPROPERTY() TArray<TObjectPtr<USceneComponent>> MirrorWalls;
    FVector MirrorCenter = FVector::ZeroVector;
    float MirrorHalfWidth = 0.f;
    UPROPERTY() TArray<TObjectPtr<USceneCaptureComponent2D>> MirrorCameras;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> MirrorGlass;
    float MirrorCaptureClock = 0.f;
    int32 MirrorCaptureIndex = 0;
    float NextMirrorVolley = 4.f;
    int32 MirrorVolleyFace = 0;
    bool bMirrorShotArmed = false;
    FVector MirrorShotOrigin = FVector::ZeroVector;
    FVector MirrorShotDirection = FVector::ZeroVector;
    float MirrorHalfLength = 0.f;
    float MirrorPressureAge = 0.f;
    int32 MirrorPhase = 0;
    enum class ECourtStage : uint8 { Waiting, Warning, Open, Retaliation, Feedback };
    ECourtStage CourtStage = ECourtStage::Waiting;
    FDuelDungeonChainsState DungeonChains;
    void BuildDungeonChains();
    int32 CourtEncounter = INDEX_NONE;
    TWeakObjectPtr<ADuelFighter> CourtBoss;
    float CourtClock = 0.f, NextCourtCycle = 5.f, CourtDeadline = 0.f, CourtActiveDeadline = 0.f;
    float CourtPulseClock = 0.f;
    int32 CourtProgress = 0, CourtCycle = 0;
    uint32 JudgmentSeed = 0;
    bool bJudgmentWard = false;
    TWeakObjectPtr<USceneComponent> JudgmentBarrier;
    bool PlaceJudgmentSeals();
    void BuildJudgmentSeals();
    bool bCourtTimeout = false;
    TStaticArray<FVector, 3> CourtPoints;
    TStaticArray<int32, 3> CourtOrder = {0, 2, 1};
    TArray<TPair<FVector, FVector>> CourtStrikes;
    UPROPERTY() TArray<TObjectPtr<USceneComponent>> CourtObjects;
    FString CourtFeedback;
    float CourtFeedbackUntil = 0.f;
    TWeakObjectPtr<ADuelFighter> LastCourtMelee;
    uint32 LastCourtMeleeSerial = MAX_uint32;
    void BuildCourtObjects();
    void ClearCourtObjects();
    void BeginCourtRetaliation(bool bTimeout);
    void CompleteCourtCounter();
    TArray<FParticle> Particles;
    TArray<FBladeSample> BladeSamples;
    struct FSoundPCM
    {
        TArray<int16> Samples;
        float Gain = 1.f;
    };
    TMap<EDuelSound, FSoundPCM> CombatSoundPCM;
    UPROPERTY() TObjectPtr<UPointLightComponent> RitualLight;
    float RitualParticleDelay = 0.f;
    bool bRitualWasActive = false;
    FRandomStream Random{704512};

    void BuildCourt();
    void BuildCastle();
    void BuildLight();
    void BuildRain();
    void TickEffects(float DeltaSeconds);
    void TickParticles(float DeltaSeconds);
    void TickBlade(float DeltaSeconds);
    void TickRitualBlade(const ADuelFighter* Player, float DeltaSeconds);
    void TickOrbs(float DeltaSeconds);
    void TickRain(float DeltaSeconds);
    void Animate(USceneComponent* Component, float Life, const FVector& EndScale,
        const FVector& Velocity = FVector::ZeroVector, float Gravity = 0.f, float Spin = 0.f, bool bWarning = false);
    UMaterialInstanceDynamic* LightMaterial(EDuelLight Shape, const FLinearColor& Tint,
        float Gain, float SoftDepth, float Repeat = 1.f);
    UStaticMeshComponent* LightPlane(const FVector& At, const FVector2D& Size, const FRotator& Rotation,
        EDuelLight Shape, const FLinearColor& Tint, float Gain, float SoftDepth, float Repeat = 1.f);
    void Emit(EDuelLight Shape, const FVector& At, const FVector& Velocity, const FVector2D& Size,
        const FLinearColor& Tint, float Life, float Gravity = 0.f,
        const FVector& Axis = FVector::ZeroVector, float Spin = 0.f, bool bOffering = false, float Delay = 0.f);
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FrostFacet;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ShadeFacet;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FrostHalo;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ShadeHalo;
    bool bSpellMaterialsWarmed = false;
    UMaterialInstanceDynamic* SpellFacet(bool bShadow);
    UMaterialInstanceDynamic* SpellHalo(bool bShadow);
    UProceduralMeshComponent* LightMesh(UMaterialInterface* Material);
    void GroundWarning(const FVector& At, const FVector& Axis, const FVector2D& Extent,
        const FLinearColor& Tint, float Life, float EndScale = 1.f, bool bLane = false, bool bEdgeOnly = false);
    void TraceSigil(EDuelMark Mark, const FVector& At, const FVector& Axis, const FVector2D& Extent,
        const FLinearColor& Tint, float Life, bool bStrike);
    USceneComponent* DrawSpellMark(EDuelMark Mark, const FVector& At, const FVector& Axis,
        const FVector2D& Extent, const FLinearColor& Tint, float Life, bool bStrike, bool bLane = false,
        float Inner = 0.f, float EndScale = 1.f, float Spin = 0.f, float HalfArc = 180.f);
    void CombatAtmosphere(const FVector& At, const FVector& Axis, const FVector2D& Extent,
        const FLinearColor& Tint, float Life, bool bStrike, bool bLane, float Inner = 0.f);
    UProceduralMeshComponent* Ribbon(const FVector& At, float Radius, float Width,
        float StartAngle, float EndAngle, float Rise, UMaterialInterface* Material);
};
