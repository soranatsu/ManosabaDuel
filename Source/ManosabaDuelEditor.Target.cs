using UnrealBuildTool;

public class ManosabaDuelEditorTarget : TargetRules
{
    public ManosabaDuelEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("ManosabaDuel");
    }
}
