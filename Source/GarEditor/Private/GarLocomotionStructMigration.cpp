// Copyright (c) SAM-tak. All Rights Reserved.

#include "GarBlueprintMigrationLibrary.h"
#include "Chooser.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "K2Node_EditablePinBase.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "State/GarBlendStackInputs.h"
#include "State/GarLocomotionChooserOutput.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/UnrealType.h"

namespace GarLocomotionStructMigration
{
	struct FConverter
	{
		TMap<const UScriptStruct*, UScriptStruct*> Types;
		TMap<FString, FString> Names;
		TArray<TPair<FString, FString>> TextReplacements;
		int32 ConvertedValues{0};
		int32 ConvertedFields{0};
		TArray<FString> Errors;

		FString Remap(FString Text) const
		{
			for (const auto& Pair : TextReplacements)
			{
				Text.ReplaceInline(*Pair.Key, *Pair.Value, ESearchCase::CaseSensitive);
			}
			return Text;
		}

		bool AddType(const TCHAR* Path, UScriptStruct* Target)
		{
			UScriptStruct* Source = LoadObject<UScriptStruct>(nullptr, Path);
			if (!Source) return false;
			Types.Add(Source, Target);
			TextReplacements.Emplace(Source->GetPathName(), Target->GetPathName());
			TextReplacements.Emplace(Source->GetName(), Target->GetName());
			int32 NumFields = 0;
			for (TFieldIterator<FProperty> It(Source); It; ++It)
			{
				const FString OldName = It->GetName();
				FString NewName, Suffix;
				if (!OldName.Split(TEXT("_"), &NewName, &Suffix)) return false;
				if (NewName == TEXT("Loop")) NewName = TEXT("bLoop");
				const FProperty* NewProperty = Target->FindPropertyByName(*NewName);
				if (!NewProperty || !It->SameType(NewProperty)) return false;
				Names.Add(OldName, NewName);
				TextReplacements.Emplace(OldName, NewName);
				++NumFields;
			}
			int32 TargetFields = 0;
			for (TFieldIterator<FProperty> It(Target); It; ++It) ++TargetFields;
			return NumFields == TargetFields;
		}

		void VisitStruct(const UStruct* Type, void* Memory)
		{
			for (TFieldIterator<FProperty> It(Type); It; ++It)
			{
				for (int32 Index = 0; Index < It->ArrayDim; ++Index)
				{
					VisitProperty(*It, It->ContainerPtrToValuePtr<void>(Memory, Index));
				}
			}
		}

		void VisitProperty(FProperty* Property, void* Memory)
		{
			if (FStructProperty* Struct = CastField<FStructProperty>(Property))
			{
				if (Struct->Struct == FInstancedStruct::StaticStruct())
				{
					FInstancedStruct& Value = *static_cast<FInstancedStruct*>(Memory);
					if (!Value.IsValid()) return;
					if (UScriptStruct* const* Target = Types.Find(Value.GetScriptStruct()))
					{
						FInstancedStruct Converted;
						Converted.InitializeAs(*Target);
						for (TFieldIterator<FProperty> It(Value.GetScriptStruct()); It; ++It)
						{
							FProperty* Dest = (*Target)->FindPropertyByName(*Names.FindChecked(It->GetName()));
							const void* SourceMemory = It->ContainerPtrToValuePtr<void>(Value.GetMemory());
							void* TargetMemory = Dest->ContainerPtrToValuePtr<void>(Converted.GetMutableMemory());
							Dest->CopyCompleteValue(TargetMemory, SourceMemory);
							// SameType was checked before changing anything. Verify every value as well.
							if (!Dest->Identical(SourceMemory, TargetMemory, PPF_None))
								Errors.Add(It->GetName() + TEXT(": value changed"));
							++ConvertedFields;
						}
						Value = MoveTemp(Converted);
						++ConvertedValues;
					}
					else
					{
						// In particular, never copy FPoseSearchColumn: its copy constructor clears DB refs.
						VisitStruct(Value.GetScriptStruct(), Value.GetMutableMemory());
					}
				}
				else VisitStruct(Struct->Struct, Memory);
			}
			else if (FArrayProperty* Array = CastField<FArrayProperty>(Property))
			{
				FScriptArrayHelper Helper(Array, Memory);
				for (int32 I = 0; I < Helper.Num(); ++I) VisitProperty(Array->Inner, Helper.GetRawPtr(I));
			}
			else if (FMapProperty* Map = CastField<FMapProperty>(Property))
			{
				FScriptMapHelper Helper(Map, Memory);
				for (int32 I = 0; I < Helper.GetMaxIndex(); ++I)
				{
					if (!Helper.IsValidIndex(I)) continue;
					VisitProperty(Map->KeyProp, Helper.GetKeyPtr(I));
					VisitProperty(Map->ValueProp, Helper.GetValuePtr(I));
				}
				Helper.Rehash();
			}
			else if (FSetProperty* Set = CastField<FSetProperty>(Property))
			{
				FScriptSetHelper Helper(Set, Memory);
				for (int32 I = 0; I < Helper.GetMaxIndex(); ++I)
					if (Helper.IsValidIndex(I)) VisitProperty(Set->ElementProp, Helper.GetElementPtr(I));
				Helper.Rehash();
			}
			else if (FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(Property))
			{
				if (UScriptStruct* Old = Cast<UScriptStruct>(Object->GetObjectPropertyValue(Memory)))
					if (UScriptStruct* const* Target = Types.Find(Old)) Object->SetObjectPropertyValue(Memory, *Target);
			}
			else if (FNameProperty* Name = CastField<FNameProperty>(Property))
			{
				Name->SetPropertyValue(Memory, *Remap(Name->GetPropertyValue(Memory).ToString()));
			}
			else if (FStrProperty* String = CastField<FStrProperty>(Property))
			{
				String->SetPropertyValue(Memory, Remap(String->GetPropertyValue(Memory)));
			}
		}

