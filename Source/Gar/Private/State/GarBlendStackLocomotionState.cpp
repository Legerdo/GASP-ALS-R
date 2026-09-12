// Copyright (c) SAM-tak. All Rights Reserved.
#include "State/GarBlendStackLocomotionState.h"
#include "GarGameplayTags.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GarBlendStackLocomotionState)

namespace GarAnimationDirectionTags
{
	UE_DEFINE_GAMEPLAY_TAG(Forward, "Gar.Animation.MovementDirection.F")
	UE_DEFINE_GAMEPLAY_TAG(Backward, "Gar.Animation.MovementDirection.B")
	UE_DEFINE_GAMEPLAY_TAG(LeftRightFoot, "Gar.Animation.MovementDirection.LR")
	UE_DEFINE_GAMEPLAY_TAG(LeftLeftFoot, "Gar.Animation.MovementDirection.LL")
	UE_DEFINE_GAMEPLAY_TAG(RightLeftFoot, "Gar.Animation.MovementDirection.RL")
	UE_DEFINE_GAMEPLAY_TAG(RightRightFoot, "Gar.Animation.MovementDirection.RR")
}

FGarBlendStackLocomotionState::FGarBlendStackLocomotionState()
	: MovementMode(GarLocomotionModeTags::Grounded), MovementMode_LastFrame(MovementMode), MovementMode_Recent(MovementMode),
	Stance(GarStanceTags::Standing), Stance_LastFrame(Stance),
	Gait(GarGaitTags::Running), Gait_LastFrame(Gait),
	MovementDirection(GarAnimationDirectionTags::Forward), MovementDirection_LastFrame(MovementDirection), MovementDirection_Recent(MovementDirection)
{}

void FGarBlendStackLocomotionState::UpdateHistory(const FGameplayTagContainer& NewState, FGameplayTagContainer& Current,
	FGameplayTagContainer& LastFrame, FGameplayTagContainer& Recent, float& TimeInState, float DeltaTime, float RecentTimeLimit)
{
	LastFrame = Current;
	Current = NewState;
	if (Current != LastFrame)
	{
		TimeInState = 0.0f;
	}
	else
	{
		TimeInState += FMath::Max(DeltaTime, 0.0f);
		if (TimeInState >= RecentTimeLimit) Recent = Current;
	}
}
