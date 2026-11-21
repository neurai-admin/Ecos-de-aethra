#include "AethraGameMode.h"

#include "AethraCharacter.h"
#include "AethraHUD.h"
#include "AethraLisDirector.h"
#include "AethraLisVillage.h"
#include "AethraVoxelWorld.h"
#include "VisualWorld/AethraVisualWorld.h"
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

AAethraVisualWorld* AAethraGameMode::GetVisualWorld() const
{
    return VisualWorld.Get();
}

AAethraVoxelWorld* AAethraGameMode::EnsureVoxelWorld()
{
    if (!VoxelWorld)
    {
        // LIS 03 demo: -AethraElden opens the Elden village demo world (its own seed, so its own voxel and
        // LIS saves: the player's world is never touched); -AethraSeed=<n> picks any seed.
        int32 Seed = 0;
        bool bSeed = FParse::Value(FCommandLine::Get(), TEXT("AethraSeed="), Seed);
        if (!bSeed && FParse::Param(FCommandLine::Get(), TEXT("AethraElden")))
        {
            Seed = FAethraLisVillage::DemoSeed;
            bSeed = true;
        }
        for (TActorIterator<AAethraVoxelWorld> It(GetWorld()); It; ++It)
        {
            VoxelWorld = *It;
            break;
        }
        if (!VoxelWorld)
        {
            FActorSpawnParameters Params;
            Params.bDeferConstruction = true;
            VoxelWorld = GetWorld()->SpawnActor<AAethraVoxelWorld>(Params);
            if (VoxelWorld)
            {
                VoxelWorld->FinishSpawning(FTransform::Identity);
            }
        }
        if (VoxelWorld && bSeed)
        {
            VoxelWorld->Seed = Seed; // before the first block is read: the world initialises lazily
        }
    }
    return VoxelWorld.Get();
}

bool AAethraGameMode::IsVisualWorld()
{
    return FParse::Param(FCommandLine::Get(), TEXT("AethraVisualWorld")) || FCommandLine::Get() && FCString::Strifind(FCommandLine::Get(), TEXT("-AethraVisualWorld="));
}

AAethraVisualWorld* AAethraGameMode::EnsureVisualWorld()
{
    if (!VisualWorld)
    {
        // Visual World prototype 01: its own generator and saves; the voxel world is not created.
        int32 Seed = AAethraVisualWorld::DemoSeed;
        FParse::Value(FCommandLine::Get(), TEXT("AethraVisualWorld="), Seed);
        VisualWorld = GetWorld()->SpawnActor<AAethraVisualWorld>();
        if (VisualWorld)
        {
            VisualWorld->Start(Seed, FVector::ZeroVector);
        }
    }
    return VisualWorld.Get();
}

void AAethraGameMode::StartPlay()
{
    if (IsVisualWorld())
    {
        EnsureVisualWorld();
    }
    else
    {
        EnsureVoxelWorld();
    }
    SpawnLighting();
    // LIS 02: the Elden villagers. -NoLIS runs the voxel world alone (benchmarks without NPCs).
    if (VoxelWorld && !FParse::Param(FCommandLine::Get(), TEXT("NoLIS")))
    {
        if (auto* Director = GetWorld()->SpawnActor<AAethraLisDirector>())
        {
            // -AethraElden: the isolated demo world, where the village may fell generated trees on its plots.
            Director->bDemoVillage = FParse::Param(FCommandLine::Get(), TEXT("AethraElden"));
            Director->Start(VoxelWorld, FIntPoint(6, 20)); // just ahead of the spawn view
        }
    }
    Super::StartPlay();
}

AActor* AAethraGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
    if (IsVisualWorld())
    {
        if (AAethraVisualWorld* Visual = EnsureVisualWorld())
        {
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Params.ObjectFlags |= RF_Transient;
            if (!VoxelStart)
            {
                VoxelStart = GetWorld()->SpawnActor<APlayerStart>(Visual->SpawnLocation(), FRotator(0, 40, 0), Params);
            }
            if (VoxelStart)
            {
                return VoxelStart.Get();
            }
        }
        return Super::ChoosePlayerStart_Implementation(Player);
    }
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
            Light->SetIntensity(3.6f);
            Light->SetLightColor(FLinearColor(1.0f, 0.89f, 0.74f)); // warm late-morning sun
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
            Light->SetIntensity(1.35f); // lifts shadowed faces toward a cool sky tint
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
            Haze->SetFogDensity(0.008f); // keeps the distant mountains readable
            Haze->SetFogHeightFalloff(0.08f);
            Haze->SetStartDistance(1500.0f);
            Haze->SetFogInscatteringColor(FLinearColor(0.42f, 0.52f, 0.66f));
            Haze->SetDirectionalInscatteringColor(FLinearColor(0.55f, 0.45f, 0.30f)); // glow toward the sun
            Haze->SetDirectionalInscatteringExponent(6.0f);
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
            S.bOverride_ColorSaturation = true;           S.ColorSaturation = FVector4(0.97f, 0.97f, 0.97f, 1.0f);
            S.bOverride_ColorContrast = true;             S.ColorContrast = FVector4(1.06f, 1.06f, 1.06f, 1.0f);
            S.bOverride_WhiteTemp = true;                 S.WhiteTemp = 6150.0f;
        }
    }
}
