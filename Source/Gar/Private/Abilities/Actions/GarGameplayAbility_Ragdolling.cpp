// Fill out your copyright notice in the Description page of Project Settings.

#include "Abilities/Actions/GarGameplayAbility_Ragdolling.h"
#include "Abilities/Tasks/GarAbilityTask_Tick.h"
#include "GarCharacter.h"
#include "GarCharacterMoverComponent.h"
#include "GarAnimationInstance.h"
#include "GarAbilitySystemComponent.h"
#include "GarPhysicsControlComponent.h"
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
	FVector Velocity = FVector::ZeroVector;
	if (const AGarCharacter* Character = GetGarCharacterFromActorInfo())
	{
		Character->GetPhysicsControl()->GetTopBodyVelocity(Velocity);
	}
	return Velocity;
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
	if (IsValid(Character))
	{
		auto* PhysicsControl{Character->GetPhysicsControl()};
		if (IsValid(PhysicsControl))
		{
			const auto& Tag{GetAssetTags().First()};
			if (PhysicsControl->HasRagdollSettings(Tag))
			{
				return true;
			}
			else
			{
				UE_LOG(LogGar, Error, TEXT("PhysicsControlComponent has no ragdoll settings for '%s'."), *Tag.ToString());
			}
		}
		else
		{
			UE_LOG(LogGar, Error, TEXT("PhysicsControlComponent is invalid."));
		}
	}
	else
	{
		UE_LOG(LogGar, Error, TEXT("GarCharacter is Invalid."));
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
		if (!Character || !Character->GetPhysicsControl()->StartRagdoll(GetAssetTags().First()))
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}

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
	auto* Character{GetGarCharacterFromActorInfo()};

	if (!IsActive())
	{
		return;
	}

	K2_OnTick(DeltaTime);

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
	auto* Character{GetGarCharacterFromActorInfo()};

	auto* OverrideModeComponent{Character->GetComponentByClass<UGarOverrideModeComponent>()};
	OverrideModeComponent->EndCurrentRagdollingTask();
	Character->GetPhysicsControl()->StopRagdoll();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

bool UGarGameplayAbility_Ragdolling::IsGroundedAndAged() const
{
	auto* Character{GetGarCharacterFromActorInfo()};
	return Character && Character->GetPhysicsControl()->IsRagdollingAndGroundedAndAged();
}
