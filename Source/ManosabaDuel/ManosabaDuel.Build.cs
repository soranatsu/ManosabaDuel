using UnrealBuildTool;

public class ManosabaDuel : ModuleRules
{
    public ManosabaDuel(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "InputCore", "ProceduralMeshComponent"
        });
        PrivateDependencyModuleNames.AddRange(new[] { "SlateCore", "RHI", "RenderCore" });
    }
}
