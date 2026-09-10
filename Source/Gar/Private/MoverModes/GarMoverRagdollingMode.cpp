// Copyright (c) SAM-tak. All Rights Reserved.

#include "MoverModes/GarMoverRagdollingMode.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "MoverComponent.h"
#include "MoveLibrary/MovementUtils.h"
#include "State/GarCharacterMoverInputs.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GarMoverRagdollingMode)

UGarMoverRagdollingMode::UGarMoverRagdollingMode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	GameplayTags.AddTag(GarLocomotionModeTags::Grounded);
}

void UGarMoverRagdollingMode::SimulationTick_Implementation(const FSimulationTickParams& Params, FMoverTickEndData& OutputState)
{
	USceneComponent* UpdatedComponent = Params.MovingComps.UpdatedComponent.Get();
	const FMoverDefaultSyncState* Start = Params.StartState.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
	const FGarCharacterMoverInputs* Inputs = Params.StartState.InputCmd.InputCollection.FindDataByType<FGarCharacterMoverInputs>();
	if (!UpdatedComponent || !Start)
	{
		return;
	}

	FMoverDefaultSyncState& Output = OutputState.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>();
	Output = *Start;
	OutputState.MovementEndState.RemainingMs = 0.0f;
	const float DeltaSeconds = Params.TimeStep.StepMs * 0.001f;
	if (!Inputs || !Inputs->bHasRagdollTransform || DeltaSeconds <= UE_SMALL_NUMBER)
	{
		// A queued transition may precede the first recorded pose. Never chase world origin.
		Output.SetTransforms_WorldSpace(Start->GetLocation_WorldSpace(), Start->GetOrientation_WorldSpace(),
			FVector::ZeroVector, FVector::ZeroVector, nullptr);
		return;
	}

	UMoverComponent* Mover = GetMoverComponent();
	const FVector Up = Mover->GetUpDirection();
	FVector TargetLocation = Inputs->RagdollTransform.GetLocation();
	const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(UpdatedComponent);
	const float Radius = Capsule ? Capsule->GetScaledCapsuleRadius() : 30.0f;
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 86.0f;
	constexpr float FloorGap = 1.9f;

	// Sample: sphere trace from the pelvis down one capsule half-height and place the
	// capsule above the hit. Use GAR's actual dimensions/up vector instead of sample constants.
	FHitResult FloorHit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(GarRagdollFloor), false, Mover->GetOwner());
	const bool bGrounded = UpdatedComponent->GetWorld()->SweepSingleByChannel(FloorHit, TargetLocation,
		TargetLocation - Up * (HalfHeight + FloorGap), FQuat::Identity, ECC_Visibility,
		FCollisionShape::MakeSphere(Radius), Query) && FVector::DotProduct(FloorHit.ImpactNormal, Up) > 0.1f;
	if (bGrounded)
	{
		const float HeightAboveFloor = FVector::DotProduct(TargetLocation - FloorHit.ImpactPoint, Up);
		TargetLocation += Up * FMath::Max(0.0f, HalfHeight + FloorGap - HeightAboveFloor);
	}

	const FRotator StartingOrientation = Start->GetOrientation_WorldSpace();
	const FQuat TargetOrientation = Inputs->OrientationIntent.IsNearlyZero()
		? StartingOrientation.Quaternion()
		: FRotationMatrix::MakeFromZX(Up, Inputs->OrientationIntent).ToQuat();
	const FVector MoveDelta = TargetLocation - Start->GetLocation_WorldSpace();
	FHitResult MoveHit;
	// The physics bodies provide collision response. Sweeping the capsule here would stop it
	// following the body and feed a competing movement response back into the skeletal mesh.
	UMovementUtils::TrySafeMoveAndSlideUpdatedComponentNoMovementRecord(Params.MovingComps,
		MoveDelta, TargetOrientation, false, MoveHit, ETeleportType::None, true);

	const FVector Velocity = (UpdatedComponent->GetComponentLocation() - Start->GetLocation_WorldSpace()) / DeltaSeconds;
	const FVector AngularVelocity = UMovementUtils::ComputeAngularVelocityDegrees(
		StartingOrientation, TargetOrientation.Rotator(), DeltaSeconds, -1.0f);
	Output.SetTransforms_WorldSpace(UpdatedComponent->GetComponentLocation(),
		UpdatedComponent->GetComponentRotation(), Velocity, AngularVelocity, nullptr);
	Output.MoveDirectionIntent = FVector::ZeroVector;
	UpdatedComponent->ComponentVelocity = Velocity;

	UMoverBlackboard* Blackboard = Mover->GetSimBlackboard_Mutable();
	Blackboard->Invalidate(CommonBlackboard::LastFloorResult);
	Blackboard->Invalidate(CommonBlackboard::LastFoundDynamicMovementBase);

	// Keep existing GAR animation/ability tag consumers working without two chase algorithms.
	if (bGrounded != GameplayTags.HasTag(GarLocomotionModeTags::Grounded))
	{
		OutputState.MovementEndState.NextModeName = bGrounded ? TEXT("Ragdolling") : TEXT("Ragdolling In Air");
	}
}
