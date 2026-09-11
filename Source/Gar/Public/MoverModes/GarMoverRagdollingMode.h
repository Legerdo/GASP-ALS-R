// Copyright (c) SAM-tak. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MovementMode.h"
#include "MoverDataModelTypes.h"
#include "GarMoverRagdollingMode.generated.h"

/** C++ equivalent of the sample's BP_MovementMode_Ragdoll: follow a recorded physics pose.
 * The two legacy mode names remain for GAR's Grounded/InAir gameplay tags only.
 */
UCLASS(Blueprintable, BlueprintType)
class UGarMoverRagdollingMode : public UBaseMovementMode
{
	GENERATED_UCLASS_BODY()

public:
	virtual void SimulationTick_Implementation(const FSimulationTickParams& Params, FMoverTickEndData& OutputState) override;
};
