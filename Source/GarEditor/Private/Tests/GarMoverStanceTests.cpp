// Copyright (c) SAM-tak. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/App.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "AbilitySystemComponent.h"
#include "GarCharacter.h"
#include "GarCharacterMoverComponent.h"
#include "GarGameplayTags.h"
#include "MoveLibrary/BasedMovementUtils.h"
#include "MoveLibrary/FloorQueryUtils.h"
#include "MoveLibrary/MoverBlackboard.h"
#include "MoverEffects/GarMoverCapsuleResizeEffect.h"
#include "State/GarCharacterMoverInputs.h"
#include "State/GarMoverStanceState.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarMoverStanceStateTest, "GAR.Mover.Stance.StateSerialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGarMoverStanceStateTest::RunTest(const FString& Parameters)
{
	FGarMoverCapsuleResizeEffect Source;
	Source.PreviousHalfHeight = 90.0f;
	Source.StanceState.bInitialized = true;
	Source.StanceState.CapsuleRadius = 30.0f;
	Source.StanceState.CapsuleHalfHeight = 55.0f;
	Source.StanceState.ProneRadius = 30.0f;
	Source.StanceState.ProneHalfHeight = 45.0f;
	Source.StanceState.ProneOffsetX = 17.0f;
	Source.StanceState.EyeHeight = 45.0f;
	Source.StanceState.bProneCollisionEnabled = true;
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	Source.NetSerialize(Writer);
	FGarMoverCapsuleResizeEffect Restored;
	FMemoryReader Reader(Bytes);
	Restored.NetSerialize(Reader);
	TestFalse(TEXT("Effect serialization is valid"), Reader.IsError());
	TestTrue(TEXT("Effect retains all stance geometry and eye height"), Source.StanceState == Restored.StanceState);
	TestEqual(TEXT("Effect retains the previous pivot height"), Restored.PreviousHalfHeight, 90.0f);

	FGarMoverStanceState Standing = Source.StanceState;
	Standing.CapsuleHalfHeight = 90.0f;
	Standing.EyeHeight = 65.0f;
	Standing.bProneCollisionEnabled = false;
	FGarMoverStanceState Blended;
	Blended.Interpolate(Source.StanceState, Standing, 0.5f);
	TestEqual(TEXT("Proxy capsule interpolates"), Blended.CapsuleHalfHeight, 72.5f);
	TestEqual(TEXT("Proxy eye height interpolates with capsule"), Blended.EyeHeight, 55.0f);
	TestFalse(TEXT("Proxy collision uses nearest frame"), Blended.bProneCollisionEnabled);
	TestTrue(TEXT("Geometry mismatch requests reconcile"), Source.StanceState.ShouldReconcile(Standing));
	Blended.Merge(Standing);
	TestTrue(TEXT("Merge retains complete geometry"), Blended == Standing);

	FGarCharacterMoverInputs Locked;
	Locked.bBlockCapsuleResize = true;
	Bytes.Reset();
	FMemoryWriter InputWriter(Bytes);
	bool bSuccess = true;
	Locked.NetSerialize(InputWriter, nullptr, bSuccess);
	FGarCharacterMoverInputs InputRestored;
	FMemoryReader InputReader(Bytes);
	InputRestored.NetSerialize(InputReader, nullptr, bSuccess);
	TestTrue(TEXT("Resize lock survives network replay"), bSuccess && Locked == InputRestored);
	InputRestored.Interpolate(Locked, FGarCharacterMoverInputs(), 1.0f);
	TestFalse(TEXT("Resize lock clears after interpolation"), InputRestored.bBlockCapsuleResize);
	InputRestored.Merge(Locked);
	TestTrue(TEXT("Merge retains resize lock"), InputRestored.bBlockCapsuleResize);
	return true;
}

