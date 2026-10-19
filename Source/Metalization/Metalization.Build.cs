// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Metalization : ModuleRules
{
	public Metalization(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"NavigationSystem",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"SlateCore",
			"DeveloperSettings"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Metalization",
			"Metalization/Game",
			"Metalization/Variant_Platforming",
			"Metalization/Variant_Platforming/Animation",
			"Metalization/Variant_Combat",
			"Metalization/Variant_Combat/AI",
			"Metalization/Variant_Combat/Animation",
			"Metalization/Variant_Combat/Gameplay",
			"Metalization/Variant_Combat/Interfaces",
			"Metalization/Variant_Combat/UI",
			"Metalization/Variant_SideScrolling",
			"Metalization/Variant_SideScrolling/AI",
			"Metalization/Variant_SideScrolling/Gameplay",
			"Metalization/Variant_SideScrolling/Interfaces",
			"Metalization/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
