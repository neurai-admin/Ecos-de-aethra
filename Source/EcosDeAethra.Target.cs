using UnrealBuildTool;

public class EcosDeAethraTarget : TargetRules
{
    public EcosDeAethraTarget(TargetInfo target) : base(target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("EcosDeAethra");
    }
}
