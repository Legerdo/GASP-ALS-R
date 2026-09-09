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
#include "GarCharacter.h"
#include "GarConstants.h"
#include "GarPhysicsControlComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GarPhysicsControlMigrationLibrary)

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
