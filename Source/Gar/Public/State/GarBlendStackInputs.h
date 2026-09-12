// Copyright (c) SAM-tak. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "AlphaBlend.h"
#include "GarBlendStackInputs.generated.h"

class UAnimationAsset;
class UBlendProfile;

/** Playback request consumed by AB_Gar's shared Blend Stack. */
USTRUCT(BlueprintType)
struct GAR_API FGarBlendStackInputs
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend Stack")
	TObjectPtr<UAnimationAsset> Anim{nullptr};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend Stack")
	bool bLoop{false};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend Stack")
	double StartTime{0.0};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend Stack")
	double BlendTime{0.0};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend Stack")
	EAlphaBlendOption BlendCurve{EAlphaBlendOption::Linear};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend Stack")
	TObjectPtr<UBlendProfile> BlendProfile{nullptr};

	/** Animation-selection labels, not gameplay state tags. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend Stack")
	TArray<FName> Tags;
};
