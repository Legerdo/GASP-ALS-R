// Copyright (c) SAM-tak. All Rights Reserved.

#include "GarBlueprintMigrationLibrary.h"

#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "Chooser.h"
#include "EnumColumn.h"
#include "MultiEnumColumn.h"
#include "GameplayTagColumn.h"
#include "GameplayTagQueryColumn.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "EdGraphUtilities.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GarBlueprintMigrationLibrary)

TArray<UEdGraphNode*> UGarBlueprintMigrationLibrary::GetGraphNodes(UEdGraph* Graph)
{
	TArray<UEdGraphNode*> Result;
	if (Graph)
	{
		for (UEdGraphNode* Node : Graph->Nodes) Result.Add(Node);
	}
	return Result;
}

FString UGarBlueprintMigrationLibrary::ExportNodes(const TArray<UEdGraphNode*>& Nodes)
{
	TSet<UObject*> Objects;
	const UEdGraph* Graph = nullptr;
	for (UEdGraphNode* Node : Nodes)
	{
		if (!Node) continue;
		if (Graph && Graph != Node->GetGraph())
		{
			// ExportNodesToText asserts on mixed outers. Reject before touching any node.
			UE_LOG(LogTemp, Warning, TEXT("GAR ExportNodes requires nodes from a single graph."));
			return FString();
		}
		Graph = Node->GetGraph();
		Objects.Add(Node);
	}
	FString Text;
	if (!Objects.IsEmpty()) FEdGraphUtilities::ExportNodesToText(Objects, Text);
	return Text;
}

TArray<UEdGraphNode*> UGarBlueprintMigrationLibrary::ImportNodes(UEdGraph* Graph, const FString& Text)
{
	TArray<UEdGraphNode*> Result;
	if (Graph && FEdGraphUtilities::CanImportNodesFromText(Graph, Text))
	{
		Graph->Modify();
		TSet<UEdGraphNode*> Imported;
		FEdGraphUtilities::ImportNodesFromText(Graph, Text, Imported);
		for (UEdGraphNode* Node : Imported) Result.Add(Node);
	}
	return Result;
}

bool UGarBlueprintMigrationLibrary::RemoveNode(UBlueprint* Blueprint, UEdGraphNode* Node)
{
	if (!Blueprint || !Node || !Node->IsIn(Blueprint)) return false;
	FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
	return true;
}

bool UGarBlueprintMigrationLibrary::ConnectGraphPins(UEdGraphNode* Source, FName SourcePin, UEdGraphNode* Target, FName TargetPin)
{
	if (!Source || !Target || Source->GetGraph() != Target->GetGraph()) return false;
	UEdGraphPin* From = Source->FindPin(SourcePin, EGPD_Output);
	UEdGraphPin* To = Target->FindPin(TargetPin, EGPD_Input);
	if (!From || !To || !Source->GetSchema()) return false;
	Source->Modify();
	Target->Modify();
	return Source->GetSchema()->TryCreateConnection(From, To);
}

UEdGraph* UGarBlueprintMigrationLibrary::CopyFunctionGraph(UBlueprint* Source, UBlueprint* Target, FName FunctionName)
{
	if (!Source || !Target || Source == Target) return nullptr;
	for (const UEdGraph* Graph : Target->FunctionGraphs)
	{
		if (Graph->GetFName() == FunctionName) return nullptr;
	}
	for (UEdGraph* Graph : Source->FunctionGraphs)
	{
		if (Graph->GetFName() != FunctionName) continue;
		Target->Modify();
		UEdGraph* Copy = FEdGraphUtilities::CloneGraph(Graph, Target);
		if (!Copy) return nullptr;
		Copy->Rename(*FunctionName.ToString(), Target, REN_DontCreateRedirectors);
		Copy->GraphGuid = FGuid::NewGuid();
		Target->FunctionGraphs.Add(Copy);
		FBlueprintEditorUtils::MarkBlueprintAsModified(Target);
		return Copy;
	}
	return nullptr;
}

bool UGarBlueprintMigrationLibrary::CopyMemberVariable(UBlueprint* Source, UBlueprint* Target, FName VariableName)
{
	if (!Source || !Target || Source == Target) return false;
	if (FBlueprintEditorUtils::FindNewVariableIndex(Target, VariableName) != INDEX_NONE) return false;
	if (Target->ParentClass && FindFProperty<FProperty>(Target->ParentClass, VariableName)) return false;
	const int32 Index = FBlueprintEditorUtils::FindNewVariableIndex(Source, VariableName);
	if (Index == INDEX_NONE) return false;
	Target->Modify();
	FBPVariableDescription Copy = Source->NewVariables[Index];
	Copy.VarGuid = FGuid::NewGuid();
	Target->NewVariables.Add(Copy);
	FBlueprintEditorUtils::MarkBlueprintAsModified(Target);
	return true;
}

