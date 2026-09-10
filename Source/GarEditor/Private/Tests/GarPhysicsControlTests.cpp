// Copyright (c) SAM-tak. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/App.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "HAL/PlatformStackWalk.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "AbilitySystemComponent.h"
#include "GarCharacter.h"
#include "GarCharacterMoverComponent.h"
#include "GarSkeletalMeshComponent.h"
#include "GarPhysicsControlComponent.h"
#include "GarGameplayTags.h"
#include "State/GarCharacterMoverInputs.h"
#include "State/GarMoverStanceState.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarRagdollInputTest, "GAR.PhysicsControl.InputSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGarRagdollInputTest::RunTest(const FString& Parameters)
{
	FGarCharacterMoverInputs Source;
	Source.bHasRagdollTransform = true;
	Source.RagdollTransform = FTransform(FRotator(0, 35, 0), FVector(123, -45, 67));
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	bool bSuccess = true;
	Source.NetSerialize(Writer, nullptr, bSuccess);
	TestTrue(TEXT("Serialize ragdoll input"), bSuccess);
	FGarCharacterMoverInputs Restored;
	FMemoryReader Reader(Bytes);
	Restored.NetSerialize(Reader, nullptr, bSuccess);
	TestTrue(TEXT("Ragdoll input round trip"), bSuccess && Source == Restored);
	FGarCharacterMoverInputs Merged;
	Merged.Merge(Source);
	TestTrue(TEXT("Merge retains pose"), Merged.bHasRagdollTransform && Merged.RagdollTransform.Equals(Source.RagdollTransform));
	FGarCharacterMoverInputs Normal;
	Merged.Interpolate(Source, Normal, 1.0f);
	TestFalse(TEXT("Interpolation clears stale ragdoll state"), Merged.bHasRagdollTransform);
	return true;
}

// Record only this regression, leaving the normal warning visible in the log. Capture addresses
// immediately, but resolve symbols after the test so reporting does not stall ragdoll simulation.
class FGarSimulatedMeshMoveWarnings : public FOutputDevice
{
public:
	FGarSimulatedMeshMoveWarnings() { GLog->AddOutputDevice(this); }
	virtual ~FGarSimulatedMeshMoveWarnings() override { GLog->RemoveOutputDevice(this); }
	virtual bool CanBeUsedOnAnyThread() const override { return true; }
	virtual bool CanBeUsedOnMultipleThreads() const override { return true; }
	virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
	{
		// Receive the message synchronously, before the logging thread loses the original stack.
		// Only the game thread accesses our counters; all other threads leave the device untouched.
		if (IsInGameThread() && Category == FName(TEXT("PIE")) && Verbosity == ELogVerbosity::Warning
			&& FCString::Strstr(Message, TEXT("Attempting to move a fully simulated skeletal mesh")))
		{
			if (++Count == 1) StackDepth = FPlatformStackWalk::CaptureStackBackTrace(Stack, UE_ARRAY_COUNT(Stack));
		}
	}
	void Check(FAutomationTestBase* Test) const
	{
		if (!Test->TestEqual(TEXT("Ragdoll and recovery do not use ordinary simulated-mesh moves"), Count, 0))
		{
			FString Trace;
			for (uint32 Index = 0; Index < StackDepth; ++Index)
			{
				ANSICHAR Line[4096] = {};
				FPlatformStackWalk::ProgramCounterToHumanReadableString(Index, Stack[Index], Line, UE_ARRAY_COUNT(Line));
				Trace += ANSI_TO_TCHAR(Line);
				Trace += TEXT("\n");
			}
			Test->AddInfo(TEXT("First simulated-mesh move warning stack:\n") + Trace);
		}
	}
private:
	int32 Count = 0;
	uint32 StackDepth = 0;
	uint64 Stack[64] = {};
};

class FGarPhysicsControlPIECommand : public IAutomationLatentCommand
{
public:
	static constexpr double FloorHeight = 500.0;
	explicit FGarPhysicsControlPIECommand(FAutomationTestBase* InTest, FVector InRagdollImpulse = FVector::ZeroVector,
		float InManualExitAfter = 0.0f, bool bInProneRecovery = false)
		: Test(InTest), RagdollImpulse(InRagdollImpulse), ManualExitAfter(InManualExitAfter), bProneRecovery(bInProneRecovery) {}
	virtual ~FGarPhysicsControlPIECommand() override
	{
		MeshMoveWarnings.Check(Test);
		if (bChangedFrameTime)
		{
			FApp::SetUseFixedTimeStep(bPreviousFixedTimeStep);
			FApp::SetFixedDeltaTime(PreviousFixedDeltaTime);
		}
	}

