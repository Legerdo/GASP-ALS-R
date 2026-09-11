// Fill out your copyright notice in the Description page of Project Settings.

#include "Abilities/Actions/GarGameplayAbility_Ragdolling.h"
#include "Abilities/Tasks/GarAbilityTask_Tick.h"
#include "GarCharacter.h"
#include "GarCharacterMoverComponent.h"
#include "GarAnimationInstance.h"
#include "GarAbilitySystemComponent.h"
#include "CharacterTasks/GarRagdollingTask.h"
#include "LinkedAnimLayers/GarRagdollingAnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/GarOverrideModeComponent.h"
#include "Net/UnrealNetwork.h"
#include "GarGameplayTags.h"
#include "GarConstants.h"
#include "Utility/GarMath.h"
#include "Utility/GarLog.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GarGameplayAbility_Ragdolling)

FVector UGarGameplayAbility_Ragdolling::GetRagdollVelocity() const
{
	return RagdollingTask.IsValid() ? RagdollingTask->GetRagdollVelocity() : FVector::ZeroVector;
}

UGarGameplayAbility_Ragdolling::UGarGameplayAbility_Ragdolling(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	SetAssetTags(FGameplayTagContainer(GarLocomotionActionTags::Unconsious));
	ActivationOwnedTags.AddTag(GarLocomotionActionTags::Unconsious);
	CancelAbilitiesWithTag.AddTag(GarLocomotionActionTags::Root);
	BlockAbilitiesWithTag.AddTag(GarLocomotionActionTags::Unconsious);
}

void UGarGameplayAbility_Ragdolling::OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnGiveAbility(ActorInfo, Spec);

	if (ActorInfo->OwnerActor.IsValid())
	{
		auto OverrideModeComponent{ActorInfo->OwnerActor->GetComponentByClass<UGarOverrideModeComponent>()};
		if (OverrideModeComponent)
		{
			OverrideModeComponent->RegisterOverrideTask(GetAssetTags().First(), OverrideTaskClass);
		}
	}
}

void UGarGameplayAbility_Ragdolling::OnAvatarSet(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
    Super::OnAvatarSet(ActorInfo, Spec);

	if (ActorInfo->AvatarActor.IsValid())
	{
		auto OverrideModeComponent{ActorInfo->AvatarActor->GetComponentByClass<UGarOverrideModeComponent>()};
		if (OverrideModeComponent)
		{
			OverrideModeComponent->RegisterOverrideTask(GetAssetTags().First(), OverrideTaskClass);
		}
	}
}

void UGarGameplayAbility_Ragdolling::OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	if (ActorInfo->OwnerActor.IsValid())
	{
		auto OverrideModeComponent{ActorInfo->OwnerActor->GetComponentByClass<UGarOverrideModeComponent>()};
		if (OverrideModeComponent)
		{
			OverrideModeComponent->UnregisterOverrideTask(GetAssetTags().First());
		}
	}
	if (ActorInfo->AvatarActor.IsValid())
	{
		auto OverrideModeComponent{ActorInfo->AvatarActor->GetComponentByClass<UGarOverrideModeComponent>()};
		if (OverrideModeComponent)
		{
			OverrideModeComponent->UnregisterOverrideTask(GetAssetTags().First());
		}
	}

	Super::OnRemoveAbility(ActorInfo, Spec);
}

bool UGarGameplayAbility_Ragdolling::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
														const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags,
														OUT FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	auto Character{GetGarCharacterFromActorInfo()};
	if (IsValid(Character) && OverrideTaskClass && OverrideTaskClass->IsChildOf(UGarRagdollingTask::StaticClass())
		&& Character->GetComponentByClass<UGarOverrideModeComponent>())
	{
		const auto& Tag{GetAssetTags().First()};
		if (UGarRagdollingTask::CanStart(Character, Tag))
		{
			return true;
		}
		UE_LOG(LogGar, Error, TEXT("RagdollingTask cannot start with the character settings for '%s'."), *Tag.ToString());
	}
	else
	{
		UE_LOG(LogGar, Error, TEXT("Ragdoll ability requires a GarCharacter and a RagdollingTask class."));
	}
	return false;
}

void UGarGameplayAbility_Ragdolling::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
													 const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		return;
	}

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (IsActive())
	{
		auto* Character{GetGarCharacterFromActorInfo()};
		auto* OverrideModeComponent = Character ? Character->GetComponentByClass<UGarOverrideModeComponent>() : nullptr;
		RagdollingTask = OverrideModeComponent ? OverrideModeComponent->StartRagdollingTask(GetAssetTags().First()) : nullptr;
		if (!RagdollingTask.IsValid())
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
		bOnGroundedAndAgedFired = false;

		TickTask = UGarAbilityTask_Tick::New(this, FName(TEXT("UGarGameplayAbility_Ragdolling")));
		if (TickTask.IsValid())
		{
			TickTask->OnTick.AddDynamic(this, &ThisClass::Tick);
			TickTask->ReadyForActivation();
		}
	}
}

void UGarGameplayAbility_Ragdolling::Tick(const float DeltaTime)
{
	if (!IsActive())
	{
		return;
	}

	K2_OnTick(DeltaTime);
	if (!IsActive()) return;

	if (IsGroundedAndAged())
	{
		if (!bOnGroundedAndAgedFired)
		{
			bOnGroundedAndAgedFired = true;
			K2_OnGroundedAndAged();
		}
	}
	else
	{
		bOnGroundedAndAgedFired = false;
	}
}

void UGarGameplayAbility_Ragdolling::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
												const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// GettingDown can cancel locomotion abilities from K2_OnEndAbility. Do not
	// clear the ragdoll state on that reentrant call, or while GAS defers ending.
	if (!IsEndAbilityValid(Handle, ActorInfo))
	{
		return;
	}
	if (ScopeLockCount > 0)
	{
		WaitingToExecute.Add(FPostLockDelegate::CreateUObject(this, &ThisClass::EndAbility,
			Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled));
		return;
	}

	// The Blueprint end event queries IsGroundedAndAged to enter GettingDown.
	// Keep the task active until that handoff has run, as the legacy component did.
	const TWeakObjectPtr<UGarRagdollingTask> EndingTask = RagdollingTask;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
	if (!IsActive())
	{
		if (EndingTask.IsValid()) EndingTask->End();
		RagdollingTask.Reset();
	}
}

bool UGarGameplayAbility_Ragdolling::IsGroundedAndAged() const
{
	return RagdollingTask.IsValid() && RagdollingTask->IsGroundedAndAged();
}
