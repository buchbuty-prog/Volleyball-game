using UnrealBuildTool;

public class VolleyballCareer26 : ModuleRules
{
	public VolleyballCareer26(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "VC26Core" });
	}
}
