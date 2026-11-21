#pragma once

#include "AethraVoxelTypes.h"
#include "GameFramework/Actor.h"
#include "AethraVoxelWorld.generated.h"

class AAethraHorizon;
class AAethraVoxelChunk;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAethraOnBlockChanged, const FAethraBlockChange&, Change);

UCLASS(Blueprintable)
class ECOSDEAETHRA_API AAethraVoxelWorld : public AActor
{
    GENERATED_BODY()

public:
    AAethraVoxelWorld();
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Voxel")
    int32 Seed = 42819;

    UPROPERTY(BlueprintAssignable, Category="Voxel")
    FAethraOnBlockChanged OnBlockChanged;

    UFUNCTION(BlueprintPure, Category="Voxel")
    EAethraBlock GetBlock(FIntVector Cell) const;

    // Unchanged contract: same as SetBlockBy with an Unknown instigator.
    UFUNCTION(BlueprintCallable, Category="Voxel")
    bool SetBlock(FIntVector Cell, EAethraBlock Block);

    // Same validation, save and broadcast as SetBlock; OnBlockChanged reports who made the change.
    UFUNCTION(BlueprintCallable, Category="Voxel")
    bool SetBlockBy(FIntVector Cell, EAethraBlock Block, const FAethraInstigator& By);

    // Read-only: the whole column at Point (generation plus saved edits), bottom to top.
    // One terrain generation instead of one per GetBlock call; for navigation and AI queries.
    void GetColumn(FIntPoint Point, TArray<EAethraBlock>& OutColumn) const;

    FVector PrepareSpawn();
    void SetStreamingRadius(int32 Radius);

private:
    friend class AAethraVoxelChunk;
#if WITH_DEV_AUTOMATION_TESTS
    friend class FAethraVoxelChunkSeams;
#endif

    using FChunkEdits = TMap<int32, EAethraBlock>;

    TMap<FIntPoint, FChunkEdits> ChunkEdits;
    TArray<FIntPoint> StreamOffsets;

    UPROPERTY(Transient)
    TMap<FIntPoint, TObjectPtr<AAethraVoxelChunk>> Loaded;

    UPROPERTY(Transient)
    TObjectPtr<AAethraHorizon> Horizon;

    int64 Revision = 0;
    int32 StreamingRadius = 3;
    bool bInitialized = false;
    bool bSaveWritable = true;
    bool bNotifying = false;

    void Initialize();
    FString SaveSlot(int32 Slot) const;
    bool SaveEdits();
    void LoadEdits();
    void ApplyChunkEdits(FIntPoint Chunk, TArray<EAethraBlock>& Blocks) const;
    void ApplyColumnEdits(FIntPoint Point, TArrayView<EAethraBlock> Column) const;
    void UpdateStreaming(FIntPoint Center);
    void LoadChunk(FIntPoint Chunk, bool bAsyncCollision = true);
};
