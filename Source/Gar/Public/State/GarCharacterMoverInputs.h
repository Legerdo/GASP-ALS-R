#pragma once

#include "MoverDataModelTypes.h"
#include "GarGameplayTags.h"
#include "GarCharacterMoverInputs.generated.h"

USTRUCT(BlueprintType)
struct GAR_API FGarCharacterMoverInputs : public FCharacterDefaultInputs
{
	GENERATED_BODY()

public:
	FGarCharacterMoverInputs() : RotationMode(GarRotationModeTags::ViewDirection), Stance(GarStanceTags::Standing), Gait(GarGaitTags::Running)
	{
	}

	bool operator==(const FGarCharacterMoverInputs& Other) const
	{
		return Super::operator==(Other) && RotationMode == Other.RotationMode && Stance == Other.Stance && Gait == Other.Gait
			&& bHasRagdollTransform == Other.bHasRagdollTransform
			&& (!bHasRagdollTransform || RagdollTransform.Equals(Other.RagdollTransform));
	}

	bool operator!=(const FGarCharacterMoverInputs& Other) const { return !operator==(Other); }

	// @return newly allocated copy of this FCharacterDefaultInputs. Must be overridden by child classes
	virtual FMoverDataStructBase* Clone() const override
	{
		// TODO: ensure that this memory allocation jives with deletion method
		return new FGarCharacterMoverInputs(*this);
	}
	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override
	{
		Super::NetSerialize(Ar, Map, bOutSuccess);
		Ar << RotationMode;
		Ar << Stance;
		Ar << Gait;
		Ar.SerializeBits(&bHasRagdollTransform, 1);
		if (bHasRagdollTransform)
		{
			Ar << RagdollTransform;
		}
		else if (Ar.IsLoading())
		{
			RagdollTransform = FTransform::Identity;
		}
		bOutSuccess = bOutSuccess && !Ar.IsError();
		return true;
	}
	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
	virtual void ToString(FAnsiStringBuilderBase& Out) const override
	{
		Super::ToString(Out);
		Out.Appendf("RotationMode: %s\tStance: %s\tGait: %s\n",
			TCHAR_TO_ANSI(*RotationMode.ToString()), TCHAR_TO_ANSI(*Stance.ToString()), TCHAR_TO_ANSI(*Gait.ToString()));
	}
	//virtual void AddReferencedObjects(FReferenceCollector& Collector) override { Super::AddReferencedObjects(Collector); }
	virtual bool ShouldReconcile(const FMoverDataStructBase& AuthorityState) const override
	{
		const FGarCharacterMoverInputs& TypedAuthority = static_cast<const FGarCharacterMoverInputs&>(AuthorityState);
		return *this != TypedAuthority;
	}
	virtual void Interpolate(const FMoverDataStructBase& From, const FMoverDataStructBase& To, float Pct) override
	{
		Super::Interpolate(From, To, Pct);
		const FGarCharacterMoverInputs& TypedFrom = static_cast<const FGarCharacterMoverInputs&>(From);
		const FGarCharacterMoverInputs& TypedTo = static_cast<const FGarCharacterMoverInputs&>(To);

		const FGarCharacterMoverInputs* ClosestInputs = Pct < 0.5f ? &TypedFrom : &TypedTo;
		if(ClosestInputs->RotationMode.IsValid()) RotationMode = ClosestInputs->RotationMode;
		if(ClosestInputs->Stance.IsValid()) Stance = ClosestInputs->Stance;
		if(ClosestInputs->Gait.IsValid()) Gait = ClosestInputs->Gait;
		bHasRagdollTransform = ClosestInputs->bHasRagdollTransform;
		RagdollTransform = ClosestInputs->RagdollTransform;
	}
	virtual void Merge(const FMoverDataStructBase& From) override
	{
		Super::Merge(From);
		const FGarCharacterMoverInputs& TypedFrom = static_cast<const FGarCharacterMoverInputs&>(From);
		if(TypedFrom.RotationMode.IsValid()) RotationMode = TypedFrom.RotationMode;
		if(TypedFrom.Stance.IsValid()) Stance = TypedFrom.Stance;
		if(TypedFrom.Gait.IsValid()) Gait = TypedFrom.Gait;
		bHasRagdollTransform = TypedFrom.bHasRagdollTransform;
		RagdollTransform = TypedFrom.RagdollTransform;
	}
	//virtual void Decay(float DecayAmount) override { Super::Decay(DecayAmount); }

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = Mover)
	FGameplayTag RotationMode;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = Mover)
	FGameplayTag Stance;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = Mover)
	FGameplayTag Gait;

	/** Sample-style physics pose captured OUTSIDE simulation, retained for network replay. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = Mover)
	bool bHasRagdollTransform{false};

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = Mover)
	FTransform RagdollTransform{FTransform::Identity};
};

template<>
struct TStructOpsTypeTraits<FGarCharacterMoverInputs> : public TStructOpsTypeTraitsBase2<FGarCharacterMoverInputs>
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = true
	};
};
