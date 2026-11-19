#include "DuelNorthNarrative.h"

#include "Components/PointLightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "DuelArena.h"
#include "DuelGame.h"
#include "DuelGeometry.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "RHI.h"

namespace
{
    constexpr int32 PortraitWidth = 384;
    constexpr int32 PortraitHeight = 448;
    constexpr float Lens = 34.f;
    const FVector PortraitOrigin(0, 0, -12000);
    const FName HeadBone(TEXT("DuelHead"));
    const FName MouthBone(TEXT("DuelMouth"));

    const TCHAR* CharacterFolder(EDuelCharacter Character)
    {
        return Character == EDuelCharacter::Yuki ? TEXT("Yuki") : Character == EDuelCharacter::Ema ? TEXT("Ema") : TEXT("Hiro");
    }

    const TCHAR* ExpressionFile(EDuelNorthExpression Expression, EDuelCharacter Character)
    {
        switch (Expression)
        {
        case EDuelNorthExpression::Angry: return TEXT("Angry.png");
        case EDuelNorthExpression::Urgent: return TEXT("Urgent.png");
        case EDuelNorthExpression::Concern: return TEXT("Concern.png");
        case EDuelNorthExpression::Resolve: return TEXT("Resolve.png");
        case EDuelNorthExpression::Soft: return TEXT("Soft.png");
        case EDuelNorthExpression::Startled: return TEXT("Startled.png");
        case EDuelNorthExpression::Smile: return TEXT("Smile.png");
        default: return Character == EDuelCharacter::Yuki ? TEXT("Neutral.png") : TEXT("Smile.png");
        }
    }

