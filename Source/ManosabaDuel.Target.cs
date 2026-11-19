using UnrealBuildTool;

public class ManosabaDuelTarget : TargetRules
{
    public ManosabaDuelTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("ManosabaDuel");
    }
}