FString UGarBlueprintMigrationLibrary::ExportProperty(UObject* Object, FName PropertyName)
{
	FString Text;
	if (Object)
	{
		if (const FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), PropertyName))
		{
			Property->ExportText_InContainer(0, Text, Object, Object, Object, PPF_None);
		}
	}
	return Text;
}

bool UGarBlueprintMigrationLibrary::ImportProperty(UObject* Object, FName PropertyName, const FString& Text)
{
	if (!Object) return false;
	FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), PropertyName);
	if (!Property) return false;
	Object->Modify();
	return Property->ImportText_InContainer(*Text, Object, Object, PPF_None) != nullptr;
}

void UGarBlueprintMigrationLibrary::ReconstructNode(UEdGraphNode* Node)
{
	if (Node)
	{
		Node->Modify();
		Node->ReconstructNode();
	}
}

bool UGarBlueprintMigrationLibrary::SplitPin(UEdGraphNode* Node, FName PinName, bool bInput)
{
	UEdGraphPin* Pin = Node ? Node->FindPin(PinName, bInput ? EGPD_Input : EGPD_Output) : nullptr;
	if (!Pin) return false;
	if (Pin->SubPins.IsEmpty()) GetDefault<UEdGraphSchema_K2>()->SplitPin(Pin);
	return !Pin->SubPins.IsEmpty();
}

TArray<UObject*> UGarBlueprintMigrationLibrary::GetOwnedObjects(UObject* Object)
{
	TArray<UObject*> Result;
	if (Object) GetObjectsWithOuter(Object, Result, EGetObjectsFlags::IncludeNestedObjects);
	return Result;
}

int32 UGarBlueprintMigrationLibrary::ConvertChooserEnumColumns(UChooserTable* Table, const TMap<FString, FString>& EnumValueTags)
{
	if (!Table) return -1;
	// FPoseSearchColumn's copy constructor deliberately clears its database links.
	// Stage only the enum columns; never copy the untouched pose-match columns.
	TMap<int32, FInstancedStruct> Converted;
	int32 Count = 0;
	for (int32 ColumnIndex = 0; ColumnIndex < Table->ColumnsStructs.Num(); ++ColumnIndex)
	{
		const FInstancedStruct& Column = Table->ColumnsStructs[ColumnIndex];
		const FEnumColumn* EnumColumn = Column.GetPtr<FEnumColumn>();
		const FMultiEnumColumn* MultiColumn = Column.GetPtr<FMultiEnumColumn>();
		if (!EnumColumn && !MultiColumn) continue;
		const FInstancedStruct& Input = EnumColumn ? EnumColumn->InputValue : MultiColumn->InputValue;
		const FEnumContextProperty* SourceBinding = Input.GetPtr<FEnumContextProperty>();
		if (!SourceBinding || !SourceBinding->Binding.Enum) return -1;
		FGameplayTagQueryColumn Target;
		Target.bDisabled = EnumColumn ? EnumColumn->bDisabled : MultiColumn->bDisabled;
		Target.InputValue.InitializeAs<FGameplayTagContextProperty>();
		auto& Binding = Target.InputValue.GetMutable<FGameplayTagContextProperty>().Binding;
		Binding.PropertyBindingChain = SourceBinding->Binding.PropertyBindingChain;
		Binding.PropertyBindingChain.Insert(TEXT("BlendStackLocomotion"), 0);
		Binding.ContextIndex = SourceBinding->Binding.ContextIndex;
		auto AddTag = [&](FGameplayTagQueryExpression& Expression, uint8 Value) -> bool
		{
			const UEnum* Enum = SourceBinding->Binding.Enum;
			const FString Key = Enum->GetName() + TEXT(".") + Enum->GetNameStringByValue(Value);
			const FString* TagName = EnumValueTags.Find(Key);
			const FGameplayTag Tag = TagName ? FGameplayTag::RequestGameplayTag(FName(*TagName), false) : FGameplayTag();
			if (!Tag.IsValid())
			{
				UE_LOG(LogTemp, Error, TEXT("GAR chooser conversion missing tag for %s"), *Key);
				return false;
			}
			Expression.AddTag(Tag);
			return true;
		};
		if (EnumColumn)
		{
			for (const FChooserEnumRowData& Row : EnumColumn->RowValues)
			{
				FGameplayTagQueryExpression Expression;
				if (Row.Comparison == EEnumColumnCellValueComparison::MatchAny) Expression.AllTagsMatch();
				else
				{
					if (Row.Comparison == EEnumColumnCellValueComparison::MatchNotEqual) Expression.NoTagsMatch();
					else Expression.AnyTagsMatch();
					if (!AddTag(Expression, Row.Value)) return -1;
				}
				Target.RowValues.Add(FGameplayTagQuery::BuildQuery(Expression));
			}
		}
		else
		{
			for (const FChooserMultiEnumRowData& Row : MultiColumn->RowValues)
			{
				FGameplayTagQueryExpression Expression;
				if (Row.Value == 0) Expression.AllTagsMatch();
				else
				{
					Expression.AnyTagsMatch();
					for (uint8 Bit = 0; Bit < 32; ++Bit)
						if ((Row.Value & (1u << Bit)) && !AddTag(Expression, Bit)) return -1;
				}
				Target.RowValues.Add(FGameplayTagQuery::BuildQuery(Expression));
			}
		}
		Target.DefaultRowValue = FGameplayTagQuery::BuildQuery(FGameplayTagQueryExpression().AllTagsMatch());
		Converted.Add(ColumnIndex).InitializeAs<FGameplayTagQueryColumn>(Target);
		++Count;
	}
	Table->Modify();
	for (auto& Pair : Converted) Table->ColumnsStructs[Pair.Key] = MoveTemp(Pair.Value);
	return Count;
}

