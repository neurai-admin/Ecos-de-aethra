#include "AethraLisDirector.h"

#include "AethraLisNpc.h"
#include "AethraVoxelWorld.h"
#include "Debug/DebugDrawService.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "AethraLisNav.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Voxel/AethraTerrain.h"

CSV_DEFINE_CATEGORY(AethraLIS, true);
DEFINE_LOG_CATEGORY_STATIC(LogAethraLisWorld, Log, All);

namespace
{
TAutoConsoleVariable<int32> CVarDebug(TEXT("aethra.lis.Debug"), 1,
    TEXT("LIS NPC overlay: 0 = off, 1 = names and the inspected NPC, 2 = names only."));
TAutoConsoleVariable<int32> CVarSelect(TEXT("aethra.lis.Select"), 0,
    TEXT("NPC id shown in the LIS panel; 0 = the NPC closest to the crosshair."));
TAutoConsoleVariable<float> CVarSecondsPerHour(TEXT("aethra.lis.SecondsPerHour"), 30.f,
    TEXT("Real seconds per LIS game hour."));

constexpr int32 NavHalfSize = 72;      // cells around the village centre the NPCs can walk
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
    case ELisAction::Farm: return 4;
    case ELisAction::Patrol: return 8;
    case ELisAction::Explore: return 10;
    case ELisAction::Idle: return 2;
    case ELisAction::Sell:
    case ELisAction::BuyFood: return 1;
    default: return 0;
    }
}

