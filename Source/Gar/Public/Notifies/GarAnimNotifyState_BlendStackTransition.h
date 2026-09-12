// Copyright (c) SAM-tak. All Rights Reserved.
#pragma once

#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "GameplayTagContainer.h"
#include "GarAnimNotifyState_BlendStackTransition.generated.h"

/** Sample early-transition windows adapted to GAR, without a Sample ABP interface or gait enum. */
UCLASS(DisplayName = "GAR Blend Stack Transition")
class GAR_API UGarAnimNotifyState_BlendStackTransition : public UAnimNotifyState
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transition")
	bool bToLoop{false};

	/** Empty means unconditional. Otherwise request a transition when this gait is no longer active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Transition", Meta = (Categories = "Gar.Gait"))
	FGameplayTag GaitNotEqual;

	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime,
		const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;
};
