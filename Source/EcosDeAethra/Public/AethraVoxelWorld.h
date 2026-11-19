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

    UFUNCTION(BlueprintCallable, Category="Voxel")
    bool SetBlock(FIntVector Cell, EAethraBlock Block);

    FVector PrepareSpawn();
    void SetStreamingRadius(int32 Radius);

private:
    friend class AAethraVoxelChunk;

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
    void LoadChunk(FIntPoint Chunk);
};
