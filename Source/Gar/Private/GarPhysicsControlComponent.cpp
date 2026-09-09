// Copyright (c) SAM-tak. All Rights Reserved.

#include "GarPhysicsControlComponent.h"

#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DefaultMovementSet/InstantMovementEffects/BasicInstantMovementEffects.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "PhysicsEngine/BodyInstance.h"
#include "GarAnimationInstance.h"
#include "GarCharacter.h"
#include "GarCharacterMoverComponent.h"
#include "GarGameplayTags.h"
#include "LinkedAnimLayers/GarRagdollingAnimInstance.h"
#include "Utility/GarLog.h"
#include "Utility/GarMath.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GarPhysicsControlComponent)

namespace GarPhysicsControl
{
	const FName AllSetName{TEXTVIEW("All")};
}

UGarPhysicsControlComponent::UGarPhysicsControlComponent()
{
	PrimaryComponentTick.bStartWithTickEnabled = true;
	DefaultRagdollTag = GarLocomotionActionTags::Unconsious;
}

void UGarPhysicsControlComponent::BeginPlay()
{
	Super::BeginPlay();

	Character = Cast<AGarCharacter>(GetOwner());
	if (!Character.IsValid())
	{
		UE_LOG(LogGar, Error, TEXT("PhysicsControlComponent must be owned by AGarCharacter: %s"), *GetPathName());
		return;
	}

	if (UGarAnimationInstance* AnimationInstance = Character->GetGarAnimationInstace())
	{
		RagdollingAnimInstance = AnimationInstance->RagdollingAnimInstance;
	}

	InitializeControls();
}

void UGarPhysicsControlComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (Character.IsValid())
	{
		if (!bControlsInitialized)
		{
			InitializeControls();
		}

		const FGameplayTag RagdollTag = FindRagdollTag();
		if (RagdollTag.IsValid())
		{
			if (!bRagdolling)
			{
				StartRagdoll(RagdollTag);
			}
			if (bRagdolling)
			{
				TickRagdoll(DeltaTime);
			}
		}
		else if (bRagdolling)
		{
			StopRagdoll();
		}

		if (!bRagdolling)
		{
			UpdatePhysicalAnimation();
			UpdateCurveDrivenControls();
		}
	}

	// Physics Control updates its animation target cache and sends controls to Chaos here. GAR's
	// profile and ragdoll state must be updated first so they apply in the same PrePhysics tick.
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

bool UGarPhysicsControlComponent::HasRagdollSettings(const FGameplayTag& RagdollTag) const
{
	return RagdollSettingsByTag.Contains(RagdollTag) || (DefaultRagdollTag.IsValid() && RagdollTag.MatchesTagExact(DefaultRagdollTag));
}

bool UGarPhysicsControlComponent::IsRagdolling() const
{
	return bRagdolling;
}

bool UGarPhysicsControlComponent::IsBoneSimulatingPhysics(FName BoneName) const
{
	if (!Character.IsValid())
	{
		return false;
	}

	const USkeletalMeshComponent* Mesh = Character->GetMesh();
	const FBodyInstance* Body = Mesh ? Mesh->GetBodyInstance(BoneName) : nullptr;
	return Body && Body->IsInstanceSimulatingPhysics();
}

bool UGarPhysicsControlComponent::IsRagdollingAndGroundedAndAged() const
{
	return bRagdolling && RagdollStatus.IsGroundedAndAged();
}

bool UGarPhysicsControlComponent::IsRagdollingFacingUpward() const
{
	return bRagdolling && RagdollStatus.bFacingUpward;
}

bool UGarPhysicsControlComponent::IsRagdollFrozen() const
{
	return bRagdolling && RagdollStatus.bFrozen;
}

const FGarRagdollStatus& UGarPhysicsControlComponent::GetRagdollStatus() const
{
	return RagdollStatus;
}

