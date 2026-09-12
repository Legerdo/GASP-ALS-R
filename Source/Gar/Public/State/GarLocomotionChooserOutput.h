// Copyright (c) SAM-tak. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "AlphaBlend.h"
#include "GarLocomotionChooserOutput.generated.h"

/** Output context of the locomotion Chooser. Defaults match its original Blueprint struct. */
USTRUCT(BlueprintType)
struct GAR_API FGarLocomotionChooserOutput
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion Chooser")
	double StartTime{0.0};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion Chooser")
	double BlendTime{0.3};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion Chooser")
	EAlphaBlendOption BlendCurve{EAlphaBlendOption::QuadraticInOut};

	/** Resolved against the skeleton before constructing FGarBlendStackInputs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion Chooser")
	FName BlendProfile{NAME_None};

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion Chooser")
	TArray<FName> Tags;
};
