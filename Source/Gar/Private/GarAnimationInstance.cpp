#include "GarAnimationInstance.h"

#include "DrawDebugHelpers.h"
#include "Components/CapsuleComponent.h"
#include "Curves/CurveFloat.h"
#include "MotionWarpingComponent.h"
#include "MoverPoseSearchTrajectoryPredictor.h"
#include "AbilitySystemComponent.h"
#include "GarAnimationInstanceProxy.h"
#include "GarCharacter.h"
#include "GarCharacterMoverComponent.h"
#include "GarConstants.h"
#include "LinkedAnimLayers/GarLayeringAnimInstance.h"
#include "LinkedAnimLayers/GarRagdollingAnimInstance.h"
#include "LinkedAnimLayers/GarDeltaOverlayAnimInstance.h"
#include "Abilities/Actions/GarGameplayAbility_Ragdolling.h"
#include "Utility/GarMath.h"
#include "Utility/GarUtility.h"
#include "Animation/TrajectoryTypes.h"
#include "Kismet/KismetMathLibrary.h"
#include "PoseSearch/PoseSearchTrajectoryLibrary.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GarAnimationInstance)

UGarAnimationInstance::UGarAnimationInstance()
{
	CurrentGameplayTags.AddTag(GarRotationModeTags::ViewDirection);
	CurrentGameplayTags.AddTag(GarStanceTags::Standing);
	CurrentGameplayTags.AddTag(GarGaitTags::Running);
	CurrentGameplayTags.AddTag(GarLocomotionModeTags::Grounded);
}

void UGarAnimationInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	Character = Cast<AGarCharacter>(GetOwningActor());
	BlendStackLocomotion = FGarBlendStackLocomotionState{};
	bBlendStackReTransition = false;
	bBlendStackToLoop = false;
}

void UGarAnimationInstance::NativeBeginPlay()
{
	LayeringAnimInstance = Cast<UGarLayeringAnimInstance>(GetLinkedAnimGraphInstanceByTag(FName{TEXTVIEW("Layering")}));
	RagdollingAnimInstance = Cast<UGarRagdollingAnimInstance>(GetLinkedAnimGraphInstanceByTag(FName{TEXTVIEW("Ragdolling")}));

	if (!ensure(Character.IsValid())) return;
	if (!ensure(LayeringAnimInstance.IsValid())) return;
	if (!ensure(RagdollingAnimInstance.IsValid())) return;

	Super::NativeBeginPlay();

	if (IsValid(Character->GetMover()))
	{
		RefreshCharacterMovementOnGameThread(0.0f);
	}

	Character->GetAbilitySystemComponent()->GetOwnedGameplayTags(CurrentGameplayTags);
}

void UGarAnimationInstance::NativeUpdateAnimation(const float DeltaTime)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UGarAnimationInstance::NativeUpdateAnimation()"),
								STAT_UGarAnimationInstance_NativeUpdateAnimation, STATGROUP_Gar)

	Super::NativeUpdateAnimation(DeltaTime);

	if (!Character.IsValid())
	{
		return;
	}

	PreviousGameplayTags = CurrentGameplayTags;
	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		FGameplayTagContainer GameplayTags;
		Character->GetAbilitySystemComponent()->GetOwnedGameplayTags(GameplayTags);
		if (!GameplayTags.IsEmpty())
		{
			CurrentGameplayTags = GameplayTags;
		}
	}
	auto* Mesh{GetSkelMeshComponent()};
	CharacterTransform = Mesh->GetComponentTransform();

	if (UMotionWarpingComponent* MotionWarping = Character->GetMotionWarping())
	{
		const auto WarpTarget{MotionWarping->FindWarpTarget(FName(TEXTVIEW("FrontLedge")))};
		if (WarpTarget)
		{
			InteractionTransform = WarpTarget->GetTargetTrasform();
		}
	}

	bIsActionRunning = Character->GetLocomotionAction().IsValid();

	RefreshCharacterMovementOnGameThread(DeltaTime);
}

