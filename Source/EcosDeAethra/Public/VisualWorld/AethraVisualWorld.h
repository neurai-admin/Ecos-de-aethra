// Aethra Visual World — Prototype 01. A procedural landscape built from FAethraWorldGen and drawn with
// the Aethra Material Engine (one surface material), HydroGen water, instanced vegetation and an
// AHG settlement (house, round tower, bridge). Started with -AethraVisualWorld[=seed]; the voxel world
// and its saves are not involved. Terrain streams around the viewer in two levels of detail plus a
// coarse horizon; everything regenerates identically from the seed.
#pragma once

#include "GameFramework/Actor.h"
#include "VisualWorld/AethraFlora.h"
#include "VisualWorld/AethraHybridGeometry.h"
#include "VisualWorld/AethraWorldGen.h"
#include "AethraVisualWorld.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
class UStaticMesh;
class UTexture2D;

USTRUCT()
struct FVwChunk
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<UProceduralMeshComponent> Mesh;

    UPROPERTY()
    TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> Foliage;

    int32 Trees = 0;
    int32 Grass = 0;
    bool bCollision = false;
    bool bGrass = false;
};

struct FVwStats
{
    int32 Generated = 0, Removed = 0, Loaded = 0, Trees = 0, Grass = 0, WaterQuads = 0, Triangles = 0;
    double TotalChunkMs = 0, MaxChunkMs = 0, SetupMs = 0, LastFrameMs = 0;
};

UCLASS()
class ECOSDEAETHRA_API AAethraVisualWorld : public AActor
{
    GENERATED_BODY()

public:
    AAethraVisualWorld();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

    static constexpr int32 DemoSeed = 125; // river by the spawn, lakes, sea at 2.2 km, waterfalls at 1.6 km
    static constexpr double ChunkSize[2] = { 128.0, 256.0 }; // metres per chunk, level 0 and 1
    static constexpr double Step[2] = { 2.0, 8.0 };          // metres between vertices
    static constexpr double Radius[2] = { 384.0, 1600.0 };   // streaming radius per level

    // Generates the world for Seed and builds what is around ViewerAt (cm). PiecesFile empty = default.
    void Start(int32 Seed, const FVector& ViewerAt, const FString& PiecesFile = FString());
    // Streams chunks toward the viewer: at most Budget chunk builds this call.
    void UpdateStreaming(const FVector& Viewer, int32 Budget);
    FVector SpawnLocation() const;
    void SetHour(float Hour);

    FAethraWorldGen& Gen() { return *World; }
    FAhgLayer& Pieces() { return Layer; }
    const FVwStats& Stats() const { return Counters; }
    bool HasChunk(int32 Level, FIntPoint Coord) const { return Chunks.Contains(FIntVector(Coord.X, Coord.Y, Level)); }
    int32 ChunkCount(int32 Level) const;
    bool SavePieces() const;
    FString PiecesPath;

    // Trees (flora species system). Each tree has a stable id; felling persists in FloraPath (next to
    // the pieces file) and rebuilds the chunk that held it. The hook for the player's and NPCs' actions.
    void TreesIn(int32 Level, FIntPoint Coord, TArray<FFloraTree>& Out) const;
    bool FellTree(uint64 TreeId);
    const FFloraLayer& FelledTrees() const { return Flora; }
    FString FloraPath;
    // Terrain mesh of a loaded chunk (tests and tools), or null.
    const UProceduralMeshComponent* ChunkMesh(int32 Level, FIntPoint Coord) const;

    // Settlement layout (metres): pad centre, river crossing and the axis across the river.
    FVector2D SettlementCenter = FVector2D::ZeroVector;
    FVector2D BridgeA = FVector2D::ZeroVector, BridgeB = FVector2D::ZeroVector;
    bool bHasSettlement = false;

private:
    UPROPERTY()
    TObjectPtr<UMaterialInterface> SurfaceMaterial;
    UPROPERTY()
    TObjectPtr<UMaterialInterface> FoliageMaterial;
    UPROPERTY()
    TObjectPtr<UMaterialInterface> HydroMaterial;
    UPROPERTY()
    TObjectPtr<UMaterialInterface> FallMaterial;
    UPROPERTY()
    TObjectPtr<UMaterialInterface> MistMaterial;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> Palette;
    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> WaterPalette;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> SurfaceMID;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> FoliageMID;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> HydroMID;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> FallMID;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> MistMID;

    UPROPERTY(Transient)
    TMap<FIntVector, FVwChunk> Chunks; // (x, y, level)
    UPROPERTY(Transient)
    TObjectPtr<UProceduralMeshComponent> Horizon;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMesh>> VegetationMeshes; // EVegetation order (grass, flowers)
    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMesh>> SpeciesMeshes;    // EFloraSpecies order
    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMesh>> ShapeMeshes;      // EAhgShape order
    UPROPERTY(Transient)
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> PieceInstances;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UProceduralMeshComponent>> Falls;

    TUniquePtr<FAethraWorldGen> World;
    FAhgLayer Layer;
    FFloraLayer Flora;
    FVector2D LastViewerM = FVector2D::ZeroVector;
    FVwStats Counters;
    void DropChunk(const FIntVector& Key);
    float StreamClock = 0.f;
    float AppliedHour = -1.f;

    void MakeMaterials();
    void MakeVegetationMeshes();
    void PlanSettlement();
    void BuildSettlement();
    void ShowPieces();
    void BuildHorizon();
    void BuildFalls();
    void BuildChunk(int32 Level, FIntPoint Coord, const FVector2D& ViewerM);
    void AddFoliage(FVwChunk& Chunk, int32 Level, FIntPoint Coord, const FVector2D& ViewerM);
    UHierarchicalInstancedStaticMeshComponent* NewFoliage(UStaticMesh* Mesh, float CullCm, bool bShadows);

    // -AethraVWShots and -AethraBench=<s> in the Visual World.
    bool bShots = false;
    float ShotClock = 0.f;
    int32 ShotStage = 0;
    float BenchSeconds = 0.f;
    float BenchClock = 0.f;
    void RunShots(float DeltaSeconds);
    void RunBench(float DeltaSeconds);
    void PlaceViewer(const FVector2D& EyeM, float EyeHeightM, const FVector2D& LookM, float LookHeightM);
};
