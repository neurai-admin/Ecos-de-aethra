#include "VisualWorld/AethraHybridGeometry.h"

#include "AethraVoxelWorld.h"
#include "Engine/StaticMesh.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "PhysicsEngine/BodySetup.h"
#include "Misc/FileHelper.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "StaticMeshAttributes.h"
#include "Voxel/AethraTerrain.h"

FBox FAhgPiece::Bounds() const
{
    const FTransform T(FRotator(0.f, Yaw, 0.f), Location, Size);
    return FBox(FVector(-50, -50, 0), FVector(50, 50, 100)).TransformBy(T);
}

// ---------------------------------------------------------------- builder

void FAhgMeshBuilder::Tri(const FVector3f& A, const FVector3f& B, const FVector3f& C, const FVector3f* NA, const FVector3f* NB, const FVector3f* NC)
{
    const FVector3f Face = FVector3f::CrossProduct(B - A, C - A).GetSafeNormal();
    auto Make = [this](const FVector3f& P, const FVector3f& N)
    {
        FVert V;
        V.P = P;
        V.N = N;
        // Planar UVs in metres from the dominant axis (the material uses world position anyway).
        const FVector3f Abs(FMath::Abs(N.X), FMath::Abs(N.Y), FMath::Abs(N.Z));
        V.UV = Abs.Z >= Abs.X && Abs.Z >= Abs.Y ? FVector2f(P.X, P.Y) / 100.f : Abs.X >= Abs.Y ? FVector2f(P.Y, P.Z) / 100.f : FVector2f(P.X, P.Z) / 100.f;
        V.Surface = Surface;
        V.Color = Color;
        return V;
    };
    Verts.Add(Make(A, NA ? *NA : Face));
    Verts.Add(Make(B, NB ? *NB : Face));
    Verts.Add(Make(C, NC ? *NC : Face));
}

void FAhgMeshBuilder::Quad(const FVector3f& A, const FVector3f& B, const FVector3f& C, const FVector3f& D)
{
    Tri(A, B, C);
    Tri(A, C, D);
}

void FAhgMeshBuilder::Box(const FVector3f& Lo, const FVector3f& Hi)
{
    const FVector3f P[8] = {
        { Lo.X, Lo.Y, Lo.Z }, { Hi.X, Lo.Y, Lo.Z }, { Hi.X, Hi.Y, Lo.Z }, { Lo.X, Hi.Y, Lo.Z },
        { Lo.X, Lo.Y, Hi.Z }, { Hi.X, Lo.Y, Hi.Z }, { Hi.X, Hi.Y, Hi.Z }, { Lo.X, Hi.Y, Hi.Z } };
    Quad(P[4], P[5], P[6], P[7]); // top
    Quad(P[3], P[2], P[1], P[0]); // bottom
    Quad(P[0], P[1], P[5], P[4]); // -y
    Quad(P[2], P[3], P[7], P[6]); // +y
    Quad(P[1], P[2], P[6], P[5]); // +x
    Quad(P[3], P[0], P[4], P[7]); // -x
}

void FAhgMeshBuilder::Cylinder(const FVector3f& Base, float Radius, float Height, int32 Sides, bool bCaps, float TopRadius)
{
    const float R1 = TopRadius < 0.f ? Radius : TopRadius;
    const float Slope = (Radius - R1) / FMath::Max(Height, 1e-3f);
    for (int32 I = 0; I < Sides; ++I)
    {
        const float A0 = 2.f * PI * I / Sides, A1 = 2.f * PI * (I + 1) / Sides;
        const FVector3f D0(FMath::Cos(A0), FMath::Sin(A0), 0.f), D1(FMath::Cos(A1), FMath::Sin(A1), 0.f);
        const FVector3f B0 = Base + D0 * Radius, B1 = Base + D1 * Radius;
        const FVector3f T0 = Base + D0 * R1 + FVector3f(0, 0, Height), T1 = Base + D1 * R1 + FVector3f(0, 0, Height);
        const FVector3f N0 = (D0 + FVector3f(0, 0, Slope)).GetSafeNormal(), N1 = (D1 + FVector3f(0, 0, Slope)).GetSafeNormal();
        if (R1 > 1e-3f)
        {
            Tri(B0, B1, T1, &N0, &N1, &N1);
            Tri(B0, T1, T0, &N0, &N1, &N0);
        }
        else
        {
            Tri(B0, B1, T0, &N0, &N1, &N0);
        }
        if (bCaps)
        {
            Tri(Base, B1, B0);
            if (R1 > 1e-3f)
            {
                Tri(Base + FVector3f(0, 0, Height), T0, T1);
            }
        }
    }
}