void UGarAnimationInstance::NativeThreadSafeUpdateAnimation(const float DeltaTime)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UGarAnimationInstance::NativeThreadSafeUpdateAnimation()"),
								STAT_UGarAnimationInstance_NativeThreadSafeUpdateAnimation, STATGROUP_Gar)

	Super::NativeThreadSafeUpdateAnimation(DeltaTime);

	if (!Character.IsValid())
	{
		return;
	}

	if (LayeringAnimInstance.IsValid())
	{
		LayeringAnimInstance->Refresh();
	}

	RefreshPose();
}

void UGarAnimationInstance::NativePostUpdateAnimation()
{
	// Can't use UAnimationInstance::NativePostEvaluateAnimation() instead this function, as it will not be called if
	// USkinnedMeshComponent::VisibilityBasedAnimTickOption is set to EVisibilityBasedAnimTickOption::AlwaysTickPose.

	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UGarAnimationInstance::NativePostUpdateAnimation()"),
								STAT_UGarAnimationInstance_NativePostUpdateAnimation, STATGROUP_Gar)

	if (!Character.IsValid())
	{
		return;
	}
}

FAnimInstanceProxy* UGarAnimationInstance::CreateAnimInstanceProxy()
{
	return new FGarAnimationInstanceProxy{this};
}

void UGarAnimationInstance::RefreshPose()
{
	const auto& Curves{GetProxyOnAnyThread<FGarAnimationInstanceProxy>().GetAnimationCurves(EAnimCurveType::AttributeCurve)};

	static const auto GetCurveValue{
		[](const TMap<FName, float>& Curves, const FName& CurveName) -> float
		{
			const auto* Value{Curves.Find(CurveName)};

			return Value != nullptr ? *Value : 0.0f;
		}
	};

	PoseState.GroundedAmount = GetCurveValue(Curves, UGarConstants::PoseGroundedCurveName());
	PoseState.InAirAmount = GetCurveValue(Curves, UGarConstants::PoseInAirCurveName());

	PoseState.StandingAmount = GetCurveValue(Curves, UGarConstants::PoseStandingCurveName());
	PoseState.CrouchingAmount = GetCurveValue(Curves, UGarConstants::PoseCrouchingCurveName());
	PoseState.LyingAmount = GetCurveValue(Curves, UGarConstants::PoseLyingCurveName());

	PoseState.MovingAmount = GetCurveValue(Curves, UGarConstants::PoseMovingCurveName());

	PoseState.GaitAmount = FMath::Clamp(GetCurveValue(Curves, UGarConstants::PoseGaitCurveName()), 0.0f, 3.0f);
	PoseState.GaitWalkingAmount = UGarMath::Clamp01(PoseState.MovingAmount);
	PoseState.GaitRunningAmount = UGarMath::Clamp01(PoseState.GaitAmount);
	PoseState.GaitSprintingAmount = UGarMath::Clamp01(PoseState.GaitAmount - 1.0f);

	PoseState.AimingAmount = GetCurveValue(Curves, UGarConstants::PoseAimingCurveName());
}

