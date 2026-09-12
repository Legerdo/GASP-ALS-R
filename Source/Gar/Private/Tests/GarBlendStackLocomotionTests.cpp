// Copyright (c) SAM-tak. All Rights Reserved.
// Kept in GAR with the implementation so Live Coding does not need new cross-DLL imports.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "GarGameplayTags.h"
#include "State/GarBlendStackLocomotionState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarBlendStackStateHistoryTest, "GAR.Animation.BlendStack.StateHistory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGarBlendStackStateHistoryTest::RunTest(const FString& Parameters)
{
	FGarBlendStackLocomotionState State;
	const FGameplayTagContainer Grounded(GarLocomotionModeTags::Grounded);
	const FGameplayTagContainer InAir(GarLocomotionModeTags::InAir);
	auto Update = [&](const FGameplayTagContainer& Mode, float DeltaTime)
	{
		FGarBlendStackLocomotionState::UpdateHistory(Mode, State.MovementMode, State.MovementMode_LastFrame,
			State.MovementMode_Recent, State.MovementModeTime, DeltaTime, 0.2f);
	};
	Update(InAir, 0.016f);
	TestTrue(TEXT("Current mode changes immediately"), State.MovementMode == InAir);
	TestTrue(TEXT("Previous frame still grounded"), State.MovementMode_LastFrame == Grounded);
	TestTrue(TEXT("Recent retains stable ground state"), State.MovementMode_Recent == Grounded);
	TestEqual(TEXT("Change frame resets timer"), State.MovementModeTime, 0.0f);
	Update(InAir, 0.1f);
	TestTrue(TEXT("Previous frame and recent are different histories"), State.MovementMode_LastFrame == InAir && State.MovementMode_Recent == Grounded);
	Update(InAir, 0.1f);
	TestTrue(TEXT("Stable in-air mode becomes recent"), State.MovementMode_Recent == InAir);
	Update(Grounded, 0.016f);
	TestTrue(TEXT("Landing preserves in-air history for chooser"), State.MovementMode_Recent == InAir);
	Update(Grounded, 0.05f);
	Update(InAir, 0.016f);
	TestTrue(TEXT("Transient landing must not replace stable history"), State.MovementMode_Recent == InAir);
	Update(InAir, -1.0f);
	TestEqual(TEXT("Negative delta cannot make time negative"), State.MovementModeTime, 0.0f);
	return true;
}

#endif