bool UGarPhysicsControlComponent::StartRagdoll(const FGameplayTag& RagdollTag)
{
	if (!Character.IsValid() || !HasRagdollSettings(RagdollTag))
	{
		return false;
	}

	if (!bControlsInitialized && !InitializeControls())
	{
		return false;
	}

	if (bRagdolling)
	{
		return CurrentRagdollTag == RagdollTag;
	}

	const FGarPhysicsControlRagdollSettings* Settings = RagdollSettingsByTag.Find(RagdollTag);
	if (!Settings && DefaultRagdollTag.IsValid() && RagdollTag.MatchesTagExact(DefaultRagdollTag))
	{
		Settings = &DefaultRagdollSettings;
	}
	if (!Settings)
	{
		return false;
	}

	CurrentRagdollTag = RagdollTag;
	bRagdolling = true;
	RagdollStatus = {};
	RagdollStatus.StartBlendTime = Settings->StartBlendTime;
	TimeAfterGrounded = 0.0f;
	TimeAfterGroundedAndStopped = 0.0f;

	if (UGarCharacterMoverComponent* Mover = Character->GetMover())
	{
		const FVector PoleDirection = Mover->GetVelocity().GetSafeNormal2D();
		if (PoleDirection.SizeSquared2D() > 0.01f)
		{
			RagdollStatus.bFacingUpward = Character->GetActorForwardVector().Dot(PoleDirection) < -0.25f;
			RagdollStatus.LyingDownYawAngleDelta = UGarMath::DirectionToAngleXY(RagdollStatus.bFacingUpward ? -PoleDirection : PoleDirection)
				- Character->GetActorRotation().Yaw;
		}
		else
		{
			RagdollStatus.bFacingUpward = true;
		}
	}

	if (!RagdollingAnimInstance.IsValid())
	{
		if (UGarAnimationInstance* AnimationInstance = Character->GetGarAnimationInstace())
		{
			RagdollingAnimInstance = AnimationInstance->RagdollingAnimInstance;
			AnimationInstance->Montage_Stop(Settings->StartBlendTime);
		}
	}
	else if (UGarAnimationInstance* AnimationInstance = Character->GetGarAnimationInstace())
	{
		AnimationInstance->Montage_Stop(Settings->StartBlendTime);
	}

	if (UCapsuleComponent* Capsule = Character->GetCapsule())
	{
		Capsule->SetCollisionObjectType(ECC_Pawn);
		Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
		Capsule->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		Capsule->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	}

	if (USkeletalMeshComponent* Mesh = Character->GetMesh(); Mesh && Character->HasAuthority() && !Character->IsLocallyControlled())
	{
		PreviousVisibilityBasedAnimTickOption = static_cast<uint8>(Mesh->VisibilityBasedAnimTickOption);
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		bOverrodeVisibilityBasedAnimTickOption = true;
	}

	if (Settings->ControlProfileName.IsValid())
	{
		InvokeControlProfile(Settings->ControlProfileName);
		CurrentControlProfileName = Settings->ControlProfileName;
	}

	SetRagdollBodyState(EPhysicsMovementType::Simulated, ECollisionEnabled::QueryAndPhysics, Settings->GravityMultiplier, Settings->bEnableControlsDuringRagdoll);
	RefreshRagdollAnimation(true);
	return true;
}

