#if WITH_DEV_AUTOMATION_TESTS

#include "AethraVoxelWorld.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "ProceduralMeshComponent.h"
#include "Voxel/AethraTerrain.h"
#include "Voxel/AethraVoxelChunk.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVoxelChunkSeams, "Aethra.Voxel.ChunkSeams",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    TOptional<FColor> VertexColor(AAethraVoxelChunk& Chunk, FVector Position, FVector Normal)
    {
        auto* Mesh = Chunk.FindComponentByClass<UProceduralMeshComponent>();
        const FProcMeshSection* Section = Mesh ? Mesh->GetProcMeshSection(0) : nullptr;
        const FVector Local = Position - Chunk.GetActorLocation();
        if (Section)
        {
            for (const FProcMeshVertex& Vertex : Section->ProcVertexBuffer)
            {
                if (Vertex.Position.Equals(Local) && Vertex.Normal.Equals(Normal))
                {
                    return Vertex.Color;
                }
            }
        }
        return {};
    }
}

bool FAethraVoxelChunkSeams::RunTest(const FString& Parameters)
{
    using namespace AethraTerrain;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    };

    FActorSpawnParameters Params;
    Params.bDeferConstruction = true;
    auto* Voxels = World->SpawnActor<AAethraVoxelWorld>(Params);
    if (!TestNotNull(TEXT("Voxel world spawned"), Voxels))
    {
        return false;
    }
    Voxels->Seed = static_cast<int32>(FGuid::NewGuid().A & 0x7fffffffu);
    const FString Slot0 = Voxels->SaveSlot(0);
    const FString Slot1 = Voxels->SaveSlot(1);
    if (!TestFalse(TEXT("Test slots are unused"), UGameplayStatics::DoesSaveGameExist(Slot0, 0)
        || UGameplayStatics::DoesSaveGameExist(Slot1, 0)))
    {
        return false;
    }
    ON_SCOPE_EXIT
    {
        UGameplayStatics::DeleteGameInSlot(Slot0, 0);
        UGameplayStatics::DeleteGameInSlot(Slot1, 0);
    };
    Voxels->FinishSpawning(FTransform::Identity);
    Voxels->Initialize();

    constexpr int32 Z = Height - 4; // Clear air above any procedural surface.
    for (const int32 Seam : {ChunkSize, -ChunkSize})
    {
        const int32 Probe = Seam > 0 ? Seam - 1 : Seam;
        const int32 Edit = Seam > 0 ? Seam : Seam - 1;
        const FIntVector Support(Probe, Probe, Z);
        const FIntVector Side(Edit, Probe, Z);
        const FIntVector Corner(Edit, Edit, Z + 1);
        const FIntPoint ProbeChunk = ChunkAt(Support);
        const FIntPoint CornerChunk = ChunkAt(Corner);
        Voxels->LoadChunk(ProbeChunk, false);
        Voxels->LoadChunk(CornerChunk, false);
        Voxels->LoadChunk(ChunkAt(Side), false);
        Voxels->LoadChunk(ChunkAt(FIntVector(Probe, Edit, Z)), false);

        if (!TestTrue(TEXT("Place the probe block"), Voxels->SetBlock(Support, EAethraBlock::Stone)))
        {
            return false;
        }
        auto* Chunk = Voxels->Loaded.FindRef(ProbeChunk).Get();
        if (!TestNotNull(TEXT("Probe chunk loaded"), Chunk))
        {
            return false;
        }

        const FVector Edge(Seam * CellSizeCm, Probe * CellSizeCm, Z * CellSizeCm);
        const FVector SideNormal(Edit > Probe ? 1.0 : -1.0, 0.0, 0.0);
        TestTrue(TEXT("Empty adjacent chunk leaves the side face visible"), VertexColor(*Chunk, Edge, SideNormal).IsSet());
        TestTrue(TEXT("Place across the edge"), Voxels->SetBlock(Side, EAethraBlock::Stone));
        TestFalse(TEXT("Neighbor hides the shared side face"), VertexColor(*Chunk, Edge, SideNormal).IsSet());
        TestTrue(TEXT("Remove across the edge"), Voxels->SetBlock(Side, EAethraBlock::Air));
        TestTrue(TEXT("Neighbor restores the side face when uncovered"), VertexColor(*Chunk, Edge, SideNormal).IsSet());

        const FVector Junction(Seam * CellSizeCm, Seam * CellSizeCm, (Z + 1) * CellSizeCm);
        const auto Open = VertexColor(*Chunk, Junction, FVector::UpVector);
        TestTrue(TEXT("Open corner has a top vertex"), Open.IsSet());
        TestTrue(TEXT("Place the diagonal occluder"), Voxels->SetBlock(Corner, EAethraBlock::Stone));
        const auto Dark = VertexColor(*Chunk, Junction, FVector::UpVector);
        TestTrue(TEXT("The same top vertex darkens across the diagonal seam"),
            Open.IsSet() && Dark.IsSet() && Dark.GetValue().R < Open.GetValue().R);

        Voxels->UpdateStreaming(ProbeChunk + FIntPoint(10, 10));
        TestFalse(TEXT("Moving away unloads the probe"), Voxels->Loaded.Contains(ProbeChunk));
        TestFalse(TEXT("Moving away unloads the diagonal chunk"), Voxels->Loaded.Contains(CornerChunk));
        for (int32 Frame = 0; Frame < 9; ++Frame)
        {
            Voxels->UpdateStreaming(ProbeChunk);
        }
        Chunk = Voxels->Loaded.FindRef(ProbeChunk).Get();
        if (!TestNotNull(TEXT("Returning reloads the probe"), Chunk))
        {
            return false;
        }
        TestTrue(TEXT("Returning reloads the diagonal chunk"), Voxels->Loaded.Contains(CornerChunk));
        TestTrue(TEXT("Reload preserves the edit and corner shading"), Dark.IsSet()
            && VertexColor(*Chunk, Junction, FVector::UpVector).Get(FColor::Transparent) == Dark.GetValue());
        TestTrue(TEXT("Remove the diagonal occluder"), Voxels->SetBlock(Corner, EAethraBlock::Air));
        TestTrue(TEXT("Removing the occluder restores the open corner"), Open.IsSet()
            && VertexColor(*Chunk, Junction, FVector::UpVector).Get(FColor::Transparent) == Open.GetValue());
    }
    return true;
}

#endif