void UGarAnimationInstance::RefreshCharacterMovementOnGameThread(float DeltaTime)
{
	check(IsInGameThread())

	const auto* Mover{Character->GetMover()};

	CharacterMovement.VelocityAcceleration = (CharacterMovement.Velocity - Mover->GetVelocity()) / FMath::Max(DeltaTime, 0.001f);
	CharacterMovement.Velocity = Mover->GetVelocity();
	CharacterMovement.CurrentMaxSpeed = Mover->CurrentMaxSpeed;
	CharacterMovement.CurrentAcceleration = Mover->CurrentAcceleration;
	CharacterMovement.CurrentDeceleration = Mover->CurrentDeceleration;
	CharacterMovement.bIsGrounded = Character->GetAbilitySystemComponent()->HasMatchingGameplayTag(GarLocomotionModeTags::Grounded);
	CharacterMovement.GravityAcceleration = Mover->GetGravityAcceleration();
	CharacterMovement.ViewRotation = Character->GetViewRotation();
	CharacterMovement.TrajectoryPredictor = Mover->GetTrajectoryPredictor();
	CharacterMovement.MovementIntent = Mover->GetMovementIntent();
	FHitResult FloorHit;
	CharacterMovement.GroundNormal = Mover->TryGetFloorCheckHitResult(FloorHit)
		? FloorHit.ImpactNormal : CharacterMovement.UpVector;

	if (CharacterMovement.GravityAcceleration.SquaredLength() > 0.001)
	{
		CharacterMovement.UpVector = -CharacterMovement.GravityAcceleration.GetUnsafeNormal();
	}
	if (FMath::Abs(CharacterMovement.Velocity.Z) > 0.5f && Character->GetAbilitySystemComponent()->HasMatchingGameplayTag(GarLocomotionModeTags::InAir))
	{
		CharacterMovement.LatestVelocityInAir = CharacterMovement.Velocity;
	}
	if (CharacterMovement.Velocity.Size2D() > 5.0f)
	{
		CharacterMovement.LastNonZeroVelocity = CharacterMovement.Velocity;
	}
}

float UGarAnimationInstance::GetCurveValueClamped01(const FName& CurveName) const
{
	return UGarMath::Clamp01(GetCurveValue(CurveName));
}

void UGarAnimationInstance::RequestBlendStackTransition(bool bToLoop)
{
	if (bToLoop) bBlendStackToLoop = true;
	else bBlendStackReTransition = true;
}

