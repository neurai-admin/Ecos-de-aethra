#if WITH_DEV_AUTOMATION_TESTS

#include "AethraVoxelWorld.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "Voxel/AethraTerrain.h"
#include "VisualWorld/AethraFlora.h"
#include "VisualWorld/AethraHybridGeometry.h"
#include "VisualWorld/AethraVisualWorld.h"
#include "VisualWorld/AethraWorldGen.h"

// Aethra.VisualWorld.*: the Visual World prototype (AME, WorldGen, HydroGen, AHG, flora). They run in
// their own temporary folder (Saved/VisualWorldTests) and delete it; the voxel saves are not touched.

#define AETHRA_VW_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
constexpr int32 Seed = AAethraVisualWorld::DemoSeed;

FString TestDir()
{
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VisualWorldTests"));
}

void CleanTestDir()
{
    IFileManager::Get().DeleteDirectory(*TestDir(), false, true);
}

// The generator is the expensive part (~0.2 s): one per seed for the whole run.
const FAethraWorldGen& Gen(int32 S = Seed)
{
    static TMap<int32, TUniquePtr<FAethraWorldGen>> Cache;
    TUniquePtr<FAethraWorldGen>& G = Cache.FindOrAdd(S);
    if (!G)
    {
        G = MakeUnique<FAethraWorldGen>(S);
    }
    return *G;
}

template <typename FBody>
void WithWorld(FBody&& Body)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    Body(*World);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
}

AAethraVisualWorld* StartVisualWorld(UWorld& World, const FString& PiecesFile, int32 S = Seed)
{
    AAethraVisualWorld* VW = World.SpawnActor<AAethraVisualWorld>();
    VW->Start(S, FVector::ZeroVector, PiecesFile);
    return VW;
}

// Terrain vertices of a chunk keyed by their (x, y) in centimetres: the highest z at each (skirts hang below).
TMap<FIntPoint, double> Surface(const UProceduralMeshComponent* Mesh)
{
    TMap<FIntPoint, double> Out;
    if (!Mesh)
    {
        return Out;
    }
    const FProcMeshSection* Section = const_cast<UProceduralMeshComponent*>(Mesh)->GetProcMeshSection(0);
    for (const FProcMeshVertex& V : Section->ProcVertexBuffer)
    {
        const FIntPoint Key(FMath::RoundToInt(V.Position.X), FMath::RoundToInt(V.Position.Y));
        double& Z = Out.FindOrAdd(Key, -1e12);
        Z = FMath::Max(Z, V.Position.Z);
    }
    return Out;
}

uint32 HashSurface(const TMap<FIntPoint, double>& S)
{
    TArray<FIntPoint> Keys;
    S.GetKeys(Keys);
    Keys.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.Y != B.Y ? A.Y < B.Y : A.X < B.X; });
    uint32 H = 0;
    for (const FIntPoint& K : Keys)
    {
        H = HashCombine(H, HashCombine(GetTypeHash(K), GetTypeHash(FMath::RoundToInt(S[K] * 10.0))));
    }
    return H;
}

FAmeEnvironment RandomEnvironment(FRandomStream& R)
{
    FAmeEnvironment E;
    E.Biome = static_cast<EAmeBiome>(R.RandRange(0, static_cast<int32>(EAmeBiome::Count) - 1));
    E.Rock = static_cast<EAmeRock>(R.RandRange(0, static_cast<int32>(EAmeRock::Count) - 1));
    E.Altitude = R.FRandRange(-5.f, 900.f);
    E.Moisture = R.FRand();
    E.Temperature = R.FRandRange(-15.f, 30.f);
    E.Slope = R.FRand();
    E.WaterDistance = R.FRandRange(0.f, 200.f);
    return E;
}
}

// ---------------------------------------------------------------- 01 deterministic material selection

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW01, "Aethra.VisualWorld.01.MaterialSelectionDeterministic", AETHRA_VW_FLAGS)
bool FAethraVW01::RunTest(const FString&)
{
    FRandomStream R(7);
    TArray<FAmeEnvironment> Envs;
    TArray<FIntPoint> Cells;
    TArray<FAmeChoice> First;
    for (int32 I = 0; I < 4000; ++I)
    {
        Envs.Add(RandomEnvironment(R));
        Cells.Add(FIntPoint(R.RandRange(-100000, 100000), R.RandRange(-100000, 100000)));
        First.Add(FAmeSelector::Terrain(Envs.Last(), 4242, Cells.Last()));
    }
    // Same answers in the opposite order (no hidden state).
    int32 Mismatch = 0;
    for (int32 I = Envs.Num() - 1; I >= 0; --I)
    {
        const FAmeChoice C = FAmeSelector::Terrain(Envs[I], 4242, Cells[I]);
        Mismatch += C.Primary != First[I].Primary || C.Secondary != First[I].Secondary || C.Blend != First[I].Blend;
    }
    TestEqual(TEXT("Same environment, seed and cell give the same surface in any order"), Mismatch, 0);

    // The environment decides: steep ground shows the local rock, cold ground snow, the water's edge sand/gravel/mud.
    FAmeEnvironment Steep;
    Steep.Biome = EAmeBiome::Plains;
    Steep.Rock = EAmeRock::Basalt;
    Steep.Slope = .8f;
    const FAmeChoice SteepChoice = FAmeSelector::Terrain(Steep, 1, FIntPoint(3, 4));
    TestTrue(TEXT("Steep ground blends fully into its rock"), SteepChoice.Secondary == EAmeSurface::Basalt && SteepChoice.Blend >= .99f);
    FAmeEnvironment Cold;
    Cold.Temperature = -8.f;
    Cold.Slope = .1f;
    TestEqual(TEXT("Cold gentle ground is snow"), FAmeSelector::Terrain(Cold, 1, FIntPoint(0, 0)).Primary, EAmeSurface::Snow);
    FAmeEnvironment Bank;
    Bank.Biome = EAmeBiome::Forest;
    Bank.WaterDistance = 1.f;
    const EAmeSurface Edge = FAmeSelector::Terrain(Bank, 1, FIntPoint(0, 0)).Primary;
    TestTrue(TEXT("The water's edge is sand, gravel or mud"), Edge == EAmeSurface::Sand || Edge == EAmeSurface::Gravel || Edge == EAmeSurface::Mud);
    // Weather and wear never change the surface.
    FAmeEnvironment Wet = Steep;
    Wet.Wetness = 1.f;
    Wet.Wear = 1.f;
    const FAmeChoice WetChoice = FAmeSelector::Terrain(Wet, 1, FIntPoint(3, 4));
    TestTrue(TEXT("Wetness and wear do not change the choice"), WetChoice.Primary == SteepChoice.Primary && WetChoice.Secondary == SteepChoice.Secondary);
    // Buildings: culture and wealth decide; the local rock shows up where the culture uses it.
    TestEqual(TEXT("Elden roof, modest"), FAmeSelector::Building(EAmeCulture::EldenStone, EAmePart::Roof, EAmeRock::Granite, .2f), EAmeSurface::RoofTile);
    TestEqual(TEXT("Elden roof, rich"), FAmeSelector::Building(EAmeCulture::EldenStone, EAmePart::Roof, EAmeRock::Granite, .9f), EAmeSurface::Ceramic);
    TestEqual(TEXT("Highland wall uses the local rock"), FAmeSelector::Building(EAmeCulture::HighlandSlate, EAmePart::Wall, EAmeRock::Limestone, .5f), EAmeSurface::Limestone);
    return true;
}

