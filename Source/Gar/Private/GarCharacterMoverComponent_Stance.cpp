// Copyright (c) SAM-tak. All Rights Reserved.

#include "GarCharacterMoverComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GarCharacter.h"
#include "MoveLibrary/MoverBlackboard.h"
#include "MoverEffects/GarMoverCapsuleResizeEffect.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Settings/GarCharacterSettings.h"
#include "State/GarCharacterMoverInputs.h"
#include "State/GarMoverStanceState.h"

FGarMoverStanceState UGarCharacterMoverComponent::CaptureStanceState() const
{
	FGarMoverStanceState State;
	const auto* Pawn = Cast<AGarCharacter>(GetOwner());
	if (Pawn && Pawn->Capsule && Pawn->ProneCapsule)
	{
		Pawn->Capsule->GetUnscaledCapsuleSize(State.CapsuleRadius, State.CapsuleHalfHeight);
		Pawn->ProneCapsule->GetUnscaledCapsuleSize(State.ProneRadius, State.ProneHalfHeight);
		State.ProneOffsetX = Pawn->ProneCapsule->GetRelativeLocation().X;
		State.EyeHeight = Pawn->BaseEyeHeight;
		State.bProneCollisionEnabled = Pawn->ProneCapsule->IsWelded();
		State.bInitialized = true;
	}
	return State;
}

void UGarCharacterMoverComponent::GetDefaultInputAndState(FMoverInputCmdContext& OutInputCmd,
	FMoverSyncState& OutSyncState, FMoverAuxStateContext& OutAuxState) const
{
	Super::GetDefaultInputAndState(OutInputCmd, OutSyncState, OutAuxState);
	OutSyncState.SyncStateCollection.FindOrAddMutableDataByType<FGarMoverStanceState>() = CaptureStanceState();
}

void UGarCharacterMoverComponent::OnPreSimulate(const FMoverTimeStep& TimeStep, const FMoverTickStartData& StartingData)
{
	// A rollback restores the root transform, but Mover does not restore custom collision shapes.
	// Restore those from the same frame before queries or interpolation use them. Do NOT add
	// another pivot adjustment here: the restored root transform already contains that adjustment.
	if (const auto* State = StartingData.SyncState.SyncStateCollection.FindDataByType<FGarMoverStanceState>())
	{
		if (ApplyStanceState(*State))
		{
			// Re-query the floor after restoring simulation geometry. The pivot did not move,
			// so retain the base snapshot. Effects/movement modes manage changes to that base.
			if (UMoverBlackboard* Blackboard = GetSimBlackboard_Mutable())
			{
				Blackboard->Invalidate(CommonBlackboard::LastFloorResult);
			}
		}
	}
	Super::OnPreSimulate(TimeStep, StartingData);
	QueueStanceUpdate(TimeStep, StartingData);
}

void UGarCharacterMoverComponent::QueueStanceUpdate(const FMoverTimeStep& TimeStep, const FMoverTickStartData& StartingData)
{
	const auto* Inputs = StartingData.InputCmd.InputCollection.FindDataByType<FGarCharacterMoverInputs>();
	if (!Inputs || !Character.IsValid() || !Character->Settings || TimeStep.StepMs <= 0.0f) return;

	const auto* Saved = StartingData.SyncState.SyncStateCollection.FindDataByType<FGarMoverStanceState>();
	const FGarMoverStanceState Current = Saved && Saved->bInitialized ? *Saved : CaptureStanceState();
	if (!Current.bInitialized) return;
	const FGarMoverStanceState Next = AdvanceStance(Current, *Inputs, TimeStep.StepMs * 0.001f);
	if (Saved && Saved->bInitialized && Next == Current) return;

	auto Effect = MakeShared<FGarMoverCapsuleResizeEffect>();
	Effect->StanceState = Next;
	Effect->PreviousHalfHeight = Current.CapsuleHalfHeight;
	QueueInstantMovementEffect(Effect);
}