void FAhgMeshBuilder::Sphere(const FVector3f& Center, const FVector3f& Radii, int32 Rings, int32 Sides, float Wobble, uint32 Seed)
{
    auto Point = [&](int32 Ring, int32 Side, FVector3f& OutN)
    {
        const float V = PI * Ring / Rings, U = 2.f * PI * (Side % Sides) / Sides;
        OutN = FVector3f(FMath::Sin(V) * FMath::Cos(U), FMath::Sin(V) * FMath::Sin(U), FMath::Cos(V));
        float Scale = 1.f;
        if (Wobble > 0.f && Ring > 0 && Ring < Rings)
        {
            uint32 H = Seed ^ (static_cast<uint32>(Ring) * 73856093U) ^ (static_cast<uint32>(Side % Sides) * 19349663U);
            H ^= H >> 13;
            H *= 0x5bd1e995U;
            Scale += Wobble * (((H >> 8) & 1023) / 1023.f - .5f);
        }
        return Center + OutN * Radii * Scale;
    };
    for (int32 R = 0; R < Rings; ++R)
    {
        for (int32 S = 0; S < Sides; ++S)
        {
            FVector3f N00, N01, N10, N11;
            const FVector3f P00 = Point(R, S, N00), P01 = Point(R, S + 1, N01), P10 = Point(R + 1, S, N10), P11 = Point(R + 1, S + 1, N11);
            if (R > 0)
            {
                Tri(P00, P01, P11, &N00, &N01, &N11);
            }
            if (R < Rings - 1)
            {
                Tri(P00, P11, P10, &N00, &N11, &N10);
            }
        }
    }
}

void FAhgMeshBuilder::Prism(const TArray<FVector2f>& Profile, float Y0, float Y1)
{
    FVector2f Centroid(0, 0);
    for (const FVector2f& P : Profile)
    {
        Centroid += P;
    }
    Centroid /= static_cast<float>(Profile.Num());
    const FVector3f Front(0, -1, 0), Back(0, 1, 0);
    for (int32 I = 1; I + 1 < Profile.Num(); ++I) // fan: profile must be convex
    {
        const FVector3f A(Profile[0].X, Y0, Profile[0].Y), B(Profile[I].X, Y0, Profile[I].Y), C(Profile[I + 1].X, Y0, Profile[I + 1].Y);
        Tri(A, B, C, &Front, &Front, &Front);
        const FVector3f A1(A.X, Y1, A.Z), B1(B.X, Y1, B.Z), C1(C.X, Y1, C.Z);
        Tri(A1, C1, B1, &Back, &Back, &Back);
    }
    for (int32 I = 0; I < Profile.Num(); ++I)
    {
        const FVector2f P = Profile[I], Q = Profile[(I + 1) % Profile.Num()];
        FVector2f N2(Q.Y - P.Y, -(Q.X - P.X));
        if (FVector2f::DotProduct(N2, (P + Q) * .5f - Centroid) < 0.f)
        {
            N2 = -N2;
        }
        const FVector3f N = FVector3f(N2.X, 0.f, N2.Y).GetSafeNormal();
        const FVector3f A(P.X, Y0, P.Y), B(Q.X, Y0, Q.Y), C(Q.X, Y1, Q.Y), D(P.X, Y1, P.Y);
        Tri(A, B, C, &N, &N, &N);
        Tri(A, C, D, &N, &N, &N);
    }
}