void UGarAnimationInstance::UpdateBlendStackLocomotion(const FTransformTrajectory& InTrajectory,
	const FTransform& RootTransform, float DeltaTime)
{
	auto& State = BlendStackLocomotion;
	State.Velocity = CharacterMovement.Velocity;
	State.Speed2D = State.Velocity.Size2D();
	State.FutureFacingDelta_LastFrame = State.FutureFacingDelta;
	if (!InTrajectory.Samples.IsEmpty())
	{
		FTransformTrajectorySample Sample;
		UPoseSearchTrajectoryLibrary::GetTransformTrajectorySampleAtTime(InTrajectory, 1.5f, Sample);
		State.Trj_FutureFacing = Sample.Facing.Rotator();
		UPoseSearchTrajectoryLibrary::GetTransformTrajectoryAngularVelocity(InTrajectory, 0.0f, 0.1f, State.Trj_CurrentAngularVelocity);
		FVector PastAngularVelocity;
		UPoseSearchTrajectoryLibrary::GetTransformTrajectoryAngularVelocity(InTrajectory, -0.4f, -0.3f, PastAngularVelocity);
		State.Trj_IsCircling = (State.Trj_CurrentAngularVelocity.Z > 200.0f && PastAngularVelocity.Z > 200.0f)
			|| (State.Trj_CurrentAngularVelocity.Z < -200.0f && PastAngularVelocity.Z < -200.0f);
		State.Trj_CirclingTime = State.Trj_IsCircling ? State.Trj_CirclingTime + DeltaTime : 0.0f;
		float PreviousYaw = RootTransform.Rotator().Yaw;
		State.FutureFacingDelta = 0.0f;
		for (const float Time : {0.0f, 0.25f, 0.75f, 1.5f})
		{
			UPoseSearchTrajectoryLibrary::GetTransformTrajectorySampleAtTime(InTrajectory, Time, Sample);
			const float Yaw = Sample.Facing.Rotator().Yaw;
			State.FutureFacingDelta += FMath::FindDeltaAngleDegrees(PreviousYaw, Yaw);
			PreviousYaw = Yaw;
		}
	}

	State.Stance_LastFrame = State.Stance;
	State.Stance = CurrentGameplayTags.Filter(FGameplayTagContainer(GarStanceTags::Root));
	State.Gait_LastFrame = State.Gait;
	State.Gait = CurrentGameplayTags.Filter(FGameplayTagContainer(GarGaitTags::Root));
	if (State.Trj_IsCircling && State.Gait.HasTag(GarGaitTags::Sprinting)) State.Gait = FGameplayTagContainer(GarGaitTags::Running);

	FGameplayTagContainer Mode = CurrentGameplayTags.Filter(FGameplayTagContainer(GarLocomotionModeTags::Root));
	// These actions own animation states; GAS/Mover still control their gameplay and movement lifetime.
	if (CurrentGameplayTags.HasTag(GarLocomotionActionTags::Traversal)) Mode = FGameplayTagContainer(GarLocomotionActionTags::Traversal);
	else if (CurrentGameplayTags.HasTag(GarLocomotionActionTags::Sliding)) Mode = FGameplayTagContainer(GarLocomotionActionTags::Sliding);
	FGarBlendStackLocomotionState::UpdateHistory(Mode, State.MovementMode, State.MovementMode_LastFrame,
		State.MovementMode_Recent, State.MovementModeTime, DeltaTime, 0.2f);

	FGameplayTag Direction = GarAnimationDirectionTags::Forward;
	const FVector Movement = CharacterMovement.bIsGrounded ? CharacterMovement.MovementIntent : State.Velocity.GetSafeNormal2D();
	if (!Movement.IsNearlyZero() && !CurrentGameplayTags.HasTag(GarRotationModeTags::VelocityDirection)
		&& !State.Gait.HasTag(GarGaitTags::Sprinting))
	{
		const float Angle = FMath::FindDeltaAngleDegrees(CharacterMovement.ViewRotation.Yaw, Movement.Rotation().Yaw);
		// Sample's standard strafe thresholds, with hysteresis while already strafing.
		const bool bWasForwardOrBackward = State.MovementDirection.HasTag(GarAnimationDirectionTags::Forward)
			|| State.MovementDirection.HasTag(GarAnimationDirectionTags::Backward);
		const float ForwardLimit = bWasForwardOrBackward ? 60.0f : 40.0f;
		const float BackwardLimit = bWasForwardOrBackward ? 120.0f : 140.0f;
		if (FMath::Abs(Angle) <= ForwardLimit) Direction = GarAnimationDirectionTags::Forward;
		else if (Angle >= -BackwardLimit && Angle < -ForwardLimit) Direction = GarAnimationDirectionTags::LeftLeftFoot;
		else if (Angle > ForwardLimit && Angle <= BackwardLimit) Direction = GarAnimationDirectionTags::RightLeftFoot;
		else Direction = GarAnimationDirectionTags::Backward;
	}
	// Preserve the Sample's reselection trigger when the future trajectory wraps through a full turn.
	if (FMath::Abs(State.FutureFacingDelta - State.FutureFacingDelta_LastFrame) > 200.0f && State.Speed2D > 50.0f)
	{
		State.MovementDirection = FGameplayTagContainer(GarAnimationDirectionTags::Backward);
		State.MovementDirection_Recent = State.MovementDirection;
	}
	FGarBlendStackLocomotionState::UpdateHistory(FGameplayTagContainer(Direction), State.MovementDirection,
		State.MovementDirection_LastFrame, State.MovementDirection_Recent, State.MovementDirectionTime, DeltaTime, 0.1f);

	State.SmoothedGroundNormal = FMath::VInterpTo(State.SmoothedGroundNormal, CharacterMovement.GroundNormal, DeltaTime, 10.0f);
	float Pitch, Roll;
	UKismetMathLibrary::GetSlopeDegreeAngles(RootTransform.GetRotation().GetRightVector(), State.SmoothedGroundNormal,
		RootTransform.GetRotation().GetUpVector(), Pitch, Roll);
	State.SlopeAngle = FVector2D(Roll, Pitch);
	State.AO = FVector2D(FMath::FindDeltaAngleDegrees(RootTransform.Rotator().Yaw, CharacterMovement.ViewRotation.Yaw), ViewPitchAngle);
}
