// Aethra Visual World — Prototype 01. A procedural landscape built from FAethraWorldGen and drawn with
// the Aethra Material Engine (one surface material), HydroGen water, instanced vegetation and an
// AHG settlement (house, round tower, bridge). Started with -AethraVisualWorld[=seed]; the voxel world
// and its saves are not involved. Terrain streams around the viewer in two levels of detail plus a
// coarse horizon; everything regenerates identically from the seed.
#pragma once

#include "Async/Future.h"
#include "GameFramework/Actor.h"
#include "VisualWorld/AethraCaveGen.h"
#include "VisualWorld/AethraFlora.h"
#include "VisualWorld/AethraRuinGen.h"
#include "VisualWorld/AethraHybridGeometry.h"
#include "VisualWorld/AethraWorldGen.h"
#include "AethraVisualWorld.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
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

// Graphics profile (aethra.vw.Quality 0 Low, 1 Medium, 2 High, 3 Ultra). Every profile has the same
// world, biomes, resources, constructions and gameplay; only the cost of drawing it changes.
struct FVwProfile
{
    const TCHAR* Name = TEXT("");
    double NearRadius = 384.0;    // metres of full-detail (2 m) terrain
    double FarRadius = 1600.0;    // metres of mid-detail (8 m) terrain
    float HorizonKm = 20.f;       // radius of the far horizon tiles
    float GroundRadius = 230.f;   // metres of grass and flowers around the viewer
    float GroundDensity = 1.f;    // fraction of ground-flora cells drawn
    float GroundDistance = 1.f;   // scale of the ground-flora draw distances
    float TreeDistance = 1.f;     // scale of the tree draw distances
    bool bFarTreeShadows = true;  // trees in far chunks cast shadows
    bool bGroundShadows = true;   // tall flowers cast shadows
    bool bVolumetricFog = true;
    float ShadowDistance = 200.f; // metres of dynamic sun shadows (cascades)
    int32 Cascades = 3;
    int32 ShadowResolution = 2048;
};

struct FVwHorizonBuild; // the result of an asynchronous horizon tile build (private)

USTRUCT()
struct FVwHorizonTile
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<UProceduralMeshComponent> Mesh;

    FVector2D SinkCenter = FVector2D::ZeroVector; // viewer position the tile was built for (metres)
    double StepM = 64.0;
    bool bDirty = false;
};

struct FVwStats
{
    int32 Generated = 0, Removed = 0, Loaded = 0, Trees = 0, Grass = 0, WaterQuads = 0, Triangles = 0, HorizonTiles = 0, HorizonBuilds = 0;
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
    virtual void BeginDestroy() override;

    static constexpr int32 DemoSeed = 125; // river by the spawn, lakes, sea at 2.2 km, waterfalls at 1.6 km
    static constexpr double ChunkSize[2] = { 128.0, 256.0 }; // metres per chunk, level 0 and 1
    static constexpr double Step[2] = { 2.0, 8.0 };          // metres between vertices
    static constexpr double HorizonTile = 4096.0;            // metres per horizon tile

    // Graphics profiles (see FVwProfile) and the one in use.
    static const FVwProfile& ProfileFor(int32 Quality);
    const FVwProfile& Profile() const { return ProfileFor(Quality); }
    int32 GetQuality() const { return Quality; }
    // Applies a profile: rendering settings now, and the streamed world rebuilt progressively.
    void SetQuality(int32 NewQuality);

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

    // Far horizon: tiles of HorizonTile metres up to Profile().HorizonKm, sampled on worker threads
    // (64 m vertices within 12 km, 128 m beyond) and swapped in when ready. Tiles near the viewer are
    // rebuilt (in the background, the old one stays visible) when the viewer moves far, and any tile can
    // be marked dirty after a large change to the world.
    void UpdateHorizon(const FVector2D& ViewerM, int32 Budget);
    void MarkHorizonDirty(const FVector2D& CenterM, double RadiusM);
    // Waits for every horizon build in flight and applies it (tests and tools).
    void FlushHorizon();
    int32 HorizonTileCount() const { return HorizonTiles.Num(); }
    double HorizonReachM() const; // farthest horizon vertex from the last viewer, metres
    const UProceduralMeshComponent* HorizonMesh(FIntPoint Tile) const;

    // Caves (CaveGen) and ruins (RuinGen). Dig removes rock (or adds it, bFill) in a sphere anywhere:
    // the cave volume is remeshed, the height field opens there, trees above it fall and the edit is
    // saved in CavePath (next to the pieces file).
    FAethraCaveGen* CaveGen() const { return Caves.Get(); }
    FAethraRuinGen* RuinGen() const { return Ruins.Get(); }
    bool Dig(const FVector& CentreCm, float RadiusM, bool bFill = false);
    void UpdateCaves(const FVector2D& ViewerM, int32 Budget);
    int32 CaveBlockCount() const;
    const UProceduralMeshComponent* CaveBlockMesh(const FIntVector& Block) const;
    int32 RuinPieceCount() const { return RuinPieces; }
    FString CavePath;

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
    TMap<FIntPoint, FVwHorizonTile> HorizonTiles;
    TMap<FIntPoint, TFuture<TSharedPtr<FVwHorizonBuild>>> HorizonJobs;
    int32 Quality = 2;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMesh>> SpeciesMeshes;    // species * FFloraRegistry::Variants + variant
    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMesh>> GroundMeshes;     // EGroundSpecies order
    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMesh>> ShapeMeshes;      // EAhgShape order
    UPROPERTY(Transient)
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> PieceInstances;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UProceduralMeshComponent>> Falls;

    TUniquePtr<FAethraWorldGen> World;
    TUniquePtr<FAethraCaveGen> Caves;
    TUniquePtr<FAethraRuinGen> Ruins;
    int32 RuinPieces = 0;
    UPROPERTY(Transient)
    TMap<FIntVector, TObjectPtr<UProceduralMeshComponent>> CaveBlocks; // null = built, empty
    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMesh>> CaveMeshes; // stalactite, stalagmite, crystal
    UPROPERTY(Transient)
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> CaveInstances;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UPointLightComponent>> CaveLights;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UProceduralMeshComponent>> CavePools;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> RuinInstances;
    void BuildCaveBlock(const FIntVector& Block);
    void BuildCaveFeatures();
    void BuildRuins();
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
    void LaunchHorizonTile(FIntPoint Tile, const FVector2D& ViewerM);
    void ApplyHorizonTile(FIntPoint Tile, FVwHorizonBuild& Build);
    void ApplyProfileRendering();
    void BuildFalls();
    void BuildChunk(int32 Level, FIntPoint Coord, const FVector2D& ViewerM);
    void AddFoliage(FVwChunk& Chunk, int32 Level, FIntPoint Coord, const FVector2D& ViewerM, const TArray<FWorldSample>* Samples);
    UHierarchicalInstancedStaticMeshComponent* NewFoliage(UStaticMesh* Mesh, float CullCm, bool bShadows);

    // -AethraVWShots and -AethraBench=<s> in the Visual World.
    bool bShots = false;
    float ShotClock = 0.f;
    int32 ShotStage = 0;
    float BenchSeconds = 0.f;
    float BenchClock = 0.f;
    void RunShots(float DeltaSeconds);
    void RunBench(float DeltaSeconds);
    void PlaceViewer(const FVector2D& EyeM, float EyeHeightM, const FVector2D& LookM, float LookHeightM, bool bUnderground = false);
};
