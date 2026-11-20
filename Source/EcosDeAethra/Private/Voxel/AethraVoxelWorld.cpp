#include "AethraVoxelWorld.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"
#include "Voxel/AethraHorizon.h"
#include "Voxel/AethraStats.h"
#include "Voxel/AethraTerrain.h"
#include "Voxel/AethraVoxelChunk.h"
#include "Voxel/AethraWorldSave.h"

using namespace AethraTerrain;

namespace
{
    constexpr int32 SaveFormat = 1;
    constexpr int32 ChunksPerFrame = 1;
    DEFINE_LOG_CATEGORY_STATIC(LogAethraVoxel, Log, All);

    TAutoConsoleVariable<int32> CVarHorizon(TEXT("aethra.Horizon"), 1,
        TEXT("Distant LOD horizon beyond the voxel radius (0 = off, 1 = on)."));
}

AAethraVoxelWorld::AAethraVoxelWorld()
{
    PrimaryActorTick.bCanEverTick = true;
}

void AAethraVoxelWorld::Initialize()
{
    if (!bInitialized)
    {
        bInitialized = true;
        LoadEdits();
        SetStreamingRadius(StreamingRadius);
    }
}

void AAethraVoxelWorld::SetStreamingRadius(int32 Radius)
{
    StreamingRadius = FMath::Clamp(Radius, 2, 4);
    StreamOffsets.Reset();
    for (int32 Y = -StreamingRadius; Y <= StreamingRadius; ++Y)
    {
        for (int32 X = -StreamingRadius; X <= StreamingRadius; ++X)
        {
            StreamOffsets.Emplace(X, Y);
        }
    }
    StreamOffsets.Sort([](FIntPoint A, FIntPoint B)
    {
        return A.SizeSquared() < B.SizeSquared();
    });
}

FVector AAethraVoxelWorld::PrepareSpawn()
{
    Initialize();
    const FIntPoint Center(0, 0);
    LoadChunk(Center, false); // the player spawns on it this frame
    int32 Top = Height - 1;
    while (Top > 0 && GetBlock(FIntVector(0, 0, Top)) == EAethraBlock::Air)
    {
        --Top;
    }
    return FVector(CellSizeCm * 0.5, CellSizeCm * 0.5, (Top + 1) * CellSizeCm + 98.0);
}

void AAethraVoxelWorld::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Initialize();

    const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
    if (!Pawn)
    {
        return;
    }

    const FIntPoint Center = ChunkAt(CellAt(Pawn->GetActorLocation()));
    const bool bHorizon = CVarHorizon.GetValueOnGameThread() != 0;
    if (bHorizon && !Horizon)
    {
        Horizon = GetWorld()->SpawnActor<AAethraHorizon>();
    }
    if (Horizon)
    {
        Horizon->SetActorHiddenInGame(!bHorizon);
        if (bHorizon)
        {
            Horizon->Update(Seed, Center, StreamingRadius);
        }
    }
    UpdateStreaming(Center);
}

void AAethraVoxelWorld::UpdateStreaming(FIntPoint Center)
{
    for (auto It = Loaded.CreateIterator(); It; ++It)
    {
        const FIntPoint Offset = It.Key() - Center;
        if (FMath::Max(FMath::Abs(Offset.X), FMath::Abs(Offset.Y)) > StreamingRadius)
        {
            It.Value()->Destroy();
            It.RemoveCurrent();
        }
    }

    int32 Remaining = ChunksPerFrame;
    for (const FIntPoint Offset : StreamOffsets)
    {
        const FIntPoint Coordinate = Center + Offset;
        if (!Loaded.Contains(Coordinate) && Contains(ChunkOrigin(Coordinate)))
        {
            LoadChunk(Coordinate);
            if (--Remaining == 0)
            {
                break;
            }
        }
    }
}

void AAethraVoxelWorld::LoadChunk(FIntPoint Coordinate, bool bAsyncCollision)
{
    if (Loaded.Contains(Coordinate))
    {
        return;
    }

    AAethraVoxelChunk* Chunk = GetWorld()->SpawnActor<AAethraVoxelChunk>(
        FVector(ChunkOrigin(Coordinate)) * CellSizeCm, FRotator::ZeroRotator);
    if (Chunk)
    {
        Chunk->Initialize(*this, Coordinate, bAsyncCollision);
        Loaded.Add(Coordinate, Chunk);
    }
}

