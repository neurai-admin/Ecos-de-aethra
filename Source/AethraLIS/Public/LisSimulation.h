// LIS simulation: scheduler, cognition (utility AI), planning, memory,
// learning and social updates for a set of NPCs in an abstract world.
#pragma once

#include "LisTypes.h"

struct FLisConfig
{
    int32 MaxDecisionsPerTick = 64;  // budget; NPCs over it decide next tick
    int32 MemoryCapacity = 48;       // per NPC; least useful memory is forgotten
    int32 EventLogCapacity = 4096;   // objective log kept for debugging and reports
    int32 MidThinkHours = 3;         // mid tier repeats its step for this long
    int32 FarStepHours = 12;         // far tier aggregated step
    float NearRadius = 100.f;        // distance to Focus on the abstract map
    float MidRadius = 200.f;
    ELisPlace Focus = ELisPlace::Village;
    float PerceptionRadiusCells = 48.f; // voxel events farther than this from an NPC's place go unseen
    // When true, travel has no fixed duration: the 3D world walks the NPC and calls Arrive or TravelFailed.
    bool bExternalTravel = false;
    // LIS 03 living village (version 4): builders and merchants get their own work (Build, Tend), and
    // chopping yields only the wood the 3D world really removed (ReportYield). Off: LIS 01/02 behaviour.
    bool bLivingVillage = false;
};

struct FLisStats
{
    int64 Ticks = 0;
    int64 Decisions = 0;
    int64 Replans = 0;
    int64 Deferred = 0;
    int64 AggregateSteps = 0;
    int64 GoalChanges = 0;
    int64 Events = 0;
    int64 MemoriesFormed = 0;
    int64 MemoriesForgotten = 0;
    // Timings are measurements, not state: not saved and not in the checksum.
    double LastTickMs = 0.0;
    double TotalTickMs = 0.0;
    double MaxTickMs = 0.0;
};

// A scored option, for tests, debugging and reports.
struct FLisOption
{
    ELisAction Action = ELisAction::Idle;
    ELisPlace Place = ELisPlace::Village;
    int32 Target = LisNobody;
    float Score = -1.f;
};

class AETHRALIS_API FLisSimulation
{
public:
    explicit FLisSimulation(uint32 InSeed = 1);

    uint32 Seed = 1;
    int64 Hour = 0;
    FLisConfig Config;
    FLisWorld World;
    TArray<FLisNpc> Npcs;
    TArray<FLisEventRecord> Events;
    int32 NextEventId = 1;
    FLisStats Stats;

    // Creates an NPC whose name, age and personality derive from Seed and its index.
    int32 AddNpc(ELisProfession Profession, ELisPlace Place = ELisPlace::Village);
    FLisNpc* Find(int32 Id);
    const FLisNpc* Find(int32 Id) const;

    // Advances the simulation one hour at a time.
    void Tick(int32 Hours = 1);

    // Cognition: score every action available to the NPC from what it believes.
    TArray<FLisOption> Evaluate(const FLisNpc& Npc) const;
    // Picks the best option, plans travel to it and starts it. Returns the chosen option.
    FLisOption Decide(FLisNpc& Npc);
    void EvaluateGoal(FLisNpc& Npc);

    // World facts. Witnesses (NPCs at the place, plus actor and target) remember it.
    int32 Emit(ELisEvent Type, ELisPlace Place, int32 Actor, int32 Target, float Amount, FIntVector Cell = FIntVector::ZeroValue);
    // Emit restricted to NPCs at the place whose anchor is within PerceptionRadiusCells of Cell.
    int32 EmitAtCell(ELisEvent Type, ELisPlace Place, int32 Actor, float Amount, FIntVector Cell);
    // Adds an NPC's account of an event (perception or hearsay) and applies its effects.
    void Remember(FLisNpc& Npc, const FLisEventRecord& Event, ELisSource Source, int32 ToldBy, float Confidence);
    // The speaker tells the listener what it finds most relevant that the listener does not know.
    int32 ShareKnowledge(FLisNpc& Speaker, FLisNpc& Listener, int32 MaxItems = 2);
    // What the NPC currently sees at its place updates its beliefs.
    void Observe(FLisNpc& Npc);

