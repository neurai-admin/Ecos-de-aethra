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
    void Initialize(AAethraVoxelWorld& World, FIntPoint Coordinate);
    void Rebuild(const AAethraVoxelWorld& World);
    void SetBlock(int32 Index, EAethraBlock Block);

private:
    UPROPERTY(VisibleAnywhere, Category="Voxel")
    TObjectPtr<UProceduralMeshComponent> Mesh;

    TArray<EAethraBlock> Blocks;
    FIntPoint Chunk = FIntPoint::ZeroValue;
};
