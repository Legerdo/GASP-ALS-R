// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MovementMode.h"
#include "MoverTypes.h"
#include "GarSlidingState.generated.h"

/**
 * スライディング中だけ使用する、開始時方位とその後の弱い旋回結果。
 * この値は SyncState に保持するため、リプレイ・ネットワーク補正でも同じ方位を使える。
 */
USTRUCT()
struct FGarSlidingState : public FMoverDataStructBase
{
	GENERATED_BODY()

	virtual UScriptStruct* GetScriptStruct() const override;
	virtual FMoverDataStructBase* Clone() const override;
	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override;
	virtual void ToString(FAnsiStringBuilderBase& Out) const override;
	virtual bool ShouldReconcile(const FMoverDataStructBase& AuthorityState) const override;
	virtual void Interpolate(const FMoverDataStructBase& From, const FMoverDataStructBase& To, float Pct) override;

	/** 現在の滑走方位。Mover の Up 平面上で正規化される。 */
	UPROPERTY(BlueprintReadOnly, Category = "Mover|Sliding")
	FVector Heading = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly, Category = "Mover|Sliding")
	bool bHasHeading = false;
};
