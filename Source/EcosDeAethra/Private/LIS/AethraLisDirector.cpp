#include "AethraLisDirector.h"

#include "AethraLisNpc.h"
#include "AethraVoxelWorld.h"
#include "Debug/DebugDrawService.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/Crc.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Voxel/AethraTerrain.h"

CSV_DEFINE_CATEGORY(AethraLIS, true);
DEFINE_LOG_CATEGORY_STATIC(LogAethraLisWorld, Log, All);

namespace
{
TAutoConsoleVariable<int32> CVarDebug(TEXT("aethra.lis.Debug"), 1,
    TEXT("LIS NPC overlay: 0 = off (nothing drawn), 1 = names and the inspected NPC, 2 = names only."));
TAutoConsoleVariable<int32> CVarSelect(TEXT("aethra.lis.Select"), 0,
    TEXT("NPC id shown in the LIS panel; 0 = the NPC closest to the crosshair."));
TAutoConsoleVariable<float> CVarSecondsPerHour(TEXT("aethra.lis.SecondsPerHour"), 30.f,
    TEXT("Real seconds per LIS game hour."));

void RemoveVillageIn(UWorld* World)
{
    for (TActorIterator<AAethraLisDirector> It(World); It; ++It)
    {
        It->RemoveVillage();
    }
}
FAutoConsoleCommandWithWorld CmdRemoveVillage(TEXT("aethra.lis.RemoveVillage"),
    TEXT("Puts the terrain back under every Elden village block still in place."), FConsoleCommandWithWorldDelegate::CreateStatic(&RemoveVillageIn));

constexpr int32 NavHalfSize = 72;          // cells around the village centre the NPCs can walk
constexpr float CellsPerTravelHour = 24.f; // abstract-map scale used by the LIS travel cost
constexpr float ReachCells = 4.5f;

ELisVoxelResource ClassifyBlock(uint8 Raw)
{
    switch (static_cast<EAethraBlock>(Raw))
    {
    case EAethraBlock::Wood:
    case EAethraBlock::BirchWood: return ELisVoxelResource::Wood;
    case EAethraBlock::Leaves:
    case EAethraBlock::PineLeaves:
    case EAethraBlock::BirchLeaves: return ELisVoxelResource::Foliage;
    case EAethraBlock::TallGrass:
    case EAethraBlock::FlowerRed:
    case EAethraBlock::FlowerYellow: return ELisVoxelResource::Crop;
    case EAethraBlock::Water: return ELisVoxelResource::Water;
    case EAethraBlock::Stone:
    case EAethraBlock::Snow: return ELisVoxelResource::Stone;
    case EAethraBlock::Grass:
    case EAethraBlock::Dirt:
    case EAethraBlock::Sand: return ELisVoxelResource::Soil;
    default: return ELisVoxelResource::None;
    }
}

int32 LisActorOf(const FAethraInstigator& By)
{
    switch (By.Kind)
    {
    case EAethraInstigatorKind::Player: return LisPlayer - By.Id; // slot 0 -> -1
    case EAethraInstigatorKind::Npc: return By.Id;
    default: return LisNobody;
    }
}

FVector CellCentre(const FIntVector& Cell)
{
    return (FVector(Cell) + FVector(.5)) * AethraTerrain::CellSizeCm;
}

int32 WanderFor(ELisAction Action)
{
    switch (Action)
    {
    case ELisAction::Patrol: return 8;
    case ELisAction::Explore: return 10;
    default: return 0;
    }
}

bool IsWorking(ELisAction Action)
{
    return Action == ELisAction::Chop || Action == ELisAction::Farm || Action == ELisAction::Build || Action == ELisAction::Tend || Action == ELisAction::Fish;
}

// Stable per-villager draws for the look (independent of the game hour).
float Hash01(int32 Id, uint32 Salt)
{
    const uint32 Data[2] = { static_cast<uint32>(Id), Salt };
    return (FCrc::MemCrc32(Data, sizeof(Data)) & 0xFFFF) / 65535.f;
}

FLinearColor Shade(const FColor& Base, float Amount)
{
    return FLinearColor(Base) * Amount;
}

FColor TunicFor(ELisProfession Profession)
{
    switch (Profession)
    {
    case ELisProfession::Farmer: return FColor(96, 132, 50);
    case ELisProfession::Fisher: return FColor(52, 98, 152);
    case ELisProfession::Woodcutter: return FColor(158, 46, 36);
    case ELisProfession::Merchant: return FColor(116, 56, 140);
    case ELisProfession::Guard: return FColor(108, 114, 124);
    case ELisProfession::Explorer: return FColor(178, 112, 42);
    case ELisProfession::Builder: return FColor(196, 124, 46);
    default: return FColor(128, 128, 128);
    }
}
}

AAethraLisDirector::AAethraLisDirector()
{
    PrimaryActorTick.bCanEverTick = true;
}

AAethraLisDirector::~AAethraLisDirector() = default;

AAethraLisNpc* AAethraLisDirector::BodyOf(int32 NpcId) const
{
    return Bodies.IsValidIndex(NpcId - 1) ? Bodies[NpcId - 1].Get() : nullptr;
}

FIntVector AAethraLisDirector::Anchor(ELisPlace Place) const
{
    return Village ? Village->Sim.World.Place(Place).Cell : FIntVector::ZeroValue;
}

void AAethraLisDirector::Log(const FString& Line)
{
    UE_LOG(LogAethraLisWorld, Log, TEXT("%s"), *Line);
    RecentLog.Add(Line);
    if (RecentLog.Num() > 6)
    {
        RecentLog.RemoveAt(0);
    }
}

void AAethraLisDirector::Fail(AAethraLisNpc& Body, const FLisNpc& Npc, const FString& What)
{
    Body.NoteFailure(FString::Printf(TEXT("h%lld %s"), Village->Sim.Hour, *What));
    Log(FString::Printf(TEXT("%s: %s"), *Npc.Identity.Name, *What));
}

FIntVector AAethraLisDirector::Snap(const FIntVector& Cell, int32 Radius)
{
    FIntVector Out;
    return NavGrid->NearestStand(Cell, Radius, Out) ? Out : Cell;
}

bool AAethraLisDirector::Start(AAethraVoxelWorld* InWorld, FIntPoint Center, int32 NpcCount, const FString& SavePath)
{
    if (!InWorld || Village)
    {
        return false;
    }
    const double Begin = FPlatformTime::Seconds();
    Voxels = InWorld;
    Voxels->PrepareSpawn(); // loads saved voxel edits before any column is read
    SaveFile = SavePath.IsEmpty()
        ? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LIS"), FString::Printf(TEXT("World_%d.lis"), Voxels->Seed))
        : SavePath;

    Village = MakeUnique<FLisElden>(static_cast<uint32>(Voxels->Seed), ELisRoster::LivingVillage);
    FLisSimulation& S = Village->Sim;
    const bool bRestored = IFileManager::Get().FileExists(*SaveFile) && S.LoadFromFile(SaveFile);
    if (!bRestored)
    {
        if (NpcCount < S.Npcs.Num())
        {
            S.Npcs.SetNum(FMath::Max(1, NpcCount)); // relations to removed ids are simply never found
        }
        S.Hour = 7; // the village wakes up
    }
    const FIntPoint NavCenter = bRestored
        ? FIntPoint(S.World.Place(ELisPlace::Village).Cell.X, S.World.Place(ELisPlace::Village).Cell.Y)
        : Center;
    NavGrid = MakeUnique<FAethraLisNav>();
    NavGrid->Initialize(Voxels, NavCenter, NavHalfSize);

    // The village layout: planned once (also for an LIS 02 save that has none), then kept in the LIS save.
    FIntPoint Home = NavCenter;
    const bool bPlan = S.World.Sites.Num() == 0;
    if (bPlan)
    {
        S.World.Sites = FAethraLisVillage::Plan(*Voxels, *NavGrid, NavCenter, VillageReport, bDemoVillage);
        if (S.World.Sites.Num())
        {
            // The walkable area is centred on the square, as it will be after every restart.
            Home = FAethraLisVillage::ToWorld(S.World.Sites[0], 1, 1);
            NavGrid->Initialize(Voxels, Home, NavHalfSize);
        }
    }
    if (!bRestored)
    {
        ChooseAnchors(Home);
    }
    Houses.Setup(*Voxels, S.World.Sites, bPlan ? NavGrid.Get() : nullptr);
    if (bPlan || !bRestored)
    {
        UseVillageAnchors();
    }
    if (!bPlan)
    {
        VillageReport = FString::Printf(TEXT("Vila restaurada: %d locais, %d blocos."), S.World.Sites.Num(), Houses.BlockCount());
    }
    bBuilding = Houses.GetSites().Num() > 0 && !Houses.IsBuilt();
    if (bBuilding)
    {
        UseVillageAnchors(); // planned cells while the village goes up
    }
    Log(VillageReport);

    // Hooks and world-integration settings are not part of the save: set them after loading.
    S.Config.bExternalTravel = true;
    S.Config.bLivingVillage = true;
    S.Perceives = [this](const FLisNpc& Npc, const FIntVector& Cell)
    {
        const AAethraLisNpc* Body = BodyOf(Npc.Identity.Id);
        if (!Body || Body->IsLying())
        {
            return false; // asleep: sees nothing
        }
        const FVector Offset = CellCentre(Cell) - Body->GetActorLocation();
        return Offset.Size2D() <= SightCells * AethraTerrain::CellSizeCm && FMath::Abs(Offset.Z) <= 10 * AethraTerrain::CellSizeCm;
    };
    S.OnDecision = [this](const FLisNpc& Npc, const TArray<FLisOption>& Options)
    {
        TArray<FLisOption> Sorted = Options;
        Sorted.Sort([](const FLisOption& A, const FLisOption& B) { return A.Score > B.Score; });
        Sorted.SetNum(FMath::Min(4, Sorted.Num()));
        LastOptions.Add(Npc.Identity.Id, MoveTemp(Sorted));
    };
    Village->Adapter.Classify = &ClassifyBlock;
    Village->Adapter.PlaceRadiusCells = 30.f;
    Voxels->OnBlockChanged.AddUniqueDynamic(this, &AAethraLisDirector::HandleBlockChanged);

    SpawnBodies(bRestored);
    bShots = FParse::Param(FCommandLine::Get(), TEXT("AethraLisShots"));
    DebugDrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateUObject(this, &AAethraLisDirector::DrawDebug));
    Log(FString::Printf(TEXT("LIS: %s %d NPCs at (%d, %d) in %.1f ms (%d columns read)."), bRestored ? TEXT("restored") : TEXT("started"),
        S.Npcs.Num(), NavCenter.X, NavCenter.Y, (FPlatformTime::Seconds() - Begin) * 1000.0, NavGrid->ColumnsComputed));
    return true;
}