// ---------------------------------------------------------------- 02 biome rules

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW02, "Aethra.VisualWorld.02.BiomeRules", AETHRA_VW_FLAGS)
bool FAethraVW02::RunTest(const FString&)
{
    const FAethraWorldGen& G = Gen();
    TMap<EAmeBiome, int32> Seen;
    int32 Broken = 0, Pairs = 0, Changes = 0;
    // A 4 x 4 km window, 32 m apart, and each point against a neighbour 8 m east.
    for (int32 Y = -64; Y < 64; ++Y)
    {
        for (int32 X = -64; X < 64; ++X)
        {
            const FVector2D P(X * 32.0, Y * 32.0);
            const FWorldSample S = G.Sample(P);
            Seen.FindOrAdd(S.Biome)++;
            if ((S.Biome == EAmeBiome::SnowPeak && S.Temperature >= -1.f)
                || (S.Biome == EAmeBiome::Mountain && S.Height <= 160.f)
                || (S.Biome == EAmeBiome::RiverBank && S.RiverStrength <= 0.f)
                || (S.Biome == EAmeBiome::Beach && S.Height >= 2.5f)
                || S.Biome >= EAmeBiome::Desert) // not produced by the prototype
            {
                ++Broken;
            }
            ++Pairs;
            Changes += G.Sample(P + FVector2D(8.0, 0.0)).Biome != S.Biome;
        }
    }
    TestEqual(TEXT("Every sample follows the biome rules"), Broken, 0);
    const float ChangeRate = static_cast<float>(Changes) / Pairs;
    AddInfo(FString::Printf(TEXT("%d biomes in the window; biome changes between points 8 m apart: %.1f%%"), Seen.Num(), ChangeRate * 100.f));
    TestTrue(TEXT("Varied landscape: at least 5 biomes in 4 x 4 km"), Seen.Num() >= 5);
    TestTrue(TEXT("Regions, not a mosaic: under 12% of 8 m steps change biome"), ChangeRate < .12f);
    return true;
}

// ---------------------------------------------------------------- 03 chunk consistency

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW03, "Aethra.VisualWorld.03.ChunkConsistency", AETHRA_VW_FLAGS)
bool FAethraVW03::RunTest(const FString&)
{
    CleanTestDir();
    // Same seed, two generators: same samples (nothing depends on evaluation order or history).
    const FAethraWorldGen& A = Gen();
    FAethraWorldGen B(Seed);
    int32 Diff = 0;
    FRandomStream R(3);
    for (int32 I = 0; I < 3000; ++I)
    {
        const FVector2D P(R.FRandRange(-3000.f, 3000.f), R.FRandRange(-3000.f, 3000.f));
        const FWorldSample SA = A.Sample(P), SB = B.Sample(P);
        Diff += SA.Height != SB.Height || SA.WaterLevel != SB.WaterLevel || SA.Biome != SB.Biome || SA.WaterBody != SB.WaterBody;
    }
    TestEqual(TEXT("Two generators with the same seed agree everywhere"), Diff, 0);

    // Neighbouring chunks share their border vertices exactly (no cracks), in both directions.
    WithWorld([&](UWorld& World)
    {
        AAethraVisualWorld* VW = StartVisualWorld(World, FPaths::Combine(TestDir(), TEXT("Pieces.ahg")));
        VW->UpdateStreaming(FVector::ZeroVector, 1000);
        int32 Borders = 0, Mismatches = 0, Shared = 0;
        for (int32 Y = -2; Y < 2; ++Y)
        {
            for (int32 X = -2; X < 2; ++X)
            {
                const TMap<FIntPoint, double> Here = Surface(VW->ChunkMesh(0, FIntPoint(X, Y)));
                for (const FIntPoint& Next : { FIntPoint(X + 1, Y), FIntPoint(X, Y + 1) })
                {
                    const TMap<FIntPoint, double> There = Surface(VW->ChunkMesh(0, Next));
                    if (!Here.Num() || !There.Num())
                    {
                        continue;
                    }
                    ++Borders;
                    for (const TPair<FIntPoint, double>& V : Here)
                    {
                        if (const double* Z = There.Find(V.Key))
                        {
                            ++Shared;
                            Mismatches += FMath::Abs(*Z - V.Value) > .01;
                        }
                    }
                }
            }
        }
        AddInfo(FString::Printf(TEXT("%d borders, %d shared vertices"), Borders, Shared));
        TestTrue(TEXT("Borders were compared"), Borders >= 8 && Shared > 500);
        TestEqual(TEXT("Shared border vertices have the same height"), Mismatches, 0);
    });
    CleanTestDir();
    return true;
}