    UTexture2D* LoadLocalPortrait(const FString& File, const FString& SharedHiroNeutral = FString())
    {
        constexpr int64 MaxBytes = 12 * 1024 * 1024;
        constexpr uint32 MaxDimension = 2048;
        const int64 Size = IFileManager::Get().FileSize(*File);
        if (Size < 24 || Size > MaxBytes) return nullptr;
        TArray<uint8> Bytes;
        constexpr uint8 PngHeader[] = {137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
        if (!FFileHelper::LoadFileToArray(Bytes, *File) || Bytes.Num() != Size ||
            FMemory::Memcmp(Bytes.GetData(), PngHeader, sizeof(PngHeader))) return nullptr;
        const auto Dimension = [&](int32 At)
        {
            return (uint32(Bytes[At]) << 24) | (uint32(Bytes[At + 1]) << 16) |
                (uint32(Bytes[At + 2]) << 8) | Bytes[At + 3];
        };
        if (!Dimension(16) || Dimension(16) > MaxDimension || !Dimension(20) || Dimension(20) > MaxDimension)
            return nullptr;
        // The private 3.4.0 install put the current urgent face in Hiro's Neutral slot.
        if (!SharedHiroNeutral.IsEmpty() && Size == 1426983 &&
            FSHA1::HashBuffer(Bytes.GetData(), Size).ToString().Equals(TEXT("8EA63A6DD6732123480209BD38B4681825328BD9"), ESearchCase::IgnoreCase))
        {
            if (UTexture2D* Replacement = LoadLocalPortrait(SharedHiroNeutral)) return Replacement;
        }
        return FImageUtils::ImportBufferAsTexture2D(Bytes);
    }

    UTexture2D* LoadPortrait(EDuelCharacter Character, const TCHAR* Filename)
    {
        const FString RelativePath = FPaths::Combine(TEXT("NorthDialogue/Portraits"), CharacterFolder(Character), Filename);
        const TCHAR* UserDirectory = FPlatformProcess::UserSettingsDir();
        const FString SharedPath = *UserDirectory ? FPaths::Combine(UserDirectory, TEXT("ManosabaDuel"), RelativePath) : FString();
        const bool bHiroNeutral = Character == EDuelCharacter::Hiro && FCString::Strcmp(Filename, TEXT("Neutral.png")) == 0;
        if (UTexture2D* Portrait = LoadLocalPortrait(FPaths::ProjectSavedDir() / RelativePath, bHiroNeutral ? SharedPath : FString()))
            return Portrait;
        if (!SharedPath.IsEmpty())
            if (UTexture2D* Portrait = LoadLocalPortrait(SharedPath)) return Portrait;
        return LoadLocalPortrait(FPaths::ProjectContentDir() / TEXT("DuelPortraits") / CharacterFolder(Character) / Filename);
    }

    UTexture2D* LoadSmilePortrait(EDuelCharacter Character)
    {
        if (UTexture2D* Smile = LoadPortrait(Character, TEXT("Smile.png"))) return Smile;
        // Yuki has no everyday smile default; old Ema/Hiro Neutral overrides cannot replace the canonical smile.
        return Character == EDuelCharacter::Yuki ? LoadPortrait(Character, TEXT("Neutral.png")) : nullptr;
    }

    struct FPortraitPose
    {
        float Pitch;
        float Yaw;
        float Roll;
        float Stress;
    };

    FPortraitPose PoseFor(EDuelNorthExpression Expression)
    {
        switch (Expression)
        {
        case EDuelNorthExpression::Angry: return {-5.f, 4.f, -3.f, 1.18f};
        case EDuelNorthExpression::Urgent: return {3.f, -3.f, -4.f, 1.22f};
        case EDuelNorthExpression::Concern: return {-7.f, -5.f, 6.f, .72f};
        case EDuelNorthExpression::Resolve: return {-2.f, 1.f, -1.f, 1.10f};
        case EDuelNorthExpression::Soft: return {-2.f, -6.f, -6.f, .68f};
        case EDuelNorthExpression::Startled: return {6.f, 5.f, 2.f, 1.34f};
        default: return {0.f, 0.f, 0.f, .9f};
        }
    }
}

UTexture* ADuelNorthNarrative::DialoguePortrait() const
{
    if (!Playback.Active()) return nullptr;
    if (LocalPortrait) return LocalPortrait;
    return PortraitMesh && PortraitMesh->IsVisible() ? PortraitTarget.Get() : nullptr;
}

void ADuelNorthNarrative::PreloadWitchPortrait(EDuelCharacter Character)
{
    if (!FApp::CanEverRender() || GUsingNullRHI ||
        (Character != EDuelCharacter::Ema && Character != EDuelCharacter::Hiro)) return;
    const uint8 Key = static_cast<uint8>(Character);
    if (!WitchPortraits.Contains(Key))
        WitchPortraits.Add(Key, LoadPortrait(Character, TEXT("Witch.png")));
}

UTexture* ADuelNorthNarrative::WitchPortrait(EDuelCharacter Character) const
{
    return WitchPortraits.FindRef(static_cast<uint8>(Character)).Get();
}

void ADuelNorthNarrative::PreloadThoughtPortrait(EDuelCharacter Character)
{
    if (!FApp::CanEverRender() || GUsingNullRHI) return;
    const uint8 Key = static_cast<uint8>(Character);
    if (ThoughtPortraits.Contains(Key)) return;
    UTexture2D* Portrait = LoadSmilePortrait(Character);
    ThoughtPortraits.Add(Key, Portrait);
}

UTexture* ADuelNorthNarrative::ThoughtPortrait(EDuelCharacter Character) const
{
    return ThoughtPortraits.FindRef(static_cast<uint8>(Character)).Get();
}

void ADuelNorthNarrative::PresentDialogueLine()
{
    DialogueLineStartedAt = GetWorld()->GetTimeSeconds();
    NextPortraitCaptureAt = 0.0;
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("NorthDialogue");
    if (ArenaRef) ArenaRef->PlayNorthDialogueVoice(Directory / TEXT("Voice/PrisonLetters") / Playback.VoiceKey());
    if (!FApp::CanEverRender() || GUsingNullRHI) return;

    const auto Subtitle = Playback.Subtitle();
    const bool bEma = Subtitle.Character == EDuelCharacter::Ema;
    const bool bYuki = Subtitle.Character == EDuelCharacter::Yuki;
    LocalPortrait = nullptr;
    if (Subtitle.bWitchPortrait)
    {
        PreloadWitchPortrait(Subtitle.Character);
        LocalPortrait = WitchPortraits.FindRef(static_cast<uint8>(Subtitle.Character));
    }
    const TCHAR* File = ExpressionFile(Subtitle.Expression, Subtitle.Character);
    if (!LocalPortrait && FCString::Strcmp(File, TEXT("Smile.png")) == 0)
    {
        PreloadThoughtPortrait(Subtitle.Character);
        LocalPortrait = ThoughtPortraits.FindRef(static_cast<uint8>(Subtitle.Character));
    }
    if (!LocalPortrait) LocalPortrait = LoadPortrait(Subtitle.Character, File);
    if (!LocalPortrait)
    {
        PreloadThoughtPortrait(Subtitle.Character);
        LocalPortrait = ThoughtPortraits.FindRef(static_cast<uint8>(Subtitle.Character));
    }
    if (LocalPortrait)
    {
        if (PortraitMesh) PortraitMesh->SetVisibility(false);
        return;
    }

    const TCHAR* AssetName = bYuki ? TEXT("SK_YukiQuad") : bEma ? TEXT("SK_EmaStand") : TEXT("SK_HiroStand");
    const FString AssetPath = FString::Printf(TEXT("/Game/DuelArt/Characters/%s/%s.%s"), AssetName, AssetName, AssetName);
    USkeletalMesh* Asset = LoadObject<USkeletalMesh>(nullptr, *AssetPath);
    if (!Asset)
    {
        if (PortraitMesh) PortraitMesh->SetVisibility(false);
        return;
    }

    if (!PortraitMesh)
    {
        PortraitMesh = NewObject<UPoseableMeshComponent>(this, TEXT("NorthPortraitFigure"));
        PortraitMesh->SetupAttachment(GetRootComponent());
        PortraitMesh->SetRelativeLocation(PortraitOrigin);
        PortraitMesh->SetLightingChannels(false, true, false);
        PortraitMesh->SetVisibleInSceneCaptureOnly(true);
        PortraitMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        PortraitMesh->SetCanEverAffectNavigation(false);
        PortraitMesh->SetCastShadow(false);
        PortraitMesh->SetForcedLOD(1);
        AddInstanceComponent(PortraitMesh);
        PortraitMesh->RegisterComponent();

        const auto Light = [&](FName Name, const FVector& At, const FColor& Color, float Candelas)
        {
            auto* Fill = NewObject<UPointLightComponent>(this, Name);
            Fill->SetupAttachment(GetRootComponent());
            Fill->SetRelativeLocation(PortraitOrigin + At);
            Fill->SetMobility(EComponentMobility::Movable);
            Fill->SetLightingChannels(false, true, false);
            Fill->SetLightColor(Color);
            Fill->SetIntensityUnits(ELightUnits::Candelas);
            Fill->SetIntensity(Candelas);
            Fill->SetAttenuationRadius(625.f);
            Fill->SetVolumetricScatteringIntensity(0.f);
            Fill->SetCastShadows(false);
            AddInstanceComponent(Fill);
            Fill->RegisterComponent();
        };
        Light(TEXT("NorthPortraitKey"), FVector(180, -115, 165), FColor(255, 220, 191), 91.f);
        Light(TEXT("NorthPortraitFill"), FVector(135, 155, 95), FColor(187, 210, 255), 38.f);
        Light(TEXT("NorthPortraitRim"), FVector(-105, 45, 140), FColor(221, 200, 255), 74.f);

        auto* Backdrop = DuelGeometry::Shape(this, GetRootComponent(), DuelGeometry::EShape::Box,
            PortraitOrigin + FVector(-390, 0, 125), FVector(12, 1100, 1100),
            DuelGeometry::ColorMaterial(this, FLinearColor(.011f, .020f, .042f), true));
        Backdrop->SetVisibleInSceneCaptureOnly(true);
        Backdrop->SetCastShadow(false);

        PortraitTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("NorthDialoguePortrait"));
        PortraitTarget->ClearColor = FLinearColor::Black;
        PortraitTarget->RenderTargetFormat = RTF_RGBA8_SRGB;
        PortraitTarget->InitAutoFormat(PortraitWidth, PortraitHeight);
        PortraitCamera = NewObject<USceneCaptureComponent2D>(this, TEXT("NorthPortraitCamera"));
        PortraitCamera->SetupAttachment(GetRootComponent());
        PortraitCamera->TextureTarget = PortraitTarget;
        PortraitCamera->bCaptureEveryFrame = false;
        PortraitCamera->bCaptureOnMovement = false;
        PortraitCamera->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
        PortraitCamera->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
        PortraitCamera->FOVAngle = Lens;
        PortraitCamera->PostProcessSettings.bOverride_AutoExposureMethod = true;
        PortraitCamera->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
        PortraitCamera->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
        PortraitCamera->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
        PortraitCamera->PostProcessSettings.bOverride_AutoExposureBias = true;
        PortraitCamera->PostProcessSettings.AutoExposureBias = -.55f;
        PortraitCamera->ShowOnlyComponent(PortraitMesh);
        PortraitCamera->ShowOnlyComponent(Backdrop);
        AddInstanceComponent(PortraitCamera);
        PortraitCamera->RegisterComponent();
        // Registration resets show flags from the component archetype.
        PortraitCamera->ShowFlags.SetFog(false);
        PortraitCamera->ShowFlags.SetVolumetricFog(false);
        PortraitCamera->ShowFlags.SetAtmosphere(false);
        PortraitCamera->ShowFlags.SetCloud(false);
        PortraitCamera->ShowFlags.SetDynamicShadows(false);
        PortraitCamera->ShowFlags.SetLumenGlobalIllumination(false);
        PortraitCamera->ShowFlags.SetLumenReflections(false);
        PortraitCamera->ShowFlags.SetScreenSpaceReflections(false);
        PortraitCamera->ShowFlags.SetBloom(false);
        PortraitCamera->ShowFlags.SetSkyLighting(false);
        PortraitCamera->ShowFlags.SetGlobalIllumination(false);
        PortraitCamera->ShowFlags.SetMotionBlur(false);
        PortraitCamera->ShowFlags.SetTemporalAA(false);
    }

