using System.IO;
using UnrealBuildTool;

public class VC26Core : ModuleRules
{
	public VC26Core(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// The rules live in <repo>/core so the same code is shared with the
		// CMake unit tests and the ranked server. Private/VC26CoreSources.cpp
		// compiles them into this module.
		string RepoRoot = Path.GetFullPath(Path.Combine(PluginDirectory, "..", "..", ".."));
		PublicIncludePaths.Add(Path.Combine(RepoRoot, "core", "include"));
		PrivateIncludePaths.Add(Path.Combine(RepoRoot, "core", "src"));

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
	}
}
