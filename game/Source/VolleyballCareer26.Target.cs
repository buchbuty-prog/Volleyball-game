using UnrealBuildTool;

public class VolleyballCareer26Target : TargetRules
{
	public VolleyballCareer26Target(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("VolleyballCareer26");
	}
}