    int32 TravelHours(ELisPlace From, ELisPlace To) const;
    float CommunityConcern(const FLisNpc& Npc) const;
    ELisTier TierOf(ELisPlace Place) const;

    // Persistence: versioned binary format. Load replaces the whole state.
    static constexpr uint32 SaveMagic = 0x3153494C; // "LIS1"
    // 2: FLisNpc::Forgotten. 3: Config.bExternalTravel and the Arrived/Met events.
    // 4: Builder, Build/Tend, three events, living-village config, storage, sites and body cells. Older versions still load.
    static constexpr int32 SaveVersion = 4;
    void Serialize(FArchive& Ar);
    TArray<uint8> SaveToBytes() const;
    bool LoadFromBytes(const TArray<uint8>& Bytes);
    bool SaveToFile(const FString& Path) const;
    bool LoadFromFile(const FString& Path);
    uint32 Checksum() const;

    int32 MemoryCount() const;
    SIZE_T ApproxBytes() const;

    // Deterministic [0,1) value from seed, hour, actor and salt. No hidden RNG state to save.
    float Rand(int32 Actor, uint32 Salt) const;

    // --- World integration (LIS 02). All optional: without them the simulation runs abstractly. ---
    // Who can perceive a voxel change at Cell. Replaces the place-anchor rule of EmitAtCell when set.
    TFunction<bool(const FLisNpc&, const FIntVector&)> Perceives;
    // Called with every option scored when an NPC decides (debug overlay: "why this action").
    TFunction<void(const FLisNpc&, const TArray<FLisOption>&)> OnDecision;
    // External travel: the NPC reached IntentPlace. Observes it and starts (or replans) the intent.
    bool Arrive(int32 Id);
    // External travel: no path. The NPC stays at Place, learns the intent went badly and decides again.
    bool TravelFailed(int32 Id);
    // The world could not carry out the running action (for example no tree in reach). Same as a failed outcome.
    bool Interrupt(int32 Id, float Outcome = -.4f);
    // Two NPCs met in the world: only they remember it.
    int32 Meet(int32 A, int32 B);

    // --- Living village (LIS 03) ---
    // The world really gave the NPC this much (wood blocks felled) during its running action.
    void ReportYield(int32 Id, float Amount);
    // Something only one NPC noticed (another villager working, a path it found blocked). Returns the event id.
    int32 Notice(int32 Observer, ELisEvent Type, ELisPlace Place, int32 Actor, float Amount, FIntVector Cell = FIntVector::ZeroValue);
    // How much a recent "path blocked" memory about Place puts this NPC off going there (0 = not at all).
    float BlockedPenalty(const FLisNpc& Npc, ELisPlace Place) const;
    // The NPC remembers recent damage to a village structure (builders inspect it).
    bool RemembersDamage(const FLisNpc& Npc, int64 WithinHours = 24) const;

private:
    void TickHour();
    // Appends the objective record to the log and returns a copy (the log may reallocate).
    FLisEventRecord LogEvent(ELisEvent Type, ELisPlace Place, int32 Actor, int32 Target, float Amount, FIntVector Cell);
    void Complete(FLisNpc& Npc);
    void StartIntent(FLisNpc& Npc);
    void Aggregate(FLisNpc& Npc);
    void Learn(FLisNpc& Npc, ELisAction Action, float Outcome);
    void Practice(FLisNpc& Npc, ELisAction Action, bool bSuccess);
    void DangerCheck(FLisNpc& Npc, float Scale);
    void Forget(FLisNpc& Npc);
    float Utility(const FLisNpc& Npc, ELisAction Action, ELisPlace Place, int32 Target) const;
    int32 FindHelper(const FLisNpc& Npc) const;
    int32 FindNeedy(const FLisNpc& Npc) const;
    int32 FindCompany(const FLisNpc& Npc, ELisPlace Place) const;
    bool IsPresent(const FLisNpc& Npc, ELisPlace Place) const;
};
