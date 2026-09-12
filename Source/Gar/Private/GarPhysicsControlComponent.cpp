// Copyright (c) SAM-tak. All Rights Reserved.

#include "GarPhysicsControlComponent.h"

#include "AbilitySystemComponent.h"
#include "Chooser.h"
#include "ChooserFunctionLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsControlBPLibrary.h"
#include "PhysicsControlAsset.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
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

	void SetLegCollisionEnabled(USkeletalMeshComponent* Mesh, const TArray<FName>& Left, const TArray<FName>& Right, bool bEnabled)
	{
		// UE 5.8 exposes these sample functions to Blueprint but not its public C++ API.
		// Invoke the reflected API with named, type-checked parameters; no engine patch required.
		UObject* Library = GetMutableDefault<UPhysicsControlBPLibrary>();
		UFunction* Function = Library->FindFunction(bEnabled ? TEXT("EnableCollisionBetweenBodyArrays") : TEXT("DisableCollisionBetweenBodyArrays"));
		if (!ensure(Function)) return;
		FStructOnScope Params(Function);
		for (const FName Name : {FName(TEXT("FirstComponent")), FName(TEXT("SecondComponent"))})
		{
			CastFieldChecked<FObjectPropertyBase>(Function->FindPropertyByName(Name))->SetObjectPropertyValue_InContainer(Params.GetStructMemory(), Mesh);
		}
		FArrayProperty* LeftProperty = CastFieldChecked<FArrayProperty>(Function->FindPropertyByName(TEXT("FirstBoneNames")));
		FArrayProperty* RightProperty = CastFieldChecked<FArrayProperty>(Function->FindPropertyByName(TEXT("SecondBoneNames")));
		LeftProperty->CopyCompleteValue(LeftProperty->ContainerPtrToValuePtr<void>(Params.GetStructMemory()), &Left);
		RightProperty->CopyCompleteValue(RightProperty->ContainerPtrToValuePtr<void>(Params.GetStructMemory()), &Right);
		Library->ProcessEvent(Function, Params.GetStructMemory());
	}
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

		// RagdollingTask owns entry/exit. Physics Control only advances that state;
		// do not independently infer another lifecycle from the ability tags.
		if (bRagdolling)
		{
			if (!RagdollStatus.bFrozen) ApplySelectedProfiles(CurrentRagdollTag);
			TickRagdoll(DeltaTime);
		}

		if (!bRagdolling)
		{
			ApplySelectedProfiles(FGameplayTag::EmptyTag);
			UpdateCurveDrivenPhysicsBlending();
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
	if (!ApplySelectedProfiles(RagdollTag, true))
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
		PreviousCapsuleResponses = Capsule->GetCollisionResponseToChannels();
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}
	if (UCapsuleComponent* ProneCapsule = Character->GetProneCapsule())
	{
		PreviousProneCapsuleResponses = ProneCapsule->GetCollisionResponseToChannels();
		ProneCapsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}

	// Profile modifiers are applied on the next PCC tick. Simulate immediately so an impulse
	// delivered by the event that started ragdoll is not lost (same as the sample).
	Character->GetMesh()->SetAllBodiesSimulatePhysics(true);
	Character->GetMesh()->WakeAllRigidBodies();
	Character->GetMover()->QueueNextMode(TEXT("Ragdolling"));
	RefreshRagdollAnimation(true);
	return true;
}