void FAhgMeshBuilder::ToMeshDescription(FMeshDescription& Out) const
{
    FStaticMeshAttributes Attributes(Out);
    Attributes.Register();
    TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
    TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
    TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
    TVertexInstanceAttributesRef<FVector4f> Colors = Attributes.GetVertexInstanceColors();
    // Three channels: the surface material reads UV2.x as the terrain flag, and a mesh without that
    // channel would hand it the last one present (UV1, the surface id) and be drawn as terrain.
    UVs.SetNumChannels(3);
    const FPolygonGroupID Group = Out.CreatePolygonGroup();
    Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Main");
    Out.ReserveNewVertices(Verts.Num());
    Out.ReserveNewVertexInstances(Verts.Num());
    Out.ReserveNewTriangles(Verts.Num() / 3);
    for (int32 I = 0; I + 2 < Verts.Num(); I += 3)
    {
        TArray<FVertexInstanceID, TInlineAllocator<3>> Corners;
        for (int32 K = 0; K < 3; ++K)
        {
            const FVert& V = Verts[I + K];
            const FVertexID Vertex = Out.CreateVertex();
            Positions[Vertex] = V.P;
            const FVertexInstanceID Instance = Out.CreateVertexInstance(Vertex);
            Normals[Instance] = V.N;
            UVs.Set(Instance, 0, V.UV);
            UVs.Set(Instance, 1, FVector2f(V.Surface, 0.f));
            UVs.Set(Instance, 2, FVector2f(0.f, 0.f));
            Colors[Instance] = FVector4f(V.Color.R, V.Color.G, V.Color.B, V.Color.A);
            Corners.Add(Instance);
        }
        Out.CreateTriangle(Group, Corners);
    }
}

// ---------------------------------------------------------------- shapes

const TCHAR* FAhgShapes::Name(EAhgShape Shape)
{
    static const TCHAR* Names[] = { TEXT("Bloco"), TEXT("Meio-bloco"), TEXT("Cunha"), TEXT("Rampa"), TEXT("Escada"), TEXT("Cilindro"), TEXT("Esfera"),
        TEXT("Arco"), TEXT("Cúpula"), TEXT("Coluna"), TEXT("Parede curva"), TEXT("Telhado cônico"), TEXT("Telhado de duas águas") };
    static_assert(UE_ARRAY_COUNT(Names) == static_cast<int32>(EAhgShape::Count));
    return Names[FMath::Clamp(static_cast<int32>(Shape), 0, UE_ARRAY_COUNT(Names) - 1)];
}

