// Copyright (c) SAM-tak. All Rights Reserved.

#include "GarSkeletalMeshComponent.h"

#include "PhysicsEngine/BodyInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GarSkeletalMeshComponent)

bool UGarSkeletalMeshComponent::MoveComponentImpl(const FVector& Delta, const FQuat& NewRotation, bool bSweep,
	FHitResult* OutHit, EMoveComponentFlags MoveFlags, ETeleportType Teleport)
{
	if (Teleport == ETeleportType::None
		&& PhysicsTransformUpdateMode == EPhysicsTransformUpdateMode::ComponentTransformIsKinematic)
	{
		// In this mode Mover owns the component frame, while Physics Control owns the bodies.
		// Check the root body directly: IsSimulatingPhysics(NAME_None) deliberately returns false
		// for a kinematic component frame, even when every ragdoll body is simulated.
		const FBodyInstance* RootBody = GetBodyInstance();
		if (RootBody && RootBody->IsInstanceSimulatingPhysics())
		{
			MoveFlags |= MOVECOMP_SkipPhysicsMove;
		}
	}
	// Keep normal animated/kinematic-body movement and explicitly requested teleports unchanged.
	return Super::MoveComponentImpl(Delta, NewRotation, bSweep, OutHit, MoveFlags, Teleport);
}