void UGarPhysicsControlComponent::StopRagdoll()
{
	if (!bRagdolling || !Character.IsValid())
	{
		return;
	}

	FTransform TopBodyTransform = Character->GetMover()->GetUpdatedComponentTransform();
	const bool bHasTopBodyTransform = GetTopBodyTransform(TopBodyTransform);

	if (RagdollingAnimInstance.IsValid())
	{
		RagdollingAnimInstance->Freeze();
		RefreshRagdollAnimation(false);
	}

	if (RagdollStatus.ElapsedTime > RagdollStatus.StartBlendTime && bHasTopBodyTransform)
	{
		const FRotator TopRotation = TopBodyTransform.Rotator();
		FRotator TargetRotation = Character->GetMover()->GetUpdatedComponentTransform().GetRotation().Rotator();
		const FVector FacingDirection = TopRotation.RotateVector(
			FMath::Abs(TopRotation.RotateVector(FVector::ForwardVector).GetSafeNormal2D().Dot(FVector::UpVector)) > 0.5f
				? (RagdollStatus.bFacingUpward ? FVector::RightVector : FVector::LeftVector)
				: (RagdollStatus.bFacingUpward ? FVector::BackwardVector : FVector::ForwardVector));
		TargetRotation.Yaw = UGarMath::DirectionToAngleXY(FacingDirection.GetSafeNormal2D());

		auto TeleportEffect = MakeShared<FTeleportEffect>();
		TeleportEffect->TargetLocation = Character->GetMover()->GetUpdatedComponentTransform().GetLocation();
		TeleportEffect->TargetRotation = TargetRotation;
		Character->GetMover()->QueueInstantMovementEffect(TeleportEffect);

		if (RagdollingAnimInstance.IsValid())
		{
			const FReferenceSkeleton& ReferenceSkeleton = Character->GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton();
			const int32 TopBoneIndex = ReferenceSkeleton.FindBoneIndex(TopBoneName);
			FPoseSnapshot& FinalPose = RagdollingAnimInstance->GetFinalPoseSnapshot();
			if (FinalPose.bIsValid && TopBoneIndex != INDEX_NONE && FinalPose.LocalTransforms.IsValidIndex(TopBoneIndex))
			{
				FinalPose.LocalTransforms[TopBoneIndex] = TopBodyTransform.GetRelativeTransform(Character->GetMesh()->GetComponentTransform());
			}
		}
	}

	SetRagdollBodyState(EPhysicsMovementType::Kinematic, ECollisionEnabled::QueryOnly, 1.0f, false);
	RestoreCapsuleCollision();

	if (USkeletalMeshComponent* Mesh = Character->GetMesh(); Mesh && bOverrodeVisibilityBasedAnimTickOption)
	{
		Mesh->VisibilityBasedAnimTickOption = static_cast<EVisibilityBasedAnimTickOption>(PreviousVisibilityBasedAnimTickOption);
		bOverrodeVisibilityBasedAnimTickOption = false;
	}

	bRagdolling = false;
	CurrentRagdollTag = FGameplayTag::EmptyTag;
	CurrentControlProfileName = NAME_None;
	RagdollStatus = {};
}

void UGarPhysicsControlComponent::SetRagdollingTaskActive(const bool bActive)
{
	if (RagdollingAnimInstance.IsValid())
	{
		RagdollingAnimInstance->SetRagdollingTaskActive(bActive);
	}
}

bool UGarPhysicsControlComponent::GetTopBodyTransform(FTransform& OutTransform) const
{
	if (!Character.IsValid())
	{
		return false;
	}

	if (const USkeletalMeshComponent* Mesh = Character->GetMesh())
	{
		if (const FBodyInstance* Body = Mesh->GetBodyInstance(TopBoneName))
		{
			OutTransform = Body->GetUnrealWorldTransform();
			return true;
		}
	}
	return false;
}

bool UGarPhysicsControlComponent::GetTopBodyVelocity(FVector& OutVelocity) const
{
	if (Character.IsValid())
	{
		if (const USkeletalMeshComponent* Mesh = Character->GetMesh())
		{
			if (const FBodyInstance* Body = Mesh->GetBodyInstance(TopBoneName))
			{
				OutVelocity = Body->GetUnrealWorldVelocity();
				return true;
			}
		}
	}
	return false;
}

void UGarPhysicsControlComponent::DisplayDebug(UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo, float& HorizontalLocation, float& VerticalLocation) const
{
	const float Scale = FMath::Min(Canvas->SizeX / (1280.0f * Canvas->GetDPIScale()), Canvas->SizeY / (720.0f * Canvas->GetDPIScale()));
	FCanvasTextItem Text(FVector2D::ZeroVector, FText::GetEmpty(), GEngine->GetSmallFont(), FLinearColor::White);
	Text.Scale = {Scale * 0.75f, Scale * 0.75f};
	Text.EnableShadow(FLinearColor::Black);
	Text.Text = FText::FromString(FString::Printf(TEXT("Profile: %s | Ragdoll: %s | Grounded: %s | Frozen: %s"),
		*CurrentControlProfileName.ToString(), bRagdolling ? TEXT("true") : TEXT("false"), RagdollStatus.bGrounded ? TEXT("true") : TEXT("false"),
		RagdollStatus.bFrozen ? TEXT("true") : TEXT("false")));
	Text.Draw(Canvas->Canvas, {HorizontalLocation, VerticalLocation});
	VerticalLocation += 12.0f * Scale;
}