// ---------------------------------------------------------------- 04 geometry persistence

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW04, "Aethra.VisualWorld.04.GeometryPersistence", AETHRA_VW_FLAGS)
bool FAethraVW04::RunTest(const FString&)
{
    CleanTestDir();
    const FString Path = FPaths::Combine(TestDir(), TEXT("Layer.ahg"));
    IFileManager::Get().MakeDirectory(*TestDir(), true);
    FAhgLayer Layer;
    TArray<int32> Ids;
    for (int32 S = 0; S < static_cast<int32>(EAhgShape::Count); ++S)
    {
        FAhgPiece P;
        P.Shape = static_cast<EAhgShape>(S);
        P.Surface = static_cast<EAmeSurface>(S % static_cast<int32>(EAmeSurface::Count));
        P.Location = FVector(S * 250.0, -120.0, 37.5);
        P.Yaw = S * 15.f;
        P.Size = FVector(1.f + S * .1f, 2.f, .5f + S * .25f);
        Ids.Add(Layer.Add(P));
    }
    TestFalse(TEXT("Every shape placed"), Ids.Contains(0));
    TestTrue(TEXT("Saved"), Layer.SaveToFile(Path));

    FAhgLayer Loaded;
    TestTrue(TEXT("Loaded"), Loaded.LoadFromFile(Path));
    TestEqual(TEXT("Same count"), Loaded.GetPieces().Num(), Layer.GetPieces().Num());
    bool bSame = Loaded.GetPieces().Num() == Layer.GetPieces().Num();
    for (int32 I = 0; bSame && I < Layer.GetPieces().Num(); ++I)
    {
        bSame = Loaded.GetPieces()[I] == Layer.GetPieces()[I];
    }
    TestTrue(TEXT("Every piece identical after a reload"), bSame);

    TestTrue(TEXT("Removed"), Loaded.Remove(Ids[3]));
    TestTrue(TEXT("Saved after removal"), Loaded.SaveToFile(Path));
    FAhgLayer Again;
    TestTrue(TEXT("Reloaded"), Again.LoadFromFile(Path));
    TestNull(TEXT("The removed piece stays removed"), Again.Find(Ids[3]));
    TestEqual(TEXT("The others stay"), Again.GetPieces().Num(), Ids.Num() - 1);
    const int32 NewId = Again.Add(FAhgPiece());
    TestFalse(TEXT("Ids are not reused after a reload"), Ids.Contains(NewId));

    // A damaged file is refused and leaves the layer as it was.
    TArray<uint8> Bytes;
    FFileHelper::LoadFileToArray(Bytes, *Path);
    Bytes.SetNum(Bytes.Num() - 7);
    FFileHelper::SaveArrayToFile(Bytes, *Path);
    TestFalse(TEXT("Truncated file refused"), Again.LoadFromFile(Path));
    TestEqual(TEXT("Layer unchanged by the failed load"), Again.GetPieces().Num(), Ids.Num());

    // The settlement of the Visual World is saved on first start and loaded, not rebuilt, afterwards.
    const FString Pieces = FPaths::Combine(TestDir(), TEXT("Pieces.ahg"));
    int32 FirstCount = 0;
    WithWorld([&](UWorld& World)
    {
        AAethraVisualWorld* VW = StartVisualWorld(World, Pieces);
        FirstCount = VW->Pieces().GetPieces().Num();
        TestTrue(TEXT("Settlement written"), IFileManager::Get().FileExists(*Pieces));
        FAhgPiece Extra;
        Extra.Location = FVector(VW->SettlementCenter * 100.0, 0.0);
        TestTrue(TEXT("Player piece added"), VW->Pieces().Add(Extra) > 0);
        TestTrue(TEXT("Saved"), VW->SavePieces());
    });
    WithWorld([&](UWorld& World)
    {
        AAethraVisualWorld* VW = StartVisualWorld(World, Pieces);
        TestEqual(TEXT("Restart loads the settlement and the added piece"), VW->Pieces().GetPieces().Num(), FirstCount + 1);
    });
    AddInfo(FString::Printf(TEXT("Settlement: %d pieces"), FirstCount));
    CleanTestDir();
    return true;
}

