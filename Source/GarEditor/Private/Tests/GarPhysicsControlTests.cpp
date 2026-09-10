// Copyright (c) SAM-tak. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "AbilitySystemComponent.h"
#include "GarCharacter.h"
#include "GarCharacterMoverComponent.h"
#include "GarPhysicsControlComponent.h"
#include "GarGameplayTags.h"
#include "State/GarCharacterMoverInputs.h"
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

class FGarPhysicsControlPIECommand : public IAutomationLatentCommand
{
public:
	explicit FGarPhysicsControlPIECommand(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override
	{
		UWorld* World = GEditor->PlayWorld;
		if (FPlatformTime::Seconds() - Started > 120.0)
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
			Character = World->SpawnActor<AGarCharacter>(Class, FVector(0, 0, 100), FRotator::ZeroRotator, Spawn);
			if (!Test->TestNotNull(TEXT("Spawn GAR character"), Character.Get())) return true;
			Controller->Possess(Character.Get());
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
			Advance(World);
		}
		else if (Stage == 4 || Stage == 6)
		{
			FTransform Body;
			Physics->GetTopBodyTransform(Body);
			if (Physics->IsRagdolling())
			{
				bSawRagdoll |= Physics->IsBoneSimulatingPhysics(TEXT("pelvis")) && !HasEnabledWorldDrive(Physics);
				bSawRagdollMode |= Pawn->GetMover()->GetMovementModeName() == TEXT("Ragdolling")
					|| Pawn->GetMover()->GetMovementModeName() == TEXT("Ragdolling In Air");
				bSawGrounded |= Physics->IsRagdollingAndGroundedAndAged();
				LowestPelvis = FMath::Min(LowestPelvis, Body.GetLocation().Z);
				if (Elapsed > 0.15f) MaxFollowError = FMath::Max(MaxFollowError, FVector::Dist2D(Pawn->GetActorLocation(), Body.GetLocation()));
			}
			// The shipped ability automatically ends after settling. Observe its whole active
			// interval, not just a late sample after it has already returned to normal.
			if (Elapsed < 0.1f || (Physics->IsRagdolling() && Elapsed < 5.0f)) return false;
			Test->TestTrue(TEXT("Ragdoll simulates with world-space drives disabled"), bSawRagdoll);
			Test->TestTrue(TEXT("Mover entered ragdoll mode"), bSawRagdollMode);
			Test->TestTrue(TEXT("Gravity lowered the pelvis"), LowestPelvis < RestingPelvis.Z - 20.0f);
			Test->TestTrue(TEXT("Capsule follows pelvis horizontally"), MaxFollowError < 15.0f);
			Test->TestTrue(TEXT("Ragdoll detects floor"), bSawGrounded);
			Test->AddInfo(FString::Printf(TEXT("Ragdoll: duration=%.3f lowest pelvis=%.3f max follow error=%.3f"),
				Elapsed, LowestPelvis, MaxFollowError));
			const FGameplayTagContainer Tags(GarLocomotionActionTags::Unconsious);
			Pawn->GetAbilitySystemComponent()->CancelAbilities(&Tags);
			bSawRagdoll = bSawRagdollMode = bSawGrounded = false;
			LowestPelvis = TNumericLimits<double>::Max();
			MaxFollowError = 0.0;
			Advance(World);
		}
		else if ((Stage == 5 || Stage == 7) && Elapsed > 2.0f)
		{
			Test->TestFalse(TEXT("Ragdoll exits"), Physics->IsRagdolling());
			Test->TestTrue(TEXT("Physical animation restored after ragdoll"), Physics->IsBoneSimulatingPhysics(TEXT("pelvis")) && HasEnabledWorldDrive(Physics));
			Test->TestTrue(TEXT("Mover exits ragdoll mode"), Pawn->GetMover()->GetMovementModeName() != TEXT("Ragdolling")
				&& Pawn->GetMover()->GetMovementModeName() != TEXT("Ragdolling In Air"));
			if (Stage == 7) return true;
			Test->TestTrue(TEXT("Activate ragdoll a second time"), Pawn->GetAbilitySystemComponent()->TryActivateAbilitiesByTag(FGameplayTagContainer(GarLocomotionActionTags::Unconsious)));
			Advance(World);
		}
		return false;
	}

private:
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
	TWeakObjectPtr<AGarCharacter> Character;
	FVector RestingPelvis = FVector::ZeroVector;
	double MaxImpulseDisplacement = 0.0;
	double Started = FPlatformTime::Seconds();
	float StageStarted = 0;
	int32 Stage = 0;
	bool bSawRagdoll = false;
	bool bSawRagdollMode = false;
	bool bSawGrounded = false;
	double LowestPelvis = TNumericLimits<double>::Max();
	double MaxFollowError = 0.0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarPhysicsControlPIETest, "GAR.PhysicsControl.RuntimeTransitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGarPhysicsControlPIETest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>(FVector(0, 0, -50), FRotator::ZeroRotator);
	Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Floor->SetActorScale3D(FVector(100, 100, 1));
	Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FGarPhysicsControlPIECommand>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
