// "Vila de Elden": the first LIS experiment. 20 NPCs in the abstract world with
// scheduled perturbations (scarcity, discovery, market shock, danger, voxel
// edits). The perturbations change the world, never an NPC's choice.
#pragma once

#include "LisSimulation.h"
#include "LisVoxelAdapter.h"

// Experiment: the LIS 01 roster (7 farmers, 3 fishers, 3 woodcutters, 2 merchants, 3 guards, 2 explorers).
// LivingVillage (LIS 03, the 3D village): 6 farmers, 2 fishers, 4 woodcutters, 3 builders, 3 merchants,
// 2 guards, with Config.bLivingVillage on and a little wood in the village storage.
enum class ELisRoster : uint8 { Experiment, LivingVillage };

class AETHRALIS_API FLisElden
{
public:
    explicit FLisElden(uint32 Seed = 2026, ELisRoster Roster = ELisRoster::Experiment);

    FLisSimulation Sim;
    FLisVoxelAdapter Adapter;

    // Hours at which the perturbations fire.
    static constexpr int64 ShortageHour = 3 * 24 + 8;
    static constexpr int64 BerriesHour = 5 * 24;
    static constexpr int64 MarketShockHour = 7 * 24;
    static constexpr int64 WolvesHour = 9 * 24;
    static constexpr int64 LoggingHour = 11 * 24 + 10;

    // Advances the experiment; applies perturbations when their hour comes.
    void Run(int32 Hours);
    // Snapshot of the top options of every NPC right now, for the report.
    FString DescribeChoices(int32 TopN = 3) const;
    FString Report() const;

    // Option scores right after the food shortage hit, and what each NPC actually
    // started during the following CrisisHours (flattened NPC x action counts).
    static constexpr int64 CrisisHours = 36;
    FString ShortageChoices;
    TArray<int32> CrisisStartCounts;
    TArray<int32> CrisisEndCounts;
    // Which action each NPC started during the crisis window.
    TArray<int32> CrisisActions(int32 NpcIndex) const;

private:
    void Perturb();
};