namespace
{
void BuildShape(EAhgShape Shape, FAhgMeshBuilder& B)
{
    switch (Shape)
    {
    case EAhgShape::Block: B.Box(FVector3f(-50, -50, 0), FVector3f(50, 50, 100)); break;
    case EAhgShape::HalfBlock: B.Box(FVector3f(-50, -50, 0), FVector3f(50, 50, 50)); break;
    case EAhgShape::Wedge: B.Prism({ FVector2f(-50, 0), FVector2f(50, 0), FVector2f(-50, 100) }, -50, 50); break;
    case EAhgShape::Ramp: B.Prism({ FVector2f(-50, 0), FVector2f(50, 0), FVector2f(50, 8), FVector2f(-50, 50) }, -50, 50); break;
    case EAhgShape::Stairs:
        for (int32 K = 0; K < 4; ++K)
        {
            B.Box(FVector3f(-50.f + 25.f * K, -50, 25.f * K), FVector3f(50, 50, 25.f * (K + 1)));
        }
        break;
    case EAhgShape::Cylinder: B.Cylinder(FVector3f(0, 0, 0), 50, 100, 24, true); break;
    case EAhgShape::Sphere: B.Sphere(FVector3f(0, 0, 50), FVector3f(50, 50, 50), 12, 20); break;
    case EAhgShape::Dome:
    {
        // Upper half of a sphere sitting on z = 0, closed underneath.
        FAhgMeshBuilder Full;
        Full.Sphere(FVector3f(0, 0, 0), FVector3f(50, 50, 100), 16, 24);
        for (int32 I = 0; I + 2 < Full.Verts.Num(); I += 3)
        {
            if (Full.Verts[I].P.Z >= -.01f && Full.Verts[I + 1].P.Z >= -.01f && Full.Verts[I + 2].P.Z >= -.01f)
            {
                B.Verts.Append(&Full.Verts[I], 3);
            }
        }
        const FVector3f Down(0, 0, -1);
        for (int32 I = 0; I < 24; ++I) // base disc
        {
            const float A0 = 2.f * PI * I / 24, A1 = 2.f * PI * (I + 1) / 24;
            B.Tri(FVector3f(0, 0, 0), FVector3f(50 * FMath::Cos(A1), 50 * FMath::Sin(A1), 0), FVector3f(50 * FMath::Cos(A0), 50 * FMath::Sin(A0), 0), &Down, &Down, &Down);
        }
        break;
    }
    case EAhgShape::Column:
        B.Box(FVector3f(-45, -45, 0), FVector3f(45, 45, 12));
        B.Cylinder(FVector3f(0, 0, 12), 32, 76, 16, false, 28);
        B.Box(FVector3f(-45, -45, 88), FVector3f(45, 45, 100));
        break;
    case EAhgShape::ConeRoof:
        B.Cylinder(FVector3f(0, 0, 0), 50, 100, 24, true, 0.f);
        break;
    case EAhgShape::GableRoof:
        B.Prism({ FVector2f(-50, 0), FVector2f(50, 0), FVector2f(0, 100) }, -50, 50);
        break;
    case EAhgShape::Arch:
    {
        // Wall with a round-headed opening: two piers and a crown around a semicircle.
        const float W = 30.f, Spring = 40.f; // opening half-width and the height where the curve starts
        B.Box(FVector3f(-50, -50, 0), FVector3f(-W, 50, Spring));
        B.Box(FVector3f(W, -50, 0), FVector3f(50, 50, Spring));
        TArray<float> Angles;
        for (int32 I = 0; I <= 16; ++I)
        {
            Angles.Add(PI * I / 16.f);
        }
        Angles.Add(FMath::Atan2(100.f - Spring, 50.f));
        Angles.Add(PI - FMath::Atan2(100.f - Spring, 50.f));
        Angles.Sort();
        auto Outer = [&](float A)
        {
            const FVector2f D(FMath::Cos(A), FMath::Sin(A));
            const float TX = FMath::Abs(D.X) > 1e-4f ? 50.f / FMath::Abs(D.X) : 1e9f;
            const float TZ = D.Y > 1e-4f ? (100.f - Spring) / D.Y : 1e9f;
            const float T = FMath::Min(TX, TZ);
            return FVector2f(D.X * T, Spring + D.Y * T);
        };
        for (int32 I = 0; I + 1 < Angles.Num(); ++I)
        {
            const float A0 = Angles[I], A1 = Angles[I + 1];
            const FVector2f I0(W * FMath::Cos(A0), Spring + W * FMath::Sin(A0)), I1(W * FMath::Cos(A1), Spring + W * FMath::Sin(A1));
            const FVector2f O0 = Outer(A0), O1 = Outer(A1);
            for (const float Y : { -50.f, 50.f })
            {
                const FVector3f N(0, Y < 0.f ? -1.f : 1.f, 0);
                const FVector3f P0(I0.X, Y, I0.Y), P1(I1.X, Y, I1.Y), Q0(O0.X, Y, O0.Y), Q1(O1.X, Y, O1.Y);
                B.Tri(P0, P1, Q1, &N, &N, &N);
                B.Tri(P0, Q1, Q0, &N, &N, &N);
            }
            // Intrados (the underside of the curve), facing the opening's centre.
            const FVector3f N0(-FMath::Cos(A0), 0, -FMath::Sin(A0)), N1(-FMath::Cos(A1), 0, -FMath::Sin(A1));
            const FVector3f F0(I0.X, -50, I0.Y), F1(I1.X, -50, I1.Y), K0(I0.X, 50, I0.Y), K1(I1.X, 50, I1.Y);
            B.Tri(F0, K0, K1, &N0, &N0, &N1);
            B.Tri(F0, K1, F1, &N0, &N1, &N1);
        }
        const FVector3f Up(0, 0, 1), Left(-1, 0, 0), Right(1, 0, 0);
        B.Tri(FVector3f(-50, -50, 100), FVector3f(50, -50, 100), FVector3f(50, 50, 100), &Up, &Up, &Up);
        B.Tri(FVector3f(-50, -50, 100), FVector3f(50, 50, 100), FVector3f(-50, 50, 100), &Up, &Up, &Up);
        B.Tri(FVector3f(-50, -50, Spring), FVector3f(-50, 50, Spring), FVector3f(-50, 50, 100), &Left, &Left, &Left);
        B.Tri(FVector3f(-50, -50, Spring), FVector3f(-50, 50, 100), FVector3f(-50, -50, 100), &Left, &Left, &Left);
        B.Tri(FVector3f(50, -50, Spring), FVector3f(50, 50, 100), FVector3f(50, 50, Spring), &Right, &Right, &Right);
        B.Tri(FVector3f(50, -50, Spring), FVector3f(50, -50, 100), FVector3f(50, 50, 100), &Right, &Right, &Right);
        break;
    }
    case EAhgShape::CurvedWall:
    {
        // A quarter ring centred on the (-50, -50) corner: outer radius 100, inner 85.
        const FVector3f C(-50, -50, 0);
        const int32 Steps = 12;
        for (int32 I = 0; I < Steps; ++I)
        {
            const float A0 = .5f * PI * I / Steps, A1 = .5f * PI * (I + 1) / Steps;
            const FVector3f D0(FMath::Cos(A0), FMath::Sin(A0), 0), D1(FMath::Cos(A1), FMath::Sin(A1), 0);
            const FVector3f O0 = C + D0 * 100.f, O1 = C + D1 * 100.f, I0 = C + D0 * 85.f, I1 = C + D1 * 85.f;
            const FVector3f H(0, 0, 100);
            const FVector3f NI0 = -D0, NI1 = -D1, Up(0, 0, 1), Dn(0, 0, -1);
            B.Tri(O0, O1, O1 + H, &D0, &D1, &D1);
            B.Tri(O0, O1 + H, O0 + H, &D0, &D1, &D0);
            B.Tri(I1, I0, I0 + H, &NI1, &NI0, &NI0);
            B.Tri(I1, I0 + H, I1 + H, &NI1, &NI0, &NI1);
            B.Tri(O0 + H, O1 + H, I1 + H, &Up, &Up, &Up);
            B.Tri(O0 + H, I1 + H, I0 + H, &Up, &Up, &Up);
            B.Tri(O1, O0, I0, &Dn, &Dn, &Dn);
            B.Tri(O1, I0, I1, &Dn, &Dn, &Dn);
        }
        B.Quad(C + FVector3f(85, 0, 0), C + FVector3f(100, 0, 0), C + FVector3f(100, 0, 100), C + FVector3f(85, 0, 100));
        B.Quad(C + FVector3f(0, 100, 0), C + FVector3f(0, 85, 0), C + FVector3f(0, 85, 100), C + FVector3f(0, 100, 100));
        break;
    }
    default:
        B.Box(FVector3f(-50, -50, 0), FVector3f(50, 50, 100));
        break;
    }
}
}