void UGarPhysicsControlComponent::StopRagdoll()
{
	if (!bRagdolling || !Character.IsValid())
	{
		return;
	}

	if (RagdollingAnimInstance.IsValid())
	{
		RagdollingAnimInstance->Freeze();
		RefreshRagdollAnimation(false);
	}

	RestoreCapsuleCollision();

	bRagdolling = false;
	CurrentRagdollTag = FGameplayTag::EmptyTag;
	CurrentControlProfileName = NAME_None;
	RagdollStatus = {};
	ApplySelectedProfiles(FGameplayTag::EmptyTag, true);
	// Forget the old animation targets so the next PCC update cannot interpret the snapshot
	// switch as a velocity impulse. A render-frame delay can expire BEFORE that update when
	// the ability ends after PCC has already ticked. This does not teleport the physical bodies
	// or override the velocity multipliers authored in the PCA.
	SetCachedBoneVelocitiesToZero();
	Character->GetMover()->QueueNextMode(Character->GetMover()->StartingMovementMode);
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

bool UGarPhysicsControlComponent::GetRagdollTransform(FTransform& OutTransform) const
{
	if (!bRagdolling || !GetTopBodyTransform(OutTransform))
	{
		return false;
	}
	const USkeletalMeshComponent* Mesh = Character->GetMesh();
	const FVector Up = Character->GetMover()->GetUpDirection();
	FVector Forward = Character->GetActorForwardVector();
	if (RagdollStatus.ElapsedTime > RagdollStatus.StartBlendTime && Mesh->GetBoneIndex(ChestBoneName) != INDEX_NONE)
	{
		const FTransform Chest = Mesh->GetBoneTransform(ChestBoneName, RTS_World);
		Forward = Chest.GetLocation() - OutTransform.GetLocation();
		if (FVector::DotProduct(Chest.GetRotation().GetRightVector(), Up) > 0.0f)
		{
			Forward *= -1.0f;
		}
		Forward = FVector::VectorPlaneProject(Forward, Up).GetSafeNormal();
		if (Forward.IsNearlyZero())
		{
			Forward = Character->GetActorForwardVector();
		}
	}
	OutTransform.SetRotation(FRotationMatrix::MakeFromZX(Up, Forward).ToQuat());
	return true;
}

void UGarPhysicsControlComponent::DisplayDebug(UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo, float& HorizontalLocation, float& VerticalLocation) const
{
	const float Scale = FMath::Min(Canvas->SizeX / (1280.0f * Canvas->GetDPIScale()), Canvas->SizeY / (720.0f * Canvas->GetDPIScale()));
	FCanvasTextItem Text(FVector2D::ZeroVector, FText::GetEmpty(), GEngine->GetSmallFont(), FLinearColor::White);
	Text.Scale = {Scale * 0.75f, Scale * 0.75f};
	Text.EnableShadow(FLinearColor::Black);
	Text.Text = FText::FromString(FString::Printf(TEXT("Control: %s | Constraint: %s | Ragdoll: %s | Grounded: %s | Frozen: %s"),
		*CurrentControlProfileName.ToString(), *CurrentConstraintProfileName.ToString(),
		bRagdolling ? TEXT("true") : TEXT("false"), RagdollStatus.bGrounded ? TEXT("true") : TEXT("false"),
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

	USkeletalMeshComponent* Mesh = Character->GetMesh();
	if (!Mesh || !Mesh->GetBodyInstance(TopBoneName) || !Mesh->GetBodyInstance(TopBoneName)->IsValidBodyInstance())
	{
		return false;
	}
	// Body modifiers change shape collision, not the component-level physics filter.
	// CharacterMesh defaults to QueryOnly, which prevents Chaos simulation altogether.
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	// Mover owns the component transform; Chaos owns only the bodies. Letting a simulated
	// pelvis also move the mesh component would move its own world-space animation targets.
	Mesh->PhysicsTransformUpdateMode = EPhysicsTransformUpdateMode::ComponentTransformIsKinematic;
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mesh->bEnableUpdateRateOptimizations = false;
	bControlsInitialized = CreateControlsAndBodyModifiersFromPhysicsControlAsset(Mesh, nullptr, NAME_None)
		&& !GetControlNamesInSet(GarPhysicsControl::AllSetName).IsEmpty()
		&& !GetBodyModifierNamesInSet(GarPhysicsControl::AllSetName).IsEmpty();
	if (!bControlsInitialized)
	{
		UE_LOG(LogGar, Error, TEXT("Unable to create Physics Control records for %s"), *GetPathName());
		DestroyAllControlsAndBodyModifiers();
	}
	else
	{
		for (const USkeletalBodySetup* Body : Mesh->GetPhysicsAsset()->SkeletalBodySetups)
		{
			if (Body->BoneName == TEXT("thigh_l") || Mesh->BoneIsChildOf(Body->BoneName, TEXT("thigh_l")))
			{
				LeftLegBones.Add(Body->BoneName);
			}
			if (Body->BoneName == TEXT("thigh_r") || Mesh->BoneIsChildOf(Body->BoneName, TEXT("thigh_r")))
			{
				RightLegBones.Add(Body->BoneName);
			}
		}
		if (!ApplySelectedProfiles(FGameplayTag::EmptyTag, true))
		{
			DestroyAllControlsAndBodyModifiers();
			LeftLegBones.Reset();
			RightLegBones.Reset();
			bControlsInitialized = false;
		}
	}
	return bControlsInitialized;
}

bool UGarPhysicsControlComponent::EvaluateProfile(const FGameplayTagContainer& GameplayTags, const FGameplayTag RagdollTag,
	FGarPhysicsControlProfileChooserResult& OutResult) const
{
	OutResult = {};
	if (!IsValid(ProfileChooser)) return false;

	// Ability tags may be added after Begin(), or still be present during End(). Never let
	// their timing take ownership of the physical ragdoll lifecycle away from the task.
	FGameplayTagContainer EvaluationTags = GameplayTags;
	FGameplayTagContainer RagdollTags;
	RagdollTags.AddTag(GarLocomotionActionTags::FreeFalling);
	RagdollTags.AddTag(GarLocomotionActionTags::Unconsious);
	RagdollTags.AddTag(GarLocomotionActionTags::Dying);
	RagdollTags.AddTag(DefaultRagdollTag);
	for (const auto& Pair : RagdollSettingsByTag) RagdollTags.AddTag(Pair.Key);
	for (const FGameplayTag& Tag : GameplayTags)
	{
		if (Tag.MatchesAny(RagdollTags)) EvaluationTags.RemoveTag(Tag);
	}
	EvaluationTags.AddTag(RagdollTag);

	FChooserEvaluationContext Context;
	Context.AddStructParam(EvaluationTags);
	Context.AddStructParam(OutResult);
	const FInstancedStruct Chooser = UChooserFunctionLibrary::MakeEvaluateChooser(ProfileChooser);
	UChooserFunctionLibrary::EvaluateObjectChooserBase(Context, Chooser, nullptr);
	return !OutResult.ControlProfileName.IsNone();
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

bool UGarPhysicsControlComponent::ApplySelectedProfiles(const FGameplayTag RagdollTag, const bool bForce)
{
	if (!bControlsInitialized || !Character.IsValid())
	{
		return false;
	}

	FGameplayTagContainer GameplayTags;
	Character->GetAbilitySystemComponent()->GetOwnedGameplayTags(GameplayTags);
	FGarPhysicsControlProfileChooserResult Selected;
	if (!EvaluateProfile(GameplayTags, RagdollTag, Selected))
	{
		UE_LOG(LogGar, Error, TEXT("Physics Control ProfileChooser has no valid output on %s"), *GetPathName());
		return false;
	}
	const bool bControlChanged = bForce || Selected.ControlProfileName != CurrentControlProfileName;
	const bool bConstraintChanged = bForce || Selected.ConstraintProfileName != CurrentConstraintProfileName;
	if (!bControlChanged && !bConstraintChanged) return true;

	// Validate the pair before changing either side. Missing names must not silently select
	// another state. Per-joint omissions in a valid PA profile still use PA defaults.
	const UPhysicsControlAsset* Asset = PhysicsControlAsset.Get();
	if (!Asset || !Asset->Profiles.Contains(BaseControlProfileName) || !Asset->Profiles.Contains(Selected.ControlProfileName))
	{
		UE_LOG(LogGar, Error, TEXT("Missing Physics Control profile '%s' or base '%s' on %s"),
			*Selected.ControlProfileName.ToString(), *BaseControlProfileName.ToString(), *GetPathName());
		return false;
	}
	const UPhysicsAsset* PhysicsAsset = Character->GetMesh()->GetPhysicsAsset();
	if (!Selected.ConstraintProfileName.IsNone() && (!PhysicsAsset || !PhysicsAsset->ConstraintSetup.ContainsByPredicate(
		[&Selected](const UPhysicsConstraintTemplate* Joint) { return Joint && Joint->ContainsConstraintProfile(Selected.ConstraintProfileName); })))
	{
		UE_LOG(LogGar, Error, TEXT("Missing constraint profile '%s' on %s"), *Selected.ConstraintProfileName.ToString(), *GetPathName());
		return false;
	}
	if (bControlChanged)
	{
		if (!InvokeControlProfile(BaseControlProfileName)) return false;
		if (Selected.ControlProfileName != BaseControlProfileName && !InvokeControlProfile(Selected.ControlProfileName)) return false;
		CurrentControlProfileName = Selected.ControlProfileName;
	}
	if (bConstraintChanged)
	{
		UpdateJointConstraints(Selected.ConstraintProfileName, RagdollTag.IsValid());
		CurrentConstraintProfileName = Selected.ConstraintProfileName;
	}
	return true;
}

void UGarPhysicsControlComponent::UpdateCurveDrivenPhysicsBlending()
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
		if (Mapping.CurveName.IsNone() || Mapping.BodyModifierSetName.IsNone())
		{
			continue;
		}

		const float LockAmount = AnimationInstance->GetCurveValueClamped01(Mapping.CurveName);
		// Keep animation tracking active while the simulated pose is hidden. Weakening the
		// drives here lets bodies drift before their physics pose is blended back in.
		SetBodyModifiersInSetPhysicsBlendWeight(Mapping.BodyModifierSetName, 1.0f - LockAmount);
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

	RagdollStatus.bGrounded = Mover->GetLocomotionMode() == GarLocomotionModeTags::Grounded;

	for (FBodyInstance* Body : Mesh->Bodies)
	{
		if (Body && Body->IsInstanceSimulatingPhysics() && Settings->MaxBodySpeed > 0.0f)
		{
			const FVector Velocity = Body->GetUnrealWorldVelocity();
			if (Velocity.Size() > Settings->MaxBodySpeed)
			{
				Body->SetLinearVelocity(Velocity.GetClampedToMaxSize(Settings->MaxBodySpeed), false);
			}
		}
	}

	if (Mesh->GetBoneIndex(ChestBoneName) != INDEX_NONE)
	{
		const FTransform Chest = Mesh->GetBoneTransform(ChestBoneName, RTS_World);
		const float FacingDot = FVector::DotProduct(Chest.GetRotation().GetRightVector(), Mover->GetUpDirection());
		// Use the physical chest axis, independent of the capsule orientation that Mover is following.
		if (FacingDot > 0.2f) RagdollStatus.bFacingUpward = true;
		else if (FacingDot < -0.2f) RagdollStatus.bFacingUpward = false;
	}

	RefreshRagdollAnimation(true);
	FVector RootVelocity = FVector::ZeroVector;
	GetTopBodyVelocity(RootVelocity);
	RagdollStatus.RootBodySpeed = RootVelocity.Size();

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
		SetControlsInSetEnabled(GarPhysicsControl::AllSetName, false);
		SetBodyModifiersInSetMovementType(GarPhysicsControl::AllSetName, EPhysicsMovementType::Kinematic);
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

void UGarPhysicsControlComponent::UpdateJointConstraints(const FName ProfileName, const bool bForRagdoll)
{
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	// Keep the selected PA limits. Only disable its motors: Physics Control owns the drives.
	Mesh->SetConstraintProfileForAll(ProfileName, true);
	for (FConstraintInstance* Constraint : Mesh->Constraints)
	{
		if (!Constraint) continue;
		Constraint->SetOrientationDriveTwistAndSwing(false, false);
		Constraint->SetOrientationDriveSLERP(false);
		Constraint->SetAngularVelocityDriveTwistAndSwing(false, false);
		Constraint->SetAngularVelocityDriveSLERP(false);
		Constraint->SetLinearPositionDrive(false, false, false);
		Constraint->SetLinearVelocityDrive(false, false, false);
	}
	if (!LeftLegBones.IsEmpty() && !RightLegBones.IsEmpty())
	{
		GarPhysicsControl::SetLegCollisionEnabled(Mesh, LeftLegBones, RightLegBones, bForRagdoll);
	}
}

void UGarPhysicsControlComponent::RestoreCapsuleCollision()
{
	if (UCapsuleComponent* Capsule = Character->GetCapsule())
	{
		Capsule->SetCollisionResponseToChannels(PreviousCapsuleResponses);
	}
	if (UCapsuleComponent* ProneCapsule = Character->GetProneCapsule())
	{
		ProneCapsule->SetCollisionResponseToChannels(PreviousProneCapsuleResponses);
	}
}
