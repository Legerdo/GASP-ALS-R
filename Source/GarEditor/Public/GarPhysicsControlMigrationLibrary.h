// Copyright (c) SAM-tak. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "GarPhysicsControlMigrationLibrary.generated.h"

class UBlueprint;

/** Editor-only graph migration helpers for the removal of UGarPhysicalAnimationComponent. */
UCLASS()
class GAREDITOR_API UGarPhysicsControlMigrationLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Rebinds legacy PhysicalAnimation variable and function nodes to UGarPhysicsControlComponent. */
	UFUNCTION(BlueprintCallable, Category = "GAR|PhysicsControl|Migration")
	static bool MigrateLegacyPhysicalAnimationReferences(UBlueprint* Blueprint);

	/** Rebinds the old GettingUp ragdoll predicate node to UGarPhysicsControlComponent. */
	UFUNCTION(BlueprintCallable, Category = "GAR|PhysicsControl|Migration")
	static bool MigrateGettingUpAbility(UBlueprint* AbilityBlueprint);
};