void AAethraLisDirector::ChooseAnchors(FIntPoint Center)
{
    FAethraLisNav& N = *NavGrid;
    FLisSimulation& S = Village->Sim;
    FIntPoint VillageAt = Center;
    if (!N.NearestWalkable(Center, 16, VillageAt))
    {
        Log(TEXT("LIS: no walkable ground near the village centre."));
    }

    // One pass over the area: trees, river banks and flat open ground.
    TArray<FIntPoint> Trunks, Banks, Flat, Far;
    const int32 R = NavHalfSize - 4;
    for (int32 Y = -R; Y <= R; ++Y)
    {
        for (int32 X = -R; X <= R; ++X)
        {
            const FIntPoint P = VillageAt + FIntPoint(X, Y);
            if (!N.InBounds(P))
            {
                continue;
            }
            const FAethraNavColumn& C = N.Column(P);
            if (C.TrunkBaseZ >= 0)
            {
                Trunks.Add(P);
            }
            if (C.StandZ < 0)
            {
                continue;
            }
            const float Distance = FVector2D(X, Y).Size();
            bool bWaterNext = false;
            int32 Level = 0;
            for (const FIntPoint D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
            {
                const FAethraNavColumn& Next = N.Column(P + D);
                bWaterNext |= Next.bWater;
                Level += Next.StandZ >= 0 && FMath::Abs(Next.StandZ - C.StandZ) == 0;
            }
            if (bWaterNext && Distance >= 6)
            {
                Banks.Add(P);
            }
            if (Level == 4 && Distance >= 8 && Distance <= 28)
            {
                Flat.Add(P);
            }
            if (Distance >= 38 && Distance <= 58)
            {
                Far.Add(P);
            }
        }
    }
    auto ByDistance = [VillageAt](TArray<FIntPoint>& List, FIntPoint Prefer)
    {
        List.Sort([VillageAt, Prefer](FIntPoint A, FIntPoint B)
        {
            return (A - VillageAt - Prefer).SizeSquared() < (B - VillageAt - Prefer).SizeSquared();
        });
    };
    // Forest: a walkable spot next to the densest group of trunks near the village.
    TArray<FIntPoint> ForestSpots;
    for (const FIntPoint T : Trunks)
    {
        int32 Around = 0;
        for (const FIntPoint U : Trunks)
        {
            Around += (U - T).SizeSquared() <= 36;
        }
        FIntPoint Stand;
        if (Around >= 3 && N.NearestWalkable(T, 2, Stand))
        {
            ForestSpots.Add(Stand);
        }
    }
    TArray<FIntPoint> Markets;
    N.NearestWalkable(VillageAt + FIntPoint(-7, 3), 6, Markets.AddDefaulted_GetRef());

    ByDistance(ForestSpots, FIntPoint::ZeroValue);
    ByDistance(Banks, FIntPoint::ZeroValue);
    ByDistance(Flat, FIntPoint(12, -6));
    Far.Sort([VillageAt](FIntPoint A, FIntPoint B) { return (A - VillageAt).SizeSquared() > (B - VillageAt).SizeSquared(); });

    auto Pick = [&](ELisPlace Place, const TArray<FIntPoint>& Candidates, FIntPoint Fallback)
    {
        TArray<FIntVector> Path;
        for (int32 I = 0; I < Candidates.Num() && I < 12; ++I)
        {
            if (N.FindPath(VillageAt, Candidates[I], Path))
            {
                S.World.Place(Place).Cell = N.Stand(Candidates[I]);
                return;
            }
        }
        FIntPoint Near = VillageAt;
        N.NearestWalkable(VillageAt + Fallback, 8, Near);
        S.World.Place(Place).Cell = N.Stand(Near);
        Log(FString::Printf(TEXT("LIS: no reachable %s found; using a stand-in spot."), LisName(Place)));
    };
    S.World.Place(ELisPlace::Village).Cell = N.Stand(VillageAt);
    Pick(ELisPlace::Market, Markets, FIntPoint(-7, 3));
    Pick(ELisPlace::Fields, Flat, FIntPoint(12, -6));
    Pick(ELisPlace::Forest, ForestSpots, FIntPoint(-14, 14));
    Pick(ELisPlace::River, Banks, FIntPoint(0, 18));
    Pick(ELisPlace::Wilds, Far, FIntPoint(40, 20));
}

void AAethraLisDirector::UseVillageAnchors()
{
    FLisSimulation& S = Village->Sim;
    // The square, the stall and the farm stand for the LIS Village, Market and Fields. Until the village
    // stands, the planned cells are used as they are (trees may still be on the plots); once built
    // they are snapped to the nav grid.
    auto Settle = [this](const FIntVector& Planned) { return bBuilding ? Planned : Snap(Planned); };
    if (Houses.FindSite(EAethraSite::Square))
    {
        S.World.Place(ELisPlace::Village).Cell = Settle(Houses.SquareSpot(0));
    }
    if (Houses.FindSite(EAethraSite::Market))
    {
        S.World.Place(ELisPlace::Market).Cell = Settle(Houses.StallSpot(false, 1));
    }
    if (const FLisSite* Farm = Houses.FindSite(EAethraSite::Farm))
    {
        S.World.Place(ELisPlace::Fields).Cell = Settle(Houses.CropCells().Num() ? Houses.CropCells()[0] : Houses.DoorSpot(*Farm));
    }
    // The abstract map follows the real layout, so travel costs in the utility match real distances.
    const FIntVector Home = S.World.Place(ELisPlace::Village).Cell;
    for (FLisPlaceState& Place : S.World.Places)
    {
        Place.Pos = FVector2f(Place.Cell.X - Home.X, Place.Cell.Y - Home.Y) / CellsPerTravelHour;
    }
    for (int32 P = 0; P < LisNum<ELisPlace>(); ++P)
    {
        const FIntVector C = S.World.Places[P].Cell;
        Log(FString::Printf(TEXT("LIS: %s at (%d, %d, %d)"), LisName(static_cast<ELisPlace>(P)), C.X, C.Y, C.Z));
    }
}

FIntVector AAethraLisDirector::SpotFor(const FLisNpc& Npc, ELisPlace Place)
{
    // A personal spot around the anchor, so villagers do not stand on top of each other.
    const FIntVector A = Anchor(Place);
    const float Angle = Npc.Identity.Id * 2.39996f;
    const float Radius = 1.5f + Npc.Identity.Id % 4;
    const FIntVector Wanted(A.X + FMath::RoundToInt(Radius * FMath::Cos(Angle)), A.Y + FMath::RoundToInt(Radius * FMath::Sin(Angle)), A.Z);
    return Snap(Wanted, 4);
}

FIntVector AAethraLisDirector::ActivitySpot(const FLisNpc& Npc, ELisPlace Place, ELisAction Action, const AAethraLisNpc& Body)
{
    const int32 Id = Npc.Identity.Id;
    if (Houses.GetSites().Num() == 0)
    {
        return SpotFor(Npc, Place);
    }
    const int32 Clock = static_cast<int32>(Village->Sim.Hour % 24);
    switch (Place)
    {
    case ELisPlace::Village:
        switch (Action)
        {
        case ELisAction::Sleep: return Snap(Houses.HomeSpot(Id), 2);
        case ELisAction::Eat: return Snap(Houses.RestSpot(Id), 2);
        case ELisAction::Idle: return Snap(Clock >= 20 || Clock < 6 ? Houses.HomeSpot(Id) : Id % 2 ? Houses.RestSpot(Id) : Houses.SquareSpot(Id), 2);
        case ELisAction::Build:
        {
            FIntVector Damage;
            if (Body.Phase == 2 && RecentDamage(Npc, Damage))
            {
                return Snap(Damage, 3);
            }
            return Snap(Body.Phase == 0 ? Houses.StorageSpot() : Houses.WorkbenchSpot(Id), 2);
        }
        default: return Snap(Houses.SquareSpot(Id), 2);
        }
    case ELisPlace::Market:
        // One merchant behind the counter, the rest (and customers) in front of the stall.
        return Snap(Action == ELisAction::Tend && Id % 3 == 0 ? Houses.StallSpot(true, 0) : Houses.StallSpot(false, Id), 2);
    case ELisPlace::Fields:
        return Snap(Anchor(ELisPlace::Fields), 2);
    default:
        return SpotFor(Npc, Place);
    }
}

FAethraVillagerLook AAethraLisDirector::LookFor(const FLisNpc& Npc) const
{
    static const FColor Skins[] = { FColor(236, 196, 160), FColor(222, 170, 128), FColor(196, 140, 100), FColor(160, 110, 76), FColor(124, 84, 58), FColor(96, 64, 44) };
    static const FColor Hairs[] = { FColor(28, 22, 20), FColor(62, 40, 24), FColor(108, 68, 36), FColor(146, 64, 32), FColor(206, 166, 96) };
    static const FColor Trousers[] = { FColor(88, 72, 56), FColor(62, 64, 78), FColor(72, 56, 46), FColor(98, 92, 82), FColor(52, 58, 48) };
    static const FColor Accents[] = { FColor(214, 184, 104), FColor(232, 230, 220), FColor(172, 62, 52), FColor(64, 96, 64), FColor(70, 90, 150) };
    const int32 Id = Npc.Identity.Id;
    auto Pick = [Id](uint32 Salt, int32 Count) { return FMath::Min(Count - 1, static_cast<int32>(Hash01(Id, Salt) * Count)); };

    FAethraVillagerLook Look;
    Look.Regions[0] = FLinearColor(Skins[Pick(1, UE_ARRAY_COUNT(Skins))]);
    Look.Regions[1] = Shade(TunicFor(Npc.Identity.Profession), .85f + .3f * Hash01(Id, 2));
    Look.Regions[2] = FLinearColor(Trousers[Pick(3, UE_ARRAY_COUNT(Trousers))]);
    Look.Regions[3] = Shade(FColor(92, 56, 30), .75f + .4f * Hash01(Id, 4));
    Look.Regions[4] = Npc.Identity.Age > 55 ? FLinearColor(FColor(168, 166, 160)) : FLinearColor(Hairs[Pick(5, UE_ARRAY_COUNT(Hairs))]);
    Look.Regions[5] = FLinearColor(FColor(20, 18, 20));
    Look.Regions[6] = FLinearColor(FColor(236, 236, 230));
    Look.Regions[7] = FLinearColor(Accents[Pick(6, UE_ARRAY_COUNT(Accents))]);
    Look.bLongHair = Hash01(Id, 7) < .45f;
    Look.Height = .93f + .13f * Hash01(Id, 8) - (Npc.Identity.Age < 20 ? .05f : 0.f);
    Look.Build = .94f + .12f * Hash01(Id, 9);
    switch (Npc.Identity.Profession)
    {
    case ELisProfession::Farmer:
        Look.Hat = EAethraVillagerHat::Straw;
        Look.HatColors[0] = FLinearColor(FColor(218, 182, 96));
        Look.HatColors[1] = FLinearColor(FColor(132, 40, 30));
        break;
    case ELisProfession::Fisher:
        Look.Hat = EAethraVillagerHat::Straw;
        Look.HatColors[0] = FLinearColor(FColor(206, 178, 110));
        Look.HatColors[1] = FLinearColor(FColor(40, 76, 130));
        break;
    case ELisProfession::Woodcutter:
        Look.Hat = EAethraVillagerHat::Beanie;
        Look.HatColors[0] = Hash01(Id, 10) < .5f ? FLinearColor(FColor(70, 92, 56)) : FLinearColor(FColor(128, 36, 28));
        Look.HatColors[1] = FLinearColor(FColor(214, 206, 190));
        break;
    case ELisProfession::Builder:
        Look.Hat = EAethraVillagerHat::Work;
        Look.HatColors[0] = FLinearColor(FColor(128, 82, 44));
        Look.HatColors[1] = FLinearColor(FColor(70, 44, 24));
        break;
    case ELisProfession::Merchant:
        Look.Hat = EAethraVillagerHat::Merchant;
        Look.HatColors[0] = FLinearColor(FColor(84, 30, 100));
        Look.HatColors[1] = FLinearColor(FColor(222, 184, 80));
        Look.bSatchel = true;
        break;
    case ELisProfession::Guard:
        Look.Hat = EAethraVillagerHat::Helmet;
        Look.HatColors[0] = FLinearColor(FColor(150, 156, 162));
        Look.HatColors[1] = FLinearColor(FColor(90, 92, 98));
        break;
    default:
        Look.Hat = EAethraVillagerHat::Beanie;
        Look.HatColors[0] = FLinearColor(FColor(120, 84, 50));
        Look.HatColors[1] = FLinearColor(FColor(200, 190, 170));
        break;
    }
    return Look;
}

void AAethraLisDirector::SpawnBodies(bool bRestored)
{
    for (const FLisNpc& Npc : Village->Sim.Npcs)
    {
        // A restored villager reappears where it stood; a new one starts around the square
        // (never inside a house that is about to be built).
        FIntVector Spot = Houses.GetSites().Num() ? Snap(Houses.SquareSpot(Npc.Identity.Id + 3)) : SpotFor(Npc, Npc.Place);
        if (bRestored && Npc.WorldCell.Z >= 0)
        {
            Spot = Snap(Npc.WorldCell, 2); // mid-step positions land on the nearest standing cell
        }
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Params.ObjectFlags |= RF_Transient;
        const FVector Location((Spot.X + .5) * AethraTerrain::CellSizeCm, (Spot.Y + .5) * AethraTerrain::CellSizeCm,
            Spot.Z * AethraTerrain::CellSizeCm + 90.0);
        AAethraLisNpc* Body = GetWorld()->SpawnActor<AAethraLisNpc>(Location, FRotator(0, Npc.Identity.Id * 37.f, 0), Params);
        if (!Body)
        {
            continue;
        }
        Body->NpcId = Npc.Identity.Id;
        Body->Spot = Spot;
        Body->Goal = Spot;
        Body->SetLook(LookFor(Npc));
#if WITH_EDITOR
        Body->SetActorLabel(FString::Printf(TEXT("LIS_%02d_%s"), Npc.Identity.Id, *Npc.Identity.Name));
#endif
        Bodies.Add(Body);
    }
}

void AAethraLisDirector::RequestPath(AAethraLisNpc& Body, FIntVector Goal)
{
    if (Body.Goal == Goal && (Body.NavState == EAethraNavState::Walking || Body.NavState == EAethraNavState::Waiting))
    {
        return;
    }
    Body.Goal = Goal;
    Body.NavState = EAethraNavState::Waiting;
    PathQueue.AddUnique(Body.NpcId);
}

void AAethraLisDirector::ServePaths()
{
    // Budget: at most PathsPerFrame A* searches per frame; the rest wait their turn.
    for (int32 Served = 0; Served < PathsPerFrame && PathQueue.Num(); ++Served)
    {
        const int32 Id = PathQueue[0];
        PathQueue.RemoveAt(0);
        AAethraLisNpc* Body = BodyOf(Id);
        FLisNpc* Npc = Village->Sim.Find(Id);
        if (!Body || !Npc || Body->NavState != EAethraNavState::Waiting)
        {
            continue;
        }
        const FIntVector From = Snap(Body->CurrentCell(), 3);
        TArray<FIntVector> Path;
        if (NavGrid->FindPath(From, Body->Goal, Path))
        {
            // Long trips and frightened villagers run.
            Body->bRunning = Path.Num() > 40 || Npc->Need(ELisNeed::Safety) > .6f;
            Body->FollowPath(Path, Body->Goal);
            continue;
        }
        Body->NavState = EAethraNavState::Failed;
        Fail(*Body, *Npc, FString::Printf(TEXT("sem caminho de (%d, %d, %d) até (%d, %d, %d)"), From.X, From.Y, From.Z, Body->Goal.X, Body->Goal.Y, Body->Goal.Z));
        if (Npc->Current == ELisAction::Travel)
        {
            Village->Sim.TravelFailed(Id); // the LIS replans with what it learned (and remembers the blocked way)
        }
        else if (Npc->Current != ELisAction::Idle)
        {
            Village->Sim.Interrupt(Id); // cannot reach the work spot any more
        }
    }
}

bool AAethraLisDirector::FindTree(const AAethraLisNpc& Body, FIntVector& OutStand, FIntVector& OutTrunk)
{
    const FIntVector Forest = Anchor(ELisPlace::Forest);
    const FIntVector Here = Body.CurrentCell();
    int32 BestDistance = MAX_int32;
    for (int32 Y = -12; Y <= 12; ++Y)
    {
        for (int32 X = -12; X <= 12; ++X)
        {
            const FIntPoint T(Forest.X + X, Forest.Y + Y);
            const int16 Base = NavGrid->Column(T).TrunkBaseZ;
            if (Base < 0 || Houses.IsVillageCell(FIntVector(T.X, T.Y, Base)))
            {
                continue; // village posts are not trees
            }
            for (const FIntPoint D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
            {
                const FIntPoint S = T + D;
                if (!NavGrid->Walkable(S))
                {
                    continue;
                }
                const FIntVector Stand = NavGrid->StandNear(S, Base);
                const int32 Distance = (S - FIntPoint(Here.X, Here.Y)).SizeSquared();
                if (FMath::Abs(Stand.Z - Base) <= 1 && Distance < BestDistance)
                {
                    BestDistance = Distance;
                    OutStand = Stand;
                    OutTrunk = FIntVector(T.X, T.Y, Base);
                }
            }
        }
    }
    return BestDistance != MAX_int32;
}

bool AAethraLisDirector::FindCrop(const AAethraLisNpc& Body, FIntVector& OutStand, FIntVector& OutCrop)
{
    const TArray<FIntVector>& Crops = Houses.CropCells();
    if (Crops.Num() == 0)
    {
        return false;
    }
    // Each farmer starts in its own part of the plot and moves along it; a bed another farmer is
    // working is left to them.
    TSet<FIntVector> Claimed;
    for (const AAethraLisNpc* Other : Bodies)
    {
        const FLisNpc* Npc = Other && Other != &Body ? Village->Sim.Find(Other->NpcId) : nullptr;
        if (Npc && Npc->Current == ELisAction::Farm)
        {
            Claimed.Add(Other->ActionTarget);
        }
    }
    const int32 Start = (Body.NpcId * 5 + Body.PathReplans + static_cast<int32>(Village->Sim.Hour)) % Crops.Num();
    for (int32 I = 0; I < Crops.Num(); ++I)
    {
        const FIntVector Crop = Crops[(Start + I) % Crops.Num()];
        if (Claimed.Contains(Crop))
        {
            continue;
        }
        // Stand on the walkway next to the crop row (one cell along the farm's depth).
        for (const FIntVector D : { FIntVector(0, 1, 0), FIntVector(0, -1, 0), FIntVector(1, 0, 0), FIntVector(-1, 0, 0) })
        {
            const FIntVector Stand = Crop + D;
            if (NavGrid->IsStand(Stand) && !Houses.IsCropCell(Stand))
            {
                OutStand = Stand;
                OutCrop = Crop;
                return true;
            }
        }
    }
    return false;
}

bool AAethraLisDirector::RecentDamage(const FLisNpc& Npc, FIntVector& OutCell) const
{
    const FLisSimulation& S = Village->Sim;
    for (int32 I = Npc.Memories.Num() - 1; I >= 0; --I)
    {
        const FLisMemory& M = Npc.Memories[I];
        if (M.Type != ELisEvent::StructureDamaged || S.Hour - M.Hour > 24)
        {
            continue;
        }
        for (int32 E = S.Events.Num() - 1; E >= 0; --E)
        {
            if (S.Events[E].Id == M.EventId)
            {
                OutCell = S.Events[E].Cell;
                return true;
            }
        }
    }
    return false;
}

EAethraVillagerAnim AAethraLisDirector::AnimFor(const FLisNpc& Npc, const AAethraLisNpc& Body)
{
    using EAnim = EAethraVillagerAnim;
    if (Body.NavState == EAethraNavState::Walking)
    {
        return Body.bRunning ? EAnim::Run : EAnim::Walk;
    }
    if (Body.NavState == EAethraNavState::Waiting || Npc.Current == ELisAction::Travel)
    {
        return EAnim::Idle;
    }
    switch (Npc.Current)
    {
    case ELisAction::Sleep: return Body.IsLying() ? EAnim::Sleep : EAnim::Idle;
    case ELisAction::Eat: return EAnim::Eat;
    case ELisAction::Farm: return Body.Phase == 1 ? EAnim::Gather : EAnim::Hoe;
    case ELisAction::Chop: return EAnim::Chop;
    case ELisAction::Fish: return EAnim::Fish;
    case ELisAction::Build: return Body.Phase == 0 ? EAnim::Gather : Body.Phase == 2 ? EAnim::Inspect : EAnim::Hammer;
    case ELisAction::Tend:
    case ELisAction::Sell:
    case ELisAction::BuyFood: return EAnim::Trade;
    case ELisAction::Socialize:
    case ELisAction::AskHelp:
    case ELisAction::GiveHelp: return EAnim::Talk;
    case ELisAction::Idle: return Body.Phase == 3 ? EAnim::Sit : EAnim::Idle;
    default: return EAnim::Idle;
    }
}

EAethraVillagerTool AAethraLisDirector::ToolFor(const FLisNpc& Npc, const AAethraLisNpc& Body)
{
    switch (Npc.Current)
    {
    case ELisAction::Chop: return EAethraVillagerTool::Axe;
    case ELisAction::Farm: return Body.Phase == 1 ? EAethraVillagerTool::Basket : EAethraVillagerTool::Hoe;
    case ELisAction::Build: return Body.Phase == 0 ? EAethraVillagerTool::Basket : Body.Phase == 1 ? EAethraVillagerTool::Hammer : EAethraVillagerTool::None;
    default: return EAethraVillagerTool::None;
    }
}

void AAethraLisDirector::SyncBody(AAethraLisNpc& Body, const FLisNpc& Npc, float DeltaSeconds)
{
    const uint64 Key = (static_cast<uint64>(Npc.Decisions) << 16) | (static_cast<uint64>(Npc.Current) << 8) | static_cast<uint64>(Npc.Place);
    const bool bNewStep = Key != Body.ShownKey;
    Body.ShownKey = Key;
    const int32 Id = Npc.Identity.Id;
    const bool bMoving = Body.NavState == EAethraNavState::Walking || Body.NavState == EAethraNavState::Waiting;

    if (Npc.Current == ELisAction::Travel)
    {
        Body.SetLying(false);
        if (bNewStep || Body.NavState == EAethraNavState::Idle || Body.NavState == EAethraNavState::Arrived)
        {
            Body.Phase = 0;
            Body.Spot = ActivitySpot(Npc, Npc.IntentPlace, Npc.Intent, Body);
            RequestPath(Body, Body.Spot);
        }
        Body.Activity = FString::Printf(TEXT("a caminho de %s para %s"), LisName(Npc.IntentPlace), LisName(Npc.Intent));
        return;
    }

    if (bNewStep)
    {
        Body.WanderRadius = WanderFor(Npc.Current);
        Body.bActionPending = false;
        Body.ActionClock = 0.f;
        Body.Phase = 0;
        Body.bRunning = false;
        Body.Spot = ActivitySpot(Npc, Npc.Place, Npc.Current, Body);
        switch (Npc.Current)
        {
        case ELisAction::Chop:
        {
            FIntVector Stand, Trunk;
            if (!FindTree(Body, Stand, Trunk))
            {
                Fail(Body, Npc, TEXT("nenhuma árvore alcançável na floresta"));
                Village->Sim.Interrupt(Id);
                return;
            }
            Body.Spot = Stand;
            Body.ActionTarget = Trunk;
            Body.bActionPending = true;
            break;
        }
        case ELisAction::Farm:
        {
            FIntVector Stand, Crop;
            if (FindCrop(Body, Stand, Crop))
            {
                Body.Spot = Stand;
                Body.ActionTarget = Crop;
                Body.bActionPending = true;
            }
            break;
        }
        case ELisAction::Build:
        {
            FIntVector Damage;
            if (Village->Sim.World.WoodStore < 1.f && RecentDamage(Npc, Damage))
            {
                Body.Phase = 2; // nothing to prepare: go and look at the damage instead
                Body.ActionTarget = Damage;
                Body.Spot = ActivitySpot(Npc, Npc.Place, Npc.Current, Body);
            }
            break;
        }
        case ELisAction::Idle:
            if (Houses.GetSites().Num() && Body.Spot == Snap(Houses.RestSpot(Id), 2))
            {
                Body.Phase = 3; // resting by the fire
            }
            break;
        default:
            break;
        }
        if (Body.CurrentCell() != Body.Spot)
        {
            RequestPath(Body, Body.Spot);
        }
    }

    const bool bAtSpot = !bMoving && Body.NavState != EAethraNavState::Failed && Body.CurrentCell() == Body.Spot;
    // One block change per villager per game hour: wait (and keep working) instead of asking again.
    const int64* LastChange = LastBlockHour.Find(Id);
    const bool bMayChange = !LastChange || *LastChange != Village->Sim.Hour;
    Body.SetLying(Npc.Current == ELisAction::Sleep && bAtSpot);
    if (bAtSpot)
    {
        Body.ActionClock += DeltaSeconds;
    }

    switch (Npc.Current)
    {
    case ELisAction::Chop:
        if (bAtSpot)
        {
            Body.FaceTowards(CellCentre(Body.ActionTarget));
            // Someone else felled it first: look for the next trunk instead of asking for nothing.
            const EAethraBlock Target = Voxels->GetBlock(Body.ActionTarget);
            if (Body.bActionPending && Target != EAethraBlock::Wood && Target != EAethraBlock::BirchWood)
            {
                Body.bActionPending = false;
                Body.ActionClock = 2.f;
            }
            // A few swings, then the strike: the world is asked to take one block of wood.
            if (Body.bActionPending && Body.ActionClock >= StrikeSeconds && bMayChange)
            {
                Body.bActionPending = false;
                Body.ActionClock = 0.f;
                RequestBlock({ Id, Body.ActionTarget, Voxels->GetBlock(Body.ActionTarget), EAethraBlock::Air });
            }
            else if (!Body.bActionPending && Body.ActionClock >= 2.f)
            {
                FIntVector Stand, Trunk;
                if (FindTree(Body, Stand, Trunk))
                {
                    Body.Spot = Stand;
                    Body.ActionTarget = Trunk;
                    Body.bActionPending = true;
                    Body.ActionClock = 0.f;
                    if (Body.CurrentCell() != Stand)
                    {
                        RequestPath(Body, Stand);
                    }
                }
            }
        }
        Body.Activity = TEXT("cortando lenha");
        break;
    case ELisAction::Farm:
        if (bAtSpot)
        {
            Body.FaceTowards(CellCentre(Body.ActionTarget));
            if (Body.Phase == 0 && Body.bActionPending && Body.ActionClock >= StrikeSeconds && bMayChange)
            {
                // Harvest a ripe crop or sow an empty bed: one real block change, validated by the director.
                const EAethraBlock Now = Voxels->GetBlock(Body.ActionTarget);
                RequestBlock({ Id, Body.ActionTarget, Now, Now == EAethraBlock::TallGrass ? EAethraBlock::Air : EAethraBlock::TallGrass });
                Body.bActionPending = false;
                Body.Phase = 1;
                Body.ActionClock = 0.f;
            }
            else if (Body.Phase == 1 && Body.ActionClock >= 2.4f)
            {
                Body.Phase = 0;
                Body.ActionClock = 0.f;
                FIntVector Stand, Crop;
                ++Body.PathReplans; // next bed along the row
                if (FindCrop(Body, Stand, Crop))
                {
                    Body.Spot = Stand;
                    Body.ActionTarget = Crop;
                    Body.bActionPending = true;
                    if (Body.CurrentCell() != Stand)
                    {
                        RequestPath(Body, Stand);
                    }
                }
            }
        }
        Body.Activity = Body.Phase == 1 ? TEXT("colhendo/semeando") : TEXT("cuidando da plantação");
        break;
    case ELisAction::Build:
        if (bAtSpot && Body.Phase == 0)
        {
            Body.FaceTowards(CellCentre(Body.Spot + FIntVector(0, 0, 0)) + FVector(0, 100, 0));
            if (Body.ActionClock >= 3.f)
            {
                Body.Phase = 1; // material taken from the storage: to the workbench
                Body.ActionClock = 0.f;
                Body.Spot = ActivitySpot(Npc, Npc.Place, Npc.Current, Body);
                RequestPath(Body, Body.Spot);
            }
        }
        else if (bAtSpot && Body.Phase == 1)
        {
            Body.FaceTowards(CellCentre(Houses.WorkbenchTarget()));
        }
        else if (bAtSpot && Body.Phase == 2)
        {
            Body.FaceTowards(CellCentre(Body.ActionTarget));
        }
        Body.Activity = Body.Phase == 0 ? TEXT("buscando material no depósito") : Body.Phase == 2 ? TEXT("inspecionando a estrutura danificada") : TEXT("preparando material na oficina");
        break;
    case ELisAction::Tend:
    case ELisAction::Sell:
    case ELisAction::BuyFood:
        if (bAtSpot)
        {
            if (const FLisSite* Market = Houses.FindSite(EAethraSite::Market))
            {
                Body.FaceTowards(CellCentre(FIntVector(FAethraLisVillage::ToWorld(*Market, 1, 0).X, FAethraLisVillage::ToWorld(*Market, 1, 0).Y, Body.Spot.Z)));
            }
        }
        Body.Activity = Npc.Current == ELisAction::Tend ? TEXT("atendendo no mercado") : TEXT("negociando no mercado");
        break;
    case ELisAction::Sleep:
        Body.Activity = Body.IsLying() ? TEXT("dormindo em casa") : TEXT("indo dormir");
        break;
    case ELisAction::Eat:
        Body.Activity = TEXT("comendo");
        if (bAtSpot && Houses.GetSites().Num())
        {
            Body.FaceTowards(CellCentre(Houses.FireSpot()));
        }
        break;
    case ELisAction::Idle:
        Body.Activity = Body.Phase == 3 ? TEXT("descansando junto ao fogo") : TEXT("ocioso");
        if (bAtSpot && Body.Phase == 3)
        {
            Body.FaceTowards(CellCentre(Houses.FireSpot()));
        }
        break;
    case ELisAction::Fish:
        Body.Activity = TEXT("pescando");
        break;
    default:
        Body.Activity = LisName(Npc.Current);
        break;
    }

    // Social actions walk up to the other person, if they are around.
    if ((Npc.Current == ELisAction::Socialize || Npc.Current == ELisAction::GiveHelp || Npc.Current == ELisAction::AskHelp) && !bMoving)
    {
        if (const AAethraLisNpc* Other = BodyOf(Npc.IntentTarget))
        {
            const FVector Gap = Other->GetActorLocation() - Body.GetActorLocation();
            if (Gap.Size2D() > 220.0 && Gap.Size2D() < 3000.0)
            {
                RequestPath(Body, Snap(Other->CurrentCell() + FIntVector(1, 0, 0), 2));
            }
            else
            {
                Body.FaceTowards(Other->GetActorLocation());
            }
            Body.Activity = FString::Printf(TEXT("conversando com %s"), *Village->Sim.Find(Npc.IntentTarget)->Identity.Name);
        }
    }

    // Light wandering for patrols and exploration, inside a small radius of the spot.
    if (Body.WanderRadius > 0 && !bMoving && GetWorld()->GetTimeSeconds() >= Body.NextWander)
    {
        Body.NextWander = GetWorld()->GetTimeSeconds() + 4.f + 4.f * Village->Sim.Rand(Id, 9100 + Body.PathReplans);
        const float Angle = 6.2832f * Village->Sim.Rand(Id, 9200 + static_cast<uint32>(Body.NextWander));
        const FIntVector Wanted(Body.Spot.X + FMath::RoundToInt(Body.WanderRadius * FMath::Cos(Angle)),
            Body.Spot.Y + FMath::RoundToInt(Body.WanderRadius * FMath::Sin(Angle)), Body.Spot.Z);
        FIntVector Found;
        if (NavGrid->NearestStand(Wanted, 2, Found))
        {
            RequestPath(Body, Found);
        }
    }
}

EAethraBlockVerdict AAethraLisDirector::ApplyBlockRequest(const FAethraBlockRequest& Request)
{
    const FLisNpc* Npc = Village ? Village->Sim.Find(Request.NpcId) : nullptr;
    AAethraLisNpc* Body = BodyOf(Request.NpcId);
    auto Verdict = [&](EAethraBlockVerdict V, const TCHAR* Why)
    {
        if (V == EAethraBlockVerdict::Applied)
        {
            ++BlocksApplied;
        }
        else
        {
            ++BlocksRejected;
            if (Body && Npc)
            {
                Body->NoteFailure(FString::Printf(TEXT("h%lld bloco (%d, %d, %d) %s"), Village->Sim.Hour, Request.Cell.X, Request.Cell.Y, Request.Cell.Z, Why));
            }
        }
        Log(FString::Printf(TEXT("%s: bloco (%d, %d, %d) %s"), Npc ? *Npc->Identity.Name : TEXT("?"),
            Request.Cell.X, Request.Cell.Y, Request.Cell.Z, Why));
        return V;
    };
    if (!Npc || !Body || !Voxels)
    {
        return Verdict(EAethraBlockVerdict::NoSuchNpc, TEXT("recusado: NPC desconhecido"));
    }
    if (const int64* Previous = LastBlockHour.Find(Request.NpcId); Previous && *Previous == Village->Sim.Hour)
    {
        return Verdict(EAethraBlockVerdict::RateLimited, TEXT("recusado: limite de uma alteração por hora"));
    }
    if (Voxels->GetBlock(Request.Cell) != Request.Expected)
    {
        return Verdict(EAethraBlockVerdict::WrongBlock, TEXT("recusado: o bloco mudou"));
    }
    const FVector Eye = Body->GetActorLocation() + FVector(0, 0, 70.0);
    if (FVector::Dist(Eye, CellCentre(Request.Cell)) > ReachCells * AethraTerrain::CellSizeCm)
    {
        return Verdict(EAethraBlockVerdict::OutOfReach, TEXT("recusado: fora de alcance"));
    }
    // Rules: woodcutters (or anyone chopping) may fell tree wood, never a village block; farmers
    // working the plot may harvest or sow the village crop beds. Nothing else.
    const bool bWood = (Request.Expected == EAethraBlock::Wood || Request.Expected == EAethraBlock::BirchWood)
        && Request.Result == EAethraBlock::Air
        && (Npc->Current == ELisAction::Chop || Npc->Identity.Profession == ELisProfession::Woodcutter)
        && NavGrid->InBounds(FIntPoint(Request.Cell.X, Request.Cell.Y)) && !Houses.IsVillageCell(Request.Cell);
    const bool bCrop = Houses.IsCropCell(Request.Cell) && Npc->Current == ELisAction::Farm
        && ((Request.Expected == EAethraBlock::TallGrass && Request.Result == EAethraBlock::Air)
            || (Request.Expected == EAethraBlock::Air && Request.Result == EAethraBlock::TallGrass));
    if (!bWood && !bCrop)
    {
        return Verdict(EAethraBlockVerdict::NotAllowed, TEXT("recusado: ação não permitida"));
    }
    if (!Voxels->SetBlockBy(Request.Cell, Request.Result, FAethraInstigator::Npc(Request.NpcId)))
    {
        return Verdict(EAethraBlockVerdict::Refused, TEXT("recusado pelo mundo voxel"));
    }
    LastBlockHour.Add(Request.NpcId, Village->Sim.Hour);
    if (bWood)
    {
        Village->Sim.ReportYield(Request.NpcId, 1.f); // the LIS counts only the wood really felled
        return Verdict(EAethraBlockVerdict::Applied, TEXT("removido (madeira)"));
    }
    return Verdict(EAethraBlockVerdict::Applied, Request.Result == EAethraBlock::Air ? TEXT("colhido") : TEXT("semeado"));
}

void AAethraLisDirector::HandleBlockChanged(const FAethraBlockChange& Change)
{
    // Called inside SetBlock's broadcast: only record. No world edits from here.
    if (!Village)
    {
        return;
    }
    const bool bVillageWork = Change.Instigator.Kind == EAethraInstigatorKind::Environment && Change.Instigator.Id == FAethraLisVillage::EnvironmentId;
    const bool bFarmWork = Change.Instigator.Kind == EAethraInstigatorKind::Npc && Houses.IsCropCell(Change.Cell);
    // Building the village and tending the beds are routine: the bodies show them, memories do not need them.
    if (!bVillageWork && !bFarmWork)
    {
        Village->Adapter.OnBlockChanged({ Change.Cell, static_cast<uint8>(Change.Before), static_cast<uint8>(Change.After), LisActorOf(Change.Instigator) });
        const EAethraBlock* Planned = Houses.ExpectedAt(Change.Cell);
        if (Planned && *Planned == Change.Before && !Houses.IsCropCell(Change.Cell))
        {
            PendingDamage.Add({ Change.Cell, LisActorOf(Change.Instigator) });
        }
    }
    const FIntPoint Column(Change.Cell.X, Change.Cell.Y);
    NavGrid->Invalidate(Column);
    for (AAethraLisNpc* Body : Bodies)
    {
        if (Body && Body->NavState == EAethraNavState::Walking && Body->PathUses(Column))
        {
            // Only paths through the changed column are planned again.
            ++Body->PathReplans;
            Body->NavState = EAethraNavState::Waiting;
            PathQueue.AddUnique(Body->NpcId);
        }
    }
}

void AAethraLisDirector::DetectMeetings()
{
    FLisSimulation& S = Village->Sim;
    for (int32 A = 0; A < Bodies.Num(); ++A)
    {
        for (int32 B = A + 1; B < Bodies.Num(); ++B)
        {
            // An encounter is someone crossing your way, not neighbours standing at the same spot.
            if (!Bodies[A] || !Bodies[B]
                || (Bodies[A]->NavState != EAethraNavState::Walking && Bodies[B]->NavState != EAethraNavState::Walking)
                || FVector::DistSquared2D(Bodies[A]->GetActorLocation(), Bodies[B]->GetActorLocation()) > FMath::Square(MeetDistanceCm))
            {
                continue;
            }
            const uint64 Key = (static_cast<uint64>(Bodies[A]->NpcId) << 32) | static_cast<uint32>(Bodies[B]->NpcId);
            const int64* Previous = LastMet.Find(Key);
            const int64* GreetA = LastGreeting.Find(Bodies[A]->NpcId);
            const int64* GreetB = LastGreeting.Find(Bodies[B]->NpcId);
            // The same pair at most twice a day, and each villager notes at most one encounter per hour:
            // walking through the crowd is not twenty separate memories.
            if ((!Previous || S.Hour - *Previous >= 12) && (!GreetA || *GreetA != S.Hour) && (!GreetB || *GreetB != S.Hour))
            {
                LastMet.Add(Key, S.Hour);
                LastGreeting.Add(Bodies[A]->NpcId, S.Hour);
                LastGreeting.Add(Bodies[B]->NpcId, S.Hour);
                S.Meet(Bodies[A]->NpcId, Bodies[B]->NpcId);
            }
        }
    }
}

void AAethraLisDirector::NoticeWork()
{
    // A villager awake within 8 blocks of another one working takes note (local, not global knowledge):
    // the same pair at most twice a day, each observer at most once every three hours.
    FLisSimulation& S = Village->Sim;
    const double Range = 8.0 * AethraTerrain::CellSizeCm;
    for (const AAethraLisNpc* Worker : Bodies)
    {
        const FLisNpc* W = Worker ? S.Find(Worker->NpcId) : nullptr;
        if (!W || !IsWorking(W->Current) || Worker->NavState == EAethraNavState::Walking)
        {
            continue;
        }
        for (const AAethraLisNpc* Observer : Bodies)
        {
            if (!Observer || Observer == Worker || Observer->IsLying()
                || FVector::DistSquared(Observer->GetActorLocation(), Worker->GetActorLocation()) > Range * Range)
            {
                continue;
            }
            const uint64 Key = (static_cast<uint64>(Observer->NpcId) << 32) | static_cast<uint32>(Worker->NpcId);
            const int64* Pair = LastSawWork.Find(Key);
            const int64* Seen = LastNoticed.Find(Observer->NpcId);
            if ((!Pair || S.Hour - *Pair >= 12) && (!Seen || S.Hour - *Seen >= 3))
            {
                LastSawWork.Add(Key, S.Hour);
                LastNoticed.Add(Observer->NpcId, S.Hour);
                S.Notice(Observer->NpcId, ELisEvent::SawWork, W->Place, Worker->NpcId, static_cast<float>(LisIdx(W->Current)), Worker->CurrentCell());
            }
        }
    }
}

void AAethraLisDirector::NoticeArrival(int32 Id)
{
    // Villagers already there see the newcomer arrive (each at most once every two hours).
    FLisSimulation& S = Village->Sim;
    const AAethraLisNpc* Comer = BodyOf(Id);
    const FLisNpc* Npc = S.Find(Id);
    if (!Comer || !Npc)
    {
        return;
    }
    const double Range = 10.0 * AethraTerrain::CellSizeCm;
    for (const AAethraLisNpc* Other : Bodies)
    {
        if (!Other || Other == Comer || Other->IsLying() || Other->NavState == EAethraNavState::Walking
            || FVector::DistSquared(Other->GetActorLocation(), Comer->GetActorLocation()) > Range * Range)
        {
            continue;
        }
        const int64* Seen = LastNoticed.Find(Other->NpcId);
        if (!Seen || S.Hour - *Seen >= 2)
        {
            LastNoticed.Add(Other->NpcId, S.Hour);
            S.Notice(Other->NpcId, ELisEvent::Arrived, Npc->Place, Id, 0.f, Comer->CurrentCell());
        }
    }
}

void AAethraLisDirector::Step(float DeltaSeconds)
{
    if (!Village || !Voxels)
    {
        return;
    }
    CSV_SCOPED_TIMING_STAT(AethraLIS, Director);
    FLisSimulation& S = Village->Sim;
    const double T0 = FPlatformTime::Seconds();

    // 0. The village goes up a few blocks per frame, once.
    if (bBuilding)
    {
        bool bDone = false;
        Houses.Build(*Voxels, BuildBudget, bDone);
        if (bDone)
        {
            bBuilding = false;
            Houses.MarkBuilt();
            S.World.Sites.Add({ static_cast<uint8>(EAethraSite::Built), 0, 0, FIntVector::ZeroValue });
            UseVillageAnchors(); // now snapped to the finished village
            Log(FString::Printf(TEXT("Vila de Elden construída: %d alterações (%d blocos de árvores geradas removidos), %d canteiros, %d células ocupadas preservadas."),
                Houses.Placed, Houses.TreesCleared, Houses.CropCells().Num(), Houses.Skipped));
            Save();
        }
    }
    const double TBuild = FPlatformTime::Seconds();

    // 1. NPC block actions requested last frame: validated and applied here, never inside a callback.
    TArray<FAethraBlockRequest> Requests = MoveTemp(BlockQueue);
    for (const FAethraBlockRequest& Request : Requests)
    {
        ApplyBlockRequest(Request);
    }
    // 2. Perception: this frame's block changes become LIS events for whoever could see them;
    //    damage to a village structure is its own event.
    for (const FDamage& Damage : PendingDamage)
    {
        S.EmitAtCell(ELisEvent::StructureDamaged, ELisPlace::Village, Damage.Actor, 1.f, Damage.Cell);
    }
    PendingDamage.Reset();
    Village->Adapter.Flush(S);
    const double T1 = FPlatformTime::Seconds();

    // 3. LIS clock: one simulated hour every SecondsPerHour.
    HourClock += DeltaSeconds;
    while (HourClock >= SecondsPerHour)
    {
        HourClock -= SecondsPerHour;
        S.Tick(1);
        if (++SaveClockHours >= 6)
        {
            SaveClockHours = 0;
            Save();
        }
    }
    const double T2 = FPlatformTime::Seconds();

    // 4. Bodies follow the minds; 5. paths within the per-frame budget.
    for (AAethraLisNpc* Body : Bodies)
    {
        if (const FLisNpc* Npc = Body ? S.Find(Body->NpcId) : nullptr)
        {
            SyncBody(*Body, *Npc, DeltaSeconds);
        }
    }
    ServePaths();
    const double T3 = FPlatformTime::Seconds();

    // 6. Movement and animation, cheaper with distance from the player.
    const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    const APawn* Player = Controller ? Controller->GetPawn() : nullptr;
    double AnimSeconds = 0.0;
    for (AAethraLisNpc* Body : Bodies)
    {
        const FLisNpc* Npc = Body ? S.Find(Body->NpcId) : nullptr;
        if (!Npc)
        {
            continue;
        }
        const double Distance = Player ? FVector::Dist(Player->GetActorLocation(), Body->GetActorLocation()) : 0.0;
        const bool bHide = Distance > 15000.0;
        if (Body->IsHidden() != bHide)
        {
            Body->SetActorHiddenInGame(bHide);
        }
        const bool bArrived = Body->StepMovement(DeltaSeconds);
        // Where the body is goes with the mind into the save (a restart puts it back there).
        S.Find(Body->NpcId)->WorldCell = Body->CurrentCell();
        if (bArrived)
        {
            Body->NavState = EAethraNavState::Idle;
            if (Npc->Current == ELisAction::Travel && Body->Goal == Body->Spot)
            {
                S.Arrive(Body->NpcId);
                NoticeArrival(Body->NpcId);
            }
        }
        // The pose is chosen after arrival is handled, so it always shows the state the LIS is in now.
        const double A0 = FPlatformTime::Seconds();
        Body->SetAnim(AnimFor(*Npc, *Body));
        Body->SetTool(ToolFor(*Npc, *Body));
        Body->Animate(DeltaSeconds, Distance < 5000.0);
        AnimSeconds += FPlatformTime::Seconds() - A0;
    }
    MeetClock += DeltaSeconds;
    if (MeetClock >= 1.f)
    {
        MeetClock = 0.f;
        DetectMeetings();
        NoticeWork();
    }
    const double T4 = FPlatformTime::Seconds();

    Last.BuildMs = (TBuild - T0) * 1000.0;
    Last.PerceptionMs = (T1 - TBuild) * 1000.0;
    Last.LisMs = (T2 - T1) * 1000.0;
    Last.NavMs = (T3 - T2) * 1000.0;
    Last.AnimMs = AnimSeconds * 1000.0;
    Last.MoveMs = (T4 - T3) * 1000.0 - Last.AnimMs;
    Last.TotalMs = (T4 - T0) * 1000.0;
    ++Frames;
    const double K = 1.0 / FMath::Min<int64>(Frames, 300); // running mean over the last ~300 frames
    Average.BuildMs += (Last.BuildMs - Average.BuildMs) * K;
    Average.PerceptionMs += (Last.PerceptionMs - Average.PerceptionMs) * K;
    Average.LisMs += (Last.LisMs - Average.LisMs) * K;
    Average.NavMs += (Last.NavMs - Average.NavMs) * K;
    Average.MoveMs += (Last.MoveMs - Average.MoveMs) * K;
    Average.AnimMs += (Last.AnimMs - Average.AnimMs) * K;
    Average.TotalMs += (Last.TotalMs - Average.TotalMs) * K;
    if (!bBuilding)
    {
        MaxTotalMs = FMath::Max(MaxTotalMs, Last.TotalMs); // the one-off construction is reported apart
    }
    CSV_CUSTOM_STAT(AethraLIS, DirectorMs, static_cast<float>(Last.TotalMs), ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(AethraLIS, AnimMs, static_cast<float>(Last.AnimMs), ECsvCustomStatOp::Set);
}

void AAethraLisDirector::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    SecondsPerHour = FMath::Max(.01f, CVarSecondsPerHour.GetValueOnGameThread());
    Step(DeltaSeconds);
    if (bShots)
    {
        RunShots(DeltaSeconds);
    }
}

void AAethraLisDirector::PlaceViewer(const FVector& Eye, const FVector& LookAt, bool bAboveTerrain)
{
    APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    ACharacter* Character = Controller ? Cast<ACharacter>(Controller->GetPawn()) : nullptr;
    if (!Character)
    {
        return;
    }
    // Keep the camera in open air above the terrain and trees at that spot.
    TArray<EAethraBlock> Column;
    Voxels->GetColumn(FIntPoint(FMath::FloorToInt(Eye.X / AethraTerrain::CellSizeCm), FMath::FloorToInt(Eye.Y / AethraTerrain::CellSizeCm)), Column);
    int32 Top = Column.Num() - 1;
    while (Top > 0 && Column[Top] == EAethraBlock::Air)
    {
        --Top;
    }
    FVector Safe = Eye;
    if (bAboveTerrain)
    {
        Safe.Z = FMath::Max(Eye.Z, (Top + 3) * AethraTerrain::CellSizeCm);
    }
    else
    {
        // Close-up: only climb out of a block the eye would be inside.
        int32 Z = FMath::Clamp(FMath::FloorToInt(Eye.Z / AethraTerrain::CellSizeCm), 0, Column.Num() - 2);
        while (Z < Column.Num() - 2 && Column[Z] != EAethraBlock::Air && !AethraTerrain::IsPlant(Column[Z]))
        {
            ++Z;
        }
        Safe.Z = FMath::Max(Eye.Z, Z * AethraTerrain::CellSizeCm + 60.0);
    }
    Character->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
    Character->GetCharacterMovement()->Velocity = FVector::ZeroVector;
    Character->SetActorLocation(Safe - FVector(0, 0, 65.0), false, nullptr, ETeleportType::TeleportPhysics);
    Controller->SetControlRotation((LookAt - Safe).Rotation());
}

void AAethraLisDirector::RunShots(float DeltaSeconds)
{
    // Views are placed, then captured a few seconds later once the chunks around them streamed in.
    ShotClock += DeltaSeconds;
    static const float Times[] = { 14.f, 19.f, 26.f, 31.f, 38.f, 43.f, 50.f, 55.f, 62.f, 67.f, 74.f, 80.f, 90.f, 95.f, 98.f };
    if (ShotStage >= UE_ARRAY_COUNT(Times) || ShotClock < Times[ShotStage])
    {
        return;
    }
    APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    const FVector Centre = CellCentre(Anchor(ELisPlace::Village));
    IConsoleVariable* Select = IConsoleManager::Get().FindConsoleVariable(TEXT("aethra.lis.Select"));
    auto CloseUp = [&](const AAethraLisNpc* Body)
    {
        if (!Body)
        {
            return;
        }
        // The first of a ring of viewpoints with a clear line of sight to the villager (no leaf or wall between).
        const FVector At = Body->GetActorLocation() + FVector(0, 0, 30);
        FVector Eye = At + Body->GetActorForwardVector() * 380.0 + FVector(0, 0, 80);
        for (int32 I = 0; I < 12; ++I)
        {
            const float Yaw = Body->GetActorRotation().Yaw + I * 30.f;
            const FVector Candidate = At + FRotator(0, Yaw, 0).Vector() * 400.0 + FVector(0, 0, 90 + (I % 3) * 60);
            FHitResult Hit;
            FCollisionQueryParams Params(TEXT("AethraShot"), false, Body);
            if (!GetWorld()->LineTraceSingleByChannel(Hit, Candidate, At, ECC_Visibility, Params))
            {
                Eye = Candidate;
                break;
            }
        }
        PlaceViewer(Eye, At - FVector(0, 0, 10), false);
        if (Select)
        {
            Select->Set(Body->NpcId);
        }
    };
    auto Working = [&](ELisAction Action)
    {
        for (const AAethraLisNpc* Body : Bodies)
        {
            const FLisNpc* Npc = Body ? Village->Sim.Find(Body->NpcId) : nullptr;
            if (Npc && Npc->Current == Action && Body->NavState != EAethraNavState::Walking)
            {
                return Body;
            }
        }
        return static_cast<const AAethraLisNpc*>(nullptr);
    };
    switch (ShotStage)
    {
    case 10:
    {
        // Demo of a reaction: a wall block of the first house is broken in the player's name, then
        // the nearest villager is shown with its panel (the damage should be in its memory).
        const FLisSite* House = Houses.FindSite(EAethraSite::House);
        if (!House)
        {
            break;
        }
        const FIntPoint Column = FAethraLisVillage::ToWorld(*House, 1, 0);
        const FIntVector Wall(Column.X, Column.Y, House->Cell.Z + 1);
        Voxels->SetBlockBy(Wall, EAethraBlock::Air, FAethraInstigator::Player());
        Log(FString::Printf(TEXT("Captura: o jogador quebrou a parede em (%d, %d, %d)."), Wall.X, Wall.Y, Wall.Z));
        const AAethraLisNpc* Nearest = nullptr;
        for (const AAethraLisNpc* Body : Bodies)
        {
            if (Body && !Body->IsLying() && (!Nearest || FVector::DistSquared(Body->GetActorLocation(), CellCentre(Wall)) < FVector::DistSquared(Nearest->GetActorLocation(), CellCentre(Wall))))
            {
                Nearest = Body;
            }
        }
        CloseUp(Nearest);
        break;
    }
    case 0:
    case 12:
        if (Select)
        {
            Select->Set(0);
        }
        PlaceViewer(Centre + FVector(-1900, -1600, 1300), Centre);
        break;
    case 2:
    {
        const AAethraLisNpc* Nearest = nullptr;
        for (const AAethraLisNpc* Body : Bodies)
        {
            if (Body && (!Nearest || FVector::DistSquared(Body->GetActorLocation(), Centre) < FVector::DistSquared(Nearest->GetActorLocation(), Centre)))
            {
                Nearest = Body;
            }
        }
        CloseUp(Nearest);
        break;
    }
    case 4:
    {
        const AAethraLisNpc* Farmer = Working(ELisAction::Farm);
        if (Farmer)
        {
            CloseUp(Farmer);
        }
        else
        {
            const FVector Farm = CellCentre(Anchor(ELisPlace::Fields));
            PlaceViewer(Farm + FVector(-900, -700, 700), Farm);
        }
        break;
    }
    case 6:
    {
        const AAethraLisNpc* Builder = Working(ELisAction::Build);
        const AAethraLisNpc* Merchant = Working(ELisAction::Tend);
        if (Builder || Merchant)
        {
            CloseUp(Builder ? Builder : Merchant);
        }
        else
        {
            const FVector Bench = CellCentre(Houses.WorkbenchSpot());
            PlaceViewer(Bench + FVector(-700, -600, 500), Bench);
        }
        break;
    }
    case 8:
    {
        const AAethraLisNpc* Woodcutter = Working(ELisAction::Chop);
        if (Woodcutter)
        {
            CloseUp(Woodcutter);
        }
        else
        {
            const FVector Forest = CellCentre(Anchor(ELisPlace::Forest));
            PlaceViewer(Forest + FVector(-900, -700, 900), Forest);
        }
        break;
    }
    case 14:
        if (Controller)
        {
            Controller->ConsoleCommand(TEXT("quit"));
        }
        break;
    default:
        if (Controller)
        {
            Controller->ConsoleCommand(TEXT("shot showui"));
        }
        break;
    }
    ++ShotStage;
}

bool AAethraLisDirector::Save()
{
    if (!Village)
    {
        return false;
    }
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(SaveFile), true);
    const bool bSaved = Village->Sim.SaveToFile(SaveFile);
    if (!bSaved)
    {
        Log(FString::Printf(TEXT("LIS: could not save %s"), *SaveFile));
    }
    return bSaved;
}

int32 AAethraLisDirector::RemoveVillage()
{
    if (!Voxels)
    {
        return 0;
    }
    bBuilding = false; // removing also stops an unfinished construction
    const int32 Restored = Houses.Remove(*Voxels);
    Log(FString::Printf(TEXT("Vila removida: %d blocos devolvidos ao terreno."), Restored));
    return Restored;
}

void AAethraLisDirector::Destroyed()
{
    // Also when the director is destroyed without having begun play (tests): the bodies go with it.
    for (AAethraLisNpc* Body : Bodies)
    {
        if (IsValid(Body))
        {
            Body->Destroy();
        }
    }
    Bodies.Reset();
    if (Voxels)
    {
        Voxels->OnBlockChanged.RemoveDynamic(this, &AAethraLisDirector::HandleBlockChanged);
    }
    if (DebugDrawHandle.IsValid())
    {
        UDebugDrawService::Unregister(DebugDrawHandle);
        DebugDrawHandle.Reset();
    }
    Super::Destroyed();
}

void AAethraLisDirector::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Village)
    {
        Save();
    }
    if (DebugDrawHandle.IsValid())
    {
        UDebugDrawService::Unregister(DebugDrawHandle);
        DebugDrawHandle.Reset();
    }
    if (Voxels)
    {
        Voxels->OnBlockChanged.RemoveDynamic(this, &AAethraLisDirector::HandleBlockChanged);
    }
    // The bodies belong to the director: they go with it.
    for (AAethraLisNpc* Body : Bodies)
    {
        if (IsValid(Body))
        {
            Body->Destroy();
        }
    }
    Bodies.Reset();
    Super::EndPlay(Reason);
}

