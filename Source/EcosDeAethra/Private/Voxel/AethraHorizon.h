#pragma once

#include "Async/Future.h"
#include "GameFramework/Actor.h"
#include "AethraHorizon.generated.h"

class UMaterialInterface;
class UProceduralMeshComponent;

// Distant terrain beyond the voxel streaming radius: blocky, coarse LOD tiles rebuilt from the seed
// off the game thread. A hole matching the loaded voxel chunks keeps both from overlapping.
UCLASS(NotBlueprintable)
class AAethraHorizon : public AActor
{
    GENERATED_BODY()

public:
    static constexpr int32 TileCells = 128;
    static constexpr int32 RadiusTiles = 8; // ~1 km of horizon around the player

    struct FMeshData
    {
        TArray<FVector> Vertices;
        TArray<FVector> Normals;
        TArray<FLinearColor> Colors;
        TArray<int32> Triangles;
    };

    AAethraHorizon();
    void Update(int32 Seed, FIntPoint CenterChunk, int32 NearRadius);

    // Pure function of its inputs, safe on worker threads. Hole is in cells, max exclusive; empty = none.
    static FMeshData BuildTile(int32 Seed, FIntPoint Tile, int32 Step, const FIntRect& Hole);
    static int32 StepFor(int32 RingDistance);

private:
    struct FSignature
    {
        int32 Step = -1;
        FIntRect Hole;
        bool operator==(const FSignature& Other) const { return Step == Other.Step && Hole == Other.Hole; }
    };

    struct FTileState
    {
        FSignature Built;
        FSignature Pending;
        TOptional<TFuture<FMeshData>> Job;
    };

    UPROPERTY(Transient)
    TMap<FIntPoint, TObjectPtr<UProceduralMeshComponent>> Meshes;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> Material;

    TMap<FIntPoint, FTileState> Tiles;
    TArray<FIntPoint> RingOrder;

    void Apply(FIntPoint Tile, FMeshData&& Data);
};
