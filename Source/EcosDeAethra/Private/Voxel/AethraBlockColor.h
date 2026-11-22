#pragma once

#include "AethraVoxelTypes.h"

// Block tint shared by the voxel chunks and the distant horizon, so both read as one terrain.
inline FLinearColor AethraBlockColor(EAethraBlock Block, const FIntVector& Cell, const FIntVector& Normal)
{
    FColor Tint;
    switch (Block)
    {
    // Grass sides carry the top colour: M_AethraVoxel paints dirt below a ragged green fringe.
    case EAethraBlock::Grass: Tint = Normal.Z > 0 ? FColor(94, 138, 56) : FColor(98, 140, 54); break;
    case EAethraBlock::Dirt: Tint = FColor(113, 78, 50); break;
    case EAethraBlock::Stone: Tint = FColor(111, 118, 125); break;
    case EAethraBlock::Sand: Tint = FColor(204, 172, 110); break;
    case EAethraBlock::Wood: Tint = Normal.Z == 0 ? FColor(104, 69, 39) : FColor(153, 110, 61); break;
    case EAethraBlock::Leaves: Tint = FColor(63, 119, 57); break;
    case EAethraBlock::PineLeaves: Tint = FColor(36, 88, 58); break;
    case EAethraBlock::TallGrass: Tint = FColor(122, 168, 58); break;
    case EAethraBlock::FlowerRed: Tint = FColor(206, 54, 48); break;
    case EAethraBlock::FlowerYellow: Tint = FColor(236, 202, 64); break;
    case EAethraBlock::Snow: Tint = FColor(236, 242, 248); break;
    case EAethraBlock::Water: Tint = FColor(44, 92, 112); break;
    case EAethraBlock::BirchWood: Tint = Normal.Z == 0 ? FColor(218, 214, 200) : FColor(196, 170, 120); break;
    case EAethraBlock::BirchLeaves: Tint = FColor(112, 158, 64); break;
    case EAethraBlock::Gravel: Tint = FColor(128, 124, 118); break;
    case EAethraBlock::Mud: Tint = FColor(84, 66, 48); break;
    case EAethraBlock::Basalt: Tint = FColor(62, 64, 70); break;
    case EAethraBlock::Limestone: Tint = FColor(196, 190, 170); break;
    case EAethraBlock::Sandstone: Tint = FColor(206, 166, 110); break;
    case EAethraBlock::Slate: Tint = FColor(78, 88, 100); break;
    case EAethraBlock::Marble: Tint = FColor(226, 222, 214); break;
    case EAethraBlock::StoneBricks: Tint = FColor(140, 138, 132); break;
    case EAethraBlock::Planks: Tint = FColor(160, 118, 70); break;
    case EAethraBlock::RoofTiles: Tint = FColor(156, 70, 52); break;
    case EAethraBlock::Aetherite: Tint = FColor(96, 214, 236); break;
    case EAethraBlock::Sunflower: Tint = FColor(244, 196, 40); break;
    case EAethraBlock::Lavender: Tint = FColor(148, 112, 206); break;
    case EAethraBlock::FlowerWhite: Tint = FColor(240, 238, 228); break;
    case EAethraBlock::FlowerBlue: Tint = FColor(76, 126, 222); break;
    case EAethraBlock::Fern: Tint = FColor(70, 132, 60); break;
    case EAethraBlock::Ceramic: Tint = FColor(184, 108, 70); break;
    default: Tint = FColor::White; break;
    }

    // One tint per block type (no per-block variation): neighbouring blocks read as one surface.
    FLinearColor Result = FLinearColor(Tint) * 0.995f;
    // Alpha = block id / 31: the materials pick the per-block pattern from it (exact in 8 bits).
    Result.A = static_cast<uint8>(Block) / 31.0f;
    return Result;
}