void UGarBlueprintMigrationLibrary::CompileChooser(UChooserTable* Table)
{
	if (!Table) return;
	TArray<UObject*> Objects = GetOwnedObjects(Table);
	Objects.Add(Table);
	for (UObject* Object : Objects)
	{
		if (UChooserTable* Child = Cast<UChooserTable>(Object))
		{
			for (FInstancedStruct& Column : Child->ColumnsStructs)
			{
				// Rebuild the non-serialized row-to-database mapping after scripted imports.
				if (FChooserColumnBase* Data = Column.GetMutablePtr<FChooserColumnBase>()) Data->PostLoad();
			}
		}
	}
	Table->Compile(true);
}

TArray<FString> UGarBlueprintMigrationLibrary::ValidateChooser(UChooserTable* Table)
{
	TArray<FString> Errors;
	if (!Table) return {TEXT("Null chooser")};
	Table->Compile(true);
	TArray<UObject*> Objects = GetOwnedObjects(Table);
	Objects.Add(Table);
	for (UObject* Object : Objects)
	{
		if (UChooserTable* Child = Cast<UChooserTable>(Object))
		{
			for (FInstancedStruct& Column : Child->ColumnsStructs)
			{
				if (FChooserColumnBase* Data = Column.GetMutablePtr<FChooserColumnBase>())
				{
					if (const FChooserParameterBase* Parameter = Data->GetInputValue())
					{
						FText Message;
						if (Parameter->HasCompileErrors(Message)) Errors.Add(Child->GetName() + TEXT(": ") + Message.ToString());
					}
				}
			}
		}
	}
	return Errors;
}

int32 UGarBlueprintMigrationLibrary::BuildPoseSearchDatabase(UPoseSearchDatabase* Database, bool bNewRequest)
{
	if (!Database) return 2;
	using namespace UE::PoseSearch;
	return static_cast<int32>(FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database,
		bNewRequest ? ERequestAsyncBuildFlag::NewRequest : ERequestAsyncBuildFlag::ContinueRequest));
}

void UGarBlueprintMigrationLibrary::ReplaceObjectReferences(UObject* Object, const TMap<UObject*, UObject*>& Replacements)
{
	if (Object)
	{
		Object->Modify();
		FArchiveReplaceObjectRef<UObject> Replace(Object, Replacements, EArchiveReplaceObjectFlags::IgnoreOuterRef | EArchiveReplaceObjectFlags::IgnoreArchetypeRef);
	}
}

FString UGarBlueprintMigrationLibrary::CompileBlueprint(UBlueprint* Blueprint)
{
	if (!Blueprint) return TEXT("{\"error\":\"Null Blueprint\"}");
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FCompilerResultsLog Log;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Log);
	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetNumberField(TEXT("errors"), Log.NumErrors);
	Report->SetNumberField(TEXT("warnings"), Log.NumWarnings);
	Report->SetNumberField(TEXT("status"), Blueprint->Status);
	TArray<TSharedPtr<FJsonValue>> Messages;
	for (const TSharedRef<FTokenizedMessage>& Message : Log.Messages)
	{
		Messages.Add(MakeShared<FJsonValueString>(Message->ToText().ToString()));
	}
	Report->SetArrayField(TEXT("messages"), Messages);
	FString Result;
	FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Result));
	return Result;
}