bool UGarPhysicsControlComponent::InitializeControls()
{
	if (bControlsInitialized)
	{
		return true;
	}

	if (!Character.IsValid() || !PhysicsControlAsset.ToSoftObjectPath().IsValid())
	{
		return false;
	}

	if (!PhysicsControlAsset.LoadSynchronous())
	{
		UE_LOG(LogGar, Error, TEXT("Unable to load PhysicsControlAsset for %s"), *GetPathName());
		return false;
	}

	bControlsInitialized = CreateControlsAndBodyModifiersFromPhysicsControlAsset(Character->GetMesh(), nullptr, NAME_None);
	if (!bControlsInitialized)
	{
		UE_LOG(LogGar, Error, TEXT("Unable to create Physics Control records for %s"), *GetPathName());
	}
	return bControlsInitialized;
}

FGameplayTag UGarPhysicsControlComponent::FindRagdollTag() const
{
	if (!Character.IsValid())
	{
		return FGameplayTag::EmptyTag;
	}

	const UAbilitySystemComponent* AbilitySystem = Character->GetAbilitySystemComponent();
	if (!AbilitySystem)
	{
		return FGameplayTag::EmptyTag;
	}

	FGameplayTag BestTag;
	int32 BestSpecificity = INDEX_NONE;
	for (const TPair<FGameplayTag, FGarPhysicsControlRagdollSettings>& Pair : RagdollSettingsByTag)
	{
		if (AbilitySystem->HasMatchingGameplayTag(Pair.Key))
		{
			const int32 Specificity = Pair.Key.ToString().Len();
			if (Specificity > BestSpecificity)
			{
				BestTag = Pair.Key;
				BestSpecificity = Specificity;
			}
		}
	}

	if (DefaultRagdollTag.IsValid() && AbilitySystem->HasMatchingGameplayTag(DefaultRagdollTag))
	{
		const int32 Specificity = DefaultRagdollTag.ToString().Len();
		if (Specificity > BestSpecificity)
		{
			BestTag = DefaultRagdollTag;
		}
	}
	return BestTag;
}

FName UGarPhysicsControlComponent::FindControlProfile(const FGameplayTagContainer& GameplayTags) const
{
	FName BestProfile = DefaultControlProfileName;
	int32 BestSpecificity = INDEX_NONE;
	for (const TPair<FGameplayTag, FName>& Pair : ControlProfileByTag)
	{
		if (Pair.Value.IsValid() && GameplayTags.HasTag(Pair.Key))
		{
			const int32 Specificity = Pair.Key.ToString().Len();
			if (Specificity > BestSpecificity)
			{
				BestProfile = Pair.Value;
				BestSpecificity = Specificity;
			}
		}
	}
	return BestProfile;
}

const FGarPhysicsControlRagdollSettings* UGarPhysicsControlComponent::GetCurrentRagdollSettings() const
{
	if (!bRagdolling)
	{
		return nullptr;
	}

	if (const FGarPhysicsControlRagdollSettings* Settings = RagdollSettingsByTag.Find(CurrentRagdollTag))
	{
		return Settings;
	}
	return CurrentRagdollTag.MatchesTagExact(DefaultRagdollTag) ? &DefaultRagdollSettings : nullptr;
}

void UGarPhysicsControlComponent::UpdatePhysicalAnimation()
{
	if (!bControlsInitialized || !Character.IsValid())
	{
		return;
	}

	FGameplayTagContainer GameplayTags;
	Character->GetAbilitySystemComponent()->GetOwnedGameplayTags(GameplayTags);
	const FName DesiredProfile = FindControlProfile(GameplayTags);
	if (DesiredProfile != CurrentControlProfileName)
	{
		if (DesiredProfile.IsValid() && !InvokeControlProfile(DesiredProfile))
		{
			UE_LOG(LogGar, Warning, TEXT("Physics Control profile '%s' was not found on %s"), *DesiredProfile.ToString(), *GetPathName());
		}
		CurrentControlProfileName = DesiredProfile;
	}
}

