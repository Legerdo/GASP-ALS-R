// Copyright (c) SAM-tak. All Rights Reserved.

#include "GarPhysicsControlMigrationLibrary.h"

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Variable.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraphUtilities.h"
#include "GarCharacter.h"
#include "GarConstants.h"
#include "GarPhysicsControlComponent.h"
#include "Abilities/Actions/GarGameplayAbility_Ragdolling.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GarPhysicsControlMigrationLibrary)

bool UGarPhysicsControlMigrationLibrary::MigrateRagdollVelocity(UBlueprint* Blueprint)
{
	if (!IsValid(Blueprint) || !Blueprint->ParentClass->IsChildOf(UGarGameplayAbility_Ragdolling::StaticClass())) return false;
	Blueprint->Modify();
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
			if (!Call || Call->FunctionReference.GetMemberName() != TEXT("GetVelocity")) continue;
			Call->Modify();
			if (UEdGraphPin* Self = Call->FindPin(TEXT("self"))) Self->BreakAllPinLinks();
			Call->FunctionReference.SetSelfMember(GET_FUNCTION_NAME_CHECKED(UGarGameplayAbility_Ragdolling, GetRagdollVelocity));
			Call->ReconstructNode();
		}
	}
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	return Blueprint->Status != BS_Error;
}

bool UGarPhysicsControlMigrationLibrary::ConfigureBaselineProfiles(UPhysicsControlAsset* Asset)
{
	if (!IsValid(Asset)) return false;
	Asset->Modify();

	// Full All-set resets make these profiles independent of the previously applied profile,
	// curve multipliers and optional freeze. Strengths use Physics Control units, not PA springs.
	FPhysicsControlControlAndModifierUpdates PhysicalAnimation;
	FPhysicsControlSparseData Disabled;
	Disabled.bEnabled = false;
	PhysicalAnimation.ControlUpdates.Emplace(TEXT("All"), Disabled);
	PhysicalAnimation.ControlMultiplierUpdates.Emplace(TEXT("All"), FPhysicsControlSparseMultiplier());
	FPhysicsControlModifierSparseData AnimatedBodies;
	AnimatedBodies.MovementType = EPhysicsMovementType::Simulated;
	AnimatedBodies.CollisionType = ECollisionEnabled::QueryAndPhysics;
	AnimatedBodies.GravityMultiplier = 0.0f;
	AnimatedBodies.PhysicsBlendWeight = 1.0f;
	PhysicalAnimation.ModifierUpdates.Emplace(TEXT("All"), AnimatedBodies);

	FPhysicsControlSparseData World;
	World.LinearStrength = 3.0f;
	World.AngularStrength = 3.0f;
	PhysicalAnimation.ControlUpdates.Emplace(TEXT("WorldSpace"), World);
	FPhysicsControlSparseData Parent;
	Parent.AngularStrength = 15.0f;
	Parent.AngularDampingRatio = 3.0f;
	PhysicalAnimation.ControlUpdates.Emplace(TEXT("ParentSpace"), Parent);
	FPhysicsControlSparseData Feet = World;
	Feet.LinearStrength = Feet.AngularStrength = 10.0f;
	PhysicalAnimation.ControlUpdates.Emplace(TEXT("WorldSpace_Feet"), Feet);
	Asset->MyProfiles.Add(TEXT("PhysicalAnimation"), PhysicalAnimation);

	FPhysicsControlControlAndModifierUpdates Ragdoll;
	Ragdoll.ControlUpdates.Emplace(TEXT("All"), Disabled);
	Ragdoll.ControlMultiplierUpdates.Emplace(TEXT("All"), FPhysicsControlSparseMultiplier());
	FPhysicsControlModifierSparseData SimulatedBodies;
	SimulatedBodies.MovementType = EPhysicsMovementType::Simulated;
	SimulatedBodies.CollisionType = ECollisionEnabled::QueryAndPhysics;
	SimulatedBodies.GravityMultiplier = 1.0f;
	SimulatedBodies.PhysicsBlendWeight = 1.0f;
	Ragdoll.ModifierUpdates.Emplace(TEXT("All"), SimulatedBodies);
	// Sample-style powered ragdoll: joint motors follow the ragdoll animation but no
	// world-space control holds the pelvis up. Set this strength to zero for a limp ragdoll.
	Parent.AngularStrength = 10.0f;
	Parent.LinearDampingRatio = 0.0f;
	Ragdoll.ControlUpdates.Emplace(TEXT("ParentSpace"), Parent);
	Asset->MyProfiles.Add(TEXT("Ragdoll"), Ragdoll);
	Asset->Compile();
	Asset->MarkPackageDirty();
	return true;
}

