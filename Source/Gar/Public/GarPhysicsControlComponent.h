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
class UChooserTable;

/** Output of the GameplayTag -> Physics Control / PhysicsAsset constraint profile Chooser. */
USTRUCT(BlueprintType)
struct GAR_API FGarPhysicsControlProfileChooserResult
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GAR|PhysicsControl")
	FName ControlProfileName;

	/** None restores the PhysicsAsset's default joint settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GAR|PhysicsControl")
	FName ConstraintProfileName;
};

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|Ragdoll", Meta = (ClampMin = 0, ForceUnits = "s"))
	float StartBlendTime{0.25f};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|Ragdoll", Meta = (ClampMin = 0, ForceUnits = "cm/s"))
	float MaxBodySpeed{5000.0f};

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

/** Maps an animation curve to a Body Modifier set. A value of one hides physics blending without weakening animation drives. */
USTRUCT(BlueprintType)
struct GAR_API FGarPhysicsControlCurveSetMapping
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|PhysicsControl")
	FName CurveName;

	/** Set whose PhysicsBlendWeight is updated to 1 - Clamp01(CurveValue). */
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

	/** Reads a GameplayTagContainer and writes FGarPhysicsControlProfileChooserResult. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|PhysicsControl")
	TObjectPtr<UChooserTable> ProfileChooser;

	/** Reset recipe applied before each selected Control Profile, including ragdoll profiles. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|PhysicsControl")
	FName BaseControlProfileName{TEXTVIEW("PhysicalAnimation")};

	/** Sample-style heading is derived from the pelvis-to-chest direction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAR|Ragdoll")
	FName ChestBoneName{TEXTVIEW("spine_05")};

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

	/** Evaluation only. RagdollTag is task-owned; empty means normal physical animation. */
	UFUNCTION(BlueprintCallable, Category = "GAR|PhysicsControl")
	bool EvaluateProfile(const FGameplayTagContainer& GameplayTags, FGameplayTag RagdollTag,
		FGarPhysicsControlProfileChooserResult& OutResult) const;

	UFUNCTION(BlueprintPure, Category = "GAR|PhysicsControl")
	FName GetCurrentControlProfileName() const { return CurrentControlProfileName; }

	UFUNCTION(BlueprintPure, Category = "GAR|PhysicsControl")
	FName GetCurrentConstraintProfileName() const { return CurrentConstraintProfileName; }

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
	bool GetRagdollTransform(FTransform& OutTransform) const;

	const TArray<FGarPhysicsControlCurveSetMapping>& GetCurveSetMappings() const
	{
		return CurveSetMappings;
	}

	void DisplayDebug(UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo, float& HorizontalLocation, float& VerticalLocation) const;

private:
	bool InitializeControls();
	const FGarPhysicsControlRagdollSettings* GetCurrentRagdollSettings() const;
	bool ApplySelectedProfiles(FGameplayTag RagdollTag, bool bForce = false);
	void UpdateCurveDrivenPhysicsBlending();
	void TickRagdoll(float DeltaTime);
	void RefreshRagdollAnimation(bool bActive);
	void UpdateJointConstraints(FName ProfileName, bool bForRagdoll);
	void RestoreCapsuleCollision();

	TWeakObjectPtr<AGarCharacter> Character;
	TWeakObjectPtr<UGarRagdollingAnimInstance> RagdollingAnimInstance;
	FGarRagdollStatus RagdollStatus;
	FGameplayTag CurrentRagdollTag;
	FName CurrentControlProfileName;
	FName CurrentConstraintProfileName;
	float TimeAfterGrounded{0.0f};
	float TimeAfterGroundedAndStopped{0.0f};
	uint8 bControlsInitialized : 1{false};
	uint8 bRagdolling : 1{false};
	FCollisionResponseContainer PreviousCapsuleResponses;
	FCollisionResponseContainer PreviousProneCapsuleResponses;
	TArray<FName> LeftLegBones;
	TArray<FName> RightLegBones;
};