// ---------------------------------------------------------------- 05 existing saves preserved

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW05, "Aethra.VisualWorld.05.ExistingSavesPreserved", AETHRA_VW_FLAGS)
bool FAethraVW05::RunTest(const FString&)
{
    // The voxel generator is untouched: a checksum of 4096 columns of seed 42819, recorded on the
    // base branch (claude/voxel-contract-lis-01). Any change to existing voxel worlds breaks it.
    uint32 Checksum = 0;
    TStaticArray<EAethraBlock, AethraTerrain::Height> Column;
    for (int32 Y = -32; Y < 32; ++Y)
    {
        for (int32 X = -32; X < 32; ++X)
        {
            AethraTerrain::GenerateColumn(FIntPoint(X * 7, Y * 5), 42819, Column);
            for (int32 Z = 0; Z < AethraTerrain::Height; ++Z)
            {
                Checksum = HashCombine(Checksum, static_cast<uint32>(Column[Z]) + Z * 131u);
            }
        }
    }
    AddInfo(FString::Printf(TEXT("Voxel column checksum: 0x%08x"), Checksum));
    TestEqual(TEXT("Voxel terrain generation unchanged"), Checksum, 0xdbc636bfu);

    // Starting the Visual World writes nothing to the voxel save slots.
    const FString SaveDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"));
    auto Snapshot = [&SaveDir]()
    {
        TMap<FString, FDateTime> Files;
        TArray<FString> Names;
        IFileManager::Get().FindFiles(Names, *FPaths::Combine(SaveDir, TEXT("*.sav")), true, false);
        for (const FString& N : Names)
        {
            Files.Add(N, IFileManager::Get().GetTimeStamp(*FPaths::Combine(SaveDir, N)));
        }
        return Files;
    };
    const TMap<FString, FDateTime> Before = Snapshot();
    CleanTestDir();
    WithWorld([&](UWorld& World)
    {
        AAethraVisualWorld* VW = StartVisualWorld(World, FPaths::Combine(TestDir(), TEXT("Pieces.ahg")));
        VW->UpdateStreaming(FVector(50000.0, 0.0, 0.0), 200);
        VW->SavePieces();
    });
    const TMap<FString, FDateTime> After = Snapshot();
    TestTrue(TEXT("No voxel save created, changed or deleted"), Before.OrderIndependentCompareEqual(After));
    TestFalse(TEXT("No voxel slot for the Visual World seed"), UGameplayStatics::DoesSaveGameExist(FString::Printf(TEXT("AethraWorld_%d_0"), Seed), 0) && !Before.Contains(FString::Printf(TEXT("AethraWorld_%d_0.sav"), Seed)));
    CleanTestDir();
    return true;
}

// ---------------------------------------------------------------- 06 river continuity

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW06, "Aethra.VisualWorld.06.RiverContinuity", AETHRA_VW_FLAGS)
bool FAethraVW06::RunTest(const FString&)
{
    const FAethraWorldGen& G = Gen();
    const TArray<FIntPoint>& River = G.MainRiver;
    TestTrue(TEXT("A main river exists and is long (> 3 km of nodes)"), River.Num() > 3000 / 16);
    int32 Breaks = 0, Uphill = 0, Dry = 0, Checked = 0;
    for (int32 I = 0; I + 1 < River.Num(); ++I)
    {
        const FIntPoint A = River[I], B = River[I + 1];
        // Upstream to downstream: every node drains into the next one.
        Breaks += G.NodeDownstream(A) != B.Y * FAethraWorldGen::Nodes + B.X;
        Uphill += G.NodeLevel(B) > G.NodeLevel(A) + .01f;
        // The drawn river (through the warp) holds water at each node.
        const FVector2D P = G.WorldOfGridPoint(G.NodePosition(A));
        if (P.Size() < 3500.0)
        {
            ++Checked;
            bool bWater = false;
            for (int32 K = 0; K < 9 && !bWater; ++K)
            {
                bWater = G.Sample(P + FVector2D(K % 3 - 1, K / 3 - 1) * 2.0).IsWater();
            }
            Dry += !bWater;
        }
    }
    AddInfo(FString::Printf(TEXT("Main river: %d nodes (%.1f km); %d checked in the world, %d dry"), River.Num(), River.Num() * 16 / 1000.f, Checked, Dry));
    TestEqual(TEXT("No break along the course"), Breaks, 0);
    TestEqual(TEXT("Never flows uphill"), Uphill, 0);
    TestTrue(TEXT("Water along at least 95% of the drawn course"), Checked > 50 && Dry <= Checked / 20);
    // Across a chunk border (128 m) the water surface is continuous: same level from both sides.
    int32 Jumps = 0, Crossings = 0;
    for (const FIntPoint& N : River)
    {
        const FVector2D P = G.WorldOfGridPoint(G.NodePosition(N));
        const double BorderX = FMath::RoundToDouble(P.X / 128.0) * 128.0;
        if (FMath::Abs(P.X - BorderX) < 8.0 && P.Size() < 3500.0)
        {
            const FWorldSample L = G.Sample(FVector2D(BorderX - .01, P.Y)), R = G.Sample(FVector2D(BorderX + .01, P.Y));
            if (L.IsWater() && R.IsWater())
            {
                ++Crossings;
                Jumps += FMath::Abs(L.WaterLevel - R.WaterLevel) > .05f;
            }
        }
    }
    TestEqual(TEXT("No water-level jump at chunk borders"), Jumps, 0);
    AddInfo(FString::Printf(TEXT("%d border crossings checked"), Crossings));
    return true;
}