    if (PortraitMesh->GetSkinnedAsset() != Asset)
    {
        PortraitMesh->BoneSpaceTransforms.Reset();
        PortraitMesh->SetSkinnedAssetAndUpdate(Asset);
    }
    PortraitMesh->SetVisibility(true);
    const FBoxSphereBounds Bounds = Asset->GetBounds();
    const float Headroom = Bounds.BoxExtent.Z * (bYuki ? 2.40f : 1.97f);
    const float Distance = Headroom * PortraitWidth / PortraitHeight /
        (2.f * FMath::Tan(FMath::DegreesToRadians(Lens * .5f)));
    FVector Focus = Bounds.Origin + FVector(0, 0, Bounds.BoxExtent.Z * .29f);
    if (bYuki)
    {
        const auto& Rig = Asset->GetRefSkeleton();
        const int32 HeadIndex = Rig.FindBoneIndex(HeadBone);
        if (Rig.GetRefBonePose().IsValidIndex(HeadIndex))
            Focus.X = FMath::Lerp(Focus.X, Rig.GetRefBonePose()[HeadIndex].GetTranslation().X, .72f);
    }
    const FVector Offset(Distance, Distance * (bEma ? -.22f : .22f), Distance * .09f);
    PortraitCamera->SetRelativeLocationAndRotation(PortraitOrigin + Focus + Offset, (-Offset).Rotation());
    UpdateDialoguePortrait(DialogueLineStartedAt);
}