class FGarMoverStancePIECommand : public IAutomationLatentCommand
{
public:
	explicit FGarMoverStancePIECommand(FAutomationTestBase* InTest, double InStep, bool bInMovableFloor)
		: Test(InTest), Step(InStep), bMovableFloor(bInMovableFloor) {}
	virtual ~FGarMoverStancePIECommand() override
	{
		if (bChangedTime)
		{
			FApp::SetUseFixedTimeStep(bPreviousFixedTime);
			FApp::SetFixedDeltaTime(PreviousDeltaTime);
		}
	}
	virtual bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 120.0)
		{
			Test->AddError(TEXT("Timed out waiting for stance simulation"));
			return true;
		}
		UWorld* World = GEditor->PlayWorld;
		if (!World || !World->HasBegunPlay()) return false;
		if (Stage == 0)
		{
			APlayerController* Controller = World->GetFirstPlayerController();
			if (!Controller) return false;
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Pawn = World->SpawnActor<AGarCharacter>(LoadClass<AGarCharacter>(nullptr,
				TEXT("/GAR/Core/B_Gar_Character.B_Gar_Character_C")), FVector(0, 0, 600), FRotator::ZeroRotator, Spawn);
			if (!Test->TestNotNull(TEXT("Spawn GAR character"), Pawn.Get())) return true;
			Controller->Possess(Pawn.Get());
			// The entire test runs without Character Tick: Mover alone must advance stance.
			Pawn->SetActorTickEnabled(false);
			Pawn->SetInputStance(GarStanceTags::Standing);
			bPreviousFixedTime = FApp::UseFixedTimeStep();
			PreviousDeltaTime = FApp::GetFixedDeltaTime();
			bChangedTime = true;
			FApp::SetUseFixedTimeStep(true);
			FApp::SetFixedDeltaTime(Step);
			Advance(World);
			return false;
		}
		if (!Pawn.IsValid()) { Test->AddError(TEXT("Stance test pawn was destroyed")); return true; }
		UCapsuleComponent* Capsule = Pawn->GetCapsule();
		UGarCharacterMoverComponent* Mover = Pawn->GetMover();
		if (bMovableFloor && Stage >= 2)
		{
			const auto* Sync = Mover->GetSyncState().SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
			if (Sync && Sync->GetMovementBase())
			{
				FRelativeBaseInfo BaseInfo;
				if (!Test->TestTrue(TEXT("Based stance frame retains its movement base cache"),
					Mover->GetSimBlackboard_Mutable()->TryGet(CommonBlackboard::LastFoundDynamicMovementBase, BaseInfo)
					&& BaseInfo.UsesSameBase(Sync->GetMovementBase(), Sync->GetMovementBaseBoneName()))) return true;
			}
		}
		if (Stage >= 2)
		{
			MinCapsuleBottom = FMath::Min(MinCapsuleBottom, Capsule->GetComponentLocation().Z - Capsule->GetScaledCapsuleHalfHeight());
			MinMeshHeight = FMath::Min(MinMeshHeight, Pawn->GetMesh()->GetComponentLocation().Z);
		}
		if (World->GetTimeSeconds() - StageStarted < 2.0f) return false;
		const auto* State = Mover->GetSyncState().SyncStateCollection.FindDataByType<FGarMoverStanceState>();
		if (!Test->TestNotNull(TEXT("Persistent stance state exists"), State)) return true;
		Test->TestTrue(TEXT("Stance state initialized"), State->bInitialized);
		Test->TestEqual(TEXT("Capsule matches simulation"), Capsule->GetUnscaledCapsuleHalfHeight(), State->CapsuleHalfHeight);
		Test->TestEqual(TEXT("Eye height matches simulation"), Pawn->BaseEyeHeight, State->EyeHeight);
		if (Stage == 1)
		{
			if (bMovableFloor)
			{
				const auto* Sync = Mover->GetSyncState().SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
				if (!Test->TestTrue(TEXT("Character is based on the movable floor"),
					Sync && UBasedMovementUtils::IsADynamicBase(Sync->GetMovementBase())
					&& Sync->GetMovementBase()->GetOwner()->IsA<AStaticMeshActor>())) return true;
			}
			Standing = *State;
			Pawn->SetInputStance(GarStanceTags::Crouching);
		}
		else if (Stage == 2)
		{
			Test->TestTrue(TEXT("Crouch resizes with Character Tick disabled"), State->CapsuleHalfHeight < Standing.CapsuleHalfHeight);
			Test->TestTrue(TEXT("Crouch lowers eyes with Character Tick disabled"), State->EyeHeight < Standing.EyeHeight);
			LockedHeight = State->CapsuleHalfHeight;
			Pawn->GetAbilitySystemComponent()->AddLooseGameplayTag(GarStateFlagTags::BlockUpdateCapsuleSize);
			Pawn->SetInputStance(GarStanceTags::LyingFront);
		}
		else if (Stage == 3)
		{
			Test->TestEqual(TEXT("Resize lock preserves capsule"), State->CapsuleHalfHeight, LockedHeight);
			Pawn->GetAbilitySystemComponent()->RemoveLooseGameplayTag(GarStateFlagTags::BlockUpdateCapsuleSize);
		}
		else if (Stage == 4)
		{
			Test->TestTrue(TEXT("Prone resizes capsule after unlocking"), State->CapsuleHalfHeight < LockedHeight);
			Test->TestTrue(TEXT("Prone collision is welded"), Pawn->GetProneCapsule()->IsWelded());
			Prone = *State;
			Pawn->SetInputStance(GarStanceTags::LyingBack);
		}
		else if (Stage == 5)
		{
			Test->TestEqual(TEXT("Supine retains prone geometry"), State->CapsuleHalfHeight, Prone.CapsuleHalfHeight);
			Pawn->SetInputStance(GarStanceTags::Standing);
		}
		else if (Stage == 6)
		{
			Test->TestEqual(TEXT("Standing height restored"), State->CapsuleHalfHeight, Standing.CapsuleHalfHeight);
			Test->TestEqual(TEXT("Standing eye height restored"), State->EyeHeight, Standing.EyeHeight);
			Test->TestFalse(TEXT("Standing disables prone collision"), Pawn->GetProneCapsule()->IsWelded());
			Test->TestTrue(TEXT("No capsule floor penetration"), MinCapsuleBottom > 499.0);
			Test->TestTrue(TEXT("No visual root below floor"), MinMeshHeight > 499.0);
			if (bMovableFloor) TestCollisionOnlyEffect(Mover, *State);
			const FVector BeforeRestore = Pawn->GetActorLocation();
			FMoverSyncState PresentationState = Mover->GetSyncState();
			PresentationState.SyncStateCollection.FindOrAddMutableDataByType<FGarMoverStanceState>() = Prone;
			FRelativeBaseInfo BeforeBase;
			const bool bHadBase = Mover->GetSimBlackboard_Mutable()->TryGet(CommonBlackboard::LastFoundDynamicMovementBase, BeforeBase);
			// Exercise the finalization callback with a different (interpolated) stance. Presentation
			// must not discard simulation data already scheduled for the later based movement tick.
			Mover->OnPostFinalize.Broadcast(PresentationState, FMoverAuxStateContext());
			Test->TestEqual(TEXT("Frame restoration restores shape"), Capsule->GetUnscaledCapsuleHalfHeight(), Prone.CapsuleHalfHeight);
			Test->TestTrue(TEXT("Frame restoration does not add pivot motion twice"), Pawn->GetActorLocation().Equals(BeforeRestore));
			if (bMovableFloor)
			{
				FRelativeBaseInfo AfterBase;
				const bool bRetainedBase = Test->TestTrue(TEXT("PostFinalize preserves the simulation movement base cache"), bHadBase
					&& Mover->GetSimBlackboard_Mutable()->TryGet(CommonBlackboard::LastFoundDynamicMovementBase, AfterBase)
					&& BeforeBase.UsesSameBase(AfterBase)
					&& BeforeBase.ContactLocalPosition.Equals(AfterBase.ContactLocalPosition)
					&& BeforeBase.WorldspaceOffsetFromContactPos.Equals(AfterBase.WorldspaceOffsetFromContactPos));
				if (bRetainedBase)
				{
					const FVector BaseDelta(10, 0, 0);
					BeforeBase.MovementBase->AddWorldOffset(BaseDelta);
					UBasedMovementUtils::UpdateSimpleBasedMovement(Mover);
					Test->TestTrue(TEXT("Based movement still follows the floor after stance finalization"),
						Pawn->GetActorLocation().Equals(BeforeRestore + BaseDelta, 0.1));
				}
			}
			// Based movement can finalize a new frame, so do not retain a pointer into the sync buffer.
			Mover->ApplyStanceState(Standing);
			return true;
		}
		Advance(World);
		return false;
	}
