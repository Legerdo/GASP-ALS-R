#pragma once

#include "MoverComponent.h"
#include "GameplayTagAssetInterface.h"
#include "GarGameplayTags.h"
#include "GarCharacterMoverComponent.generated.h"

// MovementModifier didn't work. maybe bug.
#define GAR_USE_MOVEMENTMODIFIER 0

class UMoverTrajectoryPredictor;
class UMotionWarpingMoverAdapter;
class UCommonLegacyMovementSettings;
class UGarMovementSettings;
class AGarCharacter;
struct FGarMoverStanceState;
struct FGarCharacterMoverInputs;

UCLASS(ClassGroup = "GAR", BlueprintType, Blueprintable, Meta = (BlueprintSpawnableComponent))
class GAR_API UGarCharacterMoverComponent : public UMoverComponent
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GarCharacterMover|Settings")
	FGameplayTagContainer LocomotionModeTags;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "GarCharacterMover|State", Transient)
	TWeakObjectPtr<AGarCharacter> Character;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "GarCharacterMover|State", Transient, Replicated)
	FGameplayTag RotationMode{GarRotationModeTags::ViewDirection};

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "GarCharacterMover|State", Transient, Replicated)
	FGameplayTag Stance{GarStanceTags::Standing};

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "GarCharacterMover|State", Transient, Replicated)
	FGameplayTag Gait{GarGaitTags::Running};

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "GarCharacterMover|State", Transient, Replicated)
	uint8 bFacingUpward : 1{false};

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GarCharacterMover|State", Transient)
	TObjectPtr<UMoverTrajectoryPredictor> TrajectoryPredictor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GarCharacterMover|State", Transient)
	TObjectPtr<UMotionWarpingMoverAdapter> MotionWarpingMoverAdapter;

public:
	/** X = Forward Speed, Y = Strafe Speed, Z = Backwards Speed */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "GarCharacterMover|State", Transient)
	FVector CurrentMaxSpeed{ForceInit};

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "GarCharacterMover|State", Transient)
	float CurrentAcceleration{0.0f};

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "GarCharacterMover|State", Transient)
	float CurrentDeceleration{0.0f};

	// Constants

	static constexpr float MIN_FLOOR_DIST = 1.9f;	// Smallest distance we want our primitive floating above walkable floors while in ground-based movement.
	static constexpr float MAX_FLOOR_DIST = 2.4f;	// Largest distance we want our primitive floating above walkable floors while in ground-based movement.

public:
	UGarCharacterMoverComponent();

#if !GAR_USE_MOVEMENTMODIFIER
	//virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
#endif
	virtual void InitializeComponent() override;

	virtual void BeginPlay() override;
	virtual void GetDefaultInputAndState(FMoverInputCmdContext& OutInputCmd, FMoverSyncState& OutSyncState,
		FMoverAuxStateContext& OutAuxState) const override;
	virtual void OnPreSimulate(const FMoverTimeStep& TimeStep, const FMoverTickStartData& StartingData) override;

	/** Apply geometry/visual state without touching simulation caches. Returns whether collision geometry
	 *  or the pivot changed. Only an instant effect supplies a nonzero pivot adjustment. */
	bool ApplyStanceState(const FGarMoverStanceState& State, float CapsuleHeightDelta = 0.0f);

protected:
	UFUNCTION()
	virtual void OnMoverPreSimulationTick(const FMoverTimeStep& TimeStep, const FMoverInputCmdContext& InputCmd);

	UFUNCTION()
	virtual void OnMoverPostFinalize(const FMoverSyncState& SyncState, const FMoverAuxStateContext& AuxState);

public:
	UFUNCTION(BlueprintPure, Category = "GAR|CharacterMover")
	const UGarMovementSettings* GetSettings() const { return Settings; }

	UMoverTrajectoryPredictor* GetTrajectoryPredictor() const { return TrajectoryPredictor; }

	/** Return true if the hit result should be considered a walkable surface for the character. */
	UFUNCTION(BlueprintCallable, Category = "GAR|CharacterMover")
	virtual bool IsWalkable(const FHitResult& Hit) const;

	const FGameplayTagContainer& GetLocomotionModeTags() const { return LocomotionModeTags; }

	FGameplayTag GetLocomotionMode() const;
#if GAR_USE_MOVEMENTMODIFIER
	FGameplayTag GetRotationMode() const;
	FGameplayTag GetStance() const;
	FGameplayTag GetGait() const;
#else
	const FGameplayTag& GetRotationMode() const { return RotationMode; }
	const FGameplayTag& GetStance() const { return Stance; }
	const FGameplayTag& GetGait() const { return Gait; }
#endif
	const bool GetFacingUpward() const { return bFacingUpward; }

private:
	FGarMoverStanceState CaptureStanceState() const;
	void QueueStanceUpdate(const FMoverTimeStep& TimeStep, const FMoverTickStartData& StartingData);
	FGarMoverStanceState AdvanceStance(const FGarMoverStanceState& Current, const FGarCharacterMoverInputs& Inputs,
		float DeltaTime) const;

#if GAR_USE_MOVEMENTMODIFIER
	FMovementModifierHandle RotationModifierHandle;
	FMovementModifierHandle StanceModifierHandle;
	FMovementModifierHandle GaitModifierHandle;
#endif

	TObjectPtr<const UGarMovementSettings> Settings;
	TObjectPtr<const UCommonLegacyMovementSettings> CommonSettings;
};
