// Copyright (c) SAM-tak. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Chooser.h"
#include "OutputStructColumn.h"
#include "GarGameplayTags.h"
#include "GarPhysicsControlComponent.h"
#include "GarPhysicsControlMigrationLibrary.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarPhysicsProfileChooserTest, "GAR.PhysicsControl.ProfileChooser",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGarPhysicsProfileChooserTest::RunTest(const FString& Parameters)
{
	UChooserTable* Table = NewObject<UChooserTable>();
	TestTrue(TEXT("Create typed profile Chooser"), UGarPhysicsControlMigrationLibrary::ConfigureProfileChooser(Table));
	UGarPhysicsControlComponent* Physics = NewObject<UGarPhysicsControlComponent>();
	Physics->ProfileChooser = Table;
	FGarPhysicsControlProfileChooserResult Result;
	const auto Check = [&](const FString& Label, const FGameplayTagContainer& Tags, const FGameplayTag ActiveRagdoll,
		const FName ExpectedControl, const FName ExpectedConstraint)
	{
		TestTrue(Label + TEXT(" resolves"), Physics->EvaluateProfile(Tags, ActiveRagdoll, Result));
		TestEqual(Label + TEXT(" Control Profile"), Result.ControlProfileName, ExpectedControl);
		TestEqual(Label + TEXT(" Constraint Profile"), Result.ConstraintProfileName, ExpectedConstraint);
	};
	const FGameplayTag Normal;
	Check(TEXT("Default fallback"), {}, Normal, TEXT("Default"), TEXT("Free"));
	Check(TEXT("Traversal"), FGameplayTagContainer(GarLocomotionActionTags::Traversal), Normal, TEXT("Traversal"), TEXT("Free"));
	Check(TEXT("InAir"), FGameplayTagContainer(GarLocomotionModeTags::InAir), Normal, TEXT("InAir"), TEXT("Free"));
	Check(TEXT("LyingFront inherits Lying"), FGameplayTagContainer(GarStanceTags::LyingFront), Normal, TEXT("Lying"), TEXT("Free"));
	Check(TEXT("LyingBack inherits Lying"), FGameplayTagContainer(GarStanceTags::LyingBack), Normal, TEXT("Lying"), TEXT("Free"));
	Check(TEXT("Rolling alone"), FGameplayTagContainer(GarLocomotionActionTags::Rolling), Normal, TEXT("Rolling"), TEXT("Free"));
	Check(TEXT("Sliding alone"), FGameplayTagContainer(GarLocomotionActionTags::Sliding), Normal, TEXT("Rolling"), TEXT("Free"));
	for (const FGameplayTag Tag : {GarLocomotionActionTags::FreeFalling.GetTag(), GarLocomotionActionTags::Unconsious.GetTag(), GarLocomotionActionTags::Dying.GetTag()})
	{
		const FName Name(*Tag.GetTagName().ToString().RightChop(FString(TEXT("Gar.LocomotionAction.")).Len()));
		Check(Name.ToString() + TEXT(" entry before ability tag"), {}, Tag, Name, NAME_None);
		Check(Name.ToString() + TEXT(" stale tag after exit"), FGameplayTagContainer(Tag), Normal, TEXT("Default"), TEXT("Free"));
	}
	FGameplayTagContainer Mixed(GarLocomotionModeTags::InAir);
	Mixed.AddTag(GarLocomotionActionTags::Traversal);
	Mixed.AddTag(GarLocomotionActionTags::Dying);
	Check(TEXT("Task selects ragdoll despite other action tags"), Mixed, GarLocomotionActionTags::FreeFalling, TEXT("FreeFalling"), NAME_None);
	Check(TEXT("Traversal takes priority over InAir after exit"), Mixed, Normal, TEXT("Traversal"), TEXT("Free"));

	// Change only the constraint output. The pair must be read afresh, independently of control name.
	Table->ColumnsStructs[1].GetMutable<FOutputStructColumn>().FallbackValue.GetMutable<FGarPhysicsControlProfileChooserResult>().ConstraintProfileName = TEXT("Lying");
	Check(TEXT("Independent constraint output"), {}, Normal, TEXT("Default"), TEXT("Lying"));
	Physics->ProfileChooser = nullptr;
	TestFalse(TEXT("Missing Chooser fails"), Physics->EvaluateProfile({}, Normal, Result));
	TestTrue(TEXT("Failure cannot reuse stale output"), Result.ControlProfileName.IsNone() && Result.ConstraintProfileName.IsNone());
	return true;
}

#endif