// ---------------------------------------------------------------- 07 fresh versus salt water

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW07, "Aethra.VisualWorld.07.FreshVersusSaltWater", AETHRA_VW_FLAGS)
bool FAethraVW07::RunTest(const FString&)
{
    const TArray<FHydroBody>& Bodies = Gen().GetBodies();
    const FHydroBody* Sea = Bodies.FindByPredicate([](const FHydroBody& B) { return B.Kind == EHydroKind::Sea; });
    TestNotNull(TEXT("The world has a sea"), Sea);
    if (!Sea)
    {
        return false;
    }
    TestTrue(TEXT("The sea is salt water (>= 30 g/kg)"), Sea->IsSalty() && Sea->Props.Salinity >= 30.f);
    for (const FHydroBody& B : Bodies)
    {
        if (B.Kind == EHydroKind::River || B.Kind == EHydroKind::Stream || B.Kind == EHydroKind::Lake || B.Kind == EHydroKind::Waterfall)
        {
            TestFalse(*FString::Printf(TEXT("%s is fresh water"), *B.Name), B.IsSalty());
            TestTrue(*FString::Printf(TEXT("%s is lighter than the sea"), *B.Name), B.Density() < Sea->Density());
        }
    }
    // Buoyancy is prepared: a 1010 kg/m3 body sinks in fresh water and floats in the sea.
    FHydroBody Fresh;
    Fresh.Props = FHydroGen::Preset(TEXT("ClearLake"));
    TestEqual(TEXT("Sinks in fresh water"), Fresh.FloatFraction(1010.f), 0.f);
    TestTrue(TEXT("Floats in sea water"), Sea->FloatFraction(1010.f) > 0.f);
    FHydroBody Salt;
    Salt.Props = FHydroGen::Preset(TEXT("SaltLake"));
    TestTrue(TEXT("A salt lake is denser than the sea"), Salt.Density() > Sea->Density());
    // Salinity never sets the colour: same water with and without salt has the same optics.
    FHydroProperties P = FHydroGen::Preset(TEXT("GreenRiver"));
    const FHydroOptics O1 = FHydroGen::ComputeOptics(P);
    P.Salinity = 35.f;
    const FHydroOptics O2 = FHydroGen::ComputeOptics(P);
    TestTrue(TEXT("Salinity does not change the colour"), O1.InScatter.Equals(O2.InScatter) && O1.Absorption.Equals(O2.Absorption));
    // ...while what is in the water does: silt is browner (more red than blue) than clear water.
    const FHydroOptics Silt = FHydroGen::ComputeOptics(FHydroGen::Preset(TEXT("SiltRiver")));
    const FHydroOptics Clear = FHydroGen::ComputeOptics(FHydroGen::Preset(TEXT("ClearLake")));
    TestTrue(TEXT("Silty water is warmer in hue than clear water"), Silt.InScatter.R / Silt.InScatter.B > Clear.InScatter.R / Clear.InScatter.B);
    TestTrue(TEXT("At least three distinct water profiles in this world"), Bodies.Num() >= 3);
    return true;
}

// ---------------------------------------------------------------- 08 depth variation

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW08, "Aethra.VisualWorld.08.DepthVariation", AETHRA_VW_FLAGS)
bool FAethraVW08::RunTest(const FString&)
{
    // Optics: deeper water transmits less of the bottom and tends to the water's own colour.
    const FHydroOptics O = FHydroGen::ComputeOptics(FHydroGen::Preset(TEXT("GreenRiver")));
    const FLinearColor Sand(.6f, .55f, .4f);
    float Last = 2.f;
    bool bMonotonic = true;
    for (float D = 0.f; D <= 20.f; D += .5f)
    {
        const float T = FHydroGen::Transmittance(O, D);
        bMonotonic &= T <= Last;
        Last = T;
    }
    TestTrue(TEXT("Transmittance falls with depth"), bMonotonic);
    TestTrue(TEXT("Shallow water shows the bottom"), FHydroGen::ApparentColor(O, .05f, Sand).Equals(Sand, .05f));
    TestTrue(TEXT("Deep water shows its in-scattered colour"), FHydroGen::ApparentColor(O, 60.f, Sand).Equals(O.InScatter, .01f));

    // World: depth varies along the river and grows downstream with the flow.
    const FAethraWorldGen& G = Gen();
    TArray<float> Depths;
    for (const FIntPoint& N : G.MainRiver)
    {
        const FWorldSample S = G.Sample(G.WorldOfGridPoint(G.NodePosition(N)));
        // River water only: lakes on the course and the sea at the mouth have their own depths.
        if (S.IsWater() && G.GetBodies().IsValidIndex(S.WaterBody) && G.GetBodies()[S.WaterBody].Kind == EHydroKind::River)
        {
            Depths.Add(S.WaterLevel - S.Height);
        }
    }
    TestTrue(TEXT("River samples found"), Depths.Num() > 50);
    if (Depths.Num() > 50)
    {
        const float Min = FMath::Min(Depths), Max = FMath::Max(Depths);
        double Up = 0, Down = 0;
        const int32 Half = Depths.Num() / 2;
        for (int32 I = 0; I < Depths.Num(); ++I)
        {
            (I < Half ? Up : Down) += Depths[I];
        }
        Up /= Half;
        Down /= Depths.Num() - Half;
        AddInfo(FString::Printf(TEXT("River depth %.2f..%.2f m; upstream mean %.2f, downstream mean %.2f"), Min, Max, Up, Down));
        TestTrue(TEXT("Depth varies by more than 1 m"), Max - Min > 1.f);
        TestTrue(TEXT("Deeper downstream"), Down > Up);
    }
    return true;
}

// ---------------------------------------------------------------- 09 registry integrity

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW09, "Aethra.VisualWorld.09.RegistryIntegrity", AETHRA_VW_FLAGS)
bool FAethraVW09::RunTest(const FString&)
{
    for (const FString& Problem : FAmeRegistry::Validate())
    {
        AddError(TEXT("AME: ") + Problem);
    }
    TestEqual(TEXT("One surface per id"), FAmeRegistry::All().Num(), static_cast<int32>(EAmeSurface::Count));
    for (const FAmeMaterialDef& D : FAmeRegistry::All())
    {
        const FAmeMaterialDef* Found = FAmeRegistry::FindByName(D.Name);
        TestTrue(*FString::Printf(TEXT("%s found by name"), D.Name), Found && Found->Id == D.Id);
    }
    UTexture2D* Palette = FAmeRegistry::CreatePalette(GetTransientPackage());
    TestTrue(TEXT("Palette texture 64 x 4"), Palette && Palette->GetSizeX() == FAmeRegistry::PaletteWidth && Palette->GetSizeY() == FAmeRegistry::PaletteRows);

    const TArray<FHydroBody>& Bodies = Gen().GetBodies();
    TestTrue(TEXT("Water bodies fit the water palette"), Bodies.Num() <= FHydroGen::MaxBodies);
    for (int32 I = 0; I < Bodies.Num(); ++I)
    {
        TestEqual(TEXT("Body ids are their index"), Bodies[I].Id, I);
    }
    for (int32 S = 0; S < static_cast<int32>(EAhgShape::Count); ++S)
    {
        const EAhgShape Shape = static_cast<EAhgShape>(S);
        TestTrue(*FString::Printf(TEXT("Shape %d has a name and triangles"), S), *FAhgShapes::Name(Shape) && FAhgShapes::Triangles(Shape) > 0);
    }
    for (const FString& Problem : FFloraRegistry::Validate())
    {
        AddError(TEXT("Flora: ") + Problem);
    }
    return true;
}