EAethraBlock AAethraVoxelWorld::GetBlock(FIntVector Cell) const
{
    if (!Contains(Cell))
    {
        return EAethraBlock::Air;
    }

    const FIntPoint Coordinate = ChunkAt(Cell);
    const FIntVector Local = Cell - ChunkOrigin(Coordinate);
    if (const FChunkEdits* Edits = ChunkEdits.Find(Coordinate))
    {
        if (const EAethraBlock* Block = Edits->Find(Index(Local)))
        {
            return *Block;
        }
    }

    TStaticArray<EAethraBlock, Height> Column;
    GenerateColumn(FIntPoint(Cell.X, Cell.Y), Seed, Column);
    return Column[Cell.Z];
}

void AAethraVoxelWorld::GetColumn(FIntPoint Point, TArray<EAethraBlock>& OutColumn) const
{
    OutColumn.SetNumUninitialized(Height);
    if (!Contains(FIntVector(Point.X, Point.Y, 0)))
    {
        for (EAethraBlock& Block : OutColumn)
        {
            Block = EAethraBlock::Air;
        }
        return;
    }
    TStaticArray<EAethraBlock, Height> Column;
    GenerateColumn(Point, Seed, Column);
    for (int32 Z = 0; Z < Height; ++Z)
    {
        OutColumn[Z] = Column[Z];
    }
    ApplyColumnEdits(Point, OutColumn);
}

bool AAethraVoxelWorld::SetBlock(FIntVector Cell, EAethraBlock Block)
{
    return SetBlockBy(Cell, Block, FAethraInstigator());
}

bool AAethraVoxelWorld::SetBlockBy(FIntVector Cell, EAethraBlock Block, const FAethraInstigator& By)
{
    Initialize();
    if (!Contains(Cell) || Cell.Z == 0 || !IsKnownBlock(Block) || !bSaveWritable || bNotifying)
    {
        return false;
    }

    const EAethraBlock Before = GetBlock(Cell);
    if (Block == Before)
    {
        return false;
    }

    const FIntPoint Coordinate = ChunkAt(Cell);
    const FIntVector Local = Cell - ChunkOrigin(Coordinate);
    const int32 BlockIndex = Index(Local);
    TStaticArray<EAethraBlock, Height> Column;
    GenerateColumn(FIntPoint(Cell.X, Cell.Y), Seed, Column);

    FChunkEdits& Edits = ChunkEdits.FindOrAdd(Coordinate);
    const EAethraBlock* Existing = Edits.Find(BlockIndex);
    const TOptional<EAethraBlock> Previous = Existing ? TOptional<EAethraBlock>(*Existing) : TOptional<EAethraBlock>();
    if (Block == Column[Cell.Z])
    {
        Edits.Remove(BlockIndex);
    }
    else
    {
        Edits.Add(BlockIndex, Block);
    }

    if (!SaveEdits())
    {
        if (Previous.IsSet())
        {
            Edits.Add(BlockIndex, Previous.GetValue());
        }
        else
        {
            Edits.Remove(BlockIndex);
        }
        if (Edits.IsEmpty())
        {
            ChunkEdits.Remove(Coordinate);
        }
        return false;
    }

    if (Edits.IsEmpty())
    {
        ChunkEdits.Remove(Coordinate);
    }
    if (const auto* Chunk = Loaded.Find(Coordinate))
    {
        (*Chunk)->SetBlock(BlockIndex, Block);
    }

    // A corner column contributes to vertex AO in the diagonal chunk as well as both side chunks.
    const int32 MinX = Local.X == 0 ? -1 : 0;
    const int32 MaxX = Local.X == ChunkSize - 1 ? 1 : 0;
    const int32 MinY = Local.Y == 0 ? -1 : 0;
    const int32 MaxY = Local.Y == ChunkSize - 1 ? 1 : 0;
    for (int32 Y = MinY; Y <= MaxY; ++Y)
    {
        for (int32 X = MinX; X <= MaxX; ++X)
        {
            if (const auto* Chunk = Loaded.Find(Coordinate + FIntPoint(X, Y)))
            {
                (*Chunk)->Rebuild(*this);
            }
        }
    }

    FAethraBlockChange Change;
    Change.Cell = Cell;
    Change.Before = Before;
    Change.After = Block;
    Change.Instigator = By;
    TGuardValue<bool> Guard(bNotifying, true);
    OnBlockChanged.Broadcast(Change);
    return true;
}

void AAethraVoxelWorld::ApplyChunkEdits(FIntPoint Coordinate, TArray<EAethraBlock>& Blocks) const
{
    if (const FChunkEdits* Edits = ChunkEdits.Find(Coordinate))
    {
        for (const auto& Edit : *Edits)
        {
            Blocks[Edit.Key] = Edit.Value;
        }
    }
}

