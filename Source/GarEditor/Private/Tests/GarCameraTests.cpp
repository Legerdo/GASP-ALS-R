// Copyright (c) SAM-tak. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameplayCameraComponentBase.h"
#include "GameFramework/GameplayCamerasPlayerCameraManager.h"
#include "Core/CameraEvaluationContext.h"
#include "Core/CameraSystemEvaluator.h"
#include "Core/RootCameraNode.h"
#include "GarCharacter.h"
#include "GarGameplayCameraStateComponent.h"
#include "Utility/GarUtility.h"

class FGarManagedCameraPIECommand : public IAutomationLatentCommand
{
public:

	explicit FGarManagedCameraPIECommand(FAutomationTestBase* InTest) : Test(InTest) {}

	virtual bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 120.0)
		{
			Test->AddError(TEXT("Timed out waiting for the GAR camera lifecycle test"));
			return true;
		}
		UWorld* World = GEditor->PlayWorld;
		if (!World || !World->HasBegunPlay()) return false;
		if (Stage == 0)
		{
			Controller = World->GetFirstPlayerController();
			if (!Controller.IsValid() || !Controller->GetPawn()) return false;
			Character = Cast<AGarCharacter>(Controller->GetPawn());
			Manager = Cast<AGameplayCamerasPlayerCameraManager>(Controller->PlayerCameraManager);
			if (!Test->TestNotNull(TEXT("GAR example pawn"), Character.Get())
				|| !Test->TestNotNull(TEXT("Gameplay camera manager"), Manager.Get())) return true;
			Camera = Character->FindComponentByClass<UGameplayCameraComponentBase>();
			State = Character->FindComponentByClass<UGarGameplayCameraStateComponent>();
			if (!Test->TestNotNull(TEXT("Gameplay camera component"), Camera.Get())
				|| !Test->TestNotNull(TEXT("Camera state component"), State.Get())) return true;
			AHUD* HUD = Controller->GetHUD();
			if (!Test->TestNotNull(TEXT("Example HUD"), HUD)) return true;
			// Do not call ShowDebug: it saves the user's config. Only modify this PIE HUD.
			HUD->DebugDisplay.AddUnique(TEXT("GAR.Shapes"));
			HUD->bShowDebugInfo = true;
			CheckManagedContext();
			Advance(World);
			return false;
		}
		if (!Character.IsValid() || !Camera.IsValid() || !State.IsValid() || !Manager.IsValid())
		{
			Test->AddError(TEXT("Camera test actors were unexpectedly destroyed"));
			return true;
		}

		// Do not repair ViewTarget or cycle the HUD target. Observe the raw camera target
		// across startup, old-context cleanup, perspective changes, and re-possession.
		bTargetsStayedOnPawn &= Manager->ViewTarget.Target == Character.Get()
			&& Controller->GetHUD()->GetCurrentDebugTargetActor() == Character.Get()
			&& UGarUtility::ShouldDisplayDebugForActor(Character.Get(), TEXT("GAR.Shapes"));
		if (World->GetTimeSeconds() - StageStarted < 1.0f) return false;

		if (Stage == 1)
		{
			Test->TestTrue(TEXT("Initial GAR debug target stays on the pawn without PageUp/PageDown"), bTargetsStayedOnPawn);
			const auto Context = Camera->GetEvaluationContext();
			Character->OnPossessed_Client.Broadcast(Controller.Get());
			Test->TestTrue(TEXT("Repeated possession notification preserves the active context"), Camera->GetEvaluationContext() == Context);
			State->SetDesiredPerspective(GarCameraPerspectiveTags::FirstPerson);
			Advance(World);
		}
		else if (Stage == 2)
		{
			Test->TestTrue(TEXT("First-person camera variable is read from the manager"), State->GetFirstPersonFactor() > 0.95f);
			const auto& Pose = Manager->GetCameraSystemEvaluator()->GetEvaluatedResult().CameraPose;
			Test->TestTrue(TEXT("Camera state reads evaluated FOV"),
				FMath::IsNearlyEqual(State->GetTanHalfVfov(), FMath::Tan(FMath::DegreesToRadians(Pose.GetEffectiveFieldOfView()) * 0.5f), 0.001f));
			State->SetDesiredPerspective(GarCameraPerspectiveTags::ThirdPerson);
			Advance(World);
		}
		else if (Stage == 3)
		{
			Test->TestTrue(TEXT("Third-person camera variable returns to zero"), State->GetFirstPersonFactor() < 0.05f);
			const auto OldContext = Camera->GetEvaluationContext();
			Controller->UnPossess();
			CheckReleasedContext(OldContext);
			Test->TestFalse(TEXT("Unpossess stops the component"), Camera->IsActive());
			Test->TestFalse(TEXT("Unpossess clears the component context"), Camera->GetEvaluationContext().IsValid());
			Controller->Possess(Character.Get());
			CheckManagedContext();
			Test->TestTrue(TEXT("Re-possession creates a fresh context"), Camera->GetEvaluationContext() != OldContext);
			Advance(World);
		}
		else if (Stage == 4)
		{
			Test->TestTrue(TEXT("ViewTarget and GAR debug target stay correct after re-possession"), bTargetsStayedOnPawn);
			const auto Context = Camera->GetEvaluationContext();
			// EndPlay must also release the context without needing an UnPossessed notification.
			State->DestroyComponent();
			CheckReleasedContext(Context);
			Test->TestFalse(TEXT("Camera-state EndPlay stops the camera"), Camera->IsActive());
			Test->TestFalse(TEXT("Camera-state EndPlay clears the context"), Camera->GetEvaluationContext().IsValid());
			return true;
		}
		return false;
	}