// ---------------------------------------------------------------- 10 voxel compatibility

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW10, "Aethra.VisualWorld.10.VoxelCompatibility", AETHRA_VW_FLAGS)
bool FAethraVW10::RunTest(const FString&)
{
    constexpr int32 VoxelSeed = 990431;
    auto DeleteSlots = []()
    {
        for (int32 Slot = 0; Slot < 2; ++Slot)
        {
            const FString Name = FString::Printf(TEXT("AethraWorld_%d_%d"), VoxelSeed, Slot);
            if (UGameplayStatics::DoesSaveGameExist(Name, 0))
            {
                UGameplayStatics::DeleteGameInSlot(Name, 0);
            }
        }
    };
    DeleteSlots();
    WithWorld([&](UWorld& World)
    {
        FActorSpawnParameters Params;
        Params.bDeferConstruction = true;
        AAethraVoxelWorld* Voxels = World.SpawnActor<AAethraVoxelWorld>(Params);
        Voxels->Seed = VoxelSeed;
        Voxels->FinishSpawning(FTransform::Identity);
        Voxels->PrepareSpawn();

        // A dry column (above the water line) with air over its surface block.
        FIntPoint Column(0, 0);
        int32 Surface = 0;
        for (int32 X = 0; X < 400; ++X)
        {
            Column = FIntPoint(X % 20, X / 20);
            Surface = AethraTerrain::SurfaceAt(Column, VoxelSeed);
            if (Surface > AethraTerrain::WaterLevel + 1 && Voxels->GetBlock(FIntVector(Column.X, Column.Y, Surface + 1)) == EAethraBlock::Air
                && Voxels->GetBlock(FIntVector(Column.X, Column.Y, Surface)) != EAethraBlock::Air)
            {
                break;
            }
        }
        const double Cell = AethraTerrain::CellSizeCm;
        const FVector Base((Column.X + .5) * Cell, (Column.Y + .5) * Cell, (Surface + 1) * Cell);
        // A 1 m piece in the air right above the ground.
        FAhgLayer Layer;
        FAhgPiece Piece;
        Piece.Shape = EAhgShape::Arch;
        Piece.Location = Base;
        const EAethraBlock Above = Voxels->GetBlock(FIntVector(Column.X, Column.Y, Surface + 1));
        const EAethraBlock Ground = Voxels->GetBlock(FIntVector(Column.X, Column.Y, Surface));
        FString Why;
        const int32 Id = Layer.Add(Piece, Voxels, &Why);
        TestTrue(TEXT("A piece fits in an air cell above the ground"), Id > 0);
        TestFalse(TEXT("Same cell twice is refused"), Layer.Add(Piece, Voxels, &Why) > 0);
        AddInfo(TEXT("Second piece: ") + Why);
        FAhgPiece Buried = Piece;
        Buried.Location.Z -= Cell; // the ground cell
        TestFalse(TEXT("A piece in a solid block is refused"), Layer.Add(Buried, Voxels, &Why) > 0);
        AddInfo(TEXT("Buried piece: ") + Why);
        // Pieces never write voxels: the blocks are as generated.
        TestEqual(TEXT("Air above unchanged"), Voxels->GetBlock(FIntVector(Column.X, Column.Y, Surface + 1)), Above);
        TestEqual(TEXT("Ground unchanged"), Voxels->GetBlock(FIntVector(Column.X, Column.Y, Surface)), Ground);
        // Removing frees the cell; mining the ground then frees it for a piece.
        TestTrue(TEXT("Removed"), Layer.Remove(Id));
        TestTrue(TEXT("The freed cell accepts a piece again"), Layer.Add(Piece, Voxels) > 0);
        TestTrue(TEXT("Mining the ground block"), Voxels->SetBlock(FIntVector(Column.X, Column.Y, Surface), EAethraBlock::Air));
        TestTrue(TEXT("A piece fits where the block was mined"), Layer.Add(Buried, Voxels, &Why) > 0);
        // The voxel world does not know pieces; the layer reports their cells so placing a block there can be refused.
        TestTrue(TEXT("The layer reports its cells as taken"), Layer.IsCellTaken(FIntVector(Column.X, Column.Y, Surface)));
    });
    DeleteSlots();
    return true;
}

