// Aethra Hybrid Geometry (AHG): shapes beyond the cube, as a separate layer over the voxel world.
//
// Representation: a piece is (shape, AME surface, position, yaw, size). Shapes are parametric meshes
// built in C++ at unit size (1 m box: x,y in [-0.5, 0.5], z in [0, 1]) and scaled per instance, so one
// mesh serves every size. Pieces render with instancing (one instanced component per shape, the
// surface id in per-instance data) and the shared M_AethraSurface material.
// Coexistence with voxels: pieces never write voxel blocks. In a voxel world a piece occupies the
// cells its bounds cover and may only be placed where those cells are air and no other piece is.
// Persistence: the layer has its own versioned file (the voxel save format is untouched).
#pragma once

#include "CoreMinimal.h"
#include "VisualWorld/AethraMaterialEngine.h"

class AAethraVoxelWorld;
class UMaterialInterface;
class UStaticMesh;
struct FMeshDescription;

// New shapes go at the end: the id is saved.
enum class EAhgShape : uint8
{
    Block, HalfBlock, Wedge, Ramp, Stairs, Cylinder, Sphere, Arch, Dome, Column, CurvedWall, ConeRoof, GableRoof,
    Count
};

struct FAhgPiece
{
    int32 Id = 0;
    EAhgShape Shape = EAhgShape::Block;
    EAmeSurface Surface = EAmeSurface::CutStone;
    FVector Location = FVector::ZeroVector; // cm, base centre
    float Yaw = 0.f;                       // degrees
    FVector Size = FVector(1, 1, 1);       // metres (x, y, z)

    bool operator==(const FAhgPiece& O) const
    {
        return Id == O.Id && Shape == O.Shape && Surface == O.Surface && Location.Equals(O.Location, .01) && FMath::IsNearlyEqual(Yaw, O.Yaw) && Size.Equals(O.Size, .001);
    }
    FBox Bounds() const; // world-space AABB (cm)
};

class ECOSDEAETHRA_API FAhgShapes
{
public:
    static const TCHAR* Name(EAhgShape Shape);
    // Unit-size mesh of a shape. UV1.x of every vertex is 0 (the instance supplies the surface).
    static void Build(EAhgShape Shape, FMeshDescription& Out);
    // Runtime static mesh from a description (no editor needed). Simple collision from the bounds.
    static UStaticMesh* MakeMesh(UObject* Outer, const FName& Name, FMeshDescription& Description, UMaterialInterface* Material, bool bCollision);
    // Triangle count of a shape's unit mesh (reports).
    static int32 Triangles(EAhgShape Shape);
};

// A low-level triangle builder shared by AHG shapes and the vegetation meshes.
struct ECOSDEAETHRA_API FAhgMeshBuilder
{
    struct FVert { FVector3f P; FVector3f N; FVector2f UV; float Surface = 0.f; FLinearColor Color = FLinearColor::White; };
    TArray<FVert> Verts; // three per triangle (flat or smooth normals as given)
    float Surface = 0.f; // UV1.x for what is added next (an AME id, or 0 = per instance)
    FLinearColor Color = FLinearColor::White;

    void Tri(const FVector3f& A, const FVector3f& B, const FVector3f& C, const FVector3f* NA = nullptr, const FVector3f* NB = nullptr, const FVector3f* NC = nullptr);
    void Quad(const FVector3f& A, const FVector3f& B, const FVector3f& C, const FVector3f& D); // counter-clockwise from outside
    void Box(const FVector3f& Min, const FVector3f& Max);
    void Cylinder(const FVector3f& Base, float Radius, float Height, int32 Sides, bool bCaps, float TopRadius = -1.f);
    void Sphere(const FVector3f& Center, const FVector3f& Radii, int32 Rings, int32 Sides, float Wobble = 0.f, uint32 Seed = 0);
    void Prism(const TArray<FVector2f>& Profile, float Y0, float Y1); // profile in x-z, extruded along y
    void ToMeshDescription(FMeshDescription& Out) const;
};

class ECOSDEAETHRA_API FAhgLayer
{
public:
    static constexpr uint32 Magic = 0x31474841; // "AHG1"
    static constexpr int32 Version = 1;

    // Adds a piece. With a voxel world, every cell its bounds cover must be air and free of other
    // pieces (cells are exclusive, as blocks are). Without one (the Visual World), pieces may overlap to
    // compose a building. Returns the id, or 0 with the reason in OutWhy.
    int32 Add(FAhgPiece Piece, const AAethraVoxelWorld* Voxels = nullptr, FString* OutWhy = nullptr);
    bool Remove(int32 Id);
    const FAhgPiece* Find(int32 Id) const;
    const TArray<FAhgPiece>& GetPieces() const { return Pieces; }
    bool IsCellTaken(const FIntVector& Cell) const { return Occupied.Contains(Cell); }

    bool SaveToFile(const FString& Path) const;
    bool LoadFromFile(const FString& Path);
    void Serialize(FArchive& Ar);

    static void CellsOf(const FAhgPiece& Piece, TArray<FIntVector>& Out);

private:
    TArray<FAhgPiece> Pieces;
    TMap<FIntVector, int32> Occupied;
    int32 NextId = 1;
};
