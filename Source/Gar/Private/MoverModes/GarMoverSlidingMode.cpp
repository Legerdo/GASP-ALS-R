// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoverModes/GarMoverSlidingMode.h"

#include "State/GarSlidingState.h"
#include "GarCharacterMoverComponent.h"
#include "GarGameplayTags.h"
#include "Math/RotationMatrix.h"
#include "MoverComponent.h"
#include "MoverDataModelTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GarMoverSlidingMode)

namespace GarSlidingMode
{
	FVector GetPlanarDirection(const FVector& Direction, const FVector& UpDirection)
	{
		return (Direction - Direction.ProjectOnTo(UpDirection)).GetSafeNormal();
	}

	FVector GetInitialHeading(const FMoverTickStartData& StartState, const FVector& UpDirection)
	{
		if (const FMoverDefaultSyncState* StartingSyncState = StartState.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>())
		{
			if (const FVector VelocityHeading = GetPlanarDirection(StartingSyncState->GetVelocity_WorldSpace(), UpDirection);
				!VelocityHeading.IsNearlyZero())
			{
				return VelocityHeading;
			}

			if (const FVector IntentHeading = GetPlanarDirection(StartingSyncState->GetIntent_WorldSpace(), UpDirection);
				!IntentHeading.IsNearlyZero())
			{
				return IntentHeading;
			}

			if (const FVector FacingHeading = GetPlanarDirection(StartingSyncState->GetOrientation_WorldSpace().Quaternion().GetForwardVector(), UpDirection);
				!FacingHeading.IsNearlyZero())
			{
				return FacingHeading;
			}
		}

		if (const FCharacterDefaultInputs* CharacterInputs = StartState.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>())
		{
			if (const FVector InputHeading = GetPlanarDirection(CharacterInputs->GetMoveInput_WorldSpace(), UpDirection);
				!InputHeading.IsNearlyZero())
			{
				return InputHeading;
			}
		}

		return FVector::ForwardVector;
	}
}

UGarMoverSlidingMode::UGarMoverSlidingMode()
{
	GameplayTags.Reset();
	GameplayTags.AddTag(GarLocomotionModeTags::Grounded);
	GameplayTags.AddTag(GarLocomotionActionTags::Sliding);
}

void UGarMoverSlidingMode::OnRegistered(const FName ModeName, const FMoverSimContext& SimContext)
{
	Super::OnRegistered(ModeName, SimContext);
}

void UGarMoverSlidingMode::OnUnregistered(const FMoverSimContext& SimContext)
{
	Super::OnUnregistered(SimContext);
}

void UGarMoverSlidingMode::Activate(const FMoverEventContext& Context, FName PrevModeName, const FMoverSimContext& SimContext, const FMoverTickStartData& StartState, FMoverSyncState* OutSyncState, FMoverAuxStateContext* OutAuxState)
{
	bInitialBoost = true;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			InitialBoostTimerHandle,
			this, &UGarMoverSlidingMode::OnInitialBoostExpired,
			static_cast<float>(InitialBoostTime),
			false);
	}

	Super::Activate(Context, PrevModeName, SimContext, StartState, OutSyncState, OutAuxState);

	if (OutSyncState != nullptr)
	{
		const UMoverComponent* MoverComponent = GetMoverComponent();
		const FVector UpDirection = IsValid(MoverComponent) ? MoverComponent->GetUpDirection() : FVector::UpVector;

		FGarSlidingState& SlidingState = OutSyncState->SyncStateCollection.FindOrAddMutableDataByType<FGarSlidingState>();
		SlidingState.Heading = GarSlidingMode::GetInitialHeading(StartState, UpDirection);
		SlidingState.bHasHeading = true;
	}
}

void UGarMoverSlidingMode::Deactivate(const FMoverEventContext& Context, FName NextModeName, const FMoverSimContext& SimContext)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(InitialBoostTimerHandle);
	}
	bInitialBoost = false;

	Super::Deactivate(Context, NextModeName, SimContext);
}

void UGarMoverSlidingMode::OnInitialBoostExpired()
{
	bInitialBoost = false;
}

void UGarMoverSlidingMode::SimulationTick_Implementation(const FSimulationTickParams& Params, FMoverTickEndData& OutputState)
{
	Super::SimulationTick_Implementation(Params, OutputState);

	// GenerateWalkMove で更新した方位を次フレームへ渡す。
	if (const FGarSlidingState* InSlidingState = Params.StartState.SyncState.SyncStateCollection.FindDataByType<FGarSlidingState>())
	{
		OutputState.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FGarSlidingState>() = *InSlidingState;
	}

	if (OutputState.MovementEndState.NextModeName != NAME_None)
	{
		return;
	}

	const UMoverComponent* MoverComp = GetMoverComponent();
	const UGarCharacterMoverComponent* GarMoverComp = Cast<UGarCharacterMoverComponent>(MoverComp);

	FHitResult FloorHit;
	const bool bHasFloor = IsValid(MoverComp) && MoverComp->TryGetFloorCheckHitResult(FloorHit);
	const bool bIsGrounded = bHasFloor && (!IsValid(GarMoverComp) || GarMoverComp->IsWalkable(FloorHit));
	if (!bIsGrounded)
	{
		OutputState.MovementEndState.NextModeName = TEXT("Falling");
		return;
	}

	const FVector LinearVelocity2D(Params.ProposedMove.LinearVelocity.X, Params.ProposedMove.LinearVelocity.Y, 0.0f);
	if (LinearVelocity2D.Size() <= ExitToWalkingSpeedThreshold)
	{
		OutputState.MovementEndState.NextModeName = TEXT("Walking");
	}
}