void UGarPhysicsControlComponent::UpdateCurveDrivenControls()
{
	if (!bControlsInitialized || !Character.IsValid())
	{
		return;
	}

	UGarAnimationInstance* AnimationInstance = Character->GetGarAnimationInstace();
	if (!AnimationInstance)
	{
		return;
	}

	for (const FGarPhysicsControlCurveSetMapping& Mapping : CurveSetMappings)
	{
		if (!Mapping.CurveName.IsValid())
		{
			continue;
		}

		const float LockAmount = AnimationInstance->GetCurveValueClamped01(Mapping.CurveName);
		if (Mapping.ControlSetName.IsValid())
		{
			FPhysicsControlSparseMultiplier Multiplier;
			Multiplier.LinearStrengthMultiplier = FVector(1.0f - LockAmount);
			Multiplier.AngularStrengthMultiplier = 1.0f - LockAmount;
			Multiplier.bEnableLinearDampingRatioMultiplier = false;
			Multiplier.bEnableLinearExtraDampingMultiplier = false;
			Multiplier.bEnableMaxForceMultiplier = false;
			Multiplier.bEnableAngularDampingRatioMultiplier = false;
			Multiplier.bEnableAngularExtraDampingMultiplier = false;
			Multiplier.bEnableMaxTorqueMultiplier = false;
			SetControlSparseMultipliersInSet(Mapping.ControlSetName, Multiplier);
		}

		if (Mapping.BodyModifierSetName.IsValid())
		{
			SetBodyModifiersInSetPhysicsBlendWeight(Mapping.BodyModifierSetName, 1.0f - LockAmount);
		}
	}
}

