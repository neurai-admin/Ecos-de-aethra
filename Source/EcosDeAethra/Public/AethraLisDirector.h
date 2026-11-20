// Bridge between the LIS (AethraLIS module) and the voxel world: game clock, villager bodies,
// voxel navigation, the Elden village layout, perception of block changes, validated NPC block
// actions, persistence and the debug overlay. The LIS decides; this actor only executes and reports.
#pragma once

#include "AethraLisNav.h"
#include "AethraLisNpc.h"
#include "AethraLisVillage.h"
#include "AethraVoxelTypes.h"
#include "GameFramework/Actor.h"
#include "LisElden.h"
#include "AethraLisDirector.generated.h"

class AAethraVoxelWorld;
class UCanvas;
class APlayerController;

// A block change an NPC asks for. The voxel world still validates and applies it.
struct FAethraBlockRequest
{
    int32 NpcId = 0;
    FIntVector Cell = FIntVector::ZeroValue;
    EAethraBlock Expected = EAethraBlock::Air; // what must be there now
    EAethraBlock Result = EAethraBlock::Air;   // what it becomes
};

enum class EAethraBlockVerdict : uint8 { Applied, NoSuchNpc, WrongBlock, OutOfReach, NotAllowed, RateLimited, Refused };

struct FAethraLisFrameStats
{
    double LisMs = 0, PerceptionMs = 0, NavMs = 0, MoveMs = 0, AnimMs = 0, BuildMs = 0, TotalMs = 0;
};

UCLASS()
class ECOSDEAETHRA_API AAethraLisDirector : public AActor
{
    GENERATED_BODY()

public:
    AAethraLisDirector();
    virtual ~AAethraLisDirector() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Destroyed() override;

    // Starts the village around Center (cells) on the given voxel world. SavePath empty = default
    // Saved/LIS/World_<seed>.lis; an existing save is restored instead of generating a new village.
    bool Start(AAethraVoxelWorld* InWorld, FIntPoint Center, int32 NpcCount = 20, const FString& SavePath = FString());
    // Advances everything by DeltaSeconds of real time (Tick calls this; tests call it directly).
    void Step(float DeltaSeconds);

    bool IsRunning() const { return Village.IsValid(); }
    FLisSimulation& Sim() { return Village->Sim; }
    const TArray<TObjectPtr<AAethraLisNpc>>& Npcs() const { return Bodies; }
    AAethraLisNpc* BodyOf(int32 NpcId) const;
    FAethraLisNav& Nav() { return *NavGrid; }
    const FAethraLisVillage& Layout() const { return Houses; }
    bool IsVillageBuilding() const { return bBuilding; }
    FIntVector Anchor(ELisPlace Place) const;
    bool Save();
    // Puts back the terrain under every village block still in place (console: aethra.lis.RemoveVillage).
    int32 RemoveVillage();

    // The animation that shows what the LIS says the villager is doing (and its tool).
    static EAethraVillagerAnim AnimFor(const FLisNpc& Npc, const AAethraLisNpc& Body);
    static EAethraVillagerTool ToolFor(const FLisNpc& Npc, const AAethraLisNpc& Body);
    // Where the villager stands for Action at Place (snapped to the nav grid).
    FIntVector ActivitySpot(const FLisNpc& Npc, ELisPlace Place, ELisAction Action, const AAethraLisNpc& Body);

    // NPC block actions: queued, then validated and applied outside any voxel callback.
    void RequestBlock(const FAethraBlockRequest& Request) { BlockQueue.Add(Request); }
    EAethraBlockVerdict ApplyBlockRequest(const FAethraBlockRequest& Request);
    int32 BlocksApplied = 0;
    int32 BlocksRejected = 0;

    float SecondsPerHour = 30.f;
    float SightCells = 20.f;
    float MeetDistanceCm = 260.f;
    int32 PathsPerFrame = 2;
    int32 BuildBudget = 8;        // village blocks placed per frame while it is being built
    bool bDemoVillage = false;    // isolated demo world: the village may fell generated trees on its plots
    float StrikeSeconds = 3.5f;   // a tool strike (chop, crop) lands after this long at the spot
    FString SaveFile;
    FString VillageReport;

    // Performance: last frame and running averages (ms).
    FAethraLisFrameStats Last;
    FAethraLisFrameStats Average;
    double MaxTotalMs = 0;
    int64 Frames = 0;

private:
    UFUNCTION()
    void HandleBlockChanged(const FAethraBlockChange& Change);

    UPROPERTY(Transient)
    TObjectPtr<AAethraVoxelWorld> Voxels;

    UPROPERTY(Transient)
    TArray<TObjectPtr<AAethraLisNpc>> Bodies;

    TUniquePtr<FLisElden> Village;
    TUniquePtr<FAethraLisNav> NavGrid;
    FAethraLisVillage Houses;
    bool bBuilding = false;
    TArray<FAethraBlockRequest> BlockQueue;
    struct FDamage { FIntVector Cell; int32 Actor; };
    TArray<FDamage> PendingDamage;
    TArray<int32> PathQueue; // NPC ids waiting for a path
    TMap<int32, TArray<FLisOption>> LastOptions;
    TMap<uint64, int64> LastMet;
    TMap<int32, int64> LastGreeting;
    TMap<int32, int64> LastBlockHour;
    TMap<uint64, int64> LastSawWork;
    TMap<int32, int64> LastNoticed;
    TArray<FString> RecentLog;
    float HourClock = 0.f;
    float MeetClock = 0.f;
    float SaveClockHours = 0.f;
    FDelegateHandle DebugDrawHandle;
    // -AethraLisShots: scripted views of the village with in-engine screenshots, then quit.
    bool bShots = false;
    float ShotClock = 0.f;
    int32 ShotStage = 0;
    void RunShots(float DeltaSeconds);
    void PlaceViewer(const FVector& Eye, const FVector& LookAt, bool bAboveTerrain = true);

    void ChooseAnchors(FIntPoint Center);
    void UseVillageAnchors();
    void SpawnBodies(bool bRestored);
    void SyncBody(AAethraLisNpc& Body, const FLisNpc& Npc, float DeltaSeconds);
    void RequestPath(AAethraLisNpc& Body, FIntVector Goal);
    void ServePaths();
    FIntVector SpotFor(const FLisNpc& Npc, ELisPlace Place);
    FIntVector Snap(const FIntVector& Cell, int32 Radius = 3);
    bool FindTree(const AAethraLisNpc& Body, FIntVector& OutStand, FIntVector& OutTrunk);
    bool FindCrop(const AAethraLisNpc& Body, FIntVector& OutStand, FIntVector& OutCrop);
    bool RecentDamage(const FLisNpc& Npc, FIntVector& OutCell) const;
    void DetectMeetings();
    void NoticeWork();
    void NoticeArrival(int32 Id);
    void Log(const FString& Line);
    void Fail(AAethraLisNpc& Body, const FLisNpc& Npc, const FString& What);
    void DrawDebug(UCanvas* Canvas, APlayerController* Controller);
    int32 SelectedNpc(APlayerController* Controller) const;
    FAethraVillagerLook LookFor(const FLisNpc& Npc) const;
};
