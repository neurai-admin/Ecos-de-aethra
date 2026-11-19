#include "AethraGameMode.h"

#include "AethraCharacter.h"
#include "AethraHUD.h"
#include "AethraVoxelWorld.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"

AAethraGameMode::AAethraGameMode()
{
    DefaultPawnClass = AAethraCharacter::StaticClass();
    HUDClass = AAethraHUD::StaticClass();

    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Clouds(
        TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst"));
    CloudMaterial = Clouds.Get();
}

AAethraVoxelWorld* AAethraGameMode::GetVoxelWorld() const
{
    return VoxelWorld.Get();
}

AAethraVoxelWorld* AAethraGameMode::EnsureVoxelWorld()
{
    if (!VoxelWorld)
    {
        for (TActorIterator<AAethraVoxelWorld> It(GetWorld()); It; ++It)
        {
            VoxelWorld = *It;
            break;
        }
        if (!VoxelWorld)
        {
            VoxelWorld = GetWorld()->SpawnActor<AAethraVoxelWorld>();
        }
    }
    return VoxelWorld.Get();
}

void AAethraGameMode::StartPlay()
{
    EnsureVoxelWorld();
    SpawnLighting();
    Super::StartPlay();
}

AActor* AAethraGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
    if (AAethraVoxelWorld* Terrain = EnsureVoxelWorld())
    {
        const FVector Start = Terrain->PrepareSpawn();
        if (!VoxelStart)
        {
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Params.ObjectFlags |= RF_Transient;
            VoxelStart = GetWorld()->SpawnActor<APlayerStart>(Start, FRotator(0, 70, 0), Params);
        }
        else
        {
            VoxelStart->SetActorLocation(Start);
        }
        if (VoxelStart)
        {
            return VoxelStart.Get();
        }
    }

    return Super::ChoosePlayerStart_Implementation(Player);
}

void AAethraGameMode::SpawnLighting()
{
    if (!TActorIterator<ASkyAtmosphere>(GetWorld()))
    {
        GetWorld()->SpawnActor<ASkyAtmosphere>();
    }
    if (!TActorIterator<ADirectionalLight>(GetWorld()))
    {
        auto* Sun = GetWorld()->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-42, -28, 0));
        auto* Light = Sun ? Cast<UDirectionalLightComponent>(Sun->GetLightComponent()) : nullptr;
        if (Light)
        {
            Light->SetMobility(EComponentMobility::Movable);
            Light->SetIntensity(4.0f);
            Light->SetLightColor(FLinearColor(1.0f, 0.92f, 0.78f));
            Light->SetAtmosphereSunLight(true);
            // Natural-light pass: sun shafts through foliage and fog, soft cloud shadows.
            Light->bEnableLightShaftBloom = true;
            Light->BloomScale = 0.12f;
            Light->BloomThreshold = 4.0f;
            Light->SetVolumetricScatteringIntensity(1.4f);
            Light->bCastCloudShadows = true;
            Light->DynamicShadowDistanceMovableLight = 12000.0f;
            Light->CascadeDistributionExponent = 2.6f;
            Light->MarkRenderStateDirty();
        }
    }
    if (!TActorIterator<ASkyLight>(GetWorld()))
    {
        auto* Sky = GetWorld()->SpawnActor<ASkyLight>();
        if (Sky)
        {
            auto* Light = Sky->GetLightComponent();
            Light->SetMobility(EComponentMobility::Movable);
            Light->SetIntensity(0.85f);
            // Ambient follows the atmosphere colour instead of a static capture.
            Light->bRealTimeCapture = true;
            Light->SetLowerHemisphereColor(FLinearColor(0.10f, 0.09f, 0.07f));
            Light->MarkRenderStateDirty();
            Light->RecaptureSky();
        }
    }
    if (!TActorIterator<AExponentialHeightFog>(GetWorld()))
    {
        if (auto* Fog = GetWorld()->SpawnActor<AExponentialHeightFog>(FVector(0, 0, 2000), FRotator::ZeroRotator))
        {
            UExponentialHeightFogComponent* Haze = Fog->GetComponent();
            Haze->SetFogDensity(0.012f);
            Haze->SetFogHeightFalloff(0.08f);
            Haze->SetStartDistance(1500.0f);
            Haze->SetFogInscatteringColor(FLinearColor(0.42f, 0.52f, 0.66f));
            Haze->SetVolumetricFog(true); // r.VolumetricFog / scalability decides whether it renders
            Haze->SetVolumetricFogScatteringDistribution(0.75f);
            Haze->SetVolumetricFogExtinctionScale(0.6f);
        }
    }
    if (CloudMaterial && !TActorIterator<AVolumetricCloud>(GetWorld()))
    {
        if (auto* Clouds = GetWorld()->SpawnActor<AVolumetricCloud>())
        {
            UVolumetricCloudComponent* Layer = Clouds->FindComponentByClass<UVolumetricCloudComponent>();
            Layer->SetMaterial(CloudMaterial);
            Layer->SetLayerBottomAltitude(3.0f);
            Layer->SetLayerHeight(6.0f);
        }
    }
    if (!TActorIterator<APostProcessVolume>(GetWorld()))
    {
        if (auto* Post = GetWorld()->SpawnActor<APostProcessVolume>())
        {
            Post->bUnbound = true;
            FPostProcessSettings& S = Post->Settings;
            S.bOverride_BloomIntensity = true;            S.BloomIntensity = 0.55f;
            S.bOverride_AmbientOcclusionIntensity = true; S.AmbientOcclusionIntensity = 0.55f;
            S.bOverride_AmbientOcclusionRadius = true;    S.AmbientOcclusionRadius = 140.0f;
            S.bOverride_VignetteIntensity = true;         S.VignetteIntensity = 0.28f;
            S.bOverride_ColorSaturation = true;           S.ColorSaturation = FVector4(1.06f, 1.06f, 1.06f, 1.0f);
            S.bOverride_WhiteTemp = true;                 S.WhiteTemp = 6150.0f;
        }
    }
}