private:

	void CheckManagedContext()
	{
		Test->TestFalse(TEXT("Runtime standalone evaluator is disabled"), Camera->bRunStandaloneCameraSystem);
		Test->TestFalse(TEXT("Component does not run a second evaluator"), Camera->GetCameraSystemEvaluator().IsValid());
		const auto Context = Camera->GetEvaluationContext();
		if (Test->TestTrue(TEXT("Camera context exists"), Context.IsValid()))
		{
			Test->TestTrue(TEXT("Context is active"), Context->IsActive());
			Test->TestTrue(TEXT("Context is owned by the component"), Context->GetOwner() == Camera.Get());
			Test->TestTrue(TEXT("Manager contains the component's context"),
				Manager->GetCameraSystemEvaluator()->GetEvaluationContextStack().FindContextByPredicate(
					[&Context](const auto& Item) { return Item == Context; }).IsValid());
		}
	}

	void CheckReleasedContext(const TSharedPtr<UE::Cameras::FCameraEvaluationContext>& Context)
	{
		if (!Test->TestTrue(TEXT("Released context was valid"), Context.IsValid())) return;
		const auto Evaluator = Manager->GetCameraSystemEvaluator();
		Test->TestFalse(TEXT("Released context is inactive"), Context->IsActive());
		Test->TestFalse(TEXT("Released context is removed from the manager stack"),
			Evaluator->GetEvaluationContextStack().FindContextByPredicate(
				[&Context](const auto& Item) { return Item == Context; }).IsValid());
		Test->TestFalse(TEXT("No rigs retain the released context"), Evaluator->GetRootNodeEvaluator()->HasAnyRunningCameraRig(Context));
	}

	void Advance(UWorld* World) { ++Stage; StageStarted = World->GetTimeSeconds(); }
	FAutomationTestBase* Test;
	const double Started = FPlatformTime::Seconds();
	int32 Stage = 0;
	float StageStarted = 0;
	bool bTargetsStayedOnPawn = true;
	TWeakObjectPtr<APlayerController> Controller;
	TWeakObjectPtr<AGarCharacter> Character;
	TWeakObjectPtr<AGameplayCamerasPlayerCameraManager> Manager;
	TWeakObjectPtr<UGameplayCameraComponentBase> Camera;
	TWeakObjectPtr<UGarGameplayCameraStateComponent> State;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarManagedCameraLifecycleTest, "GAR.Camera.ManagedLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGarManagedCameraLifecycleTest::RunTest(const FString& Parameters)
{
	UClass* GameMode = LoadClass<AGameModeBase>(nullptr, TEXT("/GAR/Example/PlayerCharacter/B_GarExtra_GameMode.B_GarExtra_GameMode_C"));
	if (!TestNotNull(TEXT("Load example game mode"), GameMode)) return false;
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	World->GetWorldSettings()->DefaultGameMode = GameMode;
	AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>(FVector(0, 0, -2), FRotator::ZeroRotator);
	Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Floor->SetActorScale3D(FVector(100, 100, 0.04));
	Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
	World->SpawnActor<APlayerStart>(FVector(0, 0, 100), FRotator::ZeroRotator);
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FGarManagedCameraPIECommand>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