void AAethraVoxelWorld::ApplyColumnEdits(FIntPoint Point, TArrayView<EAethraBlock> Column) const
{
    const FIntVector Bottom(Point.X, Point.Y, 0);
    const FIntPoint Coordinate = ChunkAt(Bottom);
    if (const FChunkEdits* Edits = ChunkEdits.Find(Coordinate))
    {
        const int32 BaseIndex = Index(Bottom - ChunkOrigin(Coordinate));
        for (int32 Z = 0; Z < Column.Num(); ++Z)
        {
            if (const EAethraBlock* Block = Edits->Find(BaseIndex + ChunkSize * ChunkSize * Z))
            {
                Column[Z] = *Block;
            }
        }
    }
}

FString AAethraVoxelWorld::SaveSlot(int32 Slot) const
{
    return FString::Printf(TEXT("AethraWorld_%d_%d"), Seed, Slot);
}

bool AAethraVoxelWorld::SaveEdits()
{
    CSV_SCOPED_TIMING_STAT(Aethra, SaveEdits);
    if (Revision == MAX_int64)
    {
        return false;
    }

    auto* Save = NewObject<UAethraWorldSave>(this);
    Save->Format = SaveFormat;
    Save->Seed = Seed;
    Save->Revision = Revision + 1;
    for (const auto& Chunk : ChunkEdits)
    {
        const FIntVector Origin = ChunkOrigin(Chunk.Key);
        for (const auto& Edit : Chunk.Value)
        {
            const int32 Z = Edit.Key / (ChunkSize * ChunkSize);
            const int32 Y = (Edit.Key / ChunkSize) % ChunkSize;
            FAethraSavedBlock& Entry = Save->Edits.AddDefaulted_GetRef();
            Entry.Cell = Origin + FIntVector(Edit.Key % ChunkSize, Y, Z);
            Entry.Block = Edit.Value;
        }
    }

    if (!UGameplayStatics::SaveGameToSlot(Save, SaveSlot(static_cast<int32>(Save->Revision % 2)), 0))
    {
        UE_LOG(LogAethraVoxel, Error, TEXT("Could not save voxel revision %lld."), Save->Revision);
        return false;
    }

    Revision = Save->Revision;
    return true;
}

void AAethraVoxelWorld::LoadEdits()
{
    bool bSlotExists = false;
    for (int32 Slot = 0; Slot < 2; ++Slot)
    {
        const FString Name = SaveSlot(Slot);
        if (!UGameplayStatics::DoesSaveGameExist(Name, 0))
        {
            continue;
        }
        bSlotExists = true;

        const auto* Save = Cast<UAethraWorldSave>(UGameplayStatics::LoadGameFromSlot(Name, 0));
        if (!Save || Save->Seed != Seed || Save->Revision <= 0 || Save->Revision % 2 != Slot)
        {
            continue;
        }
        if (Save->Format > SaveFormat)
        {
            bSaveWritable = false;
        }
        if (Save->Format != SaveFormat || Save->Revision <= Revision)
        {
            continue;
        }

        TMap<FIntPoint, FChunkEdits> Candidate;
        bool bValid = true;
        for (const FAethraSavedBlock& Entry : Save->Edits)
        {
            if (!Contains(Entry.Cell) || Entry.Cell.Z == 0 || !IsKnownBlock(Entry.Block))
            {
                bValid = false;
                break;
            }
            const FIntPoint Coordinate = ChunkAt(Entry.Cell);
            const int32 BlockIndex = Index(Entry.Cell - ChunkOrigin(Coordinate));
            FChunkEdits& Edits = Candidate.FindOrAdd(Coordinate);
            if (Edits.Contains(BlockIndex))
            {
                bValid = false;
                break;
            }
            Edits.Add(BlockIndex, Entry.Block);
        }
        if (bValid)
        {
            ChunkEdits = MoveTemp(Candidate);
            Revision = Save->Revision;
        }
    }

    if (Revision > 0)
    {
        int32 Count = 0;
        for (const auto& Chunk : ChunkEdits)
        {
            Count += Chunk.Value.Num();
        }
        UE_LOG(LogAethraVoxel, Display, TEXT("Loaded %d voxel edits (revision %lld) for seed %d."), Count, Revision, Seed);
    }
    if (bSlotExists && Revision == 0)
    {
        bSaveWritable = false;
        UE_LOG(LogAethraVoxel, Warning, TEXT("No valid voxel save could be loaded for seed %d."), Seed);
    }
}