void ADuelNorthNarrative::ClearDialogueMedia()
{
    if (IsValid(ArenaRef) && DialogueLineStartedAt >= 0.0) ArenaRef->StopVoice(0.f);
    DialogueLineStartedAt = -1.0;
    LocalPortrait = nullptr;
    if (PortraitMesh) PortraitMesh->SetVisibility(false);
}

void ADuelNorthNarrative::UpdateDialoguePortrait(double Now)
{
    if (!PortraitMesh || !PortraitMesh->IsVisible() || Now < NextPortraitCaptureAt) return;
    NextPortraitCaptureAt = Now + 1.0 / 15.0;
    const auto* Asset = Cast<USkeletalMesh>(PortraitMesh->GetSkinnedAsset());
    if (!Asset) return;

    const auto Subtitle = Playback.Subtitle();
    const float Age = static_cast<float>(Now - DialogueLineStartedAt);
    const FPortraitPose Pose = PoseFor(Subtitle.Expression);
    const auto& Rig = Asset->GetRefSkeleton();
    const auto& Rest = Rig.GetRefBonePose();
    auto& Bones = PortraitMesh->BoneSpaceTransforms;
    const int32 HeadIndex = Rig.FindBoneIndex(HeadBone);
    const int32 MouthIndex = Rig.FindBoneIndex(MouthBone);
    if (Bones.IsValidIndex(HeadIndex))
    {
        const FRotator Turn(Pose.Pitch + 2.5f * FMath::Sin(Age * 3.7f),
            Pose.Yaw + 2.f * FMath::Sin(Age * 2.4f), Pose.Roll);
        Bones[HeadIndex].SetRotation(Rest[HeadIndex].GetRotation() * Turn.Quaternion());
    }
    if (Bones.IsValidIndex(MouthIndex))
    {
        const float PhraseTime = FMath::Clamp(Subtitle.Text.Len() * .1f, .65f, 4.f);
        const float Articulation = (1.f - FMath::SmoothStep(PhraseTime - .25f, PhraseTime, Age)) *
            FMath::Max(0.f, FMath::Sin(Age * 17.5f));
        const FVector Shift(0, 0, -1.45f * Pose.Stress * Articulation);
        Bones[MouthIndex].SetTranslation(Rest[MouthIndex].GetTranslation() + Shift);
    }
    PortraitMesh->SetRelativeLocation(PortraitOrigin + FVector(0, 0, .9f * FMath::Sin(Age * 3.f)));
    PortraitMesh->MarkRefreshTransformDirty();
    PortraitMesh->RefreshBoneTransforms();
    PortraitCamera->CaptureSceneDeferred();
}

