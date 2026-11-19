#include "DuelNorthExtraction.h"

#include "DuelGame.h"
#include "DuelNorthInvestigationQA.h"
#include "DuelNorthLayout.h"
#include "DuelNorthNarrative.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWaveProcedural.h"

using namespace DuelNorthExtractionVisual;

namespace
{
    constexpr float AlignSeconds = .52f;
    constexpr float ClimbSeconds = 6.1f;
    constexpr float StepInsideSeconds = .9f;
    constexpr float CompanionDelay = 1.45f;
    constexpr float BoardingSeconds = AlignSeconds + ClimbSeconds + StepInsideSeconds + CompanionDelay;
    constexpr float CallRadius = 120.f;
    constexpr float LadderRadius = 130.f;
    constexpr float StatusRadius = 430.f;

    FVector Feet(const ADuelFighter& Figure)
    {
        return Figure.GetActorLocation() - FVector(0, 0, Figure.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    }

    bool Near(const ADuelFighter& Figure, const FVector& Point, float Radius)
    {
        const FVector At = Feet(Figure);
        return FMath::Abs(At.Z - Point.Z) < 80.f && FVector::DistSquared2D(At, Point) < FMath::Square(Radius);
    }

    bool InGarden(const ADuelFighter& Figure)
    {
        return DuelNorthLayout::Locate(Feet(Figure), 0).Room == TEXT("DawnGarden");
    }

    bool CompanionReady(const ADuelFighter* Companion)
    {
        return IsValid(Companion) && Companion->IsAlive() && !Companion->IsHidden() && InGarden(*Companion) &&
            Near(*Companion, LadderFoot, 345.f);
    }

    void PlaceFeet(ADuelFighter& Figure, FVector At, const FQuat& Rotation)
    {
        At.Z += Figure.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        Figure.SetActorLocationAndRotation(At, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
    }
}

ADuelNorthExtraction::ADuelNorthExtraction()
{
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("NorthExtraction"));
    PrimaryActorTick.bCanEverTick = false;
}

bool ADuelNorthExtraction::CanCall(const ADuelFighter& Player, const FDuelNorthProgress& State)
{
    return State.CanViewEnding() && Player.IsAlive() && InGarden(Player) && Near(Player, PickupMark, CallRadius);
}

FString ADuelNorthExtraction::CallPrompt(const FDuelNorthProgress& State)
{
    return State.bExtractionSeen ? TEXT("F  再次呼叫直升机") : TEXT("F  用海岸电台呼叫直升机");
}

FString ADuelNorthExtraction::Prompt(const ADuelFighter& Player, const ADuelFighter* Companion,
    const FDuelNorthProgress& State) const
{
    if (EndingVisible()) return TEXT("F  返回标题画面");
    if (BlocksInput() || !State.CanViewEnding() || !InGarden(Player)) return {};
    if (Stage == EStage::Idle) return CanCall(Player, State) ? CallPrompt(State) : FString();
    if (Stage == EStage::Hold && Near(Player, LadderFoot, LadderRadius))
    {
        return CompanionReady(Companion) ? TEXT("F  登上绳梯") : TEXT("等同伴走近，再一起登机");
    }
    if (!Near(Player, LadderFoot, StatusRadius) && !Near(Player, PickupMark, StatusRadius)) return {};
    if (Stage == EStage::Approach) return TEXT("直升机正在接近");
    if (Stage == EStage::Deploy) return TEXT("绳梯正在放下");
    return TEXT("绳梯已放下，走近梯脚");
}

bool ADuelNorthExtraction::Confirm(ADuelFighter& Player, ADuelFighter* Companion, const FDuelNorthProgress& State)
{
    if (EndingVisible())
    {
        Reset(true);
        return true;
    }
    if (BlocksInput()) return true;
    if (Stage == EStage::Idle)
    {
        if (!CanCall(Player, State)) return false;
        if (!Visual.Root.IsValid()) Visual = Build(this, RootComponent);
        if (!Visual.Root.IsValid()) return false;
        bPlayerIsEma = Player.SelectedCharacter() == EDuelCharacter::Ema;
        RotorSeconds = 0.f;
        Enter(EStage::Approach);
        SetPose(Visual, EPhase::Approach, 0.f, RotorSeconds);
        return true;
    }
    if (Stage != EStage::Hold || !State.CanViewEnding() || !Player.IsAlive() || !InGarden(Player) ||
        !Near(Player, LadderFoot, LadderRadius)) return false;
    if (CompanionReady(Companion))
    {
        TakePassenger(Passengers[0], Player);
        TakePassenger(Passengers[1], *Companion);
        Enter(EStage::Boarding);
    }
    return true;
}

void ADuelNorthExtraction::Enter(EStage Next)
{
    Stage = Next;
    StageSeconds = 0.f;
}

void ADuelNorthExtraction::TakePassenger(FPassenger& Slot, ADuelFighter& Fighter)
{
    auto* Motion = Fighter.GetCharacterMovement();
    Slot.Fighter = &Fighter;
    Slot.Before = Fighter.GetActorTransform();
    Slot.MovementMode = uint8(Motion->MovementMode);
    Slot.CustomMovementMode = Motion->CustomMovementMode;
    Slot.bCollision = Fighter.GetActorEnableCollision();
    Fighter.EndCombat();
    Fighter.ConsumeMovementInputVector();
    Motion->StopMovementImmediately();
    Motion->DisableMovement();
    Fighter.SetActorEnableCollision(false);
}

void ADuelNorthExtraction::Board(FPassenger& Slot, const FVector& Seat, float Delay)
{
    ADuelFighter* Figure = Slot.Fighter.Get();
    if (!Figure || StageSeconds < Delay) return;
    const float Age = StageSeconds - Delay;
    const FVector Start = Slot.Before.GetLocation() - FVector(0, 0, Figure->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    const FVector HangOffset = LadderFacing.Vector() * -33.f;
    const FQuat Facing = LadderFacing.Quaternion();
    if (Age < AlignSeconds)
    {
        const float Align = FMath::SmoothStep(0.f, AlignSeconds, Age);
        PlaceFeet(*Figure, FMath::Lerp(Start, LadderFoot + HangOffset, Align),
            FQuat::Slerp(Slot.Before.GetRotation(), Facing, Align));
        Figure->SetNorthExtractionPose(0.f, Align * .7f, false);
    }
    else if (Age < AlignSeconds + ClimbSeconds)
    {
        const float Climb = FMath::InterpEaseInOut(0.f, 1.f, (Age - AlignSeconds) / ClimbSeconds, 1.2f);
        const FVector Position = FMath::Lerp(LadderFoot, BoardingPoint, Climb) + HangOffset;
        PlaceFeet(*Figure, Position, Facing);
        Figure->SetNorthExtractionPose(float(Position.Z - LadderFoot.Z) * (PI / RungSpacing), 1.f, false);
    }
    else
    {
        const FTransform Vehicle = FlightTransform(EPhase::Hold, 1.f);
        const float Step = FMath::SmoothStep(0.f, StepInsideSeconds, Age - AlignSeconds - ClimbSeconds);
        PlaceFeet(*Figure, FMath::Lerp(BoardingPoint + HangOffset, Vehicle.TransformPosition(Seat), Step),
            FQuat::Slerp(Facing, Vehicle.GetRotation(), Step));
        Figure->SetNorthExtractionPose(0.f, 1.f, true);
    }
}

void ADuelNorthExtraction::SeatPassengers(const FTransform& Vehicle)
{
    if (ADuelFighter* Player = Passengers[0].Fighter.Get())
        PlaceFeet(*Player, Vehicle.TransformPosition(NearSeatLocal), Vehicle.GetRotation());
    if (ADuelFighter* Companion = Passengers[1].Fighter.Get())
        PlaceFeet(*Companion, Vehicle.TransformPosition(FarSeatLocal), Vehicle.GetRotation());
}

void ADuelNorthExtraction::CalcCamera(float DeltaSeconds, FMinimalViewInfo& View)
{
    (void)DeltaSeconds;
    View = FlightView;
}

void ADuelNorthExtraction::StartRotor()
{
    if (!FApp::CanEverRender()) return;
    constexpr int32 Rate = 24000;
    TArray<int16> PCM;
    PCM.SetNumUninitialized(Rate * 3 / 2);
    // Original synthesis. Whole-period harmonics give the 1.5-second loop a continuous seam.
    for (int32 Index = 0; Index < PCM.Num(); ++Index)
    {
        const float Phase = 2.f * PI * Index / PCM.Num();
        const float Impulse = FMath::Square(FMath::Max(0.f, FMath::Sin(20.f * Phase)));
        const float Engine = .033f * FMath::Sin(132.f * Phase) + .018f * FMath::Sin(255.f * Phase);
        const float Blade = (.045f + .19f * Impulse) *
            (.67f * FMath::Sin(75.f * Phase) + .22f * FMath::Sin(109.f * Phase) + .11f * FMath::Sin(185.f * Phase));
        PCM[Index] = static_cast<int16>((Engine + Blade) * 32767.f);
    }
    auto* Wave = NewObject<USoundWaveProcedural>(this);
    Wave->SetSampleRate(Rate);
    Wave->NumChannels = 1;
    Wave->Duration = float(PCM.Num()) / Rate;
    Wave->bLooping = true;
    Wave->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
    Wave->QueueAudio(reinterpret_cast<const uint8*>(PCM.GetData()), PCM.Num() * sizeof(int16));
    Wave->OnSoundWaveProceduralUnderflow.BindLambda([Samples = MoveTemp(PCM)](USoundWaveProcedural* Source, int32 Needed)
    {
        if (Source->GetAvailableAudioByteCount() < Needed * int32(sizeof(int16)))
            Source->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * sizeof(int16));
    });
    auto* Attenuation = NewObject<USoundAttenuation>(this);
    Attenuation->Attenuation.bAttenuate = true;
    Attenuation->Attenuation.bSpatialize = true;
    Attenuation->Attenuation.AttenuationShapeExtents = FVector(900.f);
    Attenuation->Attenuation.FalloffDistance = 8600.f;
    RotorAudio = UGameplayStatics::SpawnSoundAttached(Wave, Visual.Root.Get(), NAME_None,
        FVector::ZeroVector, EAttachLocation::KeepRelativeOffset, true, .70f, 1.f, 0.f, Attenuation);
}

bool ADuelNorthExtraction::Update(FDuelNorthProgress& State, ADuelFighter& Player, float DeltaSeconds, bool bExploring)
{
    if (Stage == EStage::Idle) return false;
    if (!bExploring || !State.CanViewEnding() || (!BlocksInput() && !InGarden(Player)) ||
        (BlocksInput() && (!Passengers[0].Fighter.IsValid() || !Passengers[1].Fighter.IsValid())))
    {
        Reset(true);
        return false;
    }
    const float Step = FMath::IsFinite(DeltaSeconds) ? FMath::Clamp(DeltaSeconds, 0.f, .25f) : 0.f;
    StageSeconds += Step;
    RotorSeconds += Step;
    switch (Stage)
    {
    case EStage::Approach:
        SetPose(Visual, EPhase::Approach, FMath::Min(StageSeconds / ApproachSeconds, 1.f), RotorSeconds);
        if (StageSeconds - Step < ApproachSeconds * .5f && StageSeconds >= ApproachSeconds * .5f) StartRotor();
        if (StageSeconds >= ApproachSeconds) Enter(EStage::Deploy);
        break;
    case EStage::Deploy:
        SetPose(Visual, EPhase::Deploy, FMath::Min(StageSeconds / DeploySeconds, 1.f), RotorSeconds);
        if (StageSeconds >= DeploySeconds) Enter(EStage::Hold);
        break;
    case EStage::Hold:
        SetPose(Visual, EPhase::Hold, 1.f, RotorSeconds);
        break;
    case EStage::Boarding:
        SetPose(Visual, EPhase::Hold, 1.f, RotorSeconds);
        Board(Passengers[0], NearSeatLocal, 0.f);
        Board(Passengers[1], FarSeatLocal, CompanionDelay);
        if (StageSeconds >= BoardingSeconds)
        {
            FlightView = FlightCamera(FlightTransform(EPhase::Hold, 1.f), 0.f);
            CameraController = Cast<APlayerController>(Player.GetController());
            if (auto* Controller = CameraController.Get()) Controller->SetViewTargetWithBlend(this, .75f, VTBlend_Cubic);
            Enter(EStage::Retract);
        }
        break;
    case EStage::Retract:
    case EStage::Departure:
    {
        const bool bLeaving = Stage == EStage::Departure;
        const EPhase Phase = bLeaving ? EPhase::Departure : EPhase::Retract;
        const float Duration = bLeaving ? DepartureSeconds : RetractSeconds;
        const float Progress = FMath::Min(StageSeconds / Duration, 1.f);
        const FTransform Vehicle = FlightTransform(Phase, Progress);
        SetPose(Visual, Phase, Progress, RotorSeconds);
        SeatPassengers(Vehicle);
        FlightView = FlightCamera(Vehicle, bLeaving ? Progress : 0.f);
        if (StageSeconds >= Duration)
        {
            Enter(bLeaving ? EStage::Ending : EStage::Departure);
            if (bLeaving)
            {
                if (auto* Audio = RotorAudio.Get()) Audio->FadeOut(.7f, 0.f);
                return State.CompleteExtraction();
            }
        }
        break;
    }
    case EStage::Ending:
        SetPose(Visual, EPhase::Departure, 1.f, RotorSeconds);
        break;
    default:
        break;
    }
    return false;
}

void ADuelNorthExtraction::Reset(bool bReturnToLadder)
{
    if (auto* Audio = RotorAudio.Get()) Audio->Stop();
    RotorAudio.Reset();
    if (auto* Controller = CameraController.Get())
        if (ADuelFighter* Player = Passengers[0].Fighter.Get(); Player && Controller->GetViewTarget() == this)
            Controller->SetViewTarget(Player);
    CameraController.Reset();
    for (FPassenger& Slot : Passengers)
    {
        if (ADuelFighter* Fighter = Slot.Fighter.Get())
        {
            if (bReturnToLadder)
                Fighter->SetActorTransform(Slot.Before, false, nullptr, ETeleportType::TeleportPhysics);
            Fighter->SetNorthExtractionPose(0.f, 0.f, false);
            Fighter->SetActorEnableCollision(Slot.bCollision);
            auto* Motion = Fighter->GetCharacterMovement();
            Motion->StopMovementImmediately();
            Motion->SetMovementMode(static_cast<EMovementMode>(Slot.MovementMode), Slot.CustomMovementMode);
        }
        Slot = {};
    }
    if (Visual.Root.IsValid()) SetPose(Visual, EPhase::Hidden, 0.f, RotorSeconds);
    Enter(EStage::Idle);
    RotorSeconds = 0.f;
}

FDuelNorthSubtitle ADuelNorthExtraction::Subtitle() const
{
    const auto Ema = [](const TCHAR* Text) -> FDuelNorthSubtitle { return {TEXT("艾玛"), Text}; };
    const auto Hiro = [](const TCHAR* Text) -> FDuelNorthSubtitle { return {TEXT("希罗"), Text}; };
    switch (Stage)
    {
    case EStage::Approach:
        if (StageSeconds < 4.2f) return Ema(TEXT("希罗酱，有人回话了！她们听见我们了。"));
        if (StageSeconds < 8.6f) return Hiro(TEXT("她们从海上来。我们在这儿等，给梯子留点地方。"));
        break;
    case EStage::Deploy:
        return Hiro(TEXT("看，绳梯下来了。等它放稳，我们再过去。"));
    case EStage::Boarding:
        if (StageSeconds < 3.8f)
            return bPlayerIsEma ? Hiro(TEXT("艾玛，你先上。我就在你后面。")) :
                Ema(TEXT("希罗酱，你慢点爬。我跟着你呢。"));
        if (StageSeconds < 7.6f)
            return bPlayerIsEma ? Ema(TEXT("我听得见你。上面有地方坐，等我给你留一个。")) :
                Hiro(TEXT("听见了。上来以后坐我旁边。"));
        break;
    case EStage::Retract:
        return Ema(TEXT("希罗酱，坐近一点。我们要走啦。"));
    case EStage::Departure:
        if (StageSeconds < 3.6f) return Ema(TEXT("希罗酱，我还想回头看看。"));
        if (StageSeconds < 7.2f) return Hiro(TEXT("嗯。我也想再看一眼。"));
        if (StageSeconds < 10.8f) return Ema(TEXT("已经看不清窗子了……到了岸上，先陪我去吃早饭吧。"));
        if (StageSeconds < 14.4f) return Hiro(TEXT("好。我也想和你一起吃早饭。"));
        break;
    default:
        break;
    }
    return {};
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDuelNorthExtractionRuntimeTest, "ManosabaDuel.NorthTower.Extraction",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FDuelNorthExtractionRuntimeTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    UWorld* World = nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
        if (Context.World() && Context.World()->GetAuthGameMode<ADuelMode>())
        {
            World = Context.World();
            break;
        }
    if (!TestTrue(TEXT("Use an isolated North Tower gameplay world"), World &&
        FParse::Param(FCommandLine::Get(), TEXT("DuelNorthTower")) &&
        FParse::Param(FCommandLine::Get(), TEXT("DuelNoSave")))) return false;
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ADuelFighter* Player = World->SpawnActor<ADuelFighter>(FVector::ZeroVector, LadderFacing, Spawn);
    ADuelFighter* Companion = World->SpawnActor<ADuelFighter>(FVector::ZeroVector, LadderFacing, Spawn);
    ADuelNorthExtraction* Extraction = World->SpawnActor<ADuelNorthExtraction>();
    ON_SCOPE_EXIT
    {
        if (IsValid(Extraction)) { Extraction->Reset(false); Extraction->Destroy(); }
        if (IsValid(Player)) Player->Destroy();
        if (IsValid(Companion)) Companion->Destroy();
    };
    if (!TestTrue(TEXT("Fixture has two live figures and the extraction actor"), Player && Companion && Extraction)) return false;
    const auto* Garden = DuelNorthLayout::FindRoom(TEXT("DawnGarden"));
    if (!TestNotNull(TEXT("The fixture has a walkable Garden deck"), Garden)) return false;
    // BeginPlay resets a fighter to the duel spawn, so place the fixture afterwards.
    PlaceFeet(*Player, PickupMark, LadderFacing.Quaternion());
    PlaceFeet(*Companion, LadderFoot + FVector(-210, 40, 0), LadderFacing.Quaternion());
    if (!TestTrue(TEXT("Both fixture fighters are at their Garden approach positions"),
        InGarden(*Player) && Near(*Player, PickupMark, CallRadius) && CompanionReady(Companion))) return false;
    FDuelNorthProgress State;
    State.Reset(Player->SelectedCharacter());
    State.bOpeningCellRequired = true;
    if (!TestTrue(TEXT("The current-route fixture reads the docket and opens the starting cell"),
        State.ReadClue(EDuelNorthClue::OpeningDocket) && State.OpenStartingCell())) return false;
    for (int32 I = 0; I < FDuelNorthProgress::BattleCount; ++I)
    {
        if (!TestTrue(TEXT("Each fixture victory grants its relic"), DuelNorthInvestigationQA::Prepare(State,I) && State.WinBattle(I) &&
            State.PickupRelic(static_cast<EDuelNorthRelic>(I)))) return false;
        const auto Door = static_cast<EDuelNorthDoor>(I);
        if (Door == EDuelNorthDoor::TowerExit)
        {
            TestFalse(TEXT("The last reunion precedes radio use"), Extraction->Confirm(*Player, Companion, State));
            if (!TestTrue(TEXT("The last reunion can be completed before opening the tower exit"),
                State.CompleteDialogue(EDuelNorthDialogue::FinalExit))) return false;
        }
        if (!TestTrue(TEXT("The fixture can open the next door"), State.OpenDoor(Door))) return false;
    }
    if (!TestTrue(TEXT("Finishing the reunion and opening the tower exit enables the coastal radio"),
        State.CanViewEnding() && ADuelNorthExtraction::CanCall(*Player, State))) return false;
    if (!TestTrue(TEXT("At the radio F calls without locking ordinary walking"),
        Extraction->Confirm(*Player, Companion, State) && !Extraction->BlocksInput())) return false;
    for (int32 Frame = 0; Frame < 112; ++Frame) Extraction->Update(State, *Player, .2f, true);
    TestFalse(TEXT("Player must walk from the radio to the ladder"), Extraction->Confirm(*Player, Companion, State));
    PlaceFeet(*Player, LadderFoot + FVector(-85, 0, 0), LadderFacing.Quaternion());
    const FTransform BeforeBoarding = Player->GetActorTransform();
    const FTransform CompanionBeforeBoarding = Companion->GetActorTransform();
    TestTrue(TEXT("F at the lowered ladder begins boarding without moving the player instantly"),
        Extraction->Confirm(*Player, Companion, State) && Extraction->BlocksInput() &&
        Player->GetActorTransform().Equals(BeforeBoarding));
    for (int32 Frame = 0; Frame < 30; ++Frame) Extraction->Update(State, *Player, .2f, true);
    TestTrue(TEXT("Both character roots ascend the ladder before departure"),
        Feet(*Player).Z > LadderFoot.Z + 400 && Feet(*Companion).Z > LadderFoot.Z + 200 && !State.bExtractionSeen);
    TestFalse(TEXT("The ending theme waits until the helicopter departs"), Extraction->WantsEndingMusic());
    bool bMusicDuringFlight = false;
    constexpr float FlightStep = .2f;
    const int32 FlightBudget = FMath::CeilToInt((BoardingSeconds + RetractSeconds + DepartureSeconds + 1.f) / FlightStep);
    for (int32 Frame = 0; Frame < FlightBudget && !Extraction->EndingVisible(); ++Frame)
    {
        Extraction->Update(State, *Player, FlightStep, true);
        bMusicDuringFlight |= Extraction->WantsEndingMusic() && !Extraction->EndingVisible();
    }
    TestTrue(TEXT("The ending theme is requested during departure and remains requested on the ending page"),
        bMusicDuringFlight && Extraction->WantsEndingMusic());
    TestTrue(TEXT("Only the completed flight opens and records the optional ending"),
        Extraction->EndingVisible() && State.bExtractionSeen && !State.bEndingSeen && State.CanViewEnding());

    const FVector Prison(0, 0, 2800);
    const auto MidView = FlightCamera(FlightTransform(EPhase::Departure, .60f), .60f);
    FMinimalViewInfo LastView;
    Extraction->CalcCamera(0.f, LastView);
    TestTrue(TEXT("The departure camera looks back at the same prison as its distance increases"),
        FVector::DotProduct(MidView.Rotation.Vector(), (Prison - MidView.Location).GetSafeNormal()) > .99 &&
        FVector::DotProduct(LastView.Rotation.Vector(), (Prison - LastView.Location).GetSafeNormal()) > .99 &&
        FVector::Dist(LastView.Location, Prison) > FVector::Dist(MidView.Location, Prison) + 6000.);
    TArray<uint8> Bytes;
    FMemoryWriter Writer(Bytes, true);
    FDuelNorthProgress::StaticStruct()->SerializeItem(Writer, &State, nullptr);
    FDuelNorthProgress Loaded;
    FMemoryReader Reader(Bytes, true);
    FDuelNorthProgress::StaticStruct()->SerializeItem(Reader, &Loaded, nullptr);
    FString Reason;
    TestTrue(TEXT("The save retains extraction and still permits the ordinary Garden ending"),
        Loaded.bExtractionSeen && Loaded.Validate(Reason) && Loaded.ViewEnding());
    TestTrue(TEXT("F closes the extraction ending"), Extraction->Confirm(*Player, Companion, State) && !Extraction->EndingVisible());
    TestFalse(TEXT("Closing the ending restores player control"), Extraction->BlocksInput());
    TestFalse(TEXT("Closing the ending releases the soundtrack"), Extraction->WantsEndingMusic());
    const auto CheckReturn = [&](const TCHAR* Name, const ADuelFighter& Fighter, const FTransform& Before)
    {
        const FVector Delta = Fighter.GetActorLocation() - Before.GetLocation();
        const double FloorClearance = Feet(Fighter).Z - Garden->Bounds.Min.Z;
        const auto* Motion = Fighter.GetCharacterMovement();
        TestTrue(FString::Printf(TEXT("%s collision is restored"), Name), Fighter.GetActorEnableCollision());
        TestTrue(FString::Printf(TEXT("%s returns to the boarding X/Y (delta %s cm)"), Name, *Delta.ToString()),
            Delta.SizeSquared2D() <= FMath::Square(.05));
        TestTrue(FString::Printf(TEXT("%s facing is restored"), Name), Fighter.GetActorQuat().Equals(Before.GetRotation()));
        TestTrue(FString::Printf(TEXT("%s resumes walking on the Garden deck (clearance %.2f cm)"), Name, FloorClearance),
            Motion->IsMovingOnGround() && Motion->CurrentFloor.IsWalkableFloor() && InGarden(Fighter) &&
            FloorClearance >= 0. && FloorClearance <= UCharacterMovementComponent::MAX_FLOOR_DIST + .25);
        AddInfo(FString::Printf(TEXT("Extraction return %s: delta %s cm; deck clearance %.2f cm"),
            Name, *Delta.ToString(), FloorClearance));
    };
    CheckReturn(TEXT("Player"), *Player, BeforeBoarding);
    CheckReturn(TEXT("Companion"), *Companion, CompanionBeforeBoarding);
    return !HasAnyErrors();
}
#endif
