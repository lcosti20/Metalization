using System.IO;
using UnrealBuildTool;

/** Editor-only tools used by the asset pipeline (Niagara effect authoring). Never cooked into the game. */
public class MetalizationEditor : ModuleRules
{
	public MetalizationEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"AssetTools",
			"Niagara",
			"NiagaraCore",
			"NiagaraShader",
			"RenderCore",
			"StructUtils"
		});

		// The lightweight (stateless) emitter classes live in Niagara's Internal folder, which isn't on the
		// include path of dependent modules by default.
		foreach (string Module in new string[] { "Niagara", "NiagaraShader" })
		{
			PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Plugins", "FX", "Niagara", "Source", Module, "Internal"));
		}
	}
}