private:
	void TestCollisionOnlyEffect(UGarCharacterMoverComponent* Mover, const FGarMoverStanceState& State)
	{
		UMoverBlackboard* Blackboard = Mover->GetSimBlackboard_Mutable();
		FRelativeBaseInfo Base;
		FFloorCheckResult Floor;
		const bool bHadBase = Blackboard->TryGet(CommonBlackboard::LastFoundDynamicMovementBase, Base);
		const bool bHadFloor = Blackboard->TryGet(CommonBlackboard::LastFloorResult, Floor);
		Test->TestTrue(TEXT("Collision-only effect starts with a valid floor and base"), bHadBase && bHadFloor);
		FMoverSyncState OutputState = Mover->GetSyncState();
		FGarMoverCapsuleResizeEffect Effect;
		Effect.StanceState = State;
		Effect.StanceState.ProneOffsetX += 1.0f;
		Effect.PreviousHalfHeight = State.CapsuleHalfHeight; // Deliberately no pivot adjustment.
		FApplyMovementEffectParams Params;
		Params.MoverComp = Mover;
		Params.UpdatedComponent = Pawn->GetCapsule();
		Params.UpdatedPrimitive = Pawn->GetCapsule();
		Test->TestTrue(TEXT("Collision-only effect applies"), Effect.ApplyMovementEffect(Params, OutputState));
		const auto* Sync = OutputState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
		Test->TestTrue(TEXT("Collision-only effect clears the output movement base"), Sync && !Sync->GetMovementBase());
		FRelativeBaseInfo AfterBase;
		FFloorCheckResult AfterFloor;
		Test->TestFalse(TEXT("Collision-only effect invalidates base cache"), Blackboard->TryGet(CommonBlackboard::LastFoundDynamicMovementBase, AfterBase));
		Test->TestFalse(TEXT("Collision-only effect invalidates floor cache"), Blackboard->TryGet(CommonBlackboard::LastFloorResult, AfterFloor));
		// This isolated effect used a copied output state. Restore the live frame before ticking again.
		Mover->ApplyStanceState(State);
		if (bHadBase) Blackboard->Set(CommonBlackboard::LastFoundDynamicMovementBase, Base);
		if (bHadFloor) Blackboard->Set(CommonBlackboard::LastFloorResult, Floor);
	}
	void Advance(UWorld* World) { ++Stage; StageStarted = World->GetTimeSeconds(); }
	FAutomationTestBase* Test;
	TWeakObjectPtr<AGarCharacter> Pawn;
	FGarMoverStanceState Standing, Prone;
	double Step;
	double Started = FPlatformTime::Seconds();
	double PreviousDeltaTime = 0.0;
	double MinCapsuleBottom = TNumericLimits<double>::Max();
	double MinMeshHeight = TNumericLimits<double>::Max();
	float LockedHeight = 0.0f;
	float StageStarted = 0.0f;
	int32 Stage = 0;
	bool bChangedTime = false;
	bool bPreviousFixedTime = false;
	bool bMovableFloor = false;
};

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FGarMoverStancePIETest, "GAR.Mover.Stance.Runtime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FGarMoverStancePIETest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	Names.Add(TEXT("60FPS")); Commands.Add(TEXT("60"));
	Names.Add(TEXT("15FPS")); Commands.Add(TEXT("15"));
	Names.Add(TEXT("Movable60FPS")); Commands.Add(TEXT("60.Movable"));
	Names.Add(TEXT("Movable15FPS")); Commands.Add(TEXT("15.Movable"));
}

bool FGarMoverStancePIETest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	auto* Floor = World->SpawnActor<AStaticMeshActor>(FVector(0, 0, 498), FRotator::ZeroRotator);
	Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Floor->SetActorScale3D(FVector(100, 100, 0.04));
	const bool bMovableFloor = Parameters.EndsWith(TEXT(".Movable"));
	Floor->GetStaticMeshComponent()->SetMobility(bMovableFloor ? EComponentMobility::Movable : EComponentMobility::Static);
	Floor->GetStaticMeshComponent()->SetCollisionProfileName(bMovableFloor ? TEXT("BlockAllDynamic") : TEXT("BlockAll"));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FGarMoverStancePIECommand>(this, 1.0 / FCString::Atof(*Parameters), bMovableFloor));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
