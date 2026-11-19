#include "Voxel/AethraTerrain.h"

namespace
{
    uint32 Hash(FIntPoint Point, int32 Seed, uint32 Salt)
    {
        uint32 Value = static_cast<uint32>(Point.X) * 0x9E3779B1u;
        Value ^= static_cast<uint32>(Point.Y) * 0x85EBCA77u;
        Value ^= static_cast<uint32>(Seed) * 0xC2B2AE3Du ^ Salt;
        Value ^= Value >> 16;
        Value *= 0x7FEB352Du;
        Value ^= Value >> 15;
        Value *= 0x846CA68Bu;
        return Value ^ (Value >> 16);
    }

    int32 Interpolate(int32 A, int32 B, int32 Position, int32 Scale)
    {
        const int32 Weight = Position * Position * (3 * Scale - 2 * Position);
        return A + (B - A) * Weight / (Scale * Scale * Scale);
    }

    int32 Noise(FIntPoint Point, int32 Seed, int32 Scale, int32 Amplitude, uint32 Salt)
    {
        const int32 X = AethraTerrain::FloorDiv(Point.X, Scale);
        const int32 Y = AethraTerrain::FloorDiv(Point.Y, Scale);
        const int32 DX = Point.X - X * Scale;
        const int32 DY = Point.Y - Y * Scale;
        const auto Sample = [Seed, Amplitude, Salt](int32 SX, int32 SY)
        {
            return static_cast<int32>(Hash(FIntPoint(SX, SY), Seed, Salt) % (2 * Amplitude + 1)) - Amplitude;
        };
        const int32 Top = Interpolate(Sample(X, Y), Sample(X + 1, Y), DX, Scale);
        const int32 Bottom = Interpolate(Sample(X, Y + 1), Sample(X + 1, Y + 1), DX, Scale);
        return Interpolate(Top, Bottom, DY, Scale);
    }

    constexpr int32 TreeSpacing = 7;
    constexpr int32 CrownRadius = 2;

    FIntPoint TreeAt(FIntPoint Patch, int32 Seed)
    {
        const uint32 Roll = Hash(Patch, Seed, 0x38E9);
        return Patch * TreeSpacing + FIntPoint(1 + Roll % 5, 1 + (Roll >> 8) % 5);
    }

    bool IsTree(FIntPoint Point, FIntPoint Patch, int32 Ground, int32 Seed)
    {
        return Point.SizeSquared() > 16 && Ground >= 21 && Ground <= 44
            && Hash(Patch, Seed, 0x2A71) % 100 < 57;
    }
}

namespace AethraTerrain
{
    int32 FloorDiv(int32 Value, int32 Divisor)
    {
        check(Divisor > 0);
        return Value / Divisor - (Value % Divisor < 0 ? 1 : 0);
    }

    FIntPoint ChunkAt(const FIntVector& Cell)
    {
        return FIntPoint(FloorDiv(Cell.X, ChunkSize), FloorDiv(Cell.Y, ChunkSize));
    }

    FIntVector ChunkOrigin(const FIntPoint& Chunk)
    {
        return FIntVector(Chunk.X * ChunkSize, Chunk.Y * ChunkSize, 0);
    }

    FIntVector CellAt(const FVector& Position)
    {
        return FIntVector(FMath::FloorToInt32(Position.X / CellSizeCm),
            FMath::FloorToInt32(Position.Y / CellSizeCm), FMath::FloorToInt32(Position.Z / CellSizeCm));
    }

    int32 Index(const FIntVector& Local)
    {
        return Local.X + ChunkSize * (Local.Y + ChunkSize * Local.Z);
    }

    bool Contains(const FIntVector& Cell)
    {
        return Cell.X >= -HorizontalLimit && Cell.X < HorizontalLimit
            && Cell.Y >= -HorizontalLimit && Cell.Y < HorizontalLimit
            && Cell.Z >= 0 && Cell.Z < Height;
    }

    bool IsKnownBlock(EAethraBlock Block)
    {
        return static_cast<uint8>(Block) <= static_cast<uint8>(EAethraBlock::Leaves);
    }