void FAhgShapes::Build(EAhgShape Shape, FMeshDescription& Out)
{
    FAhgMeshBuilder B;
    BuildShape(Shape, B);
    B.ToMeshDescription(Out);
}

int32 FAhgShapes::Triangles(EAhgShape Shape)
{
    FAhgMeshBuilder B;
    BuildShape(Shape, B);
    return B.Verts.Num() / 3;
}

UStaticMesh* FAhgShapes::MakeMesh(UObject* Outer, const FName& Name, FMeshDescription& Description, UMaterialInterface* Material, bool bCollision)
{
    UStaticMesh* Mesh = NewObject<UStaticMesh>(Outer, Name, RF_Transient);
    Mesh->GetStaticMaterials().Add(FStaticMaterial(Material, TEXT("Main"), TEXT("Main")));
    UStaticMesh::FBuildMeshDescriptionsParams Params;
    Params.bFastBuild = true;
    Params.bBuildSimpleCollision = bCollision;
    Params.bCommitMeshDescription = false;
    Params.bAllowCpuAccess = bCollision; // physics reads the 13 small AHG meshes on the CPU; vegetation stays GPU-only
    TArray<const FMeshDescription*> Lods = { &Description };
    Mesh->BuildFromMeshDescriptions(Lods, Params);
    if (bCollision && Mesh->GetBodySetup())
    {
        // The simple box is the collision for every query: no triangle data is kept on the CPU.
        Mesh->GetBodySetup()->CollisionTraceFlag = CTF_UseSimpleAsComplex;
    }
    return Mesh;
}

// ---------------------------------------------------------------- layer

void FAhgLayer::CellsOf(const FAhgPiece& Piece, TArray<FIntVector>& Out)
{
    Out.Reset();
    const FBox Box = Piece.Bounds().ExpandBy(-1.0); // touching faces do not overlap
    const FIntVector Lo = AethraTerrain::CellAt(Box.Min), Hi = AethraTerrain::CellAt(Box.Max);
    for (int32 Z = Lo.Z; Z <= Hi.Z; ++Z)
    {
        for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
        {
            for (int32 X = Lo.X; X <= Hi.X; ++X)
            {
                Out.Add(FIntVector(X, Y, Z));
            }
        }
    }
}

