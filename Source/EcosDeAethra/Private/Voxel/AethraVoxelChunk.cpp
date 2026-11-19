#include "Voxel/AethraVoxelChunk.h"

#include "AethraVoxelWorld.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Voxel/AethraTerrain.h"

using namespace AethraTerrain;

namespace
{
    struct FFace
    {
        FIntVector Normal;
        FIntVector First;
        FIntVector Across;
        FIntVector Up;
    };

    const FFace Faces[] = {
        { {1, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0} },
        { {-1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {0, 0, 1} },
        { {0, 1, 0}, {0, 1, 0}, {1, 0, 0}, {0, 0, 1} },
        { {0, -1, 0}, {0, 0, 0}, {0, 0, 1}, {1, 0, 0} },
        { {0, 0, 1}, {0, 0, 1}, {0, 1, 0}, {1, 0, 0} },
        { {0, 0, -1}, {0, 0, 0}, {1, 0, 0}, {0, 1, 0} }
    };

    FLinearColor ColorFor(EAethraBlock Block, const FIntVector& Cell, const FIntVector& Normal)
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
}

AAethraVoxelChunk::AAethraVoxelChunk()
{
    Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ChunkMesh"));
    SetRootComponent(Mesh);
    Mesh->SetCollisionProfileName(TEXT("BlockAll"));
    Mesh->bUseComplexAsSimpleCollision = true;
    // Initial spawning and block edits require collision to match the mesh immediately.
    Mesh->bUseAsyncCooking = false;
    Mesh->SetCanEverAffectNavigation(false);

    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
        TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
    if (Material.Succeeded())
    {
        Mesh->SetMaterial(0, Material.Object);
    }
}

void AAethraVoxelChunk::Initialize(AAethraVoxelWorld& World, FIntPoint Coordinate)
{
    Chunk = Coordinate;
    GenerateChunk(Chunk, World.Seed, Blocks);
    World.ApplyChunkEdits(Chunk, Blocks);
    Rebuild(World);
}

void AAethraVoxelChunk::SetBlock(int32 BlockIndex, EAethraBlock Block)
{
    Blocks[BlockIndex] = Block;
}

void AAethraVoxelChunk::Rebuild(const AAethraVoxelWorld& World)
{
    const FIntVector Origin = ChunkOrigin(Chunk);
    TStaticArray<EAethraBlock, 4 * ChunkSize * Height> Border;
    for (int32 Offset = 0; Offset < ChunkSize; ++Offset)
    {
        const FIntPoint Edges[] = {
            { Origin.X + ChunkSize, Origin.Y + Offset }, { Origin.X - 1, Origin.Y + Offset },
            { Origin.X + Offset, Origin.Y + ChunkSize }, { Origin.X + Offset, Origin.Y - 1 }
        };
        for (int32 Edge = 0; Edge < 4; ++Edge)
        {
            TStaticArray<EAethraBlock, Height> Column;
            GenerateColumn(Edges[Edge], World.Seed, Column);
            World.ApplyColumnEdits(Edges[Edge], MakeArrayView(Column.GetData(), Height));
            FMemory::Memcpy(&Border[(Edge * ChunkSize + Offset) * Height], Column.GetData(), Height * sizeof(EAethraBlock));
        }
    }

    TArray<FVector> Vertices, Normals;
    TArray<int32> Triangles;
    TArray<FVector2D> UV;
    TArray<FLinearColor> Colors;
    for (int32 Z = 0; Z < Height; ++Z)
    {
        for (int32 Y = 0; Y < ChunkSize; ++Y)
        {
            for (int32 X = 0; X < ChunkSize; ++X)
            {
                const FIntVector Local(X, Y, Z);
                const EAethraBlock Block = Blocks[Index(Local)];
                if (Block == EAethraBlock::Air)
                {
                    continue;
                }

                for (int32 Side = 0; Side < UE_ARRAY_COUNT(Faces); ++Side)
                {
                    const FFace& Face = Faces[Side];
                    const FIntVector Neighbor = Local + Face.Normal;
                    if (Neighbor.Z < 0)
                    {
                        continue;
                    }
                    if (Neighbor.Z < Height)
                    {
                        const bool bOutside = Neighbor.X < 0 || Neighbor.X >= ChunkSize || Neighbor.Y < 0 || Neighbor.Y >= ChunkSize;
                        const int32 EdgeOffset = Side < 2 ? Y : X;
                        const EAethraBlock Adjacent = bOutside
                            ? Border[(Side * ChunkSize + EdgeOffset) * Height + Z]
                            : Blocks[Index(Neighbor)];
                        if (Adjacent != EAethraBlock::Air)
                        {
                            continue;
                        }
                    }

                    const int32 Start = Vertices.Num();
                    const FIntVector Corner = Local + Face.First;
                    const FLinearColor Color = ColorFor(Block, Origin + Local, Face.Normal);
                    Vertices.Append({
                        FVector(Corner) * CellSizeCm,
                        FVector(Corner + Face.Across) * CellSizeCm,
                        FVector(Corner + Face.Across + Face.Up) * CellSizeCm,
                        FVector(Corner + Face.Up) * CellSizeCm
                    });
                    Triangles.Append({ Start, Start + 1, Start + 2, Start, Start + 2, Start + 3 });
                    UV.Append({ {0, 0}, {1, 0}, {1, 1}, {0, 1} });
                    for (int32 Vertex = 0; Vertex < 4; ++Vertex)
                    {
                        Normals.Add(FVector(Face.Normal));
                        Colors.Add(Color);
                    }
                }
            }
        }
    }

    Mesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UV, Colors, {}, true, false);
}
