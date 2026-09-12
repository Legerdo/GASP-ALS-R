// Copyright (c) SAM-tak. All Rights Reserved.

#include "GarPhysicsControlMigrationLibrary.h"

#include "Chooser.h"
#include "ObjectChooser_Class.h"
#include "GameplayTagColumn.h"
#include "GameplayTagQueryColumn.h"
#include "OutputStructColumn.h"
#include "PhysicsControlAsset.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "GarGameplayTags.h"
#include "GarPhysicsControlComponent.h"

bool UGarPhysicsControlMigrationLibrary::ConfigureProfileChooser(UChooserTable* Table)
{
	if (!IsValid(Table)) return false;
	Table->Modify();
	Table->ResultType = EObjectChooserResultType::NoPrimaryResult;
	Table->OutputObjectType = UObject::StaticClass();
	Table->ContextData.Reset();
	FContextObjectTypeStruct& Input = Table->ContextData.AddDefaulted_GetRef().InitializeAs<FContextObjectTypeStruct>();
	Input.Struct = FGameplayTagContainer::StaticStruct();
	Input.Direction = EContextObjectDirection::Read;
	FContextObjectTypeStruct& Output = Table->ContextData.AddDefaulted_GetRef().InitializeAs<FContextObjectTypeStruct>();
	Output.Struct = FGarPhysicsControlProfileChooserResult::StaticStruct();
	Output.Direction = EContextObjectDirection::Write;

	Table->ColumnsStructs.Reset();
	Table->ColumnsStructs.SetNum(2);
	FGameplayTagQueryColumn& Tags = Table->ColumnsStructs[0].InitializeAs<FGameplayTagQueryColumn>();
	FGameplayTagContextProperty& TagBinding = Tags.InputValue.InitializeAs<FGameplayTagContextProperty>();
	TagBinding.Binding.ContextIndex = 0;
	TagBinding.Binding.IsBoundToRoot = true;
	TagBinding.Binding.DisplayName = TEXT("Gameplay Tags");
	FOutputStructColumn& Profiles = Table->ColumnsStructs[1].InitializeAs<FOutputStructColumn>();
	FStructContextProperty& ProfileBinding = Profiles.InputValue.InitializeAs<FStructContextProperty>();
	ProfileBinding.Binding.ContextIndex = 1;
	ProfileBinding.Binding.IsBoundToRoot = true;
	ProfileBinding.Binding.StructType = FGarPhysicsControlProfileChooserResult::StaticStruct();
	ProfileBinding.Binding.DisplayName = TEXT("Physics Profiles");
	Profiles.DefaultRowValue.InitializeAs<FGarPhysicsControlProfileChooserResult>();
	FGarPhysicsControlProfileChooserResult& Fallback = Profiles.FallbackValue.InitializeAs<FGarPhysicsControlProfileChooserResult>();
	Fallback.ControlProfileName = TEXT("Default");
	Fallback.ConstraintProfileName = TEXT("Free");

	// No-primary-result tables still need a successful row result to stop at the first match.
	FInstancedStruct Result;
	Result.InitializeAs<FClassChooser>().Class = UObject::StaticClass();
	Table->ResultsStructs.Reset();
	Table->DisabledRows.Reset();
	Table->FallbackResult = Result;
	const auto AddRow = [&](const FGameplayTagContainer& RowTags, const FName Profile, const bool bRagdoll)
	{
		Tags.RowValues.Add(FGameplayTagQuery::MakeQuery_MatchAnyTags(RowTags));
		FGarPhysicsControlProfileChooserResult& Row = Profiles.RowValues.AddDefaulted_GetRef().InitializeAs<FGarPhysicsControlProfileChooserResult>();
		Row.ControlProfileName = Profile;
		Row.ConstraintProfileName = bRagdoll ? NAME_None : FName(TEXT("Free"));
		Table->ResultsStructs.Add(Result);
	};
	AddRow(FGameplayTagContainer(GarLocomotionActionTags::Dying), TEXT("Dying"), true);
	AddRow(FGameplayTagContainer(GarLocomotionActionTags::FreeFalling), TEXT("FreeFalling"), true);
	AddRow(FGameplayTagContainer(GarLocomotionActionTags::Unconsious), TEXT("Unconsious"), true);
	AddRow(FGameplayTagContainer(GarLocomotionActionTags::Traversal), TEXT("Traversal"), false);
	AddRow(FGameplayTagContainer(GarLocomotionModeTags::InAir), TEXT("InAir"), false);
	AddRow(FGameplayTagContainer(GarStanceTags::Lying), TEXT("Lying"), false);
	FGameplayTagContainer RollingTags(GarLocomotionActionTags::Rolling);
	RollingTags.AddTag(GarLocomotionActionTags::Sliding);
	AddRow(RollingTags, TEXT("Rolling"), false);
	// Preserve legacy row priority, but correct its impossible Rolling AND Sliding condition.
	Table->Compile(true);
	Table->MarkPackageDirty();
	return true;
}

bool UGarPhysicsControlMigrationLibrary::CreateProfileVariants(UPhysicsControlAsset* Asset)
{
	if (!IsValid(Asset) || !Asset->MyProfiles.Contains(TEXT("PhysicalAnimation")) || !Asset->MyProfiles.Contains(TEXT("Ragdoll"))) return false;
	// Copies, not references into a TMap that can be reallocated by Add(). No tuning is invented.
	const FPhysicsControlControlAndModifierUpdates Animated = Asset->MyProfiles[TEXT("PhysicalAnimation")];
	const FPhysicsControlControlAndModifierUpdates Ragdoll = Asset->MyProfiles[TEXT("Ragdoll")];
	Asset->Modify();
	for (const FName Name : {FName(TEXT("Default")), FName(TEXT("Traversal")), FName(TEXT("Lying")), FName(TEXT("InAir")), FName(TEXT("Rolling"))})
	{
		if (!Asset->MyProfiles.Contains(Name)) Asset->MyProfiles.Add(Name, Animated);
	}
	for (const FName Name : {FName(TEXT("FreeFalling")), FName(TEXT("Unconsious")), FName(TEXT("Dying"))})
	{
		if (!Asset->MyProfiles.Contains(Name)) Asset->MyProfiles.Add(Name, Ragdoll);
	}
	Asset->Compile();
	Asset->MarkPackageDirty();
	return true;
}

bool UGarPhysicsControlMigrationLibrary::EnsureFreeConstraintProfile(UPhysicsAsset* Asset)
{
	if (!IsValid(Asset)) return false;
	Asset->Modify();
	Asset->ConstraintProfiles.AddUnique(TEXT("Free"));
	for (UPhysicsConstraintTemplate* Joint : Asset->ConstraintSetup)
	{
		if (!Joint || Joint->ContainsConstraintProfile(TEXT("Free"))) continue;
		Joint->Modify();
		FPhysicsConstraintProfileHandle& Handle = Joint->ProfileHandles.AddDefaulted_GetRef();
		Handle.ProfileName = TEXT("Free");
		Handle.ProfileProperties = Joint->GetConstraintProfilePropertiesOrDefault(NAME_None);
		Handle.ProfileProperties.ConeLimit.Swing1Motion = ACM_Free;
		Handle.ProfileProperties.ConeLimit.Swing2Motion = ACM_Free;
		Handle.ProfileProperties.TwistLimit.TwistMotion = ACM_Free;
	}
	Asset->MarkPackageDirty();
	return true;
}
