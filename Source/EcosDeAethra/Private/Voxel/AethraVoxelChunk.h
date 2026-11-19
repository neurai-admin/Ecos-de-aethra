#pragma once

#include "AethraVoxelTypes.h"
#include "GameFramework/Actor.h"
#include "AethraVoxelChunk.generated.h"

class AAethraVoxelWorld;
class UProceduralMeshComponent;

UCLASS(NotBlueprintable)
class AAethraVoxelChunk : public AActor
{
    GENERATED_BODY()

public:
    AAethraVoxelChunk();
    // bAsyncCollision = false only for the chunk the player spawns on this frame.
    void Initialize(AAethraVoxelWorld& World, FIntPoint Coordinate, bool bAsyncCollision = false);
    void Rebuild(const AAethraVoxelWorld& World);
    void SetBlock(int32 Index, EAethraBlock Block);

private:
    UPROPERTY(VisibleAnywhere, Category="Voxel")
    TObjectPtr<UProceduralMeshComponent> Mesh;

    TArray<EAethraBlock> Blocks;
    FIntPoint Chunk = FIntPoint::ZeroValue;
};
