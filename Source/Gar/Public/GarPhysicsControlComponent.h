// Copyright (c) SAM-tak. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "PhysicsControlComponent.h"
#include "GarPhysicsControlComponent.generated.h"

class AGarCharacter;
class UGarRagdollingAnimInstance;
class UCanvas;
class FDebugDisplayInfo;

/** Runtime state consumed by GAR's ragdoll animation, ability, task, and Mover mode. */
USTRUCT(BlueprintType)
struct GAR_API FGarRagdollStatus
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAR|Ragdoll")
	uint8 bGrounded : 1{false};

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAR|Ragdoll")
	uint8 bFacingUpward : 1{false};

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAR|Ragdoll")
	uint8 bFrozen : 1{false};

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAR|Ragdoll", Meta = (ForceUnits = "s"))
	float ElapsedTime{0.0f};

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAR|Ragdoll", Meta = (ForceUnits = "s"))
	float StartBlendTime{0.25f};

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAR|Ragdoll", Meta = (ForceUnits = "deg"))
	float LyingDownYawAngleDelta{0.0f};

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAR|Ragdoll", Meta = (ForceUnits = "cm/s"))
	float RootBodySpeed{0.0f};

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAR|Ragdoll", Meta = (ForceUnits = "cm/s"))
	float MaxBodySpeed{0.0f};

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAR|Ragdoll", Meta = (ForceUnits = "deg/s"))
	float MaxBodyAngularSpeed{0.0f};

	bool IsGroundedAndAged() const
	{
		return bGrounded && ElapsedTime > StartBlendTime;
	}
};

/** Physics Control configuration used while a ragdoll gameplay tag is active. */
USTRUCT(BlueprintType)
struct GAR_API FGarPhysicsControlRagdollSettings
{
	GENERATED_BODY()

	/** Profile in PhysicsControlAsset to invoke before entering ragdoll. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|Ragdoll")
	FName ControlProfileName{TEXTVIEW("Ragdoll")};

	/** Keeps animation-driven controls active while ragdolling. Usually false for a full ragdoll. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|Ragdoll")
	uint8 bEnableControlsDuringRagdoll : 1{false};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|Ragdoll", Meta = (ClampMin = 0, ForceUnits = "s"))
	float StartBlendTime{0.25f};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|Ragdoll", Meta = (ClampMin = 0, ForceUnits = "cm/s"))
	float MaxBodySpeed{5000.0f};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|Ragdoll", Meta = (ClampMin = 0))
	float GravityMultiplier{1.0f};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|Ragdoll|Freeze")
	uint8 bAllowFreeze : 1{false};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|Ragdoll|Freeze", Meta = (ClampMin = 0, ForceUnits = "s", EditCondition = "bAllowFreeze"))
	float TimeAfterGroundedForForceFreeze{5.0f};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|Ragdoll|Freeze", Meta = (ClampMin = 0, ForceUnits = "s", EditCondition = "bAllowFreeze"))
	float TimeAfterGroundedAndStoppedForForceFreeze{1.0f};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|Ragdoll|Freeze", Meta = (ClampMin = 0, ForceUnits = "cm/s", EditCondition = "bAllowFreeze"))
	float RootBodySpeedConsideredStopped{5.0f};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|Ragdoll|Freeze", Meta = (ClampMin = 0, ForceUnits = "cm/s", EditCondition = "bAllowFreeze"))
	float BodySpeedThreshold{1.0f};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|Ragdoll|Freeze", Meta = (ClampMin = 0, ForceUnits = "deg/s", EditCondition = "bAllowFreeze"))
	float BodyAngularSpeedThreshold{45.0f};
};

/** Maps an animation curve to Physics Control sets. A value of one suppresses the control strength. */
USTRUCT(BlueprintType)
struct GAR_API FGarPhysicsControlCurveSetMapping
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|PhysicsControl")
	FName CurveName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|PhysicsControl")
	FName ControlSetName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|PhysicsControl")
	FName BodyModifierSetName;
};

/**
 * GAR's sole owner of Physics Control state. The legacy PhysicalAnimationComponent is intentionally
 * not used: all physical-animation and ragdoll configuration lives in a Physics Control Asset.
 */