    int32 SurfaceAt(FIntPoint Point, int32 Seed)
    {
        const int32 RiverX = Noise(FIntPoint(0, Point.Y), Seed, 44, 14, 0xD612);
        const int32 Rise = FMath::Min(FMath::Abs(Point.X - RiverX) / 2, 19);
        const int32 Hills = Noise(Point, Seed, 32, 11, 0x9DE1);
        const int32 Detail = Noise(Point, Seed, 9, 3, 0x319F);
        return FMath::Clamp(21 + Rise + Hills + Detail, 8, 56);
    }

    void GenerateColumn(FIntPoint Point, int32 Seed, TStaticArray<EAethraBlock, Height>& Column)
    {
        for (EAethraBlock& Block : Column)
        {
            Block = EAethraBlock::Air;
        }
        if (!Contains(FIntVector(Point.X, Point.Y, 0)))
        {
            return;
        }

        const int32 Surface = SurfaceAt(Point, Seed);
        const bool bSandy = Surface <= 20;
        for (int32 Z = 0; Z <= Surface; ++Z)
        {
            if (Z == 0 || Z < Surface - 3)
            {
                Column[Z] = EAethraBlock::Stone;
            }
            else
            {
                Column[Z] = bSandy ? EAethraBlock::Sand : Z == Surface ? EAethraBlock::Grass : EAethraBlock::Dirt;
            }
        }

        const int32 MinPatchX = FloorDiv(Point.X - CrownRadius, TreeSpacing);
        const int32 MaxPatchX = FloorDiv(Point.X + CrownRadius, TreeSpacing);
        const int32 MinPatchY = FloorDiv(Point.Y - CrownRadius, TreeSpacing);
        const int32 MaxPatchY = FloorDiv(Point.Y + CrownRadius, TreeSpacing);
        for (int32 PY = MinPatchY; PY <= MaxPatchY; ++PY)
        {
            for (int32 PX = MinPatchX; PX <= MaxPatchX; ++PX)
            {
                const FIntPoint Patch(PX, PY);
                const FIntPoint Tree = TreeAt(Patch, Seed);
                const int32 Distance = FMath::Max(FMath::Abs(Point.X - Tree.X), FMath::Abs(Point.Y - Tree.Y));
                if (Distance > CrownRadius || !Contains(FIntVector(Tree.X, Tree.Y, 0)))
                {
                    continue;
                }

                const int32 Ground = Tree == Point ? Surface : SurfaceAt(Tree, Seed);
                if (!IsTree(Tree, Patch, Ground, Seed))
                {
                    continue;
                }

                for (int32 Offset = 1; Offset <= 6; ++Offset)
                {
                    const int32 Z = Ground + Offset;
                    if (Z <= Surface)
                    {
                        continue;
                    }
                    if (Distance == 0 && Offset <= 4)
                    {
                        Column[Z] = EAethraBlock::Wood;
                    }
                    else if (Offset >= 3 && (Offset == 4 || Distance < CrownRadius) && Column[Z] == EAethraBlock::Air)
                    {
                        Column[Z] = EAethraBlock::Leaves;
                    }
                }
            }
        }
        if (!bSandy && Point.SizeSquared() > 16 && Column[Surface + 1] == EAethraBlock::Air
            && Hash(Point, Seed, 0x8BC9) % 100 < 3)
        {
            Column[Surface + 1] = EAethraBlock::Leaves;
        }
    }

    void GenerateChunk(FIntPoint Chunk, int32 Seed, TArray<EAethraBlock>& Blocks)
    {
        Blocks.SetNumUninitialized(CellCount);
        const FIntVector Origin = ChunkOrigin(Chunk);
        TStaticArray<EAethraBlock, Height> Column;
        for (int32 Y = 0; Y < ChunkSize; ++Y)
        {
            for (int32 X = 0; X < ChunkSize; ++X)
            {
                GenerateColumn(FIntPoint(Origin.X + X, Origin.Y + Y), Seed, Column);
                for (int32 Z = 0; Z < Height; ++Z)
                {
                    Blocks[Index(FIntVector(X, Y, Z))] = Column[Z];
                }
            }
        }
    }
}