// ---------------------------------------------------------------- 11 region streaming

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW11, "Aethra.VisualWorld.11.RegionStreaming", AETHRA_VW_FLAGS)
bool FAethraVW11::RunTest(const FString&)
{
    CleanTestDir();
    WithWorld([&](UWorld& World)
    {
        AAethraVisualWorld* VW = StartVisualWorld(World, FPaths::Combine(TestDir(), TEXT("Pieces.ahg")));
        VW->UpdateStreaming(FVector::ZeroVector, 1000);
        const FIntPoint Home(0, 0);
        TestTrue(TEXT("Chunk at the origin loaded"), VW->HasChunk(0, Home));
        const uint32 HomeHash = HashSurface(Surface(VW->ChunkMesh(0, Home)));
        const int32 NearCount = VW->ChunkCount(0), FarCount = VW->ChunkCount(1);
        AddInfo(FString::Printf(TEXT("At the origin: %d near chunks, %d far chunks"), NearCount, FarCount));
        TestTrue(TEXT("Both levels of detail present"), NearCount > 0 && FarCount > 0);

        // Fly 3 km east: the origin unloads, the new region loads at full detail.
        const FVector Away(300000.0, 0.0, 0.0);
        VW->UpdateStreaming(Away, 1000);
        TestFalse(TEXT("Origin chunk unloaded far away"), VW->HasChunk(0, Home));
        TestTrue(TEXT("Chunk under the viewer loaded"), VW->HasChunk(0, FIntPoint(FMath::FloorToInt(3000.0 / 128.0), 0)));
        TestTrue(TEXT("Chunk counts stay bounded"), VW->ChunkCount(0) <= NearCount + 8 && VW->ChunkCount(1) <= FarCount + 16);

        // Back home: the region regenerates identically.
        VW->UpdateStreaming(FVector::ZeroVector, 1000);
        TestTrue(TEXT("Origin chunk reloaded"), VW->HasChunk(0, Home));
        TestEqual(TEXT("Regenerated chunk identical"), HashSurface(Surface(VW->ChunkMesh(0, Home))), HomeHash);
        AddInfo(FString::Printf(TEXT("Generated %d, removed %d chunks"), VW->Stats().Generated, VW->Stats().Removed));
    });
    CleanTestDir();
    return true;
}

// ---------------------------------------------------------------- 12 performance

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW12, "Aethra.VisualWorld.12.Performance", AETHRA_VW_FLAGS)
bool FAethraVW12::RunTest(const FString&)
{
    // Loose limits: they catch serious regressions, not small ones (the frame-rate benchmark is separate).
    double Begin = FPlatformTime::Seconds();
    FAethraWorldGen G(Seed + 1);
    const double InitMs = (FPlatformTime::Seconds() - Begin) * 1000.0;
    FRandomStream R(11);
    constexpr int32 Samples = 200000;
    double Sink = 0;
    Begin = FPlatformTime::Seconds();
    for (int32 I = 0; I < Samples; ++I)
    {
        Sink += G.Sample(FVector2D(R.FRandRange(-3000.f, 3000.f), R.FRandRange(-3000.f, 3000.f))).Height;
    }
    const double SampleUs = (FPlatformTime::Seconds() - Begin) * 1e6 / Samples;
    AddInfo(FString::Printf(TEXT("Generator setup %.0f ms; Sample %.2f us (checksum %.0f)"), InitMs, SampleUs, Sink));
    TestTrue(TEXT("Generator setup under 2 s"), InitMs < 2000.0);
    TestTrue(TEXT("A sample under 25 us"), SampleUs < 25.0);

    CleanTestDir();
    WithWorld([&](UWorld& World)
    {
        AAethraVisualWorld* VW = StartVisualWorld(World, FPaths::Combine(TestDir(), TEXT("Pieces.ahg")));
        VW->UpdateStreaming(FVector::ZeroVector, 1000);
        const FVwStats& S = VW->Stats();
        const double MeanChunk = S.Generated ? S.TotalChunkMs / S.Generated : 0.0;
        AddInfo(FString::Printf(TEXT("Start %.0f ms; %d chunks, mean %.1f ms, max %.1f ms; %d trees, %d grass, %d triangles"),
            S.SetupMs, S.Generated, MeanChunk, S.MaxChunkMs, S.Trees, S.Grass, S.Triangles));
        TestTrue(TEXT("Start under 5 s"), S.SetupMs < 5000.0);
        TestTrue(TEXT("Mean chunk build under 60 ms (streaming builds 2 per 0.2 s)"), MeanChunk < 60.0);
    });
    CleanTestDir();
    return true;
}

