#include "AethraGameMode.h"

#include "AethraCharacter.h"
#include "AethraHUD.h"
#include "AethraVoxelWorld.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"

AAethraGameMode::AAethraGameMode()
{
    DefaultPawnClass = AAethraCharacter::StaticClass();
    HUDClass = AAethraHUD::StaticClass();
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
        }
    }
}
