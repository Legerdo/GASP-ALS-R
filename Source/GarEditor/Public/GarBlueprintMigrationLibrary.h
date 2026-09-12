// Copyright (c) SAM-tak. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "GarBlueprintMigrationLibrary.generated.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UChooserTable;
class UPoseSearchDatabase;

/** Editor-only, transactional primitives for migrating existing Blueprint graphs. Never saves assets. */
UCLASS()
class GAREDITOR_API UGarBlueprintMigrationLibrary : public UBlueprintFunctionLibrary
{

	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "GAR|Migration")
	static TArray<UEdGraphNode*> GetGraphNodes(UEdGraph* Graph);

	/** Nodes must belong to one graph. Mixed graphs are rejected without invoking the engine exporter. */
	UFUNCTION(BlueprintCallable, Category = "GAR|Migration")
	static FString ExportNodes(const TArray<UEdGraphNode*>& Nodes);

	UFUNCTION(BlueprintCallable, Category = "GAR|Migration")
	static TArray<UEdGraphNode*> ImportNodes(UEdGraph* Graph, const FString& Text);

	UFUNCTION(BlueprintCallable, Category = "GAR|Migration")
	static bool RemoveNode(UBlueprint* Blueprint, UEdGraphNode* Node);

	UFUNCTION(BlueprintCallable, Category = "GAR|Migration")
	static UEdGraph* CopyFunctionGraph(UBlueprint* Source, UBlueprint* Target, FName FunctionName);

	UFUNCTION(BlueprintCallable, Category = "GAR|Migration")
	static bool CopyMemberVariable(UBlueprint* Source, UBlueprint* Target, FName VariableName);

	UFUNCTION(BlueprintCallable, Category = "GAR|Migration")
	static FString ExportProperty(UObject* Object, FName PropertyName);

	UFUNCTION(BlueprintCallable, Category = "GAR|Migration")
	static bool ImportProperty(UObject* Object, FName PropertyName, const FString& Text);

	UFUNCTION(BlueprintCallable, Category = "GAR|Migration")
	static void ReconstructNode(UEdGraphNode* Node);

	UFUNCTION(BlueprintCallable, Category = "GAR|Migration")
	static bool SplitPin(UEdGraphNode* Node, FName PinName, bool bInput);

	/** Connects graph pins, including state-machine nodes not supported by the K2-only Python pin wrapper. */
	UFUNCTION(BlueprintCallable, Category = "GAR|Migration")
	static bool ConnectGraphPins(UEdGraphNode* Source, FName SourcePin, UEdGraphNode* Target, FName TargetPin);

	UFUNCTION(BlueprintCallable, Category = "GAR|Migration")
	static TArray<UObject*> GetOwnedObjects(UObject* Object);

	/** Converts enum filters without changing equal/not-equal/wildcard or multi-value semantics. */
	UFUNCTION(BlueprintCallable, Category = "GAR|Migration")
	static int32 ConvertChooserEnumColumns(UChooserTable* Table, const TMap<FString, FString>& EnumValueTags);

	UFUNCTION(BlueprintCallable, Category = "GAR|Migration")
	static void CompileChooser(UChooserTable* Table);

	UFUNCTION(BlueprintCallable, Category = "GAR|Migration")
	static TArray<FString> ValidateChooser(UChooserTable* Table);

	/** Non-blocking: 0 = indexing, 1 = ready, 2 = failed. Start once, then poll. */
	UFUNCTION(BlueprintCallable, Category = "GAR|Migration")
	static int32 BuildPoseSearchDatabase(UPoseSearchDatabase* Database, bool bNewRequest);

	UFUNCTION(BlueprintCallable, Category = "GAR|Migration")
	static void ReplaceObjectReferences(UObject* Object, const TMap<UObject*, UObject*>& Replacements);

	/** Rebuild skeleton and compile; returns a JSON summary including all compiler messages. */
	UFUNCTION(BlueprintCallable, Category = "GAR|Migration")
	static FString CompileBlueprint(UBlueprint* Blueprint);

	/** One-off, unsaved migration of GAR's two locomotion structs. Preserves Chooser row storage in place. */
	UFUNCTION(BlueprintCallable, Category = "GAR|Migration")
	static FString MigrateLocomotionStructs(UBlueprint* Blueprint, UChooserTable* Table);
};
