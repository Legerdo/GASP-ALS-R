// Copyright (c) SAM-tak. All Rights Reserved.

#pragma once

#include "Components/SkeletalMeshComponent.h"
#include "GarSkeletalMeshComponent.generated.h"

/** Keeps Mover-owned component-frame updates separate from Physics Control's simulated bodies. */
UCLASS()
class GAR_API UGarSkeletalMeshComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()

protected:
	virtual bool MoveComponentImpl(const FVector& Delta, const FQuat& NewRotation, bool bSweep,
		FHitResult* OutHit, EMoveComponentFlags MoveFlags, ETeleportType Teleport) override;
};
