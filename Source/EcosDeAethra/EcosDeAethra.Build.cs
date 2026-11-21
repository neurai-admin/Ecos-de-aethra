using UnrealBuildTool;

public class EcosDeAethra : ModuleRules
{
    public EcosDeAethra(ReadOnlyTargetRules target) : base(target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "AethraLIS" });
        PrivateDependencyModuleNames.AddRange(new[] { "InputCore", "ProceduralMeshComponent", "MeshDescription", "StaticMeshDescription" });
    }
}
