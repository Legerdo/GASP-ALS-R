// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CharacterTasks/GarOverrideTask.h"
#include "GarRagdollingTask.generated.h"

class UGarLinkedAnimationInstance;
struct FGameplayTag;

/**
 * Ragdolling
 */
UCLASS(Abstract)
class GAR_API UGarRagdollingTask : public UGarOverrideTask
{
	GENERATED_BODY()

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "State", Transient)
	uint8 bOnGroundedAndAgedFired : 1{false};

public:
	static bool CanStart(const AGarCharacter* Character, const FGameplayTag& RagdollTag);
	FVector GetRagdollVelocity() const;

	UFUNCTION(BlueprintPure, Category = "GAR|CharacterTask|Ragdolling")
	bool IsGroundedAndAged() const;

public:
	virtual void Begin() override;

	virtual void End() override;
	virtual void Cancel() override;

	virtual void Tick(float DeltaTime) override;

	virtual bool IsEpilogRunning_Implementation() const override;

	virtual void OnFinished() override;

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "GAR|CharacterTask|Ragdolling", DisplayName = "On Grounded And Aged", Meta = (ScriptName = "OnGroundedAndAged"))
	void K2_OnGroundedAndAged();

private:
	void StopPhysicsRagdoll();
	bool bOwnsPhysicsRagdoll = false;
};
