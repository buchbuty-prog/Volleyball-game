using UnrealBuildTool;

public class VolleyballCareer26EditorTarget : TargetRules
{
	public VolleyballCareer26EditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("VolleyballCareer26");
	}
}
