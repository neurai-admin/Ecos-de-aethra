#include "VisualWorld/AethraVisualWorld.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Async/ParallelFor.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "UObject/ConstructorHelpers.h"

CSV_DEFINE_CATEGORY(AethraVW, true);
DEFINE_LOG_CATEGORY_STATIC(LogAethraVW, Log, All);

namespace
{
TAutoConsoleVariable<float> CVarHour(TEXT("aethra.vw.Hour"), 16.f, TEXT("Visual World time of day, 0-24 (16 = warm afternoon light)."));

enum EVegetation : int32 { GrassClump, Flower, VegetationCount }; // trees come from the species system

uint32 Mix(uint32 H)
{
    H ^= H >> 16;
    H *= 0x7feb352dU;
    H ^= H >> 15;
    H *= 0x846ca68bU;
    H ^= H >> 16;
    return H;
}

float Hash01(int32 Seed, int32 X, int32 Y, uint32 Salt)
{
    return (Mix(static_cast<uint32>(Seed) ^ Mix(static_cast<uint32>(X) * 0x9E3779B1U ^ Mix(static_cast<uint32>(Y) * 0x85EBCA77U + Salt))) & 0xFFFFFF) / 16777215.f;
}

float Id(EAmeSurface S)
{
    return static_cast<float>(S);
}

FVector Cm(const FVector2D& M, float HeightM)
{
    return FVector(M.X * 100.0, M.Y * 100.0, HeightM * 100.0);
}
}