	virtual bool Update() override
	{
		UWorld* World = GEditor->PlayWorld;
		if (FPlatformTime::Seconds() - Started > 180.0)
		{
			Test->AddError(TEXT("Timed out waiting for the GAR Physics Control PIE test"));
			return true;
		}
		if (!World || !World->HasBegunPlay()) return false;
		if (Stage == 0)
		{
			APlayerController* Controller = World->GetFirstPlayerController();
			if (!Controller) return false;
			UClass* Class = LoadClass<AGarCharacter>(nullptr, TEXT("/GAR/Core/B_Gar_Character.B_Gar_Character_C"));
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Character = World->SpawnActor<AGarCharacter>(Class, FVector(0, 0, FloorHeight + 100), FRotator::ZeroRotator, Spawn);
			if (!Test->TestNotNull(TEXT("Spawn GAR character"), Character.Get())) return true;
			Controller->Possess(Character.Get());
			Test->TestTrue(TEXT("Blueprint character uses GAR's physics-aware mesh component"),
				Character->GetMesh()->IsA<UGarSkeletalMeshComponent>());
			if (bProneRecovery)
			{
				bPreviousFixedTimeStep = FApp::UseFixedTimeStep();
				PreviousFixedDeltaTime = FApp::GetFixedDeltaTime();
				bChangedFrameTime = true;
				FApp::SetUseFixedTimeStep(true);
				FApp::SetFixedDeltaTime(1.0 / 15.0);
			}
			Advance(World);
			return false;
		}
		if (!Character.IsValid())
		{
			Test->AddError(TEXT("Test character was unexpectedly destroyed"));
			return true;
		}
		AGarCharacter* Pawn = Character.Get();
		UGarPhysicsControlComponent* Physics = Pawn->GetPhysicsControl();
		const float Elapsed = World->GetTimeSeconds() - StageStarted;
		if (Stage == 1 && Elapsed > 2.0f)
		{
			Test->TestTrue(TEXT("Always-on physical animation simulates pelvis"), Physics->IsBoneSimulatingPhysics(TEXT("pelvis")));
			Test->TestTrue(TEXT("Controls were created"), !Physics->GetControlNamesInSet(TEXT("All")).IsEmpty());
			Test->TestTrue(TEXT("Physical animation has enabled world-space drives"), HasEnabledWorldDrive(Physics));
			FTransform Body;
			Physics->GetTopBodyTransform(Body);
			RestingPelvis = Body.GetLocation();
			Test->AddInfo(FString::Printf(TEXT("PhysicalAnimation: pelvis=%s capsule=%s controls=%d modifiers=%d"),
				*RestingPelvis.ToString(), *Pawn->GetActorLocation().ToString(),
				Physics->GetControlNamesInSet(TEXT("All")).Num(), Physics->GetBodyModifierNamesInSet(TEXT("All")).Num()));
			// Excite the whole constrained skeleton so the response is large enough to separate
			// from idle animation. A pelvis-only impulse is shared with the other bodies.
			// Chaos queues impulses until simulation; reading velocity immediately is not a test.
			Pawn->GetMesh()->AddImpulseToAllBodiesBelow(FVector(1000, 0, 0), TEXT("pelvis"), true, true);
			Advance(World);
		}
		else if (Stage == 2)
		{
			FTransform Body;
			Physics->GetTopBodyTransform(Body);
			MaxImpulseDisplacement = FMath::Max(MaxImpulseDisplacement, FVector::Dist(Body.GetLocation(), RestingPelvis));
			if (Elapsed < 0.3f) return false;
			Test->TestTrue(TEXT("Physical-animation bodies move after impulse"), MaxImpulseDisplacement > 1.0);
			Advance(World);
		}
		else if (Stage == 3 && Elapsed > 2.0f)
		{
			FTransform Body;
			Physics->GetTopBodyTransform(Body);
			FVector Velocity = FVector::ZeroVector;
			Physics->GetTopBodyVelocity(Velocity);
			const double RecoveryError = FVector::Dist(Body.GetLocation(), RestingPelvis);
			Test->TestTrue(TEXT("World drives recover position after impulse"), RecoveryError < 10.0);
			Test->TestTrue(TEXT("World drives settle velocity after impulse"), Velocity.Size() < 50.0);
			Test->AddInfo(FString::Printf(TEXT("Impulse recovery: sampled max displacement=%.3f error=%.3f speed=%.3f"),
				MaxImpulseDisplacement, RecoveryError, Velocity.Size()));
			Test->TestTrue(TEXT("Activate ragdoll ability"), Pawn->GetAbilitySystemComponent()->TryActivateAbilitiesByTag(FGameplayTagContainer(GarLocomotionActionTags::Unconsious)));
			Pawn->GetMesh()->AddImpulseToAllBodiesBelow(RagdollImpulse, TEXT("pelvis"), true, true);
			Advance(World);
		}
		else if (Stage == 4 || Stage == 6)
		{
			if (bProneRecovery) Pawn->Supine();
			FTransform Body;
			Physics->GetTopBodyTransform(Body);
			if (Physics->IsRagdolling())
			{
				if (!bCheckedRagdollVisual)
				{
					TestRagdollVisualFrame(Pawn);
					bCheckedRagdollVisual = true;
				}
				bSawRagdoll |= Physics->IsBoneSimulatingPhysics(TEXT("pelvis")) && !HasEnabledWorldDrive(Physics);
				bSawRagdollMode |= Pawn->GetMover()->GetMovementModeName() == TEXT("Ragdolling")
					|| Pawn->GetMover()->GetMovementModeName() == TEXT("Ragdolling In Air");
				bSawGrounded |= Physics->GetRagdollStatus().bGrounded;
				LowestPelvis = FMath::Min(LowestPelvis, Body.GetLocation().Z);
				if (Elapsed > 0.15f) MaxFollowError = FMath::Max(MaxFollowError, FVector::Dist2D(Pawn->GetActorLocation(), Body.GetLocation()));
			}
			// The shipped ability automatically ends after settling. Observe its whole active
			// interval, not just a late sample after it has already returned to normal.
			if (ManualExitAfter > 0.0f && Elapsed >= ManualExitAfter && Physics->IsRagdolling())
			{
				const FGameplayTagContainer Tags(GarLocomotionActionTags::Unconsious);
				Pawn->GetAbilitySystemComponent()->CancelAbilities(&Tags);
			}
			if (Elapsed < 0.1f || (Physics->IsRagdolling() && Elapsed < 5.0f)) return false;
			Test->TestTrue(TEXT("Ragdoll simulates with world-space drives disabled"), bSawRagdoll);
			Test->TestTrue(TEXT("Mover entered ragdoll mode"), bSawRagdollMode);
			if (ManualExitAfter <= 0.0f)
			{
				Test->TestTrue(TEXT("Gravity lowered the pelvis"), LowestPelvis < RestingPelvis.Z - 20.0f);
				Test->TestTrue(TEXT("Ragdoll detects floor"), bSawGrounded);
			}
			Test->TestTrue(TEXT("Capsule follows pelvis horizontally"), MaxFollowError < 15.0f);
			Test->AddInfo(FString::Printf(TEXT("Ragdoll: duration=%.3f lowest pelvis=%.3f max follow error=%.3f"),
				Elapsed, LowestPelvis, MaxFollowError));
			const FGameplayTagContainer Tags(GarLocomotionActionTags::Unconsious);
			Pawn->GetAbilitySystemComponent()->CancelAbilities(&Tags);
			bSawRagdoll = bSawRagdollMode = bSawGrounded = false;
			bCheckedRagdollVisual = false;
			LowestPelvis = TNumericLimits<double>::Max();
			MaxFollowError = 0.0;
			Advance(World);
		}
		else if (Stage == 5 || Stage == 7)
		{
			FTransform Body;
			Physics->GetTopBodyTransform(Body);
			MinRecoveryPelvisHeight = FMath::Min(MinRecoveryPelvisHeight, Body.GetLocation().Z - FloorHeight);
			const double CapsuleBottom = Pawn->GetActorLocation().Z - Pawn->GetCapsule()->GetScaledCapsuleHalfHeight();
			MinRecoveryCapsuleHeight = FMath::Min(MinRecoveryCapsuleHeight, CapsuleBottom - FloorHeight);
			MinRecoveryMeshHeight = FMath::Min(MinRecoveryMeshHeight, Pawn->GetMesh()->GetComponentLocation().Z - FloorHeight);
			if (Elapsed >= NextRecoveryLog)
			{
				Test->AddInfo(FString::Printf(TEXT("Recovery stage=%d t=%.3f pelvis=%s capsule=%s mesh=%s relative=%s root=%s target=%s"),
					Stage, Elapsed, *Body.GetLocation().ToString(), *Pawn->GetActorLocation().ToString(),
					*Pawn->GetMesh()->GetComponentLocation().ToString(), *Pawn->GetMesh()->GetRelativeLocation().ToString(),
					*Pawn->GetMesh()->GetBoneLocation(TEXT("root")).ToString(),
					*Physics->GetCachedBonePosition(Pawn->GetMesh(), TEXT("pelvis")).ToString()));
				NextRecoveryLog += 0.5f;
			}
			if (Elapsed < 3.0f) return false;
			Test->TestTrue(TEXT("Pelvis stays above floor throughout recovery"), MinRecoveryPelvisHeight > -2.0);
			Test->TestTrue(TEXT("Capsule stays above floor throughout recovery"), MinRecoveryCapsuleHeight > -3.0);
			Test->TestTrue(TEXT("Animation frame stays above floor throughout recovery"), MinRecoveryMeshHeight > -1.0);
			Test->AddInfo(FString::Printf(TEXT("Recovery minimum heights: pelvis=%.3f capsule bottom=%.3f mesh=%.3f"),
				MinRecoveryPelvisHeight, MinRecoveryCapsuleHeight, MinRecoveryMeshHeight));
			Test->TestFalse(TEXT("Ragdoll exits"), Physics->IsRagdolling());
			Test->TestTrue(TEXT("Physical animation restored after ragdoll"), Physics->IsBoneSimulatingPhysics(TEXT("pelvis")) && HasEnabledWorldDrive(Physics));
			Test->TestTrue(TEXT("Mover exits ragdoll mode"), Pawn->GetMover()->GetMovementModeName() != TEXT("Ragdolling")
				&& Pawn->GetMover()->GetMovementModeName() != TEXT("Ragdolling In Air"));
			if (Stage == 7) return true;
			NextRecoveryLog = 0.0f;
			MinRecoveryPelvisHeight = MinRecoveryCapsuleHeight = MinRecoveryMeshHeight = TNumericLimits<double>::Max();
			Test->TestTrue(TEXT("Activate ragdoll a second time"), Pawn->GetAbilitySystemComponent()->TryActivateAbilitiesByTag(FGameplayTagContainer(GarLocomotionActionTags::Unconsious)));
			Pawn->GetMesh()->AddImpulseToAllBodiesBelow(-RagdollImpulse, TEXT("pelvis"), true, true);
			Advance(World);
		}
		return false;
	}

private:
	void TestRagdollVisualFrame(AGarCharacter* Pawn)
	{
		UGarCharacterMoverComponent* Mover = Pawn->GetMover();
		USkeletalMeshComponent* Mesh = Pawn->GetMesh();
		FMoverSyncState Frame = Mover->GetSyncState();
		const auto* Stance = Frame.SyncStateCollection.FindDataByType<FGarMoverStanceState>();
		if (!Test->TestNotNull(TEXT("Ragdoll has a stance frame"), Stance)) return;
		if (!Test->TestTrue(TEXT("Mover owns the component frame"),
			Mesh->PhysicsTransformUpdateMode == EPhysicsTransformUpdateMode::ComponentTransformIsKinematic)) return;
		if (!Test->TestTrue(TEXT("Ragdoll root body is simulated"),
			Mesh->GetBodyInstance() && Mesh->GetBodyInstance()->IsInstanceSimulatingPhysics())) return;
		struct FBodySnapshot
		{
			FBodyInstance* Body;
			FTransform Transform;
			FVector Velocity;
			FVector AngularVelocity;
		};
		TArray<FBodySnapshot> Bodies;
		for (FBodyInstance* Body : Mesh->Bodies)
		{
			if (Body && Body->IsInstanceSimulatingPhysics())
			{
				Bodies.Add({Body, Body->GetUnrealWorldTransform(), Body->GetUnrealWorldVelocity(),
					Body->GetUnrealWorldAngularVelocityInRadians()});
			}
		}
		Test->TestTrue(TEXT("Ragdoll visual test captures simulated bodies"), !Bodies.IsEmpty());
		// Exercise ordinary presentation/smoothing offsets followed by Mover's finalization.
		// These must change the component frame, without moving the simulated ragdoll bodies.
		FTransform Offset = Mover->GetBaseVisualComponentTransform();
		Offset.AddToTranslation(FVector(13, -9, 7));
		Offset.SetRotation(FQuat(FRotator(11, 17, 23)) * Offset.GetRotation());
		Mesh->SetRelativeTransform(Offset);
		const FMoverAuxStateContext Aux;
		Mover->FinalizeFrame(&Frame, &Aux);
		Test->TestTrue(TEXT("Mover restores both location and rotation of the visual frame"),
			Mesh->GetRelativeTransform().Equals(Mover->GetBaseVisualComponentTransform()));
		for (const FBodySnapshot& Before : Bodies)
		{
			Test->TestTrue(TEXT("Visual frame correction does not teleport a ragdoll body"),
				Before.Body->GetUnrealWorldTransform().Equals(Before.Transform));
			Test->TestTrue(TEXT("Visual frame correction preserves body velocity"),
				Before.Body->GetUnrealWorldVelocity().Equals(Before.Velocity));
			Test->TestTrue(TEXT("Visual frame correction preserves body angular velocity"),
				Before.Body->GetUnrealWorldAngularVelocityInRadians().Equals(Before.AngularVelocity));
		}
	}
	static bool HasEnabledWorldDrive(const UGarPhysicsControlComponent* Physics)
	{
		for (FName Name : Physics->GetControlNamesInSet(TEXT("WorldSpace")))
		{
			FPhysicsControlData Data;
			if (Physics->GetControlData(Name, Data) && Data.bEnabled && Data.LinearStrength > 0.0f) return true;
		}
		return false;
	}
	void Advance(UWorld* World) { ++Stage; StageStarted = World->GetTimeSeconds(); }
	FAutomationTestBase* Test;
	FGarSimulatedMeshMoveWarnings MeshMoveWarnings;
	TWeakObjectPtr<AGarCharacter> Character;
	FVector RestingPelvis = FVector::ZeroVector;
	double MaxImpulseDisplacement = 0.0;
	double Started = FPlatformTime::Seconds();
	float StageStarted = 0;
	int32 Stage = 0;
	bool bSawRagdoll = false;
	bool bSawRagdollMode = false;
	bool bSawGrounded = false;
	bool bCheckedRagdollVisual = false;
	double LowestPelvis = TNumericLimits<double>::Max();
	double MaxFollowError = 0.0;
	double MinRecoveryPelvisHeight = TNumericLimits<double>::Max();
	double MinRecoveryCapsuleHeight = TNumericLimits<double>::Max();
	double MinRecoveryMeshHeight = TNumericLimits<double>::Max();
	float NextRecoveryLog = 0.0f;
	FVector RagdollImpulse;
	float ManualExitAfter;
	bool bProneRecovery;
	bool bChangedFrameTime = false;
	bool bPreviousFixedTimeStep = false;
	double PreviousFixedDeltaTime = 0.0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarPhysicsControlPIETest, "GAR.PhysicsControl.RuntimeTransitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

static void CreateGarPhysicsControlTestWorld()
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>(FVector(0, 0, FGarPhysicsControlPIECommand::FloorHeight - 2), FRotator::ZeroRotator);
	Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Floor->SetActorScale3D(FVector(100, 100, 0.04));
	Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
}

bool FGarPhysicsControlPIETest::RunTest(const FString& Parameters)
{
	CreateGarPhysicsControlTestWorld();
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FGarPhysicsControlPIECommand>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarPhysicsControlMovingRecoveryTest, "GAR.PhysicsControl.MovingRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGarPhysicsControlMovingRecoveryTest::RunTest(const FString& Parameters)
{
	CreateGarPhysicsControlTestWorld();
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FGarPhysicsControlPIECommand>(this, FVector(600, 100, 150)));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarPhysicsControlInterruptedRecoveryTest, "GAR.PhysicsControl.InterruptedRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGarPhysicsControlInterruptedRecoveryTest::RunTest(const FString& Parameters)
{
	CreateGarPhysicsControlTestWorld();
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FGarPhysicsControlPIECommand>(this, FVector(300, 0, 0), 0.15f));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarPhysicsControlProneRecoveryTest, "GAR.PhysicsControl.ProneRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGarPhysicsControlProneRecoveryTest::RunTest(const FString& Parameters)
{
	CreateGarPhysicsControlTestWorld();
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FGarPhysicsControlPIECommand>(this, FVector::ZeroVector, 0.0f, true));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
