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
    FlowerYellow
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
};