FString UGarPhysicsControlMigrationLibrary::ExportBlueprintGraphs(UBlueprint* Blueprint)
{
	FString Result;
	if (IsValid(Blueprint))
	{
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			TSet<UObject*> Nodes;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				Nodes.Add(Node);
			}
			FString GraphText;
			FEdGraphUtilities::ExportNodesToText(Nodes, GraphText);
			Result += FString::Printf(TEXT("\nGRAPH %s\n"), *Graph->GetPathName()) + GraphText;
		}
	}
	return Result;
}

bool UGarPhysicsControlMigrationLibrary::MigrateLegacyPhysicalAnimationReferences(UBlueprint* Blueprint)
{
	if (!IsValid(Blueprint))
	{
		return false;
	}

	Blueprint->Modify();
	const FName LegacyPhysicalAnimationName(TEXT("PhysicalAnimation"));
	const FName PhysicsControlName(TEXT("PhysicsControl"));
	FBlueprintEditorUtils::ReplaceVariableReferences(Blueprint, LegacyPhysicalAnimationName, PhysicsControlName);

	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	const FName PredicateName = GET_FUNCTION_NAME_CHECKED(UGarPhysicsControlComponent, IsRagdollingAndGroundedAndAged);
	const FName LegacySimulationName(TEXT("IsBoneUnderSimulation"));
	const FName SimulationName = GET_FUNCTION_NAME_CHECKED(UGarPhysicsControlComponent, IsBoneSimulatingPhysics);
	const FName LegacyDebugDisplayName(TEXT("PADebugDisplayName"));
	const FName PhysicsControlDebugDisplayName = GET_FUNCTION_NAME_CHECKED(UGarConstants, PhysicsControlDebugDisplayName);
	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph)
		{
			continue;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node);
				VariableNode && VariableNode->GetVarName() == LegacyPhysicalAnimationName)
			{
				VariableNode->Modify();
				for (UEdGraphPin* Pin : VariableNode->Pins)
				{
					if (Pin && Pin->PinName == LegacyPhysicalAnimationName)
					{
						Pin->Modify();
						Pin->PinName = PhysicsControlName;
					}
				}

				VariableNode->VariableReference.SetExternalMember(PhysicsControlName, AGarCharacter::StaticClass());
				VariableNode->ReconstructNode();
				continue;
			}

			UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Node);
			if (!CallFunctionNode)
			{
				continue;
			}

			const FName CurrentFunctionName = CallFunctionNode->FunctionReference.GetMemberName();
			FName ReplacementFunctionName;
			UClass* ReplacementClass = nullptr;
			if (CurrentFunctionName == LegacySimulationName)
			{
				ReplacementFunctionName = SimulationName;
				ReplacementClass = UGarPhysicsControlComponent::StaticClass();
			}
			else if (CurrentFunctionName == PredicateName)
			{
				ReplacementFunctionName = PredicateName;
				ReplacementClass = UGarPhysicsControlComponent::StaticClass();
			}
			else if (CurrentFunctionName == LegacyDebugDisplayName)
			{
				ReplacementFunctionName = PhysicsControlDebugDisplayName;
				ReplacementClass = UGarConstants::StaticClass();
			}
			else
			{
				continue;
			}

			CallFunctionNode->Modify();
			CallFunctionNode->FunctionReference.SetExternalMember(ReplacementFunctionName, ReplacementClass);
			CallFunctionNode->ReconstructNode();
		}
	}

	FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	return Blueprint->Status != BS_Error;
}

bool UGarPhysicsControlMigrationLibrary::MigrateGettingUpAbility(UBlueprint* AbilityBlueprint)
{
	return MigrateLegacyPhysicalAnimationReferences(AbilityBlueprint);
}
