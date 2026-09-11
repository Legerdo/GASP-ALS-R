// Copyright (c) SAM-tak. All Rights Reserved.

#pragma once

#include "InstantMovementEffect.h"
#include "State/GarMoverStanceState.h"
#include "GarMoverCapsuleResizeEffect.generated.h"

/** Stance pivot adjustment for GAR's game-thread Mover. Not an actor/physics teleport. */
USTRUCT()
struct GAR_API FGarMoverCapsuleResizeEffect : public FInstantMovementEffect
{
	GENERATED_BODY()

	UPROPERTY()
	FGarMoverStanceState StanceState;

	UPROPERTY()
	float PreviousHalfHeight = 0.0f;

	virtual bool ApplyMovementEffect(FApplyMovementEffectParams& Params, FMoverSyncState& OutputState) override;
	virtual FInstantMovementEffect* Clone() const override { return new FGarMoverCapsuleResizeEffect(*this); }
	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
	virtual void NetSerialize(FArchive& Ar) override;
	virtual FString ToSimpleString() const override { return TEXT("GAR capsule resize"); }
};
