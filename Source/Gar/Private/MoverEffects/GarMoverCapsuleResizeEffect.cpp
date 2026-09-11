// Copyright (c) SAM-tak. All Rights Reserved.

#include "MoverEffects/GarMoverCapsuleResizeEffect.h"

#include "Components/CapsuleComponent.h"
#include "GarCharacter.h"
#include "GarCharacterMoverComponent.h"
#include "MoveLibrary/MoverBlackboard.h"
#include "MoverDataModelTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GarMoverCapsuleResizeEffect)

bool FGarMoverCapsuleResizeEffect::ApplyMovementEffect(FApplyMovementEffectParams& Params, FMoverSyncState& OutputState)
{
	UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Params.UpdatedComponent);
	AGarCharacter* Character = Capsule ? Cast<AGarCharacter>(Capsule->GetOwner()) : nullptr;
	if (!Character || !StanceState.bInitialized) return false;
	UGarCharacterMoverComponent* Mover = Character->GetMover();
	if (!Mover) return false;
	const float HeightDelta = StanceState.CapsuleHalfHeight - PreviousHalfHeight;
	const bool bGeometryChanged = Mover->ApplyStanceState(StanceState, HeightDelta);
	OutputState.SyncStateCollection.FindOrAddMutableDataByType<FGarMoverStanceState>() = StanceState;
	if (bGeometryChanged)
	{
		// Invalidate only during simulation, together with the sync state's base. Prone geometry
		// and weld/collision changes also require a new floor query, even with zero height delta.
		if (UMoverBlackboard* Blackboard = Mover->GetSimBlackboard_Mutable())
		{
			Blackboard->Invalidate(CommonBlackboard::LastFloorResult);
			Blackboard->Invalidate(CommonBlackboard::LastFoundDynamicMovementBase);
		}
		FMoverDefaultSyncState& Sync = OutputState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>();
		Sync.SetTransforms_WorldSpace(Capsule->GetComponentLocation(), Capsule->GetComponentRotation(),
			Sync.GetVelocity_WorldSpace(), Sync.GetAngularVelocityDegrees_WorldSpace(), nullptr);
	}
	return true;
}

void FGarMoverCapsuleResizeEffect::NetSerialize(FArchive& Ar)
{
	Super::NetSerialize(Ar);
	bool bSuccess = true;
	StanceState.NetSerialize(Ar, nullptr, bSuccess);
	Ar << PreviousHalfHeight;
}