		void VisitNode(UEdGraphNode* Node)
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin) continue;
				VisitStruct(FEdGraphPinType::StaticStruct(), &Pin->PinType);
				Pin->PinName = *Remap(Pin->PinName.ToString());
				Pin->DefaultValue = Remap(Pin->DefaultValue);
				Pin->AutogeneratedDefaultValue = Remap(Pin->AutogeneratedDefaultValue);
			}
			if (UK2Node_EditablePinBase* Editable = Cast<UK2Node_EditablePinBase>(Node))
				for (auto& Pin : Editable->UserDefinedPins)
					if (Pin.IsValid()) VisitStruct(FUserPinInfo::StaticStruct(), Pin.Get());
		}
	};
}

FString UGarBlueprintMigrationLibrary::MigrateLocomotionStructs(UBlueprint* Blueprint, UChooserTable* Table)
{
	using namespace GarLocomotionStructMigration;
	// Deliberately narrow: this is a migration of two known assets, not a generic asset replacement API.
	if (!Blueprint || Blueprint->GetPathName() != TEXT("/GAR/Core/AnimationInstances/AB_Gar.AB_Gar") ||
		!Table || Table->GetPathName() != TEXT("/GAR/Core/AnimationInstances/LocomotionData/CHT_GarCharacterAnimations_PoseMatch.CHT_GarCharacterAnimations_PoseMatch"))
		return TEXT("ERROR: unexpected migration targets");

	FConverter Converter;
	if (!Converter.AddType(TEXT("/GAR/Core/AnimationInstances/LocomotionData/S_BlendStackInputs.S_BlendStackInputs"), FGarBlendStackInputs::StaticStruct()) ||
		!Converter.AddType(TEXT("/GAR/Core/AnimationInstances/LocomotionData/S_CHT_MoverCharacterAnimations_OUT.S_CHT_MoverCharacterAnimations_OUT"), FGarLocomotionChooserOutput::StaticStruct()))
		return TEXT("ERROR: source/native property layouts do not match");
	// Replace full object paths before simple type names, and object class only in those typed references.
	for (const auto& Pair : Converter.Types)
		Converter.TextReplacements.Insert(TPair<FString, FString>(
			FString::Printf(TEXT("/Script/CoreUObject.UserDefinedStruct'%s'"), *Pair.Key->GetPathName()),
			FString::Printf(TEXT("/Script/CoreUObject.ScriptStruct'%s'"), *Pair.Value->GetPathName())), 0);

	Blueprint->Modify();
	// Native struct layouts differ from UDS layouts. Keep CDO defaults as text, not raw struct memory.
	TMap<FName, FString> Defaults;
	UObject* OldCDO = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
	for (FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		if (Converter.Types.Contains(Cast<UScriptStruct>(Variable.VarType.PinSubCategoryObject.Get())) && OldCDO)
		{
			FString Value = Converter.Remap(ExportProperty(OldCDO, Variable.VarName));
			Defaults.Add(Variable.VarName, Value);
			Variable.DefaultValue = Value;
		}
	}

	TArray<UObject*> Objects = GetOwnedObjects(Blueprint);
	Objects.Add(Blueprint);
	TArray<UEdGraphNode*> Nodes;
	for (UObject* Object : Objects)
	{
		if (Object->IsA<UStruct>() || Object->HasAnyFlags(RF_ClassDefaultObject)) continue;
		Object->Modify();
		Converter.VisitStruct(Object->GetClass(), Object);
		if (UEdGraphNode* Node = Cast<UEdGraphNode>(Object))
		{
			Converter.VisitNode(Node);
			Nodes.Add(Node);
		}
	}
	Objects = GetOwnedObjects(Table);
	Objects.Add(Table);
	for (UObject* Object : Objects)
	{
		if (UChooserTable* Child = Cast<UChooserTable>(Object))
		{
			Child->Modify();
			Converter.VisitStruct(Child->GetClass(), Child);
		}
	}
	if (!Converter.Errors.IsEmpty()) return TEXT("ERROR: ") + FString::Join(Converter.Errors, TEXT("; "));

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	for (UEdGraphNode* Node : Nodes) Node->ReconstructNode();
	const FString CompileResult = CompileBlueprint(Blueprint);
	UObject* NewCDO = Blueprint->GeneratedClass->GetDefaultObject();
	for (const auto& Pair : Defaults)
	{
		if (!ImportProperty(NewCDO, Pair.Key, Pair.Value))
			return TEXT("ERROR: failed to restore default ") + Pair.Key.ToString();
	}
	CompileChooser(Table);
	return FString::Printf(TEXT("Converted %d struct values / %d fields. Blueprint: %s"),
		Converter.ConvertedValues, Converter.ConvertedFields, *CompileResult);
}