FGarMoverStanceState UGarCharacterMoverComponent::AdvanceStance(const FGarMoverStanceState& Current,
	const FGarCharacterMoverInputs& Inputs, float DeltaTime) const
{
	FGarMoverStanceState Next = Current;
	const AGarCharacter& Pawn = *Character.Get();
	const bool bLying = Inputs.Stance.MatchesTag(GarStanceTags::Lying);
	const bool bCrouching = Inputs.Stance.MatchesTag(GarStanceTags::Crouching);
	if (!bLying && !bCrouching && !Inputs.Stance.MatchesTag(GarStanceTags::Standing)) return Next;

	// Preserve GAR's existing transition speeds and authored Character/Settings values.
	const float Duration = Pawn.Settings->CapsuleUpdateSpeed;
	const float TargetEye = bLying ? Pawn.LiedEyeHeight : bCrouching ? Pawn.CrouchedEyeHeight : Pawn.InitialEyeHeight;
	const float EyeDistance = bLying ? FMath::Abs(Pawn.CrouchedEyeHeight - Pawn.LiedEyeHeight)
		: FMath::Abs(Pawn.InitialEyeHeight - Pawn.CrouchedEyeHeight);
	Next.EyeHeight = Duration > 0.0f
		? FMath::FInterpConstantTo(Current.EyeHeight, TargetEye, DeltaTime, EyeDistance / Duration) : TargetEye;

	// Eye height still follows stance while the capsule is locked, as it did in Character Tick.
	// Use the recorded inputs, not live GAS/physics state, when replaying a simulation frame.
	if (Inputs.bBlockCapsuleResize || Inputs.bHasRagdollTransform) return Next;

	// Retain the existing async-physics fallback. This component still uses game-thread Mover,
	// not the separate ChaosMover ApplyMovementEffect_Async API.
	const float CapsuleDuration = UPhysicsSettings::Get()->bTickPhysicsAsync ? 0.0f : Duration;
	const auto Interp = [DeltaTime, CapsuleDuration](float Value, float Target, float Distance)
	{
		return CapsuleDuration > 0.0f
			? FMath::FInterpConstantTo(Value, Target, DeltaTime, FMath::Abs(Distance) / CapsuleDuration) : Target;
	};
	Next.CapsuleRadius = FMath::Max(0.0f, Pawn.InitialCapsuleRadius);
	const float TargetHeight = FMath::Max(Next.CapsuleRadius,
		bLying ? Pawn.LiedCapsuleHalfHeight : bCrouching ? Pawn.CrouchedCapsuleHalfHeight : Pawn.InitialCapsuleHalfHeight);
	Next.CapsuleHalfHeight = FMath::Max(Next.CapsuleRadius, Interp(Current.CapsuleHalfHeight, TargetHeight,
		bLying ? Pawn.CrouchedCapsuleHalfHeight - Pawn.LiedCapsuleHalfHeight
			: Pawn.InitialCapsuleHalfHeight - Pawn.CrouchedCapsuleHalfHeight));
	Next.ProneRadius = FMath::Max(0.0f, Pawn.InitialProneCapsuleRadius);
	Next.ProneHalfHeight = FMath::Max(Next.ProneRadius, Interp(Current.ProneHalfHeight,
		FMath::Max(Next.ProneRadius, bLying ? Pawn.LiedProneCapsuleHalfHeight : Pawn.InitialProneCapsuleHalfHeight),
		Pawn.InitialProneCapsuleHalfHeight - Pawn.LiedProneCapsuleHalfHeight));
	Next.ProneOffsetX = Interp(Current.ProneOffsetX,
		Pawn.InitialProneCapsuleX + (bLying ? Pawn.LiedProneCapsuleZOffset : 0.0f), Pawn.LiedProneCapsuleZOffset);
	const bool bProneChanging = Next.ProneHalfHeight != Current.ProneHalfHeight
		|| Next.ProneRadius != Current.ProneRadius || Next.ProneOffsetX != Current.ProneOffsetX;
	Next.bProneCollisionEnabled = bLying || bProneChanging;
	return Next;
}

