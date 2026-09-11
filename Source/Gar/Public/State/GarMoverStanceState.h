// Copyright (c) SAM-tak. All Rights Reserved.

#pragma once

#include "MoverDataModelTypes.h"
#include "GarMoverStanceState.generated.h"

/** Interpolated stance geometry, retained across movement modes and network replay. */
USTRUCT()
struct GAR_API FGarMoverStanceState : public FMoverDataStructBase
{
	GENERATED_BODY()

	UPROPERTY()
	float CapsuleRadius = 0.0f;
	UPROPERTY()
	float CapsuleHalfHeight = 0.0f;
	UPROPERTY()
	float ProneRadius = 0.0f;
	UPROPERTY()
	float ProneHalfHeight = 0.0f;
	UPROPERTY()
	float ProneOffsetX = 0.0f;
	UPROPERTY()
	float EyeHeight = 0.0f;
	UPROPERTY()
	bool bProneCollisionEnabled = false;
	UPROPERTY()
	bool bInitialized = false;

	bool operator==(const FGarMoverStanceState& Other) const
	{
		return CapsuleRadius == Other.CapsuleRadius && CapsuleHalfHeight == Other.CapsuleHalfHeight
			&& ProneRadius == Other.ProneRadius && ProneHalfHeight == Other.ProneHalfHeight
			&& ProneOffsetX == Other.ProneOffsetX && EyeHeight == Other.EyeHeight
			&& bProneCollisionEnabled == Other.bProneCollisionEnabled && bInitialized == Other.bInitialized;
	}

	virtual FMoverDataStructBase* Clone() const override { return new FGarMoverStanceState(*this); }
	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override
	{
		Super::NetSerialize(Ar, Map, bOutSuccess);
		Ar << CapsuleRadius << CapsuleHalfHeight << ProneRadius << ProneHalfHeight << ProneOffsetX << EyeHeight;
		Ar.SerializeBits(&bProneCollisionEnabled, 1);
		Ar.SerializeBits(&bInitialized, 1);
		bOutSuccess = bOutSuccess && !Ar.IsError();
		return true;
	}
	virtual bool ShouldReconcile(const FMoverDataStructBase& AuthorityState) const override
	{
		return !(*this == static_cast<const FGarMoverStanceState&>(AuthorityState));
	}
	virtual void Interpolate(const FMoverDataStructBase& From, const FMoverDataStructBase& To, float Pct) override
	{
		const auto& A = static_cast<const FGarMoverStanceState&>(From);
		const auto& B = static_cast<const FGarMoverStanceState&>(To);
		*this = Pct < 0.5f ? A : B;
		if (A.bInitialized && B.bInitialized)
		{
			CapsuleRadius = FMath::Lerp(A.CapsuleRadius, B.CapsuleRadius, Pct);
			CapsuleHalfHeight = FMath::Lerp(A.CapsuleHalfHeight, B.CapsuleHalfHeight, Pct);
			ProneRadius = FMath::Lerp(A.ProneRadius, B.ProneRadius, Pct);
			ProneHalfHeight = FMath::Lerp(A.ProneHalfHeight, B.ProneHalfHeight, Pct);
			ProneOffsetX = FMath::Lerp(A.ProneOffsetX, B.ProneOffsetX, Pct);
			EyeHeight = FMath::Lerp(A.EyeHeight, B.EyeHeight, Pct);
		}
	}
	virtual void Merge(const FMoverDataStructBase& From) override
	{
		*this = static_cast<const FGarMoverStanceState&>(From);
	}
	virtual void ToString(FAnsiStringBuilderBase& Out) const override
	{
		Out.Appendf("Capsule: %.3f/%.3f Prone: %.3f/%.3f X: %.3f Eye: %.3f Weld: %d\n",
			CapsuleRadius, CapsuleHalfHeight, ProneRadius, ProneHalfHeight, ProneOffsetX, EyeHeight, bProneCollisionEnabled);
	}
};

template<>
struct TStructOpsTypeTraits<FGarMoverStanceState> : public TStructOpsTypeTraitsBase2<FGarMoverStanceState>
{
	enum { WithNetSerializer = true, WithCopy = true };
};