void UGarMoverSlidingMode::GenerateWalkMove_Implementation(
	FMoverTickStartData& StartState,
	float DeltaSeconds,
	const FMoverSimContext& SimContext,
	const FVector& DesiredVelocity,
	const FQuat& DesiredFacing,
	const FQuat& CurrentFacing,
	FVector& InOutAngularVelocityDegrees,
	FVector& InOutVelocity)
{
	const UMoverComponent* MoverComponent = GetMoverComponent();
	if (!IsValid(MoverComponent))
	{
		Super::GenerateWalkMove_Implementation(
			StartState, DeltaSeconds, SimContext,
			DesiredVelocity, DesiredFacing, CurrentFacing,
			InOutAngularVelocityDegrees, InOutVelocity);
		return;
	}

	const FVector UpDirection = MoverComponent->GetUpDirection();
	bool bSlidingStateAdded = false;
	FGarSlidingState& SlidingState = StartState.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FGarSlidingState>(bSlidingStateAdded);
	if (bSlidingStateAdded || !SlidingState.bHasHeading)
	{
		SlidingState.Heading = GarSlidingMode::GetInitialHeading(StartState, UpDirection);
		SlidingState.bHasHeading = true;
	}

	float SteeringInput = 0.0f;
	if (const FCharacterDefaultInputs* CharacterInputs = StartState.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>())
	{
		const FRotator InputControlYaw(0.0f, CharacterInputs->ControlRotation.Yaw, 0.0f);
		const FVector InputRight = FRotationMatrix(InputControlYaw).GetUnitAxis(EAxis::Y);
		SteeringInput = FMath::Clamp(
			FVector::DotProduct(CharacterInputs->GetMoveInput_WorldSpace(), InputRight),
			-1.0f, 1.0f);
	}

	const float TurnRadians = FMath::DegreesToRadians(SteeringInput * static_cast<float>(SteeringTurnRate) * DeltaSeconds);
	SlidingState.Heading = GarSlidingMode::GetPlanarDirection(
		FQuat(UpDirection, TurnRadians).RotateVector(SlidingState.Heading), UpDirection);
	if (SlidingState.Heading.IsNearlyZero())
	{
		SlidingState.Heading = GarSlidingMode::GetInitialHeading(StartState, UpDirection);
	}

	// スロープ角を先に計算し、今フレームの Super 計算に反映させる。
	// 滑走方位に対する符号付き角度: 上り坂=負、下り坂=正、平地=0。

	FHitResult FloorHit;
	const bool bHasFloor = GetMoverComponent()->TryGetFloorCheckHitResult(FloorHit);

	float SlopeAngle = 0.0f;
	if (bHasFloor)
	{
		const FVector VelDir = SlidingState.Heading;
		if (!VelDir.IsNearlyZero(UE_KINDA_SMALL_NUMBER))
		{
			const float DotVal = FVector::DotProduct(FloorHit.Normal.GetSafeNormal(), VelDir);
			SlopeAngle = FMath::RadiansToDegrees(FMath::Acos(DotVal)) - 90.0f;
		}
	}

	// InitialBoost フェーズ
	if (bInitialBoost)
	{
		MaxSpeedOverride = static_cast<float>(InitialBoostSpeed);
		Acceleration     = static_cast<float>(InitialBoostAcceleration);
	}
	else
	{
		// SlopeAngle > -ShallowSlopeAngle は平地扱い
		if (SlopeAngle > static_cast<float>(-ShallowSlopeAngle))
		{
			MaxSpeedOverride = static_cast<float>(FlatGroundSpeed);
		}
		else
		{
			MaxSpeedOverride = static_cast<float>(FMath::GetMappedRangeValueClamped(
				FVector2D(-ShallowSlopeAngle, -SteepSlopeAngle),
				FVector2D(ShallowSlopeSpeed, SteepSlopeSpeed),
				static_cast<double>(SlopeAngle)));
		}
		Acceleration = static_cast<float>(AfterBoostAcceleration);
	}

	// 傾斜によるDeceleration (上り坂ほど制動が強い: SteepSlopeDecel、下り坂は FlatGroundDecel)
	Deceleration = static_cast<float>(FMath::GetMappedRangeValueClamped(
		FVector2D(ShallowSlopeAngle, SteepSlopeAngle),
		FVector2D(FlatGroundDeceleration, SteepSlopeDeceleration),
		static_cast<double>(SlopeAngle)));

	// DesiredVelocity / DesiredFacing はカメラ・移動入力から作られるため滑走中には使わない。
	// 保持した方位だけを速度目標・キャラクターの向きに渡す。
	const FVector LockedDesiredVelocity = SlidingState.Heading * MaxSpeedOverride;
	const FQuat LockedDesiredFacing = FQuat::FindBetween(FVector::ForwardVector, SlidingState.Heading);

	Super::GenerateWalkMove_Implementation(
		StartState, DeltaSeconds, SimContext,
		LockedDesiredVelocity, LockedDesiredFacing, CurrentFacing,
		InOutAngularVelocityDegrees, InOutVelocity);
}