bool UGarCharacterMoverComponent::ApplyStanceState(const FGarMoverStanceState& State, float CapsuleHeightDelta)
{
	if (!State.bInitialized || !Character.IsValid()) return false;
	AGarCharacter& Pawn = *Character.Get();
	UCapsuleComponent* Capsule = Pawn.Capsule;
	UCapsuleComponent* Prone = Pawn.ProneCapsule;
	if (!Capsule || !Prone) return false;

	const bool bMainChanged = Capsule->GetUnscaledCapsuleRadius() != State.CapsuleRadius
		|| Capsule->GetUnscaledCapsuleHalfHeight() != State.CapsuleHalfHeight;
	const bool bProneChanged = Prone->GetUnscaledCapsuleRadius() != State.ProneRadius
		|| Prone->GetUnscaledCapsuleHalfHeight() != State.ProneHalfHeight
		|| Prone->GetRelativeLocation().X != State.ProneOffsetX;
	const ECollisionEnabled::Type ProneCollision = State.bProneCollisionEnabled
		? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision;
	const bool bCollisionChanged = Prone->GetCollisionEnabled() != ProneCollision
		|| Prone->IsWelded() != State.bProneCollisionEnabled;

	// Detach before resizing either shape so Chaos never retains the previous welded geometry.
	if (Prone->IsWelded() && (bMainChanged || bProneChanged || !State.bProneCollisionEnabled))
	{
		Prone->UnWeldFromParent();
	}
	if (bMainChanged) Capsule->SetCapsuleSize(State.CapsuleRadius, State.CapsuleHalfHeight, false);
	if (bProneChanged)
	{
		Prone->SetCapsuleSize(State.ProneRadius, State.ProneHalfHeight, false);
		FVector Location = Prone->GetRelativeLocation();
		Location.X = State.ProneOffsetX;
		Prone->SetRelativeLocation(Location);
	}
	if (Prone->GetCollisionEnabled() != ProneCollision) Prone->SetCollisionEnabled(ProneCollision);
	if (State.bProneCollisionEnabled && !Prone->IsWelded()) Prone->WeldTo(Capsule, NAME_None, true);

	if (CapsuleHeightDelta != 0.0f)
	{
		Capsule->MoveComponent(GetUpDirection() * CapsuleHeightDelta * Capsule->GetShapeScale(),
			Capsule->GetComponentQuat(), false, nullptr, MOVECOMP_NoFlags, ETeleportType::None);
	}

	if (USkeletalMeshComponent* Mesh = Pawn.Mesh)
	{
		FTransform VisualOffset = GetBaseVisualComponentTransform();
		FVector Location = VisualOffset.GetLocation();
		// Both values are local/unscaled. The parent transform applies scale exactly once.
		Location.Z = Pawn.InitialMeshZ + Pawn.InitialCapsuleHalfHeight - State.CapsuleHalfHeight;
		VisualOffset.SetLocation(Location);
		SetBaseVisualComponentTransform(VisualOffset);
		if (USceneComponent* Parent = Mesh->GetAttachParent())
		{
			const FVector WorldLocation = Parent->GetSocketTransform(Mesh->GetAttachSocketName()).TransformPosition(Location);
			Mesh->MoveComponent(WorldLocation - Mesh->GetComponentLocation(), Mesh->GetComponentQuat(),
				false, nullptr, MOVECOMP_SkipPhysicsMove, ETeleportType::None);
		}
	}
	Pawn.BaseEyeHeight = State.EyeHeight;

	return bMainChanged || bProneChanged || bCollisionChanged || CapsuleHeightDelta != 0.0f;
}
