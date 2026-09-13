// Copyright Epic Games, Inc. All Rights Reserved.

#include "State/GarSlidingState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GarSlidingState)

UScriptStruct* FGarSlidingState::GetScriptStruct() const
{
	return StaticStruct();
}

FMoverDataStructBase* FGarSlidingState::Clone() const
{
	return new FGarSlidingState(*this);
}

bool FGarSlidingState::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	const bool bSuccess = Super::NetSerialize(Ar, Map, bOutSuccess);

	Ar << Heading;
	Ar.SerializeBits(&bHasHeading, 1);

	bOutSuccess = bOutSuccess && bSuccess && !Ar.IsError();
	return bOutSuccess;
}

void FGarSlidingState::ToString(FAnsiStringBuilderBase& Out) const
{
	Super::ToString(Out);
	Out.Appendf("Heading=%s HasHeading=%d\n", *Heading.ToCompactString(), bHasHeading);
}

bool FGarSlidingState::ShouldReconcile(const FMoverDataStructBase& AuthorityState) const
{
	const FGarSlidingState* Authority = static_cast<const FGarSlidingState*>(&AuthorityState);
	return bHasHeading != Authority->bHasHeading
		|| (bHasHeading && FVector::DotProduct(Heading, Authority->Heading) < 0.9998f);
}

void FGarSlidingState::Interpolate(const FMoverDataStructBase& From, const FMoverDataStructBase& To, float Pct)
{
	const FGarSlidingState* FromState = static_cast<const FGarSlidingState*>(&From);
	const FGarSlidingState* ToState = static_cast<const FGarSlidingState*>(&To);

	bHasHeading = Pct < 0.5f ? FromState->bHasHeading : ToState->bHasHeading;
	if (FromState->bHasHeading && ToState->bHasHeading)
	{
		Heading = FMath::Lerp(FromState->Heading, ToState->Heading, Pct).GetSafeNormal();
	}
	else
	{
		Heading = bHasHeading
			? (Pct < 0.5f ? FromState->Heading : ToState->Heading)
			: FVector::ForwardVector;
	}
}
