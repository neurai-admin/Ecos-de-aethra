#pragma once

#include "CoreMinimal.h"
#include "AethraVoxelTypes.generated.h"

UENUM(BlueprintType)
enum class EAethraBlock : uint8
{
    Air,
    Grass,
    Dirt,
    Stone,
    Sand,
    Wood,
    Leaves,
    // Appended only: saved edits store these numeric values.
    PineLeaves,
    TallGrass,
    FlowerRed,
    FlowerYellow,
    Snow,
    Water,
    BirchWood,
    BirchLeaves,
    // Aethra block world (WorldGen soils and rocks, ruins, enchanted caves, FloraGen plants).
    // Ids stop at 31: the materials read the id from vertex alpha / 31.
    Gravel,
    Mud,
    Basalt,
    Limestone,
    Sandstone,
    Slate,
    Marble,
    StoneBricks,
    Planks,
    RoofTiles,
    Aetherite,
    Sunflower,
    Lavender,
    FlowerWhite,
    FlowerBlue,
    Fern,
    Ceramic
};

// Who caused a block change. Ids must stay meaningful across restarts:
// Player = local player slot, Npc = persistent LIS identity id, Environment = system id.
UENUM(BlueprintType)
enum class EAethraInstigatorKind : uint8
{
    Unknown,     // callers of the original SetBlock(Cell, Block)
    Player,
    Npc,
    Environment
};

USTRUCT(BlueprintType)
struct ECOSDEAETHRA_API FAethraInstigator
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Voxel")
    EAethraInstigatorKind Kind = EAethraInstigatorKind::Unknown;

    UPROPERTY(BlueprintReadOnly, Category="Voxel")
    int32 Id = 0;

    static FAethraInstigator Player(int32 Slot = 0) { return { EAethraInstigatorKind::Player, Slot }; }
    static FAethraInstigator Npc(int32 PersistentId) { return { EAethraInstigatorKind::Npc, PersistentId }; }
    static FAethraInstigator Environment(int32 SystemId = 0) { return { EAethraInstigatorKind::Environment, SystemId }; }
};

USTRUCT(BlueprintType)
struct ECOSDEAETHRA_API FAethraBlockChange
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Voxel")
    FIntVector Cell = FIntVector::ZeroValue;

    UPROPERTY(BlueprintReadOnly, Category="Voxel")
    EAethraBlock Before = EAethraBlock::Air;

    UPROPERTY(BlueprintReadOnly, Category="Voxel")
    EAethraBlock After = EAethraBlock::Air;

    // Appended field: listeners that ignore it keep working. Not stored in the voxel save.
    UPROPERTY(BlueprintReadOnly, Category="Voxel")
    FAethraInstigator Instigator;
};
