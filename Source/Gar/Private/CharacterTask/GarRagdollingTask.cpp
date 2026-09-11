// Fill out your copyright notice in the Description page of Project Settings.

#include "CharacterTasks/GarRagdollingTask.h"

#include "GarCharacter.h"
#include "GarAbilitySystemComponent.h"
#include "GarPhysicsControlComponent.h"
#include "Components/GarOverrideModeComponent.h"
#include "LinkedAnimLayers/GarRagdollingOverrideAnimInstance.h"
#include "GarGameplayTags.h"
#include "Utility/GarMath.h"
#include "Utility/GarLog.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GarRagdollingTask)

void UGarRagdollingTask::Begin()
{
	if (IsActive() || !Component.IsValid() || !CanStart(Character.Get(), Component->GetCurrentOverrideTag())) return;
	bOnGroundedAndAgedFired = false;
	Super::Begin();
	if (!Character->GetPhysicsControl()->StartRagdoll(Component->GetCurrentOverrideTag()))
	{
		Super::End();
		return;
	}
	bOwnsPhysicsRagdoll = true;
	// Physics ownership must not depend on evaluating a linked animation layer.
	Character->GetPhysicsControl()->SetRagdollingTaskActive(true);

	if(OverrideAnimInstance.IsValid())
	{
		auto RagdollingOverrideAnimInstance{Cast<UGarRagdollingOverrideAnimInstance>(OverrideAnimInstance.Get())};
		if (RagdollingOverrideAnimInstance)
		{
			RagdollingOverrideAnimInstance->Reset();
			RagdollingOverrideAnimInstance->SetRagdollingTaskActive(true);
		}
	}
}

void UGarRagdollingTask::End()
{
	StopPhysicsRagdoll();
	Super::End();
}

void UGarRagdollingTask::Cancel()
{
	StopPhysicsRagdoll();
	Super::Cancel();
}

void UGarRagdollingTask::StopPhysicsRagdoll()
{
	if (!bOwnsPhysicsRagdoll) return;
	bOwnsPhysicsRagdoll = false;
	if (Character.IsValid()) Character->GetPhysicsControl()->StopRagdoll();

	if (OverrideAnimInstance.IsValid())
	{
		auto RagdollingOverrideAnimInstance{Cast<UGarRagdollingOverrideAnimInstance>(OverrideAnimInstance.Get())};
		if (RagdollingOverrideAnimInstance)
		{
			RagdollingOverrideAnimInstance->Reset();
		}
	}
}

bool UGarRagdollingTask::CanStart(const AGarCharacter* Character, const FGameplayTag& RagdollTag)
{
	return IsValid(Character) && Character->GetPhysicsControl()
		&& Character->GetPhysicsControl()->HasRagdollSettings(RagdollTag);
}

FVector UGarRagdollingTask::GetRagdollVelocity() const
{
	FVector Velocity = FVector::ZeroVector;
	if (Character.IsValid() && bOwnsPhysicsRagdoll) Character->GetPhysicsControl()->GetTopBodyVelocity(Velocity);
	return Velocity;
}

void UGarRagdollingTask::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	auto* PhysicsControl{Character->GetPhysicsControl()};
	if (!IsActive() || PhysicsControl->IsRagdollFrozen())
	{
		return;
	}

	if (PhysicsControl->IsRagdollingAndGroundedAndAged())
	{
		if (!bOnGroundedAndAgedFired)
		{
			bOnGroundedAndAgedFired = true;
			K2_OnGroundedAndAged();
		}
		Character->SetInputStance(PhysicsControl->IsRagdollingFacingUpward() ? GarStanceTags::LyingBack : GarStanceTags::LyingFront);

		// local only. not be replicated.
		Character->GetGarAbilitySystem()->SetLooseGameplayTagCount(GarStateFlagTags::FacingUpward, PhysicsControl->IsRagdollingFacingUpward() ? 1 : 0);
	}
	else
	{
		if (PhysicsControl->GetRagdollStatus().bGrounded)
		{
			Character->SetInputStance(GarStanceTags::Crouching);
		}
		bOnGroundedAndAgedFired = false;
	}
}

bool UGarRagdollingTask::IsEpilogRunning_Implementation() const
{
	// Server-side remote pawns don't evaluate animations, so ObservingFinalBlendWeight never reaches 1.0.
	if (Character.IsValid() && Character->HasAuthority() && !Character->IsLocallyControlled())
	{
		return false;
	}

	if (OverrideAnimInstance.IsValid())
	{
		auto RagdollingOverrideAnimInstance{Cast<UGarRagdollingOverrideAnimInstance>(OverrideAnimInstance.Get())};
		if (RagdollingOverrideAnimInstance)
		{
			bool bRagdollingTaskActive{RagdollingOverrideAnimInstance->GetRagdollingTaskActive()};
			float BlendWeight{RagdollingOverrideAnimInstance->GetObservingFinalBlendWeight()};
			UE_LOG(LogTemp, Log, TEXT("bActive:%d ObservingFinalBlendWeight:%0.2f (%d)"), bRagdollingTaskActive, BlendWeight,
				bRagdollingTaskActive || BlendWeight < 1.0f);
			return bRagdollingTaskActive || BlendWeight < 1.0f;
		}
	}
	return false;
}

void UGarRagdollingTask::OnFinished()
{
	Super::OnFinished();

	if (Character.IsValid()) Character->GetPhysicsControl()->SetRagdollingTaskActive(false);
}

bool UGarRagdollingTask::IsGroundedAndAged() const
{
	return bOwnsPhysicsRagdoll && Character.IsValid() && Character->GetPhysicsControl()->IsRagdollingAndGroundedAndAged();
}