#if WITH_DEV_AUTOMATION_TESTS
#include "DuelNorthInvestigationQA.h"
#include "DuelNorthLayout.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthSmilePortraitsTest, "ManosabaDuel.NorthTower.SmilePortraits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthSmilePortraitsTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    const EDuelCharacter Characters[] = {EDuelCharacter::Ema, EDuelCharacter::Hiro};
    const TCHAR* Hashes[] = {TEXT("420011F8B3014C3B3DD524E6A047268716B9348F"), TEXT("16BC2976E22F986EE0F2E1BA4B7158A4BF524720")};
    for (int32 I = 0; I < 2; ++I)
    {
        const auto Character = Characters[I];
        TArray<uint8> Bytes;
        const FString File = FPaths::ProjectContentDir() / TEXT("DuelPortraits") / CharacterFolder(Character) / TEXT("Smile.png");
        if (!TestTrue(TEXT("Canonical original smile PNG is packaged for this character"), FFileHelper::LoadFileToArray(Bytes, *File)))
            continue;
        TestEqual(TEXT("Actual original-art bytes match the visually inspected smile, not a renamed fright image"),
            FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num()).ToString(), FString(Hashes[I]));
        TestEqual(TEXT("A legacy Neutral label resolves to the new smile slot"), FString(ExpressionFile(EDuelNorthExpression::Neutral, Character)), FString(TEXT("Smile.png")));
        TestEqual(TEXT("The new default resolves to actual smile art"), FString(ExpressionFile(FDuelNorthDialogueLine{}.Expression, Character)), FString(TEXT("Smile.png")));
        TestEqual(TEXT("Explicit hallucination surprise retains its own separate slot"), FString(ExpressionFile(EDuelNorthExpression::Startled, Character)), FString(TEXT("Startled.png")));
        if (FApp::CanEverRender() && !GUsingNullRHI)
            TestNotNull(TEXT("The runtime loader obtains a genuine 2D smile even without personal portrait files"), LoadSmilePortrait(Character));

        FDuelNorthProgress State;
        State.Reset(Character);
        State.bOpeningCellRequired = true;
        State.ReadClue(EDuelNorthClue::OpeningDocket);
        State.OpenStartingCell();
        TSet<FString> Seen;
        int32 Total = 0, Smiles = 0, Fright = 0;
        const auto CountScript = [&](const FDuelNorthDialogueScript& Script)
        {
            for (const auto& Line : Script.Lines)
            {
                if (Line.Speaker == EDuelCharacter::Yuki || Line.bDefeatedBoss) continue;
                const FString Key = FString::Printf(TEXT("%d:%s"), int32(Line.Speaker), *Line.Text);
                if (Seen.Contains(Key)) continue;
                Seen.Add(Key); ++Total;
                Smiles += Line.Expression == EDuelNorthExpression::Smile;
                if (Line.Expression == EDuelNorthExpression::Startled)
                {
                    ++Fright;
                    TestTrue(TEXT("Fright belongs only to an immediate recurring hallucination"), Line.Text.Contains(TEXT("我又看见")));
                }
            }
        };
        const auto Collect = [&]()
        {
            for (int32 Clue = 0; Clue < int32(EDuelNorthClue::Count); ++Clue) State.ReadClue(static_cast<EDuelNorthClue>(Clue));
            for (int32 D = 0; D < int32(EDuelNorthDialogue::Count); ++D) CountScript(State.DialogueScript(static_cast<EDuelNorthDialogue>(D)));
            for (const TCHAR* Id : {TEXT("OpeningDocket"), TEXT("RefugeLedger"), TEXT("FindRations"),
                TEXT("ArchiveTransferLedger"), TEXT("ArchiveSignatureRegister"), TEXT("FindCounterseal"),
                TEXT("OwnVerdict"), TEXT("Signature"), TEXT("BeaconChart"), TEXT("LensWorkshop"), TEXT("BeaconLetter"), TEXT("BeaconSwitch")})
                CountScript(State.InvestigationScript(Id));
        };
        Collect();
        for (int32 Battle = 0; Battle < FDuelNorthProgress::BattleCount; ++Battle)
        {
            if (!TestTrue(TEXT("Prepare the authoritative story stage"), DuelNorthInvestigationQA::Prepare(State, Battle))) break;
            Collect();
            if (Battle == 1)
            {
                const auto Before = State.InvestigationScript(TEXT("FindCounterseal"));
                bool bExplainedFloor = false;
                for (const auto& Line : Before.Lines) bExplainedFloor |= Line.Text.Contains(TEXT("三楼的镜厅"));
                TestTrue(TEXT("Both protagonist viewpoints naturally identify the mirror hall's third floor"), bExplainedFloor);
                TestTrue(TEXT("The matching quest points to the third-floor mirror hall"),
                    State.NextObjective().Floor == EDuelNorthFloor::Mirror && State.NextObjective().Text.Contains(TEXT("3F")));
            }
            if (!State.WinBattle(Battle)) break;
            State.PickupRelic(static_cast<EDuelNorthRelic>(Battle));
            if (Battle < 2) State.OpenDoor(static_cast<EDuelNorthDoor>(Battle));
            if (Battle == 0) State.CompleteDialogue(EDuelNorthDialogue::RescuedCompanion);
            Collect();
        }
        TestTrue(TEXT("The audit covers the full dialogue and investigation route"), Total >= 65);
        TestTrue(TEXT("Genuine smiles are the predominant default across dialogue and hints"), Smiles * 100 >= Total * 85);
        TestTrue(TEXT("Full fright is rare and not reused for an ordinary greeting or reunion"), Fright <= 1);
        AddInfo(FString::Printf(TEXT("%s: %d unique ordinary lines, %d smiles, %d fright reactions"),
            CharacterFolder(Character), Total, Smiles, Fright));
    }
    TestTrue(TEXT("Map and task floor naming still identify Mirror as 3F"),
        FString(DuelNorthLayout::FloorName(DuelNorthLayout::EFloor::Mirror)).Contains(TEXT("3F")) &&
        FString(FDuelNorthProgress::FloorName(EDuelNorthFloor::Mirror)).Contains(TEXT("3F")));
    return !HasAnyErrors();
}
#endif

