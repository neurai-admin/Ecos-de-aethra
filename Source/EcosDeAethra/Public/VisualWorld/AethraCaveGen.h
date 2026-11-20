// Aethra CaveGen: caves of the one main world (no other dimensions), for the Visual World.
//
// A cave system is a few primitives in world space (metres): an entrance tunnel from a hillside,
// tunnels and chambers, natural columns. The solid rock is a signed density: positive inside rock,
// negative in air. Air is above the terrain surface (the same WorldGen height the chunks use) and
// inside the carved primitives, with noise on the walls. Around every cave (and every excavation) a
// volume region replaces the height-field terrain: 16 m blocks meshed at 1 m with Surface Nets and flat
// shading - angular, stylised rock, not plasticine - and with collision, so the player walks in.
//
// Edits (digging and filling) are spheres added to the density, persisted in their own file ("CAV1").
// Digging works anywhere: an edit outside a cave opens a new volume region there. The voxel world and
// its saves are not involved.
//
// Categories now: Natural, Enchanted (Aetherite crystals, glowing moss, a pool), Dark (a deep shaft to
// a lightless chamber) and Giant (a huge chamber with natural columns). Declared for later: glacial,
// flooded, volcanic, mineral, overgrown and ruin-bound systems. There are no monsters in caves.
#pragma once

#include "CoreMinimal.h"
#include "VisualWorld/AethraMaterialEngine.h"

class FAethraWorldGen;

enum class ECaveKind : uint8 { Natural, Enchanted, Dark, Giant, Count };

struct FCaveChamber
{
    FVector Center = FVector::ZeroVector; // metres
    FVector Radii = FVector(8, 8, 5);
    float FloorZ = 0.f;                   // lowest air point (water and ruins sit on it)
};

struct FCaveTunnel
{
    FVector A = FVector::ZeroVector, B = FVector::ZeroVector; // metres
    float RadiusA = 3.f, RadiusB = 3.f;
};

struct FCaveColumn
{
    FVector A = FVector::ZeroVector, B = FVector::ZeroVector; // a pillar of rock left standing
    float Radius = 1.5f;
};

struct FCaveSystem
{
    int32 Id = 0;
    bool bDemo = false;
    ECaveKind Kind = ECaveKind::Natural;
    EAmeRock Rock = EAmeRock::Granite;
    FVector Entrance = FVector::ZeroVector; // metres, on the surface
    FVector2D Inward = FVector2D(1, 0);     // horizontal direction of the entrance tunnel
    TArray<FCaveTunnel> Tunnels;
    TArray<FCaveChamber> Chambers;
    TArray<FCaveColumn> Columns;
    FBox Bounds = FBox(ForceInit);          // metres, including the walls
    float PoolLevel = -1e9f;                // water surface in the deepest chamber, if any
};

struct FCaveEdit
{
    FVector Center = FVector::ZeroVector; // metres
    float Radius = 1.f;
    bool bFill = false;                   // false = dig (remove rock), true = fill (add rock)
};

// A meshed volume block: flat-shaded triangles with per-vertex resolved albedo (terrain path of the
// surface material), plus the points where formations go.
struct FCaveMesh
{
    TArray<FVector> Positions; // centimetres
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UV0, UV1, UV2;
    TArray<FLinearColor> Colors;
};

class ECOSDEAETHRA_API FAethraCaveGen
{
public:
    static constexpr double BlockM = 16.0; // volume block edge, metres
    static constexpr double CellM = 1.0;   // meshing resolution, metres
    static constexpr int32 BlockCells = 16;

    // Plans the cave systems of the world: a demonstration cave near Spawn (metres) and a sparse set
    // across the generator's domain. Pure function of the generator (seed).
    FAethraCaveGen(const FAethraWorldGen& Gen, const FVector2D& Spawn);

    const TArray<FCaveSystem>& Systems() const { return CaveSystems; }
    const FCaveSystem* DemoSystem() const { return CaveSystems.FindByPredicate([](const FCaveSystem& S) { return S.bDemo; }); }
    static const TCHAR* KindName(ECaveKind Kind);

    // Signed density at P (metres): > 0 rock, < 0 air. Includes the terrain surface, caves and edits.
    float Density(const FVector& P) const;
    bool IsAir(const FVector& P) const { return Density(P) < 0.f; }

    // Edits. Returns the blocks the edit touches (to rebuild).
    void AddEdit(const FCaveEdit& Edit, TArray<FIntVector>& OutBlocks);
    const TArray<FCaveEdit>& Edits() const { return CaveEdits; }
    bool SaveEdits(const FString& Path) const;
    bool LoadEdits(const FString& Path);
    static constexpr uint32 Magic = 0x31564143; // "CAV1"
    static constexpr int32 Version = 1;

    // Volume blocks (key = block coordinate) that replace the height field: around every system and
    // every edit, from below the deepest air to above the terrain.
    const TSet<FIntVector>& ActiveBlocks() const { return Blocks; }
    // True if the height-field quad around P (metres) must not be drawn (the volume draws it).
    bool CoversColumn(const FVector2D& P, double InsetM) const;
    // Mesh of one block (Surface Nets, 1 m). Seed selects surface patterns; deterministic.
    void MeshBlock(const FIntVector& Block, FCaveMesh& Out) const;
    static FIntVector BlockOf(const FVector& P);
    // True within RadiusM of a cave entrance (trees and props keep the mouth clear).
    bool NearEntrance(const FVector2D& P, double RadiusM) const
    {
        for (const FCaveSystem& S : CaveSystems)
        {
            if (FVector2D::Distance(P, FVector2D(S.Entrance)) < RadiusM)
            {
                return true;
            }
        }
        return false;
    }

    // Formations of a system, deterministic: stalactites (ceiling), stalagmites (floor), crystals.
    struct FFormation { FVector Base; FVector Up; float Size; int32 Type; }; // Type 0 stalactite, 1 stalagmite, 2 crystal
    void Formations(const FCaveSystem& System, TArray<FFormation>& Out) const;

private:
    const FAethraWorldGen& Gen;
    int32 Seed = 1;
    TArray<FCaveSystem> CaveSystems;
    TArray<FCaveEdit> CaveEdits;
    TSet<FIntVector> Blocks;
    TSet<FIntPoint> Columns; // XY of active blocks

    void PlanSystem(int32 Id, const FVector2D& Near, ECaveKind Kind, bool bDemo);
    void ActivateBox(const FBox& Box);
    float Carve(const FVector& P) const; // > 0 inside carved air (metres), from systems
    float Ground(const FVector2D& P) const;
    mutable TMap<FIntPoint, float> GroundCache; // shortcut: grows with use, cleared when large
    mutable FCriticalSection CacheLock;
};