UCLASS(ClassGroup = Physics, meta = (BlueprintSpawnableComponent))
class GAR_API UGarPhysicsControlComponent : public UPhysicsControlComponent
{
	GENERATED_BODY()

public:
	UGarPhysicsControlComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|PhysicsControl")
	FName TopBoneName{TEXTVIEW("pelvis")};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|PhysicsControl")
	FName DefaultControlProfileName;

	/** More-specific matching gameplay tags take precedence over less-specific tags. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|PhysicsControl")
	TMap<FGameplayTag, FName> ControlProfileByTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|PhysicsControl")
	TMap<FGameplayTag, FGarPhysicsControlRagdollSettings> RagdollSettingsByTag;

	/** Fallback used by GAR's built-in ragdoll ability. Matching map entries override this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|Ragdoll")
	FGameplayTag DefaultRagdollTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|Ragdoll")
	FGarPhysicsControlRagdollSettings DefaultRagdollSettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|PhysicsControl")
	TArray<FGarPhysicsControlCurveSetMapping> CurveSetMappings;

	UFUNCTION(BlueprintPure, Category = "GAR|PhysicsControl")
	bool HasRagdollSettings(const FGameplayTag& RagdollTag) const;

	UFUNCTION(BlueprintPure, Category = "GAR|PhysicsControl")
	bool IsRagdolling() const;

	/** Returns whether the physics body for BoneName is currently simulated by Chaos. */
	UFUNCTION(BlueprintPure, Category = "GAR|PhysicsControl")
	bool IsBoneSimulatingPhysics(FName BoneName) const;

	UFUNCTION(BlueprintPure, Category = "GAR|PhysicsControl")
	bool IsRagdollingAndGroundedAndAged() const;

	UFUNCTION(BlueprintPure, Category = "GAR|PhysicsControl")
	bool IsRagdollingFacingUpward() const;

	UFUNCTION(BlueprintPure, Category = "GAR|PhysicsControl")
	bool IsRagdollFrozen() const;

	UFUNCTION(BlueprintPure, Category = "GAR|PhysicsControl")
	const FGarRagdollStatus& GetRagdollStatus() const;

	UFUNCTION(BlueprintCallable, Category = "GAR|PhysicsControl")
	bool StartRagdoll(const FGameplayTag& RagdollTag);

	UFUNCTION(BlueprintCallable, Category = "GAR|PhysicsControl")
	void StopRagdoll();

	UFUNCTION(BlueprintCallable, Category = "GAR|PhysicsControl")
	void SetRagdollingTaskActive(bool bActive);

	bool GetTopBodyTransform(FTransform& OutTransform) const;
	bool GetTopBodyVelocity(FVector& OutVelocity) const;

	const TArray<FGarPhysicsControlCurveSetMapping>& GetCurveSetMappings() const
	{
		return CurveSetMappings;
	}

	void DisplayDebug(UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo, float& HorizontalLocation, float& VerticalLocation) const;

private:
	bool InitializeControls();
	FGameplayTag FindRagdollTag() const;
	FName FindControlProfile(const FGameplayTagContainer& GameplayTags) const;
	const FGarPhysicsControlRagdollSettings* GetCurrentRagdollSettings() const;
	void UpdatePhysicalAnimation();
	void UpdateCurveDrivenControls();
	void TickRagdoll(float DeltaTime);
	void RefreshRagdollAnimation(bool bActive);
	void SetRagdollBodyState(EPhysicsMovementType MovementType, ECollisionEnabled::Type CollisionType, float GravityMultiplier, bool bEnableControls);
	void RestoreCapsuleCollision();

	TWeakObjectPtr<AGarCharacter> Character;
	TWeakObjectPtr<UGarRagdollingAnimInstance> RagdollingAnimInstance;
	FGarRagdollStatus RagdollStatus;
	FGameplayTag CurrentRagdollTag;
	FName CurrentControlProfileName;
	float TimeAfterGrounded{0.0f};
	float TimeAfterGroundedAndStopped{0.0f};
	uint8 bControlsInitialized : 1{false};
	uint8 bRagdolling : 1{false};
	uint8 bOverrodeVisibilityBasedAnimTickOption : 1{false};
	uint8 PreviousVisibilityBasedAnimTickOption{0};
};
