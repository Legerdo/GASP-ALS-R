#include "Components/GarOverrideModeComponent.h"

#include "AbilitySystemComponent.h"
#include "GarAbilitySystemComponent.h"
#include "GarCharacter.h"
#include "GarLinkedAnimationInstance.h"
#include "CharacterTasks/GarOverrideTask.h"
#include "CharacterTasks/GarRagdollingTask.h"
#include "Abilities/Actions/GarGameplayAbility_Ragdolling.h"
#include "Utility/GarMath.h"
#include "Utility/GarLog.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GarOverrideModeComponent)

void UGarOverrideModeComponent::OnRegister()
{
	Super::OnRegister();

	if (Character.IsValid() && Character->GetAbilitySystemComponent() && Character->GetLocalRole() == ROLE_SimulatedProxy)
	{
		// no effect
		Character->GetGarAbilitySystem()->OnRepActivateAbilities.AddUObject(this, &ThisClass::CheckActiveAbility);
	}
}

void UGarOverrideModeComponent::BeginPlay()
{
	Super::BeginPlay();

	InstancedOverrideTasks.Reset();
}

void UGarOverrideModeComponent::EndCurrentRagdollingTask()
{
	if (CurrentOverrideTask.IsValid() && CurrentOverrideTask->IsA(UGarRagdollingTask::StaticClass()))
	{
		CurrentOverrideTask->End();
		if (CurrentOverrideTask->HasFinished())
		{
			CurrentOverrideTask.Reset();
			CurrentOverrideTag = FGameplayTag::EmptyTag;
		}
	}
}

UGarRagdollingTask* UGarOverrideModeComponent::StartRagdollingTask(const FGameplayTag& RagdollTag)
{
	const auto* TaskClass = OverrideClassMap.Find(RagdollTag);
	if (!TaskClass || !*TaskClass || !(*TaskClass)->IsChildOf(UGarRagdollingTask::StaticClass())) return nullptr;

	if (CurrentOverrideTag == RagdollTag && CurrentOverrideTask.IsValid())
	{
		// Re-entering the same task may interrupt its visual blend-out.
		if (!CurrentOverrideTask->IsActive()) CurrentOverrideTask->Begin();
	}
	else
	{
		ChangeOverrideTask(RagdollTag);
	}
	auto* Task = Cast<UGarRagdollingTask>(CurrentOverrideTask.Get());
	return CurrentOverrideTag == RagdollTag && Task && Task->IsActive() ? Task : nullptr;
}

void UGarOverrideModeComponent::ChangeOverrideTask(const FGameplayTag& NewOverrideMode)
{
	if (CurrentOverrideTask.IsValid())
	{
		CurrentOverrideTask->End();
		if (CurrentOverrideTask->HasFinished())
		{
			CurrentOverrideTask.Reset();
			CurrentOverrideTag = FGameplayTag::EmptyTag;
		}
		else
		{
			return;
		}
	}

	if (!CurrentOverrideTask.IsValid() && OverrideClassMap.Contains(NewOverrideMode))
	{
		if (InstancedOverrideTasks.Contains(NewOverrideMode))
		{
			CurrentOverrideTask = InstancedOverrideTasks[NewOverrideMode];
		}
		else
		{
			auto* NewTask = UGarOverrideTask::New(Character.Get(), OverrideClassMap[NewOverrideMode], this);
			InstancedOverrideTasks.Add(NewOverrideMode, NewTask);
			CurrentOverrideTask = NewTask;
		}
		CurrentOverrideTag = NewOverrideMode;
		CurrentOverrideTask->Begin();
	}
}

void UGarOverrideModeComponent::CheckActiveAbility(UGarAbilitySystemComponent* AbilitySystem)
{
	if (Character->GetLocalRole() == ROLE_SimulatedProxy)
	{
		for (auto& Ability : AbilitySystem->GetActivatableAbilities())
		{
			auto RagdollingAbility{Cast<UGarGameplayAbility_Ragdolling>(Ability.Ability)};
			if (RagdollingAbility)
			{
				RegisterOverrideTask(RagdollingAbility->GetAssetTags().First(), RagdollingAbility->OverrideTaskClass);
			}
		}
	}
}

void UGarOverrideModeComponent::OnOwnerTick_Implementation(float DeltaTime)
{
	Super::OnOwnerTick_Implementation(DeltaTime);

	FGameplayTagContainer OverrideTagsMask;
	for (auto& KeyValue : OverrideClassMap)
	{
		OverrideTagsMask.AddTag(KeyValue.Key);
	}

	FGameplayTagContainer Container;
	Character->GetAbilitySystemComponent()->GetOwnedGameplayTags(Container);
	auto OverrideMode{Container.Filter(OverrideTagsMask).First()};

	if (CurrentOverrideTag != OverrideMode)
	{
		ChangeOverrideTask(OverrideMode);
	}
	else if (OverrideMode.IsValid() && CurrentOverrideTask.IsValid() && !CurrentOverrideTask->IsActive())
	{
		// A replicated tag can return before the previous visual epilogue finishes.
		CurrentOverrideTask->Begin();
	}

	if (CurrentOverrideTask.IsValid())
	{
		CurrentOverrideTask->Tick(DeltaTime);
	}
}

void UGarOverrideModeComponent::OnPossessed_Implementation(AController* NewController)
{
	Super::OnPossessed_Implementation(NewController);
	if (CurrentOverrideTask.IsValid())
	{
		CurrentOverrideTask->OnPossessed(NewController);
	}
}

void UGarOverrideModeComponent::OnUnPossessed_Implementation(AController* PreviousController)
{
	Super::OnUnPossessed_Implementation(PreviousController);
	if (CurrentOverrideTask.IsValid())
	{
		CurrentOverrideTask->OnUnPossessed(PreviousController);
	}
}

void UGarOverrideModeComponent::RegisterOverrideTask(const FGameplayTag& OverrideMode, TSubclassOf<UGarOverrideTask> OverrideTaskClass)
{
	if (OverrideTaskClass)
	{
		OverrideClassMap.Add(OverrideMode, OverrideTaskClass);
	}
}

void UGarOverrideModeComponent::UnregisterOverrideTask(const FGameplayTag& OverrideMode)
{
	if (OverrideMode.IsValid())
	{
		OverrideClassMap.Remove(OverrideMode);
	}
}
