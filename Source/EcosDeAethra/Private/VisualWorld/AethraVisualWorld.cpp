#include "VisualWorld/AethraVisualWorld.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PointLightComponent.h"
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
#include "Async/Async.h"
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
TAutoConsoleVariable<int32> CVarShowHorizon(TEXT("aethra.vw.ShowHorizon"), 1, TEXT("Debug: 0 hides the far horizon tiles."));
TAutoConsoleVariable<int32> CVarHide(TEXT("aethra.vw.Hide"), 0, TEXT("Debug: bit mask of things to hide (1 near terrain, 2 far terrain, 4 horizon, 8 pools, 16 caves, 32 falls, 64 foliage)."));
TAutoConsoleVariable<int32> CVarQuality(TEXT("aethra.vw.Quality"), 2, TEXT("Visual World graphics profile: 0 Low, 1 Medium, 2 High, 3 Ultra."));


uint32 VwMix(uint32 H)
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
    return (VwMix(static_cast<uint32>(Seed) ^ VwMix(static_cast<uint32>(X) * 0x9E3779B1U ^ VwMix(static_cast<uint32>(Y) * 0x85EBCA77U + Salt))) & 0xFFFFFF) / 16777215.f;
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
    auto Make = [this](FAhgMeshBuilder& B, const FString& Name, UMaterialInterface* Material)
    {
        FMeshDescription Description;
        B.ToMeshDescription(Description);
        return FAhgShapes::MakeMesh(this, *Name, Description, Material, false);
    };
    // Trees: Variants meshes per species (index = species * Variants + variant).
    for (const FFloraSpecies& Species : FFloraRegistry::All())
    {
        for (int32 V = 0; V < FFloraRegistry::Variants; ++V)
        {
            FAhgMeshBuilder B;
            FFloraRegistry::BuildMesh(Species, Seed, V, B);
            SpeciesMeshes.Add(Make(B, FString::Printf(TEXT("SM_VW_Tree_%d_%d"), static_cast<int32>(Species.Id), V), SurfaceMaterial));
        }
    }
    // Ground flora: one mesh per species (grass uses the light foliage material).
    for (const FGroundSpecies& Species : FFloraGen::Ground())
    {
        FAhgMeshBuilder B;
        FFloraGen::BuildMesh(Species, Seed, B);
        GroundMeshes.Add(Make(B, FString::Printf(TEXT("SM_VW_Ground_%d"), static_cast<int32>(Species.Id)), Species.bFoliageMaterial ? FoliageMaterial : SurfaceMaterial));
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
    Caves = MakeUnique<FAethraCaveGen>(*World, bHasSettlement ? SettlementCenter : FVector2D::ZeroVector);
    CavePath = FPaths::Combine(FPaths::GetPath(PiecesPath), FPaths::GetBaseFilename(PiecesPath) + TEXT(".caves"));
    Caves->LoadEdits(CavePath); // none yet = untouched
    Ruins = MakeUnique<FAethraRuinGen>(*World, Caves.Get(), bHasSettlement ? SettlementCenter : FVector2D::ZeroVector);
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
    BuildRuins();
    BuildCaveFeatures();
    Quality = FMath::Clamp(CVarQuality.GetValueOnGameThread(), 0, 3);
    ApplyProfileRendering();
    BuildFalls();
    UpdateStreaming(ViewerAt.IsNearlyZero() ? SpawnLocation() : ViewerAt, 12);
    UpdateHorizon(LastViewerM, 1000); // every tile starts building in the background
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
        C->NumCustomDataFloats = 3; // 0 tint (wind phase), 1 surface override, 2 wind strength
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
    int32& WaterQuads, double SinkInsideM = -1.0, bool bSkirt = true, TArray<FWorldSample>* OutSamples = nullptr, FVector2D SinkCenter = FVector2D::ZeroVector,
    const FAethraCaveGen* Caves = nullptr, float SinkDepthM = 30.f)
{
    // Samples with one extra ring for normals (kept for the ground flora when asked).
    const int32 M = N + 2;
    TArray<FWorldSample> Local;
    TArray<FWorldSample>& S = OutSamples ? *OutSamples : Local;
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
            if (SinkInsideM > 0.0 && (P - SinkCenter).Size() < SinkInsideM)
            {
                H -= SinkDepthM; // horizon under the streamed terrain
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
            // Flower fields colour the ground at every distance (the flowers themselves are drawn near the
            // viewer only), so a field reads the same from a mountain top and from inside it.
            const FFieldSample Field = FFloraGen::FieldAt(Seed, P, FFloraEnvironment::From(C, FVector3f(Normal)));
            if (Field.Strength > 0.f)
            {
                const float Alpha = Albedo.A;
                Albedo = FMath::Lerp(Albedo, FFloraGen::FieldColor(Field.Field), Field.Strength * .55f);
                Albedo.A = Alpha;
            }
            T.Colors.Add(Albedo);
        }
    }
    for (int32 J = 0; J + 1 < N; ++J)
    {
        for (int32 I = 0; I + 1 < N; ++I)
        {
            if (Caves && Caves->CoversColumn(Origin + FVector2D((I + .5) * StepM, (J + .5) * StepM), 2.0))
            {
                continue; // a cave or an excavation: the volume mesh draws this ground
            }
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
                W.Positions.Add(Cm(P, Z - (SinkInsideM > 0.0 && (P - SinkCenter).Size() < SinkInsideM ? SinkDepthM : 0.f)));
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

struct FVwHorizonBuild
{
    FSurfaceBuffers T, W;
    FVector2D SinkCenter = FVector2D::ZeroVector;
    double StepM = 64.0;
};

void AAethraVisualWorld::BuildChunk(int32 Level, FIntPoint Coord, const FVector2D& ViewerM)
{
    const double Begin = FPlatformTime::Seconds();
    const double Size = ChunkSize[Level], StepM = Step[Level];
    const int32 N = FMath::RoundToInt(Size / StepM) + 1;
    const FVector2D Origin(Coord.X * Size, Coord.Y * Size);
    FSurfaceBuffers T, W;
    TArray<FWorldSample> Samples;
    // Far chunks sit 0.4 m low: where they still overlap near chunks for a moment, the near ones win.
    AddGridSurface(*World, Origin, StepM, N, World->GetSeed(), T, W, Counters.WaterQuads, Level == 1 ? 1e9 : -1.0, true, &Samples, FVector2D::ZeroVector,
        Level == 0 ? Caves.Get() : nullptr, .4f);

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
    AddFoliage(Chunk, Level, Coord, ViewerM, &Samples);
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
    C->NumCustomDataFloats = 3; // 0 tint (wind phase), 1 surface override, 2 wind strength
    C->SetupAttachment(RootComponent);
    C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    C->SetCullDistances(static_cast<int32>(CullCm * .8f), static_cast<int32>(CullCm));
    C->SetCastShadow(bShadows);
    C->bAffectDistanceFieldLighting = false;
    return C;
}

void AAethraVisualWorld::AddFoliage(FVwChunk& Chunk, int32 Level, FIntPoint Coord, const FVector2D& ViewerM, const TArray<FWorldSample>* Samples)
{
    const int32 Seed = World->GetSeed();
    const double Size = ChunkSize[Level];
    const FVector2D Origin(Coord.X * Size, Coord.Y * Size);
    // Trees from the species system: one instanced component per species present in the chunk.
    // Custom data: 0 tint (also the wind phase), 2 the species' wind strength.
    TArray<FFloraTree> Found;
    TreesIn(Level, Coord, Found);
    const int32 SpeciesCount = static_cast<int32>(EFloraSpecies::Count) * FFloraRegistry::Variants;
    TArray<TArray<FTransform>> Trees;
    TArray<TArray<float>> Tints;
    Trees.SetNum(SpeciesCount);
    Tints.SetNum(SpeciesCount);
    for (const FFloraTree& T : Found)
    {
        // Far chunks draw every tree with variant 0 (same tree, simpler mesh set): a third of the draw calls.
        const int32 S = static_cast<int32>(T.Species) * FFloraRegistry::Variants + (Level == 0 ? T.Variant : 0);
        Trees[S].Add(FTransform(FRotator(0, T.Yaw, 0), Cm(T.Position, T.Ground - .2f), FVector(T.Scale)));
        Tints[S].Add(T.Tint);
    }
    for (int32 S = 0; S < SpeciesCount; ++S)
    {
        if (!Trees[S].Num())
        {
            continue;
        }
        const FFloraSpecies& Def = FFloraRegistry::All()[S / FFloraRegistry::Variants];
        // LOD: near chunks draw to the species' distance, far chunks to the edge of the streamed area; far
        // trees cast shadows only on the higher profiles.
        const float Draw = (Level == 0 ? FMath::Min(Def.DrawDistance, 600.f) : Def.DrawDistance) * Profile().TreeDistance;
        UHierarchicalInstancedStaticMeshComponent* C = NewFoliage(SpeciesMeshes[S], Draw * 100.f, Level == 0 || Profile().bFarTreeShadows);
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

    // Ground flora (grass, ferns, flowers, flower fields) only next to the viewer, from the chunk's own
    // terrain samples: no extra generator calls. Cells belong to the chunk holding their lower corner.
    const FVwProfile& Q = Profile();
    if (Level != 0 || !Samples || (Origin + FVector2D(Size * .5, Size * .5) - ViewerM).Size() > Q.GroundRadius)
    {
        return;
    }
    const double StepM = Step[0];
    const int32 N = FMath::RoundToInt(Size / StepM) + 1, M = N + 2;
    auto At = [&](int32 I, int32 J) -> const FWorldSample& { return (*Samples)[(FMath::Clamp(J, -1, N) + 1) * M + (FMath::Clamp(I, -1, N) + 1)]; };
    const int32 GroundCount = static_cast<int32>(EGroundSpecies::Count);
    TArray<TArray<FTransform>> Plants;
    TArray<TArray<float>> PlantTints;
    Plants.SetNum(GroundCount);
    PlantTints.SetNum(GroundCount);
    const double G = FFloraGen::GroundCell;
    auto First = [Size, G](int32 C) { return FMath::CeilToInt(C * Size / G); };
    for (int32 GY = First(Coord.Y); GY < First(Coord.Y + 1); ++GY)
    {
        for (int32 GX = First(Coord.X); GX < First(Coord.X + 1); ++GX)
        {
            if (Hash01(Seed, GX, GY, 19) > Q.GroundDensity)
            {
                continue; // lower profiles thin the ground flora evenly
            }
            const FVector2D P = FFloraGen::GroundPosition(Seed, FIntPoint(GX, GY));
            if ((bHasSettlement && FVector2D::Distance(P, SettlementCenter) < 22.0) || (Caves && Caves->CoversColumn(P, 0.0)))
            {
                continue;
            }
            const double FI = (P.X - Origin.X) / StepM, FJ = (P.Y - Origin.Y) / StepM;
            const int32 I = FMath::Clamp(FMath::FloorToInt(FI), 0, N - 2), J = FMath::Clamp(FMath::FloorToInt(FJ), 0, N - 2);
            const float TX = static_cast<float>(FI - I), TY = static_cast<float>(FJ - J);
            const FWorldSample& S = At(FMath::RoundToInt(FI), FMath::RoundToInt(FJ));
            if (S.WaterDistance < 1.f)
            {
                continue;
            }
            const float Ground = FMath::Lerp(FMath::Lerp(At(I, J).Height, At(I + 1, J).Height, TX), FMath::Lerp(At(I, J + 1).Height, At(I + 1, J + 1).Height, TX), TY);
            const FVector3f Normal = FVector3f(-(At(I + 1, J).Height - At(I, J).Height) / StepM, -(At(I, J + 1).Height - At(I, J).Height) / StepM, 1.f).GetSafeNormal();
            FGroundPlant Plant;
            if (!FFloraGen::GroundAt(Seed, FIntPoint(GX, GY), FFloraEnvironment::From(S, Normal), Plant))
            {
                continue;
            }
            const int32 K = static_cast<int32>(Plant.Species);
            Plants[K].Add(FTransform(FRotator(0, Plant.Yaw, 0), Cm(Plant.Position, Ground - .05f), FVector(Plant.Scale)));
            PlantTints[K].Add(Plant.Tint);
        }
    }
    for (int32 K = 0; K < GroundCount; ++K)
    {
        if (!Plants[K].Num())
        {
            continue;
        }
        const FGroundSpecies& Def = FFloraGen::Ground()[K];
        // Tall flowers (sunflowers) cast shadows on the higher profiles; low ground flora never (cost).
        UHierarchicalInstancedStaticMeshComponent* C = NewFoliage(GroundMeshes[K], Def.DrawDistance * Q.GroundDistance * 100.f, Def.HeightMax > 1.f && Q.bGroundShadows);
        C->SetMaterial(0, Def.bFoliageMaterial ? FoliageMID.Get() : SurfaceMID.Get());
        C->RegisterComponent();
        C->AddInstances(Plants[K], false);
        for (int32 I = 0; I < Plants[K].Num(); ++I)
        {
            C->SetCustomDataValue(I, 0, PlantTints[K][I] * .6f, false);
            C->SetCustomDataValue(I, 2, Def.Wind, false);
        }
        C->MarkRenderStateDirty();
        Chunk.Foliage.Add(C);
        Chunk.Grass += Plants[K].Num();
    }
    Chunk.bGrass = true;
    Counters.Grass += Chunk.Grass;
}

// ------------------------------------------------------------------ profiles and the far horizon

const FVwProfile& AAethraVisualWorld::ProfileFor(int32 Q)
{
    static const FVwProfile Profiles[4] = {
        //  name          near   far    km    ground dens  gdist tree  farSh  grSh   vfog   shadow casc res
        { TEXT("Baixo"),  256.0, 1100.0,  8.f, 140.f, .45f, .6f,  .6f,  false, false, false,  80.f, 2, 1024 },
        { TEXT("MÃ©dio"),  320.0, 1400.0, 12.f, 180.f, .7f,  .8f,  .8f,  false, false, false, 120.f, 3, 2048 },
        { TEXT("Alto"),   384.0, 1600.0, 20.f, 230.f, 1.f,  1.f,  1.f,  true,  true,  true,  200.f, 3, 2048 },
        { TEXT("Ultra"),  448.0, 2200.0, 32.f, 280.f, 1.f,  1.2f, 1.25f, true, true,  true,  300.f, 4, 4096 },
    };
    return Profiles[FMath::Clamp(Q, 0, 3)];
}

void AAethraVisualWorld::SetQuality(int32 NewQuality)
{
    NewQuality = FMath::Clamp(NewQuality, 0, 3);
    const bool bChanged = NewQuality != Quality;
    Quality = NewQuality;
    ApplyProfileRendering();
    if (bChanged && World)
    {
        // Ground flora and draw distances belong to the chunks: rebuild them (streaming refills the
        // nearest first); horizon tiles outside the new radius go at the next horizon update.
        TArray<FIntVector> Keys;
        Chunks.GetKeys(Keys);
        for (const FIntVector& K : Keys)
        {
            DropChunk(K);
        }
        UpdateStreaming(FVector(LastViewerM.X * 100.0, LastViewerM.Y * 100.0, 0.0), 32);
    }
}

void AAethraVisualWorld::ApplyProfileRendering()
{
    const FVwProfile& Q = Profile();
    auto SetInt = [](const TCHAR* Name, int32 Value)
    {
        if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
        {
            Var->Set(Value, ECVF_SetByCode);
        }
    };
    SetInt(TEXT("r.VolumetricFog"), Q.bVolumetricFog ? 1 : 0);
    SetInt(TEXT("r.Shadow.MaxCSMResolution"), Q.ShadowResolution);
    if (UWorld* W = GetWorld())
    {
        for (TActorIterator<ADirectionalLight> It(W); It; ++It)
        {
            if (UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(It->GetLightComponent()))
            {
                // Adaptive shadow quality: sharp cascades near the viewer, none for the far landscape.
                Light->SetDynamicShadowDistanceMovableLight(Q.ShadowDistance * 100.f);
                Light->SetDynamicShadowCascades(Q.Cascades);
            }
        }
    }
}

void AAethraVisualWorld::LaunchHorizonTile(FIntPoint Tile, const FVector2D& ViewerM)
{
    if (HorizonJobs.Contains(Tile) || !World)
    {
        return;
    }
    const FVector2D Origin(Tile.X * HorizonTile, Tile.Y * HorizonTile);
    const double Distance = (Origin + FVector2D(HorizonTile * .5, HorizonTile * .5) - ViewerM).Size();
    const double StepM = Distance < 12000.0 ? 64.0 : 128.0;
    const int32 N = FMath::RoundToInt(HorizonTile / StepM) + 1;
    const FAethraWorldGen* Gen = World.Get();
    const int32 Seed = World->GetSeed();
    // The whole horizon sits 10 m under the true surface: hidden wherever real terrain is drawn (a coarse
    // 64 m surface can rise a few metres above the real one in valleys), invisible at kilometres. No
    // position-dependent sinking, so a tile never has to follow the viewer. Same WorldGen surface.
    const double Sink = 1e9;
    HorizonJobs.Add(Tile, Async(EAsyncExecution::ThreadPool, [Gen, Seed, Origin, StepM, N, Sink, ViewerM]()
    {
        TSharedPtr<FVwHorizonBuild> Build = MakeShared<FVwHorizonBuild>();
        int32 Quads = 0;
        AddGridSurface(*Gen, Origin, StepM, N, Seed, Build->T, Build->W, Quads, Sink, true, nullptr, ViewerM, nullptr, 10.f);
        Build->SinkCenter = ViewerM;
        Build->StepM = StepM;
        return Build;
    }));
    ++Counters.HorizonBuilds;
}

void AAethraVisualWorld::ApplyHorizonTile(FIntPoint Tile, FVwHorizonBuild& Build)
{
    FVwHorizonTile& T = HorizonTiles.FindOrAdd(Tile);
    if (!T.Mesh)
    {
        T.Mesh = NewObject<UProceduralMeshComponent>(this);
        T.Mesh->SetupAttachment(RootComponent);
        T.Mesh->SetCastShadow(false);
        T.Mesh->RegisterComponent();
    }
    T.Mesh->ClearAllMeshSections();
    T.Mesh->CreateMeshSection_LinearColor(0, Build.T.Positions, Build.T.Triangles, Build.T.Normals, Build.T.UV0, Build.T.UV1, Build.T.UV2, Build.T.UV3, Build.T.Colors, Build.T.Tangents, false);
    T.Mesh->SetMaterial(0, SurfaceMID);
    if (Build.W.Positions.Num())
    {
        T.Mesh->CreateMeshSection_LinearColor(1, Build.W.Positions, Build.W.Triangles, Build.W.Normals, Build.W.UV0, Build.W.UV1, Build.W.UV2, Build.W.UV3, Build.W.Colors, Build.W.Tangents, false);
        T.Mesh->SetMaterial(1, HydroMID);
    }
    T.SinkCenter = Build.SinkCenter;
    T.StepM = Build.StepM;
    T.bDirty = false;
    Counters.HorizonTiles = HorizonTiles.Num();
}

void AAethraVisualWorld::UpdateHorizon(const FVector2D& ViewerM, int32 Budget)
{
    if (!World)
    {
        return;
    }
    // Finished builds first (cheap: mesh upload only).
    TArray<FIntPoint> Ready;
    for (const TPair<FIntPoint, TFuture<TSharedPtr<FVwHorizonBuild>>>& Job : HorizonJobs)
    {
        if (Job.Value.IsReady())
        {
            Ready.Add(Job.Key);
        }
    }
    for (const FIntPoint& Key : Ready)
    {
        TSharedPtr<FVwHorizonBuild> Build = HorizonJobs[Key].Get();
        HorizonJobs.Remove(Key);
        if (Build)
        {
            ApplyHorizonTile(Key, *Build);
        }
    }
    const FVwProfile& Q = Profile();
    const double Reach = Q.HorizonKm * 1000.0;
    auto RectDistance = [&ViewerM](FIntPoint T)
    {
        const double X0 = T.X * HorizonTile, Y0 = T.Y * HorizonTile;
        const double DX = FMath::Max3(X0 - ViewerM.X, 0.0, ViewerM.X - (X0 + HorizonTile));
        const double DY = FMath::Max3(Y0 - ViewerM.Y, 0.0, ViewerM.Y - (Y0 + HorizonTile));
        return FMath::Sqrt(DX * DX + DY * DY);
    };
    // Nearest missing or stale tiles first.
    TArray<TPair<double, FIntPoint>> Wanted;
    const int32 R = FMath::CeilToInt(Reach / HorizonTile) + 1;
    const FIntPoint C(FMath::FloorToInt(ViewerM.X / HorizonTile), FMath::FloorToInt(ViewerM.Y / HorizonTile));
    for (int32 Y = -R; Y <= R; ++Y)
    {
        for (int32 X = -R; X <= R; ++X)
        {
            const FIntPoint T = C + FIntPoint(X, Y);
            const double D = RectDistance(T);
            if (D > Reach)
            {
                continue;
            }
            const FVwHorizonTile* Have = HorizonTiles.Find(T);
            const bool bStale = Have && Have->bDirty;
            if ((!Have || bStale) && !HorizonJobs.Contains(T))
            {
                Wanted.Add({ D, T });
            }
        }
    }
    Wanted.Sort([](const TPair<double, FIntPoint>& A, const TPair<double, FIntPoint>& B) { return A.Key < B.Key; });
    for (int32 I = 0; I < Wanted.Num() && I < Budget; ++I)
    {
        LaunchHorizonTile(Wanted[I].Value, ViewerM);
    }
    // Tiles beyond the reach (with a tile of margin, against flicker) go away.
    TArray<FIntPoint> Far;
    for (const TPair<FIntPoint, FVwHorizonTile>& T : HorizonTiles)
    {
        if (RectDistance(T.Key) > Reach + HorizonTile)
        {
            Far.Add(T.Key);
        }
    }
    for (const FIntPoint& Key : Far)
    {
        if (HorizonTiles[Key].Mesh)
        {
            HorizonTiles[Key].Mesh->DestroyComponent();
        }
        HorizonTiles.Remove(Key);
    }
    Counters.HorizonTiles = HorizonTiles.Num();
}

void AAethraVisualWorld::MarkHorizonDirty(const FVector2D& CenterM, double RadiusM)
{
    for (TPair<FIntPoint, FVwHorizonTile>& T : HorizonTiles)
    {
        const FBox2D Box(FVector2D(T.Key.X * HorizonTile, T.Key.Y * HorizonTile), FVector2D((T.Key.X + 1) * HorizonTile, (T.Key.Y + 1) * HorizonTile));
        if (Box.ComputeSquaredDistanceToPoint(CenterM) <= RadiusM * RadiusM)
        {
            T.Value.bDirty = true;
        }
    }
}

void AAethraVisualWorld::FlushHorizon()
{
    for (int32 Guard = 0; Guard < 64; ++Guard)
    {
        for (TPair<FIntPoint, TFuture<TSharedPtr<FVwHorizonBuild>>>& Job : HorizonJobs)
        {
            Job.Value.Wait();
        }
        UpdateHorizon(LastViewerM, 1000);
        if (!HorizonJobs.Num())
        {
            return;
        }
    }
}

double AAethraVisualWorld::HorizonReachM() const
{
    double Reach = 0.0;
    for (const TPair<FIntPoint, FVwHorizonTile>& T : HorizonTiles)
    {
        for (int32 K = 0; K < 4; ++K)
        {
            const FVector2D Corner((T.Key.X + (K & 1)) * HorizonTile, (T.Key.Y + (K >> 1)) * HorizonTile);
            Reach = FMath::Max(Reach, (Corner - LastViewerM).Size());
        }
    }
    return Reach;
}

const UProceduralMeshComponent* AAethraVisualWorld::HorizonMesh(FIntPoint Tile) const
{
    const FVwHorizonTile* T = HorizonTiles.Find(Tile);
    return T ? T->Mesh.Get() : nullptr;
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
    // Level 1 chunks within the far radius; a level-1 chunk near the viewer is replaced by its four level-0 children.
    TSet<FIntVector> Wanted;
    TArray<TPair<double, FIntVector>> Missing;
    const double NearRadius = Profile().NearRadius, FarRadius = Profile().FarRadius;
    const int32 R1 = FMath::CeilToInt(FarRadius / ChunkSize[1]);
    const FIntPoint C1(FMath::FloorToInt(V.X / ChunkSize[1]), FMath::FloorToInt(V.Y / ChunkSize[1]));
    for (int32 Y = -R1; Y <= R1; ++Y)
    {
        for (int32 X = -R1; X <= R1; ++X)
        {
            const FIntPoint P = C1 + FIntPoint(X, Y);
            const FVector2D Center((P.X + .5) * ChunkSize[1], (P.Y + .5) * ChunkSize[1]);
            const double D = (Center - V).Size();
            if (D > FarRadius)
            {
                continue;
            }
            if (D < NearRadius)
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
    // Remove chunks no longer wanted as soon as their area is covered again (no holes, and no coarse
    // chunk left under fine ones, where it would poke through): a far chunk goes when its four near
    // children exist, a near chunk when its far parent exists; anything else once nothing is missing.
    TArray<FIntVector> Drop;
    for (const TPair<FIntVector, FVwChunk>& C : Chunks)
    {
        if (Wanted.Contains(C.Key))
        {
            continue;
        }
        bool bCovered = Missing.Num() <= Budget;
        if (C.Key.Z == 1)
        {
            bool bChildren = true;
            for (int32 K = 0; K < 4; ++K)
            {
                bChildren &= Chunks.Contains(FIntVector(C.Key.X * 2 + (K & 1), C.Key.Y * 2 + (K >> 1), 0));
            }
            bCovered |= bChildren;
        }
        else
        {
            bCovered |= Chunks.Contains(FIntVector(FMath::FloorToInt(C.Key.X / 2.0), FMath::FloorToInt(C.Key.Y / 2.0), 1));
        }
        if (bCovered)
        {
            Drop.Add(C.Key);
        }
    }
    for (const FIntVector& K : Drop)
    {
        DropChunk(K);
    }
    Counters.Loaded = Chunks.Num();
    UpdateCaves(V, FMath::Max(Budget, 2));
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
            if ((Caves && (Caves->CoversColumn(T.Position, 0.0) || Caves->NearEntrance(T.Position, 16.0))) || (Ruins && Ruins->SurfaceSiteAt(T.Position)))
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

// ------------------------------------------------------------------ caves and ruins

void AAethraVisualWorld::BuildRuins()
{
    for (UInstancedStaticMeshComponent* C : RuinInstances)
    {
        if (C)
        {
            C->DestroyComponent();
        }
    }
    RuinInstances.Reset();
    RuinPieces = 0;
    if (!Ruins)
    {
        return;
    }
    for (int32 S = 0; S < ShapeMeshes.Num(); ++S)
    {
        UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this);
        C->SetStaticMesh(ShapeMeshes[S]);
        C->SetMaterial(0, SurfaceMID);
        C->NumCustomDataFloats = 3;
        C->SetupAttachment(RootComponent);
        C->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        C->RegisterComponent();
        RuinInstances.Add(C);
    }
    TArray<FRuinPiece> Pieces;
    for (const FRuinSite& Site : Ruins->Sites())
    {
        Pieces.Reset();
        FAethraRuinGen::Build(Site, World->GetSeed(), Pieces);
        for (int32 K = 0; K < Pieces.Num(); ++K)
        {
            UInstancedStaticMeshComponent* C = RuinInstances[static_cast<int32>(Pieces[K].Shape)];
            const int32 Index = C->AddInstance(Pieces[K].Transform, true);
            C->SetCustomDataValue(Index, 0, (Hash01(Site.Id, K, 0, 6) - .5f) * .6f);
            C->SetCustomDataValue(Index, 1, Id(Pieces[K].Surface) + 1.f);
        }
        RuinPieces += Pieces.Num();
    }
}

void AAethraVisualWorld::BuildCaveFeatures()
{
    for (UInstancedStaticMeshComponent* C : CaveInstances)
    {
        if (C) C->DestroyComponent();
    }
    for (UPointLightComponent* L : CaveLights)
    {
        if (L) L->DestroyComponent();
    }
    for (UProceduralMeshComponent* P : CavePools)
    {
        if (P) P->DestroyComponent();
    }
    CaveInstances.Reset();
    CaveLights.Reset();
    CavePools.Reset();
    if (!Caves)
    {
        return;
    }
    if (!CaveMeshes.Num())
    {
        // Stylised, faceted formations: a 4-sided stalactite, a 5-sided stalagmite, a hexagonal crystal.
        auto Make = [this](FAhgMeshBuilder& B, const TCHAR* Name)
        {
            FMeshDescription Description;
            B.ToMeshDescription(Description);
            CaveMeshes.Add(FAhgShapes::MakeMesh(this, Name, Description, SurfaceMaterial, false));
        };
        FAhgMeshBuilder A;
        A.Surface = Id(EAmeSurface::Limestone);
        A.Cylinder(FVector3f(0, 0, -100), 2.f, 100.f, 4, true, 32.f);
        Make(A, TEXT("SM_VW_Stalactite"));
        FAhgMeshBuilder B;
        B.Surface = Id(EAmeSurface::Limestone);
        B.Cylinder(FVector3f(0, 0, 0), 36.f, 100.f, 5, true, 4.f);
        Make(B, TEXT("SM_VW_Stalagmite"));
        FAhgMeshBuilder C;
        C.Surface = Id(EAmeSurface::Aetherite);
        C.Cylinder(FVector3f(0, 0, -10), 16.f, 95.f, 6, true, 16.f);
        C.Cylinder(FVector3f(0, 0, 85), 16.f, 40.f, 6, false, 0.f);
        Make(C, TEXT("SM_VW_Crystal"));
    }
    for (int32 T = 0; T < 3; ++T)
    {
        UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this);
        C->SetStaticMesh(CaveMeshes[T]);
        C->SetMaterial(0, SurfaceMID);
        C->NumCustomDataFloats = 3;
        C->SetupAttachment(RootComponent);
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->SetCullDistances(0, 40000);
        C->RegisterComponent();
        CaveInstances.Add(C);
    }
    TArray<FAethraCaveGen::FFormation> Formations;
    for (const FCaveSystem& Sys : Caves->Systems())
    {
        Formations.Reset();
        Caves->Formations(Sys, Formations);
        for (const FAethraCaveGen::FFormation& F : Formations)
        {
            const FRotator Rot = F.Type == 2 ? FRotationMatrix::MakeFromZ(F.Up).Rotator() : FRotator(0.f, 360.f * Hash01(Sys.Id, FMath::FloorToInt(F.Base.X * 10.0), FMath::FloorToInt(F.Base.Y * 10.0), 7), 0.f);
            const FVector Scale = F.Type == 2 ? FVector(F.Size * .5f, F.Size * .5f, F.Size) : FVector(F.Size * .55f, F.Size * .55f, F.Size);
            CaveInstances[F.Type]->AddInstance(FTransform(Rot, F.Base * 100.0, Scale), true);
        }
        if (Sys.Id == 0)
        {
            UE_LOG(LogAethraVW, Display, TEXT("Demo cave (%s): %d formations, %d chambers, pool %s."), FAethraCaveGen::KindName(Sys.Kind), Formations.Num(), Sys.Chambers.Num(), Sys.PoolLevel > -1e8f ? TEXT("yes") : TEXT("no"));
        }
        // A pool in the deepest chamber (enchanted and giant caves): still, clear water.
        if (Sys.PoolLevel > -1e8f && Sys.Chambers.Num())
        {
            const FCaveChamber& Ch = Sys.Chambers.Last();
            UProceduralMeshComponent* Pool = NewObject<UProceduralMeshComponent>(this);
            Pool->SetupAttachment(RootComponent);
            const double Z = Sys.PoolLevel * 100.0;
            const FVector2D C(Ch.Center.X, Ch.Center.Y);
            TArray<FVector> P = { FVector((C.X - Ch.Radii.X) * 100.0, (C.Y - Ch.Radii.Y) * 100.0, Z), FVector((C.X + Ch.Radii.X) * 100.0, (C.Y - Ch.Radii.Y) * 100.0, Z),
                FVector((C.X - Ch.Radii.X) * 100.0, (C.Y + Ch.Radii.Y) * 100.0, Z), FVector((C.X + Ch.Radii.X) * 100.0, (C.Y + Ch.Radii.Y) * 100.0, Z) };
            const int32 Body = World->GetBodies().Num() > 4 ? 4 : 1;
            TArray<FVector2D> UV0 = { FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector };
            TArray<FVector2D> UV1 = { FVector2D(Body, 0), FVector2D(Body, 0), FVector2D(Body, 0), FVector2D(Body, 0) };
            TArray<FLinearColor> Col = { FLinearColor(.1f, 0, 0, 1), FLinearColor(.1f, 0, 0, 1), FLinearColor(.1f, 0, 0, 1), FLinearColor(.1f, 0, 0, 1) };
            TArray<FVector> Normals = { FVector::UpVector, FVector::UpVector, FVector::UpVector, FVector::UpVector };
            TArray<FVector2D> Empty;
            TArray<FProcMeshTangent> Tangents;
            Pool->CreateMeshSection_LinearColor(0, P, { 0, 2, 1, 1, 2, 3 }, Normals, UV0, UV1, Empty, Empty, Col, Tangents, false);
            Pool->SetMaterial(0, HydroMID);
            Pool->SetCastShadow(false);
            Pool->RegisterComponent();
            CavePools.Add(Pool);
        }
        // Enchanted caves glow: soft teal lights without shadows in each chamber (cheap).
        if (Sys.Kind == ECaveKind::Enchanted)
        {
            for (const FCaveChamber& Ch : Sys.Chambers)
            {
                UPointLightComponent* L = NewObject<UPointLightComponent>(this);
                L->SetupAttachment(RootComponent);
                L->SetWorldLocation(Ch.Center * 100.0);
                L->SetIntensityUnits(ELightUnits::Candelas);
                L->SetIntensity(40.f);
                L->SetLightColor(FLinearColor(.35f, 1.f, .85f));
                L->SetAttenuationRadius(Ch.Radii.GetMax() * 220.f);
                L->SetCastShadows(false);
                L->RegisterComponent();
                CaveLights.Add(L);
            }
        }
    }
}

void AAethraVisualWorld::BuildCaveBlock(const FIntVector& Block)
{
    if (TObjectPtr<UProceduralMeshComponent>* Old = CaveBlocks.Find(Block))
    {
        if (*Old)
        {
            (*Old)->DestroyComponent();
        }
        CaveBlocks.Remove(Block);
    }
    FCaveMesh M;
    Caves->MeshBlock(Block, M);
    UProceduralMeshComponent* Mesh = nullptr;
    if (M.Triangles.Num())
    {
        Mesh = NewObject<UProceduralMeshComponent>(this);
        Mesh->SetupAttachment(RootComponent);
        Mesh->bUseAsyncCooking = true;
        TArray<FVector2D> Empty;
        TArray<FProcMeshTangent> Tangents;
        Mesh->CreateMeshSection_LinearColor(0, M.Positions, M.Triangles, M.Normals, M.UV0, M.UV1, M.UV2, Empty, M.Colors, Tangents, true);
        Mesh->SetMaterial(0, SurfaceMID);
        Mesh->RegisterComponent();
        Counters.Triangles += M.Triangles.Num() / 3;
    }
    CaveBlocks.Add(Block, Mesh);
}

void AAethraVisualWorld::UpdateCaves(const FVector2D& ViewerM, int32 Budget)
{
    if (!Caves)
    {
        return;
    }
    // Blocks under the full-detail terrain are meshed; far ones are released (the height field and
    // the horizon show that ground from afar, from the same WorldGen surface).
    const double Near = Profile().NearRadius + 48.0, Far = Profile().NearRadius + 160.0;
    TArray<TPair<double, FIntVector>> Missing;
    for (const FIntVector& B : Caves->ActiveBlocks())
    {
        const double D = (FVector2D((B.X + .5) * FAethraCaveGen::BlockM, (B.Y + .5) * FAethraCaveGen::BlockM) - ViewerM).Size();
        if (D < Near && !CaveBlocks.Contains(B))
        {
            Missing.Add({ D, B });
        }
    }
    Missing.Sort([](const TPair<double, FIntVector>& A, const TPair<double, FIntVector>& B) { return A.Key < B.Key; });
    for (int32 I = 0; I < Missing.Num() && I < Budget; ++I)
    {
        BuildCaveBlock(Missing[I].Value);
    }
    TArray<FIntVector> Drop;
    for (const TPair<FIntVector, TObjectPtr<UProceduralMeshComponent>>& B : CaveBlocks)
    {
        if ((FVector2D((B.Key.X + .5) * FAethraCaveGen::BlockM, (B.Key.Y + .5) * FAethraCaveGen::BlockM) - ViewerM).Size() > Far)
        {
            Drop.Add(B.Key);
        }
    }
    for (const FIntVector& K : Drop)
    {
        if (CaveBlocks[K])
        {
            CaveBlocks[K]->DestroyComponent();
        }
        CaveBlocks.Remove(K);
    }
}

int32 AAethraVisualWorld::CaveBlockCount() const
{
    int32 N = 0;
    for (const TPair<FIntVector, TObjectPtr<UProceduralMeshComponent>>& B : CaveBlocks)
    {
        N += B.Value != nullptr;
    }
    return N;
}

const UProceduralMeshComponent* AAethraVisualWorld::CaveBlockMesh(const FIntVector& Block) const
{
    const TObjectPtr<UProceduralMeshComponent>* M = CaveBlocks.Find(Block);
    return M ? M->Get() : nullptr;
}

bool AAethraVisualWorld::Dig(const FVector& CentreCm, float RadiusM, bool bFill)
{
    if (!Caves || RadiusM <= 0.f || RadiusM > 8.f)
    {
        return false;
    }
    const FVector C = CentreCm / 100.0;
    const int32 ColumnsBefore = Caves->ActiveBlocks().Num();
    TArray<FIntVector> Touched;
    Caves->AddEdit({ C, RadiusM, bFill }, Touched);
    for (const FIntVector& B : Touched)
    {
        BuildCaveBlock(B);
    }
    // New volume columns: the level-0 terrain there must open (rebuild the chunks it touches).
    const bool bNewColumns = Caves->ActiveBlocks().Num() != ColumnsBefore;
    TSet<FIntPoint> ChunkKeys;
    for (int32 K = 0; K < 4; ++K)
    {
        const FVector2D Corner(C.X + ((K & 1) ? 1 : -1) * (RadiusM + FAethraCaveGen::BlockM), C.Y + ((K & 2) ? 1 : -1) * (RadiusM + FAethraCaveGen::BlockM));
        ChunkKeys.Add(FIntPoint(FMath::FloorToInt(Corner.X / ChunkSize[0]), FMath::FloorToInt(Corner.Y / ChunkSize[0])));
    }
    // Trees standing in the dug sphere fall (persisted like any felled tree).
    if (!bFill)
    {
        for (const FIntPoint& K : ChunkKeys)
        {
            TArray<FFloraTree> Trees;
            TreesIn(0, K, Trees);
            for (const FFloraTree& T : Trees)
            {
                if (FVector2D::Distance(T.Position, FVector2D(C)) < RadiusM + 1.5f && FMath::Abs(T.Ground - C.Z) < RadiusM + 1.f)
                {
                    Flora.Fell(T.Id);
                }
            }
        }
        Flora.SaveToFile(FloraPath);
    }
    for (const FIntPoint& K : ChunkKeys)
    {
        if (bNewColumns || !bFill)
        {
            const FIntVector Key(K.X, K.Y, 0);
            if (Chunks.Contains(Key))
            {
                DropChunk(Key);
                BuildChunk(0, K, LastViewerM);
            }
        }
    }
    BuildCaveFeatures();
    MarkHorizonDirty(FVector2D(C), RadiusM);
    return Caves->SaveEdits(CavePath);
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
        // Restrained bloom and saturation: natural materials, not a glossy toy look.
        It->Settings.bOverride_BloomIntensity = true;
        It->Settings.BloomIntensity = .3f;
        It->Settings.bOverride_ColorSaturation = true;
        It->Settings.ColorSaturation = FVector4(.95f, .95f, .95f, 1.f);
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
    // One chunk build every 0.1 s (the same throughput as two every 0.2 s, half the spike per frame).
    if (StreamClock >= .1f)
    {
        StreamClock = 0.f;
        const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
        if (const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr)
        {
            UpdateStreaming(Pawn->GetActorLocation(), 1);
        }
        UpdateHorizon(LastViewerM, 2);
    }
    // Debug visibility mask (aethra.vw.Hide): 1 near terrain, 2 far terrain, 4 horizon, 8 pools, 16 cave
    // blocks, 32 falls, 64 foliage.
    const int32 Hide = CVarShowHorizon.GetValueOnGameThread() ? CVarHide.GetValueOnGameThread() : CVarHide.GetValueOnGameThread() | 4;
    if (Hide != AppliedHide || Hide)
    {
        AppliedHide = Hide;
        for (TPair<FIntPoint, FVwHorizonTile>& T : HorizonTiles)
        {
            if (T.Value.Mesh) T.Value.Mesh->SetVisibility(!(Hide & 4));
        }
        for (TPair<FIntVector, FVwChunk>& C : Chunks)
        {
            if (C.Value.Mesh) C.Value.Mesh->SetVisibility(!(Hide & (C.Key.Z ? 2 : 1)));
            for (UHierarchicalInstancedStaticMeshComponent* F : C.Value.Foliage)
            {
                if (F) F->SetVisibility(!(Hide & 64));
            }
        }
        for (UProceduralMeshComponent* P : CavePools) { if (P) P->SetVisibility(!(Hide & 8)); }
        for (TPair<FIntVector, TObjectPtr<UProceduralMeshComponent>>& B : CaveBlocks) { if (B.Value) B.Value->SetVisibility(!(Hide & 16)); }
        for (UProceduralMeshComponent* F : Falls) { if (F) F->SetVisibility(!(Hide & 32)); }
    }
    const int32 WantedQuality = FMath::Clamp(CVarQuality.GetValueOnGameThread(), 0, 3);
    if (WantedQuality != Quality)
    {
        SetQuality(WantedQuality);
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

void AAethraVisualWorld::PlaceViewer(const FVector2D& EyeM, float EyeHeightM, const FVector2D& LookM, float LookHeightM, bool bUnderground)
{
    APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    ACharacter* Character = Controller ? Cast<ACharacter>(Controller->GetPawn()) : nullptr;
    if (!Character)
    {
        return;
    }
    const FVector Eye = Cm(EyeM, bUnderground ? EyeHeightM : FMath::Max(EyeHeightM, World->Height(EyeM) + 2.f));
    Character->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
    Character->GetCharacterMovement()->Velocity = FVector::ZeroVector;
    Character->SetActorLocation(Eye - FVector(0, 0, 65.0), false, nullptr, ETeleportType::TeleportPhysics);
    Controller->SetControlRotation((Cm(LookM, LookHeightM) - Eye).Rotation());
    UpdateStreaming(Eye, 64); // build the view now, not over the next seconds
}

void AAethraVisualWorld::RunShots(float DeltaSeconds)
{
    // Even stages place the camera, odd ones take the shot 4 s later (time for streaming), then quit.
    ShotClock += DeltaSeconds;
    constexpr int32 LastStage = 24;
    if (ShotStage > LastStage || ShotClock < 6.f + 4.f * ShotStage)
    {
        return;
    }
    APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    IConsoleVariable* HourVar = IConsoleManager::Get().FindConsoleVariable(TEXT("aethra.vw.Hour"));
    const FVector2D Town = bHasSettlement ? SettlementCenter : FVector2D::ZeroVector;
    const int32 Seed = World->GetSeed();
    // Searches around the town on rings (the generator decides what is where; the tour only finds it).
    auto Find = [&](TFunctionRef<bool(const FVector2D&, const FWorldSample&)> Test, FVector2D& Out)
    {
        for (int32 R = 80; R < 3200; R += 40)
        {
            for (int32 K = 0; K < 48; ++K)
            {
                const FVector2D P = Town + FVector2D(FMath::Cos(K * PI / 24.0), FMath::Sin(K * PI / 24.0)) * R;
                if (Test(P, World->Sample(P)))
                {
                    Out = P;
                    return true;
                }
            }
        }
        return false;
    };
    auto EnvAt = [this](const FVector2D& P, const FWorldSample& S)
    {
        const float DX = World->Height(P + FVector2D(2, 0)) - World->Height(P - FVector2D(2, 0));
        const float DY = World->Height(P + FVector2D(0, 2)) - World->Height(P - FVector2D(0, 2));
        return FFloraEnvironment::From(S, FVector3f(-DX / 4.f, -DY / 4.f, 1.f).GetSafeNormal());
    };
    FVector2D Spot;
    switch (ShotStage)
    {
    case 0: // A: the great plain, the river and the mountains
        if (HourVar) HourVar->Set(16.f);
        PlaceViewer(Town + FVector2D(-260, -220), World->Height(Town) + 70.f, Town + FVector2D(900, 700), World->Height(Town) + 40.f);
        break;
    case 2: // B: a flower field (sunflowers if there are any near, else any field), from inside it
        if (Find([&](const FVector2D& P, const FWorldSample& S) { const FFieldSample F = FFloraGen::FieldAt(Seed, P, EnvAt(P, S)); return F.Field == EFlowerField::Sunflower && F.Strength > .95f; }, Spot)
            || Find([&](const FVector2D& P, const FWorldSample& S) { return FFloraGen::FieldAt(Seed, P, EnvAt(P, S)).Strength > .95f; }, Spot))
        {
            const FVector2D Dir = (Spot - Town).GetSafeNormal();
            PlaceViewer(Spot - Dir * 12.0, World->Height(Spot - Dir * 12.0) + 2.6f, Spot + Dir * 45.0, World->Height(Spot + Dir * 45.0) + .5f);
        }
        break;
    case 4: // C: a forest with geometric trees, from its edge
        if (Find([](const FVector2D& P, const FWorldSample& S) { return S.Biome == EAmeBiome::Forest && S.ForestDensity > .55f && S.Height < 120.f; }, Spot))
        {
            const FVector2D Dir = (Spot - Town).GetSafeNormal();
            const FVector2D Eye = Spot - Dir * 35.0;
            PlaceViewer(Eye, World->Height(Eye) + 4.f, Spot + Dir * 20.0, World->Height(Spot) + 5.f);
        }
        break;
    case 6: // D: the cave entrance on its hillside
        if (const FCaveSystem* Sys = Caves ? Caves->DemoSystem() : nullptr)
        {
            const FVector2D E(Sys->Entrance), In = Sys->Inward, Side(-In.Y, In.X);
            const FVector2D Eye = E - In * 15.0 + Side * 5.0;
            PlaceViewer(Eye, World->Height(Eye) + 3.5f, E + In * 3.0, Sys->Entrance.Z - 2.f);
        }
        break;
    case 8: // D: inside the cave (enchanted chamber with the underground ruin)
        if (const FCaveSystem* Sys = Caves ? Caves->DemoSystem() : nullptr)
        {
            const FCaveChamber& Ch = Sys->Chambers[0];
            FVector Look = Ch.Center;
            for (const FRuinSite& R : Ruins->Sites())
            {
                if (R.CaveSystem == Sys->Id)
                {
                    Look = R.Anchor + FVector(0, 0, 1.5);
                }
            }
            const FVector2D Eye = FVector2D(Ch.Center) - Sys->Inward * (Ch.Radii.X * .75);
            PlaceViewer(Eye, Ch.Center.Z + 1.5f, FVector2D(Look), Look.Z, true);
        }
        break;
    case 10: // the surface ruin (temple of the Aldarim)
        for (const FRuinSite& R : Ruins ? Ruins->Sites() : TArray<FRuinSite>())
        {
            if (!R.bUnderground && R.Kind == ERuinKind::Temple)
            {
                const FVector2D A(R.Anchor), Eye = A + FVector2D(-20, -16);
                PlaceViewer(Eye, World->Height(Eye) + 7.f, A, R.Anchor.Z + 2.f);
                break;
            }
        }
        break;
    case 12: // C: architecture of today, from the land side with the river and the bridge behind
    {
        const FVector2D Across = (BridgeA - Town).GetSafeNormal(), Along(-Across.Y, Across.X);
        PlaceViewer(Town - Across * 34.0 + Along * 22.0, World->Height(Town) + 16.f, Town + Across * 6.0, World->Height(Town) + 3.f);
        break;
    }
    case 14: // A: the river and the bridge
    {
        const FVector2D Mid = (BridgeA + BridgeB) * .5;
        PlaceViewer(Mid + (BridgeB - BridgeA).GetSafeNormal() * 30.0 + FVector2D((BridgeB - BridgeA).Y, -(BridgeB - BridgeA).X).GetSafeNormal() * 40.0,
            World->Sample(Mid).WaterLevel + 6.f, Mid, World->Sample(Mid).WaterLevel);
        break;
    }
    case 16: // the horizon from a mountain top: everything in view is real and reachable
    {
        FVector2D Top = Town;
        float TopH = -1e9f;
        for (int32 R = 600; R < 4000; R += 100)
        {
            for (int32 K = 0; K < 48; ++K)
            {
                const FVector2D P = Town + FVector2D(FMath::Cos(K * PI / 24.0), FMath::Sin(K * PI / 24.0)) * R;
                const float H = World->Height(P);
                if (H > TopH)
                {
                    TopH = H;
                    Top = P;
                }
            }
        }
        PlaceViewer(Top, TopH + 6.f, Town, World->Height(Town) + (TopH - World->Height(Town)) * .55f);
        FlushHorizon();
        break;
    }
    case 18: // dusk
        if (HourVar) HourVar->Set(17.6f);
        PlaceViewer(Town + FVector2D(-260, -220), World->Height(Town) + 70.f, Town + FVector2D(900, 700), World->Height(Town) + 40.f);
        break;
    case 20: // night
        if (HourVar) HourVar->Set(22.5f);
        PlaceViewer(Town + FVector2D(-120, -100), World->Height(Town) + 30.f, Town, World->Height(Town) + 4.f);
        break;
    case 22: // a waterfall (visual cascade)
        if (HourVar) HourVar->Set(12.5f);
        if (World->GetWaterfalls().Num())
        {
            const FWaterfallSite& W = World->GetWaterfalls()[0];
            const FVector2D Look = W.Position + W.Direction * (W.Length * .5);
            const FVector2D Eye = W.Position + W.Direction * (W.Length + 26.0);
            PlaceViewer(Eye, FMath::Max(W.Top + 2.f, World->Height(Eye) + 14.f), Look, (W.Top + W.Bottom) * .5f);
        }
        break;
    case LastStage:
        if (Controller) Controller->ConsoleCommand(TEXT("quit"));
        break;
    default:
        if (Controller) Controller->ConsoleCommand(TEXT("shot showui"));
        UE_LOG(LogAethraVW, Display, TEXT("Shot %d: %d chunks (%d near, %d far), %d horizon tiles (reach %.1f km), %d cave blocks, %d pools, %d trees, %d ground plants."),
            ShotStage / 2, Chunks.Num(), ChunkCount(0), ChunkCount(1), HorizonTiles.Num(), HorizonReachM() / 1000.0, CaveBlockCount(), CavePools.Num(), Counters.Trees, Counters.Grass);
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
    // Horizon builds read the generator: let them finish before it can go away.
    for (TPair<FIntPoint, TFuture<TSharedPtr<FVwHorizonBuild>>>& Job : HorizonJobs)
    {
        Job.Value.Wait();
    }
    HorizonJobs.Reset();
    Super::EndPlay(Reason);
}

void AAethraVisualWorld::BeginDestroy()
{
    for (TPair<FIntPoint, TFuture<TSharedPtr<FVwHorizonBuild>>>& Job : HorizonJobs)
    {
        Job.Value.Wait();
    }
    HorizonJobs.Reset();
    Super::BeginDestroy();
}