int32 FAhgLayer::Add(FAhgPiece Piece, const AAethraVoxelWorld* Voxels, FString* OutWhy)
{
    auto Refuse = [OutWhy](const FString& Why)
    {
        if (OutWhy)
        {
            *OutWhy = Why;
        }
        return 0;
    };
    if (Piece.Shape >= EAhgShape::Count || Piece.Surface >= EAmeSurface::Count || Piece.Size.GetMin() <= 0.f || Piece.Size.GetMax() > 64.f)
    {
        return Refuse(TEXT("peça inválida"));
    }
    TArray<FIntVector> Cells;
    CellsOf(Piece, Cells);
    for (const FIntVector& Cell : Cells)
    {
        if (Voxels && Occupied.Contains(Cell))
        {
            return Refuse(FString::Printf(TEXT("célula (%d, %d, %d) já ocupada por outra peça"), Cell.X, Cell.Y, Cell.Z));
        }
        if (Voxels && Voxels->GetBlock(Cell) != EAethraBlock::Air && !AethraTerrain::IsPlant(Voxels->GetBlock(Cell)))
        {
            return Refuse(FString::Printf(TEXT("célula (%d, %d, %d) ocupada por um bloco voxel"), Cell.X, Cell.Y, Cell.Z));
        }
    }
    Piece.Id = NextId++;
    for (const FIntVector& Cell : Cells)
    {
        if (!Occupied.Contains(Cell))
        {
            Occupied.Add(Cell, Piece.Id);
        }
    }
    Pieces.Add(Piece);
    return Piece.Id;
}

bool FAhgLayer::Remove(int32 Id)
{
    const int32 Index = Pieces.IndexOfByPredicate([Id](const FAhgPiece& P) { return P.Id == Id; });
    if (Index == INDEX_NONE)
    {
        return false;
    }
    TArray<FIntVector> Cells;
    CellsOf(Pieces[Index], Cells);
    for (const FIntVector& Cell : Cells)
    {
        if (Occupied.FindRef(Cell) == Id)
        {
            Occupied.Remove(Cell);
        }
    }
    Pieces.RemoveAt(Index);
    return true;
}

const FAhgPiece* FAhgLayer::Find(int32 Id) const
{
    return Pieces.FindByPredicate([Id](const FAhgPiece& P) { return P.Id == Id; });
}

void FAhgLayer::Serialize(FArchive& Ar)
{
    uint32 M = Magic;
    int32 V = Version;
    Ar << M << V;
    if (Ar.IsLoading() && (M != Magic || V < 1 || V > Version))
    {
        Ar.SetError();
        return;
    }
    Ar << NextId;
    int32 Count = Pieces.Num();
    Ar << Count;
    if (Ar.IsLoading())
    {
        if (Count < 0 || Count > 1 << 20)
        {
            Ar.SetError();
            return;
        }
        Pieces.SetNum(Count);
    }
    for (FAhgPiece& P : Pieces)
    {
        uint8 Shape = static_cast<uint8>(P.Shape), Surface = static_cast<uint8>(P.Surface);
        Ar << P.Id << Shape << Surface << P.Location << P.Yaw << P.Size;
        if (Ar.IsLoading() && (Shape >= static_cast<uint8>(EAhgShape::Count) || Surface >= static_cast<uint8>(EAmeSurface::Count)))
        {
            Ar.SetError();
            return;
        }
        P.Shape = static_cast<EAhgShape>(Shape);
        P.Surface = static_cast<EAmeSurface>(Surface);
    }
    if (Ar.IsLoading())
    {
        Occupied.Reset();
        TArray<FIntVector> Cells;
        for (const FAhgPiece& P : Pieces)
        {
            CellsOf(P, Cells);
            for (const FIntVector& Cell : Cells)
            {
                Occupied.Add(Cell, P.Id);
            }
        }
    }
}

bool FAhgLayer::SaveToFile(const FString& Path) const
{
    TArray<uint8> Bytes;
    FMemoryWriter Writer(Bytes);
    const_cast<FAhgLayer*>(this)->Serialize(Writer);
    const FString Temp = Path + TEXT(".tmp");
    return FFileHelper::SaveArrayToFile(Bytes, *Temp) && IFileManager::Get().Move(*Path, *Temp, true);
}

bool FAhgLayer::LoadFromFile(const FString& Path)
{
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *Path))
    {
        return false;
    }
    FAhgLayer Loaded;
    FMemoryReader Reader(Bytes);
    Loaded.Serialize(Reader);
    if (Reader.IsError() || !Reader.AtEnd())
    {
        return false;
    }
    *this = MoveTemp(Loaded);
    return true;
}
