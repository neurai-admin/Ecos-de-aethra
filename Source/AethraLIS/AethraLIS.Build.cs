using UnrealBuildTool;

// Living Intelligence System: NPC simulation core. Depends only on Core so it
// stays independent of the voxel runtime; integration happens through
// FLisVoxelAdapter in the game module.
public class AethraLIS : ModuleRules
{
    public AethraLIS(ReadOnlyTargetRules target) : base(target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core" });
    }
}
