// Copyright (c) SAM-tak. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "GarPhysicsControlMigrationLibrary.generated.h"

class UBlueprint;
class UPhysicsControlAsset;
class UPhysicsAsset;
class UChooserTable;

/** Editor-only graph migration helpers for the removal of UGarPhysicalAnimationComponent. */
UCLASS()
class GAREDITOR_API UGarPhysicsControlMigrationLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** One-time authoring of the tag-to-profile-pair Chooser. Does not save assets. */
	UFUNCTION(BlueprintCallable, Category = "GAR|PhysicsControl|Migration")
	static bool ConfigureProfileChooser(UChooserTable* Table);

	/** Seeds missing per-state profiles from the current baselines; preserves existing tuning. */
	UFUNCTION(BlueprintCallable, Category = "GAR|PhysicsControl|Migration")
	static bool CreateProfileVariants(UPhysicsControlAsset* Asset);

	/** Makes the former runtime free-angle policy an explicit PA profile, without changing old profiles. */
	UFUNCTION(BlueprintCallable, Category = "GAR|PhysicsControl|Migration")
	static bool EnsureFreeConstraintProfile(UPhysicsAsset* Asset);

	/** One-time baseline authoring. Replaces only PhysicalAnimation/Ragdoll in the supplied PCA. */
	UFUNCTION(BlueprintCallable, Category = "GAR|PhysicsControl|Migration")
	static bool ConfigureBaselineProfiles(UPhysicsControlAsset* Asset);

	/** Replace the built-in ragdoll ability's capsule-velocity settling check. */
	UFUNCTION(BlueprintCallable, Category = "GAR|PhysicsControl|Migration")
	static bool MigrateRagdollVelocity(UBlueprint* Blueprint);

	/** Read-only export for comparing sample Blueprint logic during C++ migrations. */
	UFUNCTION(BlueprintCallable, Category = "GAR|PhysicsControl|Migration")
	static FString ExportBlueprintGraphs(UBlueprint* Blueprint);

	/** Rebinds legacy PhysicalAnimation variable and function nodes to UGarPhysicsControlComponent. */
	UFUNCTION(BlueprintCallable, Category = "GAR|PhysicsControl|Migration")
	static bool MigrateLegacyPhysicalAnimationReferences(UBlueprint* Blueprint);

	/** Rebinds the old GettingUp ragdoll predicate node to UGarPhysicsControlComponent. */
	UFUNCTION(BlueprintCallable, Category = "GAR|PhysicsControl|Migration")
	static bool MigrateGettingUpAbility(UBlueprint* AbilityBlueprint);
};
