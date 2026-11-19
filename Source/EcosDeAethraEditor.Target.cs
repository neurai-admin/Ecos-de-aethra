using UnrealBuildTool;

public class EcosDeAethraEditorTarget : TargetRules
{
    public EcosDeAethraEditorTarget(TargetInfo target) : base(target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("EcosDeAethra");
    }
}
