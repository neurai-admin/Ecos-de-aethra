#pragma once

#include "AethraVoxelTypes.h"
#include "Containers/StaticArray.h"

namespace AethraTerrain
{
    inline constexpr int32 ChunkSize = 16;
    inline constexpr int32 Height = 256; // 64 until Marco 02, 128 until the Aethra mountains; dense index and saved cells are unaffected
    inline constexpr int32 CellCount = ChunkSize * ChunkSize * Height;
    inline constexpr int32 HorizontalLimit = 16384;
    inline constexpr double CellSizeCm = 100.0;

    int32 FloorDiv(int32 Value, int32 Divisor);
    FIntPoint ChunkAt(const FIntVector& Cell);
    FIntVector ChunkOrigin(const FIntPoint& Chunk);
    FIntVector CellAt(const FVector& Position);
    int32 Index(const FIntVector& Local);
    bool Contains(const FIntVector& Cell);
    bool IsKnownBlock(EAethraBlock Block);
    // Plants are cross-shaped, non-colliding and do not hide neighbouring faces.
    bool IsPlant(EAethraBlock Block);
    bool IsLeaves(EAethraBlock Block);
    // Opaque: hides neighbouring faces and darkens ambient occlusion. Leaves, water and plants are not.
    bool IsOpaque(EAethraBlock Block);
    // Visible top surface for the horizon: anything but air and plants.
    bool IsSurface(EAethraBlock Block);
    // Placing a block may overwrite these.
    bool IsReplaceable(EAethraBlock Block);
    inline constexpr int32 WaterLevel = 18; // highest water cell
    int32 SurfaceAt(FIntPoint Point, int32 Seed);
    void GenerateColumn(FIntPoint Point, int32 Seed, TStaticArray<EAethraBlock, Height>& Column);
    void GenerateChunk(FIntPoint Chunk, int32 Seed, TArray<EAethraBlock>& Blocks);
}
