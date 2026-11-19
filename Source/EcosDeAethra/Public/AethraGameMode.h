#pragma once

#include "GameFramework/GameModeBase.h"
#include "AethraGameMode.generated.h"

class AAethraVoxelWorld;
class APlayerStart;

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

    AAethraVoxelWorld* EnsureVoxelWorld();
    void SpawnLighting();
};
