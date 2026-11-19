#pragma once

#include "AethraVoxelTypes.h"

// Block tint shared by the voxel chunks and the distant horizon, so both read as one terrain.
inline FLinearColor AethraBlockColor(EAethraBlock Block, const FIntVector& Cell, const FIntVector& Normal)
{
    FColor Tint;
    switch (Block)
    {
    case EAethraBlock::Grass: Tint = Normal.Z > 0 ? FColor(110, 156, 61) : FColor(105, 112, 55); break;
    case EAethraBlock::Dirt: Tint = FColor(113, 78, 50); break;
    case EAethraBlock::Stone: Tint = FColor(111, 118, 125); break;
    case EAethraBlock::Sand: Tint = FColor(204, 172, 110); break;
    case EAethraBlock::Wood: Tint = Normal.Z == 0 ? FColor(104, 69, 39) : FColor(153, 110, 61); break;
    case EAethraBlock::Leaves: Tint = FColor(63, 119, 57); break;
    default: Tint = FColor::White; break;
    }

    const uint32 Variation = (GetTypeHash(Cell) >> 5) % 16;
    FLinearColor Result = FLinearColor(Tint) * (0.92f + 0.01f * Variation);
    Result.A = 1.0f;
    return Result;
}