int32 AAethraLisDirector::SelectedNpc(APlayerController* Controller) const
{
    if (const int32 Forced = CVarSelect.GetValueOnGameThread(); Forced > 0)
    {
        return Forced;
    }
    if (!Controller)
    {
        return 0;
    }
    FVector Eye;
    FRotator View;
    Controller->GetPlayerViewPoint(Eye, View);
    const FVector Forward = View.Vector();
    int32 Best = 0;
    double BestDot = .94; // within ~20 degrees of the crosshair
    for (const AAethraLisNpc* Body : Bodies)
    {
        if (!Body)
        {
            continue;
        }
        const FVector To = Body->GetActorLocation() + FVector(0, 0, 40) - Eye;
        const double Distance = To.Size();
        const double Dot = FVector::DotProduct(To / FMath::Max(Distance, 1.0), Forward);
        if (Distance < 4000.0 && Dot > BestDot)
        {
            BestDot = Dot;
            Best = Body->NpcId;
        }
    }
    return Best;
}

void AAethraLisDirector::DrawDebug(UCanvas* Canvas, APlayerController* Controller)
{
    const int32 Mode = CVarDebug.GetValueOnGameThread();
    if (!Canvas || !Village || Mode == 0 || (Controller && Controller->GetWorld() != GetWorld()))
    {
        return;
    }
    UFont* Font = GEngine->GetSmallFont();
    const FLisSimulation& S = Village->Sim;
    const int32 Selected = SelectedNpc(Controller);
    FVector Eye(0);
    FRotator View;
    if (Controller)
    {
        Controller->GetPlayerViewPoint(Eye, View);
    }
    auto NameOf = [&S](int32 Id) -> FString
    {
        if (Id == LisPlayer)
        {
            return TEXT("jogador");
        }
        const FLisNpc* Who = S.Find(Id);
        return Who ? Who->Identity.Name : FString(TEXT("?"));
    };

    // Names over the heads of nearby villagers: name, trade and what the body shows.
    for (const AAethraLisNpc* Body : Bodies)
    {
        const FLisNpc* Npc = Body ? S.Find(Body->NpcId) : nullptr;
        if (!Npc || Body->IsHidden() || FVector::Dist(Eye, Body->GetActorLocation()) > 4000.0)
        {
            continue;
        }
        const FVector Screen = Canvas->Project(Body->GetActorLocation() + FVector(0, 0, 135));
        if (Screen.Z <= 0)
        {
            continue;
        }
        const FString Label = FString::Printf(TEXT("%s (%s) · %s"), *Npc->Identity.Name, LisName(Npc->Identity.Profession),
            LisName(Npc->Current == ELisAction::Travel ? Npc->Intent : Npc->Current));
        float W, H;
        Canvas->StrLen(Font, Label, W, H);
        Canvas->SetDrawColor(FColor::Black);
        Canvas->DrawText(Font, Label, Screen.X - W * .5f + 1, Screen.Y + 1);
        Canvas->SetDrawColor(Body->NpcId == Selected ? FColor(255, 220, 90) : Body->NavState == EAethraNavState::Failed ? FColor(255, 120, 100) : FColor::White);
        Canvas->DrawText(Font, Label, Screen.X - W * .5f, Screen.Y);
    }
    if (Mode != 1)
    {
        return;
    }

    TArray<FString> Lines;
    const int64 Day = S.Hour / 24 + 1;
    Lines.Add(FString::Printf(TEXT("LIS  dia %lld, %02lld:00  |  %d NPCs  |  vila: %s  |  depósito: %.0f lenha  |  blocos de NPC %d/%d"),
        Day, S.Hour % 24, S.Npcs.Num(), bBuilding ? TEXT("em construção") : Houses.GetSites().Num() ? TEXT("pronta") : TEXT("sem vila"),
        S.World.WoodStore, BlocksApplied, BlocksApplied + BlocksRejected));
    Lines.Add(FString::Printf(TEXT("Tempo (média ms): total %.3f (máx %.2f) = LIS %.3f + percepção %.3f + navegação %.3f + movimento %.3f + animação %.3f  |  caminhos %d (falhas %d)"),
        Average.TotalMs, MaxTotalMs, Average.LisMs, Average.PerceptionMs, Average.NavMs, Average.MoveMs, Average.AnimMs, NavGrid->PathsSearched, NavGrid->PathsFailed));
    const FLisNpc* Npc = S.Find(Selected);
    const AAethraLisNpc* Body = BodyOf(Selected);
    if (Npc && Body)
    {
        Lines.Add(FString::Printf(TEXT("[%d] %s — %s, %d anos, de %s"), Npc->Identity.Id, *Npc->Identity.Name,
            LisName(Npc->Identity.Profession), Npc->Identity.Age, *Npc->Identity.Origin));
        Lines.Add(FString::Printf(TEXT("Objetivo: %s  |  Ação LIS: %s  |  Local: %s%s"), LisName(Npc->Goal), LisName(Npc->Current), LisName(Npc->Place),
            Npc->Current == ELisAction::Travel ? *FString::Printf(TEXT(" → %s para %s"), LisName(Npc->IntentPlace), LisName(Npc->Intent)) : TEXT("")));
        static const TCHAR* AnimNames[] = { TEXT("parado"), TEXT("andando"), TEXT("correndo"), TEXT("machado"), TEXT("enxada"), TEXT("martelo"), TEXT("coletando"),
            TEXT("conversando"), TEXT("negociando"), TEXT("sentado"), TEXT("comendo"), TEXT("dormindo"), TEXT("pescando"), TEXT("inspecionando") };
        static_assert(UE_ARRAY_COUNT(AnimNames) == static_cast<int32>(EAethraVillagerAnim::Count));
        Lines.Add(FString::Printf(TEXT("Atividade: %s  |  Animação: %s"), *Body->Activity, AnimNames[static_cast<int32>(Body->GetAnim())]));
        FString Needs;
        int32 Pressing = 0;
        for (int32 I = 0; I < LisNum<ELisNeed>(); ++I)
        {
            Needs += FString::Printf(TEXT("%s %.2f  "), LisName(static_cast<ELisNeed>(I)), Npc->Needs[I]);
            Pressing = Npc->Needs[I] > Npc->Needs[Pressing] ? I : Pressing;
        }
        Lines.Add(TEXT("Necessidades: ") + Needs);
        static const TCHAR* NavNames[] = { TEXT("parado"), TEXT("aguardando caminho"), TEXT("andando"), TEXT("chegou"), TEXT("sem caminho") };
        Lines.Add(FString::Printf(TEXT("Navegação: %s, passo %d/%d, destino (%d, %d, %d), replanejamentos %d%s"), NavNames[static_cast<int32>(Body->NavState)],
            Body->PathIndex, Body->Path.Num(), Body->Goal.X, Body->Goal.Y, Body->Goal.Z, Body->PathReplans, Body->bRunning ? TEXT(", correndo") : TEXT("")));
        if (const TArray<FLisOption>* Options = LastOptions.Find(Selected))
        {
            FString Why;
            for (const FLisOption& O : *Options)
            {
                Why += FString::Printf(TEXT("%s em %s %.2f   "), LisName(O.Action), LisName(O.Place), O.Score);
            }
            Lines.Add(FString::Printf(TEXT("Última decisão (necessidade mais forte: %s %.2f): %s"), LisName(static_cast<ELisNeed>(Pressing)), Npc->Needs[Pressing], *Why));
        }
        // The relations that matter most to this villager.
        TArray<TPair<int32, FLisRelation>> Relations;
        for (const TPair<int32, FLisRelation>& Pair : Npc->Relations)
        {
            Relations.Add(Pair);
        }
        Relations.Sort([](const TPair<int32, FLisRelation>& A, const TPair<int32, FLisRelation>& B)
        {
            return FMath::Abs(A.Value.Affection - .5f) + A.Value.Resentment > FMath::Abs(B.Value.Affection - .5f) + B.Value.Resentment;
        });
        FString Bonds;
        for (int32 I = 0; I < FMath::Min(3, Relations.Num()); ++I)
        {
            const FLisRelation& R = Relations[I].Value;
            Bonds += FString::Printf(TEXT("%s (afeto %.2f, confiança %.2f, ressentimento %.2f)   "), *NameOf(Relations[I].Key), R.Affection, R.Trust, R.Resentment);
        }
        Lines.Add(TEXT("Relações: ") + (Bonds.IsEmpty() ? FString(TEXT("nenhuma marcante")) : Bonds));
        Lines.Add(TEXT("Memória recente:"));
        for (int32 I = Npc->Memories.Num() - 1, Shown = 0; I >= 0 && Shown < 5; --I, ++Shown)
        {
            const FLisMemory& M = Npc->Memories[I];
            const bool bMet = M.Type == ELisEvent::Met;
            const int32 Other = bMet ? (M.Actor == Npc->Identity.Id ? M.Target : M.Actor) : M.Actor;
            const bool bSelf = !bMet && M.Actor == Npc->Identity.Id;
            const FString By = bSelf || Other == LisNobody ? FString() : FString::Printf(TEXT("%s%s"), bMet ? TEXT(": ") : TEXT(" por "), *NameOf(Other));
            Lines.Add(FString::Printf(TEXT("   h%lld  %s em %s%s  (%s, confiança %.2f, relevância %.2f)"), M.Hour, LisName(M.Type), LisName(M.Place), *By,
                M.Source == ELisSource::Observed ? TEXT("visto") : *FString::Printf(TEXT("ouvido de %s"), *NameOf(M.ToldBy)), M.Confidence, M.Relevance));
        }
        if (Body->FailedActions.Num())
        {
            Lines.Add(TEXT("Ações que falharam:"));
            for (int32 I = Body->FailedActions.Num() - 1; I >= 0; --I)
            {
                Lines.Add(TEXT("   ") + Body->FailedActions[I]);
            }
        }
    }
    else
    {
        Lines.Add(TEXT("Mire num habitante para inspecioná-lo (ou aethra.lis.Select <id>; aethra.lis.Debug 0 desliga)."));
    }
    for (const FString& Entry : RecentLog)
    {
        Lines.Add(TEXT("· ") + Entry);
    }

    const float X = 16.f, LineHeight = 15.f;
    float Y = 16.f;
    FCanvasTileItem Back(FVector2D(X - 6, Y - 4), FVector2D(860, Lines.Num() * LineHeight + 8), FLinearColor(0, 0, 0, .58f));
    Back.BlendMode = SE_BLEND_Translucent;
    Canvas->DrawItem(Back);
    for (const FString& Line : Lines)
    {
        Canvas->SetDrawColor(FColor::White);
        Canvas->DrawText(Font, Line, X, Y);
        Y += LineHeight;
    }
}
