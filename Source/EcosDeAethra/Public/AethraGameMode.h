#pragma once

#include "GameFramework/GameModeBase.h"
#include "AethraGameMode.generated.h"

class AAethraVoxelWorld;
class APlayerStart;
class UMaterialInterface;

UCLASS()
class ECOSDEAETHRA_API AAethraGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AAethraGameMode();
    virtual void StartPlay() override;
    virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
    AAethraVoxelWorld* GetVoxelWorld() const;

private:
    UPROPERTY(Transient)
    TObjectPtr<AAethraVoxelWorld> VoxelWorld;

    UPROPERTY(Transient)
    TObjectPtr<APlayerStart> VoxelStart;

    // Hard reference so the cooker packages the engine cloud material.
    UPROPERTY()
    TObjectPtr<UMaterialInterface> CloudMaterial;

    AAethraVoxelWorld* EnsureVoxelWorld();
    void SpawnLighting();
};
