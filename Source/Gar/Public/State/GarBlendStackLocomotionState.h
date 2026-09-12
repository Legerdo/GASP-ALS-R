// Copyright (c) SAM-tak. All Rights Reserved.
#pragma once

#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"
#include "GarBlendStackLocomotionState.generated.h"

namespace GarAnimationDirectionTags
{
	GAR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Forward)
	GAR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Backward)
	GAR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(LeftRightFoot)
	GAR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(LeftLeftFoot)
	GAR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(RightLeftFoot)
	GAR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(RightRightFoot)
}

/** Animation-only selection context. Does not add tags to the character's ASC. */
USTRUCT(BlueprintType)
struct GAR_API FGarBlendStackLocomotionState
{
	GENERATED_BODY()

	FGarBlendStackLocomotionState();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	FGameplayTagContainer MovementMode;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	FGameplayTagContainer MovementMode_LastFrame;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	FGameplayTagContainer MovementMode_Recent;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	FGameplayTagContainer Stance;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	FGameplayTagContainer Stance_LastFrame;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	FGameplayTagContainer Gait;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	FGameplayTagContainer Gait_LastFrame;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	FGameplayTagContainer MovementDirection;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	FGameplayTagContainer MovementDirection_LastFrame;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	FGameplayTagContainer MovementDirection_Recent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trajectory")
	FRotator Trj_FutureFacing{ForceInit};
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trajectory")
	FVector Trj_CurrentAngularVelocity{ForceInit};
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trajectory")
	bool Trj_IsCircling{false};
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trajectory")
	float Trj_CirclingTime{0.0f};
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trajectory")
	float FutureFacingDelta{0.0f};
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trajectory")
	float FutureFacingDelta_LastFrame{0.0f};
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	FVector2D SlopeAngle{ForceInit};
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	FVector Velocity{ForceInit};
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	float Speed2D{0.0f};
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	FVector2D AO{ForceInit};

	// Recent means the last state held for the debounce interval, not last frame.
	float MovementModeTime{0.0f};
	float MovementDirectionTime{0.0f};
	FVector SmoothedGroundNormal{FVector::UpVector};

	static void UpdateHistory(const FGameplayTagContainer& NewState, FGameplayTagContainer& Current,
		FGameplayTagContainer& LastFrame, FGameplayTagContainer& Recent, float& TimeInState,
		float DeltaTime, float RecentTimeLimit);
};