// ---------------------------------------------------------------- 13-14 flora (vegetation addendum)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW13, "Aethra.VisualWorld.13.FloraSpecies", AETHRA_VW_FLAGS)
bool FAethraVW13::RunTest(const FString&)
{
    for (const FString& Problem : FFloraRegistry::Validate())
    {
        AddError(Problem);
    }
    // Geometric identity: common broadleaf species are boxes only (every face axis-aligned), pines
    // narrow to a point, only rare species are organic.
    for (const FFloraSpecies& S : FFloraRegistry::All())
    {
        FAhgMeshBuilder B;
        FFloraRegistry::BuildMesh(S, Seed, B);
        TestTrue(*FString::Printf(TEXT("%s has geometry"), S.Name), B.Verts.Num() > 0);
        if (S.Form == EFloraForm::Blocky)
        {
            int32 Slanted = 0;
            for (const FAhgMeshBuilder::FVert& V : B.Verts)
            {
                Slanted += FMath::Max3(FMath::Abs(V.N.X), FMath::Abs(V.N.Y), FMath::Abs(V.N.Z)) < .999f;
            }
            TestEqual(*FString::Printf(TEXT("%s is built from boxes only"), S.Name), Slanted, 0);
        }
        if (S.Form == EFloraForm::Conical)
        {
            float TopZ = 0.f, TopWidth = 0.f, BaseWidth = 0.f;
            for (const FAhgMeshBuilder::FVert& V : B.Verts)
            {
                TopZ = FMath::Max(TopZ, V.P.Z);
            }
            for (const FAhgMeshBuilder::FVert& V : B.Verts)
            {
                const float W = FVector2f(V.P.X, V.P.Y).Size();
                (V.P.Z > TopZ * .9f ? TopWidth : BaseWidth) = FMath::Max(V.P.Z > TopZ * .9f ? TopWidth : BaseWidth, W);
            }
            TestTrue(*FString::Printf(TEXT("%s narrows to a point (triangular silhouette)"), S.Name), TopWidth < BaseWidth * .3f);
        }
        TestTrue(*FString::Printf(TEXT("%s: organic only if rare"), S.Name), S.IsCommon() || S.Id == EFloraSpecies::Ancient);
    }
    // Distribution by biome and temperature.
    TMap<int32, int32> Forest, Beach, Cold;
    for (int32 I = 0; I < 20000; ++I)
    {
        const float Roll = (I + .5f) / 20000.f;
        Forest.FindOrAdd(FFloraRegistry::Pick(EAmeBiome::Forest, 14.f, Roll))++;
        Beach.FindOrAdd(FFloraRegistry::Pick(EAmeBiome::Beach, 20.f, Roll))++;
        Cold.FindOrAdd(FFloraRegistry::Pick(EAmeBiome::Mountain, 1.f, Roll))++;
    }
    TestTrue(TEXT("Beaches grow palms only"), Beach.Num() == 1 && Beach.Contains(static_cast<int32>(EFloraSpecies::Palm)));
    TestFalse(TEXT("No oaks on cold mountains"), Cold.Contains(static_cast<int32>(EFloraSpecies::Oak)));
    const float AncientShare = Forest.FindRef(static_cast<int32>(EFloraSpecies::Ancient)) / 20000.f;
    TestTrue(TEXT("Ancient trees are rare in forests (0 < share < 1%)"), AncientShare > 0.f && AncientShare < .01f);
    TestTrue(TEXT("Forests are mixed (4+ species)"), Forest.Num() >= 4);

    // Placement: deterministic, stable ids, ids decode back to their cell.
    const FAethraWorldGen& G = Gen();
    int32 Trees = 0, Mismatch = 0;
    TMap<int32, int32> Species;
    for (int32 Y = -60; Y < 60; ++Y)
    {
        for (int32 X = -60; X < 60; ++X)
        {
            FFloraTree A, B;
            const bool bA = FFloraPlacer::TreeAt(G, FIntPoint(X, Y), A), bB = FFloraPlacer::TreeAt(G, FIntPoint(X, Y), B);
            Mismatch += bA != bB || (bA && (A.Id != B.Id || A.Position != B.Position || A.Scale != B.Scale));
            if (bA)
            {
                ++Trees;
                Species.FindOrAdd(static_cast<int32>(A.Species))++;
                Mismatch += FFloraPlacer::CellOf(A.Id) != FIntPoint(X, Y);
                Mismatch += !G.Sample(A.Position).IsWater() ? 0 : 1;
            }
        }
    }
    AddInfo(FString::Printf(TEXT("%d trees of %d species in 840 x 840 m"), Trees, Species.Num()));
    TestEqual(TEXT("Placement deterministic, ids decode, never in water"), Mismatch, 0);
    TestTrue(TEXT("A forested landscape with several species"), Trees > 500 && Species.Num() >= 3);
    TestEqual(TEXT("Negative cells decode"), FFloraPlacer::CellOf(FFloraPlacer::MakeId(FIntPoint(-123456, 98765), EFloraSpecies::Pine)), FIntPoint(-123456, 98765));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAethraVW14, "Aethra.VisualWorld.14.FloraPersistence", AETHRA_VW_FLAGS)
bool FAethraVW14::RunTest(const FString&)
{
    CleanTestDir();
    const FString Pieces = FPaths::Combine(TestDir(), TEXT("Pieces.ahg"));
    uint64 Felled = 0;
    FIntPoint ChunkKey;
    int32 Before = 0;
    WithWorld([&](UWorld& World)
    {
        AAethraVisualWorld* VW = StartVisualWorld(World, Pieces);
        VW->UpdateStreaming(FVector::ZeroVector, 1000);
        // The first chunk near the origin with trees.
        TArray<FFloraTree> Trees;
        for (int32 K = 0; K < 16 && !Trees.Num(); ++K)
        {
            ChunkKey = FIntPoint(K % 4 - 2, K / 4 - 2);
            VW->TreesIn(0, ChunkKey, Trees);
        }
        if (!TestTrue(TEXT("A loaded chunk with trees"), Trees.Num() > 0 && VW->HasChunk(0, ChunkKey)))
        {
            return;
        }
        Before = Trees.Num();
        Felled = Trees[0].Id;
        TestTrue(TEXT("Felling saves"), VW->FellTree(Felled));
        TestFalse(TEXT("Felling twice does nothing"), VW->FellTree(Felled));
        TArray<FFloraTree> After;
        VW->TreesIn(0, ChunkKey, After);
        TestEqual(TEXT("One tree fewer in the chunk"), After.Num(), Before - 1);
        TestTrue(TEXT("Chunk rebuilt in place"), VW->HasChunk(0, ChunkKey));
        TestTrue(TEXT("Flora file written next to the pieces"), IFileManager::Get().FileExists(*VW->FloraPath));
    });
    WithWorld([&](UWorld& World)
    {
        AAethraVisualWorld* VW = StartVisualWorld(World, Pieces);
        TestTrue(TEXT("Restart: the tree stays felled"), VW->FelledTrees().IsFelled(Felled));
        TArray<FFloraTree> After;
        VW->TreesIn(0, ChunkKey, After);
        TestEqual(TEXT("Restart: the chunk still lacks it"), After.Num(), Before - 1);
        TestFalse(TEXT("No tree with that id is generated"), After.ContainsByPredicate([Felled](const FFloraTree& T) { return T.Id == Felled; }));
    });
    // Another seed is unaffected.
    WithWorld([&](UWorld& World)
    {
        AAethraVisualWorld* VW = StartVisualWorld(World, FPaths::Combine(TestDir(), TEXT("Other.ahg")), Seed + 7);
        TestEqual(TEXT("Another world has nothing felled"), VW->FelledTrees().Num(), 0);
    });
    CleanTestDir();
    return true;
}

#endif