AAethraVisualWorld::AAethraVisualWorld()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork; // after the pawn moved: streaming follows it
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Surface(TEXT("/Game/VisualWorld/M_AethraSurface.M_AethraSurface"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Foliage(TEXT("/Game/VisualWorld/M_AethraFoliage.M_AethraFoliage"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Hydro(TEXT("/Game/VisualWorld/M_AethraHydro.M_AethraHydro"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Fall(TEXT("/Game/VisualWorld/M_AethraFall.M_AethraFall"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mist(TEXT("/Game/VisualWorld/M_AethraMist.M_AethraMist"));
    SurfaceMaterial = Surface.Object;
    FoliageMaterial = Foliage.Object;
    HydroMaterial = Hydro.Object;
    FallMaterial = Fall.Object;
    MistMaterial = Mist.Object;
}

void AAethraVisualWorld::MakeMaterials()
{
    Palette = FAmeRegistry::CreatePalette(this);
    WaterPalette = FHydroGen::CreatePalette(World->GetBodies());
    auto Make = [this](UMaterialInterface* Base, UTexture2D* Texture, FName Param) -> UMaterialInstanceDynamic*
    {
        if (!Base)
        {
            return nullptr;
        }
        UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, this);
        Mid->SetTextureParameterValue(Param, Texture);
        return Mid;
    };
    SurfaceMID = Make(SurfaceMaterial, Palette, TEXT("Palette"));
    FoliageMID = Make(FoliageMaterial, Palette, TEXT("Palette"));
    HydroMID = Make(HydroMaterial, WaterPalette, TEXT("Water"));
    FallMID = Make(FallMaterial, WaterPalette, TEXT("Water"));
    MistMID = Make(MistMaterial, WaterPalette, TEXT("Water"));
}

void AAethraVisualWorld::MakeVegetationMeshes()
{
    // Trees: one mesh per species of the flora system (blocky, conical, angular or, for rare trees,
    // organic). Grass and flowers below. Surfaces come from the AME palette (ids in UV1).
    const int32 Seed = World->GetSeed();
    auto Finish = [this](FAhgMeshBuilder& B, const TCHAR* Name, UMaterialInterface* Material)
    {
        FMeshDescription Description;
        B.ToMeshDescription(Description);
        VegetationMeshes.Add(FAhgShapes::MakeMesh(this, Name, Description, Material, false));
    };
    for (const FFloraSpecies& Species : FFloraRegistry::All())
    {
        FAhgMeshBuilder B;
        FFloraRegistry::BuildMesh(Species, Seed, B);
        FMeshDescription Description;
        B.ToMeshDescription(Description);
        SpeciesMeshes.Add(FAhgShapes::MakeMesh(this, *FString::Printf(TEXT("SM_VW_Flora_%d"), static_cast<int32>(Species.Id)), Description, SurfaceMaterial, false));
    }
    {
        // Grass clump: thin bent blades; vertex colour R = height along the blade (wind weight).
        FAhgMeshBuilder B;
        B.Surface = Id(EAmeSurface::Grass);
        for (int32 I = 0; I < 9; ++I)
        {
            const float A = I * 2.4f, R = 8.f + 9.f * (I % 3);
            const FVector3f Root(R * FMath::Cos(A), R * FMath::Sin(A), -2.f);
            const FVector3f Side(-FMath::Sin(A) * 3.5f, FMath::Cos(A) * 3.5f, 0.f);
            const float H = 38.f + 7.f * (I % 4);
            const FVector3f Tip = Root + FVector3f(FMath::Cos(A) * 14.f, FMath::Sin(A) * 14.f, H);
            B.Color = FLinearColor(0, 0, 0, 1);
            B.Tri(Root - Side, Root + Side, Tip);
            B.Verts.Last().Color = FLinearColor(1, 0, 0, 1);
        }
        Finish(B, TEXT("SM_VW_Grass"), FoliageMaterial);
    }
    {
        // Flower: a few blades and a head; the head's colour comes from the instance (custom data 1).
        FAhgMeshBuilder B;
        B.Surface = Id(EAmeSurface::Grass);
        for (int32 I = 0; I < 4; ++I)
        {
            const float A = I * 1.7f;
            const FVector3f Root(6.f * FMath::Cos(A), 6.f * FMath::Sin(A), -2.f), Side(-FMath::Sin(A) * 3.f, FMath::Cos(A) * 3.f, 0.f);
            B.Color = FLinearColor(0, 0, 0, 1);
            B.Tri(Root - Side, Root + Side, Root + FVector3f(FMath::Cos(A) * 8.f, FMath::Sin(A) * 8.f, 30.f));
            B.Verts.Last().Color = FLinearColor(1, 0, 0, 1);
        }
        B.Surface = Id(EAmeSurface::Meadow);
        B.Color = FLinearColor(1, 1, 0, 1); // G = 1 marks the petals
        B.Sphere(FVector3f(0, 0, 42), FVector3f(8, 8, 5), 3, 6);
        Finish(B, TEXT("SM_VW_Flower"), FoliageMaterial);
    }
    for (int32 S = 0; S < static_cast<int32>(EAhgShape::Count); ++S)
    {
        FMeshDescription Description;
        FAhgShapes::Build(static_cast<EAhgShape>(S), Description);
        ShapeMeshes.Add(FAhgShapes::MakeMesh(this, *FString::Printf(TEXT("SM_AHG_%d"), S), Description, SurfaceMaterial, true));
    }
}

void AAethraVisualWorld::Start(int32 Seed, const FVector& ViewerAt, const FString& PiecesFile)
{
    const double Begin = FPlatformTime::Seconds();
    World = MakeUnique<FAethraWorldGen>(Seed);
    PiecesPath = PiecesFile.IsEmpty()
        ? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VisualWorld"), FString::Printf(TEXT("Pieces_%d.ahg"), Seed))
        : PiecesFile;
    PlanSettlement(); // adds flat pads to the generator before any terrain is built
    MakeMaterials();
    MakeVegetationMeshes();
    FloraPath = FPaths::Combine(FPaths::GetPath(PiecesPath), FPaths::GetBaseFilename(PiecesPath) + TEXT(".flora"));
    Flora = FFloraLayer();
    Flora.LoadFromFile(FloraPath); // none yet = nothing felled
    if (!Layer.LoadFromFile(PiecesPath))
    {
        BuildSettlement();
    }
    ShowPieces();
    BuildHorizon();
    BuildFalls();
    UpdateStreaming(ViewerAt.IsNearlyZero() ? SpawnLocation() : ViewerAt, 12);
    bShots = FParse::Param(FCommandLine::Get(), TEXT("AethraVWShots"));
    FParse::Value(FCommandLine::Get(), TEXT("AethraBench="), BenchSeconds);
    Counters.SetupMs = (FPlatformTime::Seconds() - Begin) * 1000.0;
    UE_LOG(LogAethraVW, Display, TEXT("Visual World seed %d: drainage %.0f ms (%d river, %d lake, %d sea nodes, %d bodies, %d waterfalls), setup %.0f ms, %d pieces."),
        Seed, World->InitMs, World->RiverNodes, World->LakeNodes, World->SeaNodes, World->GetBodies().Num(), World->GetWaterfalls().Num(), Counters.SetupMs, Layer.GetPieces().Num());
}

FVector AAethraVisualWorld::SpawnLocation() const
{
    if (!World)
    {
        return FVector::ZeroVector;
    }
    const FVector2D At = bHasSettlement ? SettlementCenter + (BridgeA - SettlementCenter).GetSafeNormal() * -14.0 : FVector2D(0, 0);
    return Cm(At, World->Height(At) + 2.f);
}

int32 AAethraVisualWorld::ChunkCount(int32 Level) const
{
    int32 N = 0;
    for (const TPair<FIntVector, FVwChunk>& C : Chunks)
    {
        N += C.Key.Z == Level;
    }
    return N;
}

// ------------------------------------------------------------------ settlement (CivGen-lite)

void AAethraVisualWorld::PlanSettlement()
{
    // On the main river, close to the spawn: a bank flat enough for a pad, the river narrow enough
    // for a stone bridge. Deterministic: candidates are walked in a fixed order.
    const TArray<FIntPoint>& River = World->MainRiver;
    float BestScore = 1e9f;
    for (int32 I = 1; I + 1 < River.Num(); ++I)
    {
        const FVector2D P = World->NodePosition(River[I]);
        if (P.Size() > 900.0)
        {
            continue;
        }
        const FVector2D Along = (World->NodePosition(River[I + 1]) - World->NodePosition(River[I - 1])).GetSafeNormal();
        const FVector2D Across(-Along.Y, Along.X);
        // River width: walk out from the centre until dry, both ways.
        float Half[2] = { 0, 0 };
        for (int32 Side = 0; Side < 2; ++Side)
        {
            for (float D = 0.f; D < 40.f; D += 1.f)
            {
                if (!World->Sample(P + Across * (Side ? -D : D)).IsWater())
                {
                    Half[Side] = D;
                    break;
                }
            }
        }
        const float Width = Half[0] + Half[1];
        if (Width < 8.f || Width > 34.f || Half[0] < 2.f || Half[1] < 2.f)
        {
            continue;
        }
        for (int32 Side = 0; Side < 2; ++Side)
        {
            const FVector2D Center = P + Across * ((Side ? -1.0 : 1.0) * (Half[Side] + 24.0));
            float Lo = 1e9f, Hi = -1e9f;
            bool bDry = true;
            for (int32 K = 0; K < 16 && bDry; ++K)
            {
                const FVector2D Q = Center + FVector2D(FMath::Cos(K * PI / 8.0), FMath::Sin(K * PI / 8.0)) * 18.0;
                const FWorldSample S = World->Sample(Q);
                bDry = !S.IsWater();
                Lo = FMath::Min(Lo, S.Height);
                Hi = FMath::Max(Hi, S.Height);
            }
            const float Score = (Hi - Lo) + .01f * static_cast<float>(P.Size());
            if (bDry && Hi - Lo < 7.f && Score < BestScore)
            {
                BestScore = Score;
                SettlementCenter = Center;
                BridgeA = P + Across * ((Side ? -1.0 : 1.0) * (Half[Side] + 3.0));
                BridgeB = P - Across * ((Side ? -1.0 : 1.0) * (Half[1 - Side] + 3.0));
                bHasSettlement = true;
            }
        }
    }
    if (!bHasSettlement)
    {
        UE_LOG(LogAethraVW, Warning, TEXT("Visual World: no river crossing near the spawn for the settlement."));
        return;
    }
    const float PadHeight = World->Height(SettlementCenter);
    World->AddPad({ SettlementCenter, 20.f, PadHeight });
    World->AddPad({ BridgeB + (BridgeB - BridgeA).GetSafeNormal() * 6.0, 6.f, World->Height(BridgeB + (BridgeB - BridgeA).GetSafeNormal() * 6.0) });
}

void AAethraVisualWorld::BuildSettlement()
{
    if (!bHasSettlement)
    {
        return;
    }
    // Elden stone culture, a prosperous river town: plaster walls, terracotta and blue ceramic.
    const EAmeCulture Culture = EAmeCulture::EldenStone;
    const EAmeRock Rock = World->Sample(SettlementCenter).Rock;
    const float Wealth = .7f;
    auto Mat = [&](EAmePart Part) { return FAmeSelector::Building(Culture, Part, Rock, Wealth); };
    const FVector2D Across = (BridgeA - SettlementCenter).GetSafeNormal(); // toward the river
    const FVector2D Along(-Across.Y, Across.X);
    const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Along.Y, Along.X));
    const float Ground = World->Height(SettlementCenter);
    auto Put = [&](EAhgShape Shape, EAmeSurface Surface, FVector2D Local, float Z, FVector Size, float ExtraYaw = 0.f)
    {
        FAhgPiece P;
        P.Shape = Shape;
        P.Surface = Surface;
        const FVector2D W = SettlementCenter + Along * Local.X + Across * Local.Y;
        P.Location = Cm(W, Ground + Z);
        P.Yaw = Yaw + ExtraYaw;
        P.Size = Size;
        Layer.Add(P);
    };

    // House: plinth, walls with a door, gable roof, chimney, steps.
    const FVector2D House(-5.f, -4.f);
    Put(EAhgShape::Block, Mat(EAmePart::Foundation), House, -1.f, FVector(8.6, 6.6, 1.6));
    Put(EAhgShape::Block, Mat(EAmePart::Wall), House + FVector2D(0, -3.1), .6f, FVector(8.0, .4, 3.4));     // back
    Put(EAhgShape::Block, Mat(EAmePart::Wall), House + FVector2D(-3.8, 0), .6f, FVector(.4, 6.2, 3.4));      // sides
    Put(EAhgShape::Block, Mat(EAmePart::Wall), House + FVector2D(3.8, 0), .6f, FVector(.4, 6.2, 3.4));
    Put(EAhgShape::Block, Mat(EAmePart::Wall), House + FVector2D(-2.4, 3.1), .6f, FVector(3.2, .4, 3.4));    // front, left of door
    Put(EAhgShape::Block, Mat(EAmePart::Wall), House + FVector2D(2.4, 3.1), .6f, FVector(3.2, .4, 3.4));     // right of door
    Put(EAhgShape::Arch, Mat(EAmePart::Trim), House + FVector2D(0, 3.1), .6f, FVector(1.6, .5, 3.4));        // door frame
    Put(EAhgShape::Block, Mat(EAmePart::Trim), House, 4.f, FVector(8.4, 6.6, .3));                           // cornice
    Put(EAhgShape::GableRoof, Mat(EAmePart::Roof), House, 4.3f, FVector(9.2, 7.4, 2.8), 0.f);
    Put(EAhgShape::Block, Mat(EAmePart::Foundation), House + FVector2D(2.6, -1.8), 4.f, FVector(.8, .8, 3.4)); // chimney
    Put(EAhgShape::Stairs, Mat(EAmePart::Foundation), House + FVector2D(0, 4.2), -.4f, FVector(1.4, 1.6, 1.0), 90.f);

    // Round tower: tapering shaft, a marble ring, a ceramic dome and a bronze spire; wedge buttresses.
    const FVector2D Tower(6.f, -3.f);
    Put(EAhgShape::Cylinder, Mat(EAmePart::Foundation), Tower, -1.f, FVector(5.6, 5.6, 1.6));
    Put(EAhgShape::Cylinder, Mat(EAmePart::Wall), Tower, .6f, FVector(5.0, 5.0, 9.0));
    Put(EAhgShape::Cylinder, Mat(EAmePart::Trim), Tower, 9.6f, FVector(5.8, 5.8, .5));
    Put(EAhgShape::Cylinder, Mat(EAmePart::Wall), Tower, 10.1f, FVector(4.4, 4.4, 2.4));
    Put(EAhgShape::Dome, Mat(EAmePart::Roof), Tower, 12.5f, FVector(5.0, 5.0, 3.4));
    Put(EAhgShape::ConeRoof, EAmeSurface::Bronze, Tower, 15.6f, FVector(.7, .7, 2.4));
    Put(EAhgShape::Sphere, EAmeSurface::Bronze, Tower, 17.8f, FVector(.5, .5, .5));
    for (int32 K = 0; K < 4; ++K)
    {
        const float A = K * 90.f + 45.f;
        const FVector2D Off(FMath::Cos(FMath::DegreesToRadians(A)) * 3.0, FMath::Sin(FMath::DegreesToRadians(A)) * 3.0);
        Put(EAhgShape::Wedge, Mat(EAmePart::Foundation), Tower + Off, .6f, FVector(1.6, .8, 3.0), A + 180.f);
    }
    for (int32 K = 0; K < 3; ++K) // arched windows around the shaft
    {
        const float A = K * 120.f + 30.f;
        const FVector2D Off(FMath::Cos(FMath::DegreesToRadians(A)) * 2.55, FMath::Sin(FMath::DegreesToRadians(A)) * 2.55);
        Put(EAhgShape::Arch, Mat(EAmePart::Trim), Tower + Off, 5.f, FVector(1.2, .3, 2.0), A + 90.f);
    }

    // A small square: columns with finials and a low curved garden wall.
    for (const float X : { -1.5f, 1.5f })
    {
        Put(EAhgShape::Column, Mat(EAmePart::Column), FVector2D(X, 9.f), 0.f, FVector(.9, .9, 3.6));
        Put(EAhgShape::Sphere, Mat(EAmePart::Trim), FVector2D(X, 9.f), 3.6f, FVector(.6, .6, .6));
    }
    Put(EAhgShape::CurvedWall, Mat(EAmePart::Foundation), FVector2D(-10.f, 8.f), 0.f, FVector(4, 4, .9), 0.f);
    Put(EAhgShape::CurvedWall, Mat(EAmePart::Foundation), FVector2D(10.f, 8.f), 0.f, FVector(4, 4, .9), 90.f);
    Put(EAhgShape::HalfBlock, Mat(EAmePart::Floor), FVector2D(0.f, 4.f), -.45f, FVector(6, 9, 1.0));

    // Stone arch bridge from bank to bank: arches standing in the river, a deck, parapets and ramps.
    const FVector2D Span = BridgeB - BridgeA;
    const float Length = static_cast<float>(Span.Size());
    const FVector2D Dir = Span / Length;
    const float BridgeYaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
    const float Deck = FMath::Max(World->Height(BridgeA), World->Height(BridgeB)) + .8f;
    const FWorldSample Mid = World->Sample((BridgeA + BridgeB) * .5);
    const float Bed = Mid.Height - .5f;
    const int32 Arches = FMath::Max(1, FMath::CeilToInt(Length / 7.f));
    const float ArchLength = Length / Arches;
    auto PutAt = [&](EAhgShape Shape, EAmeSurface Surface, FVector2D W, float Z, FVector Size, float PieceYaw)
    {
        FAhgPiece P;
        P.Shape = Shape;
        P.Surface = Surface;
        P.Location = Cm(W, Z);
        P.Yaw = PieceYaw;
        P.Size = Size;
        Layer.Add(P);
    };
    for (int32 K = 0; K < Arches; ++K)
    {
        const FVector2D C = BridgeA + Dir * ((K + .5f) * ArchLength);
        PutAt(EAhgShape::Arch, Mat(EAmePart::Deck), C, Bed, FVector(ArchLength, 3.4, Deck - Bed), BridgeYaw);
    }
    const FVector2D Side(-Dir.Y, Dir.X);
    PutAt(EAhgShape::Block, Mat(EAmePart::Deck), (BridgeA + BridgeB) * .5, Deck, FVector(Length + 1.f, 3.6, .4), BridgeYaw);
    for (const float S : { -1.65f, 1.65f })
    {
        PutAt(EAhgShape::Block, Mat(EAmePart::Trim), (BridgeA + BridgeB) * .5 + Side * S, Deck + .4f, FVector(Length + 1.f, .3, .9), BridgeYaw);
    }
    for (const FVector2D End : { BridgeA - Dir * 2.5, BridgeB + Dir * 2.5 })
    {
        const float Ground2 = World->Height(End);
        const bool bStart = (End - BridgeA).Size() < 4.0;
        if (Deck + .4f - Ground2 > .1f)
        {
            PutAt(EAhgShape::Ramp, Mat(EAmePart::Deck), End, Ground2 - .3f, FVector(4.f, 3.4, Deck + .4f - Ground2 + .3f), BridgeYaw + (bStart ? 180.f : 0.f));
        }
    }
    SavePieces();
}

void AAethraVisualWorld::ShowPieces()
{
    for (UInstancedStaticMeshComponent* C : PieceInstances)
    {
        if (C)
        {
            C->DestroyComponent();
        }
    }
    PieceInstances.Reset();
    for (int32 S = 0; S < ShapeMeshes.Num(); ++S)
    {
        UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this);
        C->SetStaticMesh(ShapeMeshes[S]);
        C->SetMaterial(0, SurfaceMID);
        C->NumCustomDataFloats = 2;
        C->SetupAttachment(RootComponent);
        C->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        C->RegisterComponent();
        PieceInstances.Add(C);
    }
    for (const FAhgPiece& P : Layer.GetPieces())
    {
        UInstancedStaticMeshComponent* C = PieceInstances[static_cast<int32>(P.Shape)];
        const int32 Index = C->AddInstance(FTransform(FRotator(0.f, P.Yaw, 0.f), P.Location, P.Size), true);
        C->SetCustomDataValue(Index, 0, Hash01(P.Id, 0, 0, 5) - .5f);
        C->SetCustomDataValue(Index, 1, Id(P.Surface) + 1.f); // > 0: the instance chooses the surface
    }
}

bool AAethraVisualWorld::SavePieces() const
{
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(PiecesPath), true);
    return Layer.SaveToFile(PiecesPath);
}

// ------------------------------------------------------------------ terrain

namespace
{
// Vertex data shared by terrain chunks and the horizon.
struct FSurfaceBuffers
{
    TArray<FVector> Positions;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UV0, UV1, UV2, UV3;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;
};

void AddGridSurface(const FAethraWorldGen& Gen, FVector2D Origin, double StepM, int32 N, int32 Seed, FSurfaceBuffers& T, FSurfaceBuffers& W,
    int32& WaterQuads, double SinkInsideM = -1.0, bool bSkirt = true)
{
    // Samples with one extra ring for normals.
    const int32 M = N + 2;
    TArray<FWorldSample> S;
    S.SetNum(M * M);
    ParallelFor(M, [&](int32 J)
    {
        for (int32 I = 0; I < M; ++I)
        {
            S[J * M + I] = Gen.Sample(Origin + FVector2D((I - 1) * StepM, (J - 1) * StepM));
        }
    });
    auto At = [&](int32 I, int32 J) -> const FWorldSample& { return S[(J + 1) * M + (I + 1)]; };
    const int32 Base = T.Positions.Num();
    for (int32 J = 0; J < N; ++J)
    {
        for (int32 I = 0; I < N; ++I)
        {
            const FWorldSample& C = At(I, J);
            const FVector2D P = Origin + FVector2D(I * StepM, J * StepM);
            const float DX = (At(I + 1, J).Height - At(I - 1, J).Height) / (2.f * StepM);
            const float DY = (At(I, J + 1).Height - At(I, J - 1).Height) / (2.f * StepM);
            const FVector Normal = FVector(-DX, -DY, 1.f).GetSafeNormal();
            float H = C.Height;
            if (SinkInsideM > 0.0 && P.Size() < SinkInsideM)
            {
                H -= 30.f; // horizon under the streamed terrain
            }
            T.Positions.Add(Cm(P, H));
            T.Normals.Add(Normal);
            FAmeEnvironment Env;
            Env.Biome = C.Biome;
            Env.Rock = C.Rock;
            Env.Altitude = C.Height;
            Env.Moisture = C.Moisture;
            Env.Temperature = C.Temperature;
            Env.Slope = 1.f - static_cast<float>(Normal.Z);
            Env.WaterDistance = C.WaterDistance;
            const FAmeChoice Choice = FAmeSelector::Terrain(Env, Seed, FIntPoint(FMath::FloorToInt(P.X / 2.0), FMath::FloorToInt(P.Y / 2.0)));
            T.UV0.Add(P / 4.0);
            const EAmeSurface Main = Choice.Blend > .5f ? Choice.Secondary : Choice.Primary;
            T.UV1.Add(FVector2D(Id(Main), Id(Main)));
            T.UV2.Add(FVector2D(1.0, 0.0)); // terrain: albedo and roughness come from the vertex
            // Surface ids must not be interpolated between vertices (stripes of unrelated surfaces), so the
            // AME blend is resolved here: vertex colour = mixed albedo (with occlusion and wetness), alpha = roughness.
            const FAmeMaterialDef& A = FAmeRegistry::Get(Choice.Primary);
            const FAmeMaterialDef& B = FAmeRegistry::Get(Choice.Secondary);
            const float Curve = (At(I + 1, J).Height + At(I - 1, J).Height + At(I, J + 1).Height + At(I, J - 1).Height) * .25f - C.Height;
            const float Occlusion = FMath::Clamp(1.f - Curve * .08f, .65f, 1.f);
            const float Wet = FMath::Clamp(1.f - C.WaterDistance / 4.f, 0.f, 1.f);
            FLinearColor Albedo = FMath::Lerp(A.Base, B.Base, Choice.Blend) * Occlusion * FMath::Lerp(1.f, .62f, Wet);
            Albedo.A = FMath::Lerp(FMath::Lerp(A.Roughness, B.Roughness, Choice.Blend), .3f, Wet);
            T.Colors.Add(Albedo);
        }
    }
    for (int32 J = 0; J + 1 < N; ++J)
    {
        for (int32 I = 0; I + 1 < N; ++I)
        {
            const int32 A = Base + J * N + I, B = A + 1, C = A + N, D = C + 1;
            T.Triangles.Append({ A, C, B, B, C, D });
        }
    }
    if (bSkirt)
    {
        // Skirts hide cracks where a finer chunk meets a coarser one.
        auto Edge = [&](int32 I0, int32 J0, int32 DI, int32 DJ)
        {
            const int32 First = T.Positions.Num();
            for (int32 K = 0; K < N; ++K)
            {
                const int32 Src = Base + (J0 + DJ * K) * N + (I0 + DI * K);
                // Copies first: adding an element of the array to itself is not allowed.
                const FVector Down = T.Positions[Src] - FVector(0, 0, 400);
                const FVector Normal = T.Normals[Src];
                const FVector2D U0 = T.UV0[Src], U1 = T.UV1[Src];
                const FLinearColor Colour = T.Colors[Src];
                T.Positions.Add(Down);
                T.Normals.Add(Normal);
                T.UV0.Add(U0);
                T.UV1.Add(U1);
                T.UV2.Add(FVector2D(1.0, 0.0));
                T.Colors.Add(Colour);
            }
            for (int32 K = 0; K + 1 < N; ++K)
            {
                const int32 A = Base + (J0 + DJ * K) * N + (I0 + DI * K), B = Base + (J0 + DJ * (K + 1)) * N + (I0 + DI * (K + 1));
                T.Triangles.Append({ A, B, First + K, B, First + K + 1, First + K });
            }
        };
        Edge(0, 0, 1, 0);
        Edge(0, N - 1, 1, 0);
        Edge(0, 0, 0, 1);
        Edge(N - 1, 0, 0, 1);
    }

    // Water: a quad wherever any corner is wet, at the wettest corner's level (banks hide the rim).
    for (int32 J = 0; J + 1 < N; ++J)
    {
        for (int32 I = 0; I + 1 < N; ++I)
        {
            const FWorldSample* Q[4] = { &At(I, J), &At(I + 1, J), &At(I, J + 1), &At(I + 1, J + 1) };
            float Level = -1e9f;
            int32 Body = -1;
            for (const FWorldSample* X : Q)
            {
                if (X->IsWater() && X->WaterLevel > Level)
                {
                    Level = X->WaterLevel;
                    Body = X->WaterBody;
                }
            }
            if (Body < 0)
            {
                continue;
            }
            ++WaterQuads;
            const int32 First = W.Positions.Num();
            const FIntPoint Corner[4] = { { I, J }, { I + 1, J }, { I, J + 1 }, { I + 1, J + 1 } };
            for (int32 K = 0; K < 4; ++K)
            {
                const FWorldSample& X = *Q[K];
                const FVector2D P = Origin + FVector2D(Corner[K].X * StepM, Corner[K].Y * StepM);
                const float Depth = FMath::Max(Level - X.Height, 0.f);
                // Dry corners dip under the bank, so the shoreline follows the terrain instead of the grid.
                const float Z = X.Height >= Level ? FMath::Min(Level, X.Height - .3f) : Level;
                W.Positions.Add(Cm(P, Z - (SinkInsideM > 0.0 && P.Size() < SinkInsideM ? 30.f : 0.f)));
                W.Normals.Add(FVector::UpVector);
                W.UV0.Add(FVector2D(X.Flow.X, X.Flow.Y));
                W.UV1.Add(FVector2D(Body, 0.f));
                // R depth / 16 m, G speed / 4 m/s, B shore foam.
                const float Speed = X.Flow.Size();
                W.Colors.Add(FLinearColor(FMath::Clamp(Depth / 16.f, 0.f, 1.f), FMath::Clamp(Speed / 4.f, 0.f, 1.f), FMath::Clamp(1.f - Depth / .6f, 0.f, 1.f), 1.f));
            }
            W.Triangles.Append({ First, First + 2, First + 1, First + 1, First + 2, First + 3 });
        }
    }
}
}

void AAethraVisualWorld::BuildChunk(int32 Level, FIntPoint Coord, const FVector2D& ViewerM)
{
    const double Begin = FPlatformTime::Seconds();
    const double Size = ChunkSize[Level], StepM = Step[Level];
    const int32 N = FMath::RoundToInt(Size / StepM) + 1;
    const FVector2D Origin(Coord.X * Size, Coord.Y * Size);
    FSurfaceBuffers T, W;
    AddGridSurface(*World, Origin, StepM, N, World->GetSeed(), T, W, Counters.WaterQuads);

    FVwChunk Chunk;
    Chunk.Mesh = NewObject<UProceduralMeshComponent>(this);
    Chunk.Mesh->SetupAttachment(RootComponent);
    Chunk.Mesh->bUseAsyncCooking = true;
    Chunk.bCollision = Level == 0 && (Origin + FVector2D(Size * .5, Size * .5) - ViewerM).Size() < 260.0;
    Chunk.Mesh->CreateMeshSection_LinearColor(0, T.Positions, T.Triangles, T.Normals, T.UV0, T.UV1, T.UV2, T.UV3, T.Colors, T.Tangents, Chunk.bCollision);
    Chunk.Mesh->SetMaterial(0, SurfaceMID);
    if (W.Positions.Num())
    {
        Chunk.Mesh->CreateMeshSection_LinearColor(1, W.Positions, W.Triangles, W.Normals, W.UV0, W.UV1, W.UV2, W.UV3, W.Colors, W.Tangents, false);
        Chunk.Mesh->SetMaterial(1, HydroMID);
    }
    Chunk.Mesh->RegisterComponent();
    Counters.Triangles += (T.Triangles.Num() + W.Triangles.Num()) / 3;
    AddFoliage(Chunk, Level, Coord, ViewerM);
    Chunks.Add(FIntVector(Coord.X, Coord.Y, Level), Chunk);
    ++Counters.Generated;
    const double Ms = (FPlatformTime::Seconds() - Begin) * 1000.0;
    Counters.TotalChunkMs += Ms;
    Counters.MaxChunkMs = FMath::Max(Counters.MaxChunkMs, Ms);
    CSV_CUSTOM_STAT(AethraVW, ChunkMs, static_cast<float>(Ms), ECsvCustomStatOp::Accumulate);
}

UHierarchicalInstancedStaticMeshComponent* AAethraVisualWorld::NewFoliage(UStaticMesh* Mesh, float CullCm, bool bShadows)
{
    UHierarchicalInstancedStaticMeshComponent* C = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
    C->SetStaticMesh(Mesh);
    C->NumCustomDataFloats = 2;
    C->SetupAttachment(RootComponent);
    C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    C->SetCullDistances(static_cast<int32>(CullCm * .8f), static_cast<int32>(CullCm));
    C->SetCastShadow(bShadows);
    C->bAffectDistanceFieldLighting = false;
    return C;
}

void AAethraVisualWorld::AddFoliage(FVwChunk& Chunk, int32 Level, FIntPoint Coord, const FVector2D& ViewerM)
{
    const int32 Seed = World->GetSeed();
    const double Size = ChunkSize[Level];
    const FVector2D Origin(Coord.X * Size, Coord.Y * Size);
    // Trees from the species system: one instanced component per species present in the chunk.
    // Custom data: 0 tint (also the wind phase), 2 the species' wind strength.
    TArray<FFloraTree> Found;
    TreesIn(Level, Coord, Found);
    const int32 SpeciesCount = static_cast<int32>(EFloraSpecies::Count);
    TArray<TArray<FTransform>> Trees;
    TArray<TArray<float>> Tints;
    Trees.SetNum(SpeciesCount);
    Tints.SetNum(SpeciesCount);
    for (const FFloraTree& T : Found)
    {
        const int32 S = static_cast<int32>(T.Species);
        Trees[S].Add(FTransform(FRotator(0, T.Yaw, 0), Cm(T.Position, T.Ground - .2f), FVector(T.Scale)));
        Tints[S].Add(T.Tint);
    }
    for (int32 S = 0; S < SpeciesCount; ++S)
    {
        if (!Trees[S].Num())
        {
            continue;
        }
        const FFloraSpecies& Def = FFloraRegistry::All()[S];
        // LOD: near chunks draw to the species' distance; far chunks to the horizon of the streamed area.
        UHierarchicalInstancedStaticMeshComponent* C = NewFoliage(SpeciesMeshes[S], Level == 0 ? FMath::Min(Def.DrawDistance, 600.f) * 100.f : Def.DrawDistance * 100.f, true);
        C->NumCustomDataFloats = 3;
        C->SetMaterial(0, SurfaceMID);
        C->RegisterComponent();
        C->AddInstances(Trees[S], false);
        for (int32 K = 0; K < Trees[S].Num(); ++K)
        {
            C->SetCustomDataValue(K, 0, Tints[S][K], false);
            C->SetCustomDataValue(K, 2, Def.Wind, false);
        }
        C->MarkRenderStateDirty();
        Chunk.Foliage.Add(C);
        Chunk.Trees += Trees[S].Num();
    }
    Counters.Trees += Chunk.Trees;

    // Grass and flowers only next to the viewer (they fade out by 120 m).
    if (Level != 0 || (Origin + FVector2D(Size * .5, Size * .5) - ViewerM).Size() > 230.0)
    {
        return;
    }
    TArray<FTransform> Grass, Flowers;
    TArray<float> FlowerColour;
    const double GCell = 1.7;
    const int32 GCells = FMath::RoundToInt(Size / GCell);
    for (int32 J = 0; J < GCells; ++J)
    {
        for (int32 I = 0; I < GCells; ++I)
        {
            const int32 GX = FMath::FloorToInt(Origin.X / GCell) + I, GY = FMath::FloorToInt(Origin.Y / GCell) + J;
            const FVector2D P(GX * GCell + Hash01(Seed, GX, GY, 11) * GCell, GY * GCell + Hash01(Seed, GX, GY, 12) * GCell);
            const FWorldSample S = World->Sample(P);
            const bool bGrassy = S.Biome == EAmeBiome::Plains || S.Biome == EAmeBiome::Meadow || S.Biome == EAmeBiome::Forest || S.Biome == EAmeBiome::RiverBank;
            if (!bGrassy || S.IsWater() || S.WaterDistance < 1.f || (bHasSettlement && FVector2D::Distance(P, SettlementCenter) < 22.0))
            {
                continue;
            }
            const float Roll = Hash01(Seed, GX, GY, 13);
            const FTransform X(FRotator(0, 360.f * Hash01(Seed, GX, GY, 14), 0), Cm(P, S.Height), FVector(.8f + .5f * Hash01(Seed, GX, GY, 15)));
            if (S.Biome == EAmeBiome::Meadow && Roll < .22f)
            {
                Flowers.Add(X);
                FlowerColour.Add(1.f + FMath::FloorToFloat(Hash01(Seed, GX, GY, 16) * 4.f)); // 1..4: white, red, yellow, violet
            }
            else if (Roll < (S.Biome == EAmeBiome::Forest ? .35f : .8f))
            {
                Grass.Add(X);
            }
        }
    }
    if (Grass.Num())
    {
        UHierarchicalInstancedStaticMeshComponent* C = NewFoliage(VegetationMeshes[GrassClump], 12000.f, false);
        C->SetMaterial(0, FoliageMID);
        C->RegisterComponent();
        C->AddInstances(Grass, false);
        Chunk.Foliage.Add(C);
    }
    if (Flowers.Num())
    {
        UHierarchicalInstancedStaticMeshComponent* C = NewFoliage(VegetationMeshes[Flower], 12000.f, false);
        C->SetMaterial(0, FoliageMID);
        C->RegisterComponent();
        C->AddInstances(Flowers, false);
        for (int32 K = 0; K < Flowers.Num(); ++K)
        {
            C->SetCustomDataValue(K, 1, FlowerColour[K], false);
        }
        C->MarkRenderStateDirty();
        Chunk.Foliage.Add(C);
    }
    Chunk.Grass = Grass.Num() + Flowers.Num();
    Chunk.bGrass = true;
    Counters.Grass += Chunk.Grass;
}

void AAethraVisualWorld::BuildHorizon()
{
    // 20 x 20 km at 64 m: the far mountains and the sea, sunk under the streamed area.
    const double StepM = 64.0;
    const int32 N = 313;
    FSurfaceBuffers T, W;
    int32 Quads = 0;
    AddGridSurface(*World, FVector2D(-StepM * (N - 1) * .5, -StepM * (N - 1) * .5), StepM, N, World->GetSeed(), T, W, Quads, 1450.0, false);
    Horizon = NewObject<UProceduralMeshComponent>(this);
    Horizon->SetupAttachment(RootComponent);
    Horizon->CreateMeshSection_LinearColor(0, T.Positions, T.Triangles, T.Normals, T.UV0, T.UV1, T.UV2, T.UV3, T.Colors, T.Tangents, false);
    Horizon->SetMaterial(0, SurfaceMID);
    if (W.Positions.Num())
    {
        Horizon->CreateMeshSection_LinearColor(1, W.Positions, W.Triangles, W.Normals, W.UV0, W.UV1, W.UV2, W.UV3, W.Colors, W.Tangents, false);
        Horizon->SetMaterial(1, HydroMID);
    }
    Horizon->SetCastShadow(false);
    Horizon->RegisterComponent();
}

void AAethraVisualWorld::BuildFalls()
{
    // Visual waterfalls at the sites the generator found: a curtain with scrolling streaks, a foam
    // pool and mist. Nothing here moves water; the river level simply steps down at the site.
    for (const FWaterfallSite& Site : World->GetWaterfalls())
    {
        if (Site.Position.Size() > 2500.0)
        {
            continue;
        }
        const FVector2D Side(-Site.Direction.Y, Site.Direction.X);
        const float Half = FMath::Max(Site.Width * .5f, 2.5f);
        const float Drop = Site.Top - Site.Bottom;
        UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
        Mesh->SetupAttachment(RootComponent);
        TArray<FVector> P;
        TArray<int32> Tri;
        TArray<FVector> Normals;
        TArray<FVector2D> UV0, UV1, Empty;
        TArray<FLinearColor> Colors;
        TArray<FProcMeshTangent> Tangents;
        // The drop spans one drainage cell (16 m) and the terrain there is a steep slope, not a
        // vertical cliff, so the curtain follows the ground from the upper node to the lower one as a
        // cascade, a little above the bed (and never below the top/bottom water levels at its ends).
        const int32 Rows = 16;
        const FVector2D Edge = Site.Position;
        const double Run = Site.Length;
        for (int32 R = 0; R <= Rows; ++R)
        {
            const float T = static_cast<float>(R) / Rows;
            const FVector2D Out = Edge + Site.Direction * (Run * T);
            const FWorldSample Ground = World->Sample(Out);
            // Over the water surface (or the bed); .5 m clears the 2 m chunk triangles, which cut
            // corners of the exact height.
            const float Surface = Ground.IsWater() ? FMath::Max(Ground.WaterLevel, Ground.Height) : Ground.Height;
            const float Z = FMath::Max(Surface, FMath::Lerp(Site.Top, Site.Bottom, FMath::Min(T * 1.4f, 1.f)) - .4f) + .5f;
            for (const float S : { -1.f, 1.f })
            {
                P.Add(Cm(Out + Side * (S * Half * (1.f + .15f * T)), Z));
                Normals.Add(FVector(-Site.Direction.X, -Site.Direction.Y, .2).GetSafeNormal());
                UV0.Add(FVector2D(S * .5 + .5, T * Drop / 3.0));
                UV1.Add(FVector2D(Site.Body, Drop));
                Colors.Add(FLinearColor(T, 1.f, 1.f, 1.f));
            }
            if (R > 0)
            {
                const int32 A = (R - 1) * 2;
                Tri.Append({ A, A + 2, A + 1, A + 1, A + 2, A + 3 });
            }
        }
        Mesh->CreateMeshSection_LinearColor(0, P, Tri, Normals, UV0, UV1, Empty, Empty, Colors, Tangents, false);
        Mesh->SetMaterial(0, FallMID);
        // Mist: a few soft sphere-like puffs over the pool.
        TArray<FVector> MP;
        TArray<int32> MT;
        TArray<FVector> MN;
        TArray<FVector2D> MUV;
        TArray<FLinearColor> MC;
        for (int32 K = 0; K < 4; ++K)
        {
            FAhgMeshBuilder B;
            const FVector2D C2 = Edge + Site.Direction * (Run + K * 1.5) + Side * ((K % 2 ? 1.0 : -1.0) * Half * .4);
            B.Sphere(FVector3f(Cm(C2, Site.Bottom + 1.2f + K * .6f)), FVector3f(Half * 45.f + 80.f, Half * 45.f + 80.f, 160.f), 5, 8, .3f, K);
            for (const FAhgMeshBuilder::FVert& V : B.Verts)
            {
                MT.Add(MP.Num());
                MP.Add(FVector(V.P));
                MN.Add(FVector(V.N));
                MUV.Add(FVector2D(V.UV));
                MC.Add(FLinearColor(1, 1, 1, 1));
            }
        }
        Mesh->CreateMeshSection_LinearColor(1, MP, MT, MN, MUV, Empty, Empty, Empty, MC, Tangents, false);
        Mesh->SetMaterial(1, MistMID);
        Mesh->SetCastShadow(false);
        Mesh->RegisterComponent();
        Falls.Add(Mesh);
    }
}

void AAethraVisualWorld::UpdateStreaming(const FVector& Viewer, int32 Budget)
{
    if (!World)
    {
        return;
    }
    const FVector2D V(Viewer.X / 100.0, Viewer.Y / 100.0);
    LastViewerM = V;
    // Level 1 chunks within Radius[1]; a level-1 chunk near the viewer is replaced by its four level-0 children.
    TSet<FIntVector> Wanted;
    TArray<TPair<double, FIntVector>> Missing;
    const int32 R1 = FMath::CeilToInt(Radius[1] / ChunkSize[1]);
    const FIntPoint C1(FMath::FloorToInt(V.X / ChunkSize[1]), FMath::FloorToInt(V.Y / ChunkSize[1]));
    for (int32 Y = -R1; Y <= R1; ++Y)
    {
        for (int32 X = -R1; X <= R1; ++X)
        {
            const FIntPoint P = C1 + FIntPoint(X, Y);
            const FVector2D Center((P.X + .5) * ChunkSize[1], (P.Y + .5) * ChunkSize[1]);
            const double D = (Center - V).Size();
            if (D > Radius[1])
            {
                continue;
            }
            if (D < Radius[0])
            {
                for (int32 K = 0; K < 4; ++K)
                {
                    Wanted.Add(FIntVector(P.X * 2 + (K & 1), P.Y * 2 + (K >> 1), 0));
                }
            }
            else
            {
                Wanted.Add(FIntVector(P.X, P.Y, 1));
            }
        }
    }
    for (const FIntVector& K : Wanted)
    {
        if (!Chunks.Contains(K))
        {
            const double Size = ChunkSize[K.Z];
            const double D = (FVector2D((K.X + .5) * Size, (K.Y + .5) * Size) - V).Size() * (K.Z ? 1.0 : .5);
            Missing.Add({ D, K });
        }
    }
    Missing.Sort([](const TPair<double, FIntVector>& A, const TPair<double, FIntVector>& B) { return A.Key < B.Key; });
    for (int32 I = 0; I < Missing.Num() && I < Budget; ++I)
    {
        BuildChunk(Missing[I].Value.Z, FIntPoint(Missing[I].Value.X, Missing[I].Value.Y), V);
    }
    // Remove chunks no longer wanted, but only once every wanted chunk exists (no holes meanwhile).
    if (Missing.Num() <= Budget)
    {
        TArray<FIntVector> Drop;
        for (const TPair<FIntVector, FVwChunk>& C : Chunks)
        {
            if (!Wanted.Contains(C.Key))
            {
                Drop.Add(C.Key);
            }
        }
        for (const FIntVector& K : Drop)
        {
            DropChunk(K);
        }
    }
    Counters.Loaded = Chunks.Num();
}

void AAethraVisualWorld::DropChunk(const FIntVector& Key)
{
    FVwChunk* C = Chunks.Find(Key);
    if (!C)
    {
        return;
    }
    for (UHierarchicalInstancedStaticMeshComponent* F : C->Foliage)
    {
        if (F)
        {
            F->DestroyComponent();
        }
    }
    if (C->Mesh)
    {
        C->Mesh->DestroyComponent();
    }
    Counters.Trees -= C->Trees;
    Counters.Grass -= C->Grass;
    Chunks.Remove(Key);
    ++Counters.Removed;
}

const UProceduralMeshComponent* AAethraVisualWorld::ChunkMesh(int32 Level, FIntPoint Coord) const
{
    const FVwChunk* C = Chunks.Find(FIntVector(Coord.X, Coord.Y, Level));
    return C ? C->Mesh.Get() : nullptr;
}

void AAethraVisualWorld::TreesIn(int32 Level, FIntPoint Coord, TArray<FFloraTree>& Out) const
{
    // The same 7 m placement grid at both levels, so a tree keeps its id; far chunks keep a stable
    // 40% subset of the species marked bFar (LOD by count), and felled trees are skipped everywhere.
    // A cell belongs to the chunk that holds its lower corner: every cell in exactly one chunk.
    const double Size = ChunkSize[Level], Cell = FFloraPlacer::Cell;
    auto First = [Size, Cell](int32 C) { return FMath::CeilToInt(C * Size / Cell); };
    const int32 Seed = World->GetSeed();
    for (int32 GY = First(Coord.Y); GY < First(Coord.Y + 1); ++GY)
    {
        for (int32 GX = First(Coord.X); GX < First(Coord.X + 1); ++GX)
        {
            const FIntPoint G(GX, GY);
            if (Level == 1 && Hash01(Seed, G.X, G.Y, 8) > .4f)
            {
                continue;
            }
            FFloraTree T;
            if (!FFloraPlacer::TreeAt(*World, G, T) || Flora.IsFelled(T.Id) || (Level == 1 && !FFloraRegistry::Get(T.Species).bFar))
            {
                continue;
            }
            if (bHasSettlement && FMath::PointDistToSegment(FVector(T.Position, 0), FVector(BridgeA, 0), FVector(BridgeB, 0)) < 10.0)
            {
                continue;
            }
            Out.Add(T);
        }
    }
}

bool AAethraVisualWorld::FellTree(uint64 TreeId)
{
    if (!World || !Flora.Fell(TreeId))
    {
        return false;
    }
    const bool bSaved = Flora.SaveToFile(FloraPath);
    // Rebuild the loaded chunk that holds the tree's cell (its lower corner), at either level.
    const FIntPoint G = FFloraPlacer::CellOf(TreeId);
    const FVector2D Corner(G.X * FFloraPlacer::Cell, G.Y * FFloraPlacer::Cell);
    for (int32 Level = 0; Level < 2; ++Level)
    {
        const FIntVector Key(FMath::FloorToInt(Corner.X / ChunkSize[Level]), FMath::FloorToInt(Corner.Y / ChunkSize[Level]), Level);
        if (Chunks.Contains(Key))
        {
            DropChunk(Key);
            BuildChunk(Level, FIntPoint(Key.X, Key.Y), LastViewerM);
        }
    }
    return bSaved;
}

// ------------------------------------------------------------------ time of day, tick

void AAethraVisualWorld::SetHour(float Hour)
{
    ADirectionalLight* Sun = nullptr;
    for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
    {
        Sun = *It;
        break;
    }
    if (!Sun || !Sun->GetLightComponent())
    {
        return;
    }
    Hour = FMath::Fmod(FMath::Max(Hour, 0.f), 24.f);
    const float Day = (Hour - 6.f) / 12.f; // 0 sunrise .. 1 sunset
    const float Elevation = 62.f * FMath::Sin(PI * Day);
    UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(Sun->GetLightComponent());
    // Only the sun lights the sky atmosphere: at night the sky goes dark instead of showing a day sky.
    Light->SetAtmosphereSunLight(Elevation > -2.f);
    // Ambient, haze and exposure follow the sun down (the fog and sky light are set up by the game
    // mode for the day; auto exposure would otherwise brighten the night back to a grey day).
    const float Night = FMath::Clamp((4.f - Elevation) / 10.f, 0.f, 1.f);
    for (TActorIterator<ASkyLight> It(GetWorld()); It; ++It)
    {
        It->GetLightComponent()->SetIntensity(FMath::Lerp(1.35f, .18f, Night));
    }
    for (TActorIterator<AExponentialHeightFog> It(GetWorld()); It; ++It)
    {
        It->GetComponent()->SetFogInscatteringColor(FMath::Lerp(FLinearColor(.42f, .52f, .66f), FLinearColor(.012f, .016f, .03f), Night));
    }
    // Water, falls and mist are unlit translucents: their in-scattered light follows the day too.
    for (UMaterialInstanceDynamic* Mid : { HydroMID.Get(), FallMID.Get(), MistMID.Get() })
    {
        if (Mid)
        {
            Mid->SetScalarParameterValue(TEXT("Light"), FMath::Lerp(1.f, .16f, Night));
        }
    }
    for (TActorIterator<APostProcessVolume> It(GetWorld()); It; ++It)
    {
        It->Settings.bOverride_AutoExposureBias = true;
        It->Settings.AutoExposureBias = FMath::Lerp(0.f, -1.f, Night);
    }
    if (Elevation > -2.f)
    {
        // Warm and low at the ends of the day, white-gold at noon.
        const float Warm = FMath::Clamp(1.f - Elevation / 35.f, 0.f, 1.f);
        Sun->SetActorRotation(FRotator(-FMath::Max(Elevation, 1.f), -110.f + 160.f * Day, 0.f));
        Light->SetIntensity(FMath::Lerp(4.2f, 2.4f, Warm) * FMath::Clamp((Elevation + 2.f) / 6.f, 0.f, 1.f));
        Light->SetLightColor(FMath::Lerp(FLinearColor(1.f, .93f, .82f), FLinearColor(1.f, .62f, .36f), Warm));
    }
    else
    {
        // Moonlight: the same light, cool and dim, high in the night sky.
        Sun->SetActorRotation(FRotator(-38.f, 140.f, 0.f));
        Light->SetIntensity(.9f);
        Light->SetLightColor(FLinearColor(.55f, .66f, 1.f));
    }
    AppliedHour = Hour;
}

void AAethraVisualWorld::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!World)
    {
        return;
    }
    const double Begin = FPlatformTime::Seconds();
    const float Hour = CVarHour.GetValueOnGameThread();
    if (!FMath::IsNearlyEqual(Hour, AppliedHour))
    {
        SetHour(Hour);
    }
    StreamClock += DeltaSeconds;
    if (StreamClock >= .2f)
    {
        StreamClock = 0.f;
        const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
        if (const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr)
        {
            UpdateStreaming(Pawn->GetActorLocation(), 2);
        }
    }
    if (bShots)
    {
        RunShots(DeltaSeconds);
    }
    else if (BenchSeconds > 0.f)
    {
        RunBench(DeltaSeconds);
    }
    Counters.LastFrameMs = (FPlatformTime::Seconds() - Begin) * 1000.0;
    CSV_CUSTOM_STAT(AethraVW, FrameMs, static_cast<float>(Counters.LastFrameMs), ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(AethraVW, Chunks, static_cast<float>(Chunks.Num()), ECsvCustomStatOp::Set);
}

void AAethraVisualWorld::PlaceViewer(const FVector2D& EyeM, float EyeHeightM, const FVector2D& LookM, float LookHeightM)
{
    APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    ACharacter* Character = Controller ? Cast<ACharacter>(Controller->GetPawn()) : nullptr;
    if (!Character)
    {
        return;
    }
    const FVector Eye = Cm(EyeM, FMath::Max(EyeHeightM, World->Height(EyeM) + 2.f));
    Character->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
    Character->GetCharacterMovement()->Velocity = FVector::ZeroVector;
    Character->SetActorLocation(Eye - FVector(0, 0, 65.0), false, nullptr, ETeleportType::TeleportPhysics);
    Controller->SetControlRotation((Cm(LookM, LookHeightM) - Eye).Rotation());
    UpdateStreaming(Eye, 64); // build the view now, not over the next seconds
}

void AAethraVisualWorld::RunShots(float DeltaSeconds)
{
    ShotClock += DeltaSeconds;
    static const float Times[] = { 6.f, 10.f, 14.f, 18.f, 22.f, 26.f, 30.f, 34.f, 38.f, 42.f, 46.f, 50.f, 54.f, 58.f, 62.f, 66.f, 69.f };
    if (ShotStage >= UE_ARRAY_COUNT(Times) || ShotClock < Times[ShotStage])
    {
        return;
    }
    APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    IConsoleVariable* HourVar = IConsoleManager::Get().FindConsoleVariable(TEXT("aethra.vw.Hour"));
    const FVector2D Town = bHasSettlement ? SettlementCenter : FVector2D::ZeroVector;
    auto NearestBody = [this](EHydroKind Kind, FVector2D& Out)
    {
        for (int32 R = 50; R < 3500; R += 50)
        {
            for (int32 K = 0; K < 32; ++K)
            {
                const FVector2D P = FVector2D(FMath::Cos(K * PI / 16.0), FMath::Sin(K * PI / 16.0)) * R;
                const FWorldSample S = World->Sample(P);
                if (S.IsWater() && World->GetBodies().IsValidIndex(S.WaterBody) && World->GetBodies()[S.WaterBody].Kind == Kind)
                {
                    Out = P;
                    return true;
                }
            }
        }
        return false;
    };
    FVector2D Spot;
    switch (ShotStage)
    {
    case 0: // A: the plain, the river and the mountains
        if (HourVar) HourVar->Set(16.f);
        PlaceViewer(Town + FVector2D(-260, -220), World->Height(Town) + 70.f, Town + FVector2D(900, 700), World->Height(Town) + 40.f);
        break;
    case 2: // C: architecture, from the land side with the river and the bridge behind
    {
        const FVector2D Across = (BridgeA - Town).GetSafeNormal(), Along(-Across.Y, Across.X);
        PlaceViewer(Town - Across * 34.0 + Along * 22.0, World->Height(Town) + 16.f, Town + Across * 6.0, World->Height(Town) + 3.f);
        break;
    }
    case 4: // B: river water and the bridge
    {
        const FVector2D Mid = (BridgeA + BridgeB) * .5;
        PlaceViewer(Mid + (BridgeB - BridgeA).GetSafeNormal() * 30.0 + FVector2D((BridgeB - BridgeA).Y, -(BridgeB - BridgeA).X).GetSafeNormal() * 40.0,
            World->Sample(Mid).WaterLevel + 6.f, Mid, World->Sample(Mid).WaterLevel);
        break;
    }
    case 6: // B: the sea
        if (NearestBody(EHydroKind::Sea, Spot))
        {
            const FVector2D Land = Spot * .97;
            PlaceViewer(Land, 18.f, Spot * 1.06, 0.f);
        }
        break;
    case 8: // B: a lake
        if (NearestBody(EHydroKind::Lake, Spot))
        {
            PlaceViewer(Spot + Spot.GetSafeNormal() * -60.0, World->Sample(Spot).WaterLevel + 14.f, Spot, World->Sample(Spot).WaterLevel);
        }
        break;
    case 10: // D: a waterfall
        if (HourVar) HourVar->Set(12.5f); // high sun: the falls sit on north-facing slopes in the shade otherwise
        if (World->GetWaterfalls().Num())
        {
            // Downstream of the fall, high enough to clear the trees (pines reach ~9 m), and the side
            // whose line of sight to the fall stays above the terrain.
            const FWaterfallSite& W = World->GetWaterfalls()[0];
            const FVector2D Side(-W.Direction.Y, W.Direction.X);
            const float LookZ = (W.Top + W.Bottom) * .5f;
            const FVector2D Look = W.Position + W.Direction * (W.Length * .5); // the middle of the cascade
            FVector2D Best = W.Position + W.Direction * 45.0;
            float BestZ = W.Top + 4.f;
            float BestClear = -1e9f;
            for (const double Offset : { 0.0, 18.0, -18.0, 32.0, -32.0 })
            {
                const FVector2D Eye = W.Position + W.Direction * (W.Length + 26.0) + Side * Offset;
                const float EyeZ = FMath::Max(W.Top + 2.f, World->Height(Eye) + 14.f);
                float Clear = 1e9f;
                for (int32 K = 1; K < 16; ++K)
                {
                    const float T = K / 16.f;
                    const FVector2D P = FMath::Lerp(Eye, Look, T);
                    Clear = FMath::Min(Clear, FMath::Lerp(EyeZ, LookZ, T) - World->Height(P));
                }
                if (Clear > BestClear)
                {
                    BestClear = Clear;
                    Best = Eye;
                    BestZ = EyeZ;
                }
            }
            PlaceViewer(Best, BestZ, Look, LookZ);
        }
        break;
    case 12: // dusk
        if (HourVar) HourVar->Set(17.6f); // sun ~8 degrees up: warm, long shadows
        PlaceViewer(Town + FVector2D(-260, -220), World->Height(Town) + 70.f, Town + FVector2D(900, 700), World->Height(Town) + 40.f);
        break;
    case 14: // night
        if (HourVar) HourVar->Set(22.5f);
        PlaceViewer(Town + FVector2D(-120, -100), World->Height(Town) + 30.f, Town, World->Height(Town) + 4.f);
        break;
    case 16:
        if (Controller) Controller->ConsoleCommand(TEXT("quit"));
        break;
    default:
        if (Controller) Controller->ConsoleCommand(TEXT("shot showui"));
        break;
    }
    ++ShotStage;
}

void AAethraVisualWorld::RunBench(float DeltaSeconds)
{
    // A repeatable flight along a circle over the plain and the river, 30 m above the ground,
    // at 15 m/s: the same path for every run with the same seed (the character does the CSV capture).
    BenchClock += DeltaSeconds;
    APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    ACharacter* Character = Controller ? Cast<ACharacter>(Controller->GetPawn()) : nullptr;
    if (!Character)
    {
        return;
    }
    const FVector2D Center = bHasSettlement ? SettlementCenter : FVector2D::ZeroVector;
    const double Angle = BenchClock * 15.0 / 420.0;
    const FVector2D P = Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * 420.0;
    const FVector2D Ahead = Center + FVector2D(FMath::Cos(Angle + .15), FMath::Sin(Angle + .15)) * 420.0;
    Character->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
    Character->GetCharacterMovement()->Velocity = FVector::ZeroVector;
    const float Z = World->Height(P) + 30.f;
    Character->SetActorLocation(Cm(P, Z) - FVector(0, 0, 65.0), false, nullptr, ETeleportType::TeleportPhysics);
    Controller->SetControlRotation((Cm(Ahead, World->Height(Ahead) + 12.f) - Cm(P, Z)).Rotation());
}

void AAethraVisualWorld::EndPlay(const EEndPlayReason::Type Reason)
{
    if (World && Layer.GetPieces().Num())
    {
        SavePieces();
    }
    Super::EndPlay(Reason);
}
