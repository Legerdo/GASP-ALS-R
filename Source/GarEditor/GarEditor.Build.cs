using UnrealBuildTool;

public class GAREditor : ModuleRules
{
	public GAREditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		CppCompileWarningSettings.NonInlinedGenCppWarningLevel = WarningLevel.Warning;

		PublicDependencyModuleNames.AddRange(
		[
			"Core", "CoreUObject", "Engine", "AnimationModifiers", "AnimationBlueprintLibrary", "GAR", "PoseSearch"
		]);

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
			[
				"BlueprintGraph", "Kismet", "Slate", "SlateCore", "Projects", "UnrealEd",
				"PhysicsControl", "PhysicsCore", "GameplayTags", "GameplayAbilities", "Mover"
			]);
		}
	}
}