void UGarPhysicsControlComponent::TickRagdoll(const float DeltaTime)
{
	const FGarPhysicsControlRagdollSettings* Settings = GetCurrentRagdollSettings();
	if (!Settings || RagdollStatus.bFrozen || !Character.IsValid())
	{
		return;
	}

	UGarCharacterMoverComponent* Mover = Character->GetMover();
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	if (!Mover || !Mesh)
	{
		return;
	}

	RagdollStatus.bGrounded = Character->HasMatchingGameplayTag(GarLocomotionModeTags::Grounded);
	FTransform TopBodyTransform;
	const bool bHasTopBodyTransform = GetTopBodyTransform(TopBodyTransform);

	for (FBodyInstance* Body : Mesh->Bodies)
	{
		if (Body && Body->IsInstanceSimulatingPhysics() && Settings->MaxBodySpeed > 0.0f)
		{
			const FVector Velocity = Body->GetUnrealWorldVelocity_AssumesLocked();
			if (Velocity.Size() > Settings->MaxBodySpeed)
			{
				Body->SetLinearVelocity(Velocity.GetClampedToMaxSize(Settings->MaxBodySpeed), false);
			}
		}
	}

	if (bHasTopBodyTransform)
	{
		const FVector UpDirection = Mover->GetUpDirection();
		const FRotator TopRotation = TopBodyTransform.Rotator();
		const FVector TopDirection = TopRotation.RotateVector(FVector::ForwardVector);
		if (FMath::Abs(TopDirection.Dot(UpDirection)) > 0.7f)
		{
			const float FacingDot = TopRotation.RotateVector(FVector::RightVector).Dot(Character->GetActorForwardVector());
			RagdollStatus.bFacingUpward = RagdollStatus.bFacingUpward ? FacingDot <= 0.2f : FacingDot < -0.2f;
		}
		else
		{
			const float FacingDot = TopDirection.Dot(Character->GetActorForwardVector());
			RagdollStatus.bFacingUpward = RagdollStatus.bFacingUpward ? FacingDot <= 0.2f : FacingDot < -0.2f;
		}
	}

	RefreshRagdollAnimation(true);
	RagdollStatus.RootBodySpeed = Mover->GetVelocity().Size();

	if (Settings->bAllowFreeze && RagdollStatus.bGrounded)
	{
		TimeAfterGrounded += DeltaTime;
		if (RagdollStatus.RootBodySpeed < Settings->RootBodySpeedConsideredStopped)
		{
			TimeAfterGroundedAndStopped += DeltaTime;
		}
		else
		{
			TimeAfterGroundedAndStopped = 0.0f;
		}

		const bool bForceFreeze = (Settings->TimeAfterGroundedForForceFreeze > 0.0f && TimeAfterGrounded > Settings->TimeAfterGroundedForForceFreeze)
			|| (Settings->TimeAfterGroundedAndStoppedForForceFreeze > 0.0f && TimeAfterGroundedAndStopped > Settings->TimeAfterGroundedAndStoppedForForceFreeze);

		if (RagdollStatus.IsGroundedAndAged() && (bForceFreeze || RagdollStatus.RootBodySpeed < Settings->RootBodySpeedConsideredStopped))
		{
			RagdollStatus.MaxBodySpeed = 0.0f;
			RagdollStatus.MaxBodyAngularSpeed = 0.0f;
			Mesh->ForEachBodyBelow(TopBoneName, true, false, [this](FBodyInstance* Body)
			{
				RagdollStatus.MaxBodySpeed = FMath::Max(RagdollStatus.MaxBodySpeed, Body->GetUnrealWorldVelocity().Size());
				RagdollStatus.MaxBodyAngularSpeed = FMath::Max(RagdollStatus.MaxBodyAngularSpeed,
					FMath::RadiansToDegrees(Body->GetUnrealWorldAngularVelocityInRadians().Size()));
			});
			RagdollStatus.bFrozen = bForceFreeze || (RagdollStatus.MaxBodySpeed < Settings->BodySpeedThreshold
				&& RagdollStatus.MaxBodyAngularSpeed < Settings->BodyAngularSpeedThreshold);
		}
	}
	else
	{
		TimeAfterGrounded = 0.0f;
		TimeAfterGroundedAndStopped = 0.0f;
	}

	if (RagdollStatus.bFrozen)
	{
		SetRagdollBodyState(EPhysicsMovementType::Kinematic, ECollisionEnabled::QueryAndPhysics, Settings->GravityMultiplier, false);
		if (RagdollingAnimInstance.IsValid())
		{
			RagdollingAnimInstance->Freeze();
		}
	}

	RagdollStatus.ElapsedTime += DeltaTime;
}

void UGarPhysicsControlComponent::RefreshRagdollAnimation(const bool bActive)
{
	if (RagdollingAnimInstance.IsValid())
	{
		if (bActive)
		{
			RagdollingAnimInstance->UnFreeze();
			RagdollingAnimInstance->SetStartBlendTime(RagdollStatus.StartBlendTime);
		}
		RagdollingAnimInstance->Refresh(RagdollStatus, bActive);
	}
}

void UGarPhysicsControlComponent::SetRagdollBodyState(const EPhysicsMovementType MovementType, const ECollisionEnabled::Type CollisionType,
	const float GravityMultiplier, const bool bEnableControls)
{
	if (!bControlsInitialized)
	{
		return;
	}

	SetControlsInSetEnabled(GarPhysicsControl::AllSetName, bEnableControls);
	SetBodyModifiersInSetMovementType(GarPhysicsControl::AllSetName, MovementType);
	SetBodyModifiersInSetCollisionType(GarPhysicsControl::AllSetName, CollisionType);
	SetBodyModifiersInSetGravityMultiplier(GarPhysicsControl::AllSetName, GravityMultiplier);
	SetBodyModifiersInSetPhysicsBlendWeight(GarPhysicsControl::AllSetName, MovementType == EPhysicsMovementType::Simulated ? 1.0f : 0.0f);
}

void UGarPhysicsControlComponent::RestoreCapsuleCollision()
{
	if (UCapsuleComponent* Capsule = Character->GetCapsule())
	{
		Capsule->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
	}
	if (UCapsuleComponent* ProneCapsule = Character->GetProneCapsule())
	{
		ProneCapsule->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
	}
}