FLinearColor ClothesFor(ELisProfession Profession)
{
    switch (Profession)
    {
    case ELisProfession::Farmer: return FLinearColor(.45f, .55f, .18f);
    case ELisProfession::Fisher: return FLinearColor(.16f, .35f, .62f);
    case ELisProfession::Woodcutter: return FLinearColor(.62f, .16f, .12f);
    case ELisProfession::Merchant: return FLinearColor(.45f, .2f, .55f);
    case ELisProfession::Guard: return FLinearColor(.5f, .52f, .56f);
    case ELisProfession::Explorer: return FLinearColor(.8f, .48f, .12f);
    default: return FLinearColor::Gray;
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

    Village = MakeUnique<FLisElden>(static_cast<uint32>(Voxels->Seed));
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
    if (!bRestored)
    {
        ChooseAnchors(Center);
    }

    // Hooks are not part of the save: set them after loading.
    S.Config.bExternalTravel = true;
    S.Perceives = [this](const FLisNpc& Npc, const FIntVector& Cell)
    {
        const AAethraLisNpc* Body = BodyOf(Npc.Identity.Id);
        if (!Body)
        {
            return false;
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

    SpawnBodies();
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

    TArray<FIntVector> Reachable;
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

    // The abstract map follows the real layout, so travel costs in the utility match real distances.
    for (FLisPlaceState& Place : S.World.Places)
    {
        Place.Pos = FVector2f(Place.Cell.X - VillageAt.X, Place.Cell.Y - VillageAt.Y) / CellsPerTravelHour;
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
    const FIntPoint Wanted(A.X + FMath::RoundToInt(Radius * FMath::Cos(Angle)), A.Y + FMath::RoundToInt(Radius * FMath::Sin(Angle)));
    FIntPoint Found(A.X, A.Y);
    NavGrid->NearestWalkable(Wanted, 4, Found);
    return NavGrid->Stand(Found);
}

void AAethraLisDirector::SpawnBodies()
{
    for (const FLisNpc& Npc : Village->Sim.Npcs)
    {
        // A traveller restored from a save starts at its origin and walks again.
        const FIntVector Spot = SpotFor(Npc, Npc.Place);
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
        const float Tone = .55f + .35f * Village->Sim.Rand(Npc.Identity.Id, 9000);
        Body->SetLook(ClothesFor(Npc.Identity.Profession), FLinearColor(Tone, Tone * .78f, Tone * .62f),
            ClothesFor(Npc.Identity.Profession) * .6f + FLinearColor(.2f, .17f, .1f) * .4f);
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
        const FIntVector Here = Body->CurrentCell();
        FIntPoint From(Here.X, Here.Y);
        NavGrid->NearestWalkable(From, 3, From);
        TArray<FIntVector> Path;
        if (NavGrid->FindPath(From, FIntPoint(Body->Goal.X, Body->Goal.Y), Path))
        {
            Body->FollowPath(Path, Body->Goal);
            continue;
        }
        Body->NavState = EAethraNavState::Failed;
        // Kept (with the game hour) until the next failure, so the overlay can explain a replan.
        Body->LastError = FString::Printf(TEXT("h%lld sem caminho até (%d, %d, %d)"), Village->Sim.Hour, Body->Goal.X, Body->Goal.Y, Body->Goal.Z);
        Log(FString::Printf(TEXT("%s: %s"), *Npc->Identity.Name, *Body->LastError));
        if (Npc->Current == ELisAction::Travel)
        {
            Village->Sim.TravelFailed(Id); // the LIS replans with what it learned
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
            if (Base < 0)
            {
                continue;
            }
            for (const FIntPoint D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
            {
                const FIntPoint S = T + D;
                const int16 Stand = NavGrid->Column(S).StandZ;
                const int32 Distance = (S - FIntPoint(Here.X, Here.Y)).SizeSquared();
                if (Stand >= 0 && FMath::Abs(Stand - Base) <= 1 && Distance < BestDistance)
                {
                    BestDistance = Distance;
                    OutStand = FIntVector(S.X, S.Y, Stand);
                    OutTrunk = FIntVector(T.X, T.Y, Base);
                }
            }
        }
    }
    return BestDistance != MAX_int32;
}

void AAethraLisDirector::SyncBody(AAethraLisNpc& Body, const FLisNpc& Npc)
{
    const uint64 Key = (static_cast<uint64>(Npc.Decisions) << 16) | (static_cast<uint64>(Npc.Current) << 8) | static_cast<uint64>(Npc.Place);
    const bool bNewStep = Key != Body.ShownKey;
    Body.ShownKey = Key;

    if (Npc.Current == ELisAction::Travel)
    {
        Body.SetLying(false);
        if (bNewStep || Body.NavState == EAethraNavState::Idle || Body.NavState == EAethraNavState::Arrived)
        {
            Body.Spot = SpotFor(Npc, Npc.IntentPlace);
            RequestPath(Body, Body.Spot);
        }
        return;
    }

    Body.SetLying(Npc.Current == ELisAction::Sleep && Body.NavState != EAethraNavState::Walking);
    if (bNewStep)
    {
        Body.WanderRadius = WanderFor(Npc.Current);
        Body.bActionPending = false;
        Body.Spot = SpotFor(Npc, Npc.Place);
        if (Npc.Current == ELisAction::Chop)
        {
            FIntVector Stand, Trunk;
            if (FindTree(Body, Stand, Trunk))
            {
                Body.Spot = Stand;
                Body.ActionTarget = Trunk;
                Body.bActionPending = true;
            }
            else
            {
                Body.LastError = FString::Printf(TEXT("h%lld nenhuma árvore alcançável na floresta"), Village->Sim.Hour);
                Log(FString::Printf(TEXT("%s: %s"), *Npc.Identity.Name, *Body.LastError));
                Village->Sim.Interrupt(Npc.Identity.Id);
                return;
            }
        }
        if (Body.CurrentCell() != Body.Spot)
        {
            RequestPath(Body, Body.Spot);
        }
    }

    // Social actions walk up to the other person, if they are around.
    if ((Npc.Current == ELisAction::Socialize || Npc.Current == ELisAction::GiveHelp || Npc.Current == ELisAction::AskHelp)
        && Body.NavState != EAethraNavState::Walking && Body.NavState != EAethraNavState::Waiting)
    {
        if (const AAethraLisNpc* Other = BodyOf(Npc.IntentTarget))
        {
            const FVector Gap = Other->GetActorLocation() - Body.GetActorLocation();
            if (Gap.Size2D() > 220.0 && Gap.Size2D() < 3000.0)
            {
                const FIntVector There = Other->CurrentCell();
                FIntPoint Next(There.X, There.Y);
                NavGrid->NearestWalkable(Next + FIntPoint(1, 0), 2, Next);
                RequestPath(Body, NavGrid->Stand(Next));
            }
            else
            {
                Body.FaceTowards(Other->GetActorLocation());
            }
        }
    }

    // Chop: once next to the trunk, ask the world to take one block of wood.
    if (Body.bActionPending && Body.NavState != EAethraNavState::Walking && Body.NavState != EAethraNavState::Waiting
        && Body.CurrentCell() == Body.Spot)
    {
        Body.bActionPending = false;
        Body.FaceTowards(CellCentre(Body.ActionTarget));
        const EAethraBlock There = Voxels->GetBlock(Body.ActionTarget);
        RequestBlock({ Npc.Identity.Id, Body.ActionTarget, There, EAethraBlock::Air });
    }

    // Light wandering for open-air work, inside a small radius of the spot.
    if (Body.WanderRadius > 0 && Body.NavState != EAethraNavState::Walking && Body.NavState != EAethraNavState::Waiting
        && GetWorld()->GetTimeSeconds() >= Body.NextWander)
    {
        Body.NextWander = GetWorld()->GetTimeSeconds() + 4.f + 4.f * Village->Sim.Rand(Npc.Identity.Id, 9100 + Body.PathReplans);
        const float Angle = 6.2832f * Village->Sim.Rand(Npc.Identity.Id, 9200 + static_cast<uint32>(Body.NextWander));
        const FIntPoint Wanted(Body.Spot.X + FMath::RoundToInt(Body.WanderRadius * FMath::Cos(Angle)),
            Body.Spot.Y + FMath::RoundToInt(Body.WanderRadius * FMath::Sin(Angle)));
        FIntPoint Found;
        if (NavGrid->NearestWalkable(Wanted, 2, Found))
        {
            RequestPath(Body, NavGrid->Stand(Found));
        }
    }
}

EAethraBlockVerdict AAethraLisDirector::ApplyBlockRequest(const FAethraBlockRequest& Request)
{
    const FLisNpc* Npc = Village ? Village->Sim.Find(Request.NpcId) : nullptr;
    const AAethraLisNpc* Body = BodyOf(Request.NpcId);
    auto Verdict = [&](EAethraBlockVerdict V, const TCHAR* Why)
    {
        if (V == EAethraBlockVerdict::Applied)
        {
            ++BlocksApplied;
        }
        else
        {
            ++BlocksRejected;
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
    // Rules: for now NPCs may only fell wood, and only while chopping or as woodcutters.
    const bool bWood = Request.Expected == EAethraBlock::Wood || Request.Expected == EAethraBlock::BirchWood;
    const bool bAllowed = bWood && Request.Result == EAethraBlock::Air
        && (Npc->Current == ELisAction::Chop || Npc->Identity.Profession == ELisProfession::Woodcutter)
        && NavGrid->InBounds(FIntPoint(Request.Cell.X, Request.Cell.Y));
    if (!bAllowed)
    {
        return Verdict(EAethraBlockVerdict::NotAllowed, TEXT("recusado: ação não permitida"));
    }
    if (!Voxels->SetBlockBy(Request.Cell, Request.Result, FAethraInstigator::Npc(Request.NpcId)))
    {
        return Verdict(EAethraBlockVerdict::Refused, TEXT("recusado pelo mundo voxel"));
    }
    LastBlockHour.Add(Request.NpcId, Village->Sim.Hour);
    return Verdict(EAethraBlockVerdict::Applied, TEXT("removido (madeira)"));
}

void AAethraLisDirector::HandleBlockChanged(const FAethraBlockChange& Change)
{
    // Called inside SetBlock's broadcast: only record. No world edits from here.
    if (!Village)
    {
        return;
    }
    Village->Adapter.OnBlockChanged({ Change.Cell, static_cast<uint8>(Change.Before), static_cast<uint8>(Change.After), LisActorOf(Change.Instigator) });
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
            if (!Previous || S.Hour - *Previous >= 12) // greet the same person at most twice a day
            {
                LastMet.Add(Key, S.Hour);
                S.Meet(Bodies[A]->NpcId, Bodies[B]->NpcId);
            }
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

    // 1. NPC block actions requested last frame: validated and applied here, never inside a callback.
    TArray<FAethraBlockRequest> Requests = MoveTemp(BlockQueue);
    for (const FAethraBlockRequest& Request : Requests)
    {
        ApplyBlockRequest(Request);
    }
    // 2. Perception: the block changes of this frame become LIS events for whoever could see them.
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
            SyncBody(*Body, *Npc);
        }
    }
    ServePaths();
    const double T3 = FPlatformTime::Seconds();

    // 6. Movement and animation, cheaper with distance from the player.
    const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    const APawn* Player = Controller ? Controller->GetPawn() : nullptr;
    for (AAethraLisNpc* Body : Bodies)
    {
        if (!Body)
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
        Body->Animate(DeltaSeconds, Distance < 5000.0);
        if (bArrived)
        {
            const FLisNpc* Npc = S.Find(Body->NpcId);
            if (Npc && Npc->Current == ELisAction::Travel && Body->Goal == Body->Spot)
            {
                S.Arrive(Body->NpcId);
            }
            Body->NavState = EAethraNavState::Idle;
        }
    }
    MeetClock += DeltaSeconds;
    if (MeetClock >= 1.f)
    {
        MeetClock = 0.f;
        DetectMeetings();
    }
    const double T4 = FPlatformTime::Seconds();

    Last.PerceptionMs = (T1 - T0) * 1000.0;
    Last.LisMs = (T2 - T1) * 1000.0;
    Last.NavMs = (T3 - T2) * 1000.0;
    Last.MoveMs = (T4 - T3) * 1000.0;
    Last.TotalMs = (T4 - T0) * 1000.0;
    ++Frames;
    const double K = 1.0 / FMath::Min<int64>(Frames, 300); // running mean over the last ~300 frames
    Average.PerceptionMs += (Last.PerceptionMs - Average.PerceptionMs) * K;
    Average.LisMs += (Last.LisMs - Average.LisMs) * K;
    Average.NavMs += (Last.NavMs - Average.NavMs) * K;
    Average.MoveMs += (Last.MoveMs - Average.MoveMs) * K;
    Average.TotalMs += (Last.TotalMs - Average.TotalMs) * K;
    MaxTotalMs = FMath::Max(MaxTotalMs, Last.TotalMs);
    CSV_CUSTOM_STAT(AethraLIS, DirectorMs, static_cast<float>(Last.TotalMs), ECsvCustomStatOp::Set);
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

void AAethraLisDirector::PlaceViewer(const FVector& Eye, const FVector& LookAt)
{
    APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    ACharacter* Character = Controller ? Cast<ACharacter>(Controller->GetPawn()) : nullptr;
    if (!Character)
    {
        return;
    }
    Character->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
    Character->GetCharacterMovement()->Velocity = FVector::ZeroVector;
    Character->SetActorLocation(Eye - FVector(0, 0, 65.0), false, nullptr, ETeleportType::TeleportPhysics);
    Controller->SetControlRotation((LookAt - Eye).Rotation());
}

void AAethraLisDirector::RunShots(float DeltaSeconds)
{
    // Views are placed, then captured a few seconds later once the chunks around them streamed in.
    ShotClock += DeltaSeconds;
    static const float Times[] = { 12.f, 17.f, 26.f, 31.f, 40.f, 45.f, 70.f, 75.f, 80.f };
    if (ShotStage >= UE_ARRAY_COUNT(Times) || ShotClock < Times[ShotStage])
    {
        return;
    }
    APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    const FVector Centre = CellCentre(Anchor(ELisPlace::Village));
    IConsoleVariable* Select = IConsoleManager::Get().FindConsoleVariable(TEXT("aethra.lis.Select"));
    switch (ShotStage)
    {
    case 0:
    case 6:
        PlaceViewer(Centre + FVector(-1500, -1300, 900), Centre);
        break;
    case 2:
    {
        // Close-up of the villager nearest the village centre, with its panel.
        const AAethraLisNpc* Nearest = nullptr;
        for (const AAethraLisNpc* Body : Bodies)
        {
            if (Body && (!Nearest || FVector::DistSquared(Body->GetActorLocation(), Centre) < FVector::DistSquared(Nearest->GetActorLocation(), Centre)))
            {
                Nearest = Body;
            }
        }
        if (Nearest)
        {
            const FVector At = Nearest->GetActorLocation();
            PlaceViewer(At + Nearest->GetActorForwardVector() * 420.0 + FVector(0, 0, 90), At + FVector(0, 0, 40));
            if (Select)
            {
                Select->Set(Nearest->NpcId);
            }
        }
        break;
    }
    case 4:
    {
        if (Select)
        {
            Select->Set(0);
        }
        const FVector Forest = CellCentre(Anchor(ELisPlace::Forest));
        PlaceViewer(Forest + FVector(-1100, -700, 650), Forest);
        break;
    }
    case 8:
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

    // Names over the heads of nearby NPCs.
    for (const AAethraLisNpc* Body : Bodies)
    {
        const FLisNpc* Npc = Body ? S.Find(Body->NpcId) : nullptr;
        if (!Npc || FVector::Dist(Eye, Body->GetActorLocation()) > 4000.0)
        {
            continue;
        }
        const FVector Screen = Canvas->Project(Body->GetActorLocation() + FVector(0, 0, 120));
        if (Screen.Z <= 0)
        {
            continue;
        }
        const FString Label = FString::Printf(TEXT("%s · %s"), *Npc->Identity.Name,
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
    Lines.Add(FString::Printf(TEXT("LIS  dia %lld, %02lld:00  |  %d NPCs  |  diretor %.3f ms (máx %.2f)  |  caminhos %d (falhas %d)  |  blocos %d/%d"),
        Day, S.Hour % 24, S.Npcs.Num(), Average.TotalMs, MaxTotalMs, NavGrid->PathsSearched, NavGrid->PathsFailed, BlocksApplied, BlocksApplied + BlocksRejected));
    const FLisNpc* Npc = S.Find(Selected);
    const AAethraLisNpc* Body = BodyOf(Selected);
    if (Npc && Body)
    {
        Lines.Add(FString::Printf(TEXT("[%d] %s — %s, %d anos, de %s"), Npc->Identity.Id, *Npc->Identity.Name,
            LisName(Npc->Identity.Profession), Npc->Identity.Age, *Npc->Identity.Origin));
        Lines.Add(FString::Printf(TEXT("Objetivo: %s  |  Ação: %s  |  Local: %s%s"), LisName(Npc->Goal), LisName(Npc->Current), LisName(Npc->Place),
            Npc->Current == ELisAction::Travel ? *FString::Printf(TEXT(" → %s para %s"), LisName(Npc->IntentPlace), LisName(Npc->Intent)) : TEXT("")));
        FString Needs;
        for (int32 I = 0; I < LisNum<ELisNeed>(); ++I)
        {
            Needs += FString::Printf(TEXT("%s %.2f  "), LisName(static_cast<ELisNeed>(I)), Npc->Needs[I]);
        }
        Lines.Add(TEXT("Necessidades: ") + Needs);
        FString Traits;
        for (int32 I = 0; I < LisNum<ELisTrait>(); ++I)
        {
            Traits += FString::Printf(TEXT("%s %.2f  "), LisName(static_cast<ELisTrait>(I)), Npc->Personality.Value[I]);
        }
        Lines.Add(TEXT("Personalidade: ") + Traits);
        Lines.Add(FString::Printf(TEXT("Comida %.1f  Lenha %.1f  Moedas %.1f  |  Decisões %d  Replanejamentos %d"),
            Npc->Food, Npc->Wood, Npc->Coins, Npc->Decisions, Npc->Replans));
        static const TCHAR* NavNames[] = { TEXT("parado"), TEXT("aguardando caminho"), TEXT("andando"), TEXT("chegou"), TEXT("sem caminho") };
        Lines.Add(FString::Printf(TEXT("Destino: (%d, %d, %d)  |  Navegação: %s, passo %d/%d, replanejamentos de caminho %d"),
            Body->Goal.X, Body->Goal.Y, Body->Goal.Z, NavNames[static_cast<int32>(Body->NavState)], Body->PathIndex, Body->Path.Num(), Body->PathReplans));
        if (const TArray<FLisOption>* Options = LastOptions.Find(Selected))
        {
            FString Why;
            for (const FLisOption& O : *Options)
            {
                Why += FString::Printf(TEXT("%s em %s %.2f   "), LisName(O.Action), LisName(O.Place), O.Score);
            }
            Lines.Add(TEXT("Motivo (melhores opções): ") + Why);
        }
        Lines.Add(TEXT("Últimos acontecimentos:"));
        for (int32 I = Npc->Memories.Num() - 1, Shown = 0; I >= 0 && Shown < 5; --I, ++Shown)
        {
            const FLisMemory& M = Npc->Memories[I];
            const FLisNpc* Who = S.Find(M.Actor);
            Lines.Add(FString::Printf(TEXT("   h%lld  %s em %s%s  (%s, confiança %.2f, relevância %.2f)"), M.Hour, LisName(M.Type), LisName(M.Place),
                M.Actor == LisPlayer ? TEXT(" por jogador") : Who ? *FString::Printf(TEXT(" por %s"), *Who->Identity.Name) : TEXT(""),
                M.Source == ELisSource::Observed ? TEXT("visto") : TEXT("ouvido"), M.Confidence, M.Relevance));
        }
        if (!Body->LastError.IsEmpty())
        {
            Lines.Add(TEXT("Último erro: ") + Body->LastError);
        }
    }
    else
    {
        Lines.Add(TEXT("Mire num habitante para inspecioná-lo (ou aethra.lis.Select <id>)."));
    }
    for (const FString& Entry : RecentLog)
    {
        Lines.Add(TEXT("· ") + Entry);
    }

    const float X = 16.f, LineHeight = 15.f;
    float Y = 16.f;
    FCanvasTileItem Back(FVector2D(X - 6, Y - 4), FVector2D(760, Lines.Num() * LineHeight + 8), FLinearColor(0, 0, 0, .55f));
    Back.BlendMode = SE_BLEND_Translucent;
    Canvas->DrawItem(Back);
    for (const FString& Line : Lines)
    {
        Canvas->SetDrawColor(FColor::White);
        Canvas->DrawText(Font, Line, X, Y);
        Y += LineHeight;
    }
}
