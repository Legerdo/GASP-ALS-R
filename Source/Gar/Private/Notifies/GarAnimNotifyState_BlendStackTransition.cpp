// Copyright (c) SAM-tak. All Rights Reserved.
#include "Notifies/GarAnimNotifyState_BlendStackTransition.h"
#include "Animation/AnimNotifyLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "GarAnimationInstance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GarAnimNotifyState_BlendStackTransition)

void UGarAnimNotifyState_BlendStackTransition::NotifyTick(USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);
	if (!MeshComp || UAnimNotifyLibrary::IsBlendingOut(EventReference)) return;
	if (auto* Instance = Cast<UGarAnimationInstance>(MeshComp->GetAnimInstance()))
	{
		if (!GaitNotEqual.IsValid() || !Instance->GetCurrentGameplayTags().HasTag(GaitNotEqual))
		{
			Instance->RequestBlendStackTransition(bToLoop);
		}
	}
}

FString UGarAnimNotifyState_BlendStackTransition::GetNotifyName_Implementation() const
{
	return FString(bToLoop ? TEXT("To Loop") : TEXT("Re-Transition"))
		+ (GaitNotEqual.IsValid() ? TEXT(" / Not ") + GaitNotEqual.ToString() : TEXT(" / Always"));
}
